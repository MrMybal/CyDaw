#include <cydaw/engine/SoundModel.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace cydaw::engine {

namespace {

constexpr double golden = 0.6180339887498949;
constexpr double twoPi = 2.0 * std::numbers::pi;
constexpr double ln10Over20 = 0.11512925464970229;

double midiToHz(const double note) noexcept
{
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

// The noise frame: about 21 ms, a power of two (1024 at 44.1 and 48 kHz).
int noiseFftSize(const double rate) noexcept
{
    return 1 << static_cast<int>(std::lround(std::log2(0.0213 * rate)));
}

float curveAt(const std::vector<float>& curve, const SoundModelZone::Span& span) noexcept
{
    if (curve.empty() || span.upper >= curve.size())
        return 0.0f;
    return curve[span.lower] + (curve[span.upper] - curve[span.lower]) * span.weight;
}

void transform(std::complex<float>* data, const int size, const std::complex<float>* twiddles,
               const std::uint32_t* reverse, const bool inverse) noexcept
{
    for (int index = 0; index < size; ++index)
        if (static_cast<std::uint32_t>(index) < reverse[index])
            std::swap(data[index], data[reverse[index]]);
    for (int length = 2; length <= size; length <<= 1) {
        const auto half = length / 2;
        const auto stride = size / length;
        for (int start = 0; start < size; start += length)
            for (int k = 0; k < half; ++k) {
                auto w = twiddles[k * stride];
                if (inverse)
                    w = std::conj(w);
                const auto t = w * data[start + k + half];
                data[start + k + half] = data[start + k] - t;
                data[start + k] += t;
            }
    }
    if (inverse) {
        const auto scale = 1.0f / static_cast<float>(size);
        for (int index = 0; index < size; ++index)
            data[index] *= scale;
    }
}

} // namespace

SoundModelZoneChoice chooseSoundModelZones(const SoundModel& model, const double midiNote, const double velocity) noexcept
{
    SoundModelZoneChoice choice;
    // The recorded pitches either side of the note.
    auto below = -1;
    auto above = -1;
    for (const auto& zone : model.zones) {
        if (zone.frames <= 0)
            continue;
        if (zone.rootNote <= midiNote && (below < 0 || zone.rootNote > below))
            below = zone.rootNote;
        if (zone.rootNote >= midiNote && (above < 0 || zone.rootNote < above))
            above = zone.rootNote;
    }
    if (below < 0 && above < 0)
        return choice;
    std::array<std::pair<int, double>, 2> pitches {};
    auto pitchCount = 0;
    if (below >= 0 && above >= 0 && below != above) {
        pitches[0] = { below, (above - midiNote) / static_cast<double>(above - below) };
        pitches[1] = { above, (midiNote - below) / static_cast<double>(above - below) };
        pitchCount = 2;
    } else {
        pitches[0] = { below >= 0 ? below : above, 1.0 };
        pitchCount = 1;
    }
    const auto add = [&choice](const int zone, const double weight) {
        if (weight > 1.0e-6 && choice.count < maximumSoundModelLanes) {
            choice.zones[static_cast<std::size_t>(choice.count)] = zone;
            choice.weights[static_cast<std::size_t>(choice.count)] = weight;
            ++choice.count;
        }
    };
    for (auto index = 0; index < pitchCount; ++index) {
        const auto [root, pitchWeight] = pitches[static_cast<std::size_t>(index)];
        // At this pitch, the velocity layers either side of the velocity.
        auto under = -1;
        auto over = -1;
        for (std::size_t zone = 0; zone < model.zones.size(); ++zone) {
            const auto& candidate = model.zones[zone];
            if (candidate.frames <= 0 || candidate.rootNote != root)
                continue;
            if (candidate.velocity <= velocity
                && (under < 0 || candidate.velocity > model.zones[static_cast<std::size_t>(under)].velocity))
                under = static_cast<int>(zone);
            if (candidate.velocity >= velocity
                && (over < 0 || candidate.velocity < model.zones[static_cast<std::size_t>(over)].velocity))
                over = static_cast<int>(zone);
        }
        if (under >= 0 && over >= 0 && under != over
            && model.zones[static_cast<std::size_t>(over)].velocity > model.zones[static_cast<std::size_t>(under)].velocity) {
            const auto v1 = static_cast<double>(model.zones[static_cast<std::size_t>(under)].velocity);
            const auto v2 = static_cast<double>(model.zones[static_cast<std::size_t>(over)].velocity);
            const auto share = (velocity - v1) / (v2 - v1);
            add(under, pitchWeight * (1.0 - share));
            add(over, pitchWeight * share);
        } else {
            add(under >= 0 ? under : over, pitchWeight);
        }
    }
    return choice;
}

void SoundModelVoice::prepare(const double sampleRate)
{
    const auto size = std::clamp(noiseFftSize(std::max(8'000.0, sampleRate)), 256, 8'192);
    fftSize_ = size;
    const auto count = static_cast<std::size_t>(size);
    window_.resize(count);
    for (int index = 0; index < size; ++index)
        window_[static_cast<std::size_t>(index)] = static_cast<float>(
            std::sqrt(0.5 - 0.5 * std::cos(twoPi * index / size)));
    twiddles_.resize(count / 2);
    for (int index = 0; index < size / 2; ++index)
        twiddles_[static_cast<std::size_t>(index)] = std::polar(1.0f, static_cast<float>(-twoPi * index / size));
    bitReverse_.resize(count);
    auto bits = 0;
    while ((1 << bits) < size)
        ++bits;
    for (int index = 0; index < size; ++index) {
        std::uint32_t reversed {};
        for (int bit = 0; bit < bits; ++bit)
            if ((index >> bit) & 1)
                reversed |= 1u << (bits - 1 - bit);
        bitReverse_[static_cast<std::size_t>(index)] = reversed;
    }
    spectrum_.assign(count, {});
    spectrumOther_.assign(count, {});
    spectrumRight_.assign(count, {});
    stream_.assign(count, 0.0f);
    streamOther_.assign(count, 0.0f);
    overlap_.assign(count, 0.0f);
    overlapRight_.assign(count, 0.0f);
    binPsd_.assign(count / 2 + 1, 0.0f);
    binPsdRight_.assign(count / 2 + 1, 0.0f);
    binCoherence_.assign(count / 2 + 1, 0.0f);
    binWeight_.assign(count / 2 + 1, 0.0f);
    reset();
}

void SoundModelVoice::reset() noexcept
{
    active_ = false;
    releaseRequested_ = false;
    lanes_ = {};
    laneCount_ = 0;
    headLane_ = -1;
    headPosition_ = 0.0;
    headStep_ = 0.0;
    headSnapped_ = false;
    blockRead_ = controlBlock;
    blockLeft_.fill(0.0f);
    blockRight_.fill(0.0f);
    std::fill(stream_.begin(), stream_.end(), 0.0f);
    std::fill(streamOther_.begin(), streamOther_.end(), 0.0f);
    std::fill(overlap_.begin(), overlap_.end(), 0.0f);
    std::fill(overlapRight_.begin(), overlapRight_.end(), 0.0f);
    noiseRead_ = fftSize_ / 2;
    partialCount_ = 0;
    samplesPlayed_ = 0;
}

float SoundModelVoice::randomSample(std::uint32_t& state) noexcept
{
    // Uniform in -sqrt(3)..sqrt(3): unit variance, as the reference player's.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (static_cast<float>(state >> 8) / 16'777'215.0f * 2.0f - 1.0f) * 1.7320508f;
}

double SoundModelVoice::laneLevel(const SoundModelZone& zone, const Lane& lane, const SoundModelValues& values) const noexcept
{
    const auto dv = velocity_ - static_cast<double>(zone.velocity);
    return lane.weight * static_cast<double>(zone.gain)
           * std::pow(10.0, static_cast<double>(soundModelValue(values, SoundModelSetting::velocityDb)) * dv / 20.0);
}

double SoundModelVoice::modelShare(const SoundModel& model, const double position) const noexcept
{
    if (headLane_ < 0)
        return 1.0;
    const auto& zone = model.zones[static_cast<std::size_t>(lanes_[static_cast<std::size_t>(headLane_)].zone)];
    const auto fade = static_cast<double>(std::max(1, zone.headFade));
    const auto fadeStart = static_cast<double>(zone.headLength()) - fade;
    return std::clamp((position - fadeStart) / fade, 0.0, 1.0);
}

double SoundModelVoice::lanePositionAhead(const SoundModel& model, const SoundModelZone& zone, const Lane& lane,
                                          const double ahead, const double speed, const double rate) const noexcept
{
    const auto last = static_cast<double>(zone.frames - 1);
    if (headLane_ >= 0 && !lane.released) {
        const auto& headZone = model.zones[static_cast<std::size_t>(lanes_[static_cast<std::size_t>(headLane_)].zone)];
        const auto head = headPosition_ + headStep_ * ahead;
        if (head < static_cast<double>(headZone.headLength()))
            return std::clamp(head / static_cast<double>(headZone.headRate) / static_cast<double>(zone.frameSeconds), 0.0, last);
    }
    const auto step = speed / (static_cast<double>(zone.frameSeconds) * rate);
    return std::clamp(lane.position + step * lane.direction * ahead, 0.0, last);
}

void SoundModelVoice::start(const SoundModel& model, const SoundModelValues& values, const double midiNote,
                            const double velocity, const double sampleRate) noexcept
{
    reset();
    pitchRatio_ = 1.0;
    const auto choice = chooseSoundModelZones(model, midiNote, velocity);
    if (choice.count == 0 || fftSize_ <= 0)
        return;
    const auto rate = std::max(8'000.0, sampleRate);
    active_ = true;
    note_ = midiNote;
    velocity_ = std::clamp(velocity, 0.0, 1.0);
    frequency_ = midiToHz(midiNote);
    rate_ = rate;
    laneCount_ = choice.count;
    auto partials = 0;
    auto heaviest = 0;
    for (int index = 0; index < laneCount_; ++index) {
        auto& lane = lanes_[static_cast<std::size_t>(index)];
        lane = {};
        lane.zone = choice.zones[static_cast<std::size_t>(index)];
        lane.weight = choice.weights[static_cast<std::size_t>(index)];
        partials = std::max(partials, model.zones[static_cast<std::size_t>(lane.zone)].harmonicCount);
        if (lane.weight > lanes_[static_cast<std::size_t>(heaviest)].weight)
            heaviest = index;
    }
    partialCount_ = std::clamp(partials, 0, maximumPartials);
    // The heaviest zone's head, resampled by the pitch as the transient is.
    const auto& headZone = model.zones[static_cast<std::size_t>(lanes_[static_cast<std::size_t>(heaviest)].zone)];
    if (headZone.hasHead()) {
        headLane_ = heaviest;
        const auto ratio = headZone.f0 > 0.0f ? frequency_ / static_cast<double>(headZone.f0) : 1.0;
        headStep_ = static_cast<double>(headZone.headRate) / rate
                    * std::pow(ratio, static_cast<double>(soundModelValue(values, SoundModelSetting::attackPitch)));
    }
    for (int k = 0; k < partialCount_; ++k) {
        const auto phase = twoPi * std::fmod((k + 1) * golden, 1.0);
        cos_[static_cast<std::size_t>(k)] = std::cos(phase);
        sin_[static_cast<std::size_t>(k)] = std::sin(phase);
    }
    std::array<double, maximumPartials> increments {};
    partialTargets(model, values, rate, modelShare(model, 0.0), amplitudeLeft_.data(), amplitudeRight_.data(),
                   increments.data());
    // The noise: the frame centred on the note-on (its second half) and the
    // next, so the first hop is whole.
    for (auto& sample : stream_)
        sample = randomSample(noiseState_);
    for (auto& sample : streamOther_)
        sample = randomSample(otherState_);
    std::fill(overlap_.begin(), overlap_.end(), 0.0f);
    std::fill(overlapRight_.begin(), overlapRight_.end(), 0.0f);
    const auto hop = fftSize_ / 2;
    shapeNoiseHop(model, values, 0.0, rate);
    std::copy(overlap_.begin() + hop, overlap_.end(), overlap_.begin());
    std::fill(overlap_.begin() + hop, overlap_.end(), 0.0f);
    std::copy(overlapRight_.begin() + hop, overlapRight_.end(), overlapRight_.begin());
    std::fill(overlapRight_.begin() + hop, overlapRight_.end(), 0.0f);
    std::copy(stream_.begin() + hop, stream_.end(), stream_.begin());
    std::copy(streamOther_.begin() + hop, streamOther_.end(), streamOther_.begin());
    for (auto index = static_cast<std::size_t>(hop); index < stream_.size(); ++index) {
        stream_[index] = randomSample(noiseState_);
        streamOther_[index] = randomSample(otherState_);
    }
    shapeNoiseHop(model, values, static_cast<double>(hop), rate);
    noiseRead_ = 0;
}

void SoundModelVoice::release() noexcept
{
    if (active_)
        releaseRequested_ = true;
}

void SoundModelVoice::nextFrame(const SoundModel* model, const SoundModelValues& values, const double sampleRate,
                                double& left, double& right) noexcept
{
    if (blockRead_ >= controlBlock) {
        if (model != nullptr && active_) {
            renderBlock(*model, values, std::max(8'000.0, sampleRate));
        } else {
            blockLeft_.fill(0.0f);
            blockRight_.fill(0.0f);
            active_ = false;
        }
        blockRead_ = 0;
    }
    left = static_cast<double>(blockLeft_[static_cast<std::size_t>(blockRead_)]);
    right = static_cast<double>(blockRight_[static_cast<std::size_t>(blockRead_)]);
    ++blockRead_;
}

double SoundModelVoice::nextSample(const SoundModel* model, const SoundModelValues& values, const double sampleRate) noexcept
{
    auto left = 0.0;
    auto right = 0.0;
    nextFrame(model, values, sampleRate, left, right);
    return 0.5 * (left + right);
}

void SoundModelVoice::partialTargets(const SoundModel& model, const SoundModelValues& values, const double rate,
                                     const double modelGain, float* left, float* right, double* increments) noexcept
{
    const auto formant = static_cast<double>(std::clamp(soundModelValue(values, SoundModelSetting::formant), 0.0f, 1.0f));
    const auto pitchCurve = static_cast<double>(soundModelValue(values, SoundModelSetting::pitchCurve));
    const auto inharmonicityScale = static_cast<double>(soundModelValue(values, SoundModelSetting::inharmonicity));
    std::array<double, maximumPartials> sumLeft {};
    std::array<double, maximumPartials> sumRight {};
    std::array<double, maximumPartials> rotationRe {};
    std::array<double, maximumPartials> rotationIm {};
    auto cents = 0.0;
    auto stretch = 0.0;
    for (int laneIndex = 0; laneIndex < laneCount_; ++laneIndex) {
        const auto& lane = lanes_[static_cast<std::size_t>(laneIndex)];
        const auto& zone = model.zones[static_cast<std::size_t>(lane.zone)];
        const auto span = zone.rowAt(lane.position);
        cents += lane.weight * static_cast<double>(curveAt(zone.pitchCents, span)) * pitchCurve;
        stretch += lane.weight * static_cast<double>(zone.inharmonicity) * inharmonicityScale;
        const auto ratio = zone.f0 > 0.0f ? frequency_ / static_cast<double>(zone.f0) : 1.0;
        const auto dv = velocity_ - static_cast<double>(zone.velocity);
        const auto tilt = static_cast<double>(soundModelValue(values, SoundModelSetting::brightness))
                          + static_cast<double>(soundModelValue(values, SoundModelSetting::velocityBrightness)) * dv;
        const auto level = laneLevel(zone, lane, values) * lane.fade * modelGain;
        const auto count = zone.harmonicCount;
        const auto stereo = zone.stereo() && !zone.partialLeft.empty();
        const auto stereoSpan = stereo ? zone.stereoRowAt(lane.position) : SoundModelZone::Span {};
        const auto amplitudeOf = [&](const int number) {
            return number >= 1 && number <= count && !zone.harmonics.empty()
                       ? static_cast<double>(SoundModelZone::cell(zone.harmonics, count, span, number - 1))
                       : 0.0;
        };
        for (int index = 0; index < partialCount_; ++index) {
            const auto k = static_cast<double>(index + 1);
            auto amplitude = amplitudeOf(index + 1);
            if (formant > 0.0) {
                // The same frequency in the recorded spectrum: harmonic k * ratio.
                const auto source = k * ratio;
                const auto lower = static_cast<int>(std::floor(source));
                const auto weight = source - static_cast<double>(lower);
                const auto inPlace = amplitudeOf(lower) * (1.0 - weight) + amplitudeOf(lower + 1) * weight;
                amplitude = amplitude * (1.0 - formant) + inPlace * formant;
            }
            amplitude *= std::exp(tilt * std::log2(k) * ln10Over20) * level;
            if (stereo && index < count) {
                sumLeft[static_cast<std::size_t>(index)]
                    += amplitude * static_cast<double>(SoundModelZone::cell(zone.partialLeft, count, stereoSpan, index));
                sumRight[static_cast<std::size_t>(index)]
                    += amplitude * static_cast<double>(SoundModelZone::cell(zone.partialRight, count, stereoSpan, index));
                rotationRe[static_cast<std::size_t>(index)] += lane.weight * static_cast<double>(zone.phaseCos[static_cast<std::size_t>(index)]);
                rotationIm[static_cast<std::size_t>(index)] += lane.weight * static_cast<double>(zone.phaseSin[static_cast<std::size_t>(index)]);
            } else {
                sumLeft[static_cast<std::size_t>(index)] += amplitude;
                sumRight[static_cast<std::size_t>(index)] += amplitude;
                rotationRe[static_cast<std::size_t>(index)] += lane.weight;
            }
        }
    }
    const auto base = frequency_ * pitchRatio_ * std::pow(2.0, cents / 1'200.0);
    for (int index = 0; index < partialCount_; ++index) {
        const auto k = static_cast<double>(index + 1);
        const auto frequency = base * k * std::sqrt(1.0 + stretch * k * k);
        const auto audible = std::clamp((0.45 * rate - frequency) / (0.05 * rate), 0.0, 1.0);
        left[index] = static_cast<float>(sumLeft[static_cast<std::size_t>(index)] * audible);
        right[index] = static_cast<float>(sumRight[static_cast<std::size_t>(index)] * audible);
        increments[index] = twoPi * frequency / rate;
        const auto re = rotationRe[static_cast<std::size_t>(index)];
        const auto im = rotationIm[static_cast<std::size_t>(index)];
        const auto magnitude = std::hypot(re, im);
        rotationCos_[static_cast<std::size_t>(index)] = magnitude > 1.0e-9 ? re / magnitude : 1.0;
        rotationSin_[static_cast<std::size_t>(index)] = magnitude > 1.0e-9 ? im / magnitude : 0.0;
    }
}

void SoundModelVoice::snapToHead(const SoundModel& model, const SoundModelValues& values, const double position) noexcept
{
    (void) values;
    const auto& zone = model.zones[static_cast<std::size_t>(lanes_[static_cast<std::size_t>(headLane_)].zone)];
    const auto fadeStart = static_cast<double>(zone.headLength()) - static_cast<double>(std::max(1, zone.headFade));
    // The recorded partials' phases at the head's position: from where it
    // gives way, at their recorded frequencies over the head's time.
    const auto since = (position - fadeStart) / static_cast<double>(zone.headRate);
    const auto frame = position / static_cast<double>(zone.headRate) / static_cast<double>(zone.frameSeconds);
    const auto cents = static_cast<double>(curveAt(zone.pitchCents, zone.rowAt(frame)));
    const auto count = std::min(partialCount_, static_cast<int>(zone.headPhase.size()));
    const auto inharmonicity = static_cast<double>(zone.inharmonicity);
    for (int index = 0; index < count; ++index) {
        const auto k = static_cast<double>(index + 1);
        const auto frequency = k * static_cast<double>(zone.f0) * std::sqrt(1.0 + inharmonicity * k * k)
                               * std::pow(2.0, cents / 1'200.0);
        const auto phase = static_cast<double>(zone.headPhase[static_cast<std::size_t>(index)]) + twoPi * frequency * since;
        cos_[static_cast<std::size_t>(index)] = std::cos(phase);
        sin_[static_cast<std::size_t>(index)] = std::sin(phase);
    }
}

void SoundModelVoice::shapeNoiseHop(const SoundModel& model, const SoundModelValues& values, const double ahead,
                                    const double rate) noexcept
{
    // One frame of each stream, shaped by the lanes' band PSDs where they
    // will be `ahead` samples from now, added into the overlaps. Its bins
    // scale unit-variance noise to that PSD; the right is the left's stream
    // by the coherence and its own by the rest.
    const auto size = fftSize_;
    std::fill(binPsd_.begin(), binPsd_.end(), 0.0f);
    std::fill(binPsdRight_.begin(), binPsdRight_.end(), 0.0f);
    std::fill(binCoherence_.begin(), binCoherence_.end(), 0.0f);
    std::fill(binWeight_.begin(), binWeight_.end(), 0.0f);
    const auto speed = std::clamp(static_cast<double>(soundModelValue(values, SoundModelSetting::speed)), 0.25, 4.0);
    const auto share = headLane_ >= 0 ? modelShare(model, headPosition_ + headStep_ * ahead) : 1.0;
    auto stereo = false;
    for (int laneIndex = 0; laneIndex < laneCount_; ++laneIndex) {
        const auto& lane = lanes_[static_cast<std::size_t>(laneIndex)];
        const auto& zone = model.zones[static_cast<std::size_t>(lane.zone)];
        const auto bands = zone.bandCount;
        if (bands <= 0 || static_cast<int>(zone.noiseEdges.size()) != bands + 1 || zone.frames <= 0 || zone.noise.empty())
            continue;
        const auto position = lanePositionAhead(model, zone, lane, ahead, speed, rate);
        const auto span = zone.rowAt(position);
        const auto zoneStereo = zone.stereo() && !zone.noiseLeft.empty();
        const auto stereoSpan = zoneStereo ? zone.stereoRowAt(position) : SoundModelZone::Span {};
        stereo = stereo || zoneStereo;
        const auto fade = lane.fade * std::pow(lane.fadeRate, ahead);
        const auto level = laneLevel(zone, lane, values) * fade * share;
        const auto power = level * level;
        const auto ratio = zone.f0 > 0.0f ? frequency_ / static_cast<double>(zone.f0) : 1.0;
        const auto shift = std::pow(ratio, static_cast<double>(soundModelValue(values, SoundModelSetting::noisePitch)));
        auto band = 0;
        auto bandPsd = 0.0;
        auto bandLeft = 1.0;
        auto bandRight = 1.0;
        auto bandCoherence = 1.0;
        auto bandRead = -1;
        for (int bin = 0; bin <= size / 2; ++bin) {
            const auto frequency = static_cast<double>(bin) * rate / size;
            while (band < bands && frequency >= static_cast<double>(zone.noiseEdges[static_cast<std::size_t>(band + 1)]) * shift)
                ++band;
            if (band >= bands)
                break;
            if (frequency < static_cast<double>(zone.noiseEdges[static_cast<std::size_t>(band)]) * shift)
                continue;
            if (band != bandRead) {
                bandRead = band;
                bandPsd = power * static_cast<double>(SoundModelZone::cell(zone.noise, bands, span, band));
                if (zoneStereo) {
                    bandLeft = static_cast<double>(SoundModelZone::cell(zone.noiseLeft, bands, stereoSpan, band));
                    bandRight = static_cast<double>(SoundModelZone::cell(zone.noiseRight, bands, stereoSpan, band));
                    bandCoherence = static_cast<double>(zone.noiseCoherence[static_cast<std::size_t>(band)]);
                }
            }
            binPsd_[static_cast<std::size_t>(bin)] += static_cast<float>(bandPsd * bandLeft);
            binPsdRight_[static_cast<std::size_t>(bin)] += static_cast<float>(bandPsd * bandRight);
            binCoherence_[static_cast<std::size_t>(bin)] += static_cast<float>(lane.weight * bandCoherence);
            binWeight_[static_cast<std::size_t>(bin)] += static_cast<float>(lane.weight);
        }
    }
    for (int index = 0; index < size; ++index)
        spectrum_[static_cast<std::size_t>(index)] = { stream_[static_cast<std::size_t>(index)] * window_[static_cast<std::size_t>(index)], 0.0f };
    transform(spectrum_.data(), size, twiddles_.data(), bitReverse_.data(), false);
    if (stereo) {
        for (int index = 0; index < size; ++index)
            spectrumOther_[static_cast<std::size_t>(index)] = { streamOther_[static_cast<std::size_t>(index)] * window_[static_cast<std::size_t>(index)], 0.0f };
        transform(spectrumOther_.data(), size, twiddles_.data(), bitReverse_.data(), false);
        for (int bin = 0; bin <= size / 2; ++bin) {
            const auto at = static_cast<std::size_t>(bin);
            const auto gain = static_cast<float>(std::sqrt(std::max(0.0, static_cast<double>(binPsdRight_[at]) * rate * 0.5)));
            const auto weight = binWeight_[at];
            const auto coherence = weight > 0.0f ? std::clamp(binCoherence_[at] / weight, 0.0f, 1.0f) : 1.0f;
            const auto own = std::sqrt(std::max(0.0f, 1.0f - coherence * coherence));
            spectrumRight_[at] = gain * (coherence * spectrum_[at] + own * spectrumOther_[at]);
            if (bin > 0 && bin < size / 2) {
                const auto mirror = static_cast<std::size_t>(size - bin);
                spectrumRight_[mirror] = gain * (coherence * spectrum_[mirror] + own * spectrumOther_[mirror]);
            }
        }
    }
    for (int bin = 0; bin <= size / 2; ++bin) {
        const auto gain = static_cast<float>(std::sqrt(std::max(0.0, static_cast<double>(binPsd_[static_cast<std::size_t>(bin)]) * rate * 0.5)));
        spectrum_[static_cast<std::size_t>(bin)] *= gain;
        if (bin > 0 && bin < size / 2)
            spectrum_[static_cast<std::size_t>(size - bin)] *= gain;
    }
    transform(spectrum_.data(), size, twiddles_.data(), bitReverse_.data(), true);
    for (int index = 0; index < size; ++index)
        overlap_[static_cast<std::size_t>(index)] += spectrum_[static_cast<std::size_t>(index)].real() * window_[static_cast<std::size_t>(index)];
    if (stereo) {
        transform(spectrumRight_.data(), size, twiddles_.data(), bitReverse_.data(), true);
        for (int index = 0; index < size; ++index)
            overlapRight_[static_cast<std::size_t>(index)] += spectrumRight_[static_cast<std::size_t>(index)].real() * window_[static_cast<std::size_t>(index)];
    } else {
        for (int index = 0; index < size; ++index)
            overlapRight_[static_cast<std::size_t>(index)] += spectrum_[static_cast<std::size_t>(index)].real() * window_[static_cast<std::size_t>(index)];
    }
}

void SoundModelVoice::renderBlock(const SoundModel& model, const SoundModelValues& values, const double rate) noexcept
{
    blockLeft_.fill(0.0f);
    blockRight_.fill(0.0f);
    for (int index = 0; index < laneCount_; ++index) {
        const auto zone = lanes_[static_cast<std::size_t>(index)].zone;
        if (zone < 0 || static_cast<std::size_t>(zone) >= model.zones.size() || model.zones[static_cast<std::size_t>(zone)].frames <= 0) {
            active_ = false;
            return;
        }
    }
    const auto speed = std::clamp(static_cast<double>(soundModelValue(values, SoundModelSetting::speed)), 0.25, 4.0);
    const auto chosenRelease = static_cast<double>(soundModelValue(values, SoundModelSetting::releaseSeconds));
    const auto keyUp = std::exchange(releaseRequested_, false);
    const auto attackMix = static_cast<float>(soundModelValue(values, SoundModelSetting::attack));

    // The head: where it is at this block's start, whether it still plays.
    const SoundModelZone* headZone = headLane_ >= 0
                                         ? &model.zones[static_cast<std::size_t>(lanes_[static_cast<std::size_t>(headLane_)].zone)]
                                         : nullptr;
    const auto headLength = headZone != nullptr ? static_cast<double>(headZone->headLength()) : 0.0;
    const auto headStart = headPosition_;
    const auto headPlaying = headZone != nullptr && headStart < headLength;
    // The block in which the model comes in takes the recording's phases first.
    if (headPlaying && !headSnapped_ && partialCount_ > 0
        && headStart + headStep_ * controlBlock >= headLength - static_cast<double>(std::max(1, headZone->headFade))) {
        snapToHead(model, values, headStart);
        headSnapped_ = true;
    }

    // Each lane on its own timeline: its release, its frames over this
    // block (the head's time while it plays and the key is down), its fade,
    // its transient.
    auto finished = true;
    for (int laneIndex = 0; laneIndex < laneCount_; ++laneIndex) {
        auto& lane = lanes_[static_cast<std::size_t>(laneIndex)];
        const auto& zone = model.zones[static_cast<std::size_t>(lane.zone)];
        const auto last = static_cast<double>(zone.frames - 1);
        const auto loops = zone.loops() && zone.loopEnd <= zone.frames - 1;
        if (keyUp && !lane.released) {
            lane.released = true;
            const auto natural = loops && zone.release >= 0;
            if (natural) {
                lane.position = std::min(static_cast<double>(zone.release), last);
                lane.direction = 1.0;
            }
            const auto seconds = chosenRelease >= 0.0 ? std::max(0.005, chosenRelease) : (natural ? 0.0 : 0.3);
            lane.fadeRate = seconds > 0.0 ? std::exp(std::log(1.0e-3) / (seconds * rate)) : 1.0;
        }
        const auto step = speed / (static_cast<double>(zone.frameSeconds) * rate);
        const auto level = laneLevel(zone, lane, values);
        // The transient (a zone without a head), once, from the start.
        const auto attackLength = headLane_ < 0 ? static_cast<double>(zone.attack.size()) : 0.0;
        const auto ratio = zone.f0 > 0.0f ? frequency_ / static_cast<double>(zone.f0) : 1.0;
        const auto attackStep = static_cast<double>(zone.attackRate) / rate
                                * std::pow(ratio, static_cast<double>(soundModelValue(values, SoundModelSetting::attackPitch)));
        for (int index = 0; index < controlBlock; ++index) {
            if (attackMix > 0.0f && lane.attackPosition < attackLength - 1.0) {
                const auto lower = static_cast<std::size_t>(lane.attackPosition);
                const auto weight = static_cast<float>(lane.attackPosition - static_cast<double>(lower));
                const auto value = attackMix * zone.attackGain * static_cast<float>(level * lane.fade)
                                   * (zone.attack[lower] + (zone.attack[lower + 1] - zone.attack[lower]) * weight);
                blockLeft_[static_cast<std::size_t>(index)] += value;
                blockRight_[static_cast<std::size_t>(index)] += value;
            }
            lane.attackPosition += attackStep;
            lane.fade *= lane.fadeRate;
            const auto head = headStart + headStep_ * static_cast<double>(index + 1);
            if (headPlaying && !lane.released && head < headLength) {
                lane.position = std::min(head / static_cast<double>(headZone->headRate) / static_cast<double>(zone.frameSeconds), last);
                continue;
            }
            auto p = lane.position + step * lane.direction;
            if (!lane.released && loops) {
                const auto loopStart = static_cast<double>(zone.loopStart);
                const auto loopEnd = static_cast<double>(zone.loopEnd);
                if (lane.direction > 0.0 && p >= loopEnd) {
                    p = loopEnd - (p - loopEnd);
                    lane.direction = -1.0;
                } else if (lane.direction < 0.0 && p <= loopStart) {
                    p = loopStart + (loopStart - p);
                    lane.direction = 1.0;
                }
            }
            lane.position = std::min(p, last);
        }
        const auto attackDone = lane.attackPosition >= attackLength - 1.0;
        const auto ended = ((lane.released || !loops) && lane.position >= last && attackDone) || lane.fade < 1.0e-4;
        finished = finished && ended;
    }

    // The head, at the lanes' summed level, going out as the model comes in.
    if (headPlaying) {
        if (attackMix > 0.0f) {
            auto total = 0.0;
            for (int laneIndex = 0; laneIndex < laneCount_; ++laneIndex) {
                const auto& lane = lanes_[static_cast<std::size_t>(laneIndex)];
                total += laneLevel(model.zones[static_cast<std::size_t>(lane.zone)], lane, values);
            }
            total *= lanes_[static_cast<std::size_t>(headLane_)].fade * static_cast<double>(attackMix);
            const auto& left = headZone->headLeft;
            const auto& right = headZone->headRight.empty() ? headZone->headLeft : headZone->headRight;
            for (int index = 0; index < controlBlock; ++index) {
                const auto head = headStart + headStep_ * static_cast<double>(index);
                if (head >= headLength - 1.0)
                    break;
                const auto lower = static_cast<std::size_t>(head);
                const auto weight = static_cast<float>(head - static_cast<double>(lower));
                const auto gain = static_cast<float>(total * (1.0 - modelShare(model, head)));
                blockLeft_[static_cast<std::size_t>(index)] += gain * (left[lower] + (left[lower + 1] - left[lower]) * weight);
                blockRight_[static_cast<std::size_t>(index)] += gain * (right[lower] + (right[lower + 1] - right[lower]) * weight);
            }
        }
        headPosition_ = headStart + headStep_ * controlBlock;
        if (headPosition_ < headLength)
            finished = false;
    }

    // Partials: one bank of rotating phasors, each amplitude ramped to the
    // lanes' blend at this block's end (the model's share of it under the
    // head); the right turned by the zones' phase offsets.
    const auto harmonicsMix = static_cast<float>(soundModelValue(values, SoundModelSetting::harmonics));
    if (partialCount_ > 0) {
        std::array<float, maximumPartials> targetsLeft {};
        std::array<float, maximumPartials> targetsRight {};
        std::array<double, maximumPartials> increments {};
        partialTargets(model, values, rate, modelShare(model, headPosition_), targetsLeft.data(), targetsRight.data(),
                       increments.data());
        std::array<float, controlBlock> sumLeft {};
        std::array<float, controlBlock> sumRight {};
        for (int k = 0; k < partialCount_; ++k) {
            const auto at = static_cast<std::size_t>(k);
            const auto fromLeft = amplitudeLeft_[at];
            const auto toLeft = targetsLeft[at];
            const auto fromRight = amplitudeRight_[at];
            const auto toRight = targetsRight[at];
            auto c = cos_[at];
            auto s = sin_[at];
            const auto increment = increments[at];
            if (harmonicsMix > 0.0f && (fromLeft > 1.0e-7f || toLeft > 1.0e-7f || fromRight > 1.0e-7f || toRight > 1.0e-7f)) {
                const auto cw = std::cos(increment);
                const auto sw = std::sin(increment);
                const auto deltaLeft = (toLeft - fromLeft) / static_cast<float>(controlBlock);
                const auto deltaRight = (toRight - fromRight) / static_cast<float>(controlBlock);
                const auto rc = rotationCos_[at];
                const auto rs = rotationSin_[at];
                for (int index = 0; index < controlBlock; ++index) {
                    sumLeft[static_cast<std::size_t>(index)] += (fromLeft + deltaLeft * static_cast<float>(index)) * static_cast<float>(s);
                    sumRight[static_cast<std::size_t>(index)]
                        += (fromRight + deltaRight * static_cast<float>(index)) * static_cast<float>(s * rc + c * rs);
                    const auto nc = c * cw - s * sw;
                    s = s * cw + c * sw;
                    c = nc;
                }
            } else {
                // Silent: its phase still turns, by the whole block at once.
                const auto angle = increment * controlBlock;
                const auto ca = std::cos(angle);
                const auto sa = std::sin(angle);
                const auto nc = c * ca - s * sa;
                s = s * ca + c * sa;
                c = nc;
            }
            // Keep the phasor on the unit circle.
            const auto norm = 1.5 - 0.5 * (c * c + s * s);
            cos_[at] = c * norm;
            sin_[at] = s * norm;
            amplitudeLeft_[at] = toLeft;
            amplitudeRight_[at] = toRight;
        }
        // Without a head, a 2 ms onset; with one, the head is the onset.
        const auto onset = 0.002 * rate;
        for (int index = 0; index < controlBlock; ++index) {
            const auto ramp = headLane_ >= 0 ? 1.0f
                                             : static_cast<float>(std::min(1.0, static_cast<double>(samplesPlayed_ + index) / onset));
            blockLeft_[static_cast<std::size_t>(index)] += harmonicsMix * ramp * sumLeft[static_cast<std::size_t>(index)];
            blockRight_[static_cast<std::size_t>(index)] += harmonicsMix * ramp * sumRight[static_cast<std::size_t>(index)];
        }
    }

    // Noise, a hop at a time; the next frame is centred one hop ahead.
    const auto noiseMix = static_cast<float>(soundModelValue(values, SoundModelSetting::noise));
    const auto hop = fftSize_ / 2;
    for (int index = 0; index < controlBlock; ++index) {
        if (noiseRead_ >= hop) {
            std::copy(overlap_.begin() + hop, overlap_.end(), overlap_.begin());
            std::fill(overlap_.begin() + hop, overlap_.end(), 0.0f);
            std::copy(overlapRight_.begin() + hop, overlapRight_.end(), overlapRight_.begin());
            std::fill(overlapRight_.begin() + hop, overlapRight_.end(), 0.0f);
            std::copy(stream_.begin() + hop, stream_.end(), stream_.begin());
            std::copy(streamOther_.begin() + hop, streamOther_.end(), streamOther_.begin());
            for (auto at = static_cast<std::size_t>(hop); at < stream_.size(); ++at) {
                stream_[at] = randomSample(noiseState_);
                streamOther_[at] = randomSample(otherState_);
            }
            // The lanes have reached this block's end: the frame's centre is
            // `hop - (block - index)` samples from there.
            shapeNoiseHop(model, values, static_cast<double>(hop - (controlBlock - index)), rate);
            noiseRead_ = 0;
        }
        if (noiseMix > 0.0f) {
            blockLeft_[static_cast<std::size_t>(index)] += noiseMix * overlap_[static_cast<std::size_t>(noiseRead_)];
            blockRight_[static_cast<std::size_t>(index)] += noiseMix * overlapRight_[static_cast<std::size_t>(noiseRead_)];
        }
        ++noiseRead_;
    }
    samplesPlayed_ += controlBlock;
    if (finished)
        active_ = false;
}

} // namespace cydaw::engine
