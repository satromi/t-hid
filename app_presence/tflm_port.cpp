/*
 * tflm_port.cpp — TensorFlow Lite Micro が要求する時間とログの関数
 *
 *   実体は tflm_port_time_us() / tflm_port_log() (C 側で μT-Kernel の機能を使って用意する)。
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "tensorflow/lite/micro/debug_log.h"
#include "tensorflow/lite/micro/micro_time.h"

extern "C" uint32_t tflm_port_time_us(void);
extern "C" void tflm_port_log(const char *format, va_list args);

namespace tflite {
void InitializeTarget() {}
uint32_t ticks_per_second() { return 1000000; }
uint32_t GetCurrentTimeTicks() { return tflm_port_time_us(); }
}  // namespace tflite

extern "C" void DebugLog(const char *format, va_list args)
{
	tflm_port_log(format, args);
}

extern "C" int DebugVsnprintf(char *buffer, size_t buf_size, const char *format, va_list args)
{
	return vsnprintf(buffer, buf_size, format, args);
}

/*
 * 関数内の static オブジェクトの後始末 (atexit) の登録先。このファームウェアは
 * 終了しないので、後始末は実行されない
 */
extern "C" {
void *__dso_handle = nullptr;
}

/* 標準ライブラリの初期化・終了処理の入口 (crt0 を使わないので空にする) */
extern "C" void _init(void) {}
extern "C" void _fini(void) {}
