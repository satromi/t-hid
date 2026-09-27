/*
 * sec_main.c — TrustZone のセキュア側: 起動時の分離設定と非セキュア側への移行
 *
 *   メモリの分け方 (オプションバイトの SECWM と合わせる)
 *     Flash  0x0C000000-0x0C00EFFF  セキュア側のコード
 *            0x0C00F000-0x0C00FFFF  呼び出し口 (NSC)
 *            0x08010000-0x0806FFFF  非セキュア側のコード
 *            0x08070000-0x08077FFF  wasm のキー処理モジュール (非セキュア)
 *            0x0C078000-0x0C07DFFF  U2F の鍵とカウンタ (セキュア)
 *            0x0807E000-0x0807FFFF  MCP の保存領域 (非セキュア)
 *     SRAM   0x20000000-0x2003BFFF  非セキュア (240KB)
 *            0x3003C000-0x30043FFF  セキュア (SRAM3 の上位 32KB)
 *
 *   セキュア側に置く周辺回路: AES、HASH、RNG、SAES、PKA、B1 のピン (PC13)
 *   それ以外 (USB、I2C、SPI、USART、GPIO、タイマなど) と割り込みは非セキュア側に渡す。
 */

#include <arm_cmse.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <tk/tkernel.h>

/*----------------------------------------------------------------------
 * 配置
 */
#define NS_VECTOR		0x08010000UL
#define NS_FLASH_START		0x08010000UL
#define NS_FLASH_END		0x0807FFFFUL
#define NSC_START		0x0C00F000UL
#define NSC_END			0x0C00FFFFUL
#define NS_SRAM_START		0x20000000UL
#define NS_SRAM_END		0x2003BFFFUL
#define NS_PERIPH_START		0x40000000UL
#define NS_PERIPH_END		0x4FFFFFFFUL

/*----------------------------------------------------------------------
 * コアのレジスタ (セキュア側から見たもの)
 */
#define SCB_AIRCR		0xE000ED0CUL
#define SCB_SHCSR		0xE000ED24UL
#define SCB_CPACR		0xE000ED88UL
#define SCB_NSACR		0xE000ED8CUL
#define FPU_FPCCR		0xE000EF34UL
#define SCB_NS_VTOR		0xE002ED08UL	/* 非セキュア側の VTOR */
#define SAU_CTRL		0xE000EDD0UL
#define SAU_RNR			0xE000EDD8UL
#define SAU_RBAR		0xE000EDDCUL
#define SAU_RLAR		0xE000EDE0UL
#define NVIC_ITNS(n)		(0xE000E380UL + 4 * (n))

#define AIRCR_VECTKEY		(0x05FAu << 16)
#define AIRCR_BFHFNMINS		(1u << 13)	/* HardFault/NMI/BusFault を非セキュア側で扱う */
#define SHCSR_SECUREFAULTENA	(1u << 19)
#define FPCCR_TS		(1u << 26)	/* 浮動小数点レジスタをセキュアとして扱う */
#define SAU_RLAR_ENABLE		(1u << 0)
#define SAU_RLAR_NSC		(1u << 1)
#define NVIC_ITNS_REGS		5		/* IRQ 0-159 (H533 は 0-132) */

/*----------------------------------------------------------------------
 * 周辺回路 (セキュア側のアドレス)
 */
#define RCC_AHB1ENR_S		0x54020C88UL
#define RCC_AHB1ENR_TZSC1EN	(1u << 24)	/* TZSC のレジスタのクロック */
#define RCC_AHB2ENR_S		0x54020C8CUL
#define RCC_AHB2ENR_GPIO_ADH	(0x0Fu | (1u << 7))	/* GPIOA-D, GPIOH */
#define GPIO_BASE_S(port)	(0x52020000UL + 0x400UL * (port))
#define GPIO_MODER		0x00
#define GPIO_PUPDR		0x0C
#define GPIO_SECCFGR		0x30
#define GPIOC_PORT		2
#define B1_PIN			13		/* PC13 = NUCLEO の B1 (USER、押すと High) */
#define GTZC_TZSC_SECCFGR3_S	0x50032418UL
#define TZSC3_AES		(1u << 16)
#define TZSC3_HASH		(1u << 17)
#define TZSC3_RNG		(1u << 18)
#define TZSC3_SAES		(1u << 19)
#define TZSC3_PKA		(1u << 20)
#define MPCBB_SECCFGR(base, i)	((base) + 0x100 + 4 * (i))
#define GTZC_MPCBB1_S		0x50032C00UL	/* SRAM1 128KB = 8 レジスタ */
#define GTZC_MPCBB2_S		0x50033000UL	/* SRAM2  80KB = 5 レジスタ */
#define GTZC_MPCBB3_S		0x50033400UL	/* SRAM3  64KB: 64KB 境界 (0x20030000) から数える */

/*----------------------------------------------------------------------
 * ログ (tm_printf の代わり)。非セキュア側が u2fs_log_read で取り出す
 */
#define SEC_LOG_SIZE		1024
LOCAL char sec_log[SEC_LOG_SIZE];
LOCAL UW sec_log_len;

EXPORT INT tm_printf(const UB *format, ...)
{
	va_list ap;
	INT n;

	if (sec_log_len >= SEC_LOG_SIZE - 1) return 0;
	va_start(ap, format);
	n = vsnprintf(&sec_log[sec_log_len], SEC_LOG_SIZE - sec_log_len, (const char *)format, ap);
	va_end(ap);
	if (n < 0) return 0;
	sec_log_len += (UW)n;
	if (sec_log_len > SEC_LOG_SIZE - 1) sec_log_len = SEC_LOG_SIZE - 1;
	return n;
}

EXPORT UW sec_log_take(char *buf, UW max)
{
	UW n = (sec_log_len < max) ? sec_log_len : max;

	memcpy(buf, sec_log, n);
	memmove(sec_log, sec_log + n, sec_log_len - n);
	sec_log_len -= n;
	return n;
}

/*----------------------------------------------------------------------
 * 分離の設定
 */
LOCAL void sau_region(UW n, UW start, UW end, UW attr)
{
	out_w(SAU_RNR, n);
	out_w(SAU_RBAR, start & ~0x1Fu);
	out_w(SAU_RLAR, (end & ~0x1Fu) | attr | SAU_RLAR_ENABLE);
}

LOCAL void setup_sau(void)
{
	sau_region(0, NS_FLASH_START, NS_FLASH_END, 0);
	sau_region(1, NSC_START, NSC_END, SAU_RLAR_NSC);
	sau_region(2, NS_SRAM_START, NS_SRAM_END, 0);
	sau_region(3, NS_PERIPH_START, NS_PERIPH_END, 0);
	out_w(SAU_CTRL, 1);	/* ENABLE。どの領域にも入らない番地はセキュア */
}

LOCAL void setup_gtzc(void)
{
	UW i;

	/* 暗号の周辺回路をセキュア専用にする (非セキュア側からは読み書きできない)。
	 * TZSC のレジスタはクロックを入れないと書き込みが無視される */
	out_w(RCC_AHB1ENR_S, in_w(RCC_AHB1ENR_S) | RCC_AHB1ENR_TZSC1EN);
	(void)in_w(RCC_AHB1ENR_S);
	out_w(GTZC_TZSC_SECCFGR3_S, in_w(GTZC_TZSC_SECCFGR3_S) |
	      TZSC3_AES | TZSC3_HASH | TZSC3_RNG | TZSC3_SAES | TZSC3_PKA);

	/*
	 * SRAM: 512 バイト単位 (レジスタ 1 本で 16KB)。SRAM1/2 は全部を非セキュアに。
	 * SRAM3 (0x20034000) の区切りは 64KB 境界の 0x20030000 から数えるので、
	 * レジスタ 1-2 が下位 32KB (非セキュア)、3-4 が上位 32KB (セキュア側の RAM)。
	 */
	for (i = 0; i < 8; i++) out_w(MPCBB_SECCFGR(GTZC_MPCBB1_S, i), 0);
	for (i = 0; i < 5; i++) out_w(MPCBB_SECCFGR(GTZC_MPCBB2_S, i), 0);
	out_w(MPCBB_SECCFGR(GTZC_MPCBB3_S, 1), 0);
	out_w(MPCBB_SECCFGR(GTZC_MPCBB3_S, 2), 0);
	out_w(MPCBB_SECCFGR(GTZC_MPCBB3_S, 3), 0xFFFFFFFFu);
	out_w(MPCBB_SECCFGR(GTZC_MPCBB3_S, 4), 0xFFFFFFFFu);
}

LOCAL void setup_gpio(void)
{
	static const UB ports[] = { 0, 1, 2, 3, 7 };	/* A, B, C, D, H */
	UW i;

	/*
	 * TrustZone 有効時は全ピンがセキュアで始まるので、B1 (PC13) 以外を非セキュア側に
	 * 渡す。B1 は U2F の本人確認に使うので、セキュア側だけが読めるようにする。
	 */
	out_w(RCC_AHB2ENR_S, in_w(RCC_AHB2ENR_S) | RCC_AHB2ENR_GPIO_ADH);
	(void)in_w(RCC_AHB2ENR_S);
	for (i = 0; i < sizeof(ports); i++) {
		out_w(GPIO_BASE_S(ports[i]) + GPIO_SECCFGR,
		      (ports[i] == GPIOC_PORT) ? (1u << B1_PIN) : 0);
	}

	/* B1: 入力、プルダウン */
	out_w(GPIO_BASE_S(GPIOC_PORT) + GPIO_MODER,
	      in_w(GPIO_BASE_S(GPIOC_PORT) + GPIO_MODER) & ~(3u << (B1_PIN * 2)));
	out_w(GPIO_BASE_S(GPIOC_PORT) + GPIO_PUPDR,
	      (in_w(GPIO_BASE_S(GPIOC_PORT) + GPIO_PUPDR) & ~(3u << (B1_PIN * 2))) | (2u << (B1_PIN * 2)));
}

LOCAL void setup_core(void)
{
	UW i;

	/* 割り込みはすべて非セキュア側へ */
	for (i = 0; i < NVIC_ITNS_REGS; i++) out_w(NVIC_ITNS(i), 0xFFFFFFFFu);

	/* 非セキュア側に FPU (CP10/CP11) を許可し、セキュア側でも使えるようにする */
	out_w(SCB_NSACR, in_w(SCB_NSACR) | (3u << 10));
	out_w(SCB_CPACR, in_w(SCB_CPACR) | (0xFu << 20));

	/*
	 * セキュア側の実行中に非セキュア側の割り込みが入ったとき、S0-S31 をすべて
	 * セキュアスタックに退避して消す。非セキュア側のディスパッチャは、セキュア側を
	 * 実行中のタスクを切り替えるときに浮動小数点レジスタを保存しないので必要
	 */
	out_w(FPU_FPCCR, in_w(FPU_FPCCR) | FPCCR_TS);

	/* HardFault などは非セキュア側の既存の処理に任せ、SecureFault は有効にする */
	out_w(SCB_AIRCR, AIRCR_VECTKEY | (in_w(SCB_AIRCR) & 0xFFFFu & ~AIRCR_VECTKEY) | AIRCR_BFHFNMINS);
	out_w(SCB_SHCSR, in_w(SCB_SHCSR) | SHCSR_SECUREFAULTENA);
	__asm__ volatile ("dsb\n\tisb" ::: "memory");
}

typedef void __attribute__((cmse_nonsecure_call)) (*ns_entry_t)(void);

LOCAL void start_nonsecure(void)
{
	UW msp = in_w(NS_VECTOR);
	UW reset = in_w(NS_VECTOR + 4);
	ns_entry_t entry;

	out_w(SCB_NS_VTOR, NS_VECTOR);
	__asm__ volatile ("msr msp_ns, %0" :: "r"(msp));
	entry = (ns_entry_t)cmse_nsfptr_create(reset);
	entry();
}

EXPORT void sec_main(void)
{
	setup_sau();
	setup_gtzc();
	setup_gpio();
	setup_core();
	start_nonsecure();
	for (;;) {}
}

/*----------------------------------------------------------------------
 * セキュア側の例外 (SecureFault など)。非セキュア側からの不正な呼び出しで起きる
 */
/*
 * 調査用に、例外の種類と両側のスタックの様子を残して止まる
 * (デバッガでセキュア側の RAM の sec_fault_info を読む)
 */
typedef struct {
	UW	sfsr, sfar, cfsr, hfsr, icsr;
	UW	exc_return, msp_s, msp_ns, psp_ns, control_ns;
	UW	msplim_s, msplim_ns, basepri_ns, primask_ns, vtor_ns, vtor_s, vec_systick_ns;
	UW	icsr_ns, shcsr_ns, fpccr_s, fpcar_s, fpccr_ns, fpcar_ns;
	UW	tzic_sr[4], ns_ram0, ns_flash0, mpcbb1_0, rcc_ahb1enr;
	UW	sau_ctrl, sau[8][2];
	UW	frame_s[16];		/* 例外の入口で積まれた内容 (MSP_S 側) */
	UW	frame_ns[24];		/* 非セキュア側の MSP の内容 */
} T_SEC_FAULT;
EXPORT volatile T_SEC_FAULT sec_fault_info;

EXPORT void sec_fault_handler(UW *sp, UW exc_return)
{
	UW msp_ns, psp_ns, control_ns, i;

	__asm__ volatile ("mrs %0, msp_ns" : "=r"(msp_ns));
	__asm__ volatile ("mrs %0, psp_ns" : "=r"(psp_ns));
	__asm__ volatile ("mrs %0, control_ns" : "=r"(control_ns));
	sec_fault_info.sfsr = in_w(0xE000EDE4);
	sec_fault_info.sfar = in_w(0xE000EDE8);
	sec_fault_info.cfsr = in_w(0xE000ED28);
	sec_fault_info.hfsr = in_w(0xE000ED2C);
	sec_fault_info.icsr = in_w(0xE000ED04);
	sec_fault_info.exc_return = exc_return;
	sec_fault_info.msp_s = (UW)sp;
	sec_fault_info.msp_ns = msp_ns;
	sec_fault_info.psp_ns = psp_ns;
	sec_fault_info.control_ns = control_ns;
	{
		UW v;
		__asm__ volatile ("mrs %0, msplim" : "=r"(v)); sec_fault_info.msplim_s = v;
		__asm__ volatile ("mrs %0, msplim_ns" : "=r"(v)); sec_fault_info.msplim_ns = v;
		__asm__ volatile ("mrs %0, basepri_ns" : "=r"(v)); sec_fault_info.basepri_ns = v;
		__asm__ volatile ("mrs %0, primask_ns" : "=r"(v)); sec_fault_info.primask_ns = v;
	}
	sec_fault_info.vtor_ns = in_w(0xE002ED08);
	sec_fault_info.vtor_s = in_w(0xE000ED08);
	sec_fault_info.vec_systick_ns = in_w(sec_fault_info.vtor_ns + 15 * 4);
	sec_fault_info.icsr_ns = in_w(0xE002ED04);
	sec_fault_info.shcsr_ns = in_w(0xE002ED24);
	sec_fault_info.fpccr_s = in_w(0xE000EF34);
	sec_fault_info.fpcar_s = in_w(0xE000EF38);
	sec_fault_info.fpccr_ns = in_w(0xE002EF34);
	sec_fault_info.fpcar_ns = in_w(0xE002EF38);
	for (i = 0; i < 4; i++) sec_fault_info.tzic_sr[i] = in_w(0x50032810 + 4 * i);
	sec_fault_info.ns_ram0 = in_w(0x20000000);
	sec_fault_info.ns_flash0 = in_w(0x08010000);
	sec_fault_info.mpcbb1_0 = in_w(0x50032D00);
	sec_fault_info.rcc_ahb1enr = in_w(0x54020C88);
	sec_fault_info.sau_ctrl = in_w(SAU_CTRL);
	for (i = 0; i < 8; i++) {
		out_w(SAU_RNR, i);
		sec_fault_info.sau[i][0] = in_w(SAU_RBAR);
		sec_fault_info.sau[i][1] = in_w(SAU_RLAR);
	}
	for (i = 0; i < 16; i++) sec_fault_info.frame_s[i] = sp[i];
	if (msp_ns >= 0x20000000u && msp_ns < 0x2003C000u - 24 * 4) {
		for (i = 0; i < 24; i++) sec_fault_info.frame_ns[i] = ((UW *)msp_ns)[i];
	}
	for (;;) {}
}
