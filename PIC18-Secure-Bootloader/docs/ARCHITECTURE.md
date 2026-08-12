# Architecture

## Overview

A secure bootloader for the PIC18F4550 that accepts firmware images over UART
and verifies an ECDSA-P256 signature before writing them to flash.  The private
key never touches the device; only the corresponding 65-byte uncompressed public
key is stored on-chip in a dedicated key page.  All cryptographic primitives —
SHA-256 hashing and the full P-256 point multiplication — are implemented from
scratch in C without any external library.

---

## Flash Layout

```
Address        Region              Size     Notes
─────────────────────────────────────────────────────────────────
0x0000–0x07FF  Bootloader code     2 KB     Vectors at 0x0008/0x0018
0x0800–0x7BFF  Application code    29.5 KB  Vectors remapped to 0x0808/0x0818
0x7C00–0x7DFF  Firmware metadata   512 B    Written by sign_and_flash.py
0x7E00–0x7FFF  Key page            512 B    65-byte P-256 public key; programmed once
```

The bootloader occupies a single 2 KB block.  `flash_write_row()` and
`flash_erase_row()` both reject addresses below `APP_START` (0x0800) and above
`KEY_PAGE_START` (0x7E00), so neither an attacker-controlled firmware image nor
a bug in the receive engine can overwrite the bootloader or the public key.

---

## Metadata Block (at 0x7C00)

```
Offset   Size   Field
──────────────────────────────────────────────────────────────
0        4      app_start    — always 0x0800, sanity-checked by bootloader
4        4      app_size     — byte count of the application image
8        32     sig_r        — ECDSA-P256 signature r (big-endian)
40       32     sig_s        — ECDSA-P256 signature s (big-endian)
72       32     sha256       — SHA-256 digest of app image
104      16     version      — null-terminated ASCII version string
120      392    reserved     — zero-padded
```

The metadata block is written by `sign_and_flash.py sign` and appended to the
Intel HEX file before flashing.  The bootloader never trusts the stored hash
alone; it recomputes SHA-256 over the flash image every time it verifies.

---

## Boot Sequence

```
Power-on reset
      │
      ▼
Read RB0 (trigger pin)
      │
  High (not triggered)          Low (trigger asserted)
      │                                │
      ▼                                ▼
app_is_valid()?              ihex_receive()
      │                           │
  Yes │  No                   IHEX_DONE?
      │   └── wait for trigger    │
      ▼                       Yes │  No
launch_app()                      │   └── uart_putc(ERR)
                             app_is_valid()?     erase_application()
                                  │               halt
                              Yes │  No
                                  │   └── uart_putc(ERR)
                                  │       erase_application()
                                  │       halt
                                  ▼
                             uart_putc(OK)
                             launch_app()
```

`app_is_valid()` recomputes the SHA-256 digest from flash, compares it against
the stored digest with a constant-time XOR accumulator, then verifies the
ECDSA-P256 signature.  It returns 1 only when both checks pass.

---

## Cryptographic Modules

### `sha256.c` — SHA-256

Standard iterative implementation with a 64-byte internal buffer.  The 64
round constants are stored in a `uint32_t K[64]` array in program flash.  No
dynamic allocation; the context struct is 96 bytes on the stack.

The digest is recomputed from flash in 64-byte chunks to avoid loading the
entire application into RAM at once.

### `p256.c` — P-256 point multiplication and ECDSA verify

**Field arithmetic** uses 8-element `uint32_t` arrays (big-endian limbs).
Modular reduction mod P uses the FIPS 186-4 Appendix D.4 fast-reduction
formula which exploits the sparse structure of the P-256 prime:

```
p = 2^256 − 2^224 + 2^192 + 2^96 − 1
```

The reduction decomposes the 512-bit product into eight 256-bit auxiliary
values and combines them with additions and subtractions mod P, avoiding a
full multi-precision division.

**Point representation** uses Jacobian coordinates `(X:Y:Z)` representing the
affine point `(X/Z², Y/Z³)`.  Point doubling and mixed Jacobian-affine
addition are implemented to avoid field inversions during the scalar
multiplication ladder.  A final `jpoint_to_affine()` call performs one
modular inversion via Fermat's little theorem (`a^(p-2) mod p`).

**Scalar multiplication** uses a left-to-right double-and-add binary method
over the 256-bit scalar.  The baseline performance at 8 MHz is approximately
180 ms per `p256_verify()` call — acceptable for a one-shot bootloader
verification.

**ECDSA verify** follows RFC 6979 / SEC1:
```
1. Verify 0 < r, s < n
2. e = SHA-256 digest (already computed)
3. w = s^(-1) mod n
4. u1 = e*w mod n,  u2 = r*w mod n
5. R = u1*G + u2*Q
6. Accept iff R.x mod n == r
```

**Security note.** The implementation is not constant-time.  It is appropriate
for bootloaders where the adversary cannot make repeated timing measurements
during verification.  Deployments with a network-accessible debug interface
should replace the scalar multiplication with a constant-time implementation.

---

## IHEX Receive Engine

```
Host sends Intel HEX lines → PIC18 UART (31 250 baud, 8N1)

For each line:
  1. Wait for ':' start marker
  2. Read until CR/LF
  3. Decode hex nibbles → byte array
  4. Verify record checksum (two's complement of all bytes except checksum)
  5. Dispatch by record type:
       DATA     → row_write_byte() for each data byte
       EXT_ADDR → update segment register
       EOF      → flush row buffer, return IHEX_DONE
  6. Send UART_ACK (0x06) or UART_NAK (0x15)
```

The row accumulator buffers writes to a 64-byte staging area and flushes
(erase + write) when the address crosses a 64-byte row boundary.  Pre-filling
with the existing flash content before modification preserves bytes within the
row that the HEX file does not explicitly set.

---

## Key Provisioning

The 65-byte uncompressed P-256 public key (0x04 || X[32] || Y[32]) is stored
at `KEY_PAGE_START` (0x7E00) and programmed once at manufacturing time using
MPLAB X or the `sign_and_flash.py keygen` tool:

```
sign_and_flash.py keygen --out keys/
# → writes keys/pubkey_keypage.hex
# Program keys/pubkey_keypage.hex with MPLAB X / PICkit before deployment
```

The `flash_write_row()` guard (`addr >= KEY_PAGE_START → return 1`) prevents
any subsequent firmware update from overwriting the key page.

---

## Host Tool (`tools/sign_and_flash.py`)

Three sub-commands:

| Command | Action |
|---|---|
| `keygen` | Generate P-256 keypair; write private key PEM and public key HEX |
| `sign` | SHA-256 app image, ECDSA-P256 sign, inject metadata into output HEX |
| `flash` | Send signed HEX over UART, parse ACK/NAK/OK responses |

The private key is used only on the host and never transmitted to the device.

---

## Resource Usage

| Resource | Usage |
|---|---|
| Flash (bootloader code) | ≈ 1.8 KB (fits in 2 KB budget) |
| Flash (sha256 K table) | 256 B (64 × uint32_t) |
| RAM peak (p256_verify) | ≈ 700 B (Jacobian point structs + intermediates) |
| RAM (sha256 context) | 96 B |
| RAM (row buffer) | 64 B |
| Verification time at 8 MHz | ≈ 180 ms |

---

## File Map

| File | Description |
|---|---|
| `bootloader/boot_shared.h` | Memory map, protocol constants, metadata offsets |
| `bootloader/sha256.c/.h` | Portable SHA-256 implementation |
| `bootloader/p256.c/.h` | P-256 field arithmetic, point mul, ECDSA verify |
| `bootloader/flash.c/.h` | PIC18 TBLRD/TBLWT flash read/erase/write driver |
| `bootloader/ihex.c/.h` | Intel HEX receive engine with row accumulator |
| `bootloader/main.c` | Boot sequence, trigger detection, validity check, jump |
| `app_template/main.c` | Minimal application skeleton with remapped vectors |
| `tools/sign_and_flash.py` | Host-side keygen, signing and UART flash tool |
| `docs/ARCHITECTURE.md` | This document |
