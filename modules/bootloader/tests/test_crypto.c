/* Host tests for the bootloader's crypto, against published vectors.
 *
 * These run on the build machine, not the PIC. Both files are portable C with
 * no device headers, and the arithmetic they implement is the part that has to
 * be right: a bootloader that verifies signatures incorrectly either bricks the
 * device or accepts an image it should have rejected, and neither shows up as a
 * crash.
 *
 * Vectors:
 *   SHA-256   FIPS 180-4 examples, plus the one-million-'a' value.
 *   ECDSA     RFC 6979 A.2.5 — P-256 with SHA-256, message "sample".
 *
 * Written so the RAM work in docs/known-issues.md can be attempted at all:
 * restructuring P-256's scratch space is not a change anyone should make
 * against a compiler error alone.
 */

#include <stdio.h>
#include <string.h>

#include "../bootloader/p256.h"
#include "../bootloader/sha256.h"

static int failures = 0;

static void check(const char *what, int ok)
{
    printf("  %s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

static int from_hex(const char *hex, uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned value;
        if (sscanf(hex + 2 * i, "%2x", &value) != 1) return -1;
        out[i] = (uint8_t)value;
    }
    return 0;
}

static int digest_is(const uint8_t digest[32], const char *expected_hex)
{
    uint8_t expected[32];
    if (from_hex(expected_hex, expected, 32) != 0) return 0;
    return memcmp(digest, expected, 32) == 0;
}

/* ---- SHA-256 ----------------------------------------------------------- */

static void test_sha256(void)
{
    uint8_t digest[32];

    sha256((const uint8_t *)"abc", 3, digest);
    check("SHA-256(\"abc\") matches FIPS 180-4",
          digest_is(digest, "ba7816bf8f01cfea414140de5dae2223"
                            "b00361a396177a9cb410ff61f20015ad"));

    sha256((const uint8_t *)"", 0, digest);
    check("SHA-256(\"\") matches the published value",
          digest_is(digest, "e3b0c44298fc1c149afbf4c8996fb924"
                            "27ae41e4649b934ca495991b7852b855"));

    /* Exercises the length-encoding path across a block boundary. */
    sha256((const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
           56, digest);
    check("SHA-256 of the 56-byte FIPS example",
          digest_is(digest, "248d6a61d20638b8e5c026930c3e6039"
                            "a33ce45964ff2167f6ecedd419db06c1"));

    /* Streaming in uneven chunks must equal the one-shot digest. */
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)"abcdbcdecdefdefgefghfghi", 24);
    sha256_update(&ctx, (const uint8_t *)"ghijhijkijkljklmklmnlmnomnopnopq", 32);
    sha256_final(&ctx, digest);
    check("streaming in uneven chunks equals the one-shot digest",
          digest_is(digest, "248d6a61d20638b8e5c026930c3e6039"
                            "a33ce45964ff2167f6ecedd419db06c1"));
}

/* ---- ECDSA P-256 ------------------------------------------------------- */

/* RFC 6979 A.2.5, key and signature for the message "sample". */
static const char *PUB_X = "60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6";
static const char *PUB_Y = "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299";
static const char *SIG_R = "EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716";
static const char *SIG_S = "F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8";

static void build_key(uint8_t pubkey[65])
{
    pubkey[0] = 0x04;
    from_hex(PUB_X, pubkey + 1, 32);
    from_hex(PUB_Y, pubkey + 33, 32);
}

static void test_p256(void)
{
    uint8_t pubkey[65], hash[32], r[32], s[32];

    build_key(pubkey);
    sha256((const uint8_t *)"sample", 6, hash);
    from_hex(SIG_R, r, 32);
    from_hex(SIG_S, s, 32);

    check("a valid RFC 6979 signature verifies",
          p256_verify(pubkey, hash, r, s) == 0);

    /* Everything below must be rejected. A verifier that accepts is worse than
       one that crashes, so each negative case is checked separately rather
       than as one loop. */
    uint8_t tampered[32];

    memcpy(tampered, hash, 32);
    tampered[0] ^= 0x01;
    check("a one-bit change in the digest is rejected",
          p256_verify(pubkey, tampered, r, s) != 0);

    memcpy(tampered, r, 32);
    tampered[31] ^= 0x01;
    check("a one-bit change in r is rejected",
          p256_verify(pubkey, hash, tampered, s) != 0);

    memcpy(tampered, s, 32);
    tampered[31] ^= 0x01;
    check("a one-bit change in s is rejected",
          p256_verify(pubkey, hash, r, tampered) != 0);

    uint8_t bad_key[65];
    memcpy(bad_key, pubkey, 65);
    bad_key[1] ^= 0x01;
    check("a public key not on the curve is rejected",
          p256_verify(bad_key, hash, r, s) != 0);

    memcpy(bad_key, pubkey, 65);
    bad_key[0] = 0x02;                       /* compressed-point marker */
    check("a key that is not an uncompressed point is rejected",
          p256_verify(bad_key, hash, r, s) != 0);

    uint8_t zero[32];
    memset(zero, 0, 32);
    check("r = 0 is rejected", p256_verify(pubkey, hash, zero, s) != 0);
    check("s = 0 is rejected", p256_verify(pubkey, hash, r, zero) != 0);

    /* A second message under the same key. One vector can pass by luck in a
       reduction that is only sometimes wrong; two, with different bit
       patterns through the scalar multiply, cannot. */
    uint8_t hash2[32], r2[32], s2[32];
    sha256((const uint8_t *)"test", 4, hash2);
    from_hex("F1ABB023518351CD71D881567B1EA663ED3EFCF6C5132B354F28D3B0B7D38367", r2, 32);
    from_hex("019F4113742A2B14BD25926B49C649155F267E60D3814B4C0CC84250E46F0083", s2, 32);
    check("the RFC 6979 \"test\" vector verifies under the same key",
          p256_verify(pubkey, hash2, r2, s2) == 0);

    check("the \"sample\" signature does not verify the \"test\" digest",
          p256_verify(pubkey, hash2, r, s) != 0);
}

int main(void)
{
    printf("\n  bootloader crypto, on the host\n\n");
    test_sha256();
    test_p256();
    printf("\n  %s\n\n", failures ? "FAILED" : "all vectors matched");
    return failures ? 1 : 0;
}
