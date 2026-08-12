"""
RTOS Scheduler Visualizer — host-side analysis and visualization tool.

Decodes binary trace frames streamed over UART from a FreeRTOS target,
reconstructs scheduling history, and produces:

  - An interactive HTML Gantt chart of task execution
  - Priority inversion detection with causal chain annotation
  - Deadline miss reporting with overrun duration
  - Per-task CPU utilization and context-switch statistics
  - A Rich terminal summary
"""

__version__ = "1.0.0"
