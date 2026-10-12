// The wavetables the synth family shares (Colosse, Mosaïque): eight sets of
// eight frames, each made once without aliasing at eleven levels of
// harmonics, and the eight ways of bending the phase they are read through.

#include "SynthTables.h"

#include "SynthParts.h"

#include <cydaw/engine/Fft.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

namespace cydaw::engine::synth::detail::tables {

namespace {

using parts::kPi;

constexpr int tableSize = size;
constexpr int tableLevels = levels; // the n-th keeps 1024 >> n harmonics
constexpr int tableFrames = frames;
constexpr int tableSets = sets;

double triangleOf(const double p)
{
    return p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0;
}

// A frame drawn in time (the sets that are shapes); 0 for the ones made of
// harmonics.
double frameShape(const int set, const int frame, const double p)
{
    const auto sine = std::sin(2.0 * kPi * p);
    switch (set) {
    case 0: {
        const auto triangle = triangleOf(p);
        const auto saw = p < 0.5 ? 2.0 * p : 2.0 * p - 2.0;
        const auto square = p < 0.5 ? 1.0 : -1.0;
        switch (frame) {
        case 0: return sine;
        case 1: return 0.5 * (sine + triangle);
        case 2: return triangle;
        case 3: return 0.5 * (triangle + saw);
        case 4: return saw;
        case 5: return 0.5 * (saw + square);
        case 6: return square;
        default: return p < 0.2 ? 1.0 : -1.0;
        }
    }
    case 4: {
        const auto gain = 1.0 + static_cast<double>(frame);
        return std::sin(gain * kPi * 0.5 * sine);
    }
    case 5: {
        static constexpr std::array<double, 8> steps { 64.0, 32.0, 16.0, 12.0, 8.0, 6.0, 4.0, 3.0 };
        const auto count = steps[static_cast<std::size_t>(frame)];
        return std::floor(p * count) / count * 2.0 - 1.0;
    }
    case 7: {
        static constexpr std::array<double, 8> ratios { 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.5, 7.0 };
        const auto x = p * ratios[static_cast<std::size_t>(frame)];
        return 2.0 * (x - std::floor(x)) - 1.0;
    }
    default: return 0.0;
    }
}

// A frame made of harmonics: the n-th's amplitude.
double frameHarmonic(const int set, const int frame, const int n)
{
    const auto h = static_cast<double>(n);
    switch (set) {
    case 1: {
        static constexpr std::array<int, 8> counts { 1, 2, 3, 5, 8, 13, 24, 48 };
        return n <= counts[static_cast<std::size_t>(frame)] ? 1.0 / h : 0.0;
    }
    case 2: {
        // Vowels sung on a low note: three formants each.
        static constexpr std::array<std::array<double, 3>, 8> formants { { { 800.0, 1'150.0, 2'900.0 },
                                                                           { 650.0, 1'500.0, 2'800.0 },
                                                                           { 400.0, 1'900.0, 2'700.0 },
                                                                           { 280.0, 2'200.0, 2'950.0 },
                                                                           { 300.0, 1'750.0, 2'250.0 },
                                                                           { 450.0, 800.0, 2'830.0 },
                                                                           { 325.0, 700.0, 2'530.0 },
                                                                           { 500.0, 1'450.0, 2'450.0 } } };
        const auto hz = h * 110.0;
        auto weight = 0.0;
        const auto& peaks = formants[static_cast<std::size_t>(frame)];
        for (std::size_t at = 0; at < 3; ++at) {
            const auto width = 70.0 + 30.0 * static_cast<double>(at);
            const auto gain = at == 0 ? 1.0 : at == 1 ? 0.6 : 0.3;
            weight += gain / (1.0 + std::pow((hz - peaks[at]) / width, 2.0));
        }
        return weight / std::sqrt(h);
    }
    case 3: {
        static constexpr std::array<double, 8> centres { 3.0, 5.0, 8.0, 12.0, 17.0, 24.0, 33.0, 45.0 };
        const auto centre = centres[static_cast<std::size_t>(frame)];
        const auto width = 1.0 + centre * 0.12;
        return (n == 1 ? 0.5 : 0.0) + std::exp(-0.5 * std::pow((h - centre) / width, 2.0)) / std::pow(h, 0.3);
    }
    case 6: return std::abs(std::cos(kPi * h * static_cast<double>(frame + 1) / 9.0)) / h;
    default: return 0.0;
    }
}

[[nodiscard]] bool madeOfHarmonics(const int set)
{
    return set == 1 || set == 2 || set == 3 || set == 6;
}

// Every frame of every set at every level (made once, off the audio thread:
// the first Colosse voice prepared).
class Bank {
public:
    Bank()
    {
        data_.assign(static_cast<std::size_t>(tableSets * tableFrames * tableLevels) * (tableSize + 1), 0.0f);
        Fft fft;
        fft.prepare(tableSize);
        std::vector<std::complex<float>> spectrum(tableSize);
        std::vector<std::complex<float>> work(tableSize);
        for (int set = 0; set < tableSets; ++set)
            for (int frame = 0; frame < tableFrames; ++frame) {
                if (madeOfHarmonics(set)) {
                    // Sines: x = sum a_n sin(2 pi n p).
                    std::fill(spectrum.begin(), spectrum.end(), std::complex<float>());
                    for (int n = 1; n < tableSize / 2; ++n) {
                        const auto amplitude = static_cast<float>(frameHarmonic(set, frame, n) * tableSize * 0.5);
                        spectrum[static_cast<std::size_t>(n)] = std::complex<float>(0.0f, -amplitude);
                        spectrum[static_cast<std::size_t>(tableSize - n)] = std::complex<float>(0.0f, amplitude);
                    }
                } else {
                    for (int at = 0; at < tableSize; ++at)
                        spectrum[static_cast<std::size_t>(at)]
                            = static_cast<float>(frameShape(set, frame, static_cast<double>(at) / tableSize));
                    fft.forward(spectrum.data());
                }
                spectrum[0] = {};
                spectrum[tableSize / 2] = {};
                auto scale = 1.0f;
                for (int level = 0; level < tableLevels; ++level) {
                    const auto keep = std::max(1, (tableSize / 2) >> level);
                    for (int at = 0; at < tableSize; ++at) {
                        const auto harmonic = at <= tableSize / 2 ? at : tableSize - at;
                        work[static_cast<std::size_t>(at)] = harmonic <= keep ? spectrum[static_cast<std::size_t>(at)] : std::complex<float>();
                    }
                    work[tableSize / 2] = {};
                    fft.inverse(work.data());
                    auto* table = writable(set, frame, level);
                    // The full band sets the scale: its peak at one, the others as loud.
                    if (level == 0) {
                        auto peak = 1.0e-9f;
                        for (int at = 0; at < tableSize; ++at)
                            peak = std::max(peak, std::abs(work[static_cast<std::size_t>(at)].real()));
                        scale = 1.0f / peak;
                    }
                    for (int at = 0; at < tableSize; ++at)
                        table[at] = work[static_cast<std::size_t>(at)].real() * scale;
                    table[tableSize] = table[0];
                }
            }
    }

    [[nodiscard]] const float* table(const int set, const int frame, const int level) const noexcept
    {
        return data_.data() + offset(set, frame, level);
    }

private:
    [[nodiscard]] static std::size_t offset(const int set, const int frame, const int level) noexcept
    {
        return ((static_cast<std::size_t>(set) * tableFrames + static_cast<std::size_t>(frame)) * tableLevels + static_cast<std::size_t>(level))
               * (tableSize + 1);
    }
    float* writable(const int set, const int frame, const int level) noexcept { return data_.data() + offset(set, frame, level); }
    std::vector<float> data_;
};

// The tables once made (none before: what reads them hears nothing).
std::atomic<const Bank*> made { nullptr };

double readTable(const float* table, const double phase) noexcept
{
    const auto position = phase * tableSize;
    const auto index = std::clamp(static_cast<int>(position), 0, tableSize - 1);
    const auto fraction = static_cast<float>(position - static_cast<double>(index));
    return static_cast<double>(table[index] + (table[index + 1] - table[index]) * fraction);
}

} // namespace

// The phase bent by a mode, how much faster it runs (for the level chosen),
// and what multiplies the sample (the formant's window).
double warpStretch(const int mode, const double warp) noexcept
{
    switch (mode) {
    case 1: return 1.0 + 5.0 * warp;
    case 2: return 1.0 + warp;
    case 3:
    case 4: return 1.0 + 7.0 * warp;
    case 6: return 0.5 / std::max(0.05, 0.5 - 0.45 * warp);
    case 7: return 1.0 + 4.0 * warp;
    default: return 1.0;
    }
}

Warp warpPhase(const int mode, const double p, const double warp) noexcept
{
    switch (mode) {
    case 1: {
        const auto k = 1.0 + 5.0 * warp;
        return { p < 0.5 ? 0.5 * std::pow(2.0 * p, k) : 1.0 - 0.5 * std::pow(2.0 * (1.0 - p), k) };
    }
    case 2: {
        const auto mirrored = p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p;
        return { p + (mirrored - p) * warp };
    }
    case 3: {
        const auto x = p * (1.0 + 7.0 * warp);
        return { x - std::floor(x), std::sin(kPi * p) };
    }
    case 4: {
        const auto x = p * (1.0 + 7.0 * warp);
        return { x - std::floor(x) };
    }
    case 5: {
        const auto steps = std::round(2.0 + (1.0 - warp) * (1.0 - warp) * 126.0);
        return { std::floor(p * steps) / steps };
    }
    case 6: {
        const auto d = std::max(0.05, 0.5 - 0.45 * warp);
        return { p < d ? 0.5 * p / d : 0.5 + 0.5 * (p - d) / (1.0 - d) };
    }
    default: return { p };
    }
}

// One oscillator's sample: the frames either side of the position, the
// level its pitch (and its bending) allows, the phase bent.
double sample(const int set, const double position, const int mode, const double warp, const int level, double phase) noexcept
{
    phase -= std::floor(phase);
    const auto bent = warpPhase(mode, phase, warp);
    const auto frame = std::clamp(position, 0.0, 1.0) * (tableFrames - 1);
    const auto lower = std::min(static_cast<int>(frame), tableFrames - 2);
    const auto mix = frame - static_cast<double>(lower);
    const auto* tables = made.load(std::memory_order_acquire);
    if (tables == nullptr)
        return 0.0;
    auto value = readTable(tables->table(set, lower, level), bent.phase);
    if (mix > 1.0e-6)
        value += (readTable(tables->table(set, lower + 1, level), bent.phase) - value) * mix;
    if (mode == 7)
        value = std::sin(value * (1.0 + 4.0 * warp) * kPi * 0.5);
    return value * bent.gain;
}

// The level for a phase increment (cycles a sample) run `stretch` faster.
int levelFor(const double increment, const double stretch) noexcept
{
    const auto top = std::max(1.0e-9, 2.0 * static_cast<double>(tableSize / 2) * increment * stretch);
    return std::clamp(static_cast<int>(std::ceil(std::log2(top))), 0, tableLevels - 1);
}


void prepare()
{
    static const Bank built;
    made.store(&built, std::memory_order_release);
}

} // namespace cydaw::engine::synth::detail::tables
