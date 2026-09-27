/*
 *----------------------------------------------------------------------
 *    I2C Master Device Driver for μT-Kernel 3.0 BSP2 (STM32H5)
 *
 *    mSDI デバイス "iica" (I2C1, PB8=SCL / PB9=SDA, 400kHz)
 *
 *    属性 TDN_I2C_EXEC で「送信 → リスタート → 受信」を 1 回で行う。
 *    start >= 0 の read/write は start をスレーブアドレスとして扱う。
 *
 *    割り込み駆動: ISR が TXDR/RXDR を処理し、完了・エラーで
 *    待ちタスクを起床する。
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef MTKBSP_CPU_STM32H5

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "../common/drvif/msdrvif.h"
#include <dev_i2c.h>

/*======================================================================
 *  ハードウェア定義
 *======================================================================*/

#define I2C1_BASE		0x40005400UL

#define I2C_CR1			(I2C1_BASE + 0x00)
#define I2C_CR2			(I2C1_BASE + 0x04)
#define I2C_TIMINGR		(I2C1_BASE + 0x10)
#define I2C_ISR			(I2C1_BASE + 0x18)
#define I2C_ICR			(I2C1_BASE + 0x1C)
#define I2C_RXDR		(I2C1_BASE + 0x24)
#define I2C_TXDR		(I2C1_BASE + 0x28)

#define I2C_CR1_PE		(1u << 0)
#define I2C_CR1_TXIE		(1u << 1)
#define I2C_CR1_RXIE		(1u << 2)
#define I2C_CR1_NACKIE		(1u << 4)
#define I2C_CR1_STOPIE		(1u << 5)
#define I2C_CR1_TCIE		(1u << 6)
#define I2C_CR1_ERRIE		(1u << 7)
#define I2C_CR1_INTS		(I2C_CR1_TXIE | I2C_CR1_RXIE | I2C_CR1_NACKIE | \
				 I2C_CR1_STOPIE | I2C_CR1_TCIE | I2C_CR1_ERRIE)

#define I2C_CR2_RD_WRN		(1u << 10)
#define I2C_CR2_START		(1u << 13)
#define I2C_CR2_AUTOEND		(1u << 25)

#define I2C_ISR_TXIS		(1u << 1)
#define I2C_ISR_RXNE		(1u << 2)
#define I2C_ISR_NACKF		(1u << 4)
#define I2C_ISR_STOPF		(1u << 5)
#define I2C_ISR_TC		(1u << 6)
#define I2C_ISR_BUSY		(1u << 15)

#define I2C_ICR_ALL		0x00003F38u

/*
 *  TIMINGR: I2CCLK = PCLK1 250MHz, Fast-mode 400kHz
 *    PRESC=6 (28ns), SCLDEL=15 (448ns), SDADEL=5 (140ns),
 *    SCLH=0x21 (0.95us), SCLL=0x2E (1.32us)
 */
#define I2C_TIMINGR_400K	0x60F5212Eu

/* CMSIS stm32h533xx.h: I2C1_EV_IRQn = 51, I2C1_ER_IRQn = 52 */
#define INTNO_I2C1_EV		51
#define INTNO_I2C1_ER		52
#define INTPRI_I2C1		5

#define I2C_TMO			100	/* ms */

/* RCC / GPIOB */
#define RCC_BASE_		0x44020C00UL
#define RCC_AHB2ENR_		(RCC_BASE_ + 0x08C)
#define RCC_APB1LENR_		(RCC_BASE_ + 0x09C)
#define I2C_RCC_GPIOBEN	(1u << 1)
#define I2C_RCC_I2C1EN	(1u << 21)

#define GPIOB_BASE		0x42020400UL
#define GPIOB_MODER		(GPIOB_BASE + 0x00)
#define GPIOB_OTYPER		(GPIOB_BASE + 0x04)
#define GPIOB_OSPEEDR		(GPIOB_BASE + 0x08)
#define GPIOB_PUPDR		(GPIOB_BASE + 0x0C)
#define GPIOB_AFRH		(GPIOB_BASE + 0x24)

#define I2C_PIN_SCL		8	/* PB8 */
#define I2C_PIN_SDA		9	/* PB9 */
#define I2C_AF			4	/* AF4 = I2C1 */

/*======================================================================
 *  通信制御
 *======================================================================*/

#define STS_START		0	/* タスクが START (またはリスタート) を出す */
#define STS_SEND		1
#define STS_RECV		2
#define STS_DONE		3	/* STOP 検出 (またはエラー) で完了 */

typedef struct {
	ID	wait_tskid;
	volatile UW	state;
	UW	sadr;
	volatile ER	ioerr;
	W	sdat_num;
	W	rdat_num;
	UB	*sbuf;
	UB	*rbuf;
} T_I2C_LLCB;

LOCAL T_I2C_LLCB	ll_cb;
LOCAL FastLock		i2c_lock;

LOCAL void i2c_wakeup(T_I2C_LLCB *p_cb)
{
	if (p_cb->wait_tskid) {
		tk_wup_tsk(p_cb->wait_tskid);
		p_cb->wait_tskid = 0;
	}
}

/*----------------------------------------------------------------------
 *  イベント割り込み
 *
 *  通信は必ず STOP で終わらせ (AUTOEND、NACK 時はハードが自動送出)、
 *  STOPF を見てからタスクを起こす。これで最終バイトの NACK も拾え、
 *  タスク側でバスの解放を待つ必要がない。
 */
LOCAL void i2c_evhdr(UINT intno)
{
	T_I2C_LLCB *p_cb = &ll_cb;
	UW st = in_w(I2C_ISR);
	BOOL wup = FALSE;

	if (st & I2C_ISR_NACKF) {
		p_cb->ioerr = E_IO;
	}

	if ((st & I2C_ISR_TXIS) && p_cb->sdat_num > 0) {
		out_w(I2C_TXDR, *p_cb->sbuf++);
		p_cb->sdat_num--;
	}

	if ((st & I2C_ISR_RXNE) && p_cb->rdat_num > 0) {
		*(p_cb->rbuf++) = (UB)in_w(I2C_RXDR);
		p_cb->rdat_num--;
	}

	if (st & I2C_ISR_TC) {
		/* 送信部の完了。TC は START を書くまで立ったままなので割り込みを止める */
		out_w(I2C_CR1, in_w(I2C_CR1) & ~I2C_CR1_TCIE);
		if (p_cb->ioerr == E_OK) {
			p_cb->state = STS_START;
			wup = TRUE;
		}
	}

	if (st & I2C_ISR_STOPF) {
		p_cb->state = STS_DONE;
		wup = TRUE;
	}

	/* 見た分だけクリアする (読んだ後に立ったフラグは次の割り込みで扱う) */
	out_w(I2C_ICR, st & I2C_ICR_ALL);
	ClearInt(intno);

	if (wup) i2c_wakeup(p_cb);
}

/*----------------------------------------------------------------------
 *  エラー割り込み (バスエラー / アービトレーション喪失 / オーバーラン)
 *    STOP が出る保証がないのでここで完了にする
 */
LOCAL void i2c_erhdr(UINT intno)
{
	T_I2C_LLCB *p_cb = &ll_cb;

	out_w(I2C_ICR, I2C_ICR_ALL);
	ClearInt(intno);

	p_cb->ioerr = E_IO;
	p_cb->state = STS_DONE;
	i2c_wakeup(p_cb);
}

/*----------------------------------------------------------------------
 *  1 回の通信 (送信 sdat_num → リスタート → 受信 rdat_num)
 */
LOCAL ER i2c_trans(T_I2C_LLCB *p_cb)
{
	UW ctl;
	UINT imask;
	ER err = E_OK;

	p_cb->ioerr = E_OK;
	p_cb->wait_tskid = 0;

	out_w(I2C_ICR, I2C_ICR_ALL);
	out_w(I2C_CR1, I2C_CR1_PE | I2C_CR1_INTS);

	while (1) {
		DI(imask);
		if (p_cb->state == STS_DONE) {
			EI(imask);
			break;
		}
		if (p_cb->state == STS_START) {
			ctl = (p_cb->sadr & 0x7F) << 1;
			if (p_cb->sdat_num > 0) {
				/* 受信が続く場合は AUTOEND を付けず TC でリスタートする */
				ctl |= (UW)p_cb->sdat_num << 16;
				if (p_cb->rdat_num == 0) ctl |= I2C_CR2_AUTOEND;
				p_cb->state = STS_SEND;
			} else {
				ctl |= ((UW)p_cb->rdat_num << 16) | I2C_CR2_RD_WRN | I2C_CR2_AUTOEND;
				p_cb->state = STS_RECV;
			}
			out_w(I2C_CR2, ctl);
			out_w(I2C_CR2, ctl | I2C_CR2_START);
			out_w(I2C_CR1, in_w(I2C_CR1) | I2C_CR1_TCIE);
		}
		p_cb->wait_tskid = tk_get_tid();
		EI(imask);

		/* ISR が先に起こしていれば起床要求が溜まっているので即戻る */
		err = tk_slp_tsk(I2C_TMO);
		if (err < E_OK) {
			p_cb->wait_tskid = 0;
			break;
		}
	}

	if (err == E_OK) err = p_cb->ioerr;

	out_w(I2C_CR1, 0);	/* PE=0 でペリフェラル状態をリセット */
	tk_can_wup(TSK_SELF);	/* 取り残した起床要求を捨てる */
	return err;
}

LOCAL ER i2c_exec(UW sadr, W snd_size, UB *snd, W rcv_size, UB *rcv)
{
	ER err;

	if (snd_size < 0 || rcv_size < 0 || snd_size > 255 || rcv_size > 255 ||
	    (snd_size == 0 && rcv_size == 0)) {
		return E_PAR;
	}

	Lock(&i2c_lock);
	ll_cb.state    = STS_START;
	ll_cb.sadr     = sadr;
	ll_cb.sdat_num = snd_size;
	ll_cb.rdat_num = rcv_size;
	ll_cb.sbuf     = snd;
	ll_cb.rbuf     = rcv;
	err = i2c_trans(&ll_cb);
	Unlock(&i2c_lock);

	return err;
}

/*======================================================================
 *  mSDI ドライバ
 *======================================================================*/

LOCAL ER i2c_openfn(ID devid, UINT omode, T_MSDI *p_msdi)
{
	(void)devid; (void)omode; (void)p_msdi;
	return E_OK;
}

LOCAL ER i2c_closefn(ID devid, UINT omode, T_MSDI *p_msdi)
{
	(void)devid; (void)omode; (void)p_msdi;
	return E_OK;
}

LOCAL INT i2c_readfn(T_DEVREQ *req, T_MSDI *p_msdi)
{
	ER err;

	(void)p_msdi;
	if (req->start < 0) return E_PAR;
	err = i2c_exec((UW)req->start, 0, NULL, (W)req->size, (UB *)req->buf);
	req->asize = (err == E_OK) ? req->size : 0;
	return err;
}

LOCAL INT i2c_writefn(T_DEVREQ *req, T_MSDI *p_msdi)
{
	T_I2C_EXEC *ex;
	ER err;

	(void)p_msdi;
	if (req->start == TDN_I2C_EXEC) {
		if (req->size < (W)sizeof(T_I2C_EXEC)) return E_PAR;
		ex = (T_I2C_EXEC *)req->buf;
		err = i2c_exec(ex->sadr, (W)ex->snd_size, ex->snd_data,
			       (W)ex->rcv_size, ex->rcv_data);
		req->asize = (err == E_OK) ? sizeof(T_I2C_EXEC) : 0;
		return err;
	}
	if (req->start < 0) return E_PAR;
	err = i2c_exec((UW)req->start, (W)req->size, (UB *)req->buf, 0, NULL);
	req->asize = (err == E_OK) ? req->size : 0;
	return err;
}

LOCAL INT i2c_eventfn(INT evttyp, void *evtinf, T_MSDI *p_msdi)
{
	(void)evttyp; (void)evtinf; (void)p_msdi;
	return E_NOSPT;
}

/*----------------------------------------------------------------------
 *  GPIO: PB8/PB9 を AF4 オープンドレイン + 内部プルアップ
 *  (内部プルアップは 40kΩ 程度と弱いので外付け 4.7kΩ を推奨)
 */
LOCAL void i2c_gpio_init(void)
{
	UW v;
	INT pin;

	out_w(RCC_AHB2ENR_, in_w(RCC_AHB2ENR_) | I2C_RCC_GPIOBEN);
	(void)in_w(RCC_AHB2ENR_);

	for (pin = I2C_PIN_SCL; pin <= I2C_PIN_SDA; pin++) {
		v = in_w(GPIOB_AFRH);
		v &= ~(0xFu << ((pin - 8) * 4));
		v |= (UW)I2C_AF << ((pin - 8) * 4);
		out_w(GPIOB_AFRH, v);

		out_w(GPIOB_OTYPER, in_w(GPIOB_OTYPER) | (1u << pin));
		out_w(GPIOB_OSPEEDR, (in_w(GPIOB_OSPEEDR) & ~(3u << (pin * 2))) | (2u << (pin * 2)));
		out_w(GPIOB_PUPDR, (in_w(GPIOB_PUPDR) & ~(3u << (pin * 2))) | (1u << (pin * 2)));
		out_w(GPIOB_MODER, (in_w(GPIOB_MODER) & ~(3u << (pin * 2))) | (2u << (pin * 2)));
	}
}

/*----------------------------------------------------------------------
 *  デバイス初期化・登録 ("iica" = I2C1)
 */
EXPORT ER dev_init_i2c(UW unit)
{
	T_DMSDI dmsdi;
	T_IDEV idev;
	T_MSDI *p_msdi;
	T_DINT dint;
	ER err;

	if (unit != 0) return E_PAR;

	err = CreateLock(&i2c_lock, (UB *)"i2cm");
	if (err < E_OK) return err;

	i2c_gpio_init();

	/* I2C1 クロック (カーネルクロックは既定の PCLK1) */
	out_w(RCC_APB1LENR_, in_w(RCC_APB1LENR_) | I2C_RCC_I2C1EN);
	(void)in_w(RCC_APB1LENR_);

	out_w(I2C_CR1, 0);
	out_w(I2C_TIMINGR, I2C_TIMINGR_400K);

	dint.intatr = TA_HLNG;
	dint.inthdr = i2c_evhdr;
	err = tk_def_int(INTNO_I2C1_EV, &dint);
	if (err < E_OK) return err;
	dint.inthdr = i2c_erhdr;
	err = tk_def_int(INTNO_I2C1_ER, &dint);
	if (err < E_OK) return err;
	EnableInt(INTNO_I2C1_EV, INTPRI_I2C1);
	EnableInt(INTNO_I2C1_ER, INTPRI_I2C1);

	memset(&dmsdi, 0, sizeof(dmsdi));
	strcpy((char *)dmsdi.devnm, "iica");
	dmsdi.exinf   = NULL;
	dmsdi.drvatr  = 0;
	dmsdi.devatr  = TDK_UNDEF;
	dmsdi.nsub    = 0;
	dmsdi.blksz   = 1;
	dmsdi.openfn  = i2c_openfn;
	dmsdi.closefn = i2c_closefn;
	dmsdi.readfn  = i2c_readfn;
	dmsdi.writefn = i2c_writefn;
	dmsdi.eventfn = i2c_eventfn;

	err = msdi_def_dev(&dmsdi, &idev, &p_msdi);
	if (err != E_OK) return err;

	tm_printf((UB *)"I2C1 master ready (PB8=SCL, PB9=SDA, 400kHz)\n");
	return E_OK;
}

#endif /* MTKBSP_CPU_STM32H5 */
