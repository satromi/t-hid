/*
 * u2f.h — FIDO U2F (CTAP1) 認証器
 *
 *   USB: キーボードと複合の HID インタフェース (usage page 0xF1D0)
 *   ユーザー確認: NUCLEO の B1 (USER) ボタン
 *   鍵: キーハンドル = nonce(32) || HMAC(mac_key, appid || nonce)
 *       秘密鍵      = HMAC(priv_key, appid || nonce)  (1 <= d < n になるまで nonce を引き直す)
 *       mac_key / priv_key はデバイス固有の master から導出し、Flash に保存するのは master のみ
 */

#ifndef __U2F_H__
#define __U2F_H__

#include <tk/tkernel.h>
#include "u2f_crypto.h"

#define U2F_KH_LEN		64
#define U2F_DER_SIG_MAX		72
#define U2F_APDU_MAX		1024	/* CTAPHID で受ける最大メッセージ長 */

/* ステータスワード */
#define U2F_SW_NO_ERROR			0x9000
#define U2F_SW_CONDITIONS_NOT_SATISFIED	0x6985
#define U2F_SW_WRONG_DATA		0x6A80
#define U2F_SW_WRONG_LENGTH		0x6700
#define U2F_SW_CLA_NOT_SUPPORTED	0x6E00
#define U2F_SW_INS_NOT_SUPPORTED	0x6D00

/* 鍵の読み込み (無ければ生成して保存) */
ER   u2f_init(void);

/*
 * APDU を処理して応答 (データ + SW) を resp に書き、その長さを返す。
 * user_present は利用者の確認が必要な時点で 1 回呼ばれ、TRUE なら確認済み。
 */
UW   u2f_process_apdu(const UB *req, UW req_len, UB *resp, UW resp_max,
		      BOOL (*user_present)(void));

/* アテステーション証明書の生成 (u2f_cert.c) */
ER   u2f_build_cert(const UB priv[P256_KEY_LEN], const UB pub[P256_PUB_LEN],
		    UB *cert, UW max, UW *cert_len);

/* r||s を ECDSA-Sig-Value (DER) にする。戻り値は長さ */
UW   u2f_der_signature(const UB sig[P256_SIG_LEN], UB *out);

/* 状態 (MCP 用) */
typedef struct {
	BOOL	ready;
	BOOL	uses_pka;
	BOOL	uses_hash;
	BOOL	uses_saes;
	UW	counter;
	UW	registrations;		/* 起動後の登録回数 */
	UW	authentications;	/* 起動後の認証回数 */
} T_U2F_STATUS;

void u2f_get_status(T_U2F_STATUS *st);

/* 秘密の値を持つアドレスか (MCP の peek/poke 対策) */
BOOL u2f_addr_is_secret(UW addr);

#endif /* __U2F_H__ */
