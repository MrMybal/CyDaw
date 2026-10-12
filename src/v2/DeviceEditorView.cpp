#include "DeviceEditorView.h"

#include "CardFiles.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <map>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

DeviceEditorView::DeviceEditorView(Studio& studio, UiState& state, DeviceSignals& signals)
    : studio_(studio), state_(state), signals_(signals), presets_(studio, state)
{
}

std::string DeviceEditorView::windowId(const TrackRef& track, const std::uint64_t nodeId)
{
    const auto kind = track.isBus()   ? "b" + juce::String(static_cast<juce::int64>(track.bus))
                      : track.master  ? juce::String("m")
                      : track.midi    ? "i" + juce::String(static_cast<juce::int64>(track.midiId))
                                      : "a" + juce::String(static_cast<juce::int64>(track.audioIndex));
    return ("###device-" + kind + "-" + juce::String(static_cast<juce::int64>(nodeId))).toStdString();
}

void DeviceEditorView::draw()
{
    // The sound is tapped for the selected track, which the device panel
    // shows: once a frame whatever the windows open on it.
    if (state_.selectedTrack)
        for (const auto& expanded : state_.expandedPanes)
            if (expanded.track == *state_.selectedTrack) {
                if (auto* slot = studio_.graphFor(expanded.track))
                    signals_.update(expanded.track, *slot);
                break;
            }
    std::vector<std::pair<TrackRef, std::uint64_t>> closed;
    for (std::size_t index = 0; index < state_.expandedPanes.size(); ++index) {
        auto& expanded = state_.expandedPanes[index];
        if (!drawWindow(expanded, static_cast<int>(index)))
            closed.emplace_back(expanded.track, expanded.nodeId);
    }
    for (const auto& [track, nodeId] : closed)
        state_.closeExpandedPane(track, nodeId);
}

bool DeviceEditorView::drawWindow(ExpandedPane& expanded, const int order)
{
    auto* slot = studio_.graphFor(expanded.track);
    if (slot == nullptr)
        return false;
    auto& graph = slot->graph();
    const auto nodes = graph.editorNodes();
    const auto found = std::find_if(nodes.begin(), nodes.end(), [&expanded](const auto& node) { return node.id == expanded.nodeId; });
    // The device was removed.
    if (found == nodes.end())
        return false;
    const auto& node = *found;

    std::vector<visual::Param> parameters;
    parameters.reserve(node.parameters.size());
    for (const auto& parameter : node.parameters)
        parameters.push_back({ parameter.binding, parameter.label, parameter.unit, parameter.minimum, parameter.maximum,
                               parameter.value, parameter.defaultValue });
    // Another track's device shows its curves alone.
    const auto selected = state_.selectedTrack && *state_.selectedTrack == expanded.track;
    const auto* span = selected ? signals_.span(node.id) : nullptr;
    const auto surface = visual::inferSurface(parameters, span != nullptr, span != nullptr && span->instrument);
    if (surface.panes.empty())
        return false;
    expanded.pane = std::clamp(expanded.pane, 0, static_cast<int>(surface.panes.size()) - 1);

    const auto id = windowId(expanded.track, expanded.nodeId);
    if (expanded.detach) {
        ui::setDetached(id.c_str(), true);
        expanded.detach = false;
    }
    ui::beforeDetachable(id.c_str());
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(dp(1'180.0f), viewport->Size.x * 0.88f), std::min(dp(760.0f), viewport->Size.y * 0.88f)),
                             ImGuiCond_FirstUseEver);
    // Each a little lower and to the right of the last, not on top of it.
    const auto step = dp(28.0f) * static_cast<float>(order % 6);
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f + step, viewport->Pos.y + viewport->Size.y * 0.45f + step),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(dp(620.0f), dp(420.0f)), ImVec2(FLT_MAX, FLT_MAX));
    if (expanded.focus) {
        ImGui::SetNextWindowFocus();
        expanded.focus = false;
    }
    auto open = true;
    const auto title = node.title + juce::String::fromUTF8("  \xe2\x80\x94  ") + studio_.trackName(expanded.track);
    const auto name = juce::String::fromUTF8(L("Périphérique", "Device")) + " : " + title + juce::String(id);
    auto flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
    if (ui::isDetached(id.c_str()))
        flags |= ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin(name.toRawUTF8(), &open, flags)) {
        ImGui::PushID(id.c_str());
        // Where the modulation puts each parameter it moves: the graph's own
        // cables, then the track's modulation.
        std::map<juce::String, float> modulated = graph.editorModulatedParameters();
        if (!expanded.track.midi && studio_.modulationActive(expanded.track)) {
            const auto output = studio_.modulationOutput(expanded.track);
            for (const auto& target : studio_.modulationTargets(expanded.track)) {
                if (!target.enabled)
                    continue;
                const auto low = std::min(target.minimum, target.maximum);
                const auto high = std::max(target.minimum, target.maximum);
                const auto value = juce::jlimit(low, high, target.base + output * target.amount * (target.maximum - target.minimum));
                modulated[target.binding] = static_cast<float>(value);
            }
        }
        DeviceSignals::Histories histories;
        visual::Live live;
        if (selected)
            live = signals_.live(node.id, histories);
        live.modulated = &modulated;
        if (const auto control = graph.editorControlValue(node.id)) {
            live.hasControl = true;
            live.control = *control;
            live.running = live.running || studio_.engine().isPlaying();
        }
        const visual::Setter set = [&graph, this](const visual::Param& parameter, const double value) {
            (void) graph.editorSetParameter(parameter.binding, value);
            state_.focused = Panel::devices;
        };

        // The header: its presets, its displays when it has several, the
        // button that takes the window out of the studio's.
        const auto track = expanded.track;
        const auto nodeId = expanded.nodeId;
        const auto headerTop = ImGui::GetCursorPosY();
        const auto rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        if (const auto holder = studio_.devicePresetHolder(track, nodeId)) {
            presets_.bar("devicePresets", *holder, [this, track, nodeId] { return studio_.devicePresetState(track, nodeId); },
                         [this, track, nodeId](const juce::var& preset, juce::String& error) {
                             return studio_.applyDevicePreset(track, nodeId, preset, error);
                         },
                         std::min(ImGui::GetContentRegionAvail().x * 0.42f, dp(380.0f)));
            ImGui::SameLine(0.0f, dp(14.0f));
        }
        if (surface.panes.size() > 1)
            for (auto index = 0; index < static_cast<int>(surface.panes.size()); ++index) {
                if (index > 0)
                    ImGui::SameLine(0.0f, dp(4.0f));
                ImGui::PushID(index);
                auto current = index == expanded.pane;
                if (ui::toggle(visual::paneTitle(surface.panes[static_cast<std::size_t>(index)]), &current, theme::palette().accent))
                    expanded.pane = index;
                ImGui::PopID();
                ImGui::SameLine(0.0f, dp(4.0f));
            }
        const auto button = ImVec2(dp(24.0f), dp(22.0f));
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightEdge - button.x));
        ImGui::SetCursorPosY(headerTop);
        (void) ui::detachButton(id.c_str(), button);
        ImGui::Spacing();

        const auto colour = IM_COL32(node.colour.getRed(), node.colour.getGreen(), node.colour.getBlue(), 255);
        const auto origin = ImGui::GetCursorScreenPos();
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size(available.x, std::max(dp(240.0f), available.y - dp(2.0f)));
        const CardFileScope cardFile(studio_, state_, expanded.track, graph, node.id, node.typeKey);
        (void) visual::drawLargePane("##large", surface.panes[static_cast<std::size_t>(expanded.pane)], parameters, origin, size, colour,
                                     live, set, &surface.knobs);
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(size);
        ImGui::PopID();
    }
    ImGui::End();
    return open;
}

} // namespace cydaw::v2
