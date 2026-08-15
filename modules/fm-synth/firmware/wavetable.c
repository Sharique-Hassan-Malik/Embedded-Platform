#include "wavetable.h"

#include <math.h>
#include <stdint.h>

/* M_PI is not part of ISO C; define it if the toolchain's <math.h> (in a
   strict-conformance mode) does not. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int16_t  sine_table[WAVE_SIZE];
uint32_t midi_phase_inc[128];

void wavetable_init(void)
{
    int i;

    /* Build Q15 sine table.
     * sin(0) = 0, sin(pi/2) = 1.0 → 32767.
     * The table wraps cleanly because WAVE_SIZE is a power of two. */
    for (i = 0; i < WAVE_SIZE; i++) {
        double angle  = 2.0 * M_PI * (double)i / (double)WAVE_SIZE;
        sine_table[i] = (int16_t)(sin(angle) * 32767.0);
    }

    /* Build MIDI note → DDS phase increment table.
     * Full-scale accumulator = 2^32 counts per cycle.
     * Increment to advance by one cycle of frequency f per sample:
     *   phase_inc = f / SAMPLE_RATE * 2^32 */
    for (i = 0; i < 128; i++) {
        double freq      = 440.0 * pow(2.0, (i - 69) / 12.0);
        midi_phase_inc[i] = (uint32_t)(freq / (double)SAMPLE_RATE * 4294967296.0);
    }
}
