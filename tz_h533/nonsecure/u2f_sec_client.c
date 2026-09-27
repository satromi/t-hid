/*
 * u2f_sec_client.c — TrustZone の非セキュア側: U2F の処理をセキュア側に任せる
 *
 *   u2f.c / u2f_crypto.c / u2f_store.c と同じ関数名で、セキュア側の呼び出し口
 *   (u2f_secure_api.h) を呼ぶ。u2f_task.c・ctaphid.c・u2f_mcp.c は変更なしで使える。
 *
 *   μT-Kernel 3.0 セキュア機能拡張の標準実行モデル: セキュア側を呼ぶのは
 *   セキュアコール可能属性 (TA_TZCALL) の U2F タスクだけで、セキュア側を実行中でも
 *   割り込みやタスクの切り替えは通常どおり起きる (セキュア側のスタックはカーネルが
 *   タスクごとに切り替える)。
 *
 *   ほかのタスク (MCP など) はセキュア側を呼べないので、状態 (u2f_get_status) は
 *   U2F タスクが取得した値の写しを返す。
 */

#include <sys/machine.h>
#include <stddef.h>
#include <string.h>

#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

#include "u2f.h"
#include "u2f_store.h"
#include "u2f_task.h"
#include "u2f_secure_api.h"

/* セキュア側のログを非セキュア側のコンソールに出す */
LOCAL void print_secure_log(void)
{
	char buf[128];

	while (u2fs_log_read(buf, sizeof(buf)) > 0) {
		tm_printf((UB *)"%s", buf);
	}
}

/* U2F タスクが最後に取得した状態 (MCP のタスクから読む) */
LOCAL T_U2FS_STATUS status_copy;

LOCAL void refresh_status(void)
{
	T_U2FS_STATUS s;
	UINT imask;

	memset(&s, 0, sizeof(s));
	u2fs_status(&s);
	DI(imask);
	status_copy = s;
	EI(imask);
}

EXPORT ER u2f_crypto_init(void)
{
	return E_OK;	/* 自己診断はセキュア側の u2fs_init() で行う */
}

EXPORT ER u2f_init(void)
{
	ER err;

	err = u2fs_init();
	print_secure_log();
	refresh_status();
	return err;
}

EXPORT void u2f_store_hide(void)
{
	/* 鍵セクタの隠蔽はセキュア側の u2fs_init() で行う */
}

EXPORT ER u2f_store_erase_all(void)
{
	ER err;

	err = u2fs_erase();
	refresh_status();
	return err;
}

EXPORT ER u2f_random(UB *buf, UW len)
{
	ER err = E_OK;
	UW n;

	while (len > 0 && err == E_OK) {
		n = (len > 64) ? 64 : len;
		err = u2fs_random(buf, n);
		buf += n;
		len -= n;
	}
	return err;
}

EXPORT UW u2f_process_apdu(const UB *req, UW req_len, UB *resp, UW resp_max,
			   BOOL (*user_present)(void))
{
	T_U2FS_APDU a;
	UW n;

	(void)user_present;
	a.req      = req;
	a.req_len  = req_len;
	a.resp     = resp;
	a.resp_max = resp_max;
	a.reserved = 0;
	a.flags    = 0;
	n = u2fs_apdu(&a);

	if (a.flags & U2FS_PRESENCE_USED) {
		u2f_presence_used();
	} else if (a.flags & U2FS_PRESENCE_MISSING) {
		u2f_presence_missing();
	}
	print_secure_log();
	refresh_status();
	return n;
}

/* B1 はセキュア専用のピンなので、読むのもセキュア側に任せる (u2f_task.c から使う) */
EXPORT BOOL u2f_sec_button(void)
{
	return u2fs_button() ? TRUE : FALSE;
}

EXPORT void u2f_get_status(T_U2F_STATUS *st)
{
	T_U2FS_STATUS s;
	UINT imask;

	DI(imask);
	s = status_copy;
	EI(imask);
	st->ready           = s.ready;
	st->uses_pka        = s.uses_pka;
	st->uses_hash       = s.uses_hash;
	st->uses_saes       = s.uses_saes;
	st->counter         = s.counter;
	st->registrations   = s.registrations;
	st->authentications = s.authentications;
}

/*
 * MCP の peek/poke から守る範囲
 *   セキュアな領域を非セキュア側から読むと SecureFault やバスエラーになるので、
 *   セキュア側のアドレス (0x0C/0x3x/0x5x) と、セキュアに割り当てた領域の
 *   非セキュア側のアドレスをすべて断る。
 */
EXPORT BOOL u2f_addr_is_secret(UW addr)
{
	if (addr >= 0x0C000000u && addr < 0x10000000u) return TRUE;		/* Flash のセキュア側のアドレス */
	if ((addr & 0xF0000000u) == 0x30000000u) return TRUE;			/* SRAM のセキュア側のアドレス */
	if ((addr & 0xF0000000u) == 0x50000000u) return TRUE;			/* 周辺回路のセキュア側のアドレス */
	if (addr >= 0x08000000u && addr < 0x08010000u) return TRUE;		/* セキュア側のコード */
	if (addr >= 0x08078000u && addr < 0x0807E000u) return TRUE;		/* 鍵とカウンタ */
	if (addr >= 0x2003C000u && addr < 0x20044000u) return TRUE;		/* セキュア側の RAM */
	if (addr >= 0x420C0000u && addr < 0x420C4000u) return TRUE;		/* 暗号の周辺回路 */
	return FALSE;
}
