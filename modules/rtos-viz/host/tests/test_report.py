"""Tests for report rendering and end-to-end pipeline."""

from __future__ import annotations

import json

import pytest

from scheduler_viz.core.decoder import Decoder
from scheduler_viz.core.generator import generate
from scheduler_viz.core.model import build_model
from scheduler_viz.report.gantt import render_html
from scheduler_viz.report.terminal import render


class TestGanttRenderer:
    def test_returns_string(self, model):
        html = render_html(model)
        assert isinstance(html, str)
        assert len(html) > 1000

    def test_valid_html_structure(self, model):
        html = render_html(model)
        assert "<!DOCTYPE html>" in html
        assert "</html>" in html

    def test_contains_embedded_json(self, model):
        html = render_html(model)
        # The JSON data block is injected via __DATA__
        start = html.find("const D =")
        assert start != -1
        json_start = html.find("{", start)
        json_end = html.find(";\n", json_start)
        doc = json.loads(html[json_start:json_end])
        assert "tasks" in doc
        assert "slices" in doc
        assert "inversions" in doc
        assert "misses" in doc

    def test_all_tasks_in_json(self, model):
        html = render_html(model)
        start = html.find("const D =")
        json_start = html.find("{", start)
        json_end = html.find(";\n", json_start)
        doc = json.loads(html[json_start:json_end])
        assert len(doc["tasks"]) == len(model.tasks)

    def test_custom_title(self, model):
        html = render_html(model, title="MyTrace")
        assert "MyTrace" in html

    def test_ticks_per_sec_embedded(self, model):
        html = render_html(model, ticks_per_sec=500)
        assert '"tps":500' in html

    def test_inversion_data_present(self, model):
        html = render_html(model)
        start = html.find("const D =")
        json_start = html.find("{", start)
        json_end = html.find(";\n", json_start)
        doc = json.loads(html[json_start:json_end])
        assert doc["inv_count"] == len(model.inversions)

    def test_miss_data_present(self, model):
        html = render_html(model)
        start = html.find("const D =")
        json_start = html.find("{", start)
        json_end = html.find(";\n", json_start)
        doc = json.loads(html[json_start:json_end])
        assert doc["miss_count"] == len(model.misses)

    def test_clean_trace_no_anomalies(self, clean_model):
        html = render_html(clean_model)
        start = html.find("const D =")
        json_start = html.find("{", start)
        json_end = html.find(";\n", json_start)
        doc = json.loads(html[json_start:json_end])
        assert doc["inv_count"] == 0
        assert doc["miss_count"] == 0


class TestTerminalRenderer:
    def test_runs_without_exception(self, model):
        from rich.console import Console
        console = Console(no_color=True, width=120)
        render(model, console)  # should not raise

    def test_clean_trace_runs(self, clean_model):
        from rich.console import Console
        console = Console(no_color=True, width=120)
        render(clean_model, console)


class TestEndToEnd:
    def test_full_pipeline_with_anomalies(self):
        raw = generate(duration_ticks=1000, seed=7, inversion=True, deadline_miss=True)
        dec = Decoder()
        events = dec.feed(raw)
        model = build_model(events)
        html = render_html(model)

        assert len(model.tasks) == 3
        assert len(model.slices) > 0
        assert len(model.misses) >= 1
        assert len(model.inversions) >= 1
        assert "<!DOCTYPE html>" in html

    def test_full_pipeline_clean(self):
        raw = generate(duration_ticks=1000, seed=8, inversion=False, deadline_miss=False)
        dec = Decoder()
        events = dec.feed(raw)
        model = build_model(events)

        assert len(model.misses) == 0
        assert len(model.inversions) == 0

    def test_varying_seeds(self):
        for seed in range(5):
            raw = generate(duration_ticks=500, seed=seed)
            events = Decoder().feed(raw)
            model = build_model(events)
            assert len(model.tasks) == 3
            assert len(model.slices) > 0

    def test_save_and_reload_html(self, model, tmp_path):
        html = render_html(model)
        p = tmp_path / "gantt.html"
        p.write_text(html, encoding="utf-8")
        assert p.exists()
        content = p.read_text(encoding="utf-8")
        assert "const D =" in content
