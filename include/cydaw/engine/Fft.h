#pragma once
#include <cassert>
#include <complex>
#include <cstddef>
#include <cydsp/processors/ComplexFft.h>
#include <new>
#include <stdexcept>

namespace cydaw::engine {
// Host size rounding/lifecycle; CyDSP owns the prepared FFT and all transforms.
class Fft {
public:
  Fft() = default;
  Fft(const Fft& other)
  {
      if (other.fft_.size())
          prepare(other.size());
  }
  Fft& operator=(const Fft& other)
  {
      if (this != &other)
          prepare(other.size());
      return *this;
  }
  Fft(Fft&&) noexcept = default;
  Fft& operator=(Fft&&) noexcept = default;
  void prepare(std::size_t requested)
  {
      if (requested > (std::size_t(1) << 24))
          throw std::length_error("FFT size");
      std::size_t size = 1;
      while (size < requested)
          size <<= 1;
      const auto status = fft_.prepare(size);
      if (status == cydsp::Status::allocation_failed)
          throw std::bad_alloc();
      if (status != cydsp::Status::ok)
          throw std::invalid_argument("FFT preparation");
  }
  [[nodiscard]] std::size_t size() const noexcept { return fft_.size() ? fft_.size() : 1; }
  void forward(std::complex<float>* data) const noexcept { transform(data, cydsp::FftDirection::forward); }
  void inverse(std::complex<float>* data) const noexcept { transform(data, cydsp::FftDirection::inverse); }

private:
  void transform(std::complex<float>* data, cydsp::FftDirection direction) const noexcept
  {
      if (!fft_.size())
          return; // historical unprepared size-one identity
      const auto status = fft_.process({data, fft_.size()}, {data, fft_.size()}, direction);
      assert(status == cydsp::Status::ok);
      (void)status;
  }
  cydsp::ComplexFft<float> fft_;
};
} // namespace cydaw::engine
