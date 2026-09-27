/*
 *----------------------------------------------------------------------
 *    tk_socket_lwip.c — tk_socket.h を lwIP raw API で実装
 *
 *    WIZnet の tk_socket.c に代わり、CYW43 WiFi + lwIP で
 *    同一の tk_sock_*() API を提供する。
 *
 *    スレッドモデル:
 *      アプリタスク → cyw43_lock → lwIP raw API → cyw43_unlock → tk_wai_flg
 *      BLE/WiFi タスク → cyw43_poll → lwIP callbacks → tk_set_flg
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef WIFI_CYW43
/* WIFI_CYW43 が定義されている場合のみコンパイル */

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

#include "cyw43.h"

/* tk_socket.h の API 定義 */
#include "../wiznet/tk_socket.h"

/* CYW43 ロック (cyw43_arch_tkernel.c) */
extern void cyw43_tkernel_lock(void);
extern void cyw43_tkernel_unlock(void);

/*======================================================================
 * ソケット制御ブロック
 *====================================================================*/

/* イベントフラグビット */
#define LWIP_EVT_RECV      (1u << 0)
#define LWIP_EVT_CON       (1u << 1)
#define LWIP_EVT_DISCON    (1u << 2)
#define LWIP_EVT_TIMEOUT   (1u << 3)
#define LWIP_EVT_SENDOK    (1u << 4)
#define LWIP_EVT_BREAK     (1u << 5)
#define LWIP_EVT_ACCEPT    (1u << 6)
#define LWIP_EVT_ERR       (1u << 7)

#define LWIP_SOCK_NUM      8

/* ソケット状態 (WIZnet SOCK_* と同値で互換性維持) */
#define LSOCK_CLOSED       0x00
#define LSOCK_INIT         0x13
#define LSOCK_LISTEN       0x14
#define LSOCK_ESTABLISHED  0x17
#define LSOCK_CLOSE_WAIT   0x1C
#define LSOCK_UDP          0x22

typedef struct {
    BOOL     in_use;
    UB       protocol;      /* 0x01=TCP, 0x02=UDP */
    UB       state;
    ID       flgid;         /* ソケット別イベントフラグ */
    struct tcp_pcb *tcp;
    struct udp_pcb *udp;

    /* TCP accept 用 */
    struct tcp_pcb *accepted_pcb;

    /* 受信バッファ (pbuf チェーン) */
    struct pbuf *rx_head;
    UH     rx_offset;       /* rx_head 内の読み取り位置 */

    /* UDP recvfrom 送信元情報 */
    ip_addr_t  udp_src_addr;
    UH         udp_src_port;

    ER         last_error;
} T_LWIP_SOCK;

LOCAL T_LWIP_SOCK lwip_sock[LWIP_SOCK_NUM];

/* select 用統合フラグ (tk_socket.h が extern) */
ID select_flgid = 0;

/*======================================================================
 * lwIP コールバック関数 (BLE/WiFi ポーリングタスク内で呼ばれる)
 *====================================================================*/

/* TCP: 受信コールバック */
LOCAL err_t lwip_tcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    T_LWIP_SOCK *sk = (T_LWIP_SOCK *)arg;
    if (sk == NULL) { if (p) pbuf_free(p); return ERR_ARG; }

    if (p == NULL) {
        /* 接続クローズ */
        sk->state = LSOCK_CLOSE_WAIT;
        tk_set_flg(sk->flgid, LWIP_EVT_DISCON);
        return ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

    /* pbuf チェーンの末尾に追加 */
    if (sk->rx_head == NULL) {
        sk->rx_head = p;
        sk->rx_offset = 0;
    } else {
        pbuf_cat(sk->rx_head, p);
    }

    tk_set_flg(sk->flgid, LWIP_EVT_RECV);
    return ERR_OK;
}

/* TCP: 送信完了コールバック */
LOCAL err_t lwip_tcp_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    T_LWIP_SOCK *sk = (T_LWIP_SOCK *)arg;
    if (sk != NULL) {
        tk_set_flg(sk->flgid, LWIP_EVT_SENDOK);
    }
    return ERR_OK;
}

/* TCP: 接続完了コールバック (tcp_connect の結果) */
LOCAL err_t lwip_tcp_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    T_LWIP_SOCK *sk = (T_LWIP_SOCK *)arg;
    if (sk == NULL) return ERR_ARG;

    if (err == ERR_OK) {
        sk->state = LSOCK_ESTABLISHED;
        tk_set_flg(sk->flgid, LWIP_EVT_CON);
    } else {
        sk->last_error = E_IO;
        tk_set_flg(sk->flgid, LWIP_EVT_ERR);
    }
    return ERR_OK;
}

/* TCP: エラーコールバック */
LOCAL void lwip_tcp_err_cb(void *arg, err_t err)
{
    T_LWIP_SOCK *sk = (T_LWIP_SOCK *)arg;
    if (sk == NULL) return;
    sk->last_error = (err == ERR_TIMEOUT) ? E_TMOUT : E_IO;
    sk->tcp = NULL;  /* PCB は lwIP が解放済み */
    sk->state = LSOCK_CLOSED;
    tk_set_flg(sk->flgid, LWIP_EVT_ERR | LWIP_EVT_DISCON);
}

/* TCP: accept コールバック */
LOCAL err_t lwip_tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    T_LWIP_SOCK *sk = (T_LWIP_SOCK *)arg;
    if (sk == NULL || err != ERR_OK) return ERR_VAL;
    sk->accepted_pcb = newpcb;
    tk_set_flg(sk->flgid, LWIP_EVT_ACCEPT);
    return ERR_OK;
}

/* UDP: 受信コールバック */
LOCAL void lwip_udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                            const ip_addr_t *addr, u16_t port)
{
    T_LWIP_SOCK *sk = (T_LWIP_SOCK *)arg;
    if (sk == NULL || p == NULL) { if (p) pbuf_free(p); return; }

    /* 送信元情報保存 */
    ip_addr_copy(sk->udp_src_addr, *addr);
    sk->udp_src_port = port;

    /* pbuf 追加 */
    if (sk->rx_head == NULL) {
        sk->rx_head = p;
        sk->rx_offset = 0;
    } else {
        pbuf_cat(sk->rx_head, p);
    }

    tk_set_flg(sk->flgid, LWIP_EVT_RECV);
}

/*======================================================================
 * イベント待ちヘルパー
 *====================================================================*/
LOCAL ER lwip_wait_event(T_LWIP_SOCK *sk, UINT wait_evt, TMO tmout)
{
    UINT flgptn;
    ER err = tk_wai_flg(sk->flgid,
                        wait_evt | LWIP_EVT_BREAK | LWIP_EVT_ERR | LWIP_EVT_DISCON,
                        TWF_ORW | TWF_BITCLR, &flgptn, tmout);
    if (err == E_TMOUT) return E_TMOUT;
    if (err != E_OK) return E_IO;
    if (flgptn & LWIP_EVT_BREAK) return E_ABORT;
    if (flgptn & LWIP_EVT_ERR) return sk->last_error;
    if (flgptn & LWIP_EVT_DISCON) return E_IO;
    return E_OK;
}

/*======================================================================
 * tk_socket.h API 実装
 *====================================================================*/

void tk_sock_init(void)
{
    INT i;
    memset(lwip_sock, 0, sizeof(lwip_sock));
    for (i = 0; i < LWIP_SOCK_NUM; i++) {
        T_CFLG cflg = { .exinf = NULL, .flgatr = TA_TPRI | TA_WMUL, .iflgptn = 0 };
        lwip_sock[i].flgid = tk_cre_flg(&cflg);
    }
    /* select 用統合フラグ */
    {
        T_CFLG cflg = { .exinf = NULL, .flgatr = TA_TPRI | TA_WMUL, .iflgptn = 0 };
        select_flgid = tk_cre_flg(&cflg);
    }
}

ER tk_sock_open(UB sn, UB protocol, UH port)
{
    if (sn >= LWIP_SOCK_NUM) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (sk->in_use) return E_OBJ;

    /* flgid は tk_sock_init() で作成済み — memset しない (flgid 保護) */
    sk->in_use = TRUE;
    sk->protocol = protocol;
    sk->state = LSOCK_CLOSED;
    sk->tcp = NULL;
    sk->udp = NULL;
    sk->accepted_pcb = NULL;
    sk->rx_head = NULL;
    sk->rx_offset = 0;
    sk->last_error = E_OK;
    ip_addr_set_zero(&sk->udp_src_addr);
    sk->udp_src_port = 0;

    cyw43_tkernel_lock();

    if (protocol == 0x01 /* TCP */) {
        struct tcp_pcb *pcb = tcp_new();
        if (pcb == NULL) { cyw43_tkernel_unlock(); sk->in_use = FALSE; return E_NOMEM; }
        if (port > 0) {
            err_t err = tcp_bind(pcb, IP_ADDR_ANY, port);
            if (err != ERR_OK) {
                tcp_close(pcb);
                cyw43_tkernel_unlock();
                sk->in_use = FALSE;
                return E_IO;
            }
        }
        tcp_arg(pcb, sk);
        tcp_recv(pcb, lwip_tcp_recv_cb);
        tcp_sent(pcb, lwip_tcp_sent_cb);
        tcp_err(pcb, lwip_tcp_err_cb);
        sk->tcp = pcb;
        sk->state = LSOCK_INIT;

    } else if (protocol == 0x02 /* UDP */) {
        struct udp_pcb *pcb = udp_new();
        if (pcb == NULL) { cyw43_tkernel_unlock(); sk->in_use = FALSE; return E_NOMEM; }
        if (port > 0) {
            err_t err = udp_bind(pcb, IP_ADDR_ANY, port);
            if (err != ERR_OK) {
                udp_remove(pcb);
                cyw43_tkernel_unlock();
                sk->in_use = FALSE;
                return E_IO;
            }
        }
        udp_recv(pcb, lwip_udp_recv_cb, sk);
        sk->udp = pcb;
        sk->state = LSOCK_UDP;

    } else {
        cyw43_tkernel_unlock();
        sk->in_use = FALSE;
        return E_PAR;
    }

    cyw43_tkernel_unlock();

    /* 前セッションの残留イベントクリア */
    tk_clr_flg(sk->flgid, 0);

    return E_OK;
}

ER tk_sock_close(UB sn)
{
    if (sn >= LWIP_SOCK_NUM) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (!sk->in_use) return E_OK;

    cyw43_tkernel_lock();

    if (sk->tcp != NULL) {
        tcp_arg(sk->tcp, NULL);
        tcp_recv(sk->tcp, NULL);
        tcp_sent(sk->tcp, NULL);
        tcp_err(sk->tcp, NULL);
        tcp_close(sk->tcp);
        sk->tcp = NULL;
    }
    if (sk->accepted_pcb != NULL) {
        tcp_close(sk->accepted_pcb);
        sk->accepted_pcb = NULL;
    }
    if (sk->udp != NULL) {
        udp_remove(sk->udp);
        sk->udp = NULL;
    }

    /* 受信バッファ解放 */
    if (sk->rx_head != NULL) {
        pbuf_free(sk->rx_head);
        sk->rx_head = NULL;
    }

    cyw43_tkernel_unlock();

    sk->state = LSOCK_CLOSED;
    sk->in_use = FALSE;

    return E_OK;
}

ER tk_sock_listen(UB sn)
{
    if (sn >= LWIP_SOCK_NUM) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (!sk->in_use || sk->tcp == NULL) return E_OBJ;

    cyw43_tkernel_lock();

    struct tcp_pcb *listen_pcb = tcp_listen(sk->tcp);
    if (listen_pcb == NULL) {
        cyw43_tkernel_unlock();
        return E_NOMEM;
    }
    sk->tcp = listen_pcb;
    tcp_arg(listen_pcb, sk);
    tcp_accept(listen_pcb, lwip_tcp_accept_cb);
    sk->state = LSOCK_LISTEN;

    cyw43_tkernel_unlock();
    return E_OK;
}

ER tk_sock_accept(UB sn, TMO tmout)
{
    if (sn >= LWIP_SOCK_NUM) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];

    if (sk->accepted_pcb != NULL) {
        /* 既に accept 済み */
        goto accepted;
    }

    {
        ER err = lwip_wait_event(sk, LWIP_EVT_ACCEPT, tmout);
        if (err != E_OK) return err;
    }

accepted:
    /* accepted_pcb を現在のソケットに移動 */
    cyw43_tkernel_lock();
    if (sk->accepted_pcb != NULL) {
        /* listen PCB を閉じて、accepted PCB に置換 */
        struct tcp_pcb *old = sk->tcp;
        sk->tcp = sk->accepted_pcb;
        sk->accepted_pcb = NULL;
        tcp_arg(sk->tcp, sk);
        tcp_recv(sk->tcp, lwip_tcp_recv_cb);
        tcp_sent(sk->tcp, lwip_tcp_sent_cb);
        tcp_err(sk->tcp, lwip_tcp_err_cb);
        sk->state = LSOCK_ESTABLISHED;
        /* old (listen PCB) は listen 状態のまま閉じる */
        tcp_close(old);
    }
    cyw43_tkernel_unlock();

    return E_OK;
}

ER tk_sock_connect(UB sn, UB *ip, UH port, TMO tmout)
{
    if (sn >= LWIP_SOCK_NUM || ip == NULL) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (!sk->in_use || sk->tcp == NULL) return E_OBJ;

    ip_addr_t addr;
    IP4_ADDR(&addr, ip[0], ip[1], ip[2], ip[3]);

    cyw43_tkernel_lock();
    err_t err = tcp_connect(sk->tcp, &addr, port, lwip_tcp_connected_cb);
    cyw43_tkernel_unlock();

    if (err != ERR_OK) return E_IO;

    /* 接続完了待ち */
    ER ret = lwip_wait_event(sk, LWIP_EVT_CON, tmout);
    if (ret != E_OK) {
        tk_sock_close(sn);
        return ret;
    }

    return E_OK;
}

ER tk_sock_disconnect(UB sn, TMO tmout)
{
    (void)tmout;
    return tk_sock_close(sn);
}

W tk_sock_send(UB sn, const UB *buf, UH len, TMO tmout)
{
    if (sn >= LWIP_SOCK_NUM || buf == NULL || len == 0) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (!sk->in_use || sk->tcp == NULL) return E_OBJ;
    if (sk->state != LSOCK_ESTABLISHED) return E_IO;

    cyw43_tkernel_lock();

    /* 送信可能サイズに制限 */
    u16_t sndbuf = tcp_sndbuf(sk->tcp);
    if (len > sndbuf) len = sndbuf;
    if (len == 0) {
        cyw43_tkernel_unlock();
        /* 送信バッファ空き待ち */
        ER err = lwip_wait_event(sk, LWIP_EVT_SENDOK, tmout);
        if (err != E_OK) return err;
        return 0;  /* リトライを促す */
    }

    err_t err = tcp_write(sk->tcp, buf, len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(sk->tcp);
    }

    cyw43_tkernel_unlock();

    if (err != ERR_OK) return E_IO;

    /* 送信完了待ち */
    ER ret = lwip_wait_event(sk, LWIP_EVT_SENDOK, tmout);
    if (ret != E_OK) return ret;

    return (W)len;
}

W tk_sock_recv(UB sn, UB *buf, UH len, TMO tmout)
{
    if (sn >= LWIP_SOCK_NUM || buf == NULL || len == 0) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (!sk->in_use) return E_OBJ;

    /* 受信データ待ち */
    while (sk->rx_head == NULL) {
        if (sk->state == LSOCK_CLOSE_WAIT || sk->state == LSOCK_CLOSED) return 0;
        ER err = lwip_wait_event(sk, LWIP_EVT_RECV | LWIP_EVT_DISCON, tmout);
        if (err == E_TMOUT) return E_TMOUT;
        if (err != E_OK) return E_IO;
    }

    /* pbuf チェーンからデータコピー */
    cyw43_tkernel_lock();

    UH copied = pbuf_copy_partial(sk->rx_head, buf, len, sk->rx_offset);
    sk->rx_offset += copied;

    /* 読み終わった pbuf を解放
     * pbuf_cat でチェーンされた pbuf は個別 ref 管理不要。
     * pbuf_free(head) は head のみ解放し、next のリファレンスカウントは
     * pbuf_cat 時に調整されているため明示的 pbuf_ref は不要。 */
    while (sk->rx_head != NULL && sk->rx_offset >= sk->rx_head->len) {
        sk->rx_offset -= sk->rx_head->len;
        struct pbuf *next = sk->rx_head->next;
        /* チェーン切断 + head のみ free (next は pbuf_cat で ref 済み) */
        sk->rx_head->next = NULL;
        pbuf_free(sk->rx_head);
        sk->rx_head = next;
    }

    /* TCP: 受信ウィンドウ更新 */
    if (sk->tcp != NULL && copied > 0) {
        tcp_recved(sk->tcp, copied);
    }

    cyw43_tkernel_unlock();

    return (W)copied;
}

W tk_sock_sendto(UB sn, const UB *buf, UH len, UB *addr, UH port, TMO tmout)
{
    (void)tmout;
    if (sn >= LWIP_SOCK_NUM || buf == NULL || addr == NULL) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (!sk->in_use || sk->udp == NULL) return E_OBJ;

    ip_addr_t dst;
    IP4_ADDR(&dst, addr[0], addr[1], addr[2], addr[3]);

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p == NULL) return E_NOMEM;
    memcpy(p->payload, buf, len);

    cyw43_tkernel_lock();
    err_t err = udp_sendto(sk->udp, p, &dst, port);
    cyw43_tkernel_unlock();

    pbuf_free(p);
    return (err == ERR_OK) ? (W)len : E_IO;
}

W tk_sock_recvfrom(UB sn, UB *buf, UH len, UB *addr, UH *port, TMO tmout)
{
    if (sn >= LWIP_SOCK_NUM || buf == NULL) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (!sk->in_use || sk->udp == NULL) return E_OBJ;

    /* 受信データ待ち */
    while (sk->rx_head == NULL) {
        ER err = lwip_wait_event(sk, LWIP_EVT_RECV, tmout);
        if (err == E_TMOUT) return E_TMOUT;
        if (err != E_OK) return E_IO;
    }

    cyw43_tkernel_lock();

    UH copied = pbuf_copy_partial(sk->rx_head, buf, len, 0);

    /* 送信元情報 */
    if (addr != NULL) {
        uint32_t a = ip_addr_get_ip4_u32(&sk->udp_src_addr);
        addr[0] = (a >> 0)  & 0xFF;
        addr[1] = (a >> 8)  & 0xFF;
        addr[2] = (a >> 16) & 0xFF;
        addr[3] = (a >> 24) & 0xFF;
    }
    if (port != NULL) {
        *port = sk->udp_src_port;
    }

    /* UDP は 1 データグラム = 1 pbuf チェーン、全て消費 */
    pbuf_free(sk->rx_head);
    sk->rx_head = NULL;
    sk->rx_offset = 0;

    cyw43_tkernel_unlock();

    return (W)copied;
}

ER tk_sock_set_keepalive(UB sn, UH sec)
{
    if (sn >= LWIP_SOCK_NUM) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (!sk->in_use || sk->tcp == NULL) return E_OBJ;

    /* lwIP TCP キープアライブ設定 */
    if (sec > 0) {
        ip_set_option(sk->tcp, SOF_KEEPALIVE);
        sk->tcp->keep_idle = (uint32_t)sec * 1000;
        sk->tcp->keep_intvl = 5000;
        sk->tcp->keep_cnt = 3;
    } else {
        ip_reset_option(sk->tcp, SOF_KEEPALIVE);
    }
    return E_OK;
}

ER tk_sock_break(UB sn)
{
    if (sn >= LWIP_SOCK_NUM) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (sk->flgid <= 0) return E_NOEXS;
    tk_set_flg(sk->flgid, LWIP_EVT_BREAK);
    return E_OK;
}

ER tk_sock_getpeer(UB sn, UB *ip, UH *port)
{
    if (sn >= LWIP_SOCK_NUM || ip == NULL || port == NULL) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (!sk->in_use || sk->tcp == NULL) return E_OBJ;

    uint32_t a = ip_addr_get_ip4_u32(&sk->tcp->remote_ip);
    ip[0] = (a >> 0) & 0xFF; ip[1] = (a >> 8) & 0xFF;
    ip[2] = (a >> 16) & 0xFF; ip[3] = (a >> 24) & 0xFF;
    *port = sk->tcp->remote_port;
    return E_OK;
}

ER tk_sock_getlocal(UB sn, UB *ip, UH *port)
{
    if (sn >= LWIP_SOCK_NUM || ip == NULL || port == NULL) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (!sk->in_use) return E_OBJ;

    if (sk->tcp != NULL) {
        uint32_t a = ip_addr_get_ip4_u32(&sk->tcp->local_ip);
        ip[0] = (a >> 0) & 0xFF; ip[1] = (a >> 8) & 0xFF;
        ip[2] = (a >> 16) & 0xFF; ip[3] = (a >> 24) & 0xFF;
        *port = sk->tcp->local_port;
    } else if (sk->udp != NULL) {
        uint32_t a = ip_addr_get_ip4_u32(&sk->udp->local_ip);
        ip[0] = (a >> 0) & 0xFF; ip[1] = (a >> 8) & 0xFF;
        ip[2] = (a >> 16) & 0xFF; ip[3] = (a >> 24) & 0xFF;
        *port = sk->udp->local_port;
    }
    return E_OK;
}

ER tk_net_sethostname(const char *name, INT len)
{
    (void)name; (void)len;
    return E_OK;  /* lwIP netif_set_hostname は別途対応 */
}

ER tk_net_gethostname(char *name, INT len)
{
    if (name == NULL || len <= 0) return E_PAR;
    name[0] = '\0';
    return E_OK;
}

W tk_sock_available(UB sn)
{
    if (sn >= LWIP_SOCK_NUM) return E_PAR;
    T_LWIP_SOCK *sk = &lwip_sock[sn];
    if (!sk->in_use) return 0;
    if (sk->rx_head == NULL) return 0;
    return (W)(sk->rx_head->tot_len - sk->rx_offset);
}

ER tk_sock_select(UB sn_mask, UINT evt_mask, UINT *result, TMO tmout)
{
    (void)sn_mask; (void)evt_mask; (void)result; (void)tmout;
    return E_NOSPT;  /* 簡易実装: select は未対応 */
}

ER tk_sock_getopt(UB sn, INT optname, void *optval, INT *optlen)
{
    (void)sn; (void)optname; (void)optval; (void)optlen;
    return E_NOSPT;
}

ER tk_sock_setopt(UB sn, INT optname, const void *optval, INT optlen)
{
    (void)sn; (void)optname; (void)optval; (void)optlen;
    return E_NOSPT;
}

#endif /* WIFI_CYW43 */
