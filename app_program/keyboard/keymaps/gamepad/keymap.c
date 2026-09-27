/*
 * keymaps/gamepad/keymap.c — QWERTY + gamepad layer for mintlsplit 72-key
 *
 * ★ User-editable file
 * Build with: make KEYMAP=gamepad GAMEPAD=1 all
 *
 * The base and FN layers are the same as keymaps/default. FN+G toggles the
 * gamepad layer (TG); in it the keys below go to the USB gamepad interface
 * instead of the keyboard, and every other key keeps its base meaning.
 */

#include "kb_keymap.h"

#define ___ KC_TRNS

/* XMK extended actions (firmware internal) */
#define BLE_PAIR  XMK_BLE_PAIR   /* FN+Q: BLE ペアリングモード */

#define LAYER_PAD  LAYER_NUM     /* gamepad layer */
#define PAD_TG     KC_TG(LAYER_PAD)

/*
 * Gamepad layer (FN+G で切り替え)
 *     ,-----------------------------------------.              ,-----------------------------------------.
 *     |      |      |      |      |      |      |              |      |      |      |      |      |      |
 * ,---+------+------+------+------+------+------+----.    ,----+------+------+------+------+------+------+----.
 * |   |      |   L  |  ↑  |   R  |      |      |    |    |    |      |  L2  |   X  |  R2  |      |      |    |
 * |---+------+------+------+------+------+------+----|    |----+------+------+------+------+------+------+----|
 * |   |      |  ←  |  ↓  |  →  |      |  (G) | SEL|    |STRT|      |   Y  |   B  |   A  |      |      |    |
 * `---+------+------+------+------+------+------+----'    `----+------+------+------+------+------+------+----'
 *     |      |      |      |      |      |      |              |      |      |      |      |      |      |
 *     `-----------------------------------------'              `-----------------------------------------'
 *            WASD = 十字キー (ハット)、矢印キー = 左スティック
 *            Q/E = L/R、U/O = L2/R2、TAB = Select、ENT = Start
 *            I/J/K/L = X/Y/B/A (SNES の菱形配置: 上 X, 左 Y, 下 B, 右 A)
 */
const keycode_t keymaps[NUM_LAYERS][MATRIX_ROWS][MATRIX_COLS] = {

    [LAYER_BASE] = LAYOUT(
             KC_RBRC, KC_1,    KC_2,   KC_END,  KC_3,    KC_4,    KC_5,   KC_CAPS,                  KC_KANA,          KC_6,    KC_7,    KC_8,   KC_LEFT, KC_9,    KC_0,   KC_MINS,
    KC_ESC,  KC_NUHS, KC_Q,    KC_W,   KC_PGDN, KC_E,    KC_R,    KC_T,   KC_MO(1), KC_DEL, KC_INS, KC_NO,            KC_Y,    KC_U,    KC_I,   KC_DOWN, KC_O,    KC_P,   KC_LBRC, KC_JYEN,
    KC_ZKHK, KC_LALT, KC_A,    KC_S,   KC_HOME, KC_D,    KC_F,    KC_G,   KC_LSFT,  KC_TAB, KC_ENT, KC_RSFT,          KC_H,    KC_J,    KC_K,   KC_RGHT, KC_L,    KC_SCLN,KC_QUOT, KC_EQL,
             KC_LCTL, KC_Z,    KC_X,   KC_PGUP, KC_C,    KC_V,    KC_B,   KC_SPC,                   KC_BSPC,          KC_N,    KC_M,    KC_COMM,KC_UP,   KC_DOT,  KC_SLSH,KC_RO
    ),

    /* Function layer (MO(1))
     * FN+Q = BLE_PAIR (ペアリングモード), FN+G = ゲームパッドレイヤーの切り替え
     */
    [LAYER_FN] = LAYOUT(
             ___,     KC_F1,   KC_F2,  ___,     KC_F3,   KC_F4,   KC_F5,  ___,                      ___,              KC_F6,   KC_F7,   KC_F8,  ___,     KC_F9,   KC_F10, ___,
    ___,     ___,   BLE_PAIR,  ___,    ___,     ___,     ___,     ___,    ___,      ___,    ___,     ___,              ___,     ___,     ___,    ___,     ___,     ___,    ___,     ___,
    KC_LGUI, ___,     ___,     ___,    ___,     ___,     ___,     PAD_TG, ___,      ___,    ___,     ___,              ___,     ___,     ___,    ___,     ___,     ___,    ___,     ___,
             ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,                       ___,              ___,     ___,     ___,    ___,     ___,     ___,    ___
    ),

    /* Gamepad layer (TG(2)) */
    [LAYER_PAD] = LAYOUT(
             ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,                       ___,              ___,     ___,     ___,    JS_LS_LEFT, ___,  ___,    ___,
    ___,     ___,     JS_L,    JS_UP,  ___,     JS_R,    ___,     ___,    ___,      ___,    ___,     ___,              ___,     JS_L2,   JS_X,   JS_LS_DOWN, JS_R2, ___,   ___,     ___,
    ___,     ___,     JS_LEFT, JS_DOWN,___,     JS_RGHT, ___,     ___,    ___,  JS_SELECT, JS_START, ___,              ___,     JS_Y,    JS_B,   JS_LS_RGHT, JS_A,  ___,   ___,     ___,
             ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,                       ___,              ___,     ___,     ___,    JS_LS_UP, ___,   ___,    ___
    ),
};
