#pragma once
// Environment logic for Milestone 2 (BME280 + BH1750). Pure C++, no Arduino dependency,
// so every decision taken on a temperature / humidity / pressure / lux reading is unit-tested.

#include <stddef.h>
#include <stdint.h>

#include "plantcore.h"  // RunningStats

namespace plantos {

// ---------- Status ----------

enum class EnvStatus : uint8_t {
  OK = 0,
  NOT_FOUND,     // nothing answers on the expected I2C addresses
  WRONG_CHIP,    // something answers at 0x76/0x77 but it is not a BME280 (e.g. a BMP280: no humidity)
  READ_ERROR,    // the sensor was found but a read failed (NaN, negative lux, I2C error)
  OUT_OF_RANGE,  // value read but outside the indoor plausibility window
  SATURATED,     // BH1750 at the top of its range: real light level is unknown (>= this value)
};

const char* env_status_name(EnvStatus st);

// ---------- BME280 chip identification ----------
// Register 0xD0 (chip id). Bosch datasheets: BME280 = 0x60; BMP280 = 0x58 (0x56/0x57 on samples).
enum class BmeChip : uint8_t { BME280, BMP280, UNKNOWN };

BmeChip identify_bme_chip(uint8_t chip_id);
const char* bme_chip_name(BmeChip chip);

// ---------- Plausibility ----------
// Indoor plausibility windows, NOT sensor limits. [Assumption] tune with measured data.
struct EnvLimits {
  float temp_min_c = -10.0f;
  float temp_max_c = 50.0f;
  float rh_min_pct = 0.0f;
  float rh_max_pct = 100.0f;
  float pressure_min_hpa = 800.0f;   // ~2000 m altitude
  float pressure_max_hpa = 1100.0f;
  // BH1750 in CONTINUOUS_HIGH_RES_MODE with the default MTreg (69) tops out at 54612.5 lx.
  float lux_full_scale = 54612.5f;
  float lux_saturation_fraction = 0.99f;  // readings at >= 99 % of full scale are flagged SATURATED
};

// t / rh / p: values as returned by the Adafruit library (NaN on failure). p in hPa.
EnvStatus classify_climate(float temp_c, float rh_pct, float pressure_hpa,
                           const EnvLimits& lim = EnvLimits{});

// lux: value as returned by the BH1750 library (-1 / -2 on failure).
EnvStatus classify_lux(float lux, const EnvLimits& lim = EnvLimits{});

// ---------- Link monitor (unplug / replug pass test) ----------
// Tracks one sensor's connection and reports transitions, so the operator sees
// "LOST" and "RECOVERED after N s" instead of having to spot a status change in the CSV.
class LinkMonitor {
 public:
  enum class Event : uint8_t { NONE, LOST, RECOVERED };

  // Call once per read cycle. `ok` = the sensor answered this cycle.
  Event update(bool ok, uint32_t now_ms);

  bool up() const { return up_; }
  uint32_t outages() const { return outages_; }
  uint32_t recoveries() const { return recoveries_; }
  uint32_t last_outage_ms() const { return last_outage_ms_; }  // duration of the last outage

 private:
  bool initialized_ = false;
  bool up_ = false;
  uint32_t lost_at_ms_ = 0;
  uint32_t last_outage_ms_ = 0;
  uint32_t outages_ = 0;
  uint32_t recoveries_ = 0;
};

// ---------- Climate stability test ----------
// With the sensor undisturbed, temperature and humidity must not wander.
// Thresholds are [Assumption]: ~2x the BME280 typical noise, tune with data.
struct ClimateStabilityLimits {
  double max_temp_stddev_c = 0.2;
  double max_rh_stddev_pct = 1.0;
  size_t min_samples = 10;
};

struct ClimateStabilityResult {
  bool enough_samples = false;
  bool temp_stable = false;
  bool rh_stable = false;
  bool pass() const { return enough_samples && temp_stable && rh_stable; }
};

ClimateStabilityResult evaluate_climate_stability(
    const RunningStats& temp, const RunningStats& rh,
    const ClimateStabilityLimits& lim = ClimateStabilityLimits{});

// ---------- Light response test ----------
// Three phases: ambient baseline, sensor covered by hand, flashlight pointed at it.
struct LightTestLimits {
  float min_baseline_lux = 20.0f;      // below this the room is too dark to judge "covered"
  float covered_abs_max_lux = 5.0f;    // covered must read <= max(5 lx, 10 % of baseline)
  float covered_fraction_max = 0.10f;
  float lit_factor_min = 3.0f;         // flashlight must read >= 3x baseline
  float lit_abs_min_lux = 100.0f;      // and at least 100 lx
};

enum class LightVerdict : uint8_t { PASS, FAIL, INCONCLUSIVE };

struct LightTestResult {
  bool covered_ok = false;
  bool lit_ok = false;
  LightVerdict verdict = LightVerdict::INCONCLUSIVE;
};

// baseline = median-ish ambient value, covered_min = lowest reading while covered,
// lit_max = highest reading under the flashlight. Any negative/NaN input -> INCONCLUSIVE.
LightTestResult evaluate_light_test(float baseline_lux, float covered_min_lux, float lit_max_lux,
                                    const LightTestLimits& lim = LightTestLimits{});

const char* light_verdict_name(LightVerdict v);

}  // namespace plantos
