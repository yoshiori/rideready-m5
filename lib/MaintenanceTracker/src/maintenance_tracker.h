#ifndef MAINTENANCE_TRACKER_H
#define MAINTENANCE_TRACKER_H

#include <stdint.h>
#include <ctime>

class MaintenanceTracker {
public:
  MaintenanceTracker();

  void reset(uint64_t currentUptimeMs);
  uint32_t elapsedHours(uint64_t currentUptimeMs) const;
  uint64_t resetUptimeMs() const;

  void setResetEpoch(time_t epoch);
  time_t resetEpoch() const;
  bool hasEpoch() const;

private:
  uint64_t resetUptimeMs_;
  time_t resetEpoch_;
};

#endif
