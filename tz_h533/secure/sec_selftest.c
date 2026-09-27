/*
 * sec_selftest.c — TrustZone の自己テストのセキュア側 (make TZ=1 TZTEST=1 のときだけ)
 *
 *   tzt_calc() は局所変数だけを使うので、複数のタスクから同時に呼ばれてもよい。
 */
#include <stdint.h>

#include "tz_selftest.h"

__attribute__((cmse_nonsecure_entry)) uint32_t tzt_work(uint32_t seed, uint32_t loops)
{
	return tzt_calc(seed, loops);
}
