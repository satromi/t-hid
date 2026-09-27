/*
 * kb_common.h — Keyboard framework common utilities
 *
 * master/slave 両方で使う共通ユーティリティ
 */

#ifndef __KB_COMMON_H__
#define __KB_COMMON_H__

#include <tk/tkernel.h>
#include "kb_config.h"

/*----------------------------------------------------------------------
 * ミリ秒タイムスタンプ取得 (μT-Kernel tk_get_otm ラッパー)
 */
static inline UW kb_get_ms(void)
{
    SYSTIM tim;
    tk_get_otm(&tim);
    return (UW)tim.lo;
}

/*----------------------------------------------------------------------
 * LED ハートビート制御
 *
 * 2秒周期: 100ms ON → 1900ms OFF
 * hb_last を更新しながら呼び出す。初回は hb_last = kb_get_ms() で初期化。
 */
#if KB_LED_HEARTBEAT
static inline void kb_heartbeat_tick(UW *hb_last)
{
    UW elapsed = kb_get_ms() - *hb_last;
    if (elapsed >= 2000) {
        *hb_last += 2000;
        KB_LED_ON();
    } else if (elapsed >= 100) {
        KB_LED_OFF();
    }
}
#endif

#endif /* __KB_COMMON_H__ */
