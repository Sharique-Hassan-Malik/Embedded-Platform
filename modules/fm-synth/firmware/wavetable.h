#ifndef WAVETABLE_H
#define WAVETABLE_H

#include <stdint.h>

/* Size of the sine lookup table.  Must be a power of two. */
#define WAVE_SIZE    1024
#define WAVE_MASK    (WAVE_SIZE - 1)

/* Audio sample rate in Hz. */
#define SAMPLE_RATE  22050u

/*
 * 1 024-point Q15 sine table.
 * Populated once by wavetable_init() using the math library.
 * Declared volatile so the compiler does not optimize away stores
 * made from wavetable_init() before the ISR runs.
 */
extern int16_t sine_table[WAVE_SIZE];

/*
 * DDS phase increment for each of the 128 MIDI notes.
 * phase_inc[n] = round(f(n) / SAMPLE_RATE * 2^32)
 * where f(n) = 440 * 2^((n-69)/12).
 * Populated by wavetable_init().
 */
extern uint32_t midi_phase_inc[128];

/* Populate sine_table and midi_phase_inc.  Call once before enabling
 * interrupts.  Uses floating-point math (software FPU on PIC32MX). */
void wavetable_init(void);

/*
 * Interpolated Q15 sine lookup — implemented in mips_dsp.S.
 *
 *   table : pointer to WAVE_SIZE-element int16_t array
 *   phase : 32-bit DDS accumulator
 *           bits[31:22] = integer index (0..1023)
 *           bits[21:14] = 8-bit interpolation fraction
 *
 * Returns interpolated Q15 value (-32767 to +32767).
 */
int16_t wavetable_interp(const int16_t *table, uint32_t phase);

/*
 * Q15 multiply — implemented in mips_dsp.S.
 * Returns (a * b) >> 15.  Both arguments and the return value are Q15.
 */
int32_t q15_mul(int32_t a, int32_t b);

#endif /* WAVETABLE_H */
