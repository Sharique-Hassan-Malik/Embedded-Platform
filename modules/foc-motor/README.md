# Motor Controller with FOC

> Part of the [Embedded Platform](../../README.md) — nineteen firmware projects
> behind one build harness. This folder builds and runs on its own; `embed build
> --only foc-motor` builds it alongside the rest.

Field-Oriented Control implementation for a brushless DC motor on an STM32F401RE
microcontroller. Implements the full FOC signal chain — Clarke and Park transforms,
SVPWM output, three-layer PID cascade (current, speed and position) and a real-time
telemetry monitor — from bare registers with no HAL or library dependencies.

---

## What it is

FOC is the standard control technique for high-performance BLDC and PMSM drives.
It transforms the three-phase currents into a rotating reference frame aligned with
the rotor flux, turning a sinusoidal AC control problem into a DC one. Two
independent PID controllers then regulate the flux-producing component Id and
the torque-producing component Iq separately, giving smooth torque at all speeds
with minimal losses.

This implementation covers:

- Clarke and Park transforms (float and Q15 fixed-point)
- Space Vector PWM with centred switching for minimum harmonic distortion
- Cross-coupling feed-forward voltage decoupling (improves current loop bandwidth at speed)
- Derivative-on-measurement PID with back-calculation anti-windup
- Three-loop cascade: position → speed → current
- Open-loop V/f startup ramp followed by automatic transition to closed-loop FOC
- Bare-register STM32F401 drivers: TIM1 complementary PWM, ADC injected simultaneous
  sampling, TIM3 quadrature encoder, USART2 DMA telemetry
- Real-time telemetry monitor with live matplotlib plots

---

## The hard part

**Timing closure on the ADC trigger.** The phase currents must be sampled at exactly
the PWM carrier peak to read the ripple-free reconstructed value. TIM1_CC4 triggers
the ADC injected group at the midpoint of each carrier period, so the ADC conversion
completes by the time the ISR runs. Sampling at any other point introduces current
ripple directly into the control loop, degrading torque quality at all speeds.

**Cross-coupling feed-forward.** Without decoupling, the d and q axes interact through
ω_e·Ls·Iq and ω_e·Ls·Id terms. At low speed this is negligible, but above 1000–2000
RPM the coupling degrades bandwidth significantly. The feed-forward terms cancel this
interaction and are computed from the measured currents and estimated electrical speed
on every ISR tick.

**Anti-windup during startup.** During the alignment and open-loop phases the current
PIDs are active but the plant is not yet responding (gate driver disabled). Without
back-calculation anti-windup the integrators would saturate and cause a large current
spike at closed-loop entry. The Kb coefficient is set to Ki/Kp, which gives the
fastest integrator discharge consistent with stability.

**Dead-time distortion.** The 95 ns dead time introduces a systematic voltage error
proportional to the sign of each phase current. At low speeds this error is large
relative to the applied voltage, causing torque ripple at six times the electrical
frequency. The firmware does not include dead-time compensation (a known open item).

---

## Repository structure

```
foc-motor/
  firmware/
    include/
      foc_math.h      — Clarke, Park, SVPWM (float + Q15)
      pid.h           — PID with anti-windup, derivative on measurement
      foc.h           — three-layer FOC cascade API
      hal.h           — peripheral interface and telemetry frame layout
    src/
      foc.c           — FOC controller implementation
      hal.c           — bare-register STM32F401 drivers
      main.c          — state machine, ADC ISR callback, main loop
      stm32f401.ld    — linker script (512 KB flash, 96 KB SRAM)
  host/
    monitor.py        — real-time telemetry monitor (matplotlib)
  tests/
    test_foc.py       — 27 pytest assertions
  docs/
    architecture.md   — design rationale and full signal chain description
  Makefile
  requirements.txt
  .gitignore
```

---

## Building the firmware

Requires `arm-none-eabi-gcc` (GCC 12+ recommended) and `stlink` for flashing.

```
make            # compile
make size       # show flash and SRAM usage
make flash      # flash to Nucleo-64 via st-link
make test       # run Python test suite
```

Toolchain installation:

```
# Debian / Ubuntu
sudo apt install gcc-arm-none-eabi stlink-tools

# macOS
brew install arm-none-eabi-gcc stlink
```

---

## Running the telemetry monitor

Install Python dependencies:

```
pip install -r requirements.txt
```

Connect to a running board:

```
python host/monitor.py --port /dev/ttyUSB0 --baud 460800
```

Replay a binary capture file:

```
python host/monitor.py --file trace.bin
```

Run the built-in simulation (no hardware required):

```
python host/monitor.py --demo
```

The demo simulates the full startup sequence — alignment pulse, open-loop ramp
and closed-loop speed tracking — and displays all four waveforms in real time.

---

## Running the tests

```
pytest tests/test_foc.py -v
```

27 tests covering Clarke symmetry and linearity, Park rotation and magnitude
preservation, inverse Park roundtrip, SVPWM duty cycle bounds and sector
assignment, PID proportional and integral behaviour, anti-windup under saturation,
derivative-on-measurement isolation, telemetry frame packing and checksum
validation, and the full three-loop cascade convergence to setpoint.

---

## Motor parameters

The default parameters in `main.c` target a small BLDC motor (4-pole, 0.5 mH,
0.3 Ω phase resistance, 8 A peak). To adapt to a different motor:

1. Update the `#define` block at the top of `main.c`.
2. Retune the PID gains using the formulas in `docs/architecture.md`.
3. Adjust `ENCODER_CPR` to match the encoder line count.

---

## Hardware

- **MCU:** STM32F401RE (Nucleo-64)
- **Gate driver:** any 3-phase gate driver with complementary inputs (e.g. IR2136)
- **Current sensing:** INA240 × 2, 10 mΩ shunt, gain 50 (0.5 V/A)
- **Encoder:** incremental quadrature, 1000 lines (4000 CPR after ×4 decoding)
- **DC bus:** 24 V nominal, 18–28 V operating range

Connections match the Nucleo-64 morpho header pinout described in `hal.c`.
