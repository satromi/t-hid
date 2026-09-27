/*
 * ble_hid_gatt.h — BLE HID Keyboard GATT service
 *
 * BTstack の HOGP (HID over GATT Profile) を使った
 * BLE キーボードの初期化と HID レポート送信。
 */

#ifndef BLE_HID_GATT_H
#define BLE_HID_GATT_H

#include <tk/tkernel.h>
#include "../../app_program/keyboard/kb_output.h"

/* BLE HID 初期化 — GATT サービス登録 (BTstack 初期化後に呼ぶ) */
ER ble_hid_gatt_init(void);

/* BLE HCI パワーオン — HCI init 開始 (BLE タスク起動後に呼ぶ) */
ER ble_hid_gatt_start(void);

/* BLE HID レポート送信 (接続済みの場合のみ) */
ER ble_hid_gatt_send_report(const T_HID_KBD_REPORT *report);

/* BLE 接続状態 */
BOOL ble_hid_gatt_is_connected(void);

/*
 * BLE タスクから呼ぶポーリング処理
 * pending report があれば BTstack API で送信リクエストを行う。
 * BTstack はスレッドセーフでないため、この関数は必ず BLE タスク
 * (run loop) コンテキストから呼ぶこと。
 */
void ble_hid_gatt_process(void);

/*
 * XMK アクション処理 (Scanner タスクから呼ばれる)
 * BLE タスクにイベントで通知し、BTstack コンテキストで処理する。
 */
void ble_hid_gatt_xmk_action(UH xmk_code);

/* ペアリングモード状態 */
BOOL ble_hid_is_pairing_mode(void);

#endif /* BLE_HID_GATT_H */
