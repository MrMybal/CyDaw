#pragma once
#include "Character.h"
#include "Text.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <locale>
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
template <class T, class... Rest> constexpr T maximum(T value, Rest... rest) {
  ((value = std::max(value, T(rest))), ...);
  return value;
}
template <class T, class... Rest> constexpr T minimum(T value, Rest... rest) {
  ((value = std::min(value, T(rest))), ...);
  return value;
}
template <class T> constexpr T mapRange(T value, T low, T high) {
  return low + value * (high - low);
}
template <class T>
constexpr T mapRange(T value, T inLow, T inHigh, T outLow, T outHigh) {
  return inHigh == inLow
             ? outLow
             : outLow + (value - inLow) * (outHigh - outLow) / (inHigh - inLow);
}
struct ByteOrder {
  template <class T> static constexpr T swapIfBigEndian(T value) {
    if constexpr (std::endian::native == std::endian::little)
      return value;
    else {
      using U = std::make_unsigned_t<T>;
      U in = static_cast<U>(value), out = 0;
      for (unsigned i = 0; i < sizeof(T); ++i) {
        out = (out << 8) | (in & 255);
        in >>= 8;
      }
      return static_cast<T>(out);
    }
  }
};
class Random {
  std::mt19937_64 engine_;

public:
  Random()
      : engine_((std::uint64_t(std::random_device{}()) << 32) ^
                std::random_device{}()) {}
  explicit Random(std::int64_t seed) : engine_(std::uint64_t(seed)) {}
  void setSeed(std::int64_t seed) { engine_.seed(std::uint64_t(seed)); }
  int nextInt() {
    return std::bit_cast<std::int32_t>(std::uint32_t(engine_()));
  }
  int nextInt(int maximum) {
    return maximum > 0
               ? std::uniform_int_distribution<int>(0, maximum - 1)(engine_)
               : 0;
  }
  float nextFloat() { return std::generate_canonical<float, 24>(engine_); }
  double nextDouble() { return std::generate_canonical<double, 53>(engine_); }
  bool nextBool() { return (engine_() & 1) != 0; }
  static Random &getSystemRandom() {
    thread_local Random value;
    return value;
  }
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
  friend bool operator==(const Uuid &, const Uuid &) = default;
};
} // namespace cydaw::base
