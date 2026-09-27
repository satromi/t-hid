/*
 * task_sysdep.c — TrustZone (make TZ=1): TA_TZCALL のタスクのセキュアスタック
 *
 *   μT-Kernel 3.0 セキュア機能拡張の標準実行モデル。セキュアコール可能属性
 *   (TA_TZCALL) のタスクを生成するとセキュア側にそのタスク専用のスタックを確保し、
 *   削除すると解放する。task_manage.c の tk_cre_tsk / tk_del_tsk / tk_exd_tsk から
 *   呼ばれる。
 *
 *   TA_TZCALL のタスクを生成・削除できるのは、TA_TZCALL のタスク (または
 *   カーネルの起動処理) だけ。
 */

#include <tk/tkernel.h>
#include <kernel.h>

#include "tz_kernel_api.h"

LOCAL BOOL tzcall_allowed(void)
{
	if (knl_isTaskIndependent()) return FALSE;
	if (knl_ctxtsk != NULL && (knl_ctxtsk->tskatr & TA_TZCALL) == 0) return FALSE;
	return TRUE;
}

EXPORT ER knl_tcb_sysdep_cre(TCB *tcb, CONST T_CTSK *pk_ctsk)
{
	if ((tcb->tskatr & TA_TZCALL) == 0) return E_OK;
	if (!tzcall_allowed()) return E_CTX;
	return (ER)tzk_task_create(tcb->tskid, tcb->tskatr, pk_ctsk->tzstksz);
}

EXPORT ER knl_tcb_sysdep_del(TCB *tcb)
{
	if ((tcb->tskatr & TA_TZCALL) == 0) return E_OK;
	if (!tzcall_allowed()) return E_CTX;
	return (ER)tzk_task_delete(tcb->tskid);
}
