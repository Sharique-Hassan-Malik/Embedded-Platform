# Embedded Rust Sensor Node

A `no_std` Rust firmware for the Arduino Nano (ATmega328P) that reads
temperature and humidity from a DHT22 sensor, displays readings on a 128×64
SSD1306 OLED over I2C and logs data over UART. The project demonstrates the
Rust embedded toolchain — avr-hal, embedded-hal traits and the no_std
compilation model — and directly compares binary size and safety properties
against an equivalent Arduino C++ implementation.

---

## The Hard Part

**`no_std` on an 8-bit AVR.** There is no heap, no operating system and no
precompiled standard library for the AVR target — `core` must be compiled from
source via `-Z build-std`. This rules out `format!`, `String`, `Vec` and the
entire `std` ecosystem. All text formatting in this project is done with a
32-byte stack-allocated `FmtBuf` and hand-rolled integer-to-ASCII conversion,
avoiding both `alloc` and the `ufmt` crate's overhead.

**DHT22 single-wire protocol with embedded-hal type-state pins.** The DHT22
protocol requires switching one GPIO pin between output mode (start signal)
and input mode (bit sampling) mid-transfer. Rust's type-state pattern encodes
pin direction in the type — `OutputPin` and `InputPin` are distinct types.
The driver in `dht22.rs` takes both types as generic parameters constrained
by embedded-hal traits, making a pin-direction mistake a compile error rather
than a runtime bug.

**Timing on a non-preemptive 8-bit MCU.** The DHT22 bit window is 26–70 µs
wide. At 20 MHz each `delay_us(1)` is approximately 20 instructions. The
driver samples 50 µs after each rising edge, placing it in the centre of
the decision window with 12 µs margin for either bit value. The timing is
documented explicitly in `ARCHITECTURE.md` with the calculation.

**No unsafe in application code.** All peripheral access goes through
avr-hal's safe abstractions. The only `unsafe` blocks are inside avr-hal
itself (hardware register access) and `panic-halt` (infinite loop). Application
code in `main.rs`, `dht22.rs`, `display.rs` and `serial_log.rs` is entirely
safe Rust.

---

## Architecture

```
src/main.rs        — #![no_std] #![no_main] entry point, peripheral init, event loop
src/dht22.rs       — DHT22 single-wire driver (embedded-hal OutputPin + InputPin)
src/display.rs     — SSD1306 layout, no-alloc FmtBuf text rendering
src/serial_log.rs  — UART formatted output, no format! macro
```

See `docs/ARCHITECTURE.md` for the DHT22 timing analysis, no-alloc formatting
approach, embedded-hal trait boundary table and a safety comparison with the
C++ equivalent.

---

## Hardware

| Component | Notes |
|---|---|
| Arduino Nano | ATmega328P, 20 MHz |
| DHT22 | 10 kΩ pull-up to 3.3 V required on DATA pin |
| SSD1306 OLED 128×64 | I2C (A4/A5), 3.3 V |

See `docs/WIRING.md` for pin assignments, toolchain installation and
troubleshooting.

---

## Building and Flashing

```bash
# Install toolchain (reads rust-toolchain.toml automatically)
rustup component add rust-src

# Install host tools
cargo install ravedude
# Linux: sudo apt install gcc-avr avr-libc avrdude

# Build
cargo build --release

# Build and flash
cargo run --release

# Monitor serial output at 9600 baud
screen /dev/ttyACM0 9600
```

---

## Binary Size Comparison

| Implementation | Flash | SRAM |
|---|---|---|
| Arduino C++ (Adafruit DHT + SSD1306) | ~14 KB | ~400 B |
| Rust no_std (this project) | ~18 KB | ~300 B |

The Rust binary is ~4 KB larger in flash due to monomorphisation of generic
trait bounds (each concrete type combination generates its own copy of the
generic function). SRAM usage is lower because there is no heap allocator
overhead and Rust's move semantics eliminate several defensive copies present
in the C++ version.

See `docs/cpp_equivalent/cpp_equivalent.ino` for the equivalent C++ sketch
and `docs/WIRING.md` for the `avr-size` comparison commands.

---

## Safety Properties vs C++

| Concern | Arduino C++ | Rust no_std |
|---|---|---|
| Pin direction errors | Runtime bug | Compile error |
| Integer overflow | Silent UB | Panics in debug; explicit wrapping ops in release |
| Buffer overrun | Silent corruption | Panics (halt) |
| Null pointer | Possible | No raw pointers in app code |
| Unused return value | Silently ignored | `#[must_use]` warning |

---

## File Map

| File | Purpose |
|---|---|
| `src/main.rs` | Entry point — peripherals, event loop, error tracking |
| `src/dht22.rs` | DHT22 single-wire driver from scratch |
| `src/display.rs` | SSD1306 dashboard layout and no-alloc text rendering |
| `src/serial_log.rs` | UART output without format! or alloc |
| `Cargo.toml` | Dependencies and optimisation profile |
| `.cargo/config.toml` | AVR target, linker flags, ravedude runner |
| `rust-toolchain.toml` | Pinned nightly toolchain |
| `docs/ARCHITECTURE.md` | Protocol timing, memory layout, safety analysis |
| `docs/WIRING.md` | Pin assignments, toolchain setup, troubleshooting |
| `docs/cpp_equivalent/cpp_equivalent.ino` | C++ reference for binary size comparison |
