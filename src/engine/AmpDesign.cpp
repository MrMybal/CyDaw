#include <cydaw/engine/AmpDesign.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cydaw::engine::amp {

namespace {

constexpr double kPi = std::numbers::pi;

double dbToGain(const double db) noexcept
{
    return std::pow(10.0, db / 20.0);
}

int choice(const float value, const int count) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

BiquadCoefficients bell(const double hz, const double db, const double q, const double rate) noexcept
{
    return designEqBand(EqBandType::bell, hz, db, q, 0, 0, rate);
}

BiquadCoefficients shelf(const bool high, const double hz, const double db, const double rate) noexcept
{
    return designEqBand(high ? EqBandType::highShelf : EqBandType::lowShelf, hz, db, 0.7, 0, 0, rate);
}

// A Butterworth cut of 12 (steep: 24) dB per octave, its sections appended.
void cut(Sections& sections, const bool high, const double hz, const bool steep, const double rate)
{
    const auto type = high ? EqBandType::highCut : EqBandType::lowCut;
    const auto slope = steep ? 1 : 0;
    for (auto stage = 0; stage < eqBandStages(type, slope); ++stage)
        sections.push_back(designEqBand(type, hz, 0.0, 0.707, slope, stage, rate));
}

double scaleOf(const Pattern pattern) noexcept
{
    switch (pattern) {
    case Pattern::omni: return 0.0;
    case Pattern::cardioid: return 1.0;
    case Pattern::hypercardioid: return 1.15;
    case Pattern::figureEight: return 1.4;
    }
    return 1.0;
}

} // namespace

const AmpModel& ampModel(const int index) noexcept
{
    return ampModels[static_cast<std::size_t>(std::clamp(index, 0, ampModelCount - 1))];
}

const CabModel& cabModel(const int index) noexcept
{
    return cabModels[static_cast<std::size_t>(std::clamp(index, 0, cabModelCount - 1))];
}

const MicModel& micModel(const int index) noexcept
{
    return micModels[static_cast<std::size_t>(std::clamp(index, 0, micModelCount - 1))];
}

// ---- The tone stack -----------------------------------------------------------------------------

// Yeh and Smith's analysis of the passive tone stack ("Discretizing the
// Fender Bassman tone stack", DAFx 2006): the network's transfer function
// from its parts and the pots' positions, then the bilinear transform.
ThirdOrder toneStack(const ToneStackParts& parts, const double bass, const double middle, const double treble,
                     const double sampleRate) noexcept
{
    const auto t = std::clamp(treble / 10.0, 0.0, 1.0);
    const auto m = std::clamp(middle / 10.0, 0.0, 1.0);
    // The bass pot is logarithmic.
    const auto l = std::exp((std::clamp(bass / 10.0, 0.0, 1.0) - 1.0) * 3.4);
    const auto R1 = parts.r1;
    const auto R2 = parts.r2;
    const auto R3 = parts.r3;
    const auto R4 = parts.r4;
    const auto C1 = parts.c1;
    const auto C2 = parts.c2;
    const auto C3 = parts.c3;
    const auto b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
    const auto b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
                    + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
    const auto b3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                    - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                    + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4) + t * C1 * C2 * C3 * R1 * R3 * R4
                    - t * m * C1 * C2 * C3 * R1 * R3 * R4 + t * l * C1 * C2 * C3 * R1 * R2 * R4;
    const auto a0 = 1.0;
    const auto a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
    const auto a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
                    + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4
                       + C2 * C3 * R3 * R4);
    const auto a3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                    - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                    + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4)
                    + l * C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R1 * R3 * R4;
    // s = c (1 - 1/z) / (1 + 1/z).
    const auto c = 2.0 * std::max(1'000.0, sampleRate);
    const auto c2 = c * c;
    const auto c3 = c2 * c;
    const auto b0 = 0.0;
    std::array<double, 4> B { b0 + b1 * c + b2 * c2 + b3 * c3, 3.0 * b0 + b1 * c - b2 * c2 - 3.0 * b3 * c3,
                              3.0 * b0 - b1 * c - b2 * c2 + 3.0 * b3 * c3, b0 - b1 * c + b2 * c2 - b3 * c3 };
    std::array<double, 4> A { a0 + a1 * c + a2 * c2 + a3 * c3, 3.0 * a0 + a1 * c - a2 * c2 - 3.0 * a3 * c3,
                              3.0 * a0 - a1 * c - a2 * c2 + 3.0 * a3 * c3, a0 - a1 * c + a2 * c2 - a3 * c3 };
    ThirdOrder filter;
    for (std::size_t i = 0; i < 4; ++i) {
        filter.b[i] = B[i] / A[0];
        filter.a[i] = A[i] / A[0];
    }
    return filter;
}

std::complex<double> response(const ThirdOrder& filter, const double hz, const double sampleRate) noexcept
{
    const auto omega = 2.0 * kPi * std::min(hz, sampleRate * 0.4999) / sampleRate;
    const auto z = std::polar(1.0, -omega);
    const auto z2 = z * z;
    const auto z3 = z2 * z;
    const auto numerator = filter.b[0] + filter.b[1] * z + filter.b[2] * z2 + filter.b[3] * z3;
    const auto denominator = filter.a[0] + filter.a[1] * z + filter.a[2] * z2 + filter.a[3] * z3;
    return numerator / denominator;
}

std::complex<double> response(const Sections& sections, const double hz, const double sampleRate) noexcept
{
    const auto omega = 2.0 * kPi * std::min(hz, sampleRate * 0.4999) / sampleRate;
    const auto z = std::polar(1.0, -omega);
    std::complex<double> total { 1.0, 0.0 };
    for (const auto& c : sections)
        total *= (c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z);
    return total;
}

// ---- The amp -------------------------------------------------------------------------------------

AmpVoicing ampVoicing(const std::array<float, ampParameterCount>& values, const double sampleRate) noexcept
{
    const auto& model = ampModel(choice(values[ampModelValue], ampModelCount));
    const auto rate = std::max(1'000.0, sampleRate);
    AmpVoicing voicing;
    cut(voicing.before, false, std::max(model.inputHighPassHz, static_cast<double>(values[ampTight])), false, rate);
    if (values[ampBright] >= 0.5f)
        voicing.before.push_back(shelf(true, 1'500.0, 9.0 * (1.0 - 0.07 * values[ampGain]), rate));
    voicing.midBoost = model.midBoostDb != 0.0 ? bell(model.midBoostHz, model.midBoostDb, 0.8, rate) : BiquadCoefficients {};
    voicing.stack = toneStack(model.stack, values[ampBass], values[ampMiddle], values[ampTreble], rate);
    voicing.stackMakeup = dbToGain(model.stackMakeupDb);
    voicing.after.push_back(shelf(true, model.presenceHz, (values[ampPresence] - 5.0) * 1.6, rate));
    voicing.after.push_back(bell(model.depthHz, values[ampDepth] * 0.6, 0.9, rate));
    return voicing;
}

std::array<float, ampParameterCount> ampChannelValues(const std::array<float, ampParameterCount>& values, const int channel) noexcept
{
    auto played = values;
    if (channel == 1)
        for (const auto& [a, b] : ampChannelPairs)
            played[static_cast<std::size_t>(a)] = values[static_cast<std::size_t>(b)];
    return played;
}

double ampToneDb(const std::array<float, ampParameterCount>& values, const double hz, const double sampleRate) noexcept
{
    const auto voicing = ampVoicing(values, sampleRate);
    Sections boost;
    boost.push_back(voicing.midBoost);
    auto total = response(voicing.before, hz, sampleRate) * response(boost, hz, sampleRate)
                 * response(voicing.stack, hz, sampleRate) * voicing.stackMakeup * response(voicing.after, hz, sampleRate);
    return 20.0 * std::log10(std::max(std::abs(total), 1.0e-9));
}

double stageGain(const AmpModel& model, const double gainKnob) noexcept
{
    const auto totalDb = model.minimumGainDb + (model.maximumGainDb - model.minimumGainDb) * std::clamp(gainKnob / 10.0, 0.0, 1.0);
    return dbToGain(totalDb / static_cast<double>(std::max(1, model.stages)));
}

double preampLevel(const AmpModel& model, const double gainKnob) noexcept
{
    // What the stages make of a guitar's usual level (-12 dBFS): a clean amp
    // barely driven gives little, a saturated one nearly all; half of the
    // difference is taken back, so the gain knob is not a volume knob.
    const auto gain = stageGain(model, gainKnob);
    auto value = 0.25;
    for (auto stage = 0; stage < model.stages; ++stage)
        value = valveStage(value * gain, model.bias);
    return std::sqrt(0.8 / std::max(std::abs(value), 0.02));
}

double preampCurve(const AmpModel& model, const double gainKnob, const double x) noexcept
{
    const auto gain = stageGain(model, gainKnob);
    auto value = x;
    for (auto stage = 0; stage < model.stages; ++stage)
        value = valveStage(value * gain, model.bias);
    // Upright whatever the number of inverting stages.
    return (model.stages % 2 == 0 ? value : -value) * preampLevel(model, gainKnob);
}

// ---- The microphones -----------------------------------------------------------------------------

double patternGain(const Pattern pattern, const double degrees) noexcept
{
    const auto c = std::cos(std::clamp(degrees, 0.0, 180.0) * kPi / 180.0);
    auto gain = 1.0;
    switch (pattern) {
    case Pattern::omni: gain = 1.0; break;
    case Pattern::cardioid: gain = 0.5 * (1.0 + c); break;
    case Pattern::hypercardioid: gain = std::abs(0.25 + 0.75 * c); break;
    case Pattern::figureEight: gain = std::abs(c); break;
    }
    return std::max(0.03, gain);
}

double proximityDb(const MicModel& model, const Pattern pattern, const double centimetres) noexcept
{
    const auto own = scaleOf(model.pattern);
    const auto base = model.proximityDb > 0.0 && own > 0.0 ? model.proximityDb / own : 7.0;
    const auto closeness = std::clamp(1.0 - std::log10(std::max(1.0, centimetres)) / std::log10(60.0), 0.0, 1.0);
    return base * scaleOf(pattern) * std::pow(closeness, 1.5);
}

Sections micSections(const MicModel& model, const Pattern pattern, const double centimetres,
                                            const double degrees, const double sampleRate) noexcept
{
    const auto rate = std::max(1'000.0, sampleRate);
    Sections sections;
    cut(sections, false, model.lowCutHz, model.steep, rate);
    if (model.warmthDb != 0.0)
        sections.push_back(bell(model.warmthHz, model.warmthDb, 0.8, rate));
    if (model.presenceDb != 0.0)
        sections.push_back(bell(model.presenceHz, model.presenceDb, model.presenceQ, rate));
    if (model.airDb != 0.0)
        sections.push_back(shelf(true, model.airHz, model.airDb, rate));
    if (model.topHz < 19'500.0)
        cut(sections, true, model.topHz, model.steep, rate);
    if (model.resonanceDb != 0.0)
        sections.push_back(bell(model.resonanceHz, model.resonanceDb, model.resonanceQ, rate));
    if (const auto proximity = proximityDb(model, pattern, centimetres); proximity > 0.05)
        sections.push_back(shelf(false, 180.0, proximity, rate));
    // Off its axis it loses the highs; behind it, more.
    const auto angle = std::clamp(degrees, 0.0, 180.0);
    auto loss = model.offAxisDb * std::pow(std::min(1.0, angle / 90.0), 1.3);
    if (angle > 90.0)
        loss += model.offAxisDb * 0.5 * (angle - 90.0) / 90.0;
    if (loss > 0.05)
        sections.push_back(shelf(true, 3'000.0, -loss, rate));
    return sections;
}

Sections cabCoreSections(const CabModel& model, const double resonance, const double sampleRate) noexcept
{
    const auto rate = std::max(1'000.0, sampleRate);
    Sections sections;
    cut(sections, false, model.lowHz, model.openBack, rate);
    sections.push_back(bell(model.resonanceHz, model.resonanceDb * 2.0 * std::clamp(resonance, 0.0, 1.0), 1.4, rate));
    if (model.bodyDb != 0.0)
        sections.push_back(bell(model.bodyHz, model.bodyDb, 0.8, rate));
    sections.push_back(bell(model.dipHz, model.dipDb, 1.8, rate));
    return sections;
}

Sections cabPlaceSections(const CabModel& model, const double position, const double degrees,
                                                 const double sampleRate) noexcept
{
    const auto rate = std::max(1'000.0, sampleRate);
    const auto place = std::clamp(position, 0.0, 1.0);
    const auto angle = std::clamp(degrees, 0.0, 90.0) / 90.0;
    Sections sections;
    // At the centre the cone's break-up is loudest and the highs reach
    // furthest; towards the edge and off axis, darker.
    sections.push_back(bell(model.breakupHz * (1.0 + 0.15 * (1.0 - place)), model.breakupDb * (1.0 - 0.7 * place), 1.6, rate));
    const auto top = model.topCentreHz * std::pow(model.topEdgeHz / model.topCentreHz, place) * (1.0 - 0.35 * angle);
    cut(sections, true, std::min(top, rate * 0.45), true, rate);
    return sections;
}

namespace {

// A cabinet and its microphones as the curve hears them: their sections
// designed once, read at as many frequencies as asked (the makeup's sweep
// reads 24; designing them again for each was most of its cost, paid on the
// audio thread at every step of a knob's glide).
class CabCurve {
public:
    CabCurve(const std::array<float, cabParameterCount>& values, double sampleRate) noexcept;
    // which: 0 the microphone A, 1 B, 2 both as blended.
    [[nodiscard]] double db(int which, double hz) const noexcept;

private:
    double rate_;
    Sections core_;
    std::array<Sections, 2> place_;
    std::array<Sections, 2> mic_;
    std::array<double, 2> flight_ {};
    Sections cuts_;
    bool aligned_ {};
    bool inverted_ {};
    bool both_ {};
    double blend_ {};
};

// The average of a curve in dB over log-spaced points of a band.
template <typename Curve>
double averageDb(const double low, const double high, Curve curve) noexcept
{
    constexpr int points = 24;
    auto sum = 0.0;
    for (int i = 0; i < points; ++i)
        sum += curve(low * std::pow(high / low, (i + 0.5) / points));
    return sum / points;
}

} // namespace

double cabMakeupDb(const std::array<float, cabParameterCount>& values, const double sampleRate) noexcept
{
    const CabCurve curve(values, sampleRate);
    return std::clamp(-averageDb(100.0, 4'000.0, [&curve](const double hz) { return curve.db(2, hz); }), -24.0, 24.0);
}

double cabResponseDb(const std::array<float, cabParameterCount>& values, const int which, const double hz,
                     const double sampleRate) noexcept
{
    const CabCurve curve(values, sampleRate);
    const auto makeup = std::clamp(-averageDb(100.0, 4'000.0, [&curve](const double at) { return curve.db(2, at); }), -24.0, 24.0);
    return curve.db(which, hz) + makeup;
}

namespace {

CabCurve::CabCurve(const std::array<float, cabParameterCount>& values, const double sampleRate) noexcept
    : rate_(sampleRate)
{
    const auto& cab = cabModel(choice(values[cabModelValue], cabModelCount));
    core_ = cabCoreSections(cab, values[cabResonance], sampleRate);
    aligned_ = values[cabAlign] >= 0.5f;
    const std::array<std::array<int, 4>, 2> mics { { { cabMicA, cabPositionA, cabDistanceA, cabAngleA },
                                                     { cabMicB, cabPositionB, cabDistanceB, cabAngleB } } };
    for (std::size_t index = 0; index < 2; ++index) {
        const auto [microphone, position, distance, angle] = mics[index];
        const auto& model = micModel(choice(values[static_cast<std::size_t>(microphone)], cabMicCount));
        place_[index] = cabPlaceSections(cab, values[static_cast<std::size_t>(position)], values[static_cast<std::size_t>(angle)],
                                         sampleRate);
        mic_[index] = micSections(model, model.pattern, values[static_cast<std::size_t>(distance)],
                                  values[static_cast<std::size_t>(angle)], sampleRate);
        flight_[index] = flightSeconds(values[static_cast<std::size_t>(distance)]);
    }
    inverted_ = values[cabPolarityB] >= 0.5f;
    blend_ = static_cast<double>(std::clamp(values[cabBlend], 0.0f, 1.0f));
    both_ = values[cabMicBOn] >= 0.5f;
    if (values[cabLowCut] > 20.5f)
        cut(cuts_, false, values[cabLowCut], false, sampleRate);
    if (values[cabHighCut] < 19'900.0f)
        cut(cuts_, true, values[cabHighCut], false, sampleRate);
}

double CabCurve::db(const int which, const double hz) const noexcept
{
    const auto omega = 2.0 * kPi * hz;
    const auto mic = [&](const std::size_t index) {
        auto value = response(place_[index], hz, rate_) * response(mic_[index], hz, rate_);
        if (!aligned_)
            value *= std::polar(1.0, -omega * flight_[index]);
        return value;
    };
    const auto a = mic(0);
    auto b = mic(1);
    if (inverted_)
        b = -b;
    const auto both = both_ ? a * (1.0 - blend_) + b * blend_ : a;
    auto total = response(core_, hz, rate_) * (which == 0 ? a : which == 1 ? b : both);
    total *= response(cuts_, hz, rate_);
    return 20.0 * std::log10(std::max(std::abs(total), 1.0e-9));
}

} // namespace

// ---- Micro ---------------------------------------------------------------------------------------

Pattern micPatternOf(const std::array<float, micParameterCount>& values) noexcept
{
    const auto chosen = choice(values[micPattern], 5);
    if (chosen == 0)
        return micModel(choice(values[micModelValue], micModelCount)).pattern;
    return static_cast<Pattern>(chosen);
}

double micMakeupDb(const std::array<float, micParameterCount>& values, const double sampleRate) noexcept
{
    // The microphone's sections (its pattern's pick-up aside), designed once
    // for the whole sweep.
    const auto& model = micModel(choice(values[micModelValue], micModelCount));
    const auto sections = micSections(model, micPatternOf(values), values[micDistance], values[micAngle], sampleRate);
    return std::clamp(-averageDb(300.0, 3'000.0,
                                 [&](const double hz) {
                                     return 20.0 * std::log10(std::max(std::abs(response(sections, hz, sampleRate)), 1.0e-9));
                                 }),
                      -24.0, 24.0);
}

double micResponseDb(const std::array<float, micParameterCount>& values, const double hz, const double sampleRate) noexcept
{
    const auto& model = micModel(choice(values[micModelValue], micModelCount));
    const auto pattern = micPatternOf(values);
    auto sections = micSections(model, pattern, values[micDistance], values[micAngle], sampleRate);
    if (values[micLowCut] > 20.5f)
        cut(sections, false, values[micLowCut], false, sampleRate);
    if (values[micHighCut] < 19'900.0f)
        cut(sections, true, values[micHighCut], false, sampleRate);
    const auto level = patternGain(pattern, values[micAngle]);
    return 20.0 * std::log10(std::max(std::abs(response(sections, hz, sampleRate)) * level, 1.0e-9))
           + micMakeupDb(values, sampleRate);
}

double preampSaturate(const int kind, const double x) noexcept
{
    switch (kind) {
    case 1: {
        // A valve: asymmetric, its small-signal slope one.
        constexpr auto bias = 0.25;
        const auto slope = 1.0 - std::tanh(bias) * std::tanh(bias);
        return (std::tanh(x + bias) - std::tanh(bias)) / slope;
    }
    case 2: return x / std::pow(1.0 + std::pow(std::abs(x), 2.5), 0.4);
    default: return std::tanh(x);
    }
}

} // namespace cydaw::engine::amp
