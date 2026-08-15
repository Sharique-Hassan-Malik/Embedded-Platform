#include "sha256.h"

#include <stdint.h>
#include <string.h>

/* ---- Round constants ----------------------------------------------------- */
static const uint32_t K[64] = {
    0x428A2F98UL, 0x71374491UL, 0xB5C0FBCFUL, 0xE9B5DBA5UL,
    0x3956C25BUL, 0x59F111F1UL, 0x923F82A4UL, 0xAB1C5ED5UL,
    0xD807AA98UL, 0x12835B01UL, 0x243185BEUL, 0x550C7DC3UL,
    0x72BE5D74UL, 0x80DEB1FEUL, 0x9BDC06A7UL, 0xC19BF174UL,
    0xE49B69C1UL, 0xEFBE4786UL, 0x0FC19DC6UL, 0x240CA1CCUL,
    0x2DE92C6FUL, 0x4A7484AAUL, 0x5CB0A9DCUL, 0x76F988DAUL,
    0x983E5152UL, 0xA831C66DUL, 0xB00327C8UL, 0xBF597FC7UL,
    0xC6E00BF3UL, 0xD5A79147UL, 0x06CA6351UL, 0x14292967UL,
    0x27B70A85UL, 0x2E1B2138UL, 0x4D2C6DFCUL, 0x53380D13UL,
    0x650A7354UL, 0x766A0ABBUL, 0x81C2C92EUL, 0x92722C85UL,
    0xA2BFE8A1UL, 0xA81A664BUL, 0xC24B8B70UL, 0xC76C51A3UL,
    0xD192E819UL, 0xD6990624UL, 0xF40E3585UL, 0x106AA070UL,
    0x19A4C116UL, 0x1E376C08UL, 0x2748774CUL, 0x34B0BCB5UL,
    0x391C0CB3UL, 0x4ED8AA4AUL, 0x5B9CCA4FUL, 0x682E6FF3UL,
    0x748F82EEUL, 0x78A5636FUL, 0x84C87814UL, 0x8CC70208UL,
    0x90BEFFFAUL, 0xA4506CEBUL, 0xBEF9A3F7UL, 0xC67178F2UL,
};

/* ---- Bit rotation -------------------------------------------------------- */
#define ROTR32(x, n)  (((x) >> (n)) | ((x) << (32u - (n))))

/* ---- SHA-256 compression function --------------------------------------- */
static void sha256_compress(sha256_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t T1, T2;
    uint8_t  i;

    /* Message schedule. */
    for (i = 0u; i < 16u; i++) {
        W[i]  = ((uint32_t)block[i*4u + 0u] << 24)
              | ((uint32_t)block[i*4u + 1u] << 16)
              | ((uint32_t)block[i*4u + 2u] <<  8)
              |  (uint32_t)block[i*4u + 3u];
    }
    for (i = 16u; i < 64u; i++) {
        uint32_t s0 = ROTR32(W[i-15u],  7u) ^ ROTR32(W[i-15u], 18u) ^ (W[i-15u] >>  3u);
        uint32_t s1 = ROTR32(W[i- 2u], 17u) ^ ROTR32(W[i- 2u], 19u) ^ (W[i- 2u] >> 10u);
        W[i] = W[i-16u] + s0 + W[i-7u] + s1;
    }

    /* Working variables. */
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0u; i < 64u; i++) {
        uint32_t S1  = ROTR32(e, 6u) ^ ROTR32(e, 11u) ^ ROTR32(e, 25u);
        uint32_t ch  = (e & f) ^ (~e & g);
        uint32_t S0  = ROTR32(a, 2u) ^ ROTR32(a, 13u) ^ ROTR32(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        T1 = h + S1 + ch + K[i] + W[i];
        T2 = S0 + maj;
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

/* ---- Public API ---------------------------------------------------------- */

void sha256_init(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6A09E667UL; ctx->state[1] = 0xBB67AE85UL;
    ctx->state[2] = 0x3C6EF372UL; ctx->state[3] = 0xA54FF53AUL;
    ctx->state[4] = 0x510E527FUL; ctx->state[5] = 0x9B05688CUL;
    ctx->state[6] = 0x1F83D9ABUL; ctx->state[7] = 0x5BE0CD19UL;
    ctx->count   = 0u;
    ctx->buf_len = 0u;
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for (i = 0u; i < len; i++) {
        ctx->buf[ctx->buf_len++] = data[i];
        ctx->count++;
        if (ctx->buf_len == 64u) {
            sha256_compress(ctx, ctx->buf);
            ctx->buf_len = 0u;
        }
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t digest[32])
{
    uint8_t  pad[64];
    uint64_t bit_count;
    uint8_t  pad_len;
    uint8_t  i;

    bit_count = ctx->count * 8u;

    /* Padding: 0x80, then zeros, then 8-byte big-endian bit count. */
    pad_len = (uint8_t)((55u - (ctx->count & 63u)) & 63u) + 1u;
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80u;

    /* Append bit count in last 8 bytes of padding. */
    for (i = 0u; i < 8u; i++)
        pad[pad_len + i] = (uint8_t)(bit_count >> (56u - 8u * i));

    sha256_update(ctx, pad, pad_len + 8u);

    /* Serialise state to digest (big-endian). */
    for (i = 0u; i < 8u; i++) {
        digest[i*4u + 0u] = (uint8_t)(ctx->state[i] >> 24);
        digest[i*4u + 1u] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4u + 2u] = (uint8_t)(ctx->state[i] >>  8);
        digest[i*4u + 3u] = (uint8_t)(ctx->state[i]);
    }
}

void sha256(const uint8_t *data, uint16_t len, uint8_t digest[32])
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}
