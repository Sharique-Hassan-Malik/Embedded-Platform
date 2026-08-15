// src/main.rs
//
// Rust embedded sensor node — ATmega328P (Arduino Nano), no_std, no HAL framework.
//
// Reads temperature and humidity from a DHT22 sensor every 2 seconds,
// displays results on a 128×64 SSD1306 OLED over I2C and logs readings
// to the UART at 9600 baud.
//
// Peripheral map:
//   I2C (TWI):   SDA = PC4 (Arduino A4), SCL = PC5 (Arduino A5)
//   DHT22 DATA:  PD2 (Arduino D2) — single-wire, requires 10 kΩ pull-up to 3.3V
//   UART0 TX:    PD1 (Arduino TX/D1) at 9600 baud
//   Status LED:  PB5 (Arduino D13, LED_BUILTIN) — blinks on each successful read
//
// All peripheral configuration is done through avr-hal; no unsafe register
// manipulation is needed in application code.

#![no_std]
#![no_main]

use panic_halt as _;  // halt on panic — no serial output on panic

use arduino_hal::prelude::*;

mod dht22;
mod display;
mod serial_log;

use dht22::{DhtError, Reading};

/// Minimum delay between DHT22 reads per the datasheet (2 s).
const READ_INTERVAL_MS: u16 = 2000;

#[arduino_hal::entry]
fn main() -> ! {
    // ── Peripheral initialisation ─────────────────────────────────────────────
    let dp   = arduino_hal::Peripherals::take().unwrap();
    let pins = arduino_hal::pins!(dp);

    // UART0 at 9600 baud (Arduino TX/RX header).
    let mut serial = arduino_hal::default_serial!(dp, pins, 9600);

    // I2C (TWI0) at 400 kHz for the SSD1306 OLED.
    let i2c = arduino_hal::I2c::new(
        dp.TWI,
        pins.a4.into_pull_up_input(),   // SDA
        pins.a5.into_pull_up_input(),   // SCL
        400_000,
    );

    // SSD1306 OLED display.
    let mut display = display::init_display(i2c);

    // Status LED.
    let mut led = pins.d13.into_output();

    // Delay provider — avr-hal wraps the hardware delay loop.
    let mut delay = arduino_hal::Delay::new();

    // DHT22 data pin — must switch between output (start signal) and input
    // (reading bits). avr-hal provides `into_output()` / `into_floating_input()`.
    // We hold both pin-mode variants and switch between them as needed.
    //
    // DHT22 single-wire protocol requires toggling the pin direction:
    //   1. Drive low (output) for the start signal.
    //   2. Release (input, rely on external 10 kΩ pull-up) to receive.
    // Owned, mutable output pin; each loop iteration drives the start signal,
    // switches it to input to read, then switches it back to output.
    let mut dht_pin = pins.d2.into_output();

    // ── State ─────────────────────────────────────────────────────────────────
    let mut last_reading:  Option<Reading> = None;
    let mut read_count:    u32             = 0;
    let mut error_count:   u32             = 0;

    // Show a startup splash while the DHT22 stabilises (≥ 1 s after power-on).
    display::render(&mut display, None, 0, 0, "Initialising...");
    arduino_hal::delay_ms(1500_u16);

    // ── Main loop ─────────────────────────────────────────────────────────────
    loop {
        // Start signal: drive the line low ~1 ms, then release it.
        dht_pin.set_low();
        arduino_hal::delay_ms(1_u16);
        dht_pin.set_high();
        arduino_hal::delay_us(30_u32);

        // Switch the same pin to input and read the sensor's response.
        let mut dht_in = dht_pin.into_floating_input();
        let result = dht22::read_response(&mut dht_in, &mut delay);
        // Restore the pin to output for the next iteration.
        dht_pin = dht_in.into_output();

        read_count += 1;

        let status = match &result {
            Ok(r) => {
                last_reading = Some(*r);
                led.set_high();
                arduino_hal::delay_ms(50_u16);
                led.set_low();
                "OK"
            }
            Err(DhtError::ChecksumError) => {
                error_count += 1;
                "err: checksum"
            }
            Err(DhtError::NoResponse) => {
                error_count += 1;
                "err: no resp"
            }
        };

        // Log to UART.
        serial_log::log_reading(&mut serial, &result);

        // Update OLED.
        display::render(
            &mut display,
            last_reading,
            read_count,
            error_count,
            status,
        );

        // Wait before next read (DHT22 minimum sampling period = 2 s).
        arduino_hal::delay_ms(READ_INTERVAL_MS);
    }
}
