#ifndef FM_SYNTH_H
#define FM_SYNTH_H

#include <stdint.h>
#include "adsr.h"
#include "lfo.h"

/*
 * Two-operator FM voice: one modulator oscillator feeds into the phase of
 * one carrier oscillator.
 *
 * Algorithm:
 *   mod_out  = sin(mod_phase) * mod_index          (modulator, Q15)
 *   carrier  = sin(carrier_phase + mod_out_scaled)  (FM output, Q15)
 *   output   = carrier * adsr_amplitude * velocity
 *
 * Modulator frequency: mod_freq = carrier_freq * op_ratio_q8 / 256
 */
typedef struct {
    uint32_t carrier_phase;
    uint32_t carrier_inc;

    uint32_t mod_phase;
    uint32_t mod_inc;

    /*
     * Operator frequency ratio in Q8.
     *   256 = 1:1 (unison)
     *   512 = 2:1 (one octave above carrier)
     *   128 = 0.5:1 (one octave below carrier)
     * Integer ratios produce harmonic spectra.  Non-integer ratios produce
     * inharmonic, bell-like timbres.
     */
    uint16_t op_ratio_q8;

    /*
     * Modulation index, Q8.
     *   0   = pure sine (no FM)
     *   256 = moderate modulation (a few sidebands)
     *   768 = heavy modulation (rich harmonic content)
     */
    uint16_t mod_index;

    adsr_t   envelope;
    lfo_t    lfo;

    uint8_t  note;       /* current MIDI note, 0xFF when idle */
    uint8_t  velocity;
} fm_voice_t;

/* Initialize voice with default timbre (piano-like). */
void fm_voice_init(fm_voice_t *v);

/* Trigger a note.  Resets oscillator phases and restarts the envelope. */
void fm_voice_note_on(fm_voice_t *v, uint8_t note, uint8_t velocity);

/* Release the note (begins release stage of envelope). */
void fm_voice_note_off(fm_voice_t *v);

/* Update modulation index (Q8). */
void fm_voice_set_mod_index(fm_voice_t *v, uint16_t index_q8);

/* Update operator ratio (Q8) and retune the modulator if a note is held. */
void fm_voice_set_op_ratio(fm_voice_t *v, uint16_t ratio_q8);

/*
 * Compute one audio sample.
 * Called from the Timer2 ISR at 22 050 Hz.
 * Returns Q15 signed sample.
 */
int32_t fm_voice_tick(fm_voice_t *v);

#endif /* FM_SYNTH_H */
