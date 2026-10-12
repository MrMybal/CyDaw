#pragma once

#include "SignalAnalysis.h"

#include <imgui.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <vector>

// The displays of the device panel, as a library: a filter's response over
// the spectrum before and after it, a compressor's curve and what it is
// doing, a saturation's transfer and scope, an envelope, an oscillator, an
// echo train, a reverb tail, a stereo field. Each draws in any rectangle and
// edits its parameters directly (drag the cutoff, the threshold, the
// envelope's corners). Which ones a device gets is read from the parameters
// it publishes, so a module made of filters and envelopes gets the same
// displays as the built-in devices.

namespace cydaw::v2::visual {

// A parameter as a display reads and edits it.
struct Param {
    juce::String binding;
    juce::String label;
    juce::String unit;
    double minimum {};
    double maximum { 1.0 };
    double value {};
    double defaultValue {};
};
// Applies a new value to a parameter (the device's graph).
using Setter = std::function<void(const Param&, double)>;

// What a display can show of the sound itself.
struct Live {
    const Spectrum* before {};
    const Spectrum* after {};
    const engine::AudioAnalysisFrame* beforeFrame {};
    const engine::AudioAnalysisFrame* afterFrame {};
    // Input and output levels over the last seconds, oldest first, dB, and
    // output minus input (a compressor's reduction, its makeup included).
    const float* inputHistory {};
    const float* outputHistory {};
    const float* reductionHistory {};
    int historyCount {};
    float inputDb { -120.0f };
    float outputDb { -120.0f };
    // Data arrived recently (the transport runs).
    bool running {};
    // A control source's current output (an LFO's), when it reports one.
    bool hasControl {};
    float control {};
    // What cables put on the device's parameters now, by binding.
    const std::map<juce::String, float>* modulated {};
    double tempo { 120.0 };
};

enum class PaneKind {
    spectrum,
    filter,
    equalizer,
    dynamics,
    saturation,
    envelope,
    oscillator,
    wavetable,
    delay,
    reverb,
    width,
    level,
    lfo,
    // The effect catalogue's.
    multiFilter,
    autoFilter,
    eqThree,
    sweep,
    tremolo,
    crusher,
    limiter,
    gate,
    transient,
    // The parametric equaliser: eight bands of six values, then the output.
    eqPro,
    // The studio compressor: threshold, ratio, knee, attack, release,
    // makeup, mix, detector filter.
    compPro,
    // The spectrometer: floor, tilt, smoothing, peak hold, waterfall length.
    spectrometer,
    // The studio effects (StudioEffectsView.h): every parameter of theirs,
    // in their catalogue's order.
    reverbPro,
    limiterPro,
    filterPro,
    // The amp, cabinet and microphone simulators (AmpDesign.h), drawn by
    // StudioEffectsView as the studio effects are.
    ampSim,
    cabSim,
    micSim,
    // The mixing effects (MixDesign.h), drawn by StudioEffectsView too.
    pression,
    fournaise,
    echoBande,
    multibandPro,
    velours,
    patine,
    sonometre,
    // The suites (SuiteDesign.h).
    tranche,
    finition,
    // Halo (HaloDesign.h).
    halo
};

// A studio effect's pane: StudioEffectsView draws it, card and window.
[[nodiscard]] inline bool isStudioPane(const PaneKind kind) noexcept
{
    return kind == PaneKind::reverbPro || kind == PaneKind::limiterPro || kind == PaneKind::filterPro || kind == PaneKind::ampSim
           || kind == PaneKind::cabSim || kind == PaneKind::micSim || kind == PaneKind::pression || kind == PaneKind::fournaise
           || kind == PaneKind::echoBande || kind == PaneKind::multibandPro || kind == PaneKind::velours || kind == PaneKind::patine
           || kind == PaneKind::sonometre || kind == PaneKind::tranche || kind == PaneKind::finition || kind == PaneKind::halo;
}

struct Pane {
    PaneKind kind { PaneKind::spectrum };
    // Indices into the device's parameters, in the order the kind reads
    // them; -1 for one the device does not have.
    std::vector<int> parameters;
    // filter: 0 low-pass, 1 high-pass; saturation: 0 drive, 1 clip;
    // sweep: 0 chorus, 1 flanger, 2 phaser; delay: 0 echo, 1 ping-pong.
    int variant {};
};

struct Surface {
    std::vector<Pane> panes;
    // What is left for knobs, in the device's order.
    std::vector<int> knobs;
};

// Reads what a device publishes and decides what to draw. `hasSignal`: the
// device's sound can be tapped; `instrument`: only what leaves it can.
[[nodiscard]] Surface inferSurface(const std::vector<Param>& parameters, bool hasSignal, bool instrument);

// The width a pane takes at a height.
[[nodiscard]] float paneWidth(const Pane& pane, float height);

// Draws a pane with its value fields; true when a parameter changed. With
// `expand`, a corner button sets it when clicked (open the pane large).
bool drawPane(const char* id, const Pane& pane, std::vector<Param>& parameters, ImVec2 position, ImVec2 size,
              ImU32 colour, const Live& live, const Setter& set, bool withFields = true, bool* expand = nullptr);

// A pane as a device's window shows it, filling the size: the parametric
// equaliser with its band panel, the compressor and the studio effects with
// theirs, any other large with its name and values over it, the sound's
// spectrum and levels beside it and its controls (and the device's
// `extras`, the parameters no display takes) large under it.
bool drawLargePane(const char* id, const Pane& pane, std::vector<Param>& parameters, ImVec2 position, ImVec2 size,
                   ImU32 colour, const Live& live, const Setter& set, const std::vector<int>* extras = nullptr);

// "FILTRE", "ÉGALISEUR PRO"…
[[nodiscard]] const char* paneTitle(const Pane& pane);

// A compact field: the label over the value, dragged to change, a double
// click for the default, the wheel for small steps.
bool valueField(const char* id, Param& parameter, float width, ImU32 colour, const Setter& set);

// "1.20 kHz", "-6.0 dB", "35 %", "4.0:1".
[[nodiscard]] juce::String formatValue(const Param& parameter, double value);

// A value as a position 0..1 of its range (logarithmic for frequencies and
// times), and back; what knobs, fields and automation lanes share.
[[nodiscard]] double positionOf(const Param& parameter, double value);
[[nodiscard]] double valueAtPosition(const Param& parameter, double position);

// What a right click on a parameter's field asks for (the device panel's
// menu); none outside it.
using ParameterMenu = std::function<void(const Param&)>;
void setParameterMenu(ParameterMenu menu);
// Opens that menu for a parameter (a display's own control right-clicked).
void openParameterMenu(const Param& parameter);

} // namespace cydaw::v2::visual
