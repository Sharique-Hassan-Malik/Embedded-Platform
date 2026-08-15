#ifndef BOOT_SHARED_H
#define BOOT_SHARED_H

/*
 * boot_shared.h — Shared memory map and protocol constants.
 *
 * Included by both the bootloader project and the application project.
 * Do not include any XC8-specific or hardware headers here.
 *
 * PIC18F4550 flash layout (32 KB = 0x0000–0x7FFF):
 *
 *   0x0000–0x07FF  Bootloader code (2 KB)
 *   0x0800–0x7BFF  Application code (29.5 KB)
 *   0x7C00–0x7DFF  Firmware metadata block (512 B, one erase page)
 *   0x7E00–0x7FFF  Bootloader config page (512 B — public key + write protect)
 *
 * The high interrupt vector lives at 0x0008 for the bootloader; the
 * application's vectors are remapped by the linker to 0x0808/0x0818.
 *
 * The metadata block at 0x7C00 is written by the signing tool and read
 * by the bootloader before accepting the application image:
 *
 *   Offset  0 : uint32_t  app_start        (always 0x0800)
 *   Offset  4 : uint32_t  app_size         (bytes of application code)
 *   Offset  8 : uint8_t   sig_r[32]        (ECDSA-P256 signature r component)
 *   Offset 40 : uint8_t   sig_s[32]        (ECDSA-P256 signature s component)
 *   Offset 72 : uint8_t   sha256[32]       (SHA-256 digest of app image)
 *   Offset104 : uint8_t   version[16]      (null-terminated version string)
 *   Offset120 : uint8_t   reserved[392]    (zero-padded to 512 bytes)
 *
 * UART protocol (31 250 baud, same baud rate as MIDI for hardware reuse):
 *
 *   Host → device: firmware image as 128-byte Intel HEX records.
 *   Device → host: single-byte status codes after each record or at end.
 *
 *   Status codes:
 *     ACK  0x06  record accepted, ready for next
 *     NAK  0x15  record rejected (bad checksum or out of range)
 *     ERR  0x45  fatal error (verification failed, abort)
 *     OK   0x4F  application verified and booted
 */

/* ---- Flash map ----------------------------------------------------------- */
#define BOOT_START          0x0000UL
#define BOOT_END            0x07FFUL
#define APP_START           0x0800UL
/* Suffix-free form of APP_START for inline-assembly GOTO targets — the PIC
   assembler rejects the UL suffix. Keep this equal to APP_START. */
#define APP_START_ASM        0x0800
#define APP_END             0x7BFFUL
#define APP_MAX_SIZE        ((APP_END - APP_START) + 1UL)   /* 0x7400 = 29 696 */
#define META_START          0x7C00UL
#define KEY_PAGE_START      0x7E00UL

/* ---- Metadata field offsets (from META_START) ---------------------------- */
#define META_APP_START_OFF  0u
#define META_APP_SIZE_OFF   4u
#define META_SIG_R_OFF      8u
#define META_SIG_S_OFF      40u
#define META_SHA256_OFF     72u
#define META_VERSION_OFF    104u
#define META_SIG_SIZE       32u
#define META_HASH_SIZE      32u

/* ---- Public key storage (at KEY_PAGE_START) ------------------------------ */
/* Uncompressed P-256 public key: 0x04 || X[32] || Y[32] = 65 bytes. */
#define PUBKEY_OFFSET       0u
#define PUBKEY_LEN          65u

/* ---- UART protocol ------------------------------------------------------- */
#define UART_ACK   0x06u
#define UART_NAK   0x15u
#define UART_ERR   0x45u
#define UART_OK    0x4Fu

/* ---- IHEX record types --------------------------------------------------- */
#define IHEX_DATA          0x00u
#define IHEX_EOF           0x01u
#define IHEX_EXT_ADDR      0x04u   /* extended linear address record */

#define IHEX_MAX_DATA_LEN  128u
#define IHEX_BUF_LEN       (IHEX_MAX_DATA_LEN + 6u)  /* :LL AAAA TT [data] CC */

#endif /* BOOT_SHARED_H */
