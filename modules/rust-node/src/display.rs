// src/display.rs
//
// Thin wrapper around the `ssd1306` and `embedded-graphics` crates.
// Provides a small API for rendering sensor readings on the 128×64 OLED.
//
// The ssd1306 crate handles the full SSD1306 initialisation sequence and
// I2C framing; this module handles layout and text rendering only.
//
// Font choice: MonoTextStyle with the built-in 6×8 pixel font.
// At 6×8 the display fits 21 columns × 8 rows — plenty for four data lines.

use embedded_graphics::{
    mono_font::{ascii::FONT_6X9, MonoTextStyleBuilder},
    pixelcolor::BinaryColor,
    prelude::*,
    text::{Baseline, Text},
};
use ssd1306::{mode::BufferedGraphicsMode, prelude::*, I2CDisplayInterface, Ssd1306};

use crate::dht22::Reading;

/// Column layout on the 128×64 display.
/// Each line is 10 px tall (8 px font + 2 px spacing).
const LINE_H: i32 = 10;

pub type OledDisplay<I2C> = Ssd1306<
    I2CInterface<I2C>,
    DisplaySize128x64,
    BufferedGraphicsMode<DisplaySize128x64>,
>;

/// Initialise an SSD1306 display on the provided I2C bus.
///
/// Returns the display in buffered-graphics mode, ready to draw.
pub fn init_display<I2C, E>(i2c: I2C) -> OledDisplay<I2C>
where
    I2C: embedded_hal::blocking::i2c::Write<Error = E>,
{
    let interface = I2CDisplayInterface::new(i2c);
    let mut display = Ssd1306::new(interface, DisplaySize128x64, DisplayRotation::Rotate0)
        .into_buffered_graphics_mode();
    display.init().ok();
    display.clear(BinaryColor::Off).ok();
    display
}

/// Render the current sensor readings and status on the OLED.
///
/// Layout:
///   Line 0:  "Rust Sensor Node"       (header)
///   Line 1:  "──────────────────"     (separator made of dashes)
///   Line 2:  "Temp:  23.4 C"
///   Line 3:  "Hum:   65.2 %"
///   Line 4:  (blank)
///   Line 5:  "Reads: 42"
///   Line 6:  "Errors: 3"
///   Line 7:  status string (e.g. "OK" or "DHT err")
pub fn render<I2C, E>(
    display: &mut OledDisplay<I2C>,
    reading: Option<Reading>,
    read_count: u32,
    error_count: u32,
    status: &str,
) where
    I2C: embedded_hal::blocking::i2c::Write<Error = E>,
{
    let style = MonoTextStyleBuilder::new()
        .font(&FONT_6X9)
        .text_color(BinaryColor::On)
        .build();

    display.clear(BinaryColor::Off).ok();

    let draw_text = |display: &mut OledDisplay<I2C>, row: i32, s: &str| {
        Text::with_baseline(s, Point::new(0, row * LINE_H), style, Baseline::Top)
            .draw(display)
            .ok();
    };

    draw_text(display, 0, "Rust Sensor Node");
    draw_text(display, 1, "----------------");

    // We need fixed-width number formatting without alloc/format!.
    // Use a small stack buffer and write_fmt via core::fmt::Write.
    let mut buf = FmtBuf::new();

    if let Some(r) = reading {
        // Temperature: e.g. "Temp:  23.4 C"
        buf.reset();
        write_temp(&mut buf, r.temperature_x10);
        draw_text(display, 2, buf.as_str());

        // Humidity: e.g. "Hum:   65.2 %"
        buf.reset();
        write_hum(&mut buf, r.humidity_x10);
        draw_text(display, 3, buf.as_str());
    } else {
        draw_text(display, 2, "Temp:  --.- C");
        draw_text(display, 3, "Hum:   --.- %");
    }

    buf.reset();
    buf.write_str("Reads: ");
    write_u32(&mut buf, read_count);
    draw_text(display, 5, buf.as_str());

    buf.reset();
    buf.write_str("Errors:");
    write_u32(&mut buf, error_count);
    draw_text(display, 6, buf.as_str());

    draw_text(display, 7, status);
    display.flush().ok();
}

// ── Stack-allocated format buffer ──────────────────────────────────────────────
// We cannot use `format!` in no_std without alloc.
// A 32-byte stack buffer with a cursor covers all our display strings.
struct FmtBuf {
    buf: [u8; 32],
    pos: usize,
}

impl FmtBuf {
    fn new() -> Self {
        FmtBuf { buf: [b' '; 32], pos: 0 }
    }
    fn reset(&mut self) {
        self.buf = [b' '; 32];
        self.pos = 0;
    }
    fn write_str(&mut self, s: &str) {
        for b in s.bytes() {
            if self.pos < self.buf.len() {
                self.buf[self.pos] = b;
                self.pos += 1;
            }
        }
    }
    fn as_str(&self) -> &str {
        // Safety: we only write valid ASCII bytes.
        core::str::from_utf8(&self.buf[..self.pos]).unwrap_or("?")
    }
}

/// Write an i16 value as "Temp:  XX.X C" into buf.
fn write_temp(buf: &mut FmtBuf, t_x10: i16) {
    buf.write_str("Temp: ");
    if t_x10 < 0 {
        buf.write_str("-");
        write_fixed1(buf, (-t_x10) as u16);
    } else {
        buf.write_str(" ");
        write_fixed1(buf, t_x10 as u16);
    }
    buf.write_str(" C");
}

/// Write a u16 value as "Hum:   XX.X %" into buf.
fn write_hum(buf: &mut FmtBuf, h_x10: u16) {
    buf.write_str("Hum:  ");
    write_fixed1(buf, h_x10);
    buf.write_str(" %");
}

/// Write an unsigned value with one implied decimal place (e.g. 234 → "23.4").
fn write_fixed1(buf: &mut FmtBuf, v: u16) {
    let int_part  = v / 10;
    let frac_part = v % 10;
    write_u16_padded(buf, int_part, 3);
    buf.write_str(".");
    write_digit(buf, frac_part as u8);
}

/// Write u16 right-aligned in `width` characters (space-padded).
fn write_u16_padded(buf: &mut FmtBuf, v: u16, width: usize) {
    let mut tmp = [b' '; 6];
    let mut n = v;
    let mut i = 5usize;
    loop {
        tmp[i] = b'0' + (n % 10) as u8;
        n /= 10;
        if n == 0 { break; }
        i -= 1;
    }
    // Right-align in `width` characters.
    let digits = 6 - i;
    for _ in 0..width.saturating_sub(digits) {
        buf.write_str(" ");
    }
    for &b in &tmp[i..] {
        if buf.pos < buf.buf.len() {
            buf.buf[buf.pos] = b;
            buf.pos += 1;
        }
    }
}

fn write_u32(buf: &mut FmtBuf, v: u32) {
    let mut tmp = [0u8; 10];
    let mut n = v;
    let mut i = 9usize;
    loop {
        tmp[i] = b'0' + (n % 10) as u8;
        n /= 10;
        if n == 0 { break; }
        i -= 1;
    }
    for &b in &tmp[i..] {
        if buf.pos < buf.buf.len() {
            buf.buf[buf.pos] = b;
            buf.pos += 1;
        }
    }
}

fn write_digit(buf: &mut FmtBuf, d: u8) {
    if buf.pos < buf.buf.len() {
        buf.buf[buf.pos] = b'0' + (d % 10);
        buf.pos += 1;
    }
}
