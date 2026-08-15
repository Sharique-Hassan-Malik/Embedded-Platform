# Wiring Guide

## Bill of Materials

| Qty | Component | Notes |
|---|---|---|
| 1 | Arduino Nano Every | ATmega4809, 20 MHz, 3.3 V / 5 V I/O |
| 1 | DHT22 sensor | AM2302 variant also works |
| 1 | 10 kΩ resistor | DHT22 data pull-up — required |
| 1 | SSD1306 OLED 128×64 | I2C, 3.3 V compatible |
| — | Jumper wires | |

---

## Wiring

```
Arduino Nano Every          DHT22
──────────────────────────────────────
 D2 (PF5)    ─── DATA  ──┬── 10kΩ ─── 3.3V
 3.3V        ─── VCC
 GND         ─── GND

Arduino Nano Every          SSD1306 OLED
──────────────────────────────────────────
 A4 (PA2/SDA) ─── SDA
 A5 (PA3/SCL) ─── SCL
 3.3V         ─── VCC     (use 3.3V — many SSD1306 modules accept 3.3–5V)
 GND          ─── GND
```

Both SDA and SCL already have internal pull-ups enabled by avr-hal's I2C
initialisation. External pull-ups (4.7 kΩ) are only needed if the wires
are long (> 30 cm) or multiple I2C devices share the bus.

---

## Toolchain Setup

Rust embedded development for AVR requires nightly Rust and several host tools.

### 1 — Install Rust nightly with AVR support

```bash
# rustup will read rust-toolchain.toml and install the correct nightly
rustup toolchain install nightly-2024-08-01
rustup component add rust-src --toolchain nightly-2024-08-01
```

### 2 — Install avr-gcc (C compiler and linker)

```bash
# Ubuntu / Debian
sudo apt install gcc-avr avr-libc avrdude

# macOS (Homebrew)
brew tap osx-cross/avr
brew install avr-gcc avrdude
```

### 3 — Install ravedude (Cargo flash runner)

ravedude wraps avrdude with automatic port detection and a Cargo runner
interface so `cargo run` both builds and flashes the binary.

```bash
cargo install ravedude
```

### 4 — Build

```bash
cd rust-sensor-node
cargo build --release
```

The build uses `-Z build-std=core` automatically via the AVR target
configuration. This compiles `core` from source for the AVR target since
it is not included in the nightly toolchain's precompiled standard library.

Expected build time: 2–5 minutes on first build (compiling core from source);
subsequent incremental builds are faster.

### 5 — Flash

Connect the Arduino Nano Every via USB, then:

```bash
cargo run --release
```

ravedude detects the port automatically on Linux and macOS. On Windows,
specify the port explicitly in `.cargo/config.toml`:

```toml
runner = "ravedude nano-every -cb 115200 -P COM3"
```

### 6 — Monitor serial output

```bash
# Linux / macOS
screen /dev/ttyACM0 9600

# Or with cargo-watch for live reload during development
cargo install cargo-watch
cargo watch -x "run --release"
```

Expected output:
```
READ ok   T=23.4 H=65.2
READ ok   T=23.5 H=65.1
READ err  checksum
READ ok   T=23.4 H=65.2
```

---

## Troubleshooting

**`error: linker 'avr-gcc' not found`**
Install `gcc-avr` (Linux) or `avr-gcc` via Homebrew (macOS).

**`avrdude: butterfly_recv(): programmer is not responding`**
Hold the reset button on the Nano Every while the upload begins, then
release it when avrdude starts transmitting. The Nano Every bootloader
has a short window.

**DHT22 always returns `no_response`**
Verify the 10 kΩ pull-up to 3.3 V on the DATA pin. Without it the line
floats and the sensor cannot pull it low. Also confirm the sensor has been
powered for at least 1 second before the first read.

**OLED shows nothing**
Run `avrdude -p atmega4809 -c jtag2updi -P /dev/ttyACM0 -U eeprom:r:-:h`
to verify the device is responsive. Check the I2C address — some SSD1306
modules use 0x3D rather than 0x3C. Change `I2CDisplayInterface::new` to
`I2CDisplayInterface::new_custom_address(i2c, 0x3D)` if needed.

---

## Binary Size Comparison

Compile both and compare with `avr-size`:

```bash
# Rust release binary
cargo build --release
avr-size target/avr-unknown-gnu-atmega4809/release/rust-sensor-node.elf

# Equivalent Arduino C++ sketch (see docs/ for the sketch)
# Flash after uploading via Arduino IDE, then:
avr-size /tmp/arduino_build_*/sketch.ino.elf
```

Typical results for DHT22 + SSD1306 + serial:

| Implementation | Flash | SRAM |
|---|---|---|
| Arduino C++ (DHT + Adafruit SSD1306) | ~14 KB | ~400 B |
| Rust no_std (this project) | ~18 KB | ~300 B |

The Rust binary is slightly larger in flash (monomorphisation of generic
trait bounds adds code) but uses less SRAM (no dynamic allocation,
tighter stack usage from Rust's guaranteed move semantics).
