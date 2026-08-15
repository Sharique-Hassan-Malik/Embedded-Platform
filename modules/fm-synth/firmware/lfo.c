#include "lfo.h"
#include "wavetable.h"

void lfo_set_params(lfo_t *lfo,
                    uint32_t     rate_centihz,
                    int32_t      depth,
                    lfo_target_t target)
{
    double freq;

    lfo->rate_centihz = rate_centihz;
    lfo->depth        = (depth < 0) ? 0 : (depth > 32767 ? 32767 : depth);
    lfo->target       = target;

    /* Compute DDS phase increment from centihz rate.
     * freq = rate_centihz / 100.0
     * phase_inc = freq / SAMPLE_RATE * 2^32 */
    freq           = (double)rate_centihz / 100.0;
    lfo->phase_inc = (uint32_t)(freq / (double)SAMPLE_RATE * 4294967296.0);
}

int32_t lfo_tick(lfo_t *lfo)
{
    int32_t sample;

    lfo->phase += lfo->phase_inc;
    sample      = wavetable_interp(sine_table, lfo->phase);   /* Q15 */
    return q15_mul(sample, lfo->depth);                        /* scale by depth */
}
