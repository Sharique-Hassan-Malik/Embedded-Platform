"""Finding the compilers, and the things about them nobody writes down.

Nineteen firmware projects across five device families and three vendors'
toolchains. Every one of them knew how to build itself, in a Makefile, and none
of that knowledge was anywhere you could find it without opening the Makefile.

The parts that are genuinely hard to rediscover:

  * **XC8 wants the `xc8` *subdirectory* of a device pack**, not the pack root.
    Point `-mdfp` at the pack itself and it says "no device-support files
    found", which reads like the pack is missing rather than one level wrong.
  * **The packs are not next to the compiler.** They live wherever they were
    unpacked, and each covers a family — `pic18f-k` is not `pic18fxxxx`, and
    a device from the wrong pack fails the same way.
  * **XC32 here is a broken installation**: the driver runs and then cannot
    execute `cc1`. That is not a missing compiler and not a bad flag, and
    reporting it as "build failed" would send someone looking in the wrong
    place for an afternoon.

So this module answers three questions — is there a compiler, is there a device
pack for this part, and if not, exactly what is wrong — and the tests assert the
answers against the machine they run on.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
MODULES_ROOT = REPO_ROOT / "modules"

# Where Microchip's installer puts things, and where the packs were unpacked.
XC_ROOTS = (Path("/opt/microchip"), Path.home() / "opt/microchip")
PACK_ROOTS = (Path.home() / "microchip-dfp", Path.home() / ".mchp_packs",
              Path("/opt/microchip/packs"))

XC8, XC16, XC32 = "xc8", "xc16", "xc32"
ARM, ARDUINO, PICO, RUST, HOST = "arm", "arduino", "pico", "rust", "host"
MICROPYTHON = "micropython"


@dataclass(frozen=True)
class Compiler:
    name: str
    binary: Path | None
    version: str = ""
    problem: str = ""          # present and unusable — say why

    @property
    def usable(self) -> bool:
        return self.binary is not None and not self.problem


def _newest(paths: list[Path]) -> Path | None:
    """The highest version directory, by natural sort of the version part."""
    def key(path: Path) -> tuple:
        return tuple(int(part) for part in re.findall(r"\d+", path.name)) or (0,)
    return sorted(paths, key=key)[-1] if paths else None


def find_xc(family: str) -> Path | None:
    """`/opt/microchip/xc8/v4.00/bin/xc8-cc` and friends."""
    binary_name = {"xc8": "xc8-cc", "xc16": "xc16-gcc", "xc32": "xc32-gcc"}[family]
    for root in XC_ROOTS:
        candidates = sorted((root / family).glob("v*")) if (root / family).is_dir() else []
        newest = _newest(candidates)
        if newest and (newest / "bin" / binary_name).exists():
            return newest / "bin" / binary_name
    found = shutil.which(binary_name)
    return Path(found) if found else None


def device_packs() -> dict[str, Path]:
    """Every device pack found, keyed by directory name."""
    packs: dict[str, Path] = {}
    for root in PACK_ROOTS:
        if not root.is_dir():
            continue
        for entry in sorted(root.iterdir()):
            if entry.is_dir() and any(entry.glob("*.pdsc")):
                packs[entry.name] = entry
    return packs


def pack_for(device: str) -> Path | None:
    """The pack that actually contains this device.

    Matched by looking for the device's `.PIC` file rather than by guessing
    from the part number: `18F26K22` lives in `pic18f-k` and `18F4550` in
    `pic18fxxxx`, and no rule over the name gets that right.
    """
    device = device.upper()
    for pack in device_packs().values():
        edc = pack / "edc"
        if not edc.is_dir():
            continue
        if any(path.stem.upper().endswith(f"PIC{device}") or path.stem.upper() == f"PIC{device}"
               for path in edc.glob("*.PIC")):
            return pack
    return None


def dfp_argument(pack: Path) -> Path:
    """What `-mdfp` actually wants.

    The `xc8` subdirectory, if the pack has one. Pointing at the pack root
    produces "no device-support files found", which reads as a missing pack
    rather than a path one level too high — an hour of looking in the wrong
    place, and the single most useful thing in this file.
    """
    inner = pack / "xc8"
    return inner if inner.is_dir() else pack


def compiler(name: str) -> Compiler:
    """Locate one toolchain and report whether it can actually compile."""
    if name in (XC8, XC16, XC32):
        binary = find_xc(name)
        if binary is None:
            return Compiler(name, None, problem="not installed")
        if name == XC32 and not _xc32_backend_present(binary):
            return Compiler(
                name, binary,
                problem="installed but its cc1 backend is missing — the driver "
                        "runs and then cannot execute cc1, so this is an "
                        "incomplete installation, not a bad flag",
            )
        return Compiler(name, binary, version=_version(binary))

    if name == ARM:
        found = shutil.which("arm-none-eabi-gcc")
    elif name == ARDUINO:
        found = shutil.which("arduino-cli")
    elif name == PICO:
        found = shutil.which("cmake")
    elif name == RUST:
        found = shutil.which("cargo")
    else:
        found = shutil.which("gcc")

    if not found:
        return Compiler(name, None, problem="not installed")
    return Compiler(name, Path(found), version=_version(Path(found)))


def _version(binary: Path) -> str:
    # arduino-cli spells it as a subcommand, not a flag.
    flag = ["version"] if binary.name == "arduino-cli" else ["--version"]
    try:
        result = subprocess.run([str(binary), *flag], capture_output=True,
                                text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return ""
    first = (result.stdout or result.stderr).strip().splitlines()
    return first[0][:80] if first else ""


def _xc32_backend_present(binary: Path) -> bool:
    """XC32's driver is present here and its `cc1` is not. Compile something
    trivial and look for exactly that, rather than trusting the file layout."""
    probe = Path("/tmp") / f"embedkit_probe_{int(time.time())}.c"
    pack = pack_for("32MX270F256B")
    if pack is None:
        # Without a pack the driver stops before it would reach cc1, so this
        # probe cannot answer the question either way.
        return True
    try:
        probe.write_text("int main(void){return 0;}\n")
        # The -mdfp is required: without it the driver rejects the command
        # line *before* invoking the backend, and the probe would report a
        # broken installation as healthy.
        result = subprocess.run(
            [str(binary), "-mprocessor=32MX270F256B", f"-mdfp={pack}",
             str(probe), "-o", "/dev/null"],
            capture_output=True, text=True, timeout=60,
        )
        return "cannot execute 'cc1'" not in (result.stderr or "")
    except (OSError, subprocess.SubprocessError):
        return False
    finally:
        probe.unlink(missing_ok=True)


@dataclass
class BuildResult:
    module: str
    ok: bool = False
    skipped: str = ""
    elapsed: float = 0.0
    output: list[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "module": self.module,
            "ok": self.ok,
            **({"skipped": self.skipped} if self.skipped else {}),
            "elapsed_s": round(self.elapsed, 2),
        }


def compile_pic(module: str, device: str, sources: list[str], family: str,
                *, out: Path, timeout: float = 600.0) -> BuildResult:
    """Compile one PIC firmware, with the device pack worked out for it."""
    result = BuildResult(module=module)
    tool = compiler(family)
    if not tool.usable:
        result.skipped = f"{family}: {tool.problem}"
        return result

    command = [str(tool.binary)]
    if family == XC8:
        pack = pack_for(device)
        if pack is None:
            result.skipped = f"no device pack found for {device}"
            return result
        command += [f"-mcpu={device}", f"-mdfp={dfp_argument(pack)}"]
    elif family == XC16:
        # XC16 finds its own device support from -mcpu — but only if the
        # device *library* is installed. Without it the compile succeeds and
        # the link fails on every special-function register (`_IFS0bits`,
        # `_SPI2BUF`), which reads like missing source rather than a missing
        # library.
        missing = _xc16_device_library_missing(tool.binary, device)
        if missing:
            result.skipped = missing
            return result
        command += [f"-mcpu={device}"]
    else:
        command += [f"-mprocessor={device}"]

    # Absolute, and created before the compiler runs: xc8-cc resolves -o
    # against its working directory, which is the module's, and reports a
    # missing directory rather than creating one.
    out = out if out.is_absolute() else (REPO_ROOT / out)
    out.parent.mkdir(parents=True, exist_ok=True)
    command += ["-o", str(out), *sources]

    started = time.perf_counter()
    try:
        completed = subprocess.run(command, cwd=MODULES_ROOT / module,
                                   capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        result.elapsed = time.perf_counter() - started
        result.output = [f"timed out after {timeout:g}s"]
        return result

    result.elapsed = time.perf_counter() - started
    result.ok = completed.returncode == 0 and out.exists()
    result.output = ((completed.stdout or "") + (completed.stderr or "")).strip().splitlines()[-10:]
    return result


def run_make(module: str, target: str = "", *, timeout: float = 900.0) -> BuildResult:
    """Build a module through its own Makefile, which is how it is meant to be built."""
    result = BuildResult(module=module)
    root = MODULES_ROOT / module
    makefile = next((root / name for name in ("Makefile", "makefile")
                     if (root / name).is_file()), None)
    if makefile is None:
        result.skipped = "no Makefile"
        return result

    started = time.perf_counter()
    try:
        completed = subprocess.run(["make", *([target] if target else [])],
                                   cwd=root, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        result.elapsed = time.perf_counter() - started
        result.output = [f"timed out after {timeout:g}s"]
        return result

    result.elapsed = time.perf_counter() - started
    result.ok = completed.returncode == 0
    result.output = ((completed.stdout or "") + (completed.stderr or "")).strip().splitlines()[-12:]
    return result


def build_module(module, *, out_root: Path | None = None,
                 timeout: float = 900.0) -> BuildResult:
    """Build one module the way that module is actually built.

    Six different ways, which is the whole reason this function exists: a
    Makefile for the ARM targets, `arduino-cli compile` for the sketches,
    `xc8-cc` with a device pack for the PICs, `cargo` for the Rust node, and
    nothing at all for the host-side tools.
    """
    out_root = out_root or (REPO_ROOT / ".build")
    family = module.family

    if family == HOST:
        return BuildResult(module=module.name, skipped="host-side only, nothing to compile")

    tool = compiler(family)
    if not tool.usable:
        return BuildResult(module=module.name, skipped=f"{family}: {tool.problem}")

    if family == ARM:
        return run_make(module.name, module.build, timeout=timeout)

    if family == ARDUINO:
        if not module.sketch:
            return BuildResult(module=module.name, skipped="no sketch directory recorded")
        libraries = [arg for path in module.libraries for arg in ("--libraries", path)]
        started = time.perf_counter()
        completed = subprocess.run(
            [str(tool.binary), "compile", "--fqbn", module.board,
             *libraries, module.sketch],
            cwd=module.path, capture_output=True, text=True, timeout=timeout,
        )
        result = BuildResult(module=module.name, ok=completed.returncode == 0)
        result.elapsed = time.perf_counter() - started
        output = (completed.stdout or "") + (completed.stderr or "")
        result.output = output.strip().splitlines()[-8:]

        # A missing third-party library is a missing dependency, not a broken
        # sketch, and saying so is the difference between "install this" and
        # "the firmware is wrong".
        if not result.ok:
            missing = _missing_arduino_library(output)
            if missing:
                result.skipped = f"needs the {missing} library, which is not installed"
        return result

    if family == RUST:
        started = time.perf_counter()
        completed = subprocess.run([str(tool.binary), "build", "--release"],
                                   cwd=module.path, capture_output=True,
                                   text=True, timeout=timeout)
        result = BuildResult(module=module.name, ok=completed.returncode == 0)
        result.elapsed = time.perf_counter() - started
        output = (completed.stdout or "") + (completed.stderr or "")
        result.output = output.strip().splitlines()[-8:]

        # AVR support lives in avr-hal, which needs `feature(asm_experimental_arch)`
        # and so a nightly compiler. On stable the failure is E0554 inside a
        # dependency, which reads as broken code rather than as a channel.
        if not result.ok and "E0554" in output:
            result.skipped = ("needs a nightly Rust toolchain — avr-hal uses "
                              "#![feature(asm_experimental_arch)], which stable "
                              "rejects with E0554")
        return result

    if family == MICROPYTHON:
        return BuildResult(
            module=module.name,
            skipped="MicroPython — the source runs on the board as-is, there is "
                    "nothing to compile",
        )

    if family == PICO:
        return BuildResult(
            module=module.name,
            skipped="needs the Pico SDK and a CMake configure step; not wired up here",
        )

    # The Microchip families: everything under firmware/, compiled as one unit.
    sources = _c_sources(module)
    if not sources:
        return BuildResult(module=module.name, skipped="no C sources found")
    return compile_pic(module.name, module.device, sources, family,
                       out=out_root / f"{module.name}.elf", timeout=timeout)


def _xc16_device_library_missing(binary: Path, device: str) -> str:
    """Whether this XC16 install actually has the device's library.

    The compiler ships a linker script for far more parts than it ships
    libraries for, so `-mcpu` is accepted and the link then fails on symbols
    that look like they should have come from the source.
    """
    root = binary.parent.parent
    library = root / "lib" / f"libp{device.upper()}-elf.a"
    alternative = root / "lib" / f"libp{device.lower()}-elf.a"
    if library.exists() or alternative.exists():
        return ""
    script = root / "support" / "PIC24F" / "gld" / f"p{device.upper()}.gld"
    if script.exists():
        return (f"xc16 has the linker script for {device} but not its device "
                f"library ({library.name}) — an incomplete installation, so "
                f"every special-function register is undefined at link")
    return f"xc16 has no device support for {device}"


# Headers that belong to a third-party library rather than to the sketch.
_LIBRARY_HEADERS = {
    "tensorflow": "TensorFlow Lite Micro",
    "RadioHead": "RadioHead",
    "RH_": "RadioHead",
    "Adafruit": "Adafruit",
    "SdFat": "SdFat",
    "LoRa": "LoRa",
}


def _missing_arduino_library(output: str) -> str:
    match = re.search(r"fatal error: ([\w./-]+): No such file", output)
    if not match:
        return ""
    header = match.group(1)
    for prefix, library in _LIBRARY_HEADERS.items():
        if header.startswith(prefix):
            return library
    return ""


def _c_sources(module) -> list[str]:
    """Every C file that belongs to the firmware.

    Not just `firmware/`: the bootloader keeps its own under `bootloader/`, and
    everything excludes `host/`, `tools/` and `tests/`, which are host programs
    that would not compile for a PIC and should not be linked into one.
    """
    excluded = {"host", "tools", "tests", "docs", "build", "app_template"}
    found = []
    for path in sorted(module.path.rglob("*.c")):
        relative = path.relative_to(module.path)
        if excluded & set(relative.parts):
            continue
        found.append(str(relative))
    return found


def report() -> list[Compiler]:
    """Every toolchain this repository can use, and the state of each."""
    return [compiler(name) for name in (XC8, XC16, XC32, ARM, ARDUINO, PICO,
                                       MICROPYTHON, RUST, HOST)]
