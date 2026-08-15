#include "fm_synth.h"
#include "wavetable.h"

#include <string.h>

void fm_voice_init(fm_voice_t *v)
{
    memset(v, 0, sizeof(*v));

    v->note        = 0xFF;
    v->op_ratio_q8 = 256;    /* 1:1 ratio — modulator tracks carrier */
    v->mod_index   = 192;    /* moderate FM depth */

    /* Default ADSR: plucked-string approximation.
     * Attack 8 ms, decay 150 ms, sustain 60% (~19 660 Q15), release 400 ms. */
    adsr_set_params(&v->envelope, 8u, 150u, 19660, 400u);

    /* LFO: 5 Hz vibrato at shallow depth. */
    lfo_set_params(&v->lfo, 500u, 2048, LFO_TARGET_PITCH);
}

void fm_voice_note_on(fm_voice_t *v, uint8_t note, uint8_t velocity)
{
    uint32_t base_inc;

    v->note     = note & 0x7Fu;
    v->velocity = velocity;

    base_inc       = midi_phase_inc[v->note];
    v->carrier_inc = base_inc;

    /* mod_inc = carrier_inc * op_ratio_q8 / 256
     * Use 64-bit intermediate to avoid overflow — only runs on note-on,
     * not in the hot ISR path. */
    v->mod_inc = (uint32_t)(((uint64_t)base_inc * v->op_ratio_q8) >> 8);

    /* Phase reset on new note for a clean attack transient. */
    v->carrier_phase = 0;
    v->mod_phase     = 0;

    adsr_note_on(&v->envelope);
}

void fm_voice_note_off(fm_voice_t *v)
{
    adsr_note_off(&v->envelope);
}

void fm_voice_set_mod_index(fm_voice_t *v, uint16_t index_q8)
{
    v->mod_index = index_q8;
}

void fm_voice_set_op_ratio(fm_voice_t *v, uint16_t ratio_q8)
{
    v->op_ratio_q8 = ratio_q8;
    /* Retune the running modulator immediately if a note is held. */
    if (v->note != 0xFF)
        v->mod_inc = (uint32_t)(((uint64_t)v->carrier_inc * ratio_q8) >> 8);
}

int32_t fm_voice_tick(fm_voice_t *v)
{
    int32_t  lfo_val;
    int32_t  mod_sample;
    uint32_t mod_scaled;
    uint32_t modulated_carrier_phase;
    int32_t  carrier_sample;
    int32_t  env_amp;
    int32_t  output;

    /* Nothing to do when the envelope has fully decayed. */
    if (adsr_is_idle(&v->envelope))
        return 0;

    /* ---- LFO ---- */
    lfo_val = lfo_tick(&v->lfo);   /* Q15, scaled by depth */

    /* ---- Modulator oscillator ---- */
    v->mod_phase += v->mod_inc;
    mod_sample    = wavetable_interp(sine_table, v->mod_phase);   /* Q15 */

    /* Scale modulator output by mod_index.
     * mod_index is Q8 (256 = 1x).  Combine with Q15 sample:
     *   mod_scaled = mod_sample * mod_index / 256
     * = (Q15 * Q8) >> 8 → Q15 */
    mod_scaled = (uint32_t)((int32_t)mod_sample * (int32_t)v->mod_index >> 8);

    /* mod_scaled is used as a phase offset on the carrier (the FM step).
     * Left-shift by 7 maps the Q15 range to roughly ±2 pi radians of
     * phase deviation at mod_index = 256.  Larger mod_index values push
     * deeper into non-linear FM territory producing more sidebands. */
    modulated_carrier_phase = v->carrier_phase + (mod_scaled << 7);

    /* ---- Carrier phase advance with optional vibrato ---- */
    if (v->lfo.target == LFO_TARGET_PITCH) {
        /* Vibrato: shift carrier_inc by ±(carrier_inc >> 19) * lfo_val.
         * At full LFO depth (lfo_val ≈ 32767) this is ±carrier_inc/16,
         * giving about ±1 semitone of pitch deviation — a musically
         * useful maximum.  The arithmetic stays within 32 bits because
         * carrier_inc >> 19 ≤ 8192 and lfo_val ≤ 32767. */
        int32_t vib = (int32_t)(v->carrier_inc >> 19) * (lfo_val >> 1);
        v->carrier_phase += v->carrier_inc + (uint32_t)vib;
    } else {
        v->carrier_phase += v->carrier_inc;
    }

    /* ---- Carrier lookup ---- */
    carrier_sample = wavetable_interp(sine_table, modulated_carrier_phase);

    /* ---- Amplitude envelope ---- */
    env_amp = adsr_tick(&v->envelope);
    output  = q15_mul(carrier_sample, env_amp);

    /* ---- Tremolo (amplitude LFO) ---- */
    if (v->lfo.target == LFO_TARGET_AMPLITUDE) {
        /* lfo_val ∈ [-depth, +depth].  Map to a [1 - depth/2, 1 + depth/2]
         * gain factor expressed in Q15 so q15_mul applies it cleanly. */
        int32_t trem_gain = 32767 + (lfo_val >> 1);
        if (trem_gain > 32767) trem_gain = 32767;
        if (trem_gain < 0)     trem_gain = 0;
        output = q15_mul(output, trem_gain);
    }

    /* ---- Velocity scaling ---- */
    /* velocity (0–127) → Q15: 127 * 258 = 32 766. */
    output = q15_mul(output, (int32_t)v->velocity * 258);

    /* Silence the voice tag once the envelope goes idle. */
    if (adsr_is_idle(&v->envelope))
        v->note = 0xFF;

    return output;
}
