"""Shared fixtures for the RTOS Scheduler Visualizer test suite."""

from __future__ import annotations

import pytest

from scheduler_viz.core.decoder import Decoder
from scheduler_viz.core.generator import generate
from scheduler_viz.core.model import build_model, SchedulingModel


@pytest.fixture(scope="session")
def raw_trace() -> bytes:
    """Full synthetic trace with inversion and deadline miss."""
    return generate(duration_ticks=2000, seed=42, inversion=True, deadline_miss=True)


@pytest.fixture(scope="session")
def clean_trace() -> bytes:
    """Synthetic trace with no anomalies."""
    return generate(duration_ticks=2000, seed=99, inversion=False, deadline_miss=False)


@pytest.fixture(scope="session")
def events(raw_trace):
    dec = Decoder()
    return dec.feed(raw_trace)


@pytest.fixture(scope="session")
def model(events) -> SchedulingModel:
    return build_model(events)


@pytest.fixture(scope="session")
def clean_events(clean_trace):
    dec = Decoder()
    return dec.feed(clean_trace)


@pytest.fixture(scope="session")
def clean_model(clean_events) -> SchedulingModel:
    return build_model(clean_events)
