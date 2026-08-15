#include "sensor.h"
#include "config.h"
#include <DHT.h>

static DHT dht(DHT_PIN, DHT22);

namespace Sensor {

void begin() {
    dht.begin();
    pinMode(PIR_PIN, INPUT);
    pinMode(LDR_PIN, INPUT);
}

bool read(DataPayload &out) {
    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();

    bool dht_ok = !isnan(temp) && !isnan(hum);

    out.temperature_x10 = dht_ok
        ? static_cast<int16_t>(temp * 10.0f + 0.5f)
        : -999;

    out.humidity_x10    = dht_ok
        ? static_cast<uint16_t>(hum * 10.0f + 0.5f)
        : 0xFFFF;

    out.light_adc  = static_cast<uint16_t>(analogRead(LDR_PIN));
    out.motion     = static_cast<uint8_t>(digitalRead(PIR_PIN) == HIGH ? 1 : 0);
    out.battery_pct = 0xFF;  // not measured on this node

    return dht_ok;
}

} // namespace Sensor
