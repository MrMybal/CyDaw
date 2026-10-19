#pragma once
#include "ModeProcessor.h"
#include "Studio.h"
#include "UiState.h"
#include <cydaw/realtime/RtPublisher.h>
#include <functional>
#include <imgui.h>

namespace cydaw::v2
{
class ImGuiHost;
class DevicesView;
class GraphEditor;
class MixerView;
class RoutingView;
class DeviceSignals;
class DeviceEditorView;
class PluginWindows;
class SampleCatalog;
class LibraryWindow;
class JsfxView;
// An independent Studio mixer, with the very same graph, device and routing
// views. Its inputs are blocks supplied by the Foundry timeline or the DJ
// decks.
class ModeProcessing final : public app::ModeProcessor
{
  public:
    explicit ModeProcessing(ImGuiHost *host = nullptr);
    ~ModeProcessing() override;
    void prepare(double rate, int block) override;
    void process(const app::ModeInput *, int count, float *, float *, int frames) override;
    void processFoundry(const engine::foundry::Session &, double at, double step, float *, float *, int frames) override;
    void syncFoundry(const engine::foundry::Session &session);
    bool copyMixTo(engine::foundry::Session &session);
    void ensureInput(std::uint64_t id, const juce::String &name, bool midi = false, bool bus = false);
    struct Parameter
    {
        std::string binding, label;
        float minimum{}, maximum{1}, value{};
    };
    std::vector<Parameter> parameters(std::uint64_t id) const;
    // An input's effects as a graph that renders on any thread (createVst3RuntimeState); void when none.
    [[nodiscard]] juce::var inputRuntimeState(std::uint64_t id) const;
    void selectMaster();
    void showEditor()
    {
        requestedEditorFrames_ = 3;
        panel_ = 4;
        selectPanel_ = true;
        state_.bringToFront = Panel::none;
    }
    void chooseInstrument(std::uint64_t id);
    juce::Result loadSample(std::uint64_t id, const juce::File &file);
    void selectInput(std::uint64_t id);
    bool routeInput(std::uint64_t id, std::uint64_t destinationId);
    void showPanel(int panel) { requestedEditorFrames_=0;panel_=std::clamp(panel,0,5);selectPanel_=true;state_.bringToFront=Panel::none; }
    void draw(const std::function<void()> &editor = {}, bool visible = true, const std::function<void()> &collapse = {},
              const std::function<void()> &subtracks = {});
    int selectedPanel() const noexcept { return panel_; }
    bool undo(bool redo = false);
    [[nodiscard]] juce::var serialise() const;
    void restore(const juce::var &state);
    [[nodiscard]] std::uint64_t revision() const;
    [[nodiscard]] juce::var command(const juce::var &request);
    static engine::foundry::Sound render(const engine::foundry::Session &, const juce::var &processing, std::int64_t start,
                                         std::int64_t end, double tail = 0.0);

  private:
    struct Input;
    Studio studio_{true};
    UiState state_;
    ImGuiHost *host_{};
    std::map<std::uint64_t, std::uint64_t> trackKeys_;
    std::map<std::uint64_t, std::uint64_t> midiSignatures_;
    struct GraphSnapshot {
        const GraphSlot* source{};
        std::uint64_t revision{};
        juce::var state;
    };
    mutable std::map<std::uint64_t, GraphSnapshot> graphSnapshots_;
    std::vector<std::unique_ptr<Input>> inputs_;
    realtime::RtPublisher<std::vector<Input *>> publishedInputs_;
    struct MasterGain
    {
        engine::foundry::AutomationLane lane;
        double rate{48000};
    };
    realtime::RtPublisher<MasterGain> masterGainAutomation_;
    std::unique_ptr<DeviceSignals> signals_;
    std::unique_ptr<DevicesView> devices_;
    std::unique_ptr<GraphEditor> graph_;
    std::unique_ptr<MixerView> mixer_;
    std::unique_ptr<RoutingView> routing_;
    std::unique_ptr<DeviceEditorView> editors_;
    std::unique_ptr<PluginWindows> windows_;
    juce::AudioBuffer<float> output_;
    double timeline_{};
    bool timelineActive_{};
    bool insideFoundry_{};
    double rate_{48000};
    int block_{512};
    int panel_{};
    int requestedEditorFrames_{};
    bool selectPanel_{};
    bool focused_{};
    std::unique_ptr<SampleCatalog> catalog_;
    std::unique_ptr<LibraryWindow> library_;
    std::unique_ptr<JsfxView> jsfx_;
    void openLibrary();
    std::optional<TrackRef> inputTrack(std::uint64_t id) const;
    void drawTargets();
};
} // namespace cydaw::v2
