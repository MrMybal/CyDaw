#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

namespace cydaw::engine {

// A radix-2 complex FFT of one size: its twiddles and bit-reversed order made
// once (prepare, off the audio thread), then forward and inverse in place,
// nothing allocated. The inverse is scaled: forward then inverse gives the
// data back.
class Fft {
public:
    void prepare(const std::size_t size)
    {
        size_ = 1;
        bits_ = 0;
        while (size_ < size) {
            size_ <<= 1;
            ++bits_;
        }
        twiddles_.resize(size_ / 2);
        for (std::size_t k = 0; k < size_ / 2; ++k)
            twiddles_[k] = std::polar(1.0f, static_cast<float>(-2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(size_)));
        reversed_.resize(size_);
        for (std::size_t i = 0; i < size_; ++i) {
            std::uint32_t r = 0;
            for (unsigned b = 0; b < bits_; ++b)
                if ((i >> b) & 1u)
                    r |= 1u << (bits_ - 1 - b);
            reversed_[i] = r;
        }
    }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    void forward(std::complex<float>* data) const noexcept { transform(data, false); }
    void inverse(std::complex<float>* data) const noexcept
    {
        transform(data, true);
        const auto scale = 1.0f / static_cast<float>(size_);
        for (std::size_t i = 0; i < size_; ++i)
            data[i] *= scale;
    }

private:
    void transform(std::complex<float>* data, const bool inverse) const noexcept
    {
        for (std::size_t i = 0; i < size_; ++i)
            if (i < reversed_[i])
                std::swap(data[i], data[reversed_[i]]);
        for (std::size_t length = 2; length <= size_; length <<= 1) {
            const auto half = length / 2;
            const auto step = size_ / length;
            for (std::size_t start = 0; start < size_; start += length)
                for (std::size_t k = 0; k < half; ++k) {
                    auto w = twiddles_[k * step];
                    if (inverse)
                        w = std::conj(w);
                    const auto even = data[start + k];
                    const auto odd = data[start + k + half] * w;
                    data[start + k] = even + odd;
                    data[start + k + half] = even - odd;
                }
        }
    }

    std::size_t size_ { 1 };
    unsigned bits_ {};
    std::vector<std::complex<float>> twiddles_;
    std::vector<std::uint32_t> reversed_;
};

} // namespace cydaw::engine
