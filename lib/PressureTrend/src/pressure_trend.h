#ifndef PRESSURE_TREND_H
#define PRESSURE_TREND_H

#include <stddef.h>

enum class TrendDirection { TREND_RISING, TREND_STABLE, TREND_FALLING };

class PressureTrend {
public:
  static const size_t BUFFER_SIZE = 6;
  static constexpr float DEFAULT_THRESHOLD = 0.5f;

  explicit PressureTrend(float threshold = DEFAULT_THRESHOLD);

  void addSample(float pressure_hpa);
  TrendDirection direction() const;
  size_t count() const;
  void reset();

private:
  float buffer_[BUFFER_SIZE];
  size_t head_;
  size_t count_;
  float threshold_;
};

#endif
