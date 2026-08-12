/* mips_dsp.S — Hand-optimized MIPS32r2 DSP routines for the PIC32MX FM synthesizer.
 *
 * Target: PIC32MX (MIPS32 M4K core, no DSP extension ASE).
 * Toolchain: XC32 (GNU assembler for MIPS).
 *
 * Routines exported:
 *   wavetable_interp  — 8-bit linear-interpolated sine table lookup
 *   q15_mul           — signed Q15 fixed-point multiply
 *
 * Both routines are called from the Timer2 ISR at 22 050 Hz so every saved
 * cycle counts.  Delay slots are filled manually; noreorder is asserted
 * throughout so the assembler does not silently reorder instructions. */

#include <xc.h>

    .set    nomips16
    .set    noreorder
    .set    mips32r2

/* ---------------------------------------------------------------------------
 * int16_t wavetable_interp(const int16_t *table, uint32_t phase)
 *
 * Performs an interpolated lookup into a 1 024-point Q15 sine table using
 * the 8 most significant fractional bits of the 32-bit DDS phase accumulator.
 *
 * Phase accumulator layout:
 *   bits[31:22] — integer index into the 1 024-entry table  (10 bits)
 *   bits[21:14] — 8-bit interpolation fraction               (8 bits)
 *   bits[13: 0] — unused sub-fraction                        (14 bits)
 *
 * Algorithm:
 *   y0   = table[index]
 *   y1   = table[(index + 1) & 1023]
 *   frac = (phase >> 14) & 0xFF        (0..255)
 *   out  = y0 + ((y1 - y0) * frac) >> 8
 *
 * Register map:
 *   a0 = table pointer, a1 = phase
 *   t0 = index, t1 = byte address of table[index]
 *   t2 = y0,   t3 = y1,   t4 = frac,   t5 = diff
 *   v0 = return value
 *
 * Estimated cycle count: 14 (no data-cache miss)
 * --------------------------------------------------------------------------- */
    .globl  wavetable_interp
    .ent    wavetable_interp
wavetable_interp:
    srl     $t0, $a1, 22            /* t0 = phase[31:22]  (0..1023) */
    sll     $t1, $t0, 1             /* byte offset = index * 2 */
    addu    $t1, $t1, $a0           /* t1 = &table[index] */
    lh      $t2, 0($t1)             /* t2 = y0  (sign-extended to 32 bits) */
    addiu   $t3, $t0, 1
    andi    $t3, $t3, 0x3FF         /* t3 = (index + 1) & 1023 */
    sll     $t3, $t3, 1
    addu    $t3, $t3, $a0
    lh      $t3, 0($t3)             /* t3 = y1  (sign-extended to 32 bits) */
    srl     $t4, $a1, 14
    andi    $t4, $t4, 0xFF          /* t4 = 8-bit fraction (0..255) */
    subu    $t5, $t3, $t2           /* t5 = y1 - y0 */
    mult    $t5, $t4                /* hi:lo = (y1-y0) * frac  (signed × positive) */
    mflo    $v0
    sra     $v0, $v0, 8             /* v0 = interpolated delta */
    addu    $v0, $v0, $t2           /* v0 = y0 + delta */
    jr      $ra
    nop
    .end    wavetable_interp


/* ---------------------------------------------------------------------------
 * int32_t q15_mul(int32_t a, int32_t b)
 *
 * Signed Q15 fixed-point multiply.  Both operands lie in [-32768, 32767]
 * (approximately [-1.0, 1.0)).  Returns (a * b) >> 15 in the same range.
 *
 * The MIPS MULT instruction produces a 64-bit signed result in hi:lo.
 * We need bits[46:15] of that product:
 *   — bits[31:15] come from lo after an arithmetic right shift by 15
 *   — bits[46:32] come from hi after a left shift by 17
 * OR-ing those two words reconstructs the 32-bit Q15 result.
 *
 * Register map:
 *   a0 = a, a1 = b
 *   v0, v1 = temporaries / return
 *
 * Estimated cycle count: 6 (MULT latency is 2 cycles on M4K, overlapped)
 * --------------------------------------------------------------------------- */
    .globl  q15_mul
    .ent    q15_mul
q15_mul:
    mult    $a0, $a1                /* hi:lo = a * b  (signed 64-bit) */
    mfhi    $v1                     /* v1 = hi = product[63:32] */
    mflo    $v0                     /* v0 = lo = product[31:0]  */
    srl     $v0, $v0, 15            /* v0 = product[31:15] in bits[16:0] */
    sll     $v1, $v1, 17            /* v1 = product[46:32] in bits[31:17] */
    or      $v0, $v0, $v1           /* v0 = product[46:15] = (a*b) >> 15 */
    jr      $ra
    nop
    .end    q15_mul
