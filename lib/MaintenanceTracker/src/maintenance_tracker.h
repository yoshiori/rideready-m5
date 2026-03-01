#ifndef MAINTENANCE_TRACKER_H
#define MAINTENANCE_TRACKER_H

#include <stdint.h>

class MaintenanceTracker {
public:
  MaintenanceTracker();

  void reset(uint64_t currentUptimeMs);
  uint32_t elapsedHours(uint64_t currentUptimeMs) const;
  uint64_t resetUptimeMs() const;

private:
  uint64_t resetUptimeMs_;
};

#endif
