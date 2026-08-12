#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Hardware Abstraction Layer for STM32F401RE (Nucleo-64).
 *
 * Peripheral map:
 *   TIM1    — 3-phase complementary PWM, 16 kHz, centre-aligned, dead-time 100 ns
 *   ADC1    — injected group triggered by TIM1_CC4, simultaneous Ia + Ib sampling
 *   TIM3    — quadrature encoder interface (CH1/CH2 on PA6/PA7)
 *   TIM2    — 1 kHz SysTick substitute for speed/position loops
 *   USART2  — 460800 baud telemetry (PA2 TX) — DMA-backed, non-blocking
 *   GPIOB   — PB0: hardware over-current trip input (active-low), PB5: status LED
 */

/* PWM period register value for 16 kHz at 84 MHz APB2. */
#define PWM_PERIOD      2625u

/* ADC full-scale: 12-bit, 3.3 V reference. */
#define ADC_FS          4095.0f
#define ADC_VREF        3.3f

/* Current sense: 10 mΩ shunt, INA240 gain 50 → 0.5 V/A.
 * ADC counts per ampere = (0.5 / 3.3) * 4095 ≈ 620.5 */
#define CURRENT_SCALE   (ADC_VREF / (0.5f * ADC_FS))   /* A per count */

/* Current sense offset (mid-rail for bipolar sensing). */
#define CURRENT_OFFSET  (ADC_FS / 2.0f)

/* Encoder CPR for a 1000-line incremental encoder (×4 decoding). */
#define ENCODER_CPR     4000.0f

/* Mechanical speed filter time constant (single-pole IIR, 1 kHz rate).
 * τ = 5 ms → α = 1 - exp(-Ts/τ) ≈ 0.181 */
#define SPEED_ALPHA     0.181f

/* Telemetry packet header and size. */
#define TELEM_MAGIC     0xA5u
#define TELEM_SIZE      24u   /* bytes per frame */

/*
 * Telemetry frame — matches the 24-byte on-wire layout decoded by monitor.py.
 * All multi-byte fields are little-endian.
 *
 * Byte  0     : TELEM_MAGIC (0xA5)
 * Bytes 1–4   : tick (uint32, little-endian)
 * Bytes 5–8   : id     (float32)
 * Bytes 9–12  : iq     (float32)
 * Bytes 13–16 : omega  (float32)
 * Bytes 17–20 : theta  (float32)
 * Byte  21    : mode   (uint8)
 * Byte  22    : faults (uint8, low byte of fault_flags)
 * Byte  23    : reserved / checksum (XOR of bytes 0–22)
 */
typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint32_t tick;
    float    id;
    float    iq;
    float    omega;
    float    theta;
    uint8_t  mode;
    uint8_t  faults;
    uint8_t  csum;
} TelemFrame;

/* Peripheral initialisation — call once from main() before enabling interrupts. */
void hal_init(void);

/* PWM output — duty cycles in [0, 1]; also enables/disables gate drivers. */
void hal_pwm_set(float ta, float tb, float tc);
void hal_pwm_enable(void);
void hal_pwm_disable(void);

/* ADC — returns raw 12-bit counts for phase A and B current sense channels. */
void hal_adc_read(uint16_t *ia_raw, uint16_t *ib_raw);

/* Encoder — returns the 32-bit signed count (wraps at ±2^31). */
int32_t hal_encoder_read(void);

/* Telemetry — non-blocking; returns false if the DMA buffer is busy. */
bool hal_telem_send(const TelemFrame *frame);

/* DC bus voltage in volts (filtered). */
float hal_vbus_read(void);

/* Over-current hardware latch — returns true if tripped since last call. */
bool hal_oc_tripped(void);

/* Status LED. */
void hal_led_set(bool on);
void hal_led_toggle(void);

/* Delay in microseconds (busy-wait using DWT cycle counter). */
void hal_delay_us(uint32_t us);

/* Interrupt enable/disable wrappers. */
static inline void hal_irq_enable(void)  { __asm volatile("cpsie i" ::: "memory"); }
static inline void hal_irq_disable(void) { __asm volatile("cpsid i" ::: "memory"); }

/*
 * Callback prototypes — implement in main.c.
 * hal_init() registers these as interrupt handlers.
 */
void foc_adc_callback(void);      /* called from TIM1_CC4 triggered ADC injected EOC */
void foc_speed_callback(void);    /* called from TIM2 at 1 kHz */

#endif /* HAL_H */
