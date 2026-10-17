#pragma once

#include <cydaw/engine/WavetableShape.h>

#include <cydaw/engine/AudioAnalysis.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cydaw::app {

enum class ModuleControlKind : std::uint8_t {
    knob,
    fader,
    waveform,
    spectrum,
    levelMeter,
    text,
    inputPort,
    outputPort,
    toggle,
    envelope,
    group,
    selector,
    xyPad,
    valueDisplay,
    filterResponse,
    dynamicsResponse,
    eqCurve,
    wavetable,
    nativePanel,
    custom
};

enum class ModuleInterfaceLayoutMode : std::uint8_t {
    graph,
    device
};

struct ModulePublishedParameter {
    juce::String key;
    juce::String label;
    juce::String unit;
    double minimum {};
    double maximum { 1.0 };
    double value {};
    bool showInDevice { true };
    // Stable across layout changes and parameter reordering. A zero value is
    // migrated deterministically from `key` when an older Graph is loaded.
    std::uint32_t hostParameterId {};
    bool publishToHost { true };
    bool automatable { true };
    bool modulatable { true };
    juce::String description;
    // Kept last to preserve source compatibility with schema-4 aggregate
    // initialisers. NaN means that an older graph has no authored reset value.
    double defaultValue { std::numeric_limits<double>::quiet_NaN() };
    // What this parameter last serialised to, and a signature of every field
    // that went into it. Rebuilding three hundred of these per serialise was
    // most of what a knob notch cost on a large patch. Validated, not
    // invalidated: no mutation site has to know.
    mutable juce::var serialised;
    mutable std::uint64_t serialisedSignature {};
};

struct ModuleInterfaceControl {
    std::uint64_t id {};
    ModuleControlKind kind { ModuleControlKind::knob };
    juce::String label;
    juce::String parameterBinding;
    float x {};
    float y {};
    float width { 0.22f };
    float height { 0.32f };
    std::uint8_t portKind {};
    // Audio topology is part of a published port contract and also defines
    // how many lanes an authored level meter presents. Non-audio controls use
    // one event/value stream.
    std::uint8_t portChannels { 1 };
    // Used by two-dimensional controls such as an XY pad. Kept independent
    // from the primary binding so each axis retains a stable host parameter.
    juce::String secondaryParameterBinding;
    // Multi-parameter controls append their extra stable bindings here. An
    // envelope maps primary/secondary/tertiary/quaternary to A/D/S/R.
    juce::String tertiaryParameterBinding;
    juce::String quaternaryParameterBinding;
    // Controls that address more parameters than the four named bindings can
    // hold list the rest here, in the order the control defines. An EQ curve
    // reads them as consecutive frequency / gain / Q triples, one per band.
    std::vector<juce::String> extraParameterBindings;
    // Portable appearance, page/group membership and custom ImGui component source.
    juce::var design;
    // Same arrangement as ModulePublishedParameter, for the same reason.
    mutable juce::var serialised;
    mutable std::uint64_t serialisedSignature {};
};

struct ModuleInterfaceState {
    std::uint32_t schema { 15 };
    std::uint64_t nextControlId { 1 };
    // Portable free-layout grid. Geometry remains normalised so reusable
    // interfaces keep the same composition at every window size and DPI.
    bool snapToGrid { true };
    std::uint8_t gridColumns { 24 };
    std::uint8_t gridRows { 16 };
    // The window a plugin made of this graph opens, in pixels; 0 for the
    // default. The controls keep their normalised places in it.
    std::uint16_t windowWidth {};
    std::uint16_t windowHeight {};
    // Laid out by the person (the workshop's Interface tab), rather than the
    // one made from the published settings: a plug-in made of the graph
    // opens on it.
    bool designed {};
    bool deviceDesigned {};
    // Per-surface appearance, independent of control-level overrides.
    juce::var windowDesign;
    juce::var deviceDesign;
    std::uint16_t deviceWidth { 480 };
    std::uint16_t deviceHeight { 240 };
    std::vector<ModulePublishedParameter> parameters;
    // Free-form controls rendered inside graph nodes and reusable sub-graphs.
    std::vector<ModuleInterfaceControl> controls;
    // Constrained wide layout rendered in the device-chain panel.
    std::vector<ModuleInterfaceControl> deviceControls;
};

// Drops every cached serialisation in a presentation, so a build can be
// made from scratch and compared with a cached one.
void dropSerialisedPresentation(ModuleInterfaceState& state);
[[nodiscard]] std::uint32_t stableModuleParameterId(
    const juce::String& parameterKey) noexcept;
[[nodiscard]] double moduleParameterDefaultValue(
    const ModulePublishedParameter& parameter) noexcept;
[[nodiscard]] std::vector<double> moduleSelectorValues(
    const ModulePublishedParameter& parameter);
[[nodiscard]] std::size_t closestModuleSelectorValue(
    const std::vector<double>& values, double value) noexcept;
[[nodiscard]] juce::String moduleSelectorValueLabel(
    const ModulePublishedParameter& parameter, double value);

[[nodiscard]] std::vector<ModuleInterfaceControl>& moduleInterfaceControls(
    ModuleInterfaceState& state, ModuleInterfaceLayoutMode mode) noexcept;
[[nodiscard]] const std::vector<ModuleInterfaceControl>& moduleInterfaceControls(
    const ModuleInterfaceState& state, ModuleInterfaceLayoutMode mode) noexcept;
void rebuildAutomaticDeviceControls(ModuleInterfaceState& state);
[[nodiscard]] juce::var serialiseModuleInterface(const ModuleInterfaceState& state);
[[nodiscard]] ModuleInterfaceState restoreModuleInterface(const juce::var& value);
[[nodiscard]] juce::Result validateModuleInterfaceDocument(const juce::var& document, const ModuleInterfaceState& parameters);
[[nodiscard]] juce::Result validateModuleInterface(const ModuleInterfaceState& state);

class ModuleXyPadComponent final : public juce::Component,
                                   public juce::SettableTooltipClient {
public:
    ModuleXyPadComponent();
    void setNormalisedValues(double x, double y,
                             juce::NotificationType notification);
    [[nodiscard]] double normalisedX() const noexcept { return x_; }
    [[nodiscard]] double normalisedY() const noexcept { return y_; }

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(juce::Component::FocusChangeType cause) override;
    void focusLost(juce::Component::FocusChangeType cause) override;

    std::function<void()> onGestureStart;
    std::function<void(double, double)> onValueChange;
    std::function<void()> onGestureEnd;
    std::function<void()> onReset;

private:
    void updateFromPosition(juce::Point<float> position);

    double x_ { 0.5 };
    double y_ { 0.5 };
    bool dragging_ {};
};

class ModuleEnvelopeComponent final : public juce::Component,
                                      public juce::SettableTooltipClient {
public:
    ModuleEnvelopeComponent();
    void setNormalisedValues(double attack, double decay, double sustain,
                             double release,
                             juce::NotificationType notification);

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(juce::Component::FocusChangeType cause) override;
    void focusLost(juce::Component::FocusChangeType cause) override;

    std::function<void()> onGestureStart;
    std::function<void(double, double, double, double)> onValueChange;
    std::function<void()> onGestureEnd;
    std::function<void()> onReset;

private:
    [[nodiscard]] std::array<juce::Point<float>, 4> handlePositions() const;
    void updateActiveHandle(juce::Point<float> position);

    std::array<double, 4> values_ { 0.18, 0.24, 0.72, 0.30 };
    int activeHandle_ {};
    bool dragging_ {};
};

class ModuleFilterResponseComponent final : public juce::Component,
                                            public juce::SettableTooltipClient {
public:
    ModuleFilterResponseComponent();
    void setParameterRanges(double minimumCutoff, double maximumCutoff,
                            double minimumResonance, double maximumResonance);
    void setValues(double cutoff, double resonance,
                   juce::NotificationType notification);
    [[nodiscard]] double cutoff() const noexcept { return cutoff_; }
    [[nodiscard]] double resonance() const noexcept { return resonance_; }
    // The response the card draws, in dB at one frequency: a two-pole
    // low-pass with the resonance as its Q, which is the engine's
    // state-variable filter. Static so the suite can ask what the picture
    // says and compare it with what the filter does.
    [[nodiscard]] static double magnitudeDb(double cutoff, double resonance,
                                            double frequency);

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(juce::Component::FocusChangeType cause) override;
    void focusLost(juce::Component::FocusChangeType cause) override;

    std::function<void()> onGestureStart;
    std::function<void(double, double)> onValueChange;
    std::function<void()> onGestureEnd;
    std::function<void()> onReset;

private:
    [[nodiscard]] double normalisedCutoff() const noexcept;
    [[nodiscard]] double normalisedResonance() const noexcept;
    void updateFromPosition(juce::Point<float> position);

    double minimumCutoff_ { 20.0 };
    double maximumCutoff_ { 20000.0 };
    double minimumResonance_ { 0.1 };
    double maximumResonance_ { 20.0 };
    double cutoff_ { 2000.0 };
    double resonance_ { 0.707 };
    bool dragging_ {};
};

// A multi-band EQ curve: the summed response drawn over a log-frequency
// grid, with one draggable point per band. Horizontal movement sets the band's
// frequency, vertical its gain, and the wheel its Q.
// The wavetable a card is shaped with, drawn and drawable. The eight frames
// are stacked back to front the way a table is read; the one being edited is
// drawn bright, and a drag inside the wave draws a new cycle into it. What the
// drag leaves is analysed into harmonics before it is stored, so a shape drawn
// with a mouse comes back band-limited rather than as a wall of aliasing.
class ModuleWavetableComponent final : public juce::Component,
                                       public juce::SettableTooltipClient {
public:
    ModuleWavetableComponent();
    void setShape(const cydaw::engine::WavetableShape& shape,
                  juce::NotificationType notification);
    [[nodiscard]] const cydaw::engine::WavetableShape& shape() const noexcept
    {
        return shape_;
    }
    // Where the morph currently reads, so the editor can show which frames
    // are actually being heard.
    void setMorphPosition(float position);
    void selectFrame(std::size_t frame);
    [[nodiscard]] std::size_t selectedFrame() const noexcept
    {
        return selectedFrame_;
    }
    // Replaces a frame with one of the classic shapes — or every frame at
    // once, which is how a table usually starts. Drawing a clean saw with a
    // mouse is not something anyone should have to do.
    enum class Preset { sine, sawtooth, square, triangle, builtIn };
    void applyPreset(Preset preset, bool allFrames);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    std::function<void(const cydaw::engine::WavetableShape&)> onShapeChanged;
    std::function<void()> onGestureStart;
    std::function<void()> onGestureEnd;
    // The editor asks; whoever owns it reads the file. It has no business
    // knowing about file choosers or audio formats.
    std::function<void()> onImportRequested;

private:
    static constexpr std::size_t drawResolution = 256;
    // How tall the edited frame is drawn, as a fraction of half the wave
    // area. Less than the whole so the stack behind it stays visible — and
    // shared with the drawing, or the line would not follow the mouse.
    static constexpr float editScale = 0.58f;
    using Curve = std::array<float, drawResolution>;

    void showPresetMenu();
    void rebuildCurves();
    void drawInto(juce::Point<float> point);
    void commitDrawnFrame();
    [[nodiscard]] juce::Rectangle<float> waveArea() const;
    [[nodiscard]] juce::Rectangle<float> frameStrip() const;
    [[nodiscard]] int frameAtX(float x) const;

    cydaw::engine::WavetableShape shape_;
    std::array<Curve, cydaw::engine::WavetableShape::frameCount> curves_ {};
    Curve drawn_ {};
    std::size_t selectedFrame_ { cydaw::engine::WavetableShape::frameCount / 2 };
    float morphPosition_ { 0.35f };
    bool drawing_ {};
    int lastDrawIndex_ { -1 };
};

class ModuleEqCurveComponent final : public juce::Component,
                                     public juce::SettableTooltipClient {
public:
    struct Band {
        double frequency { 1'000.0 };
        double gainDb {};
        double q { 1.0 };
    };

    ModuleEqCurveComponent();
    void setBandRanges(double minimumFrequency, double maximumFrequency,
                       double minimumGain, double maximumGain,
                       double minimumQ, double maximumQ);
    void setBands(std::vector<Band> bands,
                  juce::NotificationType notification);
    [[nodiscard]] const std::vector<Band>& bands() const noexcept
    {
        return bands_;
    }

    // The summed response of a set of bands, in dB at one frequency. Static so
    // the node card can paint the same curve without an interactive control.
    [[nodiscard]] static double magnitudeDb(const std::vector<Band>& bands,
                                            double frequency);
    // Where the curve and its points sit inside a rectangle, shared for the
    // same reason.
    [[nodiscard]] static float frequencyToX(double frequency,
                                            juce::Rectangle<float> area,
                                            double minimumFrequency,
                                            double maximumFrequency);
    [[nodiscard]] static float gainToY(double gainDb,
                                       juce::Rectangle<float> area,
                                       double minimumGain, double maximumGain);

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(juce::Component::FocusChangeType cause) override;
    void focusLost(juce::Component::FocusChangeType cause) override;

    std::function<void()> onGestureStart;
    std::function<void(std::size_t, double, double, double)> onBandChange;
    std::function<void()> onGestureEnd;

    // The signal, as levels at frequencies spaced logarithmically across the
    // plot's own span, drawn behind the curve: what is heard under what is
    // being set. Repaints only the plot.
    void setSpectrum(const float* bins, std::size_t count);
    // Opaque, so a frame's repaint of the plot reaches nothing under it —
    // not the surface, not the knobs beside it. The corners outside the
    // rounded plot take the host's own colour.
    void setSurroundColour(juce::Colour colour);
    // Where the spectrum it holds peaks, in hertz. Written for the suite,
    // which cannot look.
    [[nodiscard]] double spectrumPeakHzForTest() const;

private:
    [[nodiscard]] juce::Rectangle<float> plotBounds() const;
    [[nodiscard]] juce::Point<float> bandPosition(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> bandAt(
        juce::Point<float> position) const;
    void notifyBand(std::size_t index);

    std::vector<Band> bands_;
    double minimumFrequency_ { 20.0 };
    double maximumFrequency_ { 20'000.0 };
    double minimumGain_ { -30.0 };
    double maximumGain_ { 30.0 };
    double minimumQ_ { 0.1 };
    double maximumQ_ { 18.0 };
    std::optional<std::size_t> selected_;
    bool dragging_ {};
    std::vector<float> spectrum_;
    juce::Colour surround_ { 0xff292d32 };
};

// A waveform, a spectrum or a level meter on a device surface, as its own
// opaque child: it repaints itself at every frame and nothing under it —
// not the surface, not the knobs beside it. Repainting the union of the
// meters repainted every control of a big graph's surface, and the
// hundreds of knobs on it, thirty times a second.
class ModuleAnalysisDisplayComponent final : public juce::Component {
public:
    ModuleAnalysisDisplayComponent(ModuleControlKind kind, std::size_t channels);

    void setWaveform(const float* samples, std::size_t count);
    void setSpectrum(const float* bins, std::size_t count);
    void setLevels(
        const std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels>& peak,
        const std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels>& rms,
        std::size_t channelCount);
    void clear();
    void paint(juce::Graphics& graphics) override;

private:
    ModuleControlKind kind_;
    std::size_t channels_ { 2 };
    std::vector<float> waveform_;
    std::array<float, 32> spectrum_ {};
    std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels> peak_ {};
    std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels> rms_ {};
    std::size_t channelCount_ { 2 };
    bool active_ {};
};

class ModuleDynamicsResponseComponent final : public juce::Component,
                                              public juce::SettableTooltipClient {
public:
    ModuleDynamicsResponseComponent();
    void setParameterRanges(double minimumThreshold, double maximumThreshold,
                            double minimumRatio, double maximumRatio);
    void setValues(double threshold, double ratio,
                   juce::NotificationType notification);
    [[nodiscard]] double threshold() const noexcept { return threshold_; }
    [[nodiscard]] double ratio() const noexcept { return ratio_; }
    // The transfer the card draws: output level for an input level, in dB
    // of the peak the engine's detector reads, a hard knee at the
    // threshold. Static so the suite can compare the picture with the
    // sound.
    [[nodiscard]] static double outputDbForInputDb(double threshold,
                                                   double ratio,
                                                   double inputDb) noexcept;

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(juce::Component::FocusChangeType cause) override;
    void focusLost(juce::Component::FocusChangeType cause) override;

    std::function<void()> onGestureStart;
    std::function<void(double, double)> onValueChange;
    std::function<void()> onGestureEnd;
    std::function<void()> onReset;

private:
    [[nodiscard]] juce::Rectangle<float> plotBounds() const;
    [[nodiscard]] juce::Point<float> thresholdHandle() const;
    [[nodiscard]] juce::Point<float> ratioHandle() const;
    [[nodiscard]] double outputForInput(double inputDb) const noexcept;
    void updateFromPosition(juce::Point<float> position);

    double minimumThreshold_ { -60.0 };
    double maximumThreshold_ { 0.0 };
    double minimumRatio_ { 1.0 };
    double maximumRatio_ { 20.0 };
    double threshold_ { -18.0 };
    double ratio_ { 4.0 };
    int activeHandle_ {};
    bool dragging_ {};
};

class ModuleInterfaceEditorComponent final : public juce::Component,
                                             public juce::SettableTooltipClient {
public:
    explicit ModuleInterfaceEditorComponent(ModuleInterfaceState state);

    void refreshTranslations();
    void setState(ModuleInterfaceState state);
    [[nodiscard]] const ModuleInterfaceState& state() const noexcept;
    void setStateChangedHandler(std::function<void(const ModuleInterfaceState&)> handler);
    void setParameterChangedHandler(std::function<void(const juce::String&, double)> handler);
    [[nodiscard]] bool setParameterValue(const juce::String& key,
                                         double value);
    void setPortCreationHandler(
        std::function<void(bool, std::uint8_t, std::uint8_t)> handler);
    void setAnalysisFrame(const cydaw::engine::AudioAnalysisFrame& frame);
    void clearAnalysis() noexcept;
    void setCompactMode(bool compact);
    // The interface as it was drawn, to play with: every control at its
    // authored place over the whole component, no editing tools (a plugin's
    // window).
    void setPerformanceMode(bool performance);
    // The width the compact surface asks for: every control in a cell wide
    // enough for its knob and its caption, on one row, or two past sixteen
    // cells. The chain scrolls; a device is never squeezed to fit.
    [[nodiscard]] int preferredCompactWidth() const;
    void setLayoutMode(ModuleInterfaceLayoutMode mode);
    [[nodiscard]] ModuleInterfaceLayoutMode layoutMode() const noexcept;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    struct SliderEntry {
        std::uint64_t controlId {};
        juce::String binding;
        std::unique_ptr<juce::Slider> slider;
    };

    struct ButtonEntry {
        std::uint64_t controlId {};
        juce::String binding;
        std::unique_ptr<juce::ToggleButton> button;
    };

    struct ComboEntry {
        std::uint64_t controlId {};
        juce::String binding;
        std::vector<double> values;
        std::unique_ptr<juce::ComboBox> combo;
    };

    struct XyEntry {
        std::uint64_t controlId {};
        juce::String xBinding;
        juce::String yBinding;
        std::unique_ptr<ModuleXyPadComponent> pad;
    };

    struct EnvelopeEntry {
        std::uint64_t controlId {};
        std::array<juce::String, 4> bindings;
        std::unique_ptr<ModuleEnvelopeComponent> envelope;
    };

    struct EqCurveEntry {
        std::uint64_t controlId {};
        std::vector<juce::String> bindings;
        std::unique_ptr<ModuleEqCurveComponent> curve;
    };

    struct FilterResponseEntry {
        std::uint64_t controlId {};
        juce::String cutoffBinding;
        juce::String resonanceBinding;
        std::unique_ptr<ModuleFilterResponseComponent> response;
    };

    struct DynamicsResponseEntry {
        std::uint64_t controlId {};
        juce::String thresholdBinding;
        juce::String ratioBinding;
        std::unique_ptr<ModuleDynamicsResponseComponent> response;
    };

    [[nodiscard]] juce::Rectangle<int> canvasBounds() const;
    [[nodiscard]] juce::Rectangle<int> inspectorBounds() const;
    [[nodiscard]] juce::Rectangle<float> controlBounds(const ModuleInterfaceControl& control) const;
    // The compact surface ignores the stored geometry and flows its
    // controls into rows of equal cells, in the stored order; wide
    // displays span two or three cells.
    struct CompactCell {
        std::uint64_t id {};
        int row {};
        int column {};
        int span { 1 };
    };
    struct CompactPlan {
        int rows { 1 };
        int columns { 1 };
        std::vector<CompactCell> cells;
    };
    [[nodiscard]] CompactPlan compactPlan() const;
    void layoutCompactCells();
    std::unordered_map<std::uint64_t, juce::Rectangle<float>> compactCells_;
    [[nodiscard]] ModuleInterfaceControl* findControl(std::uint64_t id);
    [[nodiscard]] const ModuleInterfaceControl* findControl(std::uint64_t id) const;
    [[nodiscard]] ModulePublishedParameter* findParameter(const juce::String& key);
    [[nodiscard]] const ModulePublishedParameter* findParameter(const juce::String& key) const;
    [[nodiscard]] juce::String controlTooltip(const ModuleInterfaceControl& control) const;
    [[nodiscard]] std::uint64_t controlAt(juce::Point<float> position) const;
    [[nodiscard]] std::vector<ModuleInterfaceControl>& activeControls() noexcept;
    [[nodiscard]] const std::vector<ModuleInterfaceControl>& activeControls() const noexcept;
    void rebuildSliders();
    void synchroniseXyPads(const juce::String& binding = {});
    void synchroniseEnvelopes(const juce::String& binding = {});
    void synchroniseFilterResponses(const juce::String& binding = {});
    void synchroniseEqCurves(const juce::String& binding = {});
    void synchroniseDynamicsResponses(const juce::String& binding = {});
    void addControl(ModuleControlKind kind,
                    const juce::String& binding = {},
                    const juce::String& label = {},
                    std::uint8_t portKind = 0,
                    std::uint8_t portChannels = 1,
                    const juce::String& secondaryBinding = {},
                    const juce::String& tertiaryBinding = {},
                    const juce::String& quaternaryBinding = {});
    void showAddMenu();
    void showGridMenu();
    void showControlMenu(std::uint64_t controlId);
    void showControlProperties(std::uint64_t controlId);
    void showDeviceParametersMenu();
    void refreshInspector();
    void applyInspectorChanges();
    void scheduleParameterStateCommit();
    void flushParameterStateCommit();
    void beginHistoryGesture();
    void endHistoryGesture();
    void undoInterfaceChange();
    void redoInterfaceChange();
    void restoreHistoryState(ModuleInterfaceState state);
    void refreshHistoryButtons();
    void notifyStateChanged();
    void updateSpectrum();

    ModuleInterfaceState state_;
    std::vector<SliderEntry> sliders_;
    std::vector<ButtonEntry> buttons_;
    std::vector<ComboEntry> combos_;
    std::vector<XyEntry> xyPads_;
    std::vector<EnvelopeEntry> envelopes_;
    std::vector<FilterResponseEntry> filterResponses_;
    std::vector<EqCurveEntry> eqCurves_;
    std::vector<DynamicsResponseEntry> dynamicsResponses_;
    struct AnalysisDisplayEntry {
        std::uint64_t controlId {};
        std::unique_ptr<ModuleAnalysisDisplayComponent> display;
    };
    std::vector<AnalysisDisplayEntry> analysisDisplays_;
    // A control's label laid out once for its text and its frame, drawn
    // from the glyphs afterwards. Fitting the text on every paint was most
    // of what a surface of hundreds of controls cost per frame.
    struct CachedLabel {
        juce::String text;
        juce::Rectangle<int> area;
        juce::GlyphArrangement glyphs;
    };
    std::unordered_map<std::uint64_t, CachedLabel> labelCache_;
    [[nodiscard]] const juce::GlyphArrangement& cachedLabel(
        const ModuleInterfaceControl& control, juce::Rectangle<int> area);
    std::uint64_t selectedControlId_ {};
    std::uint64_t hoveredControlId_ {};
    juce::Point<float> dragOffset_;
    ModuleInterfaceControl resizeStartControl_;
    bool draggingControl_ {};
    bool resizingControl_ {};
    int resizeEdges_ {};
    bool parameterStateCommitPending_ {};
    bool parameterStateCommitScheduled_ {};
    bool historyGestureActive_ {};
    bool restoringHistory_ {};
    // Trees, not text. Rebuilding these as JSON strings on every setState
    // put the cost of the whole interface on every graph edit.
    juce::var committedState_;
    juce::var historyGestureStart_;
    std::vector<ModuleInterfaceState> undoStates_;
    std::vector<ModuleInterfaceState> redoStates_;
    bool compactMode_ {};
    bool performanceMode_ {};
    ModuleInterfaceLayoutMode layoutMode_ { ModuleInterfaceLayoutMode::graph };
    bool analysisActive_ {};
    std::array<float, cydaw::engine::AudioAnalysisFrame::waveformCapacity> waveform_ {};
    // The frame as handed over, for the shared spectrum computation.
    cydaw::engine::AudioAnalysisFrame lastFrame_;
    std::array<float, 32> spectrum_ {};
    std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels> peak_ {};
    std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels> rms_ {};
    std::size_t waveformCount_ {};
    std::size_t analysisChannelCount_ { 2 };

    juce::Label titleLabel_;
    juce::ToggleButton editButton_;
    juce::TextButton addButton_;
    juce::TextButton graphModeButton_;
    juce::TextButton deviceModeButton_;
    juce::ToggleButton snapButton_;
    juce::TextButton gridButton_;
    juce::TextButton undoButton_;
    juce::TextButton redoButton_;
    juce::TextButton copyToDeviceButton_;
    juce::Label hintLabel_;
    juce::Label inspectorTitleLabel_;
    juce::Label inspectorKindLabel_;
    juce::Label inspectorBindingLabel_;
    juce::Label controlPrimaryBindingCaption_;
    juce::Label controlSecondaryBindingCaption_;
    juce::Label controlTertiaryBindingCaption_;
    juce::Label controlQuaternaryBindingCaption_;
    juce::ComboBox controlPrimaryBindingSelector_;
    juce::ComboBox controlSecondaryBindingSelector_;
    juce::ComboBox controlTertiaryBindingSelector_;
    juce::ComboBox controlQuaternaryBindingSelector_;
    juce::Label controlLabelCaption_;
    juce::Label controlXCaption_;
    juce::Label controlYCaption_;
    juce::Label controlWidthCaption_;
    juce::Label controlHeightCaption_;
    juce::TextEditor controlLabelEditor_;
    juce::TextEditor controlXEditor_;
    juce::TextEditor controlYEditor_;
    juce::TextEditor controlWidthEditor_;
    juce::TextEditor controlHeightEditor_;
    juce::TextButton applyInspectorButton_;
    bool refreshingInspector_ {};

    std::function<void(const ModuleInterfaceState&)> stateChangedHandler_;
    std::function<void(const juce::String&, double)> parameterChangedHandler_;
    std::function<void(bool, std::uint8_t, std::uint8_t)>
        portCreationHandler_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleInterfaceEditorComponent)
};

} // namespace cydaw::app
