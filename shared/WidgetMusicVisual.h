#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <windows.h>

namespace widgetmusic {

inline float ClampUnit(float value) {
  if (value <= 0.0f) return 0.0f;
  if (value >= 1.0f) return 1.0f;
  return value;
}

inline float SmoothStep(float progress) {
  const float t = ClampUnit(progress);
  return t * t * (3.0f - (2.0f * t));
}

inline int InterpolateInt(int from, int to, float progress) {
  const float t = ClampUnit(progress);
  return from + static_cast<int>(std::lround(static_cast<float>(to - from) * t));
}

inline int InterpolateSmoothInt(int from, int to, float progress) {
  return InterpolateInt(from, to, SmoothStep(progress));
}

inline COLORREF MedianColor(const std::vector<COLORREF>& samples, COLORREF fallback) {
  if (samples.empty()) return fallback;

  std::vector<BYTE> red;
  std::vector<BYTE> green;
  std::vector<BYTE> blue;
  red.reserve(samples.size());
  green.reserve(samples.size());
  blue.reserve(samples.size());
  for (COLORREF sample : samples) {
    red.push_back(GetRValue(sample));
    green.push_back(GetGValue(sample));
    blue.push_back(GetBValue(sample));
  }

  const size_t middle = samples.size() / 2;
  std::nth_element(red.begin(), red.begin() + middle, red.end());
  std::nth_element(green.begin(), green.begin() + middle, green.end());
  std::nth_element(blue.begin(), blue.begin() + middle, blue.end());
  return RGB(red[middle], green[middle], blue[middle]);
}

inline int MaxChannelDelta(COLORREF a, COLORREF b) {
  return max(abs(static_cast<int>(GetRValue(a)) - static_cast<int>(GetRValue(b))),
             max(abs(static_cast<int>(GetGValue(a)) - static_cast<int>(GetGValue(b))),
                 abs(static_cast<int>(GetBValue(a)) - static_cast<int>(GetBValue(b)))));
}

}  // namespace widgetmusic
