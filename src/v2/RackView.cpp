#include "RackView.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <cydaw/engine/SynthFamily.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace cydaw::v2 {

using ui::dp;
using ui::L;
namespace container = engine::instrument_container;

namespace {

// The pads: sixteen keys at a time, four by four; the lowest row from C1
// (36) at first.
constexpr int kDefaultFirst = 36;
constexpr int kColumns = 4;
constexpr int kRows = 4;

std::unique_ptr<juce::FileChooser> chooser;

juce::String noteName(const int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1);
}

// What each track's card shows: the drum machine's chosen pad and lowest
// key, the layer chosen.
int& chosenNote(const std::uint64_t track)
{
    static std::map<std::uint64_t, int> chosen;
    return chosen.try_emplace(track, -1).first->second;
}

int& firstNote(const std::uint64_t track)
{
    static std::map<std::uint64_t, int> first;
    return first.try_emplace(track, kDefaultFirst).first->second;
}

std::map<std::uint64_t, std::size_t>& chosenLayers()
{
    static std::map<std::uint64_t, std::size_t> chosen;
    return chosen;
}

// A selector's card: its settings by their keys (their bindings and values).
struct SelectorCard {
    app::NodeGraphComponent* graph {};
    std::map<juce::String, std::pair<juce::String, float>> values;

    [[nodiscard]] float value(const juce::String& key, const float fallback) const
    {
        const auto found = values.find(key);
        return found != values.end() ? found->second.second : fallback;
    }
    void set(const juce::String& key, const double to) const
    {
        if (const auto found = values.find(key); found != values.end() && graph != nullptr)
            (void) graph->editorSetParameter(found->second.first, to);
    }
};

SelectorCard selectorCard(Studio& studio, const TrackRef& track)
{
    SelectorCard card;
    auto* slot = studio.graphFor(track.owner());
    if (slot == nullptr)
        return card;
    card.graph = &slot->graph();
    for (const auto& node : card.graph->editorNodes())
        if (node.typeKey == "instrument-selector") {
            for (const auto& parameter : node.parameters)
                card.values[parameter.binding.fromLastOccurrenceOf(".", false, false)]
                    = { parameter.binding, static_cast<float>(parameter.value) };
            break;
        }
    return card;
}

} // namespace

float RackView::paneWidth()
{
    return dp(450.0f);
}

void RackView::report(const juce::String& error)
{
    if (error.isEmpty())
        return;
    state_.status = error;
    state_.statusIsError = true;
    state_.statusTime = ImGui::GetTime();
}

juce::String RackView::instrumentItems(Studio& studio, const juce::String& current)
{
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    juce::String chosen;
    const auto choices = studio.instrumentChoices();
    for (const auto& choice : choices)
        if (choice.category.isEmpty() && ImGui::MenuItem(choice.name.toRawUTF8(), nullptr, choice.key == current))
            chosen = choice.key;
    // The factory's sampled instruments, by their category.
    std::vector<juce::String> categories;
    for (const auto& choice : choices)
        if (choice.category.isNotEmpty() && std::find(categories.begin(), categories.end(), choice.category) == categories.end())
            categories.push_back(choice.category);
    if (!categories.empty()) {
        ImGui::Separator();
        if (ImGui::BeginMenu("Factory Instruments")) {
            for (const auto& category : categories)
                if (ImGui::BeginMenu(category.toRawUTF8())) {
                    for (const auto& choice : choices) {
                        if (choice.category != category)
                            continue;
                        if (choice.missing)
                            ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(theme::palette().textDim));
                        if (ImGui::MenuItem(choice.name.toRawUTF8(), choice.missing ? "download" : nullptr, choice.key == current))
                            chosen = choice.key;
                        if (choice.missing) {
                            ImGui::PopStyleColor();
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", "Its samples are downloaded when it is chosen: it plays once they are here.");
                        }
                    }
                    ImGui::EndMenu();
                }
            ImGui::EndMenu();
        }
    }
    return chosen;
}

bool RackView::instrumentMenu(Studio& studio, const TrackRef& track, const int note, const std::optional<std::size_t> pad,
                              juce::String& error)
{
    const auto chosen = instrumentItems(studio);
    if (chosen.isEmpty())
        return false;
    if (pad)
        return studio.setRackPadInstrument(track, *pad, chosen, error);
    if (studio.containerKind(track) == container::Kind::drum)
        return studio.addRackPad(track, note, chosen, error).has_value();
    return studio.addLayer(track, chosen, error).has_value();
}

void RackView::chooseSample(const TrackRef& track, const int note)
{
    chooser = std::make_unique<juce::FileChooser>(L("Un son pour ce pad", "A sound for this pad"), juce::File(),
                                                  "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    auto* studio = &studio_;
    auto* state = &state_;
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [studio, state, track, note](const juce::FileChooser& picked) {
                             const auto file = picked.getResult();
                             if (file == juce::File())
                                 return;
                             juce::String error;
                             if (!studio->loadRackPadSample(track, note, file, error)) {
                                 state->status = error;
                                 state->statusIsError = true;
                                 state->statusTime = ImGui::GetTime();
                             }
                         });
}

void RackView::drawPane(const TrackRef& track, const ImVec2 at, const ImVec2 size, const ImU32 colour)
{
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), p.bg1, dp(4.0f));
    ImGui::PushID("rackPane");
    switch (studio_.containerKind(track)) {
    case container::Kind::layer: drawLayers(track, at, size, colour, false); break;
    case container::Kind::selector: drawLayers(track, at, size, colour, true); break;
    default: drawDrums(track, at, size, colour); break;
    }
    ImGui::PopID();
}

// ---- A Drum Machine ---------------------------------------------------------------------

void RackView::drawDrums(const TrackRef& track, const ImVec2 at, const ImVec2 size, const ImU32 colour)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + size.x, at.y + size.y);
    auto& first = firstNote(track.midiId);
    first = std::clamp(first / kColumns * kColumns, 0, 128 - kColumns * kRows);
    auto& chosen = chosenNote(track.midiId);
    if (chosen < 0 || chosen > 127)
        chosen = first;
    // The grid at the left, square cells; the 128 keys beside it; the
    // chosen pad's settings at the right.
    const auto controlsWidth = dp(150.0f);
    const auto stripWidth = dp(18.0f);
    const auto cell = std::min((size.x - controlsWidth - stripWidth - dp(20.0f)) / static_cast<float>(kColumns),
                               (size.y - dp(8.0f)) / static_cast<float>(kRows));
    const ImVec2 gridAt(at.x + dp(4.0f), at.y + dp(4.0f));
    juce::String error;
    auto gridHovered = false;
    for (int row = 0; row < kRows; ++row)
        for (int column = 0; column < kColumns; ++column) {
            const auto note = first + (kRows - 1 - row) * kColumns + column;
            const ImVec2 min(gridAt.x + cell * static_cast<float>(column), gridAt.y + cell * static_cast<float>(row));
            const ImVec2 max(min.x + cell - dp(3.0f), min.y + cell - dp(3.0f));
            const auto pad = studio_.rackPadOfNote(track, note);
            const auto* state = pad ? studio_.rackPad(track, *pad) : nullptr;
            ImGui::PushID(note);
            ImGui::SetCursorScreenPos(min);
            ImGui::InvisibleButton("##pad", ImVec2(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y)));
            const auto hovered = ImGui::IsItemHovered();
            gridHovered = gridHovered || hovered;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                chosen = note;
            // Double click: its chain in the device panel (an empty one: its instrument chosen).
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (pad)
                    state_.openRackPad = std::make_pair(track.owner(), static_cast<int>(*pad));
                else
                    ImGui::OpenPopup("##padMenu");
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                chosen = note;
                ImGui::OpenPopup("##padMenu");
            }
            // A sound dropped onto it (from the browser or the explorer).
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload); payload != nullptr && payload->DataSize > 1) {
                    if (!studio_.loadRackPadSample(track, note, juce::File(juce::String::fromUTF8(static_cast<const char*>(payload->Data))), error))
                        report(error);
                    chosen = note;
                }
                ImGui::EndDragDropTarget();
            }
            // Its face: lit by its sound, its name, its key.
            const auto peak = pad ? std::min(1.0f, studio_.rackPadPeak(track, *pad)) : 0.0f;
            const auto selected = chosen == note;
            const auto base = state != nullptr ? (state->muted ? p.bg2 : theme::mix(colour, p.bg2, 0.72f)) : p.bg2;
            list->AddRectFilled(min, max, theme::mix(base, colour, std::sqrt(peak) * 0.8f), dp(4.0f));
            list->AddRect(min, max, selected ? p.accent : hovered ? p.line : theme::alpha(p.line, 0.6f), dp(4.0f), selected ? dp(2.0f) : dp(1.0f));
            list->PushClipRect(min, max, true);
            if (state != nullptr) {
                ImGui::PushFont(nullptr, ui::currentFontBase() * 0.72f);
                // Its name over two lines if it must (the key's corner kept free on the first).
                list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + dp(4.0f), min.y + dp(12.0f)), p.text, state->name.toRawUTF8(),
                              nullptr, max.x - min.x - dp(6.0f));
                if (state->solo || state->muted)
                    list->AddText(ImVec2(max.x - dp(12.0f), max.y - ImGui::GetFontSize() - dp(2.0f)), state->solo ? p.accent : p.textFaint,
                                  state->solo ? "S" : "M");
                if (state->choke > 0)
                    list->AddText(ImVec2(min.x + dp(4.0f), max.y - ImGui::GetFontSize() - dp(2.0f)), p.textFaint,
                                  (juce::String("C") + juce::String(state->choke)).toRawUTF8());
                ImGui::PopFont();
            } else {
                ui::centredText(list, min, max, p.textFaint, "+");
            }
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.62f);
            const auto key = noteName(note);
            const auto keySize = ImGui::CalcTextSize(key.toRawUTF8());
            list->AddText(ImVec2(max.x - keySize.x - dp(3.0f), min.y + dp(3.0f)), p.textFaint, key.toRawUTF8());
            ImGui::PopFont();
            list->PopClipRect();
            if (hovered)
                ui::tooltip(state != nullptr ? "Click: choose it. Double click: its chain (instrument and effects). Right click: its instrument, a "
                                               "sound, mute, solo, remove. Drop a sound on it. Wheel: other keys."
                                             : "An empty pad: right click to give it an instrument, or drop a sound on it. Wheel: other keys.");
            // Its menu.
            if (ImGui::BeginPopup("##padMenu")) {
                ImGui::TextDisabled("Pad %s", noteName(note).toRawUTF8());
                ImGui::Separator();
                if (pad && ImGui::MenuItem("Open its chain"))
                    state_.openRackPad = std::make_pair(track.owner(), static_cast<int>(*pad));
                if (ImGui::BeginMenu(pad ? "Change instrument" : "Instrument")) {
                    if (instrumentMenu(studio_, track, note, pad, error))
                        chosen = note;
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Load a sound\xe2\x80\xa6"))
                    chooseSample(track, note);
                if (pad && state != nullptr) {
                    ImGui::Separator();
                    auto mix = Studio::mixOf(*state);
                    if (ImGui::MenuItem("Mute", nullptr, state->muted)) {
                        mix.muted = !mix.muted;
                        (void) studio_.setRackPadMix(track, *pad, mix);
                    }
                    if (ImGui::MenuItem("Solo", nullptr, state->solo)) {
                        mix.solo = !mix.solo;
                        (void) studio_.setRackPadMix(track, *pad, mix);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove the pad"))
                        (void) studio_.removeRackPad(track, *pad);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    // The wheel over the pads: a row of keys up or down.
    if (gridHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        first = std::clamp(first + (ImGui::GetIO().MouseWheel > 0.0f ? kColumns : -kColumns), 0, 128 - kColumns * kRows);
        ImGui::GetIO().MouseWheel = 0.0f;
    }
    report(error);

    // The 128 keys, four a row from the bottom: the pads there, the sixteen
    // shown framed; a click or a drag shows others.
    {
        const ImVec2 stripMin(gridAt.x + cell * static_cast<float>(kColumns) + dp(2.0f), gridAt.y);
        const ImVec2 stripMax(stripMin.x + stripWidth, gridAt.y + cell * static_cast<float>(kRows) - dp(3.0f));
        constexpr int rows = 128 / kColumns;
        const auto rowHeight = (stripMax.y - stripMin.y) / static_cast<float>(rows);
        ImGui::SetCursorScreenPos(stripMin);
        ImGui::InvisibleButton("##keys", ImVec2(stripWidth, std::max(1.0f, stripMax.y - stripMin.y)));
        if (ImGui::IsItemActive() || ImGui::IsItemClicked()) {
            const auto row = static_cast<int>((stripMax.y - ImGui::GetIO().MousePos.y) / std::max(1.0f, rowHeight));
            first = std::clamp((row - 1) * kColumns, 0, 128 - kColumns * kRows);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Keys %s to %s (pads on %d keys). Click or drag: other keys.", noteName(first).toRawUTF8(),
                              noteName(first + kColumns * kRows - 1).toRawUTF8(), static_cast<int>(studio_.rackPadCount(track)));
            if (ImGui::GetIO().MouseWheel != 0.0f) {
                first = std::clamp(first + (ImGui::GetIO().MouseWheel > 0.0f ? kColumns : -kColumns), 0, 128 - kColumns * kRows);
                ImGui::GetIO().MouseWheel = 0.0f;
            }
        }
        list->AddRectFilled(stripMin, stripMax, p.bg2, dp(2.0f));
        const auto dot = (stripWidth - dp(4.0f)) / static_cast<float>(kColumns);
        for (std::size_t pad = 0; pad < studio_.rackPadCount(track); ++pad) {
            const auto* state = studio_.rackPad(track, pad);
            const auto row = state->note / kColumns;
            const auto column = state->note % kColumns;
            const ImVec2 min(stripMin.x + dp(2.0f) + dot * static_cast<float>(column), stripMax.y - rowHeight * static_cast<float>(row + 1));
            list->AddRectFilled(min, ImVec2(min.x + std::max(1.0f, dot - dp(0.5f)), min.y + std::max(1.0f, rowHeight - dp(0.5f))),
                                state->muted ? p.textFaint : colour);
        }
        // Every C, a faint line.
        for (int octave = 0; octave <= 10; ++octave) {
            const auto row = octave * 12 / kColumns;
            const auto y = stripMax.y - rowHeight * static_cast<float>(row);
            list->AddLine(ImVec2(stripMin.x, y), ImVec2(stripMin.x + dp(3.0f), y), p.textFaint);
        }
        const auto frameLow = stripMax.y - rowHeight * static_cast<float>(first / kColumns);
        const auto frameHigh = frameLow - rowHeight * static_cast<float>(kRows);
        list->AddRect(ImVec2(stripMin.x, frameHigh), ImVec2(stripMax.x, frameLow), p.accent, dp(1.0f), dp(1.0f));
    }

    // The chosen pad's settings at the right.
    const ImVec2 controlsAt(gridAt.x + cell * static_cast<float>(kColumns) + stripWidth + dp(10.0f), at.y + dp(4.0f));
    if (const auto pad = studio_.rackPadOfNote(track, chosen))
        drawPadControls(track, *pad, controlsAt, end.x - controlsAt.x - dp(4.0f), colour);
    else {
        ImGui::SetCursorScreenPos(controlsAt);
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
        ImGui::TextColored(theme::vec(p.textDim), "Pad %s", noteName(chosen).toRawUTF8());
        ImGui::SetCursorScreenPos(ImVec2(controlsAt.x, ImGui::GetCursorScreenPos().y + dp(4.0f)));
        if (ImGui::Button("Instrument\xe2\x80\xa6"))
            ImGui::OpenPopup("##emptyMenu");
        ImGui::SetCursorScreenPos(ImVec2(controlsAt.x, ImGui::GetCursorScreenPos().y + dp(2.0f)));
        if (ImGui::Button("A sound\xe2\x80\xa6"))
            chooseSample(track, chosen);
        if (ImGui::BeginPopup("##emptyMenu")) {
            juce::String menuError;
            (void) instrumentMenu(studio_, track, chosen, std::nullopt, menuError);
            report(menuError);
            ImGui::EndPopup();
        }
        ImGui::PopFont();
    }
}

void RackView::drawPadControls(const TrackRef& track, const std::size_t pad, const ImVec2 at, const float width, const ImU32 colour)
{
    const auto& p = theme::palette();
    const auto* state = studio_.rackPad(track, pad);
    if (state == nullptr)
        return;
    auto mix = Studio::mixOf(*state);
    auto changed = false;
    ImGui::PushID(static_cast<int>(pad));
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    const auto line = ImGui::GetFrameHeight() + dp(3.0f);
    auto y = at.y;
    // Its name and key.
    auto* list = ImGui::GetWindowDrawList();
    list->PushClipRect(at, ImVec2(at.x + width, at.y + dp(30.0f)), true);
    list->AddText(at, p.text, state->name.toRawUTF8());
    const auto key = juce::String("key ") + noteName(state->note);
    list->AddText(ImVec2(at.x, at.y + ImGui::GetFontSize() + dp(2.0f)), p.textFaint, key.toRawUTF8());
    list->PopClipRect();
    y += ImGui::GetFontSize() * 2.0f + dp(6.0f);
    // Its chain: its instrument and effects, in the device panel.
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    if (ImGui::Button("Open its chain", ImVec2(width, 0.0f)))
        state_.openRackPad = std::make_pair(track.owner(), static_cast<int>(pad));
    ui::tooltip("Its instrument and effects, in the devices (back arrow to come back).");
    y += line + dp(2.0f);
    // Level and pan.
    auto gain = mix.gainDb;
    ui::KnobStyle style;
    style.diameter = 26.0f;
    style.colour = colour;
    style.label = "Volume";
    style.unit = "dB";
    style.format = "%.1f";
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    if (ui::knob("##gain", &gain, -60.0f, 12.0f, 0.0f, style)) {
        mix.gainDb = gain;
        changed = true;
    }
    auto pan = mix.pan;
    style.label = "Pan";
    style.unit = "";
    style.format = "%.2f";
    style.flags = ui::knobBipolar;
    ImGui::SetCursorScreenPos(ImVec2(at.x + width * 0.5f, y));
    if (ui::knob("##pan", &pan, -1.0f, 1.0f, 0.0f, style)) {
        mix.pan = pan;
        changed = true;
    }
    y += dp(26.0f) + ImGui::GetFontSize() * 2.2f;
    // Mute and solo side by side.
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    if (ImGui::Checkbox("Mute", &mix.muted))
        changed = true;
    ImGui::SetCursorScreenPos(ImVec2(at.x + width * 0.5f, y));
    if (ImGui::Checkbox("Solo", &mix.solo))
        changed = true;
    y += line;
    // Its choke group.
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    ImGui::SetNextItemWidth(width);
    const auto chokeLabel = mix.choke == 0 ? juce::String("No choke") : juce::String("Chokes group ") + juce::String(mix.choke);
    if (ImGui::BeginCombo("##choke", chokeLabel.toRawUTF8())) {
        for (int group = 0; group <= 4; ++group) {
            const auto label = group == 0 ? juce::String("No choke") : juce::String("Group ") + juce::String(group);
            if (ImGui::Selectable(label.toRawUTF8(), group == mix.choke)) {
                mix.choke = group;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ui::tooltip("Pads of one group cut each other: a closed hi-hat cuts the open one.");
    y += line;
    // The note its instrument receives.
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    ImGui::SetNextItemWidth(width);
    const auto playsLabel = mix.playAs < 0 ? juce::String("Plays its key") : juce::String("Plays ") + noteName(mix.playAs);
    auto plays = mix.playAs < 0 ? state->note : mix.playAs;
    if (ImGui::DragInt("##plays", &plays, 0.15f, 0, 127, playsLabel.toRawUTF8())) {
        mix.playAs = plays == state->note ? -1 : plays;
        changed = true;
    }
    ui::tooltip("The note its instrument receives: the pad's key, or another (C4 for a pitched instrument).");
    if (changed)
        (void) studio_.setRackPadMix(track, pad, mix);
    ImGui::PopFont();
    ImGui::PopID();
}

// ---- An Instrument Layer, an Instrument Selector ------------------------------------------

void RackView::drawSelection(const TrackRef& track, const ImVec2 at, const float width)
{
    const auto card = selectorCard(studio_, track);
    if (card.graph == nullptr || card.values.empty())
        return;
    const auto mode = std::clamp(static_cast<int>(card.value("mode", 0.0f)), 0, container::selectionCount - 1);
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.82f);
    ImGui::SetCursorScreenPos(at);
    ImGui::SetNextItemWidth(width * 0.52f);
    {
        const auto found = card.values.find("mode");
        const ui::LearnParameterScope learn(found->second.first, "Mode");
        if (ImGui::BeginCombo("##selection", container::selectionName(mode))) {
            for (int index = 0; index < container::selectionCount; ++index)
                if (ImGui::Selectable(container::selectionName(index), index == mode))
                    card.set("mode", index);
            ImGui::EndCombo();
        }
        ui::learnItem();
    }
    ui::tooltip("How a new note chooses its layer: the one lit (Manual), each in turn (Round Robin), another at random (Random), "
                "keys below the others (Keyswitch), a controller (CC), a program change. A note ends where it began.");
    ImGui::SameLine(0.0f, dp(6.0f));
    ImGui::SetNextItemWidth(width * 0.48f - dp(6.0f));
    if (mode == static_cast<int>(container::Selection::controller)) {
        auto number = static_cast<int>(card.value("controller", 1.0f));
        if (ImGui::DragInt("##controller", &number, 0.2f, 0, 127, "CC %d"))
            card.set("controller", number);
        ui::tooltip("The controller whose value chooses the layer (from the first at 0 to the last at 127).");
    } else if (mode == static_cast<int>(container::Selection::keyswitch)) {
        auto lowest = static_cast<int>(card.value("keyswitch", 24.0f));
        const auto label = "From " + noteName(lowest);
        if (ImGui::DragInt("##keyswitch", &lowest, 0.15f, 0, 127, label.toRawUTF8()))
            card.set("keyswitch", lowest);
        ui::tooltip("The key that chooses the first layer; the next keys choose the next ones. A key switch plays nothing.");
    } else {
        ImGui::TextDisabled("%s", mode == static_cast<int>(container::Selection::manual)       ? "Click a light to choose"
                                  : mode == static_cast<int>(container::Selection::program)    ? "Program number = layer"
                                  : mode == static_cast<int>(container::Selection::roundRobin) ? "One layer per note"
                                                                                               : "Never the same twice");
    }
    ImGui::PopFont();
}

void RackView::drawLayers(const TrackRef& track, const ImVec2 at, const ImVec2 size, const ImU32 colour, const bool selector)
{
    const auto& p = theme::palette();
    const auto count = studio_.rackPadCount(track);
    auto& chosen = chosenLayers()[track.midiId];
    if (count > 0)
        chosen = std::min(chosen, count - 1);
    const auto controlsWidth = dp(150.0f);
    const auto listWidth = size.x - controlsWidth - dp(14.0f);
    const auto line = ImGui::GetFrameHeight() + dp(3.0f);
    auto y = at.y + dp(4.0f);
    if (selector) {
        drawSelection(track, ImVec2(at.x + dp(4.0f), y), listWidth);
        y += line + dp(2.0f);
    }
    const auto card = selector ? selectorCard(studio_, track) : SelectorCard {};
    const auto playing = selector ? studio_.selectedLayer(track) : -1;
    const auto manual = selector ? static_cast<int>(card.value("layer", 1.0f)) - 1 : -1;
    const auto listHeight = std::max(dp(40.0f), at.y + size.y - y - line - dp(8.0f));
    std::optional<std::size_t> menuFor;
    std::optional<std::size_t> renameFor;
    ImGui::SetCursorScreenPos(ImVec2(at.x + dp(4.0f), y));
    ImGui::BeginChild("##layers", ImVec2(listWidth, listHeight), ImGuiChildFlags_None);
    {
        auto* list = ImGui::GetWindowDrawList();
        const auto rowHeight = dp(24.0f);
        for (std::size_t layer = 0; layer < count; ++layer) {
            const auto* state = studio_.rackPad(track, layer);
            if (state == nullptr)
                continue;
            ImGui::PushID(static_cast<int>(layer));
            const auto origin = ImGui::GetCursorScreenPos();
            const auto width = ImGui::GetContentRegionAvail().x;
            const ImVec2 rowEnd(origin.x + width, origin.y + rowHeight - dp(2.0f));
            const auto lit = static_cast<int>(layer) == playing;
            list->AddRectFilled(origin, rowEnd, layer == chosen ? p.bg3 : p.bg2, dp(3.0f));
            if (lit)
                list->AddRectFilled(origin, ImVec2(origin.x + dp(3.0f), rowEnd.y), p.accent, dp(3.0f), ImDrawFlags_RoundCornersLeft);
            auto x = origin.x + dp(5.0f);
            const auto midY = origin.y + (rowHeight - dp(2.0f)) * 0.5f;
            // A selector's light: the layer it plays; a click chooses it.
            if (selector) {
                ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
                ImGui::InvisibleButton("##light", ImVec2(dp(14.0f), rowHeight - dp(2.0f)));
                if (ImGui::IsItemClicked())
                    card.set("layer", static_cast<double>(layer + 1));
                ui::tooltip("The layer it plays (lit). Click: this one (the one a switch starts from).");
                const ImVec2 centre(x + dp(6.0f), midY);
                if (lit)
                    list->AddCircleFilled(centre, dp(4.0f), p.accent);
                else
                    list->AddCircle(centre, dp(4.0f), static_cast<int>(layer) == manual ? p.text : p.textFaint, 0, dp(1.2f));
                x += dp(16.0f);
            }
            // Its name: a click chooses it, a double click renames it.
            const auto buttonsWidth = dp(20.0f) * 3.0f + dp(60.0f) + dp(14.0f);
            const auto nameWidth = std::max(dp(30.0f), rowEnd.x - x - buttonsWidth);
            ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
            ImGui::InvisibleButton("##name", ImVec2(nameWidth, rowHeight - dp(2.0f)));
            if (ImGui::IsItemClicked())
                chosen = layer;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                renameFor = layer;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                chosen = layer;
                menuFor = layer;
            }
            ui::tooltip("Click: its settings. Double click: rename it. Right click: its instrument, its chain, remove it.");
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.82f);
            ui::clippedText(list, ImVec2(x + dp(2.0f), midY - ImGui::GetFontSize() * 0.5f), nameWidth - dp(4.0f),
                            state->muted ? p.textFaint : p.text, state->name.toRawUTF8());
            ImGui::PopFont();
            x += nameWidth + dp(2.0f);
            auto mix = Studio::mixOf(*state);
            auto changed = false;
            // Mute, solo.
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.72f);
            ImGui::SetCursorScreenPos(ImVec2(x, origin.y + dp(2.0f)));
            if (ui::iconButton(icon::mute, "Mute", mix.muted, p.record, ImVec2(dp(20.0f), dp(18.0f)))) {
                mix.muted = !mix.muted;
                changed = true;
            }
            x += dp(21.0f);
            ImGui::SetCursorScreenPos(ImVec2(x, origin.y + dp(2.0f)));
            ImGui::PushStyleColor(ImGuiCol_Button, mix.solo ? theme::vec(p.accent) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if (ImGui::Button("S", ImVec2(dp(20.0f), dp(18.0f)))) {
                mix.solo = !mix.solo;
                changed = true;
            }
            ImGui::PopStyleColor();
            ui::tooltip("Solo");
            x += dp(22.0f);
            // Its level.
            ImGui::SetCursorScreenPos(ImVec2(x, origin.y + dp(2.0f)));
            ImGui::SetNextItemWidth(dp(56.0f));
            auto gain = mix.gainDb;
            if (ImGui::DragFloat("##gain", &gain, 0.1f, -60.0f, 12.0f, "%.1f dB")) {
                mix.gainDb = gain;
                changed = true;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                mix.gainDb = 0.0f;
                changed = true;
            }
            x += dp(58.0f);
            // How loud it sounds now.
            const auto peak = std::min(1.0f, std::sqrt(studio_.rackPadPeak(track, layer)));
            list->AddRectFilled(ImVec2(x, origin.y + dp(3.0f)), ImVec2(x + dp(3.0f), rowEnd.y - dp(1.0f)), p.bg1);
            list->AddRectFilled(ImVec2(x, rowEnd.y - dp(1.0f) - (rowEnd.y - origin.y - dp(4.0f)) * peak), ImVec2(x + dp(3.0f), rowEnd.y - dp(1.0f)),
                                colour);
            x += dp(5.0f);
            // Its chain.
            ImGui::SetCursorScreenPos(ImVec2(x, origin.y + dp(2.0f)));
            if (ui::iconButton(icon::chevronRight, "Open its chain: its instrument and effects", false, 0, ImVec2(dp(20.0f), dp(18.0f))))
                state_.openRackPad = std::make_pair(track.owner(), static_cast<int>(layer));
            ImGui::PopFont();
            if (changed)
                (void) studio_.setRackPadMix(track, layer, mix);
            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowHeight));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            ImGui::PopID();
        }
        if (count == 0)
            ImGui::TextDisabled("%s", "No layer yet: add one below.");
    }
    ImGui::EndChild();
    // Popups opened here, outside the list (a child's would close at once).
    if (menuFor)
        ImGui::OpenPopup("##layerMenu");
    if (renameFor) {
        renaming_ = std::make_pair(studio_.routingKey(track), *renameFor);
        if (const auto* state = studio_.rackPad(track, *renameFor))
            juce::String(state->name).copyToUTF8(renameBuffer_, sizeof(renameBuffer_));
        ImGui::OpenPopup("##renameLayer");
    }
    juce::String error;
    if (ImGui::BeginPopup("##layerMenu")) {
        if (chosen < count) {
            if (const auto* state = studio_.rackPad(track, chosen)) {
                ImGui::TextDisabled("%s", state->name.toRawUTF8());
                ImGui::Separator();
                if (ImGui::MenuItem("Open its chain"))
                    state_.openRackPad = std::make_pair(track.owner(), static_cast<int>(chosen));
                if (ImGui::BeginMenu("Change instrument")) {
                    (void) instrumentMenu(studio_, track, 0, chosen, error);
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Rename\xe2\x80\xa6")) {
                    renaming_ = std::make_pair(studio_.routingKey(track), chosen);
                    juce::String(state->name).copyToUTF8(renameBuffer_, sizeof(renameBuffer_));
                    renameFor = chosen;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Remove the layer"))
                    (void) studio_.removeRackPad(track, chosen);
            }
        }
        ImGui::EndPopup();
    }
    if (renameFor && !ImGui::IsPopupOpen("##renameLayer"))
        ImGui::OpenPopup("##renameLayer");
    if (ImGui::BeginPopup("##renameLayer")) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(dp(180.0f));
        const auto done = ImGui::InputText("##name", renameBuffer_, sizeof(renameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (done && renaming_ && renaming_->first == studio_.routingKey(track)) {
            if (const auto* state = studio_.rackPad(track, renaming_->second)) {
                auto mix = Studio::mixOf(*state);
                mix.name = juce::String::fromUTF8(renameBuffer_).trim();
                (void) studio_.setRackPadMix(track, renaming_->second, mix);
            }
            renaming_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // A layer added: its instrument chosen.
    ImGui::SetCursorScreenPos(ImVec2(at.x + dp(4.0f), at.y + size.y - line - dp(2.0f)));
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.82f);
    ImGui::BeginDisabled(count >= static_cast<std::size_t>(container::maximumLayers));
    if (ImGui::Button("+ Layer"))
        ImGui::OpenPopup("##addLayer");
    ImGui::EndDisabled();
    ui::tooltip("A new layer: an instrument, its effects of its own.");
    if (ImGui::BeginPopup("##addLayer")) {
        if (instrumentMenu(studio_, track, 0, std::nullopt, error))
            chosen = studio_.rackPadCount(track) - 1;
        ImGui::EndPopup();
    }
    ImGui::PopFont();
    report(error);
    // The chosen layer's settings at the right.
    if (chosen < studio_.rackPadCount(track))
        drawLayerControls(track, chosen, ImVec2(at.x + listWidth + dp(10.0f), at.y + dp(4.0f)),
                          ImVec2(controlsWidth, size.y - dp(8.0f)), colour);
}

void RackView::drawLayerControls(const TrackRef& track, const std::size_t layer, const ImVec2 at, const ImVec2 size, const ImU32 colour)
{
    const auto& p = theme::palette();
    const auto* state = studio_.rackPad(track, layer);
    if (state == nullptr)
        return;
    const auto width = size.x;
    auto mix = Studio::mixOf(*state);
    auto changed = false;
    const auto layered = studio_.containerKind(track) == container::Kind::layer;
    ImGui::PushID(static_cast<int>(layer) + 1'000);
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    const auto line = ImGui::GetFrameHeight() + dp(3.0f);
    auto y = at.y;
    auto* list = ImGui::GetWindowDrawList();
    list->PushClipRect(at, ImVec2(at.x + width, at.y + dp(18.0f)), true);
    list->AddText(at, p.text, (juce::String("Layer ") + juce::String(static_cast<int>(layer) + 1) + juce::String::fromUTF8(" \xc2\xb7 ")
                               + state->name).toRawUTF8());
    list->PopClipRect();
    y += ImGui::GetFontSize() + dp(5.0f);
    // Its instrument, its chain.
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    if (ImGui::Button("Instrument\xe2\x80\xa6", ImVec2(width * 0.5f - dp(2.0f), 0.0f)))
        ImGui::OpenPopup("##instrument");
    ui::tooltip("Its instrument, for another (its effects stay).");
    ImGui::SameLine(0.0f, dp(4.0f));
    if (ImGui::Button("Chain", ImVec2(width * 0.5f - dp(2.0f), 0.0f)))
        state_.openRackPad = std::make_pair(track.owner(), static_cast<int>(layer));
    ui::tooltip("Its instrument and effects, in the devices (back arrow to come back).");
    if (ImGui::BeginPopup("##instrument")) {
        juce::String error;
        (void) instrumentMenu(studio_, track, 0, layer, error);
        report(error);
        ImGui::EndPopup();
    }
    y += line + dp(2.0f);
    // Its pan.
    ui::KnobStyle style;
    style.diameter = 24.0f;
    style.colour = colour;
    style.label = "Pan";
    style.format = "%.2f";
    style.flags = ui::knobBipolar;
    auto pan = mix.pan;
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    if (ui::knob("##pan", &pan, -1.0f, 1.0f, 0.0f, style)) {
        mix.pan = pan;
        changed = true;
    }
    auto gain = mix.gainDb;
    style.label = "Volume";
    style.unit = "dB";
    style.format = "%.1f";
    style.flags = 0;
    ImGui::SetCursorScreenPos(ImVec2(at.x + width * 0.5f, y));
    if (ui::knob("##gain", &gain, -60.0f, 12.0f, 0.0f, style)) {
        mix.gainDb = gain;
        changed = true;
    }
    y += dp(24.0f) + ImGui::GetFontSize() * 2.2f;
    // A layer's keys and velocities: the notes it plays.
    if (layered) {
        ImGui::SetCursorScreenPos(ImVec2(at.x, y));
        ImGui::SetNextItemWidth(width * 0.5f - dp(2.0f));
        const auto lowLabel = "Keys " + noteName(mix.keyLow);
        if (ImGui::DragInt("##keyLow", &mix.keyLow, 0.15f, 0, mix.keyHigh, lowLabel.toRawUTF8()))
            changed = true;
        ui::tooltip("Its lowest key.");
        ImGui::SameLine(0.0f, dp(4.0f));
        ImGui::SetNextItemWidth(width * 0.5f - dp(2.0f));
        const auto highLabel = "to " + noteName(mix.keyHigh);
        if (ImGui::DragInt("##keyHigh", &mix.keyHigh, 0.15f, mix.keyLow, 127, highLabel.toRawUTF8()))
            changed = true;
        ui::tooltip("Its highest key.");
        y += line;
        ImGui::SetCursorScreenPos(ImVec2(at.x, y));
        ImGui::SetNextItemWidth(width * 0.5f - dp(2.0f));
        if (ImGui::DragInt("##velocityLow", &mix.velocityLow, 0.2f, 1, mix.velocityHigh, "Vel %d"))
            changed = true;
        ui::tooltip("The softest note it plays (velocity).");
        ImGui::SameLine(0.0f, dp(4.0f));
        ImGui::SetNextItemWidth(width * 0.5f - dp(2.0f));
        if (ImGui::DragInt("##velocityHigh", &mix.velocityHigh, 0.2f, mix.velocityLow, 127, "to %d"))
            changed = true;
        ui::tooltip("The loudest note it plays (velocity).");
        y += line;
    }
    // Out of the container.
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    if (ImGui::Button("Remove", ImVec2(width, 0.0f)))
        (void) studio_.removeRackPad(track, layer);
    else if (changed)
        (void) studio_.setRackPadMix(track, layer, mix);
    ImGui::PopFont();
    ImGui::PopID();
}

} // namespace cydaw::v2
