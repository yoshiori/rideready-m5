#include "maintenance_display.h"
#include <cstdio>

static const uint64_t MS_PER_HOUR = 3600ULL * 1000ULL;
static const int SECONDS_PER_DAY = 86400;
static const int DATE_THRESHOLD_DAYS = 7;
static const int WARNING_DAYS = 7;
static const int CRITICAL_DAYS = 14;
static const int WARNING_KM = 300;
static const int CRITICAL_KM = 400;
static const int TIRE_CHANGE_WARNING_KM = 3000;
static const int TIRE_CHANGE_CRITICAL_KM = 5000;

MaintenanceDisplayResult MaintenanceDisplay::format(time_t resetEpoch,
                                                     time_t currentEpoch,
                                                     uint64_t elapsedMs) {
  MaintenanceDisplayResult result;

  int severityDays = 0;

  if (resetEpoch == 0 || currentEpoch == 0) {
    // NTP not synced: fallback to hours
    uint32_t hours = static_cast<uint32_t>(elapsedMs / MS_PER_HOUR);
    snprintf(result.text, sizeof(result.text), "%u h", hours);
    severityDays = static_cast<int>(hours / 24);
  } else {
    int days = static_cast<int>((currentEpoch - resetEpoch) / SECONDS_PER_DAY);
    if (days < 0) days = 0;
    severityDays = days;

    if (days > DATE_THRESHOLD_DAYS) {
      // Show reset date as MM/DD
      struct tm *t = localtime(&resetEpoch);
      snprintf(result.text, sizeof(result.text), "%02d/%02d",
               t->tm_mon + 1, t->tm_mday);
    } else {
      snprintf(result.text, sizeof(result.text), "%d %s", days, days == 1 ? "day" : "days");
    }
  }

  if (severityDays >= CRITICAL_DAYS) {
    result.severity = Severity::CRITICAL;
  } else if (severityDays >= WARNING_DAYS) {
    result.severity = Severity::WARNING;
  } else {
    result.severity = Severity::NORMAL;
  }

  return result;
}

MaintenanceDisplayResult MaintenanceDisplay::formatDistance(float distanceKm) {
  MaintenanceDisplayResult result;
  int km = static_cast<int>(distanceKm);
  if (km < 0) km = 0;
  snprintf(result.text, sizeof(result.text), "%d km", km);

  if (km >= CRITICAL_KM) {
    result.severity = Severity::CRITICAL;
  } else if (km >= WARNING_KM) {
    result.severity = Severity::WARNING;
  } else {
    result.severity = Severity::NORMAL;
  }

  return result;
}

MaintenanceDisplayResult MaintenanceDisplay::formatTireChangeDistance(float distanceKm) {
  MaintenanceDisplayResult result;
  int km = static_cast<int>(distanceKm);
  if (km < 0) km = 0;
  snprintf(result.text, sizeof(result.text), "%d km", km);

  if (km >= TIRE_CHANGE_CRITICAL_KM) {
    result.severity = Severity::CRITICAL;
  } else if (km >= TIRE_CHANGE_WARNING_KM) {
    result.severity = Severity::WARNING;
  } else {
    result.severity = Severity::NORMAL;
  }

  return result;
}
