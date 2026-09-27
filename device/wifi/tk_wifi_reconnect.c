/*
 *----------------------------------------------------------------------
 *    WiFi Auto-reconnect Manager for μT-Kernel 3.0
 *
 *    別タスクで WiFi のリンク状態を監視し、切断時に自動再接続する。
 *    指数バックオフでリトライ間隔を調整し、AP/DHCP の負荷を抑える。
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) && defined(WIFI_CYW43)

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "cyw43.h"
#include "tk_wifi.h"

#if CYW43_LWIP
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#endif

extern cyw43_t cyw43_state;

/*----------------------------------------------------------------------
 * 再接続マネージャ内部状態
 */
LOCAL ID                     rc_tskid = 0;
LOCAL volatile BOOL          rc_running = FALSE;
LOCAL T_WIFI_CONF            rc_conf;       /* ssid/key のコピーは呼出し側の責任 */
LOCAL T_WIFI_RECONNECT_CONF  rc_params;
LOCAL T_WIFI_STATS           rc_stats;
LOCAL SYSTIM                 rc_connect_time;  /* 最後に接続成功した時刻 */

/* デフォルト値 */
#define DEFAULT_RETRY_INTERVAL_MS   5000
#define DEFAULT_RETRY_MAX_INTERVAL  60000
/* tk_wifi_connect 冒頭で cyw43_wifi_leave を呼ぶようになったので、
 * リトライ時は 5 秒程度で接続完了する。初回だけ 30 秒でタイムアウト
 * しても、2 回目のリトライが短時間で成功するため合計 boot time は
 * 30+5+5 ≈ 40 秒で済む。60 秒だと 70 秒。 */
#define DEFAULT_CONNECT_TIMEOUT_MS  30000
#define DEFAULT_HEALTH_CHECK_MS     2000

/*----------------------------------------------------------------------
 * リンク健全性チェック
 *
 * WiFi が接続済みかつ IP 取得済みなら TRUE。それ以外 (切断、DHCP 失敗等) は FALSE。
 */
LOCAL BOOL link_is_healthy(void)
{
    /* CYW43 層のリンク状態 */
    int wifi_link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    if (wifi_link != CYW43_LINK_UP && wifi_link != CYW43_LINK_JOIN) {
        return FALSE;
    }

#if CYW43_LWIP
    /* TCP/IP 層のリンク状態 (IP 取得済みかつ LINK_UP フラグ) */
    int tcpip_link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    return (tcpip_link == CYW43_LINK_UP);
#else
    return (wifi_link == CYW43_LINK_JOIN);
#endif
}

/*----------------------------------------------------------------------
 * uptime 更新 (接続中のみ)
 */
LOCAL void update_uptime(void)
{
    SYSTIM now;
    tk_get_otm(&now);
    UW elapsed_ms = (UW)(now.lo - rc_connect_time.lo);
    rc_stats.current_uptime_ms = elapsed_ms;
}

/*----------------------------------------------------------------------
 * RSSI 更新 (30 秒毎に最新値を取得)
 */
LOCAL void update_rssi(void)
{
    int32_t rssi = 0;
    if (cyw43_wifi_get_rssi(&cyw43_state, &rssi) == 0) {
        rc_stats.last_rssi = (INT)rssi;
    }
}

/*----------------------------------------------------------------------
 * 再接続マネージャタスク
 *
 * 起動フロー:
 *   1. tk_wifi_init()
 *   2. ループ:
 *      a. 接続試行 (指数バックオフ)
 *      b. 接続成功したら健全性チェックループ
 *      c. 切断検出したら (a) に戻る
 */
LOCAL void wifi_reconnect_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

    UW retry_interval = rc_params.retry_interval_ms;
    UW rssi_counter = 0;

    /* WiFi 初期化 (STA モード有効化) */
    tm_printf((UB *)"WiFi-rc: initializing STA mode\n");
    ER err = tk_wifi_init();
    if (err != E_OK) {
        tm_printf((UB *)"WiFi-rc: init failed (%d)\n", err);
        rc_running = FALSE;
        tk_ext_tsk();
        return;
    }

    while (rc_running) {
        /* ------ 接続試行フェーズ ------ */
        tm_printf((UB *)"WiFi-rc: connecting to '%s' (backoff=%ums)\n",
                  rc_conf.ssid, (unsigned)retry_interval);
        err = tk_wifi_connect(&rc_conf, rc_params.connect_timeout_ms);

        if (err == E_OK) {
            /* 接続成功 */
            tk_get_otm(&rc_connect_time);
            rc_stats.connect_count++;
            retry_interval = rc_params.retry_interval_ms;  /* バックオフリセット */
            update_rssi();
            rssi_counter = 0;
            tm_printf((UB *)"WiFi-rc: connected (count=%u)\n",
                      (unsigned)rc_stats.connect_count);

            /* ------ 健全性監視フェーズ ------ */
            while (rc_running) {
                tk_dly_tsk(rc_params.health_check_ms);

                if (!link_is_healthy()) {
                    /* 切断検出 */
                    update_uptime();
                    rc_stats.total_uptime_ms += rc_stats.current_uptime_ms;
                    rc_stats.disconnect_count++;
                    tm_printf((UB *)"WiFi-rc: link lost (uptime=%us, count=%u)\n",
                              (unsigned)(rc_stats.current_uptime_ms / 1000),
                              (unsigned)rc_stats.disconnect_count);
                    rc_stats.current_uptime_ms = 0;
                    break;  /* 再接続フェーズへ */
                }

                /* 定期統計更新 */
                update_uptime();
                rssi_counter += rc_params.health_check_ms;
                if (rssi_counter >= 30000) {  /* 30秒毎に RSSI 更新 */
                    rssi_counter = 0;
                    update_rssi();
                }
            }
        } else {
            /* 接続失敗: 指数バックオフで待機 */
            tm_printf((UB *)"WiFi-rc: connect failed (%d), retry in %ums\n",
                      err, (unsigned)retry_interval);
            tk_dly_tsk(retry_interval);

            /* バックオフ: 倍々、ただし上限あり */
            retry_interval *= 2;
            if (retry_interval > rc_params.retry_max_interval) {
                retry_interval = rc_params.retry_max_interval;
            }
        }
    }

    tm_printf((UB *)"WiFi-rc: stopped\n");
    rc_tskid = 0;
    tk_ext_tsk();
}

/*----------------------------------------------------------------------
 * 再接続マネージャ開始
 */
EXPORT ER tk_wifi_reconnect_start(const T_WIFI_CONF *conf,
                                  const T_WIFI_RECONNECT_CONF *rc)
{
    if (conf == NULL || conf->ssid == NULL) return E_PAR;
    if (rc_running) return E_OBJ;  /* 既に起動中 */

    /* 設定をコピー (呼出し側の T_WIFI_CONF 寿命に依存しないように) */
    rc_conf = *conf;

    /* 再接続パラメータ (0 指定ならデフォルト) */
    rc_params.retry_interval_ms = (rc && rc->retry_interval_ms)
                                  ? rc->retry_interval_ms : DEFAULT_RETRY_INTERVAL_MS;
    rc_params.retry_max_interval = (rc && rc->retry_max_interval)
                                   ? rc->retry_max_interval : DEFAULT_RETRY_MAX_INTERVAL;
    rc_params.connect_timeout_ms = (rc && rc->connect_timeout_ms)
                                   ? rc->connect_timeout_ms : DEFAULT_CONNECT_TIMEOUT_MS;
    rc_params.health_check_ms = (rc && rc->health_check_ms)
                                ? rc->health_check_ms : DEFAULT_HEALTH_CHECK_MS;

    memset(&rc_stats, 0, sizeof(rc_stats));
    rc_running = TRUE;

    /* タスク生成 (pri=11, ネットワーク処理タスクより1低い) */
    T_CTSK ctsk = {
        .exinf   = NULL,
        .tskatr  = TA_HLNG | TA_RNG3,
        .task    = (FP)wifi_reconnect_task,
        .itskpri = 11,
        .stksz   = 4096,
        .bufptr  = NULL,
    };
    ID tid = tk_cre_tsk(&ctsk);
    if (tid <= 0) {
        rc_running = FALSE;
        return (ER)tid;
    }
    rc_tskid = tid;
    tk_sta_tsk(tid, 0);

    return E_OK;
}

/*----------------------------------------------------------------------
 * 再接続マネージャ停止
 */
EXPORT ER tk_wifi_reconnect_stop(void)
{
    if (!rc_running) return E_OK;
    rc_running = FALSE;
    /* タスクは次の tk_dly_tsk から戻った後に rc_running=FALSE を検出して終了 */
    return E_OK;
}

/*----------------------------------------------------------------------
 * 統計情報取得
 */
EXPORT ER tk_wifi_get_stats(T_WIFI_STATS *stats)
{
    if (stats == NULL) return E_PAR;
    if (rc_running && link_is_healthy()) {
        update_uptime();  /* 呼出し時点で uptime 更新 */
    }
    *stats = rc_stats;
    return E_OK;
}

#endif /* CPU_RP2040 && WIFI_CYW43 */
