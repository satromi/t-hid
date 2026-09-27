/*
 * kb_matrix.c — Key matrix scanner with debounce
 *
 * Scans a COL2ROW or ROW2COL matrix using GPIO.
 * Row pins are driven as outputs, column pins are read as inputs with pull-up.
 */

#include <tk/tkernel.h>
#include <bsp/libbsp.h>
#include <sys/sysdef.h>
#include "kb_matrix.h"
#include "kb_config.h"

/*
 * Local pin arrays (initialized from config macros)
 *
 * 左右で GPIO 割当が異なるため、ビルド対象の手に応じて配列を選ぶ。
 * 担当する手は kb_config.h の KB_MATRIX_HAND_R で決まる。
 */
#if KB_MATRIX_HAND_R
LOCAL const UINT row_pins[] = ROW_PINS_R;
LOCAL const UINT col_pins[] = COL_PINS_R;
#else
LOCAL const UINT row_pins[] = ROW_PINS_L;
LOCAL const UINT col_pins[] = COL_PINS_L;
#endif

/*
 * Initialize GPIO pins for matrix scanning
 */
void matrix_init(void)
{
    INT i;

#if defined(KB_MATRIX_NONE)
    /* マトリクスを読まない (ピンを別の用途に使う基板での試験用) */
    return;
#endif

    /* Row pins: output, initially HIGH (inactive for COL2ROW) */
    for (i = 0; i < MATRIX_ROWS_PER_HAND; i++) {
        gpio_set_pin(row_pins[i], GPIO_MODE_OUT);
        gpio_set_val(row_pins[i], 1);
    }

    /*
     * Column pins: input with pull-up
     * KB_COL_INIT() は sysdepend/TARGET_DIR/kb_config_hw.h で定義
     */
    for (i = 0; i < MATRIX_COLS_PER_HAND; i++) {
        KB_COL_INIT(col_pins[i]);
    }
}

/*
 * Scan the local matrix
 *
 * COL2ROW: Drive each row LOW, read columns.
 *   Pressed key pulls column LOW through diode.
 */
void matrix_scan(T_MATRIX_STATE *raw)
{
    INT r, c;

#if defined(KB_MATRIX_NONE)
    (void)raw;
    return;
#endif

    for (r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
#if DIODE_DIRECTION == COL2ROW
        /* Drive row LOW (active) */
        gpio_set_val(row_pins[r], 0);
#else
        /* ROW2COL: Drive row HIGH */
        gpio_set_val(row_pins[r], 1);
#endif

        /*
         * Settling delay: ~800ns busy-wait at 125MHz.
         *
         * 意図的なビジーウェイト: GPIO の RC 過渡応答 (数百 ns) を待つ。
         * tk_dly_tsk() の最小粒度 (1ms) より遥かに短いため、
         * RTOS のタスク切替ではなくハードウェア的な待ちが必要。
         * 800ns は 1 スキャン周期 (2ms) の 0.04% なので影響は無視できる。
         *
         * RC 定数: 10pF × 4.7kΩ = 47ns (外部プルアップ)
         *          10pF × 60kΩ  = 600ns (内蔵プルアップのみ)
         */
        for (volatile INT d = 0; d < 100; d++) {}

        /* Read columns */
        UB row_state = 0;
        for (c = 0; c < MATRIX_COLS_PER_HAND; c++) {
#if DIODE_DIRECTION == COL2ROW
            /* COL2ROW: pressed = LOW (pulled down through diode) */
            if (gpio_get_val(col_pins[c]) == 0) {
                row_state |= (1 << c);
            }
#else
            /* ROW2COL: pressed = HIGH */
            if (gpio_get_val(col_pins[c]) != 0) {
                row_state |= (1 << c);
            }
#endif
        }
        raw->rows[r] = row_state;

#if DIODE_DIRECTION == COL2ROW
        /* Restore row to HIGH (inactive) */
        gpio_set_val(row_pins[r], 1);
#else
        gpio_set_val(row_pins[r], 0);
#endif
    }
}

/*
 * Debounce filter (per-key counter algorithm)
 *
 * Each key has a counter. When raw state differs from debounced state,
 * counter increments each scan. When counter reaches threshold, state changes.
 * If raw matches debounced, counter resets.
 *
 * With DEBOUNCE_MS=5, SCAN_PERIOD_MS=10:
 *   Threshold = max(1, ceil(5/10)) = 1 scan = 10ms actual debounce.
 * With DEBOUNCE_MS=20, SCAN_PERIOD_MS=10:
 *   Threshold = ceil(20/10) = 2 scans = 20ms actual debounce.
 */
void matrix_debounce(T_DEBOUNCE_STATE *db, const T_MATRIX_STATE *raw)
{
    INT r, c;

    /* Ceiling division: (a + b - 1) / b */
    UB debounce_threshold = (DEBOUNCE_MS + SCAN_PERIOD_MS - 1) / SCAN_PERIOD_MS;
    if (debounce_threshold < 1) debounce_threshold = 1;

    /* Save previous state */
    db->previous = db->current;

    for (r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
        for (c = 0; c < MATRIX_COLS_PER_HAND; c++) {
            BOOL raw_pressed = (raw->rows[r] >> c) & 1;
            BOOL cur_pressed = (db->current.rows[r] >> c) & 1;

            if (raw_pressed != cur_pressed) {
                db->counter[r][c]++;
                if (db->counter[r][c] >= debounce_threshold) {
                    /* Accept state change */
                    if (raw_pressed) {
                        db->current.rows[r] |= (1 << c);
                    } else {
                        db->current.rows[r] &= ~(1 << c);
                    }
                    db->counter[r][c] = 0;
                }
            } else {
                db->counter[r][c] = 0;
            }
        }
    }
}

/*
 * Check if debounced state changed
 */
BOOL matrix_changed(const T_DEBOUNCE_STATE *db)
{
    INT r;
    for (r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
        if (db->current.rows[r] != db->previous.rows[r]) {
            return TRUE;
        }
    }
    return FALSE;
}
