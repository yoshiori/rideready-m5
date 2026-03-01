#include "maintenance_display.h"
#include <cstdio>

static const uint64_t MS_PER_HOUR = 3600ULL * 1000ULL;
static const int SECONDS_PER_DAY = 86400;
static const int DATE_THRESHOLD_DAYS = 7;

MaintenanceDisplayResult MaintenanceDisplay::format(time_t resetEpoch,
                                                     time_t currentEpoch,
                                                     uint64_t elapsedMs) {
  MaintenanceDisplayResult result;

  if (resetEpoch == 0 || currentEpoch == 0) {
    // NTP not synced: fallback to hours
    uint32_t hours = static_cast<uint32_t>(elapsedMs / MS_PER_HOUR);
    snprintf(result.text, sizeof(result.text), "%u h", hours);
  } else {
    int days = static_cast<int>((currentEpoch - resetEpoch) / SECONDS_PER_DAY);
    if (days < 0) days = 0;

    if (days > DATE_THRESHOLD_DAYS) {
      // Show reset date as MM/DD
      struct tm *t = localtime(&resetEpoch);
      snprintf(result.text, sizeof(result.text), "%02d/%02d",
               t->tm_mon + 1, t->tm_mday);
    } else {
      snprintf(result.text, sizeof(result.text), "%d %s", days, days == 1 ? "day" : "days");
    }
  }

  return result;
}
