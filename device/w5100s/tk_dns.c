/*
 *----------------------------------------------------------------------
 *    μT-Kernel Native DNS Client
 *
 *    RFC 1035 準拠の DNS スタブリゾルバ。
 *    UDP ソケット 1 往復で A レコード (IPv4) を解決する。
 *
 *    RTOS 機能:
 *    - tk_sock_sendto/recvfrom: 割り込み駆動の UDP 送受信
 *    - ブロッキング API: 呼び出しタスクは応答まで CPU を解放
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <string.h>

#include "w5100s_reg.h"
#include "tk_socket.h"
#include "tk_dns.h"
#include "../include/dev_w5100s.h"

/* DNS 定数 */
#define DNS_PORT        53
#define DNS_MSG_MAXSZ   512
#define DNS_FLAG_QR     0x8000  /* Response */
#define DNS_FLAG_RD     0x0100  /* Recursion Desired */
#define DNS_TYPE_A      1       /* IPv4 address */
#define DNS_CLASS_IN    1       /* Internet */

/* DNS ヘッダ (12 byte) */
typedef struct {
    UH id;
    UH flags;
    UH qdcount;
    UH ancount;
    UH nscount;
    UH arcount;
} T_DNS_HDR;

/* トランザクション ID */
LOCAL UH dns_txid = 0x1234;

/*----------------------------------------------------------------------
 * ホスト名を DNS ワイヤフォーマットに変換
 * "www.example.com" → "\x03www\x07example\x03com\x00"
 * 戻り値: 書き込みバイト数
 */
LOCAL UH dns_encode_name(UB *dst, const char *hostname)
{
    UH pos = 0;
    const char *p = hostname;

    /*
     * DNS ドメイン名の最大長は 253 文字 (RFC 1035)。
     * ワイヤフォーマットでは各ラベルの長さバイト + 終端 0 が加わるため
     * 最大 255 バイト。バッファオーバーフロー防止のため上限チェック。
     */
    #define DNS_NAME_MAX  255

    while (*p) {
        if (pos >= DNS_NAME_MAX - 1) return 0;  /* オーバーフロー防止 */

        /* ラベル長の位置を記録 */
        UH label_pos = pos++;
        UH label_len = 0;

        while (*p && *p != '.') {
            if (pos >= DNS_NAME_MAX - 1) return 0;
            dst[pos++] = (UB)*p++;
            label_len++;
        }
        /* DNS ラベル長は最大 63 バイト (RFC 1035) */
        if (label_len > 63) return 0;
        dst[label_pos] = (UB)label_len;

        if (*p == '.') p++;
    }
    dst[pos++] = 0;  /* 終端 */
    return pos;
}

/*----------------------------------------------------------------------
 * 16bit big-endian 書き込み/読み取り
 */
LOCAL void put_be16(UB *p, UH val)
{
    p[0] = (UB)(val >> 8);
    p[1] = (UB)(val);
}

LOCAL UH get_be16(const UB *p)
{
    return ((UH)p[0] << 8) | p[1];
}

/*----------------------------------------------------------------------
 * DNS クエリ組み立て
 * 戻り値: メッセージサイズ
 */
LOCAL UH dns_build_query(UB *buf, const char *hostname, UH txid)
{
    UH pos = 0;

    /* ヘッダ (12 byte) */
    put_be16(&buf[0], txid);
    put_be16(&buf[2], DNS_FLAG_RD);     /* RD=1 */
    put_be16(&buf[4], 1);               /* QDCOUNT=1 */
    put_be16(&buf[6], 0);               /* ANCOUNT */
    put_be16(&buf[8], 0);               /* NSCOUNT */
    put_be16(&buf[10], 0);              /* ARCOUNT */
    pos = 12;

    /* Question: QNAME + QTYPE + QCLASS */
    UH name_len = dns_encode_name(&buf[pos], hostname);
    if (name_len == 0) return 0;  /* ホスト名が長すぎる */
    pos += name_len;
    put_be16(&buf[pos], DNS_TYPE_A);    pos += 2;
    put_be16(&buf[pos], DNS_CLASS_IN);  pos += 2;

    return pos;
}

/*----------------------------------------------------------------------
 * DNS 応答パース
 * 戻り値: E_OK=成功, E_IO=パースエラー/NXDOMAIN
 */
LOCAL ER dns_parse_response(const UB *buf, UH len, UH txid, UB *result)
{
    if (len < 12) return E_IO;

    /* ヘッダ検証 */
    UH id    = get_be16(&buf[0]);
    UH flags = get_be16(&buf[2]);
    UH ancount = get_be16(&buf[6]);

    if (id != txid) return E_IO;
    if (!(flags & DNS_FLAG_QR)) return E_IO;        /* Not a response */
    if ((flags & 0x000F) != 0) return E_IO;         /* RCODE != 0 (NOERROR) */
    if (ancount == 0) return E_IO;                   /* No answers */

    /* Question セクションをスキップ */
    UH pos = 12;
    UH qdcount = get_be16(&buf[4]);
    UH i;
    for (i = 0; i < qdcount; i++) {
        /* QNAME をスキップ */
        while (pos < len && buf[pos] != 0) {
            if ((buf[pos] & 0xC0) == 0xC0) {
                pos += 2;  /* 圧縮ポインタ */
                goto qname_done;
            }
            UB ql = buf[pos];
            if (pos + 1 + ql > len) return E_IO;
            pos += ql + 1;
        }
        if (pos < len) pos++;  /* 終端の 0 */
qname_done:
        pos += 4;  /* QTYPE(2) + QCLASS(2) */
    }

    /* Answer セクションから最初の A レコードを取得 */
    for (i = 0; i < ancount && pos + 12 <= len; i++) {
        /* NAME (圧縮ポインタ or ラベル列) をスキップ */
        if ((buf[pos] & 0xC0) == 0xC0) {
            pos += 2;
        } else {
            while (pos < len && buf[pos] != 0) {
                UB label_len = buf[pos];
                if (pos + 1 + label_len > len) return E_IO;  /* 境界チェック */
                pos += label_len + 1;
            }
            if (pos < len) pos++;  /* 終端 0 をスキップ */
        }

        if (pos + 10 > len) return E_IO;

        UH type   = get_be16(&buf[pos]);     pos += 2;
        UH class  = get_be16(&buf[pos]);     pos += 2;
        pos += 4;  /* TTL (4 byte) */
        UH rdlen  = get_be16(&buf[pos]);     pos += 2;

        if (type == DNS_TYPE_A && class == DNS_CLASS_IN && rdlen == 4) {
            if (pos + 4 > len) return E_IO;
            result[0] = buf[pos];
            result[1] = buf[pos + 1];
            result[2] = buf[pos + 2];
            result[3] = buf[pos + 3];
            return E_OK;
        }

        pos += rdlen;  /* 他のレコードタイプはスキップ */
    }

    return E_IO;  /* A レコードが見つからなかった */
}

/*======================================================================
 * 公開 API
 *====================================================================*/

EXPORT ER tk_dns_resolve(UB sn, const UB *dns_ip, const char *hostname,
                         UB *result, TMO tmout)
{
    if (sn >= W5100S_SOCK_NUM) return E_PAR;
    if (dns_ip == NULL || hostname == NULL || result == NULL) return E_PAR;

    UB buf[DNS_MSG_MAXSZ];
    UH txid = dns_txid++;
    ER err;

    /* UDP ソケットオープン */
    err = tk_sock_open(sn, Sn_MR_UDP, 0);
    if (err != E_OK) return err;

    /* DNS クエリ送信 */
    UH qlen = dns_build_query(buf, hostname, txid);
    if (qlen == 0) { tk_sock_close(sn); return E_PAR; }  /* ホスト名不正 */
    W sent = tk_sock_sendto(sn, buf, qlen, (UB *)dns_ip, DNS_PORT, tmout);
    if (sent < 0) {
        tk_sock_close(sn);
        return (sent == E_TMOUT) ? E_TMOUT : E_IO;
    }

    /* DNS 応答受信 (ブロッキング — タスクは CPU を解放) */
    UB svr_addr[4];
    UH svr_port;
    W rcvd = tk_sock_recvfrom(sn, buf, DNS_MSG_MAXSZ,
                               svr_addr, &svr_port, tmout);
    tk_sock_close(sn);

    if (rcvd <= 0) {
        return (rcvd == E_TMOUT) ? E_TMOUT : E_IO;
    }

    /* 応答パース */
    return dns_parse_response(buf, (UH)rcvd, txid, result);
}

#endif /* CPU_RP2040 */
