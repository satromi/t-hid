/*
 * u2f_crypto.c — U2F 用の暗号プリミティブ (TRNG / PKA / micro-ecc / SHA-256)
 */

#include <sys/machine.h>
#include <stddef.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

#include "u2f_crypto.h"
#include "uECC.h"
#include "../../device/wiznet/tk_sha256.h"

/*----------------------------------------------------------------------
 * P-256 パラメータ (ビッグエンディアン)
 */
LOCAL const UB P256_P[32] = {
	0xFF,0xFF,0xFF,0xFF, 0x00,0x00,0x00,0x01, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF };
LOCAL const UB P256_ABS_A[32] = {	/* a = -3 (PKA には符号と絶対値で渡す) */
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,3 };
LOCAL const UB P256_B[32] = {
	0x5A,0xC6,0x35,0xD8, 0xAA,0x3A,0x93,0xE7, 0xB3,0xEB,0xBD,0x55, 0x76,0x98,0x86,0xBC,
	0x65,0x1D,0x06,0xB0, 0xCC,0x53,0xB0,0xF6, 0x3B,0xCE,0x3C,0x3E, 0x27,0xD2,0x60,0x4B };
LOCAL const UB P256_GX[32] = {
	0x6B,0x17,0xD1,0xF2, 0xE1,0x2C,0x42,0x47, 0xF8,0xBC,0xE6,0xE5, 0x63,0xA4,0x40,0xF2,
	0x77,0x03,0x7D,0x81, 0x2D,0xEB,0x33,0xA0, 0xF4,0xA1,0x39,0x45, 0xD8,0x98,0xC2,0x96 };
LOCAL const UB P256_GY[32] = {
	0x4F,0xE3,0x42,0xE2, 0xFE,0x1A,0x7F,0x9B, 0x8E,0xE7,0xEB,0x4A, 0x7C,0x0F,0x9E,0x16,
	0x2B,0xCE,0x33,0x57, 0x6B,0x31,0x5E,0xCE, 0xCB,0xB6,0x40,0x68, 0x37,0xBF,0x51,0xF5 };
LOCAL const UB P256_N[32] = {
	0xFF,0xFF,0xFF,0xFF, 0x00,0x00,0x00,0x00, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
	0xBC,0xE6,0xFA,0xAD, 0xA7,0x17,0x9E,0x84, 0xF3,0xB9,0xCA,0xC2, 0xFC,0x63,0x25,0x51 };

LOCAL BOOL use_pka = FALSE;
LOCAL BOOL use_hash = FALSE;
LOCAL BOOL use_saes = FALSE;

/*======================================================================
 * 共通
 *====================================================================*/

EXPORT BOOL u2f_equal(const UB *a, const UB *b, UW len)
{
	UB diff = 0;
	UW i;
	for (i = 0; i < len; i++) diff |= a[i] ^ b[i];
	return diff == 0;
}

EXPORT BOOL u2f_p256_valid_private(const UB priv[P256_KEY_LEN])
{
	UB any = 0;
	INT i;

	for (i = 0; i < 32; i++) any |= priv[i];
	if (any == 0) return FALSE;
	for (i = 0; i < 32; i++) {
		if (priv[i] < P256_N[i]) return TRUE;
		if (priv[i] > P256_N[i]) return FALSE;
	}
	return FALSE;	/* d == n */
}

/*======================================================================
 * SHA-256 / HMAC
 *====================================================================*/

LOCAL void sw_sha256(const UB *data, UW len, UB out[32])
{
	SHA256_CTX ctx;
	sha256_init(&ctx);
	sha256_update(&ctx, data, len);
	sha256_final(&ctx, out);
}

LOCAL void sw_hmac_sha256(const UB *key, UW key_len,
			  const UB *m1, UW len1, const UB *m2, UW len2, UB out[32])
{
	SHA256_CTX ctx;
	UB k0[64], pad[64], inner[32];
	UW i;

	memset(k0, 0, sizeof(k0));
	if (key_len > 64) {
		sw_sha256(key, key_len, k0);
	} else {
		memcpy(k0, key, key_len);
	}

	for (i = 0; i < 64; i++) pad[i] = k0[i] ^ 0x36;
	sha256_init(&ctx);
	sha256_update(&ctx, pad, 64);
	if (len1 > 0) sha256_update(&ctx, m1, len1);
	if (len2 > 0) sha256_update(&ctx, m2, len2);
	sha256_final(&ctx, inner);

	for (i = 0; i < 64; i++) pad[i] = k0[i] ^ 0x5C;
	sha256_init(&ctx);
	sha256_update(&ctx, pad, 64);
	sha256_update(&ctx, inner, 32);
	sha256_final(&ctx, out);

	memset(k0, 0, sizeof(k0));
	memset(pad, 0, sizeof(pad));
}

/*======================================================================
 * 乱数
 *====================================================================*/

#if defined(U2F_HOST_TEST)

IMPORT INT u2f_host_random(UB *buf, UW len);
EXPORT ER u2f_random(UB *buf, UW len)
{
	return u2f_host_random(buf, len) ? E_OK : E_IO;
}

#else /* STM32H5 TRNG */

#define RCC_BASE_		(0x44020C00UL + U2F_PERIPH_ALIAS)
#define RCC_CR_			(RCC_BASE_ + 0x000)
#define RCC_AHB2ENR_		(RCC_BASE_ + 0x08C)
#define RCC_CR_HSI48ON		(1u << 12)
#define RCC_CR_HSI48RDY		(1u << 13)
#define RCC_AHB2ENR_RNGEN	(1u << 18)
#define RCC_AHB2ENR_PKAEN	(1u << 19)

#define RNG_BASE		(0x420C0800UL + U2F_PERIPH_ALIAS)
#define RNG_CR			(RNG_BASE + 0x00)
#define RNG_SR			(RNG_BASE + 0x04)
#define RNG_DR			(RNG_BASE + 0x08)
#define RNG_HTCR		(RNG_BASE + 0x10)
#define RNG_CR_RNGEN		(1u << 2)
#define RNG_CR_CONDRST		(1u << 30)
#define RNG_CR_NIST_VALUE	0x00F00D00u	/* AN4230 推奨値 (CMSIS RNG_CR_NIST_VALUE) */
#define RNG_HTCR_NIST_VALUE	0x0000AAC7u
#define RNG_SR_DRDY		(1u << 0)
#define RNG_SR_CECS		(1u << 1)
#define RNG_SR_SECS		(1u << 2)
#define RNG_SR_SEIS		(1u << 6)

LOCAL ER rng_init(void)
{
	UW i;

	/* RNG のカーネルクロックは既定の HSI48 */
	out_w(RCC_CR_, in_w(RCC_CR_) | RCC_CR_HSI48ON);
	while (!(in_w(RCC_CR_) & RCC_CR_HSI48RDY)) {}
	out_w(RCC_AHB2ENR_, in_w(RCC_AHB2ENR_) | RCC_AHB2ENR_RNGEN);
	(void)in_w(RCC_AHB2ENR_);

	out_w(RNG_CR, RNG_CR_NIST_VALUE | RNG_CR_CONDRST);
	out_w(RNG_HTCR, RNG_HTCR_NIST_VALUE);
	out_w(RNG_CR, in_w(RNG_CR) & ~RNG_CR_CONDRST);
	for (i = 0; (in_w(RNG_CR) & RNG_CR_CONDRST) && i < 1000000; i++) {}
	if (in_w(RNG_CR) & RNG_CR_CONDRST) return E_TMOUT;

	out_w(RNG_CR, in_w(RNG_CR) | RNG_CR_RNGEN);
	return E_OK;
}

/* シードエラーからの復帰: 条件付けロジックをリセットし直す */
LOCAL void rng_recover(void)
{
	UW i;
	out_w(RNG_CR, in_w(RNG_CR) | RNG_CR_CONDRST);
	out_w(RNG_CR, in_w(RNG_CR) & ~RNG_CR_CONDRST);
	for (i = 0; (in_w(RNG_CR) & RNG_CR_CONDRST) && i < 1000000; i++) {}
	out_w(RNG_SR, in_w(RNG_SR) & ~RNG_SR_SEIS);
}

LOCAL ER rng_word(UW *out)
{
	UW i, sr, v;
	INT retry;

	for (retry = 0; retry < 4; retry++) {
		for (i = 0; i < 1000000; i++) {
			sr = in_w(RNG_SR);
			if (sr & (RNG_SR_SECS | RNG_SR_CECS)) break;
			if (sr & RNG_SR_DRDY) break;
		}
		if (sr & RNG_SR_SECS) { rng_recover(); continue; }
		if (!(sr & RNG_SR_DRDY)) return E_TMOUT;
		v = in_w(RNG_DR);
		if (v == 0) continue;	/* シードエラー時は 0 が読める */
		*out = v;
		return E_OK;
	}
	return E_IO;
}

EXPORT ER u2f_random(UB *buf, UW len)
{
	UW i, w = 0;
	ER err;

	for (i = 0; i < len; i++) {
		if ((i & 3) == 0) {
			err = rng_word(&w);
			if (err != E_OK) return err;
		}
		buf[i] = (UB)w;
		w >>= 8;
	}
	return E_OK;
}

/*======================================================================
 * PKA (RM0481 "Public key accelerator")
 *====================================================================*/

#define PKA_BASE		(0x420C2000UL + U2F_PERIPH_ALIAS)
#define PKA_CR			(PKA_BASE + 0x00)
#define PKA_SR			(PKA_BASE + 0x04)
#define PKA_CLRFR		(PKA_BASE + 0x08)
#define PKA_RAM(idx)		(PKA_BASE + 0x400 + 4 * (idx))

#define PKA_CR_EN		(1u << 0)
#define PKA_CR_START		(1u << 1)
#define PKA_CR_MODE_SHIFT	8
#define PKA_CR_MODE_MASK	(0x3Fu << 8)
#define PKA_CR_IE_MASK		((1u << 17) | (1u << 19) | (1u << 20) | (1u << 21))
#define PKA_SR_INITOK		(1u << 0)
#define PKA_SR_BUSY		(1u << 16)
#define PKA_SR_PROCENDF		(1u << 17)
#define PKA_SR_ERR_MASK		((1u << 19) | (1u << 20) | (1u << 21))
#define PKA_CLRFR_ALL		((1u << 17) | (1u << 19) | (1u << 20) | (1u << 21))

#define PKA_MODE_ECC_MUL	0x20
#define PKA_MODE_ECDSA_SIGN	0x24
#define PKA_NO_ERROR		0xD60Du

/* PKA RAM の語インデックス (CMSIS stm32h533xx.h: (アドレス - 0x400) >> 2) */
#define PKA_IDX(addr)		(((addr) - 0x400u) >> 2)

#define MUL_IN_EXP_NB_BITS	PKA_IDX(0x0400u)
#define MUL_IN_OP_NB_BITS	PKA_IDX(0x0408u)
#define MUL_IN_A_COEFF_SIGN	PKA_IDX(0x0410u)
#define MUL_IN_A_COEFF		PKA_IDX(0x0418u)
#define MUL_IN_B_COEFF		PKA_IDX(0x0520u)
#define MUL_IN_MOD_GF		PKA_IDX(0x1088u)
#define MUL_IN_K		PKA_IDX(0x12A0u)
#define MUL_IN_POINT_X		PKA_IDX(0x0578u)
#define MUL_IN_POINT_Y		PKA_IDX(0x0470u)
#define MUL_IN_N		PKA_IDX(0x0F88u)
#define MUL_OUT_X		PKA_IDX(0x0578u)
#define MUL_OUT_Y		PKA_IDX(0x05D0u)
#define MUL_OUT_ERROR		PKA_IDX(0x0680u)

#define SIGN_IN_ORDER_NB_BITS	PKA_IDX(0x0400u)
#define SIGN_IN_MOD_NB_BITS	PKA_IDX(0x0408u)
#define SIGN_IN_A_COEFF_SIGN	PKA_IDX(0x0410u)
#define SIGN_IN_A_COEFF		PKA_IDX(0x0418u)
#define SIGN_IN_B_COEFF		PKA_IDX(0x0520u)
#define SIGN_IN_MOD_GF		PKA_IDX(0x1088u)
#define SIGN_IN_K		PKA_IDX(0x12A0u)
#define SIGN_IN_POINT_X		PKA_IDX(0x0578u)
#define SIGN_IN_POINT_Y		PKA_IDX(0x0470u)
#define SIGN_IN_HASH_E		PKA_IDX(0x0FE8u)
#define SIGN_IN_PRIV_D		PKA_IDX(0x0F28u)
#define SIGN_IN_ORDER_N		PKA_IDX(0x0F88u)
#define SIGN_OUT_ERROR		PKA_IDX(0x0FE0u)
#define SIGN_OUT_R		PKA_IDX(0x0730u)
#define SIGN_OUT_S		PKA_IDX(0x0788u)

/*
 * ビッグエンディアンのバイト列をワード単位で下位から格納し、
 * 直後に 0 のワードを 2 つ置く (オペランドの終端)
 */
LOCAL void pka_write(UW idx, const UB *be, UW len)
{
	UW nw = (len + 3) / 4;
	UW i;

	for (i = 0; i < nw; i++) {
		UW w = 0;
		UW b;
		for (b = 0; b < 4; b++) {
			UW pos = i * 4 + b;		/* 下位からのバイト位置 */
			if (pos < len) w |= (UW)be[len - 1 - pos] << (8 * b);
		}
		out_w(PKA_RAM(idx + i), w);
	}
	out_w(PKA_RAM(idx + nw), 0);
	out_w(PKA_RAM(idx + nw + 1), 0);
}

LOCAL void pka_read(UW idx, UB *be, UW len)
{
	UW i;
	for (i = 0; i < len; i++) {
		UW w = in_w(PKA_RAM(idx + i / 4));
		be[len - 1 - i] = (UB)(w >> (8 * (i % 4)));
	}
}

/* 秘密の値 (鍵・乱数 k) を PKA RAM に残さない */
LOCAL void pka_wipe(UW idx, UW len)
{
	UW i;
	for (i = 0; i < (len + 3) / 4 + 2; i++) out_w(PKA_RAM(idx + i), 0);
}

LOCAL ER pka_init(void)
{
	UW i;

	out_w(RCC_AHB2ENR_, in_w(RCC_AHB2ENR_) | RCC_AHB2ENR_PKAEN);
	(void)in_w(RCC_AHB2ENR_);

	/* EN が立つまで書き続ける (PKA RAM の消去中は無視される) */
	for (i = 0; !(in_w(PKA_CR) & PKA_CR_EN) && i < 1000000; i++) {
		out_w(PKA_CR, PKA_CR_EN);
	}
	if (!(in_w(PKA_CR) & PKA_CR_EN)) return E_TMOUT;

	for (i = 0; !(in_w(PKA_SR) & PKA_SR_INITOK) && i < 10000000; i++) {}
	if (!(in_w(PKA_SR) & PKA_SR_INITOK)) return E_TMOUT;

	out_w(PKA_CLRFR, PKA_CLRFR_ALL);
	return E_OK;
}

LOCAL ER pka_run(UW mode, UW err_idx)
{
	UW i, sr;
	ER err = E_OK;

	out_w(PKA_CR, (in_w(PKA_CR) & ~(PKA_CR_MODE_MASK | PKA_CR_IE_MASK)) |
		      (mode << PKA_CR_MODE_SHIFT));
	out_w(PKA_CR, in_w(PKA_CR) | PKA_CR_START);

	for (i = 0; i < 100000000; i++) {
		sr = in_w(PKA_SR);
		if (sr & (PKA_SR_PROCENDF | PKA_SR_ERR_MASK)) break;
	}
	if (!(sr & PKA_SR_PROCENDF)) {
		/* 実行中の演算を止めて次に備える */
		out_w(PKA_CR, in_w(PKA_CR) & ~PKA_CR_EN);
		out_w(PKA_CR, in_w(PKA_CR) | PKA_CR_EN);
		err = E_TMOUT;
	} else if ((sr & PKA_SR_ERR_MASK) || in_w(PKA_RAM(err_idx)) != PKA_NO_ERROR) {
		err = E_IO;
	}
	out_w(PKA_CLRFR, PKA_CLRFR_ALL);
	return err;
}

LOCAL ER pka_pubkey(const UB priv[32], UB pub[64])
{
	out_w(PKA_RAM(MUL_IN_EXP_NB_BITS), 256);
	out_w(PKA_RAM(MUL_IN_OP_NB_BITS), 256);
	out_w(PKA_RAM(MUL_IN_A_COEFF_SIGN), 1);
	pka_write(MUL_IN_A_COEFF, P256_ABS_A, 32);
	pka_write(MUL_IN_B_COEFF, P256_B, 32);
	pka_write(MUL_IN_MOD_GF, P256_P, 32);
	pka_write(MUL_IN_K, priv, 32);
	pka_write(MUL_IN_POINT_X, P256_GX, 32);
	pka_write(MUL_IN_POINT_Y, P256_GY, 32);
	pka_write(MUL_IN_N, P256_N, 32);

	if (pka_run(PKA_MODE_ECC_MUL, MUL_OUT_ERROR) != E_OK) {
		pka_wipe(MUL_IN_K, 32);
		return E_IO;
	}
	pka_read(MUL_OUT_X, &pub[0], 32);
	pka_read(MUL_OUT_Y, &pub[32], 32);
	pka_wipe(MUL_IN_K, 32);
	return E_OK;
}

LOCAL ER pka_sign(const UB priv[32], const UB hash[32], UB sig[64])
{
	UB k[32];
	INT retry;
	ER err = E_IO;

	for (retry = 0; retry < 4; retry++) {
		if (u2f_random(k, sizeof(k)) != E_OK) break;
		if (!u2f_p256_valid_private(k)) continue;

		out_w(PKA_RAM(SIGN_IN_ORDER_NB_BITS), 256);
		out_w(PKA_RAM(SIGN_IN_MOD_NB_BITS), 256);
		out_w(PKA_RAM(SIGN_IN_A_COEFF_SIGN), 1);
		pka_write(SIGN_IN_A_COEFF, P256_ABS_A, 32);
		pka_write(SIGN_IN_B_COEFF, P256_B, 32);
		pka_write(SIGN_IN_MOD_GF, P256_P, 32);
		pka_write(SIGN_IN_K, k, 32);
		pka_write(SIGN_IN_POINT_X, P256_GX, 32);
		pka_write(SIGN_IN_POINT_Y, P256_GY, 32);
		pka_write(SIGN_IN_HASH_E, hash, 32);
		pka_write(SIGN_IN_PRIV_D, priv, 32);
		pka_write(SIGN_IN_ORDER_N, P256_N, 32);

		/* r または s が 0 になった場合もエラーで返るので k を替えて再試行 */
		err = pka_run(PKA_MODE_ECDSA_SIGN, SIGN_OUT_ERROR);
		if (err == E_OK) {
			pka_read(SIGN_OUT_R, &sig[0], 32);
			pka_read(SIGN_OUT_S, &sig[32], 32);
			break;
		}
	}
	memset(k, 0, sizeof(k));
	pka_wipe(SIGN_IN_K, 32);
	pka_wipe(SIGN_IN_PRIV_D, 32);
	return err;
}

/*----------------------------------------------------------------------
 * HASH (SHA-256 / HMAC-SHA256 のハードウェア計算)
 *   データは 8 ビット単位 (DATATYPE=2) で渡し、バイト順の入れ替えは HASH に任せる。
 *   DIN への書き込みは HASH が受け付けるまでバスで待たされるので、
 *   ブロックごとの DINIS 待ちは不要。
 *   完了フラグ (DINIS/DCIS) は前の計算の分が残るので、計算開始 (DCAL) の
 *   直前に SR へ 0 を書いてクリアしてから待つ。
 */
#define HASH_BASE		(0x420C0400UL + U2F_PERIPH_ALIAS)
#define HASH_CR			(HASH_BASE + 0x00)
#define HASH_DIN		(HASH_BASE + 0x04)
#define HASH_STR		(HASH_BASE + 0x08)
#define HASH_SR			(HASH_BASE + 0x24)
#define HASH_HR(i)		(HASH_BASE + 0x310 + 4 * (i))
#define HASH_CR_INIT		(1u << 2)
#define HASH_CR_DATATYPE_8B	(2u << 4)
#define HASH_CR_MODE_HMAC	(1u << 6)
#define HASH_CR_ALGO_SHA256	(3u << 17)
#define HASH_STR_DCAL		(1u << 8)
#define HASH_SR_DINIS		(1u << 0)
#define HASH_SR_DCIS		(1u << 1)
#define HASH_SR_BUSY		(1u << 3)
#define RCC_AHB2ENR_HASHEN	(1u << 17)
#define HASH_TIMEOUT		1000000

/* 1 回分の入力 (最後の語の有効ビット数を設定 → データ → 計算開始) */
LOCAL void hash_feed(const UB *data, UW len)
{
	UW i, w;

	out_w(HASH_STR, 8 * (len % 4));
	for (i = 0; i + 4 <= len; i += 4) {
		w = (UW)data[i] | ((UW)data[i + 1] << 8) |
		    ((UW)data[i + 2] << 16) | ((UW)data[i + 3] << 24);
		out_w(HASH_DIN, w);
	}
	if (i < len) {
		w = 0;
		for (; i < len; i++) w |= (UW)data[i] << (8 * (i % 4));
		out_w(HASH_DIN, w);
	}
	out_w(HASH_SR, 0);
	out_w(HASH_STR, in_w(HASH_STR) | HASH_STR_DCAL);
}

/* SR の flag が立ち、かつ BUSY が落ちるまで待つ */
LOCAL ER hash_wait(UW flag)
{
	UW i, sr;

	for (i = 0; i < HASH_TIMEOUT; i++) {
		sr = in_w(HASH_SR);
		if ((sr & flag) && !(sr & HASH_SR_BUSY)) return E_OK;
	}
	return E_TMOUT;
}

LOCAL void hash_read(UB out[32])
{
	UW i, v;

	for (i = 0; i < 8; i++) {
		v = in_w(HASH_HR(i));
		out[4 * i]     = (UB)(v >> 24);
		out[4 * i + 1] = (UB)(v >> 16);
		out[4 * i + 2] = (UB)(v >> 8);
		out[4 * i + 3] = (UB)v;
	}
}

LOCAL ER hw_sha256(const UB *data, UW len, UB out[32])
{
	out_w(HASH_CR, HASH_CR_ALGO_SHA256 | HASH_CR_DATATYPE_8B | HASH_CR_INIT);
	hash_feed(data, len);
	if (hash_wait(HASH_SR_DCIS) != E_OK) return E_TMOUT;
	hash_read(out);
	return E_OK;
}

/* 導出鍵が HASH のレジスタに残らないよう、空データのハッシュで上書きする */
LOCAL void hash_scrub(void)
{
	UB dummy[32];

	(void)hw_sha256(NULL, 0, dummy);
}

/*
 * 鍵は 64 バイト以下 (U2F の鍵はすべて 32 バイト)。
 * メッセージが空だと、メッセージ段の後に DINIS が立たず先へ進めないため扱わない
 * (U2F の HMAC は常にメッセージを持つ)。
 */
LOCAL ER hw_hmac_sha256(const UB *key, UW key_len, const UB *msg, UW len, UB out[32])
{
	ER err = E_TMOUT;

	if (key_len > 64 || len == 0) return E_PAR;

	out_w(HASH_CR, HASH_CR_ALGO_SHA256 | HASH_CR_DATATYPE_8B |
		       HASH_CR_MODE_HMAC | HASH_CR_INIT);
	hash_feed(key, key_len);
	if (hash_wait(HASH_SR_DINIS) != E_OK) goto out;
	hash_feed(msg, len);
	if (hash_wait(HASH_SR_DINIS) != E_OK) goto out;
	hash_feed(key, key_len);
	if (hash_wait(HASH_SR_DCIS) != E_OK) goto out;
	hash_read(out);
	err = E_OK;
out:
	hash_scrub();
	return err;
}

LOCAL void hash_init(void)
{
	out_w(RCC_AHB2ENR_, in_w(RCC_AHB2ENR_) | RCC_AHB2ENR_HASHEN);
	(void)in_w(RCC_AHB2ENR_);
}

/*----------------------------------------------------------------------
 * SAES (保存する秘密の暗号化)
 *   鍵は DHUK (チップ固有の鍵から導出され、ソフトウェアからは読めない)。
 *   AES-256 CTR で暗号化と復号が同じ操作になる。DHUK は隠蔽レベル (HDPL) や
 *   セキュリティ状態ごとに異なるので、必ず起動直後の同じ状態で使う。
 */
#define SAES_BASE		(0x420C0C00UL + U2F_PERIPH_ALIAS)
#define SAES_CR			(SAES_BASE + 0x000)
#define SAES_SR			(SAES_BASE + 0x004)
#define SAES_DINR		(SAES_BASE + 0x008)
#define SAES_DOUTR		(SAES_BASE + 0x00C)
#define SAES_IVR(i)		(SAES_BASE + 0x020 + 4 * (i))
#define SAES_ISR		(SAES_BASE + 0x304)
#define SAES_ICR		(SAES_BASE + 0x308)
#define SAES_CR_EN		(1u << 0)
#define SAES_CR_DATATYPE_8B	(2u << 1)
#define SAES_CR_CHMOD_CTR	(2u << 5)
#define SAES_CR_KEYSIZE_256	(1u << 18)
#define SAES_CR_KEYSEL_DHUK	(1u << 28)
#define SAES_CR_IPRST		(1u << 31)
#define SAES_SR_BUSY		(1u << 3)
#define SAES_SR_KEYVALID	(1u << 7)
#define SAES_ISR_CCF		(1u << 0)
#define SAES_ISR_RNGEIF		(1u << 3)
#define RCC_AHB2ENR_SAESEN	(1u << 20)
#define SAES_TIMEOUT		1000000

LOCAL ER saes_wait(UW reg, UW flag, BOOL set)
{
	UW i;

	for (i = 0; i < SAES_TIMEOUT; i++) {
		if (((in_w(reg) & flag) != 0) == set) return E_OK;
	}
	return E_TMOUT;
}

LOCAL UW pack_le(const UB *p)
{
	return (UW)p[0] | ((UW)p[1] << 8) | ((UW)p[2] << 16) | ((UW)p[3] << 24);
}

LOCAL void unpack_le(UB *p, UW v)
{
	p[0] = (UB)v; p[1] = (UB)(v >> 8); p[2] = (UB)(v >> 16); p[3] = (UB)(v >> 24);
}

/* SAES をリセットし、鍵と途中状態をレジスタから消す */
LOCAL void saes_reset(void)
{
	out_w(SAES_CR, SAES_CR_IPRST);
	out_w(SAES_CR, 0);
}

/* buf (16 の倍数バイト) をその場で AES-256 CTR 処理する。IV = nonce (8) || 0 (8) */
LOCAL ER saes_ctr(const UB nonce[8], UB *buf, UW len)
{
	UB iv[16];
	UW i, j;
	ER err = E_TMOUT;

	if (len % 16 != 0) return E_PAR;

	memcpy(iv, nonce, 8);
	memset(iv + 8, 0, 8);

	saes_reset();
	/* 起動直後の SAES は RNG から乱数を取り込む間 BUSY になる */
	if (saes_wait(SAES_SR, SAES_SR_BUSY, FALSE) != E_OK) goto out;
	if (in_w(SAES_ISR) & SAES_ISR_RNGEIF) { err = E_IO; goto out; }

	out_w(SAES_CR, SAES_CR_KEYSEL_DHUK | SAES_CR_KEYSIZE_256 |
		       SAES_CR_CHMOD_CTR | SAES_CR_DATATYPE_8B);
	if (saes_wait(SAES_SR, SAES_SR_KEYVALID, TRUE) != E_OK) goto out;

	for (i = 0; i < 4; i++) {
		out_w(SAES_IVR(3 - i), ((UW)iv[4 * i] << 24) | ((UW)iv[4 * i + 1] << 16) |
				       ((UW)iv[4 * i + 2] << 8) | iv[4 * i + 3]);
	}
	out_w(SAES_CR, in_w(SAES_CR) | SAES_CR_EN);

	for (i = 0; i < len; i += 16) {
		for (j = 0; j < 16; j += 4) out_w(SAES_DINR, pack_le(buf + i + j));
		if (saes_wait(SAES_ISR, SAES_ISR_CCF, TRUE) != E_OK) goto out;
		for (j = 0; j < 16; j += 4) unpack_le(buf + i + j, in_w(SAES_DOUTR));
		out_w(SAES_ICR, SAES_ISR_CCF);
	}
	err = E_OK;
out:
	saes_reset();
	return err;
}

/* 暗号化して復号すると元に戻り、暗号文が平文と異なることを確かめる */
LOCAL BOOL saes_self_test(void)
{
	UB nonce[8], plain[64], buf[64];

	out_w(RCC_AHB2ENR_, in_w(RCC_AHB2ENR_) | RCC_AHB2ENR_SAESEN);
	(void)in_w(RCC_AHB2ENR_);

	u2f_random(nonce, sizeof(nonce));
	u2f_random(plain, sizeof(plain));
	memcpy(buf, plain, sizeof(buf));
	if (saes_ctr(nonce, buf, sizeof(buf)) != E_OK) return FALSE;
	if (u2f_equal(buf, plain, sizeof(buf))) return FALSE;
	if (saes_ctr(nonce, buf, sizeof(buf)) != E_OK) return FALSE;
	return u2f_equal(buf, plain, sizeof(buf));
}

#endif /* U2F_HOST_TEST */

/*======================================================================
 * 保存する秘密の暗号化 (SAES が使えるときだけ)
 *====================================================================*/

EXPORT ER u2f_seal(const UB nonce[8], UB *buf, UW len)
{
#if !defined(U2F_HOST_TEST)
	if (use_saes) return saes_ctr(nonce, buf, len);
#endif
	(void)nonce; (void)buf; (void)len;
	return E_NOSPT;
}

/*======================================================================
 * SHA-256 / HMAC の入口
 *   HASH の自己診断が通っていればハードウェア、そうでなければソフトウェア
 *====================================================================*/

#define HW_HMAC_MSG_MAX		128

EXPORT void u2f_sha256(const UB *data, UW len, UB out[32])
{
#if !defined(U2F_HOST_TEST)
	if (use_hash && hw_sha256(data, len, out) == E_OK) return;
#endif
	sw_sha256(data, len, out);
}

EXPORT void u2f_hmac_sha256(const UB *key, UW key_len,
			    const UB *m1, UW len1, const UB *m2, UW len2, UB out[32])
{
#if !defined(U2F_HOST_TEST)
	UB msg[HW_HMAC_MSG_MAX];
	ER err = E_PAR;

	if (use_hash && key_len <= 64 && len1 + len2 > 0 && len1 + len2 <= sizeof(msg)) {
		if (len1 > 0) memcpy(msg, m1, len1);
		if (len2 > 0) memcpy(msg + len1, m2, len2);
		err = hw_hmac_sha256(key, key_len, msg, len1 + len2, out);
		memset(msg, 0, sizeof(msg));
	}
	if (err == E_OK) return;
#endif
	sw_hmac_sha256(key, key_len, m1, len1, m2, len2, out);
}

/*======================================================================
 * micro-ecc
 *====================================================================*/

LOCAL int uecc_rng(uint8_t *dest, unsigned size)
{
	return u2f_random(dest, size) == E_OK;
}

/*======================================================================
 * 公開 API
 *====================================================================*/

EXPORT ER u2f_p256_pubkey(const UB priv[P256_KEY_LEN], UB pub[P256_PUB_LEN])
{
	if (!u2f_p256_valid_private(priv)) return E_PAR;
#if !defined(U2F_HOST_TEST)
	if (use_pka) return pka_pubkey(priv, pub);
#endif
	return uECC_compute_public_key(priv, pub, uECC_secp256r1()) ? E_OK : E_IO;
}

EXPORT ER u2f_p256_sign(const UB priv[P256_KEY_LEN], const UB hash[32], UB sig[P256_SIG_LEN])
{
	if (!u2f_p256_valid_private(priv)) return E_PAR;
#if !defined(U2F_HOST_TEST)
	if (use_pka) return pka_sign(priv, hash, sig);
#endif
	return uECC_sign(priv, hash, 32, sig, uECC_secp256r1()) ? E_OK : E_IO;
}

EXPORT BOOL u2f_p256_verify(const UB pub[P256_PUB_LEN], const UB hash[32], const UB sig[P256_SIG_LEN])
{
	return uECC_verify(pub, hash, 32, sig, uECC_secp256r1()) ? TRUE : FALSE;
}

EXPORT BOOL u2f_crypto_uses_pka(void)
{
	return use_pka;
}

EXPORT BOOL u2f_crypto_uses_hash(void)
{
	return use_hash;
}

EXPORT BOOL u2f_crypto_uses_saes(void)
{
	return use_saes;
}

/*
 * 自己診断用の既知の鍵 (RFC 6979 A.2.5, P-256)
 */
LOCAL const UB TEST_PRIV[32] = {
	0xC9,0xAF,0xA9,0xD8, 0x45,0xBA,0x75,0x16, 0x6B,0x5C,0x21,0x57, 0x67,0xB1,0xD6,0x93,
	0x4E,0x50,0xC3,0xDB, 0x36,0xE8,0x9B,0x12, 0x7B,0x8A,0x62,0x2B, 0x12,0x0F,0x67,0x21 };
LOCAL const UB TEST_PUB[64] = {
	0x60,0xFE,0xD4,0xBA, 0x25,0x5A,0x9D,0x31, 0xC9,0x61,0xEB,0x74, 0xC6,0x35,0x6D,0x68,
	0xC0,0x49,0xB8,0x92, 0x3B,0x61,0xFA,0x6C, 0xE6,0x69,0x62,0x2E, 0x60,0xF2,0x9F,0xB6,
	0x79,0x03,0xFE,0x10, 0x08,0xB8,0xBC,0x99, 0xA4,0x1A,0xE9,0xE9, 0x56,0x28,0xBC,0x64,
	0xF2,0xF1,0xB2,0x0C, 0x2D,0x7E,0x9F,0x51, 0x77,0xA3,0xC2,0x94, 0xD4,0x46,0x22,0x99 };

#if !defined(U2F_HOST_TEST)
/*
 * HASH の自己診断
 *   SHA-256("abc") と RFC 4231 テストケース 2 を既知の値と比べ、さらに
 *   境界の長さ (0, 1, 3, 55, 56, 63, 64, 65, 100 バイト) でソフトウェア実装と比べる。
 *   HMAC は空のメッセージを扱わないので 1 バイト以上で比べる。
 */
LOCAL const UB TEST_SHA256_ABC[32] = {
	0xBA,0x78,0x16,0xBF, 0x8F,0x01,0xCF,0xEA, 0x41,0x41,0x40,0xDE, 0x5D,0xAE,0x22,0x23,
	0xB0,0x03,0x61,0xA3, 0x96,0x17,0x7A,0x9C, 0xB4,0x10,0xFF,0x61, 0xF2,0x00,0x15,0xAD };
LOCAL const UB TEST_HMAC_JEFE[32] = {
	0x5B,0xDC,0xC1,0x46, 0xBF,0x60,0x75,0x4E, 0x6A,0x04,0x24,0x26, 0x08,0x95,0x75,0xC7,
	0x5A,0x00,0x3F,0x08, 0x9D,0x27,0x39,0x83, 0x9D,0xEC,0x58,0xB9, 0x64,0xEC,0x38,0x43 };

LOCAL BOOL hash_self_test(void)
{
	LOCAL const UW lens[] = { 0, 1, 3, 55, 56, 63, 64, 65, 100 };
	UB data[100], key[32], hw[32], sw[32];
	UW i;

	if (hw_sha256((const UB *)"abc", 3, hw) != E_OK || !u2f_equal(hw, TEST_SHA256_ABC, 32)) {
		return FALSE;
	}
	if (hw_hmac_sha256((const UB *)"Jefe", 4,
			   (const UB *)"what do ya want for nothing?", 28, hw) != E_OK ||
	    !u2f_equal(hw, TEST_HMAC_JEFE, 32)) {
		return FALSE;
	}

	u2f_random(data, sizeof(data));
	u2f_random(key, sizeof(key));
	for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
		if (hw_sha256(data, lens[i], hw) != E_OK) return FALSE;
		sw_sha256(data, lens[i], sw);
		if (!u2f_equal(hw, sw, 32)) return FALSE;

		if (lens[i] == 0) continue;
		if (hw_hmac_sha256(key, sizeof(key), data, lens[i], hw) != E_OK) return FALSE;
		sw_hmac_sha256(key, sizeof(key), data, lens[i], NULL, 0, sw);
		if (!u2f_equal(hw, sw, 32)) return FALSE;
	}
	return TRUE;
}
#endif

EXPORT ER u2f_crypto_init(void)
{
	UB pub[64], hash[32], sig[64];

	uECC_set_rng(uecc_rng);
	use_pka = FALSE;

#if !defined(U2F_HOST_TEST)
	{
		ER err = rng_init();
		if (err != E_OK) {
			tm_printf((UB *)"U2F: TRNG init failed (%d)\n", err);
			return err;
		}
	}

	/* PKA の結果を既知の値と micro-ecc の検証で確かめてから使う */
	if (pka_init() == E_OK &&
	    pka_pubkey(TEST_PRIV, pub) == E_OK && u2f_equal(pub, TEST_PUB, 64)) {
		u2f_random(hash, sizeof(hash));
		if (pka_sign(TEST_PRIV, hash, sig) == E_OK &&
		    u2f_p256_verify(TEST_PUB, hash, sig)) {
			use_pka = TRUE;
		}
	}
	tm_printf((UB *)"U2F: P-256 by %s\n", use_pka ? "PKA" : "software (PKA self-test failed)");

	/* HASH の結果を既知の値とソフトウェア実装で確かめてから使う */
	hash_init();
	use_hash = hash_self_test();
	tm_printf((UB *)"U2F: SHA-256 by %s\n", use_hash ? "HASH" : "software (HASH self-test failed)");

	/* 保存する鍵の暗号化 (DHUK) */
	use_saes = saes_self_test();
	tm_printf((UB *)"U2F: key storage %s\n",
		  use_saes ? "encrypted by SAES (DHUK)" : "in plaintext (SAES self-test failed)");
#endif

	/* ソフトウェア側も既知の値で確認しておく */
	if (!use_pka) {
		if (u2f_p256_pubkey(TEST_PRIV, pub) != E_OK || !u2f_equal(pub, TEST_PUB, 64)) {
			return E_SYS;
		}
		u2f_random(hash, sizeof(hash));
		if (u2f_p256_sign(TEST_PRIV, hash, sig) != E_OK ||
		    !u2f_p256_verify(TEST_PUB, hash, sig)) {
			return E_SYS;
		}
	}
	return E_OK;
}
