#pragma once
#include <cydaw/base/Geometry.h>
#include <cydsp/math/VectorOps.h>
#include <span>
namespace cydaw::audio {
struct Decibels {
  template <class T> static T decibelsToGain(T db, T floor = T(-100)) {
    return db > floor ? std::pow(T(10), db / T(20)) : T(0);
  }
  template <class T> static T gainToDecibels(T gain, T floor = T(-100)) {
    return gain > T(0) ? std::max(floor, T(20) * std::log10(gain)) : floor;
  }
};
struct VectorMath {
  inline static const cydsp::VectorOps ops = [] {
    cydsp::VectorOps result;
    (void)cydsp::VectorOps::create(cydsp::CpuBackend::automatic, result);
    return result;
  }();
  template <class T> static void clear(T *out, int count) {
    if (count > 0)
      ops.fill(std::span<T>(out, std::size_t(count)), T(0));
  }
  template <class T> static void copy(T *out, const T *in, int count) {
    if (count > 0)
      (void)ops.copy(std::span<const T>(in, std::size_t(count)),
                     std::span<T>(out, std::size_t(count)));
  }
  template <class T> static void multiply(T *out, T gain, int count) {
    if (count > 0)
      (void)ops.scale(std::span<const T>(out, std::size_t(count)),
                      std::span<T>(out, std::size_t(count)), gain);
  }
  template <class T> static void multiply(T *out, const T *in, int count) {
    if (count > 0)
      (void)ops.multiply(std::span<const T>(out, std::size_t(count)),
                         std::span<const T>(in, std::size_t(count)),
                         std::span<T>(out, std::size_t(count)));
  }
  template <class T>
  static void addWithMultiply(T *out, const T *in, T gain, int count) {
    if (count > 0)
      (void)ops.addScaled(std::span<const T>(in, std::size_t(count)),
                          std::span<T>(out, std::size_t(count)), gain);
  }
  template <class T> static void add(T *out, const T *in, int count) {
    addWithMultiply(out, in, T(1), count);
  }
  template <class T>
  static base::Range<T> findMinAndMax(const T *in, int count) {
    if (count <= 0)
      return {};
    const auto pair = std::minmax_element(in, in + count);
    return {*pair.first, *pair.second};
  }
};
} // namespace cydaw::audio
