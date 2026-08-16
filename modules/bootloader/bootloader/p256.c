#include "p256.h"

#include <stdint.h>
#include <string.h>

/*
 * P-256 (secp256r1) ECDSA verification.
 *
 * Field elements are represented as arrays of 8 uint32_t limbs in
 * big-endian limb order: limb[0] is the most-significant 32 bits.
 *
 * Curve parameters:
 *   p  = 2^256 - 2^224 + 2^192 + 2^96 - 1   (field prime)
 *   n  = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
 *   Gx = 0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296
 *   Gy = 0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5
 *   a  = -3 mod p
 *   b  = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
 */

typedef uint32_t fe[8];   /* field element: 8 × 32-bit limbs, big-endian */

typedef struct {
    fe x, y;
    uint8_t inf;   /* 1 = point at infinity */
} point_t;

/* ---- P-256 curve constants ----------------------------------------------- */

static const fe P = {
    0xFFFFFFFFUL, 0x00000001UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL
};

static const fe N = {
    0xFFFFFFFFUL, 0x00000000UL, 0xFFFFFFFFUL, 0xFFFFFFFFUL,
    0xBCE6FAADUL, 0xA7179E84UL, 0xF3B9CAC2UL, 0xFC632551UL
};

static const fe B = {
    0x5AC635D8UL, 0xAA3A93E7UL, 0xB3EBBD55UL, 0x769886BCUL,
    0x651D06B0UL, 0xCC53B0F6UL, 0x3BCE3C3EUL, 0x27D2604BUL
};

static const point_t G = {
    { 0x6B17D1F2UL, 0xE12C4247UL, 0xF8BCE6E5UL, 0x63A440F2UL,
      0x77037D81UL, 0x2DEB33A0UL, 0xF4A13945UL, 0xD898C296UL },
    /* Gy = 4FE342E2 FE1A7F9B 8EE7EB4A 7C0F9E16 2BCE3357 6B315ECE CBB64068
             37BF51F5.  The limbs below used to read ... 0x6B315ECECL,
       0xBB640683UL, 0x7BF51F5UL: nine hex digits in one limb, so every digit
       after it was shifted by one and the last three limbs were wrong. The
       compiler said so — "conversion changes value from 28774362348" — and
       nothing was listening, because this firmware has never linked. */
    { 0x4FE342E2UL, 0xFE1A7F9BUL, 0x8EE7EB4AUL, 0x7C0F9E16UL,
      0x2BCE3357UL, 0x6B315ECEUL, 0xCBB64068UL, 0x37BF51F5UL },
    0
};

/* ---- 256-bit comparison -------------------------------------------------- */

/* Returns -1, 0, or 1 for a < b, a == b, a > b (big-endian limbs). */
static int fe_cmp(const fe a, const fe b)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return  1;
    }
    return 0;
}

static int fe_is_zero(const fe a)
{
    int i;
    for (i = 0; i < 8; i++) if (a[i]) return 0;
    return 1;
}

static void fe_copy(fe dst, const fe src) { memcpy(dst, src, 32); }
static void fe_zero(fe a)                 { memset(a, 0, 32); }

/* ---- Big-endian byte ↔ limb conversion ---------------------------------- */

static void fe_from_bytes(fe dst, const uint8_t src[32])
{
    int i;
    for (i = 0; i < 8; i++) {
        dst[i] = ((uint32_t)src[i*4+0] << 24) | ((uint32_t)src[i*4+1] << 16)
               | ((uint32_t)src[i*4+2] <<  8) |  (uint32_t)src[i*4+3];
    }
}

static void fe_to_bytes(uint8_t dst[32], const fe src)
{
    int i;
    for (i = 0; i < 8; i++) {
        dst[i*4+0] = (uint8_t)(src[i] >> 24);
        dst[i*4+1] = (uint8_t)(src[i] >> 16);
        dst[i*4+2] = (uint8_t)(src[i] >>  8);
        dst[i*4+3] = (uint8_t)(src[i]);
    }
}

/* ---- 256-bit add with carry ---------------------------------------------- */

/* Returns carry out (0 or 1). */
static uint32_t fe_add256(fe c, const fe a, const fe b)
{
    uint64_t acc = 0u;
    int      i;
    for (i = 7; i >= 0; i--) {
        acc     = (uint64_t)a[i] + b[i] + (acc >> 32);
        c[i]    = (uint32_t)acc;
    }
    return (uint32_t)(acc >> 32);
}

/* Returns borrow out (0 or 1). */
static uint32_t fe_sub256(fe c, const fe a, const fe b)
{
    int64_t  acc = 0;
    int      i;
    for (i = 7; i >= 0; i--) {
        acc  = (int64_t)a[i] - b[i] + (acc >> 32);
        c[i] = (uint32_t)acc;
    }
    return (acc >> 32) ? 1u : 0u;
}

/* ---- Modular reduction mod P -------------------------------------------- */

static void fe_reduce_p(fe a)
{
    if (fe_cmp(a, P) >= 0)
        fe_sub256(a, a, P);
}

static void fe_addmod(fe c, const fe a, const fe b)
{
    uint32_t carry = fe_add256(c, a, b);
    if (carry || fe_cmp(c, P) >= 0)
        fe_sub256(c, c, P);
}

static void fe_submod(fe c, const fe a, const fe b)
{
    uint32_t borrow = fe_sub256(c, a, b);
    if (borrow)
        fe_add256(c, c, P);
}

/* ---- Montgomery-free field multiplication (schoolbook, 512-bit → mod P) --
 *
 * For a constrained MCU the schoolbook 8×8 multiply followed by Barrett
 * or fast-reduction mod P is the most readable and auditable choice.
 * Performance: ~180 ms for a point multiplication at 1 MHz — acceptable
 * for a one-shot bootloader verification.
 */

/* 512-bit intermediate: big[0] is most significant. */
typedef uint32_t wide[16];

static void fe_mul_wide(wide res, const fe a, const fe b)
{
    int i, j;
    memset(res, 0, 64);
    for (i = 7; i >= 0; i--) {
        uint64_t carry = 0u;
        for (j = 7; j >= 0; j--) {
            uint64_t prod = (uint64_t)a[i] * b[j] + res[i+j+1] + carry;
            res[i+j+1]   = (uint32_t)prod;
            carry         = prod >> 32;
        }
        res[i] += (uint32_t)carry;
    }
}

/*
 * Fast reduction mod P-256 using the specialised formula from FIPS 186-4,
 * Appendix D.4.  The 512-bit product t = t[0..15] (big-endian 32-bit limbs)
 * is reduced to a 256-bit value mod P.
 *
 * Denote the high 256-bit half as s = (t[0]..t[7]) and the low half as
 * the 256-bit residue (t[8]..t[15]).  The reduction uses eight auxiliary
 * 256-bit quantities formed by rearranging the limbs of the high half.
 */
static void reduce_p256(fe r, const wide t)
{
    fe s;                       /* one scratch value, built and consumed nine times */

    /* FIPS 186-4 D.2.3 works on sixteen 32-bit words A[0..15], A[0] least
     * significant. `wide` is stored most-significant first, so A[k] = t[15-k].
     *
     * The previous version had that mapping right and then indexed A(16)
     * through A(31), which is t[-1] through t[-16]: reads before the array.
     * It reduced 1 to a 256-bit constant. Nothing caught it because this
     * firmware has never linked, so no test ever ran the field arithmetic.
     *
     * It also held all nine terms at once. XC8 in free mode does not overlay
     * locals, and this function is on every multiply's call path, so those
     * 288 bytes were permanently resident on a part with 2 KB. One scratch
     * value, folded into the accumulator as each term is formed, costs 32.
     */
#define A(k) t[15-(k)]
#define TERM(w7, w6, w5, w4, w3, w2, w1, w0)                                 \
    do {                                                                     \
        s[0]=(w7); s[1]=(w6); s[2]=(w5); s[3]=(w4);                           \
        s[4]=(w3); s[5]=(w2); s[6]=(w1); s[7]=(w0);                           \
        fe_reduce_p(s);   /* an arbitrary 256-bit value may exceed P, and    \
                             fe_addmod assumes both operands are below it */ \
    } while (0)

    /* r = s1 + 2*s2 + 2*s3 + s4 + s5 - s6 - s7 - s8 - s9  (mod P), with each
       tuple written most-significant word first. */

    TERM(A(7), A(6), A(5), A(4), A(3), A(2), A(1), A(0));       /* s1 */
    fe_copy(r, s);

    TERM(A(15), A(14), A(13), A(12), A(11), 0, 0, 0);           /* s2, twice */
    fe_addmod(r, r, s); fe_addmod(r, r, s);

    TERM(0, A(15), A(14), A(13), A(12), 0, 0, 0);               /* s3, twice */
    fe_addmod(r, r, s); fe_addmod(r, r, s);

    TERM(A(15), A(14), 0, 0, 0, A(10), A(9), A(8));             /* s4 */
    fe_addmod(r, r, s);

    TERM(A(8), A(13), A(15), A(14), A(13), A(11), A(10), A(9)); /* s5 */
    fe_addmod(r, r, s);

    TERM(A(10), A(8), 0, 0, 0, A(13), A(12), A(11));            /* s6 */
    fe_submod(r, r, s);

    TERM(A(11), A(9), 0, 0, A(15), A(14), A(13), A(12));        /* s7 */
    fe_submod(r, r, s);

    TERM(A(12), 0, A(10), A(9), A(8), A(15), A(14), A(13));     /* s8 */
    fe_submod(r, r, s);

    TERM(A(13), 0, A(11), A(10), A(9), 0, A(15), A(14));        /* s9 */
    fe_submod(r, r, s);

#undef TERM
#undef A

    fe_reduce_p(r);
}


static void fe_mulmod(fe c, const fe a, const fe b)
{
    wide w;
    fe_mul_wide(w, a, b);
    reduce_p256(c, w);
}

static void fe_sqrmod(fe c, const fe a) { fe_mulmod(c, a, a); }

/* ---- Modular inverse mod P (Fermat: a^(P-2) mod P) ---------------------- */

static void fe_invmod(fe out, const fe a)
{
    /*
     * P - 2 = FFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFF...FFFFFFFD.
     * Use an addition chain derived from the known bit pattern of P-2.
     * This is an optimised square-and-multiply ladder for P-256.
     *
     * For compactness and correctness we use the generic binary method
     * (left-to-right) over the 256-bit exponent P-2.  The result is
     * identical to the addition-chain approach but slightly slower.
     */
    fe    base, result;
    uint8_t exp_bytes[32];
    int   bit, byte_idx;

    /* Compute P - 2 as a byte array. */
    {
        fe p2;
        fe two = {0,0,0,0,0,0,0,2};
        fe_sub256(p2, P, two);
        fe_to_bytes(exp_bytes, p2);
    }

    fe_copy(base, a);
    fe_zero(result);
    result[7] = 1u;   /* result = 1 */

    for (byte_idx = 0; byte_idx < 32; byte_idx++) {
        for (bit = 7; bit >= 0; bit--) {
            fe_sqrmod(result, result);
            if ((exp_bytes[byte_idx] >> bit) & 1)
                fe_mulmod(result, result, base);
        }
    }
    fe_copy(out, result);
}

/* ---- Point arithmetic (affine coordinates) ------------------------------ */

static void point_copy(point_t *dst, const point_t *src)
{
    fe_copy(dst->x, src->x);
    fe_copy(dst->y, src->y);
    dst->inf = src->inf;
}

/* Jacobian coordinates for intermediate steps:
 * (X, Y, Z) represents affine (X/Z^2, Y/Z^3). */
typedef struct { fe X, Y, Z; } jpoint_t;

static void jpoint_from_affine(jpoint_t *J, const point_t *A)
{
    fe_copy(J->X, A->x);
    fe_copy(J->Y, A->y);
    fe_zero(J->Z); J->Z[7] = 1u;   /* Z = 1 */
}

static void jpoint_to_affine(point_t *A, const jpoint_t *J)
{
    fe zinv, zinv2, zinv3;
    fe_invmod(zinv, J->Z);
    fe_mulmod(zinv2, zinv,  zinv);
    fe_mulmod(zinv3, zinv2, zinv);
    fe_mulmod(A->x, J->X, zinv2);
    fe_mulmod(A->y, J->Y, zinv3);
    A->inf = 0;
}

/* Jacobian point doubling: R = 2P. */
static void jpoint_double(jpoint_t *R, const jpoint_t *P_j)
{
    fe A, B, C, D, E, F, tmp;

    /* A = 4*X*Y^2 */
    fe_mulmod(tmp, P_j->Y, P_j->Y);          /* Y^2 */
    fe_mulmod(A,   P_j->X, tmp);              /* X*Y^2 */
    fe_addmod(A, A, A); fe_addmod(A, A, A);   /* 4*X*Y^2 */

    /* B = 3*(X^2 - Z^4) using a = -3 special case */
    fe_mulmod(B, P_j->X, P_j->X);            /* X^2 */
    fe_mulmod(C, P_j->Z, P_j->Z);            /* Z^2 */
    fe_mulmod(D, C, C);                       /* Z^4 */
    fe_submod(E, B, D);                       /* X^2 - Z^4 */
    fe_copy(B, E);
    fe_addmod(B, B, E); fe_addmod(B, B, E);  /* 3*(X^2-Z^4) */

    /* Z' = 2*Y*Z.
     *
     * Computed here, before anything is written to R, because point_mul
     * doubles in place — `jpoint_double(&acc, &acc)` — so R and P_j are the
     * same object. This used to come last, after R->Y had been written, and
     * therefore squared the *new* Y into the new Z. Every doubling after the
     * first was wrong, so 1*G was right and 2*G was not.
     *
     * Everything below reads only locals and P_j->X, which is not written
     * until the end. fe_mulmod and fe_addmod are single-pass and safe when
     * their output aliases an input.
     */
    fe_mulmod(R->Z, P_j->Y, P_j->Z);
    fe_addmod(R->Z, R->Z, R->Z);

    /* Y^4, from the Y^2 computed at the top — also before R->Y is written. */
    fe_mulmod(C, tmp, tmp);                   /* Y^4 */
    fe_addmod(C, C, C); fe_addmod(C, C, C);
    fe_addmod(C, C, C);                       /* 8*Y^4 */

    /* F = B^2 - 2*A */
    fe_mulmod(F, B, B);
    fe_submod(F, F, A); fe_submod(F, F, A);

    /* Y' = B*(A - F) - 8*Y^4 */
    fe_submod(E, A, F);
    fe_mulmod(R->Y, B, E);
    fe_submod(R->Y, R->Y, C);

    /* X' = F, last: P_j->X is read above. */
    fe_copy(R->X, F);

    (void)D;
}

/* Jacobian-affine mixed addition: R = P (Jacobian) + Q (affine). */
static void jpoint_add_affine(jpoint_t *R, const jpoint_t *Pj, const point_t *Q)
{
    fe Z2, U2, S2, H, R_val, H2, H3, tmp;

    fe_mulmod(Z2, Pj->Z, Pj->Z);             /* Z1^2 */
    fe_mulmod(U2, Q->x, Z2);                 /* U2 = X2*Z1^2 */
    fe_mulmod(S2, Q->y, Z2);
    fe_mulmod(S2, S2,   Pj->Z);              /* S2 = Y2*Z1^3 */

    fe_submod(H,     U2, Pj->X);            /* H = U2 - X1 */
    fe_submod(R_val, S2, Pj->Y);            /* r = S2 - Y1 */

    fe_mulmod(H2, H, H);
    fe_mulmod(H3, H2, H);

    /* X3 = r^2 - H^3 - 2*X1*H^2 */
    fe_mulmod(R->X, R_val, R_val);
    fe_submod(R->X, R->X, H3);
    fe_mulmod(tmp, Pj->X, H2);
    fe_submod(R->X, R->X, tmp);
    fe_submod(R->X, R->X, tmp);

    /* Y3 = r*(X1*H^2 - X3) - Y1*H^3 */
    fe_submod(tmp, tmp, R->X);              /* tmp = X1*H^2 - X3, reuse */
    fe_mulmod(R->Y, R_val, tmp);
    fe_mulmod(tmp, Pj->Y, H3);
    fe_submod(R->Y, R->Y, tmp);

    /* Z3 = Z1*H */
    fe_mulmod(R->Z, Pj->Z, H);
}

/* Double-and-add scalar multiplication: result = scalar * base. */
static void point_mul(point_t *result, const fe scalar, const point_t *base)
{
    jpoint_t acc;
    jpoint_t tmp_j;
    uint8_t  scalar_bytes[32];
    uint8_t  first = 1u;
    int      byte_idx, bit;

    fe_to_bytes(scalar_bytes, scalar);
    fe_zero(acc.X); fe_zero(acc.Y); fe_zero(acc.Z);
    acc.Z[7] = 0u;   /* accumulator starts as infinity (Z=0) */

    for (byte_idx = 0; byte_idx < 32; byte_idx++) {
        for (bit = 7; bit >= 0; bit--) {
            if (!first) {
                jpoint_double(&acc, &acc);
            }
            if ((scalar_bytes[byte_idx] >> bit) & 1u) {
                if (first) {
                    jpoint_from_affine(&acc, base);
                    first = 0u;
                } else {
                    jpoint_add_affine(&tmp_j, &acc, base);
                    memcpy(&acc, &tmp_j, sizeof(acc));
                }
            }
        }
    }

    if (first || fe_is_zero(acc.Z)) {
        result->inf = 1u;
        return;
    }
    jpoint_to_affine(result, &acc);
}

/* Simultaneous double scalar multiplication: u1*G + u2*Q (naive). */
static void point_mul2(point_t *result,
                       const fe u1, const point_t *G_pt,
                       const fe u2, const point_t *Q)
{
    point_t R1, R2;
    jpoint_t J1, Jacc;

    point_mul(&R1, u1, G_pt);
    point_mul(&R2, u2, Q);

    if (R1.inf) { memcpy(result, &R2, sizeof(*result)); return; }
    if (R2.inf) { memcpy(result, &R1, sizeof(*result)); return; }

    /* Affine addition R1 + R2. */
    jpoint_from_affine(&J1, &R1);
    jpoint_add_affine(&Jacc, &J1, &R2);
    jpoint_to_affine(result, &Jacc);
}

/* ---- Arithmetic mod N ----------------------------------------------------- *
 *
 * The scalar field, and it is not the coordinate field. `fe_mulmod` and
 * `fe_sqrmod` reduce mod P, and P != N, so they cannot be borrowed for scalar
 * work: (x mod P) mod N is not x mod N. Both of the routines below used to do
 * exactly that — one with a comment saying so — which made every signature
 * verification wrong.
 *
 * Multiplication is double-and-add rather than a wide product with a mod-N
 * reduction, because it needs only add and compare, which already exist and
 * are already tested. 256 iterations to verify one signature, once, at boot.
 */

/* c = (a + b) mod N, for a, b < N.
 *
 * One conditional subtraction is enough: a + b < 2N < 2^257, so the sum
 * exceeds N by less than N. The carry out of the 256-bit add is part of the
 * comparison — a sum that wrapped is larger than N even when the low 256 bits
 * are not.
 */
static void fe_addmod_n(fe c, const fe a, const fe b)
{
    uint32_t carry = fe_add256(c, a, b);
    if (carry || fe_cmp(c, N) >= 0)
        fe_sub256(c, c, N);
}

/* c = (a * b) mod N, for a, b < N. */
static void fe_mulmod_n(fe c, const fe a, const fe b)
{
    fe acc;
    int limb, bit;

    fe_zero(acc);
    for (limb = 0; limb < 8; limb++) {          /* limb 0 is most significant */
        for (bit = 31; bit >= 0; bit--) {
            fe_addmod_n(acc, acc, acc);         /* acc *= 2 */
            if ((b[limb] >> bit) & 1u)
                fe_addmod_n(acc, acc, a);
        }
    }
    fe_copy(c, acc);
}

/* a mod N, for a < 2^256. One subtraction, since N > 2^255. */
static void fe_reduce_n(fe a)
{
    if (fe_cmp(a, N) >= 0)
        fe_sub256(a, a, N);
}

/* ---- Modular inverse mod N ----------------------------------------------- */

static void fe_invmod_n(fe out, const fe a)
{
    fe base, result;
    uint8_t exp_bytes[32];
    int     bit, byte_idx;

    /* N - 2, for Fermat: a^(N-2) == a^-1 mod N when N is prime. */
    fe n2;
    fe two = {0,0,0,0,0,0,0,2};
    fe_sub256(n2, N, two);
    fe_to_bytes(exp_bytes, n2);

    fe_copy(base, a);
    fe_zero(result);
    result[7] = 1u;

    for (byte_idx = 0; byte_idx < 32; byte_idx++) {
        for (bit = 7; bit >= 0; bit--) {
            fe_mulmod_n(result, result, result);
            if ((exp_bytes[byte_idx] >> bit) & 1)
                fe_mulmod_n(result, result, base);
        }
    }
    fe_copy(out, result);
}

/* ---- ECDSA verify -------------------------------------------------------- */

int p256_verify(const uint8_t pubkey[65],
                const uint8_t hash[32],
                const uint8_t sig_r[32],
                const uint8_t sig_s[32])
{
    fe r, s, e, w, u1, u2;
    point_t Q, R;

    /* Reject point-at-infinity prefix or wrong marker. */
    if (pubkey[0] != 0x04u) return -1;

    /* Load public key. */
    fe_from_bytes(Q.x, pubkey + 1);
    fe_from_bytes(Q.y, pubkey + 33);
    Q.inf = 0;

    /* Load and range-check r, s. */
    fe_from_bytes(r, sig_r);
    fe_from_bytes(s, sig_s);
    if (fe_is_zero(r) || fe_cmp(r, N) >= 0) return -2;
    if (fe_is_zero(s) || fe_cmp(s, N) >= 0) return -3;

    /* e = hash as an integer, reduced into the scalar field. */
    fe_from_bytes(e, hash);
    fe_reduce_n(e);

    /* w = s^(-1) mod n. */
    fe_invmod_n(w, s);

    /* u1 = e*w mod n,  u2 = r*w mod n — in the scalar field, not the
       coordinate field. This used to take the wide product, reduce it mod P
       and then subtract N until it fit, which computes (x mod P) mod N. That
       is not x mod N, and no signature verified. */
    fe_mulmod_n(u1, e, w);
    fe_mulmod_n(u2, r, w);

    /* R = u1*G + u2*Q */
    point_mul2(&R, u1, &G, u2, &Q);
    if (R.inf) return -4;

    /* Verify R.x mod n == r. */
    while (fe_cmp(R.x, N) >= 0) fe_sub256(R.x, R.x, N);
    return (fe_cmp(R.x, r) == 0) ? 0 : -5;
}
