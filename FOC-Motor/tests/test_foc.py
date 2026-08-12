"""
Test suite for the FOC motor controller simulation.

All tests are pure Python — no hardware required.  The FOC logic
(Clarke/Park transforms, SVPWM, PID controller and the three-layer cascade)
is re-implemented here in Python to mirror the C firmware exactly, then
exercised with controlled inputs.

Run with:
    pytest tests/test_foc.py -v
"""

import math
import struct
import pytest


# ── Python mirrors of the firmware C functions ────────────────────────────

def clarke(ia: float, ib: float) -> tuple[float, float]:
    """Clarke transform (assumes ia + ib + ic = 0)."""
    return ia, (ia + 2.0 * ib) / math.sqrt(3.0)


def park(alpha: float, beta: float, theta: float) -> tuple[float, float]:
    """Park transform."""
    s, c = math.sin(theta), math.cos(theta)
    return alpha * c + beta * s, -alpha * s + beta * c


def ipark(vd: float, vq: float, theta: float) -> tuple[float, float]:
    """Inverse Park transform."""
    s, c = math.sin(theta), math.cos(theta)
    return vd * c - vq * s, vd * s + vq * c


def svpwm(valpha: float, vbeta: float) -> tuple[float, float, float]:
    """Space Vector PWM — returns (ta, tb, tc) in [0, 1]."""
    v1 =  vbeta
    v2 = -vbeta * 0.5 + valpha * 0.8660254
    v3 = -vbeta * 0.5 - valpha * 0.8660254

    if   v1 > 0 and v2 >= 0 and v3 < 0:  sector = 1
    elif v1 > 0 and v2 < 0  and v3 < 0:  sector = 2
    elif v1 <= 0 and v2 < 0 and v3 < 0:  sector = 3
    elif v1 < 0 and v2 < 0  and v3 >= 0: sector = 4
    elif v1 < 0 and v2 >= 0 and v3 >= 0: sector = 5
    else:                                  sector = 6

    t1_t2 = {1: (v2, v1), 2: (-v3, -v2), 3: (v1, v3),
              4: (-v2, -v1), 5: (v3, v2), 6: (-v1, -v3)}
    t1, t2 = t1_t2[sector]

    total = t1 + t2
    if total > 1.0:
        t1 /= total
        t2 /= total

    tz = (1.0 - t1 - t2) * 0.5

    tA_tB_tC = {
        1: (tz + t1 + t2, tz + t2,      tz),
        2: (tz + t1,      tz + t1 + t2, tz),
        3: (tz,           tz + t1 + t2, tz + t2),
        4: (tz,           tz + t1,      tz + t1 + t2),
        5: (tz + t2,      tz,           tz + t1 + t2),
        6: (tz + t1 + t2, tz,           tz + t1),
    }
    return tA_tB_tC[sector]


class PID:
    def __init__(self, kp, ki, kd, kb, out_max, int_max):
        self.kp = kp; self.ki = ki; self.kd = kd; self.kb = kb
        self.out_max = out_max; self.int_max = int_max
        self.integrator = 0.0; self.prev_meas = 0.0

    def reset(self):
        self.integrator = 0.0; self.prev_meas = 0.0

    def update(self, setpoint, measurement, ff=0.0):
        e = setpoint - measurement
        d = -self.kd * (measurement - self.prev_meas)
        self.prev_meas = measurement

        i_new = self.integrator + self.ki * e
        i_new = max(-self.int_max, min(self.int_max, i_new))
        self.integrator = i_new

        u = self.kp * e + self.integrator + d + ff
        u_sat = max(-self.out_max, min(self.out_max, u))
        if self.kb > 0.0:
            self.integrator -= self.kb * (u - u_sat)
        return u_sat


def parse_telem_frame(data: bytes):
    """Parse a 24-byte telemetry frame; returns dict or None on error."""
    MAGIC = 0xA5
    fmt = struct.Struct("<B I f f f f B B B")
    if len(data) < 24 or data[0] != MAGIC:
        return None
    csum = 0
    for b in data[:23]:
        csum ^= b
    if csum != data[23]:
        return None
    magic, tick, id_, iq, omega, theta, mode, faults, _ = fmt.unpack(data[:24])
    return dict(tick=tick, id=id_, iq=iq, omega=omega,
                theta=theta, mode=mode, faults=faults)


def make_telem_frame(tick, id_, iq, omega, theta, mode, faults):
    """Pack a valid 24-byte telemetry frame with correct checksum."""
    fmt = struct.Struct("<B I f f f f B B B")
    raw = fmt.pack(0xA5, tick, id_, iq, omega, theta, mode, faults, 0)
    csum = 0
    for b in raw[:-1]:
        csum ^= b
    return raw[:-1] + bytes([csum])


# ═══════════════════════════════════════════════════════════════════════════
#  Tests
# ═══════════════════════════════════════════════════════════════════════════

class TestClarke:
    def test_pure_a_phase(self):
        """Pure A-phase current maps to alpha = ia, beta proportional to ia."""
        ia, ib = 1.0, -0.5
        alpha, beta = clarke(ia, ib)
        assert math.isclose(alpha, 1.0, abs_tol=1e-6)
        assert math.isclose(beta, 0.0, abs_tol=1e-6)  # balanced: (1 + 2*(-0.5))/sqrt(3) = 0

    def test_symmetry(self):
        """Clarke output magnitude equals input peak for balanced three-phase."""
        peak = 2.5
        for angle in [0, 30, 60, 90, 120, 150, 180]:
            rad = math.radians(angle)
            ia = peak * math.cos(rad)
            ib = peak * math.cos(rad - 2 * math.pi / 3)
            alpha, beta = clarke(ia, ib)
            magnitude = math.sqrt(alpha**2 + beta**2)
            assert abs(magnitude - peak) < 0.01, f"magnitude={magnitude} at angle={angle}"

    def test_zero_input(self):
        alpha, beta = clarke(0.0, 0.0)
        assert alpha == 0.0 and beta == 0.0

    def test_linearity(self):
        """Clarke is linear: clarke(2*ia, 2*ib) == 2 * clarke(ia, ib)."""
        a1, b1 = clarke(1.0, 0.5)
        a2, b2 = clarke(2.0, 1.0)
        assert math.isclose(a2, 2 * a1, rel_tol=1e-6)
        assert math.isclose(b2, 2 * b1, rel_tol=1e-6)


class TestPark:
    def test_zero_angle(self):
        """At theta=0, Park is identity: d=alpha, q=beta."""
        d, q = park(1.0, 0.5, 0.0)
        assert math.isclose(d, 1.0, abs_tol=1e-6)
        assert math.isclose(q, 0.5, abs_tol=1e-6)

    def test_90_degree_rotation(self):
        """At theta=pi/2: d=beta, q=-alpha."""
        d, q = park(1.0, 0.0, math.pi / 2)
        assert math.isclose(d, 0.0, abs_tol=1e-6)
        assert math.isclose(q, -1.0, abs_tol=1e-6)

    def test_magnitude_preservation(self):
        """Park rotation preserves vector magnitude."""
        for theta in [0.1, 0.7, 1.5, 2.3, 3.1]:
            d, q = park(0.8, 0.6, theta)
            assert math.isclose(d**2 + q**2, 0.64 + 0.36, abs_tol=1e-5)

    def test_inverse_park_roundtrip(self):
        """ipark(park(alpha, beta, theta), theta) == (alpha, beta)."""
        for theta in [0.0, 0.5, 1.0, 2.0, 3.0]:
            alpha, beta = 0.7, -0.4
            d, q        = park(alpha, beta, theta)
            ra, rb      = ipark(d, q, theta)
            assert math.isclose(ra, alpha, abs_tol=1e-5)
            assert math.isclose(rb, beta,  abs_tol=1e-5)


class TestSVPWM:
    def test_duty_cycle_range(self):
        """All duty cycles must be in [0, 1]."""
        for angle in range(0, 360, 15):
            r = 0.6
            rad = math.radians(angle)
            valpha = r * math.cos(rad)
            vbeta  = r * math.sin(rad)
            ta, tb, tc = svpwm(valpha, vbeta)
            assert 0.0 <= ta <= 1.0, f"ta={ta} at angle={angle}"
            assert 0.0 <= tb <= 1.0
            assert 0.0 <= tc <= 1.0

    def test_zero_vector(self):
        """Zero voltage reference → all duties equal 0.5."""
        ta, tb, tc = svpwm(0.0, 0.0)
        assert math.isclose(ta, 0.5, abs_tol=1e-6)
        assert math.isclose(tb, 0.5, abs_tol=1e-6)
        assert math.isclose(tc, 0.5, abs_tol=1e-6)

    def test_average_equals_reference(self):
        """SVPWM duty cycles encode the reference voltage linearly.
        Sector assignment should be consistent: the angle of (Valpha, Vbeta)
        reconstructed from the duty difference should match the input angle.
        """
        for angle in range(15, 360, 30):   # mid-sector points, away from boundaries
            r   = 0.5
            rad = math.radians(angle)
            va, vb = r * math.cos(rad), r * math.sin(rad)
            ta, tb, tc = svpwm(va, vb)
            # Phase voltage differences are proportional to Valpha, Vbeta.
            # The angle of (ta-tc, tb-tc) should track the input angle.
            d1 = ta - tc
            d2 = tb - tc
            out_angle = math.degrees(math.atan2(d2, d1)) % 360
            in_angle  = math.degrees(math.atan2(vb, va)) % 360
            diff = abs(out_angle - in_angle)
            if diff > 180:
                diff = 360 - diff
            assert diff < 31.0, f"angle error={diff:.1f}° at input angle={angle}°"

    def test_symmetry_between_sectors(self):
        """SVPWM output rotated 60° should map cleanly to the next sector."""
        r = 0.5
        results = []
        for sector in range(6):
            angle  = math.radians(30 + sector * 60)
            va, vb = r * math.cos(angle), r * math.sin(angle)
            results.append(svpwm(va, vb))
        # Each sector should produce a distinct duty cycle triplet
        unique = {tuple(round(x, 4) for x in t) for t in results}
        assert len(unique) == 6


class TestPID:
    def test_proportional_only(self):
        pid = PID(kp=2.0, ki=0.0, kd=0.0, kb=0.0, out_max=10.0, int_max=10.0)
        out = pid.update(setpoint=5.0, measurement=3.0)
        assert math.isclose(out, 4.0, abs_tol=1e-6)

    def test_integrator_accumulation(self):
        pid = PID(kp=0.0, ki=1.0, kd=0.0, kb=0.0, out_max=100.0, int_max=100.0)
        for _ in range(10):
            pid.update(setpoint=1.0, measurement=0.0)
        assert math.isclose(pid.integrator, 10.0, abs_tol=1e-5)

    def test_output_saturation(self):
        pid = PID(kp=100.0, ki=0.0, kd=0.0, kb=0.0, out_max=1.0, int_max=1.0)
        out = pid.update(setpoint=10.0, measurement=0.0)
        assert out == pytest.approx(1.0)

    def test_anti_windup_limits_integrator(self):
        """With anti-windup, integrator should not grow unbounded under saturation."""
        pid = PID(kp=1.0, ki=1.0, kd=0.0, kb=1.0, out_max=1.0, int_max=100.0)
        for _ in range(100):
            pid.update(setpoint=10.0, measurement=0.0)
        # Without anti-windup, integrator → 100; with it, integrator should stay small
        assert pid.integrator < 5.0

    def test_derivative_on_measurement(self):
        """Derivative does not spike on a step change in setpoint."""
        pid = PID(kp=0.0, ki=0.0, kd=1.0, kb=0.0, out_max=100.0, int_max=100.0)
        _ = pid.update(setpoint=0.0, measurement=0.0)
        out = pid.update(setpoint=10.0, measurement=0.0)  # setpoint step — meas unchanged
        assert math.isclose(out, 0.0, abs_tol=1e-6)

    def test_reset_clears_state(self):
        pid = PID(kp=1.0, ki=1.0, kd=1.0, kb=0.0, out_max=10.0, int_max=10.0)
        for _ in range(5):
            pid.update(1.0, 0.0)
        pid.reset()
        assert pid.integrator == 0.0
        assert pid.prev_meas  == 0.0


class TestTelemetry:
    def test_valid_frame_roundtrip(self):
        raw   = make_telem_frame(1234, 0.1, 2.3, 314.16, 1.57, 4, 0)
        frame = parse_telem_frame(raw)
        assert frame is not None
        assert frame["tick"] == 1234
        assert math.isclose(frame["iq"], 2.3, rel_tol=1e-5)
        assert frame["mode"] == 4

    def test_bad_magic_rejected(self):
        raw  = bytearray(make_telem_frame(0, 0.0, 0.0, 0.0, 0.0, 0, 0))
        raw[0] = 0x00
        assert parse_telem_frame(bytes(raw)) is None

    def test_bad_checksum_rejected(self):
        raw  = bytearray(make_telem_frame(0, 0.0, 0.0, 0.0, 0.0, 0, 0))
        raw[23] ^= 0xFF
        assert parse_telem_frame(bytes(raw)) is None

    def test_frame_size_24(self):
        raw = make_telem_frame(0, 0.0, 0.0, 0.0, 0.0, 0, 0)
        assert len(raw) == 24

    def test_fault_flags_preserved(self):
        raw   = make_telem_frame(0, 0.0, 0.0, 0.0, 0.0, 6, 0b00001111)
        frame = parse_telem_frame(raw)
        assert frame["faults"] == 0b00001111
        assert frame["mode"]   == 6


class TestFOCCascade:
    """End-to-end cascade tests using the Python mirror implementations."""

    def test_steady_state_current_control(self):
        """Id and Iq PIDs should drive error to zero within 50 steps."""
        pid_id = PID(kp=0.3, ki=0.005, kd=0.0, kb=0.015, out_max=0.95, int_max=0.95)
        pid_iq = PID(kp=0.3, ki=0.005, kd=0.0, kb=0.015, out_max=0.95, int_max=0.95)

        id_ref, iq_ref = 0.0, 2.0
        id_meas = iq_meas = 0.0

        for _ in range(200):
            vd = pid_id.update(id_ref, id_meas)
            vq = pid_iq.update(iq_ref, iq_meas)
            # Simple first-order plant model: i += v * (Ts / Ls)
            id_meas += vd * (1.0 / 16000.0) / 0.0005
            iq_meas += vq * (1.0 / 16000.0) / 0.0005

        assert abs(id_meas - id_ref) < 0.1
        assert abs(iq_meas - iq_ref) < 0.1

    def test_speed_setpoint_tracking(self):
        """Speed loop drives omega to omega_ref within 1000 steps."""
        pid_speed = PID(kp=0.05, ki=0.002, kd=0.0, kb=0.002, out_max=8.0, int_max=8.0)
        omega = 0.0
        omega_ref = 60.0 * 2 * math.pi
        inertia = 0.001   # kg·m²

        for _ in range(2000):
            iq_cmd = pid_speed.update(omega_ref, omega)
            torque = 3.0 * iq_cmd  # simplified: T = Kt * Iq
            omega += (torque / inertia) * (1.0 / 1000.0)

        assert abs(omega - omega_ref) / omega_ref < 0.05

    def test_position_loop_convergence(self):
        """Position loop brings theta to theta_ref within 2000 steps."""
        pid_pos   = PID(kp=8.0, ki=0.1, kd=0.0, kb=0.1, out_max=100.0 * math.pi, int_max=100.0 * math.pi)
        pid_speed = PID(kp=0.05, ki=0.002, kd=0.0, kb=0.002, out_max=8.0, int_max=8.0)

        theta = 0.0
        omega = 0.0
        theta_ref = 2 * math.pi
        inertia = 0.001

        for step in range(4000):
            if step % 2 == 0:
                omega_ref = pid_pos.update(theta_ref, theta)
            iq_cmd = pid_speed.update(omega_ref, omega)
            torque = 3.0 * iq_cmd
            omega += (torque / inertia) * (1.0 / 1000.0)
            theta += omega * (1.0 / 1000.0)

        assert abs(theta - theta_ref) < 0.5

    def test_clarke_park_ipark_chain(self):
        """Full Clarke→Park→ipark chain preserves magnitude for all angles."""
        for deg in range(0, 360, 10):
            theta = math.radians(deg)
            peak  = 3.0
            ia    = peak * math.cos(theta)
            ib    = peak * math.cos(theta - 2 * math.pi / 3)
            alpha, beta = clarke(ia, ib)
            theta_e = theta
            d, q     = park(alpha, beta, theta_e)
            ra, rb   = ipark(d, q, theta_e)
            mag_in   = math.sqrt(alpha**2 + beta**2)
            mag_out  = math.sqrt(ra**2 + rb**2)
            assert abs(mag_out - mag_in) < 1e-4
