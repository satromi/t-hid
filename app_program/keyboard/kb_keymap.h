/*
 * kb_keymap.h — Keymap definitions and LAYOUT macro
 *
 * mintlsplit (72 keys) layout: 10 rows × 4 cols per hand
 */

#ifndef __KB_KEYMAP_H__
#define __KB_KEYMAP_H__

#include <tk/typedef.h>
#include "kb_config.h"
#include "kb_hid_keycodes.h"

/* 16-bit keycode type (supports action keycodes above 0xFF) */
typedef UH keycode_t;

/* Number of layers */
#define NUM_LAYERS  3
#define LAYER_BASE  0
#define LAYER_FN    1
#define LAYER_NUM   2

/*
 * LAYOUT macro — mintlsplit 72-key physical layout → matrix[20][4]
 *
 * Physical layout:
 *        ,-----------------------------------------.              ,-----------------------------------------.
 *        | L03  | L07  | L11  | L15  | L19  | L23  |              | R03  | R07  | R11  | R15  | R19  | R23  |
 * ,------+------+------+------+------+------+------+------.,------+------+------+------+------+------+------+------.
 * | L01  | L04  | L08  | L12  | L16  | L20  | L24  | L28  || L32  | L35  | R04  | R08  | R12  | R16  | R20  | R24  |
 * |------+------+------+------+------+------+------+------||------+------+------+------+------+------+------+------|
 * | L02  | L05  | L09  | L13  | L17  | L21  | L25  | L29  || L33  | L36  | R05  | R09  | R13  | R17  | R21  | R25  |
 * `------+------+------+------+------+------+------+------'`------+------+------+------+------+------+------+------'
 *        | L06  | L10  | L14  | L18  | L22  | L26  | L30  |       | L34  | R06  | R10  | R14  | R18  | R22  | R26  |
 *        `-----------------------------------------'              `-----------------------------------------'
 *
 * Matrix mapping (same as mintlsplit.h):
 *   Row 0: { ___,  L01, L02, ___  }    Row 10: { ___,  R01, R02, ___  }
 *   Row 1: { L03, L04, L05, L06 }    Row 11: { R03, R04, R05, R06 }
 *   Row 2: { L07, L08, L09, L10 }    Row 12: { R07, R08, R09, R10 }
 *   Row 3: { L11, L12, L13, L14 }    Row 13: { R11, R12, R13, R14 }
 *   Row 4: { L15, L16, L17, L18 }    Row 14: { R15, R16, R17, R18 }
 *   Row 5: { L19, L20, L21, L22 }    Row 15: { R19, R20, R21, R22 }
 *   Row 6: { L23, L24, L25, L26 }    Row 16: { R23, R24, R25, R26 }
 *   Row 7: { L27, L28, L29, L30 }    Row 17: { R27, R28, R29, R30 }
 *   Row 8: { L31, L32, L33, L34 }    Row 18: { R31, R32, R33, R34 }
 *   Row 9: { ___,  L35, L36, ___  }    Row 19: { ___,  R35, R36, ___  }
 */
#define _x_ KC_NO

#define LAYOUT( \
         L03, L07, L11, L15, L19, L23, L27, L31,               R03, R07, R11, R15, R19, R23, R27, R31,      \
    L01, L04, L08, L12, L16, L20, L24, L28, L32, L35,     R01, R04, R08, R12, R16, R20, R24, R28, R32, R35, \
    L02, L05, L09, L13, L17, L21, L25, L29, L33, L36,     R02, R05, R09, R13, R17, R21, R25, R29, R33, R36, \
         L06, L10, L14, L18, L22, L26, L30, L34,               R06, R10, R14, R18, R22, R26, R30, R34       \
) {                                                       \
    { _x_, L01, L02, _x_ }, \
    { L03, L04, L05, L06 }, \
    { L07, L08, L09, L10 }, \
    { L11, L12, L13, L14 }, \
    { L15, L16, L17, L18 }, \
    { L19, L20, L21, L22 }, \
    { L23, L24, L25, L26 }, \
    { L27, L28, L29, L30 }, \
    { L31, L32, L33, L34 }, \
    { _x_, L35, L36, _x_ }, \
    { _x_, R01, R02, _x_ }, \
    { R03, R04, R05, R06 }, \
    { R07, R08, R09, R10 }, \
    { R11, R12, R13, R14 }, \
    { R15, R16, R17, R18 }, \
    { R19, R20, R21, R22 }, \
    { R23, R24, R25, R26 }, \
    { R27, R28, R29, R30 }, \
    { R31, R32, R33, R34 }, \
    { _x_, R35, R36, _x_ }  \
}

/* Layer keymap array (defined in kb_keymap.c) */
extern const keycode_t keymaps[NUM_LAYERS][MATRIX_ROWS][MATRIX_COLS];

#endif /* __KB_KEYMAP_H__ */
