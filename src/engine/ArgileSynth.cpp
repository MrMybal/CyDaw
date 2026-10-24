// Argile: a classic virtual-analog synthesiser of the synth family, with two
// of everything. Two oscillators (sine, saw, rectangle with its width, a noise
// that follows the note), each with a square sub an octave below, the second
// hard-synced to the first if asked and swept by the first filter envelope; a
// noise of adjustable colour. Each source is shared between two filters of
// eight types (low-pass 12 and 24 dB, band-pass 6 and 12 dB, notch, high-pass
// 12 and 24 dB, a formant filter with its vowel), each with its drive, its
// key tracking and its own envelope; the filters run side by side, or the
// first into the second. After each filter its amplifier: its envelope, its
// level, its pan. Two LFOs (to pitch, width, both cutoffs, pan, volume),
// unison up to four voices across the stereo, an analog drift.

#include "SynthEngines.h"
#include "SynthParts.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

// Argile's settings after the shared voicing, in its table's order.
enum Argile : int {
    unisonVoices = voicingFieldCount,
    unisonDetune,
    unisonSpread,
    driftAmount,
    osc1Wave,
    osc1Octave,
    osc1Semi,
    osc1Fine,
    osc1Pw,
    osc1Sub,
    osc1Level,
    osc1Route,
    osc2Wave,
    osc2Octave,
    osc2Semi,
    osc2Fine,
    osc2Pw,
    osc2Sub,
    osc2Level,
    osc2Route,
    osc2Sync,
    osc2Env,
    noiseLevel,
    noiseColour,
    noiseRoute,
    filter1Type,
    filter1Cutoff,
    filter1Resonance,
    filter1Drive,
    filter1Key,
    filter1Env,
    filter1Vowel,
    fenv1Attack,
    fenv1Decay,
    fenv1Sustain,
    fenv1Release,
    filter2Type,
    filter2Cutoff,
    filter2Resonance,
    filter2Drive,
    filter2Key,
    filter2Env,
    filter2Vowel,
    fenv2Attack,
    fenv2Decay,
    fenv2Sustain,
    fenv2Release,
    filterSerial,
    filterVelocity,
    amp1Attack,
    amp1Decay,
    amp1Sustain,
    amp1Release,
    amp1Level,
    amp1Pan,
    amp1Velocity,
    amp2Attack,
    amp2Decay,
    amp2Sustain,
    amp2Release,
    amp2Level,
    amp2Pan,
    amp2Velocity,
    lfo1Shape,
    lfo1Rate,
    lfo1Sync,
    lfo1Delay,
    lfo1Pitch,
    lfo1Pw,
    lfo1Cutoff1,
    lfo1Cutoff2,
    lfo1Pan,
    lfo1Amp,
    lfo2Shape,
    lfo2Rate,
    lfo2Sync,
    lfo2Delay,
    lfo2Pitch,
    lfo2Pw,
    lfo2Cutoff1,
    lfo2Cutoff2,
    lfo2Pan,
    lfo2Amp,
    modWheel,
    argileCount
};
static_assert(argileCount == 90);

// The settings of an oscillator, a filter, an amplifier, an LFO, from its first.
enum OscField : int { oscWave, oscOctave, oscSemi, oscFine, oscPw, oscSub, oscLevel, oscRoute, oscFields };
enum FilterField : int {
    filterType,
    filterCutoff,
    filterResonance,
    filterDrive,
    filterKey,
    filterEnv,
    filterVowel,
    filterAttack,
    filterDecay,
    filterSustain,
    filterRelease,
    filterFields
};
enum AmpField : int { ampAttack, ampDecay, ampSustain, ampRelease, ampLevel, ampPan, ampVelocity, ampFields };
enum LfoField : int { lfoShape, lfoRate, lfoSync, lfoDelay, lfoPitch, lfoPw, lfoCutoff1, lfoCutoff2, lfoPan, lfoAmp, lfoFields };
constexpr int fieldOf(const int first, const int field) noexcept
{
    return first + field;
}
static_assert(osc2Wave - osc1Wave == oscFields && fieldOf(osc1Wave, oscRoute) == osc1Route && fieldOf(osc2Wave, oscRoute) == osc2Route);
static_assert(filter2Type - filter1Type == filterFields && fieldOf(filter1Type, filterRelease) == fenv1Release
              && fieldOf(filter2Type, filterRelease) == fenv2Release);
static_assert(amp2Attack - amp1Attack == ampFields && fieldOf(amp2Attack, ampVelocity) == amp2Velocity);
static_assert(lfo2Shape - lfo1Shape == lfoFields && fieldOf(lfo2Shape, lfoAmp) == lfo2Amp);

constexpr std::array<int, 2> kOscs { osc1Wave, osc2Wave };
constexpr std::array<int, 2> kFilters { filter1Type, filter2Type };
constexpr std::array<int, 2> kAmps { amp1Attack, amp2Attack };
constexpr std::array<int, 2> kLfos { lfo1Shape, lfo2Shape };

enum Wave : int { waveSine, waveSaw, waveRectangle, waveNoise, waveCount };
enum FilterKind : int { kindLp12, kindLp24, kindBp6, kindBp12, kindNotch, kindHp12, kindHp24, kindFormant, kindCount };

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices kWaves { { "Sinus", "Sine" }, { "Dent de scie", "Saw" }, { "Rectangle", "Rectangle" }, { "Bruit", "Noise" } };
const Choices kFilterKinds { { "Passe-bas 12", "LP12" },    { "Passe-bas 24", "LP24" },  { "Passe-bande 6", "BP6" },
                             { "Passe-bande 12", "BP12" },  { "Coupe-bande", "Notch" },  { "Passe-haut 12", "HP12" },
                             { "Passe-haut 24", "HP24" },   { "Formant", "Formant" } };
const Choices kLfoShapes { { "Sinus", "Sine" }, { "Triangle", "Triangle" }, { "Dent de scie", "Saw" }, { "Carré", "Square" },
                           { "Aléatoire", "Random" } };

// The vowels a formant filter moves across (A, E, I, O, U): three formants each, in Hz.
constexpr std::array<std::array<double, 3>, 5> kVowels { { { 800.0, 1'150.0, 2'800.0 },
                                                          { 400.0, 1'700.0, 2'600.0 },
                                                          { 280.0, 2'250.0, 2'900.0 },
                                                          { 450.0, 800.0, 2'830.0 },
                                                          { 325.0, 700.0, 2'530.0 } } };
constexpr std::array<double, 3> kFormantGains { 1.0, 0.63, 0.4 };
constexpr double kRoot2 = 1.4142135623730951;
// What one voice gives out (a chord of four at the default volume stays well inside -1..1).
constexpr double kOutput = 0.7;

} // namespace

std::vector<Section> argileSections()
{
    return { { "VOIX", "VOICES", 0xffc8b8b0u },
             { "OSCILLATEURS", "OSCILLATORS", 0xffe8a06au },
             { "FILTRES", "FILTERS", 0xffe0705au },
             { "AMPLIS", "AMPLIFIERS", 0xff8fd0a0u },
             { "LFO", "LFO", 0xffb49cffu } };
}

std::vector<Parameter> argileParameters()
{
    Table table;
    addVoicing(table, 16, 0.7f);
    table.whole("unison.voices", "Unisson", "Unison", {}, 1, 4, 1);
    table.add("unison.detune", "Unisson · désaccord", "Unison · detune", "ct", 0.0f, 50.0f, 12.0f);
    table.add("unison.spread", "Unisson · largeur", "Unison · spread", {}, 0.0f, 1.0f, 0.7f);
    table.add("drift", "Dérive", "Drift", {}, 0.0f, 1.0f, 0.25f);
    table.section = 1;
    for (int index = 1; index <= 2; ++index) {
        const auto number = std::to_string(index);
        const auto prefix = "osc" + number;
        const auto name = "Osc " + number + " · ";
        table.choice(prefix + ".wave", name + "forme", name + "wave", kWaves, waveSaw);
        table.whole(prefix + ".octave", name + "octave", name + "octave", {}, -3, 3, 0);
        table.whole(prefix + ".semi", name + "demi-tons", name + "semitones", "st", -12, 12, 0);
        table.add(prefix + ".fine", name + "désaccord", name + "detune", "ct", -50.0f, 50.0f, index == 1 ? 0.0f : 7.0f);
        table.add(prefix + ".pw", name + "largeur", name + "width", {}, 0.05f, 0.95f, 0.5f);
        table.add(prefix + ".sub", name + "sub", name + "sub", {}, 0.0f, 1.0f, 0.0f);
        table.add(prefix + ".level", name + "niveau", name + "level", {}, 0.0f, 1.0f, index == 1 ? 0.8f : 0.5f);
        table.add(prefix + ".route", "Osc " + number + " → filtre 2", "Osc " + number + " → filter 2", {}, 0.0f, 1.0f, 0.0f);
    }
    table.toggle("osc2.sync", "Osc 2 · synchro sur osc 1", "Osc 2 · sync to osc 1", false);
    table.add("osc2.env", "Osc 2 · env. filtre 1 → hauteur", "Osc 2 · filter env 1 → pitch", {}, -1.0f, 1.0f, 0.0f);
    table.add("noise.level", "Bruit · niveau", "Noise · level", {}, 0.0f, 1.0f, 0.0f);
    table.add("noise.colour", "Bruit · couleur", "Noise · colour", {}, 0.0f, 1.0f, 0.5f);
    table.add("noise.route", "Bruit → filtre 2", "Noise → filter 2", {}, 0.0f, 1.0f, 0.0f);
    table.section = 2;
    for (int index = 1; index <= 2; ++index) {
        const auto number = std::to_string(index);
        const auto prefix = "filter" + number;
        const auto french = "Filtre " + number + " · ";
        const auto english = "Filter " + number + " · ";
        table.choice(prefix + ".type", french + "type", english + "type", kFilterKinds, index == 1 ? kindLp24 : kindLp12);
        table.add(prefix + ".cutoff", french + "fréquence", english + "cutoff", "Hz", 20.0f, 20'000.0f, index == 1 ? 1'800.0f : 2'500.0f,
                  Kind::continuous, true);
        table.add(prefix + ".resonance", french + "résonance", english + "resonance", {}, 0.0f, 1.0f, 0.2f);
        table.add(prefix + ".drive", french + "saturation", english + "drive", {}, 0.0f, 1.0f, 0.15f);
        table.add(prefix + ".key", french + "suivi du clavier", english + "key tracking", {}, 0.0f, 1.0f, 0.5f);
        table.add(prefix + ".env", french + "enveloppe", english + "envelope", {}, -1.0f, 1.0f, index == 1 ? 0.35f : 0.3f);
        table.add(prefix + ".vowel", french + "voyelle", english + "vowel", {}, 0.0f, 1.0f, 0.0f);
        const auto envelope = "fenv" + number;
        const auto frenchEnvelope = "Env. filtre " + number + " · ";
        const auto englishEnvelope = "Filter env " + number + " · ";
        table.add(envelope + ".attack", frenchEnvelope + "attaque", englishEnvelope + "attack", "s", 0.001f, 10.0f, 0.003f, Kind::continuous, true);
        table.add(envelope + ".decay", frenchEnvelope + "déclin", englishEnvelope + "decay", "s", 0.005f, 10.0f, 0.6f, Kind::continuous, true);
        table.add(envelope + ".sustain", frenchEnvelope + "maintien", englishEnvelope + "sustain", {}, 0.0f, 1.0f, 0.3f);
        table.add(envelope + ".release", frenchEnvelope + "relâchement", englishEnvelope + "release", "s", 0.005f, 12.0f, 0.4f, Kind::continuous,
                  true);
    }
    table.add("filter.serial", "Filtre 1 → filtre 2 (série)", "Filter 1 → filter 2 (serial)", {}, 0.0f, 1.0f, 0.0f);
    table.add("filter.velocity", "Filtres · vélocité", "Filters · velocity", {}, 0.0f, 1.0f, 0.3f);
    table.section = 3;
    for (int index = 1; index <= 2; ++index) {
        const auto number = std::to_string(index);
        const auto prefix = "amp" + number;
        const auto french = "Ampli " + number + " · ";
        const auto english = "Amp " + number + " · ";
        table.add(prefix + ".attack", french + "attaque", english + "attack", "s", 0.001f, 10.0f, 0.004f, Kind::continuous, true);
        table.add(prefix + ".decay", french + "déclin", english + "decay", "s", 0.005f, 10.0f, 0.6f, Kind::continuous, true);
        table.add(prefix + ".sustain", french + "maintien", english + "sustain", {}, 0.0f, 1.0f, 0.8f);
        table.add(prefix + ".release", french + "relâchement", english + "release", "s", 0.005f, 12.0f, 0.35f, Kind::continuous, true);
        table.add(prefix + ".level", french + "niveau", english + "level", {}, 0.0f, 1.0f, 0.8f);
        table.add(prefix + ".pan", french + "panoramique", english + "pan", {}, -1.0f, 1.0f, 0.0f);
        table.add(prefix + ".velocity", french + "vélocité", english + "velocity", {}, 0.0f, 1.0f, 0.5f);
    }
    table.section = 4;
    for (int index = 1; index <= 2; ++index) {
        const auto number = std::to_string(index);
        const auto prefix = "lfo" + number;
        const auto name = "LFO " + number + " · ";
        const auto towards = "LFO " + number + " → ";
        table.choice(prefix + ".shape", name + "forme", name + "shape", kLfoShapes, index == 1 ? 0 : 1);
        table.add(prefix + ".rate", name + "vitesse", name + "rate", "Hz", 0.02f, 30.0f, index == 1 ? 5.0f : 0.5f, Kind::continuous, true);
        table.whole(prefix + ".sync", name + "synchro", name + "sync", {}, 0, 12, 0);
        table.add(prefix + ".delay", name + "retard", name + "delay", "s", 0.0f, 4.0f, index == 1 ? 0.3f : 0.0f);
        table.add(prefix + ".pitch", towards + "hauteur", towards + "pitch", {}, 0.0f, 1.0f, 0.0f);
        table.add(prefix + ".pw", towards + "largeur", towards + "width", {}, 0.0f, 1.0f, 0.0f);
        table.add(prefix + ".cutoff1", towards + "filtre 1", towards + "cutoff 1", {}, -1.0f, 1.0f, 0.0f);
        table.add(prefix + ".cutoff2", towards + "filtre 2", towards + "cutoff 2", {}, -1.0f, 1.0f, 0.0f);
        table.add(prefix + ".pan", towards + "panoramique", towards + "pan", {}, 0.0f, 1.0f, 0.0f);
        table.add(prefix + ".amp", towards + "volume", towards + "volume", {}, 0.0f, 1.0f, 0.0f);
    }
    table.add("mod.wheel", "Molette → vibrato", "Wheel → vibrato", {}, 0.0f, 1.0f, 0.5f);
    return table.parameters;
}

namespace {

// How strongly a two-pole stage of a filter type is damped at a resonance
// (0..1): the less, the sharper it rings.
double stageDamping(const int kind, const double resonance) noexcept
{
    switch (kind) {
    case kindBp6: return 2.0 * (1.0 - 0.97 * resonance);
    case kindBp12:
    case kindNotch: return 2.0 * (1.0 - 0.9 * resonance);
    case kindHp24: return 0.765 * (1.0 - 0.96 * resonance);
    case kindFormant: return 0.35 - 0.29 * resonance;
    default: return 1.414 * (1.0 - 0.975 * resonance);
    }
}

// What a band-pass type gives back for the narrowing of its band.
double bandGain(const double resonance) noexcept
{
    return 1.0 + 1.5 * resonance;
}

// The formant type's own gain: narrow bands pass little.
double formantGain(const double damping) noexcept
{
    return 2.5 * std::sqrt(0.35 / std::max(0.01, damping));
}

// The three formants at a place among the vowels (0 A .. 1 U), moved by `shift`.
std::array<double, 3> formantsAt(const double position, const double shift) noexcept
{
    const auto t = std::clamp(position, 0.0, 1.0) * 4.0;
    const auto lower = std::min(static_cast<std::size_t>(t), std::size_t { 3 });
    const auto mix = t - static_cast<double>(lower);
    std::array<double, 3> result {};
    for (std::size_t at = 0; at < 3; ++at)
        result[at] = (kVowels[lower][at] + (kVowels[lower + 1][at] - kVowels[lower][at]) * mix) * shift;
    return result;
}

// The formant type moves its formants by its cutoff around 1 kHz, half as far.
double formantShift(const double cutoffHz) noexcept
{
    return std::clamp(std::sqrt(std::max(1.0, cutoffHz) / 1'000.0), 0.5, 2.0);
}

// One state-variable stage (two poles, solved without delay): its
// coefficients, made once a control step.
struct SvfCoefficients {
    double k { 1.414 };
    double a1 { 1.0 };
    double a2 {};
    double a3 {};
};

SvfCoefficients svfCoefficients(const double g, const double k) noexcept
{
    SvfCoefficients c;
    c.k = k;
    c.a1 = 1.0 / (1.0 + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

struct SvfOutputs {
    double low {};
    double band {};
    double high {};
};

struct Svf {
    double ic1 {};
    double ic2 {};
    SvfOutputs tick(const SvfCoefficients& c, const double x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = c.a1 * ic1 + c.a2 * v3;
        const auto v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return { v2, v1, x - c.k * v1 - v2 };
    }
};

// One filter for one control step: its type, its stages, its gain.
struct FilterSetting {
    int kind { kindLp12 };
    std::array<SvfCoefficients, 3> stages {};
    double g {};         // the ladder's
    double resonance {}; // the ladder's
    double gain { 1.0 };
    std::array<double, 3> formantGains {};
};

FilterSetting settingFor(const int kind, const double cutoff, const double resonance, const double rate) noexcept
{
    FilterSetting setting;
    setting.kind = kind;
    const auto g = std::tan(kPi * cutoff / rate);
    const auto k = stageDamping(kind, resonance);
    switch (kind) {
    case kindLp24:
        setting.g = g;
        setting.resonance = resonance;
        break;
    case kindBp6:
        setting.stages[0] = svfCoefficients(g, k);
        setting.gain = k * bandGain(resonance);
        break;
    case kindBp12:
        setting.stages[0] = svfCoefficients(g, k);
        setting.stages[1] = setting.stages[0];
        setting.gain = k * bandGain(resonance);
        break;
    case kindHp24:
        setting.stages[0] = svfCoefficients(g, 1.848);
        setting.stages[1] = svfCoefficients(g, k);
        break;
    default: setting.stages[0] = svfCoefficients(g, k); break;
    }
    return setting;
}

FilterSetting formantSetting(const double position, const double shift, const double resonance, const double rate) noexcept
{
    FilterSetting setting;
    setting.kind = kindFormant;
    const auto k = stageDamping(kindFormant, resonance);
    const auto formants = formantsAt(position, shift);
    for (std::size_t at = 0; at < 3; ++at) {
        const auto hz = std::clamp(formants[at], 40.0, rate * 0.45);
        setting.stages[at] = svfCoefficients(std::tan(kPi * hz / rate), k);
        setting.formantGains[at] = kFormantGains[at] * k;
    }
    setting.gain = formantGain(k);
    return setting;
}

void flushTiny(double& value) noexcept
{
    if (std::abs(value) < 1.0e-15)
        value = 0.0;
}

// One filter's state for one channel: its stages, its ladder.
struct FilterState {
    std::array<Svf, 3> stages {};
    Filter ladder {};

    void reset() noexcept
    {
        stages = {};
        ladder.reset();
    }

    // No denormals in a tail.
    void settle() noexcept
    {
        for (auto& stage : stages) {
            flushTiny(stage.ic1);
            flushTiny(stage.ic2);
        }
        for (auto& pole : ladder.s)
            flushTiny(pole);
        flushTiny(ladder.low);
        flushTiny(ladder.band);
        flushTiny(ladder.last);
    }

    double process(const FilterSetting& setting, const double x) noexcept
    {
        switch (setting.kind) {
        case kindLp12: return stages[0].tick(setting.stages[0], x).low;
        case kindLp24: return ladder.ladder(x, setting.g, setting.resonance);
        case kindBp6: return stages[0].tick(setting.stages[0], x).band * setting.gain;
        case kindBp12: {
            const auto first = stages[0].tick(setting.stages[0], x).band * setting.stages[0].k;
            return stages[1].tick(setting.stages[1], first).band * setting.gain;
        }
        case kindNotch: {
            const auto out = stages[0].tick(setting.stages[0], x);
            return out.low + out.high;
        }
        case kindHp12: return stages[0].tick(setting.stages[0], x).high;
        case kindHp24: {
            const auto first = stages[0].tick(setting.stages[0], x).high;
            return stages[1].tick(setting.stages[1], first).high;
        }
        default: {
            auto sum = 0.0;
            for (std::size_t at = 0; at < 3; ++at)
                sum += stages[at].tick(setting.stages[at], x).band * setting.formantGains[at];
            return sum * setting.gain;
        }
        }
    }
};

// Past 1.5 a resonance rounds off rather than climbs.
double softLimit(const double x) noexcept
{
    const auto magnitude = std::abs(x);
    return magnitude <= 1.5 ? x : std::copysign(1.5 + std::tanh(magnitude - 1.5), x);
}

// A number from an integer, the same each time (the picture's noise).
double hashed(std::uint32_t n) noexcept
{
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return static_cast<double>(n >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
}

class ArgileEngine final : public Engine {
public:
    void prepare(const double sampleRate) override { rate_ = std::max(1.0, sampleRate); }

    void start(const Values& values, const int note, const float velocity, const double glideFrom, const double sampleRate) noexcept override
    {
        rate_ = std::max(1.0, sampleRate);
        target_ = static_cast<double>(note);
        pitch_ = glideFrom >= 0.0 && values[voicingGlide] > 0.0f ? glideFrom : target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        for (auto* envelope : { &amps_[0], &amps_[1], &filterEnvelopes_[0], &filterEnvelopes_[1] }) {
            envelope->stage = Envelope::Stage::idle;
            envelope->level = 0.0;
            envelope->start();
        }
        for (auto& lane : filters_)
            for (auto& side : lane)
                side.reset();
        elapsed_ = 0.0;
        lfoPhases_ = { 0.0, 0.0 };
        lfoHeld_ = { random(), random() };
        noteRandom_ = random();
        noiseLow_ = 0.0;
        for (auto& unison : subs_) {
            // Free-running oscillators: each note starts where they were.
            for (auto& phase : unison.phase)
                phase = 0.5 * (random() + 1.0);
            unison.flip = { 0, 0 };
            unison.noiseStep = { -1, -1 };
            unison.noiseHeld = { 0.0, 0.0 };
            unison.drift = random() * 0.5;
            unison.driftTarget = random();
        }
        previousGains_ = { 0.0, 0.0 };
        previousPans_ = { 1.0, 1.0, 1.0, 1.0 };
    }

    void retarget(const Values& values, const int note, const float velocity) noexcept override
    {
        target_ = static_cast<double>(note);
        if (values[voicingGlide] <= 0.0f)
            pitch_ = target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        if (amps_[0].stage == Envelope::Stage::release || amps_[1].stage == Envelope::Stage::release)
            for (auto* envelope : { &amps_[0], &amps_[1], &filterEnvelopes_[0], &filterEnvelopes_[1] })
                envelope->start();
    }

    void release() noexcept override
    {
        for (auto* envelope : { &amps_[0], &amps_[1], &filterEnvelopes_[0], &filterEnvelopes_[1] })
            envelope->release();
    }

    void kill() noexcept override
    {
        for (auto* envelope : { &amps_[0], &amps_[1], &filterEnvelopes_[0], &filterEnvelopes_[1] }) {
            envelope->stage = Envelope::Stage::idle;
            envelope->level = 0.0;
        }
    }

    [[nodiscard]] bool active() const noexcept override
    {
        return amps_[0].stage != Envelope::Stage::idle || amps_[1].stage != Envelope::Stage::idle;
    }

    [[nodiscard]] bool releasing() const noexcept override
    {
        const auto over = [](const Envelope& envelope) {
            return envelope.stage == Envelope::Stage::release || envelope.stage == Envelope::Stage::idle;
        };
        return active() && over(amps_[0]) && over(amps_[1]);
    }

    [[nodiscard]] double currentPitch() const noexcept override { return active() ? pitch_ : -1.0; }

    bool render(const Values& values, const prism::Context& context, float* left, float* right, const int count, const double sampleRate,
                const double expressionPitch, const double expressionGain) noexcept override
    {
        if (!active())
            return false;
        rate_ = std::max(1.0, sampleRate);
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto unit = [&v](const int index) { return std::clamp(v(index), 0.0, 1.0); };
        const auto choice = [&values](const int index, const int options) {
            return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(index)])), 0, options - 1);
        };
        constexpr int step = 16;

        // What holds for the whole block: the sources, and how they share the filters.
        std::array<int, 2> waves {};
        std::array<double, 2> ratios {};
        std::array<double, 2> levels {};
        std::array<double, 2> subLevels {};
        std::array<double, 2> toTwo {};
        for (std::size_t osc = 0; osc < 2; ++osc) {
            const auto base = kOscs[osc];
            waves[osc] = choice(base + oscWave, waveCount);
            ratios[osc] = std::pow(2.0, (12.0 * std::round(v(base + oscOctave)) + std::round(v(base + oscSemi)) + v(base + oscFine) / 100.0) / 12.0);
            levels[osc] = unit(base + oscLevel);
            subLevels[osc] = unit(base + oscSub);
            toTwo[osc] = unit(base + oscRoute);
        }
        const auto sync = v(osc2Sync) >= 0.5;
        const auto sweep = std::clamp(v(osc2Env), -1.0, 1.0) * 36.0;
        const auto noise = unit(noiseLevel);
        const auto noiseToTwo = unit(noiseRoute);
        // The noise's colour: below the middle darkened by a one-pole low-pass,
        // above it thinned by the matching high-pass; its loudness kept near.
        const auto colour = unit(noiseColour);
        const auto bright = colour > 0.5;
        const auto noiseHz = 100.0 * std::pow(2.0, bright ? (colour - 0.5) * 2.0 * 6.3 : colour * 2.0 * 7.6);
        const auto noiseCoefficient = 1.0 - std::exp(-2.0 * kPi * std::min(noiseHz, rate_ * 0.45) / rate_);
        const auto noiseVariance = bright ? 2.0 * (1.0 - noiseCoefficient) * (1.0 - noiseCoefficient) / (2.0 - noiseCoefficient)
                                          : noiseCoefficient / (2.0 - noiseCoefficient);
        const auto noiseGain = noise * std::pow(1.0 / std::max(1.0e-4, noiseVariance), 0.375);
        const auto voices = std::clamp(static_cast<int>(std::lround(v(unisonVoices))), 1, 4);
        const auto detune = v(unisonDetune);
        const auto spread = unit(unisonSpread);
        const auto drift = unit(driftAmount);
        const auto stereo = voices > 1 && spread > 1.0e-3;
        const std::size_t sides = stereo ? 2u : 1u;
        const auto unisonGain = 1.0 / std::sqrt(static_cast<double>(voices));
        const auto serial = unit(filterSerial);
        // Which filters have something to filter, which amplifiers are heard.
        const auto sentOne = (levels[0] + subLevels[0]) * (1.0 - toTwo[0]) + (levels[1] + subLevels[1]) * (1.0 - toTwo[1]) + noise * (1.0 - noiseToTwo);
        const auto sentTwo = (levels[0] + subLevels[0]) * toTwo[0] + (levels[1] + subLevels[1]) * toTwo[1] + noise * noiseToTwo;
        const auto heardOne = sentOne > 1.0e-6 && serial < 1.0 - 1.0e-6 && v(amp1Level) > 1.0e-6;
        const auto heardTwo = (sentTwo > 1.0e-6 || (sentOne > 1.0e-6 && serial > 1.0e-6)) && v(amp2Level) > 1.0e-6;
        const auto runOne = sentOne > 1.0e-6 && (heardOne || (heardTwo && serial > 1.0e-6));
        const auto runTwo = heardTwo;
        // An amplifier nobody hears ends with its note.
        if (heardOne != heardTwo) {
            auto& silent = amps_[heardOne ? 1u : 0u];
            if (silent.stage == Envelope::Stage::release) {
                silent.stage = Envelope::Stage::idle;
                silent.level = 0.0;
            }
        }
        // The filters.
        std::array<int, 2> kinds {};
        std::array<double, 2> knobs {}; // the cutoffs' knobs, in semitones
        std::array<double, 2> resonances {};
        std::array<double, 2> inputs {};
        std::array<double, 2> driveBacks {};
        std::array<double, 2> keys {};
        std::array<double, 2> envs {};
        std::array<double, 2> vowels {};
        for (std::size_t lane = 0; lane < 2; ++lane) {
            const auto base = kFilters[lane];
            kinds[lane] = choice(base + filterType, kindCount);
            knobs[lane] = 12.0 * std::log2(std::max(20.0, v(base + filterCutoff)));
            resonances[lane] = unit(base + filterResonance);
            const auto drive = unit(base + filterDrive);
            inputs[lane] = 0.5 * (1.0 + 4.0 * drive);
            driveBacks[lane] = 1.0 / (1.0 + 1.5 * drive);
            keys[lane] = unit(base + filterKey);
            envs[lane] = std::clamp(v(base + filterEnv), -1.0, 1.0);
            vowels[lane] = unit(base + filterVowel);
        }
        const auto kilohertz = 12.0 * std::log2(1'000.0);
        const auto velocityFilter = 1.0 - unit(filterVelocity) * (1.0 - velocity_);
        // The amplifiers.
        std::array<double, 2> ampKnobs {};
        std::array<double, 2> panKnobs {};
        std::array<double, 2> velocityLevels {};
        for (std::size_t lane = 0; lane < 2; ++lane) {
            const auto base = kAmps[lane];
            ampKnobs[lane] = unit(base + ampLevel);
            panKnobs[lane] = std::clamp(v(base + ampPan), -1.0, 1.0);
            velocityLevels[lane] = 1.0 - unit(base + ampVelocity) * (1.0 - velocity_);
        }
        // The envelopes' coefficients for a step of `step` samples.
        const auto coefficient = [this](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(step) / (std::max(1.0e-4, seconds * fraction) * rate_));
        };
        struct Steps {
            double attack, decay, sustain, release;
        };
        const auto stepsOf = [&](const int first) {
            return Steps { coefficient(v(first), 1.0 / 1.466), coefficient(v(first + 1), 1.0 / 4.6), unit(first + 2),
                           coefficient(v(first + 3), 1.0 / 4.6) };
        };
        const std::array<Steps, 2> ampSteps { stepsOf(amp1Attack), stepsOf(amp2Attack) };
        const std::array<Steps, 2> filterSteps { stepsOf(fenv1Attack), stepsOf(fenv2Attack) };
        // The LFOs: their paces, shapes, delays and where they send.
        const auto paceOf = [&](const int base) {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(base + lfoSync)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(base + lfoRate);
        };
        std::array<double, 2> paces {};
        std::array<int, 2> shapes {};
        std::array<double, 2> delays {};
        std::array<double, 2> pitchDepths {};
        std::array<double, 2> widthDepths {};
        std::array<std::array<double, 2>, 2> cutoffDepths {}; // [lfo][filter]
        std::array<double, 2> panDepths {};
        std::array<double, 2> ampDepths {};
        for (std::size_t lfo = 0; lfo < 2; ++lfo) {
            const auto base = kLfos[lfo];
            paces[lfo] = paceOf(base);
            shapes[lfo] = choice(base + lfoShape, 5);
            delays[lfo] = std::max(0.0, v(base + lfoDelay));
            const auto pitch = unit(base + lfoPitch);
            pitchDepths[lfo] = 12.0 * pitch * pitch;
            widthDepths[lfo] = 0.45 * unit(base + lfoPw);
            cutoffDepths[lfo] = { 36.0 * std::clamp(v(base + lfoCutoff1), -1.0, 1.0), 36.0 * std::clamp(v(base + lfoCutoff2), -1.0, 1.0) };
            panDepths[lfo] = unit(base + lfoPan);
            ampDepths[lfo] = unit(base + lfoAmp);
        }
        // The wheel's vibrato rides LFO 1 (up to a semitone).
        pitchDepths[0] += static_cast<double>(context.modWheel) * unit(modWheel);
        const std::array<double, 2> widthKnobs { v(osc1Pw), v(osc2Pw) };
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        const auto master = v(voicingMaster);

        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            const auto part = static_cast<double>(length) / static_cast<double>(step);
            // The slow things, once a step: envelopes, LFOs, glide, drift, the pitches and the filters.
            const std::array<double, 2> ampLevels { amps_[0].step(ampSteps[0].attack, ampSteps[0].decay, ampSteps[0].sustain, ampSteps[0].release, part),
                                                    amps_[1].step(ampSteps[1].attack, ampSteps[1].decay, ampSteps[1].sustain, ampSteps[1].release, part) };
            const std::array<double, 2> filterLevels {
                filterEnvelopes_[0].step(filterSteps[0].attack, filterSteps[0].decay, filterSteps[0].sustain, filterSteps[0].release, part),
                filterEnvelopes_[1].step(filterSteps[1].attack, filterSteps[1].decay, filterSteps[1].sustain, filterSteps[1].release, part)
            };
            if (!active())
                return false;
            const auto seconds = static_cast<double>(length) / rate_;
            elapsed_ += seconds;
            std::array<double, 2> lfo {};
            for (std::size_t index = 0; index < 2; ++index) {
                lfoPhases_[index] += paces[index] * seconds;
                if (lfoPhases_[index] >= 1.0) {
                    lfoPhases_[index] -= std::floor(lfoPhases_[index]);
                    lfoHeld_[index] = random();
                }
                const auto fade = delays[index] > 0.0 ? std::min(1.0, elapsed_ / delays[index]) : 1.0;
                lfo[index] = lfoWave(shapes[index], lfoPhases_[index], lfoHeld_[index]) * fade;
                showLfo(index, lfo[index], lfoPhases_[index]);
            }
            // The glide: the same time whatever the distance, at an even pace.
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            const auto vibrato = lfo[0] * pitchDepths[0] + lfo[1] * pitchDepths[1];
            const auto baseHz = 440.0 * std::pow(2.0, (pitch_ + expressionPitch + bend + vibrato - 69.0) / 12.0);
            // The second oscillator swept by the first filter envelope (a synced sweep).
            const auto sweepRatio = sweep != 0.0 ? std::pow(2.0, sweep * filterLevels[0] / 12.0) : 1.0;
            const auto widthShift = lfo[0] * widthDepths[0] + lfo[1] * widthDepths[1];
            const std::array<double, 2> widths { std::clamp(widthKnobs[0] + widthShift, 0.05, 0.95),
                                                 std::clamp(widthKnobs[1] + widthShift, 0.05, 0.95) };
            show(osc1Pw, widths[0], widthKnobs[0]);
            show(osc2Pw, widths[1], widthKnobs[1]);
            for (int voice = 0; voice < voices; ++voice) {
                auto& unison = subs_[static_cast<std::size_t>(voice)];
                unison.drift += (unison.driftTarget - unison.drift) * 0.02;
                if (random() > 0.985)
                    unison.driftTarget = random();
                const auto place = voices > 1 ? 2.0 * static_cast<double>(voice) / static_cast<double>(voices - 1) - 1.0 : 0.0;
                const auto cents = place * detune + unison.drift * drift * 8.0;
                const auto hz = baseHz * std::pow(2.0, cents / 1'200.0);
                unison.increment[0] = std::clamp(hz * ratios[0] / rate_, 0.0, 0.45);
                unison.increment[1] = std::clamp(hz * ratios[1] * sweepRatio / rate_, 0.0, 0.45);
                const auto angle = (place * spread + 1.0) * kPi * 0.25;
                unison.left = std::min(1.0, kRoot2 * std::cos(angle));
                unison.right = std::min(1.0, kRoot2 * std::sin(angle));
            }
            // The cutoffs: their knobs, the key, the drift; the envelopes (6
            // octaves), the velocity, the LFOs (3). The formant type moves its
            // vowel with what moves the others' cutoffs, its formants with the
            // knob and the key.
            const auto driftCut = noteRandom_ * drift * 2.0;
            std::array<FilterSetting, 2> settings {};
            for (std::size_t lane = 0; lane < 2; ++lane) {
                const auto base = kFilters[lane];
                const auto keyed = knobs[lane] + keys[lane] * (pitch_ - 60.0) + driftCut;
                const auto moved = envs[lane] * filterLevels[lane] * 72.0 * velocityFilter + lfo[0] * cutoffDepths[0][lane] + lfo[1] * cutoffDepths[1][lane];
                const auto runs = lane == 0 ? runOne : runTwo;
                if (kinds[lane] == kindFormant) {
                    const auto vowel = std::clamp(vowels[lane] + moved / 72.0, 0.0, 1.0);
                    const auto shift = std::clamp(std::pow(2.0, (keyed - kilohertz) / 24.0), 0.5, 2.0);
                    show(base + filterVowel, vowel, vowels[lane]);
                    show(base + filterCutoff, std::clamp(std::pow(2.0, keyed / 12.0), 20.0, 20'000.0), v(base + filterCutoff));
                    if (runs)
                        settings[lane] = formantSetting(vowel, shift, resonances[lane], rate_);
                } else {
                    const auto cutoff = std::clamp(std::pow(2.0, (keyed + moved) / 12.0), 20.0, std::min(20'000.0, rate_ * 0.45));
                    show(base + filterVowel, vowels[lane], vowels[lane]);
                    show(base + filterCutoff, cutoff, v(base + filterCutoff));
                    if (runs)
                        settings[lane] = settingFor(kinds[lane], cutoff, resonances[lane], rate_);
                }
            }
            if (!runOne)
                for (auto& side : filters_[0])
                    side.reset();
            if (!runTwo)
                for (auto& side : filters_[1])
                    side.reset();
            // The amplifiers: their envelopes, levels and velocities, the
            // tremolo, the pans as the LFOs move them.
            const auto panShift = lfo[0] * panDepths[0] + lfo[1] * panDepths[1];
            const auto tremolo = (1.0 - ampDepths[0] * 0.5 * (1.0 - lfo[0])) * (1.0 - ampDepths[1] * 0.5 * (1.0 - lfo[1]));
            const auto common = master * tremolo * expressionGain * kOutput;
            const std::array<double, 2> gains { heardOne ? ampLevels[0] * ampKnobs[0] * velocityLevels[0] * (1.0 - serial) * common : 0.0,
                                                heardTwo ? ampLevels[1] * ampKnobs[1] * velocityLevels[1] * common : 0.0 };
            std::array<double, 4> pans {}; // filter 1's left and right, filter 2's
            for (std::size_t lane = 0; lane < 2; ++lane) {
                const auto pan = std::clamp(panKnobs[lane] + panShift, -1.0, 1.0);
                show(kAmps[lane] + ampPan, pan, panKnobs[lane]);
                const auto angle = (pan + 1.0) * kPi * 0.25;
                pans[2 * lane] = std::min(1.0, kRoot2 * std::cos(angle));
                pans[2 * lane + 1] = std::min(1.0, kRoot2 * std::sin(angle));
            }
            showEnvelope(0, ampLevels[0]);
            showEnvelope(1, filterLevels[0]);
            showEnvelope(2, ampLevels[1]);
            showEnvelope(3, filterLevels[1]);
            // The gains and pans eased across the step: no steps heard on a quick attack.
            const auto gainsFrom = previousGains_;
            previousGains_ = gains;
            const auto pansFrom = previousPans_;
            previousPans_ = pans;
            for (int i = 0; i < length; ++i) {
                const auto t = static_cast<double>(i + 1) / static_cast<double>(length);
                std::array<double, 2> one {};
                std::array<double, 2> two {};
                for (int voice = 0; voice < voices; ++voice) {
                    auto& unison = subs_[static_cast<std::size_t>(voice)];
                    std::array<double, 2> sounds {};
                    for (std::size_t osc = 0; osc < 2; ++osc) {
                        if (levels[osc] > 0.0)
                            sounds[osc] += levels[osc] * waveOf(waves[osc], unison, osc, widths[osc]);
                        if (subLevels[osc] > 0.0)
                            sounds[osc] += subLevels[osc] * subOf(unison, osc);
                    }
                    const auto first = sounds[0] * (1.0 - toTwo[0]) + sounds[1] * (1.0 - toTwo[1]);
                    const auto second = sounds[0] * toTwo[0] + sounds[1] * toTwo[1];
                    if (stereo) {
                        one[0] += first * unison.left;
                        one[1] += first * unison.right;
                        two[0] += second * unison.left;
                        two[1] += second * unison.right;
                    } else {
                        one[0] += first;
                        two[0] += second;
                    }
                    advance(unison, sync);
                }
                const auto hiss = noiseGain > 0.0 ? colouredNoise(bright, noiseCoefficient) * noiseGain : 0.0;
                std::array<double, 2> outOne {};
                std::array<double, 2> outTwo {};
                for (std::size_t side = 0; side < sides; ++side) {
                    if (runOne) {
                        const auto toOne = one[side] * unisonGain + hiss * (1.0 - noiseToTwo);
                        outOne[side] = softLimit(filters_[0][side].process(settings[0], std::tanh(toOne * inputs[0]) * driveBacks[0]));
                    }
                    if (runTwo) {
                        // Filter 1's output joins filter 2's sources as loud as a source would.
                        const auto toSecond = two[side] * unisonGain + hiss * noiseToTwo + 2.0 * serial * outOne[side];
                        outTwo[side] = softLimit(filters_[1][side].process(settings[1], std::tanh(toSecond * inputs[1]) * driveBacks[1]));
                    }
                }
                if (!stereo) {
                    outOne[1] = outOne[0];
                    outTwo[1] = outTwo[0];
                }
                const auto gainOne = gainsFrom[0] + (gains[0] - gainsFrom[0]) * t;
                const auto gainTwo = gainsFrom[1] + (gains[1] - gainsFrom[1]) * t;
                const auto panAt = [&](const std::size_t at) { return pansFrom[at] + (pans[at] - pansFrom[at]) * t; };
                left[done + i] += static_cast<float>(outOne[0] * gainOne * panAt(0) + outTwo[0] * gainTwo * panAt(2));
                right[done + i] += static_cast<float>(outOne[1] * gainOne * panAt(1) + outTwo[1] * gainTwo * panAt(3));
            }
            for (auto& lane : filters_) {
                // Played in mono, the right side follows the left (ready if the unison opens).
                if (!stereo)
                    lane[1] = lane[0];
                for (auto& side : lane)
                    side.settle();
            }
            flushTiny(noiseLow_);
        }
        return active();
    }

private:
    struct Unison {
        std::array<double, 2> phase {};
        std::array<double, 2> increment {};
        // Each oscillator's sub: which half of its period it is in.
        std::array<int, 2> flip {};
        // The noise wave: its step in the period, its value held.
        std::array<int, 2> noiseStep { -1, -1 };
        std::array<double, 2> noiseHeld {};
        double drift {};
        double driftTarget {};
        double left { 1.0 };
        double right { 1.0 };
    };

    // One oscillator's wave now: a sine, a saw or a rectangle without
    // aliasing, or a noise that takes a new value sixteen times a period.
    double waveOf(const int wave, Unison& unison, const std::size_t osc, const double width) noexcept
    {
        const auto t = unison.phase[osc];
        switch (wave) {
        case waveSine: return std::sin(2.0 * kPi * t);
        case waveSaw: return oscillator(0, t, unison.increment[osc], width);
        case waveRectangle: return oscillator(1, t, unison.increment[osc], width);
        default: {
            const auto at = static_cast<int>(t * 16.0);
            if (at != unison.noiseStep[osc]) {
                unison.noiseStep[osc] = at;
                unison.noiseHeld[osc] = random();
            }
            return unison.noiseHeld[osc];
        }
        }
    }

    // A square an octave below its oscillator: half its pace, turning over
    // every other period.
    static double subOf(const Unison& unison, const std::size_t osc) noexcept
    {
        return oscillator(1, 0.5 * (unison.phase[osc] + static_cast<double>(unison.flip[osc])), 0.5 * unison.increment[osc], 0.5);
    }

    static void advance(Unison& unison, const bool sync) noexcept
    {
        unison.phase[0] += unison.increment[0];
        const auto wrapped = unison.phase[0] >= 1.0;
        if (wrapped) {
            unison.phase[0] -= 1.0;
            unison.flip[0] ^= 1;
        }
        unison.phase[1] += unison.increment[1];
        auto turned = unison.phase[1] >= 1.0;
        if (sync && wrapped) {
            // Hard sync: the second starts again with the first, as far into
            // its period as the first is into its own.
            unison.phase[1] = unison.phase[0] * (unison.increment[1] / std::max(1.0e-9, unison.increment[0]));
            turned = true;
        }
        unison.phase[1] -= std::floor(unison.phase[1]);
        if (turned)
            unison.flip[1] ^= 1;
    }

    double colouredNoise(const bool bright, const double coefficient) noexcept
    {
        const auto white = random();
        noiseLow_ += (white - noiseLow_) * coefficient;
        return bright ? white - noiseLow_ : noiseLow_;
    }

    // The pace a glide from here to the note takes to last its time.
    void aimGlide(const Values& values) noexcept
    {
        const auto seconds = static_cast<double>(values[voicingGlide]);
        glidePerSample_ = seconds > 0.0 ? std::abs(target_ - pitch_) / (seconds * rate_) : 0.0;
    }

    double random() noexcept
    {
        seed_ = seed_ * 1'664'525u + 1'013'904'223u;
        return static_cast<double>(seed_ >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }

    double rate_ { 48'000.0 };
    double target_ {};
    double pitch_ {};
    double velocity_ { 1.0 };
    double elapsed_ {};
    double noteRandom_ {};
    double noiseLow_ {};
    std::array<Envelope, 2> amps_ {};
    std::array<Envelope, 2> filterEnvelopes_ {};
    std::array<std::array<FilterState, 2>, 2> filters_ {}; // [filter][side]
    std::array<double, 2> lfoPhases_ {};
    std::array<double, 2> lfoHeld_ {};
    std::array<Unison, 4> subs_ {};
    std::array<double, 2> previousGains_ {};
    std::array<double, 4> previousPans_ { 1.0, 1.0, 1.0, 1.0 };
    double glidePerSample_ {};
    // Each voice its own draw: their drifts and phases apart.
    std::uint32_t seed_ { nextSeed() };
};

// One of Argile's filters at `hz` as its knobs set it.
std::complex<double> stageResponse(const int kind, const double cutoff, const double resonance, const double vowel, const double hz)
{
    using complex = std::complex<double>;
    const complex s(0.0, hz / std::max(1.0, cutoff));
    const auto k = stageDamping(kind, resonance);
    switch (kind) {
    case kindLp12: return 1.0 / (s * s + k * s + 1.0);
    case kindLp24: {
        const auto feedback = 4.2 * resonance;
        const auto pole = 1.0 + s;
        return (1.0 + 0.6 * feedback) / (pole * pole * pole * pole + feedback);
    }
    case kindBp6: return bandGain(resonance) * k * s / (s * s + k * s + 1.0);
    case kindBp12: {
        const auto band = k * s / (s * s + k * s + 1.0);
        return bandGain(resonance) * band * band;
    }
    case kindNotch: return (s * s + 1.0) / (s * s + k * s + 1.0);
    case kindHp12: return s * s / (s * s + k * s + 1.0);
    case kindHp24: return s * s / (s * s + 1.848 * s + 1.0) * (s * s / (s * s + k * s + 1.0));
    default: {
        const auto formants = formantsAt(vowel, formantShift(cutoff));
        complex sum;
        for (std::size_t at = 0; at < 3; ++at) {
            const complex f(0.0, hz / formants[at]);
            sum += kFormantGains[at] * k * f / (f * f + k * f + 1.0);
        }
        return sum * formantGain(k);
    }
    }
}

} // namespace

std::unique_ptr<Engine> makeArgile()
{
    return std::make_unique<ArgileEngine>();
}

std::vector<float> argilePicture(const Values& values, const std::string_view part, const int points)
{
    // One oscillator over two of its periods (the second, synced, over two of
    // the first's): its wave, its width, its sub.
    const auto index = part == "osc1" ? 0 : part == "osc2" ? 1 : -1;
    if (index < 0 || points < 2)
        return {};
    const auto v = [&values](const int at) { return static_cast<double>(values[static_cast<std::size_t>(at)]); };
    const auto base = kOscs[static_cast<std::size_t>(index)];
    const auto wave = std::clamp(static_cast<int>(std::lround(v(base + oscWave))), 0, waveCount - 1);
    const auto width = std::clamp(v(base + oscPw), 0.05, 0.95);
    const auto sub = std::clamp(v(base + oscSub), 0.0, 1.0);
    const auto offset = [&v](const int first) {
        return 12.0 * std::round(v(first + oscOctave)) + std::round(v(first + oscSemi)) + v(first + oscFine) / 100.0;
    };
    const auto synced = index == 1 && v(osc2Sync) >= 0.5;
    const auto ratio = synced ? std::pow(2.0, (offset(osc2Wave) - offset(osc1Wave)) / 12.0) : 1.0;
    // The second's periods begun in one of the first's.
    const auto turns = std::ceil(ratio - 1.0e-9);
    std::vector<float> result(static_cast<std::size_t>(points));
    for (int at = 0; at < points; ++at) {
        const auto phase = 2.0 * static_cast<double>(at) / static_cast<double>(points - 1);
        const auto period = std::floor(phase);
        const auto within = (phase - period) * ratio;
        const auto t = within - std::floor(within);
        const auto cycle = period * turns + std::floor(within);
        auto x = 0.0;
        switch (wave) {
        case waveSine: x = std::sin(2.0 * kPi * t); break;
        case waveSaw: x = 2.0 * t - 1.0; break;
        case waveRectangle: x = (t < width ? 1.0 : -1.0) - (2.0 * width - 1.0); break;
        default: x = hashed(static_cast<std::uint32_t>(cycle * 16.0 + std::floor(t * 16.0))); break;
        }
        if (sub > 0.0)
            x += sub * (0.5 * (t + std::fmod(cycle, 2.0)) < 0.5 ? 1.0 : -1.0);
        result[static_cast<std::size_t>(at)] = static_cast<float>(x);
    }
    return result;
}

double argileFilterDb(const Values& values, const double hz)
{
    // Both filters as the sources reach them (as the knobs set them): filter 1
    // heard as much as it is not sent on, filter 2 alone and after filter 1.
    const auto v = [&values](const int at) { return static_cast<double>(values[static_cast<std::size_t>(at)]); };
    const auto unit = [&v](const int at) { return std::clamp(v(at), 0.0, 1.0); };
    const auto stage = [&](const int base) {
        return stageResponse(std::clamp(static_cast<int>(std::lround(v(base + filterType))), 0, kindCount - 1), v(base + filterCutoff),
                             unit(base + filterResonance), unit(base + filterVowel), hz);
    };
    const auto one = stage(filter1Type);
    const auto two = stage(filter2Type);
    auto sentOne = unit(noiseLevel) * (1.0 - unit(noiseRoute));
    auto sentTwo = unit(noiseLevel) * unit(noiseRoute);
    for (const auto base : kOscs) {
        const auto level = unit(base + oscLevel) + unit(base + oscSub);
        sentOne += level * (1.0 - unit(base + oscRoute));
        sentTwo += level * unit(base + oscRoute);
    }
    const auto total = sentOne + sentTwo;
    const auto shareOne = total > 1.0e-9 ? sentOne / total : 1.0;
    const auto shareTwo = total > 1.0e-9 ? sentTwo / total : 0.0;
    const auto serial = unit(filterSerial);
    const auto heardOne = unit(amp1Level) > 1.0e-6 ? 1.0 : 0.0;
    const auto heardTwo = unit(amp2Level) > 1.0e-6 ? 1.0 : 0.0;
    const auto response = heardOne * shareOne * (1.0 - serial) * one + heardTwo * two * (shareTwo + shareOne * serial * one);
    return 20.0 * std::log10(std::max(1.0e-9, std::abs(response)));
}

} // namespace cydaw::engine::synth::detail
