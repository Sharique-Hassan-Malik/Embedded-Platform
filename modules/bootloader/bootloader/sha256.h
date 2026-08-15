#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
    uint8_t  buf_len;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint16_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[32]);

/* Single-call convenience wrapper. */
void sha256(const uint8_t *data, uint16_t len, uint8_t digest[32]);

#endif /* SHA256_H */
