/*
 * radio.h — SX1276 LoRa driver (no RadioHead, no LoRa.h).
 *
 * Wiring (Arduino Uno/Nano):
 *   NSS  → D10    SCK  → D13
 *   MOSI → D11    MISO → D12
 *   RST  → D9     DIO0 → D2   (RxDone / TxDone IRQ)
 *
 * The driver operates in LoRa implicit-header mode for DATA packets
 * and explicit-header mode for HELLO packets so the receiver can
 * determine payload length without prior knowledge.
 *
 * Default RF parameters:
 *   Frequency:       915 MHz  (change RADIO_FREQ_HZ for 868 / 433)
 *   Bandwidth:       125 kHz  (BW_125)
 *   Spreading factor: SF7     (fast, short range — increase for distance)
 *   Coding rate:     4/5
 *   TX power:        +17 dBm  (PA_BOOST pin)
 *   Preamble:        8 symbols
 *   CRC:             enabled
 */

#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stdbool.h>

/* ── Configuration ────────────────────────────────────────────────────────── */
#ifndef RADIO_FREQ_HZ
#define RADIO_FREQ_HZ   915000000UL
#endif

#ifndef RADIO_SF
#define RADIO_SF        7u    /* 6–12 */
#endif

#ifndef RADIO_BW
#define RADIO_BW        7u    /* 7 = 125 kHz; see BW_* constants below */
#endif

#ifndef RADIO_TX_POWER
#define RADIO_TX_POWER  17    /* dBm, 2–17 on PA_BOOST */
#endif

/* Bandwidth register values (RegModemConfig1 bits [7:4]) */
#define BW_7K8    0u
#define BW_10K4   1u
#define BW_15K6   2u
#define BW_20K8   3u
#define BW_31K25  4u
#define BW_41K7   5u
#define BW_62K5   6u
#define BW_125K   7u
#define BW_250K   8u
#define BW_500K   9u

/* ── Pin defaults (override by defining before including) ─────────────────── */
#ifndef RADIO_PIN_NSS
#define RADIO_PIN_NSS  10
#endif
#ifndef RADIO_PIN_RST
#define RADIO_PIN_RST   9
#endif
#ifndef RADIO_PIN_DIO0
#define RADIO_PIN_DIO0  2
#endif

/* ── Results ──────────────────────────────────────────────────────────────── */
typedef enum {
    RADIO_OK      =  0,
    RADIO_ERR     = -1,   /* general error (chip not found, SPI fault)  */
    RADIO_TIMEOUT = -2,   /* TX did not complete within expected window  */
    RADIO_CRC     = -3,   /* packet received with CRC error              */
    RADIO_EMPTY   = -4,   /* no packet waiting in FIFO                   */
} RadioResult;

/* ── Received-packet metadata ─────────────────────────────────────────────── */
typedef struct {
    int16_t rssi;       /* dBm */
    int8_t  snr;        /* dB × 4 (raw register value) */
    uint8_t len;        /* payload bytes written into buf */
} RadioRxMeta;

/* ── API ──────────────────────────────────────────────────────────────────── */

/*
 * radio_init — reset and configure the SX1276.
 * Returns RADIO_OK on success; RADIO_ERR if the chip is not detected.
 */
RadioResult radio_init(void);

/* radio_send — transmit len bytes from buf; blocks until TxDone or timeout. */
RadioResult radio_send(const uint8_t *buf, uint8_t len);

/*
 * radio_recv — copy the next received packet into buf (max buf_len bytes).
 * Returns RADIO_EMPTY if no packet is available; RADIO_CRC on CRC failure.
 * meta is filled on success.
 */
RadioResult radio_recv(uint8_t *buf, uint8_t buf_len, RadioRxMeta *meta);

/* radio_start_rx — put the chip into continuous-receive mode. */
void radio_start_rx(void);

/* radio_packet_available — true if DIO0 is high (RxDone flag set). */
bool radio_packet_available(void);

/* radio_set_freq — change centre frequency at runtime. */
void radio_set_freq(uint32_t freq_hz);

/* radio_set_sf — change spreading factor (6–12). */
void radio_set_sf(uint8_t sf);

/* radio_channel_rssi — instantaneous RSSI of the channel (no packet needed). */
int16_t radio_channel_rssi(void);

#endif /* RADIO_H */
