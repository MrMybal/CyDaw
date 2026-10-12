#pragma once

#include "UiState.h"

#include <cstdint>

// The routing panel: the picked track's (or the master's) input and output,
// large, and nothing else. Its input: what it records (an input of the sound
// card), or its instrument's notes, what other tracks send it and its other
// inputs (a sidechain); its output: where its sound goes and its sends, each
// at a level. The master's: the tracks mixed, and the card's outputs that
// play it. The sound card itself is set in Options › Audio.

namespace cydaw::v2 {

class RoutingView final {
public:
    RoutingView(Studio& studio, UiState& state);
    void draw();

private:
    void drawInput(const TrackRef& track);
    void drawOutput(const TrackRef& track);
    void drawSenders(const TrackRef& track);
    // A virtual input or output picked: its side of the card, its effects,
    // who uses it.
    void drawBusInput(const app::VirtualAudioBus& bus);
    void drawBusOutput(const app::VirtualAudioBus& bus);
    void drawCardChannels(const app::VirtualAudioBus& bus);
    void drawBusEffects(std::uint64_t busId);
    void afterEffectsChoice(const TrackRef& track);
    void report(const juce::String& error);

    Studio& studio_;
    UiState& state_;
};

} // namespace cydaw::v2
