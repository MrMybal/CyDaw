#include <cydaw/engine/Convolver.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace cydaw::engine {

namespace {

// In place, radix 2, the table of twiddles and the bit-reversed order made
// beforehand (nothing allocated).
double blackman(const double x) noexcept
{
    // x in [-1, 1].
    if (std::abs(x) >= 1.0)
        return 0.0;
    const auto phase = std::numbers::pi * (x + 1.0);
    return 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
}

} // namespace

std::vector<std::vector<float>> impulseAtRate(const ImpulseResponse& response, const double rate)
{
    std::vector<std::vector<float>> channels;
    if (response.channels.empty() || response.length() == 0 || response.sampleRate <= 0.0 || rate <= 0.0)
        return channels;
    const auto ratio = response.sampleRate / rate; // source samples per output sample
    for (const auto& source : response.channels) {
        std::vector<float> read;
        if (std::abs(ratio - 1.0) < 1.0e-6) {
            read.assign(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(std::min(source.size(), maximumImpulseSamples)));
        } else {
            const auto length = std::min(maximumImpulseSamples,
                                         static_cast<std::size_t>(std::ceil(static_cast<double>(source.size()) / ratio)));
            read.resize(length);
            const auto cutoff = std::min(1.0, 1.0 / ratio);
            constexpr double zeroCrossings = 16.0;
            const auto reach = zeroCrossings / cutoff;
            for (std::size_t i = 0; i < length; ++i) {
                const auto position = static_cast<double>(i) * ratio;
                const auto first = static_cast<std::ptrdiff_t>(std::ceil(position - reach));
                const auto last = static_cast<std::ptrdiff_t>(std::floor(position + reach));
                auto sum = 0.0;
                for (auto n = std::max<std::ptrdiff_t>(0, first);
                     n <= std::min<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(source.size()) - 1, last); ++n) {
                    const auto distance = position - static_cast<double>(n);
                    const auto argument = cutoff * distance;
                    const auto sinc = std::abs(argument) < 1.0e-9 ? 1.0
                                                                  : std::sin(std::numbers::pi * argument) / (std::numbers::pi * argument);
                    sum += static_cast<double>(source[static_cast<std::size_t>(n)]) * cutoff * sinc * blackman(distance / reach);
                }
                read[i] = static_cast<float>(sum);
            }
        }
        channels.push_back(std::move(read));
    }
    auto energy = 0.0;
    for (const auto& channel : channels)
        for (const auto sample : channel)
            energy += static_cast<double>(sample) * sample;
    energy /= static_cast<double>(channels.size());
    if (energy < 1.0e-12)
        return {};
    const auto scale = static_cast<float>(1.0 / std::sqrt(energy));
    for (auto& channel : channels)
        for (auto& sample : channel)
            sample *= scale;
    return channels;
}

void Convolver::prepare(const std::vector<float>& response)
{
    constexpr auto size = 2 * block;
    head_.assign(block, 0.0f);
    std::copy_n(response.begin(), std::min(block, response.size()), head_.begin());
    history_.assign(2 * block, 0.0f);
    historyAt_ = 0;
    if (fft_.prepare(size) != cydsp::Status::ok)
        throw std::runtime_error("CyDSP FFT preparation failed");
    work_.assign(size, {});
    sum_.assign(block + 1, {});
    frame_.assign(size, 0.0f);
    tail_.assign(block, 0.0f);
    at_ = 0;
    // The rest of the response, block by block, as spectra.
    partitions_.clear();
    for (auto start = block; start < response.size(); start += block) {
        std::fill(work_.begin(), work_.end(), std::complex<float> {});
        for (std::size_t i = 0; i < block && start + i < response.size(); ++i)
            work_[i] = response[start + i];
        (void) fft_.process(work_, work_, cydsp::FftDirection::forward);
        partitions_.emplace_back(work_.begin(), work_.begin() + static_cast<std::ptrdiff_t>(block + 1));
    }
    spectra_.assign(partitions_.size(), std::vector<std::complex<float>>(block + 1));
    newest_ = 0;
}

void Convolver::reset() noexcept
{
    std::fill(history_.begin(), history_.end(), 0.0f);
    std::fill(frame_.begin(), frame_.end(), 0.0f);
    std::fill(tail_.begin(), tail_.end(), 0.0f);
    for (auto& spectrum : spectra_)
        std::fill(spectrum.begin(), spectrum.end(), std::complex<float> {});
    at_ = 0;
}

float Convolver::process(const float input) noexcept
{
    if (head_.empty())
        return input;
    // The response's head, directly: the last `block` inputs, newest first.
    historyAt_ = (historyAt_ + 1) % block;
    history_[historyAt_] = input;
    history_[historyAt_ + block] = input;
    const auto* newest = history_.data() + historyAt_ + block;
    auto sum = 0.0f;
    for (std::size_t tap = 0; tap < block; ++tap)
        sum += head_[tap] * newest[-static_cast<std::ptrdiff_t>(tap)];
    const auto output = sum + tail_[at_];
    frame_[block + at_] = input;
    if (++at_ == block) {
        runBlock();
        at_ = 0;
    }
    return output;
}

void Convolver::runBlock() noexcept
{
    constexpr auto size = 2 * block;
    if (!partitions_.empty()) {
        for (std::size_t i = 0; i < size; ++i)
            work_[i] = frame_[i];
        (void) fft_.process(work_, work_, cydsp::FftDirection::forward);
        const auto count = partitions_.size();
        newest_ = (newest_ + count - 1) % count;
        std::copy_n(work_.begin(), block + 1, spectra_[newest_].begin());
        std::fill(sum_.begin(), sum_.end(), std::complex<float> {});
        for (std::size_t m = 0; m < count; ++m) {
            const auto& input = spectra_[(newest_ + m) % count];
            const auto& filter = partitions_[m];
            for (std::size_t k = 0; k <= block; ++k)
                sum_[k] += input[k] * filter[k];
        }
        // Back to time: the conjugate's forward transform, conjugated.
        for (std::size_t k = 0; k <= block; ++k)
            work_[k] = std::conj(sum_[k]);
        for (std::size_t k = 1; k < block; ++k)
            work_[size - k] = sum_[k];
        (void) fft_.process(work_, work_, cydsp::FftDirection::forward);
        const auto scale = 1.0f / static_cast<float>(size);
        for (std::size_t i = 0; i < block; ++i)
            tail_[i] = work_[block + i].real() * scale;
    }
    std::copy_n(frame_.begin() + static_cast<std::ptrdiff_t>(block), block, frame_.begin());
}

} // namespace cydaw::engine
