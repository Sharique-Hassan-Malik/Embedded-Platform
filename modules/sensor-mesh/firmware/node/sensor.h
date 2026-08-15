#pragma once
#include <Arduino.h>
#include "packet.h"

// Reads all three onboard sensors and packs them into a DataPayload.
// Used only by sensor nodes (NODE_ID != 0).
namespace Sensor {

// Initialise pins. Call once from setup().
void begin();

// Take a full reading. Returns false if the DHT22 fails to respond.
// Partial results (light, motion) are always valid even if DHT22 fails.
bool read(DataPayload &out);

} // namespace Sensor
