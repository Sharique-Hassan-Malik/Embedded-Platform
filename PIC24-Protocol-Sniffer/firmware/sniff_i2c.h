#ifndef SNIFF_I2C_H
#define SNIFF_I2C_H

/*
 * Passive I2C sniffer using change-notification interrupts on RB8 (SCL)
 * and RB9 (SDA).
 *
 * Captured events pushed to the shared circular buffer (hw.h):
 *   START condition  → FLAG_I2C_START word (data byte = 0)
 *   STOP  condition  → FLAG_I2C_STOP  word (data byte = 0)
 *   Address byte     → FLAG_I2C_ADDR  word (data byte = 7-bit addr | R/W)
 *   Data byte + ACK  → FLAG_I2C_ACK   word (data byte = received byte)
 *   Data byte + NACK → FLAG_I2C_NACK  word (data byte = received byte)
 */

void sniff_i2c_init(void);

#endif /* SNIFF_I2C_H */
