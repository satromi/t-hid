/*
 * ble_split_central.h — BLE Split Keyboard Central (Master side)
 *
 * Master が BLE Central として Slave に接続し、
 * カスタム GATT Matrix Service の Notification でマトリクスデータを受信する。
 */

#ifndef BLE_SPLIT_CENTRAL_H
#define BLE_SPLIT_CENTRAL_H

#include <tk/tkernel.h>
#include "../../app_program/keyboard/split_transport.h"

/* Master: BLE Central 初期化 (scan 開始、slave 接続) */
ER ble_split_central_init(void);

/*
 * Master: slave マトリクスデータを取得
 *
 * 最後に BLE Notification で受信したマトリクスデータを返す。
 * 新しいデータがなければ前回のデータを返す (変化なしとして処理)。
 *
 * 戻り値: E_OK=データあり, E_IO=未接続, E_NOEXS=データ未受信
 */
ER ble_split_central_read_matrix(matrix_row_t *slave_matrix);

/* Master: slave 接続状態 */
BOOL ble_split_central_is_connected(void);

#endif /* BLE_SPLIT_CENTRAL_H */
