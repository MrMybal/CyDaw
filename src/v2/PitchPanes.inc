// The displays and large views of Harmonie, Justesse
// (PitchDesign.h computes what they draw). Included by StudioEffectsView.cpp
// inside its anonymous namespace: it has its helpers (knobCell, segmented,
// chooser, comboCell, switchCell, frame, title, section, text, fillUnder…)
// and addedLarge(), the common window layout.

namespace pitchfxd = engine::pitchfx;

// The two voices' colours, on the ruler, the card and their knobs.
constexpr ImU32 kHarmonieVoiceA = IM_COL32(255, 190, 92, 255);
constexpr ImU32 kHarmonieVoiceB = IM_COL32(118, 188, 255, 255);

// A note's name ("A3", "La3"); empty for none.
juce::String pitchNoteText(const int note)
{
    if (note < 0 || note > 127)
        return {};
    const auto& spelled = ui::french() ? pitchfxd::pitchNoteNamesFrench : pitchfxd::pitchNoteNames;
    return juce::String::fromUTF8(spelled[static_cast<std::size_t>(pitchfxd::pitchClassOf(note))]) + juce::String(note / 12 - 1);
}

// A whole number with its sign and unit: "+12 st", "-5 ct", "0 st".
juce::String pitchSignedText(const double value, const char* unit)
{
    const auto rounded = juce::roundToInt(value);
    return (rounded > 0 ? juce::String("+") : juce::String()) + juce::String(rounded) + unit;
}

juce::String harmonieModeName(const int index)
{
    const auto& mode = pitchfxd::harmonieModes[static_cast<std::size_t>(std::clamp(index, 0, pitchfxd::harmonieModeCount - 1))];
    return juce::String::fromUTF8(ui::french() ? mode.french : mode.english);
}
juce::String harmonieModeHint(const int index)
{
    const auto& mode = pitchfxd::harmonieModes[static_cast<std::size_t>(std::clamp(index, 0, pitchfxd::harmonieModeCount - 1))];
    return juce::String::fromUTF8(ui::french() ? mode.frenchHint : mode.englishHint);
}
juce::String harmonieIntervalName(const double semitones)
{
    const auto& interval = pitchfxd::harmonieIntervalOf(semitones);
    return juce::String::fromUTF8(ui::french() ? interval.french : interval.english);
}

juce::String justesseKeyName(const int index)
{
    const auto& spelled = ui::french() ? pitchfxd::pitchNoteNamesFrench : pitchfxd::pitchNoteNames;
    return juce::String::fromUTF8(spelled[static_cast<std::size_t>(std::clamp(index, 0, 11))]);
}
juce::String justesseKeyHint(const int index)
{
    return ui::Ls("La tonique de la gamme : ", "The scale's root: ") + justesseKeyName(index);
}
juce::String justesseScaleName(const int index)
{
    const auto& scale = pitchfxd::justesseScales[static_cast<std::size_t>(std::clamp(index, 0, pitchfxd::justesseScaleCount - 1))];
    return juce::String::fromUTF8(ui::french() ? scale.french : scale.english);
}
juce::String justesseScaleHint(const int index)
{
    const auto& scale = pitchfxd::justesseScales[static_cast<std::size_t>(std::clamp(index, 0, pitchfxd::justesseScaleCount - 1))];
    return juce::String::fromUTF8(ui::french() ? scale.frenchHint : scale.englishHint);
}

// ---- Harmonie -------------------------------------------------------------------------------------

// A semitone ruler from two octaves down to two octaves up, the sound at 0
// (its note when the voice mode hears one), each voice a bar at its
// interval: as thick as its level, as far left or right as its pan. On a
// card, the two intervals big beside it. Drag a bar: up and down its
// interval, sideways its place.
bool harmonieDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                     const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<pitchfxd::harmonieParameterCount>(parameters, pane, "harmonie");
    const auto value = [&values](const int index) { return values[static_cast<std::size_t>(index)]; };
    const auto modeIndex = whole(value(pitchfxd::harmonieMode), pitchfxd::harmonieModeCount);
    const auto heardHz = live.running && live.readings != nullptr && live.readingCount > pitchfxd::harmonieReadHz
                             ? live.readings[pitchfxd::harmonieReadHz]
                             : 0.0f;
    struct Voice {
        int shift;
        int fine;
        int level;
        int pan;
        ImU32 tint;
        const char* name;
    };
    const std::array<Voice, 2> voices { { { pitchfxd::harmonieShiftA, pitchfxd::harmonieFineA, pitchfxd::harmonieLevelA,
                                            pitchfxd::harmoniePanA, kHarmonieVoiceA, "A" },
                                          { pitchfxd::harmonieShiftB, pitchfxd::harmonieFineB, pitchfxd::harmonieLevelB,
                                            pitchfxd::harmoniePanB, kHarmonieVoiceB, "B" } } };
    const auto semitonesOf = [&](const Voice& voice) { return pitchfxd::harmonieSemitonesOf(value(voice.shift), value(voice.fine)); };
    const Box area { ImVec2(box.min.x + dp(4.0f), box.min.y + dp(large ? 26.0f : 18.0f)), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    Box ruler = area;
    if (large) {
        ruler.min.x += dp(40.0f);
    } else {
        // The card: the two intervals big on the left, their names under them.
        ruler.min.x = area.min.x + area.width() * 0.44f;
        const auto big = 1.15f;
        auto x = area.min.x + dp(4.0f);
        const auto y = area.min.y + dp(2.0f);
        for (const auto& voice : voices) {
            const auto shown = pitchSignedText(value(voice.shift), "");
            const auto tint = value(voice.level) > 0.005f ? voice.tint : theme::alpha(voice.tint, 0.35f);
            text(list, ImVec2(x, y), tint, shown.toRawUTF8(), big);
            x += textSize(shown.toRawUTF8(), big).x + dp(12.0f);
        }
        auto line = y + textSize("+12", big).y + dp(2.0f);
        for (const auto& voice : voices) {
            const auto tint = value(voice.level) > 0.005f ? theme::alpha(voice.tint, 0.85f) : theme::alpha(voice.tint, 0.35f);
            const auto name = juce::String(voice.name) + "  " + harmonieIntervalName(semitonesOf(voice));
            text(list, ImVec2(area.min.x + dp(4.0f), line), tint, name.toRawUTF8(), 0.56f);
            line += ImGui::GetFontSize() * 0.62f;
        }
        if (value(pitchfxd::harmonieDry) > 0.005f) {
            const auto dry = ui::Ls("Sec ", "Dry ") + percent(value(pitchfxd::harmonieDry));
            text(list, ImVec2(area.min.x + dp(4.0f), line), p.textFaint, dry.toRawUTF8(), 0.54f);
        }
    }
    list->AddRectFilled(ruler.min, ruler.max, theme::alpha(kDark, 0.45f), dp(3.0f));
    const auto margin = dp(large ? 8.0f : 4.0f);
    const auto yOf = [&ruler, margin](const double semitones) {
        const auto t = static_cast<float>((std::clamp(semitones, -24.0, 24.0) + 24.0) / 48.0);
        return ruler.max.y - margin - t * (ruler.height() - 2.0f * margin);
    };
    const auto padX = ruler.width() * 0.12f;
    const auto xOfPan = [&ruler, padX](const double pan) {
        return ruler.min.x + padX + static_cast<float>((std::clamp(pan, -1.0, 1.0) + 1.0) * 0.5) * (ruler.width() - 2.0f * padX);
    };
    // The ruler: octaves across, the fifths and fourths faint, a tick a semitone.
    for (int step = -24; step <= 24; ++step) {
        const auto y = yOf(step);
        const auto degree = std::abs(step) % 12;
        const auto octave = degree == 0;
        if (octave)
            list->AddLine(ImVec2(ruler.min.x, y), ImVec2(ruler.max.x, y), p.gridStrong);
        else if (large && (degree == 7 || degree == 5))
            list->AddLine(ImVec2(ruler.min.x, y), ImVec2(ruler.max.x, y), theme::alpha(p.grid, 0.6f));
        list->AddLine(ImVec2(ruler.min.x, y), ImVec2(ruler.min.x + dp(octave ? 7.0f : 3.0f), y), p.textFaint);
        if (large && (octave || degree == 7)) {
            const auto mark = pitchSignedText(step, "");
            const auto size = textSize(mark.toRawUTF8(), 0.58f);
            text(list, ImVec2(ruler.min.x - size.x - dp(5.0f), y - size.y * 0.5f), octave ? p.textDim : p.textFaint, mark.toRawUTF8(), 0.58f);
        }
    }
    if (large) {
        dashedLine(list, ImVec2(xOfPan(0.0), ruler.min.y), ImVec2(xOfPan(0.0), ruler.max.y), theme::alpha(p.textFaint, 0.5f));
        const auto bottom = ruler.max.y - ImGui::GetFontSize() * 0.62f - dp(2.0f);
        text(list, ImVec2(xOfPan(-1.0) - dp(3.0f), bottom), p.textFaint, L("G", "L"), 0.56f);
        text(list, ImVec2(xOfPan(1.0) - dp(3.0f), bottom), p.textFaint, L("D", "R"), 0.56f);
    }
    // The sound itself at 0, its note when the voice mode follows it.
    const auto zero = yOf(0.0);
    list->AddLine(ImVec2(ruler.min.x, zero), ImVec2(ruler.max.x, zero), theme::alpha(p.text, 0.75f), dp(large ? 2.0f : 1.5f));
    juce::String heard;
    if (heardHz > 0.0f)
        heard = pitchNoteText(static_cast<int>(std::lround(pitchfxd::pitchNoteOfHz(heardHz))))
                + (large ? "  " + juce::String(juce::roundToInt(heardHz)) + " Hz" : juce::String());
    else if (large)
        heard = modeIndex == pitchfxd::harmonieModeVoice ? ui::Ls("Entrée (pas de hauteur)", "Input (no pitch)") : ui::Ls("Entrée", "Input");
    if (heard.isNotEmpty()) {
        const auto scale = large ? 0.62f : 0.52f;
        text(list, ImVec2(ruler.min.x + dp(9.0f), zero - textSize(heard.toRawUTF8(), scale).y - dp(1.0f)), p.textDim, heard.toRawUTF8(), scale);
    }
    // The voices: a line from the sound to each, its bar, its name.
    std::array<ImVec2, 2> places {};
    for (std::size_t index = 0; index < voices.size(); ++index) {
        const auto& voice = voices[index];
        const auto semitones = semitonesOf(voice);
        const auto level = std::clamp(value(voice.level), 0.0f, 1.0f);
        const auto x = xOfPan(value(voice.pan));
        const auto y = yOf(semitones);
        places[index] = ImVec2(x, y);
        const auto silent = level < 0.005f;
        const auto tint = silent ? theme::alpha(voice.tint, 0.4f) : voice.tint;
        dashedLine(list, ImVec2(x, zero), ImVec2(x, y), theme::alpha(voice.tint, 0.45f));
        const auto halfWidth = dp(large ? 22.0f : 12.0f);
        if (silent) {
            list->AddRect(ImVec2(x - halfWidth, y - dp(2.0f)), ImVec2(x + halfWidth, y + dp(2.0f)), tint, dp(2.0f), dp(1.0f));
        } else {
            const auto halfHeight = dp(1.0f) + level * dp(large ? 5.0f : 3.5f);
            list->AddRectFilled(ImVec2(x - halfWidth, y - halfHeight), ImVec2(x + halfWidth, y + halfHeight), tint, halfHeight);
        }
        if (large) {
            auto label = juce::String(voice.name) + "  " + pitchSignedText(value(voice.shift), " st");
            if (std::abs(value(voice.fine)) >= 0.5f)
                label += " " + pitchSignedText(value(voice.fine), " ct");
            label += "  " + harmonieIntervalName(semitones);
            const auto size = textSize(label.toRawUTF8(), 0.66f);
            const auto lx = value(voice.pan) > 0.25f ? x - halfWidth - dp(6.0f) - size.x : x + halfWidth + dp(6.0f);
            text(list, ImVec2(lx, y - size.y * 0.5f), tint, label.toRawUTF8(), 0.66f);
        } else {
            centred(list, ImVec2(x + halfWidth + dp(6.0f), y), tint, voice.name, 0.5f);
        }
    }
    // The echo after the voices.
    if (large && value(pitchfxd::harmonieDelay) >= 0.5f) {
        const auto echo = ui::Ls("Écho ", "Echo ") + juce::String(juce::roundToInt(value(pitchfxd::harmonieDelay))) + " ms  "
                          + ui::Ls("retour ", "feedback ") + percent(value(pitchfxd::harmonieFeedback));
        text(list, ImVec2(xOfPan(-1.0) + dp(16.0f), ruler.max.y - ImGui::GetFontSize() * 0.62f - dp(2.0f)), p.textFaint, echo.toRawUTF8(),
             0.58f);
    }
    // Dragging a voice: the nearer bar to where the press began.
    auto changed = false;
    auto& drag = dragState(parameters, pane);
    ImGui::SetCursorScreenPos(ruler.min);
    ImGui::InvisibleButton("##harmonieRuler", ImVec2(std::max(1.0f, ruler.width()), std::max(1.0f, ruler.height())));
    const auto mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActivated()) {
        drag.active = -1;
        auto nearest = dp(large ? 40.0f : 28.0f);
        for (std::size_t index = 0; index < places.size(); ++index)
            if (const auto distance = std::hypot(mouse.x - places[index].x, mouse.y - places[index].y); distance < nearest) {
                nearest = distance;
                drag.active = static_cast<int>(index);
            }
    }
    if (ImGui::IsItemActive() && drag.active >= 0 && drag.active < 2) {
        const auto& voice = voices[static_cast<std::size_t>(drag.active)];
        const auto t = std::clamp((ruler.max.y - margin - mouse.y) / std::max(1.0f, ruler.height() - 2.0f * margin), 0.0f, 1.0f);
        changed |= apply(at(parameters, pane, voice.shift), std::round(-24.0 + 48.0 * static_cast<double>(t)), set);
        const auto pan = std::clamp((mouse.x - ruler.min.x - padX) / std::max(1.0f, ruler.width() - 2.0f * padX) * 2.0f - 1.0f, -1.0f, 1.0f);
        changed |= apply(at(parameters, pane, voice.pan), std::abs(pan) < 0.04f ? 0.0 : static_cast<double>(pan), set);
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        drag.active = -1;
    }
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", L("Glissez une voix : de haut en bas son intervalle, de côté sa place.",
                                  "Drag a voice: up and down its interval, sideways its place."));
    // Its mode, to click.
    const auto chooserScale = large ? 0.82f : 0.66f;
    changed |= chooser("##harmonieMode", at(parameters, pane, pitchfxd::harmonieMode), pitchfxd::harmonieModeCount, harmonieModeName,
                       harmonieModeHint,
                       ImVec2(box.max.x - textSize((harmonieModeName(modeIndex) + "  ").toRawUTF8(), chooserScale).x - dp(16.0f),
                              box.min.y + dp(2.0f)),
                       colour, set, chooserScale);
    return changed;
}

// The window: the ruler large, then the mode and every knob, each voice's in
// its colour.
bool harmonieLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                   const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto diameter = 36.0f;
    struct Item {
        int index;
        const char* label;
        ImU32 tint;
    };
    const std::array<Item, 13> knobs { { { pitchfxd::harmonieShiftA, L("INTERVALLE A", "INTERVAL A"), kHarmonieVoiceA },
                                         { pitchfxd::harmonieFineA, L("FIN A", "FINE A"), kHarmonieVoiceA },
                                         { pitchfxd::harmonieLevelA, L("NIVEAU A", "LEVEL A"), kHarmonieVoiceA },
                                         { pitchfxd::harmoniePanA, L("PLACE A", "PAN A"), kHarmonieVoiceA },
                                         { pitchfxd::harmonieShiftB, L("INTERVALLE B", "INTERVAL B"), kHarmonieVoiceB },
                                         { pitchfxd::harmonieFineB, L("FIN B", "FINE B"), kHarmonieVoiceB },
                                         { pitchfxd::harmonieLevelB, L("NIVEAU B", "LEVEL B"), kHarmonieVoiceB },
                                         { pitchfxd::harmoniePanB, L("PLACE B", "PAN B"), kHarmonieVoiceB },
                                         { pitchfxd::harmonieDry, L("SEC", "DRY"), colour },
                                         { pitchfxd::harmonieDelay, L("ÉCHO", "DELAY"), colour },
                                         { pitchfxd::harmonieFeedback, L("RETOUR", "FEEDBACK"), colour },
                                         { pitchfxd::harmonieWindow, L("FENÊTRE", "WINDOW"), colour },
                                         { pitchfxd::harmonieOutput, L("SORTIE", "OUTPUT"), colour } } };
    const auto columns = knobColumns(size.x - dp(16.0f), knobs.size(), dp(80.0f));
    const auto rows = static_cast<float>((knobs.size() + columns - 1) / columns);
    const auto panelHeight = dp(36.0f) + knobHeight(diameter) * rows + dp(16.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(190.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= harmonieDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("HARMONIE  ·  deux voix à leurs intervalles", "HARMONIE  ·  two voices at their intervals"));
    list->PopClipRect();

    auto y = outer.max.y + dp(8.0f);
    const Box plate { ImVec2(position.x, y), ImVec2(position.x + size.x, position.y + size.y) };
    list->AddRectFilled(plate.min, plate.max, theme::mix(colour, p.bg1, 0.9f), dp(6.0f));
    list->AddRect(plate.min, plate.max, theme::alpha(colour, 0.5f), dp(6.0f));
    y += dp(6.0f);
    section(list, ImVec2(position.x + dp(10.0f), y + dp(4.0f)), L("MODE", "MODE"));
    const auto& modes = pitchfxd::harmonieModes;
    changed |= segmented("##harmonieModes", at(parameters, pane, pitchfxd::harmonieMode),
                         { L(modes[0].french, modes[0].english), L(modes[1].french, modes[1].english), L(modes[2].french, modes[2].english) },
                         ImVec2(position.x + dp(80.0f), y), std::min(dp(330.0f), size.x - dp(90.0f)), dp(22.0f), colour, set,
                         L("Lisse : des grains longs, pour les accords. Voix : suit la voix et garde son timbre. Percussif : des grains "
                           "courts qui gardent les attaques.",
                           "Smooth: long grains, for chords. Voice: follows the voice and keeps its timbre. Percussive: short grains that "
                           "keep the attacks."));
    y += dp(30.0f);
    const auto cell = std::min(dp(100.0f), (size.x - dp(16.0f)) / static_cast<float>(columns));
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(8.0f) + cell * static_cast<float>(index % columns),
                                   y + knobHeight(diameter) * static_cast<float>(index / columns)),
                            cell, at(parameters, pane, knobs[index].index), knobs[index].label, knobs[index].tint, live, set, diameter);
    return changed;
}

// ---- Justesse -------------------------------------------------------------------------------------

// A strip of a piano roll, seven semitones each side of the note sung: the
// scale's notes lit (its root brighter), the target framed, the pitch heard
// over the last 0.6 s as a thin line and the pitch made of it as a thick
// one; the note and how far the voice is from it. The key and the scale to
// click at the top.
bool justesseDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                     const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<pitchfxd::justesseParameterCount>(parameters, pane, "justesse");
    const auto key = pitchfxd::justesseKeyOf(values[pitchfxd::justesseKey]);
    const auto& scale = pitchfxd::justesseScaleOf(values[pitchfxd::justesseScale]);
    const auto scaleIndex = whole(values[pitchfxd::justesseScale], pitchfxd::justesseScaleCount);
    const auto has = live.running && live.readings != nullptr && live.readingCount >= pitchfxd::justesseReadingCount;
    const auto hz = has ? live.readings[pitchfxd::justesseReadHz] : 0.0f;
    const auto target = has ? static_cast<int>(std::lround(live.readings[pitchfxd::justesseReadTarget])) : -1;
    const auto cents = has ? live.readings[pitchfxd::justesseReadCents] : 0.0f;
    const auto detected = hz > 0.0f ? pitchfxd::pitchNoteOfHz(hz) : -1.0;
    // The rows stay put while the voice stays within a few notes of their
    // middle (kept with the display's state).
    auto& view = dragState(parameters, pane);
    const auto current = target >= 0 ? target : detected >= 0.0 ? static_cast<int>(std::lround(detected)) : -1;
    if (view.selected <= 0)
        view.selected = current > 0 ? current : 60;
    if (current > 0 && std::abs(current - view.selected) > 4)
        view.selected = current;
    const auto middle = view.selected;
    constexpr int span = 7;
    const Box strip { ImVec2(box.min.x + dp(large ? 40.0f : 4.0f), box.min.y + dp(large ? 26.0f : 18.0f)),
                      ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    const auto rowHeight = strip.height() / static_cast<float>(2 * span + 1);
    const auto yOf = [&strip, rowHeight, middle](const double note) {
        return strip.max.y - static_cast<float>(note - (static_cast<double>(middle - span) - 0.5)) * rowHeight;
    };
    for (int note = middle - span; note <= middle + span; ++note) {
        const auto top = yOf(note + 0.5);
        const auto bottom = yOf(note - 0.5);
        const auto allowed = pitchfxd::justesseAllowsOf(note, key, scale);
        const auto root = pitchfxd::pitchClassOf(note - key) == 0;
        const auto fill = !allowed ? theme::alpha(kDark, 0.55f) : root ? theme::alpha(colour, 0.24f) : theme::alpha(colour, 0.1f);
        list->AddRectFilled(ImVec2(strip.min.x, top), ImVec2(strip.max.x, bottom), fill);
        list->AddLine(ImVec2(strip.min.x, bottom), ImVec2(strip.max.x, bottom), theme::alpha(p.grid, 0.6f));
        if (note == target)
            list->AddRect(ImVec2(strip.min.x, top), ImVec2(strip.max.x, bottom), theme::alpha(colour, 0.9f), 0.0f, dp(1.5f));
        if (large) {
            const auto name = pitchNoteText(note);
            const auto scaleText = std::min(0.6f, rowHeight / ImGui::GetFontSize() * 0.95f);
            const auto size = textSize(name.toRawUTF8(), scaleText);
            text(list, ImVec2(strip.min.x - size.x - dp(5.0f), (top + bottom - size.y) * 0.5f), allowed ? p.textDim : p.textFaint,
                 name.toRawUTF8(), scaleText);
        }
    }
    // The last 0.6 s: what was sung (thin), what came out (thick).
    const auto trailEnd = strip.max.x - dp(large ? 90.0f : 40.0f);
    if (has) {
        const auto count = pitchfxd::justesseTrailLength;
        const auto step = (trailEnd - strip.min.x) / static_cast<float>(count - 1);
        for (const auto corrected : { false, true }) {
            const auto from = corrected ? pitchfxd::justesseReadCorrectedTrail : pitchfxd::justesseReadDetectedTrail;
            const auto tint = corrected ? colour : theme::alpha(p.text, 0.55f);
            const auto thickness = dp(corrected ? (large ? 3.0f : 2.2f) : 1.2f);
            std::vector<ImVec2> points;
            const auto flush = [&] {
                if (points.size() >= 2)
                    list->AddPolyline(points.data(), static_cast<int>(points.size()), tint, thickness);
                points.clear();
            };
            for (int index = 0; index < count; ++index) {
                const auto note = static_cast<double>(live.readings[from + index]);
                if (note < 0.0 || std::abs(note - middle) > span + 0.5) {
                    flush();
                    continue;
                }
                points.emplace_back(strip.min.x + step * static_cast<float>(index), yOf(note));
            }
            flush();
        }
        if (detected >= 0.0 && std::abs(detected - middle) <= span + 0.5) {
            list->AddCircleFilled(ImVec2(trailEnd, yOf(detected)), dp(2.5f), theme::alpha(p.text, 0.7f));
            const auto out = detected + static_cast<double>(cents) / 100.0;
            if (std::abs(out - middle) <= span + 0.5)
                list->AddCircleFilled(ImVec2(trailEnd, yOf(out)), dp(large ? 5.0f : 3.5f), colour);
        }
    }
    // The note, how far the voice is from it, the correction made.
    char label[64];
    if (target >= 0 && detected >= 0.0)
        std::snprintf(label, sizeof(label), "%s  %+d ct", pitchNoteText(target).toRawUTF8(),
                      static_cast<int>(std::lround((detected - target) * 100.0)));
    else
        std::snprintf(label, sizeof(label), "%s", "--");
    const auto labelScale = large ? 0.86f : 0.62f;
    const auto labelSize = textSize(label, labelScale);
    text(list, ImVec2(strip.max.x - labelSize.x - dp(5.0f), strip.min.y + dp(3.0f)), p.text, label, labelScale);
    if (large && target >= 0 && detected >= 0.0) {
        std::snprintf(label, sizeof(label), "%s %+d ct", L("correction", "correction"), static_cast<int>(std::lround(cents)));
        const auto size = textSize(label, 0.62f);
        text(list, ImVec2(strip.max.x - size.x - dp(5.0f), strip.min.y + labelSize.y + dp(5.0f)), colour, label, 0.62f);
    }
    // The key and the scale, to click.
    const auto chooserScale = large ? 0.82f : 0.62f;
    const auto scaleX = box.max.x - textSize((justesseScaleName(scaleIndex) + "  ").toRawUTF8(), chooserScale).x - dp(16.0f);
    const auto keyX = scaleX - textSize((justesseKeyName(key) + "  ").toRawUTF8(), chooserScale).x - dp(22.0f);
    auto changed = chooser("##justesseKey", at(parameters, pane, pitchfxd::justesseKey), 12, justesseKeyName, justesseKeyHint,
                           ImVec2(keyX, box.min.y + dp(2.0f)), colour, set, chooserScale);
    changed |= chooser("##justesseScale", at(parameters, pane, pitchfxd::justesseScale), pitchfxd::justesseScaleCount, justesseScaleName,
                       justesseScaleHint, ImVec2(scaleX, box.min.y + dp(2.0f)), colour, set, chooserScale);
    return changed;
}

// The window: the strip large, the key and the scale, every knob.
bool justesseLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                   const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto diameter = 38.0f;
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, 9> knobs { { { pitchfxd::justesseSpeed, L("VITESSE", "SPEED") },
                                        { pitchfxd::justesseAmount, L("QUANTITÉ", "AMOUNT") },
                                        { pitchfxd::justesseHumanize, L("HUMANISER", "HUMANIZE") },
                                        { pitchfxd::justesseTranspose, L("TRANSPOSER", "TRANSPOSE") },
                                        { pitchfxd::justesseLowPitch, L("PLUS GRAVE", "LOWEST") },
                                        { pitchfxd::justesseHighPitch, L("PLUS AIGU", "HIGHEST") },
                                        { pitchfxd::justesseSensitivity, L("SENSIBILITÉ", "SENSITIVITY") },
                                        { pitchfxd::justesseMix, L("MÉLANGE", "MIX") },
                                        { pitchfxd::justesseOutput, L("SORTIE", "OUTPUT") } } };
    const auto columns = knobColumns(size.x - dp(16.0f), knobs.size(), dp(80.0f));
    const auto rows = static_cast<float>((knobs.size() + columns - 1) / columns);
    const auto panelHeight = dp(36.0f) + knobHeight(diameter) * rows + dp(16.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(190.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= justesseDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("JUSTESSE  ·  la voix ramenée sur ses notes", "JUSTESSE  ·  the voice brought onto its notes"));
    list->PopClipRect();

    auto y = outer.max.y + dp(8.0f);
    const Box plate { ImVec2(position.x, y), ImVec2(position.x + size.x, position.y + size.y) };
    list->AddRectFilled(plate.min, plate.max, theme::mix(colour, p.bg1, 0.9f), dp(6.0f));
    list->AddRect(plate.min, plate.max, theme::alpha(colour, 0.5f), dp(6.0f));
    y += dp(6.0f);
    section(list, ImVec2(position.x + dp(10.0f), y + dp(4.0f)), L("TONIQUE", "KEY"));
    changed |= comboCell("##justesseKeys", at(parameters, pane, pitchfxd::justesseKey), names(12, justesseKeyName),
                         ImVec2(position.x + dp(80.0f), y), dp(90.0f), set);
    section(list, ImVec2(position.x + dp(190.0f), y + dp(4.0f)), L("GAMME", "SCALE"));
    changed |= comboCell("##justesseScales", at(parameters, pane, pitchfxd::justesseScale),
                         names(pitchfxd::justesseScaleCount, justesseScaleName), ImVec2(position.x + dp(250.0f), y), dp(180.0f), set);
    y += dp(30.0f);
    const auto cell = std::min(dp(100.0f), (size.x - dp(16.0f)) / static_cast<float>(columns));
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(8.0f) + cell * static_cast<float>(index % columns),
                                   y + knobHeight(diameter) * static_cast<float>(index / columns)),
                            cell, at(parameters, pane, knobs[index].index), knobs[index].label, colour, live, set, diameter);
    return changed;
}

// ---- The five entry points --------------------------------------------------------------------

// A card's display (large: the window's), true when a value changed.
bool pitchDisplay(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour,
                    const Live& live, const Setter& set, const bool large)
{
    if (kind == PaneKind::harmonie)
        return harmonieDisplay(parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::justesse)
        return justesseDisplay(parameters, pane, box, colour, live, set, large);
    return false;
}

// The window: its display large and every control.
bool pitchLarge(const PaneKind kind, const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size,
                  const ImU32 colour, const Live& live, const Setter& set)
{
    if (kind == PaneKind::harmonie)
        return harmonieLarge(pane, parameters, position, size, colour, live, set);
    if (kind == PaneKind::justesse)
        return justesseLarge(pane, parameters, position, size, colour, live, set);
    return false;
}

// The values a card shows as knobs (pane indices).
std::vector<int> pitchCardFields(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane)
{
    juce::ignoreUnused(parameters, pane);
    if (kind == PaneKind::harmonie)
        return { pitchfxd::harmonieShiftA, pitchfxd::harmonieShiftB, pitchfxd::harmonieLevelB, pitchfxd::harmonieDry };
    if (kind == PaneKind::justesse)
        return { pitchfxd::justesseSpeed, pitchfxd::justesseAmount, pitchfxd::justesseHumanize, pitchfxd::justesseKey,
                 pitchfxd::justesseScale };
    return {};
}

// A value as these effects show it; empty for none of theirs.
juce::String pitchFormat(const juce::String& binding, const double value)
{
    const auto off = [] { return ui::Ls("Coupé", "Off"); };
    if (binding.contains("harmonie.")) {
        const auto suffix = binding.fromLastOccurrenceOf("harmonie.", false, false);
        if (suffix == "shiftA" || suffix == "shiftB")
            return pitchSignedText(value, " st");
        if (suffix == "fineA" || suffix == "fineB")
            return pitchSignedText(value, " ct");
        if (suffix == "levelA" || suffix == "levelB" || suffix == "dry")
            return value < 0.005 ? off() : percent(value);
        if (suffix == "feedback")
            return percent(value);
        if (suffix == "delay")
            return value < 0.5 ? off() : juce::String(juce::roundToInt(value)) + " ms";
        if (suffix == "panA" || suffix == "panB") {
            const auto amount = juce::roundToInt(std::abs(value) * 100.0);
            if (amount == 0)
                return ui::Ls("Centre", "Centre");
            return (value < 0.0 ? ui::Ls("G ", "L ") : ui::Ls("D ", "R ")) + juce::String(amount);
        }
        if (suffix == "mode")
            return harmonieModeName(whole(static_cast<float>(value), pitchfxd::harmonieModeCount));
        return {};
    }
    if (binding.contains("justesse.")) {
        const auto suffix = binding.fromLastOccurrenceOf("justesse.", false, false);
        if (suffix == "speed")
            return value < 0.5 ? juce::String("Robot") : juce::String(juce::roundToInt(value)) + " ms";
        if (suffix == "amount" || suffix == "mix" || suffix == "sensitivity")
            return percent(value);
        if (suffix == "humanize")
            return value < 0.005 ? off() : percent(value);
        if (suffix == "transpose")
            return pitchSignedText(value, " st");
        if (suffix == "key")
            return justesseKeyName(whole(static_cast<float>(value), 12));
        if (suffix == "scale")
            return justesseScaleName(whole(static_cast<float>(value), pitchfxd::justesseScaleCount));
        if (suffix == "lowPitch" || suffix == "highPitch")
            return juce::String(juce::roundToInt(value)) + " Hz  "
                   + pitchNoteText(static_cast<int>(std::lround(pitchfxd::pitchNoteOfHz(std::max(1.0, value)))));
        return {};
    }
    return {};
}

// A parameter of theirs that takes whole steps (a choice, a switch).
bool pitchStepped(const juce::String& binding)
{
    for (const auto* suffix : { "harmonie.shiftA", "harmonie.shiftB", "harmonie.mode", "justesse.transpose", "justesse.key", "justesse.scale" })
        if (binding.endsWith(suffix))
            return true;
    return false;
}
