"""The nineteen modules: what each targets, what builds it, what it is.

Static data. Reading it runs no compiler, so `embed targets` works on a machine
with none of the toolchains installed and says which ones it would need.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .toolchain import (ARDUINO, ARM, HOST, MICROPYTHON, MODULES_ROOT, PICO,
                        RUST, XC8, XC16, XC32)

FIRMWARE, HOST_TOOL, KERNEL, LIBRARY = "firmware", "host-tool", "kernel", "library"


@dataclass(frozen=True)
class Module:
    name: str
    title: str
    summary: str
    role: str
    family: str                  # which toolchain builds it
    device: str = ""             # the part number, where there is one
    build: str = ""              # `make` target, if it builds through a Makefile
    sketch: str = ""             # path to an Arduino sketch directory
    board: str = "arduino:avr:uno"
    # Directories to search for Arduino libraries, for a module whose sketches
    # share code with each other rather than with the wider world.
    libraries: tuple[str, ...] = ()
    host_side: bool = False      # ships a Python host program too

    @property
    def path(self) -> Path:
        return MODULES_ROOT / self.name


MANIFEST: tuple[Module, ...] = (
    Module("rtos", "Cortex-M RTOS", role=KERNEL, family=ARM, device="STM32F401RE", build="all",
           summary="A pre-emptive kernel: context switching in assembly, priority "
                   "scheduling, mutexes with priority inheritance, and a heap."),
    Module("rtos-viz", "Scheduler visualiser", role=HOST_TOOL, family=HOST, host_side=True,
           summary="Traces the kernel's scheduling decisions off the wire and draws "
                   "them, which is the only way to see a priority inversion."),
    Module("foc-motor", "Field-oriented motor control", role=FIRMWARE, family=ARM,
           device="STM32F401RE", build="all", host_side=True,
           summary="Clarke and Park transforms, dual PI current loops and space-vector "
                   "modulation, at the switching frequency."),
    Module("bootloader", "Secure bootloader", role=FIRMWARE, family=XC8, device="18F4550",
           summary="Signed firmware updates over UART with a rollback slot: the part "
                   "that has to be right because it cannot be updated."),
    Module("weather", "Weather station", role=FIRMWARE, family=XC8, device="18F26K22",
           summary="BME280 and DS3231 over I2C, an e-paper display, and SD logging."),
    Module("captouch", "Capacitive touch", role=FIRMWARE, family=XC8, device="16F1829",
           summary="Charge-time measurement on bare pads, with gesture recognition "
                   "and drift compensation."),
    Module("lcd-console", "LCD console", role=FIRMWARE, family=XC8, device="18F4550",
           summary="A character-LCD terminal with a scrollback buffer, EEPROM "
                   "settings and debounced buttons."),
    Module("sniffer", "Protocol sniffer", role=FIRMWARE, family=XC16, device="24FJ64GA002",
           host_side=True,
           summary="Captures I2C, SPI and UART concurrently, timestamps the frames "
                   "and streams them to a host decoder."),
    Module("fm-synth", "FM synthesiser", role=FIRMWARE, family=XC32, device="32MX270F256B",
           summary="Four-operator FM voices with envelopes, into an audio DAC."),
    Module("oscilloscope", "Digital oscilloscope", role=FIRMWARE, family=PICO, host_side=True,
           summary="Sampling front end with triggering, plus a host FFT and display."),
    Module("power-profiler", "Power profiler", role=FIRMWARE, family=ARDUINO, sketch="firmware/power_profiler", host_side=True,
           summary="High-rate current measurement, streamed to a host that turns it "
                   "into an energy budget per firmware phase."),
    # node and base_node are two sketches over one mesh implementation, so the
    # shared half is an Arduino library and each sketch is its own directory.
    Module("lora-mesh", "LoRa mesh", role=FIRMWARE, family=ARDUINO,
           sketch="firmware/node", libraries=("firmware",), host_side=True,
           summary="A routing mesh over LoRa: neighbour discovery, retries, and a "
                   "dashboard showing the topology."),
    Module("sensor-mesh", "Sensor mesh", role=FIRMWARE, family=ARDUINO, sketch="firmware/node", host_side=True,
           summary="Battery-powered sensor nodes reporting to a base station."),
    # TensorFlow Lite Micro needs a C++ standard library, which avr-gcc does not
    # ship — on an AVR board this fails at <cstddef>, which reads as a missing
    # header rather than as the wrong architecture for the model.
    Module("fall-detect", "Fall detection", role=FIRMWARE, family=ARDUINO,
           sketch="firmware/fall_detect", board="arduino:mbed_nano:nano33ble", host_side=True,
           summary="Accelerometer features and a small classifier, running on the node "
                   "rather than on a phone."),
    Module("spectrophotometer", "Spectrophotometer", role=FIRMWARE, family=ARDUINO, sketch="firmware/spectrophotometer", host_side=True,
           summary="LED and photodiode absorbance measurement, with Beer-Lambert "
                   "concentration fitting on the host."),
    Module("irrigation", "Irrigation controller", role=FIRMWARE, family=MICROPYTHON,
           summary="Soil moisture, a valve schedule, and a watchdog that fails dry."),
    Module("midi", "MIDI controller", role=FIRMWARE, family=ARDUINO, sketch="firmware/midi_controller",
           board="arduino:avr:leonardo",
           summary="Velocity-sensitive keys and continuous controllers over USB MIDI."),
    Module("pov", "Persistence-of-vision display", role=FIRMWARE, family=ARDUINO, sketch="firmware/pov_display",
           summary="Column timing against a rotation sensor, which is all this is."),
    Module("rust-node", "Rust sensor node", role=FIRMWARE, family=RUST, device="atmega328p",
           summary="The same class of node in no_std Rust, to see what the type system "
                   "buys on eight bits."),
)

_BY_NAME = {module.name: module for module in MANIFEST}


def modules(role: str | None = None, family: str | None = None) -> list[Module]:
    found = list(MANIFEST)
    if role:
        found = [m for m in found if m.role == role]
    if family:
        found = [m for m in found if m.family == family]
    return found


def module(name: str) -> Module:
    try:
        return _BY_NAME[name]
    except KeyError:
        raise KeyError(
            f"unknown module {name!r}; choose from {', '.join(sorted(_BY_NAME))}"
        ) from None


def families() -> list[str]:
    return sorted({m.family for m in MANIFEST})
