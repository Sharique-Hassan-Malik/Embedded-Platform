"""`embed` — one command over nineteen firmware projects.

    embed toolchains          which compilers are here, and what is wrong with them
    embed modules             what is here, what builds it, what it targets
    embed build               build everything that can be built
    embed build --only rtos
"""

from __future__ import annotations

import argparse
import sys

from . import registry, toolchain


def _wrap(text: str, width: int) -> list[str]:
    words, lines, line = text.split(), [], []
    for word in words:
        if sum(len(w) + 1 for w in line) + len(word) > width and line:
            lines.append(" ".join(line))
            line = []
        line.append(word)
    if line:
        lines.append(" ".join(line))
    return lines


def _cmd_toolchains(args) -> int:
    print()
    for tool in toolchain.report():
        state = "ready" if tool.usable else "unusable"
        print(f"  {tool.name:9} {state:9} {tool.version or ''}")
        if tool.problem:
            for line in _wrap(tool.problem, 66):
                print(f"  {'':19} {line}")
        print()

    packs = toolchain.device_packs()
    print(f"  device packs: {', '.join(sorted(packs)) if packs else 'none found'}")
    if packs:
        print("  (XC8 wants the pack's `xc8` subdirectory, not the pack root —")
        print("   pointing at the root reports 'no device-support files found')")
    print()
    return 0


def _cmd_modules(args) -> int:
    print()
    for module in registry.modules(role=args.role, family=args.family):
        target = f"{module.device}" if module.device else ""
        print(f"  {module.name:18} {module.family:8} {module.role:10} {target}")
        print(f"  {'':18} {module.title}")
        for line in _wrap(module.summary, 66):
            print(f"  {'':18} {line}")
        if module.host_side:
            print(f"  {'':18} ships a host-side program too")
        print()
    return 0


def _cmd_build(args) -> int:
    selected = registry.modules(role=args.role, family=args.family)
    if args.only:
        selected = [m for m in selected if m.name in args.only]
    if not selected:
        print("embed: nothing selected", file=sys.stderr)
        return 2

    print()
    failures = 0
    for module in selected:
        result = toolchain.build_module(module, timeout=args.timeout)
        if result.skipped:
            state = f"skip — {result.skipped}"
        elif result.ok:
            state = "PASS"
        else:
            state = "FAIL"
            failures += 1
        print(f"  {module.name:18} {module.family:8} {state}")
        if not result.ok and not result.skipped and args.verbose:
            for line in result.output:
                print(f"      {line}")
    print()
    return 1 if failures else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="embed",
        description="Nineteen firmware projects, one build harness.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("toolchains", help="which compilers are installed and usable")

    modules = sub.add_parser("modules", help="what is here")
    modules.add_argument("--role")
    modules.add_argument("--family")

    build = sub.add_parser("build", help="build firmware")
    build.add_argument("--only", action="append", metavar="NAME")
    build.add_argument("--role")
    build.add_argument("--family")
    build.add_argument("--timeout", type=float, default=900.0)
    build.add_argument("-v", "--verbose", action="store_true")

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return {
        "toolchains": _cmd_toolchains,
        "modules": _cmd_modules,
        "build": _cmd_build,
    }[args.command](args)


if __name__ == "__main__":
    raise SystemExit(main())
