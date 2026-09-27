/*
 * ble_split_service.c — BLE Split Keyboard Matrix Service (Slave side)
 *
 * Slave 半分のマトリクスデータを BLE Notification で Master に送信する。
 * Master は BLE Central としてこのサービスに接続し、Notification を subscribe する。
 *
 * データ形式: [checksum:1][smatrix:MATRIX_ROWS_PER_HAND]
 * I2C split transport の shared memory と同一フォーマット。
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

#include "ble_split_service.h"

/* Slave 用 GATT DB (compile_gatt.py で生成) */
#include "ble_split_keyboard.h"

/* CRC8 (split_transport.c で定義) */
extern UB crc8(const void *data, UW len);

/*----------------------------------------------------------------------
 * 状態管理
 */
LOCAL hci_con_handle_t split_con_handle = HCI_CON_HANDLE_INVALID;
LOCAL uint16_t         matrix_char_handle = 0;  /* GATT DB から取得 */
LOCAL BOOL             notifications_enabled = FALSE;

/* マトリクスバッファ: [checksum:1][rows:MATRIX_ROWS_PER_HAND] */
#define MATRIX_BUF_SIZE  (1 + MATRIX_ROWS_PER_HAND)
LOCAL uint8_t matrix_buf[MATRIX_BUF_SIZE];

/*----------------------------------------------------------------------
 * アドバタイジングデータ (Slave 用: Matrix Service UUID を含む)
 */
/*
 * Slave アドバタイジングデータ
 * Matrix Service の 128-bit UUID を含めることで、
 * Master が UUID ベースで slave を発見できる。
 */
LOCAL const uint8_t split_adv_data[] = {
    /* Flags: LE General Discoverable, BR/EDR not supported */
    0x02, 0x01, 0x06,
    /* Complete Local Name: "TK Split" */
    0x09, 0x09, 'T', 'K', ' ', 'S', 'p', 'l', 'i', 't',
    /* Complete List of 128-bit Service UUIDs (big-endian) */
    0x11, 0x07,  /* length=17, type=0x07 */
    0x4b, 0x4b, 0x53, 0x48, 0x5f, 0x53, 0x50, 0x4c,
    0x49, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
};

/*----------------------------------------------------------------------
 * イベントハンドラ登録
 */
LOCAL btstack_packet_callback_registration_t split_hci_cb;

LOCAL void split_packet_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);

    switch (event_type) {
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        split_con_handle = HCI_CON_HANDLE_INVALID;
        notifications_enabled = FALSE;
        tm_printf((UB *)"BLE Split: disconnected\n");
        /* 再アドバタイズ */
        gap_advertisements_enable(1);
        break;

    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet)) {
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
            split_con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
            tm_printf((UB *)"BLE Split: master connected (handle=%d)\n",
                      split_con_handle);
            break;
        }
        break;

    case ATT_EVENT_CAN_SEND_NOW:
        /* Notification 送信可能 — 現在のマトリクスを送信 */
        if (split_con_handle != HCI_CON_HANDLE_INVALID &&
            notifications_enabled && matrix_char_handle != 0) {
            att_server_notify(split_con_handle, matrix_char_handle,
                              matrix_buf, MATRIX_BUF_SIZE);
        }
        break;

    default:
        break;
    }
}

/*----------------------------------------------------------------------
 * ATT コールバック — CCC (Client Characteristic Configuration) 変更通知
 *
 * H5 修正: CCC handle と値を正しくチェックする。
 * BTstack は CCC 書き込みを自動処理するが、アプリにも通知する。
 */
LOCAL int split_att_write_callback(hci_con_handle_t con_handle,
                                    uint16_t attribute_handle,
                                    uint16_t transaction_mode,
                                    uint16_t offset,
                                    uint8_t *buffer, uint16_t buffer_size)
{
    (void)con_handle; (void)transaction_mode; (void)offset;

    /* Matrix Characteristic の CCC handle (value handle + 1) */
    uint16_t matrix_ccc = matrix_char_handle + 1;

    if (attribute_handle == matrix_ccc && buffer_size >= 2) {
        uint16_t ccc_value = buffer[0] | ((uint16_t)buffer[1] << 8);
        notifications_enabled = (ccc_value & 0x0001) != 0;
        tm_printf((UB *)"BLE Split: notifications %s\n",
                  notifications_enabled ? "enabled" : "disabled");
    }
    return 0;
}

/*----------------------------------------------------------------------
 * Slave: Matrix Service 初期化
 */
EXPORT ER ble_split_service_init(void)
{
    /* HCI 初期化 */
    {
        extern const hci_transport_t *hci_transport_cyw43_instance(void);
        hci_init(hci_transport_cyw43_instance(), NULL);
    }

    /* L2CAP */
    l2cap_init();

    /* Security Manager (Slave 間通信は簡易: Just Works) */
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING);

    /* ATT サーバ (Slave 用 GATT DB: Matrix Service のみ) */
    att_server_init(profile_data, NULL, split_att_write_callback);

    /*
     * Matrix Characteristic のハンドル値を取得
     * GATT DB 内のカスタム UUID に対応するハンドルを検索する。
     * compile_gatt.py で生成された DB の定数を使う。
     */
    /* GATT DB (ble_split_keyboard.h) から生成されたハンドル定数 */
    matrix_char_handle = ATT_CHARACTERISTIC_4b4b5348_5f53_504c_4954_000000000002_01_VALUE_HANDLE;  /* 0x0008 */

    /* アドバタイジング設定 */
    {
        uint16_t adv_int_min = 0x0030;  /* 30ms — Master が素早く発見できるように */
        uint16_t adv_int_max = 0x0060;  /* 60ms */
        bd_addr_t null_addr;
        memset(null_addr, 0, 6);
        gap_advertisements_set_params(adv_int_min, adv_int_max, 0, 0,
                                      null_addr, 0x07, 0x00);
        gap_advertisements_set_data(sizeof(split_adv_data), (uint8_t *)split_adv_data);
        gap_advertisements_enable(1);
    }

    /* イベントハンドラ */
    split_hci_cb.callback = &split_packet_handler;
    hci_add_event_handler(&split_hci_cb);

    /* HCI パワーオン */
    {
        int ret = hci_power_control(HCI_POWER_ON);
        if (ret != 0) {
            tm_printf((UB *)"BLE Split: HCI power on failed: %d\n", ret);
            return E_IO;
        }
    }

    /* マトリクスバッファ初期化 */
    memset(matrix_buf, 0, sizeof(matrix_buf));

    tm_printf((UB *)"BLE Split: slave service initialized\n");
    return E_OK;
}

/*----------------------------------------------------------------------
 * Slave: マトリクスデータ更新 → Master に Notification 送信
 *
 * split_transport.c の transport_slave_update_matrix() から呼ばれる。
 */
EXPORT void ble_split_service_update_matrix(const matrix_row_t *matrix)
{
    /* CRC8 + マトリクスデータをバッファにコピー */
    matrix_buf[0] = crc8(matrix, MATRIX_ROWS_PER_HAND);
    memcpy(&matrix_buf[1], matrix, MATRIX_ROWS_PER_HAND);

    /* Notification 送信リクエスト */
    if (split_con_handle != HCI_CON_HANDLE_INVALID &&
        notifications_enabled && matrix_char_handle != 0) {
        att_server_request_can_send_now_event(split_con_handle);
    }
}

#endif /* CPU_RP2040 */
