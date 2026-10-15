#pragma once

#include "Studio.h"
#include "UiState.h"

#include <cstdint>
#include <map>
#include <string>

// The sound card's inputs and outputs, as the audio settings window shows
// them: how many of its channels are open, the input buses tracks record
// from and the output buses the master and the tracks sent there come out
// of — each with its name, format, the card's channel for each of its own,
// level, mute, monitoring or the master.

namespace cydaw::v2 {

class SoundCardView final {
public:
    SoundCardView(Studio& studio, UiState& state);
    void draw();

private:
    void drawBuses(bool inputs);
    void report(const juce::String& error);

    Studio& studio_;
    UiState& state_;
    // The device's channels being edited, applied when the field is left.
    int inputsEdit_ { -1 };
    int outputsEdit_ { -1 };
    // A bus's name being typed.
    std::map<std::uint64_t, std::string> names_;
};

} // namespace cydaw::v2
