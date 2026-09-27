/*
 * keymaps/matrixtest/keymap.c — Matrix probe keymap
 *
 * 全セルに重複しないキーを割り当てた、配線調査専用のキーマップ。
 * LAYOUT マクロを通さずマトリクスを直接定義しているので、
 * 通常の 72 キー配列で使われていないセルも区別できる。
 *
 * 1 セル = 1 文字なので、キーを順に押して出力された文字列を読めば
 * 「どの物理キーがマトリクスのどのセルに繋がっているか」が確定する。
 *
 * 文字の並び (行内は col 0→3):
 *   左手 行0-9 : a b c d / e f g h / i j k l / m n o p / q r s t
 *                u v w x / y z 0 1 / 2 3 4 5 / 6 7 8 9 / - ^ @ [
 *   右手 行10-19: 同じ並びを繰り返す
 *
 * 左右どちらの手も同じ文字列になるが、KB_MATRIX_HAND_R で
 * 片手だけをスキャンするビルドに使うため衝突しない。
 */

#include "kb_keymap.h"

/* 行ごとの 4 セル分を並べたマクロ */
#define ROW_ABCD  { KC_A,    KC_B,    KC_C,    KC_D    }
#define ROW_EFGH  { KC_E,    KC_F,    KC_G,    KC_H    }
#define ROW_IJKL  { KC_I,    KC_J,    KC_K,    KC_L    }
#define ROW_MNOP  { KC_M,    KC_N,    KC_O,    KC_P    }
#define ROW_QRST  { KC_Q,    KC_R,    KC_S,    KC_T    }
#define ROW_UVWX  { KC_U,    KC_V,    KC_W,    KC_X    }
#define ROW_YZ01  { KC_Y,    KC_Z,    KC_0,    KC_1    }
#define ROW_2345  { KC_2,    KC_3,    KC_4,    KC_5    }
#define ROW_6789  { KC_6,    KC_7,    KC_8,    KC_9    }
#define ROW_SYMS  { KC_MINS, KC_EQL,  KC_LBRC, KC_RBRC }

#define PROBE_HALF \
    ROW_ABCD, \
    ROW_EFGH, \
    ROW_IJKL, \
    ROW_MNOP, \
    ROW_QRST, \
    ROW_UVWX, \
    ROW_YZ01, \
    ROW_2345, \
    ROW_6789, \
    ROW_SYMS

const keycode_t keymaps[NUM_LAYERS][MATRIX_ROWS][MATRIX_COLS] = {
    [LAYER_BASE] = {
        PROBE_HALF,     /* 行 0-9   左手 */
        PROBE_HALF,     /* 行 10-19 右手 */
    },
};
