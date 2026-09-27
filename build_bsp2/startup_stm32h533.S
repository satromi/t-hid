/*
 *	startup_stm32h533.S — STM32H533RET6 スタートアップコード
 *
 *	Cortex-M33 (ARMv8-M) 最小スタートアップ:
 *	  1. ベクタテーブル (.isr_vector)
 *	  2. Reset_Handler: .data コピー, .bss クリア, main() 呼び出し
 *
 *	STM32CubeMX 生成の startup_stm32h533xx.s の最小版。
 *	必要な例外ハンドラのみ定義し、未使用 IRQ は Default_Handler に集約。
 */

	.syntax unified
	.cpu cortex-m33
	.fpu fpv5-sp-d16
	.thumb

/*----------------------------------------------------------------------
 * 外部シンボル (リンカスクリプトから)
 */
	.word	_estack		/* 初期スタックポインタ (RAM 上端) */
	.word	_sidata		/* .data ROM コピー元 */
	.word	_sdata		/* .data RAM 先頭 */
	.word	_edata		/* .data RAM 末尾 */
	.word	_sbss		/* .bss 先頭 */
	.word	_ebss		/* .bss 末尾 */

/*----------------------------------------------------------------------
 * Reset_Handler — エントリポイント
 */
	.section .text.Reset_Handler
	.weak	Reset_Handler
	.type	Reset_Handler, %function
Reset_Handler:
	/* FPU 有効化 (CP10, CP11) */
	ldr	r0, =0xE000ED88		/* SCB->CPACR */
	ldr	r1, [r0]
	orr	r1, r1, #(0xF << 20)	/* CP10 + CP11 = Full Access */
	str	r1, [r0]
	dsb
	isb

	/* .data セクションをフラッシュから RAM にコピー */
	ldr	r0, =_sdata
	ldr	r1, =_edata
	ldr	r2, =_sidata
	b	.Ldata_check
.Ldata_copy:
	ldr	r3, [r2], #4
	str	r3, [r0], #4
.Ldata_check:
	cmp	r0, r1
	blt	.Ldata_copy

	/* .bss セクションをゼロクリア */
	ldr	r0, =_sbss
	ldr	r1, =_ebss
	movs	r2, #0
	b	.Lbss_check
.Lbss_clear:
	str	r2, [r0], #4
.Lbss_check:
	cmp	r0, r1
	blt	.Lbss_clear

	/* main() 呼び出し → knl_start_mtkernel() → カーネル起動 */
	bl	main
	b	.		/* NOTREACHED */
	.size	Reset_Handler, .-Reset_Handler

/*----------------------------------------------------------------------
 * Default_Handler — 未定義割り込みのフォールバック
 */
	.section .text.Default_Handler, "ax", %progbits
	.weak	Default_Handler
	.type	Default_Handler, %function
Default_Handler:
	b	.
	.size	Default_Handler, .-Default_Handler

/*----------------------------------------------------------------------
 * ベクタテーブル (Cortex-M33 + STM32H533)
 *
 * 最小構成: システム例外 16 + 外部 IRQ 133 = 149 エントリ
 * 未使用スロットは Default_Handler にエイリアス。
 * μT-Kernel は knl_start_mtkernel() 内で RAM にコピーし SCB_VTOR を書き換える。
 */
	.section .isr_vector, "a", %progbits
	.type	g_pfnVectors, %object
	.size	g_pfnVectors, .-g_pfnVectors
g_pfnVectors:
	/* System exceptions (0-15) */
	.word	_estack			/*  0: Initial SP */
	.word	Reset_Handler		/*  1: Reset */
	.word	Default_Handler		/*  2: NMI */
	.word	Default_Handler		/*  3: HardFault */
	.word	Default_Handler		/*  4: MemManage */
	.word	Default_Handler		/*  5: BusFault */
	.word	Default_Handler		/*  6: UsageFault */
	.word	Default_Handler		/*  7: SecureFault */
	.word	0			/*  8: Reserved */
	.word	0			/*  9: Reserved */
	.word	0			/* 10: Reserved */
	.word	Default_Handler		/* 11: SVCall */
	.word	Default_Handler		/* 12: DebugMonitor */
	.word	0			/* 13: Reserved */
	.word	Default_Handler		/* 14: PendSV */
	.word	Default_Handler		/* 15: SysTick */

	/* External IRQ (0-132) — 全て Default_Handler */
	.rept	133
	.word	Default_Handler
	.endr
