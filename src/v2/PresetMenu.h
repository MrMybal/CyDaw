#pragma once

#include "Presets.h"
#include "DeviceHost.h"
#include "UiState.h"

#include <imgui.h>

#include <functional>

// The presets of anything with settings, from a button on it (a device card,
// a plug-in, a track's chain) or a bar (a device's window): the list, CyDAW's
// then the person's, a click loading one (one undo step for a card); the
// previous and the next; the current state saved under a name (a name already
// there is replaced); a preset renamed, removed (to the recycle bin), its
// folder opened. A plug-in's own programs (its factory presets) come first,
// when it has some.

namespace cydaw::v2 {

class PresetMenu final {
public:
    using Capture = std::function<juce::var()>;
    using Apply = std::function<bool(const juce::var&, juce::String&)>;
    // A hosted plug-in's own programs: listed (with the current one) when
    // the list opens, one chosen. Any list of that kind (a track's factory
    // chains) under its own heading.
    struct Programs {
        std::function<bool(juce::StringArray&, int&)> list;
        std::function<bool(int, juce::String&)> select;
        const char* heading {};
    };

    PresetMenu(DeviceHost& studio, UiState& state);
    // An icon button opening the presets of `holder` (`tip`: what it is,
    // when not a card's presets).
    void button(const char* id, const PresetHolder& holder, const Capture& capture, const Apply& apply,
                ImVec2 size, const Programs* programs = nullptr, const char* tip = nullptr);
    // A bar: previous, the current name (opening the list), next, save.
    void bar(const char* id, const PresetHolder& holder, const Capture& capture, const Apply& apply, float width,
             const Programs* programs = nullptr);

private:
    void popup(const char* id, const PresetHolder& holder, const Capture& capture, const Apply& apply,
               const Programs* programs);
    void open(const char* id, const PresetHolder& holder, const Programs* programs);
    bool load(const presets::Entry& entry, const PresetHolder& holder, const Apply& apply);
    void step(int direction, const PresetHolder& holder, const Apply& apply);
    void report(const juce::String& text, bool error);

    DeviceHost& studio_;
    UiState& state_;
    char name_[128] {};
    char newName_[128] {};
    juce::String renaming_;
    juce::String removing_;
    juce::StringArray programNames_; // the open list's plug-in programs
    int programCurrent_ { -1 };
};

} // namespace cydaw::v2
