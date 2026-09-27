/*
 * u2f_cert.c — アテステーション用の自己署名 X.509 証明書 (DER) を作る
 *
 *   Certificate ::= SEQUENCE {
 *     tbsCertificate (v1: version フィールドなし)
 *       serialNumber, signature(ecdsa-with-SHA256), issuer, validity,
 *       subject, subjectPublicKeyInfo(id-ecPublicKey, prime256v1)
 *     signatureAlgorithm (ecdsa-with-SHA256)
 *     signatureValue BIT STRING (ECDSA-Sig-Value)
 *   }
 * issuer = subject = CN=uT-Kernel U2F
 */

#include <sys/machine.h>
#include <stddef.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>

#include "u2f.h"
#include "u2f_crypto.h"

#define CERT_CN		"uT-Kernel U2F"
#define NOT_BEFORE	"240101000000Z"
#define NOT_AFTER	"491231235959Z"

LOCAL const UB OID_ECDSA_SHA256[] = { 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02 };
LOCAL const UB OID_EC_PUBKEY[]    = { 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01 };
LOCAL const UB OID_PRIME256V1[]   = { 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07 };
LOCAL const UB OID_CN[]           = { 0x06, 0x03, 0x55, 0x04, 0x03 };

/*----------------------------------------------------------------------
 * DER 書き込み (呼び出し側がバッファ長を保証する)
 */
typedef struct {
	UB	*buf;
	UW	len;
	UW	max;
	BOOL	overflow;
} T_DER;

LOCAL void der_bytes(T_DER *d, const void *src, UW n)
{
	if (d->len + n > d->max) { d->overflow = TRUE; return; }
	memcpy(d->buf + d->len, src, n);
	d->len += n;
}

LOCAL void der_byte(T_DER *d, UB b)
{
	der_bytes(d, &b, 1);
}

/* タグと長さ (短形式 / 0x81 / 0x82) */
LOCAL void der_header(T_DER *d, UB tag, UW len)
{
	der_byte(d, tag);
	if (len < 0x80) {
		der_byte(d, (UB)len);
	} else if (len < 0x100) {
		der_byte(d, 0x81);
		der_byte(d, (UB)len);
	} else {
		der_byte(d, 0x82);
		der_byte(d, (UB)(len >> 8));
		der_byte(d, (UB)len);
	}
}

LOCAL UW der_header_len(UW len)
{
	return (len < 0x80) ? 2 : (len < 0x100) ? 3 : 4;
}

/*
 * Name ::= SEQUENCE { SET { SEQUENCE { OID(CN), UTF8String } } }
 *   inner / set / outer は各階層の「中身」の長さ
 */
#define NAME_STR_LEN	(sizeof(CERT_CN) - 1)
#define NAME_INNER	(sizeof(OID_CN) + 2 + NAME_STR_LEN)
#define NAME_SET	(2 + NAME_INNER)
#define NAME_OUTER	(2 + NAME_SET)

LOCAL UW name_len(void)
{
	return 2 + NAME_OUTER;		/* 外側 SEQUENCE の TLV 全体 */
}

LOCAL void der_name(T_DER *d)
{
	der_header(d, 0x30, NAME_OUTER);
	der_header(d, 0x31, NAME_SET);
	der_header(d, 0x30, NAME_INNER);
	der_bytes(d, OID_CN, sizeof(OID_CN));
	der_header(d, 0x0C, NAME_STR_LEN);
	der_bytes(d, CERT_CN, NAME_STR_LEN);
}

/*----------------------------------------------------------------------
 * ECDSA 署名 (r||s) を ECDSA-Sig-Value の DER にする
 */
LOCAL UW der_integer_body(const UB *v, UW n, UB *out)
{
	UW i = 0, o = 0;

	while (i < n - 1 && v[i] == 0) i++;		/* 先頭の 0 を落とす */
	if (v[i] & 0x80) out[o++] = 0x00;		/* 負数にならないよう 0 を足す */
	while (i < n) out[o++] = v[i++];
	return o;
}

EXPORT UW u2f_der_signature(const UB sig[P256_SIG_LEN], UB *out)
{
	UB r[33], s[33];
	UW lr = der_integer_body(&sig[0], 32, r);
	UW ls = der_integer_body(&sig[32], 32, s);
	UW o = 0;

	out[o++] = 0x30;
	out[o++] = (UB)(2 + lr + 2 + ls);
	out[o++] = 0x02; out[o++] = (UB)lr;
	memcpy(&out[o], r, lr); o += lr;
	out[o++] = 0x02; out[o++] = (UB)ls;
	memcpy(&out[o], s, ls); o += ls;
	return o;
}

/*----------------------------------------------------------------------
 * 証明書の生成
 */
EXPORT ER u2f_build_cert(const UB priv[P256_KEY_LEN], const UB pub[P256_PUB_LEN],
			 UB *cert, UW max, UW *cert_len)
{
	UB tbs[320];
	UB serial[8];
	UB hash[32], sig[P256_SIG_LEN], sig_der[U2F_DER_SIG_MAX];
	UW sig_der_len, spki_alg, spki, validity, tbs_body;
	T_DER d;
	ER err;

	err = u2f_random(serial, sizeof(serial));
	if (err != E_OK) return err;
	/* 正の値で最上位バイトが 0 にならないようにし、DER の最短符号化を保つ */
	serial[0] = (UB)((serial[0] & 0x7F) | 0x40);

	spki_alg = sizeof(OID_EC_PUBKEY) + sizeof(OID_PRIME256V1);
	spki     = (2 + spki_alg) + (2 + 1 + 1 + P256_PUB_LEN);	/* BIT STRING 00 04 X Y */
	validity = 2 * (2 + 13);
	tbs_body = (2 + sizeof(serial))				/* INTEGER serial */
		 + (2 + sizeof(OID_ECDSA_SHA256))
		 + name_len()
		 + (2 + validity)
		 + name_len()
		 + (2 + spki);

	/* tbsCertificate */
	d.buf = tbs; d.len = 0; d.max = sizeof(tbs); d.overflow = FALSE;
	der_header(&d, 0x30, tbs_body);
	der_header(&d, 0x02, sizeof(serial));
	der_bytes(&d, serial, sizeof(serial));
	der_header(&d, 0x30, sizeof(OID_ECDSA_SHA256));
	der_bytes(&d, OID_ECDSA_SHA256, sizeof(OID_ECDSA_SHA256));
	der_name(&d);
	der_header(&d, 0x30, validity);
	der_header(&d, 0x17, 13);
	der_bytes(&d, NOT_BEFORE, 13);
	der_header(&d, 0x17, 13);
	der_bytes(&d, NOT_AFTER, 13);
	der_name(&d);
	der_header(&d, 0x30, spki);
	der_header(&d, 0x30, spki_alg);
	der_bytes(&d, OID_EC_PUBKEY, sizeof(OID_EC_PUBKEY));
	der_bytes(&d, OID_PRIME256V1, sizeof(OID_PRIME256V1));
	der_header(&d, 0x03, 2 + P256_PUB_LEN);
	der_byte(&d, 0x00);
	der_byte(&d, 0x04);
	der_bytes(&d, pub, P256_PUB_LEN);
	if (d.overflow || d.len != der_header_len(tbs_body) + tbs_body) return E_SYS;

	/* 自己署名 */
	u2f_sha256(tbs, d.len, hash);
	err = u2f_p256_sign(priv, hash, sig);
	if (err != E_OK) return err;
	sig_der_len = u2f_der_signature(sig, sig_der);

	/* Certificate */
	{
		UW tbs_len = d.len;
		UW alg = 2 + sizeof(OID_ECDSA_SHA256);
		UW bits = 2 + 1 + sig_der_len;
		UW body = tbs_len + alg + bits;
		T_DER c;

		c.buf = cert; c.len = 0; c.max = max; c.overflow = FALSE;
		der_header(&c, 0x30, body);
		der_bytes(&c, tbs, tbs_len);
		der_header(&c, 0x30, sizeof(OID_ECDSA_SHA256));
		der_bytes(&c, OID_ECDSA_SHA256, sizeof(OID_ECDSA_SHA256));
		der_header(&c, 0x03, 1 + sig_der_len);
		der_byte(&c, 0x00);
		der_bytes(&c, sig_der, sig_der_len);
		if (c.overflow) return E_LIMIT;
		*cert_len = c.len;
	}
	return E_OK;
}
