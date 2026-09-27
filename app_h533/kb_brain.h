/*
 * kb_brain.h — NUCLEO-H533RE キーボードブレイン
 *
 * 左右の Pico (I2C スレーブ) からマトリクスを読み、キー処理
 * (組み込みキーマップ or wasm モジュール) を行って USB HID に出力する。
 */

#ifndef __KB_BRAIN_H__
#define __KB_BRAIN_H__

#include <tk/tkernel.h>
#include "kb_output.h"

/* スキャンタスク起動 (usermain から 1 回) */
ER   kb_brain_start(void);

/* HID レポートを即時送信 (EP 使用中なら最大 50ms 再試行) */
ER   kb_brain_send_report(const T_HID_KBD_REPORT *report);

/* 左右スレーブの接続状態 */
BOOL kb_brain_left_connected(void);
BOOL kb_brain_right_connected(void);

#endif /* __KB_BRAIN_H__ */
