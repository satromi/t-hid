/*
 *----------------------------------------------------------------------
 *    μT-Kernel 3.0 BSP — 日時管理 (Standard Extension 準拠)
 *
 *    T-Kernel Standard Extension (TEF021-S004) の日時管理 API 実装。
 *
 *    SYSTIM (ms 単位, tk_get_tim/tk_set_tim) と
 *    UNIX タイムスタンプ (1970-01-01 起点, 秒) と
 *    カレンダー (DATE_TIM 構造体, 年月日時分秒) の相互変換を提供。
 *
 *    閏年判定・月日計算は標準アルゴリズムで実装。
 *    外部ライブラリ (time.h / libc) には一切依存しない。
 *----------------------------------------------------------------------
 */

#include <tk/tkernel.h>
#include <tk/datetime.h>

/* タイムゾーンオフセット (分) — デフォルト JST (+9h) */
LOCAL INT tz_offset_min = 9 * 60;

/* 月ごとの日数 (平年) */
LOCAL const UB days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*----------------------------------------------------------------------
 * 閏年判定
 */
LOCAL BOOL is_leap_year(H year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/*----------------------------------------------------------------------
 * 指定年月の日数
 */
LOCAL UB month_days(H year, H month)
{
    if (month == 2 && is_leap_year(year)) return 29;
    return days_in_month[month - 1];
}

/*======================================================================
 * 公開 API
 *====================================================================*/

EXPORT void dt_settimezone(INT offset_min)
{
    tz_offset_min = offset_min;
}

/*----------------------------------------------------------------------
 * UNIX タイムスタンプ → カレンダー変換
 */
EXPORT void dt_unix_to_date(UW unixtime, DATE_TIM *dt)
{
    /* タイムゾーン適用 (符号付き演算で 2038 年問題を回避) */
    W tz_sec = (W)tz_offset_min * 60;
    UW t = unixtime + (UW)tz_sec;  /* UW 演算で 2106 年まで対応 */

    /* 時分秒 */
    dt->d_sec  = (H)(t % 60); t /= 60;
    dt->d_min  = (H)(t % 60); t /= 60;
    dt->d_hour = (H)(t % 24); t /= 24;

    /* t = 1970-01-01 からの日数 */
    /* 曜日: 1970-01-01 は木曜 (wday=4) */
    dt->d_wday = (H)((t + 4) % 7);

    /* 年の計算 */
    H year = 1970;
    UH yday = 0;
    while (1) {
        UW days_year = is_leap_year(year) ? 366 : 365;
        if (t < days_year) break;
        t -= days_year;
        year++;
    }
    dt->d_year = year;
    yday = (UH)t;  /* 年内通算日 (0始まり) */

    /* 月日の計算 */
    H month = 1;
    while (month <= 12) {
        UB md = month_days(year, month);
        if (t < md) break;
        t -= md;
        month++;
    }
    dt->d_month = month;
    dt->d_day   = (H)(t + 1);  /* 1始まり */
    dt->d_days  = (H)yday;
}

/*----------------------------------------------------------------------
 * カレンダー → UNIX タイムスタンプ変換
 */
EXPORT UW dt_date_to_unix(const DATE_TIM *dt)
{
    UW days = 0;
    H y, m;

    /* 1970 年から dt->d_year-1 年までの日数 */
    for (y = 1970; y < dt->d_year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }

    /* 1 月から dt->d_month-1 月までの日数 */
    for (m = 1; m < dt->d_month; m++) {
        days += month_days(dt->d_year, m);
    }

    /* 日 (0 始まりに変換) */
    days += (UW)(dt->d_day - 1);

    /* 秒に変換 */
    UW t = days * 86400UL + (UW)dt->d_hour * 3600 +
           (UW)dt->d_min * 60 + (UW)dt->d_sec;

    /* タイムゾーン逆補正 (ローカル → UTC, UW 演算で 2106 年まで対応) */
    t -= (UW)((W)tz_offset_min * 60);

    return t;
}

/*----------------------------------------------------------------------
 * 現在の UNIX タイムスタンプを取得
 *
 * T-Kernel の SYSTIM は 1985-01-01 GMT 基準。
 * UNIX epoch (1970-01-01) に変換するため TK_EPOCH_DIFF を加算。
 */
EXPORT UW dt_getunixtime(void)
{
    SYSTIM tim;
    tk_get_tim(&tim);
    UD ms = ((UD)(UW)tim.hi << 32) | tim.lo;
    return (UW)(ms / 1000) + TK_EPOCH_DIFF;
}

/*----------------------------------------------------------------------
 * 現在の日時を取得 (カレンダー形式)
 */
EXPORT ER dt_gettime(DATE_TIM *dt)
{
    if (dt == NULL) return E_PAR;

    UW unixtime = dt_getunixtime();
    if (unixtime < 946684800UL) {
        /* 2000年より前 = SNTP 未同期 */
        return E_IO;
    }

    dt_unix_to_date(unixtime, dt);
    return E_OK;
}

/*----------------------------------------------------------------------
 * 日時を設定 (カレンダー形式)
 */
/*
 * 日時を設定 (カレンダー形式)
 * UNIX → T-Kernel 基準 (1985) に変換して tk_set_tim
 */
EXPORT ER dt_settime(const DATE_TIM *dt)
{
    if (dt == NULL) return E_PAR;

    UW unixtime = dt_date_to_unix(dt);

    /* UNIX epoch → T-Kernel epoch (1985) 変換 */
    UD ms = (UD)(unixtime - TK_EPOCH_DIFF) * 1000;
    SYSTIM tim;
    tim.hi = (W)(ms >> 32);
    tim.lo = (UW)(ms & 0xFFFFFFFF);
    return tk_set_tim(&tim);
}

/*----------------------------------------------------------------------
 * カレンダーを文字列にフォーマット
 * "YYYY-MM-DD HH:MM:SS" (19文字 + NUL)
 */
EXPORT INT dt_format(const DATE_TIM *dt, char *buf, UH bufsz)
{
    if (dt == NULL || buf == NULL || bufsz < 20) return 0;

    H yr = dt->d_year;
    buf[0]  = '0' + (yr / 1000) % 10;
    buf[1]  = '0' + (yr / 100) % 10;
    buf[2]  = '0' + (yr / 10) % 10;
    buf[3]  = '0' + yr % 10;
    buf[4]  = '-';
    buf[5]  = '0' + dt->d_month / 10;
    buf[6]  = '0' + dt->d_month % 10;
    buf[7]  = '-';
    buf[8]  = '0' + dt->d_day / 10;
    buf[9]  = '0' + dt->d_day % 10;
    buf[10] = ' ';
    buf[11] = '0' + dt->d_hour / 10;
    buf[12] = '0' + dt->d_hour % 10;
    buf[13] = ':';
    buf[14] = '0' + dt->d_min / 10;
    buf[15] = '0' + dt->d_min % 10;
    buf[16] = ':';
    buf[17] = '0' + dt->d_sec / 10;
    buf[18] = '0' + dt->d_sec % 10;
    buf[19] = '\0';

    return 19;
}
