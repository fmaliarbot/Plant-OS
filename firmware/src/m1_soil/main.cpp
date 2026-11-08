// Plant OS — MILESTONE 1: capacitive soil moisture sensors (DFRobot SEN0193)
//
// What it does:
//   - Forces both pumps LOW at boot (even though they are not wired yet).
//   - Reads both sensors every 2 s with a burst of 31 samples (median + spread).
//   - Prints one CSV line per sensor: time, plant, mV, spread, 0-100 index, status.
//   - Captures dry/wet calibration per sensor and stores it in flash (NVS).
//   - Runs a 60 s stability test on demand.
//
// Commands (type in the serial monitor and press Enter):
//   h        help
//   d1 / d2  capture DRY point of sensor 1 (basil) / 2 (rosemary)  -> sensor in air, clean and dry
//   w1 / w2  capture WET point of sensor 1 / 2                      -> sensor in a glass of water UP TO THE LINE
//   c        show stored calibration
//   x        clear calibration
//   s        stability test (60 s, sensors untouched)
//   p        pause / resume streaming

#include <Arduino.h>
#include <Preferences.h>

#include "pins.h"
#include "plantcore.h"
#include "pump_safety.h"

using namespace plantos;

namespace {

constexpr uint32_t kReadPeriodMs = 2000;
constexpr size_t kBurstSamples = 31;
constexpr uint32_t kStabilityMs = 60000;
constexpr int kCalibrationBursts = 10;
constexpr double kStableStddevMv = 15.0;      // assumption, tune with real data
constexpr double kCalibrationMaxStddevMv = 30.0;

struct SoilChannel {
  const char* name;
  uint8_t pin;
  const char* key_dry;
  const char* key_wet;
  SoilCalibration cal;
  RunningStats stability;
};

SoilChannel channels[2] = {
    {"basil", pins::SOIL_BASIL, "b_dry", "b_wet", {}, {}},
    {"rosemary", pins::SOIL_ROSEMARY, "r_dry", "r_wet", {}, {}},
};

Preferences prefs;
bool streaming = true;
uint32_t last_read_ms = 0;
bool stability_running = false;
uint32_t stability_start_ms = 0;
String line_buf;

BurstSummary read_burst(uint8_t pin) {
  uint16_t samples[kBurstSamples];
  for (size_t i = 0; i < kBurstSamples; ++i) {
    samples[i] = static_cast<uint16_t>(analogReadMilliVolts(pin));
    delayMicroseconds(800);
  }
  return summarize(samples, kBurstSamples);
}

void load_calibration() {
  prefs.begin("plantos", true);
  for (auto& ch : channels) {
    ch.cal.dry_mv = prefs.getUShort(ch.key_dry, 0);
    ch.cal.wet_mv = prefs.getUShort(ch.key_wet, 0);
  }
  prefs.end();
}

void save_point(SoilChannel& ch, bool dry, uint16_t mv) {
  prefs.begin("plantos", false);
  prefs.putUShort(dry ? ch.key_dry : ch.key_wet, mv);
  prefs.end();
  if (dry) ch.cal.dry_mv = mv; else ch.cal.wet_mv = mv;
}

void print_header() {
  Serial.println(F("t_ms,plant,mv,spread_mv,index,status"));
}

void print_calibration() {
  for (auto& ch : channels) {
    Serial.printf("# %-9s dry=%4u mV  wet=%4u mV  span=%4d mV  %s\n", ch.name, ch.cal.dry_mv,
                  ch.cal.wet_mv, static_cast<int>(ch.cal.dry_mv) - static_cast<int>(ch.cal.wet_mv),
                  ch.cal.valid() ? "VALID" : "INVALID");
  }
}

void print_help() {
  Serial.println(F("# Commands: h help | d1/d2 dry | w1/w2 wet | c show cal | x clear cal | s stability 60s | p pause"));
}

void capture(int idx, bool dry) {
  SoilChannel& ch = channels[idx];
  Serial.printf("# Capturing %s point for %s: do not move the sensor...\n", dry ? "DRY" : "WET", ch.name);
  RunningStats rs;
  uint16_t max_spread = 0;
  for (int i = 0; i < kCalibrationBursts; ++i) {
    BurstSummary b = read_burst(ch.pin);
    rs.add(b.median_mv);
    if (b.spread_mv > max_spread) max_spread = b.spread_mv;
    delay(200);
  }
  const uint16_t mv = static_cast<uint16_t>(rs.mean() + 0.5);
  Serial.printf("# Result: %u mV (stddev %.1f mV, max spread %u mV)\n", mv, rs.stddev(), max_spread);
  if (rs.stddev() > kCalibrationMaxStddevMv) {
    Serial.println(F("# WARNING: unstable reading. Check the wiring and repeat before trusting this point."));
  }
  save_point(ch, dry, mv);
  print_calibration();
}

void handle_command(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.isEmpty()) return;
  if (cmd == "h") print_help();
  else if (cmd == "d1") capture(0, true);
  else if (cmd == "d2") capture(1, true);
  else if (cmd == "w1") capture(0, false);
  else if (cmd == "w2") capture(1, false);
  else if (cmd == "c") print_calibration();
  else if (cmd == "x") {
    prefs.begin("plantos", false);
    prefs.clear();
    prefs.end();
    load_calibration();
    Serial.println(F("# Calibration cleared."));
  } else if (cmd == "s") {
    for (auto& ch : channels) ch.stability.reset();
    stability_running = true;
    stability_start_ms = millis();
    Serial.println(F("# Stability test started (60 s). Do not touch the sensors."));
  } else if (cmd == "p") {
    streaming = !streaming;
    Serial.println(streaming ? F("# Streaming resumed.") : F("# Streaming paused."));
    if (streaming) print_header();
  } else {
    Serial.printf("# Unknown command: '%s'\n", cmd.c_str());
    print_help();
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

void finish_stability() {
  stability_running = false;
  Serial.println(F("# --- Stability test result ---"));
  for (auto& ch : channels) {
    const double sd = ch.stability.stddev();
    Serial.printf("# %-9s n=%u mean=%.0f mV stddev=%.1f mV min=%.0f max=%.0f -> %s\n", ch.name,
                  static_cast<unsigned>(ch.stability.count()), ch.stability.mean(), sd,
                  ch.stability.min(), ch.stability.max(), sd <= kStableStddevMv ? "STABLE" : "CHECK");
  }
}

}  // namespace

void setup() {
  pumps_force_off();  // always the first line

  Serial.begin(115200);
  delay(300);

  analogReadResolution(12);
  for (auto& ch : channels) analogSetPinAttenuation(ch.pin, ADC_11db);

  load_calibration();

  Serial.println();
  Serial.println(F("# Plant OS V0.1 - Milestone 1: soil moisture"));
  print_help();
  print_calibration();
  print_header();
}

void loop() {
  poll_serial();

  const uint32_t now = millis();
  if (now - last_read_ms >= kReadPeriodMs) {
    last_read_ms = now;
    for (auto& ch : channels) {
      BurstSummary b = read_burst(ch.pin);
      ReadingStatus st = classify(b, ch.cal);
      float idx = moisture_index(b.median_mv, ch.cal);
      if (stability_running) ch.stability.add(b.median_mv);
      if (streaming) {
        Serial.printf("%lu,%s,%u,%u,", static_cast<unsigned long>(now), ch.name, b.median_mv, b.spread_mv);
        if (idx >= 0) Serial.printf("%.1f", idx);  // empty field when uncalibrated
        Serial.printf(",%s\n", status_name(st));
      }
    }
  }

  if (stability_running && now - stability_start_ms >= kStabilityMs) finish_stability();
}
