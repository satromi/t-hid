/*
 * kb_wiring_test.c — Matrix wiring verification firmware
 *
 * キーマップを介さず、押されたキーのマトリクス座標を USB HID
 * キーボードとして文字入力する。テキストエディタを開いた状態で
 * 1 キーずつ押すと
 *
 *     R03C2
 *
 * のように「手・行・列」が 1 行ずつ入力されるため、ROW_PINS /
 * COL_PINS の並びが LAYOUT マクロのマトリクス行と一致しているかを
 * 実機で確認できる。行番号は手の中での添字 (0〜9) であり、
 * ROW_PINS_L / ROW_PINS_R の添字にそのまま対応する。
 *
 * スレーブ基板 (右手) を単体で USB 接続して調べる用途を想定し、
 * KB_MATRIX_HAND_R=1 を指定すれば master ビルド (USB 有効) のまま
 * 右手のピン割当でスキャンする。I2C スプリット通信は行わない。
 */

#include "kb_config.h"

#if defined(KB_WIRING_TEST)

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "kb_config.h"
#include "kb_common.h"
#include "kb_matrix.h"
#include "kb_output.h"
#include "kb_hid_keycodes.h"
#include "kb_wiring_test.h"

/* kb_main.c の周期ハンドラが立てるスキャン契機フラグ */
IMPORT ID scan_flgid;
#define SCAN_EVT_TICK		(1u << 0)

/*
 * キーストローク間隔
 *
 * 押下レポートと解放レポートをホストが別イベントとして取り込めるよう、
 * スキャン周期 (2ms) より十分長い間隔を空ける。
 */
#define TYPE_INTERVAL_MS	12

#if KB_MATRIX_HAND_R
#define HAND_KEYCODE		KC_R
#define HAND_NAME		"right"
#else
#define HAND_KEYCODE		KC_L
#define HAND_NAME		"left"
#endif

LOCAL T_DEBOUNCE_STATE	test_db;
LOCAL T_MATRIX_STATE	test_prev;

/*
 * 1 文字分のキーストローク (押下 → 解放) を送る
 */
LOCAL void type_key(UB keycode)
{
    T_HID_KBD_REPORT rep;

    memset(&rep, 0, sizeof(rep));
    rep.keycode[0] = keycode;
    kb_output_send(&rep);
    tk_dly_tsk(TYPE_INTERVAL_MS);

    memset(&rep, 0, sizeof(rep));
    kb_output_send(&rep);
    tk_dly_tsk(TYPE_INTERVAL_MS);
}

/*
 * 数字 0〜9 のキーコード
 * HID Usage 上は 1〜9 が連番で、0 はその後ろに置かれる。
 */
LOCAL UB digit_keycode(INT n)
{
    return (n == 0) ? (UB)KC_0 : (UB)(KC_1 + (n - 1));
}

/*
 * セル座標を "<L|R><行2桁>C<列1桁>" + 改行 で入力する
 */
LOCAL void type_cell(INT row, INT col)
{
    type_key(HAND_KEYCODE);
    type_key(digit_keycode(row / 10));
    type_key(digit_keycode(row % 10));
    type_key(KC_C);
    type_key(digit_keycode(col));
    type_key(KC_ENT);
}

/*
 * 配線チェックループ
 *
 * 押下エッジ (0→1) のみを出力するため、キーを押しっぱなしにしても
 * 座標は 1 回しか入力されない。
 */
void kb_wiring_test_loop(void)
{
    INT r, c;

    if (kb_output_init() != E_OK) {
        tm_printf((UB *)"KB: wiring test: no HID output, exit\n");
        tk_ext_tsk();
        return;
    }

    matrix_init();
    memset(&test_db, 0, sizeof(test_db));
    memset(&test_prev, 0, sizeof(test_prev));

    tm_printf((UB *)"KB: wiring test started (%s hand)\n", (UB *)HAND_NAME);

#if KB_LED_HEARTBEAT
    UW hb_last = kb_get_ms();
    KB_LED_ON();
#endif

    while (1) {
        UINT flgptn;
        T_MATRIX_STATE raw;

        tk_wai_flg(scan_flgid, SCAN_EVT_TICK,
                   TWF_ORW | TWF_BITCLR, &flgptn, TMO_FEVR);

#if KB_LED_HEARTBEAT
        kb_heartbeat_tick(&hb_last);
#endif

        matrix_scan(&raw);
        matrix_debounce(&test_db, &raw);

        for (r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
            UB now = test_db.current.rows[r];
            UB was = test_prev.rows[r];

            for (c = 0; c < MATRIX_COLS_PER_HAND; c++) {
                if (((now >> c) & 1) && !((was >> c) & 1)) {
                    type_cell(r, c);
                }
            }
            test_prev.rows[r] = now;
        }
    }
}

#endif /* KB_WIRING_TEST */
