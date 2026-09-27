/*
 * tz_selftest.h — TrustZone の自己テスト (make TZ=1 TZTEST=1)
 *
 *   tzt_calc() はセキュア側 (tzt_work) と非セキュア側の両方で同じ計算をして、
 *   結果を比べる。整数レジスタと浮動小数点レジスタを多く使い、途中で割り込まれて
 *   タスクが切り替わってもレジスタが壊れないことを確かめる。浮動小数点は
 *   加減算だけにして (融合積和にならない)、どちらでビルドしても同じ結果にする。
 */
#ifndef TZ_SELFTEST_H
#define TZ_SELFTEST_H

#include <stdint.h>

static inline uint32_t tzt_calc(uint32_t seed, uint32_t loops)
{
	uint32_t x = seed | 1u, a = 0, b = 0, c = 0, d = 0;
	float f0 = 0, f1 = 0, f2 = 0, f3 = 0, f4 = 0, f5 = 0, f6 = 0, f7 = 0;
	uint32_t i;

	for (i = 0; i < loops; i++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		a += x;
		b ^= a + (x >> 3);
		c += b ^ (x << 7);
		d = (d << 1 | d >> 31) ^ c;
		if ((i & 0xFF) == 0) {
			f0 = f1 = f2 = f3 = f4 = f5 = f6 = f7 = 0;
		}
		f0 += (float)(x & 0xFF);
		f1 += (float)(a & 0xFF);
		f2 += (float)(b & 0xFF);
		f3 += (float)(c & 0xFF);
		f4 += (float)(d & 0xFF);
		f5 += f0 - f1;
		f6 += f2 - f3;
		f7 += f4 - f5;
	}
	return a ^ b ^ c ^ d ^ (uint32_t)(int32_t)(f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7);
}

/* セキュア側で tzt_calc() を実行する */
uint32_t tzt_work(uint32_t seed, uint32_t loops);

#endif /* TZ_SELFTEST_H */
