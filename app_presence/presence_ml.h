/*
 * presence_ml.h — 人が写っているかの推論 (TensorFlow Lite Micro の person detection)
 *
 *   入力は 96x96 のグレースケール。presence_ml_input() が返すバッファに
 *   int8 (画素値 - 128) で書き込み、presence_ml_invoke() で推論する。
 */
#ifndef PRESENCE_ML_H
#define PRESENCE_ML_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRESENCE_ML_COLS	96
#define PRESENCE_ML_ROWS	96

/* モデルを読み込む。0: 成功、負: 失敗 */
int presence_ml_init(void);

/* 入力画像のバッファ (PRESENCE_ML_COLS * PRESENCE_ML_ROWS バイト) */
int8_t *presence_ml_input(void);

/* 推論して「人がいる」点数 (-128..127、大きいほど人らしい) を返す。失敗時は -129 */
int presence_ml_invoke(void);

#ifdef __cplusplus
}
#endif

#endif /* PRESENCE_ML_H */
