#pragma once

#include <cydaw/engine/SynthFamily.h>

#include <imgui.h>

#include <vector>

// How each model of the synth family (SynthFamily.h) shows itself: its
// window as pages of rows of panels, each a title, a picture perhaps and its
// controls (by their keys, a short name under each knob); its card as a few
// small panels of its main settings. SynthView draws any of them.

namespace cydaw::v2::synth_layout {

// What a panel draws above its controls: a wave (the oscillators' sum, or
// one oscillator as the engine draws it), the filter's curve (dragged:
// cutoff and resonance), an envelope (its points dragged), an LFO's shape,
// a sequence's steps (drawn with the pointer), the orb (a point dragged in
// a circle, moving by itself).
enum class Picture { none, wave, filter, envelope, lfo, steps, orb };

struct Control {
    const char* key {};
    // Under its knob (a choice and a toggle show their own text).
    const char* french {};
    const char* english {};
    // Greyed unless the setting `onlyWhen` lies between these (the pulse's
    // width when the wave is a pulse).
    const char* onlyWhen {};
    float from {};
    float to {};
    // A choice's width (0: the usual), a line of its own from here.
    float width {};
    bool newLine {};
    // Left out (not greyed) when `onlyWhen` is not met: an engine's
    // settings for its kind.
    bool hide {};
};

struct Panel {
    const char* id {};
    const char* french {};
    const char* english {};
    int section {};        // the model's section: its colour
    float weight { 1.0f }; // its share of its row
    Picture picture { Picture::none };
    // The picture's keys' start: an envelope's or an LFO's ("amp", "lfo1"),
    // a filter's ("filter", "f1"), an oscillator's ("osc1"), a sequence's.
    const char* pictureKey {};
    // A second filter drawn with the first (its handle too).
    const char* secondKey {};
    std::vector<Control> controls;
};

struct Page {
    const char* french {};
    const char* english {};
    std::vector<std::vector<Panel>> rows;
};

struct Layout {
    ImU32 colour {};
    const char* title {}; // "AMBRE"
    std::vector<Page> pages;
    // The card in Devices: a few small panels side by side, the sound's main
    // settings (the window has them all).
    std::vector<Panel> card;
};

[[nodiscard]] const Layout& layoutFor(engine::synth::Model model);

} // namespace cydaw::v2::synth_layout
