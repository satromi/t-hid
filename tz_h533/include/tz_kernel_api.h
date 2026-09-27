/*
 * tz_kernel_api.h — TrustZone: μT-Kernel のカーネルが使うセキュア側の呼び出し口
 *
 *   μT-Kernel 3.0 セキュア機能拡張の「標準実行モデル」で、カーネルがセキュア側に
 *   任せる処理。セキュアコール可能属性 (TA_TZCALL) のタスクは、セキュア側に専用の
 *   スタックを持ち、セキュア側の関数を実行している途中でもタスクを切り替えられる。
 *
 *     tzk_init             カーネル起動時に 1 回。セキュアスタックの領域を初期化する
 *     tzk_task_create      TA_TZCALL のタスクの生成時。セキュアスタックを確保する
 *     tzk_task_delete      TA_TZCALL のタスクの削除時。セキュアスタックを解放する
 *     tzk_save_context     ディスパッチャから。切り替え前のタスクのセキュアスタックを記録する
 *     tzk_restore_context  ディスパッチャから。切り替え後のタスクのセキュアスタックに切り替える
 *
 *   カーネル以外 (アプリケーション) から呼んではならない。
 */
#ifndef TZ_KERNEL_API_H
#define TZ_KERNEL_API_H

#include <stdint.h>

int32_t tzk_init(void);
int32_t tzk_task_create(int32_t tskid, uint32_t tskatr, int32_t tzstksz);
int32_t tzk_task_delete(int32_t tskid);
void tzk_save_context(int32_t tskid);
void tzk_restore_context(int32_t tskid);

#endif /* TZ_KERNEL_API_H */
