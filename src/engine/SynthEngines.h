#pragma once

#include <cydaw/engine/SynthFamily.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// What each model of the synth family brings to it (SynthFamily.cpp asks
// them): its parameter table after the shared voicing ones, its sections,
// and its voice's engine. Internal to the engine.

namespace cydaw::engine::synth::detail {

// One voice's engine for one model.
class Engine {
public:
    virtual ~Engine() = default;
    // Where it writes what it plays, for an interface to show (its voice's).
    void showInto(VoiceLive* live) noexcept { live_ = live; }
    virtual void prepare(double sampleRate) = 0;
    virtual void start(const Values& values, int note, float velocity, double glideFrom, double sampleRate) noexcept = 0;
    virtual void retarget(const Values& values, int note, float velocity) noexcept = 0;
    virtual void release() noexcept = 0;
    virtual void kill() noexcept = 0;
    [[nodiscard]] virtual bool active() const noexcept = 0;
    [[nodiscard]] virtual bool releasing() const noexcept = 0;
    [[nodiscard]] virtual double currentPitch() const noexcept = 0;
    // Adds into left and right; false once silent. The note's expression:
    // its pitch moved (semitones), its gain (linear).
    virtual bool render(const Values& values, const prism::Context& context, float* left, float* right, int count, double sampleRate,
                        double expressionPitch, double expressionGain) noexcept
        = 0;

protected:
    // A setting its own modulations moved to `value` (its knob says
    // `knob`): shown there, or as its knob when they did not move it.
    void show(const int index, const double value, const double knob) noexcept
    {
        if (live_ != nullptr && index >= 0 && static_cast<std::size_t>(index) < VoiceLive::valueCount)
            live_->values[static_cast<std::size_t>(index)] = std::abs(value - knob) > 1.0e-6 * std::max(1.0, std::abs(knob))
                                                                 ? static_cast<float>(value)
                                                                 : std::numeric_limits<float>::quiet_NaN();
    }
    void showLfo(const std::size_t index, const double value, const double phase) noexcept
    {
        if (live_ != nullptr && index < VoiceLive::lfoCount) {
            live_->lfos[index] = static_cast<float>(value);
            live_->lfoPhases[index] = static_cast<float>(phase);
        }
    }
    void showEnvelope(const std::size_t index, const double level) noexcept
    {
        if (live_ != nullptr && index < VoiceLive::envelopeCount)
            live_->envelopes[index] = static_cast<float>(level);
    }

private:
    VoiceLive* live_ {};
};

// A parameter table being written: each call adds one.
struct Table {
    std::vector<Parameter> parameters;
    int section {};
    Table& add(std::string key, std::string french, std::string english, std::string unit, float minimum, float maximum,
               float defaultValue, Kind kind = Kind::continuous, bool logarithmic = false,
               std::vector<std::pair<std::string, std::string>> choices = {})
    {
        parameters.push_back({ std::move(key), std::move(french), std::move(english), std::move(unit), minimum, maximum, defaultValue, kind,
                               section, logarithmic, std::move(choices) });
        return *this;
    }
    Table& choice(std::string key, std::string french, std::string english, std::vector<std::pair<std::string, std::string>> choices,
                  const int defaultValue)
    {
        const auto last = static_cast<float>(choices.size() - 1);
        return add(std::move(key), std::move(french), std::move(english), {}, 0.0f, last, static_cast<float>(defaultValue), Kind::choice,
                   false, std::move(choices));
    }
    Table& toggle(std::string key, std::string french, std::string english, const bool on)
    {
        return add(std::move(key), std::move(french), std::move(english), {}, 0.0f, 1.0f, on ? 1.0f : 0.0f, Kind::toggle);
    }
    Table& whole(std::string key, std::string french, std::string english, std::string unit, const int minimum, const int maximum,
                 const int defaultValue)
    {
        return add(std::move(key), std::move(french), std::move(english), std::move(unit), static_cast<float>(minimum),
                   static_cast<float>(maximum), static_cast<float>(defaultValue), Kind::integer);
    }
};

// The voicing every model begins with (section 0).
void addVoicing(Table& table, int voices, float master);

// Ambre: its table (voicing included), its sections, a voice's engine.
std::vector<Parameter> ambreParameters();
std::vector<Section> ambreSections();
std::unique_ptr<Engine> makeAmbre();

// Colosse: the same, and its pictures (an oscillator over two periods).
std::vector<Parameter> colosseParameters();
std::vector<Section> colosseSections();
std::unique_ptr<Engine> makeColosse();
std::vector<float> colossePicture(const Values& values, std::string_view part, int points);

// Mosaïque: the same, its pictures one engine ("a", "b").
std::vector<Parameter> mosaiqueParameters();
std::vector<Section> mosaiqueSections();
std::unique_ptr<Engine> makeMosaique();
std::vector<float> mosaiquePicture(const Values& values, std::string_view part, int points);

// Nébuleuse: the same, its pictures one layer ("a", "b"); its textures (and
// the tables) made by its warm-up.
std::vector<Parameter> nebuleuseParameters();
std::vector<Section> nebuleuseSections();
std::unique_ptr<Engine> makeNebuleuse();
std::vector<float> nebuleusePicture(const Values& values, std::string_view part, int points);
void nebuleuseWarmUp();

// Opale: its table, its sections, a voice's engine, its pictures.
std::vector<Parameter> opaleParameters();
std::vector<Section> opaleSections();
std::unique_ptr<Engine> makeOpale();
std::vector<float> opalePicture(const Values& values, std::string_view part, int points);

// Onyx: its table, its sections, a voice's engine, its pictures.
std::vector<Parameter> onyxParameters();
std::vector<Section> onyxSections();
std::unique_ptr<Engine> makeOnyx();
std::vector<float> onyxPicture(const Values& values, std::string_view part, int points);

// Tirette: its table, its sections, a voice's engine, its pictures.
std::vector<Parameter> tiretteParameters();
std::vector<Section> tiretteSections();
std::unique_ptr<Engine> makeTirette();
std::vector<float> tirettePicture(const Values& values, std::string_view part, int points);

// Alliage: its table, its sections, a voice's engine, its pictures.
std::vector<Parameter> alliageParameters();
std::vector<Section> alliageSections();
std::unique_ptr<Engine> makeAlliage();
std::vector<float> alliagePicture(const Values& values, std::string_view part, int points);

// Quartz: its table, its sections, a voice's engine, its pictures.
std::vector<Parameter> quartzParameters();
std::vector<Section> quartzSections();
std::unique_ptr<Engine> makeQuartz();
std::vector<float> quartzPicture(const Values& values, std::string_view part, int points);

// Argile: its table, its sections, a voice's engine, its pictures.
std::vector<Parameter> argileParameters();
std::vector<Section> argileSections();
std::unique_ptr<Engine> makeArgile();
std::vector<float> argilePicture(const Values& values, std::string_view part, int points);

// Saphir: its table, its sections, a voice's engine, its pictures.
std::vector<Parameter> saphirParameters();
std::vector<Section> saphirSections();
std::unique_ptr<Engine> makeSaphir();
std::vector<float> saphirPicture(const Values& values, std::string_view part, int points);
void saphirWarmUp();
double argileFilterDb(const Values& values, double hz);

// Dérive: its table, its sections, a voice's engine, its pictures.
std::vector<Parameter> deriveParameters();
std::vector<Section> deriveSections();
std::unique_ptr<Engine> makeDerive();
std::vector<float> derivePicture(const Values& values, std::string_view part, int points);

// Bronze: its table, its sections, a voice's engine, its pictures.
std::vector<Parameter> bronzeParameters();
std::vector<Section> bronzeSections();
std::unique_ptr<Engine> makeBronze();
std::vector<float> bronzePicture(const Values& values, std::string_view part, int points);
// Jade: its table, its sections, a voice's engine, its pictures.
std::vector<Parameter> jadeParameters();
std::vector<Section> jadeSections();
std::unique_ptr<Engine> makeJade();
std::vector<float> jadePicture(const Values& values, std::string_view part, int points);
// Silex: its table, its sections, a voice's engine, its pictures.
std::vector<Parameter> silexParameters();
std::vector<Section> silexSections();
std::unique_ptr<Engine> makeSilex();
std::vector<float> silexPicture(const Values& values, std::string_view part, int points);

} // namespace cydaw::engine::synth::detail
