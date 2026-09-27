/*
 *	app_main.c — Pico W WiFi (+BLE) / WiFi MCP テスト
 *
 *	BLE=0: WiFi-only MCP テスト
 *	BLE=1: WiFi + BLE + keyboard
 */

#include <sys/machine.h>
#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "../device/wifi/tk_wifi.h"
#include "../device/wifi/tk_wifi_creds.h"
#include "../device/wiznet/tk_socket.h"
#include "../device/wiznet/tk_mqtt.h"
#include "../device/wiznet/tk_mcp.h"
#include "../device/wiznet/tk_wasm.h"

#ifdef KB_OUTPUT_BLE
extern void kb_start(void);
#endif

extern ER   cyw43_arch_tkernel_init(void);
extern void cyw43_arch_tkernel_poll(void);
extern void cyw43_arch_set_ready(void);
extern BOOL cyw43_arch_is_ready(void);
extern void cyw43_arch_tkernel_enable_pm(void);
extern void cyw43_arch_set_poll_task(ID tskid);

/* A-5 診断用カウンタ (cyw43_arch_tkernel.c) */
extern volatile uint32_t cyw43_lock_acquire_count;
extern volatile uint32_t cyw43_lock_contention_count;

BOOL g_led_manual = FALSE;

/* WiFi 認証情報 — Flash に保存があればそれを使い、無ければ .wifi_config
 * 由来の fallback を使う。tk_wifi_reconnect_start は conf を浅くコピーするだけ
 * (ssid/key ポインタはそのまま) なので、g_wifi_creds は static に置いて
 * タスクの寿命の間生き続けさせる。 */
LOCAL T_WIFI_CREDS g_wifi_creds;
LOCAL T_WIFI_CONF  wifi_conf;

/* Flash に保存がないときに使う認証情報。リポジトリ直下の .wifi_config
 * (リポジトリ管理外) からビルド時に生成される。ファイルがなければ SSID は空 */
#include "wifi_config_embedded.h"

#ifndef KB_OUTPUT_BLE
/*----------------------------------------------------------------------
 * CYW43 ポーリングタスク (WiFi-only)
 * lwIP タイマ + async event を駆動する。
 */
LOCAL void cyw43_poll_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    cyw43_arch_set_poll_task(tk_get_tid());
    while (1) {
        cyw43_arch_tkernel_poll();
        tk_dly_tsk(5);
    }
}
#endif

/*----------------------------------------------------------------------
 * WiFi ネットワークタスク
 */
LOCAL void wifi_network_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

#ifndef KB_OUTPUT_BLE
    tm_printf((UB *)"CYW43: initializing...\n");
    ER err = cyw43_arch_tkernel_init();
    if (err != E_OK) {
        tm_printf((UB *)"CYW43: init failed (%d)\n", err);
        tk_ext_tsk();
        return;
    }
    cyw43_arch_set_ready();
    tm_printf((UB *)"CYW43: initialized\n");

    /* ポーリングタスク起動 */
    {
        T_CTSK ctsk;
        ctsk.exinf   = NULL;
        ctsk.tskatr  = TA_HLNG | TA_RNG3;
        ctsk.task    = (FP)cyw43_poll_task;
        ctsk.itskpri = 15;     /* WiFiタスク(10)より低優先度 */
        ctsk.stksz   = 2048;
        ctsk.bufptr  = NULL;
        ID tid = tk_cre_tsk(&ctsk);
        if (tid > 0) {
            tk_sta_tsk(tid, 0);
            tm_printf((UB *)"CYW43: poll task started (pri=15)\n");
        }
    }
#else
    ER err;
    /* BLE 有効時: kb_start() が非同期で cyw43_arch_tkernel_init → BLE 初期化 →
     * cyw43_arch_set_ready() を呼ぶ。その完了を待たずに cyw43_wifi_set_up を
     * 呼ぶと BLE 初期化と競合して WiFi 側が無応答になるため、ready を待つ。 */
    tm_printf((UB *)"WiFi: waiting for CYW43 (BLE) init\n");
    while (!cyw43_arch_is_ready()) {
        tk_dly_tsk(100);
    }
    /* BT FW ロード直後は HCI パケットがまだ流れている。
     * そのまま WiFi join を開始すると WPA ハンドシェイクが極端に遅くなる
     * (実測 20 秒以上で 30 秒タイムアウトぎりぎり) ため、1.5 秒待機して
     * BT 側の初期トラフィックを落ち着かせる。 */
    tm_printf((UB *)"WiFi: CYW43 ready, waiting 1500ms for BT to settle\n");
    tk_dly_tsk(1500);
    tm_printf((UB *)"WiFi: starting reconnect manager\n");
    tm_printf((UB *)"WiFi: lock acq=%u cont=%u (pre-join)\n",
              (unsigned)cyw43_lock_acquire_count,
              (unsigned)cyw43_lock_contention_count);
#endif

    /* Flash から認証情報を読み込む (無ければ fallback) */
    tk_wifi_creds_get(&g_wifi_creds,
                      WIFI_FALLBACK_SSID, WIFI_FALLBACK_PSK, WIFI_FALLBACK_AUTH);
    wifi_conf.ssid = g_wifi_creds.ssid;
    wifi_conf.key  = g_wifi_creds.psk;
    wifi_conf.auth = g_wifi_creds.auth;
    tm_printf((UB *)"WiFi: creds from %s, ssid='%s'\n",
              g_wifi_creds.from_flash ? "flash" : "fallback",
              g_wifi_creds.ssid);
    if (g_wifi_creds.ssid[0] == '\0') {
        tm_printf((UB *)"WiFi: no credentials (create .wifi_config and rebuild)\n");
        tk_ext_tsk();
        return;
    }

    /* WiFi 自動再接続マネージャ開始 (内部で tk_wifi_init + 接続 + 切断時再接続)
     * 0 指定でデフォルト値 (retry 5s → 60s, timeout 30s, health 2s) */
    LOCAL const T_WIFI_RECONNECT_CONF rc_conf = { 0, 0, 0, 0 };
    err = tk_wifi_reconnect_start(&wifi_conf, &rc_conf);
    if (err != E_OK) {
        tm_printf((UB *)"WiFi: reconnect manager start failed (%d)\n", err);
        tk_ext_tsk();
        return;
    }

    /* 初回接続完了まで待機 */
    while (tk_wifi_get_state() != TK_WIFI_ST_READY) {
        tk_dly_tsk(500);
    }

#ifndef KB_OUTPUT_BLE
    cyw43_arch_tkernel_enable_pm();
#endif

    /* 接続情報 */
    {
        T_WIFI_INFO info;
        tk_wifi_get_info(&info);
        tm_printf((UB *)"WiFi: IP=%d.%d.%d.%d  RSSI=%d dBm\n",
            info.ip[0], info.ip[1], info.ip[2], info.ip[3], info.rssi);
    }

    tk_sock_init();

    /* WebAssembly runtime (wasm3) 初期化 */
    {
        ER werr = tk_wasm_init();
        if (werr == E_OK) {
            tm_printf((UB *)"WASM: runtime ready\n");
        } else {
            tm_printf((UB *)"WASM: init failed (%d)\n", werr);
        }
    }

    /* MQTT broker 設定 (ビルド時オーバーライド可能)
     *   MQTT_BROKER_HOST: "a.b.c.d" 形式の IP or ホスト名 (DNS 解決)
     *   MQTT_BROKER_PORT: ポート番号
     * 例: make ... EXTRA_DEFS='-DMQTT_BROKER_HOST=\"192.168.0.141\" -DMQTT_BROKER_PORT=1884'
     */
#ifndef MQTT_BROKER_HOST
#define MQTT_BROKER_HOST  "test.mosquitto.org"
#endif
#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT  1883
#endif

    /* ホストが "N.N.N.N" 形式なら DNS をスキップして直接パース */
    {
        UB broker_ip[4];
        BOOL ip_ok = FALSE;
        {
            const char *s = MQTT_BROKER_HOST;
            UW oct[4] = {0, 0, 0, 0};
            INT idx = 0, digits = 0;
            BOOL valid = TRUE;
            while (*s && idx < 4) {
                if (*s >= '0' && *s <= '9') {
                    oct[idx] = oct[idx] * 10 + (UW)(*s - '0');
                    if (oct[idx] > 255) { valid = FALSE; break; }
                    digits++;
                } else if (*s == '.') {
                    if (digits == 0) { valid = FALSE; break; }
                    idx++; digits = 0;
                } else {
                    valid = FALSE; break;
                }
                s++;
            }
            if (valid && idx == 3 && digits > 0 && *s == '\0') {
                broker_ip[0] = (UB)oct[0]; broker_ip[1] = (UB)oct[1];
                broker_ip[2] = (UB)oct[2]; broker_ip[3] = (UB)oct[3];
                ip_ok = TRUE;
            }
        }

        if (!ip_ok) {
            err = tk_wifi_dns_resolve(MQTT_BROKER_HOST, broker_ip, 5000);
        } else {
            err = E_OK;
        }

        if (err != E_OK) {
            tm_printf((UB *)"MCP: DNS failed (%d) host=" MQTT_BROKER_HOST "\n", err);
        } else {
            tm_printf((UB *)"MCP: broker=%d.%d.%d.%d:%d (%s)\n",
                broker_ip[0], broker_ip[1], broker_ip[2], broker_ip[3],
                MQTT_BROKER_PORT,
                ip_ok ? "direct" : "dns");

            LOCAL T_MCP_CONF mcp_conf;
            memcpy(mcp_conf.broker_ip, broker_ip, 4);
            mcp_conf.sn = 0;
            mcp_conf.broker_port = MQTT_BROKER_PORT;
            mcp_conf.mqtt_user = NULL;   /* test.mosquitto.org は匿名 */
            mcp_conf.mqtt_pass = NULL;
            mcp_conf.rate_limit = 10;

            err = tk_mcp_start(&mcp_conf);
            if (err == E_OK) {
                tm_printf((UB *)"MCP: server started\n");
            } else {
                tm_printf((UB *)"MCP: start failed (%d)\n", err);
            }
        }
    }

    /* 永続化された wasm モジュールの autoboot (MCP 起動後に呼ぶ — wasm 側から
     * mqtt_pub を呼ぶコードの接続準備が整ってから起動させるため) */
    {
        ER berr = tk_wasm_boot();
        INT autoslot = tk_wasm_autoboot_get();
        if (autoslot >= 0) {
            tm_printf((UB *)"WASM: autoboot flash_slot=%d rc=%d\n", autoslot, berr);
        }
    }

    tm_printf((UB *)"WiFi: network ready\n");

    /* 再接続は tk_wifi_reconnect_task が自動処理。メインタスクは定期的に情報表示 */
    while (1) {
        tk_dly_tsk(60000);
        T_WIFI_INFO info;
        tk_wifi_get_info(&info);
        tm_printf((UB *)"WiFi: IP=%d.%d.%d.%d  RSSI=%d dBm\n",
            info.ip[0], info.ip[1], info.ip[2], info.ip[3], info.rssi);
    }
}

EXPORT INT usermain(void)
{
#ifdef KB_OUTPUT_BLE
    tm_printf((UB *)"=== Pico W WiFi + BLE Keyboard ===\n");
    kb_start();
#else
    tm_printf((UB *)"=== Pico W WiFi MCP (no BLE) ===\n");
#endif

    {
        T_CTSK ctsk;
        ctsk.exinf   = NULL;
        ctsk.tskatr  = TA_HLNG | TA_RNG3;
        ctsk.task    = (FP)wifi_network_task;
        ctsk.itskpri = 10;
        ctsk.stksz   = 8192;
        ctsk.bufptr  = NULL;
        ID tskid = tk_cre_tsk(&ctsk);
        if (tskid > 0) tk_sta_tsk(tskid, 0);
    }

    tk_slp_tsk(TMO_FEVR);
    return 0;
}
