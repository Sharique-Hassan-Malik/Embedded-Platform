# Known issues

Things found while making the nineteen modules build under one harness that are
not fixed here. Each says what is wrong, how to see it, and why it is recorded
rather than patched.

Everything else builds — `embed build` is the check, and a module that cannot be
built for an environmental reason says so as a skip rather than a failure.

---

## The PIC18 bootloader does not fit in RAM

**Module:** `modules/bootloader` · **Reproduce:** `embed build --only bootloader -v`

```
bootloader/main.c:136:: error: (1250) could not find space (65 bytes)
                        for variable app_is_valid@phase
```

The 65 bytes are the uncompressed P-256 public key (`0x04 || X || Y`). The
PIC18F4550 has 2 KB of RAM, and XC8 must place each object entirely inside one
256-byte bank, so this is a placement failure and not simply a total-size one.

What has been done: `app_is_valid()` used to hold a 64-byte flash window, a
32-byte stored hash and the 65-byte key as three separate static objects — 161
bytes — even though their lifetimes do not overlap. They are now one union of 65
bytes, and the flash window is 32 bytes instead of 64. That is 96 bytes of RAM
returned, and it is a real improvement, but the build still fails on the same
65-byte request.

What is left is not in this function. XC8 compiles in free mode with a *compiled
stack*: locals of non-reentrant functions are statically allocated, so
`p256_verify()`'s working set — several `point_t`, each three 32-byte field
elements, plus temporaries — is permanently resident alongside the 105-byte
SHA-256 context. Making it fit means deciding how the curve arithmetic shares
scratch space across the whole of `p256.c`.

**Why it is recorded rather than fixed:** that decision is a memory layout for
signature verification in a bootloader — the one piece of firmware that cannot
be updated if it is wrong. Getting it wrong does not produce a crash; it
produces a bootloader that accepts an image it should have rejected. That is
worth measuring properly rather than guessing at, so the finding is written
down with its numbers and the build reports it honestly.

The host-side tooling, the signing script and the iHEX receiver are unaffected
and are tested.

---

## `sniffer` — XC16 is installed without its device library

**Module:** `modules/sniffer` · Reported as a **skip**, not a failure.

XC16 has the linker script for the `24FJ64GA002` but not the matching
`libp24FJ64GA002-elf.a`, so every special-function register is undefined at
link:

```
: undefined reference to `_IFS0bits'
```

This is an incomplete toolchain installation rather than anything about the
firmware. Installing the XC16 device support pack for the PIC24F family fixes
it. The harness distinguishes this from a compile error deliberately — the two
send you to different places.

## `fm-synth` — XC32 is installed without its compiler backend

**Module:** `modules/fm-synth` · Reported as a **skip**.

The `xc32-gcc` driver runs and then cannot execute `cc1`. Again an incomplete
installation, not a bad flag.

## `oscilloscope` — needs the Pico SDK

**Module:** `modules/oscilloscope` · Reported as a **skip**.

The firmware builds through CMake against `pico-sdk`, which is not wired into
this harness. The host-side capture, FFT and display are Python and are tested.

## `rust-node` — needs a nightly toolchain

**Module:** `modules/rust-node` · Reported as a **skip**.

`avr-hal` uses `#![feature(asm_experimental_arch)]`, which the stable channel
rejects:

```
error[E0554]: `#![feature]` may not be used on the stable release channel
```

`rustup toolchain install nightly` and a `rust-toolchain.toml` pinning it makes
this build. It is left as a skip because installing a second compiler channel is
the user's call, not the harness's.

## `irrigation` — nothing to compile

**Module:** `modules/irrigation` · Reported as a **skip**.

MicroPython. The source runs on the board as-is. Its host-side tests run.

---

## Fixed along the way

Recorded because each one meant the project had never been built:

| module | what was wrong |
|---|---|
| `rtos` | A use-before-declaration, an opaque type allocated statically, and a linker script with no `end` symbol for newlib's `sbrk`. It did not compile at all. |
| `power-profiler` | A 2048-entry ring buffer of 5-byte samples is 10 kB on a part with 2 kB of RAM; the sketch reported 520% of dynamic memory. Now 128 entries and 51%, with `static_assert`s on the budget. |
| `fall-detect` | Headers in `firmware/include/`, where the Arduino build cannot see them, and an AVR target for a TensorFlow Lite Micro model — avr-gcc ships no C++ standard library, so it failed at `<cstddef>`. Now a Nano 33 BLE. |
| `lora-mesh` | Two sketches (`node`, `base_node`) in one directory, which `arduino-cli` cannot build. The shared mesh and radio code is now an Arduino library and each sketch has its own directory. |
| `midi` | Built for an Uno, which has no USB MIDI. Now a Leonardo. |
