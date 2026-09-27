/*
 *	usermain.c — NUCLEO-H533RE キーボードブレイン
 *
 *	構成:
 *	  USER USB (Type-C)      → PC へ USB HID キーボードとして出力
 *	  I2C1 (PB8=SCL,PB9=SDA) → 左右の Pico (I2C スレーブ) からマトリクス取得
 *	  WIZ550io (SPI1)        → MCP サーバ (wasm キー処理のアップロード)
 *	  ST-LINK 仮想 COM       → T-Monitor コンソール (USART2)
 *
 *	キー処理は組み込みキーマップで動き、MCP 経由で wasm モジュールを
 *	アタッチすると以降はモジュールの kb_scan() に置き換わる。
 */

#include <sys/machine.h>
#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>
#include <string.h>

#include <dev_i2c.h>
#include "../device/include/dev_usb_hid.h"
#include "kb_brain.h"
#include "kb_wasm.h"
#if defined(USE_U2F)
#include "u2f/u2f_task.h"
#endif

#if defined(USE_NET)
#include "../device/include/dev_wiznet.h"
#include "../device/wiznet/wiznet_drv.h"
#include "../device/wiznet/tk_dhcp.h"
#include "../device/wiznet/tk_dns.h"
#include "../device/wiznet/tk_sntp.h"
#include <tk/datetime.h>
#if defined(USE_MCP)
#include "../device/wiznet/tk_mcp.h"
#include "../device/wiznet/tk_mcp_hal.h"
#endif
#if defined(USE_WASM)
#include "../device/wiznet/tk_wasm.h"
#endif

/* ソケット割当 (W5500: 8 本): 0=MCP(MQTT), 1=DNS/SNTP(一時), 7=DHCP */
#define MCP_SN		0
#define UTIL_SN		1
#endif /* USE_NET */

/* MCP set_led で TRUE にするとハートビート停止 (LED は SPI と共用のため未使用) */
BOOL g_led_manual = FALSE;

#if defined(USE_NET)
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
 * ネットワークと MCP サーバ
 */
LOCAL void start_network(void)
{
	T_DHCP_INFO dhcp;
	UB dns_ip[4];
	ER err;

	err = dev_init_wiznet(0);
	if (err != E_OK) {
		tm_printf((UB *)"WIZnet: init failed (%d), network disabled\n", err);
		return;
	}

	if (!wait_dhcp(&dhcp)) {
		tm_printf((UB *)"DHCP: not ready, network disabled\n");
		return;
	}
	tm_printf((UB *)"IP=%d.%d.%d.%d GW=%d.%d.%d.%d\n",
		  dhcp.ip[0], dhcp.ip[1], dhcp.ip[2], dhcp.ip[3],
		  dhcp.gw[0], dhcp.gw[1], dhcp.gw[2], dhcp.gw[3]);

	memcpy(dns_ip, dhcp.dns, 4);
	if ((dns_ip[0] | dns_ip[1] | dns_ip[2] | dns_ip[3]) == 0) {
		memcpy(dns_ip, dhcp.gw, 4);
	}

	/* SNTP 時刻同期 */
	{
		UB ntp_ip[4];
		if (tk_dns_resolve(UTIL_SN, dns_ip, "ntp.nict.jp", ntp_ip, 5000) == E_OK) {
			dt_settimezone(9 * 60);
			tk_sntp_sync(UTIL_SN, ntp_ip, 5000);
		}
	}

#if defined(USE_MCP)
	{
		UB broker_ip[4];
		T_MCP_CONF mcp_conf;

		err = tk_dns_resolve(UTIL_SN, dns_ip, "test.mosquitto.org", broker_ip, 5000);
		if (err != E_OK) {
			tm_printf((UB *)"MCP: DNS failed (%d)\n", err);
			return;
		}

		mcp_conf.sn = MCP_SN;
		memcpy(mcp_conf.broker_ip, broker_ip, 4);
		mcp_conf.broker_port = 1883;
		mcp_conf.mqtt_user = NULL;
		mcp_conf.mqtt_pass = NULL;
		mcp_conf.rate_limit = 10;

		err = tk_mcp_start(&mcp_conf);
		tm_printf((UB *)"MCP: %s (%d)\n", (err == E_OK) ? "started" : "start failed", err);
	}
#endif /* USE_MCP */
}
#endif /* USE_NET */

EXPORT INT usermain(void)
{
	ER err;

	tm_printf((UB *)"=== NUCLEO-H533RE keyboard brain ===\n");

	err = dev_init_usb_hid(0);
	if (err != E_OK) tm_printf((UB *)"USB HID init failed (%d)\n", err);

	err = dev_init_i2c(0);
	if (err != E_OK) tm_printf((UB *)"I2C init failed (%d)\n", err);

	err = kb_wasm_init();
	if (err != E_OK) tm_printf((UB *)"kb.wasm init failed (%d)\n", err);

#if defined(USE_U2F)
	/* FIDO U2F 認証器 (USB 複合デバイスの 2 つ目のインタフェース) */
	err = u2f_start();
	if (err != E_OK) tm_printf((UB *)"U2F start failed (%d)\n", err);
#endif

#if defined(TZ_SELFTEST)
	{
		IMPORT ER tzt_start(void);
		err = tzt_start();
		tm_printf((UB *)"TZTEST: start (%d)\n", err);
	}
#endif

#if defined(USE_WASM)
	err = tk_wasm_init();
	if (err != E_OK) tm_printf((UB *)"wasm init failed (%d)\n", err);
#endif

	/*
	 * キーボードを先に起動する。ネットワーク側は DHCP 待ちで最大 30 秒
	 * ブロックするため、後に置くとキー入力がそれまで止まる。
	 */
	err = kb_brain_start();
	if (err != E_OK) tm_printf((UB *)"Brain start failed (%d)\n", err);

#if defined(USE_NET)
	start_network();
#if defined(USE_MCP) && MCP_HAS_ADC
	mcp_adc_init();
#endif
#endif

	tk_slp_tsk(TMO_FEVR);
	return 0;
}
