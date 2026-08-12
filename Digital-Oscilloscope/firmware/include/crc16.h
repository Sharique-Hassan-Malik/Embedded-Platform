#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <stddef.h>

/*
 * CRC-16/CCITT-FALSE
 *   Poly:  0x1021
 *   Init:  0xFFFF
 *   RefIn: false
 *   RefOut:false
 *   XorOut:0x0000
 */
static inline uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

static inline uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) crc = crc16_update(crc, data[i]);
    return crc;
}

#endif /* CRC16_H */
