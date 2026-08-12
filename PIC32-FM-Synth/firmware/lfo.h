#ifndef LFO_H
#define LFO_H

#include <stdint.h>

typedef enum {
    LFO_TARGET_PITCH = 0,   /* vibrato: frequency modulation of carrier */
    LFO_TARGET_AMPLITUDE    /* tremolo: amplitude modulation of output   */
} lfo_target_t;

typedef struct {
    uint32_t     phase;
    uint32_t     phase_inc;
    uint32_t     rate_centihz;  /* stored rate for partial updates (0.01 Hz units) */
    int32_t      depth;         /* Q15 depth scalar (0 = off, 32 767 = full) */
    lfo_target_t target;
} lfo_t;

/*
 * Configure the LFO.
 *
 *   rate_centihz : frequency in 0.01 Hz units (e.g. 500 = 5.00 Hz)
 *   depth        : Q15 modulation depth (0–32 767)
 *   target       : LFO_TARGET_PITCH or LFO_TARGET_AMPLITUDE
 */
void lfo_set_params(lfo_t *lfo,
                    uint32_t     rate_centihz,
                    int32_t      depth,
                    lfo_target_t target);

/*
 * Advance the LFO by one sample and return the current output.
 * Returns a Q15 value in [-depth, +depth].
 * Called from the audio ISR at SAMPLE_RATE Hz.
 */
int32_t lfo_tick(lfo_t *lfo);

#endif /* LFO_H */
