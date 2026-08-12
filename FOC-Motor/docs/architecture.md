# Architecture — FOC Motor Controller

## Overview

Field-Oriented Control (FOC) decouples a brushless motor's torque and flux
into two independently controllable DC quantities — Id (flux-producing) and
Iq (torque-producing) — by continuously rotating the reference frame to align
with the rotor flux vector. This document describes every layer of the
implementation.

---

## Control Hierarchy

```
┌─────────────────────────────────────────────────────────┐
│  Position loop (500 Hz)                                 │
│  Input:  θ_ref, θ_mech                                  │
│  Output: ω_ref                                          │
│  PID: Kp=8.0, Ki=0.1, Kd=0 — tuned for ~10 rad/s BW   │
└────────────────────────┬────────────────────────────────┘
                         │ ω_ref
┌────────────────────────▼────────────────────────────────┐
│  Speed loop (1 kHz)                                     │
│  Input:  ω_ref, ω_mech (filtered encoder velocity)      │
│  Output: Iq_ref                                         │
│  PID: Kp=0.05, Ki=0.002 — tuned for ~20 rad/s BW       │
└────────────────────────┬────────────────────────────────┘
                         │ Iq_ref   (Id_ref = 0 for SPMSM)
┌────────────────────────▼────────────────────────────────┐
│  Current loop (16 kHz, ADC-triggered)                   │
│  Input:  Id_ref, Iq_ref, Ia, Ib, θ_elec                 │
│  Output: ta, tb, tc (SVPWM duty cycles)                 │
│  PID: Kp=0.30, Ki=0.005 — BW ≈ Rs/Ls = 600 rad/s       │
└─────────────────────────────────────────────────────────┘
```

---

## Signal Chain (Current Loop ISR)

```
Ia, Ib (ADC)
     │
     ▼
Clarke transform
     │  α, β  (stationary two-phase)
     ▼
Park transform  ←── θ_elec = θ_mech × pole_pairs
     │  Id, Iq  (rotating frame, DC in steady state)
     ▼
PID controllers (Id→Vd, Iq→Vq)  +  cross-coupling feed-forward
     │  Vd, Vq
     ▼
Inverse Park
     │  Vα, Vβ
     ▼
SVPWM
     │  ta, tb, tc  ∈ [0, 1]
     ▼
TIM1 CCR1/2/3  →  gate drivers  →  MOSFET bridge  →  motor
```

---

## Mathematics

### Clarke Transform

Converts three-phase (ia, ib, ic) to stationary two-phase (α, β).
Assumes a balanced load so ic = −ia − ib:

```
α = ia
β = (ia + 2·ib) / √3
```

The 1/√3 factor normalises the amplitude so |α + jβ| = |ia| for a balanced
sinusoidal input.

### Park Transform

Rotates the stationary (α, β) frame to a frame rotating at electrical
angular velocity ω_e, aligned with the rotor flux vector at angle θ_e:

```
d =  α·cos(θ_e) + β·sin(θ_e)
q = −α·sin(θ_e) + β·cos(θ_e)
```

In steady state d and q are constant DC values — this is why PI controllers
(zero steady-state error for DC inputs) are sufficient.

### Inverse Park Transform

```
Vα = Vd·cos(θ_e) − Vq·sin(θ_e)
Vβ = Vd·sin(θ_e) + Vq·cos(θ_e)
```

### Cross-Coupling Feed-Forward

In a surface PMSM the d-q voltage equations are:

```
Vd = Rs·Id + Ls·dId/dt − ω_e·Ls·Iq
Vq = Rs·Iq + Ls·dIq/dt + ω_e·Ls·Id + ω_e·ψ_pm
```

The ω_e·Ls·Iq and ω_e·Ls·Id terms couple the d and q axes. Cancelling them
with feed-forward decouples the axes and greatly improves current loop
bandwidth at high speed. The permanent magnet flux linkage term ψ_pm is
omitted here (treated as a constant disturbance integrated out by the PI).

### Space Vector PWM

SVPWM divides the (Vα, Vβ) plane into six 60° sectors. In each sector, two
adjacent active voltage vectors and one zero vector are applied for dwell
times T1, T2 and Tz:

```
T1 + T2 ≤ Ts   (clamped to prevent over-modulation)
Tz = Ts − T1 − T2
```

Centred SVPWM distributes Tz/2 at the start and end of each switching period,
minimising harmonic content. The three compare values ta, tb, tc represent the
fraction of Ts during which each high-side switch is on.

---

## PID Implementation

Position-form discrete PID with:

- **Derivative on measurement** — the derivative term is computed from
  −Kd·(meas[n] − meas[n−1]) rather than Kd·(e[n] − e[n−1]). This eliminates
  the derivative kick that occurs on step setpoint changes.

- **Back-calculation anti-windup** — when the output saturates, the integrator
  is decremented by Kb·(u − u_sat). The coefficient Kb = Ki/Kp is a common
  default. This avoids integrator wind-up during motor start-up when the
  current cannot be driven by the requested voltage.

- **Integrator clamping** — a separate int_max limit prevents the integrator
  from growing beyond a useful range independent of output saturation.

---

## Startup Sequence

```
IDLE
  │  foc_enable()
  ▼
ALIGN (200 ms)
  Drive Id = 0.5 A, Iq = 0, θ = 0° to lock rotor to d-axis.
  Establishes a known encoder reference position.
  │  align_ticks reaches zero
  ▼
OPEN-LOOP (V/f ramp)
  Increment electrical angle at an accelerating rate (10 rev/s²).
  Drive Vd = 0.20 (normalised) with no encoder feedback.
  Motor follows the rotating field under load.
  │  open-loop speed ≥ OL_EXIT_OMEGA (≈ 628 rad/s)
  ▼
CURRENT (closed-loop)
  FOC cascade active. Speed and position loops available.
```

---

## Hardware Interface

### TIM1 — Centre-Aligned PWM

TIM1 runs in centre-aligned mode 1 (up-down counting). The ARR register is set
to PWM_PERIOD = 2625 for 84 MHz APB2 → 16 kHz switching frequency. The PWM
period is counted twice per carrier cycle, so the effective interrupt rate is
also 16 kHz. Dead-time is inserted by the BDTR register (DTG = 8 counts ≈
95 ns) to prevent shoot-through.

### ADC1 — Injected Simultaneous Sampling

The injected group is triggered by TIM1_CC4 (compare output 4), which fires at
the PWM period midpoint. This guarantees that the ADC samples the phase
currents exactly when the PWM carriers are all at their peak — the instant at
which the reconstructed current has minimum ripple. Both channels are sampled
simultaneously to avoid phase error between Ia and Ib.

### TIM3 — Quadrature Encoder

TIM3 is configured in encoder mode 3 (both edges of both channels), giving a
resolution of 4× the encoder line count. A 1000-line encoder yields 4000
counts per revolution. The 32-bit CNT register never needs software overflow
handling for rotational applications.

### DMA-Backed UART Telemetry

Telemetry frames are sent via DMA from a double buffer (two alternating
TELEM_SIZE-byte regions). The ADC ISR checks the DMA-busy flag and drops the
frame if the previous transfer is not complete. This ensures that telemetry
never stalls the current loop — the 1 kHz effective telemetry rate uses only
24 bytes × 1000 = 24 KB/s of the available 460800 baud capacity.

---

## Telemetry Frame Format

```
Offset  Size  Type     Field
0       1     uint8    Magic (0xA5)
1       4     uint32   Tick counter (ISR calls)
5       4     float32  Id (A)
9       4     float32  Iq (A)
13      4     float32  ω_mech (rad/s)
17      4     float32  θ_mech (rad)
21      1     uint8    Mode (FocMode enum)
22      1     uint8    Fault flags (bitfield)
23      1     uint8    XOR checksum (bytes 0–22)
```

Total: 24 bytes. All multi-byte fields are little-endian.

---

## File Map

| File | Description |
|------|-------------|
| `firmware/include/foc_math.h` | Clarke, Park, inverse Park, SVPWM (float and Q15) |
| `firmware/include/pid.h` | PID controller with anti-windup and derivative on measurement |
| `firmware/include/foc.h` | FOC controller struct, mode enum and function declarations |
| `firmware/include/hal.h` | Hardware abstraction layer API and telemetry frame definition |
| `firmware/src/foc.c` | Three-layer FOC cascade implementation |
| `firmware/src/hal.c` | Bare-register STM32F401 peripheral drivers |
| `firmware/src/main.c` | State machine, ADC ISR callback and main loop |
| `firmware/src/stm32f401.ld` | Linker script (512 KB flash, 96 KB SRAM) |
| `host/monitor.py` | Real-time telemetry monitor with matplotlib plots |
| `tests/test_foc.py` | 27 pytest assertions covering all subsystems |
| `Makefile` | Cross-compilation with arm-none-eabi-gcc |
| `requirements.txt` | Python host dependencies |

---

## Gain Tuning Reference

### Current Loop

The current loop bandwidth ω_c is set by the motor electrical time constant:

```
ω_c = Rs / Ls   (rad/s)
Kp  = Ls · ω_c  = Rs
Ki  = Rs · ω_c / f_sw  (discretised, approximate)
```

For Rs = 0.3 Ω, Ls = 0.5 mH: ω_c = 600 rad/s, Kp = 0.30.

### Speed Loop

The speed loop bandwidth should be at least 10× lower than the current loop
bandwidth. Starting point: Kp = rated_torque / (J · ω_c_speed) where J is
the rotor inertia. Increase Ki until the load disturbance response is
acceptable; back off if oscillations appear.

### Position Loop

The position loop bandwidth should be at least 5× lower than the speed loop.
A pure P controller (Ki = Kd = 0) is often sufficient for positioning; add a
small Ki only if a steady-state position error under load is unacceptable.
