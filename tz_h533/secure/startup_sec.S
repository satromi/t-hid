/*
 * startup_sec.S — TrustZone のセキュア側の起動処理 (STM32H533)
 *
 *   リセット後はセキュア側から始まる。.data と .bss を用意して sec_main() に入り、
 *   分離の設定をしてから非セキュア側 (μT-Kernel) へ移る。
 */
	.syntax unified
	.cpu cortex-m33
	.thumb

	.section .isr_vector, "a", %progbits
	.global g_sec_vectors
g_sec_vectors:
	.word	_estack
	.word	Reset_Handler
	.rept	14
	.word	Fault_Handler		/* NMI, HardFault, MemManage, BusFault, UsageFault, SecureFault, ... */
	.endr
	.rept	133
	.word	Fault_Handler		/* 割り込みはすべて非セキュア側へ渡すので使わない */
	.endr

	.section .text.Reset_Handler, "ax", %progbits
	.global Reset_Handler
	.type	Reset_Handler, %function
Reset_Handler:
	ldr	r0, =_sstack
	msr	msplim, r0

	/* .data を Flash から RAM へ */
	ldr	r0, =_sdata
	ldr	r1, =_edata
	ldr	r2, =_sidata
1:	cmp	r0, r1
	bhs	2f
	ldr	r3, [r2], #4
	str	r3, [r0], #4
	b	1b
2:
	/* .bss を 0 に */
	ldr	r0, =_sbss
	ldr	r1, =_ebss
	movs	r2, #0
3:	cmp	r0, r1
	bhs	4f
	str	r2, [r0], #4
	b	3b
4:
	bl	sec_main
5:	b	5b
	.size	Reset_Handler, .-Reset_Handler

	.type	Fault_Handler, %function
Fault_Handler:
	mov	r0, sp
	mov	r1, lr
	b	sec_fault_handler
	.size	Fault_Handler, .-Fault_Handler
