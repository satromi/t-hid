/*
 * ble_split_service.h — BLE Split Keyboard Matrix Service (Slave side)
 *
 * Slave 半分が Master にマトリクスデータを BLE で送信するための
 * カスタム GATT サービス。
 *
 * Service UUID: 4b4b5348-5f53-504c-4954-000000000001
 * Matrix Characteristic UUID: 4b4b5348-5f53-504c-4954-000000000002
 *   Value: [checksum:1][smatrix:MATRIX_ROWS_PER_HAND]
 *   Properties: Read, Notify
 *
 * Master が BLE Central として接続し、Notification を subscribe して
 * マトリクス変化をリアルタイムで受信する。
 */

#ifndef BLE_SPLIT_SERVICE_H
#define BLE_SPLIT_SERVICE_H

#include <tk/tkernel.h>
#include "../../app_program/keyboard/split_transport.h"

/* カスタム UUID (ASCII "KKSH_SPLIT" + 連番) */
#define BLE_SPLIT_SERVICE_UUID  "4b4b5348-5f53-504c-4954-000000000001"
#define BLE_SPLIT_MATRIX_UUID   "4b4b5348-5f53-504c-4954-000000000002"

/* Slave: Matrix Service 初期化 (GATT DB 登録 + アドバタイズ開始) */
ER ble_split_service_init(void);

/* Slave: マトリクスデータ更新 → Master に Notification 送信 */
void ble_split_service_update_matrix(const matrix_row_t *matrix);

#endif /* BLE_SPLIT_SERVICE_H */
