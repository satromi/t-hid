/*
 * tz_selftest.c — TrustZone の自己テスト (make TZ=1 TZTEST=1 のときだけ)
 *
 *   標準実行モデルで、セキュア側を実行中のタスクの切り替えが正しく行われることを
 *   実機で確かめる。U2F などの通常の動作と並行して動き、10 秒ごとに結果を表示する。
 *
 *   TZT_A, TZT_B (TA_TZCALL, 優先度 12 と 13)
 *     セキュア側で tzt_calc() を 1 回あたり数十 ms 実行し、非セキュア側で計算した
 *     値と比べる。A が B を横取りするので、両方がセキュア側の途中で切り替わる。
 *     呼び出しの前後で S16 と R4-R11 相当の局所変数が保たれていることも確かめる。
 *   TZT_HI (TA_TZCALL なし, 優先度 7)
 *     1ms ごとに起き、浮動小数点レジスタを使う計算をする。A や B がセキュア側を
 *     実行中に起きた回数を「preempted」として数える。
 *   TZT_C (TA_TZCALL, 優先度 11。A と B より高くして、結果の表示が止まらないようにする)
 *     TA_TZCALL のタスクの生成・起動・終了 (tk_exd_tsk) と、生成してすぐの削除
 *     (tk_del_tsk) を繰り返し、セキュアスタックが漏れないことを確かめる。
 */

#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "tz_selftest.h"

#define TZT_LOOPS	200000		/* 1 回のセキュア側の計算 (数十 ms) */
#define TZT_REPORT_MS	10000

LOCAL volatile UW hi_count;		/* TZT_HI が動いた回数 */
LOCAL volatile UW hi_fp_err;
LOCAL volatile UW calls[2], preempted[2], calc_err[2], reg_err[2];
LOCAL volatile UW child_ok, child_err, cre_err, del_ok;
LOCAL ID tzt_c_id;

/* 浮動小数点レジスタを使う計算 (結果は整数と同じになる) */
LOCAL void hi_task(INT stacd, void *exinf)
{
	UW n = 0;

	for (;;) {
		float s = 0;
		UW i, t = 0;

		for (i = 0; i < 64; i++) {
			s += (float)((n + i) & 0xFF);
			t += (n + i) & 0xFF;
		}
		if ((UW)s != t) hi_fp_err++;
		hi_count++;
		n++;
		tk_dly_tsk(1);
	}
}

LOCAL void work_task(INT stacd, void *exinf)
{
	UW seed = 0x1234u + (UW)stacd * 0x10000u;

	for (;;) {
		UW expect, got, c0, c1;
		UW keep1 = seed * 3u, keep2 = seed ^ 0xA5A5A5A5u;
		float before = (float)(seed & 0xFFFF), after;

		expect = tzt_calc(seed, TZT_LOOPS);

		__asm__ volatile ("vmov.f32 s16, %0" :: "t"(before) : "s16");
		c0 = hi_count;
		got = tzt_work(seed, TZT_LOOPS);
		c1 = hi_count;
		__asm__ volatile ("vmov.f32 %0, s16" : "=t"(after));

		calls[stacd]++;
		if (got != expect) calc_err[stacd]++;
		if (after != before || keep1 != seed * 3u || keep2 != (seed ^ 0xA5A5A5A5u)) {
			reg_err[stacd]++;
		}
		if (c1 != c0) preempted[stacd]++;
		seed += 0x9E3779B9u;
		tk_dly_tsk(stacd + 1);
	}
}

LOCAL void child_task(INT stacd, void *exinf)
{
	if (tzt_work((UW)stacd, 1000) == tzt_calc((UW)stacd, 1000)) child_ok++;
	else child_err++;
	tk_wup_tsk(tzt_c_id);
	tk_exd_tsk();
}

LOCAL ID create(FP task, PRI pri, BOOL tzcall, SZ tzstksz)
{
	T_CTSK c;

	c.exinf   = NULL;
	c.tskatr  = TA_HLNG | TA_RNG0 | (tzcall ? TA_TZCALL : 0);
	c.task    = task;
	c.itskpri = pri;
	c.stksz   = 4096;
	c.bufptr  = NULL;
	c.tzstksz = tzstksz;
	return tk_cre_tsk(&c);
}

LOCAL void churn_task(INT stacd, void *exinf)
{
	UW n = 0, last = 0;
	SYSTIM now;
	ID id;

	for (;;) {
		/* 生成 → 起動 → 自分で終了して削除 */
		id = create((FP)child_task, 11, TRUE, 512);
		if (id < E_OK) {
			cre_err++;
		} else {
			tk_sta_tsk(id, (INT)n);
			tk_slp_tsk(1000);
		}

		/* 生成してすぐ削除 */
		id = create((FP)child_task, 11, TRUE, 512);
		if (id < E_OK) cre_err++;
		else if (tk_del_tsk(id) == E_OK) del_ok++;

		n++;
		tk_get_otm(&now);
		if (now.lo - last >= TZT_REPORT_MS) {
			last = now.lo;
			tm_printf((UB *)"TZTEST: A calls=%u preempted=%u calc_err=%u reg_err=%u"
				  " | B calls=%u preempted=%u calc_err=%u reg_err=%u\n",
				  calls[0], preempted[0], calc_err[0], reg_err[0],
				  calls[1], preempted[1], calc_err[1], reg_err[1]);
			tm_printf((UB *)"TZTEST: hi=%u fp_err=%u | child ok=%u err=%u del=%u cre_err=%u\n",
				  hi_count, hi_fp_err, child_ok, child_err, del_ok, cre_err);
		}
		tk_dly_tsk(20);
	}
}

EXPORT ER tzt_start(void)
{
	ID id;

	id = create((FP)hi_task, 7, FALSE, 0);
	if (id < E_OK) return id;
	tk_sta_tsk(id, 0);

	id = create((FP)work_task, 12, TRUE, 1024);
	if (id < E_OK) return id;
	tk_sta_tsk(id, 0);

	id = create((FP)work_task, 13, TRUE, 1024);
	if (id < E_OK) return id;
	tk_sta_tsk(id, 1);

	tzt_c_id = create((FP)churn_task, 11, TRUE, 512);
	if (tzt_c_id < E_OK) return tzt_c_id;
	return tk_sta_tsk(tzt_c_id, 0);
}
