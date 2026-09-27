/*
 *	app_main.c — BLE キーボード専用 usermain
 *
 *	Pico W BLE HID キーボードのエントリポイント。
 *	ネットワーク機能なし (WiFi / Ethernet 不使用)。
 *	キーボードスキャナタスクを起動して永久スリープ。
 */

#include <tk/tkernel.h>
#include <tm/tmonitor.h>

/* keyboard/kb_main.c */
extern void kb_start(void);

/* MCP set_led で使用 (ネットワーク機能なしでもリンク要) */
BOOL g_led_manual = FALSE;

EXPORT INT usermain(void)
{
	tm_printf((UB *)"=== Pico W BLE Keyboard ===\n");

	/* キーボードスキャナ起動 (マトリクススキャン + HID レポート送信) */
	kb_start();

	/* スキャナタスクが全処理を行うので、usermain は永久スリープ */
	tk_slp_tsk(TMO_FEVR);
	return 0;
}
