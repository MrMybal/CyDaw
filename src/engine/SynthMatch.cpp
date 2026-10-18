#include <cydaw/engine/SynthMatch.h>

#include <cydaw/engine/PrismSynth.h>

#include "SynthParts.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <mutex>
#include <numbers>
#include <random>
#include <thread>

namespace cydaw::engine::synth_match {

namespace {

constexpr double twoPi = 2.0 * std::numbers::pi;

// A radix-2 transform in place (size a power of two).
void transform(std::vector<std::complex<float>>& data)
{
    const auto size = data.size();
    for (std::size_t index = 1, reversed = 0; index < size; ++index) {
        auto bit = size >> 1;
        for (; reversed & bit; bit >>= 1)
            reversed ^= bit;
        reversed ^= bit;
        if (index < reversed)
            std::swap(data[index], data[reversed]);
    }
    for (std::size_t length = 2; length <= size; length <<= 1) {
        const auto angle = -twoPi / static_cast<double>(length);
        const std::complex<float> step(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
        for (std::size_t start = 0; start < size; start += length) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t k = 0; k < length / 2; ++k) {
                const auto t = w * data[start + k + length / 2];
                data[start + k + length / 2] = data[start + k] - t;
                data[start + k] += t;
                w *= step;
            }
        }
    }
}

// YIN (de Cheveigné and Kawahara) over one window: the period's frequency, 0
// when none is clear.
double yin(const float* signal, const int length, const double rate)
{
    const auto maximumLag = std::min(length / 2, static_cast<int>(rate / 30.0));
    const auto minimumLag = std::max(2, static_cast<int>(rate / 2'500.0));
    const auto window = length - maximumLag;
    if (window < 256 || maximumLag <= minimumLag)
        return 0.0;
    std::vector<double> difference(static_cast<std::size_t>(maximumLag + 1), 0.0);
    for (int lag = 1; lag <= maximumLag; ++lag) {
        auto sum = 0.0;
        for (int index = 0; index < window; ++index) {
            const auto delta = static_cast<double>(signal[index]) - signal[index + lag];
            sum += delta * delta;
        }
        difference[static_cast<std::size_t>(lag)] = sum;
    }
    auto running = 0.0;
    std::vector<double> normalised(difference.size(), 1.0);
    for (int lag = 1; lag <= maximumLag; ++lag) {
        running += difference[static_cast<std::size_t>(lag)];
        normalised[static_cast<std::size_t>(lag)] = running > 0.0 ? difference[static_cast<std::size_t>(lag)] * lag / running : 1.0;
    }
    auto chosen = -1;
    for (int lag = minimumLag; lag < maximumLag; ++lag)
        if (normalised[static_cast<std::size_t>(lag)] < 0.15) {
            while (lag + 1 < maximumLag && normalised[static_cast<std::size_t>(lag + 1)] < normalised[static_cast<std::size_t>(lag)])
                ++lag;
            chosen = lag;
            break;
        }
    if (chosen < 0) {
        auto best = minimumLag;
        for (int lag = minimumLag; lag < maximumLag; ++lag)
            if (normalised[static_cast<std::size_t>(lag)] < normalised[static_cast<std::size_t>(best)])
                best = lag;
        if (normalised[static_cast<std::size_t>(best)] > 0.4)
            return 0.0;
        chosen = best;
    }
    // Parabolic interpolation of the dip.
    const auto a = normalised[static_cast<std::size_t>(chosen - 1)];
    const auto b = normalised[static_cast<std::size_t>(chosen)];
    const auto c = normalised[static_cast<std::size_t>(std::min(chosen + 1, maximumLag))];
    const auto denominator = a - 2.0 * b + c;
    const auto shift = std::abs(denominator) > 1.0e-12 ? std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5) : 0.0;
    return rate / (chosen + shift);
}

// A searched setting: its index in the model's values and how 0..1 maps to it.
struct Dimension {
    std::string key;
    int index {};
    synth::Kind kind {};
    float minimum {};
    float maximum {};
    // Only these values (an interval in semitones): the point picks one.
    std::vector<float> allowed;
};

// Settings searched among a few values rather than across their range.
std::vector<float> allowedValues(const synth::Model model, const std::string& key)
{
    if (model == synth::Model::opale && key == "osc2.pitch")
        return { -12.0f, -7.0f, -5.0f, 0.0f, 5.0f, 7.0f, 12.0f, 19.0f, 24.0f };
    // The ratios Quartz plays with its whole ratios on (its default): the
    // knob then reads what is heard.
    if (model == synth::Model::quartz && key.size() >= 6 && key.compare(key.size() - 6, 6, ".ratio") == 0)
        return { 0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f };
    return {};
}

std::vector<Dimension> dimensionsOf(const synth::Model model)
{
    std::vector<Dimension> dimensions;
    for (const auto& key : searchedSettings(model)) {
        const auto index = synth::parameterIndex(model, key);
        if (index < 0)
            continue;
        const auto& parameter = synth::parameter(model, index);
        dimensions.push_back({ key, index, parameter.kind, parameter.minimum, parameter.maximum, allowedValues(model, key) });
    }
    return dimensions;
}

// The fixed settings a search starts from (and keeps): no drift (it would
// make two hearings of one candidate differ).
synth::Values baseValues(const synth::Model model)
{
    auto values = synth::defaultValues(model);
    if (const auto drift = synth::parameterIndex(model, "drift"); drift >= 0)
        values[static_cast<std::size_t>(drift)] = 0.0f;
    return values;
}

float valueOf(const synth::Model model, const Dimension& dimension, const double unit)
{
    const auto x = std::clamp(unit, 0.0, 1.0);
    if (!dimension.allowed.empty())
        return dimension.allowed[static_cast<std::size_t>(std::lround(x * static_cast<double>(dimension.allowed.size() - 1)))];
    switch (dimension.kind) {
    case synth::Kind::continuous:
        return synth::denormalise(model, dimension.index, static_cast<float>(x));
    case synth::Kind::toggle:
        return x > 0.5 ? 1.0f : 0.0f;
    case synth::Kind::integer:
    case synth::Kind::choice:
        return std::round(dimension.minimum + static_cast<float>(x) * (dimension.maximum - dimension.minimum));
    }
    return dimension.minimum;
}

double unitOf(const synth::Model model, const Dimension& dimension, const float value)
{
    if (!dimension.allowed.empty()) {
        std::size_t nearest = 0;
        for (std::size_t index = 0; index < dimension.allowed.size(); ++index)
            if (std::abs(dimension.allowed[index] - value) < std::abs(dimension.allowed[nearest] - value))
                nearest = index;
        return dimension.allowed.size() > 1 ? static_cast<double>(nearest) / static_cast<double>(dimension.allowed.size() - 1) : 0.0;
    }
    switch (dimension.kind) {
    case synth::Kind::continuous:
        return static_cast<double>(synth::normalise(model, dimension.index, value));
    case synth::Kind::toggle:
        return value > 0.5f ? 0.75 : 0.25;
    case synth::Kind::integer:
    case synth::Kind::choice:
        return dimension.maximum > dimension.minimum ? static_cast<double>((value - dimension.minimum) / (dimension.maximum - dimension.minimum))
                                                     : 0.0;
    }
    return 0.0;
}

// The values a point of the search stands for, tuned to the target (its
// oscillators' or operators' fine tuning moved by the target's cents).
synth::Values valuesAt(const synth::Model model, const std::vector<Dimension>& dimensions, const std::vector<double>& point,
                       const Target& target)
{
    auto values = baseValues(model);
    for (std::size_t index = 0; index < dimensions.size(); ++index)
        values[static_cast<std::size_t>(dimensions[index].index)] = valueOf(model, dimensions[index], point[index]);
    const auto detune = [&](const std::string& key) {
        if (const auto index = synth::parameterIndex(model, key); index >= 0)
            values[static_cast<std::size_t>(index)] = synth::clampValue(
                model, index, values[static_cast<std::size_t>(index)] + static_cast<float>(target.cents));
    };
    if (model == synth::Model::quartz) {
        for (const auto* key : { "op1.fine", "op2.fine", "op3.fine", "op4.fine" })
            detune(key);
    } else {
        detune("osc1.fine");
        detune("osc2.fine");
    }
    return values;
}

// Jacobi's rotations: a symmetric matrix's eigenvectors (columns of `vectors`)
// and eigenvalues.
void eigen(std::vector<double> matrix, const int n, std::vector<double>& vectors, std::vector<double>& values)
{
    vectors.assign(static_cast<std::size_t>(n * n), 0.0);
    for (int i = 0; i < n; ++i)
        vectors[static_cast<std::size_t>(i * n + i)] = 1.0;
    const auto at = [n](std::vector<double>& m, const int row, const int column) -> double& {
        return m[static_cast<std::size_t>(row * n + column)];
    };
    for (int sweep = 0; sweep < 60; ++sweep) {
        auto off = 0.0;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q)
                off += at(matrix, p, q) * at(matrix, p, q);
        if (off < 1.0e-22)
            break;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) {
                const auto apq = at(matrix, p, q);
                if (std::abs(apq) < 1.0e-30)
                    continue;
                const auto theta = (at(matrix, q, q) - at(matrix, p, p)) / (2.0 * apq);
                const auto t = (theta >= 0.0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const auto c = 1.0 / std::sqrt(t * t + 1.0);
                const auto s = t * c;
                for (int k = 0; k < n; ++k) {
                    const auto akp = at(matrix, k, p);
                    const auto akq = at(matrix, k, q);
                    at(matrix, k, p) = c * akp - s * akq;
                    at(matrix, k, q) = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const auto apk = at(matrix, p, k);
                    const auto aqk = at(matrix, q, k);
                    at(matrix, p, k) = c * apk - s * aqk;
                    at(matrix, q, k) = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {
                    const auto vkp = at(vectors, k, p);
                    const auto vkq = at(vectors, k, q);
                    at(vectors, k, p) = c * vkp - s * vkq;
                    at(vectors, k, q) = s * vkp + c * vkq;
                }
            }
    }
    values.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        values[static_cast<std::size_t>(i)] = std::max(1.0e-20, at(matrix, i, i));
}

} // namespace

std::vector<std::string> searchedSettings(const synth::Model model)
{
    if (model == synth::Model::opale)
        return { "osc1.shape", "osc1.sub", "osc2.shape", "osc2.level", "osc2.pitch", "osc2.fine", "noise.level", "f.type",
                 "f.cutoff", "f.resonance", "filter.env", "filter.key", "amp.attack", "amp.decay", "amp.sustain", "fenv.attack",
                 "fenv.decay", "fenv.sustain" };
    if (model == synth::Model::quartz) {
        std::vector<std::string> keys;
        for (int op = 1; op <= 4; ++op) {
            const auto prefix = "op" + std::to_string(op) + ".";
            for (const auto* suffix : { "ratio", "level", "attack", "decay", "sustain" })
                keys.push_back(prefix + suffix);
            for (int to = 1; to <= 4; ++to)
                keys.push_back("m." + std::to_string(op) + std::to_string(to));
        }
        for (const auto* key : { "f.cutoff", "f.resonance", "filter.env", "amp.attack", "amp.decay", "amp.sustain", "fenv.decay" })
            keys.emplace_back(key);
        return keys;
    }
    return {};
}

Target prepareTarget(const std::vector<float>& mono, const double rate, const int note, const double seconds)
{
    Target target;
    target.rate = rate > 0.0 ? rate : 48'000.0;
    auto peak = 0.0f;
    for (const auto sample : mono)
        peak = std::max(peak, std::abs(sample));
    if (peak <= 0.0f)
        return target;
    // From 2 ms before the first sound at -50 dB of the peak.
    const auto threshold = peak * 0.00316f;
    std::size_t first = 0;
    while (first < mono.size() && std::abs(mono[first]) < threshold)
        ++first;
    first = first > static_cast<std::size_t>(0.002 * target.rate) ? first - static_cast<std::size_t>(0.002 * target.rate) : 0;
    const auto count = std::min(mono.size() - first, static_cast<std::size_t>(seconds * target.rate));
    target.samples.assign(mono.begin() + static_cast<std::ptrdiff_t>(first), mono.begin() + static_cast<std::ptrdiff_t>(first + count));
    // Its level: a velocity as a note recorded alone is given one.
    target.velocity = static_cast<float>(std::clamp((20.0 * std::log10(peak) + 36.0) / 36.0, 0.1, 1.0));
    // Its pitch: YIN over 100 ms after the attack (the median of three windows).
    if (note >= 0) {
        target.note = note;
        target.cents = 0.0;
        return target;
    }
    std::vector<double> found;
    const auto window = static_cast<int>(0.08 * target.rate);
    for (const auto at : { 0.06, 0.12, 0.2 }) {
        const auto start = static_cast<std::size_t>(at * target.rate);
        if (start + static_cast<std::size_t>(window) < target.samples.size())
            if (const auto hz = yin(target.samples.data() + start, window, target.rate); hz > 0.0)
                found.push_back(hz);
    }
    if (found.empty()) {
        target.note = 60;
        return target;
    }
    std::sort(found.begin(), found.end());
    const auto hz = found[found.size() / 2];
    const auto midi = 69.0 + 12.0 * std::log2(hz / 440.0);
    target.note = std::clamp(static_cast<int>(std::lround(midi)), 0, 127);
    target.cents = 100.0 * (midi - target.note);
    return target;
}

std::vector<float> render(const synth::Model model, const synth::Values& values, const Target& target, const int frames)
{
    std::vector<float> left(static_cast<std::size_t>(frames), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(frames), 0.0f);
    // Every rendering from the same seeds: a candidate always sounds the same,
    // its loss free of the phases' and the noise's luck.
    struct Seeds {
        std::uint32_t next { 0x2545f491u };
        Seeds() noexcept { synth::detail::parts::fixedSeeds = &next; }
        ~Seeds() { synth::detail::parts::fixedSeeds = nullptr; }
    } seeds;
    synth::Voice voice;
    voice.prepare(target.rate);
    voice.start(model, values, target.note, target.velocity, -1.0, target.rate);
    prism::Context context;
    auto done = 0;
    while (done < frames) {
        const auto count = std::min(256, frames - done);
        if (!voice.render(values, context, left.data() + done, right.data() + done, count, target.rate))
            break;
        context.clockSeconds += count / target.rate;
        done += count;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
        left[index] = 0.5f * (left[index] + right[index]);
    return left;
}

// ---- The comparison ----

std::vector<float> Comparison::logSpectrogram(const std::vector<float>& signal, const int size, const int hop, int& rows)
{
    // Log band energies: 40 bands from 40 Hz to 16 kHz (a fifth of an octave
    // or so, as the ear groups them), each at least its nearest bin; rate is
    // taken as 48 kHz for the bands' bins (close enough at 44.1 and 96).
    constexpr int bands = 40;
    const auto columns = size / 2 + 1;
    std::vector<int> bandOf(static_cast<std::size_t>(columns), -1);
    {
        const auto binHz = 48'000.0 / size;
        for (int band = 0; band < bands; ++band) {
            const auto low = 40.0 * std::pow(400.0, static_cast<double>(band) / bands);
            const auto high = 40.0 * std::pow(400.0, static_cast<double>(band + 1) / bands);
            auto any = false;
            for (int column = 1; column < columns; ++column)
                if (column * binHz >= low && column * binHz < high) {
                    bandOf[static_cast<std::size_t>(column)] = band;
                    any = true;
                }
            if (!any) {
                const auto nearest = std::clamp(static_cast<int>(std::lround(std::sqrt(low * high) / binHz)), 1, columns - 1);
                if (bandOf[static_cast<std::size_t>(nearest)] < 0)
                    bandOf[static_cast<std::size_t>(nearest)] = band;
            }
        }
    }
    rows = std::max(1, static_cast<int>(signal.size()) / hop);
    std::vector<float> out(static_cast<std::size_t>(rows * bands), 0.0f);
    std::vector<float> window(static_cast<std::size_t>(size));
    for (int index = 0; index < size; ++index)
        window[static_cast<std::size_t>(index)] = static_cast<float>(0.5 - 0.5 * std::cos(twoPi * index / size));
    std::vector<std::complex<float>> frame(static_cast<std::size_t>(size));
    std::vector<double> energy(static_cast<std::size_t>(bands));
    for (int row = 0; row < rows; ++row) {
        const auto start = row * hop;
        for (int index = 0; index < size; ++index) {
            const auto at = static_cast<std::size_t>(start + index);
            frame[static_cast<std::size_t>(index)] = { at < signal.size() ? signal[at] * window[static_cast<std::size_t>(index)] : 0.0f, 0.0f };
        }
        transform(frame);
        std::fill(energy.begin(), energy.end(), 0.0);
        for (int column = 1; column < columns; ++column)
            if (const auto band = bandOf[static_cast<std::size_t>(column)]; band >= 0)
                energy[static_cast<std::size_t>(band)] += std::norm(frame[static_cast<std::size_t>(column)]);
        for (int band = 0; band < bands; ++band)
            out[static_cast<std::size_t>(row * bands + band)] = static_cast<float>(energy[static_cast<std::size_t>(band)]);
    }
    // Over about 40 ms: two hearings of one patch (its oscillators started
    // at other phases, beating otherwise) read alike.
    const auto span = std::max(1, static_cast<int>(std::lround(0.02 * 48'000.0 / hop)));
    std::vector<float> smoothed(out.size());
    for (int row = 0; row < rows; ++row)
        for (int band = 0; band < bands; ++band) {
            auto sum = 0.0;
            auto count = 0;
            for (int at = std::max(0, row - span); at <= std::min(rows - 1, row + span); ++at) {
                sum += out[static_cast<std::size_t>(at * bands + band)];
                ++count;
            }
            smoothed[static_cast<std::size_t>(row * bands + band)] = static_cast<float>(0.5 * std::log(sum / count + 1.0e-18));
        }
    return smoothed;
}

std::vector<float> Comparison::envelope(const std::vector<float>& signal, const int hop)
{
    // The level every hop, over five hops (about 50 ms).
    const auto rows = std::max(1, static_cast<int>(signal.size()) / hop);
    std::vector<double> power(static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        auto sum = 0.0;
        for (int index = 0; index < hop; ++index) {
            const auto at = static_cast<std::size_t>(row * hop + index);
            const auto value = at < signal.size() ? signal[at] : 0.0f;
            sum += static_cast<double>(value) * value;
        }
        power[static_cast<std::size_t>(row)] = sum / hop;
    }
    std::vector<float> out(static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        auto sum = 0.0;
        auto count = 0;
        for (int at = std::max(0, row - 2); at <= std::min(rows - 1, row + 2); ++at) {
            sum += power[static_cast<std::size_t>(at)];
            ++count;
        }
        out[static_cast<std::size_t>(row)] = static_cast<float>(0.5 * std::log(sum / count + 1.0e-18));
    }
    return out;
}

std::vector<float> Comparison::tonality(const std::vector<float>& signal, const double rate, std::vector<float>& level)
{
    // Four stretches of the note (its attack, its start, its body, its end);
    // in each, the power of every bin of 4096 points averaged over the frames
    // that start there; in each of the 40 bands, the mean of its loudest
    // quarter of bins over the mean of its quietest half (natural log of the
    // amplitudes' ratio): harmonics stand far above their floor, noise does
    // not. A band of fewer than 6 bins says nothing (-1).
    constexpr int size = 4'096;
    constexpr int hop = 1'024;
    constexpr int bands = 40;
    const double edges[] = { 0.0, 0.12, 0.4, 1.0, 1.0e9 };
    constexpr int stretches = 4;
    const auto columns = size / 2 + 1;
    const auto binHz = rate / size;
    std::vector<std::vector<int>> binsOf(static_cast<std::size_t>(bands));
    for (int column = 1; column < columns; ++column) {
        const auto hz = column * binHz;
        if (hz < 40.0 || hz >= 16'000.0)
            continue;
        const auto band = std::clamp(static_cast<int>(std::floor(bands * std::log(hz / 40.0) / std::log(400.0))), 0, bands - 1);
        binsOf[static_cast<std::size_t>(band)].push_back(column);
    }
    std::vector<float> window(static_cast<std::size_t>(size));
    for (int index = 0; index < size; ++index)
        window[static_cast<std::size_t>(index)] = static_cast<float>(0.5 - 0.5 * std::cos(twoPi * index / size));
    std::vector<float> out(static_cast<std::size_t>(stretches * bands), -1.0f);
    level.assign(out.size(), -40.0f);
    std::vector<std::complex<float>> frame(static_cast<std::size_t>(size));
    std::vector<double> power(static_cast<std::size_t>(columns));
    std::vector<double> sorted;
    const auto length = static_cast<int>(signal.size());
    for (int stretch = 0; stretch < stretches; ++stretch) {
        const auto from = static_cast<int>(edges[stretch] * rate);
        const auto to = std::min(length, static_cast<int>(std::min(edges[stretch + 1] * rate, 1.0e9)));
        if (from >= to)
            continue;
        std::fill(power.begin(), power.end(), 0.0);
        auto frames = 0;
        for (auto start = from; start < to; start += hop) {
            for (int index = 0; index < size; ++index) {
                const auto at = static_cast<std::size_t>(start + index);
                frame[static_cast<std::size_t>(index)] = { at < signal.size() ? signal[at] * window[static_cast<std::size_t>(index)] : 0.0f, 0.0f };
            }
            transform(frame);
            for (int column = 1; column < columns; ++column)
                power[static_cast<std::size_t>(column)] += std::norm(frame[static_cast<std::size_t>(column)]);
            ++frames;
        }
        for (int band = 0; band < bands; ++band) {
            const auto& bins = binsOf[static_cast<std::size_t>(band)];
            const auto cell = static_cast<std::size_t>(stretch * bands + band);
            if (bins.empty())
                continue;
            sorted.clear();
            auto sum = 0.0;
            for (const auto column : bins) {
                sorted.push_back(power[static_cast<std::size_t>(column)] / std::max(1, frames));
                sum += sorted.back();
            }
            level[cell] = static_cast<float>(0.5 * std::log(sum / static_cast<double>(bins.size()) + 1.0e-18));
            if (bins.size() < 6)
                continue;
            std::sort(sorted.begin(), sorted.end());
            const auto count = sorted.size();
            auto low = 0.0;
            for (std::size_t index = 0; index < count / 2; ++index)
                low += sorted[index];
            low /= static_cast<double>(count / 2);
            auto high = 0.0;
            const auto quarter = std::max<std::size_t>(1, count / 4);
            for (std::size_t index = count - quarter; index < count; ++index)
                high += sorted[index];
            high /= static_cast<double>(quarter);
            out[cell] = static_cast<float>(std::clamp(0.5 * std::log((high + 1.0e-18) / (low + 1.0e-18)), 0.0, 6.0));
        }
    }
    return out;
}

Comparison::Comparison(const Target& target)
{
    frames_ = static_cast<int>(target.samples.size());
    rate_ = target.rate;
    tonal_ = tonality(target.samples, rate_, tonalLevel_);
    {
        auto top = -1.0e30f;
        for (const auto value : tonalLevel_)
            top = std::max(top, value);
        // A band's tonality is heard within 50 dB of the loudest.
        tonalFloor_ = top - 50.0f * 0.1151293f;
    }
    for (const auto size : { 2048, 512 }) {
        Scale scale;
        scale.size = size;
        scale.hop = size / 4;
        scale.columns = 40;
        scale.target = logSpectrogram(target.samples, size, scale.hop, scale.rows);
        auto top = -1.0e30f;
        for (const auto value : scale.target)
            top = std::max(top, value);
        // What is heard: within 70 dB of the loudest bin (natural log).
        scale.floor = top - 70.0f * 0.1151293f;
        // The ear weighs the loud moments more than a quiet tail: each frame
        // by its amplitude's square root (-20 dB: a third, -40 dB: a tenth).
        scale.weights.assign(static_cast<std::size_t>(scale.rows), 0.0f);
        auto loudest = -1.0e30;
        std::vector<double> loudness(static_cast<std::size_t>(scale.rows));
        for (int row = 0; row < scale.rows; ++row) {
            auto sum = 0.0;
            for (int band = 0; band < scale.columns; ++band)
                sum += std::exp(2.0 * scale.target[static_cast<std::size_t>(row * scale.columns + band)]);
            loudness[static_cast<std::size_t>(row)] = 0.5 * std::log(sum + 1.0e-18);
            loudest = std::max(loudest, loudness[static_cast<std::size_t>(row)]);
        }
        for (int row = 0; row < scale.rows; ++row)
            scale.weights[static_cast<std::size_t>(row)] = static_cast<float>(std::exp(0.5 * (loudness[static_cast<std::size_t>(row)] - loudest)));
        scales_.push_back(std::move(scale));
    }
    envelopeHop_ = std::max(1, static_cast<int>(0.01 * target.rate));
    envelope_ = envelope(target.samples, envelopeHop_);
    {
        auto loudest = -1.0e30f;
        for (const auto value : envelope_)
            loudest = std::max(loudest, value);
        envelopeWeights_.resize(envelope_.size());
        for (std::size_t index = 0; index < envelope_.size(); ++index)
            envelopeWeights_[index] = std::exp(0.5f * (envelope_[index] - loudest));
    }
}

Distance Comparison::distance(const std::vector<float>& candidate) const
{
    Distance result;
    // The gain that brings it closest: the median of the differences over the
    // target's loud bins of the finer-frequency scale.
    std::vector<std::vector<float>> spectra;
    for (const auto& scale : scales_) {
        auto rows = 0;
        spectra.push_back(logSpectrogram(candidate, scale.size, scale.hop, rows));
    }
    {
        const auto& scale = scales_.front();
        std::vector<float> differences;
        differences.reserve(scale.target.size() / 4);
        for (std::size_t index = 0; index < scale.target.size(); ++index)
            if (scale.target[index] > scale.floor)
                differences.push_back(scale.target[index] - spectra.front()[index]);
        if (!differences.empty()) {
            std::nth_element(differences.begin(), differences.begin() + static_cast<std::ptrdiff_t>(differences.size() / 2), differences.end());
            result.gainDb = differences[differences.size() / 2] * 8.685889638;
        }
    }
    const auto offset = static_cast<float>(result.gainDb / 8.685889638);
    auto total = 0.0;
    for (std::size_t which = 0; which < scales_.size(); ++which) {
        const auto& scale = scales_[which];
        const auto& own = spectra[which];
        auto sum = 0.0;
        auto counted = 0.0;
        for (std::size_t index = 0; index < scale.target.size(); ++index) {
            const auto t = scale.target[index];
            const auto c = own[index] + offset;
            if (t <= scale.floor && c <= scale.floor)
                continue;
            const auto weight = static_cast<double>(scale.weights[index / static_cast<std::size_t>(scale.columns)]);
            sum += weight * std::abs(static_cast<double>(std::max(t, scale.floor)) - std::max(c, scale.floor));
            counted += weight;
        }
        total += counted > 0.0 ? sum / counted : 0.0;
    }
    // The level over time (the envelope), as the spectra are weighed.
    const auto own = envelope(candidate, envelopeHop_);
    auto top = -1.0e30f;
    for (const auto value : envelope_)
        top = std::max(top, value);
    const auto floor = top - 60.0f * 0.1151293f;
    auto sum = 0.0;
    auto counted = 0.0;
    for (std::size_t index = 0; index < envelope_.size() && index < own.size(); ++index) {
        const auto t = envelope_[index];
        const auto c = own[index] + offset;
        if (t <= floor && c <= floor)
            continue;
        sum += envelopeWeights_[index] * std::abs(static_cast<double>(std::max(t, floor)) - std::max(c, floor));
        counted += envelopeWeights_[index];
    }
    result.loss = total / static_cast<double>(scales_.size()) + 0.5 * (counted > 0.0 ? sum / counted : 0.0);
    // Harmonics where the note has harmonics, noise where it has noise.
    std::vector<float> levels;
    const auto tonal = tonality(candidate, rate_, levels);
    auto tonalSum = 0.0;
    std::size_t tonalCount = 0;
    for (std::size_t index = 0; index < tonal_.size(); ++index)
        if (tonal_[index] >= 0.0f && tonal[index] >= 0.0f && tonalLevel_[index] > tonalFloor_) {
            tonalSum += std::abs(static_cast<double>(tonal[index]) - tonal_[index]);
            ++tonalCount;
        }
    result.loss += 0.25 * (tonalCount > 0 ? tonalSum / static_cast<double>(tonalCount) : 0.0);
    return result;
}

double Comparison::envelopeLoss(const std::vector<float>& candidate) const
{
    const auto own = envelope(candidate, envelopeHop_);
    auto top = -1.0e30f;
    for (const auto value : envelope_)
        top = std::max(top, value);
    const auto floor = top - 60.0f * 0.1151293f;
    std::vector<float> differences;
    for (std::size_t index = 0; index < envelope_.size() && index < own.size(); ++index)
        if (envelope_[index] > floor)
            differences.push_back(envelope_[index] - own[index]);
    if (differences.empty())
        return 0.0;
    std::nth_element(differences.begin(), differences.begin() + static_cast<std::ptrdiff_t>(differences.size() / 2), differences.end());
    const auto offset = differences[differences.size() / 2];
    auto sum = 0.0;
    std::size_t counted = 0;
    for (std::size_t index = 0; index < envelope_.size() && index < own.size(); ++index) {
        const auto t = envelope_[index];
        const auto c = own[index] + offset;
        if (t <= floor && c <= floor)
            continue;
        sum += std::abs(static_cast<double>(std::max(t, floor)) - std::max(c, floor));
        ++counted;
    }
    return counted > 0 ? sum / static_cast<double>(counted) : 0.0;
}

// ---- The search: CMA-ES ----

Result match(const Target& target, const Options& options)
{
    Result result;
    const auto model = options.model;
    const auto dimensions = dimensionsOf(model);
    const auto n = static_cast<int>(dimensions.size());
    if (n == 0 || target.samples.size() < 2048)
        return result;
    const Comparison comparison(target);
    const auto frames = comparison.frames();
    const auto threads = std::max(1, options.threads > 0 ? options.threads
                                                         : static_cast<int>(std::max(2u, std::thread::hardware_concurrency())) - 1);
    std::mt19937_64 random(options.seed);
    std::normal_distribution<double> normal(0.0, 1.0);

    // Where to start: the model's defaults.
    std::vector<double> start(static_cast<std::size_t>(n));
    const auto defaults = baseValues(model);
    for (int index = 0; index < n; ++index)
        start[static_cast<std::size_t>(index)] = std::clamp(
            unitOf(model, dimensions[static_cast<std::size_t>(index)], defaults[static_cast<std::size_t>(dimensions[static_cast<std::size_t>(index)].index)]),
            0.0, 1.0);

    auto bestLoss = 1.0e30;
    std::vector<double> best = start;
    auto bestGain = 0.0;
    auto spent = 0;
    const auto evaluate = [&](std::vector<std::vector<double>>& points, std::vector<double>& losses, std::vector<double>& gains) {
        losses.assign(points.size(), 1.0e30);
        gains.assign(points.size(), 0.0);
        std::atomic<std::size_t> next { 0 };
        const auto work = [&] {
            for (auto index = next.fetch_add(1); index < points.size(); index = next.fetch_add(1)) {
                std::vector<double> clipped(points[index].size());
                auto outside = 0.0;
                for (std::size_t d = 0; d < clipped.size(); ++d) {
                    clipped[d] = std::clamp(points[index][d], 0.0, 1.0);
                    outside += (clipped[d] - points[index][d]) * (clipped[d] - points[index][d]);
                }
                const auto values = valuesAt(model, dimensions, clipped, target);
                const auto distance = comparison.distance(render(model, values, target, frames));
                // A patch far quieter (or louder) than the note costs a little:
                // its track's fader makes up 12 dB at most.
                losses[index] = distance.loss + 4.0 * outside + 0.02 * std::max(0.0, std::abs(distance.gainDb) - 12.0);
                gains[index] = distance.gainDb;
            }
        };
        std::vector<std::thread> pool;
        for (int worker = 1; worker < threads; ++worker)
            pool.emplace_back(work);
        work();
        for (auto& thread : pool)
            thread.join();
    };

    // A point's settings that step (a filter's type, an interval) each tried
    // at every value, the others held: CMA-ES moves them poorly, a step
    // being another sound. The point and its loss improved in place.
    std::vector<int> stepped;
    for (int d = 0; d < n; ++d) {
        const auto& dimension = dimensions[static_cast<std::size_t>(d)];
        if (!dimension.allowed.empty() || dimension.kind == synth::Kind::choice || dimension.kind == synth::Kind::toggle)
            stepped.push_back(d);
    }
    const auto polish = [&](std::vector<double>& point, double& loss) {
        for (const auto d : stepped) {
            const auto& dimension = dimensions[static_cast<std::size_t>(d)];
            const auto count = !dimension.allowed.empty() ? static_cast<int>(dimension.allowed.size())
                               : dimension.kind == synth::Kind::toggle ? 2
                                                                       : static_cast<int>(dimension.maximum - dimension.minimum) + 1;
            if (count < 2 || count > 64)
                continue;
            std::vector<std::vector<double>> tries(static_cast<std::size_t>(count), point);
            for (int value = 0; value < count; ++value)
                tries[static_cast<std::size_t>(value)][static_cast<std::size_t>(d)] = static_cast<double>(value) / static_cast<double>(count - 1);
            std::vector<double> losses;
            std::vector<double> gains;
            evaluate(tries, losses, gains);
            spent += count;
            for (int value = 0; value < count; ++value)
                if (losses[static_cast<std::size_t>(value)] < loss) {
                    loss = losses[static_cast<std::size_t>(value)];
                    point = tries[static_cast<std::size_t>(value)];
                    if (loss < bestLoss) {
                        bestLoss = loss;
                        best = point;
                        bestGain = gains[static_cast<std::size_t>(value)];
                    }
                }
        }
    };

    // The level's envelope first, alone: the amplifier's settings tried one
    // by one at nine values (three passes), the others at their defaults, by
    // the envelope only. It weighs much in the loss and depends little on
    // the rest; the scouts keep it, scattering the timbre.
    std::vector<int> envelopeDims;
    for (int d = 0; d < n; ++d)
        if (dimensions[static_cast<std::size_t>(d)].key.rfind("amp.", 0) == 0)
            envelopeDims.push_back(d);
    if (!envelopeDims.empty()) {
        auto bestEnvelope = comparison.envelopeLoss(render(model, valuesAt(model, dimensions, start, target), target, frames));
        ++spent;
        for (auto pass = 0; pass < 3; ++pass)
            for (const auto d : envelopeDims) {
                constexpr int count = 9;
                std::vector<std::vector<double>> tries(static_cast<std::size_t>(count), start);
                const auto reach = pass == 0 ? 0.5 : pass == 1 ? 0.15 : 0.05;
                for (int value = 0; value < count; ++value) {
                    auto& at = tries[static_cast<std::size_t>(value)][static_cast<std::size_t>(d)];
                    at = pass == 0 ? static_cast<double>(value) / (count - 1)
                                   : std::clamp(at + reach * (static_cast<double>(value) / (count - 1) * 2.0 - 1.0), 0.0, 1.0);
                }
                std::vector<double> losses(tries.size(), 1.0e30);
                std::atomic<std::size_t> next { 0 };
                const auto work = [&] {
                    for (auto index = next.fetch_add(1); index < tries.size(); index = next.fetch_add(1))
                        losses[index] = comparison.envelopeLoss(render(model, valuesAt(model, dimensions, tries[index], target), target, frames));
                };
                std::vector<std::thread> pool;
                for (int worker = 1; worker < std::min<int>(threads, count); ++worker)
                    pool.emplace_back(work);
                work();
                for (auto& thread : pool)
                    thread.join();
                spent += count;
                for (int value = 0; value < count; ++value)
                    if (losses[static_cast<std::size_t>(value)] < bestEnvelope) {
                        bestEnvelope = losses[static_cast<std::size_t>(value)];
                        start = tries[static_cast<std::size_t>(value)];
                    }
            }
    }

    // Scouts first: a quarter of the candidates scattered over the whole
    // space (one per stratum of every setting, a Latin hypercube), the
    // defaults among them; the search starts from the best of them rather
    // than wherever the defaults happen to lie.
    std::vector<std::vector<double>> starts;
    {
        const auto count = std::max(8, options.evaluations / 4);
        std::vector<std::vector<double>> points(static_cast<std::size_t>(count), std::vector<double>(static_cast<std::size_t>(n)));
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        for (int d = 0; d < n; ++d) {
            std::vector<int> strata(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i)
                strata[static_cast<std::size_t>(i)] = i;
            std::shuffle(strata.begin(), strata.end(), random);
            for (int i = 0; i < count; ++i)
                points[static_cast<std::size_t>(i)][static_cast<std::size_t>(d)]
                    = (static_cast<double>(strata[static_cast<std::size_t>(i)]) + uniform(random)) / static_cast<double>(count);
        }
        // The envelope found kept (a little either side).
        for (auto& point : points)
            for (const auto d : envelopeDims)
                point[static_cast<std::size_t>(d)] = std::clamp(start[static_cast<std::size_t>(d)] + 0.1 * (uniform(random) - 0.5), 0.0, 1.0);
        points.front() = start;
        std::vector<double> losses;
        std::vector<double> gains;
        evaluate(points, losses, gains);
        spent += count;
        for (int i = 0; i < count; ++i)
            if (losses[static_cast<std::size_t>(i)] < bestLoss) {
                bestLoss = losses[static_cast<std::size_t>(i)];
                best = points[static_cast<std::size_t>(i)];
                bestGain = gains[static_cast<std::size_t>(i)];
            }
        // The best three that lie apart (other sounds, other basins): a
        // descent from each, as one may end in a pit the others avoid.
        std::vector<int> order(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
            order[static_cast<std::size_t>(i)] = i;
        std::sort(order.begin(), order.end(), [&losses](const int a, const int b) { return losses[static_cast<std::size_t>(a)] < losses[static_cast<std::size_t>(b)]; });
        for (const auto index : order) {
            const auto& point = points[static_cast<std::size_t>(index)];
            const auto apart = std::all_of(starts.begin(), starts.end(), [&point, n](const std::vector<double>& other) {
                auto sum = 0.0;
                for (int d = 0; d < n; ++d)
                    sum += (point[static_cast<std::size_t>(d)] - other[static_cast<std::size_t>(d)]) * (point[static_cast<std::size_t>(d)] - other[static_cast<std::size_t>(d)]);
                return std::sqrt(sum / n) > 0.2;
            });
            if (apart) {
                starts.push_back(point);
                auto loss = losses[static_cast<std::size_t>(index)];
                polish(starts.back(), loss);
            }
            if (starts.size() == 3)
                break;
        }
        if (options.cancel != nullptr && options.cancel->load())
            result.cancelled = true;
        else if (options.progress && !options.progress(static_cast<double>(spent) / std::max(1, options.evaluations), bestLoss))
            result.cancelled = true;
    }

    // Then CMA-ES: a descent from each start (a fifth of the candidates
    // each), then one closer around the best found (the rest).
    const auto runs = static_cast<int>(starts.size()) + 1;
    for (auto run = 0; run < runs && spent < options.evaluations && !result.cancelled; ++run) {
        const auto last = run == runs - 1;
        const auto lambda = 4 + static_cast<int>(3.0 * std::log(static_cast<double>(n))) + (last ? 0 : 4);
        const auto mu = lambda / 2;
        std::vector<double> weights(static_cast<std::size_t>(mu));
        auto weightSum = 0.0;
        for (int i = 0; i < mu; ++i) {
            weights[static_cast<std::size_t>(i)] = std::log(mu + 0.5) - std::log(i + 1.0);
            weightSum += weights[static_cast<std::size_t>(i)];
        }
        auto weightSquares = 0.0;
        for (auto& weight : weights) {
            weight /= weightSum;
            weightSquares += weight * weight;
        }
        const auto mueff = 1.0 / weightSquares;
        const auto dn = static_cast<double>(n);
        const auto cc = (4.0 + mueff / dn) / (dn + 4.0 + 2.0 * mueff / dn);
        const auto cs = (mueff + 2.0) / (dn + mueff + 5.0);
        const auto c1 = 2.0 / ((dn + 1.3) * (dn + 1.3) + mueff);
        const auto cmu = std::min(1.0 - c1, 2.0 * (mueff - 2.0 + 1.0 / mueff) / ((dn + 2.0) * (dn + 2.0) + mueff));
        const auto damps = 1.0 + 2.0 * std::max(0.0, std::sqrt((mueff - 1.0) / (dn + 1.0)) - 1.0) + cs;
        const auto chiN = std::sqrt(dn) * (1.0 - 1.0 / (4.0 * dn) + 1.0 / (21.0 * dn * dn));

        auto mean = last ? best : starts[static_cast<std::size_t>(run)];
        auto sigma = last ? 0.08 : 0.2;
        std::vector<double> covariance(static_cast<std::size_t>(n * n), 0.0);
        std::vector<double> basis(static_cast<std::size_t>(n * n), 0.0);
        std::vector<double> scales(static_cast<std::size_t>(n), 1.0);
        for (int i = 0; i < n; ++i) {
            covariance[static_cast<std::size_t>(i * n + i)] = 1.0;
            basis[static_cast<std::size_t>(i * n + i)] = 1.0;
        }
        std::vector<double> pc(static_cast<std::size_t>(n), 0.0);
        std::vector<double> ps(static_cast<std::size_t>(n), 0.0);
        const auto budget = last ? options.evaluations
                                 : std::min(options.evaluations, spent + options.evaluations * 3 / (5 * std::max(1, runs - 1)));
        for (auto generation = 0; spent + lambda <= budget; ++generation) {
            if (options.cancel != nullptr && options.cancel->load()) {
                result.cancelled = true;
                break;
            }
            std::vector<std::vector<double>> points(static_cast<std::size_t>(lambda), std::vector<double>(static_cast<std::size_t>(n)));
            std::vector<std::vector<double>> steps(points.size(), std::vector<double>(static_cast<std::size_t>(n)));
            for (auto& point : points) {
                const auto k = static_cast<std::size_t>(&point - points.data());
                std::vector<double> z(static_cast<std::size_t>(n));
                for (auto& value : z)
                    value = normal(random);
                for (int row = 0; row < n; ++row) {
                    auto y = 0.0;
                    for (int column = 0; column < n; ++column)
                        y += basis[static_cast<std::size_t>(row * n + column)] * scales[static_cast<std::size_t>(column)] * z[static_cast<std::size_t>(column)];
                    steps[k][static_cast<std::size_t>(row)] = y;
                    point[static_cast<std::size_t>(row)] = mean[static_cast<std::size_t>(row)] + sigma * y;
                }
            }
            std::vector<double> losses;
            std::vector<double> gains;
            evaluate(points, losses, gains);
            spent += lambda;
            std::vector<int> order(static_cast<std::size_t>(lambda));
            for (int i = 0; i < lambda; ++i)
                order[static_cast<std::size_t>(i)] = i;
            std::sort(order.begin(), order.end(), [&losses](const int a, const int b) { return losses[static_cast<std::size_t>(a)] < losses[static_cast<std::size_t>(b)]; });
            if (losses[static_cast<std::size_t>(order.front())] < bestLoss) {
                bestLoss = losses[static_cast<std::size_t>(order.front())];
                best = points[static_cast<std::size_t>(order.front())];
                for (auto& value : best)
                    value = std::clamp(value, 0.0, 1.0);
                bestGain = gains[static_cast<std::size_t>(order.front())];
            }
            // The new mean, and the paths.
            std::vector<double> step(static_cast<std::size_t>(n), 0.0);
            for (int i = 0; i < mu; ++i)
                for (int d = 0; d < n; ++d)
                    step[static_cast<std::size_t>(d)] += weights[static_cast<std::size_t>(i)] * steps[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])][static_cast<std::size_t>(d)];
            for (int d = 0; d < n; ++d)
                mean[static_cast<std::size_t>(d)] += sigma * step[static_cast<std::size_t>(d)];
            // C^-1/2 step = B D^-1 B^T step.
            std::vector<double> whitened(static_cast<std::size_t>(n), 0.0);
            {
                std::vector<double> projected(static_cast<std::size_t>(n), 0.0);
                for (int column = 0; column < n; ++column) {
                    auto sum = 0.0;
                    for (int row = 0; row < n; ++row)
                        sum += basis[static_cast<std::size_t>(row * n + column)] * step[static_cast<std::size_t>(row)];
                    projected[static_cast<std::size_t>(column)] = sum / scales[static_cast<std::size_t>(column)];
                }
                for (int row = 0; row < n; ++row) {
                    auto sum = 0.0;
                    for (int column = 0; column < n; ++column)
                        sum += basis[static_cast<std::size_t>(row * n + column)] * projected[static_cast<std::size_t>(column)];
                    whitened[static_cast<std::size_t>(row)] = sum;
                }
            }
            auto psNorm = 0.0;
            for (int d = 0; d < n; ++d) {
                ps[static_cast<std::size_t>(d)] = (1.0 - cs) * ps[static_cast<std::size_t>(d)] + std::sqrt(cs * (2.0 - cs) * mueff) * whitened[static_cast<std::size_t>(d)];
                psNorm += ps[static_cast<std::size_t>(d)] * ps[static_cast<std::size_t>(d)];
            }
            psNorm = std::sqrt(psNorm);
            const auto hsig = psNorm / std::sqrt(1.0 - std::pow(1.0 - cs, 2.0 * (generation + 1))) / chiN < 1.4 + 2.0 / (dn + 1.0) ? 1.0 : 0.0;
            for (int d = 0; d < n; ++d)
                pc[static_cast<std::size_t>(d)] = (1.0 - cc) * pc[static_cast<std::size_t>(d)] + hsig * std::sqrt(cc * (2.0 - cc) * mueff) * step[static_cast<std::size_t>(d)];
            for (int row = 0; row < n; ++row)
                for (int column = 0; column < n; ++column) {
                    auto rankMu = 0.0;
                    for (int i = 0; i < mu; ++i) {
                        const auto& y = steps[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])];
                        rankMu += weights[static_cast<std::size_t>(i)] * y[static_cast<std::size_t>(row)] * y[static_cast<std::size_t>(column)];
                    }
                    auto& cell = covariance[static_cast<std::size_t>(row * n + column)];
                    cell = (1.0 - c1 - cmu) * cell
                           + c1 * (pc[static_cast<std::size_t>(row)] * pc[static_cast<std::size_t>(column)] + (1.0 - hsig) * cc * (2.0 - cc) * cell)
                           + cmu * rankMu;
                }
            sigma = std::clamp(sigma * std::exp((cs / damps) * (psNorm / chiN - 1.0)), 1.0e-4, 1.0);
            std::vector<double> eigenvalues;
            eigen(covariance, n, basis, eigenvalues);
            for (int d = 0; d < n; ++d)
                scales[static_cast<std::size_t>(d)] = std::sqrt(eigenvalues[static_cast<std::size_t>(d)]);
            if (options.progress && !options.progress(static_cast<double>(spent) / std::max(1, options.evaluations), bestLoss)) {
                result.cancelled = true;
                break;
            }
        }
        if (result.cancelled)
            break;
    }
    if (!result.cancelled) {
        auto point = best;
        auto loss = bestLoss;
        polish(point, loss);
    }
    result.values = valuesAt(model, dimensions, best, target);
    for (const auto& dimension : dimensions)
        result.settings.emplace_back(dimension.key, result.values[static_cast<std::size_t>(dimension.index)]);
    // The settings it keeps fixed that a track's defaults would not (the drift, the tuning).
    if (const auto drift = synth::parameterIndex(model, "drift"); drift >= 0)
        result.settings.emplace_back("drift", 0.0f);
    for (const auto* key : { "osc1.fine", "op1.fine", "op2.fine", "op3.fine", "op4.fine" })
        if (const auto index = synth::parameterIndex(model, key); index >= 0
            && std::none_of(result.settings.begin(), result.settings.end(), [key](const auto& setting) { return setting.first == key; }))
            result.settings.emplace_back(key, result.values[static_cast<std::size_t>(index)]);
    result.loss = bestLoss;
    result.gainDb = bestGain;
    result.evaluations = spent;
    return result;
}

} // namespace cydaw::engine::synth_match
