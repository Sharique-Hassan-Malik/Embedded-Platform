#!/usr/bin/env python3
"""
sign_and_flash.py — Signing tool and host-side firmware uploader.

Workflow:
    1. Generate a P-256 keypair and write the public key to a .bin file
       that can be programmed into the PIC18's key page at 0x7E00.
    2. Sign a compiled .hex firmware file: compute SHA-256 of the
       application image, sign with ECDSA-P256, and append a metadata
       block at 0x7C00 in the output .hex file.
    3. Flash a signed .hex file to the PIC18 over UART (triggering
       bootloader update mode).

Requires: cryptography, pyserial, intelhex
    pip install cryptography pyserial intelhex

Usage:
    # Generate keypair (run once):
    python3 sign_and_flash.py keygen --out keys/

    # Sign firmware:
    python3 sign_and_flash.py sign --key keys/privkey.pem \\
        --hex build/app.hex --out build/app_signed.hex

    # Flash signed firmware:
    python3 sign_and_flash.py flash --port /dev/ttyUSB0 \\
        --hex build/app_signed.hex

    # Sign and flash in one step:
    python3 sign_and_flash.py sign --key keys/privkey.pem \\
        --hex build/app.hex --out build/app_signed.hex --flash /dev/ttyUSB0
"""

import argparse
import hashlib
import os
import struct
import sys
import time

# ---- Optional dependency guards ----------------------------------------- #

try:
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import (
        decode_dss_signature,
    )
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.backends import default_backend
    _CRYPTO_OK = True
except ImportError:
    _CRYPTO_OK = False

try:
    from intelhex import IntelHex
    _IHEX_OK = True
except ImportError:
    _IHEX_OK = False

try:
    import serial
    _SERIAL_OK = True
except ImportError:
    _SERIAL_OK = False


# ---- Memory map (must match boot_shared.h) ------------------------------ #

APP_START       = 0x0800
APP_END         = 0x7BFF
META_START      = 0x7C00
KEY_PAGE_START  = 0x7E00
PUBKEY_LEN      = 65          # 0x04 || X[32] || Y[32]

META_APP_START_OFF = 0
META_APP_SIZE_OFF  = 4
META_SIG_R_OFF     = 8
META_SIG_S_OFF     = 40
META_SHA256_OFF    = 72
META_VERSION_OFF   = 104
META_TOTAL         = 512

UART_ACK = 0x06
UART_NAK = 0x15
UART_ERR = 0x45
UART_OK  = 0x4F

BAUD_RATE = 31250


# ---- Sub-command: keygen ------------------------------------------------ #

def cmd_keygen(args):
    if not _CRYPTO_OK:
        sys.exit("Install the 'cryptography' package: pip install cryptography")

    os.makedirs(args.out, exist_ok=True)
    priv_path   = os.path.join(args.out, "privkey.pem")
    pub_bin_path = os.path.join(args.out, "pubkey.bin")
    pub_hex_path = os.path.join(args.out, "pubkey_keypage.hex")

    key = ec.generate_private_key(ec.SECP256R1(), default_backend())
    pub = key.public_key()

    # Write private key PEM (keep secret).
    with open(priv_path, "wb") as f:
        f.write(key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption(),
        ))

    # Extract uncompressed public key bytes.
    pub_bytes = pub.public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )
    assert len(pub_bytes) == PUBKEY_LEN, f"Unexpected key length {len(pub_bytes)}"

    with open(pub_bin_path, "wb") as f:
        f.write(pub_bytes)

    # Write a minimal Intel HEX file placing the public key at KEY_PAGE_START.
    # Program this into the PIC18 once using MPLAB X / PICkit before deploying.
    ih = IntelHex() if _IHEX_OK else None
    if ih is not None:
        for i, b in enumerate(pub_bytes):
            ih[KEY_PAGE_START + i] = b
        ih.write_hex_file(pub_hex_path)
        print(f"  Key page HEX : {pub_hex_path}")

    print(f"  Private key  : {priv_path}  (KEEP SECRET)")
    print(f"  Public key   : {pub_bin_path}")

    # Print public key in C array format for manual embedding.
    print("\n  /* Paste into bootloader/keys.h if not using the key-page mechanism: */")
    print("  static const uint8_t PUB_KEY[65] = {")
    row = []
    for b in pub_bytes:
        row.append(f"0x{b:02X}u")
        if len(row) == 8:
            print("    " + ", ".join(row) + ",")
            row = []
    if row:
        print("    " + ", ".join(row))
    print("  };")


# ---- Sub-command: sign -------------------------------------------------- #

def _load_app_image(hex_path):
    """Load an Intel HEX file and return the byte array for APP_START..APP_END."""
    if not _IHEX_OK:
        sys.exit("Install the 'intelhex' package: pip install intelhex")
    ih = IntelHex()
    ih.loadhex(hex_path)

    # Find the actual extent of application data.
    addresses = sorted(a for a in ih.addresses() if APP_START <= a <= APP_END)
    if not addresses:
        sys.exit("No application data found in 0x0800..0x7BFF range.")

    app_end = addresses[-1]
    app_size = app_end - APP_START + 1
    data = bytearray(app_size)
    for i in range(app_size):
        data[i] = ih[APP_START + i]   # unset bytes default to 0xFF in IntelHex

    return bytes(data), app_size


def cmd_sign(args):
    if not _CRYPTO_OK:
        sys.exit("Install the 'cryptography' package.")
    if not _IHEX_OK:
        sys.exit("Install the 'intelhex' package.")

    # Load private key.
    with open(args.key, "rb") as f:
        priv = serialization.load_pem_private_key(f.read(), password=None,
                                                  backend=default_backend())

    # Load application image.
    app_data, app_size = _load_app_image(args.hex)
    print(f"  App start    : 0x{APP_START:04X}")
    print(f"  App size     : {app_size} bytes (0x{app_size:X})")

    # SHA-256 of the application image.
    digest = hashlib.sha256(app_data).digest()
    print(f"  SHA-256      : {digest.hex()}")

    # ECDSA-P256 signature over the digest.
    sig_der = priv.sign(digest, ec.ECDSA(hashes.Prehashed()))
    r_int, s_int = decode_dss_signature(sig_der)
    sig_r = r_int.to_bytes(32, "big")
    sig_s = s_int.to_bytes(32, "big")
    print(f"  Sig r        : {sig_r.hex()[:32]}...")
    print(f"  Sig s        : {sig_s.hex()[:32]}...")

    # Build the 512-byte metadata block.
    meta = bytearray(META_TOTAL)
    struct.pack_into(">I", meta, META_APP_START_OFF, APP_START)
    struct.pack_into(">I", meta, META_APP_SIZE_OFF,  app_size)
    meta[META_SIG_R_OFF : META_SIG_R_OFF + 32] = sig_r
    meta[META_SIG_S_OFF : META_SIG_S_OFF + 32] = sig_s
    meta[META_SHA256_OFF : META_SHA256_OFF + 32] = digest
    version = (args.version or "1.0.0").encode("ascii")[:15]
    meta[META_VERSION_OFF : META_VERSION_OFF + len(version)] = version

    # Load the original HEX, inject the metadata block, write output.
    ih_out = IntelHex()
    ih_out.loadhex(args.hex)
    for i, b in enumerate(meta):
        ih_out[META_START + i] = b

    ih_out.write_hex_file(args.out)
    print(f"  Signed HEX   : {args.out}")

    if args.flash:
        # Inline flash after signing.
        args.port = args.flash
        args.hex  = args.out
        cmd_flash(args)


# ---- Sub-command: flash ------------------------------------------------- #

def cmd_flash(args):
    if not _SERIAL_OK:
        sys.exit("Install the 'pyserial' package: pip install pyserial")
    if not _IHEX_OK:
        sys.exit("Install the 'intelhex' package: pip install intelhex")

    ih = IntelHex()
    ih.loadhex(args.hex)

    with serial.Serial(args.port, BAUD_RATE, timeout=3.0) as ser:
        print(f"  Opened {args.port} at {BAUD_RATE} baud")
        time.sleep(0.1)   # allow bootloader to settle

        total_recs = 0
        ok_recs    = 0
        nak_recs   = 0

        for record in ih.tobinarray():
            pass   # exhaust the generator (we use todict below)

        # Re-generate Intel HEX records and send them.
        hex_str = ih.tobinstr()   # fallback: use tofile

        import io
        buf = io.StringIO()
        ih.write_hex_file(buf)
        lines = buf.getvalue().splitlines(keepends=True)

        for line in lines:
            if not line.startswith(":"):
                continue
            ser.write(line.encode("ascii"))
            total_recs += 1

            resp = ser.read(1)
            if not resp:
                print("  TIMEOUT waiting for device response")
                sys.exit(1)
            code = resp[0]
            if code == UART_ACK:
                ok_recs += 1
            elif code == UART_NAK:
                nak_recs += 1
                print(f"  NAK on record: {line.strip()}")
            elif code == UART_ERR:
                print("  ERR: device reported fatal error")
                sys.exit(1)
            elif code == UART_OK:
                print(f"  Flash complete. {ok_recs}/{total_recs} records accepted.")
                return

        # Wait for final OK after EOF record.
        resp = ser.read(1)
        if resp and resp[0] == UART_OK:
            print(f"  Verification passed. Booting application.")
        else:
            print(f"  Unexpected final response: {resp.hex() if resp else 'timeout'}")
            sys.exit(1)

    if nak_recs:
        print(f"  Warning: {nak_recs} records were NAK'd.")


# ---- Entry point -------------------------------------------------------- #

def main():
    p = argparse.ArgumentParser(description="PIC18 secure bootloader signing tool")
    sub = p.add_subparsers(dest="cmd", required=True)

    kg = sub.add_parser("keygen", help="Generate P-256 keypair")
    kg.add_argument("--out", default="keys", help="Output directory")

    sg = sub.add_parser("sign", help="Sign a firmware HEX file")
    sg.add_argument("--key",     required=True, help="Private key PEM file")
    sg.add_argument("--hex",     required=True, help="Input unsigned HEX file")
    sg.add_argument("--out",     required=True, help="Output signed HEX file")
    sg.add_argument("--version", default="1.0.0", help="Version string (max 15 chars)")
    sg.add_argument("--flash",   default=None, help="Also flash to this serial port")

    fl = sub.add_parser("flash", help="Flash a signed HEX to the device")
    fl.add_argument("--port", required=True, help="Serial port")
    fl.add_argument("--hex",  required=True, help="Signed HEX file")

    args = p.parse_args()
    {"keygen": cmd_keygen, "sign": cmd_sign, "flash": cmd_flash}[args.cmd](args)


if __name__ == "__main__":
    main()
