#pragma once

#include <juce_core/juce_core.h>

#include <memory>

// The controllers tool (Options › Contrôleurs MIDI / OSC…). RAW devices: the
// MIDI ports, OSC sockets (local or on the network) and the devices that
// application plug-ins (.cyctl) bring. Virtual controllers made from them,
// a chain of modifiers between each raw address and its virtual control,
// both ways. Assignments of virtual controls to what they move in CyDAW
// (ControlTargets.h), learnt by clicking a knob then moving a control. A
// monitor of what comes and goes. Kept in the settings (devices, virtual
// controllers, the assignments for every project) and in the project (its
// tracks' and cards' assignments).
//
// Everything runs on the message thread; the devices' own threads only
// queue what they hear.

namespace cydaw::v2 {

class Studio;
struct UiState;
class ImGuiHost;

class Controllers final {
public:
    Controllers(Studio& studio, UiState& state, ImGuiHost& host, bool keepSettings);
    ~Controllers();

    // The window.
    void draw(bool& visible);
    // The toolbar's button: the window, and learning on a right click.
    void drawButton(bool& visible);
    // Learn mode over the whole interface: after every other window.
    void drawLearnOverlay();
    // On or off (Ctrl+M).
    void setLearning(bool on);
    [[nodiscard]] bool learning() const;
    // The control port's "controllers" command.
    juce::var command(const juce::var& request);

    class Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace cydaw::v2
