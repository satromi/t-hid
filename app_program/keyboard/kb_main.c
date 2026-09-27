/*
 * kb_main.c — Keyboard scanner task
 *
 * Master: scans local matrix + reads slave via I2C (Layer 1-3) + USB HID
 * Slave:  scans local matrix + updates shared memory (Layer 0-1)
 *
 * RTOS 機能の活用:
 * - イベントフラグ: USB CONFIGURED 通知で起動待ち (固定 delay 不要)
 * - 周期ハンドラ: tk_cre_cyc() で正確な周期スキャンをカーネルが管理
 * - タスク待ち: スキャン契機をイベントフラグで待機 (CPU を他タスクに譲る)
 */

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "kb_config.h"
#include "kb_common.h"
#include "kb_matrix.h"
#include "kb_split.h"

#if defined(KB_WIRING_TEST)
#include "kb_wiring_test.h"
#elif KB_IS_MASTER
#include "kb_process.h"
#include "kb_output.h"
#include "kb_hid_keycodes.h"
#endif

/*----------------------------------------------------------------------
 * スキャン起動イベントフラグ (周期ハンドラ → タスクへの通知)
 */
EXPORT ID scan_flgid;
#define SCAN_EVT_TICK	(1u << 0)
#define SCAN_EVT_LOCK	(1u << 1)	/* 画面のロック (Win+L) の要求 */

/*----------------------------------------------------------------------
 * 周期ハンドラ: SCAN_PERIOD_MS ごとにイベントフラグをセット
 * カーネルのタイマーで正確に周期管理される
 */
LOCAL void scan_cyc_handler(void *exinf)
{
    (void)exinf;
    tk_set_flg(scan_flgid, SCAN_EVT_TICK);
}

/*----------------------------------------------------------------------
 * Wiring test task
 *
 * キーマップもスプリット通信も使わず、押されたセルの座標を
 * USB HID で入力するだけの配線確認モード。
 *----------------------------------------------------------------------*/
#if defined(KB_WIRING_TEST)

LOCAL void kb_scanner_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    kb_wiring_test_loop();
}

#else

/*----------------------------------------------------------------------
 * Master scanner task
 *----------------------------------------------------------------------*/
#if KB_IS_MASTER

LOCAL struct {
    T_DEBOUNCE_STATE db_left;
    T_DEBOUNCE_STATE db_right;
    T_KB_STATE       kb_state;
    T_HID_PAD_REPORT pad_sent;      /* 最後に送れたゲームパッドレポート */
} master;

LOCAL void kb_master_scan_once(void)
{
    T_MATRIX_STATE raw_left;
    T_MATRIX_STATE raw_right;

    memset(&raw_left, 0, sizeof(raw_left));
    memset(&raw_right, 0, sizeof(raw_right));

    /*
     * ローカルスキャン結果の格納先は、この基板がどちらの手かで決まる。
     * 右手担当ビルドでは自分がマトリクス行 10〜19 を埋める。
     */
#if KB_MATRIX_HAND_R
    matrix_scan(&raw_right);
#else
    matrix_scan(&raw_left);
#endif

#if SPLIT_ENABLED
    {
        matrix_row_t slave_rows[MATRIX_ROWS_PER_HAND];
        if (split_master_read(slave_rows)) {
            INT r;
            for (r = 0; r < MATRIX_ROWS_PER_HAND; r++) {
                raw_right.rows[r] = slave_rows[r];
            }
        }
    }
#endif

    matrix_debounce(&master.db_left, &raw_left);
    matrix_debounce(&master.db_right, &raw_right);

    BOOL changed = kb_process_keys(&master.kb_state,
                                   &master.db_left.current,
                                   &master.db_right.current);

    if (changed) {
        kb_output_send(&master.kb_state.report);
    }

    /*
     * ゲームパッド: 送れるまで毎スキャン送り直す (送信は待たない)。
     * ゲームパッドを持たないトランスポートなら送れたことにする。
     */
    if (memcmp(&master.kb_state.pad, &master.pad_sent, sizeof(master.pad_sent)) != 0) {
        ER err = kb_output_send_pad(&master.kb_state.pad);
        if (err == E_OK || err == E_NOSPT) {
            master.pad_sent = master.kb_state.pad;
        }
    }

    /* XMK アクション (press edge で 1 回だけ発火) */
    if (master.kb_state.xmk_pending != 0) {
        kb_output_xmk_action(master.kb_state.xmk_pending);
    }
}

/*
 * 画面のロック (Win+L) を送る
 *   USB に送るのはこのタスクだけにする。今押しているキーの状態に Win+L を重ねた
 *   レポートを送り、すぐ元の状態に戻すので、押しているキーは離されたことにならない。
 */
LOCAL void kb_master_send_screen_lock(void)
{
    T_HID_KBD_REPORT r = master.kb_state.report;
    INT i;

    r.modifier |= HID_MOD_LGUI;
    for (i = 0; i < 6 && r.keycode[i] != 0; i++) ;
    r.keycode[(i < 6) ? i : 5] = KC_L;
    kb_output_send(&r);
    kb_output_send(&master.kb_state.report);
}

/* 他のタスク (離席の判定など) から画面のロックを頼む */
EXPORT void kb_request_screen_lock(void)
{
    tk_set_flg(scan_flgid, SCAN_EVT_LOCK);
}

#if defined(USE_PRESENCE)
/*
 * スキャンの間隔の最大値 (μs)。推論などの重い処理と同居しても、スキャンが
 * 遅れていないか (キーを取りこぼさないか) を確かめるために測る
 */
#define TIMER_RAWL	0x40054028	/* RP2040 のタイマー (1μs) */
LOCAL UW scan_last_us, scan_max_gap_us;

LOCAL void kb_scan_gap_update(void)
{
    UW now = in_w(TIMER_RAWL);

    if (scan_last_us != 0 && now - scan_last_us > scan_max_gap_us) {
        scan_max_gap_us = now - scan_last_us;
    }
    scan_last_us = now;
}

EXPORT UW kb_scan_max_gap_us(BOOL reset)
{
    UW v = scan_max_gap_us;

    if (reset) scan_max_gap_us = 0;
    return v;
}
#endif

LOCAL void kb_scanner_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

    /*
     *  HID 出力初期化 (USB/BLE 自動切替)
     */
    {
        ER err = kb_output_init();
        if (err != E_OK) {
            tm_printf((UB *)"KB: no HID output, exit\n");
            tk_ext_tsk();
            return;
        }
    }

#if SPLIT_ENABLED
    split_init();
#endif

    matrix_init();
    memset(&master.db_left, 0, sizeof(master.db_left));
    memset(&master.db_right, 0, sizeof(master.db_right));
    kb_process_init(&master.kb_state);
    /* 起動直後に中立のゲームパッドレポートを 1 回送るため、送信済み値を無効にしておく */
    memset(&master.pad_sent, 0xFF, sizeof(master.pad_sent));

    tm_printf((UB *)"KB: master scanner started\n");

#if KB_LED_HEARTBEAT
    UW hb_last = kb_get_ms();
    KB_LED_ON();
#endif

    /*
     *  メインループ: 周期ハンドラからのイベントを待って1回スキャン
     *  tk_wai_flg() でブロックするため、待機中は CPU を他タスクに譲る。
     *  ポーリングループではなくカーネルのタイマーで正確に周期管理される。
     */
    while (1) {
        UINT flgptn;
        tk_wai_flg(scan_flgid, SCAN_EVT_TICK | SCAN_EVT_LOCK,
                   TWF_ORW | TWF_BITCLR, &flgptn, TMO_FEVR);

#if KB_LED_HEARTBEAT
        kb_heartbeat_tick(&hb_last);
#endif
        if (flgptn & SCAN_EVT_TICK) {
#if defined(USE_PRESENCE)
            kb_scan_gap_update();
#endif
            kb_master_scan_once();
        }
        if (flgptn & SCAN_EVT_LOCK) {
            kb_master_send_screen_lock();
        }
    }
}

/*----------------------------------------------------------------------
 * kb_process 外部アクセッサ実装 (MCP から呼ぶ)
 *
 * master.kb_state の active_layers / toggled_layers は各 1 バイトなので
 * Cortex-M0+ での read/write はアトミック。スキャナと MCP の競合は
 * 最大でも 1 スキャン周期 (1ms) で収束する。
 */
EXPORT UB kb_get_current_layer(void)
{
    UB active = master.kb_state.active_layers;
    INT i;
    for (i = 7; i >= 0; i--) {
        if (active & (1u << i)) return (UB)i;
    }
    return 0;
}

EXPORT UB kb_get_active_layers(void)
{
    return master.kb_state.active_layers;
}

EXPORT void kb_set_toggled_layer(UB layer_num, BOOL on)
{
    if (layer_num >= 8) return;           /* bitmask 範囲外 */
    if (layer_num == 0) return;           /* base は常にアクティブ (toggle 不可) */
    UB mask = (UB)(1u << layer_num);
    if (on) master.kb_state.toggled_layers |= mask;
    else    master.kb_state.toggled_layers &= (UB)~mask;
}

EXPORT void kb_clear_toggled_layers(void)
{
    master.kb_state.toggled_layers = 0;
}

#else /* Slave side */

/*----------------------------------------------------------------------
 * Slave scanner task
 *----------------------------------------------------------------------*/
LOCAL void kb_scanner_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

#if defined(SPLIT_TRANSPORT_BLE) || \
    (defined(SPLIT_TRANSPORT_AUTO) && defined(KB_OUTPUT_BLE))
    /*
     * BLE Split Slave: BTstack + Matrix Service を初期化。
     * HID Service は不要 (マトリクスデータ転送のみ)。
     *
     * 初期化順序:
     *   1. CYW43 ハードウェア初期化
     *   2. BTstack run loop 初期化
     *   3. Matrix Service 初期化 (GATT 登録 + アドバタイズ)
     *   4. BLE タスク起動
     */
    {
        extern ER cyw43_arch_tkernel_init(void);
        extern ER btstack_tkernel_init(void);
        extern ER btstack_tkernel_start(void);
        extern ER ble_split_service_init(void);

        ER err = cyw43_arch_tkernel_init();
        if (err != E_OK) {
            tm_printf((UB *)"KB slave: CYW43 init failed\n");
            tk_ext_tsk();
            return;
        }
        btstack_tkernel_init();
        ble_split_service_init();
        btstack_tkernel_start();
    }
#endif

    split_init();
    split_slave_loop();
}

#endif /* KB_IS_MASTER */

#endif /* KB_WIRING_TEST */

/*----------------------------------------------------------------------
 * Start keyboard framework
 */
void kb_start(void)
{
    T_CTSK ctsk;

    /*
     *  スキャン起動用イベントフラグ生成
     */
    {
        T_CFLG cflg;
        cflg.exinf   = NULL;
        cflg.flgatr  = TA_TPRI | TA_WMUL;
        cflg.iflgptn = 0;
        scan_flgid = tk_cre_flg(&cflg);
    }

    /*
     *  周期ハンドラ生成 — SCAN_PERIOD_MS ごとにスキャンをトリガー
     *  カーネルが正確にタイマー管理するため、tk_dly_tsk() より精度が高い
     */
    {
        T_CCYC ccyc;
        ccyc.exinf   = NULL;
        ccyc.cycatr  = TA_HLNG | TA_STA;
        ccyc.cychdr  = (FP)scan_cyc_handler;
        ccyc.cyctim  = SCAN_PERIOD_MS;
        ccyc.cycphs  = 0;
        tk_cre_cyc(&ccyc);
    }

    ctsk.exinf   = NULL;
    ctsk.tskatr  = TA_HLNG | TA_RNG3;
    ctsk.task    = (FP)kb_scanner_task;
    ctsk.itskpri = KB_SCAN_TASK_PRI;
    ctsk.stksz   = KB_SCAN_TASK_STKSZ;
    ctsk.bufptr  = NULL;

    ID tskid = tk_cre_tsk(&ctsk);
    if (tskid > 0) {
        tk_sta_tsk(tskid, 0);
#if defined(KB_WIRING_TEST)
        tm_printf((UB *)"KB: wiring test mode\n");
#elif KB_IS_MASTER
        tm_printf((UB *)"KB: master mode\n");
#else
        tm_printf((UB *)"KB: slave mode\n");
#endif
    } else {
        tm_printf((UB *)"KB: task create failed: %d\n", tskid);
    }
}
