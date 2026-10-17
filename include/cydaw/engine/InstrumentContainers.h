#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

// Bitwig's instrument containers, on an instrument track: its instrument card
// is the container, the track's pads the chains it holds (each an instrument
// and its effects, the track's engine playing them, summed before the
// track's effects).
// - Drum Machine (card "drum-rack"): a pad per key, played by its key.
// - Instrument Layer ("instrument-layer"): every layer plays every note that
//   falls in its keys and its velocities.
// - Instrument Selector ("instrument-selector"): one layer plays each note,
//   chosen by its index, in turn, at random, by a key switch, by a
//   controller or by a program change. A note ends where it began.

namespace cydaw::engine::instrument_container {

enum class Kind : std::uint8_t { none, drum, layer, selector };

[[nodiscard]] constexpr Kind kindOfType(const std::string_view typeKey) noexcept
{
    if (typeKey == "drum-rack")
        return Kind::drum;
    if (typeKey == "instrument-layer")
        return Kind::layer;
    if (typeKey == "instrument-selector")
        return Kind::selector;
    return Kind::none;
}

[[nodiscard]] constexpr Kind kindOfProcessor(const std::string_view processorKey) noexcept
{
    if (processorKey == "core.drum-rack")
        return Kind::drum;
    if (processorKey == "core.instrument-layer")
        return Kind::layer;
    if (processorKey == "core.instrument-selector")
        return Kind::selector;
    return Kind::none;
}

[[nodiscard]] constexpr bool isContainerType(const std::string_view typeKey) noexcept
{
    return kindOfType(typeKey) != Kind::none;
}

[[nodiscard]] constexpr bool isContainerProcessor(const std::string_view processorKey) noexcept
{
    return kindOfProcessor(processorKey) != Kind::none;
}

// Its name, as its card shows it (English, as every module).
[[nodiscard]] constexpr const char* name(const Kind kind) noexcept
{
    switch (kind) {
    case Kind::drum: return "Drum Machine";
    case Kind::layer: return "Instrument Layer";
    case Kind::selector: return "Instrument Selector";
    default: return "";
    }
}

// The card a kind is, and the catalogue's instrument that makes it.
[[nodiscard]] constexpr const char* typeOf(const Kind kind) noexcept
{
    switch (kind) {
    case Kind::drum: return "drum-rack";
    case Kind::layer: return "instrument-layer";
    case Kind::selector: return "instrument-selector";
    default: return "";
    }
}

[[nodiscard]] constexpr const char* instrumentOf(const Kind kind) noexcept
{
    switch (kind) {
    case Kind::drum: return "rack-instrument";
    case Kind::layer: return "layer-instrument";
    case Kind::selector: return "selector-instrument";
    default: return "";
    }
}

[[nodiscard]] constexpr Kind kindOfInstrument(const std::string_view instrumentKey) noexcept
{
    if (instrumentKey == "rack-instrument")
        return Kind::drum;
    if (instrumentKey == "layer-instrument")
        return Kind::layer;
    if (instrumentKey == "selector-instrument")
        return Kind::selector;
    return Kind::none;
}

// A drum machine has a pad on any key; a layer or a selector holds this many.
inline constexpr int maximumPads = 128;
inline constexpr int maximumLayers = 16;

// How a selector chooses the layer of a new note.
enum class Selection : std::uint8_t { manual, roundRobin, random, keyswitch, controller, program, count };
inline constexpr int selectionCount = static_cast<int>(Selection::count);

[[nodiscard]] constexpr const char* selectionName(const int selection) noexcept
{
    constexpr std::array<const char*, selectionCount> names { "Manual", "Round Robin", "Random", "Keyswitch", "CC",
                                                              "Program Change" };
    return selection >= 0 && selection < selectionCount ? names[static_cast<std::size_t>(selection)] : "";
}

// A selector's settings, on its card ("selector.<key>").
struct Setting {
    std::string_view key;
    const char* label;
    float minimum;
    float maximum;
    float defaultValue;
};

enum Value : int { mode, layer, controller, keyswitch, valueCount };

inline constexpr std::array<Setting, valueCount> settings { {
    // How the layer is chosen (Selection).
    { "mode", "Mode", 0.0f, static_cast<float>(selectionCount - 1), 0.0f },
    // The layer it plays, from 1 (the one a key switch, a controller or a
    // program change starts from).
    { "layer", "Layer", 1.0f, static_cast<float>(maximumLayers), 1.0f },
    // The controller that chooses (CC mode): its value across the layers.
    { "controller", "CC", 0.0f, 127.0f, 1.0f },
    // The lowest key switch (Keyswitch mode): the next keys choose the next layers.
    { "keyswitch", "Keyswitch", 0.0f, 127.0f, 24.0f },
} };

using Values = std::array<float, valueCount>;

[[nodiscard]] constexpr Values defaultValues() noexcept
{
    Values values {};
    for (std::size_t index = 0; index < settings.size(); ++index)
        values[index] = settings[index].defaultValue;
    return values;
}

inline constexpr std::string_view prefix = "selector";

[[nodiscard]] constexpr int settingIndex(const std::string_view key) noexcept
{
    for (std::size_t index = 0; index < settings.size(); ++index)
        if (settings[index].key == key)
            return static_cast<int>(index);
    return -1;
}

// Every setting is a whole number.
[[nodiscard]] inline float clampValue(const int index, const float value) noexcept
{
    if (index < 0 || index >= valueCount || !std::isfinite(value))
        return 0.0f;
    const auto& setting = settings[static_cast<std::size_t>(index)];
    return std::clamp(std::round(value), setting.minimum, setting.maximum);
}

} // namespace cydaw::engine::instrument_container
