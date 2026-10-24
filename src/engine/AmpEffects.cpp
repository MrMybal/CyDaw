#include <cydaw/engine/AmpEffects.h>

#include <cydaw/engine/AmpDesign.h>
#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/Convolver.h>
#include <cydaw/engine/MixEffects.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

namespace cydaw::engine {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr std::size_t kChannels = 8;

double dbToGain(const double db) noexcept
{
    return std::pow(10.0, db / 20.0);
}

// The coefficient of a one-pole smoother of that many milliseconds.
double smoothing(const double milliseconds, const double rate) noexcept
{
    return std::exp(-1.0 / (std::max(0.001, milliseconds) * 0.001 * rate));
}

int choice(const float value, const int count) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

struct BiquadState {
    double z1 {};
    double z2 {};
};

double run(const BiquadCoefficients& c, BiquadState& s, const double x) noexcept
{
    const auto y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

// Sections and their states; new coefficients keep the states, so a knob
// turned does not click.
struct Chain {
    amp::Sections sections;
    std::array<BiquadState, 12> states {};

    void set(const amp::Sections& designed) noexcept
    {
        if (designed.count != sections.count)
            states = {};
        sections = designed;
    }
    void reset() noexcept { states = {}; }
    double process(double x) noexcept
    {
        for (int i = 0; i < sections.count; ++i)
            x = run(sections.items[static_cast<std::size_t>(i)], states[static_cast<std::size_t>(i)], x);
        return x;
    }
};

struct ThirdOrderState {
    double s1 {};
    double s2 {};
    double s3 {};
};

double run(const amp::ThirdOrder& f, ThirdOrderState& s, const double x) noexcept
{
    const auto y = f.b[0] * x + s.s1;
    s.s1 = f.b[1] * x - f.a[1] * y + s.s2;
    s.s2 = f.b[2] * x - f.a[2] * y + s.s3;
    s.s3 = f.b[3] * x - f.a[3] * y;
    return y;
}

// One pole: its low-pass, and the high-pass that is what the low-pass leaves.
struct OnePole {
    double coefficient {};
    double state {};
    void design(const double hz, const double rate) noexcept
    {
        coefficient = std::exp(-2.0 * kPi * std::clamp(hz, 1.0, rate * 0.45) / rate);
    }
    double lowPass(const double x) noexcept
    {
        state = x + coefficient * (state - x);
        return state;
    }
    double highPass(const double x) noexcept { return x - lowPass(x); }
};

double besselI0(const double x) noexcept
{
    auto sum = 1.0;
    auto term = 1.0;
    for (int k = 1; k < 32; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

// Up and down by 2 or 4: a linear-phase windowed-sinc filter of 16 taps a
// factor and one, so the way there and back delays the sound 16 samples.
class Oversampler {
public:
    void prepare(const int factor)
    {
        factor_ = std::max(1, factor);
        taps_ = 16 * factor_ + 1;
        coefficients_.assign(static_cast<std::size_t>(taps_), 0.0);
        const auto cutoff = 0.45 / static_cast<double>(factor_);
        const auto middle = (taps_ - 1) * 0.5;
        constexpr auto beta = 7.0;
        auto sum = 0.0;
        for (int n = 0; n < taps_; ++n) {
            const auto t = n - middle;
            const auto sinc = t == 0.0 ? 2.0 * cutoff : std::sin(2.0 * kPi * cutoff * t) / (kPi * t);
            const auto ratio = 2.0 * n / (taps_ - 1) - 1.0;
            const auto window = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - ratio * ratio))) / besselI0(beta);
            coefficients_[static_cast<std::size_t>(n)] = sinc * window;
            sum += sinc * window;
        }
        for (auto& c : coefficients_)
            c /= sum;
        upHistory_.assign(static_cast<std::size_t>((taps_ + factor_ - 1) / factor_ + 1), 0.0);
        downHistory_.assign(static_cast<std::size_t>(taps_), 0.0);
    }
    void reset() noexcept
    {
        std::fill(upHistory_.begin(), upHistory_.end(), 0.0);
        std::fill(downHistory_.begin(), downHistory_.end(), 0.0);
    }
    [[nodiscard]] int factor() const noexcept { return factor_; }
    [[nodiscard]] int latency() const noexcept { return factor_ > 1 ? 16 : 0; }
    // One sample in, `factor` out.
    void up(const double x, double* out) noexcept
    {
        if (factor_ == 1) {
            out[0] = x;
            return;
        }
        std::copy_backward(upHistory_.begin(), upHistory_.end() - 1, upHistory_.end());
        upHistory_[0] = x;
        for (int phase = 0; phase < factor_; ++phase) {
            auto sum = 0.0;
            for (int k = 0, j = phase; j < taps_; ++k, j += factor_)
                sum += coefficients_[static_cast<std::size_t>(j)] * upHistory_[static_cast<std::size_t>(k)];
            out[phase] = sum * factor_;
        }
    }
    // `factor` samples in, one out.
    double down(const double* in) noexcept
    {
        if (factor_ == 1)
            return in[0];
        for (int phase = 0; phase < factor_; ++phase) {
            std::copy_backward(downHistory_.begin(), downHistory_.end() - 1, downHistory_.end());
            downHistory_[0] = in[phase];
        }
        auto sum = 0.0;
        for (int n = 0; n < taps_; ++n)
            sum += coefficients_[static_cast<std::size_t>(n)] * downHistory_[static_cast<std::size_t>(n)];
        return sum;
    }

private:
    int factor_ { 1 };
    int taps_ { 1 };
    std::vector<double> coefficients_;
    std::vector<double> upHistory_;
    std::vector<double> downHistory_;
};

// A delay line read between samples (linear).
class Delay {
public:
    void prepare(const std::size_t samples)
    {
        std::size_t size = 1;
        while (size < samples + 4)
            size <<= 1;
        buffer_.assign(size, 0.0);
        mask_ = size - 1;
        write_ = 0;
    }
    void reset() noexcept { std::fill(buffer_.begin(), buffer_.end(), 0.0); }
    void push(const double x) noexcept
    {
        buffer_[write_] = x;
        write_ = (write_ + 1) & mask_;
    }
    // The sample pushed `delay` samples before the last one.
    [[nodiscard]] double read(const double delay) const noexcept
    {
        const auto position = static_cast<double>(write_) - 1.0 - std::clamp(delay, 0.0, static_cast<double>(mask_ - 2));
        const auto whole = std::floor(position);
        const auto t = position - whole;
        const auto index = static_cast<std::int64_t>(whole);
        const auto a = buffer_[static_cast<std::size_t>(index) & mask_];
        const auto b = buffer_[static_cast<std::size_t>(index + 1) & mask_];
        return a + (b - a) * t;
    }

private:
    std::vector<double> buffer_;
    std::size_t mask_ {};
    std::size_t write_ {};
};

// The early reflections of a small live room: taps of a line, alternate
// signs, the right side's a little later.
constexpr std::array<std::pair<double, double>, 8> kRoomTaps { { { 3.1, 0.8 },
                                                                 { 5.7, -0.6 },
                                                                 { 8.9, 0.55 },
                                                                 { 11.3, -0.45 },
                                                                 { 14.8, 0.4 },
                                                                 { 19.5, -0.32 },
                                                                 { 24.1, 0.27 },
                                                                 { 31.7, -0.2 } } };

struct Room {
    Delay line;
    OnePole tone;
    double toneHz {};
    // How far its taps are spread (1: the small live room), and where they
    // are now, gliding there: a size turned does not click.
    double scale { 1.0 };
    double spread { 1.0 };
    // Just reset: the first size set is where it starts, without a glide.
    bool fresh { true };
    void prepare(const double rate)
    {
        // Up to the largest size, the right side's later taps.
        line.prepare(static_cast<std::size_t>(0.12 * rate));
        tone.design(5'000.0, rate);
        toneHz = 5'000.0;
    }
    void reset() noexcept
    {
        line.reset();
        tone.state = 0.0;
        fresh = true;
    }
    // Its size (0..1: 0.25 the small live room) and its walls' tone (Hz).
    void set(const double size, const double hz, const double rate) noexcept
    {
        scale = 0.4 + 2.4 * std::clamp(size, 0.0, 1.0);
        if (std::exchange(fresh, false))
            spread = scale;
        if (hz != toneHz) {
            tone.design(hz, rate);
            toneHz = hz;
        }
    }
    double process(const double x, const double rate, const bool right) noexcept
    {
        spread += (scale - spread) * 0.0005;
        line.push(tone.lowPass(x));
        auto sum = 0.0;
        for (const auto& [ms, gain] : kRoomTaps)
            sum += gain * line.read(ms * spread * (right ? 1.07 : 1.0) * 0.001 * rate);
        return sum * 0.5;
    }
};

// What a fraction of the way to a metre a distance is (cm), for the room.
double farness(const double centimetres) noexcept
{
    return std::clamp(std::log10(std::max(1.0, centimetres)) / 2.0, 0.0, 1.0);
}

// ---- Ampli --------------------------------------------------------------------------------------

class AmpSim final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        for (auto& channel : state_) {
            for (std::size_t i = 0; i < 3; ++i) {
                channel.up[i].prepare(1 << i);
                channel.down[i].prepare(1 << i);
            }
            channel.dry.prepare(64);
        }
        designedFactor_ = -1;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : state_) {
            for (std::size_t i = 0; i < 3; ++i) {
                channel.up[i].reset();
                channel.down[i].reset();
            }
            channel.dry.reset();
            for (auto& path : channel.paths)
                path.reset();
            channel.pedalHigh.state = 0.0;
            channel.pedalTone.state = 0.0;
            channel.boostShelf = {};
            channel.gateEnvelope = 0.0;
            channel.gateGain = 1.0;
            channel.gateOpen = true;
        }
        // The channel chosen plays at once, without a fade from the other.
        snap_ = true;
    }

    // Oversampled, the filters there and back delay it (the dry part too).
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override
    {
        return choice(value(amp::ampOversampling), 3) > 0 ? 16u : 0u;
    }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override { return 16; }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace amp;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, ampParameterCount> values {};
        for (int index = 0; index < ampParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        const auto factorIndex = static_cast<std::size_t>(choice(values[ampOversampling], 3));
        const auto factor = 1 << factorIndex;
        const auto osRate = rate_ * factor;
        if (values != designed_ || factor != designedFactor_)
            design(values, osRate, factor);

        // The channel playing; switched, the other fades out over 20 ms
        // while it fades in (equal power: two tones, not one).
        const auto target = static_cast<double>(choice(values[ampChannel], 2));
        if (std::exchange(snap_, false))
            weight_ = target;
        const auto step = 1.0 / (0.02 * rate_);
        const auto from = weight_;
        const auto to = from < target ? std::min(target, from + step * frames) : std::max(target, from - step * frames);
        // A channel coming in starts from rest, its valves and filters quiet.
        if (from >= 1.0 && to < 1.0)
            for (auto& channel : state_)
                channel.paths[0].reset();
        if (from <= 0.0 && to > 0.0)
            for (auto& channel : state_)
                channel.paths[1].reset();
        weight_ = to;

        const auto sag = static_cast<double>(std::clamp(values[ampSag], 0.0f, 1.0f));
        const auto sagAttack = smoothing(8.0, osRate);
        const auto sagRelease = smoothing(160.0, osRate);
        const auto outputGain = dbToGain(values[ampOutput]) * 0.5;
        const auto mix = static_cast<double>(std::clamp(values[ampMix], 0.0f, 1.0f));
        const auto pedal = choice(values[ampPedal], 4);
        const auto drive = static_cast<double>(std::clamp(values[ampPedalDrive], 0.0f, 1.0f));
        const auto pedalLevel = dbToGain(values[ampPedalLevel]);
        const auto gateOn = values[ampGate] > -89.5f;
        const auto gateOpenAt = dbToGain(values[ampGate]);
        const auto gateCloseAt = gateOpenAt * 0.5;
        const auto gateRelease = smoothing(40.0, rate_);
        const auto gateOpening = smoothing(1.0, rate_);
        const auto gateClosing = smoothing(60.0, rate_);

        for (std::size_t channel = 0; channel < channels; ++channel) {
            auto& s = state_[channel];
            auto& up = s.up[factorIndex];
            auto& down = s.down[factorIndex];
            const auto latency = up.latency();
            const auto* in = input.channel(static_cast<std::uint32_t>(channel));
            auto* out = output.channel(static_cast<std::uint32_t>(channel));
            auto weight = from;
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                auto x = static_cast<double>(in[frame]);
                s.dry.push(x);
                // The gate, before anything.
                if (gateOn) {
                    s.gateEnvelope = std::max(std::abs(x), s.gateEnvelope * gateRelease);
                    if (s.gateOpen && s.gateEnvelope < gateCloseAt)
                        s.gateOpen = false;
                    else if (!s.gateOpen && s.gateEnvelope > gateOpenAt)
                        s.gateOpen = true;
                    const auto wanted = s.gateOpen ? 1.0 : 0.0;
                    const auto coefficient = s.gateOpen ? gateOpening : gateClosing;
                    s.gateGain = wanted + coefficient * (s.gateGain - wanted);
                    x *= s.gateGain;
                }
                weight = weight < target ? std::min(target, weight + step) : std::max(target, weight - step);
                const auto gainA = weight <= 0.0 ? 1.0 : weight >= 1.0 ? 0.0 : std::cos(weight * kPi * 0.5);
                const auto gainB = weight <= 0.0 ? 0.0 : weight >= 1.0 ? 1.0 : std::sin(weight * kPi * 0.5);
                std::array<double, 4> fast {};
                up.up(x, fast.data());
                for (int phase = 0; phase < factor; ++phase) {
                    auto v = fast[static_cast<std::size_t>(phase)];
                    // The pedal.
                    if (pedal == 1) {
                        v = run(boostShelf_, s.boostShelf, v * boostGain_);
                        v *= pedalLevel;
                    } else if (pedal == 2) {
                        const auto clipped = std::tanh(s.pedalHigh.highPass(v) * (1.0 + drive * 60.0)) * 0.7;
                        v = s.pedalTone.lowPass(0.4 * v + clipped) * pedalLevel;
                    } else if (pedal == 3) {
                        const auto pushed = v * (10.0 + drive * 300.0);
                        const auto clipped = pushed > 0.0 ? std::tanh(pushed) : std::max(-1.0, pushed * 0.8);
                        v = s.pedalTone.lowPass(clipped) * 0.5 * pedalLevel;
                    }
                    // The amp: the channel playing, both while one fades into the other.
                    auto played = 0.0;
                    if (gainA > 0.0)
                        played += gainA * amplify(paths_[0], s.paths[0], v, sag, sagAttack, sagRelease);
                    if (gainB > 0.0)
                        played += gainB * amplify(paths_[1], s.paths[1], v, sag, sagAttack, sagRelease);
                    fast[static_cast<std::size_t>(phase)] = played;
                }
                const auto wet = down.down(fast.data()) * outputGain;
                const auto dry = s.dry.read(static_cast<double>(latency));
                out[frame] = static_cast<float>(mix * wet + (1.0 - mix) * dry);
            }
        }
        // Channels past those processed pass through.
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    // One of the amp's channels as designed: its model, its valves' gains,
    // its filters' coefficients.
    struct PathDesign {
        const amp::AmpModel* model { &amp::ampModel(0) };
        double gain { 1.0 };
        double level { 1.0 };
        double powerGain { 1.0 };
        amp::ThirdOrder stack;
        double stackMakeup { 1.0 };
        BiquadCoefficients midBoost;
    };
    // A channel's states on one side of the sound.
    struct PathState {
        Chain before;
        Chain after;
        BiquadState midBoost;
        ThirdOrderState stack;
        std::array<OnePole, 4> coupling {};
        std::array<OnePole, 4> smooth {};
        OnePole dc;
        double sag {};
        void reset() noexcept
        {
            before.reset();
            after.reset();
            midBoost = {};
            stack = {};
            for (auto& pole : coupling)
                pole.state = 0.0;
            for (auto& pole : smooth)
                pole.state = 0.0;
            dc.state = 0.0;
            sag = 0.0;
        }
    };

    // The preamp, the tone stack and the power stage of a channel.
    static double amplify(const PathDesign& d, PathState& s, double v, const double sag, const double sagAttack,
                          const double sagRelease) noexcept
    {
        using namespace amp;
        const auto& model = *d.model;
        v = s.before.process(v);
        for (int stage = 0; stage < model.stages; ++stage) {
            if (stage == model.stages - 1 && model.midBoostDb != 0.0)
                v = run(d.midBoost, s.midBoost, v);
            v = valveStage(v * d.gain, model.bias);
            v = s.coupling[static_cast<std::size_t>(stage)].highPass(v);
            v = s.smooth[static_cast<std::size_t>(stage)].lowPass(v);
        }
        if (model.stages % 2 != 0)
            v = -v;
        v *= d.level;
        // The tone stack.
        v = run(d.stack, s.stack, v) * d.stackMakeup;
        // The power stage, its supply sagging under it.
        auto p = v * d.powerGain;
        const auto magnitude = std::abs(p);
        s.sag = magnitude > s.sag ? magnitude + sagAttack * (s.sag - magnitude) : magnitude + sagRelease * (s.sag - magnitude);
        p /= 1.0 + sag * s.sag * 1.2;
        v = powerStage(p, model.powerKnee);
        v = s.after.process(v);
        return s.dc.highPass(v);
    }

    void design(const std::array<float, amp::ampParameterCount>& values, const double osRate, const int factor) noexcept
    {
        using namespace amp;
        for (std::size_t path = 0; path < paths_.size(); ++path) {
            const auto played = ampChannelValues(values, static_cast<int>(path));
            auto& d = paths_[path];
            d.model = &ampModel(choice(played[ampModelValue], ampModelCount));
            const auto voicing = ampVoicing(played, osRate);
            d.stack = voicing.stack;
            d.stackMakeup = voicing.stackMakeup;
            d.midBoost = voicing.midBoost;
            d.gain = stageGain(*d.model, played[ampGain]);
            d.level = preampLevel(*d.model, played[ampGain]);
            d.powerGain = dbToGain(-18.0 + played[ampMaster] / 10.0 * (18.0 + d.model->powerDriveDb));
            for (auto& channel : state_) {
                auto& s = channel.paths[path];
                s.before.set(voicing.before);
                s.after.set(voicing.after);
                for (int stage = 0; stage < 4; ++stage) {
                    s.coupling[static_cast<std::size_t>(stage)].design(d.model->couplingHz, osRate);
                    s.smooth[static_cast<std::size_t>(stage)].design(d.model->smoothingHz, osRate);
                }
                s.dc.design(10.0, osRate);
                if (factor != designedFactor_) {
                    s.stack = {};
                    s.midBoost = {};
                }
            }
        }
        const auto drive = static_cast<double>(std::clamp(values[ampPedalDrive], 0.0f, 1.0f));
        const auto tone = static_cast<double>(std::clamp(values[ampPedalTone], 0.0f, 1.0f));
        boostGain_ = dbToGain(drive * 20.0);
        boostShelf_ = designEqBand(EqBandType::highShelf, 1'500.0, (tone - 0.5) * 12.0, 0.7, 0, 0, osRate);
        for (auto& channel : state_) {
            channel.pedalHigh.design(720.0, osRate);
            channel.pedalTone.design(600.0 * std::pow(10.0, tone), osRate);
        }
        designed_ = values;
        designedFactor_ = factor;
    }

    struct ChannelState {
        std::array<Oversampler, 3> up;
        std::array<Oversampler, 3> down;
        Delay dry;
        // Channel A's and channel B's.
        std::array<PathState, 2> paths {};
        OnePole pedalHigh;
        OnePole pedalTone;
        BiquadState boostShelf;
        double gateEnvelope {};
        double gateGain { 1.0 };
        bool gateOpen { true };
    };

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<ChannelState, kChannels> state_ {};
    std::array<float, amp::ampParameterCount> designed_ {};
    int designedFactor_ { -1 };
    std::array<PathDesign, 2> paths_ {};
    BiquadCoefficients boostShelf_;
    double boostGain_ { 1.0 };
    // How much of channel B is heard (0 A alone, 1 B alone).
    double weight_ {};
    bool snap_ { true };
};

// ---- Baffle et micro -----------------------------------------------------------------------------

class CabSim final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    // A response loaded (a cabinet recorded with its microphone): it takes
    // the place of the modelled cabinet and microphones.
    void setImpulse(ImpulseResponseRef response) override { impulse_ = std::move(response); }
    [[nodiscard]] const ImpulseResponse* impulse() const noexcept override { return impulse_.get(); }

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        for (auto& channel : state_) {
            channel.delayA.prepare(static_cast<std::size_t>(0.006 * rate_));
            channel.delayB.prepare(static_cast<std::size_t>(0.006 * rate_));
            channel.room.prepare(rate_);
        }
        // The response read at this rate, a convolver per side.
        responseChannels_ = 0;
        const auto response = impulse_ != nullptr ? impulseAtRate(*impulse_, rate_) : std::vector<std::vector<float>> {};
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            if (!response.empty() && channel < std::max<std::size_t>(channels_, 2))
                state_[channel].convolver.prepare(response[channel % response.size()]);
            else
                state_[channel].convolver = {};
        }
        responseChannels_ = response.size();
        designed_ = {};
        designed_[0] = -1.0f;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : state_) {
            channel.core.reset();
            channel.placeA.reset();
            channel.placeB.reset();
            channel.micA.reset();
            channel.micB.reset();
            channel.cuts.reset();
            channel.delayA.reset();
            channel.delayB.reset();
            channel.room.reset();
            channel.convolver.reset();
        }
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace amp;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, cabParameterCount> values {};
        for (int index = 0; index < cabParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        if (designKey(values) != designed_)
            design(values);
        const auto& micA = micModel(choice(values[cabMicA], cabMicCount));
        const auto& micB = micModel(choice(values[cabMicB], cabMicCount));
        // A loaded response is the cabinet and its microphone: the models,
        // the second microphone and the placement are not played then.
        const auto loaded = responseChannels_ > 0 && !state_[0].convolver.empty();
        const auto bOn = !loaded && values[cabMicBOn] >= 0.5f;
        const auto aligned = loaded || values[cabAlign] >= 0.5f;
        const auto blend = static_cast<double>(std::clamp(values[cabBlend], 0.0f, 1.0f));
        const auto polarity = values[cabPolarityB] >= 0.5f ? -1.0 : 1.0;
        const auto stereo = values[cabStereo] >= 0.5f && bOn;
        const auto delayA = aligned ? 0.0 : flightSeconds(values[cabDistanceA]) * rate_;
        const auto delayB = aligned ? 0.0 : flightSeconds(values[cabDistanceB]) * rate_;
        const auto roomGain = static_cast<double>(values[cabRoom]) * (0.5 + 0.5 * farness(values[cabDistanceA])) * 0.6;
        for (auto& channel : state_)
            channel.room.set(values[cabRoomSize], values[cabRoomTone], rate_);
        const auto outputGain = dbToGain(values[cabOutput]) * (loaded ? 0.5 : makeup_);
        const auto mix = static_cast<double>(std::clamp(values[cabMix], 0.0f, 1.0f));
        // A mono source makes both sides of a stereo pair (of microphones, or
        // of a stereo response).
        const auto outputs = std::min<std::size_t>(output.channelCount, kChannels);
        const auto pair = (stereo || (loaded && responseChannels_ >= 2)) && channels == 1 && outputs >= 2;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            auto& s = state_[channel];
            const auto* in = input.channel(static_cast<std::uint32_t>(channel));
            auto* out = output.channel(static_cast<std::uint32_t>(channel));
            auto* second = pair ? output.channel(1) : nullptr;
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                const auto x = static_cast<double>(in[frame]);
                if (loaded) {
                    // The response, then the room, the cuts and the output.
                    const auto heard = static_cast<double>(s.convolver.process(static_cast<float>(x)));
                    const auto room = roomGain > 0.0 ? s.room.process(heard, rate_, channel % 2 == 1) * roomGain : 0.0;
                    out[frame] = static_cast<float>(mix * s.cuts.process(heard + room) * outputGain + (1.0 - mix) * x);
                    if (second != nullptr) {
                        auto& right = state_[1];
                        const auto other = static_cast<double>(right.convolver.process(static_cast<float>(x)));
                        const auto roomRight = roomGain > 0.0 ? right.room.process(other, rate_, true) * roomGain : 0.0;
                        second[frame] = static_cast<float>(mix * s.cutsRight.process(other + roomRight) * outputGain + (1.0 - mix) * x);
                    }
                    continue;
                }
                const auto cabinet = s.core.process(x);
                auto a = s.micA.process(s.placeA.process(cabinet));
                if (micA.saturation == 1)
                    a = preampSaturate(1, a);
                s.delayA.push(a);
                a = s.delayA.read(delayA);
                auto b = 0.0;
                if (bOn) {
                    b = s.micB.process(s.placeB.process(cabinet));
                    if (micB.saturation == 1)
                        b = preampSaturate(1, b);
                    s.delayB.push(b);
                    b = s.delayB.read(delayB) * polarity;
                }
                const auto room = roomGain > 0.0 ? s.room.process(cabinet, rate_, channel % 2 == 1) * roomGain : 0.0;
                auto heard = 0.0;
                if (stereo)
                    heard = (channel % 2 == 0 ? a : b) + room;
                else
                    heard = (bOn ? a * (1.0 - blend) + b * blend : a) + room;
                const auto wet = s.cuts.process(heard) * outputGain;
                out[frame] = static_cast<float>(mix * wet + (1.0 - mix) * x);
                if (second != nullptr) {
                    const auto right = s.cutsRight.process(b + room) * outputGain;
                    second[frame] = static_cast<float>(mix * right + (1.0 - mix) * x);
                }
            }
        }
        if (!pair)
            for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
                std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames,
                            output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    void design(const std::array<float, amp::cabParameterCount>& values) noexcept
    {
        using namespace amp;
        const auto& cab = cabModel(choice(values[cabModelValue], cabModelCount));
        const auto& micA = micModel(choice(values[cabMicA], cabMicCount));
        const auto& micB = micModel(choice(values[cabMicB], cabMicCount));
        const auto core = cabCoreSections(cab, values[cabResonance], rate_);
        const auto placeA = cabPlaceSections(cab, values[cabPositionA], values[cabAngleA], rate_);
        const auto placeB = cabPlaceSections(cab, values[cabPositionB], values[cabAngleB], rate_);
        const auto sectionsA = micSections(micA, micA.pattern, values[cabDistanceA], values[cabAngleA], rate_);
        const auto sectionsB = micSections(micB, micB.pattern, values[cabDistanceB], values[cabAngleB], rate_);
        Sections cuts;
        if (values[cabLowCut] > 20.5f)
            cuts.push_back(designEqBand(EqBandType::lowCut, values[cabLowCut], 0.0, 0.707, 0, 0, rate_));
        if (values[cabHighCut] < 19'900.0f)
            cuts.push_back(designEqBand(EqBandType::highCut, std::min(static_cast<double>(values[cabHighCut]), rate_ * 0.45), 0.0, 0.707,
                                        0, 0, rate_));
        for (auto& channel : state_) {
            channel.core.set(core);
            channel.placeA.set(placeA);
            channel.placeB.set(placeB);
            channel.micA.set(sectionsA);
            channel.micB.set(sectionsB);
            channel.cuts.set(cuts);
            channel.cutsRight.set(cuts);
        }
        makeup_ = dbToGain(cabMakeupDb(values, rate_));
        designed_ = designKey(values);
    }

    // The values the sections and the makeup are designed from; the others
    // (output, mix, room, stereo) are read every block and design nothing.
    [[nodiscard]] static std::array<float, amp::cabParameterCount> designKey(std::array<float, amp::cabParameterCount> values) noexcept
    {
        using namespace amp;
        for (const auto index : { cabOutput, cabMix, cabRoom, cabRoomSize, cabRoomTone, cabStereo })
            values[static_cast<std::size_t>(index)] = 0.0f;
        return values;
    }

    struct ChannelState {
        Chain core;
        Chain placeA;
        Chain placeB;
        Chain micA;
        Chain micB;
        Chain cuts;
        Chain cutsRight;
        Delay delayA;
        Delay delayB;
        Room room;
        Convolver convolver;
    };

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<ChannelState, kChannels> state_ {};
    std::array<float, amp::cabParameterCount> designed_ {};
    double makeup_ { 1.0 };
    ImpulseResponseRef impulse_;
    std::size_t responseChannels_ {};
};

// ---- Micro ---------------------------------------------------------------------------------------

class MicSim final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        for (auto& channel : state_)
            channel.room.prepare(rate_);
        designed_ = {};
        designed_[0] = -1.0f;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : state_) {
            channel.mic.reset();
            channel.cuts.reset();
            channel.room.reset();
            channel.emphasis.state = 0.0;
        }
        random_ = 0x6c8e9cf5u;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace amp;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, micParameterCount> values {};
        for (int index = 0; index < micParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        if (values != designed_)
            design(values);
        const auto& model = micModel(choice(values[micModelValue], micModelCount));
        const auto pattern = micPatternOf(values);
        const auto level = patternGain(pattern, values[micAngle]);
        const auto modelDrive = dbToGain(model.driveDb);
        const auto drive = dbToGain(values[micDrive]);
        const auto preamp = choice(values[micPreamp], 3);
        const auto noiseDb = std::max(static_cast<double>(values[micNoise]), model.noiseDb);
        const auto noise = noiseDb > -89.5 ? dbToGain(noiseDb) : 0.0;
        const auto roomGain = static_cast<double>(values[micRoom]) * (0.3 + 0.7 * farness(values[micDistance])) * 0.8;
        for (auto& channel : state_)
            channel.room.set(values[micRoomSize], values[micRoomTone], rate_);
        const auto outputGain = dbToGain(values[micOutput]) * makeup_;
        const auto mix = static_cast<double>(std::clamp(values[micMix], 0.0f, 1.0f));
        for (std::size_t channel = 0; channel < channels; ++channel) {
            auto& s = state_[channel];
            const auto* in = input.channel(static_cast<std::uint32_t>(channel));
            auto* out = output.channel(static_cast<std::uint32_t>(channel));
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                const auto x = static_cast<double>(in[frame]);
                auto v = x;
                if (noise > 0.0)
                    v += noise * whiteNoise();
                v = s.mic.process(v) * level;
                // A cheap speaker's or a valve's own colour.
                if (model.saturation == 2)
                    v = std::clamp(std::tanh(v * modelDrive * 1.3), -0.85, 0.85) / std::sqrt(modelDrive);
                else if (model.saturation == 1)
                    v = preampSaturate(1, v * modelDrive) / modelDrive;
                // The preamp, driven: a transformer saturates its lows first.
                if (values[micDrive] > 0.05f) {
                    if (preamp == 2) {
                        const auto low = s.emphasis.lowPass(v);
                        v = preampSaturate(2, (v + low) * drive) / drive - low;
                    } else {
                        v = preampSaturate(preamp, v * drive) / drive;
                    }
                }
                if (roomGain > 0.0)
                    v += s.room.process(v, rate_, channel % 2 == 1) * roomGain;
                const auto wet = s.cuts.process(v) * outputGain;
                out[frame] = static_cast<float>(mix * wet + (1.0 - mix) * x);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    double whiteNoise() noexcept
    {
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        return static_cast<double>(random_ & 0xffffffu) / 8'388'607.5 - 1.0;
    }

    void design(const std::array<float, amp::micParameterCount>& values) noexcept
    {
        using namespace amp;
        const auto& model = micModel(choice(values[micModelValue], micModelCount));
        const auto sections = micSections(model, micPatternOf(values), values[micDistance], values[micAngle], rate_);
        Sections cuts;
        if (values[micLowCut] > 20.5f)
            cuts.push_back(designEqBand(EqBandType::lowCut, values[micLowCut], 0.0, 0.707, 0, 0, rate_));
        if (values[micHighCut] < 19'900.0f)
            cuts.push_back(designEqBand(EqBandType::highCut, std::min(static_cast<double>(values[micHighCut]), rate_ * 0.45), 0.0, 0.707,
                                        0, 0, rate_));
        for (auto& channel : state_) {
            channel.mic.set(sections);
            channel.cuts.set(cuts);
            channel.emphasis.design(150.0, rate_);
        }
        makeup_ = dbToGain(micMakeupDb(values, rate_));
        designed_ = values;
    }

    struct ChannelState {
        Chain mic;
        Chain cuts;
        Room room;
        OnePole emphasis;
    };

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<ChannelState, kChannels> state_ {};
    std::array<float, amp::micParameterCount> designed_ {};
    double makeup_ { 1.0 };
    std::uint32_t random_ { 0x6c8e9cf5u };
};

} // namespace

std::unique_ptr<EffectProcessor> makeAmpEffect(const EffectDescriptor& descriptor)
{
    switch (descriptor.kind) {
    case NativeDspNodeKind::ampSim: return std::make_unique<AmpSim>(descriptor);
    case NativeDspNodeKind::cabSim: return std::make_unique<CabSim>(descriptor);
    case NativeDspNodeKind::micSim: return std::make_unique<MicSim>(descriptor);
    // The mixing effects (MixEffects.cpp).
    default: return makeMixEffect(descriptor);
    }
}

} // namespace cydaw::engine
