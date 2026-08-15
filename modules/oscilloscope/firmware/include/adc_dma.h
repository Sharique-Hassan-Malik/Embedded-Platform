#ifndef ADC_DMA_H
#define ADC_DMA_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Ping-pong DMA capture on the Raspberry Pi Pico.
 *
 * Two DMA channels alternate.  While channel A fills buffer A, channel B is
 * chained to restart channel A when B finishes, and vice versa.  The IRQ
 * fires on each completion and sets a flag so the main loop can consume the
 * finished buffer without gaps.
 *
 * ADC FIFO → DMA channel → uint16_t buffer[FRAME_MAX_SAMPLES]
 *
 * The ADC is configured for free-running mode on a single input channel.
 * Changing the channel resets and reconfigures the ADC.
 */

#include "protocol.h"

/* Number of samples in each ping-pong buffer half */
#define DMA_HALF_SAMPLES FRAME_MAX_SAMPLES

typedef struct {
    uint16_t buf[2][DMA_HALF_SAMPLES];   /* ping / pong */
    volatile int  ready_half;            /* 0 or 1; -1 = none ready */
    volatile bool overflow;              /* set when IRQ fires before main loop drains */
    int dma_chan[2];
    uint8_t channel;
    uint32_t clkdiv;
    volatile bool running;               /* true between adc_dma_start and _stop */
} AdcDma;

void adc_dma_init(AdcDma *adc, uint8_t channel, uint32_t clkdiv);
void adc_dma_set_channel(AdcDma *adc, uint8_t channel);
void adc_dma_set_clkdiv(AdcDma *adc, uint32_t clkdiv);
void adc_dma_start(AdcDma *adc);
void adc_dma_stop(AdcDma *adc);

#endif /* ADC_DMA_H */
