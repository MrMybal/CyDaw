#pragma once

#include <complex>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Convolution with an impulse response (a cabinet's, a room's), as loaded
// from a file: no latency, nothing allocated once prepared.

namespace cydaw::engine {

// A response as it was recorded: its rate, one or two channels.
struct ImpulseResponse {
    std::string name;
    double sampleRate { 48'000.0 };
    std::vector<std::vector<float>> channels;

    [[nodiscard]] std::size_t length() const noexcept { return channels.empty() ? 0 : channels.front().size(); }
};
using ImpulseResponseRef = std::shared_ptr<const ImpulseResponse>;

// The longest response kept: two seconds at 48 kHz (a cabinet's is a few
// hundred milliseconds, a room's a second or two).
inline constexpr std::size_t maximumImpulseSamples = 96'000;

// A response's channels read again at `rate` (windowed sinc, low-passed
// when read slower), at most maximumImpulseSamples long, scaled together so
// white noise keeps its power through them. Empty when it is silent.
[[nodiscard]] std::vector<std::vector<float>> impulseAtRate(const ImpulseResponse& response, double rate);

// One channel convolved with a fixed response. Its first `block` samples
// are applied directly (no delay); the rest in blocks of `block` by FFT
// (overlap-save), each block's result played during the next one, which is
// exactly the delay that part of the response starts at.
class Convolver {
public:
    static constexpr std::size_t block = 128;

    // Allocates: not on the audio thread.
    void prepare(const std::vector<float>& response);
    void reset() noexcept;
    [[nodiscard]] bool empty() const noexcept { return head_.empty(); }
    [[nodiscard]] float process(float input) noexcept;

private:
    void runBlock() noexcept;

    std::vector<float> head_;          // the first `block` taps
    std::vector<float> history_;       // the last inputs, twice over (no wrap)
    std::size_t historyAt_ {};
    std::vector<std::vector<std::complex<float>>> partitions_; // the rest, by block, as spectra
    std::vector<std::vector<std::complex<float>>> spectra_;    // the inputs' last blocks, as spectra
    std::size_t newest_ {};
    std::vector<float> frame_;         // the last two blocks of input
    std::vector<std::complex<float>> work_;
    std::vector<std::complex<float>> sum_;
    std::vector<float> tail_;          // the rest's output for this block
    std::size_t at_ {};
    std::vector<std::complex<float>> twiddles_;
    std::vector<std::size_t> reversed_;
};

} // namespace cydaw::engine
