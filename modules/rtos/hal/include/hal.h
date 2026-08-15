/*
 * hal.h — minimal HAL for STM32F401 Nucleo.
 *
 * Only the peripherals needed by the demo application:
 *   • System clock init (84 MHz from HSE PLL)
 *   • UART2 (connected to ST-Link virtual COM, PA2/PA3)
 *   • GPIO — onboard LED (PA5)
 */

#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>

void     hal_clock_init(void);
void     hal_uart_init(uint32_t baud);
void     hal_uart_putc(char c);
void     hal_uart_puts(const char *s);
void     hal_uart_printf(const char *fmt, ...);
void     hal_led_init(void);
void     hal_led_toggle(void);
void     hal_led_on(void);
void     hal_led_off(void);

/* Busy-loop microsecond delay (uses DWT cycle counter) */
void     hal_delay_us(uint32_t us);

#endif /* HAL_H */
