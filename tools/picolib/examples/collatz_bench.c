/*
 * collatz_bench.c — wasm3 on RP2040 の計算性能を見るミニベンチマーク
 *
 * Collatz (3n+1) 予想: n が奇数なら 3n+1, 偶数なら n/2 を繰り返し、
 * 1 に到達するまでのステップ数と最大到達値を求める。
 * 1..1000 の範囲でブルートフォースして、経過時間を MQTT に報告する。
 *
 *   1..1000 max_steps=178 at n=871 peak=190996 elapsed_ms=<WASM runtime 性能>
 *
 * 参考値 (Cortex-M0+ 125MHz, wasm3 interpreter, -O2):
 *   通常 数百 ms オーダ (C ネイティブと比べて 5-15 倍遅)
 *
 * 使い方:
 *   python tools/wasm_upload.py collatz_bench.wasm --slot 2 --run
 */

#include "../picolib.h"

static uint32_t collatz_steps(uint32_t n, uint32_t *peak_out)
{
    uint32_t steps = 0;
    uint32_t peak  = n;
    while (n > 1) {
        if (n & 1u) n = 3u * n + 1u;
        else        n = n >> 1u;
        if (n > peak) peak = n;
        steps++;
    }
    *peak_out = peak;
    return steps;
}

static int append_str(char *buf, int n, const char *s)
{
    while (*s) buf[n++] = *s++;
    return n;
}

static int append_u32(char *buf, int n, uint32_t v)
{
    char tmp[12];
    int j = 0;
    if (v == 0) tmp[j++] = '0';
    while (v > 0) { tmp[j++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (j > 0) buf[n++] = tmp[--j];
    return n;
}

__attribute__((export_name("run")))
void run(void)
{
    LOG("collatz: benchmarking 1..1000");

    uint32_t t0 = get_ticks_ms();
    uint32_t max_steps = 0;
    uint32_t peak_val  = 0;
    uint32_t peak_n    = 0;
    uint32_t n;
    for (n = 1; n <= 1000u; n++) {
        uint32_t peak;
        uint32_t s = collatz_steps(n, &peak);
        if (s > max_steps) {
            max_steps = s;
            peak_n    = n;
            peak_val  = peak;
        }
    }
    uint32_t elapsed = get_ticks_ms() - t0;

    char msg[128];
    int k = 0;
    k = append_str(msg, k, "collatz 1..1000 max_steps=");
    k = append_u32(msg, k, max_steps);
    k = append_str(msg, k, " at n=");
    k = append_u32(msg, k, peak_n);
    k = append_str(msg, k, " peak=");
    k = append_u32(msg, k, peak_val);
    k = append_str(msg, k, " elapsed_ms=");
    k = append_u32(msg, k, elapsed);

    log_printf(msg, (uint32_t)k);
    mqtt_pub("mcp/kb-000001/collatz", 21, (const uint8_t *)msg, (uint32_t)k);
    LOG("collatz: done");
}
