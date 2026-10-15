#include <cydaw/engine/PitchEffects.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/PitchDesign.h>
#include <cydaw/engine/PitchTracker.h>

#include "DspParts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// Harmonie and Justesse share one pitch shifter made of two parts.
//
// For a voice (one pitch at a time), pitch-synchronous grains: the sound's
// pitch marks are found one period apart (the tracker gives the period, a
// correlation with the period before puts each mark on the same point of
// the wave), and a grain two periods long is cut around each, windowed, and
// laid down again at the new period. The grains are not resampled, so what
// shapes the voice (its formants) stays where it was while its pitch moves.
// At the ratio 1 the grains fall back exactly where they were cut and sum
// to the sound itself.
//
// For the rest (breath, noise, chords, drums), grains read at the new speed
// (so resampled, formants and all), each a window long, half a window apart,
// faded in and out by Hann windows that sum to one. Each new grain starts
// where its beginning best matches what the grain before is about to read
// (a correlation over a few milliseconds), so the splices fall in phase and
// a held note keeps its pitch exactly.
//
// Both read the sound `latency` samples late; the dry sound waits as long.

namespace cydaw::engine {

namespace {

using namespace dsp;
using namespace pitchfx;

// The four-point cubic (Hermite) between x0 and x1.
inline double hermite(const double xm1, const double x0, const double x1, const double x2, const double t) noexcept
{
    const auto c1 = 0.5 * (x1 - xm1);
    const auto c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    const auto c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

// The sound's last samples, addressed by their time (a sample count from
// the start that never wraps): written once each, read anywhere in the
// last `size` samples, between samples by the cubic.
class History {
public:
    void prepare(const std::size_t samples)
    {
        std::size_t size = 16;
        while (size < samples + 8)
            size <<= 1;
        data_.assign(size, 0.0f);
        mask_ = size - 1;
    }
    void reset() noexcept { std::fill(data_.begin(), data_.end(), 0.0f); }
    void write(const std::int64_t time, const float x) noexcept { data_[static_cast<std::size_t>(time) & mask_] = x; }
    [[nodiscard]] float at(const std::int64_t time) const noexcept { return data_[static_cast<std::size_t>(time) & mask_]; }
    [[nodiscard]] double read(const double time) const noexcept
    {
        const auto whole = std::floor(time);
        const auto i = static_cast<std::int64_t>(whole);
        return hermite(at(i - 1), at(i), at(i + 1), at(i + 2), time - whole);
    }

private:
    std::vector<float> data_;
    std::size_t mask_ {};
};

// One answer of the tracker: the time its window was centred on.
struct Answer {
    std::int64_t centre {};
    float hz {};
    float confidence {};
};

// A pitch mark: where (a time between samples) and how far after the one
// before it (the period there).
struct Mark {
    double position {};
    double period {};
};

// The sound's pitch and its pitch marks as they come, and its recent past.
class PitchMarks {
public:
    // `lowest`..`highest` Hz for the tracker; `keep` samples of the past
    // kept; marks older than `markReach` samples forgotten.
    void prepare(const double rate, const double lowest, const double highest, const std::size_t keep, const double markReach)
    {
        rate_ = rate;
        tracker_.prepare(rate, lowest, highest);
        signal_.prepare(keep);
        markReach_ = markReach;
        const auto longest = rate / std::max(20.0, lowest * 0.9);
        scores_.assign(static_cast<std::size_t>(2.0 * (kReach * longest + 2.0) + 8.0), 0.0);
        reset();
    }

    void reset() noexcept
    {
        tracker_.reset();
        signal_.reset();
        answerCount_ = 0;
        first_ = 0;
        end_ = 0;
        following_ = false;
    }

    void setRange(const double lowest, const double highest) noexcept { tracker_.setRange(lowest, highest); }
    void setThreshold(const float threshold) noexcept { tracker_.setThreshold(threshold); }

    // The mono sound's sample at `time`: kept, heard by the tracker,
    // searched for marks.
    void push(const float x, const std::int64_t time) noexcept
    {
        signal_.write(time, x);
        if (tracker_.push(x)) {
            auto& answer = answers_[static_cast<std::size_t>(answerCount_) & (kAnswers - 1)];
            // Its window: four hops, ending now.
            answer.centre = time - 2 * static_cast<std::int64_t>(tracker_.hopSamples());
            answer.hz = tracker_.frequency();
            answer.confidence = tracker_.confidence();
            ++answerCount_;
        }
        place(time);
        while (end_ > first_ && (mark(first_).position < static_cast<double>(time) - markReach_ || end_ - first_ >= kMarks - 1))
            ++first_;
    }

    [[nodiscard]] const History& signal() const noexcept { return signal_; }
    [[nodiscard]] double hopSeconds() const noexcept { return static_cast<double>(tracker_.hopSamples()) / rate_; }

    // The last answer whose window was centred at or before `time`: its
    // number (-1 for none yet), and the answer.
    [[nodiscard]] std::int64_t answerIndexAt(const std::int64_t time) const noexcept
    {
        const auto oldest = std::max<std::int64_t>(0, answerCount_ - static_cast<std::int64_t>(kAnswers));
        for (auto index = answerCount_ - 1; index >= oldest; --index)
            if (answer(index).centre <= time)
                return index;
        return -1;
    }
    [[nodiscard]] const Answer& answer(const std::int64_t index) const noexcept
    {
        static constexpr Answer none {};
        return index < 0 ? none : answers_[static_cast<std::size_t>(index) & (kAnswers - 1)];
    }
    // An answer with one lost pitch bridged: a breathy voice often loses a
    // single answer, which should not stop its grains (at the end of a
    // note, the pitch lasts one answer longer).
    [[nodiscard]] Answer heard(const std::int64_t index) const noexcept
    {
        auto bridged = answer(index);
        if (bridged.hz <= 0.0f && index > 0)
            bridged.hz = answer(index - 1).hz;
        return bridged;
    }
    [[nodiscard]] Answer heardAt(const std::int64_t time) const noexcept { return heard(answerIndexAt(time)); }

    [[nodiscard]] bool has(const std::int64_t index) const noexcept { return index >= first_ && index < end_; }
    [[nodiscard]] const Mark& mark(const std::int64_t index) const noexcept
    {
        return marks_[static_cast<std::size_t>(index) & (kMarks - 1)];
    }
    // The mark nearest a time; -1 for none.
    [[nodiscard]] std::int64_t nearest(const double time) const noexcept
    {
        if (end_ <= first_)
            return -1;
        const auto after = lowerBound(time);
        if (after >= end_)
            return end_ - 1;
        if (after == first_)
            return after;
        return time - mark(after - 1).position <= mark(after).position - time ? after - 1 : after;
    }
    // The first mark whose grain (a period each side) starts after `time`;
    // -1 for none.
    [[nodiscard]] std::int64_t firstStartingAfter(const double time) const noexcept
    {
        for (auto index = lowerBound(time); index < end_; ++index)
            if (mark(index).position - mark(index).period > time)
                return index;
        return -1;
    }

private:
    static constexpr std::size_t kAnswers = 64;
    static constexpr std::size_t kMarks = 1'024;
    // How far a mark may stray from one period after the last (of a period).
    static constexpr double kReach = 0.08;

    [[nodiscard]] std::int64_t lowerBound(const double time) const noexcept
    {
        auto low = first_;
        auto high = end_;
        while (low < high) {
            const auto middle = low + (high - low) / 2;
            if (mark(middle).position < time)
                low = middle + 1;
            else
                high = middle;
        }
        return low;
    }

    void append(const double position, const double period) noexcept
    {
        marks_[static_cast<std::size_t>(end_) & (kMarks - 1)] = { position, period };
        ++end_;
    }

    // Puts the marks the sound heard so far allows: one period after the
    // last, where the period ending there looks most like the period ending
    // at the last.
    void place(const std::int64_t time) noexcept
    {
        const auto now = heard(answerCount_ - 1);
        if (now.hz <= 0.0f) {
            following_ = false;
            return;
        }
        const auto period = rate_ / static_cast<double>(now.hz);
        const auto reach = std::ceil(period * kReach) + 1.0;
        const auto hasLast = end_ > first_;
        const auto lastPosition = hasLast ? mark(end_ - 1).position : -1.0e18;
        if (!following_ || lastPosition < static_cast<double>(time) - 4.0 * period) {
            // A new start: the highest sample of the last period.
            auto start = time - static_cast<std::int64_t>(period);
            if (hasLast)
                start = std::max(start, static_cast<std::int64_t>(std::ceil(lastPosition + 0.5 * period)));
            if (start > time)
                return;
            auto best = time;
            auto peak = -1.0e30f;
            for (auto t = start; t <= time; ++t)
                if (const auto x = signal_.at(t); x > peak) {
                    peak = x;
                    best = t;
                }
            append(static_cast<double>(best), period);
            following_ = true;
            return;
        }
        for (int guard = 0; guard < 4; ++guard) {
            const auto last = mark(end_ - 1).position;
            const auto candidate = last + period;
            const auto low = static_cast<std::int64_t>(std::lround(candidate - reach));
            const auto high = std::min(static_cast<std::int64_t>(std::lround(candidate + reach)),
                                       low + static_cast<std::int64_t>(scores_.size()) - 1);
            if (high > time)
                return;
            const auto reference = static_cast<std::int64_t>(std::lround(last));
            const auto length = std::max<std::int64_t>(8, static_cast<std::int64_t>(period));
            const auto stride = std::max<std::int64_t>(1, length / 160);
            auto referenceEnergy = 0.0;
            for (std::int64_t k = 0; k < length; k += stride) {
                const auto r = static_cast<double>(signal_.at(reference - k));
                referenceEnergy += r * r;
            }
            auto best = std::int64_t { 0 };
            auto bestScore = -2.0;
            for (auto m = low; m <= high; ++m) {
                auto cross = 0.0;
                auto energy = 0.0;
                for (std::int64_t k = 0; k < length; k += stride) {
                    const auto x = static_cast<double>(signal_.at(m - k));
                    cross += x * static_cast<double>(signal_.at(reference - k));
                    energy += x * x;
                }
                const auto score = cross / std::sqrt(energy * referenceEnergy + 1.0e-24);
                scores_[static_cast<std::size_t>(m - low)] = score;
                if (score > bestScore) {
                    bestScore = score;
                    best = m - low;
                }
            }
            // The distance from the reference, between samples by the
            // parabola through the best and its neighbours; the new mark that
            // far from the last (the reference was the last, rounded).
            auto spacing = static_cast<double>(low + best - reference);
            if (best > 0 && low + best < high) {
                const auto a = scores_[static_cast<std::size_t>(best - 1)];
                const auto b = scores_[static_cast<std::size_t>(best)];
                const auto c = scores_[static_cast<std::size_t>(best + 1)];
                const auto bend = a - 2.0 * b + c;
                if (bend < -1.0e-12)
                    spacing += std::clamp(0.5 * (a - c) / bend, -0.5, 0.5);
            }
            spacing = std::clamp(spacing, 0.5 * period, 1.5 * period);
            append(last + spacing, spacing);
        }
    }

    double rate_ { 48'000.0 };
    PitchTracker tracker_;
    History signal_;
    std::array<Answer, kAnswers> answers_ {};
    std::int64_t answerCount_ {};
    std::array<Mark, kMarks> marks_ {};
    std::int64_t first_ {};
    std::int64_t end_ {};
    double markReach_ {};
    bool following_ {};
    std::vector<double> scores_;
};

// The pitch-synchronous voice: grains cut around the marks, laid down at
// the new period.
class GrainVoice {
public:
    void reset() noexcept
    {
        for (auto& grain : grains_)
            grain.live = false;
        anchored_ = false;
        next_ = 0.0;
    }

    // Whether a grain sounds now.
    [[nodiscard]] bool sounding(const std::int64_t time) const noexcept
    {
        for (const auto& grain : grains_)
            if (grain.live && grain.first <= time)
                return true;
        return false;
    }

    // Plans the grains due by `time`, read `latency` samples late, each's
    // pitch multiplied by `ratioOf(period)`, the period (samples) where it
    // is cut.
    template <typename RatioOf>
    void plan(const PitchMarks& marks, const std::int64_t time, const double latency, RatioOf&& ratioOf) noexcept
    {
        const auto now = static_cast<double>(time);
        for (int guard = 0; guard < 16; ++guard) {
            if (!anchored_) {
                // Start again on a mark, so that at the ratio 1 every grain
                // lands where it was cut.
                const auto index = marks.firstStartingAfter(now + 1.0 - latency);
                if (index < 0)
                    return;
                next_ = marks.mark(index).position + latency;
                anchored_ = true;
            }
            const auto index = marks.nearest(next_ - latency);
            if (index < 0) {
                anchored_ = false;
                return;
            }
            const auto& mark = marks.mark(index);
            if (std::abs(mark.position - (next_ - latency)) > 1.5 * mark.period || next_ + mark.period < now) {
                // The marks stopped (no pitch): wait for them.
                anchored_ = false;
                return;
            }
            const auto left = mark.period;
            if (next_ - left > now + 1.0)
                return;
            // Its right half reaches the next mark when it is known, so that
            // neighbouring grains' windows sum to one.
            const auto right = marks.has(index + 1) ? marks.mark(index + 1).period : left;
            const auto r = std::clamp(static_cast<double>(ratioOf(right)), 0.2, 5.0);
            add(next_, left, right, std::min(-3.0, mark.position - next_), 1.0 / std::sqrt(r));
            next_ += right / r;
        }
    }

    // Adds this sample of the grains, read from the histories, into `out`.
    template <std::size_t N>
    void render(const std::array<const History*, N>& from, const std::size_t count, const std::int64_t time,
                std::array<double, N>& out) noexcept
    {
        const auto t = static_cast<double>(time);
        for (auto& grain : grains_) {
            if (!grain.live || time < grain.first)
                continue;
            if (time > grain.last) {
                grain.live = false;
                continue;
            }
            // Its window, a raised cosine on each side of its centre, turned
            // sample by sample.
            if (!grain.started || (!grain.rightHalf && t > grain.centre)) {
                grain.rightHalf = t > grain.centre;
                const auto half = grain.rightHalf ? grain.right : grain.left;
                const auto phase = kPi * (t - grain.centre) / half;
                grain.c = std::cos(phase);
                grain.s = std::sin(phase);
                grain.stepC = std::cos(kPi / half);
                grain.stepS = std::sin(kPi / half);
                grain.started = true;
            }
            const auto weight = grain.gain * 0.5 * (1.0 + grain.c);
            const auto at = t + grain.offset;
            for (std::size_t channel = 0; channel < count; ++channel)
                out[channel] += weight * from[channel]->read(at);
            const auto c = grain.c * grain.stepC - grain.s * grain.stepS;
            grain.s = grain.s * grain.stepC + grain.c * grain.stepS;
            grain.c = c;
        }
    }

private:
    struct Grain {
        double centre {};
        double left {};
        double right {};
        double offset {};
        double gain {};
        std::int64_t first {};
        std::int64_t last {};
        double c {};
        double s {};
        double stepC {};
        double stepS {};
        bool started {};
        bool rightHalf {};
        bool live {};
    };

    void add(const double centre, const double left, const double right, const double offset, const double gain) noexcept
    {
        for (auto& grain : grains_)
            if (!grain.live) {
                grain = {};
                grain.centre = centre;
                grain.left = std::max(1.0, left);
                grain.right = std::max(1.0, right);
                grain.offset = offset;
                grain.gain = gain;
                grain.first = static_cast<std::int64_t>(std::floor(centre - grain.left)) + 1;
                grain.last = static_cast<std::int64_t>(std::ceil(centre + grain.right)) - 1;
                grain.live = true;
                return;
            }
    }

    std::array<Grain, 24> grains_ {};
    double next_ {};
    bool anchored_ {};
};

// Keeps the formant-keeping grains near the sound's level: harmonics moved
// under a kept envelope catch more or less of it (an octave up on a voice
// can lose 6 dB). The ratio of the two levels over the last ~100 ms, within
// 6 dB either way, measured only while the grains alone sound.
class LevelMatch {
public:
    void prepare(const double rate)
    {
        follow_ = smoothing(100.0, rate);
        settle_ = static_cast<int>(0.03 * rate);
    }
    void reset() noexcept
    {
        heard_ = 0.0;
        made_ = 0.0;
        counted_ = 0;
        gain_ = 1.0;
    }
    // The sound read and the grains made of it this sample (`measuring`
    // while the grains alone sound); the gain for the grains.
    double operator()(const double source, const double made, const bool measuring) noexcept
    {
        if (!measuring) {
            heard_ = 0.0;
            made_ = 0.0;
            counted_ = 0;
            return gain_;
        }
        heard_ = source * source + follow_ * (heard_ - source * source);
        made_ = made * made + follow_ * (made_ - made * made);
        if (++counted_ >= settle_ && heard_ > 1.0e-9 && made_ > 1.0e-9) {
            counted_ = settle_;
            gain_ = std::clamp(std::sqrt(heard_ / made_), 0.5, 2.0);
        }
        return gain_;
    }

private:
    double follow_ {};
    double heard_ {};
    double made_ {};
    double gain_ { 1.0 };
    int counted_ {};
    int settle_ { 1'440 };
};

// The voice for what has no single pitch: grains read at the new speed,
// each placed where it best continues the grain before.
class SpliceVoice {
public:
    void prepare(const double rate)
    {
        coarse_ = std::max<std::int64_t>(1, std::lround(rate / 12'000.0));
        search_ = std::max<std::int64_t>(8, std::lround(kSearchMs * 0.001 * rate));
        shortestMatch_ = std::max<std::int64_t>(16, std::lround(0.008 * rate));
        longestMatch_ = std::max<std::int64_t>(32, std::lround(0.025 * rate));
        scores_.assign(static_cast<std::size_t>(2 * search_ / coarse_ + 2), 0.0);
    }

    void reset() noexcept
    {
        for (auto& grain : grains_)
            grain.live = false;
        next_ = -1;
    }

    // The longest grain for a ratio: its read must stay between the present
    // and the latency's other side, the search's reach left on each side.
    [[nodiscard]] double longestFor(const double ratio, const double latency) const noexcept
    {
        const auto room = 2.0 * (latency - static_cast<double>(search_) - 4.0);
        const auto speed = std::abs(ratio - 1.0);
        return speed > 1.0e-6 ? std::max(32.0, room / speed) : 1.0e9;
    }

    // This sample of the grains (`window` samples long, read `latency`
    // samples late on average) from the histories into `out`; `match` is
    // the sound the splices are matched on, null for none.
    template <std::size_t N>
    void render(const std::array<const History*, N>& from, const std::size_t count, const History* match, const std::int64_t time,
                const double ratio, const double window, const double latency, std::array<double, N>& out) noexcept
    {
        const auto r = std::clamp(ratio, 0.2, 5.0);
        if (next_ < 0) {
            // A start: a grain half way through, so the windows sum to one at once.
            const auto length = lengthFor(r, window, latency);
            start(time - length / 2, length, r, latency, nullptr, time);
            next_ = time;
        }
        if (time >= next_) {
            const auto length = lengthFor(r, window, latency);
            start(time, length, r, latency, match, time);
            next_ = time + length / 2;
        }
        const auto newest = static_cast<double>(time) - 3.0;
        for (auto& grain : grains_) {
            if (!grain.live)
                continue;
            if (grain.age >= grain.length) {
                grain.live = false;
                continue;
            }
            const auto weight = 0.5 * (1.0 - grain.c);
            const auto at = std::min(grain.position, newest);
            for (std::size_t channel = 0; channel < count; ++channel)
                out[channel] += weight * from[channel]->read(at);
            grain.position += r;
            ++grain.age;
            const auto c = grain.c * grain.stepC - grain.s * grain.stepS;
            grain.s = grain.s * grain.stepC + grain.c * grain.stepS;
            grain.c = c;
        }
    }

private:
    // How far (ms) a grain's start may move to fall in phase: half the
    // period of a bass's low E (41 Hz), and some.
    static constexpr double kSearchMs = 12.5;

    struct Grain {
        double position {};
        std::int64_t length {};
        std::int64_t age {};
        double c {};
        double s {};
        double stepC {};
        double stepS {};
        bool live {};
    };

    // An even length, so the next grain starts exactly half way.
    [[nodiscard]] std::int64_t lengthFor(const double ratio, const double window, const double latency) const noexcept
    {
        const auto length = std::clamp(window, 32.0, longestFor(ratio, latency));
        return std::max<std::int64_t>(32, 2 * static_cast<std::int64_t>(length * 0.5));
    }

    // A grain started at `at` (now or, at the very start, half a grain ago).
    void start(const std::int64_t at, const std::int64_t length, const double ratio, const double latency, const History* match,
               const std::int64_t time) noexcept
    {
        // Where it would read: centred `latency` behind, over its length.
        auto position = static_cast<double>(at) - latency - (ratio - 1.0) * static_cast<double>(length) * 0.5;
        if (match != nullptr)
            if (const auto* before = newestGrain(); before != nullptr)
                position = aligned(*match, before->position, position, time,
                                   std::clamp(static_cast<std::int64_t>(ratio * static_cast<double>(length) * 0.5), shortestMatch_,
                                              longestMatch_));
        for (auto& grain : grains_)
            if (!grain.live) {
                grain.live = true;
                grain.length = length;
                grain.age = time - at;
                grain.position = position + ratio * static_cast<double>(grain.age);
                const auto phase = 2.0 * kPi * static_cast<double>(grain.age) / static_cast<double>(length);
                grain.c = std::cos(phase);
                grain.s = std::sin(phase);
                grain.stepC = std::cos(2.0 * kPi / static_cast<double>(length));
                grain.stepS = std::sin(2.0 * kPi / static_cast<double>(length));
                return;
            }
    }

    [[nodiscard]] const Grain* newestGrain() const noexcept
    {
        const Grain* newest = nullptr;
        for (const auto& grain : grains_)
            if (grain.live && (newest == nullptr || grain.age < newest->age))
                newest = &grain;
        return newest;
    }

    // The start near `wanted` whose next `span` samples look most like those
    // the grain before reads next (from `continuing`; the sound the two
    // share while they overlap, 8 to 25 ms, so a low note's whole period):
    // coarse steps, then the samples around the best.
    [[nodiscard]] double aligned(const History& x, const double continuing, const double wanted, const std::int64_t time,
                                 const std::int64_t span) noexcept
    {
        const auto base = static_cast<std::int64_t>(std::lround(wanted));
        const auto from = static_cast<std::int64_t>(std::lround(continuing));
        const auto length = std::min(span, time - 3 - std::max(from, base + search_));
        if (length < 16)
            return wanted;
        auto referenceEnergy = 0.0;
        for (std::int64_t k = 0; k < length; k += coarse_) {
            const auto a = static_cast<double>(x.at(from + k));
            referenceEnergy += a * a;
        }
        if (referenceEnergy < 1.0e-12)
            return wanted;
        // Every `coarse_` sample of the stretch compared.
        const auto score = [&](const std::int64_t shift) {
            auto cross = 0.0;
            auto energy = 0.0;
            for (std::int64_t k = 0; k < length; k += coarse_) {
                const auto b = static_cast<double>(x.at(base + shift + k));
                cross += b * static_cast<double>(x.at(from + k));
                energy += b * b;
            }
            return cross / std::sqrt(energy * referenceEnergy + 1.0e-24);
        };
        // The coarse starts' peaks; of those nearly as good as the best (a
        // held note has one every period), the nearest the wanted one, so
        // the grains stay centred on the latency.
        auto& scores = scores_;
        const auto steps = std::min<std::int64_t>(static_cast<std::int64_t>(scores.size()), 2 * search_ / coarse_ + 1);
        auto top = -2.0;
        for (std::int64_t step = 0; step < steps; ++step) {
            scores[static_cast<std::size_t>(step)] = score(-search_ + step * coarse_);
            top = std::max(top, scores[static_cast<std::size_t>(step)]);
        }
        auto best = std::int64_t { 0 };
        auto nearest = search_ + coarse_;
        for (std::int64_t step = 0; step < steps; ++step) {
            const auto here = scores[static_cast<std::size_t>(step)];
            const auto peak = (step == 0 || here >= scores[static_cast<std::size_t>(step - 1)])
                              && (step == steps - 1 || here >= scores[static_cast<std::size_t>(step + 1)]);
            const auto shift = -search_ + step * coarse_;
            if (peak && here >= top - 0.05 && std::abs(shift) < nearest) {
                nearest = std::abs(shift);
                best = shift;
            }
        }
        // Then the top of that peak, sample by sample.
        auto bestScore = score(best);
        const auto centre = best;
        for (auto shift = centre - coarse_ + 1; shift < centre + coarse_; ++shift)
            if (shift != centre && std::abs(shift) <= search_)
                if (const auto s = score(shift); s > bestScore) {
                    bestScore = s;
                    best = shift;
                }
        return static_cast<double>(base + best);
    }

    std::array<Grain, 6> grains_ {};
    std::int64_t next_ { -1 };
    std::int64_t coarse_ { 4 };
    std::int64_t search_ { 480 };
    std::int64_t shortestMatch_ { 384 };
    std::int64_t longestMatch_ { 1'200 };
    std::vector<double> scores_;
};

inline double finite(const double x) noexcept
{
    return std::isfinite(x) ? x : 0.0;
}

// ---- Harmonie ----------------------------------------------------------------------------------

class Harmonie final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(8'000.0, spec.sampleRate);
        channels_ = std::clamp<std::size_t>(spec.channelCount, 1, kChannels);
        latency_ = static_cast<std::int64_t>(std::lround(pitchLatencyMs * 0.001 * rate_));
        const auto longestPeriod = rate_ / harmonieLowestHz;
        // The grains reach back the latency and two periods, the resampled
        // ones at most twice the latency.
        const auto latency = static_cast<double>(latency_);
        const auto keep = static_cast<std::size_t>(std::max(latency + 4.0 * longestPeriod, 2.5 * latency));
        marks_.prepare(rate_, harmonieLowestHz, harmonieHighestHz, keep, latency + 4.0 * longestPeriod);
        marks_.setThreshold(0.2f);
        for (std::size_t channel = 0; channel < channels_; ++channel)
            dry_[channel].prepare(static_cast<std::size_t>(latency_) + 8);
        for (auto& line : echo_)
            line.prepare(static_cast<std::size_t>(0.51 * rate_) + 8);
        for (auto& voice : splices_)
            voice.prepare(rate_);
        for (auto& match : levels_)
            match.prepare(rate_);
        reset();
    }

    void reset() noexcept override
    {
        marks_.reset();
        for (auto& line : dry_)
            line.reset();
        for (auto& line : echo_)
            line.reset();
        for (auto& voice : grains_)
            voice.reset();
        for (auto& voice : splices_)
            voice.reset();
        for (auto& match : levels_)
            match.reset();
        weights_ = {};
        time_ = 0;
        for (auto& reading : readings_)
            reading.store(0.0f, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return static_cast<std::uint32_t>(latency_); }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override { return static_cast<std::uint32_t>(latency_); }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto shown = std::min(count, readings_.size());
        for (std::size_t index = 0; index < shown; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return shown;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (channels == 0)
            return;
        const std::array<double, 2> ratios { harmonieRatioOf(value(harmonieShiftA), value(harmonieFineA)),
                                             harmonieRatioOf(value(harmonieShiftB), value(harmonieFineB)) };
        const std::array<double, 2> levels { std::clamp(static_cast<double>(value(harmonieLevelA)), 0.0, 1.0),
                                             std::clamp(static_cast<double>(value(harmonieLevelB)), 0.0, 1.0) };
        const std::array<PitchPanGains, 2> pans { harmoniePanOf(value(harmoniePanA)), harmoniePanOf(value(harmoniePanB)) };
        const auto dryLevel = std::clamp(static_cast<double>(value(harmonieDry)), 0.0, 1.0);
        const auto delay = std::clamp(static_cast<double>(value(harmonieDelay)), 0.0, 500.0) * 0.001 * rate_;
        const auto feedback = std::clamp(static_cast<double>(value(harmonieFeedback)), 0.0, 0.9);
        const auto gain = dbToGain(std::clamp(static_cast<double>(value(harmonieOutput)), -24.0, 24.0));
        const auto mode = harmonieModeOf(value(harmonieMode));
        const auto voiceMode = mode == harmonieModeVoice;
        // The resampled grains' window: shorter to keep attacks.
        const auto latency = static_cast<double>(latency_);
        auto window = std::clamp(static_cast<double>(value(harmonieWindow)), 10.0, 120.0) * 0.001 * rate_;
        if (mode == harmonieModePercussive)
            window *= harmoniePercussiveWindow;
        const auto fade = smoothing(4.0, rate_);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto time = time_;
            auto mono = 0.0;
            for (std::size_t channel = 0; channel < std::min<std::size_t>(channels, 2); ++channel)
                mono += static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
            mono = finite(mono / static_cast<double>(std::min<std::size_t>(channels, 2)));
            marks_.push(static_cast<float>(mono), time);
            for (std::size_t channel = 0; channel < channels; ++channel)
                dry_[channel].write(time, input.channel(static_cast<std::uint32_t>(channel))[frame]);
            const auto voiced = voiceMode && marks_.heardAt(time - latency_).hz > 0.0f;
            const std::array<const History*, 1> source { &marks_.signal() };
            std::array<double, 2> voices {};
            for (std::size_t voice = 0; voice < 2; ++voice) {
                auto& weight = weights_[voice];
                if (voiceMode || weight > 1.0e-4) {
                    grains_[voice].plan(marks_, time, latency, [&](double) { return ratios[voice]; });
                    const auto target = voiced && grains_[voice].sounding(time) ? 1.0 : 0.0;
                    weight = target + fade * (weight - target);
                } else {
                    grains_[voice].reset();
                    weight = 0.0;
                }
                auto sound = 0.0;
                if (weight > 1.0e-4) {
                    std::array<double, 1> grained {};
                    grains_[voice].render(source, 1, time, grained);
                    const auto matched = levels_[voice](marks_.signal().at(time - latency_), grained[0], weight > 0.999);
                    sound += weight * matched * grained[0];
                }
                if (weight < 1.0 - 1.0e-4) {
                    std::array<double, 1> spliced {};
                    splices_[voice].render(source, 1, &marks_.signal(), time, ratios[voice], window, latency, spliced);
                    sound += (1.0 - weight) * spliced[0];
                }
                voices[voice] = sound * levels[voice];
            }
            // The voices placed, then their echoes.
            std::array<double, 2> wet {};
            if (channels == 1) {
                wet[0] = voices[0] + voices[1];
            } else {
                wet[0] = voices[0] * pans[0].left + voices[1] * pans[1].left;
                wet[1] = voices[0] * pans[0].right + voices[1] * pans[1].right;
            }
            for (std::size_t side = 0; side < std::min<std::size_t>(channels, 2); ++side) {
                auto& line = echo_[side];
                if (delay >= 1.0) {
                    const auto heard = line.read(delay - 1.0);
                    auto fed = wet[side] + feedback * heard;
                    if (std::abs(fed) < 1.0e-20)
                        fed = 0.0;
                    line.push(std::clamp(fed, -8.0, 8.0));
                    wet[side] = heard;
                } else {
                    line.push(wet[side]);
                }
            }
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto dry = static_cast<double>(dry_[channel].at(time - latency_));
                const auto out = channel < 2 ? (dry * dryLevel + wet[channel]) * gain : dry;
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>(finite(out));
            }
            ++time_;
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
        readings_[harmonieReadHz].store(voiceMode ? marks_.heardAt(time_ - 1 - latency_).hz : 0.0f, std::memory_order_relaxed);
        readings_[harmonieReadRatioA].store(static_cast<float>(ratios[0]), std::memory_order_relaxed);
        readings_[harmonieReadRatioB].store(static_cast<float>(ratios[1]), std::memory_order_relaxed);
    }

private:
    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::int64_t latency_ {};
    std::int64_t time_ {};
    PitchMarks marks_;
    std::array<History, kChannels> dry_;
    std::array<Delay, 2> echo_;
    std::array<GrainVoice, 2> grains_;
    std::array<SpliceVoice, 2> splices_;
    std::array<LevelMatch, 2> levels_;
    std::array<double, 2> weights_ {};
    std::array<std::atomic<float>, harmonieReadingCount> readings_ {};
};

// ---- Justesse ----------------------------------------------------------------------------------

class Justesse final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(8'000.0, spec.sampleRate);
        channels_ = std::clamp<std::size_t>(spec.channelCount, 1, kChannels);
        latency_ = static_cast<std::int64_t>(std::lround(pitchLatencyMs * 0.001 * rate_));
        const auto longestPeriod = rate_ / justesseLowestHz;
        const auto latency = static_cast<double>(latency_);
        const auto keep = static_cast<std::size_t>(std::max(latency + 4.0 * longestPeriod, 2.5 * latency));
        marks_.prepare(rate_, justesseLowestHz, 1'600.0, keep, latency + 4.0 * longestPeriod);
        // The sound itself: the two sides the grains read (and the dry), the
        // channels above passed through as late.
        for (std::size_t channel = 0; channel < channels_; ++channel)
            sound_[channel].prepare(channel < 2 ? keep : static_cast<std::size_t>(latency_) + 8);
        splices_.prepare(rate_);
        level_.prepare(rate_);
        range_ = { -1.0f, -1.0f, -1.0f };
        reset();
    }

    void reset() noexcept override
    {
        marks_.reset();
        for (auto& line : sound_)
            line.reset();
        grains_.reset();
        splices_.reset();
        level_.reset();
        time_ = 0;
        heardAnswer_ = -1;
        weight_ = 0.0;
        correction_ = 0.0;
        timeConstant_ = 0.0;
        corrected_ = 0;
        target_ = -1;
        held_ = 0.0;
        silent_ = 1.0;
        hz_ = 0.0f;
        confidence_ = 0.0f;
        detected_ = -1.0;
        trailCount_ = 0.0;
        trailWrite_ = 0;
        detectedTrail_.fill(-1.0f);
        correctedTrail_.fill(-1.0f);
        for (auto& reading : readings_)
            reading.store(-1.0f, std::memory_order_relaxed);
        readings_[justesseReadHz].store(0.0f, std::memory_order_relaxed);
        readings_[justesseReadCents].store(0.0f, std::memory_order_relaxed);
        readings_[justesseReadConfidence].store(0.0f, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return static_cast<std::uint32_t>(latency_); }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override { return static_cast<std::uint32_t>(latency_); }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto shown = std::min(count, readings_.size());
        for (std::size_t index = 0; index < shown; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return shown;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (channels == 0)
            return;
        const auto low = std::clamp(value(justesseLowPitch), 50.0f, 400.0f);
        const auto high = std::clamp(value(justesseHighPitch), 300.0f, 1'600.0f);
        const auto threshold = justesseThresholdOf(value(justesseSensitivity));
        if (range_ != std::array<float, 3> { low, high, threshold }) {
            marks_.setRange(low, std::max(high, low * 1.5f));
            marks_.setThreshold(threshold);
            range_ = { low, high, threshold };
        }
        const auto transpose = std::clamp(static_cast<double>(value(justesseTranspose)), -12.0, 12.0);
        const auto amount = std::clamp(static_cast<double>(value(justesseAmount)), 0.0, 1.0);
        const auto mix = std::clamp(static_cast<double>(value(justesseMix)), 0.0, 1.0);
        const auto gain = dbToGain(std::clamp(static_cast<double>(value(justesseOutput)), -24.0, 24.0));
        const auto latency = static_cast<double>(latency_);
        const auto window = justesseNoiseWindowMs * 0.001 * rate_;
        const auto noiseRatio = pitchRatioOf(transpose);
        const auto fade = smoothing(4.0, rate_);
        const auto sides = std::min<std::size_t>(channels, 2);
        const std::array<const History*, 2> source { &sound_[0], &sound_[sides > 1 ? 1 : 0] };
        const auto trailStep = justesseTrailSeconds * rate_;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto time = time_;
            auto mono = 0.0;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto x = finite(static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]));
                sound_[channel].write(time, static_cast<float>(x));
                if (channel < 2)
                    mono += x;
            }
            marks_.push(static_cast<float>(mono / static_cast<double>(sides)), time);
            // What the tracker said of the sound now being read.
            if (const auto index = marks_.answerIndexAt(time - latency_); index != heardAnswer_) {
                heardAnswer_ = index;
                hear(marks_.heard(index), low, high);
            }
            grains_.plan(marks_, time, latency,
                         [&](const double period) { return pitchRatioOf(correct(period, time, amount) + transpose); });
            const auto target = hz_ > 0.0f && grains_.sounding(time) ? 1.0 : 0.0;
            weight_ = target + fade * (weight_ - target);
            std::array<double, 2> wet {};
            if (weight_ > 1.0e-4) {
                grains_.render(source, sides, time, wet);
                const auto made = (wet[0] + wet[sides - 1]) * 0.5;
                const auto matched = level_(marks_.signal().at(time - latency_), made, weight_ > 0.999);
                for (auto& side : wet)
                    side *= weight_ * matched;
            }
            if (weight_ < 1.0 - 1.0e-4) {
                std::array<double, 2> spliced {};
                splices_.render(source, sides, &marks_.signal(), time, noiseRatio, window, latency, spliced);
                for (std::size_t side = 0; side < sides; ++side)
                    wet[side] += (1.0 - weight_) * spliced[side];
            }
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto dry = static_cast<double>(sound_[channel].at(time - latency_));
                const auto out = channel < 2 ? (dry * (1.0 - mix) + wet[channel] * mix) * gain : dry;
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>(finite(out));
            }
            // The trails, one point every 20 ms.
            trailCount_ += 1.0;
            if (trailCount_ >= trailStep) {
                trailCount_ -= trailStep;
                detectedTrail_[trailWrite_] = static_cast<float>(detected_);
                correctedTrail_[trailWrite_] = detected_ >= 0.0 ? static_cast<float>(detected_ + correction_) : -1.0f;
                trailWrite_ = (trailWrite_ + 1) % static_cast<std::size_t>(justesseTrailLength);
            }
            ++time_;
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
        readings_[justesseReadHz].store(hz_, std::memory_order_relaxed);
        readings_[justesseReadTarget].store(static_cast<float>(target_), std::memory_order_relaxed);
        readings_[justesseReadCents].store(static_cast<float>(correction_ * 100.0), std::memory_order_relaxed);
        readings_[justesseReadConfidence].store(confidence_, std::memory_order_relaxed);
        for (std::size_t index = 0; index < static_cast<std::size_t>(justesseTrailLength); ++index) {
            const auto from = (trailWrite_ + index) % static_cast<std::size_t>(justesseTrailLength);
            readings_[static_cast<std::size_t>(justesseReadDetectedTrail) + index].store(detectedTrail_[from], std::memory_order_relaxed);
            readings_[static_cast<std::size_t>(justesseReadCorrectedTrail) + index].store(correctedTrail_[from], std::memory_order_relaxed);
        }
    }

private:
    // A new answer of the tracker reaches the sound being read: the note it
    // is, the note it goes to, how fast.
    void hear(const Answer& answer, const float low, const float high) noexcept
    {
        const auto hop = marks_.hopSeconds();
        confidence_ = answer.confidence;
        const auto voiced = answer.hz > 0.0f && answer.hz >= low * 0.97f && answer.hz <= high * 1.03f;
        if (!voiced) {
            hz_ = 0.0f;
            detected_ = -1.0;
            silent_ += hop;
            // A pause: the next note starts afresh, uncorrected.
            if (silent_ > 0.1) {
                target_ = -1;
                correction_ = 0.0;
                corrected_ = time_;
            }
            return;
        }
        silent_ = 0.0;
        hz_ = answer.hz;
        detected_ = pitchNoteOfHz(answer.hz);
        const auto key = justesseKeyOf(value(justesseKey));
        const auto& scale = justesseScaleOf(value(justesseScale));
        const auto next = justesseTargetOf(detected_, target_, key, scale);
        if (next != target_) {
            target_ = next;
            held_ = 0.0;
        } else {
            held_ += hop;
        }
        timeConstant_ = justesseTimeConstantOf(value(justesseSpeed), held_, value(justesseHumanize));
    }

    // The correction (semitones) for a grain cut where the period is
    // `period` samples: towards the target from the pitch of that very
    // period (so the robot setting lands exactly on the note, vibrato and
    // glides flattened), unless the tracker hears another note there (a
    // change the marks have not caught yet); the way there taken at the
    // speed set.
    double correct(const double period, const std::int64_t time, const double amount) noexcept
    {
        if (target_ < 0 || detected_ < 0.0)
            return correction_;
        auto local = pitchNoteOfHz(rate_ / std::max(1.0, period));
        if (std::abs(local - detected_) > 0.75)
            local = detected_;
        const auto wanted = (static_cast<double>(target_) - local) * amount;
        const auto elapsed = std::clamp(static_cast<double>(time - corrected_) / rate_, 0.0, 0.05);
        corrected_ = time;
        correction_ = timeConstant_ <= 1.0e-5 ? wanted : correction_ + (wanted - correction_) * (1.0 - std::exp(-elapsed / timeConstant_));
        return correction_;
    }

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::int64_t latency_ {};
    std::int64_t time_ {};
    PitchMarks marks_;
    std::array<History, kChannels> sound_;
    GrainVoice grains_;
    SpliceVoice splices_;
    LevelMatch level_;
    std::array<float, 3> range_ {};
    std::int64_t heardAnswer_ { -1 };
    double weight_ {};
    // The correction (semitones) as last applied, when, and how fast it
    // moves to the one wanted (a time constant, s).
    double correction_ {};
    std::int64_t corrected_ {};
    double timeConstant_ {};
    int target_ { -1 };
    double held_ {};
    double silent_ { 1.0 };
    float hz_ {};
    float confidence_ {};
    double detected_ { -1.0 };
    double trailCount_ {};
    std::size_t trailWrite_ {};
    std::array<float, justesseTrailLength> detectedTrail_ {};
    std::array<float, justesseTrailLength> correctedTrail_ {};
    std::array<std::atomic<float>, justesseReadingCount> readings_ {};
};

} // namespace

std::unique_ptr<EffectProcessor> makePitchEffect(const EffectDescriptor& descriptor)
{
    if (descriptor.kind == NativeDspNodeKind::harmonie)
        return std::make_unique<Harmonie>(descriptor);
    if (descriptor.kind == NativeDspNodeKind::justesse)
        return std::make_unique<Justesse>(descriptor);
    return {};
}

} // namespace cydaw::engine
