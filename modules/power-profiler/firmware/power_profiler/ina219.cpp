#include "ina219.h"
#include "config.h"
#include <Wire.h>

// ── INA219 configuration register bit fields (datasheet Table 1) ──────────────
// [15:13]  RST | — | —    (reset bit, reserved)
// [12]     BRNG  bus voltage range: 0 = 16 V, 1 = 32 V
// [11:10]  PG    shunt PGA gain/range
// [9:6]    BADC  bus ADC resolution/averaging
// [5:2]    SADC  shunt ADC resolution/averaging
// [1:0]    MODE  operating mode: 7 = continuous shunt and bus

static uint16_t buildConfigReg(uint8_t pg, uint8_t badc, uint8_t sadc) {
    uint16_t cfg = 0;
    cfg |= (1 << 13);                       // BRNG = 1: 32 V bus range
    cfg |= ((pg   & 0x03) << 11);           // PGA gain
    cfg |= ((badc & 0x0F) << 7);            // bus ADC
    cfg |= ((sadc & 0x0F) << 3);            // shunt ADC
    cfg |= 0x07;                            // mode: continuous shunt + bus
    return cfg;
}

uint16_t INA219::_readReg(uint8_t reg) const {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(static_cast<int>(_addr), 2);
    uint16_t hi = Wire.read();
    uint16_t lo = Wire.read();
    return static_cast<uint16_t>((hi << 8) | lo);
}

void INA219::_writeReg(uint8_t reg, uint16_t val) const {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.write(static_cast<uint8_t>(val >> 8));
    Wire.write(static_cast<uint8_t>(val & 0xFF));
    Wire.endTransmission();
}

bool INA219::begin(uint8_t addr, uint16_t shunt_mohm,
                   uint8_t pg, uint8_t badc, uint8_t sadc) {
    _addr       = addr;
    _shunt_mohm = shunt_mohm;

    Wire.begin();
    Wire.setClock(400000);

    // Soft-reset via config register RST bit.
    _writeReg(0x00, 0x8000);
    delay(1);

    // Probe: after reset the config register should read the default (0x399F).
    uint16_t def = _readReg(0x00);
    if (def != 0x399F) return false;

    // Write operating configuration.
    _writeReg(0x00, buildConfigReg(pg, badc, sadc));

    // Calibration register: not used for raw shunt-voltage current derivation,
    // but set to a safe value so the Current register does not produce garbage
    // if read accidentally.
    _writeReg(0x05, 0x0000);

    return true;
}

int32_t INA219::shuntVoltage_uV() const {
    // Register 0x01: 16-bit signed, 1 LSB = 10 µV.
    int16_t raw = static_cast<int16_t>(_readReg(0x01));
    return static_cast<int32_t>(raw) * 10;
}

int32_t INA219::current_0p1mA() const {
    // I (mA) = V_shunt (µV) / R_shunt (mΩ) × 10⁻³
    // I (0.1 mA) = V_shunt (µV) × 10 / R_shunt (mΩ)
    int32_t v_uv = shuntVoltage_uV();
    return (v_uv * 10L) / static_cast<int32_t>(_shunt_mohm);
}

uint16_t INA219::busVoltage_mV() const {
    // Register 0x02: bits [15:3] are the voltage; 1 LSB = 4 mV.
    uint16_t raw = _readReg(0x02) >> 3;
    return static_cast<uint16_t>(raw * 4);
}
