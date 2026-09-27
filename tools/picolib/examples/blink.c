/*
 * blink.c — 汎用 GPIO 点滅のミニマル wasm サンプル
 *
 * **注意 (Pico W + mintlsplit キーボード)**:
 *   GP25: CYW43 制御ライン (叩くと WiFi/BLE 停止)
 *   GP0-1:   UART serial
 *   GP2-7, 10-13: キーマトリクス ROW
 *   GP14-17:      キーマトリクス COL
 *   GP8-9:   I2C (スプリット slave 通信)
 *   GP23-24: CYW43 internal
 *   GP26-28: ADC
 *   **安全に自由利用できる pin: GP18-22** (本サンプルは GP22)
 *
 * Pico W 内蔵 LED を点滅させたい場合は別途 set_led MCP ツールを使う。
 *
 * Build:
 *   make blink.wasm     (or: clang --target=wasm32 ... — Makefile 参照)
 *
 * Upload:
 *   python tools/wasm_upload.py blink.wasm --slot 0 --memory-kb 4 --run
 */

#include "../picolib.h"

#define BLINK_PIN  22    /* GP22 — Pico W/mintlsplit で空いている安全な GPIO */

__attribute__((export_name("run")))
void run(void)
{
    LOG("blink: starting on GP15");
    int i;
    for (i = 0; i < 10; i++) {
        gpio_write(BLINK_PIN, 1);
        delay_ms(200);
        gpio_write(BLINK_PIN, 0);
        delay_ms(200);
    }
    LOG("blink: done");
}
