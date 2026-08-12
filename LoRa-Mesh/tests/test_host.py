"""Tests for the Python host protocol parser and node statistics logic."""

import sys
import os
import time
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "host"))

from protocol import parse_line, DataRecord, StatusRecord, NackRecord, BootRecord


# ── parse_line ─────────────────────────────────────────────────────────────

def test_parse_data_frame():
    line = ('{"type":"data","src":2,"rssi":-78,"snr":7,"hops":2,'
            '"temp_c":23.45,"hum_pct":61.20,"uptime_s":3600}')
    f = parse_line(line)
    assert isinstance(f, DataRecord)
    assert f.src     == 2
    assert f.rssi    == -78
    assert f.snr     == 7
    assert f.hops    == 2
    assert abs(f.temp_c   - 23.45) < 0.01
    assert abs(f.hum_pct  - 61.20) < 0.01
    assert f.uptime_s == 3600


def test_parse_status_frame():
    line = ('{"type":"status","addr":1,"routes":3,"tx":12,"rx":47,'
            '"relay":8,"drop_dup":2,"drop_ttl":0,"drop_no_route":0}')
    f = parse_line(line)
    assert isinstance(f, StatusRecord)
    assert f.addr     == 1
    assert f.routes   == 3
    assert f.tx       == 12
    assert f.rx       == 47
    assert f.relay    == 8
    assert f.drop_dup == 2


def test_parse_nack_frame():
    f = parse_line('{"type":"nack","dest":5,"seq":42}')
    assert isinstance(f, NackRecord)
    assert f.dest == 5
    assert f.seq  == 42


def test_parse_boot_frame():
    f = parse_line('{"type":"boot","addr":1}')
    assert isinstance(f, BootRecord)
    assert f.addr  == 1
    assert f.ready == False


def test_parse_ready_frame():
    f = parse_line('{"type":"ready"}')
    assert isinstance(f, BootRecord)
    assert f.ready == True


def test_parse_empty_string_returns_none():
    assert parse_line("") is None
    assert parse_line("   ") is None


def test_parse_invalid_json_returns_none():
    assert parse_line("{not json}") is None
    assert parse_line("hello world") is None


def test_parse_unknown_type_returns_none():
    assert parse_line('{"type":"unknown","x":1}') is None


def test_parse_whitespace_stripped():
    line = '  {"type":"nack","dest":3,"seq":1}  \r\n'
    f = parse_line(line)
    assert isinstance(f, NackRecord)


def test_rx_time_is_recent():
    f = parse_line('{"type":"nack","dest":1,"seq":0}')
    assert abs(f.rx_time - time.time()) < 2.0


# ── NodeStats accumulation ─────────────────────────────────────────────────

def test_node_stats_rssi_average():
    """NodeStats.rssi_avg should be the running mean."""
    from dashboard import NodeStats

    ns = NodeStats(src=2)
    ns.rssi_sum   = -78 + -82 + -70
    ns.packet_count = 3
    ns.rssi_min   = -82
    ns.rssi_max   = -70

    assert abs(ns.rssi_avg - (-76.67)) < 0.1
    assert ns.rssi_min == -82
    assert ns.rssi_max == -70


def test_node_stats_age():
    from dashboard import NodeStats
    ns = NodeStats(src=3)
    ns.last_seen = time.time() - 5.0
    assert 4.5 < ns.age_s < 6.0


def test_dashboard_accumulates_data():
    """Dashboard._handle updates NodeStats correctly."""
    from dashboard import Dashboard

    class FakeSrc:
        line_count = 0; error_count = 0
        def get(self, timeout=0): return None
        def start(self): pass
        def stop(self):  pass

    db = Dashboard(FakeSrc())
    f = DataRecord(src=5, rssi=-77, snr=8, hops=2,
                   temp_c=24.1, hum_pct=60.5, uptime_s=100)
    db._handle(f)

    assert 5 in db._nodes
    ns = db._nodes[5]
    assert ns.last_rssi  == -77
    assert ns.last_hops  == 2
    assert ns.packet_count == 1
    assert abs(ns.last_temp_c - 24.1) < 0.01


def test_dashboard_nack_increments_count():
    from dashboard import Dashboard
    from protocol import NackRecord

    class FakeSrc:
        line_count = 0; error_count = 0
        def get(self, timeout=0): return None
        def start(self): pass
        def stop(self):  pass

    db = Dashboard(FakeSrc())
    # Create a node first
    db._handle(DataRecord(src=7, rssi=-80, snr=6, hops=1,
                           temp_c=20.0, hum_pct=50.0, uptime_s=50))
    db._handle(NackRecord(dest=7, seq=1))
    assert db._nodes[7].nack_count == 1
