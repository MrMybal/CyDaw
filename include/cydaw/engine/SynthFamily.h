#pragma once

#include <cydaw/engine/VoiceLive.h>
#include <cydaw/engine/PrismSynth.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The family of synthesisers that share one frame: Ambre (an analog
// polysynth), and the ones after it. Each is a model: its settings are one
// flat array of numbers (Values), described by its parameter table (a key,
// names, a range, how it is shown, the section it belongs to), the first six
// of them the same for all of them (how its voices are played). A card
// stores the array with the model it belongs to; a plan carries both; a
// voice plays them.
//
// Nothing here knows JUCE; a host gives a voice the model, the values, the
// note and the buffers to add into.

namespace cydaw::engine::synth {

enum class Model : std::uint8_t { none, ambre, colosse, mosaique, nebuleuse, opale, onyx, tirette, alliage, quartz, argile, saphir, derive, bronze, silex, jade, count };
inline constexpr int modelCount = static_cast<int>(Model::count);

// How long Nébuleuse's textures last (its scan runs across them).
inline constexpr double textureSeconds = 3.0;

// Silex's hats: a closed (42) or a pedal (44) hat cuts the open one (46),
// as on a drum machine. The choke group a note belongs to (0: none).
inline constexpr int silexOpenHat = 46;
[[nodiscard]] constexpr int silexChokeGroupOf(const int note) noexcept
{
    return note == 42 || note == 44 || note == silexOpenHat ? 1 : 0;
}

// Room for the largest model's settings.
inline constexpr int valueCapacity = 256;
using Values = std::array<float, valueCapacity>;

// How every model's voices are played: its first settings, in this order.
enum Voicing : int { voicingMode, voicingCount, voicingGlide, voicingGlideAlways, voicingBend, voicingMaster, voicingFieldCount };

enum class Kind : std::uint8_t { continuous, integer, toggle, choice };

struct Parameter {
    std::string key;     // "osc1.wave"
    std::string french;  // "Forme"
    std::string english;
    std::string unit;    // "Hz", "s", "ct", "st", "dB" or empty
    float minimum {};
    float maximum { 1.0f };
    float defaultValue {};
    Kind kind { Kind::continuous };
    int section {};      // its section in the model's list
    bool logarithmic {};
    // A choice's options, French then English for each.
    std::vector<std::pair<std::string, std::string>> choices;
};

// A section of a model's panel: its name and colour.
struct Section {
    std::string french;
    std::string english;
    std::uint32_t colour {};
};

struct ModelInfo {
    Model model { Model::none };
    std::string typeKey;      // the card's type: "ambre-synth"
    std::string prefix;       // its settings' bindings: "ambre.osc1.wave"
    std::string processorKey; // the primitive: "core.ambre-synth"
    std::string instrument;   // the factory graph that holds it: "ambre-instrument"
    std::string french;
    std::string english;
    std::string frenchHint;
    std::string englishHint;
    std::vector<Parameter> parameters;
    std::vector<Section> sections;
};

[[nodiscard]] const ModelInfo& info(Model model);
// The model a card's type, a binding's prefix or a primitive belongs to;
// none for any other.
[[nodiscard]] Model modelForType(std::string_view typeKey);
[[nodiscard]] Model modelForPrefix(std::string_view prefix);
[[nodiscard]] Model modelForProcessor(std::string_view processorKey);
// The model a stored number names (none past the known ones).
[[nodiscard]] Model modelOf(float stored) noexcept;

// Makes what a model plays from (its wavetables, its textures) before its
// first note: once, off the audio thread (a plan compiled, a state
// restored). A voice of a model not warmed up plays silence where it lacks.
void warmUp(Model model);

[[nodiscard]] int parameterCount(Model model);
[[nodiscard]] const Parameter& parameter(Model model, int index);
// -1 when the model has no setting of that key.
[[nodiscard]] int parameterIndex(Model model, const std::string& key);
[[nodiscard]] Values defaultValues(Model model);
[[nodiscard]] float clampValue(Model model, int index, float value);
// 0..1 across the range (logarithmic where the parameter is), and back.
[[nodiscard]] float normalise(Model model, int index, float value);
[[nodiscard]] float denormalise(Model model, int index, float normalised);
// The value as a person reads it ("440 Hz", "Ladder", "1/8").
[[nodiscard]] std::string displayValue(Model model, int index, float value, bool french);
// A part of a model drawn as it plays ("osc1": that oscillator over two of
// its periods), `points` values around -1..1; empty when the model has no
// such picture.
[[nodiscard]] std::vector<float> picture(Model model, const Values& values, std::string_view part, int points);
// A model's own filter curve, in dB at `hz`, when its keys alone do not say
// it (Argile's two filters and their routing); NaN for the others.
[[nodiscard]] double filterDb(Model model, const Values& values, double hz);

// One voice of any model of the family, from a note-on to silence.
class Voice final {
public:
    Voice();
    ~Voice();
    Voice(Voice&&) noexcept;
    Voice& operator=(Voice&&) noexcept;

    void prepare(double sampleRate);
    // A note. glideFrom: the note it slides from (below 0: none).
    void start(Model model, const Values& values, int note, float velocity, double glideFrom, double sampleRate) noexcept;
    // Legato: the same voice slides to another note without starting again.
    void retarget(const Values& values, int note, float velocity) noexcept;
    void release() noexcept;
    void kill() noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool releasing() const noexcept;
    [[nodiscard]] double currentPitch() const noexcept;
    // A note's expression: its pitch moved (semitones) and its gain (linear).
    void setExpression(double pitchSemitones, double gain) noexcept;
    // Adds `count` stereo frames; false once silent.
    bool render(const Values& values, const prism::Context& context, float* left, float* right, int count, double sampleRate) noexcept;
    // What it plays now: the settings its own modulations move, its LFOs
    // and its envelopes.
    [[nodiscard]] const VoiceLive& live() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace cydaw::engine::synth
