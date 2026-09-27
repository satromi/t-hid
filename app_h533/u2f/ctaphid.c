/*
 * ctaphid.c — FIDO CTAPHID トランスポート (U2F 用)
 *
 * FIDO Client to Authenticator Protocol, "USB HID" 節の CTAPHID に従う。
 *
 *   初期パケット:  CID(4) | CMD(1, bit7=1) | BCNTH | BCNTL | DATA(57)
 *   継続パケット:  CID(4) | SEQ(1, 0..0x7F) | DATA(59)
 *
 * 組み立て中のメッセージは 1 つだけ持つ。その間に別チャネルから
 * 初期パケットが来たら CHANNEL_BUSY を返す (INIT は常に受け付ける)。
 */

#include <sys/machine.h>
#include <stddef.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>

#include "ctaphid.h"
#include "u2f.h"

#define INIT_DATA	(CTAPHID_PKT_SIZE - 7)	/* 57 */
#define CONT_DATA	(CTAPHID_PKT_SIZE - 5)	/* 59 */

LOCAL T_CTAPHID_IF ifc;

LOCAL struct {
	BOOL	busy;		/* 組み立て中 */
	UW	cid;
	UB	cmd;
	UW	bcnt;		/* 全長 */
	UW	got;		/* 受信済み */
	UB	seq;		/* 次に期待する SEQ */
	UW	deadline;
	UB	buf[U2F_APDU_MAX];
} rx;

LOCAL UB resp_buf[U2F_APDU_MAX];
LOCAL UW next_cid;

/*======================================================================
 * 送信
 *====================================================================*/

LOCAL UW get_cid(const UB *p)
{
	return ((UW)p[0] << 24) | ((UW)p[1] << 16) | ((UW)p[2] << 8) | p[3];
}

LOCAL void put_cid(UB *p, UW cid)
{
	p[0] = (UB)(cid >> 24);
	p[1] = (UB)(cid >> 16);
	p[2] = (UB)(cid >> 8);
	p[3] = (UB)cid;
}

LOCAL void send_msg(UW cid, UB cmd, const UB *data, UW len)
{
	UB pkt[CTAPHID_PKT_SIZE];
	UW off, n;
	UB seq = 0;

	memset(pkt, 0, sizeof(pkt));
	put_cid(pkt, cid);
	pkt[4] = 0x80 | cmd;
	pkt[5] = (UB)(len >> 8);
	pkt[6] = (UB)len;
	n = (len < INIT_DATA) ? len : INIT_DATA;
	if (n > 0) memcpy(&pkt[7], data, n);
	if (ifc.send(pkt) != E_OK) return;

	for (off = n; off < len; off += n) {
		memset(pkt, 0, sizeof(pkt));
		put_cid(pkt, cid);
		pkt[4] = seq++;
		n = len - off;
		if (n > CONT_DATA) n = CONT_DATA;
		memcpy(&pkt[5], &data[off], n);
		if (ifc.send(pkt) != E_OK) return;
	}
}

LOCAL void send_error(UW cid, UB code)
{
	send_msg(cid, CTAPHID_ERROR, &code, 1);
}

/*======================================================================
 * メッセージ処理
 *====================================================================*/

LOCAL UW alloc_cid(void)
{
	UW cid;
	do {
		cid = ++next_cid;
	} while (cid == 0 || cid == CTAPHID_BROADCAST_CID);
	return cid;
}

LOCAL void handle_init(UW cid, const UB *data, UW len)
{
	UB r[17];
	UW new_cid;

	if (len != 8) {
		send_error(cid, CTAPHID_ERR_INVALID_LEN);
		return;
	}
	/* ブロードキャストなら新しいチャネルを割り当て、既存チャネルなら同じ ID で再同期 */
	new_cid = (cid == CTAPHID_BROADCAST_CID) ? alloc_cid() : cid;

	memcpy(&r[0], data, 8);		/* nonce をそのまま返す */
	put_cid(&r[8], new_cid);
	r[12] = 2;			/* CTAPHID プロトコル版 */
	r[13] = 1;			/* デバイス版 major */
	r[14] = 0;			/* minor */
	r[15] = 0;			/* build */
	r[16] = CTAPHID_CAPFLAG_WINK;	/* CBOR なし (U2F のみ) */
	send_msg(cid, CTAPHID_INIT, r, sizeof(r));
}

LOCAL void dispatch(UW cid, UB cmd, const UB *data, UW len)
{
	UW n;

	switch (cmd) {
	case CTAPHID_MSG:
		n = u2f_process_apdu(data, len, resp_buf, sizeof(resp_buf), ifc.user_present);
		send_msg(cid, CTAPHID_MSG, resp_buf, n);
		break;
	case CTAPHID_PING:
		send_msg(cid, CTAPHID_PING, data, len);
		break;
	case CTAPHID_WINK:
		if (ifc.wink != NULL) ifc.wink();
		send_msg(cid, CTAPHID_WINK, NULL, 0);
		break;
	default:
		send_error(cid, CTAPHID_ERR_INVALID_CMD);
		break;
	}
}

/*======================================================================
 * 公開 API
 *====================================================================*/

EXPORT void ctaphid_init(const T_CTAPHID_IF *iface)
{
	UB r[4];

	ifc = *iface;
	memset(&rx, 0, sizeof(rx));

	/* チャネル ID を起動ごとに変える (前回の ID を使い続けるホストを弾く) */
	if (ifc.random != NULL && ifc.random(r, sizeof(r)) == E_OK) {
		next_cid = get_cid(r) & 0x7FFFFFFFUL;
	} else {
		next_cid = 0x00010000UL;
	}
}

EXPORT void ctaphid_recv(const UB pkt[CTAPHID_PKT_SIZE], UW now_ms)
{
	UW cid = get_cid(pkt);
	UB b4 = pkt[4];

	if (cid == 0) {
		send_error(cid, CTAPHID_ERR_INVALID_CHANNEL);
		return;
	}

	if (b4 & 0x80) {
		/* 初期パケット */
		UB cmd = b4 & 0x7F;
		UW bcnt = ((UW)pkt[5] << 8) | pkt[6];

		if (cmd == CTAPHID_INIT) {
			if (rx.busy && rx.cid == cid) rx.busy = FALSE;	/* 再同期 */
			if (bcnt > INIT_DATA) {
				send_error(cid, CTAPHID_ERR_INVALID_LEN);
			} else {
				handle_init(cid, &pkt[7], bcnt);
			}
			return;
		}
		if (cid == CTAPHID_BROADCAST_CID) {
			send_error(cid, CTAPHID_ERR_INVALID_CHANNEL);
			return;
		}
		if (rx.busy && rx.cid != cid) {
			send_error(cid, CTAPHID_ERR_CHANNEL_BUSY);
			return;
		}
		if (bcnt > sizeof(rx.buf)) {
			rx.busy = FALSE;
			send_error(cid, CTAPHID_ERR_INVALID_LEN);
			return;
		}

		rx.cid  = cid;
		rx.cmd  = cmd;
		rx.bcnt = bcnt;
		rx.got  = (bcnt < INIT_DATA) ? bcnt : INIT_DATA;
		rx.seq  = 0;
		memcpy(rx.buf, &pkt[7], rx.got);

		if (rx.got >= rx.bcnt) {
			rx.busy = FALSE;
			dispatch(cid, cmd, rx.buf, rx.bcnt);
		} else {
			rx.busy = TRUE;
			rx.deadline = now_ms + CTAPHID_MSG_TIMEOUT_MS;
		}
		return;
	}

	/* 継続パケット */
	if (!rx.busy || rx.cid != cid) return;	/* 対応する初期パケットがない: 無視 */
	if (b4 != rx.seq) {
		rx.busy = FALSE;
		send_error(cid, CTAPHID_ERR_INVALID_SEQ);
		return;
	}
	{
		UW n = rx.bcnt - rx.got;
		if (n > CONT_DATA) n = CONT_DATA;
		memcpy(&rx.buf[rx.got], &pkt[5], n);
		rx.got += n;
		rx.seq++;
		rx.deadline = now_ms + CTAPHID_MSG_TIMEOUT_MS;
	}
	if (rx.got >= rx.bcnt) {
		rx.busy = FALSE;
		dispatch(cid, rx.cmd, rx.buf, rx.bcnt);
	}
}

EXPORT void ctaphid_poll(UW now_ms)
{
	if (rx.busy && (W)(now_ms - rx.deadline) > 0) {
		rx.busy = FALSE;
		send_error(rx.cid, CTAPHID_ERR_MSG_TIMEOUT);
	}
}
