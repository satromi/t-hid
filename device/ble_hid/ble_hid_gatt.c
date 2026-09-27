/*
 * ble_hid_gatt.c — BLE HID Keyboard GATT service
 *
 * BTstack の hog_keyboard_demo.c を μT-Kernel に移植。
 * HOGP (HID over GATT Profile) で BLE キーボードとして動作する。
 *
 * HID レポート形式は USB HID と完全同一 (8byte Boot Protocol):
 *   [0] modifier, [1] reserved, [2-7] keycode[6]
 *
 * セキュリティ:
 *   IO_CAPABILITY_KEYBOARD_ONLY + MITM 保護
 *   ペアリング時にホストがパスキーを表示し、キーボードから入力
 *
 * 省電力:
 *   アドバタイズ間隔 2 段階 (30ms×30秒 → 1.5秒)
 *   接続後は接続パラメータ更新要求 (7.5-15ms)
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
#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "ble/gatt-service/hids_device.h"
#include "ble/le_device_db_tlv.h"
#include "btstack_tlv.h"

/* GATT データベース (compile_gatt.py で生成) — profile_data[] を定義 */
#include "ble_hid_keyboard.h"

/* KB_PRODUCT, KB_MANUFACTURER マクロ */
#include "../../app_program/keyboard/kb_config.h"

#include "ble_hid_gatt.h"

/*----------------------------------------------------------------------
 * HID Report Descriptor (Boot Protocol Keyboard)
 * USB HID Specification 1.1, Appendix B.1 と同一
 */
static const uint8_t hid_descriptor_keyboard[] = {
    0x05, 0x01,     /* Usage Page (Generic Desktop) */
    0x09, 0x06,     /* Usage (Keyboard) */
    0xa1, 0x01,     /* Collection (Application) */

    0x85, 0x01,     /*   Report ID 1 */

    /* Modifier byte (8 bits) */
    0x75, 0x01,     /*   Report Size (1) */
    0x95, 0x08,     /*   Report Count (8) */
    0x05, 0x07,     /*   Usage Page (Key codes) */
    0x19, 0xe0,     /*   Usage Minimum (Keyboard LeftControl) */
    0x29, 0xe7,     /*   Usage Maximum (Keyboard Right GUI) */
    0x15, 0x00,     /*   Logical Minimum (0) */
    0x25, 0x01,     /*   Logical Maximum (1) */
    0x81, 0x02,     /*   Input (Data, Variable, Absolute) */

    /* Reserved byte */
    0x75, 0x01,     /*   Report Size (1) */
    0x95, 0x08,     /*   Report Count (8) */
    0x81, 0x03,     /*   Input (Constant, Variable, Absolute) */

    /* LED report (output) */
    0x95, 0x05,     /*   Report Count (5) */
    0x75, 0x01,     /*   Report Size (1) */
    0x05, 0x08,     /*   Usage Page (LEDs) */
    0x19, 0x01,     /*   Usage Minimum (Num Lock) */
    0x29, 0x05,     /*   Usage Maximum (Kana) */
    0x91, 0x02,     /*   Output (Data, Variable, Absolute) */
    0x95, 0x01,     /*   Report Count (1) */
    0x75, 0x03,     /*   Report Size (3) */
    0x91, 0x03,     /*   Output (Constant, Variable, Absolute) */

    /* Keycodes (6 bytes) */
    0x95, 0x06,     /*   Report Count (6) */
    0x75, 0x08,     /*   Report Size (8) */
    0x15, 0x00,     /*   Logical Minimum (0) */
    0x25, 0xff,     /*   Logical Maximum (255) */
    0x05, 0x07,     /*   Usage Page (Key codes) */
    0x19, 0x00,     /*   Usage Minimum (0) */
    0x29, 0xff,     /*   Usage Maximum (255) */
    0x81, 0x00,     /*   Input (Data, Array) */

    0xc0,           /* End collection */
};

/*----------------------------------------------------------------------
 * BLE デバイス名 (KB_PRODUCT から取得)
 */
#ifndef KB_BLE_NAME
#ifdef KB_PRODUCT
#define KB_BLE_NAME  KB_PRODUCT
#else
#define KB_BLE_NAME  "TK Keyboard"
#endif
#endif

/*
 * adv data は ble_hid_gatt_init() で動的に構築する。
 */
#define ADV_DATA_MAX    31
LOCAL uint8_t adv_data[ADV_DATA_MAX];
LOCAL uint8_t adv_data_len;

/*----------------------------------------------------------------------
 * アドバタイズ間隔 2 段階制御
 *
 * Phase 1: 高速 (30ms) — 接続を素早く確立
 * Phase 2: 低速 (1.5s) — 省電力 (ADV_SLOW_TIMEOUT 後に切替)
 */
#define ADV_FAST_INT        0x0030   /* 48 * 0.625ms = 30ms */
#define ADV_SLOW_INT        0x0960   /* 2400 * 0.625ms = 1500ms */
#define ADV_SLOW_TIMEOUT    30000    /* 30 秒後にスローアドバタイズに切替 */

LOCAL btstack_timer_source_t adv_slow_timer;
LOCAL BOOL adv_is_fast = TRUE;

LOCAL void adv_slow_timer_handler(btstack_timer_source_t *ts)
{
    (void)ts;
    if (adv_is_fast) {
        /* スローアドバタイズに切替 */
        bd_addr_t null_addr;
        memset(null_addr, 0, 6);
        gap_advertisements_set_params(ADV_SLOW_INT, ADV_SLOW_INT, 0, 0,
                                      null_addr, 0x07, 0x00);
        gap_advertisements_enable(1);
        adv_is_fast = FALSE;
        tm_printf((UB *)"BLE: slow advertising\n");
    }
}

LOCAL void adv_start_fast(void)
{
    bd_addr_t null_addr;
    memset(null_addr, 0, 6);
    gap_advertisements_set_params(ADV_FAST_INT, ADV_FAST_INT, 0, 0,
                                  null_addr, 0x07, 0x00);
    gap_advertisements_set_data(adv_data_len, adv_data);
    gap_advertisements_enable(1);
    adv_is_fast = TRUE;

    /* ADV_SLOW_TIMEOUT 後にスローアドバタイズタイマー発火 */
    btstack_run_loop_set_timer(&adv_slow_timer, ADV_SLOW_TIMEOUT);
    adv_slow_timer.process = adv_slow_timer_handler;
    btstack_run_loop_add_timer(&adv_slow_timer);
}

/*----------------------------------------------------------------------
 * 状態管理
 */
LOCAL hci_con_handle_t  ble_con_handle = HCI_CON_HANDLE_INVALID;
LOCAL uint8_t           ble_protocol_mode = 1;  /* 1=Report Mode */
LOCAL BOOL              ble_can_send = FALSE;
LOCAL uint8_t           ble_battery = 100;

/* 送信待ちレポートバッファ (Scanner タスクから書込み、BLE タスクから読出し) */
LOCAL T_HID_KBD_REPORT  pending_report;
LOCAL volatile BOOL     report_pending = FALSE;

/* ペアリングモード */
LOCAL BOOL              pairing_mode = FALSE;
LOCAL volatile BOOL     pairing_request = FALSE;  /* Scanner→BLE タスク通知 */
LOCAL volatile BOOL     pairing_cooldown = FALSE;  /* ペアリング時アドバタイズ停止中 */

/* LED インジケータ (CYW43 GPIO 0 = Pico W オンボード LED) */
LOCAL btstack_timer_source_t led_timer;
LOCAL BOOL              led_state = FALSE;

#define LED_BLINK_FAST_MS   150   /* ペアリングモード: 高速点滅 */
#define LED_BLINK_SLOW_MS   1000  /* アドバタイズ中: ゆっくり点滅 */
#define LED_BLINK_CONN_MS   0     /* 接続済み: 常時点灯 (タイマー停止) */

LOCAL void led_set(BOOL on)
{
    extern void cyw43_arch_gpio_put(int gpio, int value);
    cyw43_arch_gpio_put(0, on ? 1 : 0);
    led_state = on;
}

LOCAL void led_timer_handler(btstack_timer_source_t *ts)
{
    (void)ts;
    led_set(!led_state);

    /* 状態に応じた点滅間隔 */
    uint32_t interval;
    if (ble_con_handle != HCI_CON_HANDLE_INVALID) {
        /* 接続済み → 常時点灯、タイマー停止 */
        led_set(TRUE);
        return;
    } else if (pairing_mode) {
        interval = LED_BLINK_FAST_MS;
    } else {
        interval = LED_BLINK_SLOW_MS;
    }
    btstack_run_loop_set_timer(&led_timer, interval);
    btstack_run_loop_add_timer(&led_timer);
}

LOCAL void led_start_blink(uint32_t interval_ms)
{
    btstack_run_loop_remove_timer(&led_timer);
    if (interval_ms == 0) {
        led_set(TRUE);
        return;
    }
    led_set(TRUE);
    btstack_run_loop_set_timer(&led_timer, interval_ms);
    led_timer.process = led_timer_handler;
    btstack_run_loop_add_timer(&led_timer);
}

/* 前方宣言 */
LOCAL void enter_pairing_mode(void);
LOCAL void check_bonding_at_startup(void);

/* イベントハンドラ登録 */
LOCAL btstack_packet_callback_registration_t hci_event_cb;
LOCAL btstack_packet_callback_registration_t sm_event_cb;

/*----------------------------------------------------------------------
 * BLE イベントハンドラ
 */
LOCAL void ble_packet_handler(uint8_t packet_type, uint16_t channel,
                              uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);

    switch (event_type) {
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        hci_con_handle_t disc_handle =
            hci_event_disconnection_complete_get_connection_handle(packet);
        if (disc_handle == ble_con_handle) {
            uint8_t reason = hci_event_disconnection_complete_get_reason(packet);
            ble_con_handle = HCI_CON_HANDLE_INVALID;
            ble_can_send = FALSE;
            report_pending = FALSE;
            tm_printf((UB *)"BLE: HID disconnected (reason=0x%02x)\n", reason);
            /* LED: 切断 → 点滅再開 */
            led_start_blink(pairing_mode ? LED_BLINK_FAST_MS : LED_BLINK_SLOW_MS);
            /*
             * ペアリングクールダウン中はアドバタイズを再開しない。
             * 再開すると古いホストが即再接続して connect/disconnect ループになる。
             */
            if (!pairing_cooldown) {
                adv_start_fast();
            }
        }
        /* split slave disconnect は ble_split_central.c が処理 */
        break;
    }

    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet)) {
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE: {
            /*
             * C4 修正: role で Central/Peripheral を区別。
             * role=1 (Peripheral) = ホストからの接続 → HID 用
             * role=0 (Central) = slave への接続 → split 用 (ble_split_central.c が処理)
             */
            uint8_t role = hci_subevent_le_connection_complete_get_role(packet);
            if (role != 1) break;  /* Central 接続は split handler に任せる */

            ble_con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
            tm_printf((UB *)"BLE: HID connected (handle=%d)\n", ble_con_handle);

            /* LED: 接続済み → 常時点灯 */
            led_start_blink(LED_BLINK_CONN_MS);
            pairing_mode = FALSE;

            /* スローアドバタイズタイマー停止 */
            btstack_run_loop_remove_timer(&adv_slow_timer);

            /*
             * 接続パラメータ更新は接続直後に送ると切断の原因になりうる。
             * ペアリング完了後に送るように変更予定。
             * TODO: SM_EVENT_PAIRING_COMPLETE 後に更新要求を送る。
             */
            break;
        }
        }
        break;

    case HCI_EVENT_HIDS_META:
        switch (hci_event_hids_meta_get_subevent_code(packet)) {
        case HIDS_SUBEVENT_INPUT_REPORT_ENABLE:
            ble_can_send = TRUE;
            tm_printf((UB *)"BLE: HID report enabled\n");
            break;
        case HIDS_SUBEVENT_BOOT_KEYBOARD_INPUT_REPORT_ENABLE:
            ble_can_send = TRUE;
            break;
        case HIDS_SUBEVENT_PROTOCOL_MODE:
            ble_protocol_mode = hids_subevent_protocol_mode_get_protocol_mode(packet);
            break;
        case HIDS_SUBEVENT_CAN_SEND_NOW:
            if (report_pending) {
                uint8_t report[8];
                report[0] = pending_report.modifier;
                report[1] = pending_report.reserved;
                memcpy(&report[2], pending_report.keycode, 6);

                if (ble_protocol_mode == 0) {
                    hids_device_send_boot_keyboard_input_report(
                        ble_con_handle, report, sizeof(report));
                } else {
                    hids_device_send_input_report(
                        ble_con_handle, report, sizeof(report));
                }
                report_pending = FALSE;
            }
            break;
        }
        break;

    /* SM (Security Manager) イベント */
    case SM_EVENT_JUST_WORKS_REQUEST:
        /*
         * Just Works ペアリング確認 — 必須！
         * BTstack は SM_EVENT_JUST_WORKS_REQUEST を送り、
         * アプリが sm_just_works_confirm() で応答しないとペアリング失敗。
         */
        tm_printf((UB *)"BLE: SM Just Works confirm\n");
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        break;

    case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
        /* Numeric Comparison (Secure Connections 使用時) — 自動承認 */
        tm_printf((UB *)"BLE: SM Numeric Comparison confirm\n");
        sm_numeric_comparison_confirm(sm_event_numeric_comparison_request_get_handle(packet));
        break;

    case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
        tm_printf((UB *)"BLE: SM passkey display: %06lu\n",
                  sm_event_passkey_display_number_get_passkey(packet));
        break;

    case SM_EVENT_PASSKEY_INPUT_NUMBER:
        tm_printf((UB *)"BLE: SM passkey input requested\n");
        /* TODO: キーボードからのパスキー入力を実装 */
        break;

    case SM_EVENT_PAIRING_STARTED:
        tm_printf((UB *)"BLE: pairing started\n");
        break;

    case SM_EVENT_PAIRING_COMPLETE:
        switch (sm_event_pairing_complete_get_status(packet)) {
        case ERROR_CODE_SUCCESS:
            tm_printf((UB *)"BLE: pairing OK\n");
            /* Flash 書込みは接続中に行わない (60ms 割込禁止で切断される)。
             * ble_hid_gatt_process() の遅延 flush で書く。 */
            break;
        case ERROR_CODE_CONNECTION_TIMEOUT:
            tm_printf((UB *)"BLE: pairing timeout\n");
            break;
        case ERROR_CODE_REMOTE_USER_TERMINATED_CONNECTION:
            tm_printf((UB *)"BLE: pairing rejected\n");
            break;
        case ERROR_CODE_AUTHENTICATION_FAILURE:
            tm_printf((UB *)"BLE: pairing auth fail\n");
            break;
        default:
            tm_printf((UB *)"BLE: pairing fail 0x%02x\n",
                      sm_event_pairing_complete_get_status(packet));
            break;
        }
        break;

    case SM_EVENT_REENCRYPTION_STARTED:
        tm_printf((UB *)"BLE: reencryption started\n");
        break;

    case SM_EVENT_REENCRYPTION_COMPLETE: {
        uint8_t re_status = sm_event_reencryption_complete_get_status(packet);
        tm_printf((UB *)"BLE: reencryption done (st=0x%02x)\n", re_status);
        if (re_status == ERROR_CODE_SUCCESS) {
            /*
             * CCC 永続化なし (btstack_tlv_set_instance 未使用) のため、
             * 再暗号化成功時に手動で HID 送信を有効化。
             * ホスト OS は CCC をキャッシュしており通知を受け入れる。
             */
            ble_can_send = TRUE;
            tm_printf((UB *)"BLE: HID report enabled (reencrypt)\n");
        }
        break;
    }

    default:
        break;
    }
}

/*----------------------------------------------------------------------
 * BLE HID GATT 初期化
 */
EXPORT ER ble_hid_gatt_init(void)
{
    /* HCI 初期化 — CYW43 HCI トランスポートを登録 */
    {
        extern const hci_transport_t *hci_transport_cyw43_instance(void);
        hci_init(hci_transport_cyw43_instance(), NULL);
    }

    /* L2CAP */
    l2cap_init();

    /*
     * Security Manager: パスキー入力 (MITM 保護あり)
     *
     * キーボードは入力デバイスなので IO_CAPABILITY_KEYBOARD_ONLY を使用。
     * ペアリング時にホストがパスキーを表示し、ユーザーがキーボードで入力。
     * LE Secure Connections + Passkey Entry により MITM 攻撃を防止。
     */
    sm_init();
    /*
     * LE Secure Connections + Just Works
     * iOS/Android は HID デバイスに Secure Connections を要求する。
     * IO_CAPABILITY_NO_INPUT_NO_OUTPUT + Just Works で自動ペアリング。
     */
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(
        SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);

    /* ボンディングデータ永続化 (Flash TLV) */
    {
        extern void btstack_tlv_flash_init(void);
        extern const btstack_tlv_t *btstack_tlv_flash_instance(void);
        extern void *btstack_tlv_flash_context(void);
        btstack_tlv_flash_init();
        const btstack_tlv_t *tlv = btstack_tlv_flash_instance();
        void *tlv_ctx = btstack_tlv_flash_context();

        /* デバイス DB を TLV バックエンドに切替 (ボンディング永続化)
         *
         * btstack_tlv_set_instance() は呼ばない。
         * グローバル TLV を登録すると ATT server が CCC を Flash に
         * inline 書込みし、SM パケット喪失でペアリングが失敗する。
         * CCC 永続化なしでもホストは再接続時に再 subscribe する。
         */
        le_device_db_tlv_configure(tlv, tlv_ctx);
    }

    /* ATT サーバ */
    att_server_init(profile_data, NULL, NULL);

    /*
     * GAP Device Name は GATT DB (ble_hid_keyboard.h) でコンパイル時に設定。
     * gap_set_local_name() は Classic BT 専用のため BLE では使わない。
     * BLE のデバイス名はアドバタイジングデータの Local Name で設定する。
     */

    /* Battery Service */
    battery_service_server_init(ble_battery);

    /* Device Information Service — KB_MANUFACTURER / KB_PRODUCT から設定 */
    device_information_service_server_init();
#ifdef KB_MANUFACTURER
    device_information_service_server_set_manufacturer_name(KB_MANUFACTURER);
#endif
    device_information_service_server_set_model_number(KB_BLE_NAME);
    device_information_service_server_set_firmware_revision("1.0.0");

    /* HID Service */
    hids_device_init(0, hid_descriptor_keyboard, sizeof(hid_descriptor_keyboard));

    /* アドバタイジングデータ構築 (KB_PRODUCT から名前取得) */
    {
        const char *name = KB_BLE_NAME;
        uint8_t name_len = (uint8_t)strlen(name);
        if (name_len > 18) name_len = 18;

        uint8_t pos = 0;
        /* Flags: LE General Discoverable, BR/EDR not supported */
        adv_data[pos++] = 0x02;
        adv_data[pos++] = 0x01;
        adv_data[pos++] = 0x06;
        /* Shortened Local Name */
        adv_data[pos++] = name_len + 1;
        adv_data[pos++] = 0x08;
        memcpy(&adv_data[pos], name, name_len);
        pos += name_len;
        /* 16-bit Service UUIDs: HID Service (0x1812) */
        adv_data[pos++] = 0x03;
        adv_data[pos++] = 0x03;
        adv_data[pos++] = 0x12;
        adv_data[pos++] = 0x18;
        /* Appearance: HID Keyboard (0x03C1) */
        adv_data[pos++] = 0x03;
        adv_data[pos++] = 0x19;
        adv_data[pos++] = 0xC1;
        adv_data[pos++] = 0x03;
        adv_data_len = pos;
    }

    /* 高速アドバタイズで開始 (30 秒後にスローアドバタイズへ自動切替) */
    adv_start_fast();

    /* イベントハンドラ登録 */
    hci_event_cb.callback = &ble_packet_handler;
    hci_add_event_handler(&hci_event_cb);

    sm_event_cb.callback = &ble_packet_handler;
    sm_add_event_handler(&sm_event_cb);

    hids_device_register_packet_handler(ble_packet_handler);

    /*
     * HCI パワーオンは ble_hid_gatt_start() に分離。
     * hci_power_control(HCI_POWER_ON) は HCI_RESET 送信後 200ms タイマーを
     * 設定するため、BLE タスク (run loop) が稼働中でないとタイムアウトする。
     * btstack_tkernel_start() の後に呼ぶ必要がある。
     */

    /* 起動時ボンディングチェック → ペアリングモード自動判定 */
    check_bonding_at_startup();

    tm_printf((UB *)"BLE: GATT HID keyboard initialized\n");
    return E_OK;
}

/*----------------------------------------------------------------------
 * BLE HCI パワーオン (BLE タスク起動後に呼ぶ)
 *
 * hci_power_control(HCI_POWER_ON) は HCI_RESET を送信し 200ms タイマーを設定する。
 * BTstack run loop (BLE タスク) が稼働していないとタイマーがたまり、
 * 最初の execute_once() で一斉に fire → HCI_RESET 無限ループになる。
 *
 * btstack_tkernel_start() の後、BLE タスクが稼働中に呼ぶこと。
 */
EXPORT ER ble_hid_gatt_start(void)
{
    /*
     * CYW43 省電力モードを HCI パワーオンの前に設定する。
     *
     * cyw43_wifi_pm() は CYW43 ロックを保持したまま IOCTL を実行し、
     * 数百 ms ブロックする。hci_power_control() の後に呼ぶと:
     *   1. HCI_RESET 送信 + 200ms タイムアウトタイマー設定
     *   2. cyw43_wifi_pm() がロック保持 → BLE タスクがブロック
     *   3. 200ms 経過 → タイムアウト fire → HCI_RESET 再送ループ
     *
     * 先に PM を設定すれば、hci_power_control() 時点ではロック競合なし。
     */
    {
        extern void cyw43_arch_tkernel_enable_pm(void);
        cyw43_arch_tkernel_enable_pm();
    }

    /* HCI パワーオン — BLE タスクが稼働中なので CC を即座に処理できる */
    int ret = hci_power_control(HCI_POWER_ON);
    if (ret != 0) {
        tm_printf((UB *)"BLE: HCI power on failed: %d\n", ret);
        return E_IO;
    }

    return E_OK;
}

/*----------------------------------------------------------------------
 * BLE HID レポート送信
 *
 * KB scanner タスクから呼ばれる。BTstack はスレッドセーフではないため、
 * BTstack API を直接呼ばず、pending_report にコピーした後
 * BLE タスクにイベントで通知する。
 */
EXPORT ER ble_hid_gatt_send_report(const T_HID_KBD_REPORT *report)
{
    if (ble_con_handle == HCI_CON_HANDLE_INVALID) return E_IO;
    if (!ble_can_send) return E_IO;

    {
        UINT imask;
        DI(imask);
        memcpy(&pending_report, report, sizeof(T_HID_KBD_REPORT));
        report_pending = TRUE;
        EI(imask);
    }

    extern void btstack_tkernel_trigger(void);
    btstack_tkernel_trigger();

    return E_OK;
}

/*----------------------------------------------------------------------
 * BLE タスクから呼ばれるポーリング処理
 * BTstack コンテキスト内なのでスレッドセーフ。
 */
/*
 * BOOTSEL ボタン読み取り (RP2040)
 *
 * BOOTSEL は QSPI_SS_N ピンに接続。通常は Flash チップセレクトとして
 * 使用されているため、読み取るには一時的に出力を Hi-Z にする必要がある。
 * その間 Flash (XIP) にアクセスできないので、この関数は RAM に配置する。
 */
#define IO_QSPI_BASE                  0x40018000u
#define IO_QSPI_GPIO_QSPI_SS_CTRL    (IO_QSPI_BASE + 0x0Cu)
#define IO_QSPI_GPIO_QSPI_SS_STATUS  (IO_QSPI_BASE + 0x08u)
#define QSPI_SS_OEOVER_LSB           12
#define QSPI_SS_OEOVER_BITS          (0x3u << QSPI_SS_OEOVER_LSB)
#define SIO_GPIO_HI_IN               0xD0000008u

__attribute__((noinline, section(".data")))
LOCAL int bootsel_read(void)
{
    /*
     * 全て直接レジスタ操作 — Flash 上の関数 (in_w/out_w/DI/EI) は
     * QSPI_SS Hi-Z 中に呼べないため volatile ポインタで直接アクセス。
     */
    volatile uint32_t *qspi_ss_ctrl =
        (volatile uint32_t *)IO_QSPI_GPIO_QSPI_SS_CTRL;
    volatile uint32_t *gpio_hi_in =
        (volatile uint32_t *)SIO_GPIO_HI_IN;

    /* 割り込み禁止 (PRIMASK) */
    uint32_t primask;
    __asm volatile ("mrs %0, primask" : "=r" (primask));
    __asm volatile ("cpsid i");

    /* QSPI_SS 出力を Hi-Z に (Flash CS 無効化) */
    uint32_t ctrl = *qspi_ss_ctrl;
    *qspi_ss_ctrl = (ctrl & ~QSPI_SS_OEOVER_BITS) | (2u << QSPI_SS_OEOVER_LSB);

    /* ピン安定待ち (Flash アクセス不可) */
    volatile int i;
    for (i = 0; i < 1000; i++) {}

    /* QSPI_SS_N ピン読み取り (SIO GPIO HI input, bit 1 = QSPI_SS) */
    int pressed = !(*gpio_hi_in & (1u << 1));

    /* QSPI_SS 出力を通常に復帰 (Flash 復活) */
    *qspi_ss_ctrl = (ctrl & ~QSPI_SS_OEOVER_BITS);

    /* 割り込み復帰 */
    __asm volatile ("msr primask, %0" :: "r" (primask));

    return pressed;
}

EXPORT void ble_hid_gatt_process(void)
{
    /* XMK ペアリングリクエスト処理 (BTstack コンテキスト) */
    if (pairing_request) {
        pairing_request = FALSE;
        enter_pairing_mode();
    }

    /*
     * BOOTSEL ボタン長押し → ペアリングモード
     *
     * 3秒以上押し続けるとボンディング情報をクリアしてペアリングモードに入る。
     * BLE タスクは ~5ms 間隔でポーリングするので、600回 ≒ 3秒。
     */
    {
        static int bootsel_hold_count = 0;
        int btn = bootsel_read();
        if (btn) {
            bootsel_hold_count++;
            if (bootsel_hold_count == 600) {  /* ~3秒 */
                tm_printf((UB *)"BLE: BOOTSEL long press -> pairing mode\n");
                enter_pairing_mode();
            }
        } else {
            bootsel_hold_count = 0;
        }
    }

    if (report_pending && ble_can_send &&
        ble_con_handle != HCI_CON_HANDLE_INVALID) {
        hids_device_request_can_send_now_event(ble_con_handle);
    }

    /* Flash 永続化は btstack_tlv_flash_bank が自動管理 */
}

/*----------------------------------------------------------------------
 * BLE 接続状態
 */
EXPORT BOOL ble_hid_gatt_is_connected(void)
{
    return (ble_con_handle != HCI_CON_HANDLE_INVALID) && ble_can_send;
}

/*----------------------------------------------------------------------
 * ペアリングモード
 */
EXPORT BOOL ble_hid_is_pairing_mode(void)
{
    return pairing_mode;
}

/*
 * ペアリングモード突入 (BTstack コンテキストから呼ぶ)
 *
 * 1. 既存のボンディング情報を全クリア
 * 2. 現在の接続を切断 (接続中の場合)
 * 3. Fast Advertising を再開 (新規ペアリング受付)
 * 4. LED 高速点滅
 */
/*
 * ペアリングモード再開タイマー
 * ボンディングクリア後、アドバタイジングを停止して
 * ホスト側のデバイス削除を待つ。
 */
LOCAL btstack_timer_source_t pairing_restart_timer;

LOCAL void pairing_restart_handler(btstack_timer_source_t *ts)
{
    (void)ts;
    pairing_cooldown = FALSE;
    tm_printf((UB *)"BLE: HCI restart for pairing\n");
    /* HCI 再起動 — コントローラが完全にリセットされた状態で再開 */
    hci_power_control(HCI_POWER_ON);
    /* advertising は HCI init 完了後に BTstack が自動設定 */
}

LOCAL void enter_pairing_mode(void)
{
    tm_printf((UB *)"BLE: entering pairing mode\n");

    /* ボンディング情報クリア */
    {
        int i;
        int max = le_device_db_max_count();
        for (i = 0; i < max; i++) {
            int addr_type = BD_ADDR_TYPE_UNKNOWN;
            bd_addr_t addr;
            sm_key_t irk;
            le_device_db_info(i, &addr_type, addr, irk);
            if (addr_type != BD_ADDR_TYPE_UNKNOWN) {
                le_device_db_remove(i);
            }
        }
        tm_printf((UB *)"BLE: bonding data cleared\n");
        /* btstack_tlv_flash_bank が le_device_db_remove() の
         * tlv_delete_tag() でバンクに即反映する */
    }

    pairing_mode = TRUE;

    /* 接続中なら切断 */
    if (ble_con_handle != HCI_CON_HANDLE_INVALID) {
        gap_disconnect(ble_con_handle);
    }

    /*
     * アドバタイジングを一旦停止。
     * 古いホストが自動再接続を繰り返すのを防ぐ。
     * 5秒後に再開 — その間にホスト側でデバイスを削除してもらう。
     */
    /*
     * HCI スタック全体をリセットして再起動。
     * resolving list, whitelist 等のコントローラ状態を完全にクリアし、
     * 初回起動と同じクリーンな状態でペアリングを受け付ける。
     */
    tm_printf((UB *)"BLE: HCI reset for re-pairing\n");
    hci_power_control(HCI_POWER_OFF);

    pairing_cooldown = TRUE;
    tm_printf((UB *)"BLE: delete device from host now!\n");

    btstack_run_loop_remove_timer(&pairing_restart_timer);
    btstack_run_loop_set_timer(&pairing_restart_timer, 5000);
    pairing_restart_timer.process = pairing_restart_handler;
    btstack_run_loop_add_timer(&pairing_restart_timer);

    /* LED 高速点滅 */
    led_start_blink(LED_BLINK_FAST_MS);
}

/*----------------------------------------------------------------------
 * XMK アクション処理
 *
 * Scanner タスクから呼ばれる。pairing_request フラグをセットし、
 * BLE タスクにイベントで通知する。
 * 実際の処理は ble_hid_gatt_process() で BTstack コンテキストから実行。
 */
EXPORT void ble_hid_gatt_xmk_action(UH xmk_code)
{
    (void)xmk_code;

    /* XMK_BLE_PAIR (0x3001) のみ対応 */
    if ((xmk_code & 0xF000) == 0x3000 && (xmk_code & 0x0FFF) == 0x01) {
        pairing_request = TRUE;
        extern void btstack_tkernel_trigger(void);
        btstack_tkernel_trigger();
    }
}

/*----------------------------------------------------------------------
 * 起動時ボンディングチェック
 *
 * ボンディング情報がなければ自動的にペアリングモードに入る。
 * ble_hid_gatt_init() の最後で呼ばれる。
 */
LOCAL void check_bonding_at_startup(void)
{
    int i;
    int max = le_device_db_max_count();
    BOOL has_bonds = FALSE;

    for (i = 0; i < max; i++) {
        int addr_type = BD_ADDR_TYPE_UNKNOWN;
        bd_addr_t addr;
        sm_key_t irk;
        le_device_db_info(i, &addr_type, addr, irk);
        if (addr_type != BD_ADDR_TYPE_UNKNOWN) {
            has_bonds = TRUE;
            break;
        }
    }

    if (!has_bonds) {
        tm_printf((UB *)"BLE: no bonding data, auto pairing mode\n");
        pairing_mode = TRUE;
        led_start_blink(LED_BLINK_FAST_MS);
    } else {
        tm_printf((UB *)"BLE: bonded device found, normal mode\n");
        pairing_mode = FALSE;
        led_start_blink(LED_BLINK_SLOW_MS);
    }
}

#endif /* CPU_RP2040 */
