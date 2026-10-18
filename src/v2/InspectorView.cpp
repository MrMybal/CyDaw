#include "InspectorView.h"
#include "ControlTargets.h"

#include "RoutingUi.h"

#include "DeviceVisuals.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

const app::AudioTrackController::AudioClip* findAudioClip(const app::AudioTrackController& engine, const std::size_t index,
                                                         const std::uint64_t id)
{
    if (const auto* clips = engine.audioClipsView(index))
        for (const auto& clip : *clips)
            if (clip.id == id)
                return &clip;
    return nullptr;
}

const app::AudioTrackController::MidiClip* findMidiClip(const app::AudioTrackController& engine, const std::uint64_t trackId,
                                                       const std::uint64_t id)
{
    if (const auto* clips = engine.midiClipsView(trackId))
        for (const auto& clip : *clips)
            if (clip.id == id)
                return &clip;
    return nullptr;
}

// A label on the left, the control filling the rest of the row.
void row(const char* label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(theme::palette().textDim), "%s", label);
    ImGui::SameLine(dp(78.0f));
    ImGui::SetNextItemWidth(-1.0f);
}

} // namespace

InspectorView::InspectorView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

void InspectorView::draw()
{
    auto& engine = studio_.engine();
    const auto& p = theme::palette();

    // ---- A virtual input or output ------------------------------------------------------
    if (state_.selectedTrack && state_.selectedTrack->isBus())
        drawBus(*state_.selectedTrack);

    // ---- Master ---------------------------------------------------------------------
    if (state_.selectedTrack && state_.selectedTrack->isMaster()) {
        const auto track = *state_.selectedTrack;
        ui::sectionTitle("MASTER");
        auto mix = studio_.trackMix(track);
        auto changed = false;
        row(L("Volume", "Volume"));
        changed |= ImGui::SliderFloat("##masterGain", &mix.gainDb, -60.0f, 12.0f, "%.1f dB");
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            mix.gainDb = 0.0f;
            changed = true;
        }
        ImGui::Dummy(ImVec2(dp(74.0f), 0.0f));
        ImGui::SameLine();
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.85f);
        changed |= ui::toggle(L("MUET", "MUTE"), &mix.muted, p.mute);
        ImGui::PopFont();
        if (changed)
            studio_.setTrackMix(track, mix);
        ImGui::TextDisabled("%s", L("Les pistes mixées passent par ses périphériques, puis son volume.",
                                    "The tracks mixed go through its devices, then its volume."));
        ImGui::Spacing();
    }

    // ---- Track --------------------------------------------------------------------
    // (The master has its own section above; isValid alone would take it for
    // the first audio track.)
    if (state_.selectedTrack && !state_.selectedTrack->master && studio_.isValid(*state_.selectedTrack)) {
        const auto track = *state_.selectedTrack;
        ui::sectionTitle(track.midi                   ? L("PISTE INSTRUMENT", "INSTRUMENT TRACK")
                         : studio_.isBusTrack(track) ? L("PISTE BUS", "BUS TRACK")
                                                     : L("PISTE AUDIO", "AUDIO TRACK"));
        char name[96];
        std::snprintf(name, sizeof(name), "%s", studio_.trackName(track).toRawUTF8());
        row(L("Nom", "Name"));
        if (ImGui::InputText("##trackName", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue))
            studio_.setTrackName(track, juce::String::fromUTF8(name));
        row(L("Couleur", "Colour"));
        for (auto index = 0; index < 12; ++index) {
            ImGui::PushID(index);
            const auto colour = theme::trackColour(index);
            const auto selected = theme::toArgb(colour) == studio_.trackColour(track);
            if (ui::colourChip("##c", colour, ImVec2(dp(13.0f), dp(13.0f))))
                studio_.setTrackColour(track, theme::toArgb(colour));
            if (selected)
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), p.text, dp(2.0f), 1.5f);
            ImGui::PopID();
            if (index != 5 && index != 11)
                ImGui::SameLine(0.0f, dp(2.0f));
            if (index == 5) {
                ImGui::NewLine();
                ImGui::SameLine(dp(78.0f));
            }
        }

        auto mix = studio_.trackMix(track);
        auto changed = false;
        row(L("Volume", "Volume"));
        // A controller can move each of them (learn mode).
        const auto learn = [&](const char* what, const juce::String& label) {
            return control_targets::forTrackLearn(studio_, track, what, label);
        };
        changed |= ImGui::SliderFloat("##gain", &mix.gainDb, -60.0f, 12.0f, "%.1f dB");
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            mix.gainDb = 0.0f;
            changed = true;
        }
        if (const auto [key, name] = learn("volume", ui::Ls("Volume", "Volume")); !key.empty())
            ui::learnItem(key, name);
        row(L("Panoramique", "Pan"));
        changed |= ImGui::SliderFloat("##pan", &mix.pan, -1.0f, 1.0f, "%+.2f");
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            mix.pan = 0.0f;
            changed = true;
        }
        if (const auto [key, name] = learn("pan", ui::Ls("Panoramique", "Pan")); !key.empty())
            ui::learnItem(key, name);
        ImGui::Dummy(ImVec2(dp(74.0f), 0.0f));
        ImGui::SameLine();
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.85f);
        {
            const auto [key, name] = learn("mute", ui::Ls("Muet", "Mute"));
            const ui::LearnScope scope(key, name);
            changed |= ui::toggle(L("MUET", "MUTE"), &mix.muted, p.mute);
        }
        ImGui::SameLine(0.0f, dp(3.0f));
        {
            const auto [key, name] = learn("solo", "Solo");
            const ui::LearnScope scope(key, name);
            changed |= ui::toggle("SOLO", &mix.solo, p.solo);
        }
        ImGui::SameLine(0.0f, dp(3.0f));
        {
            const auto [key, name] = learn("arm", ui::Ls("Armer", "Arm"));
            const ui::LearnScope scope(key, name);
            changed |= ui::toggle(L("ARMER", "ARM"), &mix.armed, p.record);
        }
        ImGui::PopFont();
        if (changed)
            studio_.setTrackMix(track, mix);

        // Its format, what it records from, and each of its channels' level
        // (the track itself shows two sides only).
        juce::String formatError;
        row("Format");
        routing_ui::formatCombo(studio_, track, -1.0f, formatError);
        if (formatError.isNotEmpty()) {
            state_.status = formatError;
            state_.statusIsError = true;
            state_.statusTime = ImGui::GetTime();
        }
        if (!track.midi && !studio_.isBusTrack(track)) {
            row(L("Entrée", "Input"));
            routing_ui::recordInputCombo(studio_, track, -1.0f);
        }
        row(L("Canaux", "Channels"));
        routing_ui::channelMeters(studio_, track, ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 2.2f));
        if (!track.midi) {
            const auto plugins = engine.trackPlugins(track.audioIndex);
            if (!plugins.empty()) {
                row(L("Plugins", "Plugins"));
                ImGui::TextDisabled("%d", static_cast<int>(plugins.size()));
            }
        }
        if (auto* slot = studio_.graphFor(track)) {
            row(L("Graphe", "Graph"));
            const auto nodes = slot->graph().editorNodes();
            auto devices = 0;
            for (const auto& node : nodes)
                if (!node.boundary)
                    ++devices;
            ImGui::TextDisabled("%d %s", devices, devices > 1 ? L("périphériques", "devices") : L("périphérique", "device"));
            if (slot->lastCompileError().isNotEmpty())
                ImGui::TextColored(theme::vec(p.record), "%s", slot->lastCompileError().toRawUTF8());
        }

        // Where its sound goes, and what reaches it from other tracks.
        ImGui::Spacing();
        ui::sectionTitle(L("ROUTAGE", "ROUTING"));
        if (studio_.isBusTrack(track) || !studio_.trackSenders(track).empty())
            routing_ui::sendersLine(studio_, track, dp(78.0f));
        juce::String routingError;
        routing_ui::inputsEditor(studio_, track, dp(78.0f), routingError);
        ImGui::Spacing();
        routing_ui::outputsEditor(studio_, track, dp(78.0f), routingError);
        if (routingError.isNotEmpty()) {
            state_.status = routingError;
            state_.statusIsError = true;
            state_.statusTime = ImGui::GetTime();
        }
        ImGui::Spacing();
        if (!track.midi)
            drawModulation(track);
    }

    // ---- Clip ---------------------------------------------------------------------
    if (state_.selectedClips.size() == 1) {
        const auto ref = state_.selectedClips.front();
        ImGui::Separator();
        if (!ref.track.midi) {
            if (const auto* clip = findAudioClip(engine, ref.track.audioIndex, ref.clipId)) {
                ui::sectionTitle(L("CLIP AUDIO", "AUDIO CLIP"));
                ImGui::TextUnformatted((clip->name.isNotEmpty() ? clip->name : clip->sourceFile.getFileNameWithoutExtension()).toRawUTF8());
                auto start = static_cast<float>(clip->startSeconds);
                auto duration = static_cast<float>(clip->durationSeconds);
                auto offset = static_cast<float>(clip->sourceOffsetSeconds);
                auto fadeIn = static_cast<float>(clip->fadeInSeconds);
                auto fadeOut = static_cast<float>(clip->fadeOutSeconds);
                auto changed = false;
                row(L("Début", "Start"));
                changed |= ImGui::DragFloat("##start", &start, 0.005f, 0.0f, 36'000.0f, "%.3f s");
                row(L("Durée", "Length"));
                changed |= ImGui::DragFloat("##duration", &duration, 0.005f, 0.02f, 36'000.0f, "%.3f s");
                row(L("Décalage", "Offset"));
                changed |= ImGui::DragFloat("##offset", &offset, 0.005f, 0.0f, 36'000.0f, "%.3f s");
                row(L("Fondu ent.", "Fade in"));
                changed |= ImGui::DragFloat("##fadeIn", &fadeIn, 0.002f, 0.0f, duration * 0.5f, "%.3f s");
                row(L("Fondu sort.", "Fade out"));
                changed |= ImGui::DragFloat("##fadeOut", &fadeOut, 0.002f, 0.0f, duration * 0.5f, "%.3f s");
                if (changed) {
                    (void) engine.updateAudioClip(ref.track.audioIndex, ref.clipId, std::max(0.0f, start), std::max(0.0f, offset),
                                                  std::max(0.02f, duration), fadeIn, fadeOut);
                    studio_.markDirty();
                }
                // Stretch.
                auto tempo = static_cast<float>(clip->sourceTempoBpm);
                const char* modes[] = { L("Vitesse", "Repitch"), L("Tonal", "Tonal"), L("Rythme", "Beats"),
                                        L("Texture", "Texture"),  L("Brut", "Raw"),   L("Complexe", "Complex") };
                auto mode = static_cast<int>(clip->stretchMode);
                auto stretch = false;
                row(L("Tempo src.", "Source BPM"));
                stretch |= ImGui::DragFloat("##sourceTempo", &tempo, 0.1f, 0.0f, 300.0f, tempo > 0.0f ? "%.1f BPM" : L("non défini", "not set"));
                row(L("Étirement", "Stretch"));
                stretch |= ImGui::Combo("##mode", &mode, modes, app::AudioTrackController::stretchModeCount);
                if (stretch) {
                    (void) engine.setAudioClipStretchSettings(ref.track.audioIndex, ref.clipId, tempo,
                                                              static_cast<app::AudioTrackController::StretchMode>(mode));
                    studio_.markDirty();
                }
                ImGui::TextDisabled("%s", clip->sourceFile.getFullPathName().toRawUTF8());
            }
        } else if (const auto* clip = findMidiClip(engine, ref.track.midiId, ref.clipId)) {
            ui::sectionTitle(L("CLIP MIDI", "MIDI CLIP"));
            char name[96];
            std::snprintf(name, sizeof(name), "%s", clip->name.toRawUTF8());
            row(L("Nom", "Name"));
            if (ImGui::InputText("##clipName", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue))
                (void) engine.setMidiClipPresentation(ref.track.midiId, ref.clipId, juce::String::fromUTF8(name), 0);
            auto start = static_cast<float>(clip->startBeats);
            row(L("Début", "Start"));
            if (ImGui::DragFloat("##startBeats", &start, 0.05f, 0.0f, 100'000.0f, L("%.2f temps", "%.2f beats"))) {
                (void) engine.setMidiClipStartBeats(ref.track.midiId, ref.clipId, std::max(0.0f, start));
                studio_.markDirty();
            }
            auto length = static_cast<float>(clip->lengthBeats);
            row(L("Longueur", "Length"));
            if (ImGui::DragFloat("##lengthBeats", &length, 0.05f, 0.25f, 4'096.0f, L("%.2f temps", "%.2f beats"))) {
                const auto beats = std::max(0.25, static_cast<double>(length));
                const auto loopEnd = clip->loopEndBeats > 0.0 ? std::min(clip->loopEndBeats, beats) : beats;
                (void) engine.setMidiClipTimelineBounds(ref.track.midiId, ref.clipId, beats, std::min(clip->playStartBeats, beats - 0.25),
                                                        beats, std::min(clip->loopStartBeats, loopEnd - 0.25), loopEnd, clip->looping);
                studio_.markDirty();
            }
            auto looping = clip->looping;
            row(L("Boucle", "Loop"));
            if (ImGui::Checkbox("##looping", &looping)) {
                (void) engine.setMidiClipLooping(ref.track.midiId, ref.clipId, looping);
                studio_.markDirty();
            }
            row(L("Notes", "Notes"));
            ImGui::TextDisabled("%d", static_cast<int>(clip->notes.size()));
            if (ui::primaryButton(L("Ouvrir dans l'éditeur", "Open in the editor"), ImVec2(-1.0f, 0.0f))) {
                state_.editedClip = ref;
                state_.bringToFront = Panel::editor;
            }
        }
    } else if (state_.selectedClips.size() > 1) {
        ImGui::Separator();
        ImGui::TextDisabled("%d %s", static_cast<int>(state_.selectedClips.size()), L("clips sélectionnés", "clips selected"));
    } else if (state_.selectedSlots.size() == 1 && !state_.selectedSlots.front().track.midi && studio_.isValid(state_.selectedSlots.front().track)) {
        // An audio slot of the launcher: its clip.
        const auto slot = state_.selectedSlots.front();
        if (const auto clip = engine.launcherClip(slot.track.audioIndex, slot.scene)) {
            ImGui::Separator();
            ui::sectionTitle(L("CLIP AUDIO DU LANCEUR", "LAUNCHER AUDIO CLIP"));
            ImGui::TextUnformatted((clip->name.isNotEmpty() ? clip->name : clip->sourceFile.getFileNameWithoutExtension()).toRawUTF8());
            row(L("Scène", "Scene"));
            ImGui::TextDisabled("%d", slot.scene + 1);
            row(L("Durée", "Length"));
            ImGui::TextDisabled("%.3f s", clip->durationSeconds);
            row(L("Boucle", "Loop"));
            ImGui::TextDisabled("%s", clip->looping ? L("oui", "yes") : L("non", "no"));
            row(L("Sons", "Sounds"));
            ImGui::TextDisabled("%d", static_cast<int>(clip->usesEventTimeline ? clip->events.size() : 1));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", clip->sourceFile.getFullPathName().toRawUTF8());
            ImGui::PopTextWrapPos();
            if (ui::primaryButton(L("Ouvrir dans l'éditeur", "Open in the editor"), ImVec2(-1.0f, 0.0f))) {
                state_.editedClip = ClipRef { slot.track, 0, slot.scene };
                state_.bringToFront = Panel::editor;
            }
        }
    }

    // ---- Project --------------------------------------------------------------------
    ImGui::Separator();
    ui::sectionTitle(L("PROJET", "PROJECT"));
    auto tempo = static_cast<float>(engine.tempo());
    row(L("Tempo", "Tempo"));
    if (ImGui::DragFloat("##projectTempo", &tempo, 0.1f, 20.0f, 300.0f, "%.2f BPM")) {
        engine.setTempo(tempo);
        studio_.markDirty();
    }
    auto loop = studio_.loopEnabled();
    row(L("Boucle", "Loop"));
    if (ImGui::Checkbox("##projectLoop", &loop))
        studio_.setLoopEnabled(loop);
    if (engine.hasExplicitLoopRange()) {
        const auto spb = 60.0 / std::max(1.0, engine.tempo());
        auto loopStart = static_cast<float>(engine.loopStart() / spb);
        auto loopEnd = static_cast<float>(engine.loopEnd() / spb);
        row(L("De / à", "From / to"));
        ImGui::SetNextItemWidth((ImGui::GetContentRegionAvail().x - dp(4.0f)) * 0.5f);
        auto edited = ImGui::DragFloat("##loopStart", &loopStart, 0.25f, 0.0f, 100'000.0f, L("%.1f t", "%.1f b"));
        ImGui::SameLine(0.0f, dp(4.0f));
        ImGui::SetNextItemWidth(-1.0f);
        edited |= ImGui::DragFloat("##loopEnd", &loopEnd, 0.25f, 0.25f, 100'000.0f, L("%.1f t", "%.1f b"));
        if (edited && loopEnd > loopStart)
            studio_.setLoopRange(loopStart * spb, loopEnd * spb);
    }
    row(L("Pistes", "Tracks"));
    ImGui::TextDisabled("%d", static_cast<int>(studio_.trackCount()));
}

// A virtual input or output of the software: its name, format, level, its
// effects, and who uses it.
void InspectorView::drawBus(const TrackRef& busRef)
{
    const auto& p = theme::palette();
    auto bus = studio_.hardwareBus(busRef.bus);
    if (!bus)
        return;
    const auto input = bus->direction == app::VirtualBusDirection::input;
    ui::sectionTitle(input ? L("ENTRÉE VIRTUELLE", "VIRTUAL INPUT") : L("SORTIE VIRTUELLE", "VIRTUAL OUTPUT"));
    char name[96];
    std::snprintf(name, sizeof(name), "%s", bus->name.toRawUTF8());
    row(L("Nom", "Name"));
    if (ImGui::InputText("##busName", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue))
        studio_.setTrackName(busRef, juce::String::fromUTF8(name));
    row("Format");
    ImGui::TextDisabled("%s  (%s %s)", Studio::formatName(bus->channels).toRawUTF8(), L("carte", "card"),
                        routing_ui::deviceChannelsText(*bus).toRawUTF8());
    auto changed = false;
    row(L("Niveau", "Level"));
    changed |= ImGui::SliderFloat("##busGain", &bus->gainDb, -60.0f, 24.0f, "%.1f dB");
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        bus->gainDb = 0.0f;
        changed = true;
    }
    ImGui::Dummy(ImVec2(dp(74.0f), 0.0f));
    ImGui::SameLine();
    ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.85f);
    changed |= ui::toggle(L("MUET", "MUTE"), &bus->muted, p.mute);
    ImGui::SameLine(0.0f, dp(3.0f));
    if (input)
        changed |= ui::toggle(L("ÉCOUTE", "MONITOR"), &bus->monitor, p.accent, ImVec2(0.0f, 0.0f),
                              L("L'entendre (après ses effets) sur ses deux premières sorties.",
                                "Hear it (after its effects) on its first two outputs."));
    else
        changed |= ui::toggle("MASTER", &bus->carriesMaster, p.accent, ImVec2(0.0f, 0.0f),
                              L("Elle joue le master ; sinon seulement les pistes envoyées directement.",
                                "It plays the master; else only the tracks sent to it directly."));
    ImGui::PopFont();
    if (changed)
        if (const auto applied = studio_.updateHardwareBus(*bus); applied.failed()) {
            state_.status = applied.getErrorMessage();
            state_.statusIsError = true;
            state_.statusTime = ImGui::GetTime();
        }
    auto devices = 0;
    if (const auto* slot = studio_.graphFor(busRef))
        for (const auto& node : slot->graph().editorNodes())
            if (!node.boundary)
                ++devices;
    row(L("Effets", "Effects"));
    ImGui::TextDisabled("%d %s", devices, devices > 1 ? L("périphériques", "devices") : L("périphérique", "device"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", input ? L("Ses effets (Périphériques, Graphe) traitent le son avant qu'une piste le prenne ; chaque piste "
                                        "l'enregistre avant ou après eux.",
                                        "Its effects (Devices, Graph) treat the sound before a track takes it; each track records it "
                                        "before or after them.")
                                    : L("Ses effets (Périphériques, Graphe) traitent ce qui sort ; une piste envoyée peut passer "
                                        "avant ou après eux.",
                                        "Its effects (Devices, Graph) treat what leaves; a track sent to it goes before or after them."));
    ImGui::PopTextWrapPos();
    if (input) {
        juce::StringArray users;
        for (const auto& track : studio_.tracks())
            if (!track.midi && studio_.engine().trackMixState(track.audioIndex).inputBusId == bus->id)
                users.add(studio_.trackName(track)
                          + (studio_.engine().trackMixState(track.audioIndex).inputAfterEffects ? "" : L(" (avant)", " (before)")));
        row(L("Pistes", "Tracks"));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(users.isEmpty() ? L("aucune", "none") : users.joinIntoString(", ").toRawUTF8());
        ImGui::PopTextWrapPos();
    }
    if (ImGui::Button(L("Entrées et sorties de la carte\xe2\x80\xa6", "Card inputs and outputs\xe2\x80\xa6"), ImVec2(-1.0f, 0.0f)))
        state_.openSoundCard = true;
    ImGui::Spacing();
}

// The track's LFO and the parameters it moves: one row per parameter the
// graph publishes, a switch and an amount.
void InspectorView::drawModulation(const TrackRef& track)
{
    const auto& p = theme::palette();
    ImGui::Separator();
    ui::sectionTitle("MODULATION");
    // The track's LFO, drawn and edited with the device panel's display.
    auto lfo = studio_.modulationLfo(track);
    std::vector<visual::Param> parameters {
        { "lfo.rate", ui::Ls("Vitesse", "Rate"), "Hz", 0.01, 40.0, lfo.rate, 1.0 },
        { "lfo.depth", ui::Ls("Profondeur", "Depth"), {}, 0.0, 1.0, lfo.depth, 1.0 },
        { "lfo.offset", ui::Ls("Décalage", "Offset"), {}, -1.0, 1.0, lfo.offset, 0.0 },
        { "lfo.shape", ui::Ls("Forme", "Shape"), {}, 0.0, 3.0, static_cast<double>(lfo.shape), 0.0 },
    };
    const visual::Setter set = [this, &track, &lfo](const visual::Param& parameter, const double value) {
        if (parameter.binding == "lfo.rate")
            lfo.rate = value;
        else if (parameter.binding == "lfo.depth")
            lfo.depth = value;
        else if (parameter.binding == "lfo.offset")
            lfo.offset = value;
        else
            lfo.shape = static_cast<int>(std::lround(value));
        studio_.setModulationLfo(track, lfo);
    };
    visual::Live live;
    live.running = studio_.modulationActive(track);
    live.hasControl = live.running;
    live.control = static_cast<float>(juce::jlimit(-1.0, 1.0, studio_.modulationOutput(track)));
    const visual::Pane pane { visual::PaneKind::lfo, { 0, 1, 2, 3 }, 0 };
    const auto width = ImGui::GetContentRegionAvail().x;
    const auto height = std::max(dp(96.0f), std::min(width * 0.62f, dp(150.0f)));
    const auto origin = ImGui::GetCursorScreenPos();
    (void) visual::drawPane("##trackLfo", pane, parameters, origin, ImVec2(width, height), p.modulation, live, set);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, height + dp(4.0f)));
    // The whole value graph, as cards: several LFOs, macros, arithmetic, logic.
    if (ImGui::SmallButton(L("Éditer le graphe de modulation ›", "Edit the modulation graph ›"))) {
        state_.graphShowsModulation = true;
        state_.bringToFront = Panel::graph;
    }
    ui::tooltip(L("Dans l'éditeur de graphe : LFO, macros, calculs, logique, plages ; sa sortie module les paramètres cochés ici",
                  "In the graph editor: LFOs, macros, arithmetic, logic, ranges; its output modulates the parameters ticked here"));

    const auto targets = studio_.modulationTargets(track);
    if (targets.empty()) {
        ImGui::TextDisabled("%s", L("Le graphe ne publie aucun paramètre.", "The graph publishes no parameter."));
        return;
    }
    if (ImGui::BeginTable("##modTargets", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("target", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("amount", ImGuiTableColumnFlags_WidthFixed, dp(92.0f));
        for (std::size_t index = 0; index < targets.size(); ++index) {
            const auto& target = targets[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            auto enabled = target.enabled;
            if (ImGui::Checkbox(target.label.toRawUTF8(), &enabled))
                studio_.setModulationAssignment(track, target.binding, enabled, target.amount);
            ui::tooltip(target.binding.toRawUTF8());
            ImGui::TableNextColumn();
            auto amount = static_cast<float>(target.amount * 100.0);
            ImGui::SetNextItemWidth(-1.0f);
            if (!target.enabled)
                ImGui::BeginDisabled();
            if (ImGui::SliderFloat("##amount", &amount, -100.0f, 100.0f, "%+.0f%%"))
                studio_.setModulationAssignment(track, target.binding, target.enabled, amount / 100.0);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                studio_.setModulationAssignment(track, target.binding, target.enabled, 0.25);
            if (!target.enabled)
                ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

} // namespace cydaw::v2
