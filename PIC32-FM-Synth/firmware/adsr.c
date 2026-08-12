#include "adsr.h"
#include "wavetable.h"   /* SAMPLE_RATE */

#define MS_TO_SAMPLES(ms)  ((uint32_t)(ms) * SAMPLE_RATE / 1000u)

void adsr_set_params(adsr_t *env,
                     uint32_t attack_ms,
                     uint32_t decay_ms,
                     int32_t  sustain_level,
                     uint32_t release_ms)
{
    uint32_t samples;

    if (sustain_level < 0)      sustain_level = 0;
    if (sustain_level > 32767)  sustain_level = 32767;
    env->sustain_level = sustain_level;

    samples = MS_TO_SAMPLES(attack_ms);
    env->attack_rate = (samples > 0) ? (32767 / (int32_t)samples) : 32767;

    samples = MS_TO_SAMPLES(decay_ms);
    env->decay_rate = (samples > 0)
                    ? ((32767 - sustain_level) / (int32_t)samples)
                    : 32767;

    samples = MS_TO_SAMPLES(release_ms);
    if (samples > 0 && sustain_level > 0) {
        env->release_rate = sustain_level / (int32_t)samples;
        if (env->release_rate < 1) env->release_rate = 1;
    } else {
        env->release_rate = (sustain_level > 0) ? sustain_level : 1;
    }
}

void adsr_note_on(adsr_t *env)
{
    env->state     = ADSR_ATTACK;
    env->amplitude = 0;
}

void adsr_note_off(adsr_t *env)
{
    if (env->state != ADSR_IDLE)
        env->state = ADSR_RELEASE;
}

int32_t adsr_tick(adsr_t *env)
{
    switch (env->state) {

    case ADSR_ATTACK:
        env->amplitude += env->attack_rate;
        if (env->amplitude >= 32767) {
            env->amplitude = 32767;
            env->state     = ADSR_DECAY;
        }
        break;

    case ADSR_DECAY:
        env->amplitude -= env->decay_rate;
        if (env->amplitude <= env->sustain_level) {
            env->amplitude = env->sustain_level;
            env->state     = ADSR_SUSTAIN;
        }
        break;

    case ADSR_SUSTAIN:
        /* Amplitude held constant until note-off. */
        break;

    case ADSR_RELEASE:
        env->amplitude -= env->release_rate;
        if (env->amplitude <= 0) {
            env->amplitude = 0;
            env->state     = ADSR_IDLE;
        }
        break;

    default: /* ADSR_IDLE */
        env->amplitude = 0;
        break;
    }

    return env->amplitude;
}
