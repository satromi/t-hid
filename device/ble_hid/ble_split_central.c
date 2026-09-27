/*
 * ble_split_central.c — BLE Split Keyboard Central (Master side)
 *
 * Master が BLE Central として:
 * 1. BLE scan で Matrix Service を持つ slave を発見
 * 2. 接続
 * 3. GATT Client で Matrix Characteristic を discover
 * 4. Notification を subscribe
 * 5. Notification 受信時にマトリクスデータを更新
 *
 * BTstack はスレッドセーフではない。全 API 呼び出しは BLE タスク
 * (btstack_run_loop) コンテキストから行われる。
 * ble_split_central_init() は BLE タスク起動前に呼ぶこと。
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
#include "btstack.h"

#include "ble_split_central.h"
#include "ble_split_service.h"

/*----------------------------------------------------------------------
 * 状態管理
 */
typedef enum {
    SPLIT_STATE_IDLE,
    SPLIT_STATE_SCANNING,
    SPLIT_STATE_CONNECTING,
    SPLIT_STATE_DISCOVERING,
    SPLIT_STATE_SUBSCRIBING,
    SPLIT_STATE_CONNECTED,
} split_central_state_t;

LOCAL split_central_state_t split_state = SPLIT_STATE_IDLE;
LOCAL hci_con_handle_t  slave_con_handle = HCI_CON_HANDLE_INVALID;
LOCAL gatt_client_notification_t notification_listener;

/* Discovered characteristic (C1 修正: struct を保存) */
LOCAL gatt_client_characteristic_t discovered_matrix_char;
LOCAL BOOL matrix_char_found = FALSE;

/* Slave の BD_ADDR (scan で発見) */
LOCAL bd_addr_t slave_addr;
LOCAL bd_addr_type_t slave_addr_type;

/* 受信マトリクスバッファ (BLE タスク → scanner タスク 共有) */
LOCAL matrix_row_t received_matrix[MATRIX_ROWS_PER_HAND];
LOCAL UB received_checksum = 0;
LOCAL BOOL matrix_received = FALSE;

/*
 * Matrix Service UUID (128-bit, big-endian / network order)
 * BTstack の GATT client API は big-endian で UUID を受け取る。
 *
 * UUID string: "4b4b5348-5f53-504c-4954-000000000001"
 * Big-endian:   4b 4b 53 48  5f 53 50 4c  49 54 00 00  00 00 00 01
 */
static const uint8_t matrix_service_uuid128[] = {
    0x4b, 0x4b, 0x53, 0x48, 0x5f, 0x53, 0x50, 0x4c,
    0x49, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};
static const uint8_t matrix_char_uuid128[] = {
    0x4b, 0x4b, 0x53, 0x48, 0x5f, 0x53, 0x50, 0x4c,
    0x49, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
};

/*----------------------------------------------------------------------
 * 名前ベースの slave 検索 (memmem の代替)
 * arm-none-eabi newlib-nano には memmem がないため手動実装
 */
LOCAL BOOL adv_contains_name(const uint8_t *data, uint8_t len,
                              const char *name, uint8_t name_len)
{
    uint8_t pos = 0;
    while (pos < len) {
        uint8_t field_len = data[pos];
        if (field_len == 0 || pos + 1 + field_len > len) break;
        uint8_t field_type = data[pos + 1];
        /* Complete Local Name (0x09) or Shortened Local Name (0x08) */
        if ((field_type == 0x09 || field_type == 0x08) &&
            field_len - 1 >= name_len &&
            memcmp(&data[pos + 2], name, name_len) == 0) {
            return TRUE;
        }
        pos += 1 + field_len;
    }
    return FALSE;
}

/*----------------------------------------------------------------------
 * GATT Client コールバック (discover + subscribe)
 */
LOCAL void gatt_client_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);

    switch (event_type) {
    case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {
        /* Characteristic discover 結果 — struct を保存 (C1 修正) */
        gatt_event_characteristic_query_result_get_characteristic(
            packet, &discovered_matrix_char);
        matrix_char_found = TRUE;
        tm_printf((UB *)"BLE Split: matrix char handle=0x%04x\n",
                  discovered_matrix_char.value_handle);
        break;
    }

    case GATT_EVENT_QUERY_COMPLETE:
        if (split_state == SPLIT_STATE_DISCOVERING) {
            if (matrix_char_found) {
                /* Notification subscribe (C1 修正: struct ポインタを渡す) */
                split_state = SPLIT_STATE_SUBSCRIBING;
                gatt_client_listen_for_characteristic_value_updates(
                    &notification_listener,
                    gatt_client_handler,
                    slave_con_handle,
                    &discovered_matrix_char);
                gatt_client_write_client_characteristic_configuration(
                    gatt_client_handler,
                    slave_con_handle,
                    &discovered_matrix_char,
                    GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
            } else {
                tm_printf((UB *)"BLE Split: matrix char not found\n");
                gap_disconnect(slave_con_handle);
            }
        } else if (split_state == SPLIT_STATE_SUBSCRIBING) {
            split_state = SPLIT_STATE_CONNECTED;
            tm_printf((UB *)"BLE Split: subscribed, ready\n");
        }
        break;

    case GATT_EVENT_NOTIFICATION: {
        /* Notification 受信 — マトリクスデータ */
        uint16_t value_length = gatt_event_notification_get_value_length(packet);
        const uint8_t *value = gatt_event_notification_get_value(packet);

        if (value_length >= 1 + MATRIX_ROWS_PER_HAND) {
            UINT imask;
            DI(imask);
            received_checksum = value[0];
            memcpy(received_matrix, &value[1], MATRIX_ROWS_PER_HAND);
            matrix_received = TRUE;
            EI(imask);
        }
        break;
    }

    default:
        break;
    }
}

/*----------------------------------------------------------------------
 * HCI イベントハンドラ (scan 結果、接続完了等)
 *
 * C4 対応: connection handle で Master-Slave 接続を識別。
 * HID イベントハンドラ (ble_hid_gatt.c) も同じイベントを受け取るため、
 * LE_CONNECTION_COMPLETE では role フィールドで Central/Peripheral を区別する。
 */
LOCAL btstack_packet_callback_registration_t central_hci_cb;

LOCAL void central_hci_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);

    switch (event_type) {
    case GAP_EVENT_ADVERTISING_REPORT: {
        if (split_state != SPLIT_STATE_SCANNING) break;

        uint8_t length = gap_event_advertising_report_get_data_length(packet);
        const uint8_t *data = gap_event_advertising_report_get_data(packet);

        /* UUID またはデバイス名で slave を検索 */
        BOOL found = ad_data_contains_uuid128(length, data, matrix_service_uuid128);
        if (!found) {
            found = adv_contains_name(data, length, "TK Split", 8);
        }

        if (found) {
            gap_event_advertising_report_get_address(packet, slave_addr);
            slave_addr_type = gap_event_advertising_report_get_address_type(packet);

            gap_stop_scan();
            split_state = SPLIT_STATE_CONNECTING;
            tm_printf((UB *)"BLE Split: slave found, connecting...\n");
            gap_connect(slave_addr, slave_addr_type);
        }
        break;
    }

    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet)) {
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE: {
            if (split_state != SPLIT_STATE_CONNECTING) break;

            uint8_t status = hci_subevent_le_connection_complete_get_status(packet);
            if (status != 0) {
                tm_printf((UB *)"BLE Split: connect failed: %d\n", status);
                split_state = SPLIT_STATE_SCANNING;
                gap_start_scan();
                break;
            }

            /*
             * C4 対応: role フィールドで Central/Peripheral を区別。
             * role=0 = Master (Central), role=1 = Slave (Peripheral)
             * Central からの接続完了のみここで処理する。
             */
            uint8_t role = hci_subevent_le_connection_complete_get_role(packet);
            if (role != 0) break;  /* Peripheral 接続は HID ハンドラに任せる */

            slave_con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
            split_state = SPLIT_STATE_DISCOVERING;
            matrix_char_found = FALSE;
            tm_printf((UB *)"BLE Split: connected (handle=%d), discovering...\n",
                      slave_con_handle);

            /* Matrix Characteristic を discover (C2 修正: big-endian UUID) */
            gatt_client_discover_characteristics_for_handle_range_by_uuid128(
                gatt_client_handler,
                slave_con_handle,
                0x0001, 0xFFFF,
                matrix_char_uuid128);
            break;
        }
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        /* C4 対応: slave_con_handle と一致する場合のみ処理 */
        hci_con_handle_t handle = hci_event_disconnection_complete_get_connection_handle(packet);
        if (handle == slave_con_handle) {
            slave_con_handle = HCI_CON_HANDLE_INVALID;
            matrix_char_found = FALSE;
            matrix_received = FALSE;
            split_state = SPLIT_STATE_SCANNING;
            tm_printf((UB *)"BLE Split: slave disconnected, re-scanning\n");
            gap_start_scan();
        }
        /* host disconnection は HID ハンドラ (ble_hid_gatt.c) が処理 */
        break;
    }

    default:
        break;
    }
}

/*----------------------------------------------------------------------
 * Master: BLE Central 初期化
 *
 * BTstack タスク起動前に呼ぶこと (スレッドセーフ対応 H1)。
 */
EXPORT ER ble_split_central_init(void)
{
    memset(received_matrix, 0, sizeof(received_matrix));
    matrix_received = FALSE;
    matrix_char_found = FALSE;

    /* HCI イベントハンドラ登録 */
    central_hci_cb.callback = &central_hci_handler;
    hci_add_event_handler(&central_hci_cb);

    /* BLE scan パラメータ設定 (passive, 30ms interval, 15ms window = 50% duty) */
    gap_set_scan_parameters(0, 0x0030, 0x0018);
    gap_start_scan();
    split_state = SPLIT_STATE_SCANNING;

    tm_printf((UB *)"BLE Split: scanning for slave...\n");
    return E_OK;
}

/*----------------------------------------------------------------------
 * Master: slave マトリクスデータを取得
 */
EXPORT ER ble_split_central_read_matrix(matrix_row_t *slave_matrix)
{
    if (split_state != SPLIT_STATE_CONNECTED) return E_IO;
    if (!matrix_received) return E_NOEXS;

    extern UB crc8(const void *data, UW len);

    UINT imask;
    DI(imask);
    UB checksum = received_checksum;
    memcpy(slave_matrix, received_matrix, MATRIX_ROWS_PER_HAND);
    EI(imask);

    UB calc = crc8(slave_matrix, MATRIX_ROWS_PER_HAND);
    if (calc != checksum) return E_IO;

    return E_OK;
}

/*----------------------------------------------------------------------
 * Master: slave 接続状態
 */
EXPORT BOOL ble_split_central_is_connected(void)
{
    return (split_state == SPLIT_STATE_CONNECTED);
}

#endif /* CPU_RP2040 */
