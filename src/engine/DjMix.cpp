#include "cydaw/engine/DjMix.h"

#include "cydaw/engine/Fft.h"
#include "cydaw/engine/Loudness.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace cydaw::engine::dj {

namespace {

constexpr double pi = std::numbers::pi;

using Biquad = ChannelStrip::Biquad;

Biquad normalised(const double b0, const double b1, const double b2, const double a0, const double a1, const double a2)
{
    Biquad q;
    q.b0 = static_cast<float>(b0 / a0);
    q.b1 = static_cast<float>(b1 / a0);
    q.b2 = static_cast<float>(b2 / a0);
    q.a1 = static_cast<float>(a1 / a0);
    q.a2 = static_cast<float>(a2 / a0);
    return q;
}

Biquad lowPass(const double hz, const double q, const double rate)
{
    const auto w = 2.0 * pi * std::min(hz, rate * 0.45) / rate;
    const auto c = std::cos(w);
    const auto alpha = std::sin(w) / (2.0 * q);
    return normalised((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

Biquad highPass(const double hz, const double q, const double rate)
{
    const auto w = 2.0 * pi * std::min(hz, rate * 0.45) / rate;
    const auto c = std::cos(w);
    const auto alpha = std::sin(w) / (2.0 * q);
    return normalised((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

Biquad allPass(const double hz, const double q, const double rate)
{
    const auto w = 2.0 * pi * std::min(hz, rate * 0.45) / rate;
    const auto c = std::cos(w);
    const auto alpha = std::sin(w) / (2.0 * q);
    return normalised(1.0 - alpha, -2.0 * c, 1.0 + alpha, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

constexpr double butterworthQ = 0.7071067811865476;
constexpr double lowCrossover = 250.0;
constexpr double highCrossover = 3'000.0;

} // namespace

// ---- Deck --------------------------------------------------------------------------------------

void DeckPlayer::prepare(const double outputRate, const int maximumBlock)
{
    outputRate_ = std::max(1.0, outputRate);
    maximumBlock_ = std::max(16, maximumBlock);
    // Grains of about 42 ms, half of one apart.
    window_ = std::max(256, static_cast<int>(std::lround(outputRate_ * 0.042 * 0.5)) * 2);
    hop_ = window_ / 2;
    hann_.resize(static_cast<std::size_t>(window_));
    for (int j = 0; j < window_; ++j)
        hann_[static_cast<std::size_t>(j)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * pi * j / window_));
    for (auto& side : accumulator_)
        side.assign(static_cast<std::size_t>(window_), 0.0f);
    for (auto& side : ready_)
        side.assign(static_cast<std::size_t>(hop_), 0.0f);
    for (auto& buffer : scratch_)
        buffer.assign(static_cast<std::size_t>(maximumBlock_), 0.0f);
    fadeLength_ = std::max(16, static_cast<int>(std::lround(outputRate_ * 0.003)));
    setTrack(track_);
}

void DeckPlayer::reset() noexcept
{
    for (auto& side : accumulator_)
        std::fill(side.begin(), side.end(), 0.0f);
    readyCount_ = 0;
    readyStart_ = 0;
    havePrevious_ = false;
    stretched_ = false;
    fadeLeft_ = 0;
    lastSpeed_ = 0.0;
}

void DeckPlayer::setTrack(const TrackAudio* const track) noexcept
{
    track_ = track != nullptr && track->frames() > 0 ? track : nullptr;
    step_ = track_ != nullptr ? track_->sampleRate / outputRate_ : 1.0;
    search_ = track_ != nullptr ? std::max(64, static_cast<int>(std::lround(track_->sampleRate * 0.012))) : 512;
    position_ = 0.0;
    clearLoop();
    reset();
}

void DeckPlayer::jump(const double frame) noexcept
{
    if (!stretched_ && std::abs(lastSpeed_) > 1.0e-6) {
        fadeFrom_ = position_;
        fadeLeft_ = fadeLength_;
    }
    position_ = frame;
}

void DeckPlayer::setLoop(const double start, const double end) noexcept
{
    if (end - start < 16.0) {
        clearLoop();
        return;
    }
    loopStart_ = start;
    loopEnd_ = end;
}

void DeckPlayer::clearLoop() noexcept
{
    loopStart_ = 0.0;
    loopEnd_ = 0.0;
}

double DeckPlayer::wrapped(const double frame) const noexcept
{
    if (!looping() || frame < loopEnd_)
        return frame;
    const auto length = loopEnd_ - loopStart_;
    return loopStart_ + std::fmod(frame - loopStart_, length);
}

float DeckPlayer::read(const int channel, const double frame) const noexcept
{
    const auto& data = track_->channels[static_cast<std::size_t>(std::min<int>(channel, static_cast<int>(track_->channels.size()) - 1))];
    const auto frames = static_cast<std::int64_t>(data.size());
    if (!(frame > -4.0) || frame > static_cast<double>(frames) + 4.0)
        return 0.0f;
    const auto i = static_cast<std::int64_t>(std::floor(frame));
    const auto t = static_cast<float>(frame - static_cast<double>(i));
    float xm1, x0, x1, x2;
    if (i >= 1 && i + 2 < frames) {
        const auto* p = data.data() + i;
        xm1 = p[-1];
        x0 = p[0];
        x1 = p[1];
        x2 = p[2];
    } else {
        const auto at = [&](const std::int64_t k) { return k >= 0 && k < frames ? data[static_cast<std::size_t>(k)] : 0.0f; };
        xm1 = at(i - 1);
        x0 = at(i);
        x1 = at(i + 1);
        x2 = at(i + 2);
    }
    const auto c1 = 0.5f * (x1 - xm1);
    const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

float DeckPlayer::mono(const std::int64_t frame) const noexcept
{
    const auto frames = track_->frames();
    if (frame < 0 || frame >= frames)
        return 0.0f;
    const auto at = static_cast<std::size_t>(frame);
    return track_->channels.size() > 1 ? 0.5f * (track_->channels[0][at] + track_->channels[1][at]) : track_->channels[0][at];
}

void DeckPlayer::render(float* const left, float* const right, const int count, const double speed, const bool keylock) noexcept
{
    if (count <= 0)
        return;
    if (track_ == nullptr) {
        std::fill_n(left, count, 0.0f);
        std::fill_n(right, count, 0.0f);
        lastSpeed_ = speed;
        return;
    }
    const auto from = lastSpeed_;
    int done = 0;
    while (done < count) {
        const auto n = std::min(maximumBlock_, count - done);
        const auto a = from + (speed - from) * static_cast<double>(done) / count;
        const auto b = from + (speed - from) * static_cast<double>(done + n) / count;
        renderBlock(left + done, right + done, n, a, b, keylock);
        done += n;
    }
    lastSpeed_ = speed;
}

void DeckPlayer::renderBlock(float* const left, float* const right, const int count, const double from, const double to,
                             const bool keylock) noexcept
{
    const auto stretch = keylock && std::min(from, to) > 0.3 && std::max(from, to) < 3.0;
    if (stretch == stretched_) {
        if (stretch)
            renderStretched(left, right, count, from, to);
        else
            renderDirect(left, right, count, from, to);
        return;
    }
    // From one way to the other: both, crossfaded over the block.
    const auto start = position_;
    auto* oldLeft = scratch_[0].data();
    auto* oldRight = scratch_[1].data();
    if (stretched_)
        renderStretched(oldLeft, oldRight, count, from, to);
    else
        renderDirect(oldLeft, oldRight, count, from, to);
    position_ = start;
    fadeLeft_ = 0;
    if (stretch) {
        primeStretch(from);
        renderStretched(left, right, count, from, to);
    } else {
        renderDirect(left, right, count, from, to);
    }
    stretched_ = stretch;
    for (int i = 0; i < count; ++i) {
        const auto t = static_cast<float>(i + 1) / static_cast<float>(count);
        left[i] = oldLeft[i] * (1.0f - t) + left[i] * t;
        right[i] = oldRight[i] * (1.0f - t) + right[i] * t;
    }
}

void DeckPlayer::renderDirect(float* const left, float* const right, const int count, const double from, const double to) noexcept
{
    const auto stereo = track_->channels.size() > 1;
    auto position = position_;
    for (int i = 0; i < count; ++i) {
        const auto speed = from + (to - from) * static_cast<double>(i) / count;
        auto l = read(0, position);
        auto r = stereo ? read(1, position) : l;
        if (fadeLeft_ > 0) {
            const auto g = static_cast<float>(fadeLeft_) / static_cast<float>(fadeLength_);
            const auto ol = read(0, fadeFrom_);
            const auto orr = stereo ? read(1, fadeFrom_) : ol;
            l = l * (1.0f - g) + ol * g;
            r = r * (1.0f - g) + orr * g;
            fadeFrom_ += speed * step_;
            --fadeLeft_;
        }
        left[i] = l;
        right[i] = r;
        const auto before = position;
        position += speed * step_;
        if (looping()) {
            const auto length = loopEnd_ - loopStart_;
            if (speed > 0.0 && before < loopEnd_ && position >= loopEnd_) {
                fadeFrom_ = position;
                fadeLeft_ = fadeLength_;
                position -= length;
            } else if (speed < 0.0 && before >= loopStart_ && position < loopStart_) {
                fadeFrom_ = position;
                fadeLeft_ = fadeLength_;
                position += length;
            }
        }
    }
    position_ = position;
}

void DeckPlayer::renderStretched(float* const left, float* const right, const int count, const double from,
                                 const double to) noexcept
{
    for (int i = 0; i < count; ++i) {
        const auto speed = from + (to - from) * static_cast<double>(i) / count;
        if (readyCount_ <= 0)
            produceHop(speed);
        left[i] = ready_[0][static_cast<std::size_t>(readyStart_)];
        right[i] = ready_[1][static_cast<std::size_t>(readyStart_)];
        ++readyStart_;
        --readyCount_;
        const auto before = position_;
        position_ += speed * step_;
        if (looping() && before < loopEnd_ && position_ >= loopEnd_)
            position_ -= loopEnd_ - loopStart_;
    }
}

void DeckPlayer::primeStretch(const double speed) noexcept
{
    // A grain before this place, so the first hop out is whole.
    for (auto& side : accumulator_)
        std::fill(side.begin(), side.end(), 0.0f);
    const auto stereo = track_->channels.size() > 1;
    const auto before = position_ - hop_ * step_ * std::max(0.3, speed);
    for (int j = hop_; j < window_; ++j) {
        const auto at = before + j * step_;
        const auto l = read(0, at);
        accumulator_[0][static_cast<std::size_t>(j - hop_)] = hann_[static_cast<std::size_t>(j)] * l;
        accumulator_[1][static_cast<std::size_t>(j - hop_)] = hann_[static_cast<std::size_t>(j)] * (stereo ? read(1, at) : l);
    }
    lastGrain_ = before;
    havePrevious_ = true;
    readyCount_ = 0;
    readyStart_ = 0;
}

double DeckPlayer::bestGrain(const double natural, const double target) const noexcept
{
    // Where the last grain would go on is where the next one goes, when that
    // is where it is due (normal speed, no jump).
    if (std::abs(natural - target) < 1.0)
        return natural;
    const auto length = std::max(32, static_cast<int>(hop_ * step_));
    // Spaced by what one output sample spans of the track: the search costs
    // the same whatever the track's rate (a 192 kHz file is read four times
    // as densely, its candidates four times as many, each four times longer).
    const auto spread = std::max(1.0, step_);
    const auto decimation = std::max(4, static_cast<int>(std::lround(4.0 * spread)));
    const auto stride = std::max<std::int64_t>(2, std::llround(2.0 * spread));
    const auto reference = static_cast<std::int64_t>(std::llround(natural));
    const auto centre = static_cast<std::int64_t>(std::llround(target));
    const auto score = [&](const std::int64_t candidate) {
        float dot = 0.0f;
        float energy = 1.0e-9f;
        for (int k = 0; k < length; k += decimation) {
            const auto a = mono(reference + k);
            const auto b = mono(candidate + k);
            dot += a * b;
            energy += b * b;
        }
        return dot / std::sqrt(energy);
    };
    auto best = centre;
    auto bestScore = score(centre);
    for (auto candidate = centre - search_; candidate <= centre + search_; candidate += stride) {
        const auto value = score(candidate);
        if (value > bestScore + 1.0e-6f) {
            bestScore = value;
            best = candidate;
        }
    }
    // Then sample by sample between the coarse steps around the best.
    const auto coarse = best;
    for (auto candidate = coarse - (stride - 1); candidate <= coarse + (stride - 1); ++candidate) {
        if (candidate == coarse)
            continue;
        const auto value = score(candidate);
        if (value > bestScore + 1.0e-6f) {
            bestScore = value;
            best = candidate;
        }
    }
    return static_cast<double>(best) + (target - static_cast<double>(centre));
}

void DeckPlayer::produceHop(const double speed) noexcept
{
    (void) speed;
    const auto target = wrapped(position_);
    const auto start = havePrevious_ ? bestGrain(lastGrain_ + hop_ * step_, target) : target;
    const auto stereo = track_->channels.size() > 1;
    for (int j = 0; j < window_; ++j) {
        const auto at = start + j * step_;
        const auto w = hann_[static_cast<std::size_t>(j)];
        const auto l = read(0, at);
        accumulator_[0][static_cast<std::size_t>(j)] += w * l;
        accumulator_[1][static_cast<std::size_t>(j)] += w * (stereo ? read(1, at) : l);
    }
    for (int side = 0; side < 2; ++side) {
        auto& acc = accumulator_[side];
        std::copy_n(acc.begin(), hop_, ready_[side].begin());
        std::copy(acc.begin() + hop_, acc.end(), acc.begin());
        std::fill(acc.begin() + (window_ - hop_), acc.end(), 0.0f);
    }
    readyStart_ = 0;
    readyCount_ = hop_;
    lastGrain_ = start;
    havePrevious_ = true;
}

// ---- Channel -----------------------------------------------------------------------------------

float eqKnobGain(const float knob) noexcept
{
    if (knob <= 0.015f)
        return 0.0f;
    const auto db = knob < 0.5f ? -26.0f * (0.5f - knob) / (0.5f - 0.015f) : 6.0f * (knob - 0.5f) / 0.5f;
    return std::pow(10.0f, db / 20.0f);
}

float faderGain(const float position) noexcept
{
    const auto p = std::clamp(position, 0.0f, 1.0f);
    return p * p;
}

std::pair<float, float> crossfaderGains(const float position, const float curve) noexcept
{
    const auto x = std::clamp(position, 0.0f, 1.0f);
    const auto c = std::clamp(curve, 0.0f, 1.0f);
    const auto smoothLeft = static_cast<float>(std::cos(x * pi * 0.5));
    const auto smoothRight = static_cast<float>(std::sin(x * pi * 0.5));
    const auto sharpLeft = x < 0.97f ? 1.0f : (1.0f - x) / 0.03f;
    const auto sharpRight = x > 0.03f ? 1.0f : x / 0.03f;
    return { smoothLeft * (1.0f - c) + sharpLeft * c, smoothRight * (1.0f - c) + sharpRight * c };
}

void ChannelStrip::prepare(const double rate)
{
    rate_ = std::max(1.0, rate);
    for (auto& side : sides_) {
        side.allPass = allPass(highCrossover, butterworthQ, rate_);
        side.lowA = side.lowB = lowPass(lowCrossover, butterworthQ, rate_);
        side.restA = side.restB = highPass(lowCrossover, butterworthQ, rate_);
        side.midA = side.midB = lowPass(highCrossover, butterworthQ, rate_);
        side.highA = side.highB = highPass(highCrossover, butterworthQ, rate_);
    }
    reset();
}

void ChannelStrip::reset() noexcept
{
    for (auto& side : sides_) {
        for (auto* q : { &side.allPass, &side.lowA, &side.lowB, &side.restA, &side.restB, &side.midA, &side.midB, &side.highA,
                         &side.highB })
            q->z1 = q->z2 = 0.0f;
        side.ic1 = side.ic2 = 0.0f;
    }
    wet_ = 0.0f;
}

void ChannelStrip::process(float* const left, float* const right, const int count, const StripSettings& settings) noexcept
{
    if (count <= 0)
        return;
    // The bands, their gains moved across the block.
    const auto from = last_;
    for (int s = 0; s < 2; ++s) {
        auto& side = sides_[static_cast<std::size_t>(s)];
        auto* data = s == 0 ? left : right;
        for (int i = 0; i < count; ++i) {
            const auto t = static_cast<float>(i + 1) / static_cast<float>(count);
            const auto trim = from.trim + (settings.trim - from.trim) * t;
            const auto gl = from.low + (settings.low - from.low) * t;
            const auto gm = from.mid + (settings.mid - from.mid) * t;
            const auto gh = from.high + (settings.high - from.high) * t;
            const auto x = data[i] * trim;
            const auto low = side.lowB.process(side.lowA.process(side.allPass.process(x)));
            const auto rest = side.restB.process(side.restA.process(x));
            const auto mid = side.midB.process(side.midA.process(rest));
            const auto high = side.highB.process(side.highA.process(rest));
            data[i] = gl * low + gm * mid + gh * high;
        }
    }
    last_ = settings;

    // The filter: its knob's side, how far, heard as much as it is turned.
    const auto amount = std::abs(std::clamp(settings.filter, -1.0f, 1.0f));
    const auto wantsHigh = settings.filter > 0.0f;
    auto wetTarget = std::clamp((amount - 0.02f) / 0.06f, 0.0f, 1.0f);
    // From one side to the other through nothing: the filter turns over
    // only once it is out of the sound.
    if (wantsHigh != highPass_) {
        if (wet_ < 1.0e-3f)
            highPass_ = wantsHigh;
        else
            wetTarget = 0.0f;
    }
    if (wetTarget <= 0.0f && wet_ < 1.0e-4f) {
        wet_ = 0.0f;
        return;
    }
    const auto course = std::clamp((amount - 0.02f) / 0.98f, 0.0f, 1.0f);
    const auto q = 0.707f + std::clamp(settings.resonance, 0.0f, 1.0f) * 3.0f;
    const auto k = 1.0f / q;
    const auto wetStep = 1.0f - std::exp(-1.0f / static_cast<float>(0.01 * rate_));
    const auto cutoffStep = 1.0f - std::exp(-16.0f / static_cast<float>(0.02 * rate_));
    for (int start = 0; start < count; start += 16) {
        const auto n = std::min(16, count - start);
        cutoff_ += (course - cutoff_) * cutoffStep;
        const auto hz = highPass_ ? 20.0 * std::pow(8'000.0 / 20.0, static_cast<double>(cutoff_))
                                  : 20'000.0 * std::pow(60.0 / 20'000.0, static_cast<double>(cutoff_));
        const auto g = static_cast<float>(std::tan(pi * std::min(hz, rate_ * 0.45) / rate_));
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        const auto a2 = g * a1;
        const auto a3 = g * a2;
        for (int i = start; i < start + n; ++i) {
            wet_ += (wetTarget - wet_) * wetStep;
            for (int s = 0; s < 2; ++s) {
                auto& side = sides_[static_cast<std::size_t>(s)];
                auto* data = s == 0 ? left : right;
                const auto v0 = data[i];
                const auto v3 = v0 - side.ic2;
                const auto v1 = a1 * side.ic1 + a2 * v3;
                const auto v2 = side.ic2 + a2 * side.ic1 + a3 * v3;
                side.ic1 = 2.0f * v1 - side.ic1;
                side.ic2 = 2.0f * v2 - side.ic2;
                const auto filtered = highPass_ ? v0 - k * v1 - v2 : v2;
                data[i] = v0 + (filtered - v0) * wet_;
            }
        }
    }
}

// ---- Effects -----------------------------------------------------------------------------------

const EffectInfo& effectInfo(const EffectKind kind) noexcept
{
    static const std::array<EffectInfo, effectKindCount> infos { {
        { "echo", "Echo", "Time", "Feedback" },
        { "reverb", "Reverb", "Size", "Tone" },
        { "flanger", "Flanger", "Rate", "Feedback" },
        { "phaser", "Phaser", "Rate", "Feedback" },
        { "filter-lfo", "Filter LFO", "Rate", "Resonance" },
        { "gater", "Gater", "Rate", "Shape" },
        { "stutter", "Stutter", "Length", "Gate" },
        { "crusher", "Bitcrusher", "Bits", "Downsample" },
    } };
    const auto index = std::clamp(static_cast<int>(kind), 0, effectKindCount - 1);
    return infos[static_cast<std::size_t>(index)];
}

double beatsOfKnob(const float knob, const bool slow) noexcept
{
    static constexpr std::array<double, 8> fast { 1.0 / 16.0, 1.0 / 8.0, 3.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 2.0, 3.0 / 4.0, 1.0 };
    static constexpr std::array<double, 7> slower { 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0, 8.0, 16.0 };
    const auto k = std::clamp(knob, 0.0f, 1.0f);
    if (slow)
        return slower[static_cast<std::size_t>(std::lround(k * static_cast<float>(slower.size() - 1)))];
    return fast[static_cast<std::size_t>(std::lround(k * static_cast<float>(fast.size() - 1)))];
}

void DeckEffect::prepare(const double rate)
{
    rate_ = std::max(1.0, rate);
    const auto delayLength = static_cast<std::size_t>(rate_ * 4.0) + 8;
    for (auto& side : delay_)
        side.assign(delayLength, 0.0f);
    static constexpr std::array<int, 8> combTunings { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static constexpr std::array<int, 4> allPassTunings { 556, 441, 341, 225 };
    const auto scale = rate_ / 44'100.0;
    for (std::size_t i = 0; i < combs_.size(); ++i) {
        const auto tuning = combTunings[i % 8] + (i >= 8 ? 23 : 0);
        combs_[i].buffer.assign(static_cast<std::size_t>(std::max(8.0, tuning * scale)), 0.0f);
    }
    for (std::size_t i = 0; i < allPasses_.size(); ++i) {
        const auto tuning = allPassTunings[i % 4] + (i >= 4 ? 23 : 0);
        allPasses_[i].buffer.assign(static_cast<std::size_t>(std::max(8.0, tuning * scale)), 0.0f);
    }
    for (auto& side : caught_)
        side.assign(static_cast<std::size_t>(rate_ * 2.0) + 8, 0.0f);
    reset();
}

void DeckEffect::reset() noexcept
{
    for (auto& side : delay_)
        std::fill(side.begin(), side.end(), 0.0f);
    delayAt_ = 0;
    delayFrames_ = 0.0f;
    for (auto& comb : combs_) {
        std::fill(comb.buffer.begin(), comb.buffer.end(), 0.0f);
        comb.at = 0;
        comb.store = 0.0f;
    }
    for (auto& pass : allPasses_) {
        std::fill(pass.buffer.begin(), pass.buffer.end(), 0.0f);
        pass.at = 0;
    }
    phaserState_.fill(0.0f);
    phaserFeedback_[0] = phaserFeedback_[1] = 0.0f;
    for (auto& side : svf_)
        side[0] = side[1] = 0.0f;
    tone_[0] = tone_[1] = toneHigh_[0] = toneHigh_[1] = 0.0f;
    caughtLength_ = caughtAt_ = caughtFill_ = 0;
    catching_ = false;
    held_[0] = held_[1] = 0.0f;
    holdPhase_ = 0.0f;
    phase_ = 0.0;
    wet_ = 0.0f;
    send_ = 0.0f;
    dry_ = 1.0f;
}

void DeckEffect::process(float* const left, float* const right, const int count, const EffectSettings& settings,
                         double beatSeconds, const double beat) noexcept
{
    if (count <= 0 || delay_[0].empty())
        return;
    if (settings.kind != kind_) {
        reset();
        kind_ = settings.kind;
    }
    if (!(beatSeconds > 0.05))
        beatSeconds = 0.5;
    const auto mix = std::clamp(settings.mix, 0.0f, 1.0f);
    // Echoes and reverb ring on once off; the others stop.
    const auto tails = kind_ == EffectKind::echo || kind_ == EffectKind::reverb;
    const auto dryTarget = settings.on ? std::min(1.0f, 2.0f * (1.0f - mix)) : 1.0f;
    const auto wetTarget = settings.on || tails ? std::min(1.0f, 2.0f * mix) : 0.0f;
    const auto sendTarget = settings.on ? 1.0f : 0.0f;
    if (!settings.on && !tails && wet_ < 1.0e-4f && dry_ > 0.9999f) {
        wet_ = 0.0f;
        dry_ = 1.0f;
        catching_ = false;
        return;
    }
    const auto smooth = 1.0f - std::exp(-1.0f / static_cast<float>(0.01 * rate_));
    const auto beatStep = 1.0 / (beatSeconds * rate_);
    const auto first = std::clamp(settings.first, 0.0f, 1.0f);
    const auto second = std::clamp(settings.second, 0.0f, 1.0f);
    const auto delaySize = delay_[0].size();
    const auto readDelay = [&](const int side, const float frames) {
        auto position = static_cast<double>(delayAt_) - static_cast<double>(frames);
        while (position < 0.0)
            position += static_cast<double>(delaySize);
        const auto i = static_cast<std::size_t>(position);
        const auto t = static_cast<float>(position - static_cast<double>(i));
        const auto& line = delay_[side];
        return line[i % delaySize] * (1.0f - t) + line[(i + 1) % delaySize] * t;
    };
    const auto lfoAt = [](const double beats, const double period) {
        return static_cast<float>(0.5 - 0.5 * std::cos(2.0 * pi * std::fmod(beats, period) / period));
    };
    for (int i = 0; i < count; ++i) {
        dry_ += (dryTarget - dry_) * smooth;
        wet_ += (wetTarget - wet_) * smooth;
        send_ += (sendTarget - send_) * smooth;
        const auto beatNow = beat + i * beatStep;
        float x[2] = { left[i], right[i] };
        float w[2] = { 0.0f, 0.0f };
        switch (kind_) {
        case EffectKind::echo: {
            const auto target = static_cast<float>(std::clamp(beatsOfKnob(first, false) * beatSeconds * rate_, 1.0,
                                                              static_cast<double>(delaySize) - 4.0));
            if (delayFrames_ <= 0.0f)
                delayFrames_ = target;
            delayFrames_ += (target - delayFrames_) * 0.0004f;
            const auto feedback = second * 0.92f;
            const auto lowCoef = 1.0f - std::exp(-2.0f * static_cast<float>(pi) * 6'000.0f / static_cast<float>(rate_));
            const auto highCoef = 1.0f - std::exp(-2.0f * static_cast<float>(pi) * 150.0f / static_cast<float>(rate_));
            for (int s = 0; s < 2; ++s) {
                const auto d = readDelay(s, delayFrames_);
                tone_[s] += (d - tone_[s]) * lowCoef;
                toneHigh_[s] += (tone_[s] - toneHigh_[s]) * highCoef;
                const auto fed = tone_[s] - toneHigh_[s];
                delay_[s][delayAt_] = x[s] * send_ + fed * feedback;
                w[s] = d;
            }
            delayAt_ = (delayAt_ + 1) % delaySize;
            break;
        }
        case EffectKind::reverb: {
            const auto room = 0.70f + first * 0.28f;
            const auto damp = 0.6f - second * 0.5f;
            const auto input = (x[0] + x[1]) * 0.5f * send_ * 0.015f;
            for (int s = 0; s < 2; ++s) {
                float sum = 0.0f;
                for (std::size_t c = 0; c < 8; ++c) {
                    auto& comb = combs_[static_cast<std::size_t>(s) * 8 + c];
                    const auto y = comb.buffer[comb.at];
                    comb.store = y * (1.0f - damp) + comb.store * damp;
                    comb.buffer[comb.at] = input + comb.store * room;
                    comb.at = (comb.at + 1) % comb.buffer.size();
                    sum += y;
                }
                for (std::size_t a = 0; a < 4; ++a) {
                    auto& pass = allPasses_[static_cast<std::size_t>(s) * 4 + a];
                    const auto buffered = pass.buffer[pass.at];
                    const auto out = -sum + buffered;
                    pass.buffer[pass.at] = sum + buffered * 0.5f;
                    pass.at = (pass.at + 1) % pass.buffer.size();
                    sum = out;
                }
                w[s] = sum * 3.0f;
            }
            break;
        }
        case EffectKind::flanger: {
            const auto lfo = lfoAt(beatNow, beatsOfKnob(first, true));
            const auto frames = static_cast<float>((0.0005 + lfo * 0.006) * rate_);
            const auto feedback = second * 0.85f;
            for (int s = 0; s < 2; ++s) {
                const auto d = readDelay(s, s == 0 ? frames : frames * 0.93f + 2.0f);
                delay_[s][delayAt_] = x[s] * send_ + d * feedback;
                w[s] = d;
            }
            delayAt_ = (delayAt_ + 1) % delaySize;
            break;
        }
        case EffectKind::phaser: {
            const auto lfo = lfoAt(beatNow, beatsOfKnob(first, true));
            const auto hz = 200.0 * std::pow(20.0, static_cast<double>(lfo));
            const auto t = std::tan(pi * hz / rate_);
            const auto a = static_cast<float>((t - 1.0) / (t + 1.0));
            const auto feedback = second * 0.7f;
            for (int s = 0; s < 2; ++s) {
                auto y = x[s] * send_ + phaserFeedback_[s] * feedback;
                for (int stage = 0; stage < 6; ++stage) {
                    auto& z = phaserState_[static_cast<std::size_t>(s * 6 + stage)];
                    const auto out = a * y + z;
                    z = y - a * out;
                    y = out;
                }
                phaserFeedback_[s] = y;
                w[s] = y;
            }
            break;
        }
        case EffectKind::filterLfo: {
            const auto lfo = lfoAt(beatNow, beatsOfKnob(first, true));
            const auto hz = 150.0 * std::pow(60.0, static_cast<double>(lfo));
            const auto g = static_cast<float>(std::tan(pi * std::min(hz, rate_ * 0.45) / rate_));
            const auto k = 1.0f / (0.8f + second * 6.0f);
            const auto a1 = 1.0f / (1.0f + g * (g + k));
            const auto a2 = g * a1;
            const auto a3 = g * a2;
            for (int s = 0; s < 2; ++s) {
                auto& ic = svf_[s];
                const auto v0 = x[s] * send_;
                const auto v3 = v0 - ic[1];
                const auto v1 = a1 * ic[0] + a2 * v3;
                const auto v2 = ic[1] + a2 * ic[0] + a3 * v3;
                ic[0] = 2.0f * v1 - ic[0];
                ic[1] = 2.0f * v2 - ic[1];
                w[s] = v2;
            }
            break;
        }
        case EffectKind::gater: {
            const auto period = beatsOfKnob(first, false);
            const auto duty = 0.15 + second * 0.75;
            const auto at = std::fmod(std::max(0.0, beatNow), period) / period;
            const auto gate = at < duty ? 1.0f : 0.0f;
            holdPhase_ += (gate - holdPhase_) * (1.0f - std::exp(-1.0f / static_cast<float>(0.002 * rate_)));
            for (int s = 0; s < 2; ++s)
                w[s] = x[s] * holdPhase_ * send_;
            break;
        }
        case EffectKind::beatMasher: {
            if (settings.on && !catching_) {
                catching_ = true;
                caughtFill_ = 0;
                caughtAt_ = 0;
                caughtLength_ = static_cast<int>(std::clamp(beatsOfKnob(first, false) * beatSeconds * rate_, 64.0,
                                                            static_cast<double>(caught_[0].size()) - 1.0));
            } else if (!settings.on) {
                catching_ = false;
            }
            if (catching_) {
                const auto length = caughtLength_;
                const auto heard = static_cast<int>((0.25f + 0.75f * second) * static_cast<float>(length));
                if (caughtFill_ < length) {
                    for (int s = 0; s < 2; ++s) {
                        caught_[s][static_cast<std::size_t>(caughtFill_)] = x[s];
                        w[s] = x[s];
                    }
                    ++caughtFill_;
                } else {
                    const auto edge = std::min({ 1.0f, static_cast<float>(caughtAt_) / 48.0f,
                                                 static_cast<float>(heard - caughtAt_) / 48.0f });
                    const auto gain = caughtAt_ < heard ? std::max(0.0f, edge) : 0.0f;
                    for (int s = 0; s < 2; ++s)
                        w[s] = caught_[s][static_cast<std::size_t>(caughtAt_)] * gain;
                    caughtAt_ = (caughtAt_ + 1) % length;
                }
            } else {
                w[0] = x[0];
                w[1] = x[1];
            }
            break;
        }
        case EffectKind::crusher: {
            const auto bits = 16.0f - first * 13.0f;
            const auto levels = std::pow(2.0f, bits - 1.0f);
            const auto factor = 1.0f + second * 39.0f;
            holdPhase_ += 1.0f;
            if (holdPhase_ >= factor) {
                holdPhase_ -= factor;
                for (int s = 0; s < 2; ++s)
                    held_[s] = std::round(x[s] * levels) / levels;
            }
            w[0] = held_[0];
            w[1] = held_[1];
            break;
        }
        }
        left[i] = x[0] * dry_ + w[0] * wet_;
        right[i] = x[1] * dry_ + w[1] * wet_;
    }
}

// ---- Analysis ----------------------------------------------------------------------------------

namespace {

// The highest value's index of a circular histogram, between bins.
double circularPeak(const std::vector<double>& bins, double& value)
{
    const auto size = bins.size();
    std::size_t best = 0;
    for (std::size_t i = 1; i < size; ++i)
        if (bins[i] > bins[best])
            best = i;
    value = bins[best];
    const auto before = bins[(best + size - 1) % size];
    const auto after = bins[(best + 1) % size];
    const auto curve = before - 2.0 * bins[best] + after;
    const auto shift = std::abs(curve) > 1.0e-12 ? std::clamp(0.5 * (before - after) / curve, -0.5, 0.5) : 0.0;
    return static_cast<double>(best) + shift;
}

// How suddenly the sound gets louder, frame after frame: the spectrum's
// rise (its log magnitudes, each bin against the loudest of its neighbours
// a frame before, so that held notes and vibrato rise by nothing), at about
// 170 frames a second. `delay`: where a frame's onset is, in seconds after
// the frame's start.
std::vector<float> onsetEnvelope(const TrackAudio& audio, double& frameRate, double& delay, std::vector<float>& bass)
{
    const auto frames = audio.frames();
    const auto factor = audio.sampleRate > 30'000.0 ? 2 : 1;
    const auto rate = audio.sampleRate / factor;
    const auto size = static_cast<std::size_t>(rate > 16'000.0 ? 1'024 : 512);
    const auto hop = size / 8;
    frameRate = rate / static_cast<double>(hop);
    delay = static_cast<double>(size) * 0.5 / rate;
    // Mono at the lower rate.
    auto filterA = lowPass(rate * 0.45, butterworthQ, audio.sampleRate);
    auto filterB = filterA;
    std::vector<float> mono;
    mono.reserve(static_cast<std::size_t>(frames / factor + 1));
    const auto stereo = audio.channels.size() > 1;
    for (std::int64_t i = 0; i < frames; ++i) {
        const auto at = static_cast<std::size_t>(i);
        auto x = stereo ? 0.5f * (audio.channels[0][at] + audio.channels[1][at]) : audio.channels[0][at];
        if (factor > 1)
            x = filterB.process(filterA.process(x));
        if (i % factor == 0)
            mono.push_back(x);
    }
    std::vector<float> onsets;
    if (mono.size() < size)
        return onsets;
    Fft fft;
    fft.prepare(size);
    std::vector<float> window(size);
    float windowSum = 0.0f;
    for (std::size_t i = 0; i < size; ++i) {
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(size)));
        windowSum += window[i];
    }
    const auto bins = size / 2;
    // Up to 200 Hz: the kick and the bass.
    const auto bassBins = static_cast<std::size_t>(200.0 / (rate / static_cast<double>(size)));
    bass.clear();
    std::vector<std::complex<float>> data(size);
    std::vector<float> now(bins, 0.0f);
    std::vector<float> before(bins, 0.0f);
    onsets.reserve((mono.size() - size) / hop + 1);
    for (std::size_t start = 0; start + size <= mono.size(); start += hop) {
        for (std::size_t i = 0; i < size; ++i)
            data[i] = { mono[start + i] * window[i], 0.0f };
        fft.forward(data.data());
        for (std::size_t bin = 0; bin < bins; ++bin)
            now[bin] = std::log1p(1'000.0f * 2.0f * std::abs(data[bin]) / windowSum);
        float flux = 0.0f;
        float low = 0.0f;
        for (std::size_t bin = 1; bin + 1 < bins; ++bin) {
            const auto previous = std::max({ before[bin - 1], before[bin], before[bin + 1] });
            const auto rise = std::max(0.0f, now[bin] - previous);
            flux += rise;
            if (bin <= bassBins)
                low += rise;
        }
        onsets.push_back(start == 0 ? 0.0f : flux);
        bass.push_back(start == 0 ? 0.0f : low);
        before.swap(now);
    }
    return onsets;
}

} // namespace

double detectTempo(const std::vector<float>& onsets, const double rate, const double minimumBpm, const double maximumBpm,
                   double& firstBeat, const std::vector<float>& beatOnsets)
{
    firstBeat = 0.0;
    const auto n = onsets.size();
    if (rate <= 0.0 || n < static_cast<std::size_t>(rate * 8.0) || minimumBpm <= 0.0 || maximumBpm <= minimumBpm)
        return 0.0;
    // How alike the envelope is with itself a lag later, up to four beats at
    // the slowest tempo.
    const auto maximumLag = static_cast<std::size_t>(std::ceil(4.0 * 60.0 * rate / minimumBpm)) + 2;
    if (maximumLag + 8 >= n)
        return 0.0;
    // The onsets blurred a little: a beat falls between two frames as
    // often as on one.
    std::vector<float> blurred(n, 0.0f);
    static constexpr float kernel[5] { 1.0f / 9.0f, 2.0f / 9.0f, 3.0f / 9.0f, 2.0f / 9.0f, 1.0f / 9.0f };
    for (std::size_t i = 0; i < n; ++i)
        for (int k = -2; k <= 2; ++k) {
            const auto at = static_cast<std::ptrdiff_t>(i) + k;
            if (at >= 0 && static_cast<std::size_t>(at) < n)
                blurred[i] += kernel[k + 2] * onsets[static_cast<std::size_t>(at)];
        }
    std::vector<double> alike(maximumLag + 2, 0.0);
    for (std::size_t lag = 1; lag <= maximumLag + 1; ++lag) {
        double sum = 0.0;
        for (std::size_t i = 0; i + lag < n; ++i)
            sum += static_cast<double>(blurred[i]) * static_cast<double>(blurred[i + lag]);
        alike[lag] = sum / static_cast<double>(n - lag);
    }
    const auto alikeAt = [&](const double lag) {
        const auto i = static_cast<std::size_t>(lag);
        if (i + 1 >= alike.size())
            return 0.0;
        const auto t = lag - static_cast<double>(i);
        return alike[i] * (1.0 - t) + alike[i + 1] * t;
    };
    // Each tempo scored by its beat and its multiples.
    auto coarse = 0.0;
    auto bestScore = -1.0;
    for (auto bpm = minimumBpm; bpm < maximumBpm; bpm += 0.05) {
        const auto beat = 60.0 * rate / bpm;
        const auto score = alikeAt(beat) + 0.5 * alikeAt(2.0 * beat) + 0.33 * alikeAt(3.0 * beat) + 0.25 * alikeAt(4.0 * beat);
        if (score > bestScore) {
            bestScore = score;
            coarse = bpm;
        }
    }
    if (bestScore <= 0.0)
        return 0.0;
    // Finer: where in the beat the onsets gather, for tempos close by; the
    // sharpest gathering wins.
    constexpr std::size_t binCount = 96;
    std::vector<double> bins(binCount);
    const auto gathering = [&](const std::vector<float>& values, const double bpm, double& phase, double& contrast) {
        const auto period = 60.0 * rate / bpm;
        std::fill(bins.begin(), bins.end(), 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const auto at = std::fmod(static_cast<double>(i), period) / period;
            bins[std::min(binCount - 1, static_cast<std::size_t>(at * binCount))] += values[i];
        }
        std::vector<double> smoothed(binCount);
        for (std::size_t i = 0; i < binCount; ++i)
            smoothed[i] = 0.25 * bins[(i + binCount - 1) % binCount] + 0.5 * bins[i] + 0.25 * bins[(i + 1) % binCount];
        double peak = 0.0;
        const auto where = circularPeak(smoothed, peak);
        double mean = 0.0;
        for (const auto value : smoothed)
            mean += value;
        mean /= static_cast<double>(binCount);
        contrast = mean > 0.0 ? peak / mean : 0.0;
        phase = (where + 0.5) / static_cast<double>(binCount) * period;
        return peak - mean;
    };
    auto bpm = coarse;
    auto phase = 0.0;
    auto contrast = 0.0;
    auto best = -1.0;
    for (auto candidate = coarse - 0.6; candidate <= coarse + 0.6; candidate += 0.01) {
        double candidatePhase = 0.0;
        double candidateContrast = 0.0;
        const auto score = gathering(onsets, candidate, candidatePhase, candidateContrast);
        if (score > best) {
            best = score;
            bpm = candidate;
            phase = candidatePhase;
            contrast = candidateContrast;
        }
    }
    // Most music is made at a whole tempo.
    if (std::abs(bpm - std::round(bpm)) < 0.035) {
        bpm = std::round(bpm);
        (void) gathering(onsets, bpm, phase, contrast);
    } else {
        bpm = std::round(bpm * 100.0) / 100.0;
    }
    if (contrast < 1.3)
        return 0.0;
    // The beats are where the bass hits, when it does (not the hats between).
    if (beatOnsets.size() == n) {
        double bassPhase = 0.0;
        double bassContrast = 0.0;
        (void) gathering(beatOnsets, bpm, bassPhase, bassContrast);
        if (bassContrast > 1.5)
            phase = bassPhase;
    }
    const auto beatSeconds = 60.0 / bpm;
    firstBeat = std::fmod(phase / rate, beatSeconds);
    if (firstBeat < 0.0)
        firstBeat += beatSeconds;
    return bpm;
}

int detectKey(const TrackAudio& audio)
{
    const auto frames = audio.frames();
    if (frames <= 0 || audio.sampleRate <= 0.0)
        return -1;
    // Mono at about 11 kHz.
    const auto factor = std::max(1, static_cast<int>(audio.sampleRate / 11'025.0));
    const auto rate = audio.sampleRate / factor;
    auto filterA = lowPass(rate * 0.42, butterworthQ, audio.sampleRate);
    auto filterB = filterA;
    std::vector<float> mono;
    mono.reserve(static_cast<std::size_t>(frames / factor + 1));
    const auto stereo = audio.channels.size() > 1;
    for (std::int64_t i = 0; i < frames; ++i) {
        const auto at = static_cast<std::size_t>(i);
        const auto x = stereo ? 0.5f * (audio.channels[0][at] + audio.channels[1][at]) : audio.channels[0][at];
        const auto y = filterB.process(filterA.process(x));
        if (i % factor == 0)
            mono.push_back(y);
    }
    constexpr std::size_t size = 8'192;
    if (mono.size() < size)
        return -1;
    Fft fft;
    fft.prepare(size);
    std::vector<std::complex<float>> data(size);
    std::vector<float> window(size);
    for (std::size_t i = 0; i < size; ++i)
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / size));
    // Each bin's pitch class (C = 0), from 55 Hz to 2 kHz.
    std::vector<int> pitchClass(size / 2, -1);
    for (std::size_t bin = 1; bin < size / 2; ++bin) {
        const auto hz = static_cast<double>(bin) * rate / static_cast<double>(size);
        if (hz < 55.0 || hz > 2'000.0)
            continue;
        const auto semitones = 12.0 * std::log2(hz / 261.6255653);
        pitchClass[bin] = static_cast<int>(((std::lround(semitones) % 12) + 12) % 12);
    }
    std::array<double, 12> chroma {};
    for (std::size_t start = 0; start + size <= mono.size(); start += size / 2) {
        double energy = 0.0;
        for (std::size_t i = 0; i < size; ++i) {
            const auto x = mono[start + i];
            energy += static_cast<double>(x) * x;
            data[i] = { x * window[i], 0.0f };
        }
        if (energy / static_cast<double>(size) < 1.0e-7)
            continue;
        fft.forward(data.data());
        // Each pitch class's energy (the peaks count, not the floor), its
        // root heard, the frame's share of it kept.
        std::array<double, 12> frame {};
        for (std::size_t bin = 1; bin < size / 2; ++bin)
            if (const auto pc = pitchClass[bin]; pc >= 0)
                frame[static_cast<std::size_t>(pc)] += std::norm(data[bin]);
        double total = 0.0;
        for (auto& value : frame) {
            value = std::sqrt(value);
            total += value;
        }
        if (total <= 0.0)
            continue;
        for (std::size_t pc = 0; pc < 12; ++pc)
            chroma[pc] += frame[pc] / total;
    }
    static constexpr std::array<double, 12> major { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr std::array<double, 12> minor { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    const auto correlation = [&](const std::array<double, 12>& profile, const int tonic) {
        double meanA = 0.0;
        double meanB = 0.0;
        for (int i = 0; i < 12; ++i) {
            meanA += chroma[static_cast<std::size_t>(i)];
            meanB += profile[static_cast<std::size_t>(i)];
        }
        meanA /= 12.0;
        meanB /= 12.0;
        double cross = 0.0;
        double varA = 0.0;
        double varB = 0.0;
        for (int i = 0; i < 12; ++i) {
            const auto a = chroma[static_cast<std::size_t>((i + tonic) % 12)] - meanA;
            const auto b = profile[static_cast<std::size_t>(i)] - meanB;
            cross += a * b;
            varA += a * a;
            varB += b * b;
        }
        return varA > 0.0 && varB > 0.0 ? cross / std::sqrt(varA * varB) : -1.0;
    };
    auto key = -1;
    auto best = 0.0;
    for (int tonic = 0; tonic < 12; ++tonic) {
        if (const auto value = correlation(major, tonic); value > best) {
            best = value;
            key = tonic;
        }
        if (const auto value = correlation(minor, tonic); value > best) {
            best = value;
            key = 12 + tonic;
        }
    }
    return key;
}

const char* keyName(const int key) noexcept
{
    static constexpr std::array<const char*, 24> names { "C",  "Db",  "D",  "Eb",  "E",  "F",  "F#",  "G",  "Ab",  "A",  "Bb",  "B",
                                                         "Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm" };
    return key >= 0 && key < 24 ? names[static_cast<std::size_t>(key)] : "";
}

const char* camelotName(const int key) noexcept
{
    static constexpr std::array<const char*, 24> names { "1A",  "2A",  "3A",  "4A",  "5A",  "6A",  "7A",  "8A",
                                                         "9A",  "10A", "11A", "12A", "1B",  "2B",  "3B",  "4B",
                                                         "5B",  "6B",  "7B",  "8B",  "9B",  "10B", "11B", "12B" };
    if (key < 0 || key >= 24)
        return "";
    // A major key's number on the wheel; a minor key has its relative
    // major's.
    const auto majorOf = key < 12 ? key : (key - 12 + 3) % 12;
    const auto number = (majorOf * 7 % 12 + 7) % 12 + 1;
    return names[static_cast<std::size_t>((key < 12 ? 12 : 0) + number - 1)];
}

TrackAnalysis analyse(const TrackAudio& audio, const double minimumBpm, const double maximumBpm)
{
    TrackAnalysis result;
    const auto frames = audio.frames();
    if (frames <= 0 || audio.sampleRate <= 0.0)
        return result;
    const auto rate = audio.sampleRate;
    const auto stereo = audio.channels.size() > 1;
    result.columnFrames = rate > 64'000.0 ? 256 : 128;
    result.columnSeconds = result.columnFrames / rate;
    const auto columnFrames = result.columnFrames;
    const auto columns = static_cast<std::size_t>((frames + columnFrames - 1) / columnFrames);
    result.wave.resize(columns);
    auto bass = lowPass(200.0, butterworthQ, rate);
    auto midLow = highPass(200.0, butterworthQ, rate);
    auto midHigh = lowPass(2'500.0, butterworthQ, rate);
    auto treble = highPass(2'500.0, butterworthQ, rate);
    std::vector<std::array<float, 3>> levels(columns);
    float peak = 0.0f;
    for (std::size_t column = 0; column < columns; ++column) {
        const auto from = static_cast<std::int64_t>(column) * columnFrames;
        const auto to = std::min<std::int64_t>(frames, from + columnFrames);
        float columnPeak = 0.0f;
        double sums[3] {};
        for (auto i = from; i < to; ++i) {
            const auto at = static_cast<std::size_t>(i);
            const auto l = audio.channels[0][at];
            const auto r = stereo ? audio.channels[1][at] : l;
            columnPeak = std::max({ columnPeak, std::abs(l), std::abs(r) });
            const auto m = 0.5f * (l + r);
            const auto lo = bass.process(m);
            const auto mi = midHigh.process(midLow.process(m));
            const auto hi = treble.process(m);
            sums[0] += static_cast<double>(lo) * lo;
            sums[1] += static_cast<double>(mi) * mi;
            sums[2] += static_cast<double>(hi) * hi;
        }
        const auto length = static_cast<double>(std::max<std::int64_t>(1, to - from));
        for (int b = 0; b < 3; ++b)
            levels[column][static_cast<std::size_t>(b)] = static_cast<float>(std::sqrt(sums[b] / length));
        result.wave[column].peak = columnPeak;
        peak = std::max(peak, columnPeak);
    }
    result.peak = peak;
    // The waveform's values over the track's own range.
    float bandTop = 1.0e-9f;
    for (const auto& level : levels)
        bandTop = std::max({ bandTop, level[0], level[1], level[2] });
    for (std::size_t column = 0; column < columns; ++column) {
        auto& wave = result.wave[column];
        wave.peak = peak > 0.0f ? wave.peak / peak : 0.0f;
        wave.low = levels[column][0] / bandTop;
        wave.mid = levels[column][1] / bandTop;
        wave.high = levels[column][2] / bandTop;
    }
    double onsetRate = 0.0;
    double onsetDelay = 0.0;
    std::vector<float> bassOnsets;
    auto onsets = onsetEnvelope(audio, onsetRate, onsetDelay, bassOnsets);
    // Their slow drift taken off.
    const auto lift = [&](std::vector<float>& values) {
        const auto half = static_cast<std::size_t>(onsetRate * 0.25);
        std::vector<double> sums(values.size() + 1, 0.0);
        for (std::size_t i = 0; i < values.size(); ++i)
            sums[i + 1] = sums[i] + values[i];
        std::vector<float> lifted(values.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto a = i > half ? i - half : 0;
            const auto b = std::min(values.size(), i + half + 1);
            const auto mean = (sums[b] - sums[a]) / static_cast<double>(b - a);
            lifted[i] = std::max(0.0f, values[i] - static_cast<float>(mean));
        }
        values.swap(lifted);
    };
    lift(onsets);
    lift(bassOnsets);
    result.bpm = detectTempo(onsets, onsetRate, minimumBpm, maximumBpm, result.firstBeat, bassOnsets);
    if (result.bpm > 0.0) {
        const auto beat = 60.0 / result.bpm;
        result.firstBeat = std::fmod(result.firstBeat + onsetDelay + beat, beat);
    }
    result.key = detectKey(audio);
    std::vector<const float*> channels;
    for (const auto& channel : audio.channels)
        channels.push_back(channel.data());
    result.loudness = integratedLoudness(channels.data(), static_cast<int>(channels.size()), frames, rate);
    return result;
}

} // namespace cydaw::engine::dj
