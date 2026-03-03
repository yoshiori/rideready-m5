#include "rain_ride_detector.h"

#include <cstring>

bool RainRideDetector::isOutdoorRide(const char* activityType) {
  return strcmp(activityType, "Ride") == 0 ||
         strcmp(activityType, "EBikeRide") == 0 ||
         strcmp(activityType, "GravelRide") == 0 ||
         strcmp(activityType, "MountainBikeRide") == 0;
}

Severity RainRideDetector::applySeverityOverride(Severity base, bool rainRide) {
  if (rainRide) return Severity::CRITICAL;
  return base;
}
