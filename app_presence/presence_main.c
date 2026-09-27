/*
 * presence_main.c — Pico4ML で離席を判定し、PC の画面をロックする
 *
 *   カメラの画像から「人が写っているか」を推論し (presence_ml.cpp)、人が写らない
 *   状態が PRESENCE_AWAY_SEC 秒続いたら、USB キーボードとして Win+L を送る。
 *
 *   - 起動後、一度でも人を検出するまでは判定しない (無人の場所で起動してもロックしない)
 *   - 人がいない状態から在席に戻すのは、人らしい判定が続いたときだけ (見間違い対策)
 *   - ロックした後は、また人を検出するまで送らない
 *
 *   キーボードと同居する場合 (PRESENCE_WITH_KEYBOARD) は、USB には送らずキーボードの
 *   スキャンタスクにロックを頼む (kb_request_screen_lock)。判定のタスクはスキャンより
 *   低い優先度で動くので、推論中もキーのスキャンは遅れない。液晶にはスキャンの間隔の
 *   最大値 (ms、水色) も表示する。
 *
 *   液晶 (80x160):
 *     y   0- 79  カメラの画像 (推論に使う範囲を 80x80 に縮小)
 *     y  82- 97  状態 (緑: 在席、黄: 人なし、赤: ロックした、灰: 判定前)
 *     y 104-125  ロックまでの残り秒数 (人なしのとき)
 *     y 132-153  人らしさ (%)
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

#include "../device/include/dev_usb_hid.h"
#include "hm01b0.h"
#include "presence_ml.h"
#include "st7735.h"

#define PRESENCE_AWAY_SEC	30	/* この秒数、人が写らなければロックする */
#define PRESENCE_THRESHOLD	40	/* 在席中: 推論の点数 (-128..127) がこれ以下になったらすぐ離席とみなす (約 66%) */
/*
 * 人がいない状態 (判定前・離席中・ロック後) から在席に戻す条件。小さなモデルは背景を
 * 人と見間違えることがあるので、高めの点数が続けて出たときだけ戻す
 */
#define PRESENCE_RETURN_THRESHOLD	40	/* 約 66% */
#define PRESENCE_RETURN_FRAMES		3	/* 連続でこの回数 (約 3 秒) */
#define PRESENCE_FPS		10
#define PRESENCE_TASK_STKSZ	8192
#define PRESENCE_TASK_PRI	10	/* キーボードのスキャン (8) より低くする */

#define HID_KEY_L		0x0F

#define PREVIEW		80
#define CROP		HM01B0_ROWS	/* 画像の中央から正方形を切り出す */
#define CROP_X		((HM01B0_COLS - CROP) / 2)

LOCAL UB frame[HM01B0_COLS * HM01B0_ROWS] __attribute__((aligned(4)));
LOCAL UB preview[PREVIEW * PREVIEW];

/*----------------------------------------------------------------------
 * TensorFlow Lite Micro から使う時間とログ
 */
EXPORT UW tflm_port_time_us(void)
{
	SYSTIM t;

	tk_get_otm(&t);
	return t.lo * 1000;
}

EXPORT void tflm_port_log(const char *format, va_list args)
{
	static char buf[128];

	vsnprintf(buf, sizeof(buf), format, args);
	tm_printf((UB *)"%s", buf);
}

LOCAL UW now_ms(void)
{
	SYSTIM t;

	tk_get_otm(&t);
	return t.lo;
}

/*----------------------------------------------------------------------
 * 画像の縮小 (中央の正方形を n x n に。各画素は範囲の平均)
 */
LOCAL void shrink(UB *dst, INT n, BOOL to_int8)
{
	INT ox, oy, x, y;

	for (oy = 0; oy < n; oy++) {
		INT y0 = oy * CROP / n, y1 = (oy + 1) * CROP / n;
		for (ox = 0; ox < n; ox++) {
			INT x0 = CROP_X + ox * CROP / n, x1 = CROP_X + (ox + 1) * CROP / n;
			UW sum = 0, cnt = 0;
			for (y = y0; y < y1; y++) {
				const UB *p = &frame[y * HM01B0_COLS];
				for (x = x0; x < x1; x++) sum += p[x];
				cnt += x1 - x0;
			}
			sum /= cnt;
			*dst++ = to_int8 ? (UB)((INT)sum - 128) : (UB)sum;
		}
	}
}

/*----------------------------------------------------------------------
 * 画面のロック (Win+L)
 */
#if defined(PRESENCE_WITH_KEYBOARD)
IMPORT void kb_request_screen_lock(void);
IMPORT UW kb_scan_max_gap_us(BOOL reset);

LOCAL BOOL lock_screen(void)
{
	kb_request_screen_lock();
	return TRUE;
}
#else
LOCAL ID usb_dd = -1;

LOCAL BOOL usb_ready(void)
{
	ID flg = usb_hid_get_cfg_flgid();
	UINT ptn;

	if (usb_dd < 0) usb_dd = tk_opn_dev((UB *)USB_HID_DEVNM, TD_UPDATE);
	if (usb_dd < 0 || flg <= 0) return FALSE;
	return tk_wai_flg(flg, USB_HID_EVT_CONFIGURED, TWF_ORW, &ptn, 0) == E_OK;
}

LOCAL ER usb_send(const T_USB_HID_KBD_REPORT *r)
{
	SZ asize;
	ER err = E_BUSY;
	INT i;

	for (i = 0; i < 5 && err == E_BUSY; i++) {
		err = tk_swri_dev(usb_dd, 0, (void *)r, sizeof(*r), &asize);
		if (err == E_BUSY) tk_dly_tsk(10);
	}
	return err;
}

LOCAL BOOL lock_screen(void)
{
	T_USB_HID_KBD_REPORT r;

	if (!usb_ready()) return FALSE;
	memset(&r, 0, sizeof(r));
	r.modifier = HID_MOD_LGUI;
	r.keycode[0] = HID_KEY_L;
	if (usb_send(&r) != E_OK) return FALSE;
	tk_dly_tsk(30);
	memset(&r, 0, sizeof(r));
	usb_send(&r);
	return TRUE;
}
#endif

/*----------------------------------------------------------------------
 * 表示
 */
typedef enum { ST_WAITING, ST_PRESENT, ST_ABSENT, ST_LOCKED } T_STATE;

LOCAL void show(T_STATE st, INT score, UW left_sec)
{
	static const UH color[] = { LCD_GRAY, LCD_GREEN, LCD_YELLOW, LCD_RED };
	UW percent = (UW)(score + 128) * 100 / 255;

	st7735_gray(0, 0, PREVIEW, PREVIEW, preview);
	st7735_fill(0, 82, LCD_W, 16, color[st]);

	st7735_fill(0, 104, LCD_W, 22, LCD_BLACK);
	if (st == ST_ABSENT) st7735_number(4, 104, left_sec, 2, LCD_YELLOW);
#if defined(PRESENCE_WITH_KEYBOARD)
	st7735_number(44, 104, (kb_scan_max_gap_us(FALSE) + 999) / 1000, 2, LCD_CYAN);
#endif

	st7735_fill(0, 132, LCD_W, 22, LCD_BLACK);
	st7735_number(4, 132, percent, 2, (score > PRESENCE_THRESHOLD) ? LCD_GREEN : LCD_WHITE);
}

LOCAL void show_error(UW code)
{
	st7735_fill(0, 0, LCD_W, LCD_H, LCD_RED);
	st7735_number(4, 60, code, 3, LCD_WHITE);
}

/*----------------------------------------------------------------------
 * 判定のタスク
 */
LOCAL void presence_task(INT stacd, void *exinf)
{
	T_STATE st = ST_WAITING;
	UW last_seen = 0;
	ER err;
	INT score, hits = 0;
	BOOL person;

	st7735_init();
	st7735_fill(0, 82, LCD_W, 16, LCD_GRAY);

	err = hm01b0_init(PRESENCE_FPS);
	if (err != E_OK) {
		tm_printf((UB *)"presence: camera init failed (%d)\n", err);
		show_error(1);
		tk_ext_tsk();
	}
	err = presence_ml_init();
	if (err != 0) {
		tm_printf((UB *)"presence: model init failed (%d)\n", err);
		show_error(2);
		tk_ext_tsk();
	}
	tm_printf((UB *)"presence: ready (lock after %d s without a person)\n", PRESENCE_AWAY_SEC);

	for (;;) {
		UW now, away;

		if (hm01b0_capture(frame, 1000) != E_OK) {
			show_error(3);
			tk_dly_tsk(500);
			continue;
		}
		shrink((UB *)presence_ml_input(), PRESENCE_ML_COLS, TRUE);
		shrink(preview, PREVIEW, FALSE);
		score = presence_ml_invoke();

		now = now_ms();
		if (st == ST_PRESENT) {
			person = score > PRESENCE_THRESHOLD;
		} else {
			hits = (score > PRESENCE_RETURN_THRESHOLD) ? hits + 1 : 0;
			person = hits >= PRESENCE_RETURN_FRAMES;
		}
		if (person) {
			last_seen = now;
			st = ST_PRESENT;
			hits = 0;
		} else if (st == ST_PRESENT || st == ST_ABSENT) {
			away = now - last_seen;
			if (away >= PRESENCE_AWAY_SEC * 1000) {
				if (lock_screen()) {
					tm_printf((UB *)"presence: away %u s, screen locked\n", away / 1000);
					st = ST_LOCKED;
				}
			} else {
				st = ST_ABSENT;
			}
		}

		away = (now - last_seen) / 1000;
		show(st, score, (st == ST_ABSENT && away < PRESENCE_AWAY_SEC) ? PRESENCE_AWAY_SEC - away : 0);
	}
}

EXPORT ER presence_start(void)
{
	T_CTSK ctsk;
	ID id;

	memset(&ctsk, 0, sizeof(ctsk));
	ctsk.tskatr  = TA_HLNG | TA_RNG0;
	ctsk.task    = (FP)presence_task;
	ctsk.itskpri = PRESENCE_TASK_PRI;
	ctsk.stksz   = PRESENCE_TASK_STKSZ;
	id = tk_cre_tsk(&ctsk);
	if (id < E_OK) return id;
	return tk_sta_tsk(id, 0);
}

#if !defined(PRESENCE_WITH_KEYBOARD)
EXPORT INT usermain(void)
{
	ER err = presence_start();

	if (err != E_OK) tm_printf((UB *)"presence: task start failed (%d)\n", err);
	tk_slp_tsk(TMO_FEVR);
	return 0;
}
#endif
