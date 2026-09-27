/*
 * ctaphid.h — FIDO CTAPHID トランスポート (U2F 用)
 *
 * 64 バイトの HID レポートをメッセージに組み立て、U2F APDU を処理して
 * 応答をパケットに分けて返す。USB やタスクには依存しない。
 */

#ifndef __CTAPHID_H__
#define __CTAPHID_H__

#include <tk/tkernel.h>

#define CTAPHID_PKT_SIZE	64
#define CTAPHID_BROADCAST_CID	0xFFFFFFFFUL

#define CTAPHID_PING		0x01
#define CTAPHID_MSG		0x03
#define CTAPHID_INIT		0x06
#define CTAPHID_WINK		0x08
#define CTAPHID_ERROR		0x3F

#define CTAPHID_ERR_INVALID_CMD		0x01
#define CTAPHID_ERR_INVALID_PAR		0x02
#define CTAPHID_ERR_INVALID_LEN		0x03
#define CTAPHID_ERR_INVALID_SEQ		0x04
#define CTAPHID_ERR_MSG_TIMEOUT		0x05
#define CTAPHID_ERR_CHANNEL_BUSY	0x06
#define CTAPHID_ERR_INVALID_CHANNEL	0x0B

#define CTAPHID_CAPFLAG_WINK	0x01

#define CTAPHID_MSG_TIMEOUT_MS	500	/* 継続パケットの待ち時間 */

typedef struct {
	/* 1 パケット送信 */
	ER   (*send)(const UB pkt[CTAPHID_PKT_SIZE]);
	/* 利用者の確認 (U2F が必要とした時点で呼ぶ) */
	BOOL (*user_present)(void);
	/* WINK 要求 (NULL 可) */
	void (*wink)(void);
	/* 新しいチャネル ID の元になる乱数 */
	ER   (*random)(UB *buf, UW len);
} T_CTAPHID_IF;

void ctaphid_init(const T_CTAPHID_IF *iface);

/* 受信パケットを処理する (完成したメッセージはこの中で処理して応答する) */
void ctaphid_recv(const UB pkt[CTAPHID_PKT_SIZE], UW now_ms);

/* 定期的に呼ぶ: 組み立て途中のメッセージのタイムアウト */
void ctaphid_poll(UW now_ms);

#endif /* __CTAPHID_H__ */
