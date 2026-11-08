#include "environment.h"

#include <math.h>

namespace plantos {

const char* env_status_name(EnvStatus st) {
  switch (st) {
    case EnvStatus::OK: return "OK";
    case EnvStatus::NOT_FOUND: return "NOT_FOUND";
    case EnvStatus::WRONG_CHIP: return "WRONG_CHIP";
    case EnvStatus::READ_ERROR: return "READ_ERROR";
    case EnvStatus::OUT_OF_RANGE: return "OUT_OF_RANGE";
    case EnvStatus::SATURATED: return "SATURATED";
  }
  return "?";
}

BmeChip identify_bme_chip(uint8_t chip_id) {
  switch (chip_id) {
    case 0x60: return BmeChip::BME280;
    case 0x56:
    case 0x57:
    case 0x58: return BmeChip::BMP280;
    default: return BmeChip::UNKNOWN;
  }
}

const char* bme_chip_name(BmeChip chip) {
  switch (chip) {
    case BmeChip::BME280: return "BME280";
    case BmeChip::BMP280: return "BMP280";
    case BmeChip::UNKNOWN: return "UNKNOWN";
  }
  return "?";
}

namespace {
bool within(float v, float lo, float hi) { return v >= lo && v <= hi; }
}  // namespace

EnvStatus classify_climate(float temp_c, float rh_pct, float pressure_hpa, const EnvLimits& lim) {
  if (isnan(temp_c) || isnan(rh_pct) || isnan(pressure_hpa)) return EnvStatus::READ_ERROR;
  const bool ok = within(temp_c, lim.temp_min_c, lim.temp_max_c) &&
                  within(rh_pct, lim.rh_min_pct, lim.rh_max_pct) &&
                  within(pressure_hpa, lim.pressure_min_hpa, lim.pressure_max_hpa);
  return ok ? EnvStatus::OK : EnvStatus::OUT_OF_RANGE;
}

EnvStatus classify_lux(float lux, const EnvLimits& lim) {
  if (isnan(lux) || lux < 0.0f) return EnvStatus::READ_ERROR;  // library returns -1 / -2
  if (lux >= lim.lux_full_scale * lim.lux_saturation_fraction) return EnvStatus::SATURATED;
  return EnvStatus::OK;
}

LinkMonitor::Event LinkMonitor::update(bool ok, uint32_t now_ms) {
  if (!initialized_) {
    // The first observation sets the state without reporting an event:
    // a sensor missing at boot is reported by the init code, not as an outage.
    initialized_ = true;
    up_ = ok;
    if (!ok) lost_at_ms_ = now_ms;
    return Event::NONE;
  }
  if (up_ && !ok) {
    up_ = false;
    lost_at_ms_ = now_ms;
    ++outages_;
    return Event::LOST;
  }
  if (!up_ && ok) {
    up_ = true;
    last_outage_ms_ = now_ms - lost_at_ms_;  // unsigned: correct across millis() wrap
    ++recoveries_;
    return Event::RECOVERED;
  }
  return Event::NONE;
}

ClimateStabilityResult evaluate_climate_stability(const RunningStats& temp, const RunningStats& rh,
                                                  const ClimateStabilityLimits& lim) {
  ClimateStabilityResult r;
  r.enough_samples = temp.count() >= lim.min_samples && rh.count() >= lim.min_samples;
  if (!r.enough_samples) return r;
  r.temp_stable = temp.stddev() <= lim.max_temp_stddev_c;
  r.rh_stable = rh.stddev() <= lim.max_rh_stddev_pct;
  return r;
}

LightTestResult evaluate_light_test(float baseline_lux, float covered_min_lux, float lit_max_lux,
                                    const LightTestLimits& lim) {
  LightTestResult r;
  const bool valid = !isnan(baseline_lux) && !isnan(covered_min_lux) && !isnan(lit_max_lux) &&
                     baseline_lux >= 0.0f && covered_min_lux >= 0.0f && lit_max_lux >= 0.0f;
  if (!valid) return r;  // INCONCLUSIVE

  float covered_max = baseline_lux * lim.covered_fraction_max;
  if (covered_max < lim.covered_abs_max_lux) covered_max = lim.covered_abs_max_lux;
  r.covered_ok = covered_min_lux <= covered_max;

  float lit_min = baseline_lux * lim.lit_factor_min;
  if (lit_min < lim.lit_abs_min_lux) lit_min = lim.lit_abs_min_lux;
  r.lit_ok = lit_max_lux >= lit_min;

  if (baseline_lux < lim.min_baseline_lux) {
    // Room too dark: "covered" cannot be told apart from "ambient". Only a clear
    // failure of the flashlight phase is conclusive.
    r.verdict = r.lit_ok ? LightVerdict::INCONCLUSIVE : LightVerdict::FAIL;
    return r;
  }
  r.verdict = (r.covered_ok && r.lit_ok) ? LightVerdict::PASS : LightVerdict::FAIL;
  return r;
}

const char* light_verdict_name(LightVerdict v) {
  switch (v) {
    case LightVerdict::PASS: return "PASS";
    case LightVerdict::FAIL: return "FAIL";
    case LightVerdict::INCONCLUSIVE: return "INCONCLUSIVE";
  }
  return "?";
}

}  // namespace plantos
