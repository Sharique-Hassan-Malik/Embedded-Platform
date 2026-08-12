"""
Rich terminal summary for a decoded scheduling trace.

Renders:
  - Trace metadata (duration, frame count, tick rate)
  - Per-task statistics table (priority, CPU%, context switches)
  - Priority inversion list with causal chain
  - Deadline miss list
"""

from __future__ import annotations

from rich import box
from rich.console import Console
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

from scheduler_viz.core.model import SchedulingModel, cpu_utilization


def render(
    model: SchedulingModel,
    console: Console | None = None,
    ticks_per_sec: int = 1000,
) -> None:
    if console is None:
        console = Console()

    _summary(model, console, ticks_per_sec)
    _task_table(model, console, ticks_per_sec)
    _inversions(model, console, ticks_per_sec)
    _misses(model, console, ticks_per_sec)


def _ms(ticks: int, tps: int) -> str:
    return f"{ticks / tps * 1000:.2f} ms"


def _summary(model: SchedulingModel, console: Console, tps: int) -> None:
    dur = model.last_ts - model.first_ts
    lines = [
        f"[bold]Duration:[/bold]          {_ms(dur, tps)}  ({dur} ticks)",
        f"[bold]Tasks:[/bold]             {len(model.tasks)}",
        f"[bold]Execution slices:[/bold]  {len(model.slices)}",
        f"[bold]Priority inversions:[/bold] "
        + (f"[bold yellow]{len(model.inversions)}[/bold yellow]" if model.inversions else "[green]0[/green]"),
        f"[bold]Deadline misses:[/bold]   "
        + (f"[bold red]{len(model.misses)}[/bold red]" if model.misses else "[green]0[/green]"),
    ]
    color = "bright_red" if model.misses else "yellow" if model.inversions else "green"
    console.print(Panel(
        "\n".join(lines),
        title="[bold bright_cyan]RTOS Scheduler Visualizer[/bold bright_cyan]",
        border_style=color,
    ))


def _task_table(model: SchedulingModel, console: Console, tps: int) -> None:
    util = cpu_utilization(model)
    tbl = Table(
        title="Task Statistics",
        box=box.SIMPLE_HEAD,
        title_style="bold bright_cyan",
    )
    tbl.add_column("ID",        style="dim",     width=4)
    tbl.add_column("Name",      style="bold",    min_width=14)
    tbl.add_column("Priority",  justify="right", width=10)
    tbl.add_column("CPU %",     justify="right", width=8)
    tbl.add_column("Switches",  justify="right", width=10)
    tbl.add_column("Miss",      justify="center",width=6)

    miss_ids = {m.task_id for m in model.misses}
    for tid in sorted(model.tasks):
        t = model.tasks[tid]
        u = util.get(tid, 0.0)
        m = "✗" if tid in miss_ids else "—"
        m_style = "bold red" if tid in miss_ids else "dim"
        tbl.add_row(
            str(tid),
            t.name,
            str(t.priority),
            f"{u * 100:.1f}",
            str(t.switch_in_count),
            Text(m, style=m_style),
        )
    console.print(tbl)


def _inversions(model: SchedulingModel, console: Console, tps: int) -> None:
    if not model.inversions:
        console.print(Panel("[green]No priority inversions detected.[/green]",
                            title="Priority Inversions", border_style="green"))
        return

    tbl = Table(
        title=f"Priority Inversions ({len(model.inversions)})",
        box=box.SIMPLE_HEAD,
        title_style="bold yellow",
    )
    tbl.add_column("Mutex",     style="cyan",   width=7)
    tbl.add_column("High Task", style="bold",   width=12)
    tbl.add_column("Low Task",  style="bold",   width=12)
    tbl.add_column("Start",     style="yellow", width=12)
    tbl.add_column("Duration",  justify="right",width=12)
    tbl.add_column("Inherited", justify="center",width=10)

    for inv in model.inversions:
        high_name = model.tasks[inv.high_task].name if inv.high_task in model.tasks else str(inv.high_task)
        low_name  = model.tasks[inv.low_task].name  if inv.low_task  in model.tasks else str(inv.low_task)
        inh = Text("yes", style="green") if inv.resolved_by_inheritance else Text("no", style="red")
        tbl.add_row(
            str(inv.mutex_id),
            high_name,
            low_name,
            f"{inv.start_ts} tick",
            _ms(inv.duration, tps),
            inh,
        )
    console.print(tbl)


def _misses(model: SchedulingModel, console: Console, tps: int) -> None:
    if not model.misses:
        console.print(Panel("[green]No deadline misses detected.[/green]",
                            title="Deadline Misses", border_style="green"))
        return

    tbl = Table(
        title=f"Deadline Misses ({len(model.misses)})",
        box=box.SIMPLE_HEAD,
        title_style="bold bright_red",
    )
    tbl.add_column("Task",    style="bold",   min_width=12)
    tbl.add_column("At tick", style="yellow", width=10)
    tbl.add_column("Overrun", justify="right",width=14)

    for m in model.misses:
        name = model.tasks[m.task_id].name if m.task_id in model.tasks else str(m.task_id)
        tbl.add_row(name, str(m.ts), _ms(m.overrun_ticks, tps))

    console.print(tbl)
