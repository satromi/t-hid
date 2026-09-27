/*
 * kb_process.h — Key processing pipeline API
 */

#ifndef __KB_PROCESS_H__
#define __KB_PROCESS_H__

#include <tk/tkernel.h>
#include "kb_config.h"
#include "kb_matrix.h"
#include "kb_keymap.h"
#include "kb_output.h"

/* Keyboard state */
typedef struct {
    UB active_layers;              /* Bitmask of active layers */
    UB momentary_layers;           /* Layers held via MO() */
    UB toggled_layers;             /* Layers toggled via TG() */
    UH xmk_pending;               /* Pending XMK action (0=none, press edge) */
    T_HID_KBD_REPORT report;      /* Current HID report */
    T_HID_KBD_REPORT prev;        /* Previous report (change detection) */
    T_HID_PAD_REPORT pad;         /* Current gamepad report (JS_* keys) */
} T_KB_STATE;

/* Initialize keyboard state */
void kb_process_init(T_KB_STATE *state);

/*
 * Process matrix states into HID report
 * Returns TRUE if report changed (needs USB send)
 *
 * state->pad is rebuilt on every call; the caller compares it with the
 * last gamepad report it managed to send, so a report the host did not
 * take yet is sent again on the next scan.
 */
BOOL kb_process_keys(T_KB_STATE *state,
                     const T_MATRIX_STATE *left,
                     const T_MATRIX_STATE *right);

/*----------------------------------------------------------------------
 * 外部アクセッサ (MCP から呼ぶ)
 *
 * マスター側の kb_state (kb_main.c の static) に対するバイト幅の読み書き。
 * Cortex-M0+ の単一バイト read/write はアトミックなのでロック不要。
 */

/* 現在アクティブな最上位レイヤー番号を返す (0..NUM_LAYERS-1) */
UB   kb_get_current_layer(void);

/* アクティブレイヤーのビットマスクを返す */
UB   kb_get_active_layers(void);

/* TG() トグルレイヤーを on/off する (base 以外) */
void kb_set_toggled_layer(UB layer_num, BOOL on);

/* 全 TG() トグルを解除 (base のみアクティブに戻す) */
void kb_clear_toggled_layers(void);

/* ゲームパッドレポートを中立 (ボタンなし・ハット中立・軸 0) にする */
void kb_pad_report_clear(T_HID_PAD_REPORT *pad);

#endif /* __KB_PROCESS_H__ */
