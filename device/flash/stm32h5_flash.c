/*
 * stm32h5_flash.c — STM32H533 内蔵 Flash の消去・書き込み
 *
 * RM0481 "Embedded flash memory (FLASH)"。レジスタは CMSIS stm32h533xx.h で確認。
 *
 * 書き込み中のバンクから命令を読むと CPU は完了まで待たされるが、
 * 例外にはならない。コードは両バンクにまたがるため、消去・書き込み中は
 * 割り込み禁止にして待ち時間を読めるものにしておく。
 */

#include <sys/machine.h>
#ifdef MTKBSP_CPU_STM32H5

#include <tk/tkernel.h>
#include <tk/syslib.h>
#include "stm32h5_flash.h"

/*
 * セキュア側イメージ (U2F_SECURE) はセキュア用のレジスタでセキュアなセクタを
 * 書き換える。名前は非セキュア用のまま、指すレジスタを切り替える。
 */
#if defined(U2F_SECURE)
#define FLASH_R_BASE		0x50022000UL
#define FLASH_NSKEYR		(FLASH_R_BASE + 0x08)	/* SECKEYR */
#define FLASH_NSSR		(FLASH_R_BASE + 0x24)	/* SECSR */
#define FLASH_NSCR		(FLASH_R_BASE + 0x2C)	/* SECCR */
#define FLASH_NSCCR		(FLASH_R_BASE + 0x34)	/* SECCCR */
#else
#define FLASH_R_BASE		0x40022000UL
#define FLASH_NSKEYR		(FLASH_R_BASE + 0x04)
#define FLASH_NSSR		(FLASH_R_BASE + 0x20)
#define FLASH_NSCR		(FLASH_R_BASE + 0x28)
#define FLASH_NSCCR		(FLASH_R_BASE + 0x30)
#endif

#define CR_LOCK			(1u << 0)
#define CR_PG			(1u << 1)
#define CR_SER			(1u << 2)
#define CR_STRT			(1u << 5)
#define CR_SNB_SHIFT		6
#define CR_BKSEL		(1u << 31)

#define SR_BSY			(1u << 0)
#define SR_WBNE			(1u << 1)
#define SR_DBNE			(1u << 3)
#define SR_ERR_MASK		(0x7Fu << 17)	/* WRPERR..OPTCHANGEERR */
#define CCR_ALL			(0xFFu << 16)	/* EOP + 各エラー (bit16-23) のクリア */

#define FLASH_KEY1		0x45670123UL
#define FLASH_KEY2		0xCDEF89ABUL

/* 命令キャッシュ: 書き換えた領域を古い内容で読まないよう無効化する */
#define ICACHE_CR		0x40030400UL
#define ICACHE_SR		0x40030404UL
#define ICACHE_FCR		0x4003040CUL
#define ICACHE_CR_CACHEINV	(1u << 1)
#define ICACHE_SR_BSYENDF	(1u << 1)
#define ICACHE_FCR_CBSYENDF	(1u << 1)

LOCAL void flash_wait(void)
{
	while (in_w(FLASH_NSSR) & (SR_BSY | SR_WBNE | SR_DBNE)) {}
}

LOCAL void flash_unlock(void)
{
	if (in_w(FLASH_NSCR) & CR_LOCK) {
		out_w(FLASH_NSKEYR, FLASH_KEY1);
		out_w(FLASH_NSKEYR, FLASH_KEY2);
	}
}

LOCAL void flash_lock(void)
{
	out_w(FLASH_NSCR, CR_LOCK);
}

LOCAL void icache_invalidate(void)
{
	UW i;

	out_w(ICACHE_CR, in_w(ICACHE_CR) | ICACHE_CR_CACHEINV);
	for (i = 0; !(in_w(ICACHE_SR) & ICACHE_SR_BSYENDF) && i < 1000000; i++) {}
	out_w(ICACHE_FCR, ICACHE_FCR_CBSYENDF);
}

EXPORT void h5_flash_icache_invalidate(void)
{
	icache_invalidate();
}

EXPORT ER h5_flash_erase(UW offset, UW len)
{
	UW end, sec;
	UINT imask;
	ER err = E_OK;

	if (offset >= H5_FLASH_SIZE || len == 0 || offset + len > H5_FLASH_SIZE) return E_PAR;
	end = offset + len;

	DI(imask);
	flash_wait();
	flash_unlock();
	out_w(FLASH_NSCCR, CCR_ALL);

	for (sec = offset & ~(H5_FLASH_SECTOR_SIZE - 1); sec < end; sec += H5_FLASH_SECTOR_SIZE) {
		UW bank = (sec >= H5_FLASH_BANK_SIZE) ? CR_BKSEL : 0;
		UW snb  = (sec % H5_FLASH_BANK_SIZE) / H5_FLASH_SECTOR_SIZE;
		UW cr   = CR_SER | (snb << CR_SNB_SHIFT) | bank;

		out_w(FLASH_NSCR, cr);
		out_w(FLASH_NSCR, cr | CR_STRT);
		flash_wait();
		if (in_w(FLASH_NSSR) & SR_ERR_MASK) { err = E_IO; break; }
	}

	out_w(FLASH_NSCR, 0);
	flash_lock();
	icache_invalidate();
	EI(imask);
	return err;
}

EXPORT ER h5_flash_program(UW offset, const void *data, UW len)
{
	const UB *src = (const UB *)data;
	UW i, k;
	UINT imask;
	ER err = E_OK;

	if ((offset % H5_FLASH_QWORD) != 0 || data == NULL) return E_PAR;
	if (offset >= H5_FLASH_SIZE || offset + len > H5_FLASH_SIZE) return E_PAR;

	DI(imask);
	flash_wait();
	flash_unlock();
	out_w(FLASH_NSCCR, CCR_ALL);
	out_w(FLASH_NSCR, CR_PG);

	for (i = 0; i < len; i += H5_FLASH_QWORD) {
		volatile UW *dst = (volatile UW *)(H5_FLASH_BASE + offset + i);
		UW w[4];

		/* 16 バイトを 1 単位としてワード順に書く (端数は 0xFF) */
		for (k = 0; k < 4; k++) {
			UW b, v = 0;
			for (b = 0; b < 4; b++) {
				UW idx = i + k * 4 + b;
				v |= (UW)((idx < len) ? src[idx] : 0xFF) << (8 * b);
			}
			w[k] = v;
		}
		dst[0] = w[0];
		dst[1] = w[1];
		dst[2] = w[2];
		dst[3] = w[3];
		flash_wait();
		if (in_w(FLASH_NSSR) & SR_ERR_MASK) { err = E_IO; break; }
	}

	out_w(FLASH_NSCR, 0);
	flash_lock();
	icache_invalidate();
	EI(imask);
	return err;
}

EXPORT ER h5_flash_write(UW offset, const void *data, UW len)
{
	ER err = h5_flash_erase(offset, len);
	if (err != E_OK) return err;
	return h5_flash_program(offset, data, len);
}

#endif /* MTKBSP_CPU_STM32H5 */
