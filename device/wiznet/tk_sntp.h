/*
 *----------------------------------------------------------------------
 *    μT-Kernel Native SNTP Client (RFC 4330)
 *
 *    UDP 1 往復で NTP サーバーから時刻を取得し、
 *    μT-Kernel のシステム時刻 (tk_set_tim) を同期する。
 *----------------------------------------------------------------------
 */

#ifndef __TK_SNTP_H__
#define __TK_SNTP_H__

#include <tk/tkernel.h>

/*
 * SNTP 時刻同期 (ブロッキング)
 *   sn:      使用するソケット番号 (0-3)
 *   ntp_ip:  NTP サーバー IP (UB[4])
 *   tmout:   タイムアウト (ms)
 *
 *   戻り値: E_OK=成功 (tk_set_tim 済み), E_TMOUT, E_IO
 */
ER tk_sntp_sync(UB sn, const UB *ntp_ip, TMO tmout);

/*
 * SNTP で取得した UNIX タイムスタンプを返す (最後の sync 結果)
 */
UW tk_sntp_get_unixtime(void);

#endif /* __TK_SNTP_H__ */
