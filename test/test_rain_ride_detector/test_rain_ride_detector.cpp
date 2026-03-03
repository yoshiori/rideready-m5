#include <unity.h>

#include "rain_ride_detector.h"

// --- isOutdoorRide tests ---

void test_ride_is_outdoor(void) {
  TEST_ASSERT_TRUE(RainRideDetector::isOutdoorRide("Ride"));
}

void test_ebike_ride_is_outdoor(void) {
  TEST_ASSERT_TRUE(RainRideDetector::isOutdoorRide("EBikeRide"));
}

void test_gravel_ride_is_outdoor(void) {
  TEST_ASSERT_TRUE(RainRideDetector::isOutdoorRide("GravelRide"));
}

void test_mountain_bike_ride_is_outdoor(void) {
  TEST_ASSERT_TRUE(RainRideDetector::isOutdoorRide("MountainBikeRide"));
}

void test_virtual_ride_not_outdoor(void) {
  TEST_ASSERT_FALSE(RainRideDetector::isOutdoorRide("VirtualRide"));
}

void test_run_not_outdoor(void) {
  TEST_ASSERT_FALSE(RainRideDetector::isOutdoorRide("Run"));
}

void test_empty_type_not_outdoor(void) {
  TEST_ASSERT_FALSE(RainRideDetector::isOutdoorRide(""));
}

// --- applySeverityOverride tests ---

void test_normal_with_rain_becomes_critical(void) {
  Severity result = RainRideDetector::applySeverityOverride(Severity::NORMAL, true);
  TEST_ASSERT_EQUAL(Severity::CRITICAL, result);
}

void test_warning_with_rain_becomes_critical(void) {
  Severity result = RainRideDetector::applySeverityOverride(Severity::WARNING, true);
  TEST_ASSERT_EQUAL(Severity::CRITICAL, result);
}

void test_normal_without_rain_stays_normal(void) {
  Severity result = RainRideDetector::applySeverityOverride(Severity::NORMAL, false);
  TEST_ASSERT_EQUAL(Severity::NORMAL, result);
}

void test_critical_without_rain_stays_critical(void) {
  Severity result = RainRideDetector::applySeverityOverride(Severity::CRITICAL, false);
  TEST_ASSERT_EQUAL(Severity::CRITICAL, result);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_ride_is_outdoor);
  RUN_TEST(test_ebike_ride_is_outdoor);
  RUN_TEST(test_gravel_ride_is_outdoor);
  RUN_TEST(test_mountain_bike_ride_is_outdoor);
  RUN_TEST(test_virtual_ride_not_outdoor);
  RUN_TEST(test_run_not_outdoor);
  RUN_TEST(test_empty_type_not_outdoor);
  RUN_TEST(test_normal_with_rain_becomes_critical);
  RUN_TEST(test_warning_with_rain_becomes_critical);
  RUN_TEST(test_normal_without_rain_stays_normal);
  RUN_TEST(test_critical_without_rain_stays_critical);
  UNITY_END();
  return 0;
}
