/*
 * kb_process.c — Key processing pipeline
 *
 * Converts debounced matrix state → layer resolution → HID report
 */

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <string.h>
#include "kb_process.h"

/*----------------------------------------------------------------------
 * Previous matrix state for edge detection (TG needs press edge).
 * Thread-safety: accessed only from KB scanner task (single-threaded).
 */
LOCAL T_MATRIX_STATE prev_left;
LOCAL T_MATRIX_STATE prev_right;

/*----------------------------------------------------------------------
 * XMK 長押し検出 (3 秒ホールドで発火、誤操作防止)
 */
#define XMK_HOLD_THRESHOLD  (3000 / SCAN_PERIOD_MS)  /* 3秒 = 1500 scans @ 2ms */
LOCAL keycode_t xmk_hold_code = 0;     /* 現在押下中の XMK キーコード */
LOCAL UINT      xmk_hold_count = 0;    /* ホールドカウンタ */
LOCAL BOOL      xmk_hold_fired = FALSE; /* 発火済みフラグ (1回だけ発火) */

/*
 * Check if key was just pressed (transition from 0→1)
 */
LOCAL BOOL key_just_pressed(UINT row, UINT col,
                            const T_MATRIX_STATE *cur_l, const T_MATRIX_STATE *cur_r)
{
    BOOL now, was;
    if (row < MATRIX_ROWS_PER_HAND) {
        now = (cur_l->rows[row] >> col) & 1;
        was = (prev_left.rows[row] >> col) & 1;
    } else {
        UINT sr = row - MATRIX_ROWS_PER_HAND;
        now = (cur_r->rows[sr] >> col) & 1;
        was = (prev_right.rows[sr] >> col) & 1;
    }
    return (now && !was);
}

/*
 * Check if key is currently pressed
 */
LOCAL BOOL key_pressed(UINT row, UINT col,
                       const T_MATRIX_STATE *left, const T_MATRIX_STATE *right)
{
    if (row < MATRIX_ROWS_PER_HAND) {
        return (left->rows[row] >> col) & 1;
    } else {
        return (right->rows[row - MATRIX_ROWS_PER_HAND] >> col) & 1;
    }
}

/*
 * Lookup keycode with layer transparency
 */
LOCAL keycode_t lookup_keycode(UINT row, UINT col, UB active_layers)
{
    INT layer;

    /* Search from highest active layer down to base */
    for (layer = NUM_LAYERS - 1; layer >= 0; layer--) {
        if (!(active_layers & (1 << layer))) continue;
        keycode_t kc = keymaps[layer][row][col];
        if (kc != KC_TRNS) return kc;
    }
    return KC_NO;
}

/*----------------------------------------------------------------------
 * Gamepad (JS_* keycodes)
 */

/* Direction bits of the d-pad and of each stick */
#define JS_DIR_UP       0x01
#define JS_DIR_DOWN     0x02
#define JS_DIR_LEFT     0x04
#define JS_DIR_RIGHT    0x08
#define JS_AXIS_MAX     127

void kb_pad_report_clear(T_HID_PAD_REPORT *pad)
{
    memset(pad, 0, sizeof(*pad));
    pad->hat = HID_PAD_HAT_CENTER;
}

/* Direction bits -> hat position (0 up, clockwise to 7 up-left), 8 centred.
 * Opposite directions held together cancel out. */
LOCAL UB pad_hat(UB dir)
{
    BOOL up    = (dir & JS_DIR_UP)    && !(dir & JS_DIR_DOWN);
    BOOL down  = (dir & JS_DIR_DOWN)  && !(dir & JS_DIR_UP);
    BOOL left  = (dir & JS_DIR_LEFT)  && !(dir & JS_DIR_RIGHT);
    BOOL right = (dir & JS_DIR_RIGHT) && !(dir & JS_DIR_LEFT);

    if (up)    return right ? 1 : left ? 7 : 0;
    if (down)  return right ? 3 : left ? 5 : 4;
    if (right) return 2;
    if (left)  return 6;
    return HID_PAD_HAT_CENTER;
}

/* One axis from the two direction bits pulling it: -127, 0 or 127 */
LOCAL B pad_axis(UB dir, UB neg, UB pos)
{
    if ((dir & neg) && !(dir & pos)) return -JS_AXIS_MAX;
    if ((dir & pos) && !(dir & neg)) return JS_AXIS_MAX;
    return 0;
}

/* Build the gamepad report from the JS_* keys held (arg of each key) */
typedef struct {
    UW buttons;
    UB dpad, lstick, rstick;
} T_PAD_KEYS;

LOCAL void pad_key(T_PAD_KEYS *k, UH arg)
{
    if (arg < KC_JS_ARG_DPAD) {
        k->buttons |= 1UL << arg;
    } else if (arg < KC_JS_ARG_STICK) {
        k->dpad |= (UB)(1 << (arg - KC_JS_ARG_DPAD));
    } else if (arg < KC_JS_ARG_STICK + 4) {
        k->lstick |= (UB)(1 << (arg - KC_JS_ARG_STICK));
    } else if (arg < KC_JS_ARG_STICK + 8) {
        k->rstick |= (UB)(1 << (arg - KC_JS_ARG_STICK - 4));
    }
}

LOCAL void pad_build(T_HID_PAD_REPORT *pad, const T_PAD_KEYS *k)
{
    pad->buttons[0] = (UB)(k->buttons);
    pad->buttons[1] = (UB)(k->buttons >> 8);
    pad->buttons[2] = (UB)(k->buttons >> 16);
    pad->buttons[3] = (UB)(k->buttons >> 24);
    pad->hat = pad_hat(k->dpad);
    pad->axis[0] = pad_axis(k->lstick, JS_DIR_LEFT, JS_DIR_RIGHT);   /* X  */
    pad->axis[1] = pad_axis(k->lstick, JS_DIR_UP,   JS_DIR_DOWN);    /* Y  */
    pad->axis[2] = pad_axis(k->rstick, JS_DIR_LEFT, JS_DIR_RIGHT);   /* Z  */
    pad->axis[3] = pad_axis(k->rstick, JS_DIR_UP,   JS_DIR_DOWN);    /* Rz */
}

/*
 * Initialize keyboard state
 */
void kb_process_init(T_KB_STATE *state)
{
    memset(state, 0, sizeof(T_KB_STATE));
    memset(&prev_left, 0, sizeof(prev_left));
    memset(&prev_right, 0, sizeof(prev_right));
    state->active_layers = (1 << LAYER_BASE);
    kb_pad_report_clear(&state->pad);
}

/*
 * Process all keys and build HID report
 */
BOOL kb_process_keys(T_KB_STATE *state,
                     const T_MATRIX_STATE *left,
                     const T_MATRIX_STATE *right)
{
    INT r, c;
    UB modifier = 0;
    UB keycodes[6];
    INT kc_idx = 0;
    UB new_momentary = 0;
    T_PAD_KEYS pad_keys;

    memset(keycodes, 0, sizeof(keycodes));
    memset(&pad_keys, 0, sizeof(pad_keys));

    state->xmk_pending = 0;
    keycode_t xmk_found = 0;

    /* First pass: detect MO/TG/XMK actions to determine active layers */
    for (r = 0; r < MATRIX_ROWS; r++) {
        for (c = 0; c < MATRIX_COLS; c++) {
            if (!key_pressed(r, c, left, right)) continue;

            keycode_t kc = lookup_keycode(r, c, state->active_layers);

            if (KC_IS_ACTION(kc)) {
                UH action = KC_ACTION_TYPE(kc);
                UH arg = KC_ACTION_ARG(kc);

                if (action == KC_ACTION_MO) {
                    /* Momentary: active while held */
                    new_momentary |= (1 << arg);
                } else if (action == KC_ACTION_TG) {
                    /* Toggle: flip layer on press EDGE only (0→1 transition) */
                    if (key_just_pressed(r, c, left, right)) {
                        state->toggled_layers ^= (1 << arg);
                    }
                } else if (action == KC_ACTION_XMK) {
                    /* XMK: 3 秒長押しで発火 (誤操作防止) */
                    xmk_found = kc;
                }
            }
        }
    }

    /* XMK 長押し判定 */
    if (xmk_found != 0 && xmk_found == xmk_hold_code) {
        /* 同じ XMK キー継続押下 → カウンタ加算 */
        if (!xmk_hold_fired) {
            xmk_hold_count++;
            if (xmk_hold_count >= XMK_HOLD_THRESHOLD) {
                state->xmk_pending = xmk_found;
                xmk_hold_fired = TRUE;  /* 1 回だけ発火 */
            }
        }
    } else {
        /* XMK キー変更 or リリース → リセット */
        xmk_hold_code = xmk_found;
        xmk_hold_count = (xmk_found != 0) ? 1 : 0;
        xmk_hold_fired = FALSE;
    }

    /* Update momentary layers */
    state->momentary_layers = new_momentary;

    /* Recompute active layers */
    state->active_layers = (1 << LAYER_BASE)
                         | state->momentary_layers
                         | state->toggled_layers;

    /* Second pass: collect keycodes and modifiers */
    for (r = 0; r < MATRIX_ROWS; r++) {
        for (c = 0; c < MATRIX_COLS; c++) {
            if (!key_pressed(r, c, left, right)) continue;

            keycode_t kc = lookup_keycode(r, c, state->active_layers);

            if (kc != KC_TRNS && KC_ACTION_TYPE(kc) == KC_ACTION_JS) {
                pad_key(&pad_keys, KC_ACTION_ARG(kc));
                continue;
            }
            if (KC_IS_ACTION(kc)) continue;
            if (kc == KC_NO || kc == KC_TRNS) continue;

            UB hid_kc = (UB)(kc & 0xFF);

            if (KC_IS_MODIFIER(hid_kc)) {
                modifier |= (1 << (hid_kc - 0xE0));
            } else {
                if (kc_idx < 6) {
                    keycodes[kc_idx++] = hid_kc;
                }
            }
        }
    }

    /* Save current matrix state for next scan's edge detection */
    prev_left = *left;
    prev_right = *right;

    pad_build(&state->pad, &pad_keys);

    /* Build HID report */
    state->report.modifier = modifier;
    state->report.reserved = 0;
    memcpy(state->report.keycode, keycodes, 6);

    /* Check if report changed */
    BOOL changed = (memcmp(&state->report, &state->prev,
                          sizeof(T_HID_KBD_REPORT)) != 0);

    if (changed) {
        state->prev = state->report;
    }

    return changed;
}
