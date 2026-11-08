#include "plantcore.h"

#include <math.h>

namespace plantos {

namespace {

void insertion_sort(uint16_t* a, size_t n) {
  for (size_t i = 1; i < n; ++i) {
    uint16_t key = a[i];
    size_t j = i;
    while (j > 0 && a[j - 1] > key) {
      a[j] = a[j - 1];
      --j;
    }
    a[j] = key;
  }
}

// Nearest-rank percentile over an already sorted array.
uint16_t percentile_sorted(const uint16_t* sorted, size_t n, unsigned pct) {
  if (n == 0) return 0;
  size_t idx = ((n - 1) * pct + 50) / 100;  // rounding
  if (idx >= n) idx = n - 1;
  return sorted[idx];
}

}  // namespace

BurstSummary summarize(const uint16_t* samples, size_t n) {
  BurstSummary out;
  if (samples == nullptr || n == 0) return out;
  if (n > kMaxSamples) n = kMaxSamples;

  uint16_t buf[kMaxSamples];
  for (size_t i = 0; i < n; ++i) buf[i] = samples[i];
  insertion_sort(buf, n);

  if (n % 2 == 1) {
    out.median_mv = buf[n / 2];
  } else {
    out.median_mv = static_cast<uint16_t>((static_cast<uint32_t>(buf[n / 2 - 1]) + buf[n / 2] + 1) / 2);
  }
  out.p10_mv = percentile_sorted(buf, n, 10);
  out.p90_mv = percentile_sorted(buf, n, 90);
  out.spread_mv = static_cast<uint16_t>(out.p90_mv - out.p10_mv);
  out.n = n;
  return out;
}

float moisture_index(uint16_t mv, const SoilCalibration& cal) {
  if (!cal.valid()) return -1.0f;
  if (mv >= cal.dry_mv) return 0.0f;
  if (mv <= cal.wet_mv) return 100.0f;
  const float span = static_cast<float>(cal.dry_mv - cal.wet_mv);
  return 100.0f * static_cast<float>(cal.dry_mv - mv) / span;
}

ReadingStatus classify(const BurstSummary& s, const SoilCalibration& cal,
                       const ValidationLimits& lim) {
  if (s.n == 0) return ReadingStatus::LOW_IMPLAUSIBLE;
  if (s.median_mv < lim.min_plausible_mv) return ReadingStatus::LOW_IMPLAUSIBLE;
  if (s.median_mv > lim.max_plausible_mv) return ReadingStatus::HIGH_SATURATED;
  if (s.spread_mv > lim.max_spread_mv) return ReadingStatus::NOISY;
  if (!cal.valid()) return ReadingStatus::UNCALIBRATED;
  return ReadingStatus::OK;
}

const char* status_name(ReadingStatus st) {
  switch (st) {
    case ReadingStatus::OK: return "OK";
    case ReadingStatus::UNCALIBRATED: return "UNCALIBRATED";
    case ReadingStatus::LOW_IMPLAUSIBLE: return "LOW_IMPLAUSIBLE";
    case ReadingStatus::HIGH_SATURATED: return "HIGH_SATURATED";
    case ReadingStatus::NOISY: return "NOISY";
  }
  return "UNKNOWN";
}

void RunningStats::add(double x) {
  ++n_;
  if (n_ == 1) {
    mean_ = x;
    m2_ = 0.0;
    min_ = max_ = x;
    return;
  }
  const double delta = x - mean_;
  mean_ += delta / static_cast<double>(n_);
  m2_ += delta * (x - mean_);
  if (x < min_) min_ = x;
  if (x > max_) max_ = x;
}

void RunningStats::reset() { *this = RunningStats{}; }

double RunningStats::stddev() const {
  if (n_ < 2) return 0.0;
  return sqrt(m2_ / static_cast<double>(n_ - 1));
}

}  // namespace plantos
