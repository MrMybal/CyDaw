#include "ImportedInstruments.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace cydaw::v2::imported {

namespace {

using app::project_import::ImportedDevice;

// The device's settings by the other application's names: the first of a
// name (its own, before those of what it holds: its modulators').
class Settings final {
public:
    explicit Settings(const ImportedDevice& device) : device_(device) {}
    [[nodiscard]] std::optional<double> operator[](const char* name) const
    {
        for (const auto& [key, value] : device_.settings)
            if (key == name && std::isfinite(value))
                return value;
        return std::nullopt;
    }

private:
    const ImportedDevice& device_;
};

class Writer final {
public:
    Writer(Equivalent& equivalent, const Settings& from) : equivalent_(equivalent), from_(from) {}
    // `key` set from `name` through `convert` when the device has it.
    template <typename Convert>
    void map(const char* key, const char* name, Convert convert)
    {
        if (const auto value = from_[name])
            set(key, static_cast<float>(convert(*value)));
    }
    void map(const char* key, const char* name)
    {
        map(key, name, [](const double value) { return value; });
    }
    void set(const char* key, const float value) { equivalent_.settings.emplace_back(key, value); }

private:
    Equivalent& equivalent_;
    const Settings& from_;
};

double noteToHz(const double note)
{
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

// The polysynth of the other application, as Opale: its two oscillators
// (octave in feet, continuous shape from triangle through saw to square,
// sub, hard sync in semitones, unison), its mixer (balance -1..1, blend
// operator, noise, drive before the filter in dB, feedback), its filter
// (cutoff as a note, envelope depth in semitones, key tracking) and its two
// envelopes in seconds.
void polysynth(Equivalent& equivalent, const Settings& from)
{
    Writer write(equivalent, from);
    write.map("voice.mode", "POLY", [](const double poly) { return poly >= 0.5 ? 0.0 : 1.0; });
    write.map("voice.glide", "GLIDE_TIME", [](const double seconds) { return std::clamp(seconds, 0.0, 2.0); });
    for (const auto osc : { 1, 2 }) {
        const auto key = [osc](const char* suffix) { return "osc" + std::to_string(osc) + "." + suffix; };
        const auto name = [osc](const char* first, const char* second) { return std::string(osc == 1 ? first : second); };
        const auto mapped = [&](const char* suffix, const std::string& source, auto convert) {
            const auto target = key(suffix);
            if (const auto value = from[source.c_str()])
                write.set(target.c_str(), static_cast<float>(convert(*value)));
        };
        const auto same = [](const double value) { return value; };
        mapped("octave", name("OSC1OCT", "OSC2OCT"), [](const double index) { return std::clamp(std::round(index), 0.0, 4.0); });
        mapped("pitch", name("OSC1_PITCH", "OSC2PITCH"), [](const double semitones) { return std::clamp(semitones, -24.0, 24.0); });
        mapped("shape", name("OSC1_SHAPE", "OSC2_SHAPE"), [](const double shape) { return std::clamp(0.5 + 0.25 * shape, 0.0, 1.0); });
        mapped("sub", name("OSC1_SUB_LEVEL", "OSC2_SUB_LEVEL"), same);
        mapped("subshape", name("OSC1_SUB_PW", "OSC2_SUB_PW"), same);
        mapped("hardsync", name("OSC1_SYNC", "OSC2_SYNC"), [](const double semitones) { return std::clamp(semitones, 0.0, 36.0); });
        mapped("unison", name("OSC1_UNISON_VOICES", "OSC2_UNISON_VOICES"), [](const double voices) { return std::clamp(std::round(voices), 1.0, 7.0); });
        mapped("detune", name("OSC1_UNISON_SPREAD", "OSC2_UNISON_SPREAD"), [](const double spread) { return std::clamp(spread * 100.0, 0.0, 100.0); });
        mapped("retrigger", name("OSC_1_RETRIG", "OSC2_RETRIG"), [](const double on) { return on >= 0.5 ? 1.0 : 0.0; });
        // No fine tuning there: both in tune.
        write.set(key("fine").c_str(), 0.0f);
    }
    write.map("mix.balance", "OSCMIX", [](const double mix) { return std::clamp((mix + 1.0) * 0.5, 0.0, 1.0); });
    write.map("mix.mode", "OSC_BLEND_OPERATOR", [](const double mode) { return std::clamp(std::round(mode), 0.0, 5.0); });
    write.map("noise.level", "NOISE");
    write.map("mix.drive", "PRE-FITLER_GAIN", [](const double db) { return std::clamp(db / 24.0, 0.0, 1.0); });
    write.map("mix.feedback", "FEEDBACK");
    // No drift there.
    write.set("drift", 0.0f);
    write.map("f.cutoff", "F1FREQ", [](const double note) { return std::clamp(noteToHz(note), 20.0, 20'000.0); });
    write.map("f.resonance", "F1RESO");
    write.map("filter.env", "FEGDEPTH", [](const double semitones) { return std::clamp(semitones / 72.0, -1.0, 1.0); });
    write.map("filter.key", "KEY > F");
    write.set("filter.velocity", 0.0f);
    write.map("amp.attack", "A", [](const double seconds) { return std::clamp(seconds, 0.001, 10.0); });
    write.map("amp.decay", "D", [](const double seconds) { return std::clamp(seconds, 0.005, 10.0); });
    write.map("amp.sustain", "S");
    write.map("amp.release", "R", [](const double seconds) { return std::clamp(seconds, 0.005, 12.0); });
    write.map("amp.velocity", "AMP_VELOCITY_SENSETIVITY");
    write.map("fenv.attack", "F1A", [](const double seconds) { return std::clamp(seconds, 0.001, 10.0); });
    write.map("fenv.decay", "F1D", [](const double seconds) { return std::clamp(seconds, 0.005, 10.0); });
    write.map("fenv.sustain", "F1S");
    write.map("fenv.release", "F1R", [](const double seconds) { return std::clamp(seconds, 0.005, 12.0); });
}

bool named(const ImportedDevice& device, std::initializer_list<const char*> names)
{
    for (const auto* name : names)
        if (device.type.equalsIgnoreCase(name) || device.name.equalsIgnoreCase(name))
            return true;
    return false;
}

} // namespace

std::optional<Equivalent> equivalentOf(const ImportedDevice& device, const juce::String& format)
{
    if (device.plugin || !device.instrument)
        return std::nullopt;
    Equivalent equivalent;
    equivalent.fallback = "opale-instrument";
    // FL Studio's, LMMS's and Reaper's own instruments, by their names.
    struct Own {
        const char* format;
        std::initializer_list<const char*> names;
        const char* instrument;
    };
    static const Own own[] {
        { "fl-studio", { "Sampler", "Audio Clip", "Slicex", "Fruity Slicer", "Fruity Granulizer", "DirectWave" }, "simple-sampler-instrument" },
        { "fl-studio", { "FPC", "Drumaxx", "Drumpad" }, "batterie-instrument" },
        { "fl-studio", { "FL Keys", "Fruity DX10" }, "jade-instrument" },
        { "fl-studio", { "Sytrus" }, "saphir-instrument" },
        { "fl-studio", { "Harmor", "Harmless", "Morphine" }, "colosse-instrument" },
        { "fl-studio", { "Sakura" }, "bronze-instrument" },
        { "fl-studio", { "BooBass", "Toxic Biohazard", "PoiZone", "Sawer", "Transistor Bass", "MiniSynth" }, "argile-instrument" },
        { "fl-studio", { "Fruity Kick", "Kickbox" }, "kick-instrument" },
        { "fl-studio", { "Flex", "GMS", "3x Osc", "Autogun" }, "opale-instrument" },
        { "lmms", { "audiofileprocessor", "slicert" }, "simple-sampler-instrument" },
        { "lmms", { "sf2player", "gigplayer" }, "sampler-instrument" },
        { "lmms", { "kicker" }, "kick-instrument" },
        { "lmms", { "lb302" }, "argile-instrument" },
        { "lmms", { "opulenz" }, "quartz-instrument" },
        { "lmms", { "organic" }, "tirette-instrument" },
        { "lmms", { "malletsstk" }, "bronze-instrument" },
        { "lmms", { "watsyn", "bitinvader" }, "prism-instrument" },
        { "lmms", { "monstro" }, "colosse-instrument" },
        { "reaper", { "ReaSamplOmatic5000" }, "simple-sampler-instrument" },
        { "midi", { "Drums" }, "batterie-instrument" },
        { "fl-studio", { "Fruit Kick" }, "kick-instrument" },
        { "fl-studio", { "Plucked!" }, "bronze-instrument" },
    };
    for (const auto& entry : own)
        if (format == entry.format)
            for (const auto* name : entry.names)
                if (device.type.equalsIgnoreCase(name) || device.name.equalsIgnoreCase(name)) {
                    equivalent.instrument = entry.instrument;
                    return equivalent;
                }
    if (format == "fl-studio" || format == "lmms" || format == "reaper" || format == "ardour" || format == "midi"
        || format == "zrythm") {
        // Another of its own: an everyday polysynth.
        equivalent.instrument = "opale-instrument";
        return equivalent;
    }
    const Settings from(device);
    const auto ableton = format == "ableton";
    // The other application's own instruments, by their names or tags.
    if (!ableton && named(device, { "Polysynth" })) {
        equivalent.instrument = "opale-instrument";
        polysynth(equivalent, from);
    } else if (!ableton && named(device, { "FM-4" })) {
        equivalent.instrument = "quartz-instrument";
    } else if (!ableton && named(device, { "Organ" })) {
        equivalent.instrument = "tirette-instrument";
    } else if (!ableton && named(device, { "Phase-4" })) {
        equivalent.instrument = "onyx-instrument";
    } else if (!ableton && named(device, { "Polymer" })) {
        equivalent.instrument = "alliage-instrument";
    } else if (!ableton && named(device, { "E-Kick" })) {
        equivalent.instrument = "kick-instrument";
    } else if (named(device, { "Operator" })) {
        equivalent.instrument = "saphir-instrument";
    } else if (named(device, { "UltraAnalog", "Analog" })) {
        equivalent.instrument = "argile-instrument";
    } else if (named(device, { "Drift" })) {
        equivalent.instrument = "derive-instrument";
    } else if (named(device, { "Collision", "StringStudio", "Tension" })) {
        equivalent.instrument = "bronze-instrument";
    } else if (named(device, { "LoungeLizard", "Electric" })) {
        equivalent.instrument = "jade-instrument";
    } else if (named(device, { "InstrumentVector", "Wavetable" })) {
        equivalent.instrument = "prism-instrument";
    } else if (named(device, { "InstrumentMeld", "Meld" })) {
        equivalent.instrument = "alliage-instrument";
    } else if (named(device, { "OriginalSimpler", "Simpler", "Sampler" }) && !(ableton && named(device, { "Sampler" }))) {
        // The other application's simple sampler (Live's Sampler is its big one).
        equivalent.instrument = "simple-sampler-instrument";
    } else if (named(device, { "MultiSampler", "Sampler" })) {
        equivalent.instrument = "sampler-instrument";
    } else if (named(device, { "DrumGroupDevice", "Drum Machine", "InstrumentImpulse", "Impulse" })) {
        equivalent.instrument = "batterie-instrument";
    } else {
        // Another of its own: an everyday polysynth.
        equivalent.instrument = "opale-instrument";
    }
    equivalent.fallback = "opale-instrument";
    return equivalent;
}

} // namespace cydaw::v2::imported
