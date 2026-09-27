/*
 * kb_wiring_test.h — Matrix wiring verification mode
 */

#ifndef __KB_WIRING_TEST_H__
#define __KB_WIRING_TEST_H__

/*
 * 配線チェックループ (戻らない)
 *
 * スキャナータスクから呼ぶ。キーマップを使わず、押されたセルの
 * マトリクス座標を USB HID キーボードとして文字入力する。
 */
void kb_wiring_test_loop(void);

#endif /* __KB_WIRING_TEST_H__ */
