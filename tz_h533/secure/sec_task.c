/*
 * sec_task.c — TrustZone のセキュア側: TA_TZCALL のタスクのセキュアスタック
 *
 *   μT-Kernel 3.0 セキュア機能拡張の標準実行モデルのうち、セキュア側で行う処理
 *   (呼び出し口は tz_kernel_api.h)。
 *
 *   セキュア側のコードはスレッドモードでもハンドラモードでも MSP_S を使う。
 *   タスクごとに MSP_S の値 (セキュアスタック) を持ち、非セキュア側のディスパッチャが
 *   タスクを切り替えるたびに tzk_save_context / tzk_restore_context (sec_ctx.S) で
 *   差し替える。セキュア側の実行中に非セキュア側の割り込みが入ると、ハードウェアが
 *   そのタスクのセキュアスタックにレジスタを退避するので、そのまま別のタスクに
 *   切り替えられる。
 *
 *   スタックは静的な領域 (TZK_POOL_SIZE) から、先頭から空いている所に割り当てる。
 */

#include <stddef.h>
#include <stdint.h>

#include <tk/tkernel.h>
#include "tz_kernel_api.h"

#define ENTRY			__attribute__((cmse_nonsecure_entry))

#define TA_TZCALL		0x00010000u
#define TZK_MAX_TSKID		32		/* 非セキュア側の CNF_MAX_TSKID と合わせる */
#define TZK_POOL_SIZE		(16 * 1024)
#define TZK_MIN_STKSZ		256
#define TZK_STACK_ALIGN		8

/*
 * スタックの最上位に置く印 (stack sealing)。スタックが空の状態から例外の戻りを
 * 偽装されても、この値は正しい戻り先として扱われない
 */
#define TZK_STACK_SEAL		0xFEF5EDA5u

/* sec_ctx.S が参照するので、並びを変えないこと */
typedef struct {
	UW	ssp;		/* 保存した MSP_S */
	UW	limit;		/* MSPLIM_S (スタックの下端) */
	UW	top;		/* スタックの上端 (0 = 未使用) */
} TZK_CTX;

LOCAL UW tzk_pool[TZK_POOL_SIZE / sizeof(UW)] __attribute__((aligned(TZK_STACK_ALIGN)));
LOCAL TZK_CTX tzk_ctx[TZK_MAX_TSKID];
LOCAL BOOL tzk_ready;

/* sec_ctx.S から呼ぶ: タスク ID から、スタックを持つタスクの情報を得る */
EXPORT TZK_CTX *tzk_get_ctx(int32_t tskid)
{
	TZK_CTX *c;

	if (tskid < 1 || tskid > TZK_MAX_TSKID) return NULL;
	c = &tzk_ctx[tskid - 1];
	return (c->top != 0) ? c : NULL;
}

/* 先頭から順に、ほかのタスクのスタックと重ならない所を探す */
LOCAL UW tzk_alloc(UW size)
{
	UW start = (UW)tzk_pool;
	UW end = start + sizeof(tzk_pool);
	INT i;
	BOOL moved;

	do {
		moved = FALSE;
		for (i = 0; i < TZK_MAX_TSKID; i++) {
			TZK_CTX *c = &tzk_ctx[i];
			if (c->top == 0) continue;
			if (start < c->top && c->limit < start + size) {
				start = c->top;
				moved = TRUE;
			}
		}
	} while (moved && start + size <= end);

	return (start + size <= end) ? start : 0;
}

ENTRY int32_t tzk_init(void)
{
	UINT imask;
	INT i;

	DI(imask);
	if (tzk_ready) {
		EI(imask);
		return E_OBJ;
	}
	for (i = 0; i < TZK_MAX_TSKID; i++) {
		tzk_ctx[i].top = 0;
	}
	tzk_ready = TRUE;
	EI(imask);
	return E_OK;
}

ENTRY int32_t tzk_task_create(int32_t tskid, uint32_t tskatr, int32_t tzstksz)
{
	TZK_CTX *c;
	UINT imask;
	UW size, base, *p;

	if (tskid < 1 || tskid > TZK_MAX_TSKID) return E_PAR;
	if ((tskatr & TA_TZCALL) == 0 || tzstksz < TZK_MIN_STKSZ) return E_PAR;
	if ((UW)tzstksz > sizeof(tzk_pool)) return E_NOMEM;
	size = ((UW)tzstksz + TZK_STACK_ALIGN - 1) & ~(UW)(TZK_STACK_ALIGN - 1);

	DI(imask);
	if (!tzk_ready) {
		EI(imask);
		return E_SYS;
	}
	c = &tzk_ctx[tskid - 1];
	if (c->top != 0) {
		EI(imask);
		return E_OBJ;
	}
	base = tzk_alloc(size);
	if (base == 0) {
		EI(imask);
		return E_NOMEM;
	}

	/* 使っていない部分を印で埋めておく (使用量はデバッガで調べられる) */
	for (p = (UW *)base; p < (UW *)(base + size); p++) {
		*p = 0xCCCCCCCCu;
	}
	p = (UW *)(base + size) - 2;
	p[0] = TZK_STACK_SEAL;
	p[1] = TZK_STACK_SEAL;

	c->limit = base;
	c->ssp   = (UW)p;
	c->top   = base + size;
	EI(imask);
	return E_OK;
}

ENTRY int32_t tzk_task_delete(int32_t tskid)
{
	TZK_CTX *c;
	UINT imask;

	if (tskid < 1 || tskid > TZK_MAX_TSKID) return E_PAR;

	DI(imask);
	c = &tzk_ctx[tskid - 1];
	if (c->top == 0) {
		EI(imask);
		return E_NOEXS;
	}
	c->top = 0;
	EI(imask);
	return E_OK;
}
