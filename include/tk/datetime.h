/*
 *----------------------------------------------------------------------
 *    μT-Kernel 3.0 BSP — 日時管理 (Standard Extension 準拠)
 *
 *    T-Kernel Standard Extension (TEF021-S004) の日時管理 API を
 *    μT-Kernel 3.0 向けに実装。
 *
 *    機能:
 *    - SYSTIM (ms) とカレンダー (年月日時分秒) の相互変換
 *    - UNIX タイムスタンプ変換
 *    - タイムゾーン対応
 *    - 文字列フォーマット
 *----------------------------------------------------------------------
 */

#ifndef __TK_DATETIME_H__
#define __TK_DATETIME_H__

#include <tk/tkernel.h>

/*
 * T-Kernel 時刻基準:
 *   SYSTIM は 1985-01-01 00:00:00 GMT からの経過ミリ秒 (T-Kernel 2.0 仕様)
 *   UNIX epoch (1970-01-01) との差: 473385600 秒
 */
#define TK_EPOCH_DIFF   473385600UL  /* 1970-01-01 〜 1985-01-01 の秒数 */

/*----------------------------------------------------------------------
 * DATE_TIM 構造体 (Standard Extension 準拠)
 *
 * T-Kernel SE の SYSTIME_CTIME に相当。
 */
typedef struct {
    H   d_year;     /* 西暦年 (1970-2099) */
    H   d_month;    /* 月 (1-12) */
    H   d_day;      /* 日 (1-31) */
    H   d_hour;     /* 時 (0-23) */
    H   d_min;      /* 分 (0-59) */
    H   d_sec;      /* 秒 (0-59) */
    H   d_wday;     /* 曜日 (0=日, 1=月, ..., 6=土) */
    H   d_days;     /* 年内通算日 (0-365, 未使用時 0) */
} DATE_TIM;

/*----------------------------------------------------------------------
 * 日時管理 API
 */

/*
 * タイムゾーン設定 (UTC からの分オフセット)
 *   例: JST = +540 (+9h), UTC = 0, EST = -300 (-5h)
 */
void dt_settimezone(INT offset_min);

/*
 * 現在の日時を取得 (カレンダー形式)
 *   tk_get_tim() → UNIX 変換 → カレンダー変換 → タイムゾーン適用
 *   SNTP 同期済みであれば正しい壁時計時刻を返す。
 */
ER dt_gettime(DATE_TIM *dt);

/*
 * 日時を設定 (カレンダー形式)
 *   カレンダー → UNIX → tk_set_tim()
 */
ER dt_settime(const DATE_TIM *dt);

/*
 * UNIX タイムスタンプ ⇔ カレンダー変換
 */
void dt_unix_to_date(UW unixtime, DATE_TIM *dt);
UW   dt_date_to_unix(const DATE_TIM *dt);

/*
 * 現在の UNIX タイムスタンプを取得 (tk_get_tim ベース)
 */
UW dt_getunixtime(void);

/*
 * カレンダーを文字列にフォーマット
 *   "YYYY-MM-DD HH:MM:SS" (19文字 + NUL, bufsz >= 20)
 */
INT dt_format(const DATE_TIM *dt, char *buf, UH bufsz);

#endif /* __TK_DATETIME_H__ */
