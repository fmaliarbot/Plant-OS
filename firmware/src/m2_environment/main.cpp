// Plant OS — MILESTONE 2: environment sensors (Adafruit BME280 + Adafruit BH1750, I2C bus)
//
// What it does:
//   - Forces both pumps LOW at boot.
//   - Scans the I2C bus and reports responding addresses (expected: 0x23 BH1750, 0x77 BME280).
//   - Every 5 s prints CSV: time, temperature, relative humidity, pressure, lux and per-sensor status.
//   - If a sensor stops responding it is flagged and re-initialized; the loop never blocks.
//
// Commands: h help | i rescan I2C | p pause

#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <BH1750.h>
#include <Wire.h>
#include <math.h>

#include "pins.h"
#include "pump_safety.h"

namespace {

constexpr uint32_t kReadPeriodMs = 5000;
constexpr uint8_t kBmeAddrPrimary = 0x77;  // Adafruit BME280 default address
constexpr uint8_t kBmeAddrAlt = 0x76;
constexpr uint8_t kLuxAddrPrimary = 0x23;  // Adafruit BH1750 default address (ADDR to GND)
constexpr uint8_t kLuxAddrAlt = 0x5C;

Adafruit_BME280 bme;
BH1750 lux_meter;
bool bme_ok = false;
bool lux_ok = false;
uint8_t bme_addr = 0;
uint8_t lux_addr = 0;
bool streaming = true;
uint32_t last_read_ms = 0;
String line_buf;

bool i2c_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void scan_i2c() {
  Serial.print(F("# I2C scan:"));
  int found = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    if (i2c_present(a)) {
      Serial.printf(" 0x%02X", a);
      ++found;
    }
  }
  Serial.printf("  (%d device(s))\n", found);
  if (found == 0) {
    Serial.println(F("# WARNING: bus is empty. Check SDA=GPIO21, SCL=GPIO22, 3V3 and GND on the STEMMA QT cable."));
  }
}

bool init_bme() {
  for (uint8_t a : {kBmeAddrPrimary, kBmeAddrAlt}) {
    if (i2c_present(a) && bme.begin(a, &Wire)) {
      bme_addr = a;
      return true;
    }
  }
  return false;
}

bool init_lux() {
  for (uint8_t a : {kLuxAddrPrimary, kLuxAddrAlt}) {
    if (i2c_present(a) && lux_meter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, a, &Wire)) {
      lux_addr = a;
      return true;
    }
  }
  return false;
}

bool in_range(float v, float lo, float hi) { return !isnan(v) && v >= lo && v <= hi; }

void print_header() {
  Serial.println(F("t_ms,temp_c,rh_pct,pressure_hpa,lux,bme_status,lux_status"));
}

void handle_command(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.isEmpty()) return;
  if (cmd == "h") {
    Serial.println(F("# Commands: h help | i scan I2C | p pause"));
  } else if (cmd == "i") {
    scan_i2c();
  } else if (cmd == "p") {
    streaming = !streaming;
    Serial.println(streaming ? F("# Streaming resumed.") : F("# Streaming paused."));
    if (streaming) print_header();
  } else {
    Serial.printf("# Unknown command: '%s'\n", cmd.c_str());
  }
}

void poll_serial() {
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      handle_command(line_buf);
      line_buf = "";
    } else if (line_buf.length() < 32) {
      line_buf += c;
    }
  }
}

}  // namespace

void setup() {
  pumps_force_off();  // always the first line

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("# Plant OS V0.1 - Milestone 2: environment"));

  Wire.begin(pins::I2C_SDA, pins::I2C_SCL);
  Wire.setClock(100000);
  scan_i2c();

  bme_ok = init_bme();
  lux_ok = init_lux();
  Serial.printf("# BME280: %s", bme_ok ? "OK" : "NOT_FOUND");
  if (bme_ok) Serial.printf(" (0x%02X, chip id 0x%02X; 0x60 = genuine BME280)", bme_addr, bme.sensorID());
  Serial.println();
  Serial.printf("# BH1750: %s", lux_ok ? "OK" : "NOT_FOUND");
  if (lux_ok) Serial.printf(" (0x%02X)", lux_addr);
  Serial.println();
  print_header();
}

void loop() {
  poll_serial();

  const uint32_t now = millis();
  if (now - last_read_ms < kReadPeriodMs) return;
  last_read_ms = now;

  if (!bme_ok) bme_ok = init_bme();
  if (!lux_ok) lux_ok = init_lux();

  float t = NAN, h = NAN, p = NAN, lx = NAN;
  const char* st_bme = "NOT_FOUND";
  const char* st_lux = "NOT_FOUND";

  if (bme_ok) {
    t = bme.readTemperature();
    h = bme.readHumidity();
    p = bme.readPressure() / 100.0f;
    // Indoor plausibility ranges (not sensor limits).
    const bool ok = in_range(t, -10, 50) && in_range(h, 0, 100) && in_range(p, 800, 1100);
    st_bme = ok ? "OK" : "OUT_OF_RANGE";
    if (isnan(t)) {
      st_bme = "READ_ERROR";
      bme_ok = false;  // retried on the next cycle
    }
  }

  if (lux_ok) {
    lx = lux_meter.readLightLevel();  // negative = library error
    if (lx < 0) {
      st_lux = "READ_ERROR";
      lux_ok = false;
      lx = NAN;
    } else {
      st_lux = in_range(lx, 0, 65535) ? "OK" : "OUT_OF_RANGE";
    }
  }

  if (streaming) {
    Serial.printf("%lu,%.2f,%.1f,%.1f,%.1f,%s,%s\n", static_cast<unsigned long>(now), t, h, p, lx,
                  st_bme, st_lux);
  }
}
