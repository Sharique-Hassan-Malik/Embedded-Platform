#!/usr/bin/env python3
"""
midi_test.py — Send MIDI test patterns to the PIC32 FM synthesizer.

The synthesizer's UART1 must be connected to the host via a USB-serial adapter.
Set the adapter baud rate to 31 250 (standard MIDI baud).

Usage:
    python3 tools/midi_test.py /dev/ttyUSB0
    python3 tools/midi_test.py COM4          # Windows

Requires: pyserial  (pip install pyserial)
"""

import sys
import time
import argparse
import serial

MIDI_BAUD = 31250
CHANNEL   = 0      # MIDI channel 1 (0-indexed)


# ---------------------------------------------------------------------------
# Low-level MIDI send helpers
# ---------------------------------------------------------------------------

def note_on(ser, note, velocity=100):
    ser.write(bytes([0x90 | CHANNEL, note & 0x7F, velocity & 0x7F]))


def note_off(ser, note):
    ser.write(bytes([0x80 | CHANNEL, note & 0x7F, 0]))


def cc(ser, number, value):
    ser.write(bytes([0xB0 | CHANNEL, number & 0x7F, value & 0x7F]))


# ---------------------------------------------------------------------------
# Demo routines
# ---------------------------------------------------------------------------

def demo_scale(ser):
    """C major scale, one octave starting at C4 (MIDI 60)."""
    print("  C major scale...")
    intervals = [0, 2, 4, 5, 7, 9, 11, 12]
    for step in intervals:
        note = 60 + step
        note_on(ser, note)
        time.sleep(0.28)
        note_off(ser, note)
        time.sleep(0.05)


def demo_fm_sweep(ser):
    """Hold C4 and sweep modulation index from pure sine to maximum FM."""
    print("  FM modulation sweep (CC 1)...")
    note_on(ser, 60, 100)
    for val in range(0, 128, 2):
        cc(ser, 1, val)
        time.sleep(0.04)
    for val in range(127, -1, -2):
        cc(ser, 1, val)
        time.sleep(0.04)
    note_off(ser, 60)


def demo_op_ratio(ser):
    """Hold C4 and step through operator ratios to show timbre change."""
    print("  Operator ratio sweep (CC 11)...")
    note_on(ser, 60, 100)
    # Ratios roughly corresponding to 0.5x, 1x, 2x, 3x, 4x
    ratio_cc_values = [0, 32, 64, 80, 96, 112, 127]
    for val in ratio_cc_values:
        cc(ser, 11, val)
        time.sleep(0.5)
    note_off(ser, 60)


def demo_vibrato(ser):
    """Hold A4 and sweep LFO depth with pitch target (vibrato)."""
    print("  Vibrato sweep (CC 80=pitch, CC 9=rate, CC 10=depth)...")
    note_on(ser, 69, 100)
    cc(ser, 80, 0)     # LFO target = pitch
    cc(ser, 9,  60)    # LFO rate ≈ 9.5 Hz

    for val in range(0, 128, 4):
        cc(ser, 10, val)
        time.sleep(0.04)
    cc(ser, 10, 0)
    time.sleep(0.2)
    note_off(ser, 69)


def demo_tremolo(ser):
    """Hold A4 and sweep LFO depth with amplitude target (tremolo)."""
    print("  Tremolo sweep (CC 80=amplitude, CC 9=rate, CC 10=depth)...")
    note_on(ser, 69, 100)
    cc(ser, 80, 127)   # LFO target = amplitude
    cc(ser, 9,  30)    # LFO rate ≈ 4.8 Hz

    for val in range(0, 128, 4):
        cc(ser, 10, val)
        time.sleep(0.04)
    cc(ser, 10, 0)
    time.sleep(0.2)
    note_off(ser, 69)


def demo_adsr(ser):
    """Demonstrate long attack and release."""
    print("  Slow attack / long release (CC 5, CC 8)...")
    cc(ser, 5, 100)    # attack  ≈ 3 150 ms
    cc(ser, 8, 100)    # release ≈ 3 150 ms
    note_on(ser, 60, 100)
    time.sleep(3.5)
    note_off(ser, 60)
    time.sleep(3.5)
    # Reset to default
    cc(ser, 5, 0)
    cc(ser, 8, 20)


def demo_chord(ser):
    """Play a simple I-IV-V-I chord progression using voice reuse."""
    print("  I-IV-V-I chord (root note only, monophonic)...")
    roots = [60, 65, 67, 60]    # C, F, G, C (MIDI note numbers)
    for root in roots:
        note_on(ser, root, 90)
        time.sleep(0.6)
        note_off(ser, root)
        time.sleep(0.1)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

DEMOS = {
    "scale":    demo_scale,
    "fm":       demo_fm_sweep,
    "ratio":    demo_op_ratio,
    "vibrato":  demo_vibrato,
    "tremolo":  demo_tremolo,
    "adsr":     demo_adsr,
    "chord":    demo_chord,
}


def main():
    parser = argparse.ArgumentParser(description="MIDI test tool for PIC32 FM synth")
    parser.add_argument("port", help="Serial port (e.g. /dev/ttyUSB0 or COM4)")
    parser.add_argument(
        "--demo",
        choices=list(DEMOS.keys()) + ["all"],
        default="all",
        help="Which demo to run (default: all)",
    )
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, MIDI_BAUD, timeout=1)
    except serial.SerialException as exc:
        print(f"Cannot open {args.port}: {exc}", file=sys.stderr)
        sys.exit(1)

    print(f"Connected to {args.port} at {MIDI_BAUD} baud\n")
    time.sleep(0.2)

    if args.demo == "all":
        selected = list(DEMOS.items())
    else:
        selected = [(args.demo, DEMOS[args.demo])]

    for name, fn in selected:
        print(f"[{name}]")
        fn(ser)
        time.sleep(0.6)

    ser.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
