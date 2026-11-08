// Unit tests for the Milestone 2 environment logic. Run on the host:  pio test -e native
#include <math.h>
#include <unity.h>

#include "environment.h"

using namespace plantos;

void setUp() {}
void tearDown() {}

// ---- chip identification ----

void test_chip_id_bme280() { TEST_ASSERT_EQUAL(BmeChip::BME280, identify_bme_chip(0x60)); }

void test_chip_id_bmp280_variants() {
  TEST_ASSERT_EQUAL(BmeChip::BMP280, identify_bme_chip(0x58));
  TEST_ASSERT_EQUAL(BmeChip::BMP280, identify_bme_chip(0x56));
  TEST_ASSERT_EQUAL(BmeChip::BMP280, identify_bme_chip(0x57));
}

void test_chip_id_unknown() {
  TEST_ASSERT_EQUAL(BmeChip::UNKNOWN, identify_bme_chip(0x00));
  TEST_ASSERT_EQUAL(BmeChip::UNKNOWN, identify_bme_chip(0xFF));  // floating bus reads 0xFF
  TEST_ASSERT_EQUAL(BmeChip::UNKNOWN, identify_bme_chip(0x61));  // BME680, not supported here
}

// ---- climate classification ----

void test_climate_ok() {
  TEST_ASSERT_EQUAL(EnvStatus::OK, classify_climate(21.5f, 45.0f, 980.0f));
}

void test_climate_nan_is_read_error() {
  TEST_ASSERT_EQUAL(EnvStatus::READ_ERROR, classify_climate(NAN, 45.0f, 980.0f));
  TEST_ASSERT_EQUAL(EnvStatus::READ_ERROR, classify_climate(21.0f, NAN, 980.0f));
  TEST_ASSERT_EQUAL(EnvStatus::READ_ERROR, classify_climate(21.0f, 45.0f, NAN));
}

void test_climate_out_of_range() {
  TEST_ASSERT_EQUAL(EnvStatus::OUT_OF_RANGE, classify_climate(85.0f, 45.0f, 980.0f));
  TEST_ASSERT_EQUAL(EnvStatus::OUT_OF_RANGE, classify_climate(21.0f, 101.0f, 980.0f));
  // Pressure read as Pa instead of hPa (a classic unit bug) must not pass.
  TEST_ASSERT_EQUAL(EnvStatus::OUT_OF_RANGE, classify_climate(21.0f, 45.0f, 98000.0f));
}

void test_climate_limits_inclusive() {
  TEST_ASSERT_EQUAL(EnvStatus::OK, classify_climate(50.0f, 100.0f, 800.0f));
  TEST_ASSERT_EQUAL(EnvStatus::OK, classify_climate(-10.0f, 0.0f, 1100.0f));
}

// ---- lux classification ----

void test_lux_ok_including_darkness() {
  TEST_ASSERT_EQUAL(EnvStatus::OK, classify_lux(0.0f));  // total darkness is a valid reading
  TEST_ASSERT_EQUAL(EnvStatus::OK, classify_lux(320.0f));
}

void test_lux_library_errors() {
  TEST_ASSERT_EQUAL(EnvStatus::READ_ERROR, classify_lux(-1.0f));  // I2C read failed
  TEST_ASSERT_EQUAL(EnvStatus::READ_ERROR, classify_lux(-2.0f));  // not configured
  TEST_ASSERT_EQUAL(EnvStatus::READ_ERROR, classify_lux(NAN));
}

void test_lux_saturation() {
  TEST_ASSERT_EQUAL(EnvStatus::OK, classify_lux(50000.0f));
  TEST_ASSERT_EQUAL(EnvStatus::SATURATED, classify_lux(54612.5f));
  TEST_ASSERT_EQUAL(EnvStatus::SATURATED, classify_lux(54100.0f));  // >= 99 % of full scale
}

// ---- link monitor ----

void test_link_first_observation_is_silent() {
  LinkMonitor up_at_boot;
  TEST_ASSERT_EQUAL(LinkMonitor::Event::NONE, up_at_boot.update(true, 0));
  LinkMonitor down_at_boot;
  TEST_ASSERT_EQUAL(LinkMonitor::Event::NONE, down_at_boot.update(false, 0));
  TEST_ASSERT_EQUAL_UINT32(0, down_at_boot.outages());
}

void test_link_lost_and_recovered() {
  LinkMonitor m;
  m.update(true, 0);
  TEST_ASSERT_EQUAL(LinkMonitor::Event::NONE, m.update(true, 5000));
  TEST_ASSERT_EQUAL(LinkMonitor::Event::LOST, m.update(false, 10000));
  TEST_ASSERT_EQUAL(LinkMonitor::Event::NONE, m.update(false, 15000));  // reported once only
  TEST_ASSERT_FALSE(m.up());
  TEST_ASSERT_EQUAL(LinkMonitor::Event::RECOVERED, m.update(true, 25000));
  TEST_ASSERT_TRUE(m.up());
  TEST_ASSERT_EQUAL_UINT32(1, m.outages());
  TEST_ASSERT_EQUAL_UINT32(1, m.recoveries());
  TEST_ASSERT_EQUAL_UINT32(15000, m.last_outage_ms());
}

void test_link_missing_at_boot_then_plugged() {
  LinkMonitor m;
  m.update(false, 1000);
  TEST_ASSERT_EQUAL(LinkMonitor::Event::RECOVERED, m.update(true, 6000));
  TEST_ASSERT_EQUAL_UINT32(0, m.outages());
  TEST_ASSERT_EQUAL_UINT32(5000, m.last_outage_ms());
}

void test_link_survives_millis_wrap() {
  LinkMonitor m;
  m.update(true, 0);
  m.update(false, 0xFFFFF000u);
  m.update(true, 0x00001000u);  // millis() wrapped after ~49.7 days
  TEST_ASSERT_EQUAL_UINT32(0x2000, m.last_outage_ms());
}

// ---- climate stability ----

void test_stability_needs_samples() {
  RunningStats t, h;
  for (int i = 0; i < 5; ++i) { t.add(21.0); h.add(45.0); }
  ClimateStabilityResult r = evaluate_climate_stability(t, h);
  TEST_ASSERT_FALSE(r.enough_samples);
  TEST_ASSERT_FALSE(r.pass());
}

void test_stability_pass() {
  RunningStats t, h;
  for (int i = 0; i < 12; ++i) {
    t.add(21.0 + (i % 2 ? 0.05 : -0.05));
    h.add(45.0 + (i % 2 ? 0.3 : -0.3));
  }
  TEST_ASSERT_TRUE(evaluate_climate_stability(t, h).pass());
}

void test_stability_fails_on_drift() {
  // Typical self-heating / hand-on-sensor signature: steady upward drift of ~1 degC.
  RunningStats t, h;
  for (int i = 0; i < 12; ++i) { t.add(21.0 + i * 0.1); h.add(45.0); }
  ClimateStabilityResult r = evaluate_climate_stability(t, h);
  TEST_ASSERT_FALSE(r.temp_stable);
  TEST_ASSERT_TRUE(r.rh_stable);
  TEST_ASSERT_FALSE(r.pass());
}

// ---- light test ----

void test_light_pass() {
  LightTestResult r = evaluate_light_test(300.0f, 2.0f, 4000.0f);
  TEST_ASSERT_TRUE(r.covered_ok);
  TEST_ASSERT_TRUE(r.lit_ok);
  TEST_ASSERT_EQUAL(LightVerdict::PASS, r.verdict);
}

void test_light_covered_uses_relative_threshold_in_bright_room() {
  // Bright room: a hand lets some light through; 10 % of 2000 lx = 200 lx is accepted.
  LightTestResult r = evaluate_light_test(2000.0f, 150.0f, 20000.0f);
  TEST_ASSERT_TRUE(r.covered_ok);
  TEST_ASSERT_EQUAL(LightVerdict::PASS, r.verdict);
}

void test_light_fails_when_cover_has_no_effect() {
  LightTestResult r = evaluate_light_test(300.0f, 280.0f, 4000.0f);
  TEST_ASSERT_FALSE(r.covered_ok);
  TEST_ASSERT_EQUAL(LightVerdict::FAIL, r.verdict);
}

void test_light_fails_when_flashlight_has_no_effect() {
  LightTestResult r = evaluate_light_test(300.0f, 2.0f, 350.0f);
  TEST_ASSERT_FALSE(r.lit_ok);
  TEST_ASSERT_EQUAL(LightVerdict::FAIL, r.verdict);
}

void test_light_dark_room_is_inconclusive() {
  LightTestResult r = evaluate_light_test(8.0f, 1.0f, 3000.0f);
  TEST_ASSERT_EQUAL(LightVerdict::INCONCLUSIVE, r.verdict);
  // ...but a dead flashlight response is still a clear failure.
  TEST_ASSERT_EQUAL(LightVerdict::FAIL, evaluate_light_test(8.0f, 1.0f, 10.0f).verdict);
}

void test_light_invalid_input_is_inconclusive() {
  TEST_ASSERT_EQUAL(LightVerdict::INCONCLUSIVE, evaluate_light_test(-1.0f, 1.0f, 3000.0f).verdict);
  TEST_ASSERT_EQUAL(LightVerdict::INCONCLUSIVE, evaluate_light_test(300.0f, NAN, 3000.0f).verdict);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_chip_id_bme280);
  RUN_TEST(test_chip_id_bmp280_variants);
  RUN_TEST(test_chip_id_unknown);
  RUN_TEST(test_climate_ok);
  RUN_TEST(test_climate_nan_is_read_error);
  RUN_TEST(test_climate_out_of_range);
  RUN_TEST(test_climate_limits_inclusive);
  RUN_TEST(test_lux_ok_including_darkness);
  RUN_TEST(test_lux_library_errors);
  RUN_TEST(test_lux_saturation);
  RUN_TEST(test_link_first_observation_is_silent);
  RUN_TEST(test_link_lost_and_recovered);
  RUN_TEST(test_link_missing_at_boot_then_plugged);
  RUN_TEST(test_link_survives_millis_wrap);
  RUN_TEST(test_stability_needs_samples);
  RUN_TEST(test_stability_pass);
  RUN_TEST(test_stability_fails_on_drift);
  RUN_TEST(test_light_pass);
  RUN_TEST(test_light_covered_uses_relative_threshold_in_bright_room);
  RUN_TEST(test_light_fails_when_cover_has_no_effect);
  RUN_TEST(test_light_fails_when_flashlight_has_no_effect);
  RUN_TEST(test_light_dark_room_is_inconclusive);
  RUN_TEST(test_light_invalid_input_is_inconclusive);
  return UNITY_END();
}
