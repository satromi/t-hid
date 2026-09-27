/*
 * u2f_store.h — U2F の鍵・証明書・カウンタの Flash 保存
 */

#ifndef __U2F_STORE_H__
#define __U2F_STORE_H__

#include <tk/tkernel.h>
#include "u2f_crypto.h"

#define U2F_CERT_MAX	512

/*
 * 初回起動時に生成し、以後変えない値
 *   master   : キーハンドルから秘密鍵と MAC 鍵を導出する元
 *   att_*    : アテステーション鍵と自己署名証明書
 */
typedef struct {
	UW	magic;
	UW	cert_len;
	UB	master[32];
	UB	att_priv[P256_KEY_LEN];
	UB	att_pub[P256_PUB_LEN];
	UB	cert[U2F_CERT_MAX];
	UB	check[32];		/* ここまでの SHA-256 (平文に対して) */
	UB	nonce[8];		/* SAES で暗号化したときの CTR の nonce */
} T_U2F_KEYS;

/* 保存済みの鍵を読む (E_NOEXS: 未生成, E_IO: 破損) */
ER   u2f_store_load(T_U2F_KEYS *keys);

/* 鍵を保存 (check は内部で計算) */
ER   u2f_store_save(T_U2F_KEYS *keys);
BOOL u2f_store_is_sealed(const T_U2F_KEYS *keys);
void u2f_store_hide(void);

/* カウンタ: 現在値 / 1 増やして保存し新しい値を返す */
UW   u2f_counter_get(void);
ER   u2f_counter_next(UW *value);

/* 鍵とカウンタを全て消去 (次回起動で再生成、既存の登録は全て無効) */
ER   u2f_store_erase_all(void);

#endif /* __U2F_STORE_H__ */
