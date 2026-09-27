/*
 * btstack_port_tkernel.c — BTstack μT-Kernel port
 *
 * BTstack の embedded run loop を μT-Kernel タスクとして動作させる。
 *
 * 構成:
 * - BLE タスク: btstack_run_loop_embedded_execute_once() をポーリング実行
 * - イベントフラグ: CYW43 IRQ → BLE タスク起床
 * - HAL 実装: hal_time_ms (tk_get_otm), hal_cpu (DI/EI)
 *
 * baremetal の btstack_run_loop_execute() (無限ループ) を使わず、
 * execute_once() を μT-Kernel タスクループから呼ぶことで、
 * RTOS のタスクスケジューリングと協調動作する。
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

#include "btstack_config.h"
#include "btstack_port_tkernel.h"
#include "btstack_run_loop_embedded.h"
#include "btstack_run_loop.h"

/*----------------------------------------------------------------------
 * BLE タスク起床用イベントフラグ
 */
LOCAL ID ble_flgid = 0;
#define BLE_EVT_IRQ     (1u << 0)   /* CYW43 IRQ */
#define BLE_EVT_POLL    (1u << 1)   /* ポーリング起床 */

/*----------------------------------------------------------------------
 * HAL: hal_time_ms — BTstack タイマー用ミリ秒時計
 *
 * BTstack は HAVE_EMBEDDED_TIME_MS を定義すると
 * hal_time_ms() を使ってタイマーを管理する。
 */
uint32_t hal_time_ms(void)
{
    SYSTIM tim;
    tk_get_otm(&tim);
    return (uint32_t)tim.lo;
}

/*----------------------------------------------------------------------
 * HAL: hal_cpu — 割り込み制御
 *
 * BTstack の embedded run loop が idle 判定時に使う。
 * μT-Kernel のタスクベースなので、sleep の代わりに
 * イベントフラグ待ちで CPU を解放する。
 */
void hal_cpu_disable_irqs(void)
{
    /* BTstack の trigger_event_received チェック用
     * μT-Kernel タスクベースでは実質 no-op */
}

void hal_cpu_enable_irqs(void)
{
    /* no-op */
}

void hal_cpu_enable_irqs_and_sleep(void)
{
    /*
     * embedded run loop が「何もすることがない」と判断した時に呼ばれる。
     * baremetal では WFI するが、μT-Kernel ではイベントフラグ待ちで
     * タスクをスリープさせ、CPU を他タスクに譲る。
     * CYW43 IRQ またはポーリングタイマーで起床する。
     */
    if (ble_flgid > 0) {
        UINT flgptn;
        tk_wai_flg(ble_flgid, BLE_EVT_IRQ | BLE_EVT_POLL,
                   TWF_ORW | TWF_BITCLR, &flgptn, BLE_POLL_INTERVAL);
    }
}

/*
 * malloc/free リダイレクト:
 * btstack_config.h で #define malloc Kmalloc / #define free Kfree を定義済み。
 * BTstack の btstack_memory.c が呼ぶ malloc() は自動的に Kmalloc() に置換される。
 */

/*----------------------------------------------------------------------
 * ISR からの起床通知
 * CYW43 の IRQ ハンドラから呼ばれる
 */
EXPORT void btstack_tkernel_trigger(void)
{
    if (ble_flgid > 0) {
        tk_set_flg(ble_flgid, BLE_EVT_IRQ);
    }
}

/*----------------------------------------------------------------------
 * BLE タスク
 *
 * btstack_run_loop_embedded_execute_once() を繰り返し呼ぶ。
 * baremetal の btstack_run_loop_execute() と同等だが、
 * 1 回ずつ実行して μT-Kernel のタスク切替と協調する。
 *
 * idle 時は hal_cpu_enable_irqs_and_sleep() 経由で
 * tk_wai_flg() に入り、CYW43 IRQ で起床する。
 */
/* CYW43 ポーリング (cyw43_arch_tkernel.c で定義) */
extern void cyw43_arch_tkernel_poll(void);
/* HID レポート送信処理 (ble_hid_gatt.c で定義) */
extern void ble_hid_gatt_process(void);

LOCAL ID ble_tskid = 0;

LOCAL void ble_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

    /* 自タスク ID を保存 (スタック監視用) */
    ble_tskid = tk_get_tid();

    /*
     * BLE タスクメインループ:
     * 1. CYW43 ドライバをポーリング (SPI 経由で BT パケット受信)
     * 2. BTstack run loop を 1 回実行 (タイマー・データソース処理)
     * 3. HID レポート送信処理 (scanner タスクからの委譲を処理)
     */
    while (1) {
        cyw43_arch_tkernel_poll();
        btstack_run_loop_embedded_execute_once();
        ble_hid_gatt_process();
    }
}

/*----------------------------------------------------------------------
 * BLE タスク状態取得 (デバッグ・監視用)
 *
 * μT-Kernel 3.0 の T_RTSK にはスタック使用量フィールドがないため、
 * タスク状態のみを返す。
 *
 * 戻り値: タスク状態 (tskstat), エラー時 -1
 */
EXPORT W btstack_tkernel_task_state(void)
{
    if (ble_tskid <= 0) return -1;
    T_RTSK rtsk;
    ER err = tk_ref_tsk(ble_tskid, &rtsk);
    if (err != E_OK) return -1;
    return (W)rtsk.tskstat;
}

/*----------------------------------------------------------------------
 * Phase 1: run loop + イベントフラグ初期化
 * BTstack API を呼べる状態にする。タスクはまだ起動しない。
 */
EXPORT ER btstack_tkernel_init(void)
{
    /* イベントフラグ生成 */
    {
        T_CFLG cflg;
        cflg.exinf   = NULL;
        cflg.flgatr  = TA_TPRI | TA_WMUL;
        cflg.iflgptn = 0;
        ble_flgid = tk_cre_flg(&cflg);
        if (ble_flgid < E_OK) return ble_flgid;
    }

    /* BTstack メモリプール初期化 (hci_init より前に必須) */
    {
        extern void btstack_memory_init(void);
        btstack_memory_init();
    }

    /* BTstack run loop 初期化 */
    btstack_run_loop_init(btstack_run_loop_embedded_get_instance());

    tm_printf((UB *)"BLE: BTstack run loop initialized\n");
    return E_OK;
}

/*----------------------------------------------------------------------
 * Phase 2: BLE タスク起動
 * BTstack の GATT サービス登録等が全て完了した後に呼ぶこと。
 * ここから run loop が回り始め、BTstack のイベント処理が開始される。
 */
EXPORT ER btstack_tkernel_start(void)
{
    T_CTSK ctsk;
    ID tskid;

    ctsk.exinf   = NULL;
    ctsk.tskatr  = TA_HLNG | TA_RNG3;
    ctsk.task    = (FP)ble_task;
    ctsk.itskpri = BLE_TASK_PRI;
    ctsk.stksz   = BLE_TASK_STKSZ;
    ctsk.bufptr  = NULL;
    tskid = tk_cre_tsk(&ctsk);
    if (tskid < E_OK) return tskid;
    tk_sta_tsk(tskid, 0);

    tm_printf((UB *)"BLE: task started\n");
    return E_OK;
}

#endif /* CPU_RP2040 */
