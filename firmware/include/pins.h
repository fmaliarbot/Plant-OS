#pragma once
// Plant OS V0.1 pin map (ESP32-DevKitC-32E).
// Single source of truth: must match the wiring diagram in docs/.

#include <stdint.h>

namespace pins {

// Outputs to the MOSFET drivers (Adafruit 5648, "In" pin on the JST PH connector).
// Not ESP32 strapping pins. Each one has an external 10 kOhm pull-down to GND.
constexpr uint8_t PUMP_BASIL    = 25;
constexpr uint8_t PUMP_ROSEMARY = 26;

// Analog inputs on ADC1 (keeps working while Wi-Fi is active; ADC2 does not).
// GPIO 34 and 35 are input-only and have no internal pull-up/pull-down.
constexpr uint8_t SOIL_BASIL    = 34;
constexpr uint8_t SOIL_ROSEMARY = 35;

// I2C bus (ESP32 Wire defaults).
constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;

// Reserved for Milestone 3 (tank level sensor). Do not connect yet.
constexpr uint8_t TANK_LEVEL = 27;

}  // namespace pins
