#pragma once
// plantcore: pure logic (no Arduino dependency) so it can be unit-tested on the host.
// Anything that decides something from a measurement lives here, not in main.cpp.

#include <stddef.h>
#include <stdint.h>

namespace plantos {

// ---------- Burst filtering ----------

// Maximum samples per burst. Functions ignore anything beyond this.
constexpr size_t kMaxSamples = 64;

// Robust summary of one burst of ADC readings (in mV).
struct BurstSummary {
  uint16_t median_mv = 0;
  uint16_t p10_mv = 0;     // 10th percentile
  uint16_t p90_mv = 0;     // 90th percentile
  uint16_t spread_mv = 0;  // p90 - p10: noise within the burst
  size_t n = 0;
};

// Computes median and spread. Does not modify `samples`.
BurstSummary summarize(const uint16_t* samples, size_t n);

// ---------- Capacitive sensor calibration ----------
// On the DFRobot SEN0193 the voltage goes DOWN as moisture goes UP.
// dry_mv = reading in air, wet_mv = reading in water up to the marked line.
struct SoilCalibration {
  uint16_t dry_mv = 0;
  uint16_t wet_mv = 0;

  // Minimum dry-wet span for the calibration to be considered valid.
  static constexpr uint16_t kMinSpanMv = 300;

  bool valid() const {
    return dry_mv > wet_mv && (dry_mv - wet_mv) >= kMinSpanMv;
  }
};

// Relative moisture index 0..100 between the dry and wet points.
// This is NOT volumetric water content: it is a scale specific to each sensor + substrate.
// Returns -1 if the calibration is not valid.
float moisture_index(uint16_t mv, const SoilCalibration& cal);

// ---------- Validation ----------

enum class ReadingStatus : uint8_t {
  OK = 0,
  UNCALIBRATED,     // plausible reading but no valid calibration
  LOW_IMPLAUSIBLE,  // below the sensor's physical range: short circuit or loose wire
  HIGH_SATURATED,   // above the useful ADC range: sensor in air or disconnected
  NOISY,            // burst too dispersed: bad contact, interference
};

struct ValidationLimits {
  uint16_t min_plausible_mv = 500;   // assumption: SEN0193 in water stays well above this
  uint16_t max_plausible_mv = 3000;  // DFRobot specifies 0-3.0 V output; above is suspicious
  uint16_t max_spread_mv = 120;      // initial assumption; tune with Milestone 1 data
};

ReadingStatus classify(const BurstSummary& s, const SoilCalibration& cal,
                       const ValidationLimits& lim = ValidationLimits{});

const char* status_name(ReadingStatus st);

// ---------- Running statistics (stability test) ----------
// Welford's algorithm: mean and standard deviation without storing samples.
class RunningStats {
 public:
  void add(double x);
  void reset();
  size_t count() const { return n_; }
  double mean() const { return n_ ? mean_ : 0.0; }
  double stddev() const;  // sample standard deviation
  double min() const { return min_; }
  double max() const { return max_; }

 private:
  size_t n_ = 0;
  double mean_ = 0.0;
  double m2_ = 0.0;
  double min_ = 0.0;
  double max_ = 0.0;
};

}  // namespace plantos
