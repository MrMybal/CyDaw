#pragma once

#include "Studio.h"
#include "UiState.h"

#include <cydaw/engine/InstrumentContainers.h>

#include <imgui.h>

#include <cstdint>

// The card of an instrument container (InstrumentContainers.h).
// - Drum Machine ("drum-rack"): sixteen keys as pads, four by four from the
//   lowest at the bottom left, scrolled along the 128 keys (the strip beside
//   them shows them all); each pad its instrument and effects of their own
//   (opened in the device panel), its level, pan, mute, solo and choke group.
//   A pad is made by choosing its instrument, or by dropping a sound onto it.
// - Instrument Layer and Instrument Selector: its layers in a list, each its
//   instrument and effects, its level, pan, mute, solo, keys and velocities;
//   a selector's way of choosing above them, the layer it plays lit.

namespace cydaw::v2 {

class RackView final {
public:
    RackView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

    [[nodiscard]] static bool handles(const juce::String& typeKey)
    {
        return engine::instrument_container::isContainerType(typeKey.toStdString());
    }
    [[nodiscard]] static float paneWidth();
    void drawPane(const TrackRef& track, ImVec2 at, ImVec2 size, ImU32 colour);

    // The instruments a pad, a layer or a Replacer can play, as menu items:
    // CyDAW's, then the factory's by category (English names, a module's).
    // The key of the one chosen, empty while none is.
    [[nodiscard]] static juce::String instrumentItems(Studio& studio, const juce::String& current = {});
    // The same for a pad of a drum machine (made on `note` when there is
    // none) or a layer; true when one was chosen.
    static bool instrumentMenu(Studio& studio, const TrackRef& track, int note, std::optional<std::size_t> pad, juce::String& error);

private:
    void drawDrums(const TrackRef& track, ImVec2 at, ImVec2 size, ImU32 colour);
    void drawLayers(const TrackRef& track, ImVec2 at, ImVec2 size, ImU32 colour, bool selector);
    void drawPadControls(const TrackRef& track, std::size_t pad, ImVec2 at, float width, ImU32 colour);
    void drawLayerControls(const TrackRef& track, std::size_t layer, ImVec2 at, ImVec2 size, ImU32 colour);
    // A selector's way of choosing: its mode and what it listens to.
    void drawSelection(const TrackRef& track, ImVec2 at, float width);
    void chooseSample(const TrackRef& track, int note);
    void report(const juce::String& error);

    Studio& studio_;
    UiState& state_;
    // A layer being renamed (its track's key and its place), and its name.
    std::optional<std::pair<std::uint64_t, std::size_t>> renaming_;
    char renameBuffer_[96] {};
};

} // namespace cydaw::v2
