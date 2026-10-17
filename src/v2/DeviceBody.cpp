#include "DeviceBody.h"

#include "CardFiles.h"
#include "InterfaceEditor.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

DeviceBody deviceBodyOf(app::NodeGraphComponent& graph, const app::NodeGraphComponent::EditorNode& node,
                        const app::NodeGraphComponent::EditorNativeSpan* span)
{
    DeviceBody body;
    body.parameters.reserve(node.parameters.size());
    for (const auto& parameter : node.parameters)
        body.parameters.push_back({ parameter.binding, parameter.label, parameter.unit, parameter.minimum, parameter.maximum,
                                    parameter.value, parameter.defaultValue });
    body.surface = visual::inferSurface(body.parameters, span != nullptr, span != nullptr && span->instrument, node.typeKey);
    // A device made of a graph may have an interface drawn for it in the
    // workshop: a tab of its own (-2), beside what its card shows.
    body.interface = node.hasInterface ? graph.editorNodeInterface(node.id) : app::ModuleInterfaceState {};
    body.designed = node.hasInterface && std::any_of(body.interface.controls.begin(), body.interface.controls.end(), [](const auto& control) {
        return control.kind != app::ModuleControlKind::inputPort && control.kind != app::ModuleControlKind::outputPort;
    });
    return body;
}

void drawDeviceBody(DeviceBody& body, int& pane, DeviceHost& host, UiState& state, PresetMenu& presets, InterfaceEditor& designed,
                    const TrackRef& track, app::NodeGraphComponent& graph, const app::NodeGraphComponent::EditorNode& node,
                    const visual::Live& live, const char* detachId)
{
    auto& surface = body.surface;
    // What its card shows, all at once (-1, every setting under them), or one
    // display large (0…); its drawn interface (-2). A device of one display
    // shows it large with all its controls.
    const auto single = surface.panes.size() == 1;
    const auto lowest = body.designed ? -2 : single ? 0 : -1;
    const auto highest = static_cast<int>(surface.panes.size()) - 1;
    pane = surface.panes.empty() ? -2 : std::clamp(pane, lowest, highest);
    if (single && pane == -1)
        pane = 0;

    const visual::Setter set = [&graph, &state](const visual::Param& parameter, const double value) {
        (void) graph.editorSetParameter(parameter.binding, value);
        state.focused = Panel::devices;
    };

    // The header: its presets, its displays when it has several, the
    // button that takes the window out of the studio's.
    const auto nodeId = node.id;
    const auto headerTop = ImGui::GetCursorPosY();
    const auto rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    if (const auto holder = host.devicePresetHolder(track, nodeId)) {
        auto* devices = &host;
        presets.bar("devicePresets", *holder, [devices, track, nodeId] { return devices->devicePresetState(track, nodeId); },
                    [devices, track, nodeId](const juce::var& preset, juce::String& error) {
                        return devices->applyDevicePreset(track, nodeId, preset, error);
                    },
                    std::min(ImGui::GetContentRegionAvail().x * 0.42f, dp(380.0f)));
        ImGui::SameLine(0.0f, dp(14.0f));
    }
    if (!surface.panes.empty() && (surface.panes.size() > 1 || body.designed))
        for (auto index = single ? 0 : -1; index < static_cast<int>(surface.panes.size()); ++index) {
            if (index > -1)
                ImGui::SameLine(0.0f, dp(4.0f));
            ImGui::PushID(index);
            if (index < 0) {
                // Every display at once, as the card has them.
                auto all = pane == -1;
                if (ui::toggle(L("TOUT", "ALL"), &all, theme::palette().accent))
                    pane = -1;
                ui::tooltip(L("Tout ce que montre sa carte, en grand : chaque section et ses réglages",
                              "All its card shows, large: every section and its settings"));
                ImGui::PopID();
                ImGui::SameLine(0.0f, dp(4.0f));
                continue;
            }
            auto current = index == pane;
            if (ui::toggle(visual::paneTitle(surface.panes[static_cast<std::size_t>(index)]), &current, theme::palette().accent))
                pane = index;
            ImGui::PopID();
            ImGui::SameLine(0.0f, dp(4.0f));
        }
    if (body.designed && !surface.panes.empty()) {
        ImGui::SameLine(0.0f, dp(10.0f));
        auto drawn = pane == -2;
        if (ui::toggle(L("INTERFACE", "INTERFACE"), &drawn, theme::palette().accent))
            pane = -2;
        ui::tooltip(L("L'interface dessinée pour lui dans l'atelier, comme la fenêtre d'un plugin",
                      "The interface drawn for it in the workshop, as a plugin's window"));
    }
    if (detachId != nullptr) {
        const auto button = ImVec2(dp(24.0f), dp(22.0f));
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightEdge - button.x));
        ImGui::SetCursorPosY(headerTop);
        (void) ui::detachButton(detachId, button);
    } else {
        ImGui::NewLine();
    }
    ImGui::Spacing();

    const auto colour = IM_COL32(node.colour.getRed(), node.colour.getGreen(), node.colour.getBlue(), 255);
    const auto origin = ImGui::GetCursorScreenPos();
    const auto available = ImGui::GetContentRegionAvail();
    const ImVec2 size(available.x, std::max(dp(240.0f), available.y - dp(2.0f)));
    const CardFileScope cardFile(host, state, track, graph, node.id, node.typeKey);
    if (pane == -2) {
        ImGui::SetCursorScreenPos(origin);
        designed.drawPlayed(("designed" + juce::String(static_cast<juce::int64>(node.id))).toRawUTF8(), body.interface, size,
                            [&graph](const juce::String& key, const double value) { (void) graph.editorSetParameter(key, value); }, &graph.editorAnalysis());
    } else if (pane < 0) {
        auto chosen = -1;
        (void) visual::drawOverview("##overview", surface.panes, body.parameters, origin, size, colour, live, set, surface.knobs, &chosen);
        if (chosen >= 0)
            pane = chosen;
    } else {
        (void) visual::drawLargePane("##large", surface.panes[static_cast<std::size_t>(pane)], body.parameters, origin, size, colour,
                                     live, set, &surface.knobs);
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(size);
}

} // namespace cydaw::v2
