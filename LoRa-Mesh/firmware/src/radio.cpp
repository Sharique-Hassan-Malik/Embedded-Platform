#include "radio.h"

#include <Arduino.h>
#include <SPI.h>

/* ── SX1276 register map (LoRa mode, page numbers from DS Rev 7) ────────── */
#define REG_FIFO                0x00u
#define REG_OP_MODE             0x01u
#define REG_FR_MSB              0x06u
#define REG_FR_MID              0x07u
#define REG_FR_LSB              0x08u
#define REG_PA_CONFIG           0x09u
#define REG_PA_RAMP             0x0Au
#define REG_OCP                 0x0Bu
#define REG_LNA                 0x0Cu
#define REG_FIFO_ADDR_PTR       0x0Du
#define REG_FIFO_TX_BASE_ADDR   0x0Eu
#define REG_FIFO_RX_BASE_ADDR   0x0Fu
#define REG_FIFO_RX_CURRENT     0x10u
#define REG_IRQ_FLAGS_MASK      0x11u
#define REG_IRQ_FLAGS           0x12u
#define REG_RX_NB_BYTES         0x13u
#define REG_PKT_SNR_VALUE       0x19u
#define REG_PKT_RSSI_VALUE      0x1Au
#define REG_RSSI_VALUE          0x1Bu
#define REG_MODEM_CONFIG1       0x1Du
#define REG_MODEM_CONFIG2       0x1Eu
#define REG_PREAMBLE_MSB        0x20u
#define REG_PREAMBLE_LSB        0x21u
#define REG_PAYLOAD_LENGTH      0x22u
#define REG_MODEM_CONFIG3       0x26u
#define REG_DIO_MAPPING1        0x40u
#define REG_VERSION             0x42u
#define REG_PA_DAC              0x4Du

/* RegOpMode modes (LoRa bit set: 0x80) */
#define MODE_SLEEP      0x80u
#define MODE_STANDBY    0x81u
#define MODE_TX         0x83u
#define MODE_RX_CONT    0x85u

/* IRQ flag bits */
#define IRQ_TX_DONE     (1u << 3)
#define IRQ_RX_DONE     (1u << 6)
#define IRQ_CRC_ERROR   (1u << 5)
#define IRQ_PAYLOAD_CRC (1u << 5)

#define EXPECTED_VERSION 0x12u
#define TX_TIMEOUT_MS    3000u

/* ── SPI helpers ─────────────────────────────────────────────────────────── */

static void _select(void)   { digitalWrite(RADIO_PIN_NSS, LOW);  }
static void _deselect(void) { digitalWrite(RADIO_PIN_NSS, HIGH); }

static uint8_t _read_reg(uint8_t reg)
{
    _select();
    SPI.transfer(reg & 0x7Fu);
    uint8_t val = SPI.transfer(0x00);
    _deselect();
    return val;
}

static void _write_reg(uint8_t reg, uint8_t val)
{
    _select();
    SPI.transfer(reg | 0x80u);
    SPI.transfer(val);
    _deselect();
}

static void _write_fifo(const uint8_t *buf, uint8_t len)
{
    _select();
    SPI.transfer(REG_FIFO | 0x80u);
    for (uint8_t i = 0; i < len; i++) SPI.transfer(buf[i]);
    _deselect();
}

static void _read_fifo(uint8_t *buf, uint8_t len)
{
    _select();
    SPI.transfer(REG_FIFO & 0x7Fu);
    for (uint8_t i = 0; i < len; i++) buf[i] = SPI.transfer(0x00);
    _deselect();
}

static void _set_mode(uint8_t mode)
{
    _write_reg(REG_OP_MODE, mode);
}

/* ── radio_init ──────────────────────────────────────────────────────────── */

RadioResult radio_init(void)
{
    pinMode(RADIO_PIN_NSS,  OUTPUT);
    pinMode(RADIO_PIN_RST,  OUTPUT);
    pinMode(RADIO_PIN_DIO0, INPUT);

    _deselect();

    SPI.begin();
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

    /* Hardware reset */
    digitalWrite(RADIO_PIN_RST, LOW);
    delay(10);
    digitalWrite(RADIO_PIN_RST, HIGH);
    delay(10);

    /* Verify chip identity */
    if (_read_reg(REG_VERSION) != EXPECTED_VERSION) return RADIO_ERR;

    /* Enter sleep to allow LoRa mode bit to be written */
    _set_mode(MODE_SLEEP);
    delay(10);

    /* LoRa mode + sleep */
    _write_reg(REG_OP_MODE, 0x80u);

    /* FIFO base addresses */
    _write_reg(REG_FIFO_TX_BASE_ADDR, 0x00u);
    _write_reg(REG_FIFO_RX_BASE_ADDR, 0x00u);

    /* LNA: max gain, boost on */
    _write_reg(REG_LNA, 0x23u);

    /* Modem config: BW | CR 4/5 | explicit header */
    _write_reg(REG_MODEM_CONFIG1, (uint8_t)((RADIO_BW << 4) | (0x1u << 1) | 0x0u));

    /* SF | CRC enable | RX timeout MSB */
    _write_reg(REG_MODEM_CONFIG2, (uint8_t)((RADIO_SF << 4) | (1u << 2)));

    /* LNA AGC auto */
    _write_reg(REG_MODEM_CONFIG3, 0x04u);

    /* Preamble length: 8 symbols */
    _write_reg(REG_PREAMBLE_MSB, 0x00u);
    _write_reg(REG_PREAMBLE_LSB, 0x08u);

    /* PA_BOOST pin, max power bits */
    _write_reg(REG_PA_CONFIG, (uint8_t)(0x80u | 0x70u | (uint8_t)(RADIO_TX_POWER - 2)));

    /* PA_DAC: high power mode for +20 dBm if needed */
    _write_reg(REG_PA_DAC, (RADIO_TX_POWER == 20) ? 0x87u : 0x84u);

    /* OCP: 120 mA */
    _write_reg(REG_OCP, 0x2Bu);

    /* DIO0 → RxDone in RX, TxDone in TX */
    _write_reg(REG_DIO_MAPPING1, 0x00u);

    radio_set_freq(RADIO_FREQ_HZ);

    _set_mode(MODE_STANDBY);
    return RADIO_OK;
}

/* ── radio_set_freq ──────────────────────────────────────────────────────── */

void radio_set_freq(uint32_t freq_hz)
{
    /* Frf = freq_hz / Fstep, Fstep = 32e6 / 2^19 ≈ 61.035 Hz */
    uint64_t frf = ((uint64_t)freq_hz << 19) / 32000000UL;
    _set_mode(MODE_STANDBY);
    _write_reg(REG_FR_MSB, (uint8_t)(frf >> 16));
    _write_reg(REG_FR_MID, (uint8_t)(frf >>  8));
    _write_reg(REG_FR_LSB, (uint8_t)(frf));
}

/* ── radio_set_sf ────────────────────────────────────────────────────────── */

void radio_set_sf(uint8_t sf)
{
    uint8_t cfg2 = _read_reg(REG_MODEM_CONFIG2) & 0x0Fu;
    _write_reg(REG_MODEM_CONFIG2, cfg2 | (uint8_t)(sf << 4));
    /* SF6 requires implicit header mode — not needed for our use case */
}

/* ── radio_send ──────────────────────────────────────────────────────────── */

RadioResult radio_send(const uint8_t *buf, uint8_t len)
{
    _set_mode(MODE_STANDBY);

    /* Reset FIFO pointer to TX base */
    _write_reg(REG_FIFO_ADDR_PTR, 0x00u);
    _write_reg(REG_PAYLOAD_LENGTH, len);

    _write_fifo(buf, len);

    /* Clear IRQ flags */
    _write_reg(REG_IRQ_FLAGS, 0xFFu);

    /* DIO0 → TxDone */
    _write_reg(REG_DIO_MAPPING1, 0x40u);

    _set_mode(MODE_TX);

    /* Wait for TxDone on DIO0 or timeout */
    uint32_t deadline = millis() + TX_TIMEOUT_MS;
    while (!digitalRead(RADIO_PIN_DIO0)) {
        if (millis() > deadline) {
            _set_mode(MODE_STANDBY);
            return RADIO_TIMEOUT;
        }
    }

    _write_reg(REG_IRQ_FLAGS, IRQ_TX_DONE);
    _set_mode(MODE_STANDBY);
    return RADIO_OK;
}

/* ── radio_start_rx ──────────────────────────────────────────────────────── */

void radio_start_rx(void)
{
    _set_mode(MODE_STANDBY);
    _write_reg(REG_FIFO_RX_BASE_ADDR, 0x00u);
    _write_reg(REG_FIFO_ADDR_PTR, 0x00u);
    _write_reg(REG_IRQ_FLAGS, 0xFFu);
    /* DIO0 → RxDone */
    _write_reg(REG_DIO_MAPPING1, 0x00u);
    _set_mode(MODE_RX_CONT);
}

/* ── radio_packet_available ──────────────────────────────────────────────── */

bool radio_packet_available(void)
{
    return digitalRead(RADIO_PIN_DIO0) == HIGH;
}

/* ── radio_recv ──────────────────────────────────────────────────────────── */

RadioResult radio_recv(uint8_t *buf, uint8_t buf_len, RadioRxMeta *meta)
{
    uint8_t irq = _read_reg(REG_IRQ_FLAGS);

    if (!(irq & IRQ_RX_DONE)) return RADIO_EMPTY;

    /* Clear all IRQ flags */
    _write_reg(REG_IRQ_FLAGS, 0xFFu);

    if (irq & IRQ_CRC_ERROR) return RADIO_CRC;

    uint8_t nb  = _read_reg(REG_RX_NB_BYTES);
    uint8_t ptr = _read_reg(REG_FIFO_RX_CURRENT);

    uint8_t read_len = (nb < buf_len) ? nb : buf_len;

    _write_reg(REG_FIFO_ADDR_PTR, ptr);
    _read_fifo(buf, read_len);

    /* RSSI: dBm = value - 157 (for HF port, 915 / 868 MHz) */
    int16_t rssi_raw = _read_reg(REG_PKT_RSSI_VALUE);
    int8_t  snr      = (int8_t)_read_reg(REG_PKT_SNR_VALUE);

    meta->rssi = (int16_t)(rssi_raw - 157);
    if (snr < 0) meta->rssi += snr / 4;
    meta->snr  = snr;
    meta->len  = read_len;

    return RADIO_OK;
}

/* ── radio_channel_rssi ──────────────────────────────────────────────────── */

int16_t radio_channel_rssi(void)
{
    return (int16_t)(_read_reg(REG_RSSI_VALUE) - 157);
}
