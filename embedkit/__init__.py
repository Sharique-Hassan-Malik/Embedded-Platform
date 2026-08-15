"""embedkit — what nineteen firmware projects in this repository share.

    from embedkit import toolchain, registry
    toolchain.build_module(registry.module("rtos"))

Three things: the build knowledge for five device families across three
vendors' toolchains, one host-side serial link, and one CRC. The build
knowledge is the valuable part — every project knew how to build itself, in a
Makefile, and none of it was anywhere you could find without opening one.
"""

from . import crc, link, registry, toolchain

__version__ = "1.0.0"
__all__ = ["toolchain", "registry", "link", "crc"]
