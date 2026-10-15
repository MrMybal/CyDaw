#include "InstrumentEffects.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <optional>

namespace cydaw::v2::instrument_effects {

using ui::dp;
using ui::L;

void draw(DeviceHost& studio, UiState& state, const TrackRef& track, const juce::String& ownType,
          const juce::String& instrumentName, const std::span<const Choice> choices, const ImU32 colour)
{
    auto* slot = studio.graphFor(track);
    if (slot == nullptr)
        return;
    auto& graph = slot->graph();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", (ui::french()
                                   ? juce::String::fromUTF8("Les effets de la piste, après ") + instrumentName
                                         + juce::String::fromUTF8(", dans l'ordre où ils sont ajoutés (Périphériques les "
                                                                  "montre aussi). Un clic en ajoute un ; la croix le retire.")
                                   : "The track's effects, after " + instrumentName
                                         + ", in the order they are added (Devices shows them too). A click adds one; the "
                                           "cross takes it out.")
                                  .toRawUTF8());
    ImGui::PopTextWrapPos();
    for (const auto& choice : choices) {
        ImGui::PushID(choice.key);
        const auto label = juce::String("+ ") + (ui::french() ? juce::String::fromUTF8(choice.french) : juce::String(choice.english));
        if (ImGui::Button(label.toRawUTF8())) {
            if (const auto result = studio.insertDevice(track, choice.key); result.failed()) {
                state.status = result.getErrorMessage();
                state.statusIsError = true;
                state.statusTime = ImGui::GetTime();
            }
        }
        ImGui::PopID();
        ImGui::SameLine(0.0f, dp(4.0f));
        if (ImGui::GetContentRegionAvail().x < dp(110.0f))
            ImGui::NewLine();
    }
    ImGui::NewLine();
    ImGui::Separator();
    // The effects there, in the chain's order (left to right), with their knobs.
    auto nodes = graph.editorNodes();
    std::stable_sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) { return a.position.x < b.position.x; });
    std::optional<std::uint64_t> removing;
    auto any = false;
    ImGui::BeginChild("##effects", ImVec2(0.0f, 0.0f));
    for (const auto& node : nodes) {
        if (node.boundary || node.typeKey == ownType || graph.editorIsVoiceNode(node.id) || node.parameters.empty())
            continue;
        any = true;
        ImGui::PushID(static_cast<int>(node.id));
        ImGui::BeginChild("##effect", ImVec2(0.0f, dp(108.0f)), ImGuiChildFlags_Borders);
        ImGui::PushFont(theme::boldFont(), 0.0f);
        ImGui::TextUnformatted(node.title.toRawUTF8());
        ImGui::PopFont();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - dp(24.0f));
        if (ImGui::SmallButton(icon::close))
            removing = node.id;
        ui::tooltip(L("Retirer cet effet", "Remove this effect"));
        for (std::size_t at = 0; at < node.parameters.size() && at < 12; ++at) {
            const auto& parameter = node.parameters[at];
            ImGui::PushID(static_cast<int>(at));
            auto current = static_cast<float>(parameter.value);
            ui::KnobStyle style;
            style.diameter = 28.0f;
            const auto shortLabel = parameter.label.length() > 9 ? parameter.label.substring(0, 8) + "." : parameter.label;
            const auto labelText = shortLabel.toStdString();
            style.label = labelText.c_str();
            const auto unit = parameter.unit.toStdString();
            style.unit = unit.c_str();
            style.colour = colour;
            if (parameter.minimum > 0.0 && (parameter.unit == "Hz" || parameter.unit == "ms"))
                style.flags |= ui::knobLogarithmic;
            if (ui::knob("##k", &current, static_cast<float>(parameter.minimum), static_cast<float>(parameter.maximum),
                         static_cast<float>(parameter.defaultValue), style))
                (void) graph.editorSetParameter(parameter.binding, current);
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
    if (!any)
        ImGui::TextDisabled("%s", L("Aucun effet pour l'instant.", "No effect yet."));
    ImGui::EndChild();
    if (removing)
        (void) graph.editorRemoveFromChain(*removing);
}

} // namespace cydaw::v2::instrument_effects
