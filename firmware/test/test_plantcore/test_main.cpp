// Unit tests for the pure logic. They run on the host:  pio test -e native
#include <unity.h>

#include "plantcore.h"

using namespace plantos;

void setUp() {}
void tearDown() {}

// ---- summarize ----

void test_summary_odd_count() {
  const uint16_t s[] = {1500, 1490, 1510, 1505, 1495};
  BurstSummary r = summarize(s, 5);
  TEST_ASSERT_EQUAL_UINT16(1500, r.median_mv);
  TEST_ASSERT_EQUAL_UINT32(5, r.n);
}

void test_summary_even_count_rounds() {
  const uint16_t s[] = {1000, 1001, 2000, 2001};
  BurstSummary r = summarize(s, 4);
  TEST_ASSERT_EQUAL_UINT16(1501, r.median_mv);  // (1001+2000)/2 = 1500.5 -> 1501
}

void test_summary_rejects_single_spike() {
  // A single spike must not move the median.
  const uint16_t s[] = {1600, 1602, 1598, 3300, 1601, 1599, 1600};
  BurstSummary r = summarize(s, 7);
  TEST_ASSERT_EQUAL_UINT16(1600, r.median_mv);
}

void test_summary_does_not_modify_input() {
  uint16_t s[] = {3, 1, 2};
  summarize(s, 3);
  TEST_ASSERT_EQUAL_UINT16(3, s[0]);
  TEST_ASSERT_EQUAL_UINT16(1, s[1]);
}

void test_summary_empty() {
  BurstSummary r = summarize(nullptr, 0);
  TEST_ASSERT_EQUAL_UINT32(0, r.n);
}

void test_summary_spread() {
  uint16_t s[11];
  for (int i = 0; i < 11; ++i) s[i] = static_cast<uint16_t>(1000 + i * 10);  // 1000..1100
  BurstSummary r = summarize(s, 11);
  TEST_ASSERT_EQUAL_UINT16(1010, r.p10_mv);
  TEST_ASSERT_EQUAL_UINT16(1090, r.p90_mv);
  TEST_ASSERT_EQUAL_UINT16(80, r.spread_mv);
}

// ---- calibration and index ----

void test_calibration_validity() {
  SoilCalibration ok{2600, 1300};
  SoilCalibration inverted{1300, 2600};
  SoilCalibration too_close{1500, 1400};
  TEST_ASSERT_TRUE(ok.valid());
  TEST_ASSERT_FALSE(inverted.valid());
  TEST_ASSERT_FALSE(too_close.valid());
}

void test_index_endpoints_and_midpoint() {
  SoilCalibration cal{2600, 1300};
  TEST_ASSERT_EQUAL_FLOAT(0.0f, moisture_index(2600, cal));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, moisture_index(1300, cal));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, moisture_index(1950, cal));
}

void test_index_clamps_outside_range() {
  SoilCalibration cal{2600, 1300};
  TEST_ASSERT_EQUAL_FLOAT(0.0f, moisture_index(2900, cal));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, moisture_index(1100, cal));
}

void test_index_invalid_calibration() {
  SoilCalibration cal{};
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, moisture_index(1800, cal));
}

// ---- classification ----

static BurstSummary make(uint16_t median, uint16_t spread) {
  BurstSummary b;
  b.median_mv = median;
  b.spread_mv = spread;
  b.n = 31;
  return b;
}

void test_classify_ok() {
  SoilCalibration cal{2600, 1300};
  TEST_ASSERT_EQUAL(ReadingStatus::OK, classify(make(1900, 20), cal));
}

void test_classify_uncalibrated() {
  TEST_ASSERT_EQUAL(ReadingStatus::UNCALIBRATED, classify(make(1900, 20), SoilCalibration{}));
}

void test_classify_low_implausible() {
  SoilCalibration cal{2600, 1300};
  TEST_ASSERT_EQUAL(ReadingStatus::LOW_IMPLAUSIBLE, classify(make(80, 5), cal));
}

void test_classify_high_saturated() {
  SoilCalibration cal{2600, 1300};
  TEST_ASSERT_EQUAL(ReadingStatus::HIGH_SATURATED, classify(make(3150, 5), cal));
}

void test_classify_noisy() {
  SoilCalibration cal{2600, 1300};
  TEST_ASSERT_EQUAL(ReadingStatus::NOISY, classify(make(1900, 400), cal));
}

void test_classify_empty_burst() {
  BurstSummary empty;
  TEST_ASSERT_EQUAL(ReadingStatus::LOW_IMPLAUSIBLE, classify(empty, SoilCalibration{2600, 1300}));
}

// ---- statistics ----

void test_running_stats() {
  RunningStats rs;
  const double xs[] = {2, 4, 4, 4, 5, 5, 7, 9};
  for (double x : xs) rs.add(x);
  TEST_ASSERT_EQUAL_UINT32(8, rs.count());
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 5.0, rs.mean());
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 2.13808994, rs.stddev());  // sample standard deviation
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 2.0, rs.min());
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 9.0, rs.max());
  rs.reset();
  TEST_ASSERT_EQUAL_UINT32(0, rs.count());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_summary_odd_count);
  RUN_TEST(test_summary_even_count_rounds);
  RUN_TEST(test_summary_rejects_single_spike);
  RUN_TEST(test_summary_does_not_modify_input);
  RUN_TEST(test_summary_empty);
  RUN_TEST(test_summary_spread);
  RUN_TEST(test_calibration_validity);
  RUN_TEST(test_index_endpoints_and_midpoint);
  RUN_TEST(test_index_clamps_outside_range);
  RUN_TEST(test_index_invalid_calibration);
  RUN_TEST(test_classify_ok);
  RUN_TEST(test_classify_uncalibrated);
  RUN_TEST(test_classify_low_implausible);
  RUN_TEST(test_classify_high_saturated);
  RUN_TEST(test_classify_noisy);
  RUN_TEST(test_classify_empty_burst);
  RUN_TEST(test_running_stats);
  return UNITY_END();
}
