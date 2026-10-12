// Colosse: a big modern synth of the synth family. Two wavetable
// oscillators (eight sets of tables, each a row of eight frames to move
// across, read through eight ways of bending their phase), a sine that
// phase-modulates them, a sub, a coloured noise, up to eight unison voices
// across the stereo; two filters of seven kinds, in series or side by side; a
// saturation of five kinds; three envelopes, two LFOs, a sixteen-step
// sequence, and six modulation slots that send any of them anywhere.

#include "SynthEngines.h"
#include "SynthFilters.h"
#include "SynthParts.h"
#include "SynthTables.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

// Colosse's settings after the shared voicing, in its table's order.
enum Colosse : int {
    osc1Table = voicingFieldCount,
    osc1Position,
    osc1Mode,
    osc1Warp,
    osc1Octave,
    osc1Semi,
    osc1Fine,
    osc1Level,
    osc2Table,
    osc2Position,
    osc2Mode,
    osc2Warp,
    osc2Octave,
    osc2Semi,
    osc2Fine,
    osc2Level,
    pmRatio,
    pmOsc1,
    pmOsc2,
    subLevel,
    noiseColour,
    noiseLevel,
    unisonVoices,
    unisonDetune,
    unisonSpread,
    f1Type,
    f1Cutoff,
    f1Resonance,
    f2Type,
    f2Cutoff,
    f2Resonance,
    filterRouting,
    filterBlend,
    filterKey,
    filterVelocity,
    driveType,
    driveAmount,
    driveMix,
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease,
    ampVelocity,
    env2Attack,
    env2Decay,
    env2Sustain,
    env2Release,
    env3Attack,
    env3Decay,
    env3Sustain,
    env3Release,
    lfo1Shape,
    lfo1Rate,
    lfo1Sync,
    lfo2Shape,
    lfo2Rate,
    lfo2Sync,
    perfSteps,
    perfSync,
    perfRate,
    perfSmooth,
    perfStep1,
    perfStepLast = perfStep1 + 15,
    mod1Source,
    modLast = mod1Source + 3 * 6 - 1,
    colosseCount
};
static_assert(colosseCount == 101);

constexpr int slotCount = 6;
constexpr int stepCount = 16;

// Where a modulation slot reads, where it sends.
enum Source : int { srcNone, srcEnv2, srcEnv3, srcLfo1, srcLfo2, srcSequence, srcVelocity, srcWheel, srcKey, srcRandom, srcPressure, sourceCount };
enum Destination : int {
    dstNone,
    dstPitch,
    dstPosition1,
    dstPosition2,
    dstWarp1,
    dstWarp2,
    dstOscMix,
    dstPm,
    dstCutoff1,
    dstCutoff2,
    dstCutoffs,
    dstResonance,
    dstBlend,
    dstDrive,
    dstNoise,
    dstVolume,
    dstPan,
    dstDetune,
    destinationCount
};

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices kTables { { "Classique", "Classic" }, { "Harmonique", "Harmonic" }, { "Voix", "Voices" }, { "Métal", "Metal" },
                        { "Plié", "Folded" },        { "Numérique", "Digital" },  { "Dents", "Teeth" }, { "Synchro", "Sync" } };
const Choices kModes { { "Direct", "Plain" },     { "Courbe", "Bend" },  { "Miroir", "Mirror" },        { "Formant", "Formant" },
                       { "Synchro", "Sync" },     { "Paliers", "Steps" }, { "Asymétrie", "Asymmetry" }, { "Pli", "Fold" } };
const Choices kFilters { { "Ladder", "Ladder" },          { "Passe-bas", "Low-pass" }, { "Passe-bande", "Band-pass" },
                         { "Passe-haut", "High-pass" },   { "Peigne", "Comb" },        { "Voyelle", "Vowel" },
                         { "Hurlement", "Scream" } };
const Choices kDrives { { "Douce", "Soft" }, { "Dure", "Hard" }, { "Plieuse", "Fold" }, { "Réducteur", "Crush" }, { "Redresseur", "Rectify" } };
const Choices kLfoShapes { { "Sinus", "Sine" },  { "Triangle", "Triangle" }, { "Dent de scie", "Saw" },
                           { "Carré", "Square" }, { "Aléatoire", "Random" } };
const Choices kSources { { "Aucune", "None" },        { "Env 2", "Env 2" },        { "Env 3", "Env 3" },   { "LFO 1", "LFO 1" },
                         { "LFO 2", "LFO 2" },        { "Séquence", "Sequence" },  { "Vélocité", "Velocity" },
                         { "Molette", "Mod wheel" },  { "Clavier", "Key" },        { "Aléatoire", "Random" },
                         { "Pression", "Pressure" } };
const Choices kDestinations { { "Aucune", "None" },
                              { "Hauteur", "Pitch" },
                              { "Osc 1 · position", "Osc 1 · position" },
                              { "Osc 2 · position", "Osc 2 · position" },
                              { "Osc 1 · déformation", "Osc 1 · warp" },
                              { "Osc 2 · déformation", "Osc 2 · warp" },
                              { "Mélange osc", "Osc mix" },
                              { "Modulation de phase", "Phase modulation" },
                              { "Filtre 1 · fréquence", "Filter 1 · cutoff" },
                              { "Filtre 2 · fréquence", "Filter 2 · cutoff" },
                              { "Filtres · fréquence", "Filters · cutoff" },
                              { "Résonance", "Resonance" },
                              { "Mélange filtres", "Filter blend" },
                              { "Saturation", "Drive" },
                              { "Bruit", "Noise" },
                              { "Volume", "Volume" },
                              { "Panoramique", "Pan" },
                              { "Désaccord", "Detune" } };
const Choices kRatios { { "1/2", "1/2" }, { "1", "1" }, { "2", "2" }, { "3", "3" }, { "4", "4" }, { "5", "5" }, { "7", "7" }, { "8", "8" } };
constexpr std::array<double, 8> kRatioValues { 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 7.0, 8.0 };

} // namespace

std::vector<Section> colosseSections()
{
    return { { "VOIX", "VOICES", 0xffb8c0d0u },         { "OSCILLATEURS", "OSCILLATORS", 0xff4fd1ffu },
             { "FILTRES", "FILTERS", 0xffff5e7au },     { "ENVELOPPES", "ENVELOPES", 0xff7dffb0u },
             { "LFO", "LFO", 0xffb98cffu },             { "SÉQUENCE", "SEQUENCE", 0xffffc14du },
             { "MODULATIONS", "MODULATIONS", 0xffff8ad8u } };
}

std::vector<Parameter> colosseParameters()
{
    Table table;
    addVoicing(table, 16, 0.7f);
    table.section = 1;
    for (int index = 1; index <= 2; ++index) {
        const auto prefix = "osc" + std::to_string(index);
        const auto french = "Osc " + std::to_string(index) + " · ";
        const auto english = "Osc " + std::to_string(index) + " · ";
        table.choice(prefix + ".table", french + "table", english + "table", kTables, index == 1 ? 0 : 1);
        table.add(prefix + ".position", french + "position", english + "position", {}, 0.0f, 1.0f, index == 1 ? 0.57f : 0.45f);
        table.choice(prefix + ".mode", french + "mode", english + "mode", kModes, 0);
        table.add(prefix + ".warp", french + "déformation", english + "warp", {}, 0.0f, 1.0f, 0.0f);
        table.whole(prefix + ".octave", french + "octave", english + "octave", {}, -3, 3, 0);
        table.whole(prefix + ".semi", french + "demi-tons", english + "semitones", "st", -12, 12, 0);
        table.add(prefix + ".fine", french + "fin", english + "fine", "ct", -50.0f, 50.0f, index == 1 ? 0.0f : 7.0f);
        table.add(prefix + ".level", french + "niveau", english + "level", {}, 0.0f, 1.0f, index == 1 ? 0.8f : 0.45f);
    }
    table.choice("pm.ratio", "Modulateur · rapport", "Modulator · ratio", kRatios, 1);
    table.add("pm.osc1", "Modulation de phase → osc 1", "Phase modulation → osc 1", {}, 0.0f, 1.0f, 0.0f);
    table.add("pm.osc2", "Modulation de phase → osc 2", "Phase modulation → osc 2", {}, 0.0f, 1.0f, 0.0f);
    table.add("sub.level", "Sub", "Sub", {}, 0.0f, 1.0f, 0.0f);
    table.choice("noise.colour", "Bruit · couleur", "Noise · colour",
                 { { "Blanc", "White" }, { "Rose", "Pink" }, { "Crépitement", "Crackle" }, { "Souffle", "Breath" } }, 0);
    table.add("noise.level", "Bruit", "Noise", {}, 0.0f, 1.0f, 0.0f);
    table.whole("unison.voices", "Unisson", "Unison", {}, 1, 8, 1);
    table.add("unison.detune", "Unisson · désaccord", "Unison · detune", "ct", 0.0f, 50.0f, 15.0f);
    table.add("unison.spread", "Unisson · largeur", "Unison · spread", {}, 0.0f, 1.0f, 0.8f);
    table.section = 2;
    table.choice("f1.type", "Filtre 1 · type", "Filter 1 · type", kFilters, 0);
    table.add("f1.cutoff", "Filtre 1 · fréquence", "Filter 1 · cutoff", "Hz", 20.0f, 20'000.0f, 2'200.0f, Kind::continuous, true);
    table.add("f1.resonance", "Filtre 1 · résonance", "Filter 1 · resonance", {}, 0.0f, 1.0f, 0.2f);
    table.choice("f2.type", "Filtre 2 · type", "Filter 2 · type", kFilters, 2);
    table.add("f2.cutoff", "Filtre 2 · fréquence", "Filter 2 · cutoff", "Hz", 20.0f, 20'000.0f, 1'200.0f, Kind::continuous, true);
    table.add("f2.resonance", "Filtre 2 · résonance", "Filter 2 · resonance", {}, 0.0f, 1.0f, 0.3f);
    table.choice("filter.routing", "Filtres · montage", "Filters · routing", { { "Série", "Serial" }, { "Parallèle", "Parallel" } }, 0);
    table.add("filter.blend", "Filtres · mélange", "Filters · blend", {}, 0.0f, 1.0f, 0.0f);
    table.add("filter.key", "Filtres · suivi du clavier", "Filters · key tracking", {}, 0.0f, 1.0f, 0.4f);
    table.add("filter.velocity", "Filtres · vélocité", "Filters · velocity", {}, 0.0f, 1.0f, 0.2f);
    table.choice("drive.type", "Saturation · type", "Drive · type", kDrives, 0);
    table.add("drive.amount", "Saturation", "Drive", {}, 0.0f, 1.0f, 0.0f);
    table.add("drive.mix", "Saturation · mélange", "Drive · mix", {}, 0.0f, 1.0f, 1.0f);
    table.section = 3;
    table.add("amp.attack", "Volume · attaque", "Amp · attack", "s", 0.001f, 10.0f, 0.003f, Kind::continuous, true);
    table.add("amp.decay", "Volume · déclin", "Amp · decay", "s", 0.005f, 10.0f, 0.6f, Kind::continuous, true);
    table.add("amp.sustain", "Volume · maintien", "Amp · sustain", {}, 0.0f, 1.0f, 0.8f);
    table.add("amp.release", "Volume · relâchement", "Amp · release", "s", 0.005f, 12.0f, 0.3f, Kind::continuous, true);
    table.add("amp.velocity", "Volume · vélocité", "Amp · velocity", {}, 0.0f, 1.0f, 0.5f);
    for (int index = 2; index <= 3; ++index) {
        const auto prefix = "env" + std::to_string(index);
        const auto name = "Env " + std::to_string(index) + " · ";
        table.add(prefix + ".attack", name + "attaque", name + "attack", "s", 0.001f, 10.0f, index == 2 ? 0.002f : 0.4f, Kind::continuous, true);
        table.add(prefix + ".decay", name + "déclin", name + "decay", "s", 0.005f, 10.0f, index == 2 ? 0.5f : 1.5f, Kind::continuous, true);
        table.add(prefix + ".sustain", name + "maintien", name + "sustain", {}, 0.0f, 1.0f, index == 2 ? 0.2f : 0.5f);
        table.add(prefix + ".release", name + "relâchement", name + "release", "s", 0.005f, 12.0f, index == 2 ? 0.4f : 1.0f, Kind::continuous, true);
    }
    table.section = 4;
    table.choice("lfo1.shape", "LFO 1 · forme", "LFO 1 · shape", kLfoShapes, 0);
    table.add("lfo1.rate", "LFO 1 · vitesse", "LFO 1 · rate", "Hz", 0.05f, 30.0f, 4.0f, Kind::continuous, true);
    table.whole("lfo1.sync", "LFO 1 · synchro", "LFO 1 · sync", {}, 0, 12, 0);
    table.choice("lfo2.shape", "LFO 2 · forme", "LFO 2 · shape", kLfoShapes, 1);
    table.add("lfo2.rate", "LFO 2 · vitesse", "LFO 2 · rate", "Hz", 0.05f, 30.0f, 0.5f, Kind::continuous, true);
    table.whole("lfo2.sync", "LFO 2 · synchro", "LFO 2 · sync", {}, 0, 12, 0);
    table.section = 5;
    table.whole("perf.steps", "Séquence · pas", "Sequence · steps", {}, 1, stepCount, 8);
    table.whole("perf.sync", "Séquence · synchro", "Sequence · sync", {}, 0, 12, 3);
    table.add("perf.rate", "Séquence · vitesse", "Sequence · rate", "Hz", 0.1f, 30.0f, 4.0f, Kind::continuous, true);
    table.add("perf.smooth", "Séquence · lissage", "Sequence · smoothing", {}, 0.0f, 1.0f, 0.1f);
    static constexpr std::array<float, stepCount> pattern { 1.0f, 0.0f, 0.5f, 0.0f, 0.75f, 0.0f, 0.25f, 0.0f,
                                                            1.0f, 0.0f, 0.5f, 0.0f, 0.75f, 0.0f, 0.25f, -0.5f };
    for (int step = 0; step < stepCount; ++step)
        table.add("perf.step" + std::to_string(step + 1), "Séquence · pas " + std::to_string(step + 1),
                  "Sequence · step " + std::to_string(step + 1), {}, -1.0f, 1.0f, pattern[static_cast<std::size_t>(step)]);
    table.section = 6;
    for (int slot = 1; slot <= slotCount; ++slot) {
        const auto prefix = "mod" + std::to_string(slot);
        const auto name = "Mod " + std::to_string(slot) + " · ";
        table.choice(prefix + ".source", name + "source", name + "source", kSources, slot == 1 ? srcEnv2 : srcNone);
        table.choice(prefix + ".dest", name + "destination", name + "destination", kDestinations, slot == 1 ? dstCutoffs : dstNone);
        table.add(prefix + ".amount", name + "quantité", name + "amount", {}, -1.0f, 1.0f, slot == 1 ? 0.35f : 0.0f);
    }
    return table.parameters;
}

namespace {

class ColosseEngine final : public Engine {
public:
    void prepare(const double sampleRate) override
    {
        rate_ = std::max(1.0, sampleRate);
        for (auto& filter : stages_)
            for (auto& stage : filter)
                stage.prepare(rate_);
    }

    void start(const Values& values, const int note, const float velocity, const double glideFrom, const double sampleRate) noexcept override
    {
        rate_ = std::max(1.0, sampleRate);
        target_ = static_cast<double>(note);
        pitch_ = glideFrom >= 0.0 && values[voicingGlide] > 0.0f ? glideFrom : target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        for (auto* envelope : { &amp_, &env2_, &env3_ }) {
            envelope->stage = Envelope::Stage::idle;
            envelope->level = 0.0;
            envelope->start();
        }
        for (auto& filter : stages_)
            for (auto& stage : filter)
                stage.clear();
        dcIn_ = {};
        dcOut_ = {};
        held_ = {};
        holdCount_ = 0;
        pink_ = {};
        elapsed_ = 0.0;
        lfoPhases_ = { 0.0, 0.0 };
        lfoHeld_ = { random(), random() };
        noteRandom_ = random();
        sequence_ = 0.0;
        for (auto& sub : subs_) {
            for (auto& phase : sub.phase)
                phase = 0.5 * (random() + 1.0);
            sub.modulator = 0.0;
            sub.sub = 0.0;
        }
        previousLevel_ = 0.0;
        previousPan_ = { 1.0, 1.0 };
    }

    void retarget(const Values& values, const int note, const float velocity) noexcept override
    {
        target_ = static_cast<double>(note);
        if (values[voicingGlide] <= 0.0f)
            pitch_ = target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        if (amp_.stage == Envelope::Stage::release)
            for (auto* envelope : { &amp_, &env2_, &env3_ })
                envelope->start();
    }

    void release() noexcept override
    {
        amp_.release();
        env2_.release();
        env3_.release();
    }

    void kill() noexcept override
    {
        for (auto* envelope : { &amp_, &env2_, &env3_ }) {
            envelope->stage = Envelope::Stage::idle;
            envelope->level = 0.0;
        }
    }

    [[nodiscard]] bool active() const noexcept override { return amp_.stage != Envelope::Stage::idle; }
    [[nodiscard]] bool releasing() const noexcept override { return amp_.stage == Envelope::Stage::release; }
    [[nodiscard]] double currentPitch() const noexcept override { return active() ? pitch_ : -1.0; }

    bool render(const Values& values, const prism::Context& context, float* left, float* right, const int count, const double sampleRate,
                const double expressionPitch, const double expressionGain) noexcept override
    {
        if (!active())
            return false;
        rate_ = std::max(1.0, sampleRate);
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto choice = [&values](const int index, const int options) {
            return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(index)])), 0, options - 1);
        };
        constexpr int step = 16;
        const std::array<int, 2> sets { choice(osc1Table, tables::sets), choice(osc2Table, tables::sets) };
        const std::array<int, 2> modes { choice(osc1Mode, 8), choice(osc2Mode, 8) };
        const std::array<double, 2> offsets { 12.0 * std::round(v(osc1Octave)) + std::round(v(osc1Semi)) + v(osc1Fine) / 100.0,
                                              12.0 * std::round(v(osc2Octave)) + std::round(v(osc2Semi)) + v(osc2Fine) / 100.0 };
        const auto ratio = kRatioValues[static_cast<std::size_t>(choice(pmRatio, 8))];
        const auto voices = std::clamp(static_cast<int>(std::lround(v(unisonVoices))), 1, 8);
        const auto noiseKind = choice(noiseColour, 4);
        const std::array<int, 2> types { choice(f1Type, 7), choice(f2Type, 7) };
        const auto parallel = choice(filterRouting, 2) == 1;
        const auto driveKind = choice(driveType, 5);
        const auto velocityLevel = 1.0 - v(ampVelocity) * (1.0 - velocity_);
        const auto master = v(voicingMaster);
        const auto coefficient = [this](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(step) / (std::max(1.0e-4, seconds * fraction) * rate_));
        };
        struct Steps {
            double attack, decay, sustain, release;
        };
        const auto stepsOf = [&](const int attack) {
            return Steps { coefficient(v(attack), 1.0 / 1.466), coefficient(v(attack + 1), 1.0 / 4.6), v(attack + 2),
                           coefficient(v(attack + 3), 1.0 / 4.6) };
        };
        const auto ampSteps = stepsOf(ampAttack);
        const auto env2Steps = stepsOf(env2Attack);
        const auto env3Steps = stepsOf(env3Attack);
        const auto lfoRate = [&](const int rateIndex, const int syncIndex) {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(syncIndex)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(rateIndex);
        };
        const std::array<double, 2> lfoRates { lfoRate(lfo1Rate, lfo1Sync), lfoRate(lfo2Rate, lfo2Sync) };
        const std::array<int, 2> lfoShapes { choice(lfo1Shape, 5), choice(lfo2Shape, 5) };
        const auto sequenceRate = lfoRate(perfRate, perfSync);
        const auto sequenceLength = std::clamp(static_cast<int>(std::lround(v(perfSteps))), 1, stepCount);
        const auto smooth = v(perfSmooth);
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        std::array<int, slotCount> slotSources {};
        std::array<int, slotCount> slotDestinations {};
        std::array<double, slotCount> slotAmounts {};
        for (int slot = 0; slot < slotCount; ++slot) {
            slotSources[static_cast<std::size_t>(slot)] = choice(mod1Source + 3 * slot, sourceCount);
            slotDestinations[static_cast<std::size_t>(slot)] = choice(mod1Source + 3 * slot + 1, destinationCount);
            slotAmounts[static_cast<std::size_t>(slot)] = v(mod1Source + 3 * slot + 2);
        }

        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            // The slow things once a step: envelopes, LFOs, the sequence, the glide, the routings.
            const auto ampLevel = amp_.step(ampSteps.attack, ampSteps.decay, ampSteps.sustain, ampSteps.release);
            const auto env2Level = env2_.step(env2Steps.attack, env2Steps.decay, env2Steps.sustain, env2Steps.release);
            const auto env3Level = env3_.step(env3Steps.attack, env3Steps.decay, env3Steps.sustain, env3Steps.release);
            if (!active())
                return false;
            const auto seconds = static_cast<double>(length) / rate_;
            elapsed_ += seconds;
            std::array<double, 2> lfo {};
            for (std::size_t index = 0; index < 2; ++index) {
                lfoPhases_[index] += lfoRates[index] * seconds;
                if (lfoPhases_[index] >= 1.0) {
                    lfoPhases_[index] -= std::floor(lfoPhases_[index]);
                    lfoHeld_[index] = random();
                }
                lfo[index] = lfoWave(lfoShapes[index], lfoPhases_[index], lfoHeld_[index]);
            }
            // The sequence: its step held, then eased into the next over the
            // end of it (as long as the smoothing says).
            sequence_ += sequenceRate * seconds;
            const auto position = sequence_ - std::floor(sequence_ / sequenceLength) * sequenceLength;
            const auto index = std::clamp(static_cast<int>(position), 0, sequenceLength - 1);
            const auto next = (index + 1) % sequenceLength;
            const auto within = position - static_cast<double>(index);
            const auto current = v(perfStep1 + index);
            const auto after = v(perfStep1 + next);
            const auto ease = smooth > 1.0e-3 ? std::clamp((within - (1.0 - smooth)) / smooth, 0.0, 1.0) : 0.0;
            const auto sequenceValue = current + (after - current) * ease;
            const std::array<double, sourceCount> sources { 0.0,
                                                            env2Level,
                                                            env3Level,
                                                            lfo[0],
                                                            lfo[1],
                                                            sequenceValue,
                                                            velocity_,
                                                            static_cast<double>(context.modWheel),
                                                            std::clamp((target_ - 60.0) / 24.0, -1.0, 1.0),
                                                            noteRandom_,
                                                            static_cast<double>(context.aftertouch) };
            std::array<double, destinationCount> mods {};
            for (int slot = 0; slot < slotCount; ++slot)
                mods[static_cast<std::size_t>(slotDestinations[static_cast<std::size_t>(slot)])]
                    += sources[static_cast<std::size_t>(slotSources[static_cast<std::size_t>(slot)])] * slotAmounts[static_cast<std::size_t>(slot)];
            mods[dstNone] = 0.0;
            // The glide: the same time whatever the distance, at an even pace.
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            const auto basePitch = pitch_ + expressionPitch + bend + 24.0 * mods[dstPitch];
            const std::array<double, 2> positions { std::clamp(v(osc1Position) + mods[dstPosition1], 0.0, 1.0),
                                                    std::clamp(v(osc2Position) + mods[dstPosition2], 0.0, 1.0) };
            const std::array<double, 2> warps { std::clamp(v(osc1Warp) + mods[dstWarp1], 0.0, 1.0),
                                                std::clamp(v(osc2Warp) + mods[dstWarp2], 0.0, 1.0) };
            const auto balance = std::clamp(mods[dstOscMix], -1.0, 1.0);
            const std::array<double, 2> levels { v(osc1Level) * std::min(1.0, 1.0 - balance), v(osc2Level) * std::min(1.0, 1.0 + balance) };
            const std::array<double, 2> pm { std::clamp(v(pmOsc1) + mods[dstPm], 0.0, 1.0) * 0.35,
                                             std::clamp(v(pmOsc2) + mods[dstPm], 0.0, 1.0) * 0.35 };
            const auto detune = v(unisonDetune) * std::clamp(1.0 + mods[dstDetune], 0.0, 3.0);
            const auto spread = v(unisonSpread);
            const auto noise = std::clamp(v(noiseLevel) + mods[dstNoise], 0.0, 1.0);
            const auto sub = v(subLevel);
            for (int voice = 0; voice < voices; ++voice) {
                auto& unison = subs_[static_cast<std::size_t>(voice)];
                const auto offset = voices > 1 ? 2.0 * static_cast<double>(voice) / static_cast<double>(voices - 1) - 1.0 : 0.0;
                const auto note = basePitch + offset * detune / 100.0;
                const auto base = 440.0 * std::pow(2.0, (note - 69.0) / 12.0) / rate_;
                for (std::size_t osc = 0; osc < 2; ++osc) {
                    unison.increment[osc] = std::min(0.45, base * std::pow(2.0, offsets[osc] / 12.0));
                    unison.level[osc] = tables::levelFor(unison.increment[osc], tables::warpStretch(modes[osc], warps[osc]) * (1.0 + 4.0 * pm[osc]));
                }
                unison.modulatorIncrement = std::min(0.45, base * ratio);
                unison.subIncrement = std::min(0.45, base * 0.5);
                const auto pan = std::clamp(offset * spread, -1.0, 1.0);
                unison.left = std::cos((pan + 1.0) * kPi * 0.25);
                unison.right = std::sin((pan + 1.0) * kPi * 0.25);
            }
            // The filters: their cutoffs as the knobs, the key, the velocity and the routings move them.
            const auto keyShift = v(filterKey) * (target_ - 60.0) + v(filterVelocity) * (velocity_ - 1.0) * 36.0;
            const auto resonanceShift = mods[dstResonance];
            const std::array<double, 2> cutoffs {
                std::clamp(std::pow(2.0, (12.0 * std::log2(std::max(20.0, v(f1Cutoff))) + keyShift + 72.0 * (mods[dstCutoff1] + mods[dstCutoffs])) / 12.0),
                           20.0, std::min(20'000.0, rate_ * 0.45)),
                std::clamp(std::pow(2.0, (12.0 * std::log2(std::max(20.0, v(f2Cutoff))) + keyShift + 72.0 * (mods[dstCutoff2] + mods[dstCutoffs])) / 12.0),
                           20.0, std::min(20'000.0, rate_ * 0.45))
            };
            const std::array<double, 2> resonances { std::clamp(v(f1Resonance) + resonanceShift, 0.0, 1.0),
                                                     std::clamp(v(f2Resonance) + resonanceShift, 0.0, 1.0) };
            std::array<MultiFilter::Coefficients, 2> filters {};
            for (std::size_t filter = 0; filter < 2; ++filter)
                filters[filter] = MultiFilter::coefficients(types[filter], cutoffs[filter], resonances[filter], rate_, stages_[filter][0].combSize());
            const auto blend = std::clamp(v(filterBlend) + mods[dstBlend], 0.0, 1.0);
            const auto drive = std::clamp(v(driveAmount) + mods[dstDrive], 0.0, 1.0);
            const auto driveMixed = v(driveMix);
            const auto pan = std::clamp(mods[dstPan], -1.0, 1.0);
            const std::array<double, 2> panGains { std::min(1.0, 1.0 - pan), std::min(1.0, 1.0 + pan) };
            const auto level = ampLevel * velocityLevel * master * std::clamp(1.0 + mods[dstVolume], 0.0, 2.0) * expressionGain * 0.5
                               / std::sqrt(static_cast<double>(voices));
            const auto from = previousLevel_;
            previousLevel_ = level;
            const auto panFrom = previousPan_;
            previousPan_ = panGains;
            for (int i = 0; i < length; ++i) {
                const auto t = static_cast<double>(i + 1) / static_cast<double>(length);
                const auto gain = from + (level - from) * t;
                auto sumLeft = 0.0;
                auto sumRight = 0.0;
                for (int voice = 0; voice < voices; ++voice) {
                    auto& unison = subs_[static_cast<std::size_t>(voice)];
                    const auto modulator = std::sin(2.0 * kPi * unison.modulator);
                    auto mixed = 0.0;
                    for (std::size_t osc = 0; osc < 2; ++osc) {
                        if (levels[osc] <= 0.0)
                            continue;
                        mixed += levels[osc]
                                 * tables::sample(sets[osc], positions[osc], modes[osc], warps[osc], unison.level[osc],
                                                    unison.phase[osc] + pm[osc] * modulator);
                    }
                    if (sub > 0.0)
                        mixed += sub * std::sin(2.0 * kPi * unison.sub);
                    sumLeft += mixed * unison.left;
                    sumRight += mixed * unison.right;
                    for (std::size_t osc = 0; osc < 2; ++osc) {
                        unison.phase[osc] += unison.increment[osc];
                        unison.phase[osc] -= std::floor(unison.phase[osc]);
                    }
                    unison.modulator += unison.modulatorIncrement;
                    unison.modulator -= std::floor(unison.modulator);
                    unison.sub += unison.subIncrement;
                    unison.sub -= std::floor(unison.sub);
                }
                if (noise > 0.0) {
                    sumLeft += noiseSample(noiseKind, 0) * noise * 0.7;
                    sumRight += noiseSample(noiseKind, 1) * noise * 0.7;
                }
                std::array<double, 2> channel { sumLeft, sumRight };
                for (std::size_t side = 0; side < 2; ++side) {
                    auto x = channel[side];
                    const auto one = stages_[0][side].process(types[0], filters[0], x);
                    if (blend <= 1.0e-4)
                        x = one;
                    else if (parallel)
                        x = one + (stages_[1][side].process(types[1], filters[1], x) - one) * blend;
                    else
                        x = one + (stages_[1][side].process(types[1], filters[1], one) - one) * blend;
                    if (drive > 1.0e-4)
                        x += (saturate(driveKind, drive, x, side) - x) * driveMixed;
                    // No offset left by a rectifier or a fold.
                    const auto blocked = x - dcIn_[side] + 0.995 * dcOut_[side];
                    dcIn_[side] = x;
                    dcOut_[side] = blocked;
                    const auto panned = panFrom[side] + (panGains[side] - panFrom[side]) * t;
                    channel[side] = blocked * gain * panned;
                }
                left[done + i] += static_cast<float>(channel[0]);
                right[done + i] += static_cast<float>(channel[1]);
            }
        }
        return active();
    }

private:
    double saturate(const int kind, const double amount, const double x, const std::size_t side) noexcept
    {
        const auto k = 1.0 + 9.0 * amount;
        switch (kind) {
        case 0: return std::tanh(k * x) / std::tanh(k * 0.5) * 0.5;
        case 1: return std::clamp(k * x, -1.0, 1.0) / std::min(1.0, k * 0.5) * 0.5;
        case 2: return std::sin(k * x * kPi * 0.5);
        case 3: {
            // Fewer bits, fewer samples.
            const auto hold = 1 + static_cast<int>(amount * 15.0);
            if (side == 0 && ++holdCount_ >= hold)
                holdCount_ = 0;
            if (holdCount_ == 0) {
                const auto levels = std::pow(2.0, 15.0 - 13.0 * amount);
                held_[side] = std::round(x * levels) / levels;
            }
            return held_[side];
        }
        default: return x + amount * (std::abs(x) * 1.5 - x);
        }
    }

    double noiseSample(const int kind, const std::size_t side) noexcept
    {
        const auto white = random();
        switch (kind) {
        case 1: {
            // Pink: three poles (after Paul Kellet's economy filter).
            auto& state = pink_[side];
            state[0] = 0.99765 * state[0] + white * 0.0990460;
            state[1] = 0.96300 * state[1] + white * 0.2965164;
            state[2] = 0.57000 * state[2] + white * 1.0526913;
            return (state[0] + state[1] + state[2] + white * 0.1848) * 0.25;
        }
        case 2: return std::abs(white) > 0.996 ? white * 4.0 : 0.0;
        case 3: {
            auto& state = pink_[side];
            state[0] += (white - state[0]) * 0.08;
            return state[0] * 3.0;
        }
        default: return white;
        }
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

    struct Unison {
        std::array<double, 2> phase {};
        std::array<double, 2> increment {};
        std::array<int, 2> level {};
        double modulator {};
        double modulatorIncrement {};
        double sub {};
        double subIncrement {};
        double left { 0.7071 };
        double right { 0.7071 };
    };

    double rate_ { 48'000.0 };
    double target_ {};
    double pitch_ {};
    double velocity_ { 1.0 };
    double elapsed_ {};
    Envelope amp_;
    Envelope env2_;
    Envelope env3_;
    std::array<std::array<MultiFilter, 2>, 2> stages_ {}; // [filter][side]
    std::array<double, 2> dcIn_ {};
    std::array<double, 2> dcOut_ {};
    std::array<double, 2> held_ {};
    int holdCount_ {};
    std::array<std::array<double, 3>, 2> pink_ {};
    std::array<double, 2> lfoPhases_ {};
    std::array<double, 2> lfoHeld_ {};
    double noteRandom_ {};
    double sequence_ {};
    std::array<Unison, 8> subs_ {};
    double previousLevel_ {};
    std::array<double, 2> previousPan_ { 1.0, 1.0 };
    double glidePerSample_ {};
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeColosse()
{
    return std::make_unique<ColosseEngine>();
}

std::vector<float> colossePicture(const Values& values, const std::string_view part, const int points)
{
    // One oscillator over two of its periods, as it plays (its table, its
    // position, its bending, its phase modulation).
    const auto index = part == "osc1" ? 0 : part == "osc2" ? 1 : -1;
    if (index < 0 || points < 2)
        return {};
    const auto base = index == 0 ? static_cast<int>(osc1Table) : static_cast<int>(osc2Table);
    const auto v = [&values](const int at) { return static_cast<double>(values[static_cast<std::size_t>(at)]); };
    const auto set = std::clamp(static_cast<int>(std::lround(v(base))), 0, tables::sets - 1);
    const auto position = v(base + 1);
    const auto mode = std::clamp(static_cast<int>(std::lround(v(base + 2))), 0, 7);
    const auto warp = v(base + 3);
    const auto pm = v(index == 0 ? pmOsc1 : pmOsc2) * 0.35;
    const auto ratio = kRatioValues[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(v(pmRatio))), 0, 7))];
    std::vector<float> result(static_cast<std::size_t>(points));
    for (int at = 0; at < points; ++at) {
        const auto phase = 2.0 * static_cast<double>(at) / static_cast<double>(points - 1);
        const auto modulator = std::sin(2.0 * kPi * phase * ratio);
        result[static_cast<std::size_t>(at)] = static_cast<float>(tables::sample(set, position, mode, warp, 2, phase + pm * modulator));
    }
    return result;
}

} // namespace cydaw::engine::synth::detail
