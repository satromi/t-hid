/*
 *----------------------------------------------------------------------
 *    μT-Kernel Native DNS Client
 *
 *    ブロッキング API: tk_dns_resolve() で名前解決し、
 *    結果を IPv4 アドレスで返す。内部で UDP ソケットを使用。
 *----------------------------------------------------------------------
 */

#ifndef __TK_DNS_H__
#define __TK_DNS_H__

#include <tk/tkernel.h>

/*
 * DNS 名前解決 (ブロッキング)
 *   sn:       使用するソケット番号 (0-3)
 *   dns_ip:   DNS サーバー IP (UB[4])
 *   hostname: ホスト名 (例: "broker.example.com")
 *   result:   解決結果 IPv4 (UB[4], 出力)
 *   tmout:    タイムアウト (ms), TMO_FEVR 可
 *
 *   戻り値: E_OK=成功, E_TMOUT=タイムアウト, E_IO=通信エラー, E_PAR=パラメータ不正
 */
ER tk_dns_resolve(UB sn, const UB *dns_ip, const char *hostname,
                  UB *result, TMO tmout);

#endif /* __TK_DNS_H__ */
