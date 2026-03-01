#include <unity.h>

#include "pressure_trend.h"

void test_initial_state_is_stable(void) {
  PressureTrend trend;
  TEST_ASSERT_EQUAL(TrendDirection::TREND_STABLE, trend.direction());
  TEST_ASSERT_EQUAL(0, trend.count());
}

void test_single_sample_is_stable(void) {
  PressureTrend trend;
  trend.addSample(1013.0f);
  TEST_ASSERT_EQUAL(TrendDirection::TREND_STABLE, trend.direction());
}

void test_rising_pressure(void) {
  PressureTrend trend;
  // First half: lower values
  trend.addSample(1010.0f);
  trend.addSample(1011.0f);
  trend.addSample(1012.0f);
  // Second half: higher values
  trend.addSample(1014.0f);
  trend.addSample(1015.0f);
  trend.addSample(1016.0f);
  TEST_ASSERT_EQUAL(TrendDirection::TREND_RISING, trend.direction());
}

void test_falling_pressure(void) {
  PressureTrend trend;
  // First half: higher values
  trend.addSample(1016.0f);
  trend.addSample(1015.0f);
  trend.addSample(1014.0f);
  // Second half: lower values
  trend.addSample(1012.0f);
  trend.addSample(1011.0f);
  trend.addSample(1010.0f);
  TEST_ASSERT_EQUAL(TrendDirection::TREND_FALLING, trend.direction());
}

void test_stable_pressure(void) {
  PressureTrend trend;
  trend.addSample(1013.0f);
  trend.addSample(1013.1f);
  trend.addSample(1013.2f);
  trend.addSample(1013.1f);
  trend.addSample(1013.0f);
  trend.addSample(1013.2f);
  TEST_ASSERT_EQUAL(TrendDirection::TREND_STABLE, trend.direction());
}

void test_ring_buffer_wraparound(void) {
  PressureTrend trend;
  // Fill buffer with stable data
  for (int i = 0; i < 6; i++) {
    trend.addSample(1013.0f);
  }
  TEST_ASSERT_EQUAL(TrendDirection::TREND_STABLE, trend.direction());

  // Now add rising data that overwrites old values
  trend.addSample(1013.0f);
  trend.addSample(1013.0f);
  trend.addSample(1013.0f);
  trend.addSample(1016.0f);
  trend.addSample(1017.0f);
  trend.addSample(1018.0f);
  TEST_ASSERT_EQUAL(TrendDirection::TREND_RISING, trend.direction());
}

void test_reset(void) {
  PressureTrend trend;
  trend.addSample(1010.0f);
  trend.addSample(1015.0f);
  trend.addSample(1020.0f);

  trend.reset();
  TEST_ASSERT_EQUAL(0, trend.count());
  TEST_ASSERT_EQUAL(TrendDirection::TREND_STABLE, trend.direction());
}

void test_custom_threshold(void) {
  // Use a large threshold — even moderate differences should be STABLE
  PressureTrend trend(5.0f);
  trend.addSample(1010.0f);
  trend.addSample(1011.0f);
  trend.addSample(1012.0f);
  trend.addSample(1014.0f);
  trend.addSample(1015.0f);
  trend.addSample(1016.0f);
  TEST_ASSERT_EQUAL(TrendDirection::TREND_STABLE, trend.direction());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_initial_state_is_stable);
  RUN_TEST(test_single_sample_is_stable);
  RUN_TEST(test_rising_pressure);
  RUN_TEST(test_falling_pressure);
  RUN_TEST(test_stable_pressure);
  RUN_TEST(test_ring_buffer_wraparound);
  RUN_TEST(test_reset);
  RUN_TEST(test_custom_threshold);
  UNITY_END();
  return 0;
}
