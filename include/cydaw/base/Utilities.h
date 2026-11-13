#pragma once
#include "Text.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <random>
namespace cydaw::base {
template <class T> constexpr T limit(T low, T high, T value) {
  return std::clamp(value, low, high);
}
template <class T, class U>
constexpr bool isPositiveAndBelow(T value, U upper) {
  return value >= 0 && value < upper;
}
template <class T> struct MathConstants {
  static constexpr T pi = std::numbers::pi_v<T>, twoPi = T(2) * pi,
                     halfPi = pi / T(2);
};
// Random version-4 identifier for application graph assets, independent of
// SDKs.
class Uuid {
  Text value_;

public:
  Uuid() {
    std::random_device source;
    static constexpr char hex[] = "0123456789abcdef";
    std::array<unsigned char, 16> bytes{};
    for (auto &v : bytes)
      v = static_cast<unsigned char>(source());
    bytes[6] = (bytes[6] & 15) | 64;
    bytes[8] = (bytes[8] & 63) | 128;
    for (auto v : bytes) {
      value_ += hex[v >> 4];
      value_ += hex[v & 15];
    }
  }
  const Text &toString() const { return value_; }
};
} // namespace cydaw::base
