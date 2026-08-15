"""
HTML Gantt chart renderer.

Produces a self-contained interactive HTML file with:
  - A zoomable/pannable Gantt timeline per task (SVG + vanilla JS)
  - Priority inversion spans highlighted in amber with tooltip
  - Deadline miss markers in red
  - Hover tooltips on every execution slice
  - A legend and statistics table
"""

from __future__ import annotations

import json
from typing import Dict, List

from scheduler_viz.core.model import (
    SchedulingModel,
    ExecSlice,
    PriorityInversion,
    DeadlineMiss,
    cpu_utilization,
)

# Colours per task (up to 16)
_TASK_COLORS = [
    "#4e9af1", "#f1a84e", "#4ef17a", "#f14e4e",
    "#a84ef1", "#4ef1f1", "#f1f14e", "#f14ea8",
    "#4ef1a8", "#a8f14e", "#4e4ef1", "#f1a84e",
    "#8af14e", "#4e8af1", "#f18a4e", "#8af1f1",
]
_INVERSION_COLOR  = "rgba(255,165,0,0.35)"
_DEADLINE_COLOR   = "#e63946"
_BLOCKED_COLOR    = "#888888"
_PREEMPT_COLOR    = None    # uses task color


def render_html(
    model: SchedulingModel,
    title: str = "RTOS Scheduler Trace",
    ticks_per_sec: int = 1000,
) -> str:
    util = cpu_utilization(model)
    task_ids = sorted(model.tasks.keys())
    task_names = {tid: model.tasks[tid].name or f"Task{tid}" for tid in task_ids}

    # ── build JSON data payload ──────────────────────────────────────
    slices_data = [
        {
            "tid":   s.task_id,
            "start": s.start_ts,
            "end":   s.end_ts,
            "prio":  s.priority,
            "reason": s.reason,
        }
        for s in model.slices
    ]

    inversions_data = [
        {
            "mutex":  inv.mutex_id,
            "high":   inv.high_task,
            "low":    inv.low_task,
            "start":  inv.start_ts,
            "end":    inv.end_ts,
            "dur":    inv.duration,
            "inherit": inv.resolved_by_inheritance,
        }
        for inv in model.inversions
    ]

    misses_data = [
        {"tid": m.task_id, "ts": m.ts, "overrun": m.overrun_ticks}
        for m in model.misses
    ]

    tasks_data = [
        {
            "id":    tid,
            "name":  task_names[tid],
            "prio":  model.tasks[tid].priority,
            "util":  round(util.get(tid, 0.0) * 100, 1),
            "switches": model.tasks[tid].switch_in_count,
            "color": _TASK_COLORS[tid % len(_TASK_COLORS)],
        }
        for tid in task_ids
    ]

    payload = json.dumps({
        "title":       title,
        "first_ts":    model.first_ts,
        "last_ts":     model.last_ts,
        "tps":         ticks_per_sec,
        "tasks":       tasks_data,
        "slices":      slices_data,
        "inversions":  inversions_data,
        "misses":      misses_data,
        "miss_count":  len(model.misses),
        "inv_count":   len(model.inversions),
    }, separators=(",", ":"))

    return _HTML_TEMPLATE.replace("__DATA__", payload).replace("__TITLE__", title)


_HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>__TITLE__</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',Arial,sans-serif;background:#1a1a2e;color:#e0e0e0}
h1{padding:16px 24px;font-size:1.2rem;border-bottom:1px solid #333;color:#7ec8e3}
#controls{display:flex;gap:12px;align-items:center;padding:10px 24px;border-bottom:1px solid #333}
button{background:#333;color:#e0e0e0;border:1px solid #555;border-radius:4px;
       padding:4px 12px;cursor:pointer;font-size:.85rem}
button:hover{background:#444}
label{font-size:.85rem;color:#aaa}
#canvas-wrap{overflow-x:auto;padding:16px 24px}
canvas{display:block;cursor:grab}
canvas.grabbing{cursor:grabbing}
#tooltip{position:fixed;background:#222;border:1px solid #555;border-radius:6px;
         padding:8px 12px;font-size:.8rem;pointer-events:none;display:none;
         max-width:300px;line-height:1.5;z-index:100}
#stats{padding:16px 24px;border-top:1px solid #333}
h2{font-size:1rem;color:#7ec8e3;margin-bottom:10px}
table{border-collapse:collapse;width:100%;max-width:700px;font-size:.85rem}
th,td{padding:6px 14px;border:1px solid #333;text-align:left}
th{background:#252540;color:#aaa}
.badge{display:inline-block;padding:1px 7px;border-radius:9px;font-size:.75rem}
.inv{background:#ff8c00;color:#000}.miss{background:#e63946;color:#fff}
.ok{background:#2a9d5c;color:#fff}
</style>
</head>
<body>
<h1 id="page-title">__TITLE__</h1>
<div id="controls">
  <button id="btn-zoom-in">Zoom In</button>
  <button id="btn-zoom-out">Zoom Out</button>
  <button id="btn-reset">Reset</button>
  <label>Scroll: drag or mouse wheel</label>
  <span id="info-badge" style="margin-left:auto;font-size:.85rem"></span>
</div>
<div id="canvas-wrap"><canvas id="gantt"></canvas></div>
<div id="tooltip"></div>
<div id="stats">
  <h2>Task Statistics</h2>
  <table id="stat-table"></table>
  <div id="anomaly-list" style="margin-top:16px"></div>
</div>
<script>
const D = __DATA__;

const ROW_H    = 40;
const LABEL_W  = 110;
const PAD_TOP  = 40;   // room for tick axis
const SCALE0   = 0.3;  // pixels per tick at zoom=1

const canvas  = document.getElementById('gantt');
const ctx     = canvas.getContext('2d');
const tip     = document.getElementById('tooltip');

let zoom   = 1.0;
let panX   = 0;
let drag   = false;
let dragX0 = 0;
let panX0  = 0;

const totalTicks = D.last_ts - D.first_ts;
const rows = D.tasks.length;
canvas.height = PAD_TOP + rows * ROW_H + 20;
canvas.style.height = canvas.height + 'px';

function ticksToX(t){ return LABEL_W + (t - D.first_ts) * SCALE0 * zoom + panX; }
function xToTick(x){ return D.first_ts + (x - LABEL_W - panX) / (SCALE0 * zoom); }

function resize(){
  canvas.width = Math.max(900, LABEL_W + totalTicks * SCALE0 * zoom + 40);
  canvas.style.width = canvas.width + 'px';
}

function draw(){
  resize();
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // Background rows
  D.tasks.forEach((task, i) => {
    const y = PAD_TOP + i * ROW_H;
    ctx.fillStyle = i % 2 === 0 ? '#1e1e30' : '#252538';
    ctx.fillRect(LABEL_W, y, canvas.width - LABEL_W, ROW_H);
    ctx.fillStyle = '#ccc';
    ctx.font = '12px Arial';
    ctx.fillText(task.name, 6, y + ROW_H / 2 + 4);
    ctx.fillStyle = '#555';
    ctx.font = '10px Arial';
    ctx.fillText('P' + task.prio, 6, y + ROW_H / 2 + 16);
  });

  // Tick axis
  const tickStep = chooseTick();
  ctx.strokeStyle = '#333';
  ctx.lineWidth   = 1;
  for(let t = D.first_ts; t <= D.last_ts + tickStep; t += tickStep){
    const x = ticksToX(t);
    if(x < LABEL_W || x > canvas.width) continue;
    ctx.beginPath(); ctx.moveTo(x, PAD_TOP - 12); ctx.lineTo(x, canvas.height - 20);
    ctx.stroke();
    ctx.fillStyle = '#888';
    ctx.font = '10px Arial';
    const label = ((t - D.first_ts) / D.tps * 1000).toFixed(0) + 'ms';
    ctx.fillText(label, x + 2, PAD_TOP - 2);
  }

  // Inversion spans
  D.inversions.forEach(inv => {
    const x1 = ticksToX(inv.start);
    const x2 = ticksToX(inv.end);
    ctx.fillStyle = 'rgba(255,140,0,0.2)';
    ctx.fillRect(x1, PAD_TOP, x2 - x1, rows * ROW_H);
    ctx.strokeStyle = '#ff8c00';
    ctx.lineWidth = 1;
    ctx.setLineDash([4,4]);
    ctx.strokeRect(x1, PAD_TOP, x2 - x1, rows * ROW_H);
    ctx.setLineDash([]);
  });

  // Execution slices
  const taskIndex = {};
  D.tasks.forEach((t, i) => { taskIndex[t.id] = i; });

  D.slices.forEach(s => {
    const i = taskIndex[s.tid];
    if(i === undefined) return;
    const x1 = ticksToX(s.start);
    const x2 = ticksToX(s.end);
    const w  = Math.max(1, x2 - x1);
    const y  = PAD_TOP + i * ROW_H + 4;
    const h  = ROW_H - 8;
    const color = s.reason === 'block' ? '#666' : D.tasks[i].color;
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.roundRect(x1, y, w, h, 2);
    ctx.fill();
    if(w > 18){
      ctx.fillStyle = '#000';
      ctx.font = '9px Arial';
      ctx.fillText('P' + s.prio, x1 + 3, y + h / 2 + 3);
    }
  });

  // Deadline miss markers
  D.misses.forEach(m => {
    const i = taskIndex[m.tid];
    if(i === undefined) return;
    const x = ticksToX(m.ts);
    const y = PAD_TOP + i * ROW_H;
    ctx.strokeStyle = '#e63946';
    ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x, y + ROW_H);
    ctx.stroke();
    ctx.fillStyle = '#e63946';
    ctx.font = 'bold 10px Arial';
    ctx.fillText('!', x - 3, y + 13);
  });

  // Label separator
  ctx.strokeStyle = '#444';
  ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(LABEL_W, 0); ctx.lineTo(LABEL_W, canvas.height); ctx.stroke();
}

function chooseTick(){
  const pixPerTick = SCALE0 * zoom;
  const targets = [10,25,50,100,200,500,1000,2000,5000];
  for(const t of targets){ if(t * pixPerTick >= 40) return t; }
  return 10000;
}

// ── interactions ──────────────────────────────────────────────────
canvas.addEventListener('wheel', e => {
  e.preventDefault();
  const mouseX = e.offsetX;
  const tickAtMouse = xToTick(mouseX);
  const factor = e.deltaY < 0 ? 1.2 : 1/1.2;
  zoom = Math.max(0.05, Math.min(50, zoom * factor));
  panX = mouseX - LABEL_W - (tickAtMouse - D.first_ts) * SCALE0 * zoom;
  draw();
}, {passive: false});

canvas.addEventListener('mousedown', e => {
  drag=true; dragX0=e.clientX; panX0=panX; canvas.classList.add('grabbing');
});
window.addEventListener('mousemove', e => {
  if(!drag) return;
  panX = panX0 + e.clientX - dragX0;
  draw();
});
window.addEventListener('mouseup', () => { drag=false; canvas.classList.remove('grabbing'); });

canvas.addEventListener('mousemove', e => {
  const tx = xToTick(e.offsetX);
  const row = Math.floor((e.offsetY - PAD_TOP) / ROW_H);
  if(row < 0 || row >= D.tasks.length){ tip.style.display='none'; return; }
  const tid = D.tasks[row].id;

  // Find slice under cursor
  const s = D.slices.find(s => s.tid===tid && tx >= s.start && tx < s.end);
  if(s){
    const dur_ms = ((s.end - s.start) / D.tps * 1000).toFixed(1);
    tip.innerHTML =
      `<b>${D.tasks[row].name}</b><br>` +
      `Start: ${s.start} tick | End: ${s.end} tick<br>` +
      `Duration: ${dur_ms} ms<br>Priority: ${s.prio}<br>End reason: ${s.reason}`;
    tip.style.display='block';
    tip.style.left=(e.clientX+14)+'px';
    tip.style.top=(e.clientY-8)+'px';
    return;
  }

  // Check inversion
  const inv = D.inversions.find(i => tx >= i.start && tx <= i.end);
  if(inv){
    const dur_ms = (inv.dur / D.tps * 1000).toFixed(1);
    tip.innerHTML =
      `<b>Priority Inversion</b><br>` +
      `Mutex: ${inv.mutex}<br>` +
      `High task: Task${inv.high} | Low task: Task${inv.low}<br>` +
      `Duration: ${dur_ms} ms<br>` +
      `Resolved by inheritance: ${inv.inherit}`;
    tip.style.display='block';
    tip.style.left=(e.clientX+14)+'px';
    tip.style.top=(e.clientY-8)+'px';
    return;
  }
  tip.style.display='none';
});
canvas.addEventListener('mouseleave', () => { tip.style.display='none'; });

document.getElementById('btn-zoom-in').onclick  = () => { zoom = Math.min(50, zoom*1.5); draw(); };
document.getElementById('btn-zoom-out').onclick = () => { zoom = Math.max(0.05, zoom/1.5); draw(); };
document.getElementById('btn-reset').onclick    = () => { zoom=1; panX=0; draw(); };

// ── statistics table ──────────────────────────────────────────────
const tbl = document.getElementById('stat-table');
tbl.innerHTML = '<tr><th>Task</th><th>Priority</th><th>CPU%</th>' +
                '<th>Context Switches</th><th>Status</th></tr>';
D.tasks.forEach(t => {
  const ok = D.miss_count === 0 && D.inv_count === 0;
  tbl.innerHTML +=
    `<tr><td>${t.name}</td><td>${t.prio}</td><td>${t.util}%</td>` +
    `<td>${t.switches}</td>` +
    `<td><span class="badge ok">OK</span></td></tr>`;
});

const al = document.getElementById('anomaly-list');
if(D.inv_count > 0)
  al.innerHTML += `<span class="badge inv" style="margin-right:8px">` +
    `⚠ ${D.inv_count} Priority Inversion${D.inv_count>1?'s':''}</span>`;
if(D.miss_count > 0)
  al.innerHTML += `<span class="badge miss">` +
    `✗ ${D.miss_count} Deadline Miss${D.miss_count>1?'es':''}</span>`;
if(D.inv_count===0 && D.miss_count===0)
  al.innerHTML = '<span class="badge ok">✓ No anomalies detected</span>';

const ib = document.getElementById('info-badge');
ib.textContent = `Duration: ${(totalTicks/D.tps*1000).toFixed(0)} ms  |  ` +
  `${D.slices.length} slices  |  ${D.tasks.length} tasks`;

draw();
</script>
</body>
</html>"""
