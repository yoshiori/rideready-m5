#include <unity.h>
#include <ctime>

#include "maintenance_display.h"

// Helper: create a time_t for a specific date (JST, but we treat as UTC for testing)
static time_t makeEpoch(int year, int month, int day) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = 0;
  t.tm_min = 0;
  t.tm_sec = 0;
  return mktime(&t);
}

static const uint64_t MS_PER_HOUR = 3600ULL * 1000ULL;

// --- NTP not synced (epoch == 0): fallback to hours ---

void test_no_epoch_shows_hours(void) {
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(0, 0, 5 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL_STRING("5 h", result.text);
}

void test_no_epoch_zero_hours(void) {
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(0, 0, 0);
  TEST_ASSERT_EQUAL_STRING("0 h", result.text);
}

// --- NTP synced: days display ---

void test_epoch_zero_days(void) {
  time_t resetEpoch = makeEpoch(2026, 3, 1);
  time_t currentEpoch = makeEpoch(2026, 3, 1);
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(resetEpoch, currentEpoch, 0);
  TEST_ASSERT_EQUAL_STRING("0 days", result.text);
}

void test_epoch_three_days(void) {
  time_t resetEpoch = makeEpoch(2026, 3, 1);
  time_t currentEpoch = makeEpoch(2026, 3, 4);
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(resetEpoch, currentEpoch, 3 * 24 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL_STRING("3 days", result.text);
}

void test_epoch_seven_days(void) {
  time_t resetEpoch = makeEpoch(2026, 3, 1);
  time_t currentEpoch = makeEpoch(2026, 3, 8);
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(resetEpoch, currentEpoch, 7 * 24 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL_STRING("7 days", result.text);
}

void test_epoch_over_seven_days_shows_date(void) {
  time_t resetEpoch = makeEpoch(2026, 3, 1);
  time_t currentEpoch = makeEpoch(2026, 3, 9);
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(resetEpoch, currentEpoch, 8 * 24 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL_STRING("03/01", result.text);
}

void test_epoch_date_format_december(void) {
  time_t resetEpoch = makeEpoch(2025, 12, 25);
  time_t currentEpoch = makeEpoch(2026, 1, 5);
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(resetEpoch, currentEpoch, 11 * 24 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL_STRING("12/25", result.text);
}

void test_epoch_one_day(void) {
  time_t resetEpoch = makeEpoch(2026, 3, 1);
  time_t currentEpoch = makeEpoch(2026, 3, 2);
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(resetEpoch, currentEpoch, 24 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL_STRING("1 day", result.text);
}

// --- Tire pressure severity (epoch-based) ---

void test_tire_severity_0_days_normal(void) {
  time_t resetEpoch = makeEpoch(2026, 3, 1);
  time_t currentEpoch = makeEpoch(2026, 3, 1);
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(resetEpoch, currentEpoch, 0);
  TEST_ASSERT_EQUAL(Severity::NORMAL, result.severity);
}

void test_tire_severity_6_days_normal(void) {
  time_t resetEpoch = makeEpoch(2026, 3, 1);
  time_t currentEpoch = makeEpoch(2026, 3, 7);
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(resetEpoch, currentEpoch, 6 * 24 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL(Severity::NORMAL, result.severity);
}

void test_tire_severity_7_days_warning(void) {
  time_t resetEpoch = makeEpoch(2026, 3, 1);
  time_t currentEpoch = makeEpoch(2026, 3, 8);
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(resetEpoch, currentEpoch, 7 * 24 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL(Severity::WARNING, result.severity);
}

void test_tire_severity_13_days_warning(void) {
  time_t resetEpoch = makeEpoch(2026, 3, 1);
  time_t currentEpoch = makeEpoch(2026, 3, 14);
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(resetEpoch, currentEpoch, 13 * 24 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL(Severity::WARNING, result.severity);
}

void test_tire_severity_14_days_critical(void) {
  time_t resetEpoch = makeEpoch(2026, 3, 1);
  time_t currentEpoch = makeEpoch(2026, 3, 15);
  MaintenanceDisplayResult result =
      MaintenanceDisplay::format(resetEpoch, currentEpoch, 14 * 24 * MS_PER_HOUR);
  TEST_ASSERT_EQUAL(Severity::CRITICAL, result.severity);
}

// --- Chain lube severity ---

void test_chain_severity_0km_normal(void) {
  MaintenanceDisplayResult result = MaintenanceDisplay::formatDistance(0.0f);
  TEST_ASSERT_EQUAL(Severity::NORMAL, result.severity);
}

void test_chain_severity_299km_normal(void) {
  MaintenanceDisplayResult result = MaintenanceDisplay::formatDistance(299.9f);
  TEST_ASSERT_EQUAL(Severity::NORMAL, result.severity);
}

void test_chain_severity_300km_warning(void) {
  MaintenanceDisplayResult result = MaintenanceDisplay::formatDistance(300.0f);
  TEST_ASSERT_EQUAL(Severity::WARNING, result.severity);
}

void test_chain_severity_399km_warning(void) {
  MaintenanceDisplayResult result = MaintenanceDisplay::formatDistance(399.9f);
  TEST_ASSERT_EQUAL(Severity::WARNING, result.severity);
}

void test_chain_severity_400km_critical(void) {
  MaintenanceDisplayResult result = MaintenanceDisplay::formatDistance(400.0f);
  TEST_ASSERT_EQUAL(Severity::CRITICAL, result.severity);
}

// --- formatDistance tests ---

void test_format_distance_normal(void) {
  MaintenanceDisplayResult result = MaintenanceDisplay::formatDistance(123.4f);
  TEST_ASSERT_EQUAL_STRING("123 km", result.text);
}

void test_format_distance_zero(void) {
  MaintenanceDisplayResult result = MaintenanceDisplay::formatDistance(0.0f);
  TEST_ASSERT_EQUAL_STRING("0 km", result.text);
}

void test_format_distance_negative(void) {
  MaintenanceDisplayResult result = MaintenanceDisplay::formatDistance(-5.0f);
  TEST_ASSERT_EQUAL_STRING("0 km", result.text);
}

void test_format_distance_large(void) {
  MaintenanceDisplayResult result = MaintenanceDisplay::formatDistance(1234.5f);
  TEST_ASSERT_EQUAL_STRING("1234 km", result.text);
}

void test_format_distance_rounds_down(void) {
  MaintenanceDisplayResult result = MaintenanceDisplay::formatDistance(99.9f);
  TEST_ASSERT_EQUAL_STRING("99 km", result.text);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_no_epoch_shows_hours);
  RUN_TEST(test_no_epoch_zero_hours);
  RUN_TEST(test_epoch_zero_days);
  RUN_TEST(test_epoch_three_days);
  RUN_TEST(test_epoch_seven_days);
  RUN_TEST(test_epoch_over_seven_days_shows_date);
  RUN_TEST(test_epoch_date_format_december);
  RUN_TEST(test_epoch_one_day);
  RUN_TEST(test_tire_severity_0_days_normal);
  RUN_TEST(test_tire_severity_6_days_normal);
  RUN_TEST(test_tire_severity_7_days_warning);
  RUN_TEST(test_tire_severity_13_days_warning);
  RUN_TEST(test_tire_severity_14_days_critical);
  RUN_TEST(test_chain_severity_0km_normal);
  RUN_TEST(test_chain_severity_299km_normal);
  RUN_TEST(test_chain_severity_300km_warning);
  RUN_TEST(test_chain_severity_399km_warning);
  RUN_TEST(test_chain_severity_400km_critical);
  RUN_TEST(test_format_distance_normal);
  RUN_TEST(test_format_distance_zero);
  RUN_TEST(test_format_distance_negative);
  RUN_TEST(test_format_distance_large);
  RUN_TEST(test_format_distance_rounds_down);
  UNITY_END();
  return 0;
}
