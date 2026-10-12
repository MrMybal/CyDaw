#include "PluginWindows.h"

#include "InterfaceEditor.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui.h>

#include <algorithm>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

PluginWindows::PluginWindows(Studio& studio, UiState& state)
    : studio_(studio), state_(state), renderer_(std::make_unique<InterfaceEditor>(studio, state)), presets_(studio, state)
{
}

PluginWindows::~PluginWindows() = default;

std::string PluginWindows::windowId(const PluginWindow& window)
{
    const auto id = juce::String(window.track.isBus() ? "b" + juce::String(static_cast<juce::int64>(window.track.bus))
                                 : window.track.master ? "m"
                                 : window.track.midi   ? "i"
                                                       : "a")
                    + juce::String(static_cast<juce::int64>(window.track.midi ? window.track.midiId : window.track.audioIndex)) + "-"
                    + juce::String(static_cast<juce::int64>(window.nodeId));
    return ("###plugin" + id).toStdString();
}

void PluginWindows::draw()
{
    std::vector<PluginWindow> closed;
    for (const auto& window : state_.pluginWindows) {
        auto* slot = studio_.graphFor(window.track);
        if (slot == nullptr || (!window.track.master && !studio_.isValid(window.track))) {
            closed.push_back(window);
            continue;
        }
        auto& graph = slot->graph();
        // A card made of a graph shows its own; the track's whole graph (an
        // instrument) its root's.
        juce::String name;
        auto interface = graph.editorInterface();
        if (window.nodeId != 0) {
            const auto nodes = graph.editorNodes();
            const auto node = std::find_if(nodes.begin(), nodes.end(), [&window](const auto& candidate) { return candidate.id == window.nodeId; });
            if (node == nodes.end()) {
                closed.push_back(window);
                continue;
            }
            name = node->title;
            interface = graph.editorNodeInterface(window.nodeId);
        } else {
            name = graph.editorGraphName().trim();
        }
        if (name.isEmpty())
            name = studio_.trackName(window.track);
        const auto width = interface.windowWidth != 0 ? static_cast<float>(interface.windowWidth) : 720.0f;
        const auto height = interface.windowHeight != 0 ? static_cast<float>(interface.windowHeight) : 420.0f;
        const auto id = juce::String(window.track.isBus() ? "b" + juce::String(static_cast<juce::int64>(window.track.bus))
                                     : window.track.master ? "m"
                                     : window.track.midi   ? "i"
                                                           : "a")
                        + juce::String(static_cast<juce::int64>(window.track.midi ? window.track.midiId : window.track.audioIndex)) + "-"
                        + juce::String(static_cast<juce::int64>(window.nodeId));
        const auto windowKey = windowId(window);
        const auto title = name + "  —  " + studio_.trackName(window.track) + juce::String(windowKey);
        ui::beforeDetachable(windowKey.c_str());
        ImGui::SetNextWindowSize(ImVec2(dp(width * 0.5f) * 2.0f, dp(height * 0.5f) * 2.0f + 2.0f * ImGui::GetFrameHeightWithSpacing()),
                                 ImGuiCond_Appearing);
        auto open = true;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::palette().bg1);
        if (ImGui::Begin(title.toRawUTF8(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
                                                        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking)) {
        // Out of the studio's window, as a plug-in's (another screen), and back.
        {
            const auto headerY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - dp(24.0f));
            (void) ui::detachButton(windowKey.c_str(), ImVec2(dp(24.0f), dp(22.0f)));
            ImGui::SetCursorPosY(headerY);
        }

            // Its presets, as a plugin's window has them: a card's, or (the
            // instrument's window) the track's whole chain.
            const auto track = window.track;
            const auto nodeId = window.nodeId;
            if (const auto holder = nodeId != 0 ? studio_.devicePresetHolder(track, nodeId) : studio_.chainPresetHolder(track)) {
                presets_.bar("windowPresets", *holder,
                             [this, track, nodeId] {
                                 return nodeId != 0 ? studio_.devicePresetState(track, nodeId) : studio_.chainPresetState(track);
                             },
                             [this, track, nodeId](const juce::var& preset, juce::String& error) {
                                 return nodeId != 0 ? studio_.applyDevicePreset(track, nodeId, preset, error)
                                                    : studio_.applyChainPreset(track, preset, error);
                             },
                             std::min(ImGui::GetContentRegionAvail().x, dp(420.0f)));
                ImGui::Separator();
            }
            const auto room = ImGui::GetContentRegionAvail();
            const auto shown = std::any_of(interface.controls.begin(), interface.controls.end(), [](const auto& control) {
                return control.kind != app::ModuleControlKind::inputPort && control.kind != app::ModuleControlKind::outputPort;
            });
            if (!shown) {
                ui::emptyState(L("Pas encore d'interface", "No interface yet"),
                               L("Dessinez-la dans l'atelier (onglet Interface) : elle s'affiche ici comme la fenêtre d'un plugin.",
                                 "Design it in the workshop (Interface tab): it shows here as a plugin's window."));
            } else {
                renderer_->drawPlayed(id.toRawUTF8(), interface, room, [&graph](const juce::String& key, const double value) {
                    (void) graph.editorSetParameter(key, value);
                });
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
        if (!open)
            closed.push_back(window);
    }
    for (const auto& window : closed)
        std::erase(state_.pluginWindows, window);
}

} // namespace cydaw::v2
