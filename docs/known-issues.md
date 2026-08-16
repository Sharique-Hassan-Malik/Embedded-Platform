# Known issues

What is not fixed, and what was. Everything here is reachable from the test
suite; nothing is a rumour.

`embed build` builds **16 of 19**. The three that do not are each a missing
piece of the host, named and explained, and none of them is a defect in the
firmware.

---

## Fixed: the bootloader's signature verification never worked

**Module:** `modules/bootloader` · **Reproduce:** `pytest -k crypto`

The bootloader did not fit in RAM, so it had never linked; because it had never
linked, nothing had ever executed it; and because nothing had ever executed it,
four defects sat in its ECDSA implementation, every one of which made
verification wrong rather than loud.

They were found by writing `tests/test_crypto.c`, which compiles `p256.c` and
`sha256.c` for the host and checks them against FIPS 180-4 and RFC 6979 A.2.5.
Fourteen vectors; before the fixes, the valid-signature case failed.

**1. The generator point was wrong.**

```c
0x2BCE3357UL, 0x6B315ECECL, 0xBB640683UL, 0x7BF51F5UL
```

`0x6B315ECEC` is nine hex digits. Every digit after it shifted by one, so the
last three limbs of *Gy* were wrong and the curve's base point was not on the
curve. The compiler had been saying so all along — *"conversion changes value
from 28774362348"* — into a build nobody could complete.

**2. The fast reduction read outside its array.** `reduce_p256` maps FIPS
`A[k]` to `t[15-k]`, correctly, and then indexed `A(16)` through `A(31)`: that
is `t[-1]` through `t[-16]`. The FIPS 186-4 P-256 reduction is over sixteen
32-bit words, not thirty-two. It reduced the value 1 to a 256-bit constant.

**3. The scalar field was the wrong field.** `u1` and `u2` were computed by
taking the wide product, reducing it mod **P**, and then subtracting **N** until
it fit — which is `(x mod P) mod N`, not `x mod N`. `fe_invmod_n` was worse: it
did a mod-N exponentiation using `fe_mulmod`, which reduces mod P, with a
comment saying `/* uses P-reduction, but we need N-reduction */`. There is now
real mod-N arithmetic (`fe_addmod_n`, `fe_mulmod_n`), built from the add and
compare that were already there and already right.

**4. Doubling could not be done in place.** `point_mul` calls
`jpoint_double(&acc, &acc)`, and the doubling wrote `R->Y` before reading
`P_j->Y` to compute `Z' = 2·Y·Z` — so `Z'` was built from the *new* `Y`. `1*G`
was correct and `2*G` was not. The reads now all happen before any write.

## Fixed: and then it fit

`reduce_p256` held all nine of its 256-bit terms at once — 288 bytes, on the
call path of every field multiply, on a part with 2 KB of RAM. XC8 in free mode
does not overlay locals, so they were permanently resident.

Each term is now built into one scratch value and folded into the accumulator as
it is formed: 288 bytes down to 32. Together with an earlier overlay of three
disjoint buffers in `app_is_valid`, that was enough. **The bootloader links.**

This is why the RAM problem was recorded rather than guessed at last time. The
right fix was obvious once the crypto could be tested; without the tests it
would have been a change to signature verification justified only by a compiler
error going away.

## Fixed: XC16 was misdiagnosed

`sniffer` was skipped with "xc16 has the linker script for 24FJ64GA002 but not
its device library". That was wrong. XC16 has full support for the part; what it
does not do is apply the linker script automatically. Passing
`-Wl,--script,.../p24FJ64GA002.gld` builds it. The script is now located by
globbing `support/*/gld/`, because the family directory is not derivable from
the part number.

The misdiagnosis is the interesting part: the skip reason was specific,
plausible, and false, which is worse than a vague one.

## Fixed: `rust-node` and `oscilloscope` are wired up

`rust-node` needs a nightly toolchain — AVR is not tier-1 — and its
`rust-toolchain.toml` pins one. But `RUSTUP_TOOLCHAIN`, when set in the
environment, silently outranks that file, turning the pin into an E0554 four
crates deep. The harness now drops that variable, and the module carries a
`.cargo/config.toml` declaring the target and `build-std` so a plain
`cargo build` works.

`oscilloscope` needs the Pico SDK, which is a checkout rather than a package.
The harness looks for `pico_sdk_init.cmake` under `$PICO_SDK_PATH` and the usual
clone locations, then runs the CMake configure and build itself.

---

## Not fixed

### `fm-synth` — XC32 is installed without its compiler backend

Reported as a **skip**. The `xc32-gcc` driver runs and then cannot execute
`cc1`; `lib/gcc/pic32m/13.2.1/` has no `cc1` binary. This is an incomplete
installation, not a bad flag, and repairing it means re-running Microchip's
installer as root:

```
sudo ./xc32-v6.00-full-install-linux-x64-installer.run --mode unattended \
     --prefix /opt/microchip/xc32/v6.00
```

There is no passwordless root here, so it stays a skip. The firmware itself is
statically checked — SFR names and vectors cross-referenced against the device
header, the pure-DSP files compiled for the host — but it has not been built for
the target.

### `rtos-viz` and `irrigation` — nothing to compile

`rtos-viz` is a host-side visualiser. `irrigation` is MicroPython: the source
runs on the board as-is. Both are correct skips rather than gaps, and both have
their own host tests.

### The bootloader's crypto is not constant-time

`p256.c` says so in its header, and it is true: the scalar multiply branches on
key bits. For a bootloader verifying a signature at boot, with no attacker-
visible timing channel, that is the right trade for code you can audit. It would
not be if the same code verified anything over a network.

Now that the host tests exist, this is a change someone could actually make
safely. Before them it was not.
