#ifndef ADSR_H
#define ADSR_H

#include <stdint.h>

typedef enum {
    ADSR_IDLE = 0,
    ADSR_ATTACK,
    ADSR_DECAY,
    ADSR_SUSTAIN,
    ADSR_RELEASE
} adsr_state_t;

/*
 * All amplitude values are Q15 (0 to 32 767).
 * Rate fields are the per-sample increment / decrement to apply in each stage.
 * A rate of 1 gives the slowest possible transition; 32 767 gives an instant step.
 */
typedef struct {
    adsr_state_t state;
    int32_t      amplitude;
    int32_t      sustain_level;
    int32_t      attack_rate;
    int32_t      decay_rate;
    int32_t      release_rate;
} adsr_t;

/*
 * Recalculate stage rates from time parameters.
 *
 *   attack_ms    : rise time from 0 to peak, in milliseconds
 *   decay_ms     : fall time from peak to sustain, in milliseconds
 *   sustain_level: Q15 amplitude held during sustain (0–32 767)
 *   release_ms   : fall time from sustain to zero after note-off
 */
void adsr_set_params(adsr_t *env,
                     uint32_t attack_ms,
                     uint32_t decay_ms,
                     int32_t  sustain_level,
                     uint32_t release_ms);

/* Trigger note-on: restart envelope from zero amplitude. */
void adsr_note_on(adsr_t *env);

/* Trigger note-off: move to release stage from whatever state is active. */
void adsr_note_off(adsr_t *env);

/*
 * Advance the envelope by one sample.
 * Called from the audio ISR at SAMPLE_RATE Hz.
 * Returns the current Q15 amplitude.
 */
int32_t adsr_tick(adsr_t *env);

static inline int adsr_is_idle(const adsr_t *env)
{
    return (env->state == ADSR_IDLE);
}

#endif /* ADSR_H */
