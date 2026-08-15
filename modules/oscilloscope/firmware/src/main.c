#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "protocol.h"
#include "adc_dma.h"
#include "crc16.h"

/* ─── Frame serialiser ─────────────────────────────────────────────────────── */

/*
 * Layout: [4B sync][1B channel][1B flags][2B n_samples][samples×2B][2B CRC16]
 * Total overhead: 10 bytes.
 */
#define FRAME_HEADER_LEN 8u
#define FRAME_FOOTER_LEN 2u

static uint8_t _frame_buf[FRAME_HEADER_LEN
                           + FRAME_MAX_SAMPLES * sizeof(uint16_t)
                           + FRAME_FOOTER_LEN];

static uint32_t _build_frame(const uint16_t *samples, uint16_t n,
                              uint8_t channel, uint8_t flags)
{
    uint8_t *p = _frame_buf;

    *p++ = FRAME_SYNC_0;
    *p++ = FRAME_SYNC_1;
    *p++ = FRAME_SYNC_2;
    *p++ = FRAME_SYNC_3;
    *p++ = channel;
    *p++ = flags;
    *p++ = (uint8_t)(n & 0xFFu);
    *p++ = (uint8_t)(n >> 8);

    memcpy(p, samples, n * sizeof(uint16_t));
    p += n * sizeof(uint16_t);

    uint16_t crc = crc16(_frame_buf, (size_t)(p - _frame_buf));
    *p++ = (uint8_t)(crc & 0xFFu);
    *p++ = (uint8_t)(crc >> 8);

    return (uint32_t)(p - _frame_buf);
}

/* ─── Command parser ───────────────────────────────────────────────────────── */

typedef struct {
    AdcDma  *adc;
    bool     streaming;
    uint16_t n_samples;   /* samples requested per frame (≤ DMA_HALF_SAMPLES) */
    uint8_t  cmd_buf[6];  /* longest command is CMD_SET_RATE (1 + 4 bytes) */
    uint8_t  cmd_len;
    uint8_t  cmd_expected;
} State;

static void _send_pong(void)
{
    uint8_t pong[5];
    pong[0] = PONG_BYTE;
    uint32_t v = FIRMWARE_VERSION;
    memcpy(&pong[1], &v, 4);
    tud_cdc_write(pong, sizeof(pong));
    tud_cdc_write_flush();
}

static void _dispatch_command(State *st)
{
    uint8_t cmd = st->cmd_buf[0];
    switch (cmd) {
    case CMD_START:
        if (!st->streaming) {
            adc_dma_start(st->adc);
            st->streaming = true;
        }
        break;

    case CMD_STOP:
        if (st->streaming) {
            adc_dma_stop(st->adc);
            st->streaming = false;
        }
        break;

    case CMD_SET_RATE: {
        uint32_t div;
        memcpy(&div, &st->cmd_buf[1], 4);
        if (div < MIN_CLKDIV) div = MIN_CLKDIV;
        if (div > MAX_CLKDIV) div = MAX_CLKDIV;
        adc_dma_set_clkdiv(st->adc, div);
        break;
    }

    case CMD_SET_CHANNEL:
        adc_dma_set_channel(st->adc, st->cmd_buf[1] & 0x03u);
        break;

    case CMD_SET_SAMPLES: {
        uint16_t n;
        memcpy(&n, &st->cmd_buf[1], 2);
        if (n < 1)                n = 1;
        if (n > DMA_HALF_SAMPLES) n = DMA_HALF_SAMPLES;
        st->n_samples = n;
        break;
    }

    case CMD_PING:
        _send_pong();
        break;

    default:
        break;
    }
}

/* Return the number of payload bytes expected after a given command byte */
static uint8_t _payload_len(uint8_t cmd)
{
    switch (cmd) {
    case CMD_SET_RATE:    return 4;
    case CMD_SET_CHANNEL: return 1;
    case CMD_SET_SAMPLES: return 2;
    default:              return 0;
    }
}

static void _process_usb_input(State *st)
{
    if (!tud_cdc_available()) return;

    uint8_t byte;
    while (tud_cdc_read(&byte, 1) == 1) {
        if (st->cmd_len == 0) {
            /* First byte of a new command */
            st->cmd_buf[0]    = byte;
            st->cmd_len       = 1;
            st->cmd_expected  = 1 + _payload_len(byte);
        } else {
            st->cmd_buf[st->cmd_len++] = byte;
        }

        if (st->cmd_len >= st->cmd_expected) {
            _dispatch_command(st);
            st->cmd_len      = 0;
            st->cmd_expected = 0;
        }
    }
}

/* ─── Main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    stdio_init_all();

    /* Onboard LED shows streaming status */
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    static AdcDma adc;
    adc_dma_init(&adc, DEFAULT_CHANNEL, DEFAULT_CLKDIV);

    tusb_init();

    State st = {
        .adc          = &adc,
        .streaming    = false,
        .n_samples    = DEFAULT_N_SAMPLES,
        .cmd_len      = 0,
        .cmd_expected = 0,
    };

    for (;;) {
        tud_task();
        _process_usb_input(&st);

        if (!st.streaming || adc.ready_half < 0) continue;

        /* Snapshot and clear the ready flag atomically */
        int half           = adc.ready_half;
        adc.ready_half     = -1;
        bool overflow      = adc.overflow;
        adc.overflow       = false;

        uint16_t n = st.n_samples;
        if (n > DMA_HALF_SAMPLES) n = DMA_HALF_SAMPLES;

        uint8_t flags = overflow ? FRAME_FLAG_OVERFLOW : 0u;
        uint32_t frame_len = _build_frame(adc.buf[half], n, adc.channel, flags);

        /* Stream only when host is connected and CDC line is open */
        if (tud_cdc_connected()) {
            tud_cdc_write(_frame_buf, frame_len);
            tud_cdc_write_flush();
        }

        gpio_put(PICO_DEFAULT_LED_PIN, st.streaming);
    }
}
