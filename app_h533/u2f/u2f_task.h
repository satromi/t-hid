/*
 * u2f_task.h — U2F 認証器タスクの起動
 */

#ifndef __U2F_TASK_H__
#define __U2F_TASK_H__

#include <tk/tkernel.h>

/* USB HID ドライバ初期化後に 1 回呼ぶ */
ER u2f_start(void);

/* 本人確認 (B1) の状態。TrustZone の構成ではセキュア側への受け渡しに使う */
BOOL u2f_presence_available(void);
void u2f_presence_used(void);
void u2f_presence_missing(void);

#endif /* __U2F_TASK_H__ */
