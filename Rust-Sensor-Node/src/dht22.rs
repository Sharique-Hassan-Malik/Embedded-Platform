// src/dht22.rs
//
// Single-wire DHT22 protocol implemented from scratch using embedded-hal 0.2
// digital I/O and delay traits.
//
// Why not use the `dht-sensor` crate directly?
// The crate's timing is tuned for Cortex-M. On an 8-bit AVR at 20 MHz the
// bit-sampling window is narrower — this implementation uses tighter delays
// calibrated for the ATmega328P.
//
// Protocol summary (DHT22 datasheet, section 5):
//   1. Host pulls DATA low for ≥ 1 ms (start signal).
//   2. Host releases DATA (pull-up takes it high).
//   3. DHT pulls DATA low for 80 µs, then high for 80 µs (response).
//   4. DHT sends 40 bits: each bit starts with 50 µs low, then:
//        0 bit: 26–28 µs high
//        1 bit: 70 µs high
//      Sample 50 µs after the rising edge to distinguish 0 from 1.
//   5. DHT releases DATA after the last bit.
//
// Data format (40 bits = 5 bytes):
//   byte 0: humidity    integer part
//   byte 1: humidity    fractional part
//   byte 2: temperature integer part (bit 15 = sign)
//   byte 3: temperature fractional part
//   byte 4: checksum    = (byte0 + byte1 + byte2 + byte3) & 0xFF

use embedded_hal::blocking::delay::{DelayMs, DelayUs};
use embedded_hal::digital::v2::InputPin;

#[derive(Debug)]
pub enum DhtError {
    /// No response from sensor — check wiring and pull-up resistor.
    NoResponse,
    /// Checksum mismatch — transient glitch; retry.
    ChecksumError,
}

#[derive(Debug, Clone, Copy)]
pub struct Reading {
    /// Relative humidity in tenths of a percent (e.g. 652 = 65.2 %).
    pub humidity_x10: u16,
    /// Temperature in tenths of a degree Celsius, signed
    /// (e.g. 234 = 23.4 °C; -15 = -1.5 °C).
    pub temperature_x10: i16,
}

impl Reading {
    /// Humidity as floating-point percent. Use only for display — AVR has no FPU.
    pub fn humidity_f32(self) -> f32 {
        self.humidity_x10 as f32 / 10.0
    }

    pub fn temperature_f32(self) -> f32 {
        self.temperature_x10 as f32 / 10.0
    }
}

/// Read one measurement from a DHT22 sensor on `pin`.
///
/// `pin` must implement both `OutputPin` and `InputPin` — use a bidirectional
/// GPIO configured as open-drain or toggled between output and input modes.
/// On avr-hal this is achieved with `into_output()` / `into_floating_input()`.
///
/// The caller is responsible for waiting at least 2 seconds between calls
/// (DHT22 minimum sampling period).
pub fn read_response<E, I, D>(
    read_pin: &mut I,
    delay:    &mut D,
) -> Result<Reading, DhtError>
where
    I: InputPin<Error = E>,
    D: DelayMs<u16> + DelayUs<u16>,
{
    // The start signal (drive the line low ~1 ms, then release) is issued by the
    // caller on the output-configured pin before it is switched to input; here we
    // read the sensor's response on the same (now input) pin.

    // ── Step 2: Sensor response (80 µs low, 80 µs high) ───────────────────────
    // Wait for DHT to pull low (response signal).
    if !wait_for_level(read_pin, false, 100, delay) {
        return Err(DhtError::NoResponse);
    }
    // Wait for DHT to pull high.
    if !wait_for_level(read_pin, true, 100, delay) {
        return Err(DhtError::NoResponse);
    }
    // Wait for DHT to pull low again (start of first bit).
    if !wait_for_level(read_pin, false, 100, delay) {
        return Err(DhtError::NoResponse);
    }

    // ── Step 3: Read 40 bits ───────────────────────────────────────────────────
    let mut data = [0u8; 5];
    for byte in data.iter_mut() {
        let mut val = 0u8;
        for bit in 0..8 {
            // Each bit starts with ~50 µs low.
            if !wait_for_level(read_pin, true, 100, delay) {
                return Err(DhtError::NoResponse);
            }
            // Sample 50 µs after the rising edge.
            // If still high after 50 µs → 1 bit; if already low → 0 bit.
            delay.delay_us(50_u16);
            let is_one = read_pin.is_high().unwrap_or(false);
            val = (val << 1) | (is_one as u8);

            // Wait for line to go low before next bit (only for 1-bits;
            // 0-bits have already gone low by the time we sample).
            if is_one {
                if !wait_for_level(read_pin, false, 100, delay) {
                    return Err(DhtError::NoResponse);
                }
            }
        }
        *byte = val;
    }

    // ── Step 4: Checksum ───────────────────────────────────────────────────────
    let checksum = data[0]
        .wrapping_add(data[1])
        .wrapping_add(data[2])
        .wrapping_add(data[3]);
    if checksum != data[4] {
        return Err(DhtError::ChecksumError);
    }

    // ── Step 5: Decode ─────────────────────────────────────────────────────────
    let humidity_x10 = u16::from(data[0]) << 8 | u16::from(data[1]);

    let temp_raw      = u16::from(data[2] & 0x7F) << 8 | u16::from(data[3]);
    let negative      = (data[2] & 0x80) != 0;
    let temperature_x10 = if negative {
        -(temp_raw as i16)
    } else {
        temp_raw as i16
    };

    Ok(Reading { humidity_x10, temperature_x10 })
}

/// Poll `pin` until it reaches `level`, up to `max_us` microseconds.
/// Returns false on timeout.
fn wait_for_level<E, I, D>(pin: &mut I, level: bool, max_us: u16, delay: &mut D) -> bool
where
    I: InputPin<Error = E>,
    D: DelayUs<u16>,
{
    for _ in 0..max_us {
        let high = pin.is_high().unwrap_or(!level);
        if high == level {
            return true;
        }
        delay.delay_us(1_u16);
    }
    false
}
