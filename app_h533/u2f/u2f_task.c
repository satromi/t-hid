/*
 * u2f_task.c — U2F 認証器タスク (USB FIDO インタフェースと B1 ボタン)
 *
 *   USB 処理タスク → (受信コールバック) → メッセージバッファ → U2F タスク
 *   U2F タスクが CTAPHID/U2F を処理し、応答を EP2 IN で返す。
 *
 * 利用者の確認は NUCLEO の B1 (USER, PC13, 押すと High)。
 * 押し始めてから U2F_PRESENCE_MS 以内の要求 1 回を確認済みとして扱う (押し続けても 1 回分)。
 * 確認が要る要求はすぐ 6985 を返し、ブラウザ側が繰り返し問い合わせる。
 */

#include <sys/machine.h>
#include <stddef.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

#include "../../device/usb_hid/usb_hid.h"
#include "u2f.h"
#include "u2f_task.h"
#include "u2f_store.h"
#include "ctaphid.h"

#define U2F_TASK_PRI		10	/* キースキャン (8) より低い */
#define U2F_TASK_STKSZ		8192	/* micro-ecc / HMAC / 応答組み立て */
#define U2F_TASK_SEC_STKSZ	(12 * 1024)	/* TrustZone: セキュア側で U2F を処理するときのスタック */
#define U2F_MBF_PKTS		8
#define U2F_POLL_MS		20	/* ボタンの見回り間隔 */
#define U2F_PRESENCE_MS		3000

/* B1 (PC13) */
#define U2F_RCC_AHB2ENR		(0x44020C00UL + 0x08C)
#define U2F_RCC_GPIOCEN	(1u << 2)
#define GPIOC_BASE		0x42020800UL
#define GPIOC_MODER		(GPIOC_BASE + 0x00)
#define GPIOC_PUPDR		(GPIOC_BASE + 0x0C)
#define GPIOC_IDR		(GPIOC_BASE + 0x10)
#define B1_PIN			13

LOCAL ID  u2f_mbfid;
LOCAL UW  presence_until;	/* 0 = 確認なし */
LOCAL UW  prompt_until;		/* 「B1 を押して」の表示間隔 */

LOCAL UW now_ms(void)
{
	SYSTIM tim;
	tk_get_otm(&tim);
	return (UW)tim.lo;
}

/*----------------------------------------------------------------------
 * B1 ボタン
 */
#if defined(TZ_NONSECURE)
/*
 * TrustZone 構成: B1 はセキュア側専用のピン。読み取りと押し始めの判定はセキュア側が
 * 行い、ここは 20ms ごとに読み取りの機会を渡すだけ。押下の承認もセキュア側が決める。
 */
IMPORT BOOL u2f_sec_button(void);
LOCAL void b1_init(void) {}
LOCAL BOOL b1_pressed(void) { return u2f_sec_button(); }
#else
LOCAL void b1_init(void)
{
	out_w(U2F_RCC_AHB2ENR, in_w(U2F_RCC_AHB2ENR) | U2F_RCC_GPIOCEN);
	(void)in_w(U2F_RCC_AHB2ENR);
	out_w(GPIOC_MODER, in_w(GPIOC_MODER) & ~(3u << (B1_PIN * 2)));	/* 入力 */
	out_w(GPIOC_PUPDR, (in_w(GPIOC_PUPDR) & ~(3u << (B1_PIN * 2))) | (2u << (B1_PIN * 2)));
}

LOCAL BOOL b1_pressed(void)
{
	return (in_w(GPIOC_IDR) & (1u << B1_PIN)) ? TRUE : FALSE;
}
#endif

/*
 * 押し始め (離した状態から押した状態への変化) だけを本人確認として数える。
 * 押し続けても 1 回分にしかならないので、1 回の押下で複数の操作は承認されない。
 */
LOCAL void b1_poll(void)
{
	static BOOL was_pressed = TRUE;	/* 起動時に押されていても数えない */
	BOOL pressed = b1_pressed();

	if (pressed && !was_pressed) {
		presence_until = now_ms() + U2F_PRESENCE_MS;
		if (presence_until == 0) presence_until = 1;
	}
	was_pressed = pressed;
}

/* ボタンが押されて有効期間内か (消費しない) */
EXPORT BOOL u2f_presence_available(void)
{
	b1_poll();
	return (presence_until != 0 && (W)(presence_until - now_ms()) > 0) ? TRUE : FALSE;
}

/* 押下を 1 回分使った */
EXPORT void u2f_presence_used(void)
{
	presence_until = 0;
}

/* 押下が必要だったが無かった。5 秒に 1 回、押すよう表示する */
EXPORT void u2f_presence_missing(void)
{
	UW now = now_ms();

	presence_until = 0;
	if ((W)(now - prompt_until) >= 0) {
		tm_printf((UB *)"U2F: press B1 (USER button) to confirm\n");
		prompt_until = now + 5000;
	}
}

/* ctaphid → u2f から呼ばれる。確認済みなら 1 回分を消費して TRUE */
LOCAL BOOL user_present(void)
{
	if (u2f_presence_available()) {
		u2f_presence_used();
		return TRUE;
	}
	u2f_presence_missing();
	return FALSE;
}

LOCAL void wink(void)
{
	tm_printf((UB *)"U2F: wink\n");
}

/*
 * 起動時に B1 を U2F_RESET_HOLD_MS 押し続けていれば鍵とカウンタを消す
 * (登録済みのサイトは全て使えなくなる)。本体に触れられる人だけが行える。
 */
#define U2F_RESET_HOLD_MS	5000

LOCAL void factory_reset_if_requested(void)
{
	UW start = now_ms();

	if (!b1_pressed()) return;
	tm_printf((UB *)"U2F: keep B1 pressed for 5 s to erase all U2F keys\n");
	while ((W)(now_ms() - start) < U2F_RESET_HOLD_MS) {
		if (!b1_pressed()) {
			tm_printf((UB *)"U2F: reset cancelled\n");
			return;
		}
		tk_dly_tsk(50);
	}
	tm_printf((UB *)"U2F: erasing keys (%s)\n",
		  (u2f_store_erase_all() == E_OK) ? "ok" : "failed");
	while (b1_pressed()) tk_dly_tsk(50);	/* 押しっぱなしを確認扱いにしない */
}

/*----------------------------------------------------------------------
 * USB とのつなぎ
 */
LOCAL ER send_pkt(const UB pkt[CTAPHID_PKT_SIZE])
{
	return usb_fido_send(pkt, 100);
}

/* USB 処理タスクの文脈で呼ばれる。溢れたら捨てる (ホストが再送する) */
LOCAL void on_usb_packet(const UB *pkt)
{
	if (u2f_mbfid > 0) tk_snd_mbf(u2f_mbfid, (void *)pkt, CTAPHID_PKT_SIZE, TMO_POL);
}

LOCAL void u2f_task(INT stacd, void *exinf)
{
	UB pkt[CTAPHID_PKT_SIZE];
	T_CTAPHID_IF ifc;
	ER err;

	(void)stacd; (void)exinf;

	factory_reset_if_requested();

	/* 鍵生成は初回だけ数百 ms かかることがあるのでこのタスクで行う */
	err = u2f_crypto_init();
	if (err == E_OK) err = u2f_init();
	u2f_store_hide();
	if (err != E_OK) {
		tm_printf((UB *)"U2F: disabled (%d)\n", err);
		tk_ext_tsk();
	}

	ifc.send         = send_pkt;
	ifc.user_present = user_present;
	ifc.wink         = wink;
	ifc.random       = u2f_random;
	ctaphid_init(&ifc);
	usb_fido_set_recv_callback(on_usb_packet);

	while (1) {
		INT n = tk_rcv_mbf(u2f_mbfid, pkt, U2F_POLL_MS);
		b1_poll();
		if (n == CTAPHID_PKT_SIZE) ctaphid_recv(pkt, now_ms());
		ctaphid_poll(now_ms());
	}
}

EXPORT ER u2f_start(void)
{
	T_CMBF cmbf;
	T_CTSK ctsk;
	ID id;

	b1_init();

	cmbf.exinf  = NULL;
	cmbf.mbfatr = TA_TFIFO;
	cmbf.bufsz  = (CTAPHID_PKT_SIZE + 4) * U2F_MBF_PKTS;
	cmbf.maxmsz = CTAPHID_PKT_SIZE;
	cmbf.bufptr = NULL;
	u2f_mbfid = tk_cre_mbf(&cmbf);
	if (u2f_mbfid < E_OK) return u2f_mbfid;

	ctsk.exinf   = NULL;
	ctsk.tskatr  = TA_HLNG | TA_RNG3;
	ctsk.task    = (FP)u2f_task;
	ctsk.itskpri = U2F_TASK_PRI;
	ctsk.stksz   = U2F_TASK_STKSZ;
	ctsk.bufptr  = NULL;
#if defined(TZ_NONSECURE)
	/* セキュア側を呼ぶのはこのタスクだけ。セキュア側に専用のスタックを持たせる */
	ctsk.tskatr |= TA_TZCALL;
	ctsk.tzstksz = U2F_TASK_SEC_STKSZ;
#endif
	id = tk_cre_tsk(&ctsk);
	if (id < E_OK) return id;
	return tk_sta_tsk(id, 0);
}

/*----------------------------------------------------------------------
 * MCP の peek/poke/shell reg から U2F の秘密を守る
 * (device/wiznet/tk_mcp_tools.c の weak 定義を上書き)
 */
EXPORT BOOL mcp_addr_is_protected(UW addr)
{
	return u2f_addr_is_secret(addr);
}
