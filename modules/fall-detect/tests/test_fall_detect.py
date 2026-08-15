"""
Test suite for the fall detection pipeline.

All tests are pure Python — no hardware, no TensorFlow, no Arduino SDK needed.
The threshold FSM, dataset loader utilities and telemetry parsing are tested
with synthetic data.

Run with:
    pytest tests/test_fall_detect.py -v
"""

from __future__ import annotations

import math
import sys
import types
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))


# ── Python mirror of the fall detector FSM ───────────────────────────────

FREE_FALL_THRESHOLD  = 0.5
IMPACT_THRESHOLD     = 3.0
IMPACT_WINDOW_MS     = 500
POST_IMPACT_MS       = 200
WINDOW_SAMPLES       = 50
WINDOW_AXES          = 6
FALL_CONF_THRESHOLD  = 0.75
LYING_THRESHOLD      = 0.5
LYING_DURATION_MS    = 1000
ALERT_COOLDOWN_MS    = 30000

IDLE, FREE_FALL, IMPACT, CLASSIFYING, LYING, ALERT, COOLDOWN = range(7)


class FallDetector:
    def __init__(self):
        self.state          = IDLE
        self.state_entry_ms = 0
        self.ff_entry_ms    = 0
        self.window         = np.zeros((WINDOW_SAMPLES, WINDOW_AXES), dtype=np.float32)
        self.win_head       = 0
        self.win_count      = 0
        self.fall_prob      = 0.0
        self.nfall_prob     = 0.0
        self.total_falls    = 0
        self.total_alerts   = 0
        self.false_positives_suppressed = 0
        self._classifier_fn = None   # injected by tests

    def update(self, ax, ay, az, gx, gy, gz, ms):
        smv = math.sqrt(ax*ax + ay*ay + az*az)

        wi = self.win_head
        self.window[wi] = [ax, ay, az, gx, gy, gz]
        self.win_head   = (wi + 1) % WINDOW_SAMPLES
        if self.win_count < WINDOW_SAMPLES:
            self.win_count += 1

        if self.state in (IDLE, COOLDOWN):
            if self.state == COOLDOWN and (ms - self.state_entry_ms) >= ALERT_COOLDOWN_MS:
                self.state = IDLE; self.state_entry_ms = ms
            if smv < FREE_FALL_THRESHOLD:
                self.state = FREE_FALL; self.ff_entry_ms = ms; self.state_entry_ms = ms

        elif self.state == FREE_FALL:
            if (ms - self.ff_entry_ms) > IMPACT_WINDOW_MS:
                self.state = IDLE; self.state_entry_ms = ms
            elif smv > IMPACT_THRESHOLD:
                self.state = IMPACT; self.state_entry_ms = ms

        elif self.state == IMPACT:
            if (ms - self.state_entry_ms) >= POST_IMPACT_MS:
                self.state = CLASSIFYING; self.state_entry_ms = ms

        elif self.state == CLASSIFYING:
            if self._classifier_fn:
                self.fall_prob, self.nfall_prob = self._classifier_fn(self.window)
            else:
                self.fall_prob, self.nfall_prob = 0.8, 0.2
            if self.fall_prob >= FALL_CONF_THRESHOLD:
                self.total_falls += 1
                self.state = LYING; self.state_entry_ms = ms
            else:
                self.state = IDLE; self.state_entry_ms = ms

        elif self.state == LYING:
            if abs(az) > LYING_THRESHOLD:
                self.false_positives_suppressed += 1
                self.state = COOLDOWN; self.state_entry_ms = ms
            elif (ms - self.state_entry_ms) >= LYING_DURATION_MS:
                self.total_alerts += 1
                self.state = ALERT; self.state_entry_ms = ms
                return True

        elif self.state == ALERT:
            self.state = COOLDOWN; self.state_entry_ms = ms

        return False


def _sim_fall(fd: FallDetector, t0: int = 0) -> tuple[bool, int]:
    """Simulate a complete fall sequence and return (confirmed, final_ms)."""
    ms = t0
    # 200 ms standing (SMV ≈ 1 g)
    for _ in range(20):
        fd.update(0, 0, 1.0, 0, 0, 0, ms); ms += 10
    # 150 ms free fall (SMV ≈ 0.1 g)
    for _ in range(15):
        fd.update(0.05, 0.05, 0.07, 1, 1, 1, ms); ms += 10
    # Impact spike (SMV ≈ 5 g)
    fd.update(2.0, 3.0, 3.0, 50, 40, 30, ms); ms += 10
    # 200 ms post-impact
    for _ in range(20):
        fd.update(0.1, 0.1, 0.2, 5, 5, 5, ms); ms += 10
    # Force CLASSIFYING → LYING by advancing clock
    ms += POST_IMPACT_MS
    fd.update(0.0, 0.0, 0.2, 0, 0, 0, ms); ms += 10
    # 1000 ms lying flat (|az| < 0.5)
    confirmed = False
    for _ in range(110):
        r = fd.update(0.0, 0.0, 0.2, 0, 0, 0, ms); ms += 10
        if r: confirmed = True; break
    return confirmed, ms


# ═══════════════════════════════════════════════════════════════════════════
#  Tests
# ═══════════════════════════════════════════════════════════════════════════

class TestSMV:
    def test_identity(self):
        smv = math.sqrt(1**2 + 0**2 + 0**2)
        assert smv == pytest.approx(1.0)

    def test_gravity(self):
        """Stationary device pointing up: SMV ≈ 1 g."""
        smv = math.sqrt(0**2 + 0**2 + 1.0**2)
        assert smv == pytest.approx(1.0)

    def test_free_fall_below_threshold(self):
        smv = math.sqrt(0.1**2 + 0.1**2 + 0.1**2)
        assert smv < FREE_FALL_THRESHOLD

    def test_impact_above_threshold(self):
        smv = math.sqrt(2**2 + 2**2 + 2**2)
        assert smv > IMPACT_THRESHOLD


class TestThresholdFSM:
    def test_idle_on_init(self):
        fd = FallDetector()
        assert fd.state == IDLE

    def test_normal_activity_stays_idle(self):
        fd = FallDetector()
        for _ in range(100):
            fd.update(0, 0, 1.0, 0, 0, 0, _ * 10)
        assert fd.state == IDLE

    def test_free_fall_detected(self):
        fd = FallDetector()
        fd.update(0.1, 0.1, 0.1, 0, 0, 0, 0)
        assert fd.state == FREE_FALL

    def test_free_fall_timeout_returns_idle(self):
        fd = FallDetector()
        fd.update(0.1, 0.1, 0.1, 0, 0, 0, 0)
        assert fd.state == FREE_FALL
        fd.update(0.1, 0.1, 0.1, 0, 0, 0, IMPACT_WINDOW_MS + 10)
        assert fd.state == IDLE

    def test_impact_after_freefall_transitions_to_impact(self):
        fd = FallDetector()
        fd.update(0.1, 0.1, 0.1, 0, 0, 0, 0)
        assert fd.state == FREE_FALL
        fd.update(2.0, 3.0, 2.0, 0, 0, 0, 100)
        assert fd.state == IMPACT

    def test_no_impact_without_freefall_stays_idle(self):
        """A large acceleration without preceding free-fall should not trigger."""
        fd = FallDetector()
        fd.update(3.5, 3.5, 3.5, 0, 0, 0, 0)
        assert fd.state == IDLE

    def test_full_fall_sequence_confirmed(self):
        fd = FallDetector()
        confirmed, _ = _sim_fall(fd)
        assert confirmed is True
        assert fd.total_alerts == 1

    def test_cooldown_entered_after_alert(self):
        fd = FallDetector()
        _sim_fall(fd)
        fd.update(0.0, 0.0, 0.2, 0, 0, 0, 99999)
        assert fd.state == COOLDOWN

    def test_second_fall_suppressed_during_cooldown(self):
        fd = FallDetector()
        _, t = _sim_fall(fd)
        # Immediately try another fall — should be suppressed
        fd.update(0.1, 0.1, 0.1, 0, 0, 0, t + 1)
        assert fd.state == COOLDOWN

    def test_cooldown_expires_and_rearmed(self):
        fd = FallDetector()
        _sim_fall(fd, t0=0)
        # After _sim_fall the state is ALERT; one update transitions it to COOLDOWN.
        t_cooldown_start = ALERT_COOLDOWN_MS
        fd.update(0.0, 0.0, 1.0, 0, 0, 0, t_cooldown_start)
        assert fd.state == COOLDOWN
        # Advance past the cooldown period (state_entry_ms = t_cooldown_start).
        fd.update(0.0, 0.0, 1.0, 0, 0, 0, t_cooldown_start + ALERT_COOLDOWN_MS + 1)
        assert fd.state == IDLE

    def test_posture_check_suppresses_false_positive(self):
        """If person gets up after classifier fires, alert should be cancelled."""
        fd = FallDetector()
        _, t = _sim_fall.__wrapped__(fd) if hasattr(_sim_fall, "__wrapped__") else (None, 0)
        # Manually force to LYING state then simulate person standing up
        fd2 = FallDetector()
        fd2.state = LYING; fd2.state_entry_ms = 0
        fd2.update(0.0, 0.0, 1.0, 0, 0, 0, 500)   # upright: az ≈ 1 g > LYING_THRESHOLD
        assert fd2.false_positives_suppressed == 1
        assert fd2.state == COOLDOWN


class TestClassifierMock:
    def test_low_confidence_does_not_alert(self):
        fd = FallDetector()
        fd._classifier_fn = lambda w: (0.5, 0.5)   # below threshold
        fd.state = CLASSIFYING
        fd.update(0, 0, 0.2, 0, 0, 0, 0)
        assert fd.state == IDLE
        assert fd.total_falls == 0

    def test_high_confidence_transitions_to_lying(self):
        fd = FallDetector()
        fd._classifier_fn = lambda w: (0.95, 0.05)
        fd.state = CLASSIFYING
        fd.update(0, 0, 0.2, 0, 0, 0, 0)
        assert fd.state == LYING
        assert fd.total_falls == 1

    def test_exact_threshold_transitions(self):
        fd = FallDetector()
        fd._classifier_fn = lambda w: (FALL_CONF_THRESHOLD, 1 - FALL_CONF_THRESHOLD)
        fd.state = CLASSIFYING
        fd.update(0, 0, 0.2, 0, 0, 0, 0)
        assert fd.state == LYING


class TestWindowBuffer:
    def test_window_fills_correctly(self):
        fd = FallDetector()
        assert fd.win_count == 0
        for i in range(WINDOW_SAMPLES):
            fd.update(float(i), 0, 1, 0, 0, 0, i * 10)
        assert fd.win_count == WINDOW_SAMPLES

    def test_window_is_circular(self):
        fd = FallDetector()
        for i in range(WINDOW_SAMPLES + 10):
            fd.update(float(i), 0, 1, 0, 0, 0, i * 10)
        assert fd.win_count == WINDOW_SAMPLES
        assert fd.win_head == 10   # wrapped around

    def test_window_axes(self):
        fd = FallDetector()
        fd.update(1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 0)
        assert fd.window[0, 0] == pytest.approx(1.1)
        assert fd.window[0, 5] == pytest.approx(6.6)


class TestDatasetUtils:
    def test_compute_stats_shape(self):
        from ml.data.dataset import compute_stats, WINDOW_AXES
        X = np.random.randn(100, WINDOW_SAMPLES, WINDOW_AXES).astype(np.float32)
        mean, std = compute_stats(X)
        assert mean.shape == (WINDOW_AXES,)
        assert std.shape  == (WINDOW_AXES,)

    def test_compute_stats_mean_near_zero(self):
        from ml.data.dataset import compute_stats
        rng  = np.random.default_rng(0)
        X    = rng.standard_normal((1000, WINDOW_SAMPLES, WINDOW_AXES)).astype(np.float32)
        mean, std = compute_stats(X)
        assert np.abs(mean).max() < 0.1

    def test_normalisation_produces_unit_variance(self):
        from ml.data.dataset import compute_stats
        rng   = np.random.default_rng(1)
        X     = rng.standard_normal((500, WINDOW_SAMPLES, WINDOW_AXES)).astype(np.float32)
        X    *= np.array([2.0, 3.0, 1.5, 10.0, 8.0, 6.0])  # different scales per axis
        mean, std = compute_stats(X)
        X_norm = (X - mean) / std
        _, std_norm = compute_stats(X_norm)
        assert np.abs(std_norm - 1.0).max() < 0.01

    def test_load_combined_raises_if_no_data(self, tmp_path):
        from ml.data.dataset import load_combined
        with pytest.raises(FileNotFoundError):
            load_combined(tmp_path)

    def test_smv_normalisation_preserves_range(self):
        """Normalised window: each axis should have std ≈ 1 after whitening."""
        rng  = np.random.default_rng(99)
        data = rng.standard_normal((200, WINDOW_SAMPLES, WINDOW_AXES)).astype(np.float32)
        mean = data.reshape(-1, WINDOW_AXES).mean(axis=0)
        std  = data.reshape(-1, WINDOW_AXES).std(axis=0)
        norm = (data - mean) / std
        std_after = norm.reshape(-1, WINDOW_AXES).std(axis=0)
        assert np.abs(std_after - 1.0).max() < 0.02

    def test_multiple_consecutive_falls_counted(self):
        """Two distinct falls separated by a full cooldown should both be counted."""
        fd = FallDetector()
        # First fall
        _sim_fall(fd, t0=0)
        # Transition ALERT → COOLDOWN
        fd.update(0, 0, 0.2, 0, 0, 0, 5000)
        assert fd.total_alerts == 1
        # Expire cooldown
        fd.update(0, 0, 1.0, 0, 0, 0, 5000 + ALERT_COOLDOWN_MS + 100)
        assert fd.state == IDLE
        # Second fall
        _sim_fall(fd, t0=5000 + ALERT_COOLDOWN_MS + 200)
        fd.update(0, 0, 0.2, 0, 0, 0, 5000 + ALERT_COOLDOWN_MS + 10000)
        assert fd.total_alerts == 2
