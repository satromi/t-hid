/*
 *	app_main.c — W5100S ネットワーク全機能テスト
 *
 *	1. DHCP → 2. DNS → 3. SNTP → 4. NETMON
 *	5. HTTP サーバ (ポート 80)
 *	6. TCP echo (ポート 7)
 *	7. MQTT テスト (test.mosquitto.org)
 */

#include <sys/machine.h>
#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>
#include <sys/sysdef.h>
#include <string.h>

/* キーボードフレームワーク (app_program/keyboard/kb_main.c) */
extern void kb_start(void);
#if defined(USE_PRESENCE)
extern ER presence_start(void);
#endif

#if defined(USE_NET)
#include "../device/wiznet/sysdepend/w5100s/w5100s_reg.h"
#include "../device/wiznet/tk_socket.h"
#include "../device/wiznet/tk_dhcp.h"
#include "../device/wiznet/tk_dns.h"
#include "../device/wiznet/tk_sntp.h"
#include "../device/wiznet/tk_netmon.h"
#include "../device/wiznet/tk_httpd.h"
#include "../device/wiznet/tk_mqtt.h"
#if defined(USE_MCP)
#include "../device/wiznet/tk_mcp.h"
#endif
#if defined(USE_WASM)
#include "../device/wiznet/tk_wasm.h"
#endif

#include <tk/datetime.h>

/* ソケット割当: 0=MCP(MQTT), 1=DNS/SNTP(一時), 2=HTTP, 3=DHCP */
#define MCP_SN		0
#define UTIL_SN		1
#define HTTP_SN		2
#define DHCP_SN		3

#define HTTP_PORT	80

/* DNS サーバ IP (グローバル — MQTT でも使用) */
LOCAL UB g_dns_ip[4];

/* LED 制御: MCP set_led で TRUE にするとハートビート停止 */
BOOL g_led_manual = FALSE;

/*----------------------------------------------------------------------
 * DHCP LEASED 待ち (最大 30 秒)
 */
LOCAL BOOL wait_dhcp(T_DHCP_INFO *info)
{
	INT i;
	for (i = 0; i < 30; i++) {
		if (tk_dhcp_get_state() == TK_DHCP_ST_LEASED) {
			tk_dhcp_get_info(info);
			return TRUE;
		}
		tk_dly_tsk(1000);
	}
	return FALSE;
}

/*----------------------------------------------------------------------
 * HTTP ハンドラ — ステータス JSON
 */
LOCAL ER http_handler(T_HTTP_REQ *req)
{
	char *p = (char *)req->resp_buf;
	UH len = 0;

	if (strcmp(req->path, "/") == 0 || strcmp(req->path, "/status") == 0) {
		req->content_type = "application/json";

		T_DHCP_INFO info;
		tk_dhcp_get_info(&info);

		DATE_TIM dt;
		char tbuf[20];
		if (dt_gettime(&dt) == E_OK) {
			dt_format(&dt, tbuf, sizeof(tbuf));
		} else {
			strcpy(tbuf, "not synced");
		}

		const char *h = "{\"ip\":\"";
		memcpy(p + len, h, strlen(h)); len += strlen(h);
		{
			INT i;
			for (i = 0; i < 4; i++) {
				if (i > 0) p[len++] = '.';
				UB v = info.ip[i];
				if (v >= 100) p[len++] = '0' + v / 100;
				if (v >= 10)  p[len++] = '0' + (v / 10) % 10;
				p[len++] = '0' + v % 10;
			}
		}
		const char *m = "\",\"link\":";
		memcpy(p + len, m, strlen(m)); len += strlen(m);
		p[len++] = tk_netmon_is_link_up() ? '1' : '0';
		const char *t = ",\"time\":\"";
		memcpy(p + len, t, strlen(t)); len += strlen(t);
		memcpy(p + len, tbuf, strlen(tbuf)); len += strlen(tbuf);
		const char *e = "\",\"mcp\":";
		memcpy(p + len, e, strlen(e)); len += strlen(e);
		p[len++] = tk_mqtt_is_connected() ? '1' : '0';
		p[len++] = '}';

		req->resp_len = len;
		return E_OK;
	}

	return E_NOEXS;
}

/*----------------------------------------------------------------------
 * リンク変化コールバック
 */
LOCAL void on_link_change(BOOL link_up)
{
	tm_printf((UB *)"LINK: %s\n", link_up ? "UP" : "DOWN");
}


#endif /* USE_NET */

/*----------------------------------------------------------------------
 * usermain
 */
EXPORT INT usermain(void)
{
	tm_printf((UB *)"=== usermain start ===\n");

	/*
	 *  キーボードを先に起動する。
	 *  この後の DHCP 待ちは最大 30 秒ブロックするため、後に置くと
	 *  ネットワーク機器が無い構成でキーボードが動き出さない。
	 *  kb_start() はタスクを生成して即座に戻る。
	 */
	kb_start();

#if defined(USE_PRESENCE)
	/* 離席の判定 (Pico4ML のカメラ)。キーのスキャンより低い優先度で動く */
	if (presence_start() != E_OK) tm_printf((UB *)"presence: start failed\n");
#endif

#if !defined(USE_NET)
	/*
	 *  ネットワーク無効構成。LED はキーボード側のハートビートが
	 *  制御するため、ここでは何もせず待つ。
	 */
	tk_slp_tsk(TMO_FEVR);
	return 0;
#else

	/* DHCP LEASED 待ち */
	T_DHCP_INFO dhcp;
	if (!wait_dhcp(&dhcp)) {
		tm_printf((UB *)"DHCP: not ready\n");
		goto heartbeat;
	}

	tm_printf((UB *)"IP=%d.%d.%d.%d GW=%d.%d.%d.%d DNS=%d.%d.%d.%d\n",
		  dhcp.ip[0], dhcp.ip[1], dhcp.ip[2], dhcp.ip[3],
		  dhcp.gw[0], dhcp.gw[1], dhcp.gw[2], dhcp.gw[3],
		  dhcp.dns[0], dhcp.dns[1], dhcp.dns[2], dhcp.dns[3]);

	/* DNS サーバ */
	memcpy(g_dns_ip, dhcp.dns, 4);
	if (g_dns_ip[0] == 0 && g_dns_ip[1] == 0 &&
	    g_dns_ip[2] == 0 && g_dns_ip[3] == 0) {
		memcpy(g_dns_ip, dhcp.gw, 4);
	}

	/* DNS テスト */
	{
		UB r[4];
		if (tk_dns_resolve(UTIL_SN, g_dns_ip, "www.google.com", r, 5000) == E_OK)
			tm_printf((UB *)"DNS: www.google.com = %d.%d.%d.%d\n", r[0],r[1],r[2],r[3]);
	}

	/* SNTP 時刻同期 */
	{
		UB ntp_ip[4];
		if (tk_dns_resolve(UTIL_SN, g_dns_ip, "ntp.nict.jp", ntp_ip, 5000) == E_OK) {
			dt_settimezone(9 * 60);
			if (tk_sntp_sync(UTIL_SN, ntp_ip, 5000) == E_OK) {
				DATE_TIM dt;
				dt_gettime(&dt);
				char tbuf[20];
				dt_format(&dt, tbuf, sizeof(tbuf));
				tm_printf((UB *)"SNTP: %s (JST)\n", tbuf);
			}
		}
	}

	/* ネットワークリンク監視 */
	tk_netmon_start(DHCP_SN, on_link_change);
	tm_printf((UB *)"NETMON: link=%s\n",
		  tk_netmon_is_link_up() ? "UP" : "DOWN");

	/* HTTP サーバ (ソケット 2, ポート 80) */
	if (tk_httpd_start(HTTP_SN, HTTP_PORT, http_handler) == E_OK) {
		tm_printf((UB *)"HTTPD: port %d\n", HTTP_PORT);
	}

#if defined(USE_WASM)
	/* wasm ランタイム初期化 (未初期化だと wasm_begin が E_OBJ になる) */
	{
		ER werr = tk_wasm_init();
		if (werr != E_OK) tm_printf((UB *)"WASM: init failed (%d)\n", werr);
	}
#endif

#if defined(USE_MCP)
	/* MCP サーバ起動 (MQTT over test.mosquitto.org) */
	{
		UB broker_ip[4];
		ER err = tk_dns_resolve(UTIL_SN, g_dns_ip,
					"test.mosquitto.org", broker_ip, 5000);
		if (err == E_OK) {
			tm_printf((UB *)"MCP: broker=%d.%d.%d.%d\n",
				  broker_ip[0], broker_ip[1],
				  broker_ip[2], broker_ip[3]);

			T_MCP_CONF mcp_conf;
			mcp_conf.sn = MCP_SN;
			memcpy(mcp_conf.broker_ip, broker_ip, 4);
			mcp_conf.broker_port = 1883;
			mcp_conf.mqtt_user = NULL;  /* test.mosquitto.org は匿名 */
			mcp_conf.mqtt_pass = NULL;
			mcp_conf.rate_limit = 10;   /* 10 コマンド/秒 */

			err = tk_mcp_start(&mcp_conf);
			if (err == E_OK) {
				tm_printf((UB *)"MCP: started\n");
				/* 接続完了は MCP タスク内でリトライ付きで待つ */
			} else {
				tm_printf((UB *)"MCP: start failed (%d)\n", err);
			}
		} else {
			tm_printf((UB *)"MCP: DNS failed (%d)\n", err);
		}
	}
#endif /* USE_MCP */

	tm_printf((UB *)"=== all services started ===\n");

heartbeat:
	out_w(IO_BANK0_BASE + 0x04 + 25*8, 5);
	out_w(GPIO_OE_SET, (1u << 25));
	while (1) {
		if (g_led_manual) {
			/* MCP 手動制御中 — ハートビート停止 */
			tk_dly_tsk(500);
		} else {
			out_w(GPIO_OUT_SET, (1u << 25));
			tk_dly_tsk(100);
			out_w(GPIO_OUT_CLR, (1u << 25));
			tk_dly_tsk(1900);
		}
	}
	return 0;
#endif /* USE_NET */
}
