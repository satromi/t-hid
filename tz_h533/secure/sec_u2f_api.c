/*
 * sec_u2f_api.c — TrustZone のセキュア側: U2F の呼び出し口 (NSC)
 *
 *   非セキュア側から渡されたポインタは、非セキュアの領域を指していることを
 *   cmse_check_address_range() で確かめてからセキュア側の作業領域に写して使う。
 *   写してから使うことで、処理の途中で非セキュア側に中身を書き換えられても
 *   検査済みの値だけを扱う。
 */

#include <arm_cmse.h>
#include <stddef.h>
#include <string.h>

#include <tk/tkernel.h>
#include "u2f.h"
#include "u2f_crypto.h"
#include "u2f_store.h"
#include "u2f_secure_api.h"

#define ENTRY	__attribute__((cmse_nonsecure_entry))

#define RANDOM_MAX	64

/*
 * B1 (PC13、押すと High)。sec_main.c でセキュア専用の入力にしている
 *   押し始め (離した状態から押した状態への変化) を見たら、その後の読み取り
 *   B1_VALID_SAMPLES 回 (非セキュア側の 20ms ごとの呼び出しで約 3 秒) の間に来た
 *   確認要求 1 回だけを承認する。押し続けても 1 回分。
 */
#define GPIOC_IDR_S		(0x52020800UL + 0x10)
#define B1_PIN			13
#define B1_VALID_SAMPLES	150

LOCAL BOOL initialized;

LOCAL UB sreq[U2F_APDU_MAX];
LOCAL UB sresp[U2F_APDU_MAX];

LOCAL BOOL b1_last = TRUE;	/* 起動時に押されていても押し始めとは数えない */
LOCAL UW   b1_valid;		/* 残りの有効回数 (0 = 承認できる押下なし) */
LOCAL BOOL presence_used, presence_asked;

LOCAL BOOL b1_pressed(void)
{
	return (in_w(GPIOC_IDR_S) & (1u << B1_PIN)) ? TRUE : FALSE;
}

LOCAL BOOL b1_sample(void)
{
	BOOL pressed = b1_pressed();

	if (pressed && !b1_last) {
		b1_valid = B1_VALID_SAMPLES;
	} else if (b1_valid > 0) {
		b1_valid--;
	}
	b1_last = pressed;
	return pressed;
}

/* APDU 処理中の本人確認。セキュア側が見た押下を 1 回分だけ使う */
LOCAL BOOL sec_user_present(void)
{
	presence_asked = TRUE;
	(void)b1_sample();
	if (b1_valid > 0) {
		b1_valid = 0;
		presence_used = TRUE;
		return TRUE;
	}
	return FALSE;
}

LOCAL BOOL ns_readable(const void *p, UW len)
{
	return len == 0 || cmse_check_address_range((void *)p, len,
				CMSE_NONSECURE | CMSE_MPU_READ) != NULL;
}

LOCAL BOOL ns_writable(void *p, UW len)
{
	return len == 0 || cmse_check_address_range(p, len,
				CMSE_NONSECURE | CMSE_MPU_READWRITE) != NULL;
}

ENTRY int32_t u2fs_erase(void)
{
	if (initialized || !b1_pressed()) return E_OBJ;
	return u2f_store_erase_all();
}

ENTRY int32_t u2fs_button(void)
{
	return b1_sample() ? 1 : 0;
}

ENTRY int32_t u2fs_init(void)
{
	ER err;

	if (initialized) return E_OBJ;
	initialized = TRUE;

	err = u2f_crypto_init();
	if (err == E_OK) err = u2f_init();
	u2f_store_hide();
	return err;
}

ENTRY uint32_t u2fs_apdu(T_U2FS_APDU *a)
{
	T_U2FS_APDU p;
	UW n, out_flags = 0;

	/* 引数の構造体を先に写し、以後は写した値だけを使う */
	if (a == NULL || !ns_writable(a, sizeof(*a))) return 0;
	p = *a;
	if (p.req_len > sizeof(sreq) || !ns_readable(p.req, p.req_len)) return 0;
	if (p.resp_max > sizeof(sresp)) p.resp_max = sizeof(sresp);
	if (!ns_writable(p.resp, p.resp_max)) return 0;

	memcpy(sreq, p.req, p.req_len);
	presence_used = presence_asked = FALSE;

	n = u2f_process_apdu(sreq, p.req_len, sresp, p.resp_max, sec_user_present);
	if (n > p.resp_max) n = 0;
	memcpy(p.resp, sresp, n);

	if (presence_used) out_flags |= U2FS_PRESENCE_USED;
	else if (presence_asked) out_flags |= U2FS_PRESENCE_MISSING;
	a->flags = out_flags;

	memset(sreq, 0, sizeof(sreq));
	memset(sresp, 0, sizeof(sresp));
	return n;
}

ENTRY int32_t u2fs_random(uint8_t *buf, uint32_t len)
{
	UB tmp[RANDOM_MAX];
	ER err;

	if (len > sizeof(tmp) || !ns_writable(buf, len)) return E_PAR;
	err = u2f_random(tmp, len);
	if (err == E_OK) memcpy(buf, tmp, len);
	memset(tmp, 0, sizeof(tmp));
	return err;
}

ENTRY void u2fs_status(T_U2FS_STATUS *st)
{
	T_U2F_STATUS s;

	if (st == NULL || !ns_writable(st, sizeof(*st))) return;
	u2f_get_status(&s);
	st->ready           = s.ready;
	st->uses_pka        = s.uses_pka;
	st->uses_hash       = s.uses_hash;
	st->uses_saes       = s.uses_saes;
	st->counter         = s.counter;
	st->registrations   = s.registrations;
	st->authentications = s.authentications;
}

IMPORT UW sec_log_take(char *buf, UW max);

ENTRY uint32_t u2fs_log_read(char *buf, uint32_t max)
{
	char tmp[256];
	UW n;

	if (max == 0 || !ns_writable(buf, max)) return 0;
	if (max > sizeof(tmp)) max = sizeof(tmp);
	n = sec_log_take(tmp, max - 1);
	memcpy(buf, tmp, n);
	buf[n] = '\0';
	return n;
}
