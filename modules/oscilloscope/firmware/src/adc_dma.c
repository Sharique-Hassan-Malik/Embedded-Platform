#include "adc_dma.h"

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

/* ADC GPIO pins: channel 0 = GPIO26, 1 = GPIO27, 2 = GPIO28 */
static const uint ADC_GPIO[3] = {26, 27, 28};

/* Module-level pointer so the shared IRQ handler can reach the struct */
static AdcDma *_g_adc;

/* ─── IRQ handler ──────────────────────────────────────────────────────────── */

static void _dma_irq_handler(void)
{
    AdcDma *adc = _g_adc;

    for (int h = 0; h < 2; h++) {
        if (dma_channel_get_irq0_status(adc->dma_chan[h])) {
            dma_channel_acknowledge_irq0(adc->dma_chan[h]);

            /* Re-arm this channel to restart after the other one finishes */
            dma_channel_set_write_addr(adc->dma_chan[h], adc->buf[h], false);
            dma_channel_set_trans_count(adc->dma_chan[h], DMA_HALF_SAMPLES, false);

            if (adc->ready_half != -1) {
                adc->overflow = true;   /* main loop didn't drain in time */
            }
            adc->ready_half = h;
            break;
        }
    }
}

/* ─── Static helpers ───────────────────────────────────────────────────────── */

static void _configure_adc(AdcDma *adc)
{
    adc_init();
    if (adc->channel < 3) {
        adc_gpio_init(ADC_GPIO[adc->channel]);
        adc_select_input(adc->channel);
    } else {
        /* channel 3 = on-chip temperature sensor */
        adc_set_temp_sensor_enabled(true);
        adc_select_input(4);
    }
    adc_fifo_setup(
        true,   /* enable FIFO */
        true,   /* DMA request on each sample */
        1,      /* DREQ threshold = 1 sample */
        false,  /* no error bit in sample */
        false   /* keep full 12-bit resolution (do not shift to 8-bit) */
    );
    adc_set_clkdiv((float)adc->clkdiv);
}

static void _configure_dma(AdcDma *adc)
{
    dma_channel_config cfg;

    for (int h = 0; h < 2; h++) {
        adc->dma_chan[h] = dma_claim_unused_channel(true);
        cfg = dma_channel_get_default_config(adc->dma_chan[h]);

        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
        channel_config_set_read_increment(&cfg, false);    /* ADC FIFO is fixed */
        channel_config_set_write_increment(&cfg, true);
        channel_config_set_dreq(&cfg, DREQ_ADC);
        /* Chain to the other channel so capture is gapless */
        channel_config_set_chain_to(&cfg, adc->dma_chan[h ^ 1]);

        dma_channel_configure(
            adc->dma_chan[h],
            &cfg,
            adc->buf[h],
            &adc_hw->fifo,
            DMA_HALF_SAMPLES,
            false   /* don't start yet */
        );

        dma_channel_set_irq0_enabled(adc->dma_chan[h], true);
    }

    irq_set_exclusive_handler(DMA_IRQ_0, _dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

/* ─── Public API ───────────────────────────────────────────────────────────── */

void adc_dma_init(AdcDma *adc, uint8_t channel, uint32_t clkdiv)
{
    _g_adc = adc;
    adc->ready_half = -1;
    adc->overflow   = false;
    adc->channel    = channel;
    adc->clkdiv     = clkdiv;

    _configure_adc(adc);
    _configure_dma(adc);
}

void adc_dma_set_channel(AdcDma *adc, uint8_t channel)
{
    bool was_running = adc->running;
    if (was_running) adc_dma_stop(adc);

    adc->channel    = channel;
    adc->ready_half = -1;
    adc->overflow   = false;
    _configure_adc(adc);

    if (was_running) adc_dma_start(adc);
}

void adc_dma_set_clkdiv(AdcDma *adc, uint32_t clkdiv)
{
    adc->clkdiv = clkdiv;
    adc_set_clkdiv((float)clkdiv);
}

void adc_dma_start(AdcDma *adc)
{
    adc->ready_half = -1;
    adc->overflow   = false;
    adc_fifo_drain();
    adc_run(false);
    /* Start channel 0 first; it will chain to channel 1 on completion */
    dma_channel_start(adc->dma_chan[0]);
    adc_run(true);
    adc->running = true;
}

void adc_dma_stop(AdcDma *adc)
{
    adc_run(false);
    dma_channel_abort(adc->dma_chan[0]);
    dma_channel_abort(adc->dma_chan[1]);
    adc_fifo_drain();
    adc->ready_half = -1;
    adc->running = false;
}
