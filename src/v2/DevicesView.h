#pragma once

#include "PresetMenu.h"
#include "Studio.h"
#include "UiState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include "DeviceSignals.h"
#include "DeviceVisuals.h"
#include "IsolatedPluginHost.h"
#include "NativeDspRuntime.h"
#include "SignalAnalysis.h"

#include <imgui.h>

#include <array>
#include <optional>
#include <map>
#include <memory>
#include <vector>

// The selected track's chain in one horizontal row, in signal order: every
// card of its graph as a device with one or two rows of knobs, then the
// plugins of an audio track. "+" opens the catalogue with a search.

namespace cydaw::v2 {

class SamplerView;
class PrismView;
class KickView;
class SynthView;
class BatterieView;
class RackView;
class InterfaceEditor;
class ModulatorsView;

class DevicesView final {
public:
    DevicesView(Studio& studio, UiState& state, DeviceSignals& signals);
    ~DevicesView();
    void draw();
    // Who draws a sampler card's zones (AppUi owns it: its editor window).
    void setSamplerView(SamplerView* sampler) noexcept { sampler_ = sampler; }
    void setPrismView(PrismView* prism) noexcept { prism_ = prism; }
    void setKickView(KickView* kick) noexcept { kick_ = kick; }
    void setSynthView(SynthView* synth) noexcept { synth_ = synth; }
    void setBatterieView(BatterieView* batterie) noexcept { batterie_ = batterie; }
    void setRackView(RackView* rack) noexcept { rack_ = rack; }

private:
    // The strip being drawn: what every card's drawing shares.
    struct CardMove {
        std::uint64_t card {};
        std::uint64_t head {};
        int branch { -1 };
        std::uint64_t before {};
    };
    struct Strip {
        const TrackRef& track;
        GraphSlot* slot {};
        app::NodeGraphComponent& graph;
        const std::vector<app::NodeGraphComponent::EditorNode>& nodes;
        std::map<juce::String, float>& modulated;
        const visual::Setter& setParameter;
        std::optional<std::uint64_t> removeNode;
        std::optional<CardMove> moveCard;
    };
    // Where a card sits: the main chain (head 0) or a container's branch;
    // the card after it there (0: the end); how deep in containers.
    struct CardPlace {
        std::uint64_t head {};
        int branch { -1 };
        std::uint64_t next {};
        int depth {};
    };
    void drawCard(Strip& strip, const app::NodeGraphComponent::EditorNode& node, float height, const CardPlace& place);
    [[nodiscard]] bool movable(const Strip& strip, const app::NodeGraphComponent::EditorNode& node) const;
    // A container's chosen branch: its cards after the container's card,
    // inside its frame, and a "+" at its end.
    void drawBranch(Strip& strip, const app::NodeGraphComponent::EditorNode& head, ImVec2 headOrigin, float height,
                    const CardPlace& place);
    // A container's own panel on its card: its branches, its mix.
    void drawContainerPanel(Strip& strip, const app::NodeGraphComponent::EditorNode& head, ImVec2 at, ImVec2 size, ImU32 colour);
    void drawCatalogue(const TrackRef& track);
    // The catalogue as a tree (nothing searched): My devices, the graphs by
    // their category, whole chains, the nodes.
    void drawCatalogueTree(const TrackRef& track,
                           const std::vector<const app::NodeGraphComponent::DeviceCatalogueEntry*>& shown,
                           const app::NodeGraphComponent::DeviceCatalogueEntry*& chosen,
                           std::optional<juce::File>& chosenFile);
    void drawPlugins(const TrackRef& track, float height);
    // A plug-in's parameters in a window of their own: knobs or a list.
    void drawPluginParameters();
    void openPluginWindow(const TrackRef& track, std::size_t plugin, const juce::File& file, const juce::String& name);
    // One of its parameters as a knob: its value as the plug-in writes it;
    // a right click automates it.
    void pluginKnob(const TrackRef& track, std::size_t plugin, const juce::String& pluginName,
                    app::IsolatedPluginHost::ParameterSnapshot& parameter, float diameter, int labelLength);
    // A parameter's right-click menu: automate it in the arrangement.
    void openParameterMenu(const TrackRef& track, const juce::String& binding, const juce::String& label);
    void drawParameterMenu();
    // A card's list of values: every parameter it has, raw, searched, the
    // last touched first (a knob turned anywhere goes to the top).
    void drawValueList(const TrackRef& track, app::NodeGraphComponent& graph, const app::NodeGraphComponent::EditorNode& node,
                       ImVec2 at, ImVec2 size, ImU32 colour, const std::map<juce::String, float>& modulated);
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::array<char, 64>> valueSearch_;
    // Each card's modulators (a column at its left) and the knobs' rings.
    std::unique_ptr<ModulatorsView> modulators_;
    // The quick catalogue opened from a container's branch: where its device
    // goes (the head, the branch).
    std::optional<std::pair<std::uint64_t, int>> catalogueBranch_;
    // The track's key (its routing key and pad), for UiState's per-card maps.
    std::uint64_t trackKey_ {};


    // A plugin's parameters as its worker last reported them, refreshed a
    // few times a second while shown, and the ones pinned as knobs.
    struct PluginParameters {
        std::vector<app::IsolatedPluginHost::ParameterSnapshot> values;
        double fetchedAt { -1.0e9 };
        juce::String error;
    };
    PluginParameters& pluginParameters(std::size_t trackIndex, std::size_t pluginIndex, const juce::File& file,
                                       bool refresh);
    [[nodiscard]] std::vector<std::uint32_t> shownParameters(const TrackRef& track, std::size_t plugin,
                                                             const PluginParameters& parameters) const;

    Studio& studio_;
    UiState& state_;
    DeviceSignals& signals_;
    char search_[96] {};
    // My devices, read again now and then while the catalogue is open.
    std::vector<Studio::LibraryDevice> library_;
    double libraryTime_ { -100.0 };
    bool focusSearch_ {};
    int highlight_ {};
    ImGuiID cataloguePopup_ {};
    bool openCatalogue_ {};
    ImGuiID parameterPopup_ {};
    std::optional<TrackRef> menuTrack_;
    juce::String menuBinding_;
    juce::String menuLabel_;
    std::map<juce::String, PluginParameters> pluginParameters_;
    // The plug-ins' output levels (left, right), falling as a meter's.
    std::vector<std::pair<float, float>> pluginLevels_;
    std::size_t pluginLevelsTrack_ { static_cast<std::size_t>(-1) };
    double pluginLevelsTaken_ {};
    std::size_t pluginPopupTrack_ {};
    std::size_t pluginPopupIndex_ {};
    juce::File pluginPopupFile_;
    juce::String pluginPopupName_;
    char pluginSearch_[64] {};
    TrackRef pluginWindowTrack_;
    bool pluginWindowOpen_ {};
    bool pluginWindowFocus_ {};
    int pluginWindowView_ {}; // 0 knobs, 1 list
    std::unique_ptr<juce::FileChooser> chooser_;
    // The presets of a card, a plug-in, the track's chain.
    PresetMenu presets_;
    SamplerView* sampler_ {};
    PrismView* prism_ {};
    KickView* kick_ {};
    SynthView* synth_ {};
    BatterieView* batterie_ {};
    RackView* rack_ {};
    // A device's designed interface (the workshop's), played in its card.
    std::unique_ptr<InterfaceEditor> designed_;
    void drawSoundModelPane(const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId, ImVec2 at,
                            ImVec2 size, ImU32 colour);
};

} // namespace cydaw::v2
