#include "SamplerControlsView.h"

#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace cydaw::v2::sampler_controls {

using ui::dp;
using ui::L;

namespace {

juce::String noteName(const int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1);
}

// The words SFZ instruments name their controls with, in French.
constexpr std::array<std::pair<const char*, const char*>, 44> frenchWords { {
    { "close", "proche" },       { "oh", "ambiance" },       { "overheads", "ambiance" }, { "room", "pièce" },
    { "bottom", "dessous" },     { "btm", "dessous" },       { "top", "dessus" },         { "mic", "micro" },
    { "mics", "micros" },        { "vol", "volume" },        { "volume", "volume" },      { "pan", "panoramique" },
    { "tune", "accord" },        { "tuning", "accord" },     { "deaden", "étouffer" },    { "dryness", "sécheresse" },
    { "position", "position" },  { "bleed", "repisse" },     { "release", "relâchement" }, { "noise", "bruit" },
    { "width", "largeur" },      { "detune", "désaccord" },  { "voices", "voix" },        { "blend", "mélange" },
    { "dynamics", "nuances" },   { "length", "durée" },      { "master", "général" },     { "kick", "grosse caisse" },
    { "snare", "caisse claire" }, { "hi-hat", "charleston" }, { "hihat", "charleston" },  { "tom", "tom" },
    { "toms", "toms" },          { "crash", "crash" },       { "ride", "ride" },          { "china", "china" },
    { "cymbal", "cymbale" },     { "cymbals", "cymbales" },  { "sustain", "maintien" },   { "pedal", "pédale" },
    { "soft", "douce" },         { "extra", "autres" },      { "breath", "souffle" },     { "fingering", "doigté" },
} };

// Whole names first: a word for word "Douce pédale" is not French.
constexpr std::array<std::pair<const char*, const char*>, 26> frenchPhrases { {
    { "sustain pedal", "Pédale forte" },          { "soft pedal", "Pédale douce" },
    { "release noise", "Bruit de relâchement" },  { "release vol", "Volume du relâchement" },
    { "mic blend", "Mélange des micros" },        { "master vol", "Volume général" },
    { "master volume", "Volume général" },        { "master pan", "Panoramique général" },
    { "master pan (disabled)", "Panoramique général (inactif)" },
    { "breath noise", "Bruit de souffle" },       { "fingering noise", "Bruit de doigté" },
    { "hi-hat position", "Position du charleston" }, { "extra mics", "Autres micros" },
    { "mono switch", "Mono" },                    { "low string selection", "Corde grave" },
    { "tailpiece bends", "Bends au cordier" },    { "marcato strength", "Force du marcato" },
    { "sus dynamics", "Nuances des tenues" },     { "legato switch", "Legato" },
    { "cymbal dryness", "Sécheresse des cymbales" }, { "cymbal tune", "Accord des cymbales" },
    { "unison vol", "Volume de l'unisson" },      { "unison width", "Largeur de l'unisson" },
    { "unison detune", "Désaccord de l'unisson" }, { "muting mode", "Étouffement" },
    { "dyn vol", "Volume des nuances" },
} };

// Words in the interface's language, a capital first.
juce::String translated(const juce::String& text)
{
    if (!ui::french())
        return text.substring(0, 1).toUpperCase() + text.substring(1);
    for (const auto& [english, french] : frenchPhrases)
        if (text.trim().equalsIgnoreCase(english))
            return juce::String::fromUTF8(french);
    juce::StringArray words;
    words.addTokens(text, " ", "");
    for (auto& word : words)
        for (const auto& [english, french] : frenchWords)
            if (word.equalsIgnoreCase(english)) {
                word = juce::String::fromUTF8(french);
                break;
            }
    const auto named = words.joinIntoString(" ");
    return named.substring(0, 1).toUpperCase() + named.substring(1);
}

// A control's name in the interface's language: the SFZ's words where it
// has some, the usual controllers' own names when it gives none.
juce::String controlName(const engine::SamplerMap::Control& control)
{
    const auto label = juce::String::fromUTF8(control.label.c_str());
    if (label.startsWith("CC ")) {
        switch (control.controller) {
        case 1: return juce::String::fromUTF8(L("Molette", "Mod wheel"));
        case 2: return juce::String::fromUTF8(L("Souffle", "Breath"));
        case 4: return juce::String::fromUTF8(L("Pédale", "Foot pedal"));
        case 7: return juce::String::fromUTF8(L("Volume", "Volume"));
        case 10: return juce::String::fromUTF8(L("Panoramique", "Pan"));
        case 11: return juce::String::fromUTF8(L("Expression", "Expression"));
        case 64: return juce::String::fromUTF8(L("Pédale forte", "Sustain pedal"));
        case 66: return juce::String::fromUTF8(L("Sostenuto", "Sostenuto"));
        case 67: return juce::String::fromUTF8(L("Pédale douce", "Soft pedal"));
        default: return label;
        }
    }
    return translated(label);
}

// The controls in rows: those whose names start with the same word (a
// piece of a kit, a microphone) together, under that word; the others
// gathered, by their whole names.
struct Group {
    juce::String title;
    std::vector<std::pair<std::size_t, juce::String>> members;
};

std::vector<Group> groupsOf(const engine::SamplerMap& map)
{
    std::vector<Group> groups;
    // Grouped by the SFZ's own words, then shown in the interface's.
    const auto firstWord = [](const juce::String& name) { return name.upToFirstOccurrenceOf(" ", false, false).toLowerCase(); };
    std::vector<juce::String> labels;
    for (const auto& control : map.controls)
        labels.push_back(juce::String::fromUTF8(control.label.c_str()));
    for (std::size_t index = 0; index < labels.size();) {
        const auto word = firstWord(labels[index]);
        auto end = index + 1;
        while (end < labels.size() && labels[end].contains(" ") && firstWord(labels[end]) == word)
            ++end;
        if (labels[index].contains(" ") && !labels[index].startsWith("CC ") && end - index >= 2) {
            const auto title = labels[index].upToFirstOccurrenceOf(" ", false, false);
            // A drum by its size: 18 is 18 inches.
            Group group { title.containsOnly("0123456789") ? title + juce::String::fromUTF8("â³") : translated(title), {} };
            for (auto at = index; at < end; ++at)
                group.members.emplace_back(at, translated(labels[at].fromFirstOccurrenceOf(" ", false, false)));
            groups.push_back(std::move(group));
        } else {
            if (groups.empty() || groups.back().title.isNotEmpty())
                groups.push_back({});
            groups.back().members.emplace_back(index, controlName(map.controls[index]));
            end = index + 1;
        }
        index = end;
    }
    return groups;
}

} // namespace

Count count(app::NodeGraphComponent& graph, const std::uint64_t nodeId)
{
    const auto map = graph.nodeSampler(nodeId);
    if (map == nullptr)
        return {};
    Count result;
    result.controls = static_cast<int>(map->controls.size());
    for (const auto key : map->keyswitchKeys)
        result.articulations += key ? 1 : 0;
    return result;
}

void draw(Studio& studio, UiState& state, const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId,
          const ImU32 colour)
{
    const auto& p = theme::palette();
    const auto map = graph.nodeSampler(nodeId);
    const auto card = graph.editorSamplerControls(nodeId);
    if (map == nullptr || !card || (map->controls.empty() && !map->hasKeyswitches())) {
        ImGui::TextDisabled("%s", L("Cet instrument n'a ni contrôle ni articulation.", "This instrument has no control and no articulation."));
        return;
    }
    const auto prefix = "node." + juce::String(static_cast<juce::int64>(nodeId)) + ".sampler.";
    const auto set = [&](const juce::String& suffix, const double value) {
        (void) graph.editorSetParameter(prefix + suffix, value);
        state.focused = Panel::devices;
    };
    ImGui::PushID("samplerControls");

    // The articulations: the one the next notes play.
    std::vector<int> keys;
    for (int key = 0; key < 128; ++key)
        if (map->keyswitchKeys[static_cast<std::size_t>(key)])
            keys.push_back(key);
    if (!keys.empty()) {
        ui::sectionTitle(L("ARTICULATION", "ARTICULATION"));
        const auto chosen = static_cast<int>(std::lround(card->articulation));
        const auto current = engine::samplerKeyswitch(*map, chosen) ? chosen : map->keyswitchStart;
        const auto right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for (std::size_t index = 0; index < keys.size(); ++index) {
            const auto key = keys[index];
            auto label = noteName(key);
            for (const auto& [labelled, name] : map->keyswitchLabels)
                if (labelled == key)
                    label = juce::String::fromUTF8(name.c_str()) + " · " + label;
            const auto width = ImGui::CalcTextSize(label.toRawUTF8()).x + dp(16.0f);
            if (index > 0) {
                ImGui::SameLine();
                if (ImGui::GetCursorScreenPos().x + width > right)
                    ImGui::NewLine();
            }
            auto on = key == current;
            ImGui::PushID(key);
            if (ui::toggle(label.toRawUTF8(), &on, colour, ImVec2(width, dp(22.0f))))
                set("articulation", key);
            ImGui::PopID();
            ui::tooltip(L("Le jeu des notes suivantes. En jouant, sa touche (keyswitch) le choisit aussi.",
                          "What the next notes play. While playing, its key (a keyswitch) chooses it too."));
        }
        ImGui::Spacing();
    }

    // The controllers, as knobs in rows: a row a piece.
    if (!map->controls.empty()) {
        ui::sectionTitle(L("CONTRÔLES", "CONTROLS"));
        constexpr auto diameter = 30.0f;
        const auto cell = dp(84.0f);
        const auto titleWidth = dp(118.0f);
        const auto left = ImGui::GetCursorScreenPos().x;
        const auto columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x - titleWidth) / cell));
        for (const auto& group : groupsOf(*map)) {
            // Its title, at the left of its row.
            const auto rowTop = ImGui::GetCursorScreenPos().y;
            if (group.title.isNotEmpty()) {
                ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 0.86f);
                ImGui::GetWindowDrawList()->AddText(ImVec2(left, rowTop + dp(diameter) * 0.5f - ImGui::GetFontSize() * 0.5f),
                                                    colour, group.title.toRawUTF8());
                ImGui::PopFont();
            }
            for (std::size_t member = 0; member < group.members.size(); ++member) {
                const auto& [index, shortName] = group.members[member];
                const auto& control = map->controls[index];
                const auto number = static_cast<std::size_t>(std::clamp(control.controller, 0, 127));
                const auto start = map->controllerStart[number];
                const auto setOnCard = card->controllers[number];
                const auto value = setOnCard >= 0.0f ? setOnCard : start;
                const auto name = controlName(control);
                const auto suffix = "cc" + juce::String(static_cast<int>(number));
                if (static_cast<int>(member) % columns == 0) {
                    if (member > 0)
                        ImGui::NewLine();
                    ImGui::SetCursorScreenPos(ImVec2(left + titleWidth, ImGui::GetCursorScreenPos().y));
                } else {
                    ImGui::SameLine(0.0f, 0.0f);
                }
                ImGui::PushID(static_cast<int>(number));
                ImGui::BeginGroup();
                const auto origin = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(origin.x + (cell - dp(diameter)) * 0.5f, origin.y));
                ui::KnobStyle style;
                style.diameter = diameter;
                style.flags = ui::knobNoLabel;
                style.colour = colour;
                auto normalised = value / 127.0f;
                bool changed {};
                bool hovered {};
                bool active {};
                {
                    const ui::LearnParameterScope learn(prefix + suffix, name);
                    changed = ui::knob("##knob", &normalised, 0.0f, 1.0f, start / 127.0f, style);
                    hovered = ImGui::IsItemHovered();
                    active = ImGui::IsItemActive();
                }
                if (changed)
                    set(suffix, std::clamp(normalised, 0.0f, 1.0f) * 127.0);
                const auto binding = prefix + suffix;
                if (ImGui::BeginPopupContextItem("##controlMenu")) {
                    ImGui::TextDisabled("%s (CC %d)", name.toRawUTF8(), static_cast<int>(number));
                    if (ImGui::MenuItem(L("Valeur de départ de l'instrument", "The instrument's starting value")))
                        set(suffix, -1.0);
                    if (studio.isValid(track) && studio.canAutomate(track, binding)) {
                        const auto automated = !studio.lanePoints(track, binding).empty();
                        if (ImGui::MenuItem(automated ? L("Afficher l'automation", "Show the automation")
                                                      : L("Automatiser dans l'arrangement", "Automate in the arrangement"))) {
                            studio.setShownLane(track, binding);
                            studio.setAutomationVisible(track, true);
                            state.selectTrack(track);
                            state.bringToFront = Panel::arrangement;
                        }
                        if (automated && ImGui::MenuItem(L("Effacer l'automation", "Clear the automation")))
                            studio.setLanePoints(track, binding, {});
                    }
                    ImGui::EndPopup();
                }
                if (hovered && !active)
                    ImGui::SetTooltip("%s : %.0f (CC %d)\n%s", name.toRawUTF8(), static_cast<double>(value),
                                      static_cast<int>(number),
                                      L("Double-clic : valeur de départ · clic droit : automatiser",
                                        "Double click: starting value · right click: automate"));
                // Its name under it (its value while it moves).
                auto* list = ImGui::GetWindowDrawList();
                ImGui::PushFont(nullptr, ui::currentFontBase() * 0.76f);
                const auto text = hovered || active ? juce::String(static_cast<int>(std::lround(value))) : shortName;
                const auto y = origin.y + dp(diameter) + dp(2.0f);
                const auto size = ImGui::CalcTextSize(text.toRawUTF8());
                const auto x = origin.x + std::max(dp(2.0f), (cell - size.x) * 0.5f);
                ui::clippedText(list, ImVec2(x, y), cell - dp(4.0f), hovered || active ? p.text : p.textDim, text.toRawUTF8());
                const auto textHeight = ImGui::GetFontSize();
                ImGui::PopFont();
                // A click on its name types its value in (0 to 127).
                {
                    ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(2.0f), y));
                    auto typed = static_cast<double>(value);
                    if (ui::valueEntryButton("##typed", ImVec2(cell - dp(4.0f), textHeight), typed, 0.0, 127.0))
                        set(suffix, typed);
                    if (ImGui::IsItemHovered())
                        ui::tooltip(L("Clic : taper la valeur (0 à 127)", "Click: type the value (0 to 127)"));
                }
                ImGui::SetCursorScreenPos(origin);
                ImGui::Dummy(ImVec2(cell, dp(diameter) + textHeight + dp(8.0f)));
                ImGui::EndGroup();
                ImGui::PopID();
            }
            // A thin line between pieces.
            const auto bottom = ImGui::GetItemRectMax().y + dp(2.0f);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(left, bottom), ImVec2(left + titleWidth + cell * static_cast<float>(columns), bottom),
                                                p.line);
            ImGui::SetCursorScreenPos(ImVec2(left, bottom + dp(4.0f)));
            ImGui::Dummy(ImVec2(1.0f, 1.0f));
        }
    }
    ImGui::PopID();
}

} // namespace cydaw::v2::sampler_controls
