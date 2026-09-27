/*
 * kb_output.h — HID output transport abstraction
 *
 * USB HID と BLE HID を抽象化し、kb_main.c から
 * トランスポートに依存しないレポート送信を可能にする。
 *
 * 動作モード (自動切替):
 *   USB ケーブル接続時 → USB HID
 *   USB 非接続時       → BLE HID (ビルド時に BLE 有効の場合)
 *
 * HID Keyboard Report (8 bytes, Boot Protocol) は
 * USB と BLE で完全に同一フォーマットである。
 */

#ifndef __KB_OUTPUT_H__
#define __KB_OUTPUT_H__

#include <tk/tkernel.h>

/*----------------------------------------------------------------------
 * HID Keyboard Report (トランスポート共通)
 *
 * USB/BLE 両方で同じ 8byte Boot Protocol レポートを使用。
 * USB: EP1 IN でホストに送信
 * BLE: GATT HID Service の Report Characteristic で Notification
 */
typedef struct {
    UB  modifier;       /* Byte 0: modifier key bits */
    UB  reserved;       /* Byte 1: reserved (always 0) */
    UB  keycode[6];     /* Bytes 2-7: up to 6 keycodes (6KRO) */
} T_HID_KBD_REPORT;

/* Modifier key bit masks */
#define HID_MOD_LCTRL   (1<<0)
#define HID_MOD_LSHIFT  (1<<1)
#define HID_MOD_LALT    (1<<2)
#define HID_MOD_LGUI    (1<<3)
#define HID_MOD_RCTRL   (1<<4)
#define HID_MOD_RSHIFT  (1<<5)
#define HID_MOD_RALT    (1<<6)
#define HID_MOD_RGUI    (1<<7)

/*----------------------------------------------------------------------
 * HID Gamepad Report (9 bytes, USB のゲームパッドインタフェースのみ)
 *
 * dev_usb_hid.h の T_USB_HID_PAD_REPORT と同じ並び。
 */
typedef struct {
    UB  buttons[4];     /* bit n (buttons[n/8] の bit n%8): ボタン n+1 */
    UB  hat;            /* 0 上, 1 右上 .. 7 左上 (時計回り), 8 中立 */
    B   axis[4];        /* X, Y, Z, Rz: -127..127。Y と Rz は下が正 */
} T_HID_PAD_REPORT;

#define HID_PAD_HAT_CENTER  8

/*----------------------------------------------------------------------
 * HID 出力トランスポート インタフェース
 */
typedef struct {
    /* 初期化。デバイスオープン・接続待ち等を行う */
    ER    (*init)(void);

    /* HID レポート送信。changed 時のみ呼ばれる */
    ER    (*send_report)(const T_HID_KBD_REPORT *report);

    /* 送信可能か (USB: CONFIGURED, BLE: connected) */
    BOOL  (*is_ready)(void);

    /* ゲームパッドレポート送信。待たない (NULL なら未対応) */
    ER    (*send_pad)(const T_HID_PAD_REPORT *report);

    /* XMK アクション処理 (NULL ならスキップ) */
    void  (*xmk_action)(UH xmk_code);

    /* 名称 (ログ用) */
    const char *name;
} T_HID_OUTPUT;

/*----------------------------------------------------------------------
 * トランスポート実装 (ビルド時に選択)
 */

/* USB HID 出力 (常に利用可能) */
extern const T_HID_OUTPUT kb_output_usb;

/* BLE HID 出力 (KB_OUTPUT_BLE 定義時のみ利用可能) */
#if defined(KB_OUTPUT_BLE)
extern const T_HID_OUTPUT kb_output_ble;

/* USB 使用時に BLE を非同期で初期化開始 (BLE タスクで CYW43/GATT 初期化) */
void ble_deferred_init(void);
#endif

/*----------------------------------------------------------------------
 * 統合 API (kb_main.c から呼ぶ)
 *
 * USB/BLE 自動切替ロジックを内包。
 * USB CONFIGURED なら USB、そうでなければ BLE にフォールバック。
 */

/* 出力初期化 (USB/BLE の init を呼ぶ) */
ER kb_output_init(void);

/* レポート送信 (アクティブなトランスポートに送信) */
ER kb_output_send(const T_HID_KBD_REPORT *report);

/*
 * ゲームパッドレポート送信 (アクティブなトランスポートに送信)
 *   E_NOSPT: トランスポートがゲームパッドを持たない
 *   E_BUSY : 前のレポートがまだ読まれていない (次の周期に送り直す)
 */
ER kb_output_send_pad(const T_HID_PAD_REPORT *report);

/* 現在のアクティブトランスポートが送信可能か */
BOOL kb_output_is_ready(void);

/* XMK アクション実行 (アクティブトランスポートに委譲) */
void kb_output_xmk_action(UH xmk_code);

/* 現在のアクティブトランスポート名 ("USB"/"BLE"/"none") */
const char *kb_output_active_name(void);

#endif /* __KB_OUTPUT_H__ */
