/*
 *----------------------------------------------------------------------
 *    USB Device Driver for μT-Kernel 3.0 BSP
 *
 *    USB HID Keyboard デバイスドライバ 公開 API
 *----------------------------------------------------------------------
 */

#ifndef __DEV_USB_HID_H__
#define __DEV_USB_HID_H__

#include <tk/typedef.h>

/* μT-Kernel デバイス名 */
#define USB_HID_DEVNM	"usbk"

/*
 *  HID Keyboard Report (8 bytes, Boot Protocol)
 */
typedef struct {
	UB	modifier;
	UB	reserved;
	UB	keycode[6];
} T_USB_HID_KBD_REPORT;

/*
 *  データ番号 (tk_wri_dev の start)
 */
#define USB_HID_DN_KBD	0	/* キーボード (T_USB_HID_KBD_REPORT) */
#define USB_HID_DN_PAD	1	/* ゲームパッド (T_USB_HID_PAD_REPORT)。
				   USB_HID_GAMEPAD 定義時のみ */

/*
 *  HID Gamepad Report (9 bytes, レポート ID なし)
 *
 *  ホストの HID ドライバ (Windows/Linux/macOS/超漢字) が汎用のゲーム
 *  コントローラとして読む。ボタン番号の慣例 (ボタン 1 Y, 2 B, 3 A, 4 X,
 *  5 L, 6 R, 9 Select, 10 Start) は kb_hid_keycodes.h の JS_* を参照。
 *
 *  書き込みは待たない: 前のレポートがまだホストに読まれていなければ
 *  E_BUSY を返す (BIOS などキーボードしか読まないホストでは読まれない)。
 */
typedef struct {
	UB	buttons[4];	/* bit n (buttons[n/8] の bit n%8): ボタン n+1 */
	UB	hat;		/* 0 上, 1 右上 .. 7 左上 (時計回り), 8 中立 */
	B	axis[4];	/* X, Y, Z, Rz: -127..127。Y と Rz は下が正 */
} T_USB_HID_PAD_REPORT;

#define USB_HID_PAD_HAT_CENTER	8

/* Modifier key bit masks */
#define HID_MOD_LCTRL	(1<<0)
#define HID_MOD_LSHIFT	(1<<1)
#define HID_MOD_LALT	(1<<2)
#define HID_MOD_LGUI	(1<<3)
#define HID_MOD_RCTRL	(1<<4)
#define HID_MOD_RSHIFT	(1<<5)
#define HID_MOD_RALT	(1<<6)
#define HID_MOD_RGUI	(1<<7)

/* デバイス初期化 (knl_start_device から呼び出し) */
IMPORT ER dev_init_usb_hid(UW unit);

/*
 *  USB CONFIGURED イベント通知
 *  ホストが SET_CONFIGURATION を完了し、HID デバイスとして使える状態になった時
 *  にイベントフラグがセットされる。アプリは固定時間待ちの代わりにこれを使う。
 */
#define USB_HID_EVT_CONFIGURED	(1u << 0)

/* CONFIGURED イベントフラグ ID を取得 (dev_init_usb_hid 後に有効) */
IMPORT ID usb_hid_get_cfg_flgid(void);

/*
 *  USB D+ プルアップ有効化 (デバイス接続開始)
 *
 *  dev_init_usb_hid() は D+ プルアップを有効にしない (遅延接続モード)。
 *  アプリが CONFIGURED 待ちの準備ができた後にこの関数を呼ぶ。
 *  これにより USB タスクが SETUP パケットを確実に処理できる。
 */
IMPORT ER usb_hid_connect(void);

#endif	/* __DEV_USB_HID_H__ */
