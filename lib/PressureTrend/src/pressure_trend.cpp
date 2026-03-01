#include "pressure_trend.h"

PressureTrend::PressureTrend(float threshold)
    : head_(0), count_(0), threshold_(threshold) {}

void PressureTrend::addSample(float pressure_hpa) {
  buffer_[head_] = pressure_hpa;
  head_ = (head_ + 1) % BUFFER_SIZE;
  if (count_ < BUFFER_SIZE) {
    count_++;
  }
}

TrendDirection PressureTrend::direction() const {
  if (count_ < BUFFER_SIZE) {
    return TrendDirection::TREND_STABLE;
  }

  const size_t half = BUFFER_SIZE / 2;
  float first_avg = 0.0f;
  float second_avg = 0.0f;

  // head_ points to the oldest slot (next to be overwritten)
  // Oldest sample is at index head_, newest is at (head_ - 1 + BUFFER_SIZE) %
  // BUFFER_SIZE First half = oldest half, Second half = newest half
  for (size_t i = 0; i < half; i++) {
    first_avg += buffer_[(head_ + i) % BUFFER_SIZE];
  }
  for (size_t i = half; i < BUFFER_SIZE; i++) {
    second_avg += buffer_[(head_ + i) % BUFFER_SIZE];
  }

  first_avg /= half;
  second_avg /= (BUFFER_SIZE - half);

  float diff = second_avg - first_avg;

  if (diff > threshold_) {
    return TrendDirection::TREND_RISING;
  } else if (diff < -threshold_) {
    return TrendDirection::TREND_FALLING;
  }
  return TrendDirection::TREND_STABLE;
}

size_t PressureTrend::count() const { return count_; }

void PressureTrend::reset() {
  head_ = 0;
  count_ = 0;
}
