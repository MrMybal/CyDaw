#include "SamplerView.h"

#include "Icons.h"
#include "SamplerControlsView.h"
#include "SamplerMapFile.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

juce::var copyOf(const juce::var& json)
{
    return juce::JSON::parse(juce::JSON::toString(json, true));
}

juce::Array<juce::var>* zonesOf(juce::var& json)
{
    return json.getProperty("zones", {}).getArray();
}

const juce::Array<juce::var>* zonesOf(const juce::var& json)
{
    return json.getProperty("zones", {}).getArray();
}

juce::String noteName(const int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1);
}

std::pair<int, int> pairOf(const juce::var& zone, const char* key, const std::pair<int, int> fallback)
{
    if (const auto* list = zone.getProperty(key, {}).getArray(); list != nullptr && list->size() == 2)
        return { static_cast<int>((*list)[0]), static_cast<int>((*list)[1]) };
    return fallback;
}

void setPair(juce::var& zone, const char* key, const juce::int64 first, const juce::int64 second)
{
    juce::Array<juce::var> list;
    list.add(first);
    list.add(second);
    if (auto* object = zone.getDynamicObject())
        object->setProperty(key, list);
}

void setValue(juce::var& zone, const char* key, const juce::var& value)
{
    if (auto* object = zone.getDynamicObject())
        object->setProperty(key, value);
}

bool isAudioFile(const juce::File& file)
{
    return file.hasFileExtension("wav;aif;aiff;flac;ogg;mp3;wv");
}

} // namespace

SamplerView::SamplerView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}
SamplerView::~SamplerView() = default;

juce::var SamplerView::currentMap(const Target& target) const
{
    if (const auto* slot = studio_.graphFor(target.track))
        return slot->graph().nodeSamplerJson(target.nodeId);
    return {};
}

void SamplerView::report(const juce::String& message, const bool error)
{
    state_.status = message;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetTime();
}

void SamplerView::commit(const Target& target, const juce::var& json, const bool applySettings)
{
    juce::String error;
    if (!studio_.setSamplerMap(target.track, target.nodeId, json, applySettings, error))
        report(error, true);
}

void SamplerView::openEditor(const TrackRef& track, const std::uint64_t nodeId, const bool detached, const bool controls,
                             const bool here)
{
    const auto id = windows_.open(track, nodeId, state_.manyInstrumentWindows);
    placeHere_ = here && !detached ? id : std::string();
    if (detached)
        ui::setDetached(id.c_str(), true);
    else if (here)
        ui::setDetached(id.c_str(), false);
    auto& kept = windowStates_[id];
    kept = {};
    kept.showControls = controls;
}

void SamplerView::addFiles(const TrackRef& track, const std::uint64_t nodeId, const juce::StringArray& paths)
{
    juce::Array<juce::File> audio;
    for (const auto& path : paths) {
        const juce::File file(path);
        if (file.hasFileExtension("sfz;cysampler")) {
            juce::String error;
            if (!studio_.loadSamplerFile(file, track, error))
                report(error, true);
            else
                report(file.getFileName());
            return;
        }
        if (isAudioFile(file))
            audio.add(file);
    }
    if (audio.isEmpty())
        return;
    const Target target { track, nodeId };
    // The simple sampler holds one sample, over the whole keyboard around
    // its note (its name's, its pitch's; middle C for a sound without one),
    // played at every key's pitch: a sample dropped replaces it.
    const auto* slot = studio_.graphFor(track);
    if (const auto controls = slot != nullptr ? slot->graph().editorSamplerControls(nodeId) : std::nullopt;
        controls && controls->simple) {
        pane_ = {};
        commit(target, app::simpleSamplerMap(audio[0]));
        report(audio[0].getFileName());
        return;
    }
    const auto current = currentMap(target);
    const auto zones = zonesOf(current);
    const auto json = zones != nullptr && !zones->isEmpty()
                          ? app::withFilesAdded(current, audio)
                          : app::samplerMapFromFiles(audio, audio.size() == 1 ? audio[0].getFileNameWithoutExtension()
                                                                               : audio[0].getParentDirectory().getFileName());
    commit(target, json, zones == nullptr || zones->isEmpty());
    report(juce::String(audio.size()) + ui::Ls(" échantillon(s) ajouté(s)", " sample(s) added"));
}

void SamplerView::chooseFiles(const Target& target)
{
    chooser_ = std::make_unique<juce::FileChooser>(L("Des échantillons pour ce sampler", "Samples for this sampler"),
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, target](const juce::FileChooser& chooser) {
                              juce::StringArray paths;
                              for (const auto& file : chooser.getResults())
                                  paths.add(file.getFullPathName());
                              if (!paths.isEmpty())
                                  addFiles(target.track, target.nodeId, paths);
                          });
}

void SamplerView::chooseSfz(const Target& target)
{
    chooser_ = std::make_unique<juce::FileChooser>(L("Un instrument SFZ (ou une carte .cysampler)",
                                                     "An SFZ instrument (or a .cysampler map)"),
                                                   juce::File(), "*.sfz;*.cysampler");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, target](const juce::FileChooser& chooser) {
                              if (const auto file = chooser.getResult(); file != juce::File())
                                  addFiles(target.track, target.nodeId, juce::StringArray { file.getFullPathName() });
                          });
}

void SamplerView::exportSfz(const Target& target)
{
    chooser_ = std::make_unique<juce::FileChooser>(L("Enregistrer en SFZ", "Save as SFZ"), juce::File(), "*.sfz");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, target](const juce::FileChooser& chooser) {
                              auto file = chooser.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension("sfz");
                              if (file.replaceWithText(app::samplerMapToSfz(currentMap(target), file)))
                                  report(ui::Ls("Écrit : ", "Written: ") + file.getFileName());
                              else
                                  report(ui::Ls("Écriture impossible : ", "Cannot write: ") + file.getFullPathName(), true);
                          });
}

int SamplerView::drawMap(const juce::var& json, const ImVec2 at, const ImVec2 size, const int selected, const ImU32 colour,
                         const bool interactive)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + size.x, at.y + size.y);
    list->AddRectFilled(at, end, p.bg0, dp(3.0f));
    const auto* zones = zonesOf(json);
    if (zones == nullptr || zones->isEmpty())
        return -1;
    // The keys shown: the roots, and a few keys on either side of the
    // outermost (the first and last zones reach the ends of the keyboard),
    // in whole octaves.
    auto low = 127;
    auto high = 0;
    for (const auto& zone : *zones) {
        const auto root = static_cast<int>(zone.getProperty("root", 60));
        const auto keys = pairOf(zone, "keys", { root, root });
        low = std::min({ low, root, std::max(keys.first, root - 5) });
        high = std::max({ high, root, std::min(keys.second, root + 5) });
    }
    low = std::max(0, low / 12 * 12);
    high = std::min(127, (high / 12 + 1) * 12 - 1);
    const auto keyboardHeight = std::min(dp(10.0f), size.y * 0.2f);
    const auto mapBottom = end.y - keyboardHeight;
    const auto keyWidth = size.x / static_cast<float>(high - low + 1);
    const auto xOf = [&](const double key) { return at.x + static_cast<float>(key - low) * keyWidth; };
    const auto yOf = [&](const double velocity) {
        return mapBottom - static_cast<float>(velocity / 127.0) * (mapBottom - at.y - dp(1.0f));
    };
    // The keyboard under the map.
    for (auto key = low; key <= high; ++key) {
        const auto black = ((1 << (key % 12)) & 0x54a) != 0;
        list->AddRectFilled(ImVec2(xOf(key) + 0.5f, mapBottom + 1.0f), ImVec2(xOf(key + 1) - 0.5f, end.y),
                            black ? p.bg3 : p.text);
        if (key % 12 == 0 && keyWidth * 12.0f > dp(36.0f)) {
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.62f);
            list->AddText(ImVec2(xOf(key) + 1.0f, at.y + 1.0f), p.textFaint, noteName(key).toRawUTF8());
            ImGui::PopFont();
        }
    }
    auto hovered = -1;
    const auto mouse = ImGui::GetIO().MousePos;
    for (int index = 0; index < zones->size(); ++index) {
        const auto& zone = (*zones)[index];
        const auto root = static_cast<int>(zone.getProperty("root", 60));
        const auto keys = pairOf(zone, "keys", { root, root });
        const auto velocities = pairOf(zone, "velocities", { 1, 127 });
        const ImVec2 min(xOf(std::max(low, keys.first)), yOf(velocities.second));
        const ImVec2 max(xOf(std::min(high, keys.second) + 1), yOf(velocities.first - 1));
        if (max.x <= min.x)
            continue;
        const auto isSelected = index == selected;
        list->AddRectFilled(min, max, isSelected ? (p.accent & 0x00ffffffu) | 0x70000000u : (colour & 0x00ffffffu) | 0x38000000u);
        list->AddRect(min, max, isSelected ? p.accent : (colour & 0x00ffffffu) | 0xa0000000u);
        // The root, as a dot on the zone's floor.
        if (root >= low && root <= high)
            list->AddCircleFilled(ImVec2(xOf(root + 0.5), max.y - dp(2.5f)), std::min(dp(2.0f), keyWidth * 0.4f),
                                  isSelected ? p.text : p.textDim);
        if (mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y)
            hovered = index;
    }
    if (!interactive)
        return -1;
    ImGui::SetCursorScreenPos(at);
    ImGui::InvisibleButton("##zoneMap", size);
    if (ImGui::IsItemHovered() && hovered >= 0) {
        const auto& zone = (*zones)[hovered];
        const auto root = static_cast<int>(zone.getProperty("root", 60));
        const auto keys = pairOf(zone, "keys", { root, root });
        const auto velocities = pairOf(zone, "velocities", { 1, 127 });
        ImGui::SetTooltip("%s\n%s %s  %s %s-%s  %s %d-%d",
                          juce::File(app::resolveSamplePath(zone.getProperty("file", {}).toString())).getFileName().toRawUTF8(),
                          L("racine", "root"), noteName(root).toRawUTF8(), L("touches", "keys"),
                          noteName(keys.first).toRawUTF8(), noteName(keys.second).toRawUTF8(), L("vélocités", "velocities"),
                          velocities.first, velocities.second);
    }
    return ImGui::IsItemClicked() ? hovered : -2;
}

void SamplerView::drawPane(const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId, const ImVec2 at,
                           const ImVec2 size, const ImU32 colour)
{
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    if (const auto controls = graph.editorSamplerControls(nodeId); controls && controls->simple) {
        drawSimplePane(track, graph, nodeId, at, size);
        return;
    }
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + size.x, at.y + size.y);
    list->AddRectFilled(at, end, p.bg1, dp(4.0f));
    const auto json = graph.nodeSamplerJson(nodeId);
    const auto textHeight = ImGui::GetFontSize() * 2.3f;
    const ImVec2 mapAt(at.x + dp(3.0f), at.y + dp(3.0f));
    const ImVec2 mapSize(size.x - dp(6.0f), size.y - textHeight - dp(4.0f));
    const auto* zones = zonesOf(json);
    const auto empty = zones == nullptr || zones->isEmpty();
    ImGui::PushID("samplerPane");
    if (!empty) {
        (void) drawMap(json, mapAt, mapSize, -1, colour, false);
    } else {
        list->AddRectFilled(mapAt, ImVec2(mapAt.x + mapSize.x, mapAt.y + mapSize.y), p.bg0, dp(3.0f));
        ui::centredText(list, mapAt, ImVec2(mapAt.x + mapSize.x, mapAt.y + mapSize.y), p.textFaint,
                        L("Déposez des échantillons ici", "Drop samples here"));
    }
    // An SFZ's articulations and controls: a button over the map (before
    // the map's own area, so that it takes the click).
    if (const auto has = sampler_controls::count(graph, nodeId); has.controls > 0 || has.articulations > 0) {
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
        const auto label = juce::String::fromUTF8(L("Contrôles", "Controls")) + " (" + juce::String(has.controls + has.articulations) + ")";
        const auto width = ImGui::CalcTextSize(label.toRawUTF8()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(mapAt.x + mapSize.x - width - dp(2.0f), mapAt.y + dp(2.0f)));
        if (ImGui::SmallButton(label.toRawUTF8()))
            openEditor(track, nodeId, false, true);
        ui::tooltip(L("Les articulations et les contrôles de l'instrument (micros, mélanges, pédales…), automatisables.",
                      "The instrument's articulations and controls (microphones, blends, pedals…), automatable."));
        ImGui::PopFont();
    }
    // Samples dragged from the browser, or dropped from the system (the
    // drop handler asks where this pane was).
    ImGui::SetCursorScreenPos(mapAt);
    ImGui::InvisibleButton("##samplerDrop", mapSize);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        openEditor(track, nodeId);
    ui::tooltip(L("Les zones du sampler (touches en travers, vélocités en hauteur). Double-clic : les éditer. "
                  "Glissez-y des échantillons (du navigateur ou de l'explorateur), ou un SFZ.",
                  "The sampler's zones (keys across, velocities up). Double click: edit them. Drag samples onto it "
                  "(from the browser or the file explorer), or an SFZ."));
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload); payload != nullptr && payload->DataSize > 1)
            addFiles(track, nodeId, juce::StringArray { juce::String::fromUTF8(static_cast<const char*>(payload->Data)) });
        ImGui::EndDragDropTarget();
    }
    state_.samplerDrop = UiState::SamplerDrop { track, nodeId, at.x, at.y, end.x, end.y };

    // What it holds, and how far its samples are read.
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    auto description = empty ? ui::Ls("Aucun échantillon", "No sample") : app::describeSamplerMap(json);
    if (const auto map = graph.nodeSampler(nodeId)) {
        auto loading = 0;
        auto missing = 0;
        for (const auto& zone : map->zones) {
            if (zone.sample == nullptr || zone.sample->failed.load())
                ++missing;
            else if (!zone.sample->ready.load() && !zone.sample->asleep())
                ++loading;
        }
        if (loading > 0)
            description << "\n" << ui::Ls("Chargement : ", "Loading: ") << loading;
        else if (missing > 0)
            description << "\n" << missing << ui::Ls(" fichier(s) introuvable(s)", " file(s) missing");
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x + dp(4.0f), end.y - textHeight + dp(1.0f)));
    list->PushClipRect(at, ImVec2(end.x - dp(60.0f), end.y), true);
    ui::pushTextWrapAt(end.x - dp(62.0f));
    ImGui::TextColored(theme::vec(p.textDim), "%s", description.toRawUTF8());
    ImGui::PopTextWrapPos();
    list->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(end.x - dp(58.0f), end.y - textHeight + dp(1.0f)));
    if (ImGui::SmallButton(L("Zones…", "Zones…")))
        openEditor(track, nodeId);
    ui::tooltip(L("Éditer les zones : touches, vélocités, accord, boucles…", "Edit the zones: keys, velocities, tuning, loops…"));
    ImGui::SetCursorScreenPos(ImVec2(end.x - dp(58.0f), end.y - textHeight + dp(1.0f) + ImGui::GetFrameHeight()));
    if (ImGui::SmallButton(L("Ajouter…", "Add…")))
        chooseFiles(Target { track, nodeId });
    ui::tooltip(L("Ajouter des échantillons (WAV, FLAC, AIFF…) : leur note vient de leur nom ou de leur hauteur.",
                  "Add samples (WAV, FLAC, AIFF…): their note comes from their name or their pitch."));
    ImGui::PopFont();
    ImGui::PopID();
}

void SamplerView::drawSimplePane(const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId, const ImVec2 at,
                                 const ImVec2 size)
{
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + size.x, at.y + size.y);
    list->AddRectFilled(at, end, p.bg1, dp(4.0f));
    const Target target { track, nodeId };
    const auto mine = pane_.target.has_value() && pane_.target->track == track && pane_.target->nodeId == nodeId;
    auto json = mine && pane_.editing ? pane_.working : copyOf(graph.nodeSamplerJson(nodeId));
    auto* zones = zonesOf(json);
    const auto empty = zones == nullptr || zones->isEmpty();
    ImGui::PushID("simplePane");
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    const auto row = ImGui::GetFrameHeight() + dp(3.0f);
    // Granular (Nuée): its grains' settings, a row of knobs under the sound.
    std::vector<app::NodeGraphComponent::EditorParameter> grainSettings;
    auto granular = false;
    for (const auto& node : graph.editorNodes())
        if (node.id == nodeId)
            for (const auto& parameter : node.parameters) {
                if (parameter.binding.endsWith(".sampler.grains"))
                    granular = parameter.value >= 0.5;
                else if (parameter.binding.contains(".sampler.grain"))
                    grainSettings.push_back(parameter);
            }
    const auto knobRow = granular ? dp(22.0f) + ImGui::GetFontSize() * 2.2f : 0.0f;
    const ImVec2 waveAt(at.x + dp(4.0f), at.y + dp(4.0f));
    const ImVec2 waveSize(size.x - dp(8.0f), std::max(dp(30.0f), size.y - row * 3.0f - dp(8.0f) - knobRow));
    if (empty) {
        const ImVec2 waveEnd(waveAt.x + waveSize.x, waveAt.y + waveSize.y);
        list->AddRectFilled(waveAt, waveEnd, p.bg0, dp(3.0f));
        ui::centredText(list, waveAt, waveEnd, p.textFaint, L("Déposez un échantillon ici", "Drop a sample here"));
        ImGui::SetCursorScreenPos(waveAt);
        ImGui::InvisibleButton("##simpleEmpty", waveSize);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            chooseFiles(target);
        ui::tooltip(L("Un échantillon joué sur tout le clavier. Glissez-en un ici (du navigateur ou de l'explorateur), "
                      "ou double-cliquez pour le choisir.",
                      "One sample played over the whole keyboard. Drag one here (from the browser or the file explorer), "
                      "or double click to choose it."));
    } else {
        // Its waveform and markers, the loop and its length.
        ImGui::SetCursorScreenPos(waveAt);
        drawWaveform(target, json, 0, waveSize, pane_.peaks, pane_.working, pane_.editing, true);
        if (pane_.editing)
            pane_.target = target;
        if (granular) {
            // Where the grains are taken (and how far around), over the sound.
            const auto valueOf = [&grainSettings](const char* suffix, const double fallback) {
                for (const auto& parameter : grainSettings)
                    if (parameter.binding.endsWith(suffix))
                        return parameter.value;
                return fallback;
            };
            const auto position = std::clamp(valueOf(".grainPosition", 0.3), 0.0, 1.0);
            const auto spray = std::clamp(valueOf(".grainSpray", 0.05), 0.0, 1.0);
            const auto scan = valueOf(".grainScan", 0.0);
            const auto x = waveAt.x + static_cast<float>(position) * waveSize.x;
            const auto half = static_cast<float>(spray * 0.5) * waveSize.x;
            const auto tint = IM_COL32(150, 210, 255, 255);
            list->AddRectFilled(ImVec2(std::max(waveAt.x, x - half), waveAt.y), ImVec2(std::min(waveAt.x + waveSize.x, x + half), waveAt.y + waveSize.y),
                                theme::alpha(tint, 0.18f));
            list->AddLine(ImVec2(x, waveAt.y), ImVec2(x, waveAt.y + waveSize.y), tint, dp(2.0f));
            if (std::abs(scan) > 0.005) {
                const auto direction = scan > 0.0 ? 1.0f : -1.0f;
                const ImVec2 tip(x + direction * dp(14.0f), waveAt.y + dp(8.0f));
                list->AddLine(ImVec2(x, tip.y), tip, tint, dp(1.5f));
                list->AddTriangleFilled(tip, ImVec2(tip.x - direction * dp(5.0f), tip.y - dp(4.0f)),
                                        ImVec2(tip.x - direction * dp(5.0f), tip.y + dp(4.0f)), tint);
            }
            // Dragging along the sound sets where the grains are taken.
            ImGui::SetCursorScreenPos(ImVec2(waveAt.x, waveAt.y + waveSize.y * 0.5f));
            ImGui::InvisibleButton("##grainPlace", ImVec2(waveSize.x, waveSize.y * 0.5f));
            if (ImGui::IsItemActive()) {
                const auto place = std::clamp((ImGui::GetIO().MousePos.x - waveAt.x) / std::max(1.0f, waveSize.x), 0.0f, 1.0f);
                for (const auto& parameter : grainSettings)
                    if (parameter.binding.endsWith(".grainPosition"))
                        (void) graph.editorSetParameter(parameter.binding, place);
            }
            ui::tooltip(L("Glisser ici : l'endroit où les grains sont pris.", "Drag here: where the grains are taken."));
        }
        // Its note: where it plays at its own pitch.
        auto& zone = zones->getReference(0);
        auto root = static_cast<int>(zone.getProperty("root", 60));
        ImGui::SetCursorScreenPos(ImVec2(waveAt.x, ImGui::GetCursorScreenPos().y));
        ImGui::SetNextItemWidth(dp(96.0f));
        const auto format = juce::String::fromUTF8(L("Racine ", "Root ")) + noteName(root);
        if (ImGui::DragInt("##root", &root, 0.15f, 0, 127, format.toRawUTF8())) {
            setValue(zone, "root", root);
            pane_.editing = true;
            pane_.working = json;
            pane_.target = target;
        }
        ui::tooltip(L("La note où l'échantillon joue à sa hauteur enregistrée (glisser, ou Ctrl+clic pour la taper).",
                      "The key that plays the sample at its recorded pitch (drag, or Ctrl+click to type it)."));
        ImGui::SameLine();
        const auto file = juce::File(app::resolveSamplePath(zone.getProperty("file", {}).toString())).getFileNameWithoutExtension();
        ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(end.x - dp(4.0f), end.y), true);
        ImGui::TextColored(theme::vec(p.textDim), "%s", file.toRawUTF8());
        ImGui::PopClipRect();
    }
    // The grains' knobs.
    if (granular && !grainSettings.empty()) {
        const auto top = end.y - row - knobRow - dp(2.0f);
        const auto cell = (size.x - dp(8.0f)) / static_cast<float>(grainSettings.size());
        for (std::size_t index = 0; index < grainSettings.size(); ++index) {
            const auto& parameter = grainSettings[index];
            auto value = static_cast<float>(parameter.value);
            ui::KnobStyle style;
            style.diameter = 22.0f;
            const auto label = parameter.label.toStdString();
            style.label = label.c_str();
            style.colour = IM_COL32(150, 210, 255, 255);
            const auto unit = parameter.unit.toStdString();
            style.unit = unit.c_str();
            style.format = parameter.maximum - parameter.minimum >= 100.0 ? "%.0f" : "%.2f";
            if (parameter.unit == "ms" || parameter.unit == "Hz")
                style.flags |= ui::knobLogarithmic;
            if (parameter.minimum < 0.0)
                style.flags |= ui::knobBipolar;
            ImGui::SetCursorScreenPos(ImVec2(at.x + dp(4.0f) + cell * static_cast<float>(index) + (cell - dp(34.0f)) * 0.5f, top));
            ImGui::PushID(static_cast<int>(index));
            if (ui::knob("##grain", &value, static_cast<float>(parameter.minimum), static_cast<float>(parameter.maximum),
                         static_cast<float>(parameter.defaultValue), style))
                (void) graph.editorSetParameter(parameter.binding, value);
            ImGui::PopID();
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x + dp(4.0f), end.y - row));
    if (ImGui::SmallButton(empty ? L("Charger…", "Load…") : L("Remplacer…", "Replace…")))
        chooseFiles(target);
    ui::tooltip(L("Choisir l'échantillon (WAV, FLAC, AIFF…).", "Choose the sample (WAV, FLAC, AIFF…)."));
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Éditer…", "Edit…")))
        openEditor(track, nodeId);
    ui::tooltip(L("La zone en détail : touches, vélocités, accord, niveau.", "The zone in detail: keys, velocities, tuning, level."));
    // A sample dropped anywhere on it (the browser's), or from the system
    // (the drop handler asks where this pane was).
    if (ImGui::BeginDragDropTargetCustom(ImRect(at, end), ImGui::GetID("##simpleDrop"))) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload); payload != nullptr && payload->DataSize > 1)
            addFiles(track, nodeId, juce::StringArray { juce::String::fromUTF8(static_cast<const char*>(payload->Data)) });
        ImGui::EndDragDropTarget();
    }
    state_.samplerDrop = UiState::SamplerDrop { track, nodeId, at.x, at.y, end.x, end.y };
    ImGui::PopFont();
    ImGui::PopID();
    // A marker or the root let go: the edit becomes the card's (one undo step).
    if (pane_.editing && pane_.target.has_value() && pane_.target->track == track && pane_.target->nodeId == nodeId
        && !ImGui::IsAnyItemActive()) {
        pane_.editing = false;
        commit(target, pane_.working);
        pane_.working = juce::var();
    }
}

void SamplerView::drawZoneFields(const Target& target, juce::var& json, const int zone)
{
    auto* zones = zonesOf(json);
    if (zones == nullptr || zone < 0 || zone >= zones->size())
        return;
    auto& item = zones->getReference(zone);
    auto changed = false;
    auto root = static_cast<int>(item.getProperty("root", 60));
    const auto fieldWidth = dp(150.0f);
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::DragInt(ui::labelId(L("Racine", "Root"), "root").c_str(), &root, 0.15f, 0, 127,
                       (noteName(root) + " (%d)").toRawUTF8())) {
        setValue(item, "root", root);
        changed = true;
    }
    ui::tooltip(L("La note où l'échantillon joue à sa hauteur enregistrée.", "The key that plays the sample at its recorded pitch."));
    auto keys = pairOf(item, "keys", { 0, 127 });
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::DragIntRange2(ui::labelId(L("Touches", "Keys"), "keys").c_str(), &keys.first, &keys.second, 0.15f, 0, 127,
                             noteName(keys.first).toRawUTF8(), noteName(keys.second).toRawUTF8())) {
        setPair(item, "keys", keys.first, keys.second);
        changed = true;
    }
    auto velocities = pairOf(item, "velocities", { 1, 127 });
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::DragIntRange2(ui::labelId(L("Vélocités", "Velocities"), "velocities").c_str(), &velocities.first,
                             &velocities.second, 0.25f, 1, 127)) {
        setPair(item, "velocities", velocities.first, velocities.second);
        changed = true;
    }
    auto tune = static_cast<float>(static_cast<double>(item.getProperty("tune", 0.0)));
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::DragFloat(ui::labelId(L("Accord", "Tuning"), "tune").c_str(), &tune, 0.5f, -1'200.0f, 1'200.0f, "%.1f c")) {
        setValue(item, "tune", tune);
        changed = true;
    }
    auto gain = static_cast<float>(static_cast<double>(item.getProperty("gain", 0.0)));
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::DragFloat(ui::labelId(L("Gain", "Gain"), "gain").c_str(), &gain, 0.1f, -48.0f, 24.0f, "%.1f dB")) {
        setValue(item, "gain", gain);
        changed = true;
    }
    auto pan = static_cast<float>(static_cast<double>(item.getProperty("pan", 0.0)));
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::SliderFloat(ui::labelId(L("Panoramique", "Pan"), "pan").c_str(), &pan, -1.0f, 1.0f, "%.2f")) {
        setValue(item, "pan", pan);
        changed = true;
    }
    auto sequence = pairOf(item, "sequence", { 1, 1 });
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::DragIntRange2(ui::labelId(L("Tour (round robin)", "Round robin"), "sequence").c_str(), &sequence.first,
                             &sequence.second, 0.05f, 1, 16, L("%d en tout", "%d in all"), L("le %d-e", "number %d"))) {
        sequence.second = std::clamp(sequence.second, 1, std::max(1, sequence.first));
        setPair(item, "sequence", sequence.first, sequence.second);
        changed = true;
    }
    ui::tooltip(L("Plusieurs zones sur les mêmes touches jouent chacune à son tour : combien en tout, et laquelle est celle-ci.",
                  "Several zones on the same keys play in turn: how many in all, and which one this is."));
    if (changed) {
        editing_ = true;
        working_ = json;
    }
    // Detect the pitch heard in it: its root and tuning from the sound.
    if (ImGui::Button(L("Hauteur du son", "Pitch from the sound"))) {
        if (const auto* slot = studio_.graphFor(target.track))
            if (const auto map = slot->graph().nodeSampler(target.nodeId);
                map != nullptr && zone < static_cast<int>(map->zones.size()) && map->zones[static_cast<std::size_t>(zone)].sample) {
                const auto heard = app::detectSamplePitch(*map->zones[static_cast<std::size_t>(zone)].sample);
                if (heard >= 0.0) {
                    const auto note = std::clamp(static_cast<int>(std::lround(heard)), 0, 127);
                    auto edited = copyOf(json);
                    auto& edit = zonesOf(edited)->getReference(zone);
                    setValue(edit, "root", note);
                    setValue(edit, "tune", std::round((note - heard) * 1'000.0) / 10.0);
                    commit(target, edited);
                    report(ui::Ls("Hauteur entendue : ", "Pitch heard: ") + noteName(note)
                           + juce::String::formatted(" %+.1f c", (heard - note) * 100.0));
                } else {
                    report(ui::Ls("Aucune hauteur nette dans ce son", "No clear pitch in this sound"), true);
                }
            }
    }
    ui::tooltip(L("La racine et l'accord d'après la hauteur entendue dans l'échantillon.",
                  "The root and tuning from the pitch heard in the sample."));
}

void SamplerView::drawWaveform(const Target& target, juce::var& json, const int zone, const ImVec2 size, Peaks& peaks,
                               juce::var& working, bool& editing, const bool compact)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto at = ImGui::GetCursorScreenPos();
    const ImVec2 end(at.x + size.x, at.y + size.y);
    list->AddRectFilled(at, end, p.bg0, dp(3.0f));
    auto* zones = zonesOf(json);
    const auto* slot = studio_.graphFor(target.track);
    const auto map = slot != nullptr ? slot->graph().nodeSampler(target.nodeId) : engine::SamplerMapRef {};
    if (zones == nullptr || zone < 0 || zone >= zones->size() || map == nullptr
        || zone >= static_cast<int>(map->zones.size())) {
        ImGui::Dummy(size);
        return;
    }
    const auto& data = map->zones[static_cast<std::size_t>(zone)].sample;
    if (data == nullptr || !data->playable()) {
        ui::centredText(list, at, end, p.textFaint,
                        data != nullptr && data->failed.load() ? L("Fichier introuvable ou illisible", "File missing or unreadable")
                                                               : L("Chargement…", "Loading…"));
        ImGui::Dummy(size);
        return;
    }
    // Its peaks, per column, read once.
    const auto columns = std::max(16, static_cast<int>(size.x));
    if (peaks.sample != data.get() || peaks.columns != columns) {
        peaks.sample = data.get();
        peaks.columns = columns;
        peaks.low.assign(static_cast<std::size_t>(columns), 0.0f);
        peaks.high.assign(static_cast<std::size_t>(columns), 0.0f);
        // A long recording has only its head in memory: its peaks read from
        // the file, once.
        std::unique_ptr<juce::AudioFormatReader> reader;
        if (data->streamed) {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            reader.reset(formats.createReaderFor(juce::File(juce::String::fromUTF8(data->path.c_str()))));
        }
        for (int column = 0; column < columns && reader != nullptr; ++column) {
            const auto from = data->frames * column / columns;
            const auto to = std::max(from + 1, data->frames * (column + 1) / columns);
            juce::Range<float> levels[2];
            reader->readMaxLevels(from, to - from, levels, 1);
            peaks.low[static_cast<std::size_t>(column)] = levels[0].getStart();
            peaks.high[static_cast<std::size_t>(column)] = levels[0].getEnd();
        }
        for (int column = 0; column < columns && !data->streamed; ++column) {
            const auto from = data->frames * column / columns;
            const auto to = std::max(from + 1, data->frames * (column + 1) / columns);
            auto low = 0;
            auto high = 0;
            const auto step = std::max<std::int64_t>(1, (to - from) / 256);
            for (auto frame = from; frame < to; frame += step) {
                const auto value = static_cast<int>(data->pcm[static_cast<std::size_t>(frame) * static_cast<std::size_t>(data->channels)]);
                low = std::min(low, value);
                high = std::max(high, value);
            }
            peaks.low[static_cast<std::size_t>(column)] = static_cast<float>(low) / 32'767.0f;
            peaks.high[static_cast<std::size_t>(column)] = static_cast<float>(high) / 32'767.0f;
        }
    }
    const auto middle = (at.y + end.y) * 0.5f;
    const auto half = size.y * 0.46f;
    for (int column = 0; column < columns; ++column) {
        const auto x = at.x + size.x * static_cast<float>(column) / static_cast<float>(columns);
        list->AddLine(ImVec2(x, middle - peaks.high[static_cast<std::size_t>(column)] * half),
                      ImVec2(x, middle - peaks.low[static_cast<std::size_t>(column)] * half + 1.0f), p.audio);
    }
    // The markers: start, end, loop start and end.
    auto& item = zones->getReference(zone);
    const auto frames = static_cast<double>(data->frames);
    const auto xOf = [&](const double frame) { return at.x + static_cast<float>(frame / frames) * size.x; };
    const auto start = static_cast<double>(static_cast<juce::int64>(item.getProperty("start", 0)));
    auto finish = static_cast<double>(static_cast<juce::int64>(item.getProperty("end", -1)));
    if (finish < 0.0)
        finish = frames;
    const auto* loop = item.getProperty("loop", {}).getArray();
    const auto hasLoop = loop != nullptr && loop->size() == 2 && static_cast<juce::int64>((*loop)[0]) >= 0;
    const auto loopStart = hasLoop ? static_cast<double>(static_cast<juce::int64>((*loop)[0])) : -1.0;
    const auto loopEnd = hasLoop ? static_cast<double>(static_cast<juce::int64>((*loop)[1])) : -1.0;
    if (hasLoop)
        list->AddRectFilled(ImVec2(xOf(loopStart), at.y), ImVec2(xOf(loopEnd), end.y), (p.accent & 0x00ffffffu) | 0x30000000u);
    const std::array<double, 4> markers { start, finish, loopStart, loopEnd };
    const std::array<ImU32, 4> colours { p.play, p.record, p.accent, p.accent };
    for (std::size_t index = 0; index < markers.size(); ++index)
        if (markers[index] >= 0.0)
            list->AddLine(ImVec2(xOf(markers[index]), at.y), ImVec2(xOf(markers[index]), end.y), colours[index], dp(1.5f));
    ImGui::InvisibleButton("##wave", size);
    if (ImGui::IsItemActivated()) {
        // The nearest marker under the pointer, dragged.
        draggedMarker_ = -1;
        auto nearest = dp(8.0f);
        for (std::size_t index = 0; index < markers.size(); ++index)
            if (markers[index] >= 0.0 && std::abs(ImGui::GetIO().MousePos.x - xOf(markers[index])) < nearest) {
                nearest = std::abs(ImGui::GetIO().MousePos.x - xOf(markers[index]));
                draggedMarker_ = static_cast<int>(index);
            }
    }
    if (ImGui::IsItemActive() && draggedMarker_ >= 0) {
        const auto frame = static_cast<juce::int64>(std::clamp((ImGui::GetIO().MousePos.x - at.x) / size.x, 0.0f, 1.0f) * frames);
        switch (draggedMarker_) {
        case 0: setValue(item, "start", std::min<juce::int64>(frame, static_cast<juce::int64>(finish) - 1)); break;
        case 1: setValue(item, "end", std::max<juce::int64>(frame, static_cast<juce::int64>(start) + 1)); break;
        case 2: setPair(item, "loop", std::min<juce::int64>(frame, static_cast<juce::int64>(loopEnd) - 2), static_cast<juce::int64>(loopEnd)); break;
        case 3: setPair(item, "loop", static_cast<juce::int64>(loopStart), std::max<juce::int64>(frame, static_cast<juce::int64>(loopStart) + 2)); break;
        default: break;
        }
        editing = true;
        working = json;
    }
    ui::tooltip(L("Glissez les repères : début (vert), fin (rouge), boucle.", "Drag the markers: start (green), end (red), loop."));
    // The loop on or off, and its crossfade, under the waveform.
    ImGui::SetCursorScreenPos(ImVec2(at.x, ImGui::GetCursorScreenPos().y + (compact ? dp(2.0f) : 0.0f)));
    auto looped = hasLoop;
    if (ImGui::Checkbox(L("Boucle", "Loop"), &looped)) {
        auto edited = copyOf(json);
        auto& edit = zonesOf(edited)->getReference(zone);
        if (looped) {
            // The middle third to begin with, a crossfade of 50 ms.
            setPair(edit, "loop", static_cast<juce::int64>(frames * 0.4), static_cast<juce::int64>(frames * 0.8));
            setValue(edit, "loopCrossfade", static_cast<int>(data->sampleRate * 0.05));
        } else if (auto* object = edit.getDynamicObject()) {
            object->removeProperty("loop");
            object->removeProperty("loopCrossfade");
        }
        commit(target, edited);
    }
    if (hasLoop && !compact) {
        ImGui::SameLine();
        auto crossfade = static_cast<float>(static_cast<int>(item.getProperty("loopCrossfade", 0)) * 1'000.0 / data->sampleRate);
        ImGui::SetNextItemWidth(dp(120.0f));
        if (ImGui::DragFloat(ui::labelId(L("Fondu", "Crossfade"), "crossfade").c_str(), &crossfade, 0.5f, 0.0f, 500.0f, "%.0f ms")) {
            setValue(item, "loopCrossfade", static_cast<int>(crossfade * 0.001 * data->sampleRate));
            editing = true;
            working = json;
        }
    }
    ImGui::SameLine();
    if (compact)
        ImGui::TextColored(theme::vec(p.textDim), "%.2f s %s", frames / data->sampleRate,
                           data->channels > 1 ? L("stéréo", "stereo") : L("mono", "mono"));
    else
        ImGui::TextColored(theme::vec(p.textDim), "%.2f s, %d Hz, %s", frames / data->sampleRate, static_cast<int>(data->sampleRate),
                           data->channels > 1 ? L("stéréo", "stereo") : L("mono", "mono"));
}

void SamplerView::drawEditor()
{
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    windows_.each([this](const TrackRef& track, const std::uint64_t nodeId, const bool focus) {
        editor_ = Target { track, nodeId };
        windowId_ = windows_.idOf(track, nodeId);
        // This window's own: its zone in hand, its edit, its waveform.
        auto& kept = windowStates_[windowId_];
        selected_ = kept.selected;
        editing_ = kept.editing;
        working_ = kept.working;
        peaks_ = std::move(kept.peaks);
        showControls_ = kept.showControls;
        const auto open = drawEditorWindow(focus);
        kept = { selected_, editing_, working_, std::move(peaks_), showControls_ };
        if (!open)
            windowStates_.erase(windowId_);
        editor_.reset();
        return open;
    });
}

bool SamplerView::drawEditorWindow(const bool focus)
{
    const auto target = *editor_;
    auto* slot = studio_.graphFor(target.track);
    if (slot == nullptr || (!studio_.isValid(target.track) && !target.track.master)) {
        return false;
    }
    auto json = editing_ ? working_ : copyOf(currentMap(target));
    const auto& p = theme::palette();
    ui::beforeDetachable(windowId_.c_str());
    windows_.placeFirst(target.track, target.nodeId);
    if (!placeHere_.empty() && placeHere_ == windowId_) {
        placeHere_.clear();
        const auto* main = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(main->Pos.x + dp(60.0f), main->Pos.y + dp(70.0f)), ImGuiCond_Always);
        ImGui::SetNextWindowViewport(main->ID);
    }
    ImGui::SetNextWindowSize(ImVec2(dp(920.0f), dp(640.0f)), ImGuiCond_FirstUseEver);
    if (focus)
        ImGui::SetNextWindowFocus();
    auto open = true;
    const auto title = ui::Ls("Sampler — ", "Sampler — ") + studio_.trackName(target.track) + juce::String(windowId_);
    if (ImGui::Begin(title.toRawUTF8(), &open, ImGuiWindowFlags_NoCollapse)) {
        // Out of the studio's window, as a plug-in's (another screen), and back.
        {
            const auto headerY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - dp(24.0f));
            (void) ui::detachButton(windowId_.c_str(), ImVec2(dp(24.0f), dp(22.0f)));
            ImGui::SetCursorPosY(headerY);
        }

        // Its zones, and an SFZ's articulations and controls.
        const auto has = sampler_controls::count(slot->graph(), target.nodeId);
        if (ImGui::BeginTabBar("##samplerTabs")) {
            if (ImGui::BeginTabItem(L("ZONES", "ZONES"))) {
            // The tools.
            if (ImGui::Button(L("Ajouter des échantillons…", "Add samples…")))
                chooseFiles(target);
            ImGui::SameLine();
            if (ImGui::Button(L("Ouvrir un SFZ…", "Open an SFZ…")))
                chooseSfz(target);
            ui::tooltip(L("Remplace les zones par celles d'un instrument SFZ (ou d'une carte .cysampler).",
                          "Replaces the zones with an SFZ instrument's (or a .cysampler map's)."));
            ImGui::SameLine();
            if (ImGui::Button(L("Enregistrer en SFZ…", "Save as SFZ…")))
                exportSfz(target);
            ImGui::SameLine();
            if (ImGui::Button(L("Répartir les touches", "Spread the keys"))) {
                commit(target, app::withKeysSpread(json));
                report(L("Chaque touche va à la racine la plus proche", "Every key goes to the nearest root"));
            }
            ui::tooltip(L("Chaque touche à la zone dont la racine est la plus proche (les couches de vélocité gardées).",
                          "Every key to the zone whose root is nearest (the velocity layers kept)."));
            auto* zones = zonesOf(json);
            const auto count = zones != nullptr ? zones->size() : 0;
            ImGui::SameLine();
            ImGui::BeginDisabled(count <= 1 || selected_ < 0 || selected_ >= count);
            if (ImGui::Button(L("Retirer la zone", "Remove the zone"))) {
                auto edited = copyOf(json);
                zonesOf(edited)->remove(selected_);
                commit(target, edited);
                selected_ = std::max(0, selected_ - 1);
            }
            ImGui::EndDisabled();
            ImGui::TextColored(theme::vec(p.textDim), "%s", app::describeSamplerMap(json).toRawUTF8());

            // The map: a click selects.
            const auto mapAt = ImGui::GetCursorScreenPos();
            const ImVec2 mapSize(ImGui::GetContentRegionAvail().x, dp(150.0f));
            const auto clicked = drawMap(json, mapAt, mapSize, selected_, p.audio, true);
            if (clicked >= 0) {
                selected_ = clicked;
                peaks_ = {};
            }
            ImGui::SetCursorScreenPos(ImVec2(mapAt.x, mapAt.y + mapSize.y + dp(6.0f)));

            // The table, then the selected zone.
            const auto tableWidth = ImGui::GetContentRegionAvail().x * 0.5f;
            if (ImGui::BeginChild("##zoneTable", ImVec2(tableWidth, 0.0f))) {
                if (ImGui::BeginTable("##zones", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV
                                                          | ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn(L("Fichier", "File"), ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn(L("Racine", "Root"));
                    ImGui::TableSetupColumn(L("Touches", "Keys"));
                    ImGui::TableSetupColumn(L("Vél.", "Vel."));
                    ImGui::TableSetupColumn(L("Accord", "Tune"));
                    ImGui::TableSetupColumn(L("Boucle", "Loop"));
                    ImGui::TableHeadersRow();
                    for (int index = 0; index < count; ++index) {
                        const auto& zone = (*zones)[index];
                        const auto root = static_cast<int>(zone.getProperty("root", 60));
                        const auto keys = pairOf(zone, "keys", { root, root });
                        const auto velocities = pairOf(zone, "velocities", { 1, 127 });
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushID(index);
                        const auto name = juce::File(app::resolveSamplePath(zone.getProperty("file", {}).toString())).getFileName();
                        if (ImGui::Selectable(name.toRawUTF8(), index == selected_, ImGuiSelectableFlags_SpanAllColumns)) {
                            selected_ = index;
                            peaks_ = {};
                        }
                        ImGui::PopID();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(noteName(root).toRawUTF8());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s-%s", noteName(keys.first).toRawUTF8(), noteName(keys.second).toRawUTF8());
                        ImGui::TableNextColumn();
                        ImGui::Text("%d-%d", velocities.first, velocities.second);
                        ImGui::TableNextColumn();
                        ImGui::Text("%+.0f", static_cast<double>(zone.getProperty("tune", 0.0)));
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(zone.hasProperty("loop") ? L("oui", "yes") : "");
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::BeginChild("##zoneFields", ImVec2(0.0f, 0.0f))) {
                if (selected_ >= 0 && selected_ < count) {
                    ImGui::TextColored(theme::vec(p.textDim), "%s",
                                       app::resolveSamplePath((*zones)[selected_].getProperty("file", {}).toString())
                                           .getFullPathName()
                                           .toRawUTF8());
                    drawZoneFields(target, json, selected_);
                    ImGui::Spacing();
                    drawWaveform(target, json, selected_, ImVec2(ImGui::GetContentRegionAvail().x, dp(130.0f)), peaks_, working_,
                                 editing_, false);
                } else {
                    ImGui::TextDisabled("%s", L("Choisissez une zone.", "Choose a zone."));
                }
            }
            ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (has.controls > 0 || has.articulations > 0) {
                const auto label = ui::labelId(L("CONTRÔLES", "CONTROLS"), "samplerControlsTab");
                if (ImGui::BeginTabItem(label.c_str(), nullptr, showControls_ ? ImGuiTabItemFlags_SetSelected : 0)) {
                    if (ImGui::BeginChild("##samplerControls", ImVec2(0.0f, 0.0f))) {
                        sampler_controls::draw(studio_, state_, target.track, slot->graph(), target.nodeId, p.audio);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            showControls_ = false;
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    // A field or marker let go: the edit becomes the card's (one undo step).
    if (editing_ && !ImGui::IsAnyItemActive()) {
        editing_ = false;
        commit(target, working_);
        working_ = juce::var();
    }
    return open;
}

} // namespace cydaw::v2
