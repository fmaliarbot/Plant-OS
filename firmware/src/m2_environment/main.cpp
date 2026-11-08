// Plant OS — MILESTONE 2: environment sensors (Adafruit BME280 + Adafruit BH1750, I2C bus)
//
// What it does:
//   - Forces both pumps LOW at boot.
//   - Scans the I2C bus and reports responding addresses (expected: 0x23 BH1750, 0x77 BME280).
//   - Reads the BME280 chip id register directly, so a BMP280 sold as "BME280" is reported as
//     WRONG_CHIP instead of a silent NOT_FOUND (the Adafruit library rejects it without saying why).
//   - Runs the BME280 in FORCED mode with x1 oversampling (Bosch "weather monitoring" setup):
//     the sensor sleeps between reads, so it does not warm itself and bias the temperature.
//   - Every 5 s prints CSV: time, temperature, relative humidity, pressure, lux, per-sensor status.
//   - Reports LOST / RECOVERED events when a sensor is unplugged and replugged; never blocks.
//
// Commands:
//   h help | i rescan I2C | p pause CSV
//   s 60 s climate stability test (leave the BME280 untouched)
//   l guided light test (baseline -> cover with hand -> flashlight)
//   r link report (outages / recoveries since boot)
//   x abort a running test

#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <BH1750.h>
#include <Wire.h>
#include <math.h>

#include "environment.h"
#include "pins.h"
#include "plantcore.h"
#include "pump_safety.h"

using namespace plantos;

namespace {

constexpr uint32_t kReadPeriodMs = 5000;
constexpr uint8_t kBmeAddrs[] = {0x77, 0x76};  // Adafruit default first
constexpr uint8_t kLuxAddrs[] = {0x23, 0x5C};  // ADDR pin low (Adafruit default) / high
constexpr uint8_t kBmeChipIdReg = 0xD0;

constexpr uint32_t kStabilityTestMs = 60000;

// Light test phases, in ms.
constexpr uint32_t kLightSampleMs = 250;
constexpr uint32_t kLightBaselineMs = 5000;
constexpr uint32_t kLightPrepMs = 3000;     // time to get the hand / flashlight in place
constexpr uint32_t kLightMeasureMs = 8000;

Adafruit_BME280 bme;
BH1750 lux_meter;

EnvStatus bme_init_status = EnvStatus::NOT_FOUND;
bool bme_ok = false;
bool lux_ok = false;
uint8_t bme_addr = 0;
uint8_t lux_addr = 0;
uint8_t last_reported_wrong_id = 0;

LinkMonitor bme_link;
LinkMonitor lux_link;

bool streaming = true;
uint32_t last_read_ms = 0;
String line_buf;

// ---- stability test state ----
bool stab_running = false;
uint32_t stab_start_ms = 0;
RunningStats stab_temp;
RunningStats stab_rh;

// ---- light test state ----
enum class LightPhase : uint8_t { IDLE, BASELINE, COVER_PREP, COVER, LIT_PREP, LIT };
LightPhase light_phase = LightPhase::IDLE;
uint32_t light_phase_start_ms = 0;
uint32_t light_last_sample_ms = 0;
RunningStats light_baseline;
float light_covered_min = NAN;
float light_lit_max = NAN;

// ---------- I2C helpers ----------

bool i2c_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Reads one register. Returns false on any I2C error.
bool i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1)) != 1) return false;
  value = static_cast<uint8_t>(Wire.read());
  return true;
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

// ---------- Sensor init ----------

EnvStatus init_bme() {
  bool any_present = false;
  for (uint8_t a : kBmeAddrs) {
    if (!i2c_present(a)) continue;
    any_present = true;
    uint8_t id = 0;
    if (!i2c_read_reg(a, kBmeChipIdReg, id)) continue;
    const BmeChip chip = identify_bme_chip(id);
    if (chip != BmeChip::BME280) {
      if (id != last_reported_wrong_id) {
        Serial.printf("# BME280: device at 0x%02X has chip id 0x%02X (%s), not 0x60.%s\n", a, id,
                      bme_chip_name(chip),
                      chip == BmeChip::BMP280 ? " It has NO humidity sensor: replace the board." : "");
        last_reported_wrong_id = id;
      }
      return EnvStatus::WRONG_CHIP;
    }
    if (!bme.begin(a, &Wire)) continue;
    // Forced mode: one conversion per takeForcedMeasurement(), sleep in between.
    bme.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_OFF);
    bme_addr = a;
    return EnvStatus::OK;
  }
  return any_present ? EnvStatus::READ_ERROR : EnvStatus::NOT_FOUND;
}

bool init_lux() {
  for (uint8_t a : kLuxAddrs) {
    if (i2c_present(a) && lux_meter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, a, &Wire)) {
      lux_addr = a;
      return true;
    }
  }
  return false;
}

// ---------- Output helpers ----------

void print_header() {
  Serial.println(F("t_ms,temp_c,rh_pct,pressure_hpa,lux,bme_status,lux_status"));
}

void print_help() {
  Serial.println(F("# Commands: h help | i scan I2C | p pause CSV | s stability test (60 s)"));
  Serial.println(F("#           l light test | r link report | x abort test"));
}

void print_link_report() {
  Serial.printf("# LINK BME280: %s, outages=%lu, recoveries=%lu, last outage=%lu ms\n",
                bme_link.up() ? "UP" : "DOWN", static_cast<unsigned long>(bme_link.outages()),
                static_cast<unsigned long>(bme_link.recoveries()),
                static_cast<unsigned long>(bme_link.last_outage_ms()));
  Serial.printf("# LINK BH1750: %s, outages=%lu, recoveries=%lu, last outage=%lu ms\n",
                lux_link.up() ? "UP" : "DOWN", static_cast<unsigned long>(lux_link.outages()),
                static_cast<unsigned long>(lux_link.recoveries()),
                static_cast<unsigned long>(lux_link.last_outage_ms()));
  const bool pass = bme_link.recoveries() > 0 && lux_link.recoveries() > 0 && bme_link.up() && lux_link.up();
  Serial.printf("# RECONNECT TEST: %s\n",
                pass ? "PASS (both sensors lost and recovered)"
                     : "PENDING (unplug the STEMMA QT cable for ~10 s, plug it back, then press r)");
}

void report_link_event(const char* name, LinkMonitor::Event ev, const LinkMonitor& m) {
  if (ev == LinkMonitor::Event::LOST) {
    Serial.printf("# EVENT: %s LOST\n", name);
  } else if (ev == LinkMonitor::Event::RECOVERED) {
    Serial.printf("# EVENT: %s RECOVERED after %lu s\n", name,
                  static_cast<unsigned long>(m.last_outage_ms() / 1000));
  }
}

// ---------- Stability test ----------

void start_stability_test(uint32_t now) {
  if (!bme_ok) {
    Serial.println(F("# STABILITY: BME280 not available, test not started."));
    return;
  }
  stab_temp.reset();
  stab_rh.reset();
  stab_start_ms = now;
  stab_running = true;
  Serial.println(F("# STABILITY: started. Do not touch or breathe on the BME280 for 60 s."));
}

void finish_stability_test() {
  stab_running = false;
  const ClimateStabilityResult r = evaluate_climate_stability(stab_temp, stab_rh);
  Serial.printf("# STABILITY: n=%u temp mean=%.2f C sd=%.3f [%s] | rh mean=%.1f %% sd=%.2f [%s]\n",
                static_cast<unsigned>(stab_temp.count()), stab_temp.mean(), stab_temp.stddev(),
                r.temp_stable ? "ok" : "HIGH", stab_rh.mean(), stab_rh.stddev(),
                r.rh_stable ? "ok" : "HIGH");
  if (!r.enough_samples) {
    Serial.println(F("# STABILITY: INCONCLUSIVE (too few valid samples; check the sensor link)"));
  } else {
    Serial.printf("# STABILITY: %s\n", r.pass() ? "PASS" : "FAIL");
  }
}

// ---------- Light test (non-blocking state machine) ----------

void enter_light_phase(LightPhase ph, uint32_t now) {
  light_phase = ph;
  light_phase_start_ms = now;
  switch (ph) {
    case LightPhase::BASELINE:
      Serial.println(F("# LIGHT 1/3: measuring ambient light for 5 s. Do not cover the sensor."));
      break;
    case LightPhase::COVER_PREP:
      Serial.println(F("# LIGHT 2/3: COVER the BH1750 completely with your hand now..."));
      break;
    case LightPhase::COVER:
      Serial.println(F("# LIGHT 2/3: measuring (keep it covered, 8 s)"));
      break;
    case LightPhase::LIT_PREP:
      Serial.println(F("# LIGHT 3/3: remove your hand and POINT a phone flashlight at the sensor (~10 cm)..."));
      break;
    case LightPhase::LIT:
      Serial.println(F("# LIGHT 3/3: measuring (keep the flashlight on it, 8 s)"));
      break;
    case LightPhase::IDLE:
      break;
  }
}

void start_light_test(uint32_t now) {
  if (!lux_ok) {
    Serial.println(F("# LIGHT: BH1750 not available, test not started."));
    return;
  }
  light_baseline.reset();
  light_covered_min = NAN;
  light_lit_max = NAN;
  light_last_sample_ms = 0;
  enter_light_phase(LightPhase::BASELINE, now);
}

void finish_light_test() {
  light_phase = LightPhase::IDLE;
  const float baseline = light_baseline.count() ? static_cast<float>(light_baseline.mean()) : NAN;
  const LightTestResult r = evaluate_light_test(baseline, light_covered_min, light_lit_max);
  Serial.printf("# LIGHT: baseline=%.1f lx, covered min=%.1f lx [%s], flashlight max=%.1f lx [%s]\n",
                baseline, light_covered_min, r.covered_ok ? "ok" : "no", light_lit_max,
                r.lit_ok ? "ok" : "no");
  Serial.printf("# LIGHT: %s\n", light_verdict_name(r.verdict));
  if (r.verdict == LightVerdict::INCONCLUSIVE) {
    Serial.println(F("# LIGHT: room too dark (or a read failed). Turn on a lamp and run l again."));
  }
}

void abort_tests() {
  const bool any = stab_running || light_phase != LightPhase::IDLE;
  stab_running = false;
  light_phase = LightPhase::IDLE;
  Serial.println(any ? F("# Test aborted.") : F("# No test running."));
}

void run_light_test(uint32_t now) {
  if (light_phase == LightPhase::IDLE) return;
  if (now - light_last_sample_ms < kLightSampleMs) return;
  light_last_sample_ms = now;

  const float lx = lux_meter.readLightLevel();
  if (classify_lux(lx) == EnvStatus::READ_ERROR) {
    Serial.println(F("# LIGHT: read error, test aborted."));
    light_phase = LightPhase::IDLE;
    return;
  }

  const uint32_t elapsed = now - light_phase_start_ms;
  switch (light_phase) {
    case LightPhase::BASELINE:
      light_baseline.add(lx);
      if (elapsed >= kLightBaselineMs) enter_light_phase(LightPhase::COVER_PREP, now);
      break;
    case LightPhase::COVER_PREP:
      if (elapsed >= kLightPrepMs) enter_light_phase(LightPhase::COVER, now);
      break;
    case LightPhase::COVER:
      if (isnan(light_covered_min) || lx < light_covered_min) light_covered_min = lx;
      if (elapsed >= kLightMeasureMs) enter_light_phase(LightPhase::LIT_PREP, now);
      break;
    case LightPhase::LIT_PREP:
      if (elapsed >= kLightPrepMs) enter_light_phase(LightPhase::LIT, now);
      break;
    case LightPhase::LIT:
      if (isnan(light_lit_max) || lx > light_lit_max) light_lit_max = lx;
      if (elapsed >= kLightMeasureMs) finish_light_test();
      break;
    case LightPhase::IDLE:
      break;
  }
}

// ---------- Serial commands ----------

void handle_command(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.isEmpty()) return;
  const uint32_t now = millis();
  const bool busy = stab_running || light_phase != LightPhase::IDLE;
  if (cmd == "h") {
    print_help();
  } else if (cmd == "i") {
    scan_i2c();
  } else if (cmd == "p") {
    streaming = !streaming;
    Serial.println(streaming ? F("# Streaming resumed.") : F("# Streaming paused."));
    if (streaming) print_header();
  } else if (cmd == "s") {
    if (busy) Serial.println(F("# A test is already running (x to abort)."));
    else start_stability_test(now);
  } else if (cmd == "l") {
    if (busy) Serial.println(F("# A test is already running (x to abort)."));
    else start_light_test(now);
  } else if (cmd == "r") {
    print_link_report();
  } else if (cmd == "x") {
    abort_tests();
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

// ---------- Periodic read ----------

void periodic_read(uint32_t now) {
  if (!bme_ok) {
    bme_init_status = init_bme();
    bme_ok = bme_init_status == EnvStatus::OK;
  }
  if (!lux_ok) lux_ok = init_lux();

  float t = NAN, h = NAN, p = NAN, lx = NAN;
  EnvStatus st_bme = bme_init_status;
  EnvStatus st_lux = EnvStatus::NOT_FOUND;

  if (bme_ok) {
    // Presence check first: with the cable unplugged some library reads fail silently
    // and return stale or zeroed registers instead of NaN.
    if (i2c_present(bme_addr) && bme.takeForcedMeasurement()) {
      t = bme.readTemperature();
      h = bme.readHumidity();
      p = bme.readPressure() / 100.0f;  // Pa -> hPa
    }
    st_bme = classify_climate(t, h, p);
    if (st_bme == EnvStatus::READ_ERROR) {
      bme_ok = false;  // re-initialized on the next cycle
      bme_init_status = EnvStatus::READ_ERROR;
    }
  }

  if (lux_ok) {
    lx = lux_meter.readLightLevel();
    st_lux = classify_lux(lx);
    if (st_lux == EnvStatus::READ_ERROR) {
      lux_ok = false;
      lx = NAN;
    }
  }

  report_link_event("BME280", bme_link.update(st_bme != EnvStatus::READ_ERROR &&
                                                  st_bme != EnvStatus::NOT_FOUND &&
                                                  st_bme != EnvStatus::WRONG_CHIP,
                                              now),
                    bme_link);
  report_link_event("BH1750", lux_link.update(st_lux != EnvStatus::READ_ERROR &&
                                                  st_lux != EnvStatus::NOT_FOUND,
                                              now),
                    lux_link);

  if (stab_running && st_bme == EnvStatus::OK) {
    stab_temp.add(t);
    stab_rh.add(h);
  }

  // CSV is muted during the light test so the prompts stay readable.
  if (streaming && light_phase == LightPhase::IDLE) {
    Serial.printf("%lu,%.2f,%.1f,%.1f,%.1f,%s,%s\n", static_cast<unsigned long>(now), t, h, p, lx,
                  env_status_name(st_bme), env_status_name(st_lux));
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

  bme_init_status = init_bme();
  bme_ok = bme_init_status == EnvStatus::OK;
  lux_ok = init_lux();
  Serial.printf("# BME280: %s", env_status_name(bme_init_status));
  if (bme_ok) Serial.printf(" (0x%02X, chip id 0x60, forced mode x1)", bme_addr);
  Serial.println();
  Serial.printf("# BH1750: %s", lux_ok ? "OK" : "NOT_FOUND");
  if (lux_ok) Serial.printf(" (0x%02X)", lux_addr);
  Serial.println();
  print_help();
  print_header();
}

void loop() {
  poll_serial();
  const uint32_t now = millis();

  run_light_test(now);

  if (stab_running && now - stab_start_ms >= kStabilityTestMs) finish_stability_test();

  if (now - last_read_ms < kReadPeriodMs) return;
  last_read_ms = now;
  periodic_read(now);
}
