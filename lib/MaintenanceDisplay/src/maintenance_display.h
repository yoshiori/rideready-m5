#ifndef MAINTENANCE_DISPLAY_H
#define MAINTENANCE_DISPLAY_H

#include <stdint.h>
#include <ctime>

enum class Severity { NORMAL, WARNING, CRITICAL };

struct MaintenanceDisplayResult {
  char text[16];
  Severity severity;
};

class MaintenanceDisplay {
public:
  // Format maintenance display text
  // resetEpoch: Unix timestamp of last reset (0 if NTP not synced)
  // currentEpoch: current Unix timestamp (0 if NTP not synced)
  // elapsedMs: elapsed milliseconds since reset (millis-based fallback)
  static MaintenanceDisplayResult format(time_t resetEpoch, time_t currentEpoch,
                                         uint64_t elapsedMs);
  static MaintenanceDisplayResult formatDistance(float distanceKm);
};

#endif
