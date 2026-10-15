#pragma once

#include <juce_core/juce_core.h>

#include <imgui.h>

#include <map>
#include <string>
#include <vector>

// The computer's keys as a MIDI keyboard, as Bitwig does: the middle row of
// letters the white keys, the row above the black ones, chosen by where the
// keys are (an AZERTY keyboard's Q plays what a QWERTY one's A does); the
// first two keys of the bottom row move the octave, the next two the
// velocity. It plays what the MIDI input plays (the selected instrument
// track, or the armed ones).

namespace cydaw::v2 {

class Studio;

class ComputerKeyboard final {
public:
    explicit ComputerKeyboard(Studio& studio);
    ~ComputerKeyboard();

    // Each frame, before the shortcuts: its keys pressed and let go play and
    // stop notes, and are kept from the shortcuts while it plays. `allowed`
    // false (a text being typed, a panel with letter shortcuts of its own):
    // nothing plays, the notes held stop.
    void update(bool allowed);
    void allNotesOff();

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled);
    // The octave of its first key (4: Do4/C4, MIDI 60), -1..8.
    [[nodiscard]] int octave() const noexcept { return octave_; }
    void setOctave(int octave);
    // 1..127.
    [[nodiscard]] int velocity() const noexcept { return velocity_; }
    void setVelocity(int velocity);
    // Whether it played this frame (its keys taken from the shortcuts).
    [[nodiscard]] bool active() const noexcept { return active_; }

    // Its keys as this computer's layout has them: the key, what it types,
    // and the semitone from the first key (-1: octave down, -2: up, -3:
    // velocity down, -4: up).
    struct Key {
        ImGuiKey key { ImGuiKey_None };
        std::string label;
        int semitone {};
    };
    [[nodiscard]] const std::vector<Key>& keys() const noexcept { return keys_; }
    // The notes held now (MIDI numbers).
    [[nodiscard]] std::vector<int> heldNotes() const;
    // The keys read again (the layout may have changed).
    void readLayout();

private:
    void save();

    Studio& studio_;
    std::vector<Key> keys_;
    // The note each key down started (the octave may move while it is held).
    std::map<ImGuiKey, int> held_;
    bool enabled_ { true };
    int octave_ { 4 };
    int velocity_ { 100 };
    bool active_ {};
};

} // namespace cydaw::v2
