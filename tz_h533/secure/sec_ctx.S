/*
 * sec_ctx.S — TrustZone のセキュア側: タスク切り替え時のセキュアスタックの差し替え
 *
 *   非セキュア側のディスパッチャ (PendSV) から呼ばれる。PendSV の中で呼ばれた
 *   セキュア側のコードは MSP_S を使うので、呼ばれた時点の SP がそのまま
 *   切り替え前のタスクのセキュアスタックの位置になる。
 *
 *     void tzk_save_context(int32_t tskid)     SP を記録する
 *     void tzk_restore_context(int32_t tskid)  SP と MSPLIM を切り替える
 *
 *   スタックを持たないタスク ID なら何もしない。戻る前に、セキュア側のアドレスが
 *   非セキュア側に見えないよう、使ったレジスタとフラグを 0 にする。
 */
	.syntax unified
	.cpu cortex-m33
	.thumb

#define CTX_ssp		0
#define CTX_limit	4

	.text

	.global	tzk_save_context
	.global	__acle_se_tzk_save_context
	.type	tzk_save_context, %function
	.type	__acle_se_tzk_save_context, %function
	.thumb_func
tzk_save_context:
__acle_se_tzk_save_context:
	push	{r4, lr}
	bl	tzk_get_ctx
	pop	{r4, lr}
	cbz	r0, 1f
	str	sp, [r0, #CTX_ssp]
1:	b	tzk_clear_return
	.size	tzk_save_context, .-tzk_save_context

	.global	tzk_restore_context
	.global	__acle_se_tzk_restore_context
	.type	tzk_restore_context, %function
	.type	__acle_se_tzk_restore_context, %function
	.thumb_func
tzk_restore_context:
__acle_se_tzk_restore_context:
	push	{r4, lr}
	bl	tzk_get_ctx
	pop	{r4, lr}
	cbz	r0, 1f
	movs	r1, #0
	msr	msplim, r1
	ldr	r1, [r0, #CTX_ssp]
	mov	sp, r1
	ldr	r1, [r0, #CTX_limit]
	msr	msplim, r1
1:	b	tzk_clear_return
	.size	tzk_restore_context, .-tzk_restore_context

	.type	tzk_clear_return, %function
	.thumb_func
tzk_clear_return:
	movs	r0, #0
	movs	r1, #0
	movs	r2, #0
	movs	r3, #0
	mov	r12, r0
	msr	apsr_nzcvq, r0
	bxns	lr
	.size	tzk_clear_return, .-tzk_clear_return
