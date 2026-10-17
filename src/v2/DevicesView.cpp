#include "DevicesView.h"

#include "CardFiles.h"
#include "InterfaceEditor.h"
#include "GraphLayout.h"
#include "NodeTree.h"
#include "SamplerView.h"
#include "PrismView.h"
#include "KickView.h"
#include "SynthView.h"
#include "BatterieView.h"
#include "RackView.h"
#include "DeviceEditorView.h"
#include "ModulatorsView.h"
#include "PluginWindows.h"
#include "SoundModelFile.h"
#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <functional>
#include <initializer_list>

#include <algorithm>
#include <map>
#include <cmath>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

constexpr float kKnob = 32.0f;
constexpr float kCell = 54.0f;

ImU32 toImColour(const juce::Colour colour)
{
    return IM_COL32(colour.getRed(), colour.getGreen(), colour.getBlue(), colour.getAlpha());
}

juce::String fold(const juce::String& text)
{
    auto out = text.toLowerCase();
    static const char* pairs[][2] = { { "é", "e" }, { "è", "e" }, { "ê", "e" }, { "à", "a" }, { "â", "a" },
                                      { "î", "i" }, { "ô", "o" }, { "ù", "u" }, { "û", "u" }, { "ç", "c" } };
    for (const auto& pair : pairs)
        out = out.replace(juce::String::fromUTF8(pair[0]), pair[1]);
    return out;
}

// A card's face for a device without one of its own: each display small
// with the controls it has under it (its main ones), then its other settings
// in columns. Each part as narrow as its controls let it be.
constexpr float kFaceKnob = 26.0f;

struct FacePart {
    int pane { -1 };         // into the surface's panes; -1: the other settings
    std::vector<int> others; // the other settings (parameters)
    float width {};
    float pictureHeight {};
};

// The controls laid out from the left, onto the next line when the width is
// full; the height they take. Drawn when `draw`.
float flowControls(std::vector<visual::Param>& parameters, const std::vector<int>& indices, const ImVec2 origin, const float width,
                   const bool draw, const ImU32 colour, const visual::Live& live, const visual::Setter& set)
{
    const auto gap = dp(4.0f);
    auto x = origin.x;
    auto y = origin.y;
    auto line = 0.0f;
    for (const auto index : indices) {
        auto& parameter = parameters[static_cast<std::size_t>(index)];
        const auto size = visual::compactControlSize(parameter, kFaceKnob);
        if (x > origin.x && x + size.x > origin.x + width) {
            x = origin.x;
            y += line + gap;
            line = 0.0f;
        }
        if (draw)
            (void) visual::compactControl(parameter, ImVec2(x, y), size.x, colour, live, set, kFaceKnob);
        x += size.x + gap;
        line = std::max(line, size.y);
    }
    return y + line - origin.y;
}

// The narrowest width from `least` on (by steps) where the controls fit in
// `height`; `most` when none does.
float fitWidth(std::vector<visual::Param>& parameters, const std::vector<int>& indices, const float least, const float most,
               const float height)
{
    const visual::Live none;
    const visual::Setter nothing = [](const visual::Param&, double) {};
    auto width = least;
    while (width < most && flowControls(parameters, indices, ImVec2(0.0f, 0.0f), width, false, 0, none, nothing) > height + 0.5f)
        width += dp(24.0f);
    return std::min(width, most);
}

std::vector<int> paneControls(const visual::Pane& pane, std::vector<visual::Param>& parameters, const char* id)
{
    std::vector<int> indices;
    for (const auto slot : visual::cardSlots(id, pane, parameters))
        if (slot >= 0 && slot < static_cast<int>(pane.parameters.size()) && pane.parameters[static_cast<std::size_t>(slot)] >= 0)
            indices.push_back(pane.parameters[static_cast<std::size_t>(slot)]);
    return indices;
}

std::vector<FacePart> cardFace(const visual::Surface& surface, std::vector<visual::Param>& parameters, const float height)
{
    std::vector<FacePart> parts;
    const auto many = surface.panes.size() >= 3;
    for (std::size_t index = 0; index < surface.panes.size(); ++index) {
        const auto& pane = surface.panes[index];
        ImGui::PushID(static_cast<int>(index));
        const auto controls = paneControls(pane, parameters, "##pane");
        ImGui::PopID();
        // At least a row of its controls under it, two when they are many.
        auto widest = 0.0f;
        for (const auto control : controls)
            widest = std::max(widest, visual::compactControlSize(parameters[static_cast<std::size_t>(control)], kFaceKnob).x);
        const auto rowHeight = visual::compactControlSize(visual::Param {}, kFaceKnob).y;
        const auto rows = controls.empty() ? 0.0f : controls.size() <= (many ? 2u : 4u) ? 1.0f : 2.0f;
        const auto controlsHeight = rows * rowHeight + (rows > 1.0f ? dp(4.0f) : 0.0f);
        const auto pictureHeight = std::max(dp(56.0f), height - controlsHeight - (rows > 0.0f ? dp(6.0f) : 0.0f));
        const auto picture = std::clamp(visual::paneWidth(pane, pictureHeight) * (many ? 0.6f : 0.8f), dp(many ? 110.0f : 130.0f),
                                        dp(many ? 150.0f : 230.0f));
        const auto controlsWidth = controls.empty() ? 0.0f
                                                    : fitWidth(parameters, controls, std::max(widest, many ? widest : picture), dp(420.0f),
                                                               height - pictureHeight - dp(6.0f));
        parts.push_back({ static_cast<int>(index), {}, std::max(picture, controlsWidth), pictureHeight });
    }
    if (!surface.knobs.empty()) {
        auto widest = 0.0f;
        for (const auto index : surface.knobs)
            widest = std::max(widest, visual::compactControlSize(parameters[static_cast<std::size_t>(index)], kFaceKnob).x);
        parts.push_back({ -1, surface.knobs, fitWidth(parameters, surface.knobs, widest, dp(600.0f), height), 0.0f });
    }
    return parts;
}

} // namespace

DevicesView::DevicesView(Studio& studio, UiState& state, DeviceSignals& signals)
    : studio_(studio), state_(state), signals_(signals), presets_(studio, state),
      designed_(std::make_unique<InterfaceEditor>(studio, state)), modulators_(std::make_unique<ModulatorsView>(studio, state))
{
}

DevicesView::~DevicesView() = default;

void DevicesView::draw()
{
    const auto& p = theme::palette();
    cataloguePopup_ = ImGui::GetID("##deviceCatalogue");
    parameterPopup_ = ImGui::GetID("##parameterMenu");
    if (!state_.selectedTrack) {
        ui::emptyState(L("Aucune piste sélectionnée", "No track selected"),
                       L("Sélectionnez une piste pour voir sa chaîne.", "Select a track to see its chain."));
        return;
    }
    auto track = *state_.selectedTrack;
    // A rack's pad opened: its own chain (its instrument and effects).
    if (state_.openRackPad) {
        if (state_.openRackPad->first == track && studio_.rackPad(track, static_cast<std::size_t>(state_.openRackPad->second)) != nullptr)
            track.pad = state_.openRackPad->second;
        else
            state_.openRackPad.reset();
    }
    // Its cards' knobs are controllers' targets (learn mode).
    const ui::LearnTrackScope learnTrack(studio_.learnPrefix(track), studio_.trackName(track).toStdString());
    auto* slot = studio_.graphFor(track);
    if (slot == nullptr)
        return;
    auto& graph = slot->graph();

    // The cards in signal order: left to right as the graph lays them out.
    auto nodes = graph.editorNodes();
    std::vector<const app::NodeGraphComponent::EditorNode*> chain;
    // Prisme plays the whole voice: the voice section's other cards (the
    // allocator, the envelope, the gain, the voice filter) do nothing it does
    // not, and stay out of sight.
    // Kick and the synth family's models the same.
    const auto wholeVoice = [](const juce::String& typeKey) {
        return typeKey == "prism-synth" || typeKey == "kick-synth" || SynthView::handles(typeKey) || BatterieView::handles(typeKey)
               || RackView::handles(typeKey);
    };
    const auto prismVoice = std::any_of(nodes.begin(), nodes.end(), [&](const auto& node) { return wholeVoice(node.typeKey); });
    // The cards' modulators, and their modulations for the knobs: each in its
    // card's column, out of the chain.
    const auto trackKey = studio_.routingKey(track) + (track.pad >= 0 ? static_cast<std::uint64_t>(track.pad + 1) << 48 : 0);
    modulators_->begin(track, trackKey, graph, nodes);
    const ui::ModulationHostScope modulationScope(modulators_->host());
    for (const auto& node : nodes)
        if (!node.boundary && !(prismVoice && !wholeVoice(node.typeKey) && graph.editorIsVoiceNode(node.id))
            && !modulators_->owned(node.id))
            chain.push_back(&node);
    std::stable_sort(chain.begin(), chain.end(), [](const auto* a, const auto* b) {
        if (std::abs(a->position.x - b->position.x) > 0.02f)
            return a->position.x < b->position.x;
        return a->position.y < b->position.y;
    });

    ImGui::BeginChild("##chain", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    // A strip, as in Live or Bitwig: a tall panel does not make tall devices.
    const auto height = std::min(ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ScrollbarSize - dp(2.0f), dp(270.0f));
    const auto colourBar = theme::fromArgb(studio_.trackColour(track));

    // The track's name, as the chain's first column.
    {
        const auto origin = ImGui::GetCursorScreenPos();
        auto* list = ImGui::GetWindowDrawList();
        const ImVec2 size(dp(26.0f), height);
        list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), p.bg2, dp(4.0f));
        list->AddRectFilled(origin, ImVec2(origin.x + dp(4.0f), origin.y + size.y), colourBar, dp(4.0f), ImDrawFlags_RoundCornersLeft);
        // A pad's chain: back to its rack, at the top of the column.
        if (track.pad >= 0) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(3.0f), origin.y + dp(4.0f)));
            if (ui::iconButton(icon::back, L("Revenir au rack", "Back to the rack"), false, p.accent, ImVec2(dp(20.0f), dp(20.0f))))
                state_.openRackPad.reset();
            ImGui::SetCursorScreenPos(origin);
        }
        // Vertical name.
        const auto name = track.pad >= 0 ? studio_.rackPad(track.owner(), static_cast<std::size_t>(track.pad))->name : studio_.trackName(track);
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.85f);
        auto y = origin.y + dp(8.0f);
        for (auto index = 0; index < name.length() && y < origin.y + size.y - dp(14.0f); ++index) {
            const auto glyph = juce::String::charToString(name[index]).toUpperCase();
            const auto glyphSize = ImGui::CalcTextSize(glyph.toRawUTF8());
            list->AddText(ImVec2(origin.x + (size.x - glyphSize.x) * 0.5f + dp(2.0f), y), p.textDim, glyph.toRawUTF8());
            y += glyphSize.y * 0.92f;
        }
        ImGui::PopFont();
        // The whole chain's presets (an instrument and its effects, an audio
        // track's effects), at the foot of the column, above the instrument's.
        if (const auto holder = studio_.chainPresetHolder(track)) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(3.0f), origin.y + size.y - dp(track.midi ? 69.0f : 23.0f)));
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
            presets_.button("chainPresets", *holder, [this, track] { return studio_.chainPresetState(track); },
                            [this, track](const juce::var& preset, juce::String& error) {
                                return studio_.applyChainPreset(track, preset, error);
                            },
                            ImVec2(dp(20.0f), dp(20.0f)));
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(origin);
        }
        // A MIDI track's instrument opens in the workshop from the foot of
        // the column; the column itself is the last item, for the row.
        if (track.midi) {
            const auto designed = [&slot] {
                const auto controls = slot->graph().editorInterface().controls;
                return std::any_of(controls.begin(), controls.end(), [](const auto& control) {
                    return control.kind != app::ModuleControlKind::inputPort && control.kind != app::ModuleControlKind::outputPort;
                });
            }();
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
            if (designed) {
                const PluginWindow window { track, 0 };
                ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(3.0f), origin.y + size.y - dp(46.0f)));
                if (ui::iconButton(icon::view, L("La fenêtre de l'instrument, comme un plugin", "The instrument's window, as a plugin's"),
                                   state_.pluginWindowOpen(window), p.accent, ImVec2(dp(20.0f), dp(20.0f))))
                    state_.togglePluginWindow(window);
            }
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(3.0f), origin.y + size.y - dp(23.0f)));
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
            if (ui::iconButton(icon::edit, L("Éditer l'instrument dans l'atelier de périphériques",
                                             "Edit the instrument in the device workshop"),
                               false, 0, ImVec2(dp(20.0f), dp(20.0f)))) {
                state_.workshopRequest = WorkshopRequest { WorkshopRequest::Kind::trackDevice, track, 0, {} };
                state_.bringToFront = Panel::workshop;
            }
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(origin);
        }
        ImGui::Dummy(size);
        ImGui::SameLine(0.0f, dp(6.0f));
    }

    if (chain.empty()) {
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0.0f, height * 0.35f));
        ImGui::TextDisabled("%s", L("Aucun périphérique : le signal passe tel quel.", "No devices: the signal passes as is."));
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, dp(10.0f));
    }

    // Where the modulation puts each parameter it moves, as a knob position:
    // the graph's own cables, then the track's modulation.
    std::map<juce::String, float> modulated = graph.editorModulatedParameters();
    if (!track.midi && studio_.modulationActive(track)) {
        const auto output = studio_.modulationOutput(track);
        for (const auto& target : studio_.modulationTargets(track)) {
            if (!target.enabled)
                continue;
            const auto low = std::min(target.minimum, target.maximum);
            const auto high = std::max(target.minimum, target.maximum);
            const auto range = target.maximum - target.minimum;
            const auto value = juce::jlimit(low, high, target.base + output * target.amount * range);
            modulated[target.binding] = static_cast<float>(value);
        }
    }

    // And the automation's: where each lane puts its parameter at the
    // playhead.
    for (const auto& binding : studio_.automatedParameters(track))
        if (const auto value = studio_.laneValueNow(track, binding))
            modulated[binding] = *value;

    // A right click on a display's field opens the parameter's menu.
    visual::setParameterMenu([this, track](const visual::Param& parameter) {
        openParameterMenu(track, parameter.binding, parameter.label);
    });

    signals_.update(track, *slot);
    const visual::Setter setParameter = [&graph, this](const visual::Param& parameter, const double value) {
        (void) graph.editorSetParameter(parameter.binding, value);
        state_.focused = Panel::devices;
    };

    std::optional<std::uint64_t> removeNode;
    // An effect moves along the chain, dragged by its name: the signal goes
    // in and out of it (an instrument's voice stays first).
    const auto movable = [&](const app::NodeGraphComponent::EditorNode& node) {
        if (node.boundary || wholeVoice(node.typeKey) || (track.midi && graph.editorIsVoiceNode(node.id)))
            return false;
        const auto has = [&node](const bool input) {
            return std::any_of(node.ports.begin(), node.ports.end(), [input](const auto& port) {
                return port.input == input && port.kind == 0 && port.key != "sidechain-in";
            });
        };
        return has(true) && has(false);
    };
    // The card after `at` in the chain that the signal goes through (0: the
    // chain's end).
    const auto nextMovable = [&](const std::size_t at) -> std::uint64_t {
        for (auto next = at + 1; next < chain.size(); ++next)
            if (movable(*chain[next]))
                return chain[next]->id;
        return 0;
    };
    std::optional<std::pair<std::uint64_t, std::uint64_t>> moveCard;
    constexpr const char* cardPayload = "CYDAW_DEVICE_CARD";
    for (std::size_t chainIndex = 0; chainIndex < chain.size(); ++chainIndex) {
        // Inside a module: English, whatever the language.
        const ui::ModuleText english;
        const auto* node = chain[chainIndex];
        // What the device shows: displays first, then the knobs left over.
        std::vector<visual::Param> parameters;
        parameters.reserve(node->parameters.size());
        for (const auto& parameter : node->parameters)
            // Not those kept off the card's face (an SFZ's dozens of
            // controls, which have their own tab).
            if (parameter.showInDevice)
                parameters.push_back({ parameter.binding, parameter.label, parameter.unit, parameter.minimum, parameter.maximum,
                                       parameter.value, parameter.defaultValue });
        const auto* span = signals_.span(node->id);
        const auto instrument = span != nullptr && span->instrument;
        auto surface = visual::inferSurface(parameters, span != nullptr, instrument, node->typeKey);
        // Prisme draws itself (its window has the rest).
        const auto prismCard = node->typeKey == "prism-synth" && prism_ != nullptr;
        const auto kickCard = node->typeKey == "kick-synth" && kick_ != nullptr;
        const auto synthCard = synth_ != nullptr && SynthView::handles(node->typeKey);
        const auto batterieCard = batterie_ != nullptr && BatterieView::handles(node->typeKey);
        const auto rackCard = rack_ != nullptr && RackView::handles(node->typeKey);
        // A device made in the workshop with an interface of its own shows
        // that interface, played, as its plugin window does (scaled to the
        // card's height), instead of a row of knobs.
        // Only while it stays readable: at 60 % of its size at least (a
        // taller panel shows it); smaller, the knobs, and its window.
        std::optional<app::ModuleInterfaceState> designed;
        if (node->composite && node->hasInterface && !node->collapsed) {
            auto shownInterface = graph.editorNodeInterface(node->id);
            if(shownInterface.deviceDesigned) {
                shownInterface.controls=shownInterface.deviceControls;
                shownInterface.windowWidth=shownInterface.deviceWidth; shownInterface.windowHeight=shownInterface.deviceHeight;
                shownInterface.windowDesign=shownInterface.deviceDesign;
            }
            const auto designedHeight = dp(static_cast<float>(shownInterface.windowHeight != 0 ? shownInterface.windowHeight : 420));
            const auto readable = (height - dp(43.0f)) / std::max(1.0f, designedHeight) >= 0.6f;
            if ((readable || shownInterface.deviceDesigned) && std::any_of(shownInterface.controls.begin(), shownInterface.controls.end(), [](const auto& control) {
                    return control.kind != app::ModuleControlKind::inputPort && control.kind != app::ModuleControlKind::outputPort;
                }))
                designed = std::move(shownInterface);
        }
        const auto missingPlugin = node->typeKey == "missing-plugin" || node->typeKey == "missing-instrument";
        if (prismCard || kickCard || synthCard || batterieCard || rackCard || designed || missingPlugin) {
            surface.knobs.clear();
            surface.panes.clear();
        }
        // The sound's spectrum is its window's, when the card shows
        // something else (the window has them all: its panes by this list).
        const auto windowPanes = surface.panes;
        if (surface.panes.size() > 1 || !surface.knobs.empty())
            std::erase_if(surface.panes, [](const visual::Pane& pane) { return pane.kind == visual::PaneKind::spectrum; });
        // At most three displays, those that tell most of the sound (its
        // envelope, its filter, its source, its dynamics…); the others'
        // settings join its other settings. The window shows them all.
        if (surface.panes.size() > 3) {
            const auto rank = [](const visual::PaneKind kind) {
                using visual::PaneKind;
                switch (kind) {
                case PaneKind::envelope: return 0;
                case PaneKind::filter:
                case PaneKind::equalizer:
                case PaneKind::multiFilter:
                case PaneKind::autoFilter:
                case PaneKind::eqThree:
                case PaneKind::eqPro:
                case PaneKind::filterPro: return 1;
                case PaneKind::oscillator:
                case PaneKind::wavetable: return 2;
                case PaneKind::dynamics:
                case PaneKind::compPro:
                case PaneKind::limiter:
                case PaneKind::limiterPro:
                case PaneKind::gate:
                case PaneKind::transient: return 3;
                case PaneKind::saturation: return 5;
                case PaneKind::width:
                case PaneKind::level:
                case PaneKind::spectrum: return 6;
                default: return 4;
                }
            };
            std::vector<std::size_t> order(surface.panes.size());
            for (std::size_t index = 0; index < order.size(); ++index)
                order[index] = index;
            std::stable_sort(order.begin(), order.end(),
                             [&](const std::size_t a, const std::size_t b) { return rank(surface.panes[a].kind) < rank(surface.panes[b].kind); });
            std::vector<bool> kept(surface.panes.size(), false);
            for (std::size_t at = 0; at < 3; ++at)
                kept[order[at]] = true;
            std::vector<visual::Pane> shown;
            std::vector<int> moved;
            for (std::size_t index = 0; index < surface.panes.size(); ++index) {
                if (kept[index]) {
                    shown.push_back(surface.panes[index]);
                    continue;
                }
                for (const auto parameter : surface.panes[index].parameters)
                    if (parameter >= 0)
                        moved.push_back(parameter);
            }
            surface.panes = std::move(shown);
            surface.knobs.insert(surface.knobs.begin(), moved.begin(), moved.end());
        }
        const auto count = static_cast<int>(surface.knobs.size());
        const auto bodyTop = dp(32.0f);
        const auto bodyHeight = height - bodyTop - dp(7.0f);
        ImGui::PushID(static_cast<int>(node->id));
        const auto faces = cardFace(surface, parameters, bodyHeight);
        ImGui::PopID();
        auto panesWidth = 0.0f;
        for (const auto& face : faces)
            panesWidth += face.width + dp(6.0f);
        // A sound model shows itself: its partials over time, what it is.
        const auto modelCard = node->typeKey == "sound-model";
        const auto modelPaneWidth = dp(176.0f);
        if (modelCard)
            panesWidth += modelPaneWidth + dp(6.0f);
        // A plug-in an imported project used that is not here: its place, its name.
        const auto missingCard = node->typeKey == "missing-plugin" || node->typeKey == "missing-instrument";
        const auto missingWidth = dp(210.0f);
        if (missingCard)
            panesWidth += missingWidth + dp(6.0f);
        // A sampler shows its zones over the keys, and takes files.
        const auto samplerCard = node->typeKey == "sampler" && sampler_ != nullptr;
        const auto simpleSampler = samplerCard && [&] {
            const auto controls = graph.editorSamplerControls(node->id);
            return controls && controls->simple;
        }();
        // Granular (Nuée): its grains' knobs in a row under the waveform.
        const auto granular = simpleSampler && std::any_of(node->parameters.begin(), node->parameters.end(), [](const auto& parameter) {
            return parameter.binding.endsWith(".sampler.grains") && parameter.value >= 0.5;
        });
        const auto samplerPaneWidth = dp(granular ? 430.0f : simpleSampler ? 300.0f : 230.0f);
        if (samplerCard)
            panesWidth += samplerPaneWidth + dp(6.0f);
        if (prismCard)
            panesWidth += PrismView::paneWidth() + dp(6.0f);
        if (kickCard)
            panesWidth += KickView::paneWidth() + dp(6.0f);
        const auto synthWidth = synthCard ? synth_->paneWidth(graph, node->id, bodyHeight) : 0.0f;
        if (synthCard)
            panesWidth += synthWidth + dp(6.0f);
        if (batterieCard)
            panesWidth += BatterieView::paneWidth() + dp(6.0f);
        if (rackCard)
            panesWidth += RackView::paneWidth() + dp(6.0f);
        const auto designedWidth = designed ? std::clamp(
                                                  (bodyHeight - dp(4.0f))
                                                      * static_cast<float>(designed->windowWidth != 0 ? designed->windowWidth : 720)
                                                      / static_cast<float>(designed->windowHeight != 0 ? designed->windowHeight : 420),
                                                  dp(120.0f), dp(900.0f))
                                            : 0.0f;
        if (designed)
            panesWidth += designedWidth + dp(6.0f);
        const auto collapsed = node->collapsed;
        // Its list of values, a column at its right.
        const auto listKey = std::make_pair(studio_.routingKey(track) + (track.pad >= 0 ? static_cast<std::uint64_t>(track.pad + 1) << 48 : 0),
                                            node->id);
        const auto listOpen = !collapsed && state_.valueLists.contains(listKey);
        const auto listWidth = dp(232.0f);
        if (listOpen)
            panesWidth += listWidth + dp(6.0f);
        // Its modulators, a column at its left.
        const auto modulatorCount = modulators_->countFor(node->id);
        const auto columnOpen = !collapsed && modulators_->open(node->id);
        if (columnOpen)
            panesWidth += ModulatorsView::width() + dp(6.0f);
        // Its output's meter, along its right edge (an instrument's cards:
        // what its voices give out).
        auto level = signals_.outputLevel(node->id);
        if (!level && track.midi && (wholeVoice(node->typeKey) || graph.editorIsVoiceNode(node->id)))
            level = signals_.instrumentLevel();
        const auto meterWidth = level ? dp(9.0f) : 0.0f;
        const auto width = collapsed ? dp(46.0f)
                                     : std::max(dp(150.0f), dp(8.0f) + panesWidth + dp(2.0f)) + meterWidth
                                           + (level ? dp(4.0f) : 0.0f);
        const auto colour = toImColour(node->colour);
        ImGui::PushID(static_cast<int>(node->id));
        const auto origin = ImGui::GetCursorScreenPos();
        auto* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), p.bg2, dp(5.0f));
        list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + dp(26.0f)), theme::mix(colour, p.bg2 | 0xff000000u, 0.5f),
                            dp(5.0f), ImDrawFlags_RoundCornersTop);
        list->AddRectFilled(ImVec2(origin.x, origin.y + dp(24.0f)), ImVec2(origin.x + width, origin.y + dp(26.0f)), colour);
        list->AddRect(origin, ImVec2(origin.x + width, origin.y + height), p.line, dp(5.0f));

        ImGui::BeginGroup();
        const auto editable = node->composite && !collapsed;
        const auto windowed = editable && node->hasInterface;
        // A card with a window of its own (its display large, its editor,
        // its designed interface): a button opens it out of the studio's.
        const auto separable
            = !collapsed && (windowed || prismCard || kickCard || synthCard || batterieCard || samplerCard || !surface.panes.empty());
        // A compressor or a gate: what its detector listens to.
        const auto sidechained = !collapsed && std::any_of(node->ports.begin(), node->ports.end(), [](const auto& port) {
                                     return port.input && port.kind == 0 && port.key == "sidechain-in";
                                 });
        // What makes or carries sound can be switched off (bypassed).
        const auto bypassable = std::any_of(node->ports.begin(), node->ports.end(), [](const auto& port) {
            return !port.input && port.kind == 0;
        });
        const auto separateWidth = (separable ? dp(22.0f) : 0.0f) + (sidechained ? dp(22.0f) : 0.0f) + (collapsed ? 0.0f : dp(44.0f));
        // The on/off switch comes first, at the left of the title.
        const auto switchWidth = bypassable && !collapsed ? dp(22.0f) : 0.0f;
        if (!collapsed) {
            ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.9f);
            ui::clippedText(list, ImVec2(origin.x + dp(8.0f) + switchWidth, origin.y + dp(5.0f)),
                            width - dp(windowed ? 126.0f : editable ? 104.0f : 82.0f) - separateWidth - switchWidth, p.text,
                            node->title.toRawUTF8());
            ImGui::PopFont();
        }
        // Its name is where it is taken to move it along the chain.
        if (movable(*node)) {
            const auto gripTop = collapsed ? origin.y + dp(28.0f) : origin.y + dp(2.0f);
            const auto gripLeft = collapsed ? origin.x + dp(4.0f) : origin.x + dp(6.0f) + switchWidth;
            const auto gripWidth = collapsed ? width - dp(16.0f)
                                             : std::max(dp(10.0f), width - dp(windowed ? 126.0f : editable ? 104.0f : 82.0f) - separateWidth
                                                                       - switchWidth);
            const auto gripHeight = collapsed ? std::max(dp(10.0f), height - dp(34.0f)) : dp(22.0f);
            ImGui::SetCursorScreenPos(ImVec2(gripLeft, gripTop));
            ImGui::InvisibleButton("##grip", ImVec2(gripWidth, gripHeight));
            if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                const auto id = node->id;
                ImGui::SetDragDropPayload(cardPayload, &id, sizeof(id));
                ImGui::TextUnformatted(node->title.toRawUTF8());
                ImGui::TextDisabled("%s", L("Lâchez-le avant ou après un autre périphérique", "Drop it before or after another device"));
                ImGui::EndDragDropSource();
            } else if (ImGui::IsItemHovered()) {
                ui::tooltip(L("Glissez son nom pour le déplacer dans la chaîne", "Drag its name to move it along the chain"));
            }
        }
        // Header buttons.
        const ImVec2 button(dp(20.0f), dp(20.0f));
        ImGui::SetCursorScreenPos(collapsed ? ImVec2(origin.x + width - (bypassable ? dp(44.0f) : dp(33.0f)), origin.y + dp(3.0f))
                                            : ImVec2(origin.x + dp(3.0f), origin.y + dp(3.0f)));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
        if (bypassable) {
            // On (lit) or switched off: what reaches it goes through, an
            // instrument falls silent; it keeps its settings.
            if (ui::iconButton(icon::power,
                               node->bypassed ? L("Éteint : le son passe sans lui. Cliquez pour le rallumer.",
                                                  "Off: the sound goes past it. Click to switch it back on.")
                                              : L("Allumé. Cliquez pour l'éteindre (bypass) : le son passe sans lui.",
                                                  "On. Click to switch it off (bypass): the sound goes past it."),
                               !node->bypassed, p.play, button))
                (void) graph.editorSetBypassed(node->id, !node->bypassed);
            if (collapsed)
                ImGui::SameLine(0.0f, dp(2.0f));
        }
        if (!collapsed)
            ImGui::SetCursorScreenPos(ImVec2(origin.x + width - (windowed ? dp(112.0f) : editable ? dp(90.0f) : dp(68.0f)) - separateWidth,
                                             origin.y + dp(3.0f)));
        if (!collapsed) {
            // All its values in a list: raw, searched, the last touched first.
            if (ui::iconButton(icon::list,
                               L("Toutes ses valeurs dans une liste : brutes, à chercher, la dernière touchée en haut",
                                 "All its values in a list: raw, searchable, the last touched at the top"),
                               listOpen, p.accent, button)) {
                if (listOpen)
                    state_.valueLists.erase(listKey);
                else
                    state_.valueLists.insert(listKey);
            }
            ImGui::SameLine(0.0f, dp(2.0f));
            // Its modulators: an LFO, steps, an envelope... dragged onto knobs.
            const auto modulatorTip = modulatorCount > 0
                                          ? juce::String(columnOpen ? "Hide" : "Show") + " its modulators ("
                                                + juce::String(static_cast<int>(modulatorCount)) + ")"
                                          : juce::String("Modulators: an LFO, steps, an envelope, a macro... moving its knobs");
            if (ui::iconButton(icon::modulation, modulatorTip.toRawUTF8(), columnOpen || modulatorCount > 0, p.modulation, button))
                modulators_->setOpen(node->id, !columnOpen);
            ImGui::SameLine(0.0f, dp(2.0f));
        }
        if (separable) {
            const PluginWindow window { track, node->id };
            const auto open = prismCard     ? prism_->showsEditor(track, node->id) && ui::isDetached(prism_->editorWindowId(track, node->id).c_str())
                              : kickCard    ? kick_->showsEditor(track, node->id) && ui::isDetached(kick_->editorWindowId(track, node->id).c_str())
                              : synthCard   ? synth_->showsEditor(track, node->id) && ui::isDetached(synth_->editorWindowId(track, node->id).c_str())
                              : batterieCard ? batterie_->showsEditor(track, node->id) && ui::isDetached(batterie_->editorWindowId(track, node->id).c_str())
                              : samplerCard ? sampler_->showsEditor(track, node->id) && ui::isDetached(sampler_->editorWindowId(track, node->id).c_str())
                              : windowed && surface.panes.empty()
                                  ? state_.pluginWindowOpen(window) && ui::isDetached(PluginWindows::windowId(window).c_str())
                                            : state_.expandedPaneOpen(track, node->id)
                                                  && ui::isDetached(DeviceEditorView::windowId(track, node->id).c_str());
            if (ui::iconButton(icon::newWindow,
                               L("Ouvrir dans une fenêtre séparée, hors du studio, comme un VST (sur un autre écran si vous voulez)",
                                 "Open in a separate window, out of the studio, as a VST's (onto another screen if you like)"),
                               open, p.accent, button)) {
                if (prismCard)
                    prism_->openEditor(track, node->id, true);
                else if (kickCard)
                    kick_->openEditor(track, node->id, true);
                else if (synthCard)
                    synth_->openEditor(track, node->id, true);
                else if (batterieCard)
                    batterie_->openEditor(track, node->id, true);
                else if (samplerCard)
                    sampler_->openEditor(track, node->id, true);
                else if (windowed && surface.panes.empty()) {
                    if (!state_.pluginWindowOpen(window))
                        state_.togglePluginWindow(window);
                    ui::setDetached(PluginWindows::windowId(window).c_str(), true);
                } else {
                    state_.openExpandedPane(ExpandedPane { track, node->id, -1, true });
                }
            }
            ImGui::SameLine(0.0f, dp(2.0f));
        }
        if (sidechained) {
            const auto source = studio_.sidechainSource(track, node->id);
            const auto tip = (source ? ui::Ls("Sidechain : ", "Sidechain: ") + studio_.trackName(*source)
                                     : ui::Ls("Sidechain : le détecteur écoute le son du périphérique. Cliquez pour qu'il écoute "
                                              "une autre piste (un kick qui fait pomper une basse).",
                                              "Sidechain: the detector listens to the device's own sound. Click to make it listen "
                                              "to another track (a kick pumping a bass)."))
                                 .toStdString();
            if (ui::iconButton(icon::link, tip.c_str(), source.has_value(), p.accent, button))
                ImGui::OpenPopup("##sidechain");
            if (ImGui::BeginPopup("##sidechain")) {
                ImGui::TextDisabled("%s", L("Ce que le détecteur écoute", "What the detector listens to"));
                ImGui::Separator();
                const auto report = [this](const juce::Result& result) {
                    if (result.failed()) {
                        state_.status = result.getErrorMessage();
                        state_.statusIsError = true;
                        state_.statusTime = ImGui::GetTime();
                    }
                };
                if (ImGui::MenuItem(L("Son propre (pas de sidechain)", "Its own sound (no sidechain)"), nullptr, !source))
                    report(studio_.setSidechainSource(track, node->id, std::nullopt));
                for (const auto& candidate : studio_.tracks()) {
                    if (candidate.master || candidate == track)
                        continue;
                    const auto name = studio_.trackName(candidate);
                    const auto routable = studio_.canRoute(candidate, track);
                    ImGui::PushID(static_cast<int>(studio_.routingKey(candidate)));
                    if (ImGui::MenuItem(name.toRawUTF8(), nullptr, source && *source == candidate, routable))
                        report(studio_.setSidechainSource(track, node->id, candidate));
                    ImGui::PopID();
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine(0.0f, dp(2.0f));
        }
        if (windowed) {
            // Its large window: all its card shows (and the interface drawn
            // for it, a tab); a card with no display, its drawn interface.
            const PluginWindow window { track, node->id };
            const auto large = !surface.panes.empty();
            if (ui::iconButton(icon::view,
                               large ? L("Ouvrir sa grande fenêtre : tout ce que montre la carte, en grand (et l'interface dessinée "
                                         "dans l'atelier, un onglet)",
                                         "Open its large window: all the card shows, large (and the interface designed in the "
                                         "workshop, a tab)")
                                     : L("Ouvrir sa fenêtre, comme un plugin (l'interface dessinée dans l'atelier)",
                                         "Open its window, as a plugin's (the interface designed in the workshop)"),
                               large ? state_.expandedPaneOpen(track, node->id) : state_.pluginWindowOpen(window), p.accent, button)) {
                if (!large)
                    state_.togglePluginWindow(window);
                else if (state_.expandedPaneOpen(track, node->id))
                    state_.closeExpandedPane(track, node->id);
                else
                    state_.openExpandedPane(ExpandedPane { track, node->id, -1 });
            }
            ImGui::SameLine(0.0f, dp(2.0f));
        }
        if (editable) {
            if (ui::iconButton(icon::edit, L("Éditer dans l'atelier de périphériques", "Edit in the device workshop"), false, 0, button)) {
                state_.workshopRequest = WorkshopRequest { WorkshopRequest::Kind::trackDevice, track, node->id, {} };
                state_.bringToFront = Panel::workshop;
            }
            ImGui::SameLine(0.0f, dp(2.0f));
        }
        if (!collapsed)
            if (const auto holder = studio_.devicePresetHolder(track, node->id)) {
                const auto nodeId = node->id;
                presets_.button("devicePresets", *holder, [this, track, nodeId] { return studio_.devicePresetState(track, nodeId); },
                                [this, track, nodeId](const juce::var& preset, juce::String& error) {
                                    return studio_.applyDevicePreset(track, nodeId, preset, error);
                                },
                                button);
                ImGui::SameLine(0.0f, dp(2.0f));
            }
        if (ui::iconButton(collapsed ? icon::chevronRight : icon::chevronLeft,
                           collapsed ? L("Déplier", "Expand") : L("Replier", "Collapse"), false, 0, button))
            graph.editorSetCollapsed(node->id, !collapsed);
        if (!collapsed) {
            ImGui::SameLine(0.0f, dp(2.0f));
            if (ui::iconButton(icon::close, L("Retirer de la chaîne", "Remove from the chain"), false, 0, button))
                removeNode = node->id;
        }
        ImGui::PopFont();

        if (collapsed) {
            // Vertical title.
            ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.8f);
            auto y = origin.y + dp(30.0f);
            for (auto index = 0; index < node->title.length() && y < origin.y + height - dp(14.0f); ++index) {
                const auto glyph = juce::String::charToString(node->title[index]);
                const auto glyphSize = ImGui::CalcTextSize(glyph.toRawUTF8());
                list->AddText(ImVec2(origin.x + (width - glyphSize.x) * 0.5f, y), p.textDim, glyph.toRawUTF8());
                y += glyphSize.y * 0.9f;
            }
            ImGui::PopFont();
        } else {
            if (const auto live = graph.editorControlValue(node->id)) {
                const auto t = std::clamp((*live + 1.0f) * 0.5f, 0.0f, 1.0f);
                list->AddRectFilled(ImVec2(origin.x, origin.y + dp(24.0f)), ImVec2(origin.x + width * t, origin.y + dp(26.0f)),
                                    p.modulation);
            }
            if (count == 0 && surface.panes.empty() && !prismCard && !kickCard && !synthCard && !batterieCard && !samplerCard
                && !modelCard && !designed) {
                ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(8.0f), origin.y + dp(36.0f)));
                ImGui::TextDisabled("%s", node->category.isNotEmpty() ? node->category.toRawUTF8() : "");
            }
            // The displays, side by side.
            DeviceSignals::Histories histories;
            auto live = signals_.live(node->id, histories);
            live.modulated = &modulated;
            if (const auto control = graph.editorControlValue(node->id)) {
                live.hasControl = true;
                live.control = *control;
                live.running = live.running || studio_.engine().isPlaying();
            }
            auto x = origin.x + dp(8.0f);
            if (columnOpen) {
                modulators_->draw(node->id, ImVec2(x, origin.y + bodyTop), ImVec2(ModulatorsView::width(), bodyHeight), colour);
                x += ModulatorsView::width() + dp(6.0f);
            }
            if (modelCard) {
                drawSoundModelPane(track, graph, node->id, ImVec2(x, origin.y + bodyTop), ImVec2(modelPaneWidth, bodyHeight), colour);
                x += modelPaneWidth + dp(6.0f);
            }
            if (missingCard) {
                // As another application shows it: a warning, the plug-in's
                // name, its maker and format, what to do.
                const ImVec2 min(x, origin.y + bodyTop);
                const ImVec2 max(x + missingWidth, origin.y + bodyTop + bodyHeight);
                list->AddRectFilled(min, max, p.bg1, dp(4.0f));
                const auto about = juce::JSON::parse(node->description);
                const auto vendor = about.getProperty("vendor", juce::var()).toString();
                const auto format = about.getProperty("format", juce::var()).toString();
                const auto missingInstrument = node->typeKey == "missing-instrument";
                auto y = min.y + std::max(dp(8.0f), bodyHeight * 0.18f);
                const auto centre = (min.x + max.x) * 0.5f;
                // The warning sign.
                const auto size = dp(15.0f);
                list->AddTriangleFilled(ImVec2(centre, y), ImVec2(centre - size, y + size * 1.7f), ImVec2(centre + size, y + size * 1.7f),
                                        p.warn);
                list->AddRectFilled(ImVec2(centre - dp(1.5f), y + size * 0.55f), ImVec2(centre + dp(1.5f), y + size * 1.15f), p.bg0);
                list->AddCircleFilled(ImVec2(centre, y + size * 1.38f), dp(1.8f), p.bg0);
                y += size * 1.7f + dp(10.0f);
                const auto line = [&](const juce::String& text, const ImU32 lineColour, const float scale, ImFont* font) {
                    ImGui::PushFont(font, ui::currentFontBase() * scale);
                    ui::centredText(list, ImVec2(min.x + dp(6.0f), y), ImVec2(max.x - dp(6.0f), y + ImGui::GetFontSize()), lineColour,
                                    text.toRawUTF8());
                    y += ImGui::GetFontSize() + dp(4.0f);
                    ImGui::PopFont();
                };
                line(missingInstrument ? ui::Ls("Instrument manquant", "Missing instrument") : ui::Ls("Plugin manquant", "Missing plugin"), p.text, 1.0f,
                     theme::boldFont());
                line(node->title, p.textDim, 0.9f, nullptr);
                if (vendor.isNotEmpty() || format.isNotEmpty())
                    line(vendor + (vendor.isNotEmpty() && format.isNotEmpty() ? juce::String::fromUTF8(" · ") : juce::String()) + format,
                         p.textFaint, 0.8f, nullptr);
                ImGui::SetCursorScreenPos(min);
                ImGui::InvisibleButton("##missing", ImVec2(missingWidth, bodyHeight));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", missingInstrument ? L("Le projet importé jouait cet instrument (un plug-in qui n'est pas ici). "
                                                           "Sa place est gardée, silencieuse : choisissez un instrument avec + "
                                                           "pour le remplacer, ses effets restent.",
                                                           "The imported project played this instrument (a plug-in that is not here). "
                                                           "Its place is kept, silent: pick an instrument with + to replace it, its "
                                                           "effects stay.")
                                                       : L("Le projet importé avait cet effet (un plug-in qui n'est pas ici). Sa place est "
                                                           "gardée et le son passe sans lui.",
                                                           "The imported project had this effect (a plug-in that is not here). Its place "
                                                           "is kept and the sound passes without it."));
                x += missingWidth + dp(6.0f);
            }
            if (samplerCard) {
                sampler_->drawPane(track, graph, node->id, ImVec2(x, origin.y + bodyTop), ImVec2(samplerPaneWidth, bodyHeight),
                                   colour);
                x += samplerPaneWidth + dp(6.0f);
            }
            if (prismCard) {
                prism_->drawPane(track, graph, node->id, ImVec2(x, origin.y + bodyTop), ImVec2(PrismView::paneWidth(), bodyHeight),
                                 colour);
                x += PrismView::paneWidth() + dp(6.0f);
            }
            if (kickCard) {
                kick_->drawPane(track, graph, node->id, ImVec2(x, origin.y + bodyTop), ImVec2(KickView::paneWidth(), bodyHeight),
                                colour);
                x += KickView::paneWidth() + dp(6.0f);
            }
            if (synthCard) {
                synth_->drawPane(track, graph, node->id, ImVec2(x, origin.y + bodyTop), ImVec2(synthWidth, bodyHeight),
                                 colour);
                x += synthWidth + dp(6.0f);
            }
            if (batterieCard) {
                batterie_->drawPane(track, graph, node->id, ImVec2(x, origin.y + bodyTop), ImVec2(BatterieView::paneWidth(), bodyHeight),
                                    colour);
                x += BatterieView::paneWidth() + dp(6.0f);
            }
            if (rackCard) {
                rack_->drawPane(track, ImVec2(x, origin.y + bodyTop), ImVec2(RackView::paneWidth(), bodyHeight), colour);
                x += RackView::paneWidth() + dp(6.0f);
            }
            if (designed) {
                ImGui::SetCursorScreenPos(ImVec2(x, origin.y + bodyTop));
                const auto playedId = "designed" + juce::String(static_cast<juce::int64>(node->id));
                designed_->drawPlayed(playedId.toRawUTF8(), *designed, ImVec2(designedWidth, bodyHeight - dp(4.0f)),
                                      [&graph, this](const juce::String& key, const double value) {
                                          (void) graph.editorSetParameter(key, value);
                                          state_.focused = Panel::devices;
                                      }, &graph.editorAnalysis());
                x += designedWidth + dp(6.0f);
            }
            const CardFileScope cardFile(studio_, state_, track, graph, node->id, node->typeKey);
            for (const auto& face : faces) {
                const auto top = origin.y + bodyTop;
                if (face.pane >= 0) {
                    // The display small, its corner opening it in its window;
                    // its controls under it.
                    const auto& pane = surface.panes[static_cast<std::size_t>(face.pane)];
                    ImGui::PushID(face.pane);
                    auto expand = false;
                    (void) visual::drawPane("##pane", pane, parameters, ImVec2(x, top), ImVec2(face.width, face.pictureHeight), colour, live,
                                            setParameter, false, &expand);
                    if (expand) {
                        const auto shown = std::find_if(windowPanes.begin(), windowPanes.end(), [&pane](const visual::Pane& other) {
                            return other.kind == pane.kind && other.parameters == pane.parameters && other.variant == pane.variant;
                        });
                        state_.openExpandedPane(
                            ExpandedPane { track, node->id, shown != windowPanes.end() ? static_cast<int>(shown - windowPanes.begin()) : 0 });
                    }
                    (void) flowControls(parameters, paneControls(pane, parameters, "##pane"), ImVec2(x, top + face.pictureHeight + dp(6.0f)),
                                        face.width, true, colour, live, setParameter);
                    ImGui::PopID();
                } else {
                    // Its other settings, in columns.
                    ImGui::PushID("##others");
                    (void) flowControls(parameters, face.others, ImVec2(x, top), face.width, true, colour, live, setParameter);
                    ImGui::PopID();
                }
                x += face.width + dp(6.0f);
            }
            if (listOpen) {
                ImGui::PushID("##valueList");
                drawValueList(track, graph, *node, ImVec2(x, origin.y + bodyTop), ImVec2(listWidth, bodyHeight), colour, modulated);
                ImGui::PopID();
                x += listWidth + dp(6.0f);
            }
        }
        if (node->bypassed)
            // Switched off: the card greyed under its header.
            list->AddRectFilled(ImVec2(origin.x + 1.0f, origin.y + dp(26.0f)), ImVec2(origin.x + width - 1.0f, origin.y + height - 1.0f),
                                (p.bg0 & 0x00ffffffu) | 0x9a000000u, dp(5.0f), ImDrawFlags_RoundCornersBottom);
        // What comes out of it: a meter down its right edge.
        if (level) {
            const auto meterTop = origin.y + dp(collapsed ? 50.0f : 30.0f);
            const auto meterLeft = origin.x + width - (collapsed ? dp(8.0f) : meterWidth) - dp(3.0f);
            ImGui::SetCursorScreenPos(ImVec2(meterLeft, meterTop));
            ui::meter("##output", ImVec2(collapsed ? dp(6.0f) : meterWidth, std::max(dp(20.0f), origin.y + height - dp(6.0f) - meterTop)),
                      level->left, level->right, level->left * 0.7f, level->right * 0.7f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("Ce qui sort de ce périphérique", "What comes out of this device"));
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(width, height));
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload(cardPayload, ImGuiDragDropFlags_AcceptBeforeDelivery
                                                                                    | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                const auto dragged = *static_cast<const std::uint64_t*>(payload->Data);
                // Its left half: before it; its right half (or a card that
                // stays first): after it.
                const auto before = movable(*node) && ImGui::GetIO().MousePos.x < origin.x + width * 0.5f;
                const auto target = before ? node->id : nextMovable(chainIndex);
                const auto lineX = before ? origin.x - dp(3.0f) : origin.x + width + dp(2.0f);
                if (dragged != node->id) {
                    list->AddLine(ImVec2(lineX, origin.y), ImVec2(lineX, origin.y + height), p.accent, dp(3.0f));
                    if (payload->IsDelivery())
                        moveCard = std::make_pair(dragged, target);
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::SameLine(0.0f, dp(5.0f));
    }
    if (removeNode) {
        // Its modulators go with it.
        modulators_->removeWith(*removeNode);
        graph.editorRemoveFromChain(*removeNode);
    }
    // A card dropped elsewhere in the chain: moved there, the cards laid out
    // again in the order the signal now takes.
    if (moveCard && graph.editorMoveInChain(moveCard->first, moveCard->second)) {
        graph_layout::arrange(graph);
        state_.focused = Panel::devices;
    }

    // VST3 plugins sit on audio tracks (the master has its graph's devices).
    if (!track.midi && !track.master)
        drawPlugins(track, height);

    // Add a device.
    {
        const auto origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(dp(56.0f), height);
        ImGui::InvisibleButton("##addDevice", size);
        auto* list = ImGui::GetWindowDrawList();
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("CYDAW_DEVICE_CARD", ImGuiDragDropFlags_AcceptBeforeDelivery
                                                                                            | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                list->AddLine(ImVec2(origin.x - dp(3.0f), origin.y), ImVec2(origin.x - dp(3.0f), origin.y + size.y), p.accent, dp(3.0f));
                if (payload->IsDelivery() && graph.editorMoveInChain(*static_cast<const std::uint64_t*>(payload->Data), 0)) {
                    graph_layout::arrange(graph);
                    state_.focused = Panel::devices;
                }
            }
            ImGui::EndDragDropTarget();
        }
        const auto hovered = ImGui::IsItemHovered();
        list->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), hovered ? p.accent : p.lineStrong, dp(5.0f), dp(1.0f));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 1.4f);
        ui::centredText(list, origin, ImVec2(origin.x + size.x, origin.y + size.y * 0.9f), hovered ? p.accent : p.textDim, icon::add);
        ImGui::PopFont();
        ui::tooltip(state_.inlineDeviceCatalogue ? L("Ajouter un effet dans le catalogue", "Add an effect from the catalogue") : L("Ajouter un périphérique : la bibliothèque en grand (clic droit : la liste rapide)",
                      "Add a device: the library, large (right click: the quick list)"));
        if (ImGui::IsItemClicked()) {
            if(state_.inlineDeviceCatalogue) {
                state_.openDeviceCatalogue=true;
            } else {
                state_.showLibrary = true;
                state_.libraryTab = 2;
                state_.libraryTarget = track;
                state_.libraryPick = true;
                state_.libraryFocus = true;
            }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            search_[0] = 0;
            highlight_ = 0;
            focusSearch_ = true;
            if(state_.inlineDeviceCatalogue)state_.openDeviceCatalogue=true;else openCatalogue_=true;
        }
    }
    // The wheel runs the strip sideways wherever nothing under the pointer
    // takes it (the displays and knobs keep theirs), as in Bitwig.
    if (auto& io = ImGui::GetIO(); io.MouseWheel != 0.0f && !io.KeyCtrl && !io.KeyShift && ImGui::IsWindowHovered()
                                   && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive())
        ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * dp(120.0f));
    ImGui::EndChild();
    // Opened from the panel itself, once the strip's own window is closed.
    if (!state_.inlineDeviceCatalogue && std::exchange(state_.openDeviceCatalogue, false)) {
        search_[0] = 0;
        highlight_ = 0;
        openCatalogue_ = true;
    }
    if (std::exchange(openCatalogue_, false))
        ImGui::OpenPopup(cataloguePopup_);
    modulators_->end();
    visual::setParameterMenu({});
    drawCatalogue(track);
    drawPluginParameters();
    drawParameterMenu();
}

void DevicesView::drawValueList(const TrackRef& track, app::NodeGraphComponent& graph, const app::NodeGraphComponent::EditorNode& node,
                                const ImVec2 at, const ImVec2 size, const ImU32 colour, const std::map<juce::String, float>& modulated)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), p.bg1, dp(4.0f));
    // The search, at the top.
    auto& search = valueSearch_[{ studio_.routingKey(track), node.id }];
    ImGui::SetCursorScreenPos(ImVec2(at.x + dp(4.0f), at.y + dp(4.0f)));
    ImGui::SetNextItemWidth(size.x - dp(8.0f));
    ImGui::InputTextWithHint("##search", L("Chercher une valeur…", "Find a value…"), search.data(), search.size());
    const auto filter = juce::String::fromUTF8(search.data()).trim();
    // Every value of the card, those touched first (the last at the top),
    // then the others as the card lists them.
    std::vector<std::pair<std::uint64_t, const app::NodeGraphComponent::EditorParameter*>> order;
    for (const auto& parameter : node.parameters)
        if ((filter.isEmpty() || parameter.label.containsIgnoreCase(filter) || parameter.binding.containsIgnoreCase(filter))
            && !parameter.binding.endsWith(".ownerLow") && !parameter.binding.endsWith(".ownerHigh"))
            order.emplace_back(graph.editorParameterTouch(parameter.binding), &parameter);
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    const auto top = ImGui::GetCursorScreenPos().y + dp(2.0f);
    ImGui::SetCursorScreenPos(ImVec2(at.x, top));
    if (ImGui::BeginChild("##values", ImVec2(size.x, at.y + size.y - top - dp(2.0f)), ImGuiChildFlags_None)) {
        auto* rows = ImGui::GetWindowDrawList();
        const auto rowHeight = dp(30.0f);
        const auto latest = order.empty() ? 0 : order.front().first;
        auto touchedShown = false;
        for (std::size_t index = 0; index < order.size(); ++index) {
            const auto [touch, parameter] = order[index];
            // Between the touched and the others: a line.
            if (touch == 0 && touchedShown) {
                const auto y = ImGui::GetCursorScreenPos().y;
                rows->AddLine(ImVec2(at.x + dp(6.0f), y), ImVec2(at.x + size.x - dp(6.0f), y), p.line, dp(1.0f));
                ImGui::Dummy(ImVec2(0.0f, dp(3.0f)));
                touchedShown = false;
            }
            touchedShown = touchedShown || touch != 0;
            ImGui::PushID(parameter->binding.toRawUTF8());
            const auto rowTop = ImGui::GetCursorScreenPos();
            const ImVec2 rowEnd(rowTop.x + size.x - dp(4.0f), rowTop.y + rowHeight);
            if (touch != 0 && touch == latest)
                rows->AddRectFilled(rowTop, rowEnd, theme::alpha(colour, 0.22f), dp(3.0f));
            const visual::Param shown { parameter->binding, parameter->label, parameter->unit, parameter->minimum, parameter->maximum,
                                        parameter->value, parameter->defaultValue };
            ui::KnobStyle style;
            style.diameter = 22.0f;
            style.flags = ui::knobNoLabel;
            if (parameter->minimum < 0.0 && parameter->maximum > 0.0)
                style.flags |= ui::knobBipolar;
            if (visual::logarithmic(shown))
                style.flags |= ui::knobLogarithmic;
            style.colour = colour;
            if (const auto moved = modulated.find(parameter->binding); moved != modulated.end() && parameter->maximum > parameter->minimum)
                style.modulation = static_cast<float>((moved->second - parameter->minimum) / (parameter->maximum - parameter->minimum));
            ImGui::SetCursorScreenPos(ImVec2(rowTop.x + dp(2.0f), rowTop.y + dp(2.0f)));
            auto value = static_cast<float>(parameter->value);
            {
                const ui::LearnParameterScope learn(parameter->binding, parameter->label);
                if (ui::knob("##knob", &value, static_cast<float>(parameter->minimum), static_cast<float>(parameter->maximum),
                             static_cast<float>(parameter->defaultValue), style)) {
                    (void) graph.editorSetParameter(parameter->binding, value);
                    state_.focused = Panel::devices;
                }
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                openParameterMenu(track, parameter->binding, parameter->label);
            // Its name, its value raw (its unit), its key under the pointer.
            const auto textX = rowTop.x + dp(40.0f);
            const auto textWidth = rowEnd.x - textX - dp(4.0f);
            ui::clippedText(rows, ImVec2(textX, rowTop.y + dp(1.0f)), textWidth, p.text, parameter->label.toRawUTF8());
            auto text = visual::formatValue(shown, parameter->value);
            // What moves it: each modulator and its amount.
            std::vector<ui::KnobModulation> moving;
            if (auto* host = ui::modulationHost())
                host->modulationsOf(parameter->binding.toStdString(), moving);
            for (const auto& modulation : moving)
                text << juce::String::fromUTF8("  \xe2\x86\x9d ") << juce::String(modulation.name)
                     << juce::String::formatted(" %+.2f", modulation.amount);
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.85f);
            ui::clippedText(rows, ImVec2(textX, rowTop.y + dp(15.0f)), textWidth, moving.empty() ? p.textDim : p.modulation,
                            text.toRawUTF8());
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(textX, rowTop.y));
            ImGui::InvisibleButton("##name", ImVec2(std::max(1.0f, textWidth), rowHeight));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%s = %.6g %s\n%s", parameter->label.toRawUTF8(), parameter->binding.toRawUTF8(), parameter->value,
                                  parameter->unit.toRawUTF8(),
                                  L("Clic droit : automatiser, valeur par défaut", "Right click: automate, default value"));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                openParameterMenu(track, parameter->binding, parameter->label);
            ImGui::SetCursorScreenPos(ImVec2(rowTop.x, rowEnd.y + dp(2.0f)));
            ImGui::PopID();
        }
        if (order.empty())
            ImGui::TextDisabled("  %s", L("Aucune valeur", "No value"));
        // The last row's end, held by an item (not by the cursor alone).
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    ImGui::EndChild();
}

void DevicesView::drawSoundModelPane(const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId,
                                     const ImVec2 at, const ImVec2 size, const ImU32 colour)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + size.x, at.y + size.y);
    list->AddRectFilled(at, end, p.bg1, dp(4.0f));
    const auto model = graph.nodeSoundModel(nodeId);
    const auto textHeight = ImGui::GetFontSize() * 2.3f;
    const ImVec2 plotEnd(end.x - dp(4.0f), end.y - textHeight);
    // Its partials over time (the first zone): harmonic number up, time
    // across, louder brighter.
    if (model != nullptr && !model->zones.empty() && model->zones.front().frames > 0
        && model->zones.front().harmonicCount > 0) {
        const auto& zone = model->zones.front();
        const auto columns = std::max(1, std::min(zone.frames, static_cast<int>((plotEnd.x - at.x - dp(4.0f)) / dp(2.0f))));
        const auto rows = std::min(zone.harmonicCount, 32);
        auto loudest = 1.0e-9f;
        for (const auto value : zone.harmonics)
            loudest = std::max(loudest, value);
        const auto cellWidth = (plotEnd.x - at.x - dp(4.0f)) / static_cast<float>(columns);
        const auto cellHeight = (plotEnd.y - at.y - dp(4.0f)) / static_cast<float>(rows);
        for (auto column = 0; column < columns; ++column) {
            const auto frame = std::min(zone.frames - 1, column * zone.frames / columns);
            for (auto row = 0; row < rows; ++row) {
                const auto amplitude = zone.harmonicAt(frame, row);
                const auto level = std::clamp((20.0f * std::log10(std::max(amplitude, 1.0e-9f) / loudest) + 60.0f) / 60.0f, 0.0f, 1.0f);
                if (level <= 0.02f)
                    continue;
                const ImVec2 cell(at.x + dp(2.0f) + column * cellWidth, plotEnd.y - dp(2.0f) - (row + 1) * cellHeight);
                list->AddRectFilled(cell, ImVec2(cell.x + cellWidth + 0.5f, cell.y + cellHeight + 0.5f),
                                    theme::mix(p.bg1 | 0xff000000u, colour, level));
            }
        }
        if (zone.loops()) {
            const auto x0 = at.x + dp(2.0f) + (plotEnd.x - at.x - dp(4.0f)) * zone.loopStart / static_cast<float>(zone.frames);
            const auto x1 = at.x + dp(2.0f) + (plotEnd.x - at.x - dp(4.0f)) * zone.loopEnd / static_cast<float>(zone.frames);
            list->AddLine(ImVec2(x0, at.y + dp(2.0f)), ImVec2(x0, plotEnd.y), p.textDim);
            list->AddLine(ImVec2(x1, at.y + dp(2.0f)), ImVec2(x1, plotEnd.y), p.textDim);
        }
    } else {
        ui::centredText(list, at, plotEnd, p.textFaint, L("Aucun modèle", "No model"));
    }
    // What it is, and another one.
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    const auto description = model != nullptr ? graph.nodeSoundModelDescription(nodeId)
                                              : ui::Ls("Piste › Recréer un son…", "Track › Recreate a sound…");
    // Its zones' notes, when it lists them, as their range ("C#3–G#5"); all
    // of it in its tooltip.
    juce::String shown;
    {
        const auto separator = juce::String::fromUTF8(" \xc2\xb7 ");
        auto rest = description;
        while (rest.isNotEmpty()) {
            auto part = rest.upToFirstOccurrenceOf(separator, false, false);
            rest = rest.fromFirstOccurrenceOf(separator, false, false);
            if (part.contains(", "))
                part = part.upToFirstOccurrenceOf(",", false, false) + juce::String::fromUTF8("\xe2\x80\x93")
                       + part.fromLastOccurrenceOf(", ", false, false);
            shown << (shown.isEmpty() ? juce::String() : separator) << part;
        }
    }
    const ImVec2 textMin(at.x + dp(4.0f), plotEnd.y + dp(2.0f));
    const ImVec2 textMax(end.x - dp(58.0f), end.y);
    ImGui::SetCursorScreenPos(textMin);
    list->PushClipRect(textMin, textMax, true);
    ui::pushTextWrapAt(textMax.x);
    ImGui::TextColored(theme::vec(p.textDim), "%s", shown.toRawUTF8());
    ImGui::PopTextWrapPos();
    list->PopClipRect();
    if (ImGui::IsMouseHoveringRect(textMin, textMax) && ImGui::IsWindowHovered())
        ImGui::SetTooltip("%s", description.toRawUTF8());
    ImGui::SetCursorScreenPos(ImVec2(end.x - dp(54.0f), plotEnd.y + dp(2.0f)));
    if (ImGui::SmallButton(L("Charger…", "Load…"))) {
        chooser_ = std::make_unique<juce::FileChooser>(L("Un modèle de son pour cette carte", "A sound model for this card"),
                                                       Studio::soundModelFolder(), "*.cysound");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, track, nodeId](const juce::FileChooser& chooser) {
                                  const auto file = chooser.getResult();
                                  if (file == juce::File())
                                      return;
                                  // On this very card, even inside a device.
                                  juce::var json;
                                  juce::String error;
                                  auto* slot = studio_.graphFor(track);
                                  if (slot == nullptr || app::loadSoundModelFile(file, json, error) == nullptr
                                      || !slot->graph().setNodeSoundModel(nodeId, json, error)) {
                                      state_.status = error.isNotEmpty() ? error : ui::Ls("Piste introuvable", "No such track");
                                      state_.statusIsError = true;
                                      state_.statusTime = ImGui::GetTime();
                                  }
                              });
    }
    ui::tooltip(L("Un autre modèle (.cysound) sur cette carte ; Piste › Recréer un son… en fait un d'un enregistrement.",
                  "Another model (.cysound) on this card; Track › Recreate a sound… makes one from a recording."));
    if (model != nullptr) {
        ImGui::SetCursorScreenPos(ImVec2(end.x - dp(54.0f), plotEnd.y + dp(2.0f) + ImGui::GetFrameHeight()));
        if (ImGui::SmallButton(L("Lisible", "Readable"))) {
            juce::String error;
            if (const auto made = studio_.buildReadablePatch(graph.nodeSoundModelJson(nodeId), error))
                state_.selectTrack(*made);
            else {
                state_.status = error;
                state_.statusIsError = true;
                state_.statusTime = ImGui::GetTime();
            }
        }
        ui::tooltip(L("Le même son en patch lisible sur une nouvelle piste : une wavetable (l'évolution de ses partiels), "
                      "l'enveloppe et le filtre analysés. Moins fidèle, mais chaque réglage est un réglage usuel.",
                      "The same sound as a readable patch on a new track: a wavetable (its partials' evolution), the "
                      "analysed envelope and filter. Less faithful, but every knob is a usual one."));
    }
    ImGui::PopFont();
}

void DevicesView::openParameterMenu(const TrackRef& track, const juce::String& binding, const juce::String& label)
{
    menuTrack_ = track;
    menuBinding_ = binding;
    menuLabel_ = label;
    // A display's field may show a shortened name ("Gain" for "Bande 3 ·
    // Gain"); the menu gives the whole one.
    if (const auto* slot = studio_.graphFor(track))
        for (const auto& parameter : slot->graph().editorGraphParameters())
            if (parameter.binding == binding)
                menuLabel_ = parameter.label;
    ImGui::OpenPopup(parameterPopup_);
}

void DevicesView::drawParameterMenu()
{
    if (!ImGui::BeginPopup("##parameterMenu"))
        return;
    if (menuTrack_ && studio_.isValid(*menuTrack_)) {
        const auto track = *menuTrack_;
        ImGui::TextDisabled("%s", menuLabel_.toRawUTF8());
        ImGui::Separator();
        const auto automated = !studio_.lanePoints(track, menuBinding_).empty();
        const auto reachable = studio_.canAutomate(track, menuBinding_);
        if (ImGui::MenuItem(automated ? L("Afficher l'automation", "Show the automation")
                                      : L("Automatiser dans l'arrangement", "Automate in the arrangement"),
                            nullptr, false, reachable)) {
            studio_.setShownLane(track, menuBinding_);
            studio_.setAutomationVisible(track, true);
            state_.selectTrack(track);
            state_.bringToFront = Panel::arrangement;
        }
        if (!reachable)
            ImGui::TextDisabled("%s", L("(ce réglage ne s'automatise pas)", "(this setting cannot be automated)"));
        if (automated && ImGui::MenuItem(L("Effacer l'automation", "Clear the automation")))
            studio_.setLanePoints(track, menuBinding_, {});
    }
    ImGui::EndPopup();
}

DevicesView::PluginParameters& DevicesView::pluginParameters(const std::size_t trackIndex, const std::size_t pluginIndex,
                                                             const juce::File& file, const bool refresh)
{
    const auto key = juce::String(static_cast<juce::int64>(trackIndex)) + ":" + juce::String(static_cast<juce::int64>(pluginIndex))
                     + ":" + file.getFullPathName();
    auto& entry = pluginParameters_[key];
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (refresh && now - entry.fetchedAt > 0.4) {
        entry.fetchedAt = now;
        std::vector<app::IsolatedPluginHost::ParameterSnapshot> fresh;
        const auto result = studio_.engine().refreshTrackPluginParameters(trackIndex, pluginIndex, fresh);
        entry.error = result.failed() ? result.getErrorMessage() : juce::String();
        if (result.wasOk())
            entry.values = std::move(fresh);
    }
    return entry;
}

// The parameters a card shows as knobs: the pinned ones, or the first
// automatable ones until the user picks.
std::vector<std::uint32_t> DevicesView::shownParameters(const TrackRef& track, const std::size_t plugin,
                                                        const PluginParameters& parameters) const
{
    if (const auto pins = studio_.pluginPins(track, plugin))
        return *pins;
    std::vector<std::uint32_t> shown;
    for (const auto& parameter : parameters.values) {
        if (!parameter.automatable || parameter.name.isEmpty() || parameter.name.startsWithIgnoreCase("MIDI CC")
            || parameter.name.equalsIgnoreCase("Bypass"))
            continue;
        shown.push_back(parameter.index);
        if (shown.size() >= 8)
            break;
    }
    return shown;
}

void DevicesView::pluginKnob(const TrackRef& track, const std::size_t plugin, const juce::String& pluginName,
                             app::IsolatedPluginHost::ParameterSnapshot& parameter, const float diameter, const int labelLength)
{
    const auto& p = theme::palette();
    ui::KnobStyle style;
    style.diameter = diameter;
    style.colour = p.audio;
    const auto label = parameter.name.length() > labelLength ? parameter.name.substring(0, labelLength - 1) + "." : parameter.name;
    const auto labelText = label.toStdString();
    style.label = labelText.c_str();
    style.format = "%.2f";
    // The value as the plug-in writes it, its unit with it.
    auto text = parameter.text.trim();
    if (text.isNotEmpty() && parameter.label.isNotEmpty() && !text.endsWithIgnoreCase(parameter.label))
        text << " " << parameter.label;
    const auto valueText = text.toStdString();
    style.valueText = valueText.empty() ? nullptr : valueText.c_str();
    const auto unit = parameter.label.toStdString();
    style.unit = unit.empty() ? nullptr : unit.c_str();
    if (parameter.boolean)
        style.flags |= ui::knobInteger;
    auto value = parameter.value;
    ImGui::PushID(static_cast<int>(parameter.index));
    if (ui::knob("##p", &value, 0.0f, 1.0f, parameter.value, style)) {
        const auto set = studio_.engine().setTrackPluginParameter(track.audioIndex, plugin, parameter.index, value);
        if (set.wasOk()) {
            parameter.value = value;
            parameter.text = {};
            studio_.markDirty();
        } else {
            state_.status = set.getErrorMessage();
        }
    }
    if (ImGui::IsItemHovered() && label != parameter.name)
        ImGui::SetTooltip("%s", parameter.name.toRawUTF8());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        const auto lane = Studio::pluginLaneName(plugin, parameter.index);
        studio_.rememberPluginParameterName(lane, parameter.name);
        openParameterMenu(track, lane, pluginName + juce::String::fromUTF8(" \xc2\xb7 ") + parameter.name);
    }
    ImGui::PopID();
}

void DevicesView::openPluginWindow(const TrackRef& track, const std::size_t plugin, const juce::File& file, const juce::String& name)
{
    pluginWindowTrack_ = track;
    pluginPopupTrack_ = track.audioIndex;
    pluginPopupIndex_ = plugin;
    pluginPopupFile_ = file;
    pluginPopupName_ = name;
    pluginSearch_[0] = 0;
    pluginWindowOpen_ = true;
    pluginWindowFocus_ = true;
}

void DevicesView::drawPlugins(const TrackRef& track, const float height)
{
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto plugins = engine.trackPlugins(track.audioIndex);
    // What each plug-in gives out: its meter, falling about 24 dB a second.
    {
        std::vector<float> left;
        std::vector<float> right;
        engine.trackPluginPeaks(track.audioIndex, left, right);
        const auto now = ImGui::GetTime();
        const auto fall = static_cast<float>(std::pow(10.0, -24.0 / 20.0 * std::clamp(now - pluginLevelsTaken_, 0.0, 0.5)));
        pluginLevelsTaken_ = now;
        if (pluginLevelsTrack_ != track.audioIndex)
            pluginLevels_.clear();
        pluginLevelsTrack_ = track.audioIndex;
        pluginLevels_.resize(left.size());
        for (std::size_t index = 0; index < left.size(); ++index)
            pluginLevels_[index] = { std::max(left[index], pluginLevels_[index].first * fall),
                                     std::max(right[index], pluginLevels_[index].second * fall) };
    }
    std::optional<std::size_t> remove;
    std::optional<std::pair<std::size_t, int>> move;
    const auto rows = height > dp(190.0f) ? 2 : 1;
    for (std::size_t index = 0; index < plugins.size(); ++index) {
        const auto& plugin = plugins[index];
        ImGui::PushID(static_cast<int>(index) + 100'000);
        const auto dragging = ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::GetActiveID() != 0;
        auto& parameters = pluginParameters(track.audioIndex, index, plugin.file, plugin.running && !dragging);
        const auto shown = shownParameters(track, index, parameters);
        const auto count = static_cast<int>(shown.size());
        const auto columns = std::max(1, (count + rows - 1) / rows);
        const auto meterWidth = dp(9.0f);
        const auto width = std::max(dp(260.0f), columns * dp(kCell) + dp(18.0f)) + meterWidth + dp(4.0f);
        const auto origin = ImGui::GetCursorScreenPos();
        auto* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), p.bg2, dp(5.0f));
        list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + dp(26.0f)), theme::mix(p.audio, p.bg2 | 0xff000000u, 0.5f),
                            dp(5.0f), ImDrawFlags_RoundCornersTop);
        list->AddRectFilled(ImVec2(origin.x, origin.y + dp(24.0f)), ImVec2(origin.x + width, origin.y + dp(26.0f)), p.audio);
        list->AddRect(origin, ImVec2(origin.x + width, origin.y + height), p.line, dp(5.0f));
        ImGui::BeginGroup();
        // Its own switch, when it has one (a VST3's bypass): the plug-in
        // passes its sound by itself, its latency and state kept.
        auto* bypass = static_cast<app::IsolatedPluginHost::ParameterSnapshot*>(nullptr);
        for (auto& parameter : parameters.values)
            if (parameter.name.equalsIgnoreCase("Bypass"))
                bypass = &parameter;
        const auto bypassWidth = bypass != nullptr ? dp(22.0f) : 0.0f;
        // Running or not, its switch, then the name.
        list->AddCircleFilled(ImVec2(origin.x + dp(11.0f), origin.y + dp(13.0f)), dp(3.5f), plugin.running ? p.play : p.record);
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.9f);
        ui::clippedText(list, ImVec2(origin.x + dp(20.0f) + bypassWidth, origin.y + dp(5.0f)), width - dp(158.0f) - bypassWidth, p.text,
                        plugin.name.toRawUTF8());
        ImGui::PopFont();
        const ImVec2 button(dp(20.0f), dp(20.0f));
        ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(18.0f), origin.y + dp(3.0f)));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
        if (bypass != nullptr) {
            const auto off = bypass->value >= 0.5f;
            if (ui::iconButton(icon::power,
                               off ? L("Éteint : le son passe sans lui. Cliquez pour le rallumer.",
                                       "Off: the sound goes past it. Click to switch it back on.")
                                   : L("Allumé. Cliquez pour l'éteindre (bypass) : le son passe sans lui.",
                                       "On. Click to switch it off (bypass): the sound goes past it."),
                               !off, p.play, button)
                && engine.setTrackPluginParameter(track.audioIndex, index, bypass->index, off ? 0.0f : 1.0f).wasOk())
                bypass->value = off ? 0.0f : 1.0f;
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x + width - dp(136.0f), origin.y + dp(3.0f)));
        // Its place in the chain.
        ImGui::BeginDisabled(index == 0);
        if (ui::iconButton(icon::back, L("Avancer dans la chaîne (avant le précédent)", "Earlier in the chain"), false, 0, button))
            move = std::make_pair(index, -1);
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, dp(2.0f));
        ImGui::BeginDisabled(index + 1 >= plugins.size());
        if (ui::iconButton(icon::chevronRight, L("Reculer dans la chaîne (après le suivant)", "Later in the chain"), false, 0, button))
            move = std::make_pair(index, 1);
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, dp(2.0f));
        if (ui::iconButton(icon::view, L("Ouvrir l'interface du plugin", "Open the plugin's interface"), false, 0, button)) {
            const auto shownEditor = engine.showTrackPluginEditor(track.audioIndex, index);
            if (shownEditor.failed())
                state_.status = shownEditor.getErrorMessage();
        }
        ImGui::SameLine(0.0f, dp(2.0f));
        if (ui::iconButton(icon::more, L("Tous les paramètres, en potards", "Every parameter, as knobs"), false, 0, button))
            openPluginWindow(track, index, plugin.file, plugin.name);
        ImGui::SameLine(0.0f, dp(2.0f));
        if(plugin.file.hasFileExtension("jsfx")||plugin.file.getFileExtension().isEmpty()) {
            if(ImGui::SmallButton("JSFX")){state_.jsfxFileToOpen=plugin.file;state_.showJsfx=true;}
            ImGui::SameLine(0.0f,dp(2.0f));
        }
        if (const auto holder = studio_.pluginPresetHolder(track, static_cast<int>(index))) {
            const auto pluginIndex = static_cast<int>(index);
            ImGui::PushID(pluginIndex);
            const PresetMenu::Programs programs {
                [this, track, pluginIndex](juce::StringArray& names, int& current) {
                    return studio_.pluginPrograms(track, pluginIndex, names, current);
                },
                [this, track, pluginIndex](const int program, juce::String& error) {
                    return studio_.selectPluginProgram(track, pluginIndex, program, error);
                }
            };
            presets_.button("pluginPresets", *holder, [this, track, pluginIndex] { return studio_.pluginPresetState(track, pluginIndex); },
                            [this, track, pluginIndex](const juce::var& preset, juce::String& error) {
                                return studio_.applyPluginPreset(track, pluginIndex, preset, error);
                            },
                            button, &programs);
            ImGui::PopID();
            ImGui::SameLine(0.0f, dp(2.0f));
        }
        if (ui::iconButton(icon::close, L("Retirer le plugin", "Remove the plugin"), false, 0, button))
            remove = index;
        ImGui::PopFont();

        if (!plugin.running) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(8.0f), origin.y + dp(36.0f)));
            ui::pushTextWrapAt(origin.x + width - dp(8.0f));
            ImGui::TextColored(theme::vec(p.record), "%s",
                               L("Arrêté : le processus du plugin ne répond plus.", "Stopped: the plugin's process is not responding."));
            ImGui::PopTextWrapPos();
        } else if (count == 0) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(8.0f), origin.y + dp(36.0f)));
            ui::pushTextWrapAt(origin.x + width - dp(8.0f));
            ImGui::TextDisabled("%s", parameters.error.isNotEmpty() ? parameters.error.toRawUTF8()
                                                                   : L("Aucun paramètre épinglé : … pour en choisir.",
                                                                       "No pinned parameter: … to pick some."));
            ImGui::PopTextWrapPos();
        } else {
            const auto available = height - dp(34.0f);
            const auto rowHeight = std::min(available / static_cast<float>(rows), dp(74.0f));
            const auto blockTop = std::max(0.0f, (available - rowHeight * static_cast<float>(rows)) * 0.3f);
            for (auto slot = 0; slot < count; ++slot) {
                const auto parameterIndex = shown[static_cast<std::size_t>(slot)];
                const auto found = std::find_if(parameters.values.begin(), parameters.values.end(),
                                                [parameterIndex](const auto& value) { return value.index == parameterIndex; });
                if (found == parameters.values.end())
                    continue;
                const auto column = slot / rows;
                const auto row = slot % rows;
                ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(9.0f) + column * dp(kCell),
                                                 origin.y + dp(34.0f) + blockTop + row * rowHeight));
                pluginKnob(track, index, plugin.name, *found, kKnob, 10);
            }
        }
        if (plugin.latencySamples > 0) {
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
            char latency[48];
            std::snprintf(latency, sizeof(latency), "%s %d", L("latence", "latency"), plugin.latencySamples);
            list->AddText(ImVec2(origin.x + dp(8.0f), origin.y + height - ImGui::GetFontSize() - dp(4.0f)), p.textFaint, latency);
            ImGui::PopFont();
        }
        // What comes out of it: a meter down its right edge.
        if (index < pluginLevels_.size()) {
            const auto meterTop = origin.y + dp(30.0f);
            ImGui::SetCursorScreenPos(ImVec2(origin.x + width - meterWidth - dp(3.0f), meterTop));
            ui::meter("##output", ImVec2(meterWidth, std::max(dp(20.0f), origin.y + height - dp(6.0f) - meterTop)), pluginLevels_[index].first,
                      pluginLevels_[index].second, pluginLevels_[index].first * 0.7f, pluginLevels_[index].second * 0.7f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", L("Ce qui sort de ce plugin", "What comes out of this plugin"));
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(width, height));
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::SameLine(0.0f, dp(5.0f));
    }
    if (remove || move) {
        if (remove)
            studio_.removePlugin(track, *remove);
        else
            (void) studio_.movePlugin(track, move->first, move->second);
        // The indices moved: their cached parameters are stale.
        pluginParameters_.clear();
        pluginWindowOpen_ = false;
    }
}

// Every parameter of a plugin, in its own window: as knobs (a grid, the
// plug-in's own value texts) or as a list, with a search; the star puts one
// on the card (and in the modulation's list).
void DevicesView::drawPluginParameters()
{
    if (!pluginWindowOpen_)
        return;
    const auto plugins = studio_.engine().trackPlugins(pluginPopupTrack_);
    if (pluginPopupIndex_ >= plugins.size() || plugins[pluginPopupIndex_].file != pluginPopupFile_) {
        pluginWindowOpen_ = false;
        return;
    }
    const auto& p = theme::palette();
    constexpr const char* windowId = "###pluginParametersWindow";
    ui::beforeDetachable(windowId);
    ImGui::SetNextWindowSize(ImVec2(dp(860.0f), dp(560.0f)), ImGuiCond_FirstUseEver);
    if (std::exchange(pluginWindowFocus_, false))
        ImGui::SetNextWindowFocus();
    auto open = true;
    const auto title = pluginPopupName_ + juce::String::fromUTF8(" \xe2\x80\x94 ") + ui::Ls("paramètres", "parameters") + windowId;
    if (ImGui::Begin(title.toRawUTF8(), &open, ImGuiWindowFlags_NoCollapse)) {
        auto& parameters = pluginParameters(pluginPopupTrack_, pluginPopupIndex_, pluginPopupFile_,
                                            !ImGui::IsMouseDown(ImGuiMouseButton_Left));
        const auto track = pluginWindowTrack_;
        // The header: its name, how many, the search, the view, the window.
        ImGui::PushFont(theme::boldFont(), 0.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(pluginPopupName_.toRawUTF8());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("%d %s", static_cast<int>(parameters.values.size()), L("paramètres", "parameters"));
        ImGui::SameLine(0.0f, dp(14.0f));
        ImGui::SetNextItemWidth(dp(240.0f));
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##pluginSearch", L("Rechercher un paramètre", "Search a parameter"), pluginSearch_,
                                 sizeof(pluginSearch_));
        ImGui::SameLine(0.0f, dp(10.0f));
        auto knobs = pluginWindowView_ == 0;
        if (ui::toggle(L("Potards", "Knobs"), &knobs, p.accent))
            pluginWindowView_ = 0;
        ImGui::SameLine(0.0f, dp(2.0f));
        auto rows = pluginWindowView_ == 1;
        if (ui::toggle(L("Liste", "List"), &rows, p.accent))
            pluginWindowView_ = 1;
        ImGui::SameLine(0.0f, dp(10.0f));
        if (ImGui::Button(L("Interface du plugin", "Plugin's interface")))
            if (const auto shownEditor = studio_.engine().showTrackPluginEditor(pluginPopupTrack_, pluginPopupIndex_); shownEditor.failed())
                state_.status = shownEditor.getErrorMessage();
        ImGui::SameLine(ImGui::GetWindowWidth() - dp(36.0f));
        (void) ui::detachButton(windowId, ImVec2(dp(24.0f), dp(22.0f)));

        const auto query = juce::String::fromUTF8(pluginSearch_).trim();
        std::vector<std::size_t> visible;
        for (std::size_t index = 0; index < parameters.values.size(); ++index) {
            const auto& parameter = parameters.values[index];
            if ((query.isEmpty() || parameter.name.containsIgnoreCase(query)) && (pluginWindowView_ == 1 || parameter.automatable))
                visible.push_back(index);
        }
        auto shown = shownParameters(track, pluginPopupIndex_, parameters);
        const auto togglePin = [&](const std::uint32_t index) {
            if (std::find(shown.begin(), shown.end(), index) != shown.end())
                shown.erase(std::remove(shown.begin(), shown.end(), index), shown.end());
            else
                shown.push_back(index);
            studio_.setPluginPins(track, pluginPopupIndex_, shown);
        };
        const auto footer = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("##pluginParameterBody", ImVec2(0.0f, -footer), ImGuiChildFlags_None);
        if (pluginWindowView_ == 0) {
            // A grid of knobs, row after row, only the rows in view drawn.
            const auto cell = dp(92.0f);
            const auto cellHeight = dp(98.0f);
            const auto columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
            const auto lines = (static_cast<int>(visible.size()) + columns - 1) / columns;
            ImGuiListClipper clipper;
            clipper.Begin(lines, cellHeight);
            while (clipper.Step())
                for (auto line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
                    const auto top = ImGui::GetCursorScreenPos();
                    for (auto column = 0; column < columns; ++column) {
                        const auto at = static_cast<std::size_t>(line * columns + column);
                        if (at >= visible.size())
                            break;
                        auto& parameter = parameters.values[visible[at]];
                        const ImVec2 corner(top.x + static_cast<float>(column) * cell, top.y);
                        const auto pinned = std::find(shown.begin(), shown.end(), parameter.index) != shown.end();
                        if (pinned)
                            ImGui::GetWindowDrawList()->AddRectFilled(corner, ImVec2(corner.x + cell - dp(4.0f), corner.y + cellHeight - dp(4.0f)),
                                                                      theme::alpha(p.accent, 0.10f), dp(4.0f));
                        ImGui::PushID(static_cast<int>(parameter.index) + 500'000);
                        ImGui::SetCursorScreenPos(ImVec2(corner.x + cell - dp(26.0f), corner.y + dp(2.0f)));
                        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.75f);
                        if (ui::iconButton(pinned ? icon::starFill : icon::star,
                                           pinned ? L("Retirer de la carte", "Remove from the card") : L("Épingler sur la carte", "Pin on the card"),
                                           pinned, pinned ? p.accent : 0, ImVec2(dp(20.0f), dp(18.0f))))
                            togglePin(parameter.index);
                        ImGui::PopFont();
                        ImGui::PopID();
                        ImGui::SetCursorScreenPos(ImVec2(corner.x + (cell - dp(44.0f)) * 0.5f - dp(8.0f), corner.y + dp(8.0f)));
                        pluginKnob(track, pluginPopupIndex_, pluginPopupName_, parameter, 40.0f, 13);
                    }
                    ImGui::SetCursorScreenPos(top);
                    ImGui::Dummy(ImVec2(static_cast<float>(columns) * cell, cellHeight));
                }
        } else if (ImGui::BeginTable("##params", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("pin", ImGuiTableColumnFlags_WidthFixed, dp(22.0f));
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("text", ImGuiTableColumnFlags_WidthFixed, dp(110.0f));
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, dp(180.0f));
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()));
            while (clipper.Step())
                for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    auto& parameter = parameters.values[visible[static_cast<std::size_t>(row)]];
                    ImGui::PushID(static_cast<int>(parameter.index));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const auto pinned = std::find(shown.begin(), shown.end(), parameter.index) != shown.end();
                    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
                    if (ui::iconButton(pinned ? icon::starFill : icon::star,
                                       pinned ? L("Retirer de la carte", "Remove from the card") : L("Épingler sur la carte", "Pin on the card"),
                                       pinned, pinned ? p.accent : 0, ImVec2(dp(20.0f), dp(18.0f))))
                        togglePin(parameter.index);
                    ImGui::PopFont();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    if (parameter.automatable)
                        ImGui::TextUnformatted(parameter.name.toRawUTF8());
                    else
                        ImGui::TextDisabled("%s", parameter.name.toRawUTF8());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s %s", parameter.text.toRawUTF8(), parameter.label.toRawUTF8());
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    auto value = parameter.value;
                    if (ImGui::SliderFloat("##v", &value, 0.0f, 1.0f, "%.3f")) {
                        if (studio_.engine().setTrackPluginParameter(pluginPopupTrack_, pluginPopupIndex_, parameter.index, value).wasOk()) {
                            parameter.value = value;
                            parameter.text = {};
                            studio_.markDirty();
                        }
                    }
                    ImGui::PopID();
                }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        if (parameters.error.isNotEmpty())
            ImGui::TextColored(theme::vec(p.record), "%s", parameters.error.toRawUTF8());
        else
            ImGui::TextDisabled("%s", L("Étoile : le paramètre en potard sur la carte, et dans la liste de la modulation. Clic droit sur "
                                        "un potard : l'automatiser.",
                                        "Star: the parameter as a knob on the card, and in the modulation's list. Right click on a "
                                        "knob: automate it."));
    }
    ImGui::End();
    if (!open)
        pluginWindowOpen_ = false;
}

namespace {

// A branch of the catalogue's tree: its devices, then its own branches.
struct CatalogueBranch {
    juce::String name;
    std::vector<CatalogueBranch> children;
    std::vector<const app::NodeGraphComponent::DeviceCatalogueEntry*> entries;
    std::vector<const Studio::LibraryDevice*> devices;
    int count {};
};

int catalogueRank(const juce::String& name)
{
    static const std::initializer_list<std::pair<const char*, const char*>> order {
        { "Mes périphériques", "My devices" }, { "Effets", "Effects" },     { "Instruments", "Instruments" },
        { "MIDI", "MIDI" },                    { "Modulateurs", "Modulators" }, { "Analyseurs", "Analyzers" },
        { "Utilitaires", "Utilities" },        { "Chaînes complètes", "Whole chains" }, { "Nœuds", "Nodes" },
    };
    auto index = 0;
    for (const auto& [fr, en] : order) {
        if (name == ui::Ls(fr, en))
            return index;
        ++index;
    }
    return 100;
}

CatalogueBranch& branchAt(CatalogueBranch& root, const std::vector<juce::String>& path)
{
    auto* branch = &root;
    for (const auto& name : path) {
        auto found = std::find_if(branch->children.begin(), branch->children.end(),
                                  [&name](const CatalogueBranch& child) { return child.name == name; });
        if (found == branch->children.end()) {
            branch->children.push_back({ name });
            found = branch->children.end() - 1;
        }
        branch = &*found;
    }
    return *branch;
}

void sortCatalogue(CatalogueBranch& branch, const bool top)
{
    for (auto& child : branch.children)
        sortCatalogue(child, false);
    std::stable_sort(branch.children.begin(), branch.children.end(), [top](const auto& a, const auto& b) {
        const auto ra = top ? catalogueRank(a.name) : 0;
        const auto rb = top ? catalogueRank(b.name) : 0;
        return ra != rb ? ra < rb : a.name.compareNatural(b.name) < 0;
    });
    std::stable_sort(branch.entries.begin(), branch.entries.end(),
                     [](const auto* a, const auto* b) { return a->name.compareNatural(b->name) < 0; });
    branch.count = static_cast<int>(branch.entries.size() + branch.devices.size());
    for (const auto& child : branch.children)
        branch.count += child.count;
}

std::vector<juce::String> splitCategory(const juce::String& category)
{
    std::vector<juce::String> parts;
    for (const auto& part : juce::StringArray::fromTokens(category, "/", ""))
        if (part.trim().isNotEmpty())
            parts.push_back(part.trim());
    return parts;
}

} // namespace

void DevicesView::drawCatalogueTree(const TrackRef& track,
                                    const std::vector<const app::NodeGraphComponent::DeviceCatalogueEntry*>& shown,
                                    const app::NodeGraphComponent::DeviceCatalogueEntry*& chosen,
                                    std::optional<juce::File>& chosenFile)
{
    CatalogueBranch root;
    for (const auto& device : library_) {
        if (device.instrument && !track.midi)
            continue;
        branchAt(root, { ui::Ls("Mes périphériques", "My devices"),
                         device.instrument ? ui::Ls("Instruments", "Instruments") : ui::Ls("Effets", "Effects") })
            .devices.push_back(&device);
    }
    for (const auto* entry : shown) {
        std::vector<juce::String> path;
        if (entry->kind == "preset") {
            path = { ui::Ls("Chaînes complètes", "Whole chains"),
                     entry->soundSource ? ui::Ls("Instruments", "Instruments") : ui::Ls("Effets", "Effects") };
        } else if (entry->kind == "module") {
            // The factory graphs under their own category.
            path = splitCategory(entry->category);
            if (path.empty())
                path = { ui::Ls("Utilitaires", "Utilities") };
        } else {
            app::NodeGraphComponent::EditorNodeType type;
            type.key = entry->key;
            type.categoryKey = entry->categoryKey;
            type.category = entry->category;
            path = node_tree::pathOf(type);
            path.insert(path.begin(), ui::Ls("Nœuds", "Nodes"));
        }
        branchAt(root, path).entries.push_back(entry);
    }
    sortCatalogue(root, true);
    const std::function<void(const CatalogueBranch&, int)> drawBranch = [&](const CatalogueBranch& branch, const int depth) {
        for (std::size_t index = 0; index < branch.children.size(); ++index) {
            const auto& child = branch.children[index];
            ImGui::PushID(static_cast<int>(index));
            // A click on its name opens a branch.
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
            // My devices and the effects open; the rest a click away.
            if (depth == 0 && (child.name == ui::Ls("Mes périphériques", "My devices") || child.name == ui::Ls("Effets", "Effects")))
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            const auto label = child.name + "  " + juce::String(child.count) + "###branch";
            if (ImGui::TreeNodeEx(label.toRawUTF8(), flags)) {
                drawBranch(child, depth + 1);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        const auto indent = depth > 0 ? ImGui::GetTreeNodeToLabelSpacing() * 0.55f : 0.0f;
        if (indent > 0.0f)
            ImGui::Indent(indent);
        for (const auto* device : branch.devices) {
            ImGui::PushID(device->file.getFullPathName().toRawUTF8());
            if (ImGui::Selectable(device->name.toRawUTF8()))
                chosenFile = device->file;
            if (ImGui::IsItemHovered() && device->description.isNotEmpty())
                ImGui::SetTooltip("%s", device->description.toRawUTF8());
            ImGui::PopID();
        }
        for (const auto* entry : branch.entries) {
            ImGui::PushID(entry->key.toRawUTF8());
            if (ImGui::Selectable(entry->name.toRawUTF8()))
                chosen = entry;
            if (ImGui::IsItemHovered() && entry->description.isNotEmpty())
                ImGui::SetTooltip("%s", entry->description.toRawUTF8());
            ImGui::PopID();
        }
        if (indent > 0.0f)
            ImGui::Unindent(indent);
    };
    drawBranch(root, 0);
}

void DevicesView::drawCatalogue(const TrackRef& track)
{
    const auto& p = theme::palette();
    ImGui::SetNextWindowSize(ImVec2(dp(340.0f), dp(420.0f)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##deviceCatalogue"))
        return;
    auto* slot = studio_.graphFor(track);
    if (slot == nullptr) {
        ImGui::EndPopup();
        return;
    }
    if (focusSearch_) {
        ImGui::SetKeyboardFocusHere();
        focusSearch_ = false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    const auto entered = ImGui::InputTextWithHint("##search", L("Rechercher un périphérique…", "Search a device…"), search_,
                                                  sizeof(search_), ImGuiInputTextFlags_EnterReturnsTrue);
    const auto query = fold(juce::String::fromUTF8(search_).trim());
    // Made when the list opens (or the track changes), not each frame.
    static std::vector<app::NodeGraphComponent::DeviceCatalogueEntry> entries;
    static std::optional<TrackRef> entriesFor;
    if (ImGui::IsWindowAppearing() || entriesFor != track || entries.empty()) {
        entries = slot->graph().deviceCatalogue();
        entriesFor = track;
    }
    std::vector<const app::NodeGraphComponent::DeviceCatalogueEntry*> shown;
    for (const auto& entry : entries) {
        if (!track.midi && entry.soundSource)
            continue;
        if (entry.key == "mono-to-stereo" || entry.key == "stereo-to-mono")
            continue;
        if (query.isNotEmpty() && !fold(entry.name).contains(query) && !fold(entry.category).contains(query)
            && !fold(entry.key).contains(query) && !fold(entry.description).contains(query))
            continue;
        shown.push_back(&entry);
    }
    const auto count = static_cast<int>(shown.size());
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
        highlight_ = std::min(count - 1, highlight_ + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
        highlight_ = std::max(0, highlight_ - 1);
    highlight_ = std::clamp(highlight_, 0, std::max(0, count - 1));
    const app::NodeGraphComponent::DeviceCatalogueEntry* chosen = nullptr;
    if (entered && count > 0)
        chosen = shown[static_cast<std::size_t>(highlight_)];

    if (ImGui::GetTime() - libraryTime_ > 2.0) {
        library_ = studio_.libraryDevices();
        libraryTime_ = ImGui::GetTime();
    }
    std::optional<juce::File> chosenFile;
    ImGui::BeginChild("##entries", ImVec2(dp(324.0f), dp(360.0f)));
    if (query.isEmpty()) {
        drawCatalogueTree(track, shown, chosen, chosenFile);
    } else {
    {
        auto titled = false;
        for (std::size_t index = 0; index < library_.size(); ++index) {
            const auto& device = library_[index];
            if ((device.instrument && !track.midi)
                || (query.isNotEmpty() && !fold(device.name).contains(query) && !fold(device.description).contains(query)))
                continue;
            if (!titled) {
                ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.78f);
                ImGui::TextColored(theme::vec(p.textFaint), "%s", L("MES PÉRIPHÉRIQUES", "MY DEVICES"));
                ImGui::PopFont();
                titled = true;
            }
            ImGui::PushID(static_cast<int>(index) + 100'000);
            if (ImGui::Selectable(device.name.toRawUTF8()))
                chosenFile = device.file;
            if (ImGui::IsItemHovered() && device.description.isNotEmpty())
                ImGui::SetTooltip("%s", device.description.toRawUTF8());
            const auto* kind = device.instrument ? L("instrument", "instrument") : L("effet", "effect");
            const auto width = ImGui::CalcTextSize(kind).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ScrollbarSize - width);
            ImGui::TextColored(theme::vec(p.textFaint), "%s", kind);
            ImGui::PopID();
        }
    }
    juce::String lastKind;
    for (auto index = 0; index < count; ++index) {
        const auto& entry = *shown[static_cast<std::size_t>(index)];
        if (query.isEmpty() && entry.kind != lastKind) {
            lastKind = entry.kind;
            const auto* title = entry.kind == "preset" ? L("CHAÎNES COMPLÈTES (remplacent le graphe)", "WHOLE CHAINS (replace the graph)")
                                : entry.kind == "module" ? L("MODULES", "MODULES")
                                                         : L("NŒUDS", "NODES");
            ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.78f);
            ImGui::TextColored(theme::vec(p.textFaint), "%s", title);
            ImGui::PopFont();
        }
        ImGui::PushID(index);
        if (ImGui::Selectable(entry.name.toRawUTF8(), index == highlight_))
            chosen = &entry;
        if (index == highlight_ && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
            ImGui::SetScrollHereY(0.5f);
        if (ImGui::IsItemHovered() && entry.description.isNotEmpty())
            ImGui::SetTooltip("%s", entry.description.toRawUTF8());
        if (entry.category.isNotEmpty()) {
            const auto width = ImGui::CalcTextSize(entry.category.toRawUTF8()).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ScrollbarSize - width);
            ImGui::TextColored(theme::vec(p.textFaint), "%s", entry.category.toRawUTF8());
        }
        ImGui::PopID();
    }
    }
    ImGui::EndChild();
    const auto plugins = !track.midi && !track.master;
    const auto half = !plugins ? -1.0f : (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button(L("Atelier de périphériques…", "Device workshop…"), ImVec2(half, 0.0f))) {
        ImGui::CloseCurrentPopup();
        state_.bringToFront = Panel::workshop;
    }
    ui::tooltip(L("Fabriquer un effet ou un instrument avec un graphe", "Build an effect or an instrument with a graph"));
    if (plugins)
        ImGui::SameLine();
    if (plugins && ImGui::Button(L("Plugin VST3 / CLAP / LV2 / JSFX…", "VST3 / CLAP / LV2 / JSFX plugin…"), ImVec2(-1.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        chooser_ = std::make_unique<juce::FileChooser>(L("Choisir un plugin", "Choose a plugin"),
                                                       juce::File("C:\\Program Files\\Common Files\\VST3"), "*.vst3;*.clap;*.lv2;*.jsfx;*.cyplugin");
        const auto index = track.audioIndex;
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this, index](const juce::FileChooser& chooser) {
                                  const auto file = chooser.getResult();
                                  if (file == juce::File {})
                                      return;
                                  const auto added = studio_.engine().setTrackPlugin(index, file);
                                  state_.status = added.wasOk() ? juce::String(file.getFileNameWithoutExtension())
                                                                : added.getErrorMessage();
                                  studio_.markDirty();
                              });
    }
    if (chosenFile) {
        if (const auto inserted = studio_.insertLibraryDevice(track, *chosenFile); inserted.failed())
            state_.status = inserted.getErrorMessage();
        ImGui::CloseCurrentPopup();
    }
    if (chosen != nullptr) {
        const auto result = studio_.insertDevice(track, chosen->key);
        if (result.failed())
            state_.status = result.getErrorMessage();
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace cydaw::v2
