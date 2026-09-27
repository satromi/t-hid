/*
 * temp_alert.c — RP2040 内蔵温度センサ監視 + MQTT 通知
 *
 * ADC ch4 (温度センサ) を 1 秒おきに読み、閾値超過で MQTT に publish。
 * 既存 Bytecode VM の温度監視サンプルを wasm で書き直したもの。
 *
 * Build:
 *   clang --target=wasm32 -nostdlib -O2 \
 *     -Wl,--no-entry -Wl,--export=run -Wl,--allow-undefined \
 *     -o temp_alert.wasm temp_alert.c
 */

#include "../picolib.h"

/* RP2040 内蔵温度センサの ADC 値 → 摂氏×10 変換 (簡易)
 * 実 formula: T = 27 - (ADC * 3.3/4096 - 0.706) / 0.001721
 * 整数演算で ×10 スケールに。
 */
static int adc_to_temp_x10(int adc)
{
    /* mv = adc * 3300 / 4096 ≈ adc * 0.80566 */
    /* T = 27 - (mv - 706) / 1.721 */
    int mv_x100 = adc * 33 * 100 / 4096;     /* mv ×100 */
    int num = (mv_x100 - 706 * 100);         /* ×100 */
    int T_x10 = 270 - num * 10 / 1721;       /* ×10 */
    return T_x10;
}

__attribute__((export_name("run")))
void run(void)
{
    const int threshold_x10 = 300;   /* 30.0 °C */
    int over_count = 0;
    char msg[64];
    int i;

    LOG("temp_alert: started (threshold=30.0C)");

    for (i = 0; i < 30; i++) {       /* 30 サイクル = 約 30 秒 */
        int adc = adc_read(4);
        int t_x10 = adc_to_temp_x10(adc);

        /* Simple decimal formatting: "T=XX.X C" */
        int len = 0;
        msg[len++] = 'T'; msg[len++] = '=';
        if (t_x10 < 0) { msg[len++] = '-'; t_x10 = -t_x10; }
        int whole = t_x10 / 10;
        if (whole >= 100) msg[len++] = '0' + (whole / 100) % 10;
        if (whole >= 10)  msg[len++] = '0' + (whole / 10) % 10;
        msg[len++] = '0' + (whole % 10);
        msg[len++] = '.';
        msg[len++] = '0' + (t_x10 % 10);
        msg[len++] = ' '; msg[len++] = 'C';
        log_printf(msg, len);

        if (t_x10 > threshold_x10) {
            over_count++;
            const char *topic = "mcp/kb-000001/alert";
            const char *body  = "TEMP_HIGH";
            mqtt_pub(topic, 19, (const uint8_t *)body, 9);
        }

        delay_ms(1000);
    }

    LOG("temp_alert: done");
}
