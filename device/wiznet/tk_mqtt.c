/*
 *----------------------------------------------------------------------
 *    μT-Kernel Native MQTT Client (v3.1.1)
 *
 *    MQTT 3.1.1 の最小実装。CONNECT/PUBLISH/SUBSCRIBE/PINGREQ をサポート。
 *    パケットシリアライズは独自実装 (MQTTPacket ライブラリ不使用)。
 *
 *    RTOS 機能:
 *    - 専用 MQTT タスク: 受信ループ + PINGREQ 自動送信
 *    - 周期ハンドラ: キープアライブタイマー (ISR コンテキスト)
 *    - イベントフラグ: PING タイミング通知
 *    - FastLock: 送信の排他制御 (PUBLISH/SUBSCRIBE/PINGREQ の競合防止)
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

#if defined(WIFI_CYW43)
#include "../wifi/tk_net_compat.h"
#elif defined(WIZCHIP_W5500)
#include "wiznet_drv.h"
#else
#include "w5100s_reg.h"
#endif
#include "tk_socket.h"
#include "tk_mqtt.h"

/* MQTT パケットタイプ */
#define MQTT_CONNECT     0x10
#define MQTT_CONNACK     0x20
#define MQTT_PUBLISH     0x30
#define MQTT_PUBACK      0x40
#define MQTT_SUBSCRIBE   0x82  /* QoS 1 固定ヘッダ */
#define MQTT_SUBACK      0x90
#define MQTT_PINGREQ     0xC0
#define MQTT_PINGRESP    0xD0
#define MQTT_DISCONNECT  0xE0

/* タスク設定 */
#define MQTT_TASK_PRI    8
/* MQTT タスクは受信した MQTT メッセージに対して mqtt_on_recv (= MCP ツール
 * ディスパッチ) を呼ぶため、MCP ツール内の cJSON 再帰処理分のスタックも必要。
 * 元は 1024 だったが、retry 時に (おそらく) スタック不足で hard fault が
 * 発生したため 4096 に拡張。 */
#define MQTT_TASK_STKSZ  4096
/* MQTT TX/RX バッファサイズ (2048 だと MCP tools/list の schema 込み
 * 約 9KB のレスポンスが送れないため 12KB に拡張)。
 * txbuf + rxbuf で計 24KB 静的確保 (264KB SRAM のうち約 9%)。 */
#define MQTT_BUF_SZ      12288

/* イベント */
#define MQTT_EVT_PING    (1u << 0)
#define MQTT_EVT_STOP    (1u << 1)

/*----------------------------------------------------------------------
 * モジュール変数
 */
LOCAL T_MQTT_CONF mqtt_conf;
LOCAL FP_MQTT_RECV mqtt_on_recv;
LOCAL volatile BOOL mqtt_connected;	/* volatile: publish/subscribe から別タスク参照 */
LOCAL UH mqtt_pkt_id;

LOCAL ID mqtt_tskid;
LOCAL ID mqtt_cycid;
LOCAL ID mqtt_flgid;
LOCAL FastLock mqtt_txlock;

LOCAL UB mqtt_txbuf[MQTT_BUF_SZ];
LOCAL UB mqtt_rxbuf[MQTT_BUF_SZ];

/*----------------------------------------------------------------------
 * Remaining Length エンコード (MQTT 3.1.1 Section 2.2.3)
 * 戻り値: 書き込みバイト数
 */
LOCAL UH mqtt_encode_remaining(UB *buf, UW len)
{
    UH pos = 0;
    do {
        UB byte = (UB)(len & 0x7F);
        len >>= 7;
        if (len > 0) byte |= 0x80;
        buf[pos++] = byte;
    } while (len > 0);
    return pos;
}

/*----------------------------------------------------------------------
 * Remaining Length デコード
 */
LOCAL UW mqtt_decode_remaining(const UB *buf, UH *consumed)
{
    UW val = 0;
    UH shift = 0;
    UH pos = 0;
    UB byte;
    do {
        byte = buf[pos++];
        val |= (UW)(byte & 0x7F) << shift;
        shift += 7;
    } while ((byte & 0x80) && pos < 4);
    *consumed = pos;
    return val;
}

/*----------------------------------------------------------------------
 * UTF-8 文字列書き込み (MQTT 形式: 2byte 長 + データ)
 */
LOCAL UH mqtt_put_string(UB *buf, const char *str)
{
    UH len = (UH)strlen(str);
    buf[0] = (UB)(len >> 8);
    buf[1] = (UB)(len);
    memcpy(&buf[2], str, len);
    return 2 + len;
}

/*----------------------------------------------------------------------
 * TCP 送信ヘルパー (排他制御付き、partial send 対応)
 *
 * tk_sock_send は TCP_SND_BUF 分しか一度に送信できず、残りは呼出側で
 * ループする必要がある (9KB を超える MCP レスポンス等で必要)。
 */
LOCAL ER mqtt_send(const UB *data, UH len)
{
    Lock(&mqtt_txlock);
    UH off = 0;
    ER result = E_OK;
    while (off < len) {
        W sent = tk_sock_send(mqtt_conf.sn, data + off, (UH)(len - off), 10000);
        if (sent < 0) { result = E_IO; break; }
        if (sent == 0) {
            /* 0 = バッファ空き待ち後のリトライ指示。少し待って再送 */
            tk_dly_tsk(10);
            continue;
        }
        off += (UH)sent;
    }
    Unlock(&mqtt_txlock);
    return result;
}

/*----------------------------------------------------------------------
 * CONNECT パケット送信
 */
LOCAL ER mqtt_send_connect(void)
{
    UB *p = mqtt_txbuf;
    UH payload_pos = 0;
    UB payload[200];

    /* 可変ヘッダ: Protocol Name + Level + Flags + KeepAlive */
    UB var_hdr[10];
    var_hdr[0] = 0x00; var_hdr[1] = 0x04;  /* Protocol Name Length */
    var_hdr[2] = 'M'; var_hdr[3] = 'Q'; var_hdr[4] = 'T'; var_hdr[5] = 'T';
    var_hdr[6] = 0x04;  /* Protocol Level (3.1.1) */
    var_hdr[7] = 0x02;  /* Clean Session */
    if (mqtt_conf.username) var_hdr[7] |= 0x80;
    if (mqtt_conf.password) var_hdr[7] |= 0x40;
    var_hdr[8] = (UB)(mqtt_conf.keepalive >> 8);
    var_hdr[9] = (UB)(mqtt_conf.keepalive);

    /* ペイロード: Client ID */
    payload_pos += mqtt_put_string(&payload[payload_pos], mqtt_conf.client_id);

    /* ペイロード: Username (オプション) */
    if (mqtt_conf.username) {
        payload_pos += mqtt_put_string(&payload[payload_pos], mqtt_conf.username);
    }
    /* ペイロード: Password (オプション) */
    if (mqtt_conf.password) {
        payload_pos += mqtt_put_string(&payload[payload_pos], mqtt_conf.password);
    }

    /* 固定ヘッダ */
    UW remaining = 10 + payload_pos;
    p[0] = MQTT_CONNECT;
    UH hdr_len = 1 + mqtt_encode_remaining(&p[1], remaining);

    /* 組み立て */
    memcpy(&p[hdr_len], var_hdr, 10);
    memcpy(&p[hdr_len + 10], payload, payload_pos);

    return mqtt_send(p, hdr_len + 10 + payload_pos);
}

/*----------------------------------------------------------------------
 * PUBLISH パケット送信 (QoS 0)
 */
EXPORT ER tk_mqtt_publish(const char *topic, const UB *payload, UH len)
{
    if (!mqtt_connected || topic == NULL) return E_IO;

    UB *p = mqtt_txbuf;
    UH topic_len = (UH)strlen(topic);
    UW remaining = 2 + topic_len + len;

    /* MQTT 2-byte remaining length encoding maximum (Section 2.2.3) */
    if (remaining > 16383) return E_PAR;

    /* バッファオーバーフロー防止 */
    if (5 + remaining > MQTT_BUF_SZ) return E_PAR;

    /* 固定ヘッダ */
    p[0] = MQTT_PUBLISH;  /* QoS 0, no DUP, no Retain */
    UH hdr_len = 1 + mqtt_encode_remaining(&p[1], remaining);

    /* トピック名 */
    p[hdr_len] = (UB)(topic_len >> 8);
    p[hdr_len + 1] = (UB)(topic_len);
    memcpy(&p[hdr_len + 2], topic, topic_len);

    /* ペイロード */
    memcpy(&p[hdr_len + 2 + topic_len], payload, len);

    return mqtt_send(p, hdr_len + 2 + topic_len + len);
}

/*----------------------------------------------------------------------
 * SUBSCRIBE パケット送信
 */
EXPORT ER tk_mqtt_subscribe(const char *topic, UB qos)
{
    if (!mqtt_connected || topic == NULL) return E_IO;

    UB *p = mqtt_txbuf;
    UH topic_len = (UH)strlen(topic);
    UH pkt_id = ++mqtt_pkt_id;
    UW remaining = 2 + 2 + topic_len + 1;  /* pkt_id + topic + qos */

    /* 固定ヘッダ */
    p[0] = MQTT_SUBSCRIBE;
    UH hdr_len = 1 + mqtt_encode_remaining(&p[1], remaining);

    /* Packet Identifier */
    p[hdr_len] = (UB)(pkt_id >> 8);
    p[hdr_len + 1] = (UB)(pkt_id);

    /* Topic Filter */
    p[hdr_len + 2] = (UB)(topic_len >> 8);
    p[hdr_len + 3] = (UB)(topic_len);
    memcpy(&p[hdr_len + 4], topic, topic_len);

    /* Requested QoS */
    p[hdr_len + 4 + topic_len] = qos;

    return mqtt_send(p, hdr_len + 2 + 2 + topic_len + 1);
}

/*----------------------------------------------------------------------
 * PINGREQ 送信 (2 byte 固定)
 */
LOCAL ER mqtt_send_pingreq(void)
{
    UB pkt[2] = { MQTT_PINGREQ, 0x00 };
    return mqtt_send(pkt, 2);
}

/*----------------------------------------------------------------------
 * DISCONNECT 送信 (2 byte 固定)
 */
LOCAL ER mqtt_send_disconnect(void)
{
    UB pkt[2] = { MQTT_DISCONNECT, 0x00 };
    return mqtt_send(pkt, 2);
}

/*----------------------------------------------------------------------
 * 受信パケット処理
 */
LOCAL void mqtt_process_rx(const UB *buf, UH len)
{
    if (len < 2) return;

    UB type = buf[0] & 0xF0;

    switch (type) {
    case MQTT_CONNACK:
        if (len >= 4 && buf[3] == 0x00) {
            mqtt_connected = TRUE;
            W5DBG("MQTT: connected\n");
        } else {
            W5DBG("MQTT: CONNACK rejected (%d)\n",
                      (len >= 4) ? buf[3] : -1);
        }
        break;

    case MQTT_PUBLISH: {
        /* QoS 0 PUBLISH 受信 */
        UH consumed;
        UH pos = 1;
        UW remaining = mqtt_decode_remaining(&buf[pos], &consumed);
        pos += consumed;

        if (pos + 2 > len) break;
        UH topic_len = ((UH)buf[pos] << 8) | buf[pos + 1];
        pos += 2;

        if (pos + topic_len > len) break;

        if (mqtt_on_recv != NULL) {
            T_MQTT_MSG msg;
            UH copy_len = (topic_len < 63) ? topic_len : 63;
            memcpy(msg.topic, &buf[pos], copy_len);
            msg.topic[copy_len] = '\0';
            pos += topic_len;

            /* QoS 1/2 ならここに Packet ID がある (QoS 0 ではない) */
            UB qos = (buf[0] >> 1) & 0x03;
            if (qos > 0) pos += 2;

            UH payload_len = (UH)(remaining - 2 - topic_len - (qos > 0 ? 2 : 0));
            copy_len = (payload_len < 512) ? payload_len : 512;
            if (pos + copy_len <= len) {
                memcpy(msg.payload, &buf[pos], copy_len);
            }
            msg.payload_len = copy_len;

            mqtt_on_recv(&msg);
        }
        break;
    }

    case MQTT_SUBACK:
        /* SUBSCRIBE 確認 — 特に処理不要 */
        break;

    case MQTT_PINGRESP:
        /* PING 応答 — 正常 */
        break;

    default:
        break;
    }
}

/*----------------------------------------------------------------------
 * 周期ハンドラ: キープアライブタイマー
 */
LOCAL void mqtt_keepalive_handler(void *exinf)
{
    (void)exinf;
    if (mqtt_flgid > 0) {
        tk_set_flg(mqtt_flgid, MQTT_EVT_PING);
    }
}

/*----------------------------------------------------------------------
 * MQTT タスク: 受信ループ + PINGREQ 自動送信
 */
LOCAL void mqtt_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    UW ping_tick = 0;

    /* TCP 接続 */
    ER err = tk_sock_open(mqtt_conf.sn, Sn_MR_TCP, 0);
    W5DBG("MQTT: sock_open sn=%d err=%d\n", mqtt_conf.sn, err);
    if (err != E_OK) goto exit;

    err = tk_sock_connect(mqtt_conf.sn, mqtt_conf.broker_ip,
                          mqtt_conf.broker_port, 10000);
    W5DBG("MQTT: sock_connect %u.%u.%u.%u:%u err=%d\n",
          mqtt_conf.broker_ip[0], mqtt_conf.broker_ip[1],
          mqtt_conf.broker_ip[2], mqtt_conf.broker_ip[3],
          mqtt_conf.broker_port, err);
    if (err != E_OK) goto exit;

    /* MQTT CONNECT 送信 */
    err = mqtt_send_connect();
    W5DBG("MQTT: CONNECT sent err=%d\n", err);
    if (err != E_OK) goto exit;

    /* CONNACK 受信待ち */
    W rcvd = tk_sock_recv(mqtt_conf.sn, mqtt_rxbuf, MQTT_BUF_SZ, 10000);
    W5DBG("MQTT: CONNACK recv rcvd=%d\n", (int)rcvd);
    if (rcvd <= 0) goto exit;
    mqtt_process_rx(mqtt_rxbuf, (UH)rcvd);

    if (!mqtt_connected) goto exit;

    /* 受信ループ */
    while (mqtt_connected) {
        /* 受信 (1 秒タイムアウト — 定期的にイベントフラグもチェック) */
        rcvd = tk_sock_recv(mqtt_conf.sn, mqtt_rxbuf, MQTT_BUF_SZ, 1000);
        if (rcvd > 0) {
            mqtt_process_rx(mqtt_rxbuf, (UH)rcvd);
        } else if (rcvd == E_IO) {
            /* TCP 切断 */
            mqtt_connected = FALSE;
            break;
        }
        /* E_TMOUT は正常 (データなし) */

        /* PINGREQ チェック */
        if (mqtt_conf.keepalive > 0) {
            UINT flgptn;
            ER ferr = tk_wai_flg(mqtt_flgid, MQTT_EVT_PING | MQTT_EVT_STOP,
                                  TWF_ORW | TWF_BITCLR, &flgptn, 0);
            if (ferr == E_OK) {
                if (flgptn & MQTT_EVT_STOP) {
                    mqtt_connected = FALSE;
                    break;
                }
                if (flgptn & MQTT_EVT_PING) {
                    ping_tick++;
                    if (ping_tick >= mqtt_conf.keepalive) {
                        mqtt_send_pingreq();
                        ping_tick = 0;
                    }
                }
            }
        }

        /* STOP イベントチェック (キープアライブ無効時) */
        if (mqtt_conf.keepalive == 0) {
            UINT flgptn;
            if (tk_wai_flg(mqtt_flgid, MQTT_EVT_STOP,
                           TWF_ORW | TWF_BITCLR, &flgptn, 0) == E_OK) {
                mqtt_connected = FALSE;
                break;
            }
        }
    }

    /* DISCONNECT 送信 (ベストエフォート) */
    mqtt_send_disconnect();

exit:
    tk_sock_close(mqtt_conf.sn);
    mqtt_connected = FALSE;
    W5DBG("MQTT: stopped\n");
    tk_ext_tsk();
}

/*======================================================================
 * 公開 API
 *====================================================================*/

EXPORT ER tk_mqtt_start(const T_MQTT_CONF *conf, FP_MQTT_RECV on_recv)
{
    if (conf == NULL || conf->client_id == NULL) return E_PAR;

    /* 既にタスクが動いている場合は先にクリーンアップ (誤操作防止) */
    if (mqtt_tskid > 0 || mqtt_cycid > 0 || mqtt_flgid > 0) {
        tk_mqtt_stop();
    }

    memcpy(&mqtt_conf, conf, sizeof(T_MQTT_CONF));
    mqtt_on_recv = on_recv;
    mqtt_connected = FALSE;
    mqtt_pkt_id = 0;

    /* CreateLock は二重初期化するとカーネルオブジェクトが漏れるため
     * 初回のみ実行 (mqtt_txlock は static なのでプロセス寿命中ずっと有効)。 */
    LOCAL BOOL txlock_created = FALSE;
    if (!txlock_created) {
        CreateLock(&mqtt_txlock, (CONST UB *)"mqtx");
        txlock_created = TRUE;
    }

    /* イベントフラグ */
    {
        T_CFLG cflg = { NULL, TA_TPRI | TA_WMUL, 0 };
        mqtt_flgid = tk_cre_flg(&cflg);
        if (mqtt_flgid < 0) return E_LIMIT;
    }

    /* キープアライブ周期ハンドラ (1 秒間隔) */
    if (conf->keepalive > 0) {
        T_CCYC ccyc;
        ccyc.exinf  = NULL;
        ccyc.cycatr = TA_HLNG;
        ccyc.cyctim = 1000;
        ccyc.cycphs = 0;
        ccyc.cychdr = (FP)mqtt_keepalive_handler;
        mqtt_cycid = tk_cre_cyc(&ccyc);
        if (mqtt_cycid > 0) tk_sta_cyc(mqtt_cycid);
    }

    /* MQTT タスク */
    {
        T_CTSK ctsk;
        ctsk.exinf  = NULL;
        ctsk.tskatr = TA_HLNG | TA_RNG3;
        ctsk.task   = (FP)mqtt_task;
        ctsk.itskpri = MQTT_TASK_PRI;
        ctsk.stksz  = MQTT_TASK_STKSZ;
        ctsk.bufptr = NULL;
        mqtt_tskid = tk_cre_tsk(&ctsk);
        if (mqtt_tskid < 0) return E_LIMIT;
        tk_sta_tsk(mqtt_tskid, 0);
    }

    return E_OK;
}

EXPORT void tk_mqtt_stop(void)
{
    /* タスクに STOP 通知 */
    if (mqtt_flgid > 0) {
        tk_set_flg(mqtt_flgid, MQTT_EVT_STOP);
    }

    /* タスク終了待ち */
    if (mqtt_tskid > 0) {
        INT retry;
        for (retry = 0; retry < 100; retry++) {
            T_RTSK rtsk;
            if (tk_ref_tsk(mqtt_tskid, &rtsk) != E_OK) break;
            if (rtsk.tskstat == TTS_DMT) break;
            tk_dly_tsk(100);
        }
        tk_del_tsk(mqtt_tskid);
        mqtt_tskid = 0;
    }

    if (mqtt_cycid > 0) {
        tk_stp_cyc(mqtt_cycid);
        tk_del_cyc(mqtt_cycid);
        mqtt_cycid = 0;
    }
    if (mqtt_flgid > 0) {
        tk_del_flg(mqtt_flgid);
        mqtt_flgid = 0;
    }
}

EXPORT BOOL tk_mqtt_is_connected(void)
{
    return mqtt_connected;
}

#endif /* CPU_RP2040 || MTKBSP_CPU_STM32H5 */
