/*
 * hm01b0.h — Pico4ML のカメラ (Himax HM01B0、モノクロ) を RP2040 の PIO と DMA で読む
 */
#ifndef HM01B0_H
#define HM01B0_H

#include <tk/typedef.h>

/* 2x2 ビニングで読み出す画像の大きさ (1 画素 1 バイト、0-255) */
#define HM01B0_COLS	164
#define HM01B0_ROWS	162

/* カメラを初期化して撮影を始める。fps は目安 (実際の値は近い値になる) */
ER hm01b0_init(INT fps);

/* 次の 1 フレームを buf (HM01B0_COLS * HM01B0_ROWS バイト、4 バイト境界) に取り込む */
ER hm01b0_capture(UB *buf, TMO tmout);

#endif /* HM01B0_H */
