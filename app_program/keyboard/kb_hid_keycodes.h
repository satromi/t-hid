/*
 * kb_hid_keycodes.h — USB HID Keyboard Usage Page (0x07) keycodes
 *
 * Standard keycodes for use in keymap definitions.
 * Values 0x00-0xFF are standard HID keycodes.
 * Values 0x1000+ are framework action keycodes (MO, TG, etc.)
 */

#ifndef __KB_HID_KEYCODES_H__
#define __KB_HID_KEYCODES_H__

/*--------------------------------------------------------------------
 * Special keycodes
 *--------------------------------------------------------------------*/
#define KC_NO       0x0000   /* No key / transparent placeholder */
#define KC_TRNS     0xFFFF   /* Transparent — fall through to lower layer */
#define ___         KC_TRNS  /* Shorthand for transparent */

/*--------------------------------------------------------------------
 * Layer action keycodes (encoded in upper nibble)
 *--------------------------------------------------------------------*/
#define KC_MO(n)    (0x1000 | (n))   /* Momentary layer activate */
#define KC_TG(n)    (0x2000 | (n))   /* Toggle layer */

#define KC_IS_ACTION(kc)   ((kc) & 0xF000)
#define KC_ACTION_TYPE(kc) ((kc) & 0xF000)
#define KC_ACTION_ARG(kc)  ((kc) & 0x0FFF)
#define KC_ACTION_MO        0x1000
#define KC_ACTION_TG        0x2000
#define KC_ACTION_XMK       0x3000

/*--------------------------------------------------------------------
 * Extended action keycodes (XMK) — user-definable in keymap
 *
 * KC_XMK(n) でキーマップに配置。押下エッジで kb_output にコールバック。
 * HID レポートには含まれない (ファームウェア内部アクション)。
 *--------------------------------------------------------------------*/
#define KC_XMK(n)   (0x3000 | (n))

/* XMK action IDs */
#define XMK_BLE_PAIR    KC_XMK(0x01)   /* BLE ペアリングモード (ボンディングクリア + Fast Adv) */

/*--------------------------------------------------------------------
 * Gamepad keycodes (JS) — USB のゲームパッドインタフェースへ出す
 *
 * キーボードのレポートには含まれない。ゲームパッドインタフェースは
 * ビルド時の GAMEPAD=1 (USB_HID_GAMEPAD) で有効になり、無効なら何もしない。
 *
 * ボタン番号は汎用 HID ゲームパッドで多い並び (SNES 配置):
 *   1 Y, 2 B, 3 A, 4 X, 5 L, 6 R, 7 L2, 8 R2, 9 Select, 10 Start,
 *   11 L3, 12 R3, 13 Home
 * 十字キーはハットスイッチ、スティックはキーを押している間だけ端まで倒す。
 *--------------------------------------------------------------------*/
#define KC_ACTION_JS        0x4000
#define KC_JS_ARG_DPAD      0x20            /* 0x20-0x23: 上 下 左 右 */
#define KC_JS_ARG_STICK     0x24            /* 0x24-0x2B: 左右スティックの 4 方向 */

#define JS_BTN(n)   (KC_ACTION_JS | ((n) & 0x1F))   /* ボタン n+1 (n = 0..31) */

#define JS_Y        JS_BTN(0)
#define JS_B        JS_BTN(1)
#define JS_A        JS_BTN(2)
#define JS_X        JS_BTN(3)
#define JS_L        JS_BTN(4)
#define JS_R        JS_BTN(5)
#define JS_L2       JS_BTN(6)
#define JS_R2       JS_BTN(7)
#define JS_SELECT   JS_BTN(8)
#define JS_START    JS_BTN(9)
#define JS_L3       JS_BTN(10)
#define JS_R3       JS_BTN(11)
#define JS_HOME     JS_BTN(12)

#define JS_UP       (KC_ACTION_JS | 0x20)   /* 十字キー (ハット) */
#define JS_DOWN     (KC_ACTION_JS | 0x21)
#define JS_LEFT     (KC_ACTION_JS | 0x22)
#define JS_RGHT     (KC_ACTION_JS | 0x23)

#define JS_LS_UP    (KC_ACTION_JS | 0x24)   /* 左スティック (X/Y) */
#define JS_LS_DOWN  (KC_ACTION_JS | 0x25)
#define JS_LS_LEFT  (KC_ACTION_JS | 0x26)
#define JS_LS_RGHT  (KC_ACTION_JS | 0x27)
#define JS_RS_UP    (KC_ACTION_JS | 0x28)   /* 右スティック (Z/Rz) */
#define JS_RS_DOWN  (KC_ACTION_JS | 0x29)
#define JS_RS_LEFT  (KC_ACTION_JS | 0x2A)
#define JS_RS_RGHT  (KC_ACTION_JS | 0x2B)

/*--------------------------------------------------------------------
 * Modifier keycodes (0xE0-0xE7)
 * These are both valid HID keycodes AND modifier bit indicators
 *--------------------------------------------------------------------*/
#define KC_LCTL     0xE0   /* Left Control */
#define KC_LSFT     0xE1   /* Left Shift */
#define KC_LALT     0xE2   /* Left Alt */
#define KC_LGUI     0xE3   /* Left GUI (Win/Cmd) */
#define KC_RCTL     0xE4   /* Right Control */
#define KC_RSFT     0xE5   /* Right Shift */
#define KC_RALT     0xE6   /* Right Alt */
#define KC_RGUI     0xE7   /* Right GUI */

/* Modifier check helper */
#define KC_IS_MODIFIER(kc) ((kc) >= 0xE0 && (kc) <= 0xE7)

/*--------------------------------------------------------------------
 * Letter keys (0x04-0x1D)
 *--------------------------------------------------------------------*/
#define KC_A        0x04
#define KC_B        0x05
#define KC_C        0x06
#define KC_D        0x07
#define KC_E        0x08
#define KC_F        0x09
#define KC_G        0x0A
#define KC_H        0x0B
#define KC_I        0x0C
#define KC_J        0x0D
#define KC_K        0x0E
#define KC_L        0x0F
#define KC_M        0x10
#define KC_N        0x11
#define KC_O        0x12
#define KC_P        0x13
#define KC_Q        0x14
#define KC_R        0x15
#define KC_S        0x16
#define KC_T        0x17
#define KC_U        0x18
#define KC_V        0x19
#define KC_W        0x1A
#define KC_X        0x1B
#define KC_Y        0x1C
#define KC_Z        0x1D

/*--------------------------------------------------------------------
 * Number keys (0x1E-0x27)
 *--------------------------------------------------------------------*/
#define KC_1        0x1E
#define KC_2        0x1F
#define KC_3        0x20
#define KC_4        0x21
#define KC_5        0x22
#define KC_6        0x23
#define KC_7        0x24
#define KC_8        0x25
#define KC_9        0x26
#define KC_0        0x27

/*--------------------------------------------------------------------
 * Common keys
 *--------------------------------------------------------------------*/
#define KC_ENT      0x28   /* Enter */
#define KC_ESC      0x29   /* Escape */
#define KC_BSPC     0x2A   /* Backspace */
#define KC_TAB      0x2B   /* Tab */
#define KC_SPC      0x2C   /* Space */
#define KC_MINS     0x2D   /* - _ */
#define KC_EQL      0x2E   /* = + */
#define KC_LBRC     0x2F   /* [ { */
#define KC_RBRC     0x30   /* ] } */
#define KC_BSLS     0x31   /* \ | */
#define KC_NUHS     0x32   /* Non-US # ~ */
#define KC_SCLN     0x33   /* ; : */
#define KC_QUOT     0x34   /* ' " */
#define KC_GRV      0x35   /* ` ~ */
#define KC_COMM     0x36   /* , < */
#define KC_DOT      0x37   /* . > */
#define KC_SLSH     0x38   /* / ? */
#define KC_CAPS     0x39   /* Caps Lock */

/*--------------------------------------------------------------------
 * Function keys (0x3A-0x45)
 *--------------------------------------------------------------------*/
#define KC_F1       0x3A
#define KC_F2       0x3B
#define KC_F3       0x3C
#define KC_F4       0x3D
#define KC_F5       0x3E
#define KC_F6       0x3F
#define KC_F7       0x40
#define KC_F8       0x41
#define KC_F9       0x42
#define KC_F10      0x43
#define KC_F11      0x44
#define KC_F12      0x45

/*--------------------------------------------------------------------
 * Navigation keys
 *--------------------------------------------------------------------*/
#define KC_PSCR     0x46   /* Print Screen */
#define KC_SLCK     0x47   /* Scroll Lock */
#define KC_PAUS     0x48   /* Pause */
#define KC_INS      0x49   /* Insert */
#define KC_HOME     0x4A   /* Home */
#define KC_PGUP     0x4B   /* Page Up */
#define KC_DEL      0x4C   /* Delete */
#define KC_END      0x4D   /* End */
#define KC_PGDN     0x4E   /* Page Down */
#define KC_RGHT     0x4F   /* Right Arrow */
#define KC_LEFT     0x50   /* Left Arrow */
#define KC_DOWN     0x51   /* Down Arrow */
#define KC_UP       0x52   /* Up Arrow */

/*--------------------------------------------------------------------
 * Numpad keys
 *--------------------------------------------------------------------*/
#define KC_NLCK     0x53   /* Num Lock */
#define KC_PSLS     0x54   /* Numpad / */
#define KC_PAST     0x55   /* Numpad * */
#define KC_PMNS     0x56   /* Numpad - */
#define KC_PPLS     0x57   /* Numpad + */
#define KC_PENT     0x58   /* Numpad Enter */
#define KC_P1       0x59   /* Numpad 1 */
#define KC_P2       0x5A   /* Numpad 2 */
#define KC_P3       0x5B   /* Numpad 3 */
#define KC_P4       0x5C   /* Numpad 4 */
#define KC_P5       0x5D   /* Numpad 5 */
#define KC_P6       0x5E   /* Numpad 6 */
#define KC_P7       0x5F   /* Numpad 7 */
#define KC_P8       0x60   /* Numpad 8 */
#define KC_P9       0x61   /* Numpad 9 */
#define KC_P0       0x62   /* Numpad 0 */
#define KC_PDOT     0x63   /* Numpad . */

/*--------------------------------------------------------------------
 * International keys
 *--------------------------------------------------------------------*/
#define KC_NUBS     0x64   /* Non-US \ | */
#define KC_APP      0x65   /* Application (Menu) */
#define KC_RO       0x87   /* JIS \ | (Ro) */
#define KC_KANA     0x88   /* Katakana/Hiragana */
#define KC_JYEN     0x89   /* JIS Yen */
#define KC_HENK     0x8A   /* Henkan */
#define KC_MHEN     0x8B   /* Muhenkan */
#define KC_ZKHK     0x35   /* Zenkaku/Hankaku (same as grave) */

/*--------------------------------------------------------------------
 * Aliases (QMK compatibility)
 *--------------------------------------------------------------------*/
#define KC_ENTER    KC_ENT
#define KC_ESCAPE   KC_ESC
#define KC_SPACE    KC_SPC
#define KC_LSHIFT   KC_LSFT
#define KC_RSHIFT   KC_RSFT
#define KC_LCTRL    KC_LCTL
#define KC_RCTRL    KC_RCTL

#endif /* __KB_HID_KEYCODES_H__ */
