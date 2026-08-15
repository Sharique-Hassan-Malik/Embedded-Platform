#ifndef FOC_H
#define FOC_H

#include <stdint.h>
#include "foc_math.h"
#include "pid.h"

/*
 * Three-layer Field-Oriented Control cascade:
 *
 *   Position loop (outer)  → speed setpoint
 *   Speed loop   (middle)  → Iq setpoint
 *   Current loop (inner)   → Vd, Vq voltages  → SVPWM duty cycles
 *
 * The inner current loop runs at the PWM frequency (16 kHz).
 * The speed loop runs at 1 kHz.
 * The position loop runs at 500 Hz.
 *
 * Electrical angle is tracked from encoder counts or estimated by an
 * open-loop V/f ramp during startup.
 *
 * All physical values use SI units:
 *   Current   — amperes
 *   Speed     — rad/s (mechanical)
 *   Position  — radians (mechanical)
 *   Voltage   — normalised to Vbus/2 ∈ [-1, 1]
 */

/* Operating modes. */
typedef enum {
    FOC_MODE_IDLE     = 0,
    FOC_MODE_ALIGN    = 1,   /* rotor alignment pulse before open-loop ramp */
    FOC_MODE_OPENLOOP = 2,   /* V/f ramp — no encoder needed */
    FOC_MODE_CURRENT  = 3,   /* Id/Iq current control only */
    FOC_MODE_SPEED    = 4,   /* current + speed loop */
    FOC_MODE_POSITION = 5,   /* current + speed + position loop */
    FOC_MODE_FAULT    = 6,
} FocMode;

/* Fault flags (bitfield). */
#define FOC_FAULT_OVERCURRENT   (1u << 0)
#define FOC_FAULT_OVERVOLTAGE   (1u << 1)
#define FOC_FAULT_UNDERVOLTAGE  (1u << 2)
#define FOC_FAULT_OVERTEMP      (1u << 3)
#define FOC_FAULT_ENCODER       (1u << 4)

typedef struct {
    /* ── Measured ───────────────────────────────────────────────── */
    float ia, ib;             /* phase A and B currents, A */
    float vbus;               /* DC bus voltage, V */
    float theta_mech;         /* mechanical rotor angle, rad */
    float omega_mech;         /* mechanical speed, rad/s */
    int32_t encoder_counts;   /* raw encoder tick count */

    /* ── Transformed currents ───────────────────────────────────── */
    float alpha, beta;        /* Clarke stationary frame */
    float id, iq;             /* Park rotating frame */

    /* ── Setpoints ──────────────────────────────────────────────── */
    float id_ref;             /* d-axis current reference (0 for IPMSM) */
    float iq_ref;             /* q-axis current reference (torque) */
    float omega_ref;          /* speed reference, rad/s */
    float theta_ref;          /* position reference, rad */

    /* ── Controller outputs ─────────────────────────────────────── */
    float vd, vq;             /* voltage demand in rotating frame */
    float valpha, vbeta;      /* after inverse Park */
    float ta, tb, tc;         /* SVPWM duty cycles [0, 1] */

    /* ── Current loop ───────────────────────────────────────────── */
    PidController pid_id;
    PidController pid_iq;

    /* ── Speed loop ─────────────────────────────────────────────── */
    PidController pid_speed;

    /* ── Position loop ──────────────────────────────────────────── */
    PidController pid_pos;

    /* ── Motor parameters ───────────────────────────────────────── */
    uint8_t  pole_pairs;      /* number of electrical pole pairs */
    float    ls;              /* phase inductance, H */
    float    rs;              /* phase resistance, Ω */
    float    rated_current;   /* rated peak current, A */
    float    encoder_cpr;     /* encoder counts per mechanical revolution */

    /* ── Open-loop startup ──────────────────────────────────────── */
    float    ol_theta;        /* open-loop electrical angle accumulator */
    float    ol_omega;        /* open-loop angular velocity, rad/s */
    float    ol_vd;           /* open-loop Vd magnitude */
    float    ol_accel;        /* angular acceleration for ramp, rad/s² */

    /* ── Alignment ──────────────────────────────────────────────── */
    uint32_t align_ticks;     /* countdown timer for alignment pulse */
    float    align_id;        /* alignment Id magnitude */

    /* ── State ──────────────────────────────────────────────────── */
    FocMode  mode;
    uint32_t fault_flags;
    uint32_t tick;            /* ISR tick counter */
} FocController;

/* Initialise with motor parameters.  All PIDs start with zero gains —
 * call foc_set_current_gains(), foc_set_speed_gains(), foc_set_pos_gains()
 * before enabling the respective loops. */
void foc_init(FocController *foc,
              uint8_t pole_pairs,
              float ls, float rs,
              float rated_current,
              float encoder_cpr,
              float vbus);

/* Set PID gains for the three loops. */
void foc_set_current_gains(FocController *foc,
                           float kp, float ki, float kd, float kb);
void foc_set_speed_gains(FocController *foc,
                         float kp, float ki, float kd, float kb);
void foc_set_pos_gains(FocController *foc,
                       float kp, float ki, float kd, float kb);

/* Transition requests — may be rejected if preconditions are not met. */
int  foc_enable(FocController *foc);   /* IDLE → ALIGN → OPENLOOP → CURRENT */
void foc_disable(FocController *foc);  /* any → IDLE */

/* Set references.  Safe to call from the main loop context. */
void foc_set_speed(FocController *foc, float omega_ref);
void foc_set_position(FocController *foc, float theta_ref);
void foc_set_current(FocController *foc, float id_ref, float iq_ref);

/*
 * Inner current loop — call from ADC interrupt at PWM frequency.
 * Reads ia, ib from the struct (caller fills them before calling).
 * Writes ta, tb, tc and advances mode state machine.
 */
void foc_current_isr(FocController *foc);

/*
 * Speed loop — call at 1 kHz from SysTick or timer interrupt.
 * Reads omega_mech (caller updates from encoder before calling).
 * Writes iq_ref for the current loop.
 */
void foc_speed_update(FocController *foc);

/*
 * Position loop — call at 500 Hz.
 * Reads theta_mech; writes omega_ref for the speed loop.
 */
void foc_position_update(FocController *foc);

/* Fault injection (for testing countermeasures). */
void foc_inject_fault(FocController *foc, uint32_t fault_mask);
void foc_clear_faults(FocController *foc);

#endif /* FOC_H */
