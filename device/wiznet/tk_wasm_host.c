/*
 *----------------------------------------------------------------------
 *    tk_wasm_host — wasm3 host function bindings (強シンボル)
 *
 *    wasm モジュールから `extern` で呼ばれる関数群。
 *    tk_wasm_register_host() で runtime にリンク (weak stub を override)。
 *
 *    関数シグネチャ (wasm3 形式):
 *      i / I  = i32 / i64
 *      v       = void
 *      *       = pointer (32bit offset into linear memory)
 *      f / F   = f32 / f64 (未使用 — M0+ は FPU なし)
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(MTKBSP_CPU_STM32H5)

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

#include "wasm3.h"
#include "m3_env.h"
#include "tk_mqtt.h"

/* プラットフォーム固有 (Step 5 で tk_mcp_hal.h から取る予定) */
#if defined(CPU_RP2040)
#define GPIO_OUT_SET    0xD0000014
#define GPIO_OUT_CLR    0xD0000018
#define GPIO_IN         0xD0000004

#include <sys/sysdef.h>        /* in_w, out_w */
#endif

/*----------------------------------------------------------------------
 * host: gpio_write(pin, value) → void
 */
m3ApiRawFunction(host_gpio_write)
{
    m3ApiGetArg(int32_t, pin);
    m3ApiGetArg(int32_t, value);
#if defined(CPU_RP2040)
    if (pin >= 0 && pin <= 29) {
        if (value) out_w(GPIO_OUT_SET, (1u << pin));
        else       out_w(GPIO_OUT_CLR, (1u << pin));
    }
#else
    (void)pin; (void)value;
#endif
    m3ApiSuccess();
}

/*----------------------------------------------------------------------
 * host: gpio_read(pin) → int32
 */
m3ApiRawFunction(host_gpio_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, pin);
    int32_t v = 0;
#if defined(CPU_RP2040)
    if (pin >= 0 && pin <= 29) {
        v = (in_w(GPIO_IN) >> pin) & 1;
    }
#else
    (void)pin;
#endif
    m3ApiReturn(v);
}

/*----------------------------------------------------------------------
 * host: adc_read(channel) → int32 (12bit 値)
 *
 * チャネル 0..3 = GP26..GP29, 4 = 内蔵温度センサ (RP2040)
 */
#if defined(CPU_RP2040)
#define ADC_BASE    0x4004C000
#define ADC_CS      (ADC_BASE + 0x00)
#define ADC_RESULT  (ADC_BASE + 0x04)
#endif

m3ApiRawFunction(host_adc_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, ch);
    int32_t v = 0;
#if defined(CPU_RP2040)
    if (ch >= 0 && ch <= 4) {
        /* 既存の mcp_adc_read_raw (tk_mcp_hal.h) と同じ 2 段階書込パターン。
         * 1 回書きで AINSEL + START_ONCE を同時設定すると ch=4 (温度センサ)
         * で hard fault 再現あり。TS_EN は全 ch で常時 ON にして安定化 */
        UW cs = (1u << 0) | ((UW)ch << 12) | (1u << 1);  /* EN | AINSEL | TS_EN */
        out_w(ADC_CS, cs);                               /* 1) config */
        out_w(ADC_CS, in_w(ADC_CS) | (1u << 2));         /* 2) START_ONCE */
        uint32_t t = 0;
        while (!(in_w(ADC_CS) & (1u << 8)) && t < 100000) t++;
        v = (int32_t)(in_w(ADC_RESULT) & 0xFFF);
    }
#else
    (void)ch;
#endif
    m3ApiReturn(v);
}

/*----------------------------------------------------------------------
 * host: delay_ms(ms) → void
 */
m3ApiRawFunction(host_delay_ms)
{
    m3ApiGetArg(uint32_t, ms);
    if (ms > 0) tk_dly_tsk((RELTIM)ms);
    m3ApiSuccess();
}

/*----------------------------------------------------------------------
 * host: get_ticks_ms() → uint32
 */
m3ApiRawFunction(host_get_ticks_ms)
{
    m3ApiReturnType(uint32_t);
    SYSTIM t;
    tk_get_otm(&t);
    m3ApiReturn((uint32_t)t.lo);
}

/*----------------------------------------------------------------------
 * host: log_printf(msg_ptr, len) → int (書込みバイト数)
 *
 * wasm メモリから msg を取り出して tm_printf 出力。
 */
m3ApiRawFunction(host_log_printf)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArgMem(const char *, msg);
    m3ApiGetArg(uint32_t, len);
    m3ApiCheckMem(msg, len);
    if (len > 240) len = 240;   /* tm_printf のバッファ保護 */
    char buf[256];
    memcpy(buf, msg, len);
    buf[len] = '\0';
    tm_printf((UB *)"[wasm] %s\n", buf);
    m3ApiReturn((int32_t)len);
}

/*----------------------------------------------------------------------
 * host: mqtt_pub(topic_ptr, topic_len, payload_ptr, payload_len) → int
 *
 * 0 成功、負値エラー。tk_mqtt_publish が MQTT 接続時のみ成功。
 */
m3ApiRawFunction(host_mqtt_pub)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArgMem(const char *, topic_ptr);
    m3ApiGetArg(uint32_t, topic_len);
    m3ApiGetArgMem(const uint8_t *, payload_ptr);
    m3ApiGetArg(uint32_t, payload_len);
    m3ApiCheckMem(topic_ptr, topic_len);
    m3ApiCheckMem(payload_ptr, payload_len);

    if (topic_len == 0 || topic_len > 120) m3ApiReturn(-1);
    if (payload_len > 2048) m3ApiReturn(-2);
    if (!tk_mqtt_is_connected()) m3ApiReturn(-3);

    /* topic を NUL 終端 */
    char topic[128];
    memcpy(topic, topic_ptr, topic_len);
    topic[topic_len] = '\0';

    ER err = tk_mqtt_publish(topic, payload_ptr, (UH)payload_len);
    m3ApiReturn((int32_t)((err == E_OK) ? 0 : err));
}

/*----------------------------------------------------------------------
 * 登録: tk_wasm.c 内の weak stub を override
 *
 * シグネチャ文字列:
 *   v(ii)   gpio_write(pin, value)
 *   i(i)    gpio_read(pin)
 *   i(i)    adc_read(ch)
 *   v(i)    delay_ms(ms)
 *   i()     get_ticks_ms()
 *   i(*i)   log_printf(msg_ptr, len)
 *   i(*i*i) mqtt_pub(topic_ptr, topic_len, payload_ptr, payload_len)
 *
 * モジュール名は "env" (clang/wasi-sdk のデフォルト import 名前空間)。
 */
EXPORT M3Result tk_wasm_register_host(IM3Runtime runtime, IM3Module module)
{
    (void)runtime;
    M3Result r;

    r = m3_LinkRawFunction(module, "env", "gpio_write",   "v(ii)",   host_gpio_write);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "gpio_read",    "i(i)",    host_gpio_read);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "adc_read",     "i(i)",    host_adc_read);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "delay_ms",     "v(i)",    host_delay_ms);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "get_ticks_ms", "i()",     host_get_ticks_ms);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "log_printf",   "i(*i)",   host_log_printf);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "mqtt_pub",     "i(*i*i)", host_mqtt_pub);
    if (r && r != m3Err_functionLookupFailed) return r;

    return NULL;   /* 成功 (未使用 import は functionLookupFailed を許容) */
}

#endif /* CPU_RP2040 || MTKBSP_CPU_STM32H5 */
