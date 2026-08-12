/*
 * hal.c — STM32F401 Nucleo HAL implementation.
 *
 * All register accesses use volatile word writes to the fixed peripheral
 * base addresses from the STM32F401 reference manual (RM0368).
 * No CMSIS or ST HAL dependency.
 */

#include "hal.h"

#include <stdarg.h>
#include <stdint.h>

/* ── Register base addresses ─────────────────────────────────────────────────── */
#define RCC_BASE    0x40023800UL
#define GPIOA_BASE  0x40020000UL
#define USART2_BASE 0x40004400UL
#define DWT_BASE    0xE0001000UL

/* RCC registers */
#define RCC_CR       (*((volatile uint32_t *)(RCC_BASE + 0x00)))
#define RCC_PLLCFGR  (*((volatile uint32_t *)(RCC_BASE + 0x04)))
#define RCC_CFGR     (*((volatile uint32_t *)(RCC_BASE + 0x08)))
#define RCC_AHB1ENR  (*((volatile uint32_t *)(RCC_BASE + 0x30)))
#define RCC_APB1ENR  (*((volatile uint32_t *)(RCC_BASE + 0x40)))
#define RCC_APB2ENR  (*((volatile uint32_t *)(RCC_BASE + 0x44)))

/* GPIOA registers */
#define GPIOA_MODER  (*((volatile uint32_t *)(GPIOA_BASE + 0x00)))
#define GPIOA_AFRL   (*((volatile uint32_t *)(GPIOA_BASE + 0x20)))
#define GPIOA_BSRR   (*((volatile uint32_t *)(GPIOA_BASE + 0x18)))
#define GPIOA_ODR    (*((volatile uint32_t *)(GPIOA_BASE + 0x14)))

/* USART2 registers */
#define USART2_SR    (*((volatile uint32_t *)(USART2_BASE + 0x00)))
#define USART2_DR    (*((volatile uint32_t *)(USART2_BASE + 0x04)))
#define USART2_BRR   (*((volatile uint32_t *)(USART2_BASE + 0x08)))
#define USART2_CR1   (*((volatile uint32_t *)(USART2_BASE + 0x0C)))

/* DWT */
#define DWT_CTRL     (*((volatile uint32_t *)(DWT_BASE + 0x00)))
#define DWT_CYCCNT   (*((volatile uint32_t *)(DWT_BASE + 0x04)))
#define CoreDebug_DEMCR (*((volatile uint32_t *)0xE000EDFC))

/* ── hal_clock_init ───────────────────────────────────────────────────────────── */

void hal_clock_init(void)
{
    /* Enable HSE */
    RCC_CR |= (1u << 16);
    while (!(RCC_CR & (1u << 17)));   /* wait HSERDY */

    /* Configure PLL: VCO = HSE(8MHz) * (N=336) / (M=8) = 336 MHz
     * SYSCLK = VCO / P=4 = 84 MHz
     * USB/SDIO = VCO / Q=7 = 48 MHz */
    RCC_PLLCFGR = (8u)           /* M = 8   bits [5:0]  */
                | (336u << 6)    /* N = 336 bits [14:6] */
                | (1u << 16)     /* P = 4   (01b)       */
                | (1u << 22)     /* PLLSRC = HSE        */
                | (7u << 24);    /* Q = 7   bits [27:24]*/

    /* Set flash latency to 2 wait states for 84 MHz */
    *((volatile uint32_t *)0x40023C00) = 0x702;   /* FLASH_ACR: PRFTEN|ICEN|DCEN|LATENCY=2 */

    /* Enable PLL */
    RCC_CR |= (1u << 24);
    while (!(RCC_CR & (1u << 25)));   /* wait PLLRDY */

    /* AHB = SYSCLK, APB1 = SYSCLK/2 = 42 MHz, APB2 = SYSCLK = 84 MHz */
    RCC_CFGR = (0u << 4)    /* HPRE  = /1  */
             | (4u << 10)   /* PPRE1 = /2  */
             | (0u << 13);  /* PPRE2 = /1  */

    /* Switch to PLL as SYSCLK */
    RCC_CFGR |= 2u;
    while ((RCC_CFGR & 0xCu) != 0x8u);   /* wait SWS = PLL */

    /* Enable DWT cycle counter */
    CoreDebug_DEMCR |= (1u << 24);
    DWT_CYCCNT = 0;
    DWT_CTRL   |= 1u;
}

/* ── hal_uart_init ────────────────────────────────────────────────────────────── */

void hal_uart_init(uint32_t baud)
{
    /* Enable GPIOA and USART2 clocks */
    RCC_AHB1ENR |= (1u << 0);   /* GPIOAEN */
    RCC_APB1ENR |= (1u << 17);  /* USART2EN */

    /* PA2 = USART2_TX (AF7), PA3 = USART2_RX (AF7) */
    GPIOA_MODER  = (GPIOA_MODER & ~(0xFu << 4)) | (0xAu << 4);   /* AF on PA2, PA3 */
    GPIOA_AFRL   = (GPIOA_AFRL  & ~(0xFFu << 8)) | (0x77u << 8); /* AF7 */

    /* Baud rate: USART2 is on APB1 = 42 MHz
     * BRR = fclk / baud = 42000000 / baud */
    USART2_BRR = 42000000u / baud;
    USART2_CR1 = (1u << 3)   /* TE */
               | (1u << 2)   /* RE */
               | (1u << 13); /* UE */
}

/* ── UART output ─────────────────────────────────────────────────────────────── */

void hal_uart_putc(char c)
{
    while (!(USART2_SR & (1u << 7)));   /* wait TXE */
    USART2_DR = (uint32_t)c;
}

void hal_uart_puts(const char *s)
{
    while (*s) hal_uart_putc(*s++);
}

/* Minimal printf: supports %d %u %x %s %c %% */
void hal_uart_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    char c;
    while ((c = *fmt++) != '\0') {
        if (c != '%') { hal_uart_putc(c); continue; }
        c = *fmt++;
        if (c == '\0') break;

        if (c == 's') {
            const char *s = va_arg(ap, const char *);
            hal_uart_puts(s ? s : "(null)");
        } else if (c == 'c') {
            hal_uart_putc((char)va_arg(ap, int));
        } else if (c == '%') {
            hal_uart_putc('%');
        } else if (c == 'd' || c == 'u' || c == 'x') {
            uint32_t v = va_arg(ap, uint32_t);
            char buf[12];
            int  i = 0;
            if (c == 'd' && (int32_t)v < 0) {
                hal_uart_putc('-');
                v = (uint32_t)(-(int32_t)v);
            }
            uint32_t base = (c == 'x') ? 16u : 10u;
            do {
                uint32_t digit = v % base;
                buf[i++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
                v /= base;
            } while (v);
            while (i--) hal_uart_putc(buf[i]);
        }
    }
    va_end(ap);
}

/* ── LED — PA5 (Nucleo onboard green LED) ────────────────────────────────────── */

void hal_led_init(void)
{
    RCC_AHB1ENR |= (1u << 0);
    GPIOA_MODER  = (GPIOA_MODER & ~(3u << 10)) | (1u << 10);   /* PA5 output */
}

void hal_led_on(void)   { GPIOA_BSRR = (1u << 5);  }
void hal_led_off(void)  { GPIOA_BSRR = (1u << 21); }
void hal_led_toggle(void)
{
    if (GPIOA_ODR & (1u << 5)) hal_led_off();
    else                        hal_led_on();
}

/* ── hal_delay_us ────────────────────────────────────────────────────────────── */

void hal_delay_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT;
    uint32_t ticks = us * 84u;   /* 84 cycles per µs at 84 MHz */
    while ((DWT_CYCCNT - start) < ticks);
}
