/*
 *----------------------------------------------------------------------
 *    μT-Kernel Native HTTP Server (最小実装)
 *
 *    ソケット 1 本で HTTP/1.0 GET リクエストを処理する。
 *    コンテンツはコールバックで動的生成する方式。
 *
 *    RTOS 機能:
 *    - 専用タスク: listen → accept → 処理 → close ループ
 *    - tk_sock_accept: 割り込み駆動の接続待ち
 *----------------------------------------------------------------------
 */

#ifndef __TK_HTTPD_H__
#define __TK_HTTPD_H__

#include <tk/tkernel.h>

/* HTTP リクエスト情報 */
typedef struct {
    const char *path;       /* リクエストパス (例: "/status") */
    UB         *resp_buf;   /* レスポンスボディ書き込み先 */
    UH          resp_max;   /* resp_buf の最大サイズ */
    UH          resp_len;   /* コールバックが書き込んだサイズ (出力) */
    const char *content_type; /* Content-Type (出力, NULL→"text/html") */
} T_HTTP_REQ;

/* コンテンツ生成コールバック */
typedef ER (*FP_HTTP_HANDLER)(T_HTTP_REQ *req);

/*
 * HTTP サーバー開始
 *   sn:      使用するソケット番号 (0-3)
 *   port:    リッスンポート (通常 80)
 *   handler: リクエストハンドラコールバック
 */
ER tk_httpd_start(UB sn, UH port, FP_HTTP_HANDLER handler);

/* HTTP サーバー停止 */
void tk_httpd_stop(void);

#endif /* __TK_HTTPD_H__ */
