/*
 * hci_transport_cyw43.c — BTstack HCI transport for CYW43439
 *
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * CYW43 ドライバの BT HCI API を BTstack の hci_transport_t に適合させる。
 * CYW43 は SPI バス経由で BT HCI パケットを送受信する。
 *
 * 同期トランスポート (can_send_packet_now = NULL):
 *   BTstack が send_packet() 返却後にバッファを即座に解放する。
 *   μT-Kernel の FastLock は非再入のため、非同期モードの
 *   HCI_EVENT_TRANSPORT_PACKET_SENT コールバック方式は使えない。
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "btstack_config.h"
#include "btstack_run_loop.h"
#include "btstack_chipset.h"
#include "bluetooth.h"
#include "hci.h"
#include "hci_transport.h"
#include "cyw43.h"

#include "hci_transport_cyw43.h"

/* BTstack パケットハンドラコールバック */
LOCAL void (*transport_packet_handler)(uint8_t packet_type,
                                       uint8_t *packet, uint16_t size);

/* 受信バッファ (cybt_shared_bus が 4 バイトアラインを要求) */
#define HCI_RX_BUF_SIZE 264
LOCAL uint8_t hci_rx_buf[HCI_RX_BUF_SIZE] __attribute__((aligned(4)));

/* BTstack data source (ポーリング用) */
LOCAL btstack_data_source_t transport_data_source;

/*----------------------------------------------------------------------
 * ポーリングコールバック — BLE タスクの run loop から定期的に呼ばれる
 */
LOCAL void transport_poll_handler(btstack_data_source_t *ds,
                                  btstack_data_source_callback_type_t type)
{
    (void)ds; (void)type;

    /*
     * 複数パケット読出し
     *
     * 1ポーリングで1パケットだと ATT 応答が遅延し、
     * ホストが切断する原因になる。最大8パケット/回で処理。
     */
    int count;
    for (count = 0; count < 8; count++) {
        uint32_t len = 0;
        int ret = cyw43_bluetooth_hci_read(hci_rx_buf, sizeof(hci_rx_buf), &len);
        if (ret != 0) {
            static int hci_err_count = 0;
            if (++hci_err_count <= 5) {
                tm_printf((UB *)"BLE: HCI read err %d\n", ret);
            }
            break;
        }
        if (len < 4) break;

        uint8_t pkt_type = hci_rx_buf[3];

        if (transport_packet_handler != NULL) {
            transport_packet_handler(pkt_type, &hci_rx_buf[4], (uint16_t)(len - 4));
        }
    }
}

/*----------------------------------------------------------------------
 * CYW43 チップセットドライバ (BD アドレス設定用)
 */
LOCAL void chipset_set_bd_addr_command(bd_addr_t addr, uint8_t *hci_cmd_buffer)
{
    hci_cmd_buffer[0] = 0x01;   /* opcode low: OGF=0x3F, OCF=0x0001 → 0xFC01 */
    hci_cmd_buffer[1] = 0xFC;   /* opcode high */
    hci_cmd_buffer[2] = 0x06;   /* parameter length = 6 */
    reverse_bd_addr(addr, &hci_cmd_buffer[3]);
}

LOCAL const btstack_chipset_t btstack_chipset_cyw43 = {
    .name                = "CYW43",
    .init                = NULL,
    .next_command        = NULL,
    .set_baudrate_command = NULL,
    .set_bd_addr_command = chipset_set_bd_addr_command,
};

/*----------------------------------------------------------------------
 * hci_transport_t 実装
 */
LOCAL void transport_init(const void *config)
{
    (void)config;
    /*
     * transport_packet_handler をリセットしてはいけない。
     *
     * BTstack の初期化順序:
     *   1. hci_init() → register_packet_handler(&packet_handler) で登録
     *   2. hci_power_control_on() → transport->init() が呼ばれる (ここ)
     *
     * init() で transport_packet_handler = NULL にすると、
     * 手順1で登録されたハンドラが消える → CC が BTstack に届かない
     * → HCI init が進まない → HCI_RESET 無限ループ。
     */
}

LOCAL int transport_open(void)
{
    int ret = cyw43_bluetooth_hci_init();
    if (ret != 0) {
        tm_printf((UB *)"BLE: HCI transport open failed: %d\n", ret);
        return -1;
    }

    /*
     * BT アドレス設定 — WiFi MAC + 1
     * CYW43439 の OTP に MAC が未設定の場合、BT FW のデフォルトアドレス
     * (43:43:A2:12:1F:AC) は不適切なので明示的に設定する。
     */
    {
        bd_addr_t addr;
        cyw43_hal_get_mac(0, (uint8_t *)&addr);
        addr[BD_ADDR_LEN - 1]++;
        hci_set_chipset(&btstack_chipset_cyw43);
        hci_set_bd_addr(addr);
        tm_printf((UB *)"BLE: BD addr %02x:%02x:%02x:%02x:%02x:%02x\n",
                  addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    }

    /* ポーリング data source を run loop に登録 */
    btstack_run_loop_set_data_source_handler(&transport_data_source,
                                              transport_poll_handler);
    btstack_run_loop_enable_data_source_callbacks(&transport_data_source,
                                                   DATA_SOURCE_CALLBACK_POLL);
    btstack_run_loop_add_data_source(&transport_data_source);

    return 0;
}

LOCAL int transport_close(void)
{
    btstack_run_loop_remove_data_source(&transport_data_source);
    return 0;
}

LOCAL void transport_register_handler(void (*handler)(uint8_t, uint8_t *, uint16_t))
{
    transport_packet_handler = handler;
}

LOCAL int transport_send(uint8_t packet_type, uint8_t *packet, int size)
{
    /*
     * CYW43 の HCI write は 4 byte ヘッダが必要:
     *   [0][1][2] = cyw43_btbus_write が上書き, [3] = パケットタイプ
     */
    packet[-4] = 0;
    packet[-3] = 0;
    packet[-2] = 0;
    packet[-1] = packet_type;

    int ret = cyw43_bluetooth_hci_write(packet - 4, size + 4);
    if (ret != 0) {
        tm_printf((UB *)"BLE: HCI write failed: %d\n", ret);
    }
    return (ret == 0) ? 0 : -1;
}

/*----------------------------------------------------------------------
 * シングルトンインスタンス — 同期トランスポート
 */
LOCAL const hci_transport_t hci_transport_cyw43 = {
    .name                    = "CYW43",
    .init                    = transport_init,
    .open                    = transport_open,
    .close                   = transport_close,
    .register_packet_handler = transport_register_handler,
    .can_send_packet_now     = NULL,   /* 同期トランスポート */
    .send_packet             = transport_send,
    .set_baudrate            = NULL,
    .reset_link              = NULL,
    .set_sco_config          = NULL,
};

EXPORT const hci_transport_t *hci_transport_cyw43_instance(void)
{
    return &hci_transport_cyw43;
}

#endif /* CPU_RP2040 */
