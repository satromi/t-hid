/*
 * kb_split.c — Split keyboard control layer (Layer 3)
 *
 * QMK split_util.c equivalent.
 * Implements connection error throttling and master/slave orchestration.
 * Uses Layer 1 (split_transport) for I2C communication.
 *
 * RTOS 機能の活用:
 * - スレーブ: I2C RD_REQ イベントフラグでスキャン起動 (ポーリング不要)
 * - マスター: 再接続スロットリングを tk_dly_tsk() で実現
 */

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "kb_split.h"
#include "kb_common.h"
#include "split_transport.h"
#include "kb_matrix.h"

#if SPLIT_ENABLED

/*----------------------------------------------------------------------
 * Connection error state (QMK split_util.c equivalent)
 */
LOCAL UB connection_errors = 0;

BOOL split_is_connected(void)
{
    return (connection_errors < SPLIT_MAX_CONNECTION_ERRORS);
}

/*----------------------------------------------------------------------
 * Initialize split keyboard
 */
void split_init(void)
{
    connection_errors = 0;
    transport_init();
}

/*======================================================================
 * MASTER SIDE
 *====================================================================*/
#if KB_IS_MASTER

/*
 * Read slave matrix with connection error throttling
 *
 * 切断時のリトライ間隔を tk_dly_tsk() で制御する。
 * kb_get_ms() による手動タイムスタンプ比較ではなく、
 * RTOS のタスクスリープでスロットリングし CPU を解放する。
 */
LOCAL UW split_retry_last = 0;

BOOL split_master_read(matrix_row_t *slave_matrix)
{
    /*
     * 切断時: スキャンループをブロックせず、リトライ間隔をスキップで制御。
     * SPLIT_CONNECTION_CHECK_TIMEOUT (500ms) ごとに 1 回だけ読み出し試行。
     * 左手のキースキャンは常に 2ms 間隔で回り続ける。
     */
    if (!split_is_connected()) {
        UW now = 0;
        {
            SYSTIM tim;
            tk_get_otm(&tim);
            now = (UW)tim.lo;
        }
        if (now - split_retry_last < SPLIT_CONNECTION_CHECK_TIMEOUT) {
            memset(slave_matrix, 0, MATRIX_ROWS_PER_HAND);
            return FALSE;  /* リトライ間隔未到達 → スキップ */
        }
        split_retry_last = now;
    }

    /* I2C 読み出し試行 */
    ER err = transport_master_read_matrix(slave_matrix);

    if (err < E_OK) {
        if (connection_errors < 255) {
            connection_errors++;
        }
        if (connection_errors == SPLIT_MAX_CONNECTION_ERRORS) {
            tm_printf((UB *)"Split: disconnected\n");
        }
        memset(slave_matrix, 0, MATRIX_ROWS_PER_HAND);
        return FALSE;
    }

    /* 成功 — エラーカウンタクリア */
    if (connection_errors > 0) {
        tm_printf((UB *)"Split: reconnected\n");
        connection_errors = 0;
    }
    return TRUE;
}

/*======================================================================
 * SLAVE SIDE
 *====================================================================*/
#else /* !KB_IS_MASTER */

/*----------------------------------------------------------------------
 * Scan once: matrix scan → debounce → transport update
 */
LOCAL void split_slave_scan_once(T_DEBOUNCE_STATE *db)
{
    T_MATRIX_STATE raw;
    matrix_row_t matrix[MATRIX_ROWS_PER_HAND];
    INT r;

    matrix_scan(&raw);
    matrix_debounce(db, &raw);

    for (r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
        matrix[r] = db->current.rows[r];
    }

    transport_slave_update_matrix(matrix);
}

#if defined(SPLIT_TRANSPORT_BLE) || \
    (defined(SPLIT_TRANSPORT_AUTO) && defined(KB_OUTPUT_BLE))
/*----------------------------------------------------------------------
 * スレーブメインループ — BLE Notification 駆動
 *
 * BLE split: マトリクスを周期的にスキャンし、変化があれば
 * BLE Notification で Master に送信する。
 * I2C と異なり Master からのポーリングはないため、
 * 周期ハンドラ (kb_start で生成済み) の tick でスキャンする。
 */
extern ID scan_flgid;  /* kb_main.c で生成 */
#define SCAN_EVT_TICK  (1u << 0)

void split_slave_loop(void)
{
    T_DEBOUNCE_STATE db;
    UW loop_count = 0;
#if KB_LED_HEARTBEAT
    UW hb_last = kb_get_ms();
    KB_LED_ON();
#endif

    matrix_init();
    memset(&db, 0, sizeof(db));

    tm_printf((UB *)"Split: BLE slave scan loop started\n");

    while (1) {
        /* 周期ハンドラからの tick を待つ (SCAN_PERIOD_MS 間隔) */
        UINT flgptn;
        tk_wai_flg(scan_flgid, SCAN_EVT_TICK,
                   TWF_ORW | TWF_BITCLR, &flgptn, TMO_FEVR);

        split_slave_scan_once(&db);
        loop_count++;

#if KB_LED_HEARTBEAT
        kb_heartbeat_tick(&hb_last);
#endif
    }
}

#else /* I2C split */
/*----------------------------------------------------------------------
 * スレーブメインループ — I2C イベント駆動
 */
#include "dev_i2c_slave.h"

LOCAL void split_slave_debug_dump(UW loop_count)
{
    T_I2CS_STATS st;
    i2c_slave_get_stats(&st);
    tm_printf((UB *)"[SLAVE] loop=%d isr=%d rx=%d rd=%d stop=%d abrt=%d\n",
              loop_count, st.isr_count,
              st.rx_full_count, st.rd_req_count,
              st.stop_det_count, st.tx_abrt_count);
}

void split_slave_loop(void)
{
    T_DEBOUNCE_STATE db;
    UW loop_count = 0;
    ID i2cs_flg;
#if KB_LED_HEARTBEAT
    UW hb_last = kb_get_ms();
    KB_LED_ON();
#endif

    matrix_init();
    memset(&db, 0, sizeof(db));

    tm_printf((UB *)"Split: I2C slave scan loop started\n");
    KB_I2C_DEBUG_DUMP();

    i2cs_flg = i2c_slave_get_flgid();

    while (1) {
        UINT flgptn;
        tk_wai_flg(i2cs_flg, I2CS_EVT_WRITE_COMPLETE,
                   TWF_ORW | TWF_BITCLR, &flgptn,
                   SCAN_PERIOD_MS * 5);

        split_slave_scan_once(&db);
        loop_count++;

#if KB_LED_HEARTBEAT
        kb_heartbeat_tick(&hb_last);
#endif

        if ((loop_count % 2500) == 0) {
            split_slave_debug_dump(loop_count);
        }
    }
}

#endif /* SPLIT_TRANSPORT_BLE / AUTO */

#endif /* KB_IS_MASTER */

#endif /* SPLIT_ENABLED */
