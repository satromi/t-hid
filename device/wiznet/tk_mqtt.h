/*
 *----------------------------------------------------------------------
 *    μT-Kernel Native MQTT Client (v3.1.1)
 *
 *    RTOS 機能:
 *    - 専用 MQTT タスク: PINGREQ 送信 + PUBLISH 受信を自動処理
 *    - メッセージバッファ: PUBLISH データの非同期受け渡し
 *    - 周期ハンドラ: キープアライブタイマー
 *    - tk_sock_send/recv: 割り込み駆動の TCP 送受信
 *----------------------------------------------------------------------
 */

#ifndef __TK_MQTT_H__
#define __TK_MQTT_H__

#include <tk/tkernel.h>

/* MQTT QoS レベル */
#define MQTT_QOS0   0
#define MQTT_QOS1   1

/* MQTT 接続パラメータ */
typedef struct {
    UB      sn;             /* 使用するソケット番号 */
    UB      *broker_ip;     /* ブローカー IP (UB[4]) */
    UH      broker_port;    /* ブローカーポート (通常 1883) */
    const char *client_id;  /* クライアント ID */
    UH      keepalive;      /* キープアライブ秒数 (0=無効) */
    const char *username;   /* ユーザー名 (NULL 可) */
    const char *password;   /* パスワード (NULL 可) */
} T_MQTT_CONF;

/* MQTT 受信メッセージ */
typedef struct {
    char    topic[64];      /* トピック名 */
    UB      payload[512];   /* ペイロード */
    UH      payload_len;    /* ペイロード長 */
} T_MQTT_MSG;

/* MQTT コールバック */
typedef void (*FP_MQTT_RECV)(const T_MQTT_MSG *msg);

/*
 * MQTT クライアント開始 (TCP 接続 + CONNECT)
 * 非同期: 内部タスクが PINGREQ/受信を自動処理
 */
ER tk_mqtt_start(const T_MQTT_CONF *conf, FP_MQTT_RECV on_recv);

/* MQTT クライアント停止 (DISCONNECT + TCP 切断) */
void tk_mqtt_stop(void);

/* PUBLISH 送信 (QoS 0, ブロッキング) */
ER tk_mqtt_publish(const char *topic, const UB *payload, UH len);

/* SUBSCRIBE 送信 (QoS 0/1, ブロッキング) */
ER tk_mqtt_subscribe(const char *topic, UB qos);

/* 接続状態 */
BOOL tk_mqtt_is_connected(void);

#endif /* __TK_MQTT_H__ */
