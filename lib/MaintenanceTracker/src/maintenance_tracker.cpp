#include "maintenance_tracker.h"

static constexpr uint64_t MS_PER_HOUR = 3600ULL * 1000ULL;

MaintenanceTracker::MaintenanceTracker() : resetUptimeMs_(0), resetEpoch_(0) {}

void MaintenanceTracker::reset(uint64_t currentUptimeMs) {
  resetUptimeMs_ = currentUptimeMs;
  resetEpoch_ = 0;
}

void MaintenanceTracker::setResetEpoch(time_t epoch) { resetEpoch_ = epoch; }

time_t MaintenanceTracker::resetEpoch() const { return resetEpoch_; }

bool MaintenanceTracker::hasEpoch() const { return resetEpoch_ != 0; }

uint32_t MaintenanceTracker::elapsedHours(uint64_t currentUptimeMs) const {
  if (currentUptimeMs <= resetUptimeMs_) {
    return 0;
  }
  return static_cast<uint32_t>((currentUptimeMs - resetUptimeMs_) / MS_PER_HOUR);
}

uint64_t MaintenanceTracker::resetUptimeMs() const { return resetUptimeMs_; }
