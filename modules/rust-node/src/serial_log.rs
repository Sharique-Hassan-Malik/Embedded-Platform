// src/serial_log.rs
//
// Formats and writes sensor readings to the ATmega4809 hardware UART0
// at 9600 baud. Uses avr-hal's `Usart` abstraction and ufmt for no_std
// integer formatting (ufmt avoids the heavy `core::fmt` machinery).
//
// Output format (one line per reading):
//   "READ ok   T=23.4 H=65.2\r\n"
//   "READ err  checksum\r\n"
//   "READ err  no_response\r\n"

use arduino_hal::hal::usart::Usart0;
use embedded_hal::serial::Write as _;
use arduino_hal::pac::USART0;
use crate::dht22::{DhtError, Reading};

pub type Serial = arduino_hal::Usart<USART0, arduino_hal::port::Pin<arduino_hal::port::mode::Input, arduino_hal::hal::port::PD0>, arduino_hal::port::Pin<arduino_hal::port::mode::Output, arduino_hal::hal::port::PD1>>;

/// Write a complete sensor reading line to the serial port.
/// Uses only byte-level writes — no heap, no format! macro.
pub fn log_reading(serial: &mut Serial, result: &Result<Reading, DhtError>) {
    match result {
        Ok(r) => {
            write_str(serial, "READ ok   T=");
            write_fixed1(serial, r.temperature_x10 as u16,
                         r.temperature_x10 < 0);
            write_str(serial, " H=");
            write_fixed1(serial, r.humidity_x10, false);
            write_str(serial, "\r\n");
        }
        Err(DhtError::ChecksumError) => {
            write_str(serial, "READ err  checksum\r\n");
        }
        Err(DhtError::NoResponse) => {
            write_str(serial, "READ err  no_response\r\n");
        }
    }
}

fn write_str(serial: &mut Serial, s: &str) {
    for b in s.bytes() {
        nb::block!(serial.write(b)).ok();
    }
}

fn write_fixed1(serial: &mut Serial, v_x10: u16, negative: bool) {
    if negative {
        nb::block!(serial.write(b'-')).ok();
    }
    let int_part  = v_x10 / 10;
    let frac_part = v_x10 % 10;
    write_u16(serial, int_part);
    nb::block!(serial.write(b'.')).ok();
    nb::block!(serial.write(b'0' + frac_part as u8)).ok();
}

fn write_u16(serial: &mut Serial, mut v: u16) {
    let mut buf = [0u8; 5];
    let mut i = 4usize;
    loop {
        buf[i] = b'0' + (v % 10) as u8;
        v /= 10;
        if v == 0 { break; }
        i -= 1;
    }
    for &b in &buf[i..] {
        nb::block!(serial.write(b)).ok();
    }
}
