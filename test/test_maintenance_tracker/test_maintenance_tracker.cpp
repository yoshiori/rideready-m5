#include <unity.h>

#include "maintenance_tracker.h"

void test_initial_elapsed_is_zero(void) {
  MaintenanceTracker tracker;
  // At uptime 0, elapsed should be 0
  TEST_ASSERT_EQUAL_UINT32(0, tracker.elapsedHours(0));
}

void test_initial_reset_uptime_is_zero(void) {
  MaintenanceTracker tracker;
  TEST_ASSERT_EQUAL_UINT64(0, tracker.resetUptimeMs());
}

void test_elapsed_hours_calculation(void) {
  MaintenanceTracker tracker;
  // 3 hours = 3 * 3600 * 1000 = 10,800,000 ms
  uint64_t threeHoursMs = 3ULL * 3600ULL * 1000ULL;
  TEST_ASSERT_EQUAL_UINT32(3, tracker.elapsedHours(threeHoursMs));
}

void test_elapsed_hours_truncates(void) {
  MaintenanceTracker tracker;
  // 2.9 hours should truncate to 2
  uint64_t almostThreeHoursMs = 2ULL * 3600ULL * 1000ULL + 3599ULL * 1000ULL;
  TEST_ASSERT_EQUAL_UINT32(2, tracker.elapsedHours(almostThreeHoursMs));
}

void test_reset_clears_elapsed(void) {
  MaintenanceTracker tracker;
  uint64_t fiveHoursMs = 5ULL * 3600ULL * 1000ULL;

  // After 5 hours, reset
  tracker.reset(fiveHoursMs);

  // Immediately after reset, elapsed should be 0
  TEST_ASSERT_EQUAL_UINT32(0, tracker.elapsedHours(fiveHoursMs));
}

void test_elapsed_after_reset(void) {
  MaintenanceTracker tracker;
  uint64_t fiveHoursMs = 5ULL * 3600ULL * 1000ULL;
  uint64_t eightHoursMs = 8ULL * 3600ULL * 1000ULL;

  // Reset at 5 hours
  tracker.reset(fiveHoursMs);

  // At 8 hours, elapsed should be 3
  TEST_ASSERT_EQUAL_UINT32(3, tracker.elapsedHours(eightHoursMs));
}

void test_reset_uptime_after_reset(void) {
  MaintenanceTracker tracker;
  uint64_t tenHoursMs = 10ULL * 3600ULL * 1000ULL;

  tracker.reset(tenHoursMs);
  TEST_ASSERT_EQUAL_UINT64(tenHoursMs, tracker.resetUptimeMs());
}

void test_restore_from_saved_value(void) {
  // Simulate restoring from NVS: create tracker and reset to a saved uptime
  MaintenanceTracker tracker;
  uint64_t savedResetUptime = 20ULL * 3600ULL * 1000ULL;  // saved at 20h
  uint64_t currentUptime = 25ULL * 3600ULL * 1000ULL;     // now at 25h

  tracker.reset(savedResetUptime);
  TEST_ASSERT_EQUAL_UINT32(5, tracker.elapsedHours(currentUptime));
}

void test_multiple_resets(void) {
  MaintenanceTracker tracker;
  uint64_t t1 = 2ULL * 3600ULL * 1000ULL;
  uint64_t t2 = 7ULL * 3600ULL * 1000ULL;
  uint64_t t3 = 10ULL * 3600ULL * 1000ULL;

  tracker.reset(t1);
  TEST_ASSERT_EQUAL_UINT32(5, tracker.elapsedHours(t2));

  tracker.reset(t2);
  TEST_ASSERT_EQUAL_UINT32(3, tracker.elapsedHours(t3));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_initial_elapsed_is_zero);
  RUN_TEST(test_initial_reset_uptime_is_zero);
  RUN_TEST(test_elapsed_hours_calculation);
  RUN_TEST(test_elapsed_hours_truncates);
  RUN_TEST(test_reset_clears_elapsed);
  RUN_TEST(test_elapsed_after_reset);
  RUN_TEST(test_reset_uptime_after_reset);
  RUN_TEST(test_restore_from_saved_value);
  RUN_TEST(test_multiple_resets);
  UNITY_END();
  return 0;
}
