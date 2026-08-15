"""
RTOS Scheduler Visualizer — command-line interface.

Subcommands:

  decode FILE           Decode a binary trace file and print a terminal summary
  html   FILE           Decode and render an interactive HTML Gantt chart
  demo                  Generate and visualize a synthetic trace (no hardware needed)
  live   PORT           Capture live from a UART port (requires pyserial)

Examples
--------
Decode a saved binary trace:
    rtos-viz decode trace.bin

Render an HTML Gantt chart:
    rtos-viz html trace.bin --output gantt.html

Generate a synthetic demo:
    rtos-viz demo --output demo.html --inversion --deadline-miss

Live capture from /dev/ttyUSB0:
    rtos-viz live /dev/ttyUSB0 --baud 921600 --output live.html
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from rich.console import Console

from scheduler_viz import __version__
from scheduler_viz.core.decoder import Decoder
from scheduler_viz.core.generator import generate
from scheduler_viz.core.model import build_model
from scheduler_viz.report.gantt import render_html
from scheduler_viz.report.terminal import render


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="rtos-viz",
        description="RTOS Scheduler Visualizer",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--version", "-V", action="version", version=f"rtos-viz {__version__}")

    sub = p.add_subparsers(dest="command", required=True)

    # decode
    dec = sub.add_parser("decode", help="Decode a binary trace file and show terminal summary")
    dec.add_argument("file", metavar="FILE")
    dec.add_argument("--tps", type=int, default=1000, metavar="HZ",
                     help="Tick rate in Hz (default: 1000)")
    dec.add_argument("--no-color", action="store_true")

    # html
    htm = sub.add_parser("html", help="Decode and render an interactive HTML Gantt chart")
    htm.add_argument("file", metavar="FILE")
    htm.add_argument("--output", "-o", metavar="FILE", default="gantt.html")
    htm.add_argument("--tps", type=int, default=1000, metavar="HZ")
    htm.add_argument("--title", default="RTOS Scheduler Trace")
    htm.add_argument("--no-color", action="store_true")

    # demo
    demo = sub.add_parser("demo", help="Generate a synthetic trace and visualize it")
    demo.add_argument("--output", "-o", metavar="FILE", default="demo.html")
    demo.add_argument("--duration", type=int, default=2000, metavar="TICKS",
                      help="Trace duration in ticks (default: 2000)")
    demo.add_argument("--no-inversion", dest="inversion", action="store_false", default=True)
    demo.add_argument("--no-deadline-miss", dest="deadline_miss", action="store_false", default=True)
    demo.add_argument("--tps", type=int, default=1000, metavar="HZ")
    demo.add_argument("--no-color", action="store_true")
    demo.add_argument("--seed", type=int, default=42)

    # live
    live = sub.add_parser("live", help="Live capture from UART (requires pyserial)")
    live.add_argument("port", metavar="PORT")
    live.add_argument("--baud", type=int, default=921600)
    live.add_argument("--output", "-o", metavar="FILE", default="live.html")
    live.add_argument("--duration", type=float, default=10.0, metavar="SEC",
                      help="Capture duration in seconds (default: 10)")
    live.add_argument("--tps", type=int, default=1000)
    live.add_argument("--no-color", action="store_true")

    return p


def cmd_decode(args: argparse.Namespace) -> int:
    console = Console(no_color=args.no_color)
    path = Path(args.file)
    if not path.exists():
        console.print(f"[red]error:[/red] file not found: {path}")
        return 1
    data = path.read_bytes()
    decoder = Decoder()
    events = decoder.feed(data)
    model = build_model(events, ticks_per_sec=args.tps)
    render(model, console, ticks_per_sec=args.tps)
    console.print(f"\n[dim]Decoded {len(events)} events from {len(data)} bytes[/dim]")
    return 0


def cmd_html(args: argparse.Namespace) -> int:
    console = Console(no_color=args.no_color)
    path = Path(args.file)
    if not path.exists():
        console.print(f"[red]error:[/red] file not found: {path}")
        return 1
    data = path.read_bytes()
    decoder = Decoder()
    events = decoder.feed(data)
    model = build_model(events, ticks_per_sec=args.tps)
    render(model, console, ticks_per_sec=args.tps)
    html = render_html(model, title=args.title, ticks_per_sec=args.tps)
    Path(args.output).write_text(html, encoding="utf-8")
    console.print(f"\nGantt chart saved → [bold]{args.output}[/bold]")
    return 0


def cmd_demo(args: argparse.Namespace) -> int:
    console = Console(no_color=args.no_color)
    console.print("[cyan]Generating synthetic RTOS trace…[/cyan]")
    raw = generate(
        duration_ticks=args.duration,
        seed=args.seed,
        inversion=args.inversion,
        deadline_miss=args.deadline_miss,
    )
    decoder = Decoder()
    events = decoder.feed(raw)
    model = build_model(events, ticks_per_sec=args.tps)
    render(model, console, ticks_per_sec=args.tps)
    html = render_html(model, title="RTOS Scheduler Demo", ticks_per_sec=args.tps)
    Path(args.output).write_text(html, encoding="utf-8")
    console.print(f"\nGantt chart saved → [bold]{args.output}[/bold]")
    return 0


def cmd_live(args: argparse.Namespace) -> int:
    console = Console(no_color=args.no_color)
    from scheduler_viz.transport import SerialTransport

    all_events = []

    def on_events(evts):
        all_events.extend(evts)
        console.print(f"[dim]  received {len(evts)} events (total {len(all_events)})[/dim]")

    console.print(f"[cyan]Capturing from {args.port} at {args.baud} baud…[/cyan]")
    transport = SerialTransport(args.port, args.baud, on_events=on_events)
    try:
        transport.start()
        time.sleep(args.duration)
    except KeyboardInterrupt:
        pass
    finally:
        transport.stop()

    if not all_events:
        console.print("[red]No events received.[/red]")
        return 1

    model = build_model(all_events, ticks_per_sec=args.tps)
    render(model, console, ticks_per_sec=args.tps)
    html = render_html(model, title=f"Live Capture — {args.port}", ticks_per_sec=args.tps)
    Path(args.output).write_text(html, encoding="utf-8")
    console.print(f"\nGantt chart saved → [bold]{args.output}[/bold]")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)

    if args.command == "decode":
        return cmd_decode(args)
    if args.command == "html":
        return cmd_html(args)
    if args.command == "demo":
        return cmd_demo(args)
    if args.command == "live":
        return cmd_live(args)
    return 1


if __name__ == "__main__":
    sys.exit(main())
