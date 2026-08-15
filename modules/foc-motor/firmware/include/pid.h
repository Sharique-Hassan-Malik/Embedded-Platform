#ifndef PID_H
#define PID_H

#include <stdint.h>

/*
 * Discrete PID controller with:
 *   - Anti-windup via integrator clamping (back-calculation).
 *   - Derivative on measurement (not on error) to avoid derivative kick
 *     on step setpoint changes.
 *   - Configurable output and integrator saturation limits.
 *   - Optional feed-forward term.
 *
 * Update law (position form):
 *
 *   e[n]    = setpoint - measurement
 *   D[n]    = -Kd * (measurement[n] - measurement[n-1]) / Ts
 *   I[n]    = I[n-1] + Ki * e[n] * Ts    (before clamping)
 *   u[n]    = Kp * e[n] + I[n] + D[n] + ff
 *
 *   If |u[n]| > out_max:
 *     u_sat[n] = clamp(u[n], -out_max, out_max)
 *     I[n]    -= (u[n] - u_sat[n]) * Kb   (back-calculation)
 *
 * Ts is implicit in the gain values — the caller is responsible for
 * invoking pid_update() at a fixed rate equal to 1/Ts.
 */
typedef struct {
    float kp;           /* proportional gain */
    float ki;           /* integral gain (already multiplied by Ts) */
    float kd;           /* derivative gain (already divided by Ts) */
    float kb;           /* back-calculation coefficient for anti-windup */
    float out_max;      /* symmetric output saturation limit */
    float int_max;      /* symmetric integrator saturation limit */
    float integrator;   /* integrator state */
    float prev_meas;    /* previous measurement for derivative */
} PidController;

/*
 * Initialise all fields.  kb = ki / kp is a common default; pass 0.0
 * to disable back-calculation (pure clamping anti-windup instead).
 */
static inline void pid_init(PidController *pid,
                             float kp, float ki, float kd, float kb,
                             float out_max, float int_max)
{
    pid->kp       = kp;
    pid->ki       = ki;
    pid->kd       = kd;
    pid->kb       = kb;
    pid->out_max  = out_max;
    pid->int_max  = int_max;
    pid->integrator = 0.0f;
    pid->prev_meas  = 0.0f;
}

/* Reset integrator and previous-measurement state. */
static inline void pid_reset(PidController *pid)
{
    pid->integrator = 0.0f;
    pid->prev_meas  = 0.0f;
}

/*
 * Run one controller step.
 * setpoint   — desired value
 * measurement— current plant output
 * ff         — feed-forward term (set to 0.0f if unused)
 *
 * Returns the saturated controller output.
 */
static inline float pid_update(PidController *pid,
                                float setpoint, float measurement,
                                float ff)
{
    float e   = setpoint - measurement;
    float d   = -pid->kd * (measurement - pid->prev_meas);
    pid->prev_meas = measurement;

    /* Pre-saturate integrator. */
    float i_new = pid->integrator + pid->ki * e;
    if      (i_new >  pid->int_max) i_new =  pid->int_max;
    else if (i_new < -pid->int_max) i_new = -pid->int_max;
    pid->integrator = i_new;

    float u = pid->kp * e + pid->integrator + d + ff;

    /* Output saturation with back-calculation anti-windup. */
    float u_sat = u;
    if      (u_sat >  pid->out_max) u_sat =  pid->out_max;
    else if (u_sat < -pid->out_max) u_sat = -pid->out_max;

    if (pid->kb > 0.0f)
        pid->integrator -= pid->kb * (u - u_sat);

    return u_sat;
}

#endif /* PID_H */
