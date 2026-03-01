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
  UNITY_END();
  return 0;
}
