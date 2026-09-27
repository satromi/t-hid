/*
 * u2f_secure_api.h — TrustZone のセキュア側が提供する U2F の呼び出し口
 *
 *   鍵・署名・ハッシュ・鍵の保存はセキュア側にあり、非セキュア側はこの関数だけを
 *   呼ぶ。呼んでよいのはセキュアコール可能属性 (TA_TZCALL) の U2F タスクだけで、
 *   セキュア側の実行中もタスクは切り替わる (μT-Kernel 3.0 セキュア機能拡張の
 *   標準実行モデル。tz_kernel_api.h)。セキュア側の処理は再入しないので、
 *   複数のタスクから呼んではならない。
 *
 *   セキュア側では関数に cmse_nonsecure_entry を付け、非セキュア側から渡された
 *   ポインタは非セキュアの領域であることを確かめてから使う。
 */
#ifndef __U2F_SECURE_API_H__
#define __U2F_SECURE_API_H__

#include <stdint.h>

/*
 * 本人確認 (B1)
 *   B1 (PC13) はセキュア側専用のピンで、押されたかどうかはセキュア側が自分で読む。
 *   非セキュア側は u2fs_button() を定期的 (20ms ごと) に呼んで読み取りの機会を渡すだけで、
 *   押下を偽ることはできない。
 */

/* u2fs_apdu の *flags */
#define U2FS_PRESENCE_USED	(1u << 0)	/* 押下を 1 回分使った */
#define U2FS_PRESENCE_MISSING	(1u << 1)	/* 押下が必要だったが無かった */

typedef struct {
	int32_t		ready;
	int32_t		uses_pka;
	int32_t		uses_hash;
	int32_t		uses_saes;
	uint32_t	counter;
	uint32_t	registrations;
	uint32_t	authentications;
} T_U2FS_STATUS;

/* 鍵とカウンタを消す。u2fs_init() より前で、B1 が押されているときだけ受け付ける */
int32_t  u2fs_erase(void);

/* B1 を読み、押し始めを記録する。戻り値は今押されているか (1/0) */
int32_t  u2fs_button(void);

/* 自己診断、鍵の読み込み (なければ生成)、鍵セクタの隠蔽。1 回だけ受け付ける */
int32_t  u2fs_init(void);

/*
 * U2F の APDU を処理し、応答の長さを返す
 *   呼び出し口の引数はレジスタで渡せる 4 個までなので、構造体にまとめて渡す
 */
typedef struct {
	const uint8_t	*req;
	uint32_t	req_len;
	uint8_t		*resp;
	uint32_t	resp_max;
	uint32_t	reserved;	/* 0 */
	uint32_t	flags;		/* 戻り: U2FS_PRESENCE_USED / U2FS_PRESENCE_MISSING */
} T_U2FS_APDU;

uint32_t u2fs_apdu(T_U2FS_APDU *a);

/* 乱数 (CTAPHID のチャネル ID 用、1 回 64 バイトまで) */
int32_t  u2fs_random(uint8_t *buf, uint32_t len);

void     u2fs_status(T_U2FS_STATUS *st);

/* セキュア側のログを取り出す。戻り値は書き込んだバイト数 (NUL を含まない) */
uint32_t u2fs_log_read(char *buf, uint32_t max);

#endif /* __U2F_SECURE_API_H__ */
