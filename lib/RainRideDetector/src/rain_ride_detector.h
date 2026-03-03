#ifndef RAIN_RIDE_DETECTOR_H
#define RAIN_RIDE_DETECTOR_H

#include "maintenance_display.h"

class RainRideDetector {
public:
  static bool isOutdoorRide(const char* activityType);
  static Severity applySeverityOverride(Severity base, bool rainRide);
};

#endif
