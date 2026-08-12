#pragma once
#include <Arduino.h>

// INA219 bidirectional current/power monitor driver.
// Implements the configuration register programming and shunt voltage
// register read from scratch using Wire (I2C transport only).
//
// INA219 register map (datasheet section 8.6):
//   0x00  Configuration  R/W  16-bit
//   0x01  Shunt Voltage  R    16-bit signed, 1 LSB = 10 µV
//   0x02  Bus Voltage    R    16-bit, 1 LSB = 4 mV
//   0x03  Power          R    16-bit (requires calibration register)
//   0x04  Current        R    16-bit signed (requires calibration register)
//   0x05  Calibration    R/W  16-bit

class INA219 {
public:
    INA219() = default;

    // Initialise and calibrate. Returns false if the device does not respond.
    bool begin(uint8_t addr, uint16_t shunt_mohm, uint8_t pg, uint8_t badc, uint8_t sadc);

    // Read shunt voltage in µV. Raw register × 10.
    int32_t shuntVoltage_uV() const;

    // Derive current in 0.1 mA units from shunt voltage and shunt resistance.
    // current_0p1mA = shunt_uV * 10 / shunt_mohm
    int32_t current_0p1mA() const;

    // Read bus voltage in mV.
    uint16_t busVoltage_mV() const;

private:
    uint8_t  _addr       = 0;
    uint16_t _shunt_mohm = 100;

    uint16_t _readReg(uint8_t reg) const;
    void     _writeReg(uint8_t reg, uint16_t val) const;
};
