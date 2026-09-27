/*
 *----------------------------------------------------------------------
 *    μT-Kernel Native HTTP Server (最小実装)
 *
 *    HTTP/1.0 GET リクエストを処理する簡易 Web サーバー。
 *    コンテンツはコールバック (FP_HTTP_HANDLER) で動的生成。
 *
 *    RTOS 機能:
 *    - 専用タスク: accept ループ (割り込み駆動の接続待ち)
 *    - tk_sock_accept/recv/send: 全てブロッキング (CPU 解放)
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "w5100s_reg.h"
#include "tk_socket.h"
#include "tk_httpd.h"

/* タスク設定 */
#define HTTPD_TASK_PRI   9
#define HTTPD_TASK_STKSZ 1024
#define HTTPD_RXBUF_SZ   512
#define HTTPD_TXBUF_SZ   512

LOCAL ID httpd_tskid;
LOCAL BOOL httpd_running;
LOCAL UB httpd_sn;
LOCAL UH httpd_port;
LOCAL FP_HTTP_HANDLER httpd_handler;

LOCAL UB httpd_rxbuf[HTTPD_RXBUF_SZ];
LOCAL UB httpd_txbuf[HTTPD_TXBUF_SZ];

/*----------------------------------------------------------------------
 * HTTP リクエスト行からパスを抽出
 * "GET /path HTTP/1.x\r\n..." → "/path"
 */
LOCAL const char *httpd_parse_path(const char *req, UH len)
{
    /* "GET " を探す */
    if (len < 5) return NULL;
    if (req[0] != 'G' || req[1] != 'E' || req[2] != 'T' || req[3] != ' ')
        return NULL;
    return &req[4];  /* パスの先頭 */
}

/*----------------------------------------------------------------------
 * HTTP レスポンス送信
 */
LOCAL void httpd_send_response(UB sn, UH status,
                                const char *content_type,
                                const UB *body, UH body_len)
{
    const char *status_str;
    switch (status) {
    case 200: status_str = "200 OK"; break;
    case 404: status_str = "404 Not Found"; break;
    default:  status_str = "500 Internal Server Error"; break;
    }
    if (content_type == NULL) content_type = "text/html";

    /* ヘッダ組み立て (スタック上に構築 — httpd_txbuf を壊さない) */
    UB hdr[192];
    INT hdr_len = 0;
    char *p = (char *)hdr;

    const char *h1 = "HTTP/1.0 ";
    const char *h2 = "\r\nContent-Type: ";
    const char *h3 = "\r\nConnection: close\r\n\r\n";

    memcpy(p + hdr_len, h1, strlen(h1)); hdr_len += strlen(h1);
    memcpy(p + hdr_len, status_str, strlen(status_str)); hdr_len += strlen(status_str);
    memcpy(p + hdr_len, h2, strlen(h2)); hdr_len += strlen(h2);
    memcpy(p + hdr_len, content_type, strlen(content_type)); hdr_len += strlen(content_type);
    memcpy(p + hdr_len, h3, strlen(h3)); hdr_len += strlen(h3);

    /* ヘッダ送信 */
    tk_sock_send(sn, hdr, (UH)hdr_len, 5000);

    /* ボディ送信 */
    if (body != NULL && body_len > 0) {
        tk_sock_send(sn, body, body_len, 5000);
    }
}

/*----------------------------------------------------------------------
 * HTTP サーバータスク
 */
LOCAL void httpd_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

    while (httpd_running) {
        /* ソケットオープン + リッスン */
        ER err = tk_sock_open(httpd_sn, Sn_MR_TCP, httpd_port);
        if (err != E_OK) {
            tk_dly_tsk(1000);
            continue;
        }

        err = tk_sock_listen(httpd_sn);
        if (err != E_OK) {
            tk_sock_close(httpd_sn);
            tk_dly_tsk(1000);
            continue;
        }

        /* 接続待ち (割り込み駆動 — CPU 解放) */
        err = tk_sock_accept(httpd_sn, 30000);
        if (err != E_OK) {
            tk_sock_close(httpd_sn);
            continue;
        }

        /* リクエスト受信 */
        W rcvd = tk_sock_recv(httpd_sn, httpd_rxbuf, HTTPD_RXBUF_SZ - 1, 5000);
        if (rcvd > 0) {
            httpd_rxbuf[rcvd] = '\0';

            /* パス抽出 */
            const char *path = httpd_parse_path((const char *)httpd_rxbuf, (UH)rcvd);

            if (path != NULL && httpd_handler != NULL) {
                /* パス終端を探す (スペースまで) */
                char path_buf[64];
                INT i;
                for (i = 0; i < 63 && path[i] != ' ' && path[i] != '\r'
                     && path[i] != '\0'; i++) {
                    path_buf[i] = path[i];
                }
                path_buf[i] = '\0';

                /* コールバックでコンテンツ生成 */
                T_HTTP_REQ req;
                req.path = path_buf;
                req.resp_buf = httpd_txbuf;
                req.resp_max = HTTPD_TXBUF_SZ;
                req.resp_len = 0;
                req.content_type = NULL;

                err = httpd_handler(&req);
                if (err == E_OK && req.resp_len > 0) {
                    httpd_send_response(httpd_sn, 200,
                                        req.content_type,
                                        httpd_txbuf, req.resp_len);
                } else {
                    const UB *msg = (const UB *)"Not Found";
                    httpd_send_response(httpd_sn, 404, NULL, msg, 9);
                }
            } else {
                const UB *msg = (const UB *)"Bad Request";
                httpd_send_response(httpd_sn, 400, NULL, msg, 11);
            }
        }

        /* TCP 切断 + クローズ */
        tk_sock_disconnect(httpd_sn, 3000);
    }

    tk_sock_close(httpd_sn);
    tk_ext_tsk();
}

/*======================================================================
 * 公開 API
 *====================================================================*/

EXPORT ER tk_httpd_start(UB sn, UH port, FP_HTTP_HANDLER handler)
{
    if (handler == NULL) return E_PAR;

    httpd_sn = sn;
    httpd_port = port;
    httpd_handler = handler;
    httpd_running = TRUE;

    T_CTSK ctsk;
    ctsk.exinf  = NULL;
    ctsk.tskatr = TA_HLNG | TA_RNG3;
    ctsk.task   = (FP)httpd_task;
    ctsk.itskpri = HTTPD_TASK_PRI;
    ctsk.stksz  = HTTPD_TASK_STKSZ;
    ctsk.bufptr = NULL;
    httpd_tskid = tk_cre_tsk(&ctsk);
    if (httpd_tskid < 0) return E_LIMIT;
    tk_sta_tsk(httpd_tskid, 0);

    return E_OK;
}

EXPORT void tk_httpd_stop(void)
{
    httpd_running = FALSE;

    if (httpd_tskid > 0) {
        INT retry;
        for (retry = 0; retry < 50; retry++) {
            T_RTSK rtsk;
            if (tk_ref_tsk(httpd_tskid, &rtsk) != E_OK) break;
            if (rtsk.tskstat == TTS_DMT) break;
            tk_dly_tsk(100);
        }
        tk_del_tsk(httpd_tskid);
        httpd_tskid = 0;
    }
}

#endif /* CPU_RP2040 */
