/*
 *----------------------------------------------------------------------
 *    WiFi Connection Manager for μT-Kernel 3.0
 *    CYW43439 (Pico W) + lwIP
 *----------------------------------------------------------------------
 */

#ifndef __TK_WIFI_H__
#define __TK_WIFI_H__

#include <tk/tkernel.h>

/* WiFi 認証方式 */
#define TK_WIFI_AUTH_OPEN       0x00000000
#define TK_WIFI_AUTH_WPA2_PSK   0x00400004  /* CYW43_AUTH_WPA2_AES_PSK */
#define TK_WIFI_AUTH_WPA3_PSK   0x01400004  /* CYW43_AUTH_WPA3_WPA2_AES_PSK */

/* WiFi 状態 */
typedef enum {
    TK_WIFI_ST_OFF = 0,       /* WiFi 未初期化 */
    TK_WIFI_ST_DISCONNECTED,  /* WiFi 初期化済み、未接続 */
    TK_WIFI_ST_CONNECTING,    /* AP 接続中 */
    TK_WIFI_ST_CONNECTED,     /* AP 接続済み (IP 未取得) */
    TK_WIFI_ST_READY,         /* AP 接続 + IP 取得済み */
    TK_WIFI_ST_ERROR,         /* エラー */
} T_WIFI_STATE;

/* WiFi 接続設定 */
typedef struct {
    const char *ssid;     /* SSID (NULL 終端) */
    const char *key;      /* パスフレーズ (NULL 終端) */
    UW auth;              /* 認証方式 (TK_WIFI_AUTH_*) */
} T_WIFI_CONF;

/* WiFi 情報 */
typedef struct {
    UB ip[4];
    UB mask[4];
    UB gw[4];
    UB mac[6];
    INT rssi;             /* 信号強度 (dBm) */
} T_WIFI_INFO;

/* 再接続マネージャ設定 */
typedef struct {
    UW retry_interval_ms;   /* 初回リトライ間隔 (デフォルト 5000) */
    UW retry_max_interval;  /* 指数バックオフ上限 (デフォルト 60000) */
    UW connect_timeout_ms;  /* 各接続試行のタイムアウト (デフォルト 30000) */
    UW health_check_ms;     /* リンク生存確認間隔 (デフォルト 2000) */
} T_WIFI_RECONNECT_CONF;

/* 接続統計 */
typedef struct {
    UW connect_count;       /* 累計成功接続回数 */
    UW disconnect_count;    /* 累計切断検出回数 */
    UW current_uptime_ms;   /* 現セッションの接続持続時間 */
    UW total_uptime_ms;     /* 累計接続時間 */
    INT last_rssi;          /* 最終 RSSI (dBm) */
} T_WIFI_STATS;

/*----------------------------------------------------------------------
 * API
 */

/* WiFi 初期化 (STA モード有効化、国コード設定) */
ER tk_wifi_init(void);

/* AP 接続 (DHCP で IP 取得まで待機) */
ER tk_wifi_connect(const T_WIFI_CONF *conf, TMO tmout);

/* 切断 */
ER tk_wifi_disconnect(void);

/* 状態取得 */
T_WIFI_STATE tk_wifi_get_state(void);

/* 接続情報取得 */
ER tk_wifi_get_info(T_WIFI_INFO *info);

/*----------------------------------------------------------------------
 * 自動再接続マネージャ
 *
 * tk_wifi_reconnect_start を呼ぶと別タスクが起動し、WiFi の切断を検出して
 * 自動的に再接続を試みる。指数バックオフでリトライ間隔を調整。
 * app 側は再接続ロジックを気にする必要がない。
 */

/* 再接続マネージャ開始: 初回接続 + 以降の自動再接続 */
ER tk_wifi_reconnect_start(const T_WIFI_CONF *conf,
                           const T_WIFI_RECONNECT_CONF *rc);

/* 再接続マネージャ停止 */
ER tk_wifi_reconnect_stop(void);

/* 統計情報取得 */
ER tk_wifi_get_stats(T_WIFI_STATS *stats);

/*----------------------------------------------------------------------
 * DNS 名前解決 (ブロッキング)
 *   hostname: ホスト名 (例: "test.mosquitto.org")
 *   result:   IPv4 アドレス出力 (UB[4])
 *   tmout:    タイムアウト (ms)
 *   DHCP が取得した DNS サーバを lwIP が自動使用する。
 *   戻り値: E_OK=成功, E_TMOUT=タイムアウト, E_IO=解決失敗, E_PAR=引数不正
 */
ER tk_wifi_dns_resolve(const char *hostname, UB *result, TMO tmout);

#endif /* __TK_WIFI_H__ */
