#include <unity.h>
#include <ctime>

#include "maintenance_tracker.h"

static const uint64_t MS_PER_HOUR = 3600ULL * 1000ULL;

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
  TEST_ASSERT_EQUAL_UINT32(3, tracker.elapsedHours(3 * MS_PER_HOUR));
}

void test_elapsed_hours_truncates(void) {
  MaintenanceTracker tracker;
  // 2.9 hours should truncate to 2
  uint64_t almostThreeHoursMs = 2 * MS_PER_HOUR + 3599ULL * 1000ULL;
  TEST_ASSERT_EQUAL_UINT32(2, tracker.elapsedHours(almostThreeHoursMs));
}

void test_reset_clears_elapsed(void) {
  MaintenanceTracker tracker;
  uint64_t fiveHoursMs = 5 * MS_PER_HOUR;

  // After 5 hours, reset
  tracker.reset(fiveHoursMs);

  // Immediately after reset, elapsed should be 0
  TEST_ASSERT_EQUAL_UINT32(0, tracker.elapsedHours(fiveHoursMs));
}

void test_elapsed_after_reset(void) {
  MaintenanceTracker tracker;
  uint64_t fiveHoursMs = 5 * MS_PER_HOUR;
  uint64_t eightHoursMs = 8 * MS_PER_HOUR;

  // Reset at 5 hours
  tracker.reset(fiveHoursMs);

  // At 8 hours, elapsed should be 3
  TEST_ASSERT_EQUAL_UINT32(3, tracker.elapsedHours(eightHoursMs));
}

void test_reset_uptime_after_reset(void) {
  MaintenanceTracker tracker;
  uint64_t tenHoursMs = 10 * MS_PER_HOUR;

  tracker.reset(tenHoursMs);
  TEST_ASSERT_EQUAL_UINT64(tenHoursMs, tracker.resetUptimeMs());
}

void test_restore_from_saved_value(void) {
  // Simulate restoring from NVS: create tracker and reset to a saved uptime
  MaintenanceTracker tracker;
  uint64_t savedResetUptime = 20 * MS_PER_HOUR;  // saved at 20h
  uint64_t currentUptime = 25 * MS_PER_HOUR;     // now at 25h

  tracker.reset(savedResetUptime);
  TEST_ASSERT_EQUAL_UINT32(5, tracker.elapsedHours(currentUptime));
}

// --- Epoch tests ---

void test_initial_has_no_epoch(void) {
  MaintenanceTracker tracker;
  TEST_ASSERT_FALSE(tracker.hasEpoch());
  TEST_ASSERT_EQUAL(0, tracker.resetEpoch());
}

void test_set_reset_epoch(void) {
  MaintenanceTracker tracker;
  time_t epoch = 1709280000;  // 2024-03-01 12:00:00 UTC
  tracker.setResetEpoch(epoch);
  TEST_ASSERT_TRUE(tracker.hasEpoch());
  TEST_ASSERT_EQUAL(epoch, tracker.resetEpoch());
}

void test_reset_clears_epoch(void) {
  MaintenanceTracker tracker;
  tracker.setResetEpoch(1709280000);
  TEST_ASSERT_TRUE(tracker.hasEpoch());

  // reset() should clear the epoch
  tracker.reset(5 * MS_PER_HOUR);
  TEST_ASSERT_FALSE(tracker.hasEpoch());
  TEST_ASSERT_EQUAL(0, tracker.resetEpoch());
}

void test_epoch_survives_set_after_reset(void) {
  MaintenanceTracker tracker;
  // Simulate: reset() then setResetEpoch()
  tracker.reset(5 * MS_PER_HOUR);
  time_t epoch = 1709280000;
  tracker.setResetEpoch(epoch);
  TEST_ASSERT_TRUE(tracker.hasEpoch());
  TEST_ASSERT_EQUAL(epoch, tracker.resetEpoch());
}

void test_multiple_resets(void) {
  MaintenanceTracker tracker;
  uint64_t t1 = 2 * MS_PER_HOUR;
  uint64_t t2 = 7 * MS_PER_HOUR;
  uint64_t t3 = 10 * MS_PER_HOUR;

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
  RUN_TEST(test_initial_has_no_epoch);
  RUN_TEST(test_set_reset_epoch);
  RUN_TEST(test_reset_clears_epoch);
  RUN_TEST(test_epoch_survives_set_after_reset);
  UNITY_END();
  return 0;
}
