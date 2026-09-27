/*
 *----------------------------------------------------------------------
 *    μT-Kernel Native DHCP Client
 *
 *    ioLibrary dhcp.c (1100行) を μT-Kernel ネイティブに書き直し。
 *
 *    RTOS 機能:
 *    - 専用 DHCP タスク: DHCP ステートマシンをタスクとして実行
 *    - 周期ハンドラ (tk_cre_cyc): 1 秒間隔でイベントフラグ通知
 *    - tk_sock_sendto/recvfrom: 割り込み駆動の UDP 送受信
 *    - tk_dly_tsk: IP 競合チェック時の 2 秒待ちをビジーウェイトなしで実現
 *    - イベントフラグ: タイマーティック通知
 *
 *    メッセージフォーマットは RFC 2131/1533 準拠。
 *    W5100S レジスタアクセスには ioLibrary の w5100s.h マクロを使用。
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(MTKBSP_CPU_STM32H5)

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>
#include <string.h>

#if defined(WIZCHIP_W5500)
#include "wiznet_drv.h"
#else
#include "w5100s_reg.h"
#endif
#include "tk_socket.h"
#include "tk_dhcp.h"

/*----------------------------------------------------------------------
 * 定数
 */
#define DHCP_SERVER_PORT	67
#define DHCP_CLIENT_PORT	68
#define DHCP_WAIT_TIME		10	/* 状態タイムアウト (秒) */
#define MAX_DHCP_RETRY		2
#define MAGIC_COOKIE		0x63825363
#define RIP_MSG_SIZE		548	/* 236 + 312(OPT) */
#define OPT_SIZE		312
#define INFINITE_LEASETIME	0xFFFFFFFF

/* DHCP message op */
#define DHCP_BOOTREQUEST	1
#define DHCP_BOOTREPLY		2

/* DHCP message type */
#define DHCP_DISCOVER		1
#define DHCP_OFFER		2
#define DHCP_REQUEST		3
#define DHCP_DECLINE		4
#define DHCP_ACK		5
#define DHCP_NAK		6

/* DHCP options (RFC 1533) */
#define OPT_SUBNET_MASK		1
#define OPT_ROUTER		3
#define OPT_DNS			6
#define OPT_HOST_NAME		12
#define OPT_REQUESTED_IP	50
#define OPT_LEASE_TIME		51
#define OPT_MSG_TYPE		53
#define OPT_SERVER_ID		54
#define OPT_PARAM_REQUEST	55
#define OPT_CLIENT_ID		61
#define OPT_END			255

/* ステートマシン */
#define ST_INIT			0
#define ST_DISCOVER		1
#define ST_REQUEST		2
#define ST_LEASED		3
#define ST_REREQUEST		4
#define ST_STOP			5

/* タスク設定 */
#define DHCP_TASK_PRI		10
#define DHCP_TASK_STKSZ		1024

/* タイマーイベント */
#define DHCP_EVT_TICK		(1u << 0)

/*----------------------------------------------------------------------
 * DHCP メッセージ構造体 (RFC 2131)
 */
typedef struct {
    UB  op;
    UB  htype;
    UB  hlen;
    UB  hops;
    UB  xid[4];
    UB  secs[2];
    UB  flags[2];
    UB  ciaddr[4];
    UB  yiaddr[4];
    UB  siaddr[4];
    UB  giaddr[4];
    UB  chaddr[16];
    UB  sname[64];
    UB  file[128];
    UB  opt[OPT_SIZE];
} T_RIP_MSG;

/*----------------------------------------------------------------------
 * モジュール変数
 */
LOCAL UB dhcp_sn;			/* 使用ソケット番号 */
LOCAL volatile UB dhcp_state;	/* volatile: tk_dhcp_stop() から別タスク書込み */
LOCAL UB dhcp_retry;
LOCAL volatile UW dhcp_tick;		/* 1秒カウンタ (タスクコンテキストで更新、タイムアウト判定で参照) */
LOCAL UW dhcp_tick_next;		/* タイムアウト目標 */
LOCAL UW dhcp_lease_time;
LOCAL UW dhcp_xid;

LOCAL UB dhcp_mac[6];			/* クライアント MAC */
LOCAL UB dhcp_sip[4];			/* DHCP サーバー IP */
LOCAL UB dhcp_offer_ip[4];		/* OFFER で提示された IP (REQUEST 用) */
LOCAL T_DHCP_INFO dhcp_result;		/* 取得結果 (ACK 後に確定) */
LOCAL T_DHCP_INFO *dhcp_result_ptr;	/* ユーザー結果格納先 */

LOCAL T_RIP_MSG dhcp_msg;		/* 送受信バッファ */

LOCAL void (*cb_ip_assign)(void);
LOCAL void (*cb_ip_update)(void);
LOCAL void (*cb_ip_conflict)(void);

LOCAL ID dhcp_tskid;
LOCAL ID dhcp_cycid;
LOCAL ID dhcp_flgid;

/*----------------------------------------------------------------------
 * ヘルパー: nibble → hex 文字
 */
LOCAL UB nibble_to_hex(UB n)
{
    n &= 0x0F;
    return (n < 10) ? ('0' + n) : ('a' + n - 10);
}

/*----------------------------------------------------------------------
 * 共通メッセージ初期化
 */
LOCAL void make_dhcp_msg(void)
{
    memset(&dhcp_msg, 0, sizeof(dhcp_msg));
    dhcp_msg.op    = DHCP_BOOTREQUEST;
    dhcp_msg.htype = 1;	/* 10Mb Ethernet */
    dhcp_msg.hlen  = 6;
    dhcp_msg.xid[0] = (UB)(dhcp_xid >> 24);
    dhcp_msg.xid[1] = (UB)(dhcp_xid >> 16);
    dhcp_msg.xid[2] = (UB)(dhcp_xid >> 8);
    dhcp_msg.xid[3] = (UB)(dhcp_xid);
    dhcp_msg.flags[0] = 0x80;	/* broadcast */
    memcpy(dhcp_msg.chaddr, dhcp_mac, 6);

    /* Magic Cookie */
    dhcp_msg.opt[0] = (UB)(MAGIC_COOKIE >> 24);
    dhcp_msg.opt[1] = (UB)(MAGIC_COOKIE >> 16);
    dhcp_msg.opt[2] = (UB)(MAGIC_COOKIE >> 8);
    dhcp_msg.opt[3] = (UB)(MAGIC_COOKIE);
}

/*----------------------------------------------------------------------
 * ホスト名をオプションに追加
 */
LOCAL UH add_hostname_opt(UB *opt, UH k)
{
    UH len_pos;
    INT i;
    const UB *host = (const UB *)"uTK3";

    opt[k++] = OPT_HOST_NAME;
    len_pos = k;
    opt[k++] = 0;  /* length placeholder */
    for (i = 0; host[i] != 0; i++) {
        opt[k++] = host[i];
    }
    /* MAC 下位3バイトを hex 追加 */
    opt[k++] = nibble_to_hex(dhcp_mac[3] >> 4);
    opt[k++] = nibble_to_hex(dhcp_mac[3]);
    opt[k++] = nibble_to_hex(dhcp_mac[4] >> 4);
    opt[k++] = nibble_to_hex(dhcp_mac[4]);
    opt[k++] = nibble_to_hex(dhcp_mac[5] >> 4);
    opt[k++] = nibble_to_hex(dhcp_mac[5]);
    opt[len_pos] = (UB)(k - len_pos - 1);
    return k;
}

/*----------------------------------------------------------------------
 * DISCOVER 送信
 */
LOCAL void send_discover(void)
{
    UB bcast[4] = {255, 255, 255, 255};
    UH k = 4;  /* Magic Cookie 後 */

    make_dhcp_msg();
    memset(dhcp_sip, 0, 4);

    dhcp_msg.opt[k++] = OPT_MSG_TYPE;
    dhcp_msg.opt[k++] = 1;
    dhcp_msg.opt[k++] = DHCP_DISCOVER;

    /* Client Identifier */
    dhcp_msg.opt[k++] = OPT_CLIENT_ID;
    dhcp_msg.opt[k++] = 7;
    dhcp_msg.opt[k++] = 1;  /* hw type: ethernet */
    memcpy(&dhcp_msg.opt[k], dhcp_mac, 6); k += 6;

    k = add_hostname_opt(dhcp_msg.opt, k);

    /* Parameter Request */
    dhcp_msg.opt[k++] = OPT_PARAM_REQUEST;
    dhcp_msg.opt[k++] = 4;
    dhcp_msg.opt[k++] = OPT_SUBNET_MASK;
    dhcp_msg.opt[k++] = OPT_ROUTER;
    dhcp_msg.opt[k++] = OPT_DNS;
    dhcp_msg.opt[k++] = OPT_LEASE_TIME;

    dhcp_msg.opt[k++] = OPT_END;

    W ret = tk_sock_sendto(dhcp_sn, (const UB *)&dhcp_msg, RIP_MSG_SIZE,
                   bcast, DHCP_SERVER_PORT, 5000);
    W5DBG("DHCP: sendto ret=%d\n", ret);
}

/*----------------------------------------------------------------------
 * REQUEST 送信
 */
LOCAL void send_request(void)
{
    UB ip[4];
    UH k = 4;

    make_dhcp_msg();

    if (dhcp_state == ST_LEASED || dhcp_state == ST_REREQUEST) {
        /* Renewal: unicast to server */
        dhcp_msg.flags[0] = 0x00;
        memcpy(dhcp_msg.ciaddr, dhcp_result.ip, 4);
        memcpy(ip, dhcp_sip, 4);
    } else {
        /* Initial: broadcast */
        memset(ip, 255, 4);
    }

    dhcp_msg.opt[k++] = OPT_MSG_TYPE;
    dhcp_msg.opt[k++] = 1;
    dhcp_msg.opt[k++] = DHCP_REQUEST;

    dhcp_msg.opt[k++] = OPT_CLIENT_ID;
    dhcp_msg.opt[k++] = 7;
    dhcp_msg.opt[k++] = 1;
    memcpy(&dhcp_msg.opt[k], dhcp_mac, 6); k += 6;

    if (dhcp_state != ST_LEASED && dhcp_state != ST_REREQUEST) {
        /* Requested IP */
        dhcp_msg.opt[k++] = OPT_REQUESTED_IP;
        dhcp_msg.opt[k++] = 4;
        memcpy(&dhcp_msg.opt[k], dhcp_offer_ip, 4); k += 4;

        /* Server Identifier */
        dhcp_msg.opt[k++] = OPT_SERVER_ID;
        dhcp_msg.opt[k++] = 4;
        memcpy(&dhcp_msg.opt[k], dhcp_sip, 4); k += 4;
    }

    k = add_hostname_opt(dhcp_msg.opt, k);

    dhcp_msg.opt[k++] = OPT_PARAM_REQUEST;
    dhcp_msg.opt[k++] = 4;
    dhcp_msg.opt[k++] = OPT_SUBNET_MASK;
    dhcp_msg.opt[k++] = OPT_ROUTER;
    dhcp_msg.opt[k++] = OPT_DNS;
    dhcp_msg.opt[k++] = OPT_LEASE_TIME;

    dhcp_msg.opt[k++] = OPT_END;

    tk_sock_sendto(dhcp_sn, (const UB *)&dhcp_msg, RIP_MSG_SIZE,
                   ip, DHCP_SERVER_PORT, 5000);
}

/*----------------------------------------------------------------------
 * DECLINE 送信
 */
LOCAL void send_decline(void)
{
    UB bcast[4] = {255, 255, 255, 255};
    UH k = 4;

    make_dhcp_msg();
    dhcp_msg.flags[0] = 0x00;

    dhcp_msg.opt[k++] = OPT_MSG_TYPE;
    dhcp_msg.opt[k++] = 1;
    dhcp_msg.opt[k++] = DHCP_DECLINE;

    dhcp_msg.opt[k++] = OPT_REQUESTED_IP;
    dhcp_msg.opt[k++] = 4;
    memcpy(&dhcp_msg.opt[k], dhcp_result.ip, 4); k += 4;

    dhcp_msg.opt[k++] = OPT_SERVER_ID;
    dhcp_msg.opt[k++] = 4;
    memcpy(&dhcp_msg.opt[k], dhcp_sip, 4); k += 4;

    dhcp_msg.opt[k++] = OPT_END;

    tk_sock_sendto(dhcp_sn, (const UB *)&dhcp_msg, RIP_MSG_SIZE,
                   bcast, DHCP_SERVER_PORT, 5000);
}

/*----------------------------------------------------------------------
 * DHCP 応答パース
 * 戻り値: DHCP メッセージタイプ (OFFER=2, ACK=5, NAK=6), 0=データなし/無効
 */
LOCAL UB parse_response(void)
{
    UB svr_addr[4];
    UH svr_port;

    /*
     * ノンブロッキング受信 (タイムアウト 0) — データなければ即リターン。
     *
     * 意図的なポーリング: DHCP ステートマシンは 1 秒周期で駆動されるため、
     * RECV イベントで即座に起床する必要がない。毎秒のティックで
     * 「データが来ていれば処理、なければタイムアウト判定」で十分であり、
     * 専用の受信待ちイベントを追加すると状態遷移が複雑化する。
     */
    UH rxrsr = getSn_RX_RSR(dhcp_sn);
    W len = tk_sock_recvfrom(dhcp_sn, (UB *)&dhcp_msg, sizeof(dhcp_msg),
                             svr_addr, &svr_port, 0);
    if (len <= 0) {
        if (dhcp_tick % 5 == 0) {
            W5DBG("DHCP: tick=%d rxrsr=%d sr=0x%02x\n",
                  dhcp_tick, rxrsr, getSn_SR(dhcp_sn));
        }
        return 0;
    }

    W5DBG("DHCP: recv %d bytes from %d.%d.%d.%d:%d\n",
          len, svr_addr[0], svr_addr[1], svr_addr[2], svr_addr[3], svr_port);

    /* 基本バリデーション */
    if (svr_port != DHCP_SERVER_PORT) { W5DBG("DHCP: bad port\n"); return 0; }
    if (dhcp_msg.op != DHCP_BOOTREPLY) { W5DBG("DHCP: bad op %d\n", dhcp_msg.op); return 0; }
    if (memcmp(dhcp_msg.chaddr, dhcp_mac, 6) != 0) { W5DBG("DHCP: bad chaddr\n"); return 0; }

    /* XID 確認 */
    UW rxid = ((UW)dhcp_msg.xid[0] << 24) | ((UW)dhcp_msg.xid[1] << 16) |
              ((UW)dhcp_msg.xid[2] << 8) | dhcp_msg.xid[3];
    if (rxid != dhcp_xid) { W5DBG("DHCP: bad xid\n"); return 0; }

    /* Magic Cookie 確認 */
    if (dhcp_msg.opt[0] != 0x63 || dhcp_msg.opt[1] != 0x82 ||
        dhcp_msg.opt[2] != 0x53 || dhcp_msg.opt[3] != 0x63) return 0;

    /* オプション解析 */
    UB msg_type = 0;
    UB *p = &dhcp_msg.opt[4];
    UB *end = &dhcp_msg.opt[OPT_SIZE];

    while (p < end && *p != OPT_END) {
        if (*p == 0) { p++; continue; }  /* pad */

        UB opt_code = *p++;
        if (p >= end) break;
        UB opt_len = *p++;
        if (p + opt_len > end) break;

        switch (opt_code) {
        case OPT_MSG_TYPE:
            msg_type = *p;
            break;
        case OPT_SUBNET_MASK:
            if (opt_len >= 4) memcpy(dhcp_result.sn, p, 4);
            break;
        case OPT_ROUTER:
            if (opt_len >= 4) memcpy(dhcp_result.gw, p, 4);
            break;
        case OPT_DNS:
            if (opt_len >= 4) memcpy(dhcp_result.dns, p, 4);
            break;
        case OPT_LEASE_TIME:
            if (opt_len >= 4) {
                dhcp_lease_time = ((UW)p[0] << 24) | ((UW)p[1] << 16) |
                                  ((UW)p[2] << 8) | p[3];
            }
            break;
        case OPT_SERVER_ID:
            if (opt_len >= 4) memcpy(dhcp_sip, p, 4);
            break;
        }
        p += opt_len;
    }

    /* yiaddr → 割当 IP (ACK のみ確定。OFFER 時は仮保持) */
    if (msg_type == DHCP_OFFER) {
        /* OFFER の yiaddr は REQUEST に使うが dhcp_result には入れない。
         * dhcp_result.ip は ACK 後に確定する (RFC 2131)。*/
        memcpy(dhcp_offer_ip, dhcp_msg.yiaddr, 4);
    } else if (msg_type == DHCP_ACK) {
        memcpy(dhcp_result.ip, dhcp_msg.yiaddr, 4);
    }

    dhcp_result.lease_time = dhcp_lease_time;

    return msg_type;
}

/*----------------------------------------------------------------------
 * 周期ハンドラ (1 秒間隔, ISR コンテキスト)
 * イベントフラグで DHCP タスクに通知するだけ。
 */
LOCAL void dhcp_cyc_handler(void *exinf)
{
    (void)exinf;
    if (dhcp_flgid > 0) {
        tk_set_flg(dhcp_flgid, DHCP_EVT_TICK);
    }
}

/*----------------------------------------------------------------------
 * DHCP タスク
 *
 * ioLibrary の DHCP_run() ポーリングループを、
 * μT-Kernel タスクのイベント駆動ループに置き換え。
 *
 * タイマーティック (1秒) を周期ハンドラからイベントフラグで受け取り、
 * ステートマシンを駆動する。待機中は CPU を他タスクに譲る。
 */
LOCAL void dhcp_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

    /* ソケットオープン (UDP, ポート 68) */
    ER err = tk_sock_open(dhcp_sn, Sn_MR_UDP, DHCP_CLIENT_PORT);
    if (err != E_OK) {
        W5DBG("DHCP: socket open failed\n");
        tk_ext_tsk();
    }

    dhcp_state = ST_INIT;

    while (dhcp_state != ST_STOP) {
        switch (dhcp_state) {

        case ST_INIT:
            dhcp_xid++;
            dhcp_tick = 0;
            dhcp_tick_next = DHCP_WAIT_TIME;
            dhcp_retry = 0;
            memset(dhcp_offer_ip, 0, 4);  /* 前回の OFFER をクリア */
            send_discover();
            dhcp_state = ST_DISCOVER;
            W5DBG("DHCP: DISCOVER sent\n");
            break;

        case ST_DISCOVER: {
            UB type = parse_response();
            if (type == DHCP_OFFER) {
                W5DBG("DHCP: OFFER %d.%d.%d.%d\n",
                          dhcp_result.ip[0], dhcp_result.ip[1],
                          dhcp_result.ip[2], dhcp_result.ip[3]);
                send_request();
                dhcp_tick = 0;
                dhcp_tick_next = DHCP_WAIT_TIME;
                dhcp_retry = 0;
                dhcp_state = ST_REQUEST;
            } else if (dhcp_tick >= dhcp_tick_next) {
                if (++dhcp_retry > MAX_DHCP_RETRY) {
                    W5DBG("DHCP: timeout\n");
                    dhcp_state = ST_STOP;
                } else {
                    dhcp_tick = 0;
                    send_discover();
                }
            }
            break;
        }

        case ST_REQUEST: {
            UB type = parse_response();
            if (type == DHCP_ACK) {
                W5DBG("DHCP: ACK %d.%d.%d.%d (lease %ds)\n",
                          dhcp_result.ip[0], dhcp_result.ip[1],
                          dhcp_result.ip[2], dhcp_result.ip[3],
                          dhcp_result.lease_time);

                /*
                 * IP 競合チェック: tk_dly_tsk() で 2 秒待ち
                 * (ioLibrary の busy-wait `while ((dhcp_tick_1s - ret) < 2);` を排除)
                 */
                tk_dly_tsk(2000);

                /* 結果を適用 */
                setSIPR(dhcp_result.ip);
                setSUBR(dhcp_result.sn);
                setGAR(dhcp_result.gw);

                if (dhcp_result_ptr != NULL) {
                    memcpy(dhcp_result_ptr, &dhcp_result, sizeof(T_DHCP_INFO));
                }
                if (cb_ip_assign != NULL) cb_ip_assign();

                dhcp_tick = 0;
                dhcp_state = ST_LEASED;
                W5DBG("DHCP: LEASED\n");

            } else if (type == DHCP_NAK) {
                W5DBG("DHCP: NAK, restart\n");
                dhcp_state = ST_INIT;

            } else if (dhcp_tick >= dhcp_tick_next) {
                if (++dhcp_retry > MAX_DHCP_RETRY) {
                    dhcp_state = ST_INIT;
                } else {
                    dhcp_tick = 0;
                    send_request();
                }
            }
            break;
        }

        case ST_LEASED:
            /* T1 (リース時間の半分) でリニューアル */
            if (dhcp_lease_time != INFINITE_LEASETIME &&
                dhcp_tick >= dhcp_lease_time / 2) {
                W5DBG("DHCP: T1 renewal\n");
                send_request();
                dhcp_tick = 0;
                dhcp_tick_next = DHCP_WAIT_TIME;
                dhcp_retry = 0;
                dhcp_state = ST_REREQUEST;
            }
            break;

        case ST_REREQUEST: {
            UB type = parse_response();
            if (type == DHCP_ACK) {
                UB old_ip[4];
                memcpy(old_ip, dhcp_result.ip, 4);

                /* yiaddr で IP 更新済み */
                setSIPR(dhcp_result.ip);
                setSUBR(dhcp_result.sn);
                setGAR(dhcp_result.gw);

                if (dhcp_result_ptr != NULL) {
                    memcpy(dhcp_result_ptr, &dhcp_result, sizeof(T_DHCP_INFO));
                }
                if (memcmp(old_ip, dhcp_result.ip, 4) != 0) {
                    if (cb_ip_update != NULL) cb_ip_update();
                }

                dhcp_tick = 0;
                dhcp_state = ST_LEASED;
                W5DBG("DHCP: renewed\n");

            } else if (type == DHCP_NAK) {
                dhcp_state = ST_INIT;

            } else if (dhcp_tick >= dhcp_tick_next) {
                if (++dhcp_retry > MAX_DHCP_RETRY) {
                    dhcp_state = ST_INIT;
                } else {
                    dhcp_tick = 0;
                    send_request();
                }
            }
            break;
        }
        } /* switch */

        /*
         * 1 秒ティック待ち (イベントフラグ)
         * 周期ハンドラが毎秒セットする。
         * 待機中タスクは CPU を他タスクに完全に譲る。
         */
        UINT flgptn;
        tk_wai_flg(dhcp_flgid, DHCP_EVT_TICK,
                   TWF_ORW | TWF_BITCLR, &flgptn, 1000);
        dhcp_tick++;
    }

    tk_sock_close(dhcp_sn);
    W5DBG("DHCP: stopped\n");
    tk_ext_tsk();
}

/*======================================================================
 * 公開 API
 *====================================================================*/

EXPORT ER tk_dhcp_start(UB sn, const T_DHCP_CB *cb, T_DHCP_INFO *info)
{
    dhcp_sn = sn;
    dhcp_result_ptr = info;
    dhcp_lease_time = INFINITE_LEASETIME;
    dhcp_xid = 0x12345678;
    memset(&dhcp_result, 0, sizeof(dhcp_result));

    /* コールバック設定 */
    cb_ip_assign  = (cb && cb->ip_assign)  ? cb->ip_assign  : NULL;
    cb_ip_update  = (cb && cb->ip_update)  ? cb->ip_update  : NULL;
    cb_ip_conflict = (cb && cb->ip_conflict) ? cb->ip_conflict : NULL;

    /* MAC アドレス取得 */
    getSHAR(dhcp_mac);

    /* XID に MAC 下位 4byte を混ぜる */
    dhcp_xid ^= ((UW)dhcp_mac[2] << 24) | ((UW)dhcp_mac[3] << 16) |
                ((UW)dhcp_mac[4] << 8) | dhcp_mac[5];

    /* イベントフラグ生成 */
    {
        T_CFLG cflg;
        cflg.exinf  = NULL;
        cflg.flgatr = TA_TPRI | TA_WMUL;
        cflg.iflgptn = 0;
        dhcp_flgid = tk_cre_flg(&cflg);
        if (dhcp_flgid < 0) return E_LIMIT;
    }

    /* 1 秒周期ハンドラ生成 */
    {
        T_CCYC ccyc;
        ccyc.exinf  = NULL;
        ccyc.cycatr = TA_HLNG;
        ccyc.cyctim = 1000;	/* 1000ms = 1秒 */
        ccyc.cycphs = 0;
        ccyc.cychdr = (FP)dhcp_cyc_handler;
        dhcp_cycid = tk_cre_cyc(&ccyc);
        if (dhcp_cycid < 0) return E_LIMIT;
        tk_sta_cyc(dhcp_cycid);
    }

    /* DHCP タスク生成・起動 */
    {
        T_CTSK ctsk;
        ctsk.exinf  = NULL;
        ctsk.tskatr = TA_HLNG | TA_RNG3;
        ctsk.task   = (FP)dhcp_task;
        ctsk.itskpri = DHCP_TASK_PRI;
        ctsk.stksz  = DHCP_TASK_STKSZ;
        ctsk.bufptr = NULL;
        dhcp_tskid = tk_cre_tsk(&ctsk);
        if (dhcp_tskid < 0) return E_LIMIT;
        tk_sta_tsk(dhcp_tskid, 0);
    }

    return E_OK;
}

EXPORT void tk_dhcp_stop(void)
{
    dhcp_state = ST_STOP;

    /* 周期ハンドラ停止 */
    if (dhcp_cycid > 0) {
        tk_stp_cyc(dhcp_cycid);
        tk_del_cyc(dhcp_cycid);
        dhcp_cycid = 0;
    }

    /*
     * タスクを安全に停止する。
     * tk_ter_tsk() は強制終了でソケットが閉じられないまま残るため使わない。
     * イベントフラグをセットして tk_wai_flg() から起床させ、
     * タスク自身が ST_STOP を検出して tk_sock_close → tk_ext_tsk する。
     * tk_ref_tsk でタスク終了 (DORMANT) を確認してからリソース削除する。
     */
    if (dhcp_tskid > 0 && dhcp_flgid > 0) {
        tk_set_flg(dhcp_flgid, DHCP_EVT_TICK);  /* ループを回す */

        /* タスクが自発終了するのを待つ (最大 10 秒) */
        INT retry;
        for (retry = 0; retry < 100; retry++) {
            T_RTSK rtsk;
            if (tk_ref_tsk(dhcp_tskid, &rtsk) != E_OK) break;
            if (rtsk.tskstat == TTS_DMT) break;  /* DORMANT = 終了済み */
            tk_dly_tsk(100);
        }
        tk_del_tsk(dhcp_tskid);
        dhcp_tskid = 0;
    }
    if (dhcp_flgid > 0) {
        tk_del_flg(dhcp_flgid);
        dhcp_flgid = 0;
    }
}

EXPORT void tk_dhcp_get_info(T_DHCP_INFO *info)
{
    if (info != NULL) {
        memcpy(info, &dhcp_result, sizeof(T_DHCP_INFO));
    }
}

EXPORT INT tk_dhcp_get_state(void)
{
    return (INT)dhcp_state;
}

#endif /* CPU_RP2040 || MTKBSP_CPU_STM32H5 */
