#include <cydaw/engine/AudioAnalysis.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace cydaw::engine {

void computeLogSpectrum(const AudioAnalysisFrame& frame, const double minimumHz,
                        const double maximumHz, float* const bins,
                        const std::size_t count)
{
    if (bins == nullptr || count == 0)
        return;
    const auto samples = std::min<std::size_t>(frame.waveformCount,
                                               frame.waveform.size());
    if (samples < 8 || frame.sampleRate <= 0.0f) {
        std::fill_n(bins, count, 0.0f);
        return;
    }
    // Hann, and the mean removed: a DC offset would otherwise sit in the
    // lowest bins and read as bass. The window is rebuilt only when the
    // length changes, which is almost never.
    thread_local std::vector<double> window;
    if (window.size() != samples) {
        window.resize(samples);
        for (std::size_t index = 0; index < samples; ++index)
            window[index] = 0.5 - 0.5 * std::cos(
                2.0 * std::numbers::pi * static_cast<double>(index)
                / static_cast<double>(samples - 1));
    }
    thread_local std::vector<double> windowed;
    windowed.resize(samples);
    double mean {};
    for (std::size_t index = 0; index < samples; ++index)
        mean += frame.waveform[index];
    mean /= static_cast<double>(samples);
    for (std::size_t index = 0; index < samples; ++index)
        windowed[index] = (static_cast<double>(frame.waveform[index]) - mean)
                          * window[index];

    const auto rate = static_cast<double>(frame.sampleRate);
    const auto nyquist = rate * 0.5;
    for (std::size_t bin = 0; bin < count; ++bin) {
        const auto position = count == 1
            ? 0.0 : static_cast<double>(bin) / static_cast<double>(count - 1);
        const auto frequency = minimumHz * std::pow(maximumHz / minimumHz,
                                                    position);
        if (frequency >= nyquist) {
            bins[bin] = 0.0f;
            continue;
        }
        const auto omega = 2.0 * std::numbers::pi * frequency / rate;
        const auto coefficient = 2.0 * std::cos(omega);
        double previous {};
        double beforePrevious {};
        for (std::size_t index = 0; index < samples; ++index) {
            const auto current = windowed[index] + coefficient * previous
                                 - beforePrevious;
            beforePrevious = previous;
            previous = current;
        }
        const auto power = previous * previous + beforePrevious * beforePrevious
                           - coefficient * previous * beforePrevious;
        // A full-scale sine under a Hann window sums to samples / 4 in
        // magnitude; scaled so that sine reads as zero decibels.
        const auto magnitude = 4.0 * std::sqrt(std::max(0.0, power))
                               / static_cast<double>(samples);
        const auto decibels = magnitude > 1.0e-8 ? 20.0 * std::log10(magnitude)
                                                 : -80.0;
        bins[bin] = static_cast<float>(std::clamp((decibels + 80.0) / 80.0,
                                                  0.0, 1.0));
    }
}

} // namespace cydaw::engine
