#ifndef P256_H
#define P256_H

#include <stdint.h>

/*
 * ECDSA-P256 signature verification.
 *
 * Implements point multiplication on the NIST P-256 curve (secp256r1)
 * from scratch using 32-bit limb arithmetic.  No external library is used.
 *
 * The only public entry point the bootloader needs is p256_verify().
 * All internal helpers (field arithmetic, point operations) are declared
 * static in p256.c and are not exposed here.
 *
 * Key and signature representation:
 *   All byte arrays are big-endian (network byte order), 32 bytes each
 *   for field elements and 65 bytes for the uncompressed public key
 *   (0x04 prefix || X[32] || Y[32]).
 *
 * Security note:
 *   This implementation is NOT constant-time.  It is suitable for use
 *   during a bootloader where the adversary does not have timing access
 *   to the MCU during the verification window.  For deployments where
 *   an attacker can repeatedly trigger verification and measure timing
 *   (e.g. a debug port exposed over a network) a constant-time
 *   implementation should replace this one.
 */

/*
 * Verify an ECDSA-P256 signature.
 *
 *   pubkey    : 65-byte uncompressed public key (0x04 || X[32] || Y[32])
 *   hash      : 32-byte SHA-256 digest of the signed message
 *   sig_r     : 32-byte signature r component (big-endian)
 *   sig_s     : 32-byte signature s component (big-endian)
 *
 * Returns 0 if the signature is valid, non-zero otherwise.
 */
int p256_verify(const uint8_t pubkey[65],
                const uint8_t hash[32],
                const uint8_t sig_r[32],
                const uint8_t sig_s[32]);

#endif /* P256_H */
