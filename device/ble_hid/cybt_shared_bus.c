/*
 * cybt_shared_bus.c -- CYW43 BT bus interface (cyw43_btbus_init/read/write)
 *
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * CYW43439 の BT HCI 通信をバックプレーン共有メモリ経由で実装する。
 *
 * cyw43_btbus_init: BT FW ダウンロード + 共有バッファ初期化
 * cyw43_btbus_read: BT → Host の HCI パケット受信
 * cyw43_btbus_write: Host → BT の HCI パケット送信
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "cyw43_btbus.h"
#include "cyw43_ll.h"
#include "cyw43_configport.h"
#include "cybt_shared_bus_driver.h"
#include "cybt_logging.h"

/* BT ファームウェア (バイナリ形式) */
#include "cyw43_btfw_43439.h"

/*----------------------------------------------------------------------
 * 定数
 */
#define BTSDIO_FW_READY_POLLING_INTERVAL_MS (1)
#define BTSDIO_BT_AWAKE_POLLING_INTERVAL_MS (1)
#define BTSDIO_FW_READY_POLLING_RETRY_COUNT (300)
#define BTSDIO_FW_AWAKE_POLLING_RETRY_COUNT (300)
#define BTFW_WAIT_TIME_MS                   (150)

#define ROUNDUP(x, a)      ((((x) + ((a) - 1)) / (a)) * (a))
#define ROUNDDN(x, a)      ((x) & ~((a) - 1))
#define ISALIGNED(a, x)    (((uint32_t)(a) & ((x) - 1)) == 0)

#define CIRC_BUF_CNT(in, out)   (((in) - (out)) & ((BTSDIO_FWBUF_SIZE) - 1))
#define CIRC_BUF_SPACE(in, out) CIRC_BUF_CNT((out), ((in) + 4))

/*----------------------------------------------------------------------
 * FW ダウンロード準備・完了
 */
static cybt_result_t cybt_fw_download_prepare(uint8_t **p_write_buf,
                                              uint8_t **p_hex_buf)
{
    *p_write_buf = Kmalloc(BTFW_DOWNLOAD_BLK_SIZE + BTFW_SD_ALIGN);
    if (*p_write_buf == NULL) return CYBT_ERR_OUT_OF_MEMORY;

    *p_hex_buf = Kmalloc(BTFW_MAX_STR_LEN);
    if (*p_hex_buf == NULL) {
        Kfree(*p_write_buf);
        *p_write_buf = NULL;
        return CYBT_ERR_OUT_OF_MEMORY;
    }
    return CYBT_SUCCESS;
}

static void cybt_fw_download_finish(uint8_t *p_write_buf, uint8_t *p_hex_buf)
{
    if (p_write_buf) Kfree(p_write_buf);
    if (p_hex_buf)   Kfree(p_hex_buf);
}

/*----------------------------------------------------------------------
 * FW Ready / Awake 待ち
 */
static cybt_result_t cybt_wait_bt_ready(uint32_t max_polling_times)
{
    cyw43_delay_ms(BTFW_WAIT_TIME_MS);
    do {
        if (cybt_ready()) return CYBT_SUCCESS;
        cyw43_delay_ms(BTSDIO_FW_READY_POLLING_INTERVAL_MS);
    } while (max_polling_times--);
    return CYBT_ERR_TIMEOUT;
}

static cybt_result_t cybt_wait_bt_awake(uint32_t max_polling_times)
{
    do {
        if (cybt_awake()) return CYBT_SUCCESS;
        cyw43_delay_ms(BTSDIO_BT_AWAKE_POLLING_INTERVAL_MS);
    } while (max_polling_times--);
    return CYBT_ERR_TIMEOUT;
}

/*----------------------------------------------------------------------
 * cyw43_btbus_init — BT FW ダウンロード + 共有バッファ初期化
 *
 * cyw43_ctrl.c の cyw43_ensure_bt_up() → cyw43_btbus_init() から呼ばれる。
 */
int cyw43_btbus_init(cyw43_ll_t *self)
{
    cybt_result_t ret;
    uint8_t *p_write_buf = NULL;
    uint8_t *p_hex_buf = NULL;

    cybt_sharedbus_driver_init(self);

    ret = cybt_fw_download_prepare(&p_write_buf, &p_hex_buf);
    if (CYBT_SUCCESS != ret) {
        cybt_error("btbus_init: memory alloc failed\n");
        return (int)ret;
    }

    cybt_info("BT FW downloading...\n");
    ret = cybt_fw_download(cyw43_btfw_43439, cyw43_btfw_43439_len,
                           p_write_buf, p_hex_buf);
    cybt_fw_download_finish(p_write_buf, p_hex_buf);

    if (CYBT_SUCCESS != ret) {
        cybt_error("btbus_init: FW download failed (0x%x)\n", ret);
        return (int)CYBT_ERR_HCI_INIT_FAILED;
    }

    ret = cybt_wait_bt_ready(BTSDIO_FW_READY_POLLING_RETRY_COUNT);
    if (CYBT_SUCCESS != ret) {
        cybt_error("btbus_init: FW not ready (timeout)\n");
        return (int)CYBT_ERR_HCI_INIT_FAILED;
    }
    cybt_info("BT FW download OK\n");

    ret = cybt_init_buffer();
    if (CYBT_SUCCESS != ret) {
        cybt_error("btbus_init: buffer init failed\n");
        return (int)ret;
    }

    ret = cybt_wait_bt_awake(BTSDIO_FW_AWAKE_POLLING_RETRY_COUNT);
    if (CYBT_SUCCESS != ret) {
        cybt_error("btbus_init: BT not awake (timeout)\n");
        return (int)ret;
    }

    cybt_set_host_ready();
    cybt_toggle_bt_intr();

    cybt_info("BT bus initialized\n");
    return CYBT_SUCCESS;
}

/*----------------------------------------------------------------------
 * 循環バッファへの HCI パケット書込み (Host → BT)
 */
static cybt_result_t cybt_hci_write_buf(const uint8_t *p_data,
                                        uint32_t length)
{
    cybt_fw_membuf_index_t fw_membuf_info = {0};

    if (!ISALIGNED(p_data, 4)) {
        cybt_error("cybt_hci_write_buf: buffer not aligned\n");
        return CYBT_ERR_BADARG;
    }

    length = ROUNDUP(length, 4);
    if (cybt_get_bt_buf_index(&fw_membuf_info) != CYBT_SUCCESS) {
        return CYBT_ERR_GENERIC;
    }

    uint32_t buf_space = CIRC_BUF_SPACE(fw_membuf_info.host2bt_in_val,
                                        fw_membuf_info.host2bt_out_val);
    if (length > buf_space) {
        cybt_error("cybt_hci_write_buf: queue full\n");
        return CYBT_ERR_QUEUE_FULL;
    }

    if (fw_membuf_info.host2bt_in_val + length <= BTSDIO_FWBUF_SIZE) {
        /* ラップ不要 */
        cybt_mem_write_idx(H2B_BUF_ADDR_IDX,
                           fw_membuf_info.host2bt_in_val, p_data, length);
        fw_membuf_info.host2bt_in_val += length;
    } else {
        /* ラップアラウンド */
        uint32_t first_len = BTSDIO_FWBUF_SIZE - fw_membuf_info.host2bt_in_val;
        if (first_len >= 4) {
            cybt_mem_write_idx(H2B_BUF_ADDR_IDX,
                               fw_membuf_info.host2bt_in_val, p_data, first_len);
            fw_membuf_info.host2bt_in_val += first_len;
        } else {
            first_len = 0;
        }
        uint32_t second_len = length - first_len;
        if (second_len > 0) {
            cybt_mem_write_idx(H2B_BUF_ADDR_IDX,
                               0, p_data + first_len, second_len);
            fw_membuf_info.host2bt_in_val += second_len;
        }
    }

    uint32_t new_h2b_in_val = fw_membuf_info.host2bt_in_val
                              & (BTSDIO_FWBUF_SIZE - 1);
    cybt_reg_write_idx(H2B_BUF_IN_ADDR_IDX, new_h2b_in_val);
    cybt_toggle_bt_intr();
    return CYBT_SUCCESS;
}

/*----------------------------------------------------------------------
 * 循環バッファからの HCI パケット読出し (BT → Host)
 */
static cybt_result_t cybt_hci_read(uint8_t *p_data, uint32_t *p_length)
{
    cybt_fw_membuf_index_t fw_membuf_info = {0};
    static uint32_t available = 0;

    if (!ISALIGNED(p_data, 4)) {
        cybt_error("cybt_hci_read: buffer not aligned\n");
        return CYBT_ERR_BADARG;
    }

    uint32_t read_len = ROUNDUP(*p_length, 4);

    if (cybt_get_bt_buf_index(&fw_membuf_info) != CYBT_SUCCESS) {
        *p_length = 0;
        return CYBT_ERR_GENERIC;
    }
    uint32_t fw_b2h_buf_count = CIRC_BUF_CNT(fw_membuf_info.bt2host_in_val,
                                              fw_membuf_info.bt2host_out_val);

    if (fw_b2h_buf_count < available) {
        cybt_error("cybt_hci_read: buffer overflow\n");
        available = 0;
    }

    if (fw_b2h_buf_count == 0) {
        *p_length = 0;
    } else {
        if (read_len > fw_b2h_buf_count) {
            read_len = fw_b2h_buf_count;
        }

        if (fw_membuf_info.bt2host_out_val + read_len <= BTSDIO_FWBUF_SIZE) {
            /* ラップ不要 */
            cybt_mem_read_idx(B2H_BUF_ADDR_IDX,
                              fw_membuf_info.bt2host_out_val, p_data, read_len);
            fw_membuf_info.bt2host_out_val += read_len;
        } else {
            /* ラップアラウンド */
            uint32_t first_len = BTSDIO_FWBUF_SIZE - fw_membuf_info.bt2host_out_val;
            if (first_len >= 4) {
                cybt_mem_read_idx(B2H_BUF_ADDR_IDX,
                                  fw_membuf_info.bt2host_out_val, p_data, first_len);
                fw_membuf_info.bt2host_out_val += first_len;
            } else {
                first_len = 0;
            }
            uint32_t second_len = read_len - first_len;
            if (second_len > 0) {
                cybt_mem_read_idx(B2H_BUF_ADDR_IDX,
                                  0, p_data + first_len, second_len);
                fw_membuf_info.bt2host_out_val += second_len;
            }
        }

        available = fw_b2h_buf_count - read_len;

        uint32_t new_b2h_out_val = fw_membuf_info.bt2host_out_val
                                   & (BTSDIO_FWBUF_SIZE - 1);
        cybt_reg_write_idx(B2H_BUF_OUT_ADDR_IDX, new_b2h_out_val);
        *p_length = read_len;
    }

    cybt_toggle_bt_intr();
    return CYBT_SUCCESS;
}

/*----------------------------------------------------------------------
 * BT ウェイクアップ + ロック
 */
static void cybt_bus_request(void)
{
    /* ロック保持パターン。
     * ロック解放パターンだと backplane window cache が stale になり、
     * cybt_get_bt_buf_index が永続的に失敗して BT バスが回復しない。
     *
     * btstack_tlv_set_instance() は未呼出のため、BLE タスクの
     * run loop 内で inline Flash write が発生せず、デッドロックしない。
     */
    CYW43_THREAD_ENTER;
    cybt_set_bt_awake(1);
    cybt_wait_bt_awake(BTSDIO_FW_AWAKE_POLLING_RETRY_COUNT);
}

static void cybt_bus_release(void)
{
    CYW43_THREAD_EXIT;
}

/*----------------------------------------------------------------------
 * cyw43_btbus_write — HCI パケット送信 (Host → BT)
 *
 * buf は 4 バイトヘッダ + データ。
 * ヘッダ: [cmd_len_lo, cmd_len_hi, 0, packet_type]
 * packet_type (buf[3]) は呼び出し元 (hci_transport_cyw43.c) が設定済み。
 */
int cyw43_btbus_write(uint8_t *buf, uint32_t size)
{
    uint16_t cmd_len = size - 4;

    /* ヘッダ書込み (buf[3] はパケットタイプで設定済み) */
    buf[0] = (uint8_t)(cmd_len & 0xFF);
    buf[1] = (uint8_t)((cmd_len >> 8) & 0xFF);
    buf[2] = 0;

    cybt_bus_request();
    cybt_hci_write_buf(buf, size);
    cybt_bus_release();

    return 0;
}

/*----------------------------------------------------------------------
 * HCI パケット 1 個分読出し
 */
static int cybt_hci_read_packet(uint8_t *buf, uint32_t max_buf_size,
                                uint32_t *size)
{
    uint32_t read_len = 4;  /* 3 bytes 長さ + 1 byte パケットタイプ */
    cybt_result_t bt_result;

    bt_result = cybt_hci_read(buf, &read_len);
    if (bt_result != CYBT_SUCCESS) {
        *size = 0;
        return -1;
    }
    if (read_len == 0) {
        *size = 0;
        return 0;
    }

    uint32_t hci_read_len = ((buf[2] << 16) & 0xFFFF00) |
                            ((buf[1] << 8) & 0xFF00)    |
                            (buf[0] & 0xFF);
    if (hci_read_len > max_buf_size - 4) {
        *size = 0;
        cybt_error("cybt_hci_read_packet: data too large %lu\n", hci_read_len);
        return -1;
    }

    uint32_t total_read_len = hci_read_len;
    bt_result = cybt_hci_read(buf + 4, &total_read_len);
    if (bt_result != CYBT_SUCCESS) {
        *size = 0;
        return -1;
    }

    if (total_read_len >= hci_read_len) {
        *size = hci_read_len + 4;
    } else {
        *size = total_read_len + 4;
    }

    return 0;
}

/*----------------------------------------------------------------------
 * cyw43_btbus_read — HCI パケット受信 (BT → Host)
 *
 * 4 バイトヘッダ付きで返す。buf[3] がパケットタイプ。
 */
int cyw43_btbus_read(uint8_t *buf, uint32_t max_buf_size, uint32_t *size)
{
    cybt_bus_request();
    int result = cybt_hci_read_packet(buf, max_buf_size, size);
    cybt_bus_release();
    return result;
}

#endif /* CPU_RP2040 */
