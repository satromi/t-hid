/*
 * sensor_dashboard.c — 全 ADC + 温度センサを serial に定期ダンプ
 *
 * ADC 0..3 (GP26..GP29) + ch4 (温度センサ raw) を 500ms おきに 10 回読み、
 * log_printf で serial に 1 行ずつ出す。
 *
 *   [wasm] sample 0: 724 633 628 16 873
 *   [wasm] sample 1: 768 580 581 16 872
 *
 * 注意 (known issue): wasm context から 50+ host call を経た後に mqtt_pub を
 * 呼ぶと hard fault する問題を確認中。そのため本サンプルでは MQTT publish を
 * 行わず log_printf のみで serial 出力する。原因究明は後続セッションにて。
 * 参考: morse/collatz のように wasm 開始後すぐに 1 回だけ mqtt_pub するのは OK。
 *
 * 使い方:
 *   python tools/wasm_upload.py sensor_dashboard.wasm --slot 1 --memory-kb 16 --run
 */

#include "../picolib.h"

static int append_str(char *buf, int n, const char *s)
{
    while (*s) buf[n++] = *s++;
    return n;
}

static int append_int(char *buf, int n, int v)
{
    if (v < 0) { buf[n++] = '-'; v = -v; }
    char tmp[12];
    int j = 0;
    if (v == 0) tmp[j++] = '0';
    while (v > 0) { tmp[j++] = (char)('0' + (v % 10)); v /= 10; }
    while (j > 0) buf[n++] = tmp[--j];
    return n;
}

__attribute__((export_name("run")))
void run(void)
{
    LOG("sensor_dashboard: start (10 samples @ 500ms)");

    int i;
    for (i = 0; i < 10; i++) {
        char line[96];
        int n = 0;
        n = append_str(line, n, "sample ");
        n = append_int(line, n, i);
        n = append_str(line, n, ":");
        int ch;
        for (ch = 0; ch < 5; ch++) {
            line[n++] = ' ';
            n = append_int(line, n, adc_read(ch));
        }
        log_printf(line, (uint32_t)n);
        delay_ms(500);
    }
    LOG("sensor_dashboard: done");
}
