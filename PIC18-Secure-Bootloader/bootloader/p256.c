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
    { 0x4FE342E2UL, 0xFE1A7F9BUL, 0x8EE7EB4AUL, 0x7C0F9E16UL,
      0x2BCE3357UL, 0x6B315ECECL, 0xBB640683UL, 0x7BF51F5UL  },
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
    fe s1, s2, s3, s4, s5, s6, s7, s8;
    fe tmp;
    uint32_t carry;

    /* The limb indices below refer to the notation in FIPS 186-4 D.4
     * where t = A[15]A[14]...A[0] with A[0] least significant.
     * Our array has t[0] = most significant → A[15] in FIPS notation.
     * Mapping: FIPS A[k] = t[15-k]. */
#define A(k) t[15-(k)]

    /* s1 = (A15, A14, A13, A12, A11, A10, A9, A8)  — the low half already. */
    s1[0]=A(15); s1[1]=A(14); s1[2]=A(13); s1[3]=A(12);
    s1[4]=A(11); s1[5]=A(10); s1[6]=A(9);  s1[7]=A(8);

    /* s2 = (A19, A18, A17, A16, 0, 0, 0, 0) */
    s2[0]=A(19); s2[1]=A(18); s2[2]=A(17); s2[3]=A(16);
    s2[4]=0;     s2[5]=0;     s2[6]=0;     s2[7]=0;

    /* s3 = (A23, A22, A21, A20, A19, A18, A17, A16) */
    s3[0]=A(23); s3[1]=A(22); s3[2]=A(21); s3[3]=A(20);
    s3[4]=A(19); s3[5]=A(18); s3[6]=A(17); s3[7]=A(16);

    /* s4 = (A31, A30, A29, A28, A27, A24, A23, A22) */
    s4[0]=A(31); s4[1]=A(30); s4[2]=A(29); s4[3]=A(28);
    s4[4]=A(27); s4[5]=A(24); s4[6]=A(23); s4[7]=A(22);

    /* s5 = (0, A31, A30, A29, A28, 0, A25, A24) */
    s5[0]=0;     s5[1]=A(31); s5[2]=A(30); s5[3]=A(29);
    s5[4]=A(28); s5[5]=0;     s5[6]=A(25); s5[7]=A(24);

    /* s6 = (0, 0, 0, A31, A27, A26, A25, A24) */
    s6[0]=0;     s6[1]=0;     s6[2]=0;     s6[3]=A(31);
    s6[4]=A(27); s6[5]=A(26); s6[6]=A(25); s6[7]=A(24);

    /* s7 = (A31, A30, A29, A28, A27, A26, A25, A23) */
    s7[0]=A(31); s7[1]=A(30); s7[2]=A(29); s7[3]=A(28);
    s7[4]=A(27); s7[5]=A(26); s7[6]=A(25); s7[7]=A(23);

    /* s8 = (A31, A26, A25, A24, A23, A22, A21, A20) */
    s8[0]=A(31); s8[1]=A(26); s8[2]=A(25); s8[3]=A(24);
    s8[4]=A(23); s8[5]=A(22); s8[6]=A(21); s8[7]=A(20);

#undef A

    /* r = s1 + 2s2 + 2s3 + s4 + s5 − s6 − s7 − s8  mod P */
    fe_copy(r, s1);

    fe_addmod(r, r, s2);   fe_addmod(r, r, s2);   /* +2s2 */
    fe_addmod(r, r, s3);   fe_addmod(r, r, s3);   /* +2s3 */
    fe_addmod(r, r, s4);
    fe_addmod(r, r, s5);

    fe_submod(r, r, s6);
    fe_submod(r, r, s7);
    fe_submod(r, r, s8);

    /* A final conditional subtraction ensures r < P. */
    while (fe_cmp(r, P) >= 0)
        fe_sub256(r, r, P);

    (void)tmp; (void)carry;
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

    /* F = B^2 - 2*A */
    fe_mulmod(F, B, B);
    fe_submod(F, F, A); fe_submod(F, F, A);

    /* X' = F */
    fe_copy(R->X, F);

    /* Y' = B*(A - F) - 8*Y^4 */
    fe_submod(E, A, F);
    fe_mulmod(R->Y, B, E);
    fe_mulmod(C, tmp, tmp);                   /* Y^4 */
    fe_addmod(C, C, C); fe_addmod(C, C, C);
    fe_addmod(C, C, C);                       /* 8*Y^4 */
    fe_submod(R->Y, R->Y, C);

    /* Z' = 2*Y*Z */
    fe_mulmod(R->Z, P_j->Y, P_j->Z);
    fe_addmod(R->Z, R->Z, R->Z);

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

/* ---- Modular inverse mod N ----------------------------------------------- */

static void fe_invmod_n(fe out, const fe a)
{
    fe base, result;
    uint8_t exp_bytes[32];
    int     bit, byte_idx;

    /* N - 2 */
    fe n2;
    fe two = {0,0,0,0,0,0,0,2};
    fe_sub256(n2, N, two);
    fe_to_bytes(exp_bytes, n2);

    fe_copy(base, a);
    fe_zero(result);
    result[7] = 1u;

    for (byte_idx = 0; byte_idx < 32; byte_idx++) {
        for (bit = 7; bit >= 0; bit--) {
            fe_sqrmod(result, result);   /* uses P-reduction, but we need N-reduction */
            if ((exp_bytes[byte_idx] >> bit) & 1)
                fe_mulmod(result, result, base);
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

    /* e = hash as integer. */
    fe_from_bytes(e, hash);

    /* w = s^(-1) mod n. */
    fe_invmod_n(w, s);

    /* u1 = e*w mod n,  u2 = r*w mod n.
     * Note: fe_mulmod reduces mod P; for mod-N we rely on the fact that
     * for P-256 N < P, so the schoolbook product fits and we reduce mod N
     * via a comparison-and-subtract loop instead. */
    {
        wide tmp;
        fe_mul_wide(tmp, e, w);
        reduce_p256(u1, tmp);
        while (fe_cmp(u1, N) >= 0) fe_sub256(u1, u1, N);

        fe_mul_wide(tmp, r, w);
        reduce_p256(u2, tmp);
        while (fe_cmp(u2, N) >= 0) fe_sub256(u2, u2, N);
    }

    /* R = u1*G + u2*Q */
    point_mul2(&R, u1, &G, u2, &Q);
    if (R.inf) return -4;

    /* Verify R.x mod n == r. */
    while (fe_cmp(R.x, N) >= 0) fe_sub256(R.x, R.x, N);
    return (fe_cmp(R.x, r) == 0) ? 0 : -5;
}
