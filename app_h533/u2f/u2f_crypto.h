/*
 * u2f_crypto.h — U2F 用の暗号プリミティブ
 *
 *   乱数       : STM32H5 TRNG
 *   SHA-256    : ソフトウェア (tk_sha256.c)
 *   P-256      : PKA (公開鍵導出・ECDSA 署名)。起動時の自己診断で
 *                micro-ecc と結果が一致しなければ micro-ecc で行う
 *
 * PC 上の検証ビルド (U2F_HOST_TEST) では PKA/TRNG を使わない。
 */

#ifndef __U2F_CRYPTO_H__
#define __U2F_CRYPTO_H__

#include <tk/tkernel.h>

#define P256_KEY_LEN	32	/* 秘密鍵・座標・ハッシュ */
#define P256_PUB_LEN	64	/* X || Y (0x04 なし) */
#define P256_SIG_LEN	64	/* r || s */

/* 初期化と PKA 自己診断。PKA が使えるかどうかは u2f_crypto_uses_pka() */
ER   u2f_crypto_init(void);
BOOL u2f_crypto_uses_pka(void);

/*
 * 周辺回路のアドレスの切り替え
 *   TrustZone のセキュア側イメージ (U2F_SECURE) では、暗号の周辺回路・RCC・SBS・
 *   Flash をセキュア側のアドレス (非セキュア側 + 0x10000000) で扱う。
 */
#if defined(U2F_SECURE)
#define U2F_PERIPH_ALIAS	0x10000000UL
#else
#define U2F_PERIPH_ALIAS	0UL
#endif
BOOL u2f_crypto_uses_hash(void);
BOOL u2f_crypto_uses_saes(void);

/* 保存する秘密を SAES (DHUK, AES-256 CTR) で暗号化・復号する (同じ操作)。
 * len は 16 の倍数。SAES が使えなければ E_NOSPT */
ER u2f_seal(const UB nonce[8], UB *buf, UW len);

/* 乱数 */
ER   u2f_random(UB *buf, UW len);

/* SHA-256 / HMAC-SHA256 (メッセージは 2 つまで連結して与えられる) */
void u2f_sha256(const UB *data, UW len, UB out[32]);
void u2f_hmac_sha256(const UB *key, UW key_len,
		     const UB *m1, UW len1, const UB *m2, UW len2, UB out[32]);

/* P-256 */
ER   u2f_p256_pubkey(const UB priv[P256_KEY_LEN], UB pub[P256_PUB_LEN]);
ER   u2f_p256_sign(const UB priv[P256_KEY_LEN], const UB hash[32], UB sig[P256_SIG_LEN]);
BOOL u2f_p256_verify(const UB pub[P256_PUB_LEN], const UB hash[32], const UB sig[P256_SIG_LEN]);

/* 秘密鍵として使えるか (1 <= d < n) */
BOOL u2f_p256_valid_private(const UB priv[P256_KEY_LEN]);

/* 定数時間比較 */
BOOL u2f_equal(const UB *a, const UB *b, UW len);

#endif /* __U2F_CRYPTO_H__ */
