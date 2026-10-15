#pragma once

#include "Theme.h"
#include "Studio.h"
#include "UiState.h"

#include <vector>

// The colour themes: the built-in ones and the person's own, one picked
// for the whole interface; a theme of one's own made from any other, its
// colours changed and seen at once, kept as a .cytheme file (JSON) in the
// presets' Themes folder.

namespace cydaw::v2 {

class ThemesView final {
public:
    ThemesView(Studio& studio, UiState& state);
    // Its window, while state.showThemes.
    void draw();
    // The theme the person picked last, at the start.
    void applySaved();
    // Every theme, the built-in ones first.
    [[nodiscard]] const std::vector<theme::Theme>& themes();
    [[nodiscard]] const juce::String& currentKey() const noexcept { return current_; }
    // Picked (applied, remembered). False when there is no such theme.
    bool choose(const juce::String& key);
    // A theme of one's own, a copy of `from` named `name`: its key.
    juce::String createFrom(const juce::String& from, const juce::String& name);
    // A theme of one's own changed (a colour, its name): saved.
    bool save(const juce::String& key, const juce::String& name, const theme::Palette& palette);
    bool remove(const juce::String& key);
    [[nodiscard]] static juce::File folder();

private:
    void reload();
    [[nodiscard]] const theme::Theme* find(const juce::String& key);

    Studio& studio_;
    UiState& state_;
    std::vector<theme::Theme> themes_;
    bool loaded_ {};
    int language_ { -1 };
    juce::String current_;
    // The theme shown on the right (its colours), and its name being typed.
    juce::String shown_;
    char name_[64] {};
};

} // namespace cydaw::v2
