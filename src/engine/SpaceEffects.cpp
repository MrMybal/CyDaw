#include <cydaw/engine/SpaceEffects.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/Fft.h>
#include <cydaw/engine/SpaceDesign.h>

#include "DspParts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace cydaw::engine {

namespace {

using namespace dsp;
using namespace spacefx;

using Complex = std::complex<float>;
using Channels = std::array<std::vector<float>, 2>;

// ---- Shared parts ------------------------------------------------------------------------------

// A generator of its own (xorshift), so a space sounds the same everywhere.
class Noise {
public:
    explicit Noise(const std::uint32_t seed) noexcept : state_(seed != 0 ? seed : 0x2545f491u) {}
    // In [0, 1).
    double unit() noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<double>(state_) * (1.0 / 4'294'967'296.0);
    }
    double bipolar() noexcept { return unit() * 2.0 - 1.0; }

private:
    std::uint32_t state_;
};

// Two real signals' spectra (bins 0 to size / 2) out of one complex
// transform that had the first as its real part, the second as its
// imaginary part: half the transforms for a pair of channels.
void split(const Complex* z, const std::size_t size, float* leftRe, float* leftIm, float* rightRe, float* rightIm) noexcept
{
    const auto mask = size - 1;
    for (std::size_t k = 0; k <= size / 2; ++k) {
        const auto a = z[k];
        const auto b = z[(size - k) & mask];
        leftRe[k] = 0.5f * (a.real() + b.real());
        leftIm[k] = 0.5f * (a.imag() - b.imag());
        rightRe[k] = 0.5f * (a.imag() + b.imag());
        rightIm[k] = 0.5f * (b.real() - a.real());
    }
}

// The other way: two real signals' spectra into one complex spectrum whose
// inverse holds the first in its real part and the second in its imaginary.
void join(Complex* z, const std::size_t size, const float* leftRe, const float* leftIm, const float* rightRe,
          const float* rightIm) noexcept
{
    const auto half = size / 2;
    for (std::size_t k = 0; k <= half; ++k)
        z[k] = Complex(leftRe[k] - rightIm[k], leftIm[k] + rightRe[k]);
    for (std::size_t k = 1; k < half; ++k)
        z[size - k] = Complex(leftRe[k] + rightIm[k], rightRe[k] - leftIm[k]);
}

// accumulator += input × filter, the spectra kept as rows of real and
// imaginary parts so the loop runs four bins at a time.
void multiplyAdd(float* __restrict accRe, float* __restrict accIm, const float* __restrict xRe, const float* __restrict xIm,
                 const float* __restrict hRe, const float* __restrict hIm, const std::size_t count) noexcept
{
    for (std::size_t k = 0; k < count; ++k) {
        accRe[k] += xRe[k] * hRe[k] - xIm[k] * hIm[k];
        accIm[k] += xRe[k] * hIm[k] + xIm[k] * hRe[k];
    }
}

double energyOf(const Channels& channels) noexcept
{
    auto sum = 0.0;
    for (const auto& channel : channels)
        for (const auto sample : channel)
            sum += static_cast<double>(sample) * sample;
    return sum;
}

// The last `count` samples of a channel faded out (a raised cosine).
void fadeOut(std::vector<float>& channel, const std::size_t count) noexcept
{
    const auto fade = std::min(count, channel.size());
    const auto from = channel.size() - fade;
    for (std::size_t i = 0; i < fade; ++i)
        channel[from + i] *= static_cast<float>(0.5 + 0.5 * std::cos(kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(fade)));
}

// Both channels scaled together so white noise keeps its power through them.
void normalise(Channels& channels) noexcept
{
    const auto energy = energyOf(channels) / 2.0;
    if (energy < 1.0e-20)
        return;
    const auto scale = static_cast<float>(1.0 / std::sqrt(energy));
    for (auto& channel : channels)
        for (auto& sample : channel)
            sample *= scale;
}

// ---- Empreinte: the built-in spaces --------------------------------------------------------------

constexpr double kSound = 343.0;

// The late field heard by one ear: noise whose echoes thicken from sparse to
// dense, split into octave bands that each decay at the space's rate for
// them, rising over the space's onset; in a tunnel, gathered into flutter
// echoes that blur as they go.
std::vector<float> lateField(const EmpreinteRoom& room, const double rate, const std::size_t length, const std::uint32_t seed,
                             const double flutterOffset)
{
    constexpr std::array<double, 7> edges { 125.0, 250.0, 500.0, 1'000.0, 2'000.0, 4'000.0, 8'000.0 };
    constexpr std::array<double, 8> centres { 70.0, 177.0, 354.0, 707.0, 1'414.0, 2'828.0, 5'657.0, 12'000.0 };
    std::array<OnePole, 7> first {};
    std::array<OnePole, 7> second {};
    std::size_t splits = 0;
    for (const auto edge : edges)
        if (edge < rate * 0.45) {
            first[splits].design(edge, rate);
            second[splits].design(edge, rate);
            ++splits;
        }
    std::array<double, 8> fall {};
    std::array<double, 8> level {};
    for (std::size_t band = 0; band < centres.size(); ++band) {
        const auto centre = band == splits ? centres.back() : centres[band];
        fall[band] = std::exp(-6.907755 / (std::max(0.05, empreinteDecayAt(room, centre)) * rate));
        level[band] = 1.0;
    }
    Noise noise(seed);
    std::vector<float> out(length, 0.0f);
    const auto onset = std::max(1.0e-4, room.onset);
    const auto flutterPeriod = room.flutterMs * 0.001;
    for (std::size_t n = 0; n < length; ++n) {
        const auto t = static_cast<double>(n) / rate;
        // Sparse reflections at first, dense once they have mixed: a sample
        // of noise kept with that chance, louder for it so the power holds.
        auto chance = 1.0;
        if (room.mixing > 0.0)
            chance = std::clamp((t / room.mixing) * (t / room.mixing), 0.002, 1.0);
        auto x = 0.0;
        if (chance >= 1.0 || noise.unit() < chance)
            x = noise.bipolar() * std::sqrt(3.0 / chance);
        // The bands: what each low-pass leaves goes on up to the next.
        auto rest = x;
        auto y = 0.0;
        for (std::size_t edge = 0; edge < splits; ++edge) {
            const auto low = second[edge].lowPass(first[edge].lowPass(rest));
            y += low * level[edge];
            rest -= low;
        }
        y += rest * level[splits];
        for (std::size_t band = 0; band < level.size(); ++band)
            level[band] *= fall[band];
        if (flutterPeriod > 0.0) {
            // Pulses a period apart, wider as they go (their average 1).
            const auto width = 0.2 + 0.6 * std::min(1.0, t / 2.0);
            auto phase = t / flutterPeriod + flutterOffset;
            phase -= std::floor(phase);
            const auto distance = std::min(phase, 1.0 - phase);
            const auto pulse = distance < width * 0.5 ? std::cos(kPi * distance / width) : 0.0;
            constexpr auto depth = 0.65;
            y *= (1.0 - depth) + depth * pulse * pulse / (width * 0.5);
        }
        out[n] = static_cast<float>(y * (1.0 - std::exp(-t / onset)));
    }
    return out;
}

// The early reflections of a rectangular room (image sources) for two ears
// 70 cm apart, up to 100 ms after the sound, fading into the late field from
// 50 ms on; the walls take some of the highs.
Channels earlyReflections(const EmpreinteRoom& room, const double rate, const std::size_t length)
{
    Channels out;
    if (room.width <= 0.0 || room.depth <= 0.0 || room.height <= 0.0)
        return out;
    constexpr auto reach = 0.1;
    const std::array<double, 3> size { room.width, room.depth, room.height };
    const std::array<double, 3> source { 0.3 * room.width, 0.3 * room.depth, std::min(1.6, 0.5 * room.height) };
    const auto volume = room.width * room.depth * room.height;
    const auto surface = 2.0 * (room.width * room.depth + room.width * room.height + room.depth * room.height);
    // Sabine's absorption for the space's decay; what a wall gives back.
    const auto absorption = std::clamp(0.161 * volume / (surface * room.seconds), 0.03, 0.9);
    const auto reflection = std::sqrt(1.0 - absorption);
    std::array<double, 128> powers {};
    powers[0] = 1.0;
    for (std::size_t order = 1; order < powers.size(); ++order)
        powers[order] = powers[order - 1] * reflection;
    for (std::size_t ear = 0; ear < 2; ++ear) {
        auto& channel = out[ear];
        channel.assign(length, 0.0f);
        const std::array<double, 3> listener { std::clamp(0.6 * room.width + (ear == 0 ? -0.35 : 0.35), 0.1, room.width - 0.1),
                                               std::min(room.depth - 0.1, source[1] + std::min(0.4 * room.depth, 8.0)),
                                               std::min(1.7, 0.55 * room.height) };
        auto direct = 0.0;
        for (std::size_t axis = 0; axis < 3; ++axis)
            direct += (source[axis] - listener[axis]) * (source[axis] - listener[axis]);
        direct = std::max(0.1, std::sqrt(direct));
        const auto farthest = direct + reach * kSound;
        // Along each axis, the images: how far from the ear, after how many walls.
        std::array<std::vector<std::pair<double, int>>, 3> images;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto span = size[axis];
            const auto most = static_cast<int>(std::ceil(farthest / (2.0 * span))) + 1;
            for (auto i = -most; i <= most; ++i) {
                const auto even = 2.0 * i * span + source[axis] - listener[axis];
                const auto odd = 2.0 * i * span - source[axis] - listener[axis];
                if (std::abs(even) <= farthest)
                    images[axis].emplace_back(even, 2 * std::abs(i));
                if (std::abs(odd) <= farthest)
                    images[axis].emplace_back(odd, std::abs(2 * i - 1));
            }
        }
        for (const auto& [x, xWalls] : images[0])
            for (const auto& [y, yWalls] : images[1]) {
                const auto across = x * x + y * y;
                if (across > farthest * farthest)
                    continue;
                for (const auto& [z, zWalls] : images[2]) {
                    const auto walls = xWalls + yWalls + zWalls;
                    if (walls == 0)
                        continue;
                    const auto distance = std::sqrt(across + z * z);
                    const auto delay = (distance - direct) / kSound;
                    if (delay < 0.0 || delay > reach)
                        continue;
                    auto gain = powers[static_cast<std::size_t>(std::min(walls, 127))] * direct / distance;
                    if (delay > 0.05) {
                        const auto fade = std::cos((delay - 0.05) / 0.05 * kPi * 0.5);
                        gain *= fade * fade;
                    }
                    const auto at = delay * rate;
                    const auto index = static_cast<std::size_t>(at);
                    const auto fraction = at - static_cast<double>(index);
                    if (index + 1 < length) {
                        channel[index] += static_cast<float>(gain * (1.0 - fraction));
                        channel[index + 1] += static_cast<float>(gain * fraction);
                    }
                }
            }
        OnePole a;
        OnePole b;
        a.design(std::min(room.topHz, 9'000.0), rate);
        b.design(std::min(room.topHz, 9'000.0), rate);
        for (auto& sample : channel)
            sample = static_cast<float>(b.lowPass(a.lowPass(sample)));
    }
    return out;
}

// Echoes off far walls (a cave's): dark bursts standing out of the field at
// times of their own, a little apart from one ear to the other.
void addEchoes(const EmpreinteRoom& room, const double rate, Channels& channels, const std::uint32_t seed)
{
    if (room.echoes <= 0 || channels[0].empty())
        return;
    const auto length = channels[0].size();
    Noise noise(seed);
    const auto burstLength = static_cast<std::size_t>(0.03 * rate);
    std::vector<float> burst(burstLength, 0.0f);
    OnePole a;
    OnePole b;
    a.design(1'200.0, rate);
    b.design(1'200.0, rate);
    auto peak = 1.0e-9;
    for (std::size_t n = 0; n < burstLength; ++n) {
        const auto x = noise.bipolar() * std::exp(-static_cast<double>(n) / (0.005 * rate));
        burst[n] = static_cast<float>(b.lowPass(a.lowPass(x)));
        peak = std::max(peak, static_cast<double>(std::abs(burst[n])));
    }
    for (auto& sample : burst)
        sample = static_cast<float>(sample / peak);
    const auto window = static_cast<std::size_t>(0.02 * rate);
    for (auto echo = 0; echo < room.echoes; ++echo) {
        const auto time = 0.1 + 0.9 * (static_cast<double>(echo) + noise.unit()) / static_cast<double>(room.echoes);
        const auto strength = 0.6 + 0.4 * noise.unit();
        for (std::size_t ear = 0; ear < 2; ++ear) {
            const auto at = static_cast<std::size_t>((time + (ear == 0 ? 0.0 : 0.003 + 0.006 * noise.unit())) * rate);
            if (at < window || at + std::max(burstLength, window) >= length)
                continue;
            auto sum = 0.0;
            for (auto n = at - window; n < at + window; ++n)
                sum += static_cast<double>(channels[ear][n]) * channels[ear][n];
            const auto gain = 4.0 * std::sqrt(sum / static_cast<double>(2 * window)) * strength;
            for (std::size_t n = 0; n < burstLength; ++n)
                channels[ear][at + n] += static_cast<float>(burst[n] * gain);
        }
    }
}

// A tank of two springs an ear: each a loop whose every pass goes through a
// chain of all-passes that delays the lows more than the highs, so every echo
// comes back as a chirp, longer each time round.
Channels springs(const EmpreinteRoom& room, const double rate, const std::size_t length)
{
    constexpr std::array<std::array<double, 2>, 2> delaysMs { { { 36.7, 47.3 }, { 41.9, 52.1 } } };
    const auto sections = static_cast<std::size_t>(std::clamp(std::lround(60.0 * rate / 48'000.0), 20L, 240L));
    constexpr auto coefficient = 0.62;
    Channels out;
    for (std::size_t ear = 0; ear < 2; ++ear) {
        out[ear].assign(length, 0.0f);
        for (std::size_t spring = 0; spring < 2; ++spring) {
            const auto delay = static_cast<std::size_t>(std::max(1L, std::lround(delaysMs[ear][spring] * 0.001 * rate)));
            std::vector<double> loop(delay, 0.0);
            std::vector<double> states(sections, 0.0);
            std::size_t at = 0;
            OnePole top;
            OnePole bottom;
            top.design(4'200.0, rate);
            bottom.design(90.0, rate);
            const auto loopSeconds = (static_cast<double>(delay) + static_cast<double>(sections) * 1.5) / rate;
            const auto gain = std::pow(10.0, -3.0 * loopSeconds / std::max(0.1, room.seconds));
            for (std::size_t n = 0; n < length; ++n) {
                auto v = (n == 0 ? 1.0 : 0.0) + loop[at];
                for (auto& state : states) {
                    const auto y = coefficient * v + state;
                    state = v - coefficient * y;
                    v = y;
                }
                out[ear][n] += static_cast<float>(v * 0.5);
                loop[at] = bottom.highPass(top.lowPass(v)) * gain;
                at = at + 1 == delay ? 0 : at + 1;
            }
        }
    }
    return out;
}

// A built-in space, the same each time (fixed seeds): its late field, its
// early reflections, its echoes or its springs, in its band, faded out at its
// end, at unit energy.
Channels synthesisedRoom(const int index, const double rate)
{
    const auto& room = empreinteRooms[static_cast<std::size_t>(std::clamp(index, 0, empreinteSpaceCount - 1))];
    const auto length = static_cast<std::size_t>(std::ceil(empreinteFullSecondsOf(room) * rate));
    const auto seed = 0x9e3779b9u * static_cast<std::uint32_t>(index + 1);
    Channels channels;
    for (std::size_t ear = 0; ear < 2; ++ear)
        channels[ear] = lateField(room, rate, length, seed ^ (0x85ebca6bu * static_cast<std::uint32_t>(ear + 1)), ear == 0 ? 0.0 : 0.08);
    if (room.spring) {
        // The springs' chirps over a little of the diffuse field.
        const auto tank = springs(room, rate, length);
        const auto fieldGain = static_cast<float>(0.3 * std::sqrt(energyOf(tank) / std::max(1.0e-20, energyOf(channels))));
        for (std::size_t ear = 0; ear < 2; ++ear)
            for (std::size_t n = 0; n < length; ++n)
                channels[ear][n] = tank[ear][n] + channels[ear][n] * fieldGain;
    }
    const auto early = earlyReflections(room, rate, length);
    if (!early[0].empty() && room.early > 0.0) {
        // The reflections' energy against the late field's first 150 ms.
        const auto window = std::min(length, static_cast<std::size_t>(0.15 * rate));
        auto start = 0.0;
        for (const auto& channel : channels)
            for (std::size_t n = 0; n < window; ++n)
                start += static_cast<double>(channel[n]) * channel[n];
        const auto reflections = energyOf(early);
        if (reflections > 1.0e-20) {
            const auto gain = static_cast<float>(std::sqrt(room.early * start / reflections));
            for (std::size_t ear = 0; ear < 2; ++ear)
                for (std::size_t n = 0; n < length; ++n)
                    channels[ear][n] += early[ear][n] * gain;
        }
    }
    addEchoes(room, rate, channels, seed ^ 0x27d4eb2fu);
    for (auto& channel : channels) {
        Biquad low;
        Biquad high;
        low.c = designEqBand(EqBandType::lowCut, room.bottomHz, 0.0, 0.7071, 0, 0, rate);
        high.c = designEqBand(EqBandType::highCut, std::min(room.topHz, rate * 0.45), 0.0, 0.7071, 0, 0, rate);
        for (auto& sample : channel)
            sample = static_cast<float>(high(low(sample)));
        // Its last tenth faded out, for what the lows still hold.
        fadeOut(channel, std::max<std::size_t>(1, length / 10));
    }
    normalise(channels);
    return channels;
}

// ---- Empreinte: a file's response ------------------------------------------------------------------

// A channel read again at another rate: a windowed sinc (Kaiser, 24 zero
// crossings, low-passed when read slower), tabulated finely.
std::vector<float> resampled(const std::vector<float>& source, const double ratio, const std::size_t length)
{
    std::vector<float> out(length, 0.0f);
    if (std::abs(ratio - 1.0) < 1.0e-9) {
        std::copy_n(source.begin(), std::min(length, source.size()), out.begin());
        return out;
    }
    const auto cutoff = std::min(1.0, 1.0 / ratio);
    constexpr int crossings = 24;
    constexpr int resolution = 256;
    constexpr auto beta = 8.0;
    std::vector<double> table(static_cast<std::size_t>(crossings * resolution + 2), 0.0);
    const auto norm = besselI0(beta);
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto u = static_cast<double>(i) / resolution;
        const auto sinc = u < 1.0e-12 ? 1.0 : std::sin(kPi * u) / (kPi * u);
        const auto r = u / crossings;
        table[i] = r < 1.0 ? sinc * besselI0(beta * std::sqrt(1.0 - r * r)) / norm : 0.0;
    }
    const auto reach = crossings / cutoff;
    const auto last = static_cast<std::ptrdiff_t>(source.size()) - 1;
    for (std::size_t i = 0; i < length; ++i) {
        const auto position = static_cast<double>(i) * ratio;
        const auto from = std::max<std::ptrdiff_t>(0, static_cast<std::ptrdiff_t>(std::ceil(position - reach)));
        const auto to = std::min(last, static_cast<std::ptrdiff_t>(std::floor(position + reach)));
        auto sum = 0.0;
        for (auto n = from; n <= to; ++n) {
            const auto u = std::abs(position - static_cast<double>(n)) * cutoff * resolution;
            const auto index = static_cast<std::size_t>(u);
            if (index + 1 >= table.size())
                continue;
            const auto fraction = u - static_cast<double>(index);
            sum += static_cast<double>(source[static_cast<std::size_t>(n)]) * (table[index] + (table[index + 1] - table[index]) * fraction);
        }
        out[i] = static_cast<float>(sum * cutoff);
    }
    return out;
}

// A file's response at the processor's rate: its first two channels (one
// played on both), eight seconds at most, at unit energy; empty when silent.
Channels fileAtRate(const ImpulseResponse& file, const double rate)
{
    Channels out;
    if (file.channels.empty() || file.length() == 0 || file.sampleRate <= 0.0)
        return out;
    const auto ratio = file.sampleRate / rate;
    const auto most = static_cast<std::size_t>(empreinteLongestSeconds * rate);
    const auto length = std::min(most, static_cast<std::size_t>(std::ceil(static_cast<double>(file.length()) / ratio)));
    for (std::size_t ear = 0; ear < 2; ++ear) {
        const auto& source = file.channels[std::min(ear, file.channels.size() - 1)];
        out[ear] = resampled(source, ratio, length);
    }
    if (energyOf(out) < 1.0e-20)
        return {};
    normalise(out);
    return out;
}

// ---- Empreinte: a response as the convolution plays it --------------------------------------------

// Its first taps are applied directly; the rest of its first 4096 in
// partitions of 128 (transforms of 256), then partitions of 2048 (transforms
// of 4096) whose work is spread over sixteen blocks of 128: nothing late,
// no block carrying more than its share.
constexpr std::size_t kDirect = 128;
constexpr std::size_t kHeadBlock = 128;
constexpr std::size_t kHeadEnd = 4'096;
constexpr std::size_t kHeadPartitions = (kHeadEnd - kDirect) / kHeadBlock;
constexpr std::size_t kBodyBlock = 2'048;
constexpr std::size_t kSteps = kBodyBlock / kHeadBlock;
constexpr std::size_t kMultiplySteps = kSteps - 2;
static_assert(kHeadEnd == 2 * kBodyBlock, "a body partition's work spreads over the period before it is heard");

// One part of a response (its early part or its tail) in one stage's
// partitions: the run of partitions it has anything in, as spectra.
struct Part {
    std::size_t first {};
    std::size_t count {};
    std::array<std::vector<float>, 2> re;
    std::array<std::vector<float>, 2> im;
};
struct Stage {
    std::size_t partitions {};
    std::array<Part, 2> parts; // early, tail
};

struct Response {
    std::uint32_t serial {};
    float seconds {};
    bool fromFile {};
    std::array<float, empreinteEnvelopePoints> envelope {};
    // Its first taps (all early), by ear.
    std::array<std::array<float, kDirect>, 2> direct {};
    Stage head;
    Stage body;
};

void partition(Stage& stage, const Channels& early, const Channels& tail, const std::size_t start, const std::size_t block,
               const std::size_t partitions, const Fft& fft, std::vector<Complex>& work)
{
    const auto size = 2 * block;
    const auto bins = block + 1;
    const auto length = early[0].size();
    stage.partitions = partitions;
    for (std::size_t which = 0; which < 2; ++which) {
        const auto& taps = which == 0 ? early : tail;
        auto& part = stage.parts[which];
        part = {};
        auto begin = partitions;
        std::size_t end = 0;
        for (std::size_t k = 0; k < partitions; ++k) {
            const auto from = start + k * block;
            const auto to = std::min(length, from + block);
            auto any = false;
            for (std::size_t ear = 0; ear < 2 && !any; ++ear)
                for (auto n = from; n < to; ++n)
                    if (taps[ear][n] != 0.0f) {
                        any = true;
                        break;
                    }
            if (any) {
                begin = std::min(begin, k);
                end = k + 1;
            }
        }
        if (begin >= end)
            continue;
        part.first = begin;
        part.count = end - begin;
        for (std::size_t ear = 0; ear < 2; ++ear) {
            part.re[ear].assign(part.count * bins, 0.0f);
            part.im[ear].assign(part.count * bins, 0.0f);
        }
        for (auto k = begin; k < end; ++k) {
            std::fill(work.begin(), work.begin() + static_cast<std::ptrdiff_t>(size), Complex {});
            const auto from = start + k * block;
            for (std::size_t n = 0; n < block && from + n < length; ++n)
                work[n] = Complex(taps[0][from + n], taps[1][from + n]);
            fft.forward(work.data());
            const auto at = (k - begin) * bins;
            split(work.data(), size, part.re[0].data() + at, part.im[0].data() + at, part.re[1].data() + at, part.im[1].data() + at);
        }
    }
}

// The share of a whole response kept (its last tenth faded out), played
// backwards if asked, measured, cut into its early part and its tail
// (crossing at 80 ms) and partitioned.
std::unique_ptr<Response> responseOf(const Channels& whole, const double share, const bool reverse, const double rate,
                                     const Fft& headFft, const Fft& bodyFft, std::vector<Complex>& work)
{
    auto response = std::make_unique<Response>();
    response->envelope.fill(empreinteEnvelopeFloorDb);
    const auto full = whole[0].size();
    if (full == 0)
        return response;
    const auto kept = std::clamp<std::size_t>(static_cast<std::size_t>(std::lround(static_cast<double>(full) * share)), 1, full);
    Channels taps;
    for (std::size_t ear = 0; ear < 2; ++ear) {
        taps[ear].assign(whole[ear].begin(), whole[ear].begin() + static_cast<std::ptrdiff_t>(kept));
        fadeOut(taps[ear], std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(static_cast<double>(kept) * empreinteFadeShare))));
        if (reverse)
            std::reverse(taps[ear].begin(), taps[ear].end());
    }
    response->seconds = static_cast<float>(static_cast<double>(kept) / rate);
    // Its envelope: the mean power of each 128th, against the loudest.
    auto loudest = -1.0e9;
    std::array<double, empreinteEnvelopePoints> levels {};
    for (std::size_t point = 0; point < levels.size(); ++point) {
        const auto from = point * kept / levels.size();
        const auto to = std::max(from + 1, std::min(kept, (point + 1) * kept / levels.size()));
        auto sum = 0.0;
        for (std::size_t ear = 0; ear < 2; ++ear)
            for (auto n = from; n < to && n < kept; ++n)
                sum += static_cast<double>(taps[ear][n]) * taps[ear][n];
        levels[point] = 10.0 * std::log10(sum / static_cast<double>(2 * (to - from)) + 1.0e-30);
        loudest = std::max(loudest, levels[point]);
    }
    for (std::size_t point = 0; point < levels.size(); ++point)
        response->envelope[point] = static_cast<float>(std::max(static_cast<double>(empreinteEnvelopeFloorDb), levels[point] - loudest));
    // Early and tail, crossing over 10 ms around 80 ms.
    const auto sampleAt = [rate](const double seconds) { return static_cast<std::size_t>(std::lround(seconds * rate)); };
    const auto crossFrom = std::max<std::size_t>(kDirect + 8, sampleAt(empreinteEarlySeconds - empreinteEarlyCrossing * 0.5));
    const auto crossTo = std::max<std::size_t>(crossFrom + 8, sampleAt(empreinteEarlySeconds + empreinteEarlyCrossing * 0.5));
    Channels early;
    Channels tail;
    for (std::size_t ear = 0; ear < 2; ++ear) {
        early[ear].assign(kept, 0.0f);
        tail[ear].assign(kept, 0.0f);
        for (std::size_t n = 0; n < kept; ++n) {
            auto weight = 1.0;
            if (n >= crossTo) {
                weight = 0.0;
            } else if (n >= crossFrom) {
                const auto c = std::cos(0.5 * kPi * (static_cast<double>(n - crossFrom) + 0.5) / static_cast<double>(crossTo - crossFrom));
                weight = c * c;
            }
            early[ear][n] = static_cast<float>(taps[ear][n] * weight);
            tail[ear][n] = taps[ear][n] - early[ear][n];
        }
        for (std::size_t n = 0; n < std::min(kDirect, kept); ++n)
            response->direct[ear][n] = early[ear][n];
    }
    const auto headPartitions = kept > kDirect ? std::min(kHeadPartitions, (kept - kDirect + kHeadBlock - 1) / kHeadBlock) : 0;
    const auto bodyPartitions = kept > kHeadEnd ? (kept - kHeadEnd + kBodyBlock - 1) / kBodyBlock : 0;
    partition(response->head, early, tail, kDirect, kHeadBlock, headPartitions, headFft, work);
    partition(response->body, early, tail, kHeadEnd, kBodyBlock, bodyPartitions, bodyFft, work);
    return response;
}

// ---- Empreinte ---------------------------------------------------------------------------------------

class Empreinte final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    ~Empreinte() override
    {
        stopWorker();
        release();
    }

    void setImpulse(ImpulseResponseRef response) override { file_ = std::move(response); }
    [[nodiscard]] const ImpulseResponse* impulse() const noexcept override { return file_.get(); }

    void prepare(const ProcessSpec& spec) override
    {
        stopWorker();
        release();
        rate_ = std::max(8'000.0, spec.sampleRate);
        headFft_.prepare(2 * kHeadBlock);
        bodyFft_.prepare(2 * kBodyBlock);
        builderHeadFft_.prepare(2 * kHeadBlock);
        builderBodyFft_.prepare(2 * kBodyBlock);
        work_.assign(2 * kBodyBlock, Complex {});
        builderWork_.assign(2 * kBodyBlock, Complex {});
        const auto longest = static_cast<std::size_t>(std::ceil(empreinteLongestSeconds * rate_));
        bodySlots_ = std::max<std::size_t>(1, (longest - std::min(longest, kHeadEnd) + kBodyBlock - 1) / kBodyBlock);
        for (std::size_t ear = 0; ear < 2; ++ear) {
            headRe_[ear].assign(kHeadPartitions * (kHeadBlock + 1), 0.0f);
            headIm_[ear].assign(kHeadPartitions * (kHeadBlock + 1), 0.0f);
            bodyRe_[ear].assign(bodySlots_ * (kBodyBlock + 1), 0.0f);
            bodyIm_[ear].assign(bodySlots_ * (kBodyBlock + 1), 0.0f);
            headAccRe_[ear].assign(kHeadBlock + 1, 0.0f);
            headAccIm_[ear].assign(kHeadBlock + 1, 0.0f);
            bodyInput_[ear].assign(2 * kBodyBlock, 0.0f);
            predelay_[ear].prepare(static_cast<std::size_t>(0.26 * rate_) + 8);
            for (auto& slot : bodyAccRe_)
                for (auto& part : slot)
                    part[ear].assign(kBodyBlock + 1, 0.0f);
            for (auto& slot : bodyAccIm_)
                for (auto& part : slot)
                    part[ear].assign(kBodyBlock + 1, 0.0f);
            for (auto& buffer : bodyOut_)
                for (auto& slot : buffer)
                    for (auto& part : slot)
                        part[ear].assign(kBodyBlock, 0.0f);
        }
        fadeLength_ = std::max(1L, std::lround(empreinteSwapSeconds * rate_));
        glide_ = smoothing(10.0, rate_);
        // A file's response at this rate, once; the space's are made as asked.
        fileWhole_ = file_ != nullptr ? fileAtRate(*file_, rate_) : Channels {};
        usesFile_ = !fileWhole_[0].empty();
        roomIndex_ = -1;
        roomWhole_ = {};
        // The first response, made here; the worker makes the next ones.
        serial_ = 0;
        asked_ = ~std::uint64_t { 0 };
        ask(empreinteSpaceIndexOf(value(empreinteSpace)), empreinteShareOf(value(empreinteLength)), value(empreinteReverse) >= 0.5f);
        slots_[0] = build(request_.load(std::memory_order_relaxed)).release();
        current_ = 0;
        show(*slots_[0]);
        designedLow_ = designedHigh_ = -1.0f;
        reset();
        stop_.store(false, std::memory_order_relaxed);
        worker_ = std::thread([this] { work(); });
    }

    void reset() noexcept override
    {
        if (fading_)
            finishFade();
        for (std::size_t ear = 0; ear < 2; ++ear) {
            std::fill(headRe_[ear].begin(), headRe_[ear].end(), 0.0f);
            std::fill(headIm_[ear].begin(), headIm_[ear].end(), 0.0f);
            std::fill(bodyRe_[ear].begin(), bodyRe_[ear].end(), 0.0f);
            std::fill(bodyIm_[ear].begin(), bodyIm_[ear].end(), 0.0f);
            std::fill(bodyInput_[ear].begin(), bodyInput_[ear].end(), 0.0f);
            history_[ear].fill(0.0f);
            predelay_[ear].reset();
            lowCut_[ear].reset();
            highCut_[ear].reset();
            for (auto& slot : bodyAccRe_)
                for (auto& part : slot)
                    std::fill(part[ear].begin(), part[ear].end(), 0.0f);
            for (auto& slot : bodyAccIm_)
                for (auto& part : slot)
                    std::fill(part[ear].begin(), part[ear].end(), 0.0f);
            for (auto& buffer : bodyOut_)
                for (auto& slot : buffer)
                    for (auto& part : slot)
                        std::fill(part[ear].begin(), part[ear].end(), 0.0f);
            for (auto& slot : headOut_)
                for (auto& part : slot)
                    part[ear].fill(0.0f);
        }
        headNewest_ = 0;
        bodyNewest_ = 0;
        position_ = 0;
        bodyPosition_ = 0;
        playing_ = 0;
        predelaySamples_ = std::clamp(static_cast<double>(value(empreintePredelay)), 0.0, 250.0) * 0.001 * rate_;
        mix_ = std::clamp(static_cast<double>(value(empreinteMix)), 0.0, 1.0);
        early_ = dbToGain(std::clamp(static_cast<double>(value(empreinteEarly)), -24.0, 6.0));
        tail_ = dbToGain(std::clamp(static_cast<double>(value(empreinteTail)), -24.0, 6.0));
        output_ = dbToGain(std::clamp(static_cast<double>(value(empreinteOutput)), -24.0, 24.0));
        width_ = std::clamp(static_cast<double>(value(empreinteWidth)), 0.0, 1.0);
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        (void) context;
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (channels == 0 || frames == 0 || slots_[current_] == nullptr)
            return;
        ears_ = std::min<std::size_t>(channels, 2);
        // A new response asked for, one made, ones done with.
        ask(empreinteSpaceIndexOf(value(empreinteSpace)), empreinteShareOf(value(empreinteLength)), value(empreinteReverse) >= 0.5f);
        if (auto* fresh = pending_.exchange(nullptr, std::memory_order_acq_rel)) {
            retire(incoming_);
            incoming_ = fresh;
        }
        retryRetired();
        designCuts(value(empreinteLowCut), value(empreinteHighCut));
        const auto mixTarget = std::clamp(static_cast<double>(value(empreinteMix)), 0.0, 1.0);
        const auto predelayTarget = std::clamp(static_cast<double>(value(empreintePredelay)), 0.0, 250.0) * 0.001 * rate_;
        const auto earlyTarget = dbToGain(std::clamp(static_cast<double>(value(empreinteEarly)), -24.0, 6.0));
        const auto tailTarget = dbToGain(std::clamp(static_cast<double>(value(empreinteTail)), -24.0, 6.0));
        const auto outputTarget = dbToGain(std::clamp(static_cast<double>(value(empreinteOutput)), -24.0, 24.0));
        const auto widthTarget = std::clamp(static_cast<double>(value(empreinteWidth)), 0.0, 1.0);
        std::size_t frame = 0;
        while (frame < frames) {
            const auto count = std::min<std::size_t>(frames - frame, kHeadBlock - position_);
            // In: the sound after its predelay, into both histories.
            for (std::size_t j = 0; j < count; ++j) {
                predelaySamples_ = predelayTarget + glide_ * (predelaySamples_ - predelayTarget);
                for (std::size_t ear = 0; ear < 2; ++ear) {
                    const auto x = ear < ears_ ? input.channel(static_cast<std::uint32_t>(ear))[frame + j] : 0.0f;
                    predelay_[ear].push(x);
                    auto delayed = static_cast<float>(predelay_[ear].read(predelaySamples_));
                    if (std::abs(delayed) < 1.0e-25f)
                        delayed = 0.0f;
                    history_[ear][kHeadBlock + position_ + j] = delayed;
                    bodyInput_[ear][kBodyBlock + bodyPosition_ + j] = delayed;
                }
            }
            convolve(count);
            // Out: the old response fading into the new one, the parts at
            // their levels, the cuts, the width, the mix.
            for (std::size_t j = 0; j < count; ++j) {
                auto weight = 1.0;
                if (fading_) {
                    if (fadeHold_ > 0) {
                        --fadeHold_;
                        weight = 0.0;
                    } else {
                        weight = std::min(1.0, static_cast<double>(fadePosition_) / static_cast<double>(fadeLength_));
                        ++fadePosition_;
                    }
                }
                mix_ = mixTarget + glide_ * (mix_ - mixTarget);
                early_ = earlyTarget + glide_ * (early_ - earlyTarget);
                tail_ = tailTarget + glide_ * (tail_ - tailTarget);
                output_ = outputTarget + glide_ * (output_ - outputTarget);
                width_ = widthTarget + glide_ * (width_ - widthTarget);
                std::array<double, 2> wet {};
                for (std::size_t ear = 0; ear < ears_; ++ear) {
                    auto early = static_cast<double>(segmentEarly_[current_][ear][j]);
                    auto tail = static_cast<double>(segmentTail_[current_][ear][j]);
                    if (fading_) {
                        early = weight * early + (1.0 - weight) * segmentEarly_[previous_][ear][j];
                        tail = weight * tail + (1.0 - weight) * segmentTail_[previous_][ear][j];
                    }
                    auto w = early_ * early + tail_ * tail;
                    if (lowOn_)
                        w = lowCut_[ear](w);
                    if (highOn_)
                        w = highCut_[ear](w);
                    wet[ear] = w;
                }
                if (ears_ == 2) {
                    const auto middle = 0.5 * (wet[0] + wet[1]);
                    const auto side = 0.5 * (wet[0] - wet[1]) * width_;
                    wet[0] = middle + side;
                    wet[1] = middle - side;
                }
                for (std::size_t ear = 0; ear < ears_; ++ear) {
                    const auto dry = static_cast<double>(input.channel(static_cast<std::uint32_t>(ear))[frame + j]);
                    output.channel(static_cast<std::uint32_t>(ear))[frame + j]
                        = static_cast<float>((dry * (1.0 - mix_) + wet[ear] * mix_) * output_);
                }
            }
            frame += count;
            position_ += count;
            bodyPosition_ += count;
            if (fading_ && fadePosition_ >= fadeLength_)
                finishFade();
            if (position_ == kHeadBlock)
                boundary();
        }
        for (auto channel = static_cast<std::uint32_t>(ears_); channel < channels; ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
        const auto building = serial_ != slots_[current_]->serial || fading_ || incoming_ != nullptr;
        readings_[empreinteReadingBuilding].store(building ? 1.0f : 0.0f, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto n = std::min<std::size_t>(count, readings_.size());
        for (std::size_t i = 0; i < n; ++i)
            into[i] = readings_[i].load(std::memory_order_relaxed);
        return n;
    }

private:
    // ---- The convolution ----

    // The segment's wet parts (up to the next block of 128) for each
    // response playing: the direct taps, then what the stages made for it.
    void convolve(const std::size_t count) noexcept
    {
        for (auto index = 0; index < (fading_ ? 2 : 1); ++index) {
            const auto slot = index == 0 ? current_ : previous_;
            const auto& response = *slots_[slot];
            for (std::size_t ear = 0; ear < ears_; ++ear) {
                auto* early = segmentEarly_[slot][ear].data();
                auto* tail = segmentTail_[slot][ear].data();
                const auto* headEarly = headOut_[slot][0][ear].data() + position_;
                const auto* headTail = headOut_[slot][1][ear].data() + position_;
                const auto* bodyEarly = bodyOut_[playing_][slot][0][ear].data() + bodyPosition_;
                const auto* bodyTail = bodyOut_[playing_][slot][1][ear].data() + bodyPosition_;
                for (std::size_t j = 0; j < count; ++j) {
                    early[j] = headEarly[j] + bodyEarly[j];
                    tail[j] = headTail[j] + bodyTail[j];
                }
                const auto* taps = response.direct[ear].data();
                const auto* newest = history_[ear].data() + kHeadBlock + position_;
                for (std::size_t tap = 0; tap < kDirect; ++tap) {
                    const auto gain = taps[tap];
                    if (gain == 0.0f)
                        continue;
                    const auto* from = newest - tap;
                    for (std::size_t j = 0; j < count; ++j)
                        early[j] += gain * from[j];
                }
            }
        }
    }

    // Every 128 samples: a new response may come in (at the start of a
    // body period), the head's partitions run, the body takes its next step.
    void boundary() noexcept
    {
        position_ = 0;
        if (bodyPosition_ == kBodyBlock) {
            bodyPosition_ = 0;
            if (incoming_ != nullptr && !fading_)
                activate();
            playing_ ^= 1;
        }
        headStage();
        bodyStep(bodyPosition_ / kHeadBlock);
        for (auto& history : history_)
            std::copy(history.begin() + kHeadBlock, history.end(), history.begin());
    }

    void headStage() noexcept
    {
        constexpr auto size = 2 * kHeadBlock;
        constexpr auto bins = kHeadBlock + 1;
        for (std::size_t n = 0; n < size; ++n)
            work_[n] = Complex(history_[0][n], history_[1][n]);
        headFft_.forward(work_.data());
        headNewest_ = (headNewest_ + kHeadPartitions - 1) % kHeadPartitions;
        const auto at = headNewest_ * bins;
        split(work_.data(), size, headRe_[0].data() + at, headIm_[0].data() + at, headRe_[1].data() + at, headIm_[1].data() + at);
        for (auto index = 0; index < (fading_ ? 2 : 1); ++index) {
            const auto slot = index == 0 ? current_ : previous_;
            const auto& stage = slots_[slot]->head;
            for (std::size_t which = 0; which < 2; ++which) {
                const auto& part = stage.parts[which];
                auto& out = headOut_[slot][which];
                if (part.count == 0) {
                    for (auto& channel : out)
                        channel.fill(0.0f);
                    continue;
                }
                for (std::size_t ear = 0; ear < 2; ++ear) {
                    std::fill(headAccRe_[ear].begin(), headAccRe_[ear].end(), 0.0f);
                    std::fill(headAccIm_[ear].begin(), headAccIm_[ear].end(), 0.0f);
                }
                for (auto k = part.first; k < part.first + part.count && k < kHeadPartitions; ++k) {
                    const auto from = ((headNewest_ + k) % kHeadPartitions) * bins;
                    const auto own = (k - part.first) * bins;
                    for (std::size_t ear = 0; ear < ears_; ++ear)
                        multiplyAdd(headAccRe_[ear].data(), headAccIm_[ear].data(), headRe_[ear].data() + from, headIm_[ear].data() + from,
                                    part.re[ear].data() + own, part.im[ear].data() + own, bins);
                }
                join(work_.data(), size, headAccRe_[0].data(), headAccIm_[0].data(), headAccRe_[1].data(), headAccIm_[1].data());
                headFft_.inverse(work_.data());
                for (std::size_t n = 0; n < kHeadBlock; ++n) {
                    out[0][n] = work_[kHeadBlock + n].real();
                    out[1][n] = work_[kHeadBlock + n].imag();
                }
            }
        }
    }

    // The body's period of 16 steps: the transform of the last two blocks,
    // fourteen shares of the partitions, the way back to time; heard over
    // the next period.
    void bodyStep(const std::size_t step) noexcept
    {
        constexpr auto size = 2 * kBodyBlock;
        constexpr auto bins = kBodyBlock + 1;
        const auto active = fading_ ? 2 : 1;
        if (step == 0) {
            for (std::size_t n = 0; n < size; ++n)
                work_[n] = Complex(bodyInput_[0][n], bodyInput_[1][n]);
            bodyFft_.forward(work_.data());
            bodyNewest_ = (bodyNewest_ + bodySlots_ - 1) % bodySlots_;
            const auto at = bodyNewest_ * bins;
            split(work_.data(), size, bodyRe_[0].data() + at, bodyIm_[0].data() + at, bodyRe_[1].data() + at, bodyIm_[1].data() + at);
            for (auto& input : bodyInput_)
                std::copy(input.begin() + kBodyBlock, input.end(), input.begin());
            for (auto index = 0; index < active; ++index) {
                const auto slot = index == 0 ? current_ : previous_;
                for (std::size_t which = 0; which < 2; ++which)
                    for (std::size_t ear = 0; ear < 2; ++ear) {
                        std::fill(bodyAccRe_[slot][which][ear].begin(), bodyAccRe_[slot][which][ear].end(), 0.0f);
                        std::fill(bodyAccIm_[slot][which][ear].begin(), bodyAccIm_[slot][which][ear].end(), 0.0f);
                    }
            }
            return;
        }
        if (step <= kMultiplySteps) {
            for (auto index = 0; index < active; ++index) {
                const auto slot = index == 0 ? current_ : previous_;
                const auto& stage = slots_[slot]->body;
                const auto begin = stage.partitions * (step - 1) / kMultiplySteps;
                const auto end = stage.partitions * step / kMultiplySteps;
                for (std::size_t which = 0; which < 2; ++which) {
                    const auto& part = stage.parts[which];
                    const auto from = std::max(begin, part.first);
                    const auto to = std::min({ end, part.first + part.count, bodySlots_ });
                    for (auto k = from; k < to; ++k) {
                        const auto input = ((bodyNewest_ + k) % bodySlots_) * bins;
                        const auto own = (k - part.first) * bins;
                        for (std::size_t ear = 0; ear < ears_; ++ear)
                            multiplyAdd(bodyAccRe_[slot][which][ear].data(), bodyAccIm_[slot][which][ear].data(), bodyRe_[ear].data() + input,
                                        bodyIm_[ear].data() + input, part.re[ear].data() + own, part.im[ear].data() + own, bins);
                    }
                }
            }
            return;
        }
        const auto target = playing_ ^ 1;
        for (auto index = 0; index < active; ++index) {
            const auto slot = index == 0 ? current_ : previous_;
            const auto& stage = slots_[slot]->body;
            for (std::size_t which = 0; which < 2; ++which) {
                auto& out = bodyOut_[target][slot][which];
                if (stage.parts[which].count == 0) {
                    for (auto& channel : out)
                        std::fill(channel.begin(), channel.end(), 0.0f);
                    continue;
                }
                const auto& re = bodyAccRe_[slot][which];
                const auto& im = bodyAccIm_[slot][which];
                join(work_.data(), size, re[0].data(), im[0].data(), re[1].data(), im[1].data());
                bodyFft_.inverse(work_.data());
                for (std::size_t n = 0; n < kBodyBlock; ++n) {
                    out[0][n] = work_[kBodyBlock + n].real();
                    out[1][n] = work_[kBodyBlock + n].imag();
                }
            }
        }
    }

    // ---- Responses coming and going ----

    // The response the values ask for: a new request for the worker when it
    // differs from the last.
    void ask(const int space, const double share, const bool reverse) noexcept
    {
        const auto key = (static_cast<std::uint64_t>(usesFile_ ? 0 : space) << 17) | (static_cast<std::uint64_t>(reverse ? 1 : 0) << 16)
                         | static_cast<std::uint64_t>(std::lround(share * 10'000.0));
        if (key == asked_)
            return;
        asked_ = key;
        ++serial_;
        request_.store((static_cast<std::uint64_t>(serial_) << 32) | key, std::memory_order_release);
    }

    // A new response takes over: both play for a while (the body's
    // outputs for it come one period later), then the old fades out.
    void activate() noexcept
    {
        const auto fresh = current_ ^ 1;
        slots_[fresh] = incoming_;
        incoming_ = nullptr;
        previous_ = current_;
        current_ = fresh;
        fading_ = true;
        fadeHold_ = static_cast<long>(kBodyBlock);
        fadePosition_ = 0;
        for (std::size_t which = 0; which < 2; ++which)
            for (std::size_t ear = 0; ear < 2; ++ear) {
                headOut_[fresh][which][ear].fill(0.0f);
                for (auto& buffer : bodyOut_)
                    std::fill(buffer[fresh][which][ear].begin(), buffer[fresh][which][ear].end(), 0.0f);
            }
        show(*slots_[current_]);
    }

    void finishFade() noexcept
    {
        retire(slots_[previous_]);
        slots_[previous_] = nullptr;
        fading_ = false;
    }

    // Back to the worker, which frees it.
    void retire(Response* response) noexcept
    {
        if (response == nullptr)
            return;
        for (auto& slot : retired_) {
            Response* expected = nullptr;
            if (slot.compare_exchange_strong(expected, response, std::memory_order_acq_rel))
                return;
        }
        for (auto& held : overflow_)
            if (held == nullptr) {
                held = response;
                return;
            }
    }

    void retryRetired() noexcept
    {
        for (auto& held : overflow_)
            if (held != nullptr) {
                auto* response = held;
                held = nullptr;
                retire(response);
            }
    }

    void show(const Response& response) noexcept
    {
        readings_[empreinteReadingSeconds].store(response.seconds, std::memory_order_relaxed);
        readings_[empreinteReadingFile].store(response.fromFile ? 1.0f : 0.0f, std::memory_order_relaxed);
        for (std::size_t point = 0; point < response.envelope.size(); ++point)
            readings_[static_cast<std::size_t>(empreinteReadingEnvelope) + point].store(response.envelope[point], std::memory_order_relaxed);
    }

    void designCuts(const float low, const float high) noexcept
    {
        lowOn_ = empreinteLowCutApplied(low);
        highOn_ = empreinteHighCutApplied(high) && high < rate_ * 0.45;
        if (std::abs(low - designedLow_) > 0.01f) {
            for (auto& filter : lowCut_)
                filter.c = designEqBand(EqBandType::lowCut, low, 0.0, 0.7071, 0, 0, rate_);
            designedLow_ = low;
        }
        if (std::abs(high - designedHigh_) > 0.01f) {
            for (auto& filter : highCut_)
                filter.c = designEqBand(EqBandType::highCut, std::min(static_cast<double>(high), rate_ * 0.45), 0.0, 0.7071, 0, 0, rate_);
            designedHigh_ = high;
        }
    }

    // ---- The worker ----

    // Made off the audio thread: from the file, or from the space (kept
    // while only its length or direction changes).
    std::unique_ptr<Response> build(const std::uint64_t request)
    {
        const auto space = static_cast<int>((request >> 17) & 0xfu);
        const auto reverse = ((request >> 16) & 1u) != 0;
        const auto share = static_cast<double>(request & 0xffffu) / 10'000.0;
        const Channels* whole = &fileWhole_;
        if (!usesFile_) {
            if (roomIndex_ != space) {
                roomWhole_ = synthesisedRoom(space, rate_);
                roomIndex_ = space;
            }
            whole = &roomWhole_;
        }
        auto response = responseOf(*whole, share, reverse, rate_, builderHeadFft_, builderBodyFft_, builderWork_);
        response->serial = static_cast<std::uint32_t>(request >> 32);
        response->fromFile = usesFile_;
        return response;
    }

    void work()
    {
        auto built = static_cast<std::uint32_t>(request_.load(std::memory_order_acquire) >> 32);
        while (!stop_.load(std::memory_order_acquire)) {
            for (auto& slot : retired_)
                delete slot.exchange(nullptr, std::memory_order_acq_rel);
            const auto request = request_.load(std::memory_order_acquire);
            const auto serial = static_cast<std::uint32_t>(request >> 32);
            if (serial != built) {
                built = serial;
                Response* fresh = nullptr;
                try {
                    fresh = build(request).release();
                } catch (...) {
                    fresh = nullptr;
                }
                if (fresh != nullptr)
                    delete pending_.exchange(fresh, std::memory_order_acq_rel);
                continue;
            }
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(8), [this] { return stop_.load(std::memory_order_acquire); });
        }
    }

    void stopWorker()
    {
        if (!worker_.joinable())
            return;
        {
            const std::lock_guard lock(mutex_);
            stop_.store(true, std::memory_order_release);
        }
        wake_.notify_all();
        worker_.join();
    }

    void release() noexcept
    {
        for (auto& slot : slots_) {
            delete slot;
            slot = nullptr;
        }
        delete incoming_;
        incoming_ = nullptr;
        delete pending_.exchange(nullptr, std::memory_order_acq_rel);
        for (auto& slot : retired_)
            delete slot.exchange(nullptr, std::memory_order_acq_rel);
        for (auto& held : overflow_) {
            delete held;
            held = nullptr;
        }
        fading_ = false;
        current_ = 0;
        previous_ = 1;
    }

    ImpulseResponseRef file_;
    double rate_ { 48'000.0 };
    std::size_t ears_ { 2 };

    // The audio thread's transforms and its partitions' inputs, as spectra.
    Fft headFft_;
    Fft bodyFft_;
    std::vector<Complex> work_;
    std::array<std::vector<float>, 2> headRe_;
    std::array<std::vector<float>, 2> headIm_;
    std::array<std::vector<float>, 2> bodyRe_;
    std::array<std::vector<float>, 2> bodyIm_;
    std::size_t headNewest_ {};
    std::size_t bodyNewest_ {};
    std::size_t bodySlots_ { 1 };
    std::array<std::vector<float>, 2> headAccRe_;
    std::array<std::vector<float>, 2> headAccIm_;
    // By response slot, part (early, tail) and ear.
    std::array<std::array<std::array<std::vector<float>, 2>, 2>, 2> bodyAccRe_;
    std::array<std::array<std::array<std::vector<float>, 2>, 2>, 2> bodyAccIm_;
    std::array<std::array<std::array<std::array<float, kHeadBlock>, 2>, 2>, 2> headOut_ {};
    // Two of them: one heard, one being made.
    std::array<std::array<std::array<std::array<std::vector<float>, 2>, 2>, 2>, 2> bodyOut_;
    std::array<std::array<std::array<float, kHeadBlock>, 2>, 2> segmentEarly_ {};
    std::array<std::array<std::array<float, kHeadBlock>, 2>, 2> segmentTail_ {};
    // The last two blocks of 128 (the direct taps', the head's), of 2048 (the body's).
    std::array<std::array<float, 2 * kHeadBlock>, 2> history_ {};
    std::array<std::vector<float>, 2> bodyInput_;
    std::size_t position_ {};
    std::size_t bodyPosition_ {};
    std::size_t playing_ {};

    // The responses: the one heard, the one fading out, the one waiting.
    std::array<Response*, 2> slots_ {};
    std::size_t current_ {};
    std::size_t previous_ { 1 };
    bool fading_ {};
    long fadeHold_ {};
    long fadePosition_ {};
    long fadeLength_ { 2'400 };
    Response* incoming_ {};
    std::atomic<Response*> pending_ {};
    std::array<std::atomic<Response*>, 8> retired_ {};
    std::array<Response*, 8> overflow_ {};
    std::atomic<std::uint64_t> request_ {};
    std::uint64_t asked_ { ~std::uint64_t { 0 } };
    std::uint32_t serial_ {};

    // The worker and what only it touches once it runs.
    std::thread worker_;
    std::atomic<bool> stop_ {};
    std::mutex mutex_;
    std::condition_variable wake_;
    Fft builderHeadFft_;
    Fft builderBodyFft_;
    std::vector<Complex> builderWork_;
    Channels fileWhole_;
    bool usesFile_ {};
    Channels roomWhole_;
    int roomIndex_ { -1 };

    // The wet sound's way out.
    std::array<Delay, 2> predelay_;
    std::array<Biquad, 2> lowCut_ {};
    std::array<Biquad, 2> highCut_ {};
    float designedLow_ { -1.0f };
    float designedHigh_ { -1.0f };
    bool lowOn_ {};
    bool highOn_ {};
    double glide_ {};
    double predelaySamples_ {};
    double mix_ {};
    double early_ { 1.0 };
    double tail_ { 1.0 };
    double output_ { 1.0 };
    double width_ { 1.0 };

    std::array<std::atomic<float>, empreinteReadingCount> readings_ {};
};

// ---- Givre -----------------------------------------------------------------------------------------

constexpr std::size_t kSize = givreFrameSize;
constexpr std::size_t kHop = givreHop;
constexpr std::size_t kBins = kSize / 2 + 1;
// A bin's phase turns this much a hop for each bin of frequency.
constexpr double kAdvance = 2.0 * kPi * static_cast<double>(kHop) / static_cast<double>(kSize);
// A bin quieter than this keeps the frequency it had (about -120 dB).
constexpr float kQuietBin = 5.0e-4f;
// A capture quieter than this is waited past.
constexpr double kQuietCapture = 1.0e-4;
constexpr long long kNoBeat = -(1LL << 62);
// The scatter's slow part is drawn every this many bins, and between them.
constexpr std::size_t kDriftSpacing = 8;

double wrapped(const double phase) noexcept
{
    return phase - 2.0 * kPi * std::floor(phase / (2.0 * kPi) + 0.5);
}

// The frequency a made-up phase turns at: the bin's own when it is near
// (a partial's), its centre when what it holds came from elsewhere (smeared
// in, or a frequency too far for the bin to carry), so the frames agree.
double heldFrequency(const float frequency, const std::size_t bin, const bool foreign) noexcept
{
    const auto centre = static_cast<double>(bin);
    return !foreign && std::abs(static_cast<double>(frequency) - centre) <= 2.0 ? static_cast<double>(frequency) : centre;
}

class Givre final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(8'000.0, spec.sampleRate);
        fft_.prepare(kSize);
        work_.assign(kSize, Complex {});
        window_.resize(kSize);
        for (std::size_t n = 0; n < kSize; ++n)
            window_[n] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(n) / static_cast<double>(kSize)));
        for (auto& lane : lanes_) {
            lane.input.assign(kSize, 0.0f);
            lane.overlap.assign(kSize, 0.0f);
            lane.ready.assign(kHop, 0.0f);
            for (auto* row : { &lane.lastPhase, &lane.level, &lane.frequency, &lane.shiftedPhase, &lane.shiftedFrequency,
                               &lane.excessPhase, &lane.blur, &lane.frozen, &lane.frozenGoal, &lane.frozenFrequency, &lane.frozenPhase })
                row->assign(kBins, 0.0f);
            for (auto& row : lane.pastPower)
                row.assign(kBins, 0.0f);
            for (auto& row : lane.pastFrequency)
                row.assign(kBins, 0.0f);
            lane.drift.assign(kBins / kDriftSpacing + 2, 0.0f);
        }
        for (auto& line : dry_)
            line.assign(kSize, 0.0f);
        for (std::size_t ear = 0; ear < 2; ++ear) {
            spectrumRe_[ear].assign(kBins, 0.0f);
            spectrumIm_[ear].assign(kBins, 0.0f);
            outRe_[ear].assign(kBins, 0.0f);
            outIm_[ear].assign(kBins, 0.0f);
        }
        for (auto* row : { &magnitude_, &phase_, &shiftedMagnitude_, &power_, &unsmeared_, &target_, &best_, &frozenShifted_,
                           &frozenShiftedFrequency_, &toneGain_, &scattered_, &previousPhase_ })
            row->assign(kBins, 0.0f);
        peaks_.assign(kBins, 0);
        peakPhase_.assign(kBins, 0.0);
        prefix_.assign(kBins + 1, 0.0);
        designedTone_ = 1.0e9f;
        // The readings' bands, in bins.
        const auto hzPerBin = rate_ / static_cast<double>(kSize);
        for (int band = 0; band < givreBands; ++band) {
            bandLow_[static_cast<std::size_t>(band)] = givreBandHzOf(band - 0.5) / hzPerBin;
            bandHigh_[static_cast<std::size_t>(band)] = givreBandHzOf(band + 0.5) / hzPerBin;
        }
        fadeStep_ = static_cast<double>(kHop) / (givreFadeSeconds * rate_);
        fall_ = 30.0 * static_cast<double>(kHop) / rate_;
        glide_ = smoothing(10.0, rate_);
        reset();
    }

    void reset() noexcept override
    {
        for (std::size_t ear = 0; ear < lanes_.size(); ++ear) {
            auto& lane = lanes_[ear];
            for (auto* row : { &lane.input, &lane.overlap, &lane.ready, &lane.lastPhase, &lane.level, &lane.shiftedPhase,
                               &lane.excessPhase, &lane.blur, &lane.frozen, &lane.frozenGoal, &lane.frozenPhase })
                std::fill(row->begin(), row->end(), 0.0f);
            for (auto& row : lane.pastPower)
                std::fill(row.begin(), row.end(), 0.0f);
            std::fill(lane.drift.begin(), lane.drift.end(), 0.0f);
            for (std::size_t k = 0; k < lane.frequency.size(); ++k) {
                // Made-up phases start as a sound centred in the frame has
                // them (each bin half a turn from the next): what is made of
                // them then sits where the window is open.
                const auto centred = k % 2 == 0 ? 0.0f : static_cast<float>(kPi);
                lane.shiftedPhase[k] = centred;
                lane.excessPhase[k] = centred;
                lane.frozenPhase[k] = centred;
                lane.frequency[k] = static_cast<float>(k);
                lane.shiftedFrequency[k] = static_cast<float>(k);
                lane.frozenFrequency[k] = static_cast<float>(k);
                for (auto& row : lane.pastFrequency)
                    row[k] = static_cast<float>(k);
            }
            lane.noise = 0x6d2b79f5u * static_cast<std::uint32_t>(ear + 1);
        }
        for (auto& line : dry_)
            std::fill(line.begin(), line.end(), 0.0f);
        hopAt_ = 0;
        dryAt_ = 0;
        pastAt_ = 0;
        frozenOn_ = false;
        frozenQuiet_ = true;
        listening_ = 0;
        fade_ = 0.0;
        syncBeat_ = kNoBeat;
        levels_.fill(static_cast<double>(givreFloorDb));
        for (auto& reading : readings_)
            reading.store(givreFloorDb, std::memory_order_relaxed);
        readings_[givreReadingFrozen].store(0.0f, std::memory_order_relaxed);
        mix_ = std::clamp(static_cast<double>(value(givreMix)), 0.0, 1.0);
        gain_ = dbToGain(std::clamp(static_cast<double>(value(givreOutput)), -24.0, 24.0));
    }

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return static_cast<std::uint32_t>(givreLatency); }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override { return static_cast<std::uint32_t>(givreLatency); }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min({ input.channelCount, output.channelCount, static_cast<std::uint32_t>(kChannels) });
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (channels == 0 || frames == 0 || window_.empty())
            return;
        ears_ = std::min<std::size_t>(channels, 2);
        const auto mixTarget = std::clamp(static_cast<double>(value(givreMix)), 0.0, 1.0);
        const auto gainTarget = dbToGain(std::clamp(static_cast<double>(value(givreOutput)), -24.0, 24.0));
        const auto& transport = context.transport;
        const auto playing = transport.state != PlaybackState::stopped;
        const auto beatsPerSample = std::clamp(transport.beatsPerMinute, 20.0, 400.0) / 60.0 / rate_;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            mix_ = mixTarget + glide_ * (mix_ - mixTarget);
            gain_ = gainTarget + glide_ * (gain_ - gainTarget);
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                // The dry sound, as late as the wet one.
                auto& line = dry_[channel];
                const auto x = input.channel(channel)[frame];
                const auto delayed = line[dryAt_];
                line[dryAt_] = x;
                if (channel < ears_) {
                    auto& lane = lanes_[channel];
                    lane.input[kSize - kHop + hopAt_] = x;
                    const auto wet = static_cast<double>(lane.ready[hopAt_]);
                    output.channel(channel)[frame] = static_cast<float>((delayed * (1.0 - mix_) + wet * mix_) * gain_);
                } else {
                    output.channel(channel)[frame] = delayed;
                }
            }
            dryAt_ = dryAt_ + 1 == kSize ? 0 : dryAt_ + 1;
            if (++hopAt_ == kHop) {
                hopAt_ = 0;
                runFrame(transport.quarterNotePosition + static_cast<double>(frame + 1) * beatsPerSample, playing);
            }
        }
        readings_[givreReadingFrozen].store(fade_ >= 0.5 ? 1.0f : 0.0f, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto n = std::min<std::size_t>(count, readings_.size());
        for (std::size_t i = 0; i < n; ++i)
            into[i] = readings_[i].load(std::memory_order_relaxed);
        return n;
    }

private:
    struct Lane {
        std::vector<float> input;   // the last frame's samples in
        std::vector<float> overlap; // the frames added up, still to come out
        std::vector<float> ready;   // the next hop's samples out
        std::vector<float> lastPhase;
        std::vector<float> level;     // each bin's magnitude in the last frame
        std::vector<float> frequency; // each bin's (in bins), held through silence
        std::vector<float> shiftedPhase;
        std::vector<float> shiftedFrequency;
        std::vector<float> excessPhase; // what the blur and smear add, made up
        std::vector<float> blur;        // power, followed slowly
        std::array<std::vector<float>, givreCaptureFrames> pastPower;
        std::array<std::vector<float>, givreCaptureFrames> pastFrequency;
        std::vector<float> frozen;      // magnitudes held
        std::vector<float> frozenGoal;  // where they go (a new capture)
        std::vector<float> frozenFrequency;
        std::vector<float> frozenPhase;
        // The scatter's slow part: a wandering value every eighth bin.
        std::vector<float> drift;
        std::uint32_t noise { 1u };
    };

    struct Settings {
        double blurCoefficient {};
        bool blurOn {};
        double smearOctaves {};
        bool smearOn {};
        double ratio { 1.0 };
        bool identity { true };
        double scatter {};
    };

    static double jitter(Lane& lane) noexcept
    {
        lane.noise ^= lane.noise << 13;
        lane.noise ^= lane.noise >> 17;
        lane.noise ^= lane.noise << 5;
        return static_cast<double>(lane.noise) * (2.0 / 4'294'967'296.0) - 1.0;
    }

    // What the scatter adds to the made-up phases this frame: a part that
    // wanders slowly and alike over neighbouring bins (a partial's bins stay
    // together, partials drift apart: a shimmer), and a part fresh at every
    // bin and frame, growing as the square of the scatter (a wash).
    void scatterPhases(Lane& lane, const double scatter) noexcept
    {
        // Frames whose phases no longer agree add up in power, not in
        // amplitude: what a fresh phase in ±s²π keeps of that agreement,
        // and the gain that makes up for the rest.
        const auto spread = scatter * scatter;
        const auto sinc = spread > 1.0e-6 ? std::sin(kPi * spread) / (kPi * spread) : 1.0;
        const auto agreement = sinc * sinc;
        scatterGain_ = 1.0 / std::sqrt(agreement + 0.25 * (1.0 - agreement));
        if (scatter <= 0.0) {
            std::fill(scattered_.begin(), scattered_.end(), 0.0f);
            return;
        }
        for (auto& point : lane.drift)
            point = static_cast<float>(0.9 * point + 0.1 * 3.8 * jitter(lane));
        const auto slow = scatter * kPi;
        const auto fast = scatter * scatter * kPi;
        for (std::size_t k = 0; k < kBins; ++k) {
            const auto at = k / kDriftSpacing;
            const auto fraction = static_cast<double>(k % kDriftSpacing) / static_cast<double>(kDriftSpacing);
            const auto wander = lane.drift[at] + (lane.drift[at + 1] - lane.drift[at]) * fraction;
            scattered_[k] = static_cast<float>(slow * wander + fast * jitter(lane));
        }
    }

    // Each hop: both lanes analysed in one transform, frozen, shifted,
    // smeared, blurred, tilted, and back by one inverse transform.
    void runFrame(const double quarterNote, const bool playing) noexcept
    {
        Settings settings;
        const auto blurSeconds = givreBlurSecondsOf(value(givreBlur));
        settings.blurOn = blurSeconds > 1.0e-3;
        settings.blurCoefficient = settings.blurOn ? std::exp(-2.0 * static_cast<double>(kHop) / (blurSeconds * rate_)) : 0.0;
        settings.smearOctaves = givreSmearOctavesOf(value(givreSmear));
        settings.smearOn = settings.smearOctaves > 1.0e-3;
        const auto shift = std::clamp(value(givreShift), -12.0f, 12.0f);
        settings.identity = std::abs(shift) < 0.005f;
        settings.ratio = givreShiftRatioOf(shift);
        settings.scatter = std::clamp(static_cast<double>(value(givreScatter)), 0.0, 1.0);
        const auto tone = std::clamp(value(givreTone), -1.0f, 1.0f);
        if (tone != designedTone_) {
            for (std::size_t k = 0; k < kBins; ++k)
                toneGain_[k] = static_cast<float>(dbToGain(givreToneDbOf(tone, static_cast<double>(k) * rate_ / static_cast<double>(kSize))));
            designedTone_ = tone;
        }
        // The freeze: on, off, caught again on the beat, or waiting for a sound.
        const auto freeze = value(givreFreeze) >= 0.5f;
        auto capture = false;
        auto recapture = false;
        if (freeze && !frozenOn_) {
            frozenOn_ = true;
            capture = true;
        }
        if (!freeze)
            frozenOn_ = false;
        const auto* division = noteDivisionFor(value(givreSync));
        if (frozenOn_ && division != nullptr && playing) {
            const auto beat = static_cast<long long>(std::floor(quarterNote / division->beats));
            if (syncBeat_ != kNoBeat && beat != syncBeat_)
                recapture = true;
            syncBeat_ = beat;
        } else {
            syncBeat_ = kNoBeat;
        }
        if (frozenOn_ && frozenQuiet_)
            capture = true;
        const auto growing = frozenOn_ && !capture && listening_ > 0;
        if (growing)
            --listening_;
        fade_ = frozenOn_ ? std::min(1.0, fade_ + fadeStep_) : std::max(0.0, fade_ - fadeStep_);

        for (std::size_t n = 0; n < kSize; ++n)
            work_[n] = Complex(lanes_[0].input[n] * window_[n], lanes_[1].input[n] * window_[n]);
        fft_.forward(work_.data());
        split(work_.data(), kSize, spectrumRe_[0].data(), spectrumIm_[0].data(), spectrumRe_[1].data(), spectrumIm_[1].data());
        const auto wasQuiet = frozenQuiet_;
        for (std::size_t ear = 0; ear < 2; ++ear) {
            if (ear < ears_)
                processLane(lanes_[ear], ear, settings, capture, recapture, growing);
            else {
                std::fill(outRe_[ear].begin(), outRe_[ear].end(), 0.0f);
                std::fill(outIm_[ear].begin(), outIm_[ear].end(), 0.0f);
            }
        }
        pastAt_ = (pastAt_ + 1) % givreCaptureFrames;
        if (capture || recapture || growing) {
            auto power = 0.0;
            for (const auto magnitude : lanes_[0].frozenGoal)
                power += static_cast<double>(magnitude) * magnitude;
            frozenQuiet_ = power < kQuietCapture;
            // Out of silence: a few more frames to catch the sound whole.
            if (wasQuiet && !frozenQuiet_)
                listening_ = 6;
        }
        if (!frozenOn_) {
            frozenQuiet_ = true;
            listening_ = 0;
        }
        showSpectrum();
        // Back to time, windowed again, added up.
        join(work_.data(), kSize, outRe_[0].data(), outIm_[0].data(), outRe_[1].data(), outIm_[1].data());
        fft_.inverse(work_.data());
        constexpr auto scale = 1.0f / 1.5f; // two Hann windows a quarter apart add up to 1.5
        for (std::size_t ear = 0; ear < ears_; ++ear) {
            auto& lane = lanes_[ear];
            for (std::size_t n = 0; n < kSize; ++n) {
                const auto sample = ear == 0 ? work_[n].real() : work_[n].imag();
                lane.overlap[n] += sample * window_[n] * scale;
            }
            std::copy_n(lane.overlap.begin(), kHop, lane.ready.begin());
            std::copy(lane.overlap.begin() + kHop, lane.overlap.end(), lane.overlap.begin());
            std::fill(lane.overlap.end() - kHop, lane.overlap.end(), 0.0f);
        }
        for (auto& lane : lanes_)
            std::copy(lane.input.begin() + kHop, lane.input.end(), lane.input.begin());
    }

    void processLane(Lane& lane, const std::size_t ear, const Settings& settings, const bool capture, const bool recapture,
                     const bool growing) noexcept
    {
        const auto* re = spectrumRe_[ear].data();
        const auto* im = spectrumIm_[ear].data();
        auto& pastPower = lane.pastPower[pastAt_];
        auto& pastFrequency = lane.pastFrequency[pastAt_];
        // What each bin holds now, and its frequency from how far its phase turned.
        for (std::size_t k = 0; k < kBins; ++k) {
            const auto magnitude = std::sqrt(re[k] * re[k] + im[k] * im[k]);
            const auto phase = std::atan2(im[k], re[k]);
            const auto turned = wrapped(static_cast<double>(phase) - lane.lastPhase[k] - static_cast<double>(k) * kAdvance);
            lane.lastPhase[k] = phase;
            // Its frequency is kept while it is quiet or falling fast: a sound
            // cut off moves its phases as much as its pitch does.
            if (magnitude > kQuietBin && magnitude >= 0.8f * lane.level[k])
                lane.frequency[k] = static_cast<float>(std::clamp(static_cast<double>(k) + turned / kAdvance, 0.0, static_cast<double>(kBins - 1)));
            lane.level[k] = magnitude;
            magnitude_[k] = magnitude;
            phase_[k] = phase;
            pastPower[k] = magnitude * magnitude;
            pastFrequency[k] = lane.frequency[k];
        }
        // Shifted: each bin's content moved to the bin of its frequency times
        // the ratio, its phase turning on at its new frequency.
        if (settings.identity) {
            std::copy(magnitude_.begin(), magnitude_.end(), shiftedMagnitude_.begin());
            std::copy(phase_.begin(), phase_.end(), lane.shiftedPhase.begin());
            std::copy(lane.frequency.begin(), lane.frequency.end(), lane.shiftedFrequency.begin());
        } else {
            remap(magnitude_.data(), lane.frequency.data(), settings.ratio, shiftedMagnitude_.data(), lane.shiftedFrequency.data());
            lockedPhases(shiftedMagnitude_.data(), lane.shiftedFrequency.data(), lane.shiftedPhase.data());
        }
        // The freeze's capture: the last frames' average, phases lined up with the sound's.
        if (capture || recapture || growing)
            captureInto(lane, capture, growing, settings.ratio);
        for (std::size_t k = 0; k < kBins; ++k)
            lane.frozen[k] += (lane.frozenGoal[k] - lane.frozen[k]) * 0.5f;

        // Where the magnitudes should be: smeared across bins, then followed slowly.
        const auto shaped = settings.smearOn || settings.blurOn;
        if (shaped) {
            for (std::size_t k = 0; k < kBins; ++k)
                power_[k] = shiftedMagnitude_[k] * shiftedMagnitude_[k];
            if (settings.smearOn) {
                std::copy(power_.begin(), power_.end(), unsmeared_.begin());
                smear(power_.data(), settings.smearOctaves);
            }
            const auto a = static_cast<float>(settings.blurCoefficient);
            for (std::size_t k = 0; k < kBins; ++k) {
                auto& blur = lane.blur[k];
                blur = settings.blurOn ? a * blur + (1.0f - a) * power_[k] : power_[k];
                if (blur < 1.0e-30f)
                    blur = 0.0f;
                target_[k] = std::sqrt(blur);
            }
        } else {
            for (std::size_t k = 0; k < kBins; ++k)
                lane.blur[k] = shiftedMagnitude_[k] * shiftedMagnitude_[k];
        }
        // The live sound out: as it is where it reaches its target, turned
        // down where the target is below it, with made-up phases for what
        // the target adds.
        auto* outRe = outRe_[ear].data();
        auto* outIm = outIm_[ear].data();
        scatterPhases(lane, settings.scatter);
        for (std::size_t k = 0; k < kBins; ++k) {
            const auto livePhase = static_cast<double>(lane.shiftedPhase[k]);
            const auto m = static_cast<double>(shiftedMagnitude_[k]);
            double xr = 0.0;
            double xi = 0.0;
            if (settings.identity) {
                xr = re[k];
                xi = im[k];
            } else {
                xr = m * std::cos(livePhase);
                xi = m * std::sin(livePhase);
            }
            auto yr = xr;
            auto yi = xi;
            if (!shaped) {
                lane.excessPhase[k] = static_cast<float>(livePhase);
            } else {
                const auto t = static_cast<double>(target_[k]);
                if (m >= t) {
                    const auto scale = m > 1.0e-20 ? t / m : 0.0;
                    yr = xr * scale;
                    yi = xi * scale;
                    lane.excessPhase[k] = static_cast<float>(livePhase);
                } else {
                    if (m >= 0.7 * t)
                        lane.excessPhase[k] = static_cast<float>(livePhase);
                    else
                        lane.excessPhase[k] = static_cast<float>(wrapped(
                            lane.excessPhase[k] + kAdvance * heldFrequency(lane.shiftedFrequency[k], k, smearedIn(settings, k))));
                    const auto excess = (t - m) * scatterGain_;
                    const auto phase = static_cast<double>(lane.excessPhase[k]) + scattered_[k];
                    yr = xr + excess * std::cos(phase);
                    yi = xi + excess * std::sin(phase);
                }
            }
            outRe[k] = static_cast<float>(yr * toneGain_[k]);
            outIm[k] = static_cast<float>(yi * toneGain_[k]);
        }
        if (fade_ <= 0.0)
            return;
        // The frozen sound: its magnitudes, shifted and smeared as the live
        // one, their phases turning on at their frequencies, scattered.
        if (settings.identity) {
            std::copy(lane.frozen.begin(), lane.frozen.end(), frozenShifted_.begin());
            std::copy(lane.frozenFrequency.begin(), lane.frozenFrequency.end(), frozenShiftedFrequency_.begin());
        } else {
            remap(lane.frozen.data(), lane.frozenFrequency.data(), settings.ratio, frozenShifted_.data(), frozenShiftedFrequency_.data());
        }
        std::copy(lane.frozenPhase.begin(), lane.frozenPhase.end(), previousPhase_.begin());
        lockedPhases(frozenShifted_.data(), frozenShiftedFrequency_.data(), lane.frozenPhase.data());
        if (settings.smearOn) {
            for (std::size_t k = 0; k < kBins; ++k)
                power_[k] = frozenShifted_[k] * frozenShifted_[k];
            std::copy(power_.begin(), power_.end(), unsmeared_.begin());
            smear(power_.data(), settings.smearOctaves);
            for (std::size_t k = 0; k < kBins; ++k)
                frozenShifted_[k] = std::sqrt(power_[k]);
        }
        const auto live = 1.0 - fade_;
        for (std::size_t k = 0; k < kBins; ++k) {
            // What the smear brought in turns on its own, at its bin's centre.
            if (smearedIn(settings, k))
                lane.frozenPhase[k] = static_cast<float>(wrapped(previousPhase_[k] + kAdvance * static_cast<double>(k)));
            const auto magnitude = static_cast<double>(frozenShifted_[k]) * toneGain_[k] * fade_ * scatterGain_;
            const auto phase = static_cast<double>(lane.frozenPhase[k]) + scattered_[k];
            outRe[k] = static_cast<float>(outRe[k] * live + magnitude * std::cos(phase));
            outIm[k] = static_cast<float>(outIm[k] * live + magnitude * std::sin(phase));
        }
    }

    // Most of a bin's power came from its neighbours (power_ smeared,
    // unsmeared_ as it was).
    [[nodiscard]] bool smearedIn(const Settings& settings, const std::size_t bin) const noexcept
    {
        return settings.smearOn && unsmeared_[bin] < 0.25f * power_[bin];
    }

    // Phases for a spectrum made up from magnitudes and frequencies: each
    // peak's turns on from where it was at its frequency, and the bins
    // around it (to the lowest between it and the next) follow it as a sound
    // centred in the frame has them, half a turn a bin, so each partial
    // stays whole and where the windows are open.
    void lockedPhases(const float* magnitude, const float* frequency, float* phase) noexcept
    {
        std::size_t count = 0;
        for (std::size_t k = 0; k < kBins; ++k) {
            const auto left = k > 0 ? magnitude[k - 1] : 0.0f;
            const auto right = k + 1 < kBins ? magnitude[k + 1] : 0.0f;
            if (magnitude[k] > 1.0e-12f && magnitude[k] > left && magnitude[k] >= right)
                peaks_[count++] = k;
        }
        if (count == 0) {
            for (std::size_t k = 0; k < kBins; ++k)
                phase[k] = static_cast<float>(wrapped(phase[k] + kAdvance * frequency[k]));
            return;
        }
        // A peak turns at its frequency, no further than a bin from its own
        // (a partial's always is; noise's estimates wander).
        for (std::size_t i = 0; i < count; ++i) {
            const auto centre = static_cast<double>(peaks_[i]);
            const auto turning = std::clamp(static_cast<double>(frequency[peaks_[i]]), centre - 1.0, centre + 1.0);
            peakPhase_[i] = wrapped(phase[peaks_[i]] + kAdvance * turning);
        }
        std::size_t start = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto peak = peaks_[i];
            auto end = kBins;
            if (i + 1 < count) {
                end = peak + 1;
                for (auto k = peak + 1; k < peaks_[i + 1]; ++k)
                    if (magnitude[k] < magnitude[end])
                        end = k;
            }
            for (auto k = start; k < end; ++k) {
                const auto apart = k >= peak ? k - peak : peak - k;
                phase[k] = static_cast<float>(apart % 2 == 0 ? peakPhase_[i] : wrapped(peakPhase_[i] + kPi));
            }
            start = end;
        }
    }

    // The last frames' power averaged, their frequencies weighed by it; at
    // once (a new freeze) its phases set so the first frozen frame goes on
    // from the sound. Growing: only when it is louder than what is held.
    void captureInto(Lane& lane, const bool atOnce, const bool growing, const double ratio) noexcept
    {
        auto louder = !growing;
        if (growing) {
            auto held = 0.0;
            auto now = 0.0;
            for (std::size_t k = 0; k < kBins; ++k) {
                held += static_cast<double>(lane.frozenGoal[k]) * lane.frozenGoal[k];
                for (const auto& row : lane.pastPower)
                    now += static_cast<double>(row[k]) / givreCaptureFrames;
            }
            louder = now > held;
        }
        if (!louder)
            return;
        for (std::size_t k = 0; k < kBins; ++k) {
            auto power = 0.0;
            auto weighted = 0.0;
            for (std::size_t frame = 0; frame < static_cast<std::size_t>(givreCaptureFrames); ++frame) {
                power += lane.pastPower[frame][k];
                weighted += static_cast<double>(lane.pastPower[frame][k]) * lane.pastFrequency[frame][k];
            }
            lane.frozenGoal[k] = static_cast<float>(std::sqrt(power / givreCaptureFrames));
            lane.frozenFrequency[k] = power > 1.0e-20 ? static_cast<float>(weighted / power) : lane.frequency[k];
            // The phases (kept by the bin it is heard at) go on from the
            // sound's: all of them for a new freeze, those of what is new in
            // it when caught again (what goes on keeps turning as it was).
            const auto renewed = atOnce || growing || lane.frozenGoal[k] > 2.0f * lane.frozen[k];
            if (atOnce || growing)
                lane.frozen[k] = lane.frozenGoal[k];
            const auto heard = static_cast<std::size_t>(std::lround(static_cast<double>(k) * ratio));
            if (renewed && heard < kBins)
                lane.frozenPhase[heard] = static_cast<float>(wrapped(lane.shiftedPhase[heard] - kAdvance * lane.shiftedFrequency[heard]));
        }
    }

    // The spectrum moved by the ratio. Down, each bin's content goes to the
    // bin of its frequency times the ratio (several gathering in one); up,
    // each bin reads between the bins its frequency came from (none left
    // empty between). Scaled, either way, so partials and noise come out
    // about as loud as they went in.
    void remap(const float* magnitude, const float* frequency, const double ratio, float* intoMagnitude, float* intoFrequency) noexcept
    {
        std::fill(intoMagnitude, intoMagnitude + kBins, 0.0f);
        const auto top = static_cast<double>(kBins - 1);
        const auto scale = static_cast<float>(std::pow(ratio, ratio > 1.0 ? -0.25 : 0.25));
        if (ratio > 1.0) {
            for (std::size_t to = 0; to < kBins; ++to) {
                const auto from = static_cast<double>(to) / ratio;
                const auto lower = static_cast<std::size_t>(from);
                if (lower + 1 >= kBins)
                    break;
                const auto fraction = static_cast<float>(from - static_cast<double>(lower));
                intoMagnitude[to] = (magnitude[lower] + (magnitude[lower + 1] - magnitude[lower]) * fraction) * scale;
                const auto nearest = fraction < 0.5f ? lower : lower + 1;
                intoFrequency[to] = static_cast<float>(std::clamp(static_cast<double>(frequency[nearest]) * ratio, 0.0, top));
            }
            return;
        }
        std::fill(best_.begin(), best_.end(), 0.0f);
        for (std::size_t k = 0; k < kBins; ++k) {
            const auto to = static_cast<std::size_t>(std::lround(static_cast<double>(k) * ratio));
            if (to >= kBins)
                break;
            intoMagnitude[to] += magnitude[k] * scale;
            if (magnitude[k] > best_[to]) {
                best_[to] = magnitude[k];
                intoFrequency[to] = static_cast<float>(std::clamp(static_cast<double>(frequency[k]) * ratio, 0.0, top));
            }
        }
    }

    // Power averaged over the bins within that many octaves either side.
    void smear(float* power, const double octaves) noexcept
    {
        prefix_[0] = 0.0;
        for (std::size_t k = 0; k < kBins; ++k)
            prefix_[k + 1] = prefix_[k] + power[k];
        const auto factor = std::pow(2.0, octaves);
        for (std::size_t k = 0; k < kBins; ++k) {
            const auto low = static_cast<std::size_t>(std::floor(static_cast<double>(k) / factor));
            const auto high = std::min(kBins - 1, static_cast<std::size_t>(std::ceil(static_cast<double>(k) * factor)));
            const auto spread = static_cast<double>(high - low + 1);
            target_[k] = static_cast<float>(std::max(0.0, (prefix_[high + 1] - prefix_[low]) / spread));
        }
        std::copy(target_.begin(), target_.end(), power);
    }

    // The output's spectrum in its bands, falling 30 dB a second.
    void showSpectrum() noexcept
    {
        const auto scale = 4.0 / static_cast<double>(kSize);
        for (int band = 0; band < givreBands; ++band) {
            const auto b = static_cast<std::size_t>(band);
            auto peak = 0.0;
            for (std::size_t ear = 0; ear < ears_; ++ear) {
                const auto magnitudeAt = [&](const std::size_t k) {
                    return std::sqrt(static_cast<double>(outRe_[ear][k]) * outRe_[ear][k] + static_cast<double>(outIm_[ear][k]) * outIm_[ear][k]);
                };
                if (bandHigh_[b] - bandLow_[b] < 1.0) {
                    const auto centre = std::clamp(0.5 * (bandLow_[b] + bandHigh_[b]), 0.0, static_cast<double>(kBins - 2));
                    const auto lower = static_cast<std::size_t>(centre);
                    const auto fraction = centre - static_cast<double>(lower);
                    peak = std::max(peak, magnitudeAt(lower) * (1.0 - fraction) + magnitudeAt(lower + 1) * fraction);
                } else {
                    const auto first = std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil(bandLow_[b])), 1, kBins - 1);
                    const auto last = std::clamp<std::size_t>(static_cast<std::size_t>(std::floor(bandHigh_[b])), first, kBins - 1);
                    for (auto k = first; k <= last; ++k)
                        peak = std::max(peak, magnitudeAt(k));
                }
            }
            const auto db = std::max(static_cast<double>(givreFloorDb), gainToDb(peak * scale));
            levels_[b] = std::max(db, levels_[b] - fall_);
            readings_[static_cast<std::size_t>(givreReadingSpectrum) + b].store(static_cast<float>(levels_[b]), std::memory_order_relaxed);
        }
    }

    double rate_ { 48'000.0 };
    std::size_t ears_ { 2 };
    Fft fft_;
    std::vector<Complex> work_;
    std::vector<float> window_;
    std::array<Lane, 2> lanes_;
    std::array<std::vector<float>, kChannels> dry_;
    std::size_t hopAt_ {};
    std::size_t dryAt_ {};
    std::size_t pastAt_ {};
    std::array<std::vector<float>, 2> spectrumRe_;
    std::array<std::vector<float>, 2> spectrumIm_;
    std::array<std::vector<float>, 2> outRe_;
    std::array<std::vector<float>, 2> outIm_;
    std::vector<float> magnitude_;
    std::vector<float> phase_;
    std::vector<float> shiftedMagnitude_;
    std::vector<float> power_;
    std::vector<float> unsmeared_;
    std::vector<float> target_;
    std::vector<float> best_;
    std::vector<float> frozenShifted_;
    std::vector<float> frozenShiftedFrequency_;
    std::vector<float> toneGain_;
    std::vector<float> scattered_;
    std::vector<float> previousPhase_;
    std::vector<std::size_t> peaks_;
    std::vector<double> peakPhase_;
    double scatterGain_ { 1.0 };
    std::vector<double> prefix_;
    float designedTone_ { 1.0e9f };
    bool frozenOn_ {};
    bool frozenQuiet_ { true };
    int listening_ {};
    double fade_ {};
    double fadeStep_ { 0.1 };
    long long syncBeat_ { kNoBeat };
    std::array<double, givreBands> bandLow_ {};
    std::array<double, givreBands> bandHigh_ {};
    std::array<double, givreBands> levels_ {};
    double fall_ {};
    double glide_ {};
    double mix_ {};
    double gain_ { 1.0 };
    std::array<std::atomic<float>, givreReadingCount> readings_ {};
};

} // namespace

std::unique_ptr<EffectProcessor> makeSpaceEffect(const EffectDescriptor& descriptor)
{
    if (descriptor.kind == NativeDspNodeKind::empreinte)
        return std::make_unique<Empreinte>(descriptor);
    if (descriptor.kind == NativeDspNodeKind::givre)
        return std::make_unique<Givre>(descriptor);
    return {};
}

} // namespace cydaw::engine
