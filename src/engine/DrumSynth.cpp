#include <cydaw/engine/DrumSynth.h>

#include <cydaw/engine/BiquadDesign.h>
#include <cydaw/engine/KickSynth.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace cydaw::engine::drums {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

struct Biquad {
    BiquadCoefficients c;
    double z1 {};
    double z2 {};
    double operator()(const double x) noexcept
    {
        const auto y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

Biquad filter(const EqBandType type, const double hz, const double q, const double rate, const double gainDb = 0.0)
{
    return { designEqBand(type, std::clamp(hz, 10.0, rate * 0.45), gainDb, q, 0, 0, rate) };
}

class Noise {
public:
    explicit Noise(const std::uint32_t seed) : state_(seed == 0 ? 0x9e3779b9u : seed) {}
    double operator()() noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<double>(state_ & 0xffffffu) / 8'388'607.5 - 1.0;
    }

private:
    std::uint32_t state_;
};

// The recipe's value, or the kind's default.
class Values {
public:
    explicit Values(const Recipe& recipe) : recipe_(recipe) {}
    double operator()(const char* key, const double fallback) const
    {
        for (const auto& [name, value] : recipe_.values)
            if (name == key && std::isfinite(value))
                return value;
        return fallback;
    }

private:
    const Recipe& recipe_;
};

double decayOf(const double t, const double milliseconds) noexcept
{
    // To -60 dB over the time given.
    return std::exp(std::log(0.001) * t / std::max(0.0005, milliseconds * 0.001));
}

double square(const double phase) noexcept
{
    return phase - std::floor(phase) < 0.5 ? 1.0 : -1.0;
}

// The 808's six detuned squares: the body of its hats and cymbals.
double metal(std::array<double, 6>& phases, const double pitch, const double step) noexcept
{
    static constexpr std::array<double, 6> frequencies { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    auto sum = 0.0;
    for (std::size_t index = 0; index < phases.size(); ++index) {
        sum += square(phases[index]);
        phases[index] += frequencies[index] * pitch * step;
    }
    return sum / 6.0;
}

std::vector<float> snare(const Values& v, const std::size_t frames, const double rate, Noise& noise)
{
    const auto tone = v("tone", 185.0);
    const auto tone2 = v("tone2", 330.0);
    const auto toneDecay = v("toneDecay", 120.0);
    const auto noiseDecay = v("noiseDecay", 180.0);
    const auto snappy = v("snappy", 0.6);
    const auto bend = v("bend", 1.4);
    const auto bendTime = v("bendTime", 25.0) * 0.001;
    const auto drive = std::pow(10.0, v("drive", 0.0) / 20.0);
    auto band = filter(EqBandType::bandPass, v("noiseTone", 5'000.0), 0.6, rate);
    auto high = filter(EqBandType::lowCut, v("noiseCut", 1'200.0), 0.7, rate);
    std::vector<float> out(frames);
    double phase1 {}, phase2 {};
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / rate;
        const auto lift = 1.0 + (bend - 1.0) * std::exp(-t / std::max(0.001, bendTime));
        const auto body = (std::sin(kTwoPi * phase1) + 0.6 * std::sin(kTwoPi * phase2)) * decayOf(t, toneDecay) * (1.0 - 0.5 * snappy);
        phase1 += tone * lift / rate;
        phase2 += tone2 * lift / rate;
        const auto rattle = high(band(noise())) * decayOf(t, noiseDecay) * snappy * 2.2;
        out[i] = static_cast<float>(std::tanh((body + rattle) * drive));
    }
    return out;
}

std::vector<float> clap(const Values& v, const std::size_t frames, const double rate, Noise& noise)
{
    const auto spacing = v("spacing", 9.0) * 0.001;
    const auto bursts = std::clamp(static_cast<int>(std::lround(v("bursts", 3.0))), 1, 8);
    const auto decay = v("decay", 220.0);
    auto band = filter(EqBandType::bandPass, v("tone", 1'100.0), v("q", 1.3), rate);
    auto high = filter(EqBandType::lowCut, 500.0, 0.7, rate);
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / rate;
        auto envelope = 0.0;
        for (int burst = 0; burst < bursts; ++burst) {
            const auto start = spacing * burst;
            if (t >= start)
                envelope = std::max(envelope, burst == bursts - 1 ? decayOf(t - start, decay) : decayOf(t - start, 12.0));
        }
        out[i] = static_cast<float>(high(band(noise())) * envelope * 2.5);
    }
    return out;
}

std::vector<float> metallic(const Values& v, const std::size_t frames, const double rate, Noise& noise, const double decayDefault,
                            const double toneDefault, const double cutDefault, const double noiseDefault)
{
    const auto pitch = v("pitch", 1.0);
    const auto decay = v("decay", decayDefault);
    const auto noiseMix = v("noise", noiseDefault);
    const auto attack = v("attack", 0.0) * 0.001;
    auto band = filter(EqBandType::bandPass, v("tone", toneDefault), 0.6, rate);
    auto cut = filter(EqBandType::lowCut, v("cut", cutDefault), 0.7, rate);
    auto cut2 = filter(EqBandType::lowCut, v("cut", cutDefault), 0.7, rate);
    std::array<double, 6> phases {};
    for (std::size_t index = 0; index < phases.size(); ++index)
        phases[index] = static_cast<double>(index) * 0.137;
    std::vector<float> out(frames);
    const auto step = 1.0 / rate;
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / rate;
        const auto source = (1.0 - noiseMix) * metal(phases, pitch, step) + noiseMix * noise();
        const auto rise = attack > 0.0 ? std::min(1.0, t / attack) : 1.0;
        out[i] = static_cast<float>(cut2(cut(band(source))) * decayOf(t, decay) * rise * 3.0);
    }
    return out;
}

std::vector<float> pitched(const Values& v, const std::size_t frames, const double rate, Noise& noise, const double startDefault,
                           const double endDefault, const double timeDefault, const double decayDefault, const double noiseDefault)
{
    const auto start = v("start", startDefault);
    const auto end = v("end", endDefault);
    const auto time = std::max(0.001, v("time", timeDefault) * 0.001);
    const auto decay = v("decay", decayDefault);
    const auto noiseMix = v("noise", noiseDefault);
    auto band = filter(EqBandType::bandPass, v("noiseTone", 3'000.0), 0.8, rate);
    std::vector<float> out(frames);
    auto phase = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / rate;
        const auto frequency = end + (start - end) * std::exp(-t / time);
        const auto body = std::sin(kTwoPi * phase) * decayOf(t, decay);
        phase += frequency / rate;
        const auto slap = band(noise()) * decayOf(t, 25.0) * noiseMix * 3.0;
        out[i] = static_cast<float>(body + slap);
    }
    return out;
}

std::vector<float> rim(const Values& v, const std::size_t frames, const double rate, Noise& noise)
{
    const auto tone = v("tone", 1'700.0);
    const auto tone2 = v("tone2", 520.0);
    const auto decay = v("decay", 22.0);
    const auto noiseMix = v("noise", 0.3);
    auto high = filter(EqBandType::lowCut, 400.0, 0.7, rate);
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / rate;
        const auto body = (std::sin(kTwoPi * tone * t) + 0.7 * std::sin(kTwoPi * tone2 * t)) * decayOf(t, decay);
        out[i] = static_cast<float>(high(body + noise() * noiseMix * decayOf(t, decay * 0.6)));
    }
    return out;
}

std::vector<float> cowbell(const Values& v, const std::size_t frames, const double rate)
{
    const auto f1 = v("f1", 540.0);
    const auto f2 = v("f2", 800.0);
    const auto decay = v("decay", 300.0);
    auto band = filter(EqBandType::bandPass, v("tone", 2'600.0), 0.8, rate);
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / rate;
        const auto source = 0.5 * (square(f1 * t) + square(f2 * t));
        // A sharp first drop, then a longer ring.
        const auto envelope = 0.6 * decayOf(t, 60.0) + 0.4 * decayOf(t, decay);
        out[i] = static_cast<float>(band(source) * envelope * 2.0);
    }
    return out;
}

std::vector<float> shaker(const Values& v, const std::size_t frames, const double rate, Noise& noise)
{
    const auto attack = std::max(0.0005, v("attack", 12.0) * 0.001);
    const auto decay = v("decay", 90.0);
    const auto shakes = std::clamp(static_cast<int>(std::lround(v("shakes", 1.0))), 1, 6);
    const auto spacing = v("spacing", 35.0) * 0.001;
    auto band = filter(EqBandType::bandPass, v("tone", 6'500.0), 1.0, rate);
    auto high = filter(EqBandType::lowCut, 2'500.0, 0.7, rate);
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / rate;
        auto envelope = 0.0;
        for (int shake = 0; shake < shakes; ++shake) {
            const auto local = t - spacing * shake;
            if (local >= 0.0)
                envelope = std::max(envelope, std::min(1.0, local / attack) * decayOf(std::max(0.0, local - attack), decay)
                                                  * (shake == 0 ? 1.0 : 0.7));
        }
        out[i] = static_cast<float>(high(band(noise())) * envelope * 3.0);
    }
    return out;
}

std::vector<float> zap(const Values& v, const std::size_t frames, const double rate)
{
    const auto start = v("start", 4'000.0);
    const auto end = v("end", 80.0);
    const auto time = std::max(0.001, v("time", 120.0) * 0.001);
    const auto decay = v("decay", 160.0);
    std::vector<float> out(frames);
    auto phase = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / rate;
        const auto frequency = end * std::pow(start / end, std::exp(-t / time));
        out[i] = static_cast<float>(std::sin(kTwoPi * phase) * decayOf(t, decay));
        phase += frequency / rate;
    }
    return out;
}

std::vector<float> sweep(const Values& v, const std::size_t frames, const double rate, Noise& noise)
{
    const auto from = v("from", 300.0);
    const auto to = v("to", 9'000.0);
    const auto q = v("q", 3.0);
    const auto rising = v("rise", 1.0) >= 0.5;
    std::vector<float> out(frames);
    Biquad band;
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / static_cast<double>(std::max<std::size_t>(1, frames - 1));
        if (i % 32 == 0) {
            const auto c = filter(EqBandType::bandPass, from * std::pow(to / from, t), q, rate);
            band.c = c.c;
        }
        const auto envelope = rising ? t * t : (1.0 - t) * (1.0 - t);
        out[i] = static_cast<float>(band(noise()) * envelope * 2.0);
    }
    return out;
}

std::vector<float> impact(const Values& v, const std::size_t frames, const double rate, Noise& noise)
{
    const auto low = v("low", 45.0);
    const auto decay = v("decay", 1'500.0);
    const auto noiseMix = v("noise", 0.5);
    const auto noiseDecay = v("noiseDecay", 300.0);
    auto lowpass = filter(EqBandType::highCut, v("tone", 3'000.0), 0.7, rate);
    std::vector<float> out(frames);
    auto phase = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / rate;
        const auto frequency = low * (1.0 + 2.0 * std::exp(-t / 0.05));
        const auto boom = std::sin(kTwoPi * phase) * decayOf(t, decay);
        phase += frequency / rate;
        out[i] = static_cast<float>(std::tanh(boom * 1.5 + lowpass(noise()) * noiseMix * decayOf(t, noiseDecay)));
    }
    return out;
}

double defaultSeconds(const std::string& kind)
{
    if (kind == "cymbal" || kind == "impact")
        return 2.5;
    if (kind == "sweep")
        return 2.0;
    if (kind == "hat" || kind == "rim" || kind == "shaker")
        return 0.5;
    return 1.0;
}

} // namespace

const std::vector<std::string>& kinds()
{
    static const std::vector<std::string> list { "kick", "snare", "clap", "hat", "cymbal", "tom", "rim", "cowbell", "shaker",
                                                 "tambourine", "conga", "zap", "sweep", "impact" };
    return list;
}

std::vector<float> render(const Recipe& recipe, const double sampleRate)
{
    const auto rate = std::max(8'000.0, sampleRate);
    const Values v(recipe);
    Noise noise(recipe.seed);
    std::vector<float> out;
    if (recipe.kind == "kick") {
        auto values = kick::defaultValues();
        for (const auto& [name, value] : recipe.values)
            if (const auto index = kick::parameterIndex(name); index >= 0)
                values[static_cast<std::size_t>(index)] = kick::clampValue(index, static_cast<float>(value));
        const auto seconds = recipe.seconds > 0.0 ? recipe.seconds : kick::lengthSeconds(values) + 0.02;
        out = kick::renderKick(values, rate, static_cast<int>(std::lround(v("note", kick::referenceNote))), 1.0f, seconds);
        out.resize(static_cast<std::size_t>(std::lround(seconds * rate)), 0.0f);
    } else {
        const auto seconds = recipe.seconds > 0.0 ? recipe.seconds : defaultSeconds(recipe.kind);
        const auto frames = static_cast<std::size_t>(std::lround(seconds * rate));
        if (recipe.kind == "snare")
            out = snare(v, frames, rate, noise);
        else if (recipe.kind == "clap")
            out = clap(v, frames, rate, noise);
        else if (recipe.kind == "hat")
            out = metallic(v, frames, rate, noise, 60.0, 10'000.0, 7'000.0, 0.2);
        else if (recipe.kind == "cymbal")
            out = metallic(v, frames, rate, noise, 1'600.0, 8'000.0, 4'000.0, 0.4);
        else if (recipe.kind == "tambourine")
            out = metallic(v, frames, rate, noise, 220.0, 9'000.0, 5'000.0, 0.5);
        else if (recipe.kind == "tom")
            out = pitched(v, frames, rate, noise, 220.0, 140.0, 90.0, 350.0, 0.1);
        else if (recipe.kind == "conga")
            out = pitched(v, frames, rate, noise, 420.0, 300.0, 15.0, 220.0, 0.35);
        else if (recipe.kind == "rim")
            out = rim(v, frames, rate, noise);
        else if (recipe.kind == "cowbell")
            out = cowbell(v, frames, rate);
        else if (recipe.kind == "shaker")
            out = shaker(v, frames, rate, noise);
        else if (recipe.kind == "zap")
            out = zap(v, frames, rate);
        else if (recipe.kind == "sweep")
            out = sweep(v, frames, rate, noise);
        else if (recipe.kind == "impact")
            out = impact(v, frames, rate, noise);
        else
            return {};
    }
    // At -1 dBFS at its peak, then its gain; the last 5 ms faded.
    auto peak = 0.0f;
    for (const auto sample : out)
        peak = std::max(peak, std::abs(sample));
    const auto scale = peak > 1.0e-9f ? static_cast<float>(std::pow(10.0, (-1.0 + recipe.gainDb) / 20.0) / peak) : 0.0f;
    const auto fade = std::min(out.size(), static_cast<std::size_t>(0.005 * rate));
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] *= scale;
        if (i + fade >= out.size() && fade > 0)
            out[i] *= static_cast<float>(out.size() - i) / static_cast<float>(fade);
    }
    return out;
}

} // namespace cydaw::engine::drums
