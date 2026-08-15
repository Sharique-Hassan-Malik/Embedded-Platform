#pragma once
#include <Arduino.h>

// Target: Arduino Uno / Nano (ATmega328P, 16 MHz).
//
// Hardware overview:
//   Shunt resistor (0.1 Ω, 1 %)   — in series with DUT supply rail
//   INA219 current-sense amplifier — I2C (SDA=A4, SCL=A5) on 0x40
//   Annotation GPIO (optional)     — DUT drives these pins HIGH to mark
//                                    code section boundaries in firmware
//   Status LED                     → D13 (flashes during capture)
//
// Alternatively the shunt voltage can be read via the on-chip ADC directly
// if an INA219 is not available; set USE_INA219 to 0 and wire the shunt's
// low side to AIN_SHUNT after a voltage divider to keep it within 0–5 V.

// ── INA219 vs direct ADC ──────────────────────────────────────────────────────
// 1 = use INA219 over I2C (recommended: 12-bit, 0–3.2 A range, calibrated)
// 0 = use ATmega ADC directly on AIN_SHUNT (simpler wiring, 10-bit only)
#define USE_INA219 1

// ── INA219 (when USE_INA219 = 1) ─────────────────────────────────────────────
static constexpr uint8_t  INA219_ADDR      = 0x40;
// Shunt resistor value in milli-ohms.
static constexpr uint16_t SHUNT_MOHM       = 100;     // 0.1 Ω
// Full-scale shunt voltage range: 0 = ±40 mV, 1 = ±80 mV, 2 = ±160 mV,
//                                  3 = ±320 mV. Choose the smallest that
//                                  covers your DUT's peak current.
static constexpr uint8_t  INA219_PG        = 0;       // ±40 mV → 400 mA FS
// ADC resolution: 0=9-bit, 1=10-bit, 2=11-bit, 3=12-bit (default).
static constexpr uint8_t  INA219_BADC      = 3;
static constexpr uint8_t  INA219_SADC      = 3;

// ── Direct ADC (when USE_INA219 = 0) ─────────────────────────────────────────
static constexpr uint8_t  AIN_SHUNT        = A0;      // shunt voltage input
static constexpr float    ADC_VREF         = 5.0f;    // V
static constexpr float    ADC_GAIN         = 10.0f;   // op-amp gain on shunt signal
// Current (mA) = ADC_voltage / (SHUNT_MOHM/1000 * ADC_GAIN) * 1000

// ── Annotation GPIO inputs ────────────────────────────────────────────────────
// The DUT (device under test) drives these pins to mark section boundaries.
// A rising edge on ANN_PIN[i] opens section i; falling edge closes it.
// Up to 4 independent annotation channels.
static constexpr uint8_t ANN_PIN_COUNT    = 4;
static constexpr uint8_t ANN_PINS[4]      = {2, 3, 4, 5};  // D2–D5

// ── Sampling ──────────────────────────────────────────────────────────────────
// Timer1 CTC drives sampling at a fixed rate.
// Maximum reliable rate with INA219 over I2C at 400 kHz: ~1 000 S/s.
// Maximum with direct ADC: ~9 600 S/s.
// SAMPLE_RATE_HZ must be a divisor of F_CPU / prescaler that fits in OCR1A.
static constexpr uint16_t SAMPLE_RATE_HZ   = 1000;    // samples per second
// Ring buffer depth (number of samples before the host must drain).
// At 1000 S/s, 128 samples is 128 ms of buffering — which is what fits. The
// host reads continuously at 1 Mbaud, so the buffer only has to cover a
// scheduling hiccup, not a disconnected host.
// Must be a power of two, and must *fit*. At 2048 this was 10 kB of ring
// buffer on a part with 2 kB of RAM — the sketch reported 520% of dynamic
// memory and could never have run on the Uno it is written for. The static
// assertion below is the part that matters: the overflow was silent because
// nothing was checking, and the linker error it eventually produced named a
// section rather than the buffer.
static constexpr uint16_t RING_BUF_SIZE    = 128;     // must be a power of 2

// Leave room for the serial buffers, the stack and everything else. The Uno
// has 2048 bytes; half of that for samples is already generous.
static constexpr uint16_t RING_BUF_BUDGET  = 1024;

// ── Serial protocol ───────────────────────────────────────────────────────────
static constexpr uint32_t BAUD_RATE        = 1000000; // 1 Mbaud for low latency
// Wire packet: 5 bytes per sample
//   byte 0: 'S' (sample marker)
//   bytes 1–2: current in 0.1 mA units, uint16_t little-endian (0–65535 → 0–6553.5 mA)
//   byte 3: annotation bitmask (bits 0–3 = ANN_PINS state)
//   byte 4: checksum = 0xFF ^ byte1 ^ byte2 ^ byte3
static constexpr uint8_t  PKT_SAMPLE       = 'S';
static constexpr uint8_t  PKT_START        = '<';     // host → fw: start capture
static constexpr uint8_t  PKT_STOP         = '>';     // host → fw: stop capture
static constexpr uint8_t  PKT_RATE         = 'R';     // fw → host: sends sample rate
static constexpr uint8_t  PKT_IDENT        = 'I';     // host → fw: identify

static constexpr uint8_t  STATUS_PIN       = 13;
