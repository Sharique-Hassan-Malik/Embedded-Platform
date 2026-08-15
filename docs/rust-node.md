# Architecture

## Overview

```
src/main.rs          — #![no_std] #![no_main] entry point
  ├── src/dht22.rs   — DHT22 single-wire driver (embedded-hal traits)
  ├── src/display.rs — SSD1306 layout and no_std text rendering
  └── src/serial_log.rs — UART formatted output (no format!, no alloc)
```

The crate is `no_std` with `no_main`. There is no operating system, no heap
allocator and no runtime support library. The `panic-halt` crate provides a
panic handler that loops forever on `panic!()`.

---

## Memory Layout (ATmega4809)

| Region | Size | Usage |
|---|---|---|
| Flash | 48 KB | Program code + string literals |
| SRAM | 6 KB | Stack, static variables |
| EEPROM | 256 bytes | Not used in this project |

`opt-level = "s"` (optimise for size) is set in all profiles. The final binary
with SSD1306 driver, DHT22 driver and embedded-graphics text rendering is
approximately 16–20 KB of flash and uses under 512 bytes of SRAM at runtime.

---

## Peripheral Map

```
ATmega4809 (Arduino Nano Every)

TWI0 (I2C)
  PA2 (A4) — SDA → SSD1306 OLED
  PA3 (A5) — SCL → SSD1306 OLED

USART0
  PD1 (D1/TX) — UART TX → USB serial converter (on-board CH340)
  9600 baud, 8N1

GPIO
  PF5 (D2) — DHT22 DATA  (10 kΩ pull-up to 3.3 V required)
  PE2 (D13/LED_BUILTIN) — status LED
```

---

## DHT22 Protocol Implementation

The single-wire DHT22 protocol requires bidirectional use of one GPIO pin.
The pin is toggled between `OutputPin` mode (for the start signal) and
`InputPin` mode (for reading sensor bits). This is the correct embedded-hal
approach — the HAL trait objects carry the mode as a type parameter, enforced
at compile time.

```
Host → Sensor (start signal):
  OutputPin: pull low ≥ 1 ms
  OutputPin: release high 30 µs

Sensor → Host (response):
  InputPin: wait for 80 µs low  (DHT pulls down)
  InputPin: wait for 80 µs high (DHT releases)

Sensor → Host (40 data bits):
  Each bit:
    InputPin: wait for ~50 µs low (preamble)
    InputPin: wait for rising edge
    InputPin: delay 50 µs
    InputPin: sample → 0 if already low, 1 if still high
```

### Timing on AVR at 20 MHz

`delay_us(1)` on avr-hal at 20 MHz inserts approximately 20 NOPs.
The `wait_for_level` function polls every 1 µs with a 100 µs timeout.
The 50 µs sample delay places the measurement midway between the 26–28 µs
(0-bit) and 70 µs (1-bit) high pulses, giving a 12 µs margin on each side.

### Error Handling

Two error variants are returned as `Result<Reading, DhtError>`:
- `NoResponse` — timeout waiting for the sensor; check pull-up and wiring.
- `ChecksumError` — data received but checksum failed; retry on next cycle.

The main loop counts both error types separately and displays the total on
the OLED. The last successful reading is retained and displayed until a new
valid reading replaces it.

---

## No-alloc Text Formatting

`core::fmt::write` (used by `format!`) pulls in significant code size for
floating-point formatting and may require the `alloc` crate. Both are
incompatible with strict no_std on AVR without careful feature flags.

This project uses a 32-byte stack-allocated `FmtBuf` struct in `display.rs`
and writes formatted numbers with hand-rolled integer arithmetic:

```
234 → int_part = 23, frac_part = 4 → "23.4"
```

The same approach is used in `serial_log.rs`. No heap allocation, no
`format!`, no `ToString`. Binary size is approximately 2 KB smaller than
a `ufmt`-based equivalent.

---

## Embedded-hal Trait Boundaries

All peripheral interactions are expressed through `embedded-hal 0.2` traits:

| Trait | Used by |
|---|---|
| `OutputPin` | DHT22 start signal |
| `InputPin` | DHT22 bit sampling |
| `DelayMs<u16>` | DHT22 start pulse, inter-read delay |
| `DelayUs<u16>` | DHT22 bit timing |
| `blocking::i2c::Write` | SSD1306 over TWI0 |

This means `dht22.rs` and `display.rs` are not coupled to avr-hal — they
compile against any HAL that implements the same traits (STM32, RP2040, etc.).

---

## Comparison with C++ Equivalent

| Concern | Arduino C++ | This project (Rust no_std) |
|---|---|---|
| Undefined behaviour on overflow | Silent wrap (UB in C++) | Checked in debug; wrapping explicit in release |
| Buffer overrun | No bounds check | Panics (halt) on slice OOB access |
| Null pointer dereference | Possible | No raw pointers in application code |
| Pin direction errors | Runtime bug | Compile-time type error |
| Stack overflow | Silent memory corruption | Still possible (no MMU on AVR) |
| Binary size | ~6 KB (Arduino blink) | ~16 KB with OLED + DHT22 + serial |
| Flash usage | Smaller for tiny sketches | ~10 KB overhead for Rust runtime stubs |

The main safety gains are compile-time enforcement of pin modes (OutputPin
vs InputPin) and elimination of C-style implicit conversions and pointer
arithmetic from application code.
