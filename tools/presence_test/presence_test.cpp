/*
 * presence_test.cpp — 離席判定の推論部分 (app_presence/presence_ml.cpp) を PC 上で確かめる
 *
 *   TensorFlow Lite Micro の person detection に付属する「人あり」「人なし」の
 *   96x96 画像を入力し、それぞれ正しく判定できるかを見る。
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "presence_ml.h"

extern const unsigned char g_person_image_data[];
extern const unsigned char g_no_person_image_data[];

extern "C" uint32_t tflm_port_time_us(void)
{
	return (uint32_t)((double)clock() * 1000000.0 / CLOCKS_PER_SEC);
}

extern "C" void tflm_port_log(const char *format, va_list args)
{
	vfprintf(stderr, format, args);
}

static int failures;

static void check(const char *name, bool ok, int score)
{
	printf("  %s  %s (score=%d)\n", ok ? "OK  " : "FAIL", name, score);
	if (!ok) failures++;
}

static int run(const unsigned char *image)
{
	memcpy(presence_ml_input(), image, PRESENCE_ML_COLS * PRESENCE_ML_ROWS);
	return presence_ml_invoke();
}

int main(void)
{
	int err = presence_ml_init();

	printf("presence_ml_init: %d\n", err);
	if (err != 0) return 1;

	int s = run(g_person_image_data);
	check("人が写った画像を「人あり」と判定", s > 0, s);
	s = run(g_no_person_image_data);
	check("人が写っていない画像を「人なし」と判定", s < 0, s);

	/* 真っ暗 (カメラを覆った・消灯) は「人なし」 */
	memset(presence_ml_input(), -128, PRESENCE_ML_COLS * PRESENCE_ML_ROWS);
	s = presence_ml_invoke();
	check("真っ暗な画像を「人なし」と判定", s < 0, s);

	printf(failures ? "\n%d FAILED\n" : "\nALL PASSED (0 failures)\n", failures);
	return failures ? 1 : 0;
}
