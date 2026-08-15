"""Cross-module tests — what is only true because these nineteen are one repo.

Each module's own logic is tested in its own folder, on the host, where it has
tests at all. What is tested here is the part that did not exist before: the
build knowledge, the shared serial link and the shared CRC.

The tests that actually invoke a compiler are marked `slow`; the rest run in
under a second and check the things that go wrong silently — a device pack
resolved to the wrong family, a CRC variant that disagrees with the firmware.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))

from embedkit import registry, toolchain  # noqa: E402
from embedkit.crc import check, crc16  # noqa: E402
from embedkit.link import Frame, FrameLink, LinkError, TextLink  # noqa: E402

slow = pytest.mark.slow


class TestLayout:
    def test_every_module_exists_with_a_readme(self):
        for module in registry.modules():
            assert module.path.is_dir(), f"{module.name} is missing"
            assert (module.path / "README.md").is_file(), f"{module.name} has no README"

    def test_unknown_module_is_a_clear_error(self):
        with pytest.raises(KeyError, match="unknown module"):
            registry.module("nope")

    def test_every_arduino_module_names_a_sketch_that_exists(self):
        """`arduino-cli compile` takes a directory, and a wrong one fails with a
        message about the sketch rather than about the path."""
        for module in registry.modules(family=toolchain.ARDUINO):
            if not module.sketch:
                continue
            assert (module.path / module.sketch).is_dir(), \
                f"{module.name}: {module.sketch} does not exist"

    def test_every_microchip_module_names_a_real_device(self):
        for family in (toolchain.XC8, toolchain.XC16, toolchain.XC32):
            for module in registry.modules(family=family):
                assert module.device, f"{module.name} has no device"
                assert toolchain.pack_for(module.device) is not None, \
                    f"no device pack contains {module.device}"


class TestToolchainDiscovery:
    def test_it_reports_every_family(self):
        names = {tool.name for tool in toolchain.report()}
        assert names >= set(registry.families()) - {toolchain.HOST}

    def test_an_unusable_toolchain_explains_itself(self):
        """The whole point: "not installed" and "installed but broken" are
        different problems and send you to different places."""
        for tool in toolchain.report():
            assert tool.usable or tool.problem, f"{tool.name} is unusable with no reason"

    def test_device_packs_are_matched_by_content_not_by_name(self):
        """`18F26K22` is in `pic18f-k` and `18F4550` is in `pic18fxxxx`. No rule
        over the part number gets that right, so the packs are searched."""
        packs = toolchain.device_packs()
        if not packs:
            pytest.skip("no device packs installed")

        k_series = toolchain.pack_for("18F26K22")
        classic = toolchain.pack_for("18F4550")
        assert k_series is not None and classic is not None
        assert k_series != classic, "two different families resolved to one pack"

    def test_the_dfp_argument_points_at_the_xc8_subdirectory(self):
        """The single most useful thing in the toolchain module.

        `-mdfp=<pack>` reports "no device-support files found", which reads as a
        missing pack rather than a path one level too high.
        """
        pack = toolchain.pack_for("18F26K22")
        if pack is None:
            pytest.skip("no PIC18F-K pack installed")
        argument = toolchain.dfp_argument(pack)
        if (pack / "xc8").is_dir():
            assert argument == pack / "xc8"
        else:
            assert argument == pack

    def test_an_unknown_device_resolves_to_nothing(self):
        assert toolchain.pack_for("99F99999") is None


class TestCrc:
    def test_it_matches_the_published_check_value(self):
        """CRC-16/CCITT-FALSE over "123456789" is 0x29B1. The variant matters:
        CCITT-FALSE and "true" CCITT differ only in the initial value, disagree
        on every input, and are both called CRC-16 CCITT in datasheets."""
        assert crc16(b"123456789") == 0x29B1

    def test_empty_input_is_the_initial_value(self):
        assert crc16(b"") == 0xFFFF

    def test_it_detects_a_single_bit_flip(self):
        original = bytes(range(32))
        flipped = bytearray(original)
        flipped[7] ^= 0x01
        assert crc16(original) != crc16(bytes(flipped))

    def test_check_accepts_a_matching_value(self):
        data = b"sample"
        assert check(data, crc16(data))
        assert not check(data, crc16(data) ^ 1)


class TestSharedLink:
    def test_opening_without_pyserial_says_what_to_install(self, monkeypatch):
        import builtins

        real_import = builtins.__import__

        def refuse(name, *args, **kwargs):
            if name == "serial":
                raise ImportError("no serial")
            return real_import(name, *args, **kwargs)

        monkeypatch.setattr(builtins, "__import__", refuse)
        with pytest.raises(LinkError, match="pyserial"):
            TextLink(port="/dev/null").open()

    def test_a_frame_with_a_bad_crc_is_dropped(self):
        """A corrupted sample is worse than a missing one."""
        link = FrameLink(port="/dev/null")
        payload = b"\x01\x02\x03"
        header = bytes([link.magic, 7, len(payload), 0])
        good = crc16(bytes([7, len(payload), 0]) + payload)

        link._serial = _FakeSerial(header + payload + bytes([good & 0xFF, good >> 8]))
        assert [f.payload for f in link.read()] == [payload]

        link._serial = _FakeSerial(header + payload + b"\x00\x00")
        assert list(link.read()) == []

    def test_a_frame_with_the_wrong_magic_is_ignored(self):
        link = FrameLink(port="/dev/null")
        link._serial = _FakeSerial(b"\x00\x07\x03\x00abc\x00\x00")
        assert list(link.read()) == []

    def test_a_truncated_frame_is_ignored(self):
        link = FrameLink(port="/dev/null")
        link._serial = _FakeSerial(bytes([0xA5, 7, 8, 0]) + b"only-3")
        assert list(link.read()) == []

    def test_text_lines_come_back_stripped(self):
        link = TextLink(port="/dev/null")
        link._serial = _FakeSerial(b"reading=42\r\n")
        assert list(link.read()) == ["reading=42"]


class _FakeSerial:
    """Enough of pyserial to exercise the framing without a port."""

    def __init__(self, data: bytes) -> None:
        self._data = data
        self._at = 0

    def read(self, count: int) -> bytes:
        chunk = self._data[self._at:self._at + count]
        self._at += len(chunk)
        return chunk

    def readline(self) -> bytes:
        end = self._data.find(b"\n", self._at)
        end = len(self._data) if end < 0 else end + 1
        chunk = self._data[self._at:end]
        self._at = end
        return chunk

    def close(self) -> None:
        pass


@slow
class TestBuilds:
    """Compiling firmware for five families. Minutes, not seconds."""

    @pytest.mark.parametrize(
        "module", registry.modules(), ids=lambda m: m.name
    )
    def test_module_builds_or_says_why_not(self, module):
        if module.name == "bootloader":
            # A real defect, kept as a failing case rather than papered over:
            # the 65-byte P-256 public key does not fit in any PIC18 bank once
            # p256_verify's compiled-stack working set is placed. Fixing it
            # means laying out crypto scratch space across p256.c, which is a
            # measurement, not a guess. See docs/known-issues.md.
            pytest.xfail("bootloader exceeds PIC18 RAM — docs/known-issues.md")
        result = toolchain.build_module(module, timeout=900)
        if result.skipped:
            pytest.skip(result.skipped)
        assert result.ok, "\n".join(result.output)

    def test_the_rtos_produces_a_binary(self):
        """It did not compile at all before this repository was assembled:
        a use-before-declaration, an opaque type allocated statically, and a
        linker script with no `end` symbol for newlib's sbrk."""
        module = registry.module("rtos")
        result = toolchain.build_module(module, timeout=900)
        if result.skipped:
            pytest.skip(result.skipped)
        assert result.ok, "\n".join(result.output)
        assert (module.path / "build" / "cortex-rtos.bin").is_file()

    def test_the_power_profiler_fits_in_its_target(self):
        """It did not: a 2048-entry ring buffer of 5-byte samples is 10 kB on a
        part with 2 kB of RAM, and the sketch reported 520% of dynamic memory."""
        module = registry.module("power-profiler")
        result = toolchain.build_module(module, timeout=900)
        if result.skipped:
            pytest.skip(result.skipped)
        assert result.ok, "\n".join(result.output)


class TestCli:
    def test_toolchains_reports_every_family(self, capsys):
        from embedkit import cli

        assert cli.main(["toolchains"]) == 0
        out = capsys.readouterr().out
        for tool in toolchain.report():
            assert tool.name in out

    def test_modules_listing_covers_the_repo(self, capsys):
        from embedkit import cli

        assert cli.main(["modules"]) == 0
        out = capsys.readouterr().out
        for module in registry.modules():
            assert module.name in out
