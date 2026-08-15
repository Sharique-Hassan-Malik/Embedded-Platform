# Embedded Platform

Nineteen firmware projects across five architectures — PIC8, PIC16, PIC32, ARM
Cortex-M, AVR, RP2040 and AVR-Rust — behind one build harness, one serial
protocol and one CRC.

Each module still lives in its own folder, with its own README, its own tests
and its own build. `cd modules/rtos && make` works exactly as it did when that
was a repository of its own. What this repository adds is the part that did not
exist before: knowing how to build all nineteen, and a host-side link they all
speak.

```
embed toolchains        which compilers are here, and what is wrong with them
embed modules           what is here, what builds it, what it targets
embed build             build everything that can be built
embed build --only rtos --family arm -v
```

## Why it is one repository

Nineteen firmware projects share almost no code — a PIC18 bootloader and a
Cortex-M scheduler have nothing to say to each other. What they do share is
everything *around* the firmware, and that is what was duplicated nineteen times:

**Building it.** Every project had its own idea of where the compiler was. Five
different toolchains, each with its own way of being half-installed. The harness
knows the difference between "not installed", "installed but its backend is
missing", and "installed but missing the device library for your part" — three
problems that send you to three different places, and that all look like a build
failure otherwise. It also knows that XC8 wants a device pack's `xc8`
subdirectory and not the pack root, because pointing at the root reports *"no
device-support files found"*, which reads as a missing pack rather than a path
one level too high.

**Talking to it.** Nine of these stream samples to a host program. There were
nine framing implementations and at least three CRCs called "CRC-16". Now there
is one `FrameLink` (magic byte, length, CRC-16/CCITT-FALSE, drop on mismatch)
and one `TextLink` for the ones that print lines, in `embedkit/link.py`.

**Checking it.** `crc16(b"123456789") == 0x29B1` is a one-line test that would
have caught the disagreement: CCITT-FALSE and "true" CCITT differ only in their
initial value, disagree on every input, and are both called CRC-16 CCITT in
datasheets.

Putting them together meant compiling all nineteen for the first time, which is
how the defects in [docs/known-issues.md](docs/known-issues.md) were found —
including three that meant a project had never built at all.

## The modules

| module | target | what it is |
|---|---|---|
| [`rtos`](modules/rtos) | STM32F401RE | A pre-emptive kernel: context switching in assembly, priority scheduling, mutexes with priority inheritance, a heap. |
| [`rtos-viz`](modules/rtos-viz) | host | Traces the kernel's scheduling decisions off the wire and draws them — the only way to *see* a priority inversion. |
| [`foc-motor`](modules/foc-motor) | STM32F401RE | Field-oriented control: Clarke and Park transforms, dual PI current loops, space-vector modulation, at the switching frequency. |
| [`bootloader`](modules/bootloader) | PIC18F4550 | Signed firmware updates over UART with a rollback slot — the part that has to be right because it cannot be updated. |
| [`weather`](modules/weather) | PIC18F26K22 | BME280 and DS3231 over I2C, an e-paper display, SD logging. |
| [`captouch`](modules/captouch) | PIC16F1829 | Charge-time measurement on bare pads, with gesture recognition and drift compensation. |
| [`lcd-console`](modules/lcd-console) | PIC18F4550 | A character-LCD terminal with scrollback, EEPROM settings and debounced buttons. |
| [`sniffer`](modules/sniffer) | PIC24FJ64GA002 | Captures I2C, SPI and UART concurrently, timestamps the frames, streams them to a host decoder. |
| [`fm-synth`](modules/fm-synth) | PIC32MX270F256B | Four-operator FM voices with envelopes, into an audio DAC. |
| [`oscilloscope`](modules/oscilloscope) | RP2040 | Sampling front end with triggering, plus a host FFT and display. |
| [`power-profiler`](modules/power-profiler) | AVR | High-rate current measurement, streamed to a host that turns it into an energy budget per firmware phase. |
| [`lora-mesh`](modules/lora-mesh) | AVR + SX1276 | A flooding mesh with duplicate suppression: node and base-station sketches over one radio library, and a dashboard showing the topology. |
| [`sensor-mesh`](modules/sensor-mesh) | AVR | Battery-powered sensor nodes reporting to a base station. |
| [`fall-detect`](modules/fall-detect) | Nano 33 BLE | Accelerometer features and a TFLite Micro classifier, running on the node rather than on a phone. |
| [`spectrophotometer`](modules/spectrophotometer) | AVR | LED and photodiode absorbance measurement, with Beer-Lambert concentration fitting on the host. |
| [`irrigation`](modules/irrigation) | RP2040 (MicroPython) | Soil moisture, a valve schedule, and a watchdog that fails dry. |
| [`midi`](modules/midi) | Leonardo | Velocity-sensitive keys and continuous controllers over USB MIDI. |
| [`pov`](modules/pov) | AVR | Column timing against a rotation sensor, which is all this is. |
| [`rust-node`](modules/rust-node) | ATmega328P | The same class of node in `no_std` Rust, to see what the type system buys on eight bits. |

## Using one module on its own

Nothing in a module imports `embedkit`. Each folder builds with its own
`Makefile`, `arduino-cli` invocation or `cargo build`, and its README says which.
The harness is a layer above them, not underneath.

```bash
cd modules/rtos && make            # produces build/cortex-rtos.bin
cd modules/foc-motor && make
```

## Using the whole repository

```bash
pip install -r requirements.txt    # host-side only: pyserial, numpy, matplotlib
python -m embedkit.cli toolchains
python -m embedkit.cli build
```

Current state on a machine with XC8, XC16, XC32, arm-none-eabi, arduino-cli and
cargo installed: **12 build, 5 skip with a reason, 1 fails**. The failure and
the skips are each explained in [docs/known-issues.md](docs/known-issues.md).

## Tests

```bash
pytest -m "not slow"     # seconds: CRC, framing, the manifest, toolchain discovery
pytest                   # minutes: compiles all nineteen
```

The fast tests are the ones that catch silent breakage — a device pack resolved
to the wrong family, a CRC variant that disagrees with the firmware. The slow
ones are the build sweep.

## Layout

```
embedkit/          the harness: toolchain discovery, the manifest, CRC, the serial link
modules/<name>/    one firmware project, unchanged and independently buildable
docs/              per-module architecture notes, and known-issues.md
tests/             what is only true because these nineteen are one repository
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for how the harness finds compilers and
why the manifest is static data.
