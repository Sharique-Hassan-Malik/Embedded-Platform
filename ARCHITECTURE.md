# Architecture

Four files do the work. Everything else is firmware that existed already.

```
embedkit/toolchain.py    finds compilers, decides what "usable" means, builds a module
embedkit/registry.py     the manifest: nineteen modules as static data
embedkit/link.py         the host-side serial link, framed and unframed
embedkit/crc.py          one CRC-16, with the check value in a test
```

## The manifest is static data

`registry.py` contains no logic and runs no subprocess. Reading it does not
require a single compiler to be installed, which is the point: `embed modules`
works on a bare machine and tells you what you would need.

```python
Module("weather", "Weather station", role=FIRMWARE, family=XC8, device="18F26K22",
       summary="BME280 and DS3231 over I2C, an e-paper display, and SD logging.")
```

A module names a *family* rather than a compiler path. Resolving a family to an
actual binary is `toolchain.py`'s job and happens only when something is built.
That separation is why the listing commands are instant and why they cannot
fail.

## "Usable" is three states, not two

The core of `toolchain.py`. A compiler is not present-or-absent:

| state | what you do about it |
|---|---|
| not installed | install it |
| installed, backend missing | your installation is incomplete — reinstall |
| installed, device library missing | install the device pack for *your part* |

XC32 here is the second: the driver runs and then cannot execute `cc1`. XC16 is
the third: it has the linker script for the `24FJ64GA002` but not
`libp24FJ64GA002-elf.a`, so every special-function register is undefined at
link. Both look identical to a build failure if you only check the exit code,
and both are detected by probing for their specific signature.

```python
def report() -> list[Tool]:
    """Every family, whether it is usable, and if not, precisely why."""
```

A `Tool` is never unusable without a `problem` string. There is a test for that,
because a silent unusable toolchain is how you end up debugging your firmware
instead of your installation.

## The device-pack rule

The single most useful thing in the file:

```python
def dfp_argument(pack: Path) -> Path:
    """The `xc8` subdirectory, if the pack has one. Pointing at the pack root
    produces "no device-support files found", which reads as a missing pack
    rather than a path one level too high."""
    inner = pack / "xc8"
    return inner if inner.is_dir() else pack
```

And packs are matched by *content*, not by name. `18F26K22` lives in `pic18f-k`
while `18F4550` lives in `pic18fxxxx`; no rule over the part number gets that
right, so `pack_for()` searches what each pack actually contains.

## Building is per-family

`build_module()` dispatches on the family and returns a `BuildResult` with three
outcomes, not two:

```python
@dataclass
class BuildResult:
    module: str
    ok: bool = False
    skipped: str = ""      # a reason, never empty when set
    output: list[str] = ...
```

`skipped` is what keeps the sweep honest. A module that cannot be built because
the machine lacks the Pico SDK, or a nightly Rust channel, or a TensorFlow Lite
Micro library, is *not* a failure of the firmware — and reporting it as one
trains you to ignore failures. So the harness recognises those signatures and
converts them into a skip that names what to install:

```python
if not result.ok and "E0554" in output:
    result.skipped = ("needs a nightly Rust toolchain — avr-hal uses "
                      "#![feature(asm_experimental_arch)], which stable "
                      "rejects with E0554")
```

The inverse matters just as much: a module that the toolchain can build but
that does not fit its part is reported as a **failure**, not a skip, because
that is a property of the firmware rather than of the machine.

## The Arduino projects needed real fixes

`arduino-cli compile` takes a directory whose name matches its `.ino`, and
headers must sit beside the sketch. Three projects violated that and had
therefore never been built:

- `fall-detect` kept its headers in `firmware/include/` and targeted an AVR
  board, while its model needs TensorFlow Lite Micro — which needs a C++
  standard library that avr-gcc does not ship. It failed at `<cstddef>`, which
  reads as a missing header rather than as the wrong architecture. Headers moved
  beside the sketch; board is now a Nano 33 BLE.
- `lora-mesh` had `node.ino` and `base_node.ino` in one directory, which is two
  sketches and not buildable. The shared mesh, routing, packet and radio code is
  now an Arduino library under `firmware/mesh_lib/`, and each sketch has its own
  directory — so both build, from one copy of the implementation.
- `midi` was built for an Uno, which has no USB MIDI. It is a Leonardo.

None of these are harness features. They are what compiling the projects for
real revealed.

## The shared serial link

Nine projects stream to a host. `link.py` has two classes because there are two
honest cases:

- `TextLink` — the module prints lines. `readline()`, stripped.
- `FrameLink` — binary samples. Magic byte, type, length, payload, CRC-16.
  **A frame whose CRC does not match is dropped**, because a corrupted sample is
  worse than a missing one. Wrong magic and truncated frames are ignored the
  same way.

Both raise `LinkError` naming `pyserial` if it is not installed, rather than an
`ImportError` from three frames down.

## The CRC

One function, `crc16()`, CRC-16/CCITT-FALSE — init `0xFFFF`, poly `0x1021`, no
reflection, no final XOR. The test that matters:

```python
assert crc16(b"123456789") == 0x29B1
```

CCITT-FALSE and "true" CCITT differ only in their initial value, disagree on
every input, and are both called "CRC-16 CCITT" in datasheets. The published
check value is the only way to know which one you have.

## What the cross-module tests cover

`tests/test_integration.py` deliberately does not re-test the firmware — each
project tests itself, in its own folder. It tests the guarantees that hold across
all nineteen:

- every module in the manifest exists and has a README;
- every Arduino module names a sketch directory that is really there;
- every Microchip module names a device some installed pack actually contains;
- two different PIC families do not resolve to the same pack;
- no toolchain is unusable without saying why;
- the CRC matches its published check value, and a bad frame is dropped;
- and, marked `slow`, all nineteen build.
