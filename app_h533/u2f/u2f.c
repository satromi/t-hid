/*
 * u2f.c — FIDO U2F (CTAP1) の REGISTER / AUTHENTICATE / VERSION
 *
 * FIDO U2F Raw Message Formats v1.2 に従う。
 */

#include <sys/machine.h>
#include <stddef.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "u2f.h"
#include "u2f_store.h"
#include "../../device/flash/stm32h5_flash.h"

#define U2F_INS_REGISTER	0x01
#define U2F_INS_AUTHENTICATE	0x02
#define U2F_INS_VERSION		0x03

#define U2F_AUTH_CHECK_ONLY	0x07
#define U2F_AUTH_ENFORCE	0x03
#define U2F_AUTH_DONT_ENFORCE	0x08

#define U2F_REGISTER_ID		0x05
#define U2F_FLAG_USER_PRESENT	0x01

LOCAL struct {
	BOOL	ready;
	UB	priv_key[32];	/* 秘密鍵導出用 (master から導出) */
	UB	mac_key[32];	/* キーハンドル認証用 (master から導出) */
	UB	att_priv[P256_KEY_LEN];
	UW	cert_len;
	UW	registrations;
	UW	authentications;
} u2f;

/* 保存値の RAM 上の写し。登録応答で証明書を使う。master と att_priv は読み込み後に消す */
LOCAL T_U2F_KEYS keys_buf;

/*======================================================================
 * 初期化
 *====================================================================*/

LOCAL void derive_keys(const UB master[32])
{
	static const UB L_PRIV[] = "u2f-private-key";
	static const UB L_MAC[]  = "u2f-key-handle-mac";

	u2f_hmac_sha256(master, 32, L_PRIV, sizeof(L_PRIV) - 1, NULL, 0, u2f.priv_key);
	u2f_hmac_sha256(master, 32, L_MAC, sizeof(L_MAC) - 1, NULL, 0, u2f.mac_key);
}

LOCAL ER generate_keys(T_U2F_KEYS *k)
{
	ER err;
	INT i;

	memset(k, 0, sizeof(*k));
	err = u2f_random(k->master, sizeof(k->master));
	if (err != E_OK) return err;

	for (i = 0; i < 8; i++) {
		err = u2f_random(k->att_priv, sizeof(k->att_priv));
		if (err != E_OK) return err;
		if (u2f_p256_valid_private(k->att_priv)) break;
	}
	if (!u2f_p256_valid_private(k->att_priv)) return E_SYS;

	err = u2f_p256_pubkey(k->att_priv, k->att_pub);
	if (err != E_OK) return err;
	return u2f_build_cert(k->att_priv, k->att_pub, k->cert, sizeof(k->cert), &k->cert_len);
}

EXPORT ER u2f_init(void)
{
	ER err;

	memset(&u2f, 0, sizeof(u2f));

	err = u2f_store_load(&keys_buf);
	if (err != E_OK) {
		tm_printf((UB *)"U2F: %s, generating device keys\n",
			  (err == E_NOEXS) ? "no keys" : "stored keys corrupted");
		err = generate_keys(&keys_buf);
		if (err == E_OK) err = u2f_store_save(&keys_buf);
		if (err == E_OK) err = u2f_store_load(&keys_buf);
		if (err != E_OK) {
			tm_printf((UB *)"U2F: key generation failed (%d)\n", err);
			return err;
		}
	}

	/* 平文で保存されていた鍵を SAES で暗号化して保存し直す (master は同じ) */
	if (u2f_crypto_uses_saes() && !u2f_store_is_sealed(&keys_buf)) {
		err = u2f_store_save(&keys_buf);
		tm_printf((UB *)"U2F: stored keys re-saved encrypted (%s)\n",
			  (err == E_OK) ? "ok" : "failed");
	}

	derive_keys(keys_buf.master);
	memcpy(u2f.att_priv, keys_buf.att_priv, sizeof(u2f.att_priv));
	u2f.cert_len = keys_buf.cert_len;
	memset(keys_buf.master, 0, sizeof(keys_buf.master));
	memset(keys_buf.att_priv, 0, sizeof(keys_buf.att_priv));
	u2f.ready = TRUE;

	tm_printf((UB *)"U2F: ready (counter=%u, cert %u bytes)\n",
		  u2f_counter_get(), u2f.cert_len);
	return E_OK;
}

/*======================================================================
 * キーハンドル
 *====================================================================*/

LOCAL void kh_mac(const UB appid[32], const UB nonce[32], UB mac[32])
{
	u2f_hmac_sha256(u2f.mac_key, 32, appid, 32, nonce, 32, mac);
}

LOCAL void kh_private(const UB appid[32], const UB nonce[32], UB priv[32])
{
	u2f_hmac_sha256(u2f.priv_key, 32, appid, 32, nonce, 32, priv);
}

/* キーハンドルがこの装置で appid 向けに作ったものなら秘密鍵を返す */
LOCAL BOOL kh_open(const UB appid[32], const UB *kh, UW kh_len, UB priv[32])
{
	UB mac[32];

	if (kh_len != U2F_KH_LEN) return FALSE;
	kh_mac(appid, &kh[0], mac);
	if (!u2f_equal(mac, &kh[32], 32)) return FALSE;
	kh_private(appid, &kh[0], priv);
	return u2f_p256_valid_private(priv);
}

/*======================================================================
 * 応答
 *====================================================================*/

LOCAL UW put_sw(UB *resp, UW pos, UH sw)
{
	resp[pos++] = (UB)(sw >> 8);
	resp[pos++] = (UB)sw;
	return pos;
}

/*
 * REGISTER: challenge(32) || application(32)
 *   応答: 05 || 04 X Y || L || keyHandle || cert || signature
 */
LOCAL UW do_register(const UB *data, UW len, UB *resp, UW max, BOOL (*user_present)(void))
{
	const UB *challenge = &data[0];
	const UB *appid = &data[32];
	UB nonce[32], mac[32], priv[32], pub[P256_PUB_LEN];
	UB hash[32], sig[P256_SIG_LEN];
	UW pos = 0;
	INT i;
	UB reserved = 0x00;
	UB point_fmt = 0x04;

	if (len != 64) return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);
	if (max < 1 + 65 + 1 + U2F_KH_LEN + u2f.cert_len + U2F_DER_SIG_MAX + 2) {
		return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);
	}
	if (!user_present()) return put_sw(resp, 0, U2F_SW_CONDITIONS_NOT_SATISFIED);

	for (i = 0; i < 8; i++) {
		if (u2f_random(nonce, sizeof(nonce)) != E_OK) break;
		kh_private(appid, nonce, priv);
		if (u2f_p256_valid_private(priv)) break;
	}
	if (!u2f_p256_valid_private(priv) || u2f_p256_pubkey(priv, pub) != E_OK) {
		memset(priv, 0, sizeof(priv));
		return put_sw(resp, 0, U2F_SW_WRONG_DATA);
	}
	memset(priv, 0, sizeof(priv));
	kh_mac(appid, nonce, mac);

	/* 署名対象: 00 || application || challenge || keyHandle || publicKey */
	{
		UB msg[1 + 32 + 32 + U2F_KH_LEN + 65];
		UW m = 0;
		msg[m++] = reserved;
		memcpy(&msg[m], appid, 32);     m += 32;
		memcpy(&msg[m], challenge, 32); m += 32;
		memcpy(&msg[m], nonce, 32);     m += 32;
		memcpy(&msg[m], mac, 32);       m += 32;
		msg[m++] = point_fmt;
		memcpy(&msg[m], pub, 64);       m += 64;
		u2f_sha256(msg, m, hash);
	}
	if (u2f_p256_sign(u2f.att_priv, hash, sig) != E_OK) {
		return put_sw(resp, 0, U2F_SW_WRONG_DATA);
	}

	resp[pos++] = U2F_REGISTER_ID;
	resp[pos++] = point_fmt;
	memcpy(&resp[pos], pub, 64);            pos += 64;
	resp[pos++] = U2F_KH_LEN;
	memcpy(&resp[pos], nonce, 32);          pos += 32;
	memcpy(&resp[pos], mac, 32);            pos += 32;
	memcpy(&resp[pos], keys_buf.cert, u2f.cert_len); pos += u2f.cert_len;
	pos += u2f_der_signature(sig, &resp[pos]);

	u2f.registrations++;
	tm_printf((UB *)"U2F: registered\n");
	return put_sw(resp, pos, U2F_SW_NO_ERROR);
}

/*
 * AUTHENTICATE: challenge(32) || application(32) || L || keyHandle
 *   応答: userPresence(1) || counter(4) || signature
 */
LOCAL UW do_authenticate(UB p1, const UB *data, UW len, UB *resp, UW max,
			 BOOL (*user_present)(void))
{
	const UB *challenge = &data[0];
	const UB *appid = &data[32];
	UB priv[32], hash[32], sig[P256_SIG_LEN];
	UB flags = 0;
	UW kh_len, counter, pos = 0;

	if (len < 65) return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);
	kh_len = data[64];
	if (len != 65 + kh_len) return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);
	if (max < 1 + 4 + U2F_DER_SIG_MAX + 2) return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);

	if (!kh_open(appid, &data[65], kh_len, priv)) {
		memset(priv, 0, sizeof(priv));
		return put_sw(resp, 0, U2F_SW_WRONG_DATA);
	}

	switch (p1) {
	case U2F_AUTH_CHECK_ONLY:
		/* 自分のキーハンドルであることだけを伝える (仕様上この SW を返す) */
		memset(priv, 0, sizeof(priv));
		return put_sw(resp, 0, U2F_SW_CONDITIONS_NOT_SATISFIED);
	case U2F_AUTH_ENFORCE:
		if (!user_present()) {
			memset(priv, 0, sizeof(priv));
			return put_sw(resp, 0, U2F_SW_CONDITIONS_NOT_SATISFIED);
		}
		flags = U2F_FLAG_USER_PRESENT;
		break;
	case U2F_AUTH_DONT_ENFORCE:
		flags = 0;
		break;
	default:
		memset(priv, 0, sizeof(priv));
		return put_sw(resp, 0, U2F_SW_WRONG_DATA);
	}

	if (u2f_counter_next(&counter) != E_OK) {
		memset(priv, 0, sizeof(priv));
		return put_sw(resp, 0, U2F_SW_WRONG_DATA);
	}

	/* 署名対象: application || userPresence || counter || challenge */
	{
		UB msg[32 + 1 + 4 + 32];
		UW m = 0;
		memcpy(&msg[m], appid, 32); m += 32;
		msg[m++] = flags;
		msg[m++] = (UB)(counter >> 24);
		msg[m++] = (UB)(counter >> 16);
		msg[m++] = (UB)(counter >> 8);
		msg[m++] = (UB)counter;
		memcpy(&msg[m], challenge, 32); m += 32;
		u2f_sha256(msg, m, hash);
	}
	if (u2f_p256_sign(priv, hash, sig) != E_OK) {
		memset(priv, 0, sizeof(priv));
		return put_sw(resp, 0, U2F_SW_WRONG_DATA);
	}
	memset(priv, 0, sizeof(priv));

	resp[pos++] = flags;
	resp[pos++] = (UB)(counter >> 24);
	resp[pos++] = (UB)(counter >> 16);
	resp[pos++] = (UB)(counter >> 8);
	resp[pos++] = (UB)counter;
	pos += u2f_der_signature(sig, &resp[pos]);

	u2f.authentications++;
	return put_sw(resp, pos, U2F_SW_NO_ERROR);
}

/*======================================================================
 * APDU
 *====================================================================*/

EXPORT UW u2f_process_apdu(const UB *req, UW req_len, UB *resp, UW resp_max,
			   BOOL (*user_present)(void))
{
	UB cla, ins, p1;
	UW lc = 0;
	const UB *data = NULL;

	if (resp_max < 2) return 0;
	if (!u2f.ready) return put_sw(resp, 0, U2F_SW_CONDITIONS_NOT_SATISFIED);
	if (req_len < 4) return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);

	cla = req[0];
	ins = req[1];
	p1  = req[2];

	/*
	 * 長さ部の形式:
	 *   (なし)                 : 4 バイトのみ
	 *   拡張: 00 Lc1 Lc2 data [Le1 Le2] / 00 Le1 Le2 (データなし)
	 *   短縮: Lc data [Le] / Le (データなし, 5 バイト)
	 */
	if (req_len == 4 || req_len == 5) {
		lc = 0;
	} else if (req[4] == 0x00 && req_len >= 7) {
		lc = ((UW)req[5] << 8) | req[6];
		if (req_len == 7) {
			lc = 0;			/* 00 Le1 Le2 */
		} else if (req_len != 7 + lc && req_len != 7 + lc + 2) {
			return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);
		} else {
			data = &req[7];
		}
	} else {
		lc = req[4];
		if (req_len != 5 + lc && req_len != 5 + lc + 1) {
			return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);
		}
		data = &req[5];
	}

	if (cla != 0x00) return put_sw(resp, 0, U2F_SW_CLA_NOT_SUPPORTED);

	switch (ins) {
	case U2F_INS_REGISTER:
		if (data == NULL) return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);
		return do_register(data, lc, resp, resp_max, user_present);
	case U2F_INS_AUTHENTICATE:
		if (data == NULL) return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);
		return do_authenticate(p1, data, lc, resp, resp_max, user_present);
	case U2F_INS_VERSION:
		if (lc != 0) return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);
		if (resp_max < 8) return put_sw(resp, 0, U2F_SW_WRONG_LENGTH);
		memcpy(resp, "U2F_V2", 6);
		return put_sw(resp, 6, U2F_SW_NO_ERROR);
	default:
		return put_sw(resp, 0, U2F_SW_INS_NOT_SUPPORTED);
	}
}

/*
 * 秘密の値を持つアドレスか (MCP の peek/poke から守る)
 *   Flash の鍵セクタ、導出鍵・アテステーション鍵を持つ RAM、PKA RAM
 */
#define PKA_RAM_START	0x420C2400UL
#define PKA_RAM_END	(0x420C2400UL + 1334 * 4)
#define HASH_REG_START	0x420C0400UL	/* HASH のレジスタ (HMAC の途中状態と結果) */
#define HASH_REG_END	0x420C0800UL

EXPORT BOOL u2f_addr_is_secret(UW addr)
{
	UW keys_flash = H5_FLASH_BASE + H5_FLASH_U2F_KEYS_OFFSET;

	if (addr >= keys_flash && addr < keys_flash + H5_FLASH_SECTOR_SIZE) return TRUE;
	if (addr >= (UW)&u2f && addr < (UW)&u2f + sizeof(u2f)) return TRUE;
	if (addr >= (UW)&keys_buf && addr < (UW)&keys_buf + sizeof(keys_buf)) return TRUE;
	if (addr >= PKA_RAM_START && addr < PKA_RAM_END) return TRUE;
	if (addr >= HASH_REG_START && addr < HASH_REG_END) return TRUE;
	return FALSE;
}

EXPORT void u2f_get_status(T_U2F_STATUS *st)
{
	st->ready           = u2f.ready;
	st->uses_pka        = u2f_crypto_uses_pka();
	st->uses_hash       = u2f_crypto_uses_hash();
	st->uses_saes       = u2f_crypto_uses_saes();
	st->counter         = u2f_counter_get();
	st->registrations   = u2f.registrations;
	st->authentications = u2f.authentications;
}
