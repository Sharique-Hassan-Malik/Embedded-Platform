# Embedded Systems & Firmware

Bare-metal and RTOS firmware across PIC, ARM Cortex-M, Raspberry Pi Pico and embedded Rust: sensors, motor control, wearables, instrumentation, and wireless mesh networking, with the host-side tooling that drives them.

A collection of 19 self-contained projects. Each lives in its own subdirectory with its own `README.md` and `LICENSE` (most also include an `ARCHITECTURE.md` and a test suite), and can be built and run independently.

## Projects

| project | what it is |
|---|---|
| [`Cortex-RTOS`](./Cortex-RTOS) | A minimal preemptive RTOS for ARM Cortex-M4, written entirely from scratch. |
| [`Digital-Oscilloscope`](./Digital-Oscilloscope) | Turns a Raspberry Pi Pico into a 3-channel digital oscilloscope with a Python GUI. |
| [`Fall-Detect`](./Fall-Detect) | An end-to-end embedded ML pipeline that detects falls using an MPU-6050 IMU on an Arduino, runs a TFLite Micro 1D-CNN classifier on-device, and sen… |
| [`FOC-Motor`](./FOC-Motor) | Field-Oriented Control implementation for a brushless DC motor on an STM32F401RE microcontroller. |
| [`Irrigation-Controller`](./Irrigation-Controller) | An autonomous irrigation controller running MicroPython on the Arduino Nano RP2040 Connect. |
| [`LoRa-Mesh`](./LoRa-Mesh) | Multi-hop mesh network using SX1276 LoRa modules on Arduino. |
| [`MIDI-Controller`](./MIDI-Controller) | A USB MIDI controller firmware for Arduino Leonardo (ATmega32U4) implementing 8 velocity-sensitive FSR pads, 4 rotary encoder knobs and 4 linear fa… |
| [`PIC16-CapTouch`](./PIC16-CapTouch) | A charge-time measurement (CTM) capacitive touch library for PIC16 microcontrollers. |
| [`PIC18-LCD-Console`](./PIC18-LCD-Console) | A portable Breakout clone running on a PIC18F4550 microcontroller driving a 128×64 KS0108 monochrome LCD. |
| [`PIC18-Secure-Bootloader`](./PIC18-Secure-Bootloader) | A secure bootloader for the PIC18F4550 that accepts firmware images over UART and verifies an ECDSA-P256 signature before committing them to flash. |
| [`PIC18-Weather-Station`](./PIC18-Weather-Station) | A battery-powered weather station on the PIC18F26K22 microcontroller. |
| [`PIC24-Protocol-Sniffer`](./PIC24-Protocol-Sniffer) | A passive, simultaneous UART/I2C/SPI protocol sniffer built on the PIC24FJ64GA002. |
| [`PIC32-FM-Synth`](./PIC32-FM-Synth) | A standalone two-operator FM synthesizer voice for the PIC32MX270F256B microcontroller. |
| [`POV-Display`](./POV-Display) | A persistence-of-vision display on an Arduino Uno that renders stable mid-air images from a spinning 8-LED strip. |
| [`Power-Profiler`](./Power-Profiler) | A sub-millisecond resolution current measurement tool for battery-powered Arduino sketches. |
| [`RTOS-Scheduler-Viz`](./RTOS-Scheduler-Viz) | Instruments a FreeRTOS application with lightweight trace hooks, streams scheduling events over UART, and renders an interactive Gantt chart showin… |
| [`Rust-Sensor-Node`](./Rust-Sensor-Node) | A nostd Rust firmware for the Arduino Nano (ATmega328P) that reads temperature and humidity from a DHT22 sensor, displays readings on a 128×64 SSD1… |
| [`Sensor-Mesh`](./Sensor-Mesh) | A multi-hop wireless sensor network using nRF24L01+ radios and a mesh routing protocol implemented from scratch. |
| [`Spectrophotometer`](./Spectrophotometer) | A working single-beam Spectrophotometer built from an Arduino, a photodiode and an LED. |

## Repository layout

Each subdirectory is a standalone project; there is no shared build. Enter one and follow its README:

```bash
cd Cortex-RTOS
cat README.md
```

## License

MIT — see the `LICENSE` file in each project.
