# PIC18 Secure Bootloader

> Part of the [Embedded Platform](../../README.md) — nineteen firmware projects
> behind one build harness. This folder builds and runs on its own; `embed build
> --only bootloader` builds it alongside the rest.

A secure bootloader for the PIC18F4550 that accepts firmware images over UART
and verifies an ECDSA-P256 signature before committing them to flash.  SHA-256
hashing and full P-256 elliptic-curve point multiplication are implemented from
scratch in C — no external cryptographic library is used anywhere in the
firmware.  The private signing key never touches the device; only the 65-byte
uncompressed public key is stored on-chip in a protected key page.  A Python
host tool handles key generation, firmware signing and serial flashing.

---

## What it does

At power-on the bootloader checks whether a trigger pin (RB0) is held low.
If not, it verifies the existing application's ECDSA-P256 signature and jumps
to it if valid.  If the trigger is asserted, the bootloader enters update mode:
it accepts an Intel HEX image over UART at 31 250 baud, writes it to the
application flash area (0x0800–0x7BFF), then verifies the signature from the
embedded metadata block at 0x7C00.  If verification passes it sends an OK byte
and boots the new firmware.  If it fails it erases the application area and
halts, requiring a power cycle and a corrected image.

---

## The hard part

**P-256 point multiplication without a library.**  The NIST P-256 curve
arithmetic requires a 256-bit prime field, modular inversion and Jacobian
coordinate geometry — none of which PIC18 provides natively.  The field
elements are 8-limb `uint32_t` arrays.  Modular reduction uses the FIPS 186-4
Appendix D.4 fast-reduction formula, which decomposes the 512-bit product into
eight 256-bit auxiliary values and combines them with additions and subtractions
exploiting the sparse structure of P-256's prime.  A Jacobian doubling and
mixed Jacobian-affine addition ladder avoids field inversions during scalar
multiplication; only the final affine conversion requires one inversion via
Fermat's little theorem.  The full `p256_verify()` runs in approximately
180 ms at 8 MHz.

**Protecting the bootloader from its own write engine.**  The flash write and
erase functions both enforce range guards: any write below `APP_START`
(0x0800) or at or above `KEY_PAGE_START` (0x7E00) is silently rejected.  This
means a maliciously crafted HEX file cannot overwrite the bootloader code or
the public key, regardless of what addresses it contains.

**Constant-time hash comparison.**  The stored and computed SHA-256 digests are
compared byte-by-byte with an XOR accumulator rather than a short-circuit
comparison, ensuring that the verification result does not leak information
about how many bytes matched.

---

## Architecture

See `docs/ARCHITECTURE.md` for the full boot sequence diagram, flash layout,
metadata block format, P-256 field arithmetic derivation and ECDSA verify
algorithm, IHEX receive engine description and resource usage table.

---

## Flash layout

```
0x0000–0x07FF  Bootloader code (2 KB)
0x0800–0x7BFF  Application code (29.5 KB)
0x7C00–0x7DFF  Firmware metadata: app_size, SHA-256, ECDSA sig, version
0x7E00–0x7FFF  Key page: 65-byte P-256 public key (programmed once)
```

---

## Hardware

| Component | Notes |
|---|---|
| PIC18F4550 | 32 KB flash, 2 KB RAM |
| RB0 | Trigger pin (active low with internal pull-up) |
| RC6 | UART TX (31 250 baud, 8N1) |
| RC7 | UART RX |
| USB-serial adapter | On host side |

---

## Workflow

### 1. Generate keypair (once per deployment)

```bash
pip install cryptography pyserial intelhex
python3 tools/sign_and_flash.py keygen --out keys/
```

This writes `keys/privkey.pem` (keep secret) and `keys/pubkey_keypage.hex`.

Program `pubkey_keypage.hex` onto the PIC18 once using MPLAB X / PICkit.

### 2. Build the application

Compile your application in MPLAB X targeting `APP_START = 0x0800`.
See `app_template/main.c` for the required linker script changes.

### 3. Sign the firmware

```bash
python3 tools/sign_and_flash.py sign \
    --key keys/privkey.pem \
    --hex build/app.hex \
    --out build/app_signed.hex \
    --version 1.0.0
```

### 4. Flash signed firmware

Hold RB0 low, apply power, then:

```bash
python3 tools/sign_and_flash.py flash \
    --port /dev/ttyUSB0 \
    --hex build/app_signed.hex
```

Or combine sign and flash:

```bash
python3 tools/sign_and_flash.py sign \
    --key keys/privkey.pem \
    --hex build/app.hex \
    --out build/app_signed.hex \
    --flash /dev/ttyUSB0
```

### 5. Tamper test

To verify the bootloader rejects modified firmware, flip any bit in the HEX
file and attempt to flash it.  The bootloader will send `ERR` (0x45), erase
the application area and halt.

---

## Building the bootloader

1. Open MPLAB X, create a project for PIC18F4550.
2. Select XC8 as the toolchain.
3. Add all `.c` files in `bootloader/` to Source Files.
4. Add all `.h` files to Header Files.
5. Set the code origin to `0x0000` and the code page end to `0x07FF`.
6. Build and program with PICkit 3/4.

Command-line build (XC8 v4.x):

```sh
xc8-cc -mcpu=18f4550 -mdfp="<PIC18Fxxxx_DFP>/xc8" bootloader/*.c -o bootloader.hex
```

### RAM note (XC8 free vs PRO)

Every translation unit compiles cleanly, and the application template links
and runs. The bootloader itself performs full ECDSA-P-256 verification plus
SHA-256, whose combined working set does not fit the PIC18F4550's 2 KB banked
RAM under the **free** XC8 compiler, which does not overlay the locals of
mutually-exclusive (non-reentrant) functions. Link it with **XC8 PRO** (whose
call-graph RAM overlaying reuses the scratch of the crypto call tree) or target
a PIC18 with more RAM (e.g. an 18FxxK-series part). The verification code is
correct — the limit is link-time RAM allocation, not source.

---

## Security properties

| Property | Status |
|---|---|
| Unsigned firmware rejected | Yes — ECDSA-P256 signature required |
| Bootloader self-overwrite prevented | Yes — flash write guard at APP_START |
| Public key overwrite prevented | Yes — flash write guard at KEY_PAGE_START |
| Constant-time hash comparison | Yes — XOR accumulator |
| Constant-time scalar multiplication | No — timing side-channel exists |
| Private key stored on device | No — only public key on device |
| Rollback protection | Not implemented (add version check in app_is_valid if needed) |
