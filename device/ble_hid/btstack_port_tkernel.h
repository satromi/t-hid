/*
 * btstack_port_tkernel.h — BTstack μT-Kernel port
 *
 * BTstack の HAL (Hardware Abstraction Layer) を μT-Kernel API で実装。
 * - hal_time_ms: tk_get_otm() による ms タイマー
 * - hal_cpu: 割り込み制御
 * - BLE タスク: btstack_run_loop_embedded_execute_once() をポーリング
 */

#ifndef BTSTACK_PORT_TKERNEL_H
#define BTSTACK_PORT_TKERNEL_H

#include <tk/tkernel.h>

/* BLE タスク設定 */
#define BLE_TASK_PRI        7       /* KB scanner(8) より高優先度 (優先度逆転防止) */
#define BLE_TASK_STKSZ      8192    /* BTstack ECDH P-256 pairing で ~3KB + 余裕 */
#define BLE_POLL_INTERVAL   5       /* 5ms ポーリング間隔 */

/*
 * BTstack 初期化 (2段階)
 *
 * btstack_tkernel_init():     run loop 初期化 + イベントフラグ生成
 *                             BTstack API を呼べる状態にする (タスクはまだ起動しない)
 * btstack_tkernel_start():    BLE タスク起動 (run loop 実行開始)
 *
 * 使用順序:
 *   1. btstack_tkernel_init()   — run loop 準備
 *   2. ble_hid_gatt_init() 等  — BTstack API で GATT サービス登録
 *   3. btstack_tkernel_start()  — BLE タスク起動 (ここから run loop が回る)
 *
 * BTstack はスレッドセーフではないため、全 API 呼び出しは
 * BLE タスク起動前 (init 段階) か、BLE タスクのコンテキスト内で行うこと。
 */
ER  btstack_tkernel_init(void);     /* run loop + イベントフラグ初期化 */
ER  btstack_tkernel_start(void);    /* BLE タスク起動 */

/* ISR から BLE タスクを起床させる (CYW43 IRQ) */
void btstack_tkernel_trigger(void);

#endif /* BTSTACK_PORT_TKERNEL_H */
