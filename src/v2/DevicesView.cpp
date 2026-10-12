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
#include "DeviceEditorView.h"
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

std::string formatFor(const double minimum, const double maximum)
{
    const auto range = std::abs(maximum - minimum);
    return range >= 200.0 ? "%.0f" : range >= 20.0 ? "%.1f" : "%.2f";
}

} // namespace

DevicesView::DevicesView(Studio& studio, UiState& state, DeviceSignals& signals)
    : studio_(studio), state_(state), signals_(signals), presets_(studio, state),
      designed_(std::make_unique<InterfaceEditor>(studio, state))
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
    const auto track = *state_.selectedTrack;
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
        return typeKey == "prism-synth" || typeKey == "kick-synth" || SynthView::handles(typeKey) || BatterieView::handles(typeKey);
    };
    const auto prismVoice = std::any_of(nodes.begin(), nodes.end(), [&](const auto& node) { return wholeVoice(node.typeKey); });
    for (const auto& node : nodes)
        if (!node.boundary && !(prismVoice && !wholeVoice(node.typeKey) && graph.editorIsVoiceNode(node.id)))
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
        // Vertical name.
        const auto name = studio_.trackName(track);
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
    for (const auto* node : chain) {
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
        auto surface = visual::inferSurface(parameters, span != nullptr, instrument);
        // Prisme draws itself (its window has the rest).
        const auto prismCard = node->typeKey == "prism-synth" && prism_ != nullptr;
        const auto kickCard = node->typeKey == "kick-synth" && kick_ != nullptr;
        const auto synthCard = synth_ != nullptr && SynthView::handles(node->typeKey);
        const auto batterieCard = batterie_ != nullptr && BatterieView::handles(node->typeKey);
        // A device made in the workshop with an interface of its own shows
        // that interface, played, as its plugin window does (scaled to the
        // card's height), instead of a row of knobs.
        // Only while it stays readable: at 60 % of its size at least (a
        // taller panel shows it); smaller, the knobs, and its window.
        std::optional<app::ModuleInterfaceState> designed;
        if (node->composite && node->hasInterface && !node->collapsed) {
            auto shownInterface = graph.editorNodeInterface(node->id);
            const auto designedHeight = dp(static_cast<float>(shownInterface.windowHeight != 0 ? shownInterface.windowHeight : 420));
            const auto readable = (height - dp(43.0f)) / std::max(1.0f, designedHeight) >= 0.6f;
            if (readable && std::any_of(shownInterface.controls.begin(), shownInterface.controls.end(), [](const auto& control) {
                    return control.kind != app::ModuleControlKind::inputPort && control.kind != app::ModuleControlKind::outputPort;
                }))
                designed = std::move(shownInterface);
        }
        if (prismCard || kickCard || synthCard || batterieCard || designed) {
            surface.knobs.clear();
            surface.panes.clear();
        }
        const auto count = static_cast<int>(surface.knobs.size());
        const auto bodyTop = dp(32.0f);
        const auto bodyHeight = height - bodyTop - dp(7.0f);
        const auto rows = bodyHeight > dp(150.0f) ? 2 : 1;
        const auto columns = count == 0 ? 0 : std::max(1, (count + rows - 1) / rows);
        auto panesWidth = 0.0f;
        for (const auto& pane : surface.panes)
            panesWidth += visual::paneWidth(pane, bodyHeight) + dp(6.0f);
        // A sound model shows itself: its partials over time, what it is.
        const auto modelCard = node->typeKey == "sound-model";
        const auto modelPaneWidth = dp(176.0f);
        if (modelCard)
            panesWidth += modelPaneWidth + dp(6.0f);
        // A sampler shows its zones over the keys, and takes files.
        const auto samplerCard = node->typeKey == "sampler" && sampler_ != nullptr;
        const auto samplerPaneWidth = dp(230.0f);
        if (samplerCard)
            panesWidth += samplerPaneWidth + dp(6.0f);
        if (prismCard)
            panesWidth += PrismView::paneWidth() + dp(6.0f);
        if (kickCard)
            panesWidth += KickView::paneWidth() + dp(6.0f);
        if (synthCard)
            panesWidth += SynthView::paneWidth() + dp(6.0f);
        if (batterieCard)
            panesWidth += BatterieView::paneWidth() + dp(6.0f);
        const auto designedWidth = designed ? std::clamp(
                                                  (bodyHeight - dp(4.0f))
                                                      * static_cast<float>(designed->windowWidth != 0 ? designed->windowWidth : 720)
                                                      / static_cast<float>(designed->windowHeight != 0 ? designed->windowHeight : 420),
                                                  dp(120.0f), dp(900.0f))
                                            : 0.0f;
        if (designed)
            panesWidth += designedWidth + dp(6.0f);
        const auto collapsed = node->collapsed;
        const auto width = collapsed ? dp(46.0f)
                                     : std::max(dp(150.0f), dp(8.0f) + panesWidth + columns * dp(kCell) + dp(6.0f));
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
        const auto separateWidth = (separable ? dp(22.0f) : 0.0f) + (sidechained ? dp(22.0f) : 0.0f);
        if (!collapsed) {
            ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.9f);
            ui::clippedText(list, ImVec2(origin.x + dp(8.0f), origin.y + dp(5.0f)),
                            width - dp(windowed ? 126.0f : editable ? 104.0f : 82.0f) - separateWidth, p.text,
                            node->title.toRawUTF8());
            ImGui::PopFont();
        }
        // Header buttons.
        const ImVec2 button(dp(20.0f), dp(20.0f));
        ImGui::SetCursorScreenPos(ImVec2(
            origin.x + width - (collapsed ? dp(33.0f) : windowed ? dp(112.0f) : editable ? dp(90.0f) : dp(68.0f)) - separateWidth,
            origin.y + dp(3.0f)));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
        if (separable) {
            const PluginWindow window { track, node->id };
            const auto open = prismCard     ? prism_->showsEditor(track, node->id) && ui::isDetached(PrismView::editorWindowId)
                              : kickCard    ? kick_->showsEditor(track, node->id) && ui::isDetached(KickView::editorWindowId)
                              : synthCard   ? synth_->showsEditor(track, node->id) && ui::isDetached(SynthView::editorWindowId)
                              : batterieCard ? batterie_->showsEditor(track, node->id) && ui::isDetached(BatterieView::editorWindowId)
                              : samplerCard ? sampler_->showsEditor(track, node->id) && ui::isDetached(SamplerView::editorWindowId)
                              : windowed    ? state_.pluginWindowOpen(window) && ui::isDetached(PluginWindows::windowId(window).c_str())
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
                else if (windowed) {
                    if (!state_.pluginWindowOpen(window))
                        state_.togglePluginWindow(window);
                    ui::setDetached(PluginWindows::windowId(window).c_str(), true);
                } else {
                    state_.openExpandedPane(ExpandedPane { track, node->id, 0, true });
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
            const PluginWindow window { track, node->id };
            if (ui::iconButton(icon::view, L("Ouvrir sa fenêtre, comme un plugin (l'interface dessinée dans l'atelier)",
                                             "Open its window, as a plugin's (the interface designed in the workshop)"),
                               state_.pluginWindowOpen(window), p.accent, button))
                state_.togglePluginWindow(window);
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
            if (modelCard) {
                drawSoundModelPane(track, graph, node->id, ImVec2(x, origin.y + bodyTop), ImVec2(modelPaneWidth, bodyHeight), colour);
                x += modelPaneWidth + dp(6.0f);
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
                synth_->drawPane(track, graph, node->id, ImVec2(x, origin.y + bodyTop), ImVec2(SynthView::paneWidth(), bodyHeight),
                                 colour);
                x += SynthView::paneWidth() + dp(6.0f);
            }
            if (batterieCard) {
                batterie_->drawPane(track, graph, node->id, ImVec2(x, origin.y + bodyTop), ImVec2(BatterieView::paneWidth(), bodyHeight),
                                    colour);
                x += BatterieView::paneWidth() + dp(6.0f);
            }
            if (designed) {
                ImGui::SetCursorScreenPos(ImVec2(x, origin.y + bodyTop));
                const auto playedId = "designed" + juce::String(static_cast<juce::int64>(node->id));
                designed_->drawPlayed(playedId.toRawUTF8(), *designed, ImVec2(designedWidth, bodyHeight - dp(4.0f)),
                                      [&graph, this](const juce::String& key, const double value) {
                                          (void) graph.editorSetParameter(key, value);
                                          state_.focused = Panel::devices;
                                      });
                x += designedWidth + dp(6.0f);
            }
            const CardFileScope cardFile(studio_, state_, track, graph, node->id, node->typeKey);
            for (std::size_t index = 0; index < surface.panes.size(); ++index) {
                const auto& pane = surface.panes[index];
                const auto paneWidth = visual::paneWidth(pane, bodyHeight);
                ImGui::PushID(static_cast<int>(index));
                auto expand = false;
                (void) visual::drawPane("##pane", pane, parameters, ImVec2(x, origin.y + bodyTop), ImVec2(paneWidth, bodyHeight),
                                        colour, live, setParameter, true, &expand);
                if (expand)
                    state_.openExpandedPane(ExpandedPane { track, node->id, static_cast<int>(index) });
                ImGui::PopID();
                x += paneWidth + dp(6.0f);
            }
            // One or two rows of knobs for the rest.
            const auto rowHeight = std::min(bodyHeight / static_cast<float>(rows), dp(74.0f));
            const auto blockTop = std::max(0.0f, (bodyHeight - rowHeight * static_cast<float>(rows)) * 0.3f);
            for (auto slotIndex = 0; slotIndex < count; ++slotIndex) {
                const auto& parameter = node->parameters[static_cast<std::size_t>(surface.knobs[static_cast<std::size_t>(slotIndex)])];
                const auto column = slotIndex / rows;
                const auto row = slotIndex % rows;
                ImGui::SetCursorScreenPos(ImVec2(x + column * dp(kCell), origin.y + bodyTop + blockTop + row * rowHeight));
                // A voice allocator's playing: three words, not a knob.
                if (parameter.binding.endsWith("voice.mode")) {
                    ImGui::PushID(parameter.binding.toRawUTF8());
                    const char* names[] = { L("Poly", "Poly"), L("Mono", "Mono"), L("Legato", "Legato") };
                    const auto current = juce::roundToInt(parameter.value);
                    const auto top = ImGui::GetCursorScreenPos();
                    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
                    for (auto index = 0; index < 3; ++index) {
                        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + static_cast<float>(index) * dp(19.0f)));
                        auto on = current == index;
                        if (ui::toggle(ui::labelId(names[index], index == 0 ? "poly" : index == 1 ? "mono" : "legato").c_str(), &on, colour,
                                       ImVec2(dp(kCell) - dp(8.0f), dp(17.0f)),
                                       L("Poly : des accords. Mono : une note à la fois, qui glisse de l'une à l'autre. "
                                         "Legato : une note à la fois, qui glisse et sans nouvelle attaque quand elle chevauche "
                                         "la précédente (saxophone, flûte, voix).",
                                         "Poly: chords. Mono: one note at a time, sliding from one to the next. Legato: one note "
                                         "at a time, sliding and without a new attack when it overlaps the last (saxophone, "
                                         "flute, voice)."))) {
                            (void) graph.editorSetParameter(parameter.binding, static_cast<float>(index));
                            state_.focused = Panel::devices;
                        }
                    }
                    ImGui::PopFont();
                    ImGui::PopID();
                    continue;
                }
                auto value = static_cast<float>(parameter.value);
                ui::KnobStyle style;
                style.diameter = kKnob;
                const auto label = parameter.label.length() > 10 ? parameter.label.substring(0, 9) + "." : parameter.label;
                const auto labelText = label.toStdString();
                style.label = labelText.c_str();
                const auto format = formatFor(parameter.minimum, parameter.maximum);
                style.format = format.c_str();
                const auto unit = parameter.unit.toStdString();
                style.unit = unit.c_str();
                style.colour = colour;
                if (parameter.minimum > 0.0 && (parameter.unit == "Hz" || parameter.unit == "ms"))
                    style.flags |= ui::knobLogarithmic;
                if (parameter.minimum < 0.0 && std::abs(parameter.minimum + parameter.maximum) < 1.0e-6)
                    style.flags |= ui::knobBipolar;
                if (const auto found = modulated.find(parameter.binding); found != modulated.end()) {
                    const auto low = static_cast<float>(parameter.minimum);
                    const auto high = static_cast<float>(parameter.maximum);
                    if ((style.flags & ui::knobLogarithmic) != 0 && low > 0.0f && high > low)
                        style.modulation = std::log(std::max(low, found->second) / low) / std::log(high / low);
                    else if (high > low)
                        style.modulation = (found->second - low) / (high - low);
                }
                ImGui::PushID(parameter.binding.toRawUTF8());
                if (ui::knob("##k", &value, static_cast<float>(parameter.minimum), static_cast<float>(parameter.maximum),
                             static_cast<float>(parameter.defaultValue), style)) {
                    (void) graph.editorSetParameter(parameter.binding, value);
                    state_.focused = Panel::devices;
                }
                if (ImGui::IsItemHovered() && label != parameter.label)
                    ImGui::SetTooltip("%s", parameter.label.toRawUTF8());
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    openParameterMenu(track, parameter.binding, parameter.label);
                ImGui::PopID();
            }
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(width, height));
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::SameLine(0.0f, dp(5.0f));
    }
    if (removeNode)
        graph.editorRemoveFromChain(*removeNode);

    // VST3 plugins sit on audio tracks (the master has its graph's devices).
    if (!track.midi && !track.master)
        drawPlugins(track, height);

    // Add a device.
    {
        const auto origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(dp(56.0f), height);
        ImGui::InvisibleButton("##addDevice", size);
        const auto hovered = ImGui::IsItemHovered();
        auto* list = ImGui::GetWindowDrawList();
        list->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), hovered ? p.accent : p.lineStrong, dp(5.0f), dp(1.0f));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 1.4f);
        ui::centredText(list, origin, ImVec2(origin.x + size.x, origin.y + size.y * 0.9f), hovered ? p.accent : p.textDim, icon::add);
        ImGui::PopFont();
        ui::tooltip(L("Ajouter un périphérique : la bibliothèque en grand (clic droit : la liste rapide)",
                      "Add a device: the library, large (right click: the quick list)"));
        if (ImGui::IsItemClicked()) {
            // The big window, on the effects, putting what is picked here.
            state_.showLibrary = true;
            state_.libraryTab = 2;
            state_.libraryTarget = track;
            state_.libraryPick = true;
            state_.libraryFocus = true;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            search_[0] = 0;
            highlight_ = 0;
            focusSearch_ = true;
            openCatalogue_ = true;
        }
    }
    // The wheel runs the strip sideways wherever nothing under the pointer
    // takes it (the displays and knobs keep theirs), as in Bitwig.
    if (auto& io = ImGui::GetIO(); io.MouseWheel != 0.0f && !io.KeyCtrl && !io.KeyShift && ImGui::IsWindowHovered()
                                   && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive())
        ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * dp(120.0f));
    ImGui::EndChild();
    // Opened from the panel itself, once the strip's own window is closed.
    if (std::exchange(state_.openDeviceCatalogue, false)) {
        search_[0] = 0;
        highlight_ = 0;
        openCatalogue_ = true;
    }
    if (std::exchange(openCatalogue_, false))
        ImGui::OpenPopup(cataloguePopup_);
    visual::setParameterMenu({});
    drawCatalogue(track);
    drawPluginParameters();
    drawParameterMenu();
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
    ImGui::SetCursorScreenPos(ImVec2(at.x + dp(4.0f), plotEnd.y + dp(2.0f)));
    ImGui::PushTextWrapPos(end.x - dp(58.0f));
    ImGui::TextColored(theme::vec(p.textDim), "%s", description.toRawUTF8());
    ImGui::PopTextWrapPos();
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
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto plugins = engine.trackPlugins(track.audioIndex);
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
        const auto width = std::max(dp(260.0f), columns * dp(kCell) + dp(18.0f));
        const auto origin = ImGui::GetCursorScreenPos();
        auto* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), p.bg2, dp(5.0f));
        list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + dp(26.0f)), theme::mix(p.audio, p.bg2 | 0xff000000u, 0.5f),
                            dp(5.0f), ImDrawFlags_RoundCornersTop);
        list->AddRectFilled(ImVec2(origin.x, origin.y + dp(24.0f)), ImVec2(origin.x + width, origin.y + dp(26.0f)), p.audio);
        list->AddRect(origin, ImVec2(origin.x + width, origin.y + height), p.line, dp(5.0f));
        ImGui::BeginGroup();
        // Running or not, then the name.
        list->AddCircleFilled(ImVec2(origin.x + dp(11.0f), origin.y + dp(13.0f)), dp(3.5f), plugin.running ? p.play : p.record);
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.9f);
        ui::clippedText(list, ImVec2(origin.x + dp(20.0f), origin.y + dp(5.0f)), width - dp(158.0f), p.text, plugin.name.toRawUTF8());
        ImGui::PopFont();
        const ImVec2 button(dp(20.0f), dp(20.0f));
        ImGui::SetCursorScreenPos(ImVec2(origin.x + width - dp(136.0f), origin.y + dp(3.0f)));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
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
            ImGui::PushTextWrapPos(origin.x + width - dp(8.0f));
            ImGui::TextColored(theme::vec(p.record), "%s",
                               L("Arrêté : le processus du plugin ne répond plus.", "Stopped: the plugin's process is not responding."));
            ImGui::PopTextWrapPos();
        } else if (count == 0) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(8.0f), origin.y + dp(36.0f)));
            ImGui::PushTextWrapPos(origin.x + width - dp(8.0f));
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
    if (plugins && ImGui::Button(L("Plugin VST3…", "VST3 plugin…"), ImVec2(-1.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        chooser_ = std::make_unique<juce::FileChooser>(L("Choisir un plugin VST3", "Choose a VST3 plugin"),
                                                       juce::File("C:\\Program Files\\Common Files\\VST3"), "*.vst3");
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
