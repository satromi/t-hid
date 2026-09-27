/*
 * picolib.h — host function declarations for μT-Kernel + wasm3 on Pico W
 *
 * Use with:
 *   clang --target=wasm32 -nostdlib -O2 -Wl,--no-entry \
 *         -Wl,--export=run -Wl,--allow-undefined \
 *         -o program.wasm program.c
 *
 * The `run` function is the default entry point for wasm_run MCP tool.
 * Any function you `__attribute__((export_name("name")))` can also be
 * invoked via wasm_run {"entry":"name"}.
 *
 * All host functions are imported from module "env".
 */

#ifndef __PICOLIB_H__
#define __PICOLIB_H__

#include <stdint.h>
#include <stddef.h>

/*----------------------------------------------------------------------
 * GPIO (pin 0..29)
 */
int  gpio_read(int pin);
void gpio_write(int pin, int value);

/*----------------------------------------------------------------------
 * ADC (channel 0..4)
 *   ch 0..3 = GP26..GP29 analog
 *   ch 4    = internal temperature sensor
 * Returns 12-bit value (0..4095).
 */
int  adc_read(int channel);

/*----------------------------------------------------------------------
 * Time / delay
 */
void     delay_ms(uint32_t ms);
uint32_t get_ticks_ms(void);

/*----------------------------------------------------------------------
 * Logging (stderr-like output to serial console)
 *   returns bytes written (<= len, max 240).
 */
int log_printf(const char *msg, uint32_t len);

/*----------------------------------------------------------------------
 * MQTT publish (requires device MQTT connection)
 *   returns 0 on success, negative on error.
 */
int mqtt_pub(const char *topic, uint32_t topic_len,
             const uint8_t *payload, uint32_t payload_len);

/*----------------------------------------------------------------------
 * Convenience: log a string literal.
 */
#define LOG(s)  log_printf((s), sizeof(s) - 1)

#endif /* __PICOLIB_H__ */
