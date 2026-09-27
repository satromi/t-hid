/*
 * test_keymap.c — kb_wasm_test 用キーマップ
 *
 * 既定のキーマップ (keymaps/default) に、検証で使うキーを追加したもの。
 *   ベース右手親指の空き (KC_NO) → TEAMS (Ctrl+Shift+M)
 *   FN レイヤーの右手 Y 位置      → TG(2)
 */
#include "kb_keymap.h"

#define ___ KC_TRNS

/* XMK extended actions (firmware internal) */
#define BLE_PAIR  XMK_BLE_PAIR   /* FN+Q: BLE ペアリングモード */

/* Shift+Ctrl combination helper */
#define LSFT_LCTL_KC(k)  (0xF000 | (k))  /* Action keycode: Shift+Ctrl+key */
#define TEAMS LSFT_LCTL_KC(KC_M)  /* Shift+Ctrl+M (Teams mute) */

/*
 * Qwerty
 *     ,-----------------------------------------.              ,-----------------------------------------.
 *     |   [  |   1  |   2  |   3  |   4  |   5  |              |   6  |   7  |   8  |   9  |   0  |   -  |
 * ,---+------+------+------+------+------+------+----.    ,----+------+------+------+------+------+------+----.
 * |ESC|   ]  |   Q  |   W  |   E  |   R  |   T  | DEL|    |INS |   Y  |   U  |   I  |   O  |   P  |   @  |Yen |
 * |---+------+------+------+------+------+------+----|    |----+------+------+------+------+------+------+----|
 * |HNK| ALT  |   A  |   S  |   D  |   F  |   G  | TAB|    |ENT |   H  |   J  |   K  |   L  |   ;  |   :  |  ~ |
 * `---+------+------+------+------+------+------+----'    `----+------+------+------+------+------+------+----'
 *     |CTRL  |   Z  |   X  |   C  |   V  |   B  |              |   N  |   M  |   ,  |   .  |   /  |   \  |
 *     `-----------------------------------------'              `-----------------------------------------'
 *            ,------.     ,-------------------------.  ,-------------------------.     ,------.
 *            | PGUP |     | SPC  | SFT  |  FN | CPS|  | KANA|TEAMS| SFT  | BSP  |     |  UP  |
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
             KC_RBRC, KC_1,    KC_2,   KC_END,  KC_3,    KC_4,    KC_5,   KC_CAPS,                  KC_KANA,          KC_6,    KC_7,    KC_8,   KC_LEFT, KC_9,    KC_0,   KC_MINS,
    KC_ESC,  KC_NUHS, KC_Q,    KC_W,   KC_PGDN, KC_E,    KC_R,    KC_T,   KC_MO(1), KC_DEL, KC_INS, TEAMS,            KC_Y,    KC_U,    KC_I,   KC_DOWN, KC_O,    KC_P,   KC_LBRC, KC_JYEN,
    KC_ZKHK, KC_LALT, KC_A,    KC_S,   KC_HOME, KC_D,    KC_F,    KC_G,   KC_LSFT,  KC_TAB, KC_ENT, KC_RSFT,          KC_H,    KC_J,    KC_K,   KC_RGHT, KC_L,    KC_SCLN,KC_QUOT, KC_EQL,
             KC_LCTL, KC_Z,    KC_X,   KC_PGUP, KC_C,    KC_V,    KC_B,   KC_SPC,                   KC_BSPC,          KC_N,    KC_M,    KC_COMM,KC_UP,   KC_DOT,  KC_SLSH,KC_RO
    ),

    /* Function layer (MO(1))
     * FN+Q = BLE_PAIR (ペアリングモード)
     */
    [LAYER_FN] = LAYOUT(
             ___,     KC_F1,   KC_F2,  ___,     KC_F3,   KC_F4,   KC_F5,  ___,                      ___,              KC_F6,   KC_F7,   KC_F8,  ___,     KC_F9,   KC_F10, ___,
    ___,     ___,   BLE_PAIR,  ___,    ___,     ___,     ___,     ___,    ___,      ___,    ___,     ___,              KC_TG(2),     ___,     ___,    ___,     ___,     ___,    ___,     ___,
    KC_LGUI, ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,      ___,    ___,     ___,              ___,     ___,     ___,    ___,     ___,     ___,    ___,     ___,
             ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,                       ___,              ___,     ___,     ___,    ___,     ___,     ___,    ___
    ),

    /* Numpad layer (for future use) */
    [LAYER_NUM] = LAYOUT(
             ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,                       ___,              ___,     KC_P7,   KC_P8,  ___,     KC_P9,   KC_PMNS,___,
    ___,     ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,      ___,    ___,     ___,              ___,     KC_P4,   KC_P5,  ___,     KC_P6,   KC_PPLS,___,     ___,
    ___,     ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,      ___,    ___,     ___,              ___,     KC_P1,   KC_P2,  ___,     KC_P3,   KC_PENT,___,     ___,
             ___,     ___,     ___,    ___,     ___,     ___,     ___,    ___,                       ___,              ___,     KC_P0,   KC_PDOT,___,     ___,     ___,    ___
    ),
};
