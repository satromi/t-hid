/*
 * keymaps/dvorak/keymap.c — Dvorak keymap for mintlsplit 72-key
 *
 * ★ User-editable file
 * Build with: make KEYMAP=dvorak all
 */

#include "kb_keymap.h"

#define ___ KC_TRNS

/* XMK extended actions (firmware internal) */
#define BLE_PAIR  XMK_BLE_PAIR   /* FN+Q: BLE ペアリングモード */

/*
 * Dvorak
 *     ,-----------------------------------------.              ,-----------------------------------------.
 *     |   [  |   1  |   2  |   3  |   4  |   5  |              |   6  |   7  |   8  |   9  |   0  |   /  |
 * ,---+------+------+------+------+------+------+----.    ,----+------+------+------+------+------+------+----.
 * | @ |   ]  |   ;  |   ,  |   .  |   P  |   Y  | ESC|    |INS |   F  |   G  |   C  |   R  |   L  |   -  |Yen |
 * |---+------+------+------+------+------+------+----|    |----+------+------+------+------+------+------+----|
 * |HNK| ALT  |   A  |   O  |   E  |   U  |   I  | TAB|    |ENT |   D  |   H  |   T  |   N  |   S  |   :  |DEL |
 * `---+------+------+------+------+------+------+----'    `----+------+------+------+------+------+------+----'
 *     |CTRL  |   ~  |   Q  |   J  |   K  |   X  |              |   B  |   M  |   W  |   V  |   Z  |   \  |
 *     `-----------------------------------------'              `-----------------------------------------'
 *            ,------.     ,-------------------------.  ,-------------------------.     ,------.
 *            | PGUP |     | SPC  | SFT  |  TG | CPS|  | KANA|TEAMS| SFT  | BSP  |     |  UP  |
 *     +------+------+-----+-------------------------'  `-------------------------+-----+------+-----+
 *     | HOME | PGDN | END |                                                      | LFT | DWN  | RGT |
 *     `-------------------'                                                      `-------------------'
 */
/*
 * カーソル/矢印クラスタは他の列と列順が異なる。
 * 基板上でクラスタの 4 キーが繋がっている列線の順が
 *   左手 行4  : col0=END,  col1=PGDN, col2=HOME, col3=PGUP
 *   右手 行15 : col0=左,   col1=下,   col2=右,   col3=上
 * であるため、LAYOUT の該当位置もその順で書く。
 */
const keycode_t keymaps[NUM_LAYERS][MATRIX_ROWS][MATRIX_COLS] = {

    [LAYER_BASE] = LAYOUT(
             KC_RBRC, KC_1,    KC_2,   KC_END,  KC_3,    KC_4,    KC_5,   KC_CAPS,                  KC_KANA,          KC_6,    KC_7,    KC_8,   KC_LEFT, KC_9,    KC_0,   KC_SLSH,
    KC_LBRC, KC_NUHS, KC_SCLN, KC_COMM,KC_PGDN, KC_DOT,  KC_P,    KC_Y,   KC_TG(1), KC_ESC, KC_INS, KC_NO,            KC_F,    KC_G,    KC_C,   KC_DOWN, KC_R,    KC_L,   KC_MINS, KC_JYEN,
    KC_ZKHK, KC_LALT, KC_A,    KC_O,   KC_HOME, KC_E,    KC_U,    KC_I,   KC_LSFT,  KC_TAB, KC_ENT, KC_RSFT,          KC_D,    KC_H,    KC_T,   KC_RGHT, KC_N,    KC_S,   KC_QUOT, KC_DEL,
             KC_LCTL, KC_EQL,  KC_Q,   KC_PGUP, KC_J,    KC_K,    KC_X,   KC_SPC,                   KC_BSPC,          KC_B,    KC_M,    KC_W,   KC_UP,   KC_V,    KC_Z,   KC_RO
    ),

    /* Function layer (TG(1))
     * FN+Q = BLE_PAIR (ペアリングモード)
     * Dvorak の Q は Base の L10 位置 (左手3段目2列目)
     */
    [LAYER_FN] = LAYOUT(
             ___,     KC_F1,   KC_F2,  ___,     KC_F3,   KC_F4,   KC_F5,  ___,                      ___,              KC_F6,   KC_F7,   KC_F8,  ___,     KC_F9,   KC_F10, ___,
    ___,     ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,      ___,    ___,     ___,              ___,     ___,     ___,    ___,     ___,     ___,    ___,     ___,
    KC_LGUI, ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,      ___,    ___,     ___,              ___,     ___,     ___,    ___,     ___,     ___,    ___,     ___,
             ___,     ___,   BLE_PAIR, ___,     ___,     ___,     ___,    ___,                       ___,              ___,     ___,     ___,    ___,     ___,     ___,    ___
    ),

    /* Numpad layer (for future use) */
    [LAYER_NUM] = LAYOUT(
             ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,                       ___,              ___,     KC_P7,   KC_P8,  ___,     KC_P9,   KC_PMNS,___,
    ___,     ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,      ___,    ___,     ___,              ___,     KC_P4,   KC_P5,  ___,     KC_P6,   KC_PPLS,___,     ___,
    ___,     ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,      ___,    ___,     ___,              ___,     KC_P1,   KC_P2,  ___,     KC_P3,   KC_PENT,___,     ___,
             ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,                       ___,              ___,     KC_P0,   KC_PDOT,___,     ___,     ___,    ___
    ),
};
