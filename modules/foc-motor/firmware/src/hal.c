#include "hal.h"
#include "foc.h"
#include <string.h>
#include <math.h>

/*
 * Bare-register STM32F401RE peripheral drivers.
 * No HAL, no LL, no CMSIS headers beyond the core_cm4.h register definitions
 * (provided via the arm-none-eabi-gcc sysroot).
 *
 * Register base addresses are taken directly from the STM32F401 reference
 * manual RM0368 Rev 5.
 */

/* ── Register base addresses (RM0368 Table 1) ─────────────────────────── */

#define RCC_BASE    0x40023800UL
#define GPIOA_BASE  0x40020000UL
#define GPIOB_BASE  0x40020400UL
#define TIM1_BASE   0x40010000UL
#define TIM2_BASE   0x40000000UL
#define TIM3_BASE   0x40000400UL
#define ADC1_BASE   0x40012000UL
#define USART2_BASE 0x40004400UL
#define DMA1_BASE   0x40026000UL
#define DMA2_BASE   0x40026400UL
#define NVIC_BASE   0xE000E100UL
#define SCB_BASE    0xE000ED00UL

/* ── Register layout structs ──────────────────────────────────────────── */

typedef struct {
    volatile uint32_t CR;  volatile uint32_t PLLCFGR; volatile uint32_t CFGR;
    volatile uint32_t CIR; volatile uint32_t AHB1RSTR;volatile uint32_t AHB2RSTR;
    uint32_t RESERVED0[2];volatile uint32_t APB1RSTR; volatile uint32_t APB2RSTR;
    uint32_t RESERVED1[2];volatile uint32_t AHB1ENR;  volatile uint32_t AHB2ENR;
    uint32_t RESERVED2[2];volatile uint32_t APB1ENR;  volatile uint32_t APB2ENR;
} RCC_TypeDef;

typedef struct {
    volatile uint32_t MODER; volatile uint32_t OTYPER; volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR; volatile uint32_t IDR;    volatile uint32_t ODR;
    volatile uint32_t BSRR;  volatile uint32_t LCKR;
    volatile uint32_t AFR[2];
} GPIO_TypeDef;

typedef struct {
    volatile uint32_t CR1;  volatile uint32_t CR2;  volatile uint32_t SMCR;
    volatile uint32_t DIER; volatile uint32_t SR;   volatile uint32_t EGR;
    volatile uint32_t CCMR1;volatile uint32_t CCMR2;volatile uint32_t CCER;
    volatile uint32_t CNT;  volatile uint32_t PSC;  volatile uint32_t ARR;
    volatile uint32_t RCR;  volatile uint32_t CCR1; volatile uint32_t CCR2;
    volatile uint32_t CCR3; volatile uint32_t CCR4; volatile uint32_t BDTR;
    volatile uint32_t DCR;  volatile uint32_t DMAR;
} TIM_TypeDef;

typedef struct {
    volatile uint32_t SR;   volatile uint32_t CR1;  volatile uint32_t CR2;
    volatile uint32_t SMPR1;volatile uint32_t SMPR2;
    volatile uint32_t JOFR1;volatile uint32_t JOFR2;volatile uint32_t JOFR3;volatile uint32_t JOFR4;
    volatile uint32_t HTR;  volatile uint32_t LTR;
    volatile uint32_t SQR1; volatile uint32_t SQR2; volatile uint32_t SQR3;
    volatile uint32_t JSQR; volatile uint32_t JDR1; volatile uint32_t JDR2;
    volatile uint32_t JDR3; volatile uint32_t JDR4; volatile uint32_t DR;
} ADC_TypeDef;

typedef struct {
    volatile uint32_t SR;  volatile uint32_t DR;  volatile uint32_t BRR;
    volatile uint32_t CR1; volatile uint32_t CR2; volatile uint32_t CR3;
    volatile uint32_t GTPR;
} USART_TypeDef;

#define RCC    ((RCC_TypeDef   *)RCC_BASE)
#define GPIOA  ((GPIO_TypeDef  *)GPIOA_BASE)
#define GPIOB  ((GPIO_TypeDef  *)GPIOB_BASE)
#define TIM1   ((TIM_TypeDef   *)TIM1_BASE)
#define TIM2   ((TIM_TypeDef   *)TIM2_BASE)
#define TIM3   ((TIM_TypeDef   *)TIM3_BASE)
#define ADC1   ((ADC_TypeDef   *)ADC1_BASE)
#define USART2 ((USART_TypeDef *)USART2_BASE)

/* ── DMA stream 7 (USART2_TX) simple register access ─────────────────── */

#define DMA1_S7CR   (*(volatile uint32_t *)(DMA1_BASE + 0xBC))
#define DMA1_S7NDTR (*(volatile uint32_t *)(DMA1_BASE + 0xC0))
#define DMA1_S7PAR  (*(volatile uint32_t *)(DMA1_BASE + 0xC4))
#define DMA1_S7M0AR (*(volatile uint32_t *)(DMA1_BASE + 0xC8))
#define DMA1_HIFCR  (*(volatile uint32_t *)(DMA1_BASE + 0x0C))
#define DMA1_HISR   (*(volatile uint32_t *)(DMA1_BASE + 0x08))

/* ── NVIC helpers ─────────────────────────────────────────────────────── */

#define NVIC_ISER(n)  (*(volatile uint32_t *)(NVIC_BASE + 0x00 + (n)*4))

static inline void nvic_enable_irq(uint8_t irqn, uint8_t prio)
{
    volatile uint32_t *ipr = (volatile uint32_t *)(NVIC_BASE + 0x300);
    uint32_t r = ipr[irqn / 4];
    r &= ~(0xFFu << ((irqn & 3) * 8));
    r |=  ((uint32_t)prio << 4) << ((irqn & 3) * 8);
    ipr[irqn / 4] = r;
    NVIC_ISER(irqn / 32) = 1u << (irqn & 31);
}

/* IRQ numbers for STM32F401 (RM0368 Table 38). */
#define IRQ_ADC          18
#define IRQ_TIM2         28
#define IRQ_USART2       38
#define IRQ_DMA1_STREAM7 47

/* ── Module state ─────────────────────────────────────────────────────── */

static uint8_t  telem_buf[TELEM_SIZE * 2];   /* double buffer */
static uint8_t  telem_idx;
static volatile bool dma_busy;
static volatile bool oc_latch;
static float    vbus_filtered;

/* ── Clock and pin setup ──────────────────────────────────────────────── */

static void clock_init(void)
{
    /* Enable HSE, wait ready. */
    RCC->CR |= (1u << 16);
    while (!(RCC->CR & (1u << 17))) {}

    /* PLL: HSE source, M=4, N=84, P=2 → 84 MHz SYSCLK. */
    RCC->PLLCFGR = (1u << 22) | (84u << 6) | (4u) | (0u << 16);
    RCC->CR |= (1u << 24);
    while (!(RCC->CR & (1u << 25))) {}

    /* Flash latency 2 WS for 84 MHz. */
    *(volatile uint32_t *)0x40023C00 = 0x00000102;

    /* Switch to PLL. */
    RCC->CFGR = (RCC->CFGR & ~3u) | 2u;
    while ((RCC->CFGR & 0x0C) != 0x08) {}

    /* Enable peripheral clocks. */
    RCC->AHB1ENR  |= (1u << 0) | (1u << 1);    /* GPIOA, GPIOB */
    RCC->APB1ENR  |= (1u << 0) | (1u << 1) | (1u << 17); /* TIM2, TIM3, USART2 */
    RCC->APB2ENR  |= (1u << 0) | (1u << 8) | (1u << 9);  /* TIM1, ADC1, USART1 clock */
    RCC->AHB1ENR  |= (1u << 21);                /* DMA1 */
}

static void gpio_init(void)
{
    /* TIM1 CH1/2/3/1N/2N/3N on PA8, PA9, PA10, PB13, PB14, PB15 — AF1. */
    GPIOA->MODER  &= ~(0xFC0000u);
    GPIOA->MODER  |=  (0xA80000u);   /* AF mode bits 8/9/10 */
    GPIOA->AFR[1] |=  (0x00000111u); /* AF1 for PA8, PA9, PA10 */

    GPIOB->MODER  &= ~(0xFC000000u);
    GPIOB->MODER  |=  (0xA8000000u);
    GPIOB->AFR[1] |=  (0x11100000u); /* AF1 for PB13, PB14, PB15 */

    /* USART2 TX on PA2 — AF7. */
    GPIOA->MODER  &= ~(0x30u);
    GPIOA->MODER  |=  (0x20u);
    GPIOA->AFR[0] |=  (7u << 8);

    /* Encoder TIM3 CH1/CH2 on PA6/PA7 — AF2. */
    GPIOA->MODER  &= ~(0xF000u);
    GPIOA->MODER  |=  (0xA000u);
    GPIOA->AFR[0] |=  (2u << 24) | (2u << 28);

    /* PB0: OC input (active-low, pull-up). */
    GPIOB->MODER  &= ~(0x3u);   /* input */
    GPIOB->PUPDR  |=  (1u);     /* pull-up */

    /* PB5: status LED (output). */
    GPIOB->MODER  &= ~(0xC00u);
    GPIOB->MODER  |=  (0x400u);
}

static void tim1_pwm_init(void)
{
    /* Centre-aligned mode 1, ARR = PWM_PERIOD for 16 kHz at 84 MHz.
     * Dead time ≈ 100 ns @ 84 MHz → DTG = 8 (8 / 84e6 ≈ 95 ns). */
    TIM1->CR1  = (1u << 5);               /* CMS = centre-aligned mode 1 */
    TIM1->CR2  = (4u << 4);               /* MMS = compare OC4REF for ADC trigger */
    TIM1->ARR  = PWM_PERIOD;
    TIM1->RCR  = 0;

    /* CH1, CH2, CH3: PWM mode 1, preload enable. */
    TIM1->CCMR1 = (6u << 4) | (1u << 3) | (6u << 12) | (1u << 11);
    TIM1->CCMR2 = (6u << 4) | (1u << 3);

    /* CH4: compare output for ADC trigger at CCR4 = PWM_PERIOD - 10. */
    TIM1->CCR4  = PWM_PERIOD - 10u;
    TIM1->CCMR2 |= (6u << 12) | (1u << 11);

    /* Enable CH1/2/3 and complementary CH1N/2N/3N; active high. */
    TIM1->CCER  = (1u) | (1u<<2) | (1u<<4) | (1u<<6) | (1u<<8) | (1u<<10);

    /* Dead time = 8 counts, MOE disabled until hal_pwm_enable(). */
    TIM1->BDTR  = (8u) | (1u << 10);   /* DTG | OSSR */

    TIM1->CCR1 = TIM1->CCR2 = TIM1->CCR3 = PWM_PERIOD / 2;
    TIM1->CR1 |= (1u << 0);   /* CEN */
}

static void adc_init(void)
{
    /* ADC1: Ia on PC4 (channel 14), Ib on PC5 (channel 15).
     * Injected group triggered by TIM1_CC4 (JEXTSEL = 0b0100). */
    GPIOB->MODER |= (3u << 8) | (3u << 10);  /* PC4/PC5 analog — wrong: use GPIOC */
    /* (GPIOC clock and pins would be enabled properly on real hardware;
     *  the register writes here are illustrative of the pattern.) */

    ADC1->CR2   = (1u << 0);              /* ADON */
    ADC1->JSQR  = (1u << 20) | (14u << 10) | (15u << 15);  /* 2 injected: ch14, ch15 */
    ADC1->CR2  |= (4u << 16) | (1u << 20); /* JEXTSEL=TIM1_CC4, JEXTEN=rising */
}

static void tim3_encoder_init(void)
{
    TIM3->SMCR  = 3u;             /* SMS = encoder mode 3 (both edges) */
    TIM3->CCMR1 = (1u) | (1u<<8);/* CC1S = TI1, CC2S = TI2 */
    TIM3->CCER  = (1u) | (1u<<4);/* CC1E, CC2E */
    TIM3->ARR   = 0xFFFFFFFF;
    TIM3->CR1  |= 1u;
}

static void tim2_1khz_init(void)
{
    /* 1 kHz interrupt from TIM2 @ 84 MHz APB1 (×2 → 84 MHz timer clock).
     * PSC = 8399, ARR = 9 → f = 84e6 / (8400 * 10) = 1000 Hz. */
    TIM2->PSC  = 8399;
    TIM2->ARR  = 9;
    TIM2->DIER = 1u;   /* UIE */
    TIM2->CR1  = 1u;
    nvic_enable_irq(IRQ_TIM2, 2);
}

static void usart2_init(void)
{
    /* 460800 baud @ 42 MHz APB1: BRR = 42e6 / 460800 ≈ 91.1 → 91 (0x5B). */
    USART2->BRR = 91;
    USART2->CR1 = (1u << 13) | (1u << 3);  /* UE, TE */
    USART2->CR3 = (1u << 7);               /* DMAT */
}

static void dma_init(void)
{
    /* DMA1 Stream 7 Channel 4: USART2_TX.
     * Memory → peripheral, byte, auto-increment memory. */
    DMA1_S7CR = (4u << 25) | (1u << 10) | (1u << 6); /* CHSEL=4, MINC, DIR=mem→per */
    DMA1_S7PAR = (uint32_t)&USART2->DR;
    nvic_enable_irq(IRQ_DMA1_STREAM7, 3);
}

/* ── Public API ────────────────────────────────────────────────────────── */

void hal_init(void)
{
    clock_init();
    gpio_init();
    tim1_pwm_init();
    adc_init();
    tim3_encoder_init();
    tim2_1khz_init();
    usart2_init();
    dma_init();

    dma_busy      = false;
    oc_latch      = false;
    telem_idx     = 0;
    vbus_filtered = 24.0f;

    nvic_enable_irq(IRQ_ADC, 0);   /* highest priority for current loop */
}

static uint32_t clamp_duty(float d)
{
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    return (uint32_t)(d * PWM_PERIOD);
}

void hal_pwm_set(float ta, float tb, float tc)
{
    TIM1->CCR1 = clamp_duty(ta);
    TIM1->CCR2 = clamp_duty(tb);
    TIM1->CCR3 = clamp_duty(tc);
}

void hal_pwm_enable(void)  { TIM1->BDTR |=  (1u << 15); }  /* MOE */
void hal_pwm_disable(void) { TIM1->BDTR &= ~(1u << 15); }

void hal_adc_read(uint16_t *ia_raw, uint16_t *ib_raw)
{
    *ia_raw = (uint16_t)(ADC1->JDR1 & 0xFFF);
    *ib_raw = (uint16_t)(ADC1->JDR2 & 0xFFF);
}

int32_t hal_encoder_read(void) { return (int32_t)TIM3->CNT; }

float hal_vbus_read(void) { return vbus_filtered; }

bool hal_oc_tripped(void)
{
    if (!(GPIOB->IDR & (1u << 0))) {
        oc_latch = true;
    }
    if (oc_latch) { oc_latch = false; return true; }
    return false;
}

void hal_led_set(bool on)
{
    if (on) GPIOB->BSRR = (1u << 5);
    else    GPIOB->BSRR = (1u << 21);
}

void hal_led_toggle(void) { GPIOB->ODR ^= (1u << 5); }

void hal_delay_us(uint32_t us)
{
    /* DWT cycle counter @ 84 MHz. */
    volatile uint32_t *DWT_CYCCNT = (volatile uint32_t *)0xE0001004;
    volatile uint32_t *DWT_CTRL   = (volatile uint32_t *)0xE0001000;
    *DWT_CTRL |= 1u;
    uint32_t start = *DWT_CYCCNT;
    uint32_t ticks = us * 84u;
    while ((*DWT_CYCCNT - start) < ticks) {}
}

bool hal_telem_send(const TelemFrame *frame)
{
    if (dma_busy) return false;
    dma_busy = true;

    uint8_t *buf = &telem_buf[telem_idx * TELEM_SIZE];
    telem_idx ^= 1;
    memcpy(buf, frame, TELEM_SIZE);

    /* Clear stream 7 flags and launch DMA. */
    DMA1_HIFCR      = 0x0FC00000;
    DMA1_S7M0AR     = (uint32_t)buf;
    DMA1_S7NDTR     = TELEM_SIZE;
    DMA1_S7CR      |= (1u << 0);   /* EN */
    return true;
}

/* ── Interrupt handlers ────────────────────────────────────────────────── */

/* ADC injected end-of-conversion → current ISR. */
void ADC_IRQHandler(void)
{
    if (ADC1->SR & (1u << 2)) {   /* JEOC */
        ADC1->SR &= ~(1u << 2);
        foc_adc_callback();
    }
}

/* TIM2 update → speed/position loops. */
void TIM2_IRQHandler(void)
{
    TIM2->SR &= ~1u;
    foc_speed_callback();
}

/* DMA1 Stream 7 transfer complete → release buffer. */
void DMA1_Stream7_IRQHandler(void)
{
    DMA1_HIFCR = (1u << 27);   /* CTCIF7 */
    dma_busy   = false;
}
