// The displays and large views of Résonateurs, Corps, Peigne
// (ResonanceDesign.h computes what they draw). Included by StudioEffectsView.cpp
// inside its anonymous namespace: it has its helpers (knobCell, segmented,
// chooser, comboCell, switchCell, frame, title, section, text, fillUnder…)
// and addedLarge(), the common window layout.

namespace resonanced = engine::resonance;

// The voices II..V of Résonateurs (I wears the device's own colour).
constexpr std::array<ImU32, 4> kResonanceVoiceColours { IM_COL32(255, 176, 92, 255), IM_COL32(110, 196, 255, 255),
                                                        IM_COL32(236, 120, 200, 255), IM_COL32(140, 222, 132, 255) };
constexpr std::array<const char*, resonanced::resonateursVoices> kResonanceRomans { "I", "II", "III", "IV", "V" };

ImU32 resonanceVoiceColour(const int voice, const ImU32 colour)
{
    return voice <= 0 ? colour : kResonanceVoiceColours[static_cast<std::size_t>(std::clamp(voice - 1, 0, 3))];
}

// "A2" (in French "La2"), MIDI 60 being C4.
juce::String resonanceNoteName(const int note)
{
    const auto pitch = static_cast<std::size_t>(resonanced::resonancePitchClassOf(note));
    const auto* name = ui::french() ? resonanced::resonanceNotesFrench[pitch] : resonanced::resonanceNotesEnglish[pitch];
    return juce::String::fromUTF8(name) + juce::String(resonanced::resonanceOctaveOf(note));
}

juce::String resonanceChoiceName(const resonanced::ResonanceChoice& choice)
{
    return juce::String::fromUTF8(ui::french() ? choice.french : choice.english);
}
juce::String resonanceChoiceHint(const resonanced::ResonanceChoice& choice)
{
    return juce::String::fromUTF8(ui::french() ? choice.frenchHint : choice.englishHint);
}

juce::String resonateursModeName(const int index)
{
    return resonanceChoiceName(resonanced::resonateursModes[static_cast<std::size_t>(std::clamp(index, 0, resonanced::resonateursModeCount - 1))]);
}
juce::String resonateursModeHint(const int index)
{
    return resonanceChoiceHint(resonanced::resonateursModes[static_cast<std::size_t>(std::clamp(index, 0, resonanced::resonateursModeCount - 1))]);
}
juce::String corpsTypeName(const int index)
{
    return resonanceChoiceName(resonanced::corpsTypes[static_cast<std::size_t>(std::clamp(index, 0, resonanced::corpsTypeCount - 1))]);
}
juce::String corpsTypeHint(const int index)
{
    return resonanceChoiceHint(resonanced::corpsTypes[static_cast<std::size_t>(std::clamp(index, 0, resonanced::corpsTypeCount - 1))]);
}
juce::String peigneModeName(const int index)
{
    return resonanceChoiceName(resonanced::peigneModes[static_cast<std::size_t>(std::clamp(index, 0, resonanced::peigneModeCount - 1))]);
}
juce::String peigneModeHint(const int index)
{
    return resonanceChoiceHint(resonanced::peigneModes[static_cast<std::size_t>(std::clamp(index, 0, resonanced::peigneModeCount - 1))]);
}

// A choice's name to click in a display's top right corner (Halo's way).
bool resonanceCornerChooser(const char* id, Param* parameter, const int count, juce::String (*name)(int), juce::String (*hint)(int),
                            const int current, const Box& box, const ImU32 colour, const Setter& set, const bool large)
{
    const auto scale = large ? 0.82f : 0.66f;
    const auto width = textSize((name(current) + "  ").toRawUTF8(), scale).x + dp(16.0f);
    return chooser(id, parameter, count, name, hint, ImVec2(box.max.x - width, box.min.y + dp(2.0f)), colour, set, scale);
}

// How loud a reading (linear) glows, 0..1 over the last 48 dB.
float resonanceGlowOf(const float reading)
{
    const auto db = 20.0f * std::log10(std::max(1.0e-6f, reading));
    return std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
}

// ---- Résonateurs ---------------------------------------------------------------------------------

bool resonanceBlackKey(const int note)
{
    const auto pitch = resonanced::resonancePitchClassOf(note);
    return pitch == 1 || pitch == 3 || pitch == 6 || pitch == 8 || pitch == 10;
}

// A keyboard from `lowest` to `highest` across `box`.
struct ResonanceKeyboard {
    int lowest {};
    int highest {};
    Box box;
    float whiteWidth {};
};

Box resonanceKeyBox(const ResonanceKeyboard& keys, const int note)
{
    auto whitesBefore = 0;
    for (auto n = keys.lowest; n < note; ++n)
        if (!resonanceBlackKey(n))
            ++whitesBefore;
    const auto left = keys.box.min.x + keys.whiteWidth * static_cast<float>(whitesBefore);
    if (!resonanceBlackKey(note))
        return { ImVec2(left, keys.box.min.y), ImVec2(left + keys.whiteWidth, keys.box.max.y) };
    const auto width = keys.whiteWidth * 0.62f;
    return { ImVec2(left - width * 0.5f, keys.box.min.y), ImVec2(left + width * 0.5f, keys.box.min.y + keys.box.height() * 0.62f) };
}

// The key under a point, -1 for none (the black keys lie over the white).
int resonanceKeyAt(const ResonanceKeyboard& keys, const ImVec2 point)
{
    for (auto note = keys.lowest; note <= keys.highest; ++note)
        if (resonanceBlackKey(note) && resonanceKeyBox(keys, note).contains(point))
            return note;
    for (auto note = keys.lowest; note <= keys.highest; ++note)
        if (!resonanceBlackKey(note) && resonanceKeyBox(keys, note).contains(point))
            return note;
    return -1;
}

// The keyboard around the chord: its five notes lit in their voices'
// colours (brighter as they are louder, glowing as they ring), their names
// over it; a click on a key puts the root there, the wheel moves it a
// semitone. In the window, chords set the four intervals at once.
bool resonateursDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                        const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    auto changed = false;
    const auto values = studioValues<resonanced::resonateursParameterCount>(parameters, pane, "resonateurs");
    const auto mode = resonanced::resonateursModeOf(values[resonanced::resonateursMode]);
    const auto small = large ? 0.66f : 0.56f;
    std::array<int, resonanced::resonateursVoices> notes {};
    std::array<float, resonanced::resonateursVoices> levels {};
    std::array<float, resonanced::resonateursVoices> glows {};
    const auto readable = live.running && live.readings != nullptr && live.readingCount >= resonanced::resonateursReadingCount;
    for (int voice = 0; voice < resonanced::resonateursVoices; ++voice) {
        const auto v = static_cast<std::size_t>(voice);
        notes[v] = static_cast<int>(std::lround(resonanced::resonateursNoteOf(values.data(), voice)));
        levels[v] = static_cast<float>(resonanced::resonateursLevelOf(values.data(), voice));
        glows[v] = readable ? resonanceGlowOf(live.readings[voice]) : 0.0f;
    }
    auto y = box.min.y + dp(large ? 26.0f : 3.0f);

    // The chords (in the window).
    const auto chord = resonanced::resonateursChordOf(values.data());
    if (large) {
        const auto gap = dp(4.0f);
        const auto cell = std::min(dp(86.0f), (box.width() - dp(16.0f)) / static_cast<float>(resonanced::resonateursChordCount));
        ImGui::PushID("##resonateursChords");
        for (int index = 0; index < resonanced::resonateursChordCount; ++index) {
            const auto& shape = resonanced::resonateursChords[static_cast<std::size_t>(index)];
            const ImVec2 min(box.min.x + dp(8.0f) + cell * static_cast<float>(index), y);
            const ImVec2 max(min.x + cell - gap, y + dp(22.0f));
            ImGui::SetCursorScreenPos(min);
            ImGui::PushID(index);
            if (ImGui::InvisibleButton("##chord", ImVec2(std::max(1.0f, max.x - min.x), dp(22.0f))))
                for (int interval = 0; interval < 4; ++interval)
                    changed |= apply(at(parameters, pane, resonanced::resonateursInterval2 + interval),
                                     static_cast<double>(shape.intervals[static_cast<std::size_t>(interval)]), set);
            const auto hovered = ImGui::IsItemHovered();
            if (hovered) {
                char steps[64];
                std::snprintf(steps, sizeof(steps), "%+d  %+d  %+d  %+d", shape.intervals[0], shape.intervals[1], shape.intervals[2],
                              shape.intervals[3]);
                ImGui::SetTooltip("%s\n%s", L("Les intervalles de II à V (demi-tons)", "Intervals II to V (semitones)"), steps);
            }
            ImGui::PopID();
            const auto on = index == chord;
            list->AddRectFilled(min, max, on ? colour : hovered ? p.bg4 : p.bg3, dp(3.0f));
            centred(list, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), on ? kDark : p.textDim, L(shape.french, shape.english), 0.66f);
        }
        ImGui::PopID();
        y += dp(28.0f);
    }

    // What it plays: the root, the chord, how long it rings.
    char info[96];
    std::snprintf(info, sizeof(info), "%s  \xc2\xb7  %s  \xc2\xb7  %.1f s", resonanceNoteName(notes[0]).toRawUTF8(),
                  chord >= 0 ? L(resonanced::resonateursChords[static_cast<std::size_t>(chord)].french,
                                 resonanced::resonateursChords[static_cast<std::size_t>(chord)].english)
                             : L("accord libre", "own chord"),
                  resonanced::resonateursSecondsOf(values[resonanced::resonateursDecay], mode));
    text(list, ImVec2(box.min.x + dp(6.0f), large ? y : box.min.y + dp(3.0f)), p.textDim, info, small);
    if (large)
        y += ImGui::GetFontSize() * small + dp(6.0f);
    else
        y = box.min.y + dp(17.0f);

    // The names over the keys, on two rows so that close notes stay apart.
    const auto nameHeight = ImGui::GetFontSize() * small;
    const auto namesTop = y;
    y += nameHeight * 2.0f + dp(3.0f);

    // The keyboard: whole octaves around the chord, two at least (three in the window).
    auto lowest = *std::min_element(notes.begin(), notes.end());
    auto highest = *std::max_element(notes.begin(), notes.end());
    lowest = (lowest / 12) * 12;
    highest = (highest / 12) * 12 + 11;
    const auto fewest = large ? 36 : 24;
    while (highest - lowest + 1 < fewest) {
        if (highest + 12 <= 131)
            highest += 12;
        else
            lowest -= 12;
    }
    ResonanceKeyboard keys;
    keys.lowest = lowest;
    keys.highest = highest;
    keys.box = Box { ImVec2(box.min.x + dp(6.0f), y), ImVec2(box.max.x - dp(6.0f), box.max.y - dp(large ? 8.0f : 4.0f)) };
    auto whites = 0;
    for (auto note = lowest; note <= highest; ++note)
        if (!resonanceBlackKey(note))
            ++whites;
    keys.whiteWidth = keys.box.width() / static_cast<float>(std::max(1, whites));

    // Which voice holds each key (I over the others when two meet).
    const auto holder = [&notes, &levels](const int note) {
        for (int voice = 0; voice < resonanced::resonateursVoices; ++voice)
            if (notes[static_cast<std::size_t>(voice)] == note && levels[static_cast<std::size_t>(voice)] > 0.0005f)
                return voice;
        return -1;
    };
    // A piano's ivory and ebony, whatever the theme.
    constexpr ImU32 whiteKey = IM_COL32(222, 224, 230, 255);
    constexpr ImU32 blackKey = IM_COL32(30, 32, 38, 255);
    const auto drawKey = [&](const int note) {
        const auto key = resonanceKeyBox(keys, note);
        const auto black = resonanceBlackKey(note);
        const auto voice = holder(note);
        auto fill = black ? blackKey : whiteKey;
        if (voice >= 0) {
            const auto v = static_cast<std::size_t>(voice);
            fill = theme::mix(fill, resonanceVoiceColour(voice, colour), 0.35f + 0.65f * levels[v]);
        }
        list->AddRectFilled(ImVec2(key.min.x + 0.5f, key.min.y), ImVec2(key.max.x - 0.5f, key.max.y), fill, dp(2.0f));
        if (voice < 0)
            return;
        const auto v = static_cast<std::size_t>(voice);
        if (glows[v] > 0.01f)
            list->AddRectFilled(ImVec2(key.min.x + 0.5f, key.min.y), ImVec2(key.max.x - 0.5f, key.max.y), theme::alpha(p.text, 0.35f * glows[v]),
                                dp(2.0f));
        centred(list, ImVec2((key.min.x + key.max.x) * 0.5f, key.max.y - nameHeight * 0.75f), kDark, kResonanceRomans[v], small * 0.9f);
    };
    for (auto note = lowest; note <= highest; ++note)
        if (!resonanceBlackKey(note))
            drawKey(note);
    for (auto note = lowest; note <= highest; ++note)
        if (resonanceBlackKey(note))
            drawKey(note);
    // A C's octave under its key, in the window.
    if (large)
        for (auto note = lowest; note <= highest; note += 12) {
            const auto key = resonanceKeyBox(keys, note);
            text(list, ImVec2(key.min.x + dp(2.0f), key.max.y - nameHeight * 2.1f), theme::alpha(kDark, 0.6f),
                 resonanceNoteName(note).toRawUTF8(), 0.52f);
        }

    // The names, lowest first, alternating rows.
    std::array<int, resonanced::resonateursVoices> order { 0, 1, 2, 3, 4 };
    std::sort(order.begin(), order.end(), [&notes](const int a, const int b) {
        return notes[static_cast<std::size_t>(a)] < notes[static_cast<std::size_t>(b)];
    });
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        const auto voice = order[rank];
        const auto v = static_cast<std::size_t>(voice);
        const auto key = resonanceKeyBox(keys, notes[v]);
        auto name = resonanceNoteName(notes[v]);
        if (large)
            name = juce::String(kResonanceRomans[v]) + " " + name;
        const auto tint = levels[v] > 0.0005f ? resonanceVoiceColour(voice, colour) : p.textFaint;
        centred(list, ImVec2((key.min.x + key.max.x) * 0.5f, namesTop + nameHeight * (rank % 2 == 0 ? 0.5f : 1.5f)),
                theme::alpha(tint, 0.55f + 0.45f * levels[v]), name.toRawUTF8(), small);
    }

    // A click on a key: the root; the wheel: a semitone.
    ImGui::SetCursorScreenPos(keys.box.min);
    ImGui::InvisibleButton("##resonateursKeys", ImVec2(std::max(1.0f, keys.box.width()), std::max(1.0f, keys.box.height())));
    if (ImGui::IsItemActive()) {
        const auto note = resonanceKeyAt(keys, io.MousePos);
        if (note >= 0)
            changed |= apply(at(parameters, pane, resonanced::resonateursNote), static_cast<double>(note), set);
    }
    if (ImGui::IsItemHovered()) {
        if (io.MouseWheel != 0.0f) {
            changed |= apply(at(parameters, pane, resonanced::resonateursNote),
                             static_cast<double>(values[resonanced::resonateursNote]) + (io.MouseWheel > 0.0f ? 1.0 : -1.0), set);
            ui::takeWheel();
        }
        if (!ImGui::IsItemActive())
            ImGui::SetTooltip("%s", L("Cliquez une touche : la note de I, les autres suivent. Molette : un demi-ton.",
                                      "Click a key: voice I's note, the others follow. Wheel: a semitone."));
    }

    changed |= resonanceCornerChooser("##resonateursMode", at(parameters, pane, resonanced::resonateursMode), resonanced::resonateursModeCount,
                                      resonateursModeName, resonateursModeHint, mode, box, colour, set, large);
    return changed;
}

// The window: the keyboard and its chords large, the five voices (I's note
// and the others' intervals, each one's level) in their colours, then the
// sound.
bool resonateursLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                      const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    constexpr auto diameter = 40.0f;
    const auto inner = size.x - dp(16.0f);
    // Ten knobs on one row when they fit, else the notes over the levels.
    const auto voiceColumns = inner / 10.0f >= dp(68.0f) ? 10 : 5;
    const auto voiceRows = voiceColumns == 10 ? 1 : 2;
    const auto row = knobHeight(diameter);
    const auto panelHeight = dp(6.0f) + dp(16.0f) + row * static_cast<float>(voiceRows) + dp(6.0f) + dp(30.0f) + row + dp(8.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(180.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inside = frame(list, outer);
    list->PushClipRect(inside.min, inside.max, true);
    changed |= resonateursDisplay(parameters, pane, inside, colour, live, set, true);
    title(list, inside, L("RÉSONATEURS  ·  cinq cordes que le son fait chanter", "RESONATORS  ·  five strings the sound makes sing"));
    list->PopClipRect();

    auto y = outer.max.y + dp(8.0f);
    const Box plate { ImVec2(position.x, y), ImVec2(position.x + size.x, std::max(y + dp(10.0f), position.y + size.y)) };
    list->AddRectFilled(plate.min, plate.max, theme::mix(colour, p.bg1, 0.9f), dp(6.0f));
    list->AddRect(plate.min, plate.max, theme::alpha(colour, 0.5f), dp(6.0f));
    y += dp(6.0f);
    section(list, ImVec2(position.x + dp(10.0f), y), L("VOIX", "VOICES"));
    y += dp(16.0f);
    struct Item {
        int index;
        const char* label;
        int voice;
    };
    const std::array<Item, 10> voices { { { resonanced::resonateursNote, L("I · NOTE", "I · NOTE"), 0 },
                                          { resonanced::resonateursGain1, L("I · NIVEAU", "I · LEVEL"), 0 },
                                          { resonanced::resonateursInterval2, L("II · INTERV.", "II · INTERV."), 1 },
                                          { resonanced::resonateursGain2, L("II · NIVEAU", "II · LEVEL"), 1 },
                                          { resonanced::resonateursInterval3, L("III · INTERV.", "III · INTERV."), 2 },
                                          { resonanced::resonateursGain3, L("III · NIVEAU", "III · LEVEL"), 2 },
                                          { resonanced::resonateursInterval4, L("IV · INTERV.", "IV · INTERV."), 3 },
                                          { resonanced::resonateursGain4, L("IV · NIVEAU", "IV · LEVEL"), 3 },
                                          { resonanced::resonateursInterval5, L("V · INTERV.", "V · INTERV."), 4 },
                                          { resonanced::resonateursGain5, L("V · NIVEAU", "V · LEVEL"), 4 } } };
    const auto cell = inner / static_cast<float>(voiceColumns);
    for (std::size_t index = 0; index < voices.size(); ++index) {
        const auto column = voiceColumns == 10 ? index : index / 2;
        const auto line = voiceColumns == 10 ? std::size_t { 0 } : index % 2;
        changed |= knobCell(ImVec2(position.x + dp(8.0f) + cell * static_cast<float>(column), y + row * static_cast<float>(line)), cell,
                            at(parameters, pane, voices[index].index), voices[index].label, resonanceVoiceColour(voices[index].voice, colour), live,
                            set, diameter);
    }
    y += row * static_cast<float>(voiceRows) + dp(6.0f);
    section(list, ImVec2(position.x + dp(10.0f), y + dp(6.0f)), L("SON", "SOUND"));
    std::vector<const char*> modes;
    for (const auto& choice : resonanced::resonateursModes)
        modes.push_back(L(choice.french, choice.english));
    changed |= segmented("##resonateursModes", at(parameters, pane, resonanced::resonateursMode), modes, ImVec2(position.x + dp(70.0f), y),
                         std::min(dp(330.0f), inner - dp(70.0f)), dp(22.0f), colour, set,
                         L("Cordes : des harmoniques justes. Pincé : plus bref et plus brillant. Métal : des partiels étirés, comme une cloche.",
                           "Strings: true harmonics. Plucked: shorter and brighter. Metal: stretched partials, bell-like."));
    y += dp(30.0f);
    const std::array<Item, 6> sound { { { resonanced::resonateursDecay, L("DURÉE", "DECAY"), 0 },
                                        { resonanced::resonateursColour, L("COULEUR", "COLOUR"), 0 },
                                        { resonanced::resonateursTone, L("TIMBRE", "TONE"), 0 },
                                        { resonanced::resonateursWidth, L("LARGEUR", "WIDTH"), 0 },
                                        { resonanced::resonateursMix, L("MÉLANGE", "MIX"), 0 },
                                        { resonanced::resonateursOutput, L("SORTIE", "OUTPUT"), 0 } } };
    const auto soundCell = std::min(dp(110.0f), inner / static_cast<float>(sound.size()));
    for (std::size_t index = 0; index < sound.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(8.0f) + soundCell * static_cast<float>(index), y), soundCell,
                            at(parameters, pane, sound[index].index), sound[index].label, colour, live, set, diameter);
    return changed;
}

// ---- Corps ---------------------------------------------------------------------------------------

// The object drawn (a bar, a marimba bar, a string, a round skin, a plate,
// an open pipe, a closed tube), where it is heard as a dot to drag, and
// where the right channel hears it at width as a ring.
bool corpsObjectView(std::vector<Param>& parameters, const Pane& pane, const Box& area, const float* values, const ImU32 colour,
                     const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    const auto type = resonanced::corpsTypeOf(values[resonanced::corpsType]);
    const auto material = std::clamp(values[resonanced::corpsMaterial], 0.0f, 1.0f);
    const auto place = std::clamp(values[resonanced::corpsPosition], 0.0f, 1.0f);
    const auto width = std::clamp(values[resonanced::corpsWidth], 0.0f, 1.0f);
    // Wood to metal.
    const auto body = theme::mix(IM_COL32(172, 120, 74, 255), IM_COL32(178, 188, 202, 255), material);
    const auto cx = (area.min.x + area.max.x) * 0.5f;
    const auto cy = (area.min.y + area.max.y) * 0.5f;
    const auto left = area.min.x + area.width() * 0.06f;
    const auto right = area.max.x - area.width() * 0.06f;
    const auto half = (right - left) * 0.5f;
    const auto thick = std::clamp(area.height() * 0.2f, dp(5.0f), dp(18.0f));
    const auto radius = std::max(dp(4.0f), std::min(area.width(), area.height()) * 0.42f);
    const auto plateWidth = std::min(area.width() * 0.88f, area.height() * 0.88f * 1.5f);
    const auto plateHeight = plateWidth / 1.5f;
    const ImVec2 plateMin(cx - plateWidth * 0.5f, cy - plateHeight * 0.5f);
    const ImVec2 plateMax(cx + plateWidth * 0.5f, cy + plateHeight * 0.5f);
    // A place (0..1) on the drawing, and back from the pointer.
    const auto pointOf = [&](const float where) {
        switch (type) {
        case resonanced::corpsObjectMembrane: return ImVec2(cx + 0.96f * where * radius, cy);
        case resonanced::corpsObjectPlate:
            return ImVec2(plateMin.x + (0.5f - 0.46f * where) * plateWidth, plateMin.y + (0.5f - 0.31f * where) * plateHeight);
        case resonanced::corpsObjectTube: return ImVec2(left + 0.92f * where * (right - left), cy);
        default: return ImVec2(cx + 0.96f * where * half, cy);
        }
    };
    const auto placeOf = [&](const ImVec2 mouse) {
        switch (type) {
        case resonanced::corpsObjectMembrane:
            return std::clamp(std::hypot(mouse.x - cx, mouse.y - cy) / std::max(1.0f, 0.96f * radius), 0.0f, 1.0f);
        case resonanced::corpsObjectPlate: {
            const auto dx = -0.46f * plateWidth;
            const auto dy = -0.31f * plateHeight;
            return std::clamp(((mouse.x - cx) * dx + (mouse.y - cy) * dy) / std::max(1.0f, dx * dx + dy * dy), 0.0f, 1.0f);
        }
        case resonanced::corpsObjectTube: return std::clamp((mouse.x - left) / std::max(1.0f, 0.92f * (right - left)), 0.0f, 1.0f);
        default: return std::clamp(std::abs(mouse.x - cx) / std::max(1.0f, 0.96f * half), 0.0f, 1.0f);
        }
    };
    const auto supports = [&](const float bottom) {
        for (const auto node : { 0.224f, 0.776f }) {
            const auto x = left + node * (right - left);
            list->AddTriangleFilled(ImVec2(x, bottom), ImVec2(x - dp(4.0f), bottom + dp(7.0f)), ImVec2(x + dp(4.0f), bottom + dp(7.0f)),
                                    p.textDim);
        }
    };
    switch (type) {
    case resonanced::corpsObjectBeam:
        list->AddRectFilled(ImVec2(left, cy - thick * 0.5f), ImVec2(right, cy + thick * 0.5f), body, dp(2.0f));
        supports(cy + thick * 0.5f);
        break;
    case resonanced::corpsObjectMarimba: {
        // Thinner under its middle, where it is cut to tune it; its
        // resonator hanging under.
        constexpr int pieces = 24;
        const auto top = cy - thick * 0.5f;
        for (int piece = 0; piece < pieces; ++piece) {
            const auto u0 = static_cast<float>(piece) / pieces;
            const auto u1 = static_cast<float>(piece + 1) / pieces;
            const auto x0 = left + u0 * (right - left);
            const auto x1 = left + u1 * (right - left);
            const auto d0 = thick * (1.0f - 0.45f * std::sin(static_cast<float>(resonanced::resonancePi) * u0));
            const auto d1 = thick * (1.0f - 0.45f * std::sin(static_cast<float>(resonanced::resonancePi) * u1));
            list->AddQuadFilled(ImVec2(x0, top), ImVec2(x1, top), ImVec2(x1, top + d1), ImVec2(x0, top + d0), body);
        }
        list->AddRect(ImVec2(cx - thick * 0.45f, cy + thick * 0.75f), ImVec2(cx + thick * 0.45f, std::min(area.max.y, cy + thick * 2.6f)),
                      theme::alpha(p.textDim, 0.6f), dp(2.0f), dp(1.0f));
        supports(cy + thick * 0.5f);
        break;
    }
    case resonanced::corpsObjectString: {
        list->AddRectFilled(ImVec2(left - dp(3.0f), cy - thick * 0.8f), ImVec2(left, cy + thick * 0.8f), p.textDim);
        list->AddRectFilled(ImVec2(right, cy - thick * 0.8f), ImVec2(right + dp(3.0f), cy + thick * 0.8f), p.textDim);
        // Its first mode, swinging.
        std::vector<ImVec2> swing;
        for (int step = 0; step <= 32; ++step) {
            const auto u = static_cast<float>(step) / 32.0f;
            swing.emplace_back(left + u * (right - left), cy - thick * 0.9f * std::sin(static_cast<float>(resonanced::resonancePi) * u));
        }
        list->AddPolyline(swing.data(), static_cast<int>(swing.size()), theme::alpha(body, 0.35f), dp(1.0f));
        list->AddLine(ImVec2(left, cy), ImVec2(right, cy), body, dp(2.0f));
        break;
    }
    case resonanced::corpsObjectMembrane:
        list->AddCircleFilled(ImVec2(cx, cy), radius, theme::alpha(body, 0.35f), 48);
        list->AddCircle(ImVec2(cx, cy), radius * 0.436f, theme::alpha(body, 0.4f), 32, dp(1.0f));
        list->AddCircle(ImVec2(cx, cy), radius, body, 48, dp(2.5f));
        break;
    case resonanced::corpsObjectPlate:
        list->AddRectFilled(plateMin, plateMax, theme::alpha(body, 0.45f), dp(2.0f));
        list->AddRect(plateMin, plateMax, body, dp(2.0f), dp(1.5f));
        break;
    default: {
        // A pipe open at both ends, or a tube closed at its left.
        list->AddRectFilled(ImVec2(left, cy - thick * 0.6f), ImVec2(right, cy + thick * 0.6f), theme::alpha(body, 0.3f));
        list->AddLine(ImVec2(left, cy - thick * 0.6f), ImVec2(right, cy - thick * 0.6f), body, dp(2.0f));
        list->AddLine(ImVec2(left, cy + thick * 0.6f), ImVec2(right, cy + thick * 0.6f), body, dp(2.0f));
        list->AddLine(ImVec2(right, cy - thick * 0.75f), ImVec2(right, cy + thick * 0.75f), theme::alpha(body, 0.5f), dp(1.0f));
        if (type == resonanced::corpsObjectTube)
            list->AddRectFilled(ImVec2(left - dp(3.0f), cy - thick * 0.6f), ImVec2(left + dp(2.0f), cy + thick * 0.6f), body);
        else
            list->AddLine(ImVec2(left, cy - thick * 0.75f), ImVec2(left, cy + thick * 0.75f), theme::alpha(body, 0.5f), dp(1.0f));
        break;
    }
    }

    // Drag the dot (or click anywhere on the object): where it is heard.
    ImGui::SetCursorScreenPos(area.min);
    ImGui::InvisibleButton("##corpsObject", ImVec2(std::max(1.0f, area.width()), std::max(1.0f, area.height())));
    auto changed = false;
    if (ImGui::IsItemActive())
        changed = apply(at(parameters, pane, resonanced::corpsPosition), static_cast<double>(placeOf(io.MousePos)), set);
    const auto hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", L("Glissez le point : où l'objet est écouté (au milieu, ou vers un bord).",
                                  "Drag the dot: where the object is heard (its middle, or towards an edge)."));
    if (width > 0.01f) {
        auto other = place + static_cast<float>(resonanced::corpsWidthPlace) * width;
        if (other > 1.0f)
            other = 2.0f - other;
        list->AddCircle(pointOf(other), dp(large ? 6.0f : 4.5f), theme::alpha(p.text, 0.55f), 16, dp(1.5f));
    }
    handleDot(list, pointOf(place), colour, hot);
    return changed;
}

// The object on the left; on the right its modes on a frequency axis, each
// a line as tall as it is heard and as bright as it lasts, lit as it rings
// now; the sound's spectrum behind. Drag the modes sideways to tune them,
// the wheel for the decay.
bool corpsDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live, const Setter& set,
                  const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    auto changed = false;
    const auto values = studioValues<resonanced::corpsParameterCount>(parameters, pane, "corps");
    const auto type = resonanced::corpsTypeOf(values[resonanced::corpsType]);
    const auto modes = resonanced::corpsModesOf(values.data(), 0, 48'000.0);
    const auto header = dp(large ? 26.0f : 17.0f);
    const auto split = box.min.x + box.width() * (large ? 0.32f : 0.36f);
    const Box object { ImVec2(box.min.x + dp(6.0f), box.min.y + header), ImVec2(split - dp(4.0f), box.max.y - dp(6.0f)) };
    const Box plot { ImVec2(split + dp(2.0f), box.min.y + header), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.45f), dp(3.0f));
    frequencyGrid(list, plot, large);
    spectrumBehind(list, plot, live.after, colour);
    // What the low cut keeps from it.
    const auto cut = xOfHz(plot, static_cast<double>(values[resonanced::corpsLowCut]));
    list->AddRectFilled(plot.min, ImVec2(cut, plot.max.y), theme::alpha(kDark, 0.4f));
    const auto readable = live.running && live.readings != nullptr && live.readingCount >= resonanced::corpsReadingCount;
    for (int mode = 0; mode < modes.count; ++mode) {
        const auto k = static_cast<std::size_t>(mode);
        const auto x = xOfHz(plot, modes.hz[k]);
        const auto lasts = static_cast<float>(std::clamp(std::log(modes.seconds[k] / 0.01) / std::log(3'000.0), 0.0, 1.0));
        const auto top = plot.max.y - (0.05f + 0.85f * static_cast<float>(modes.weight[k])) * plot.height();
        list->AddLine(ImVec2(x, plot.max.y), ImVec2(x, top), theme::alpha(colour, 0.2f + 0.8f * lasts), dp(large ? 2.5f : 1.6f));
        if (readable) {
            const auto glow = resonanceGlowOf(live.readings[mode] * 8.0f);
            if (glow > 0.01f)
                list->AddCircleFilled(ImVec2(x, top), dp(large ? 2.5f : 1.5f) + dp(3.0f) * glow, theme::alpha(p.text, 0.25f + 0.6f * glow));
        }
    }
    // What it is tuned to, how long it rings.
    const auto tune = static_cast<double>(values[resonanced::corpsTune]);
    const auto nearest = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(std::max(1.0, tune) / 440.0)));
    char info[64];
    std::snprintf(info, sizeof(info), "%.0f Hz  \xc2\xb7  %s  \xc2\xb7  %.2g s", tune, resonanceNoteName(nearest).toRawUTF8(),
                  resonanced::corpsSecondsOf(values[resonanced::corpsDecay]));
    text(list, ImVec2(plot.min.x + dp(4.0f), plot.min.y + dp(2.0f)), p.textDim, info, large ? 0.66f : 0.56f);

    // Sideways: the tuning; the wheel: the decay.
    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton("##corpsModes", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    if (ImGui::IsItemActive() && io.MouseDelta.x != 0.0f)
        changed |= apply(at(parameters, pane, resonanced::corpsTune), hzOfX(plot, xOfHz(plot, tune) + io.MouseDelta.x), set);
    if (ImGui::IsItemHovered()) {
        if (io.MouseWheel != 0.0f) {
            changed |= apply(at(parameters, pane, resonanced::corpsDecay),
                             static_cast<double>(values[resonanced::corpsDecay]) + 0.03 * static_cast<double>(io.MouseWheel), set);
            ui::takeWheel();
        }
        if (!ImGui::IsItemActive())
            ImGui::SetTooltip("%s", L("Glissez de côté : l'accord. Molette : la durée.", "Drag sideways: the tuning. Wheel: the decay."));
    }

    changed |= corpsObjectView(parameters, pane, object, values.data(), colour, set, large);
    changed |= resonanceCornerChooser("##corpsType", at(parameters, pane, resonanced::corpsType), resonanced::corpsTypeCount, corpsTypeName,
                                      corpsTypeHint, type, box, colour, set, large);
    return changed;
}

// ---- Peigne --------------------------------------------------------------------------------------

// The comb's response on a frequency axis (its teeth finer than a pixel up
// high: each column its highest and lowest), the sound's spectrum behind,
// where its LFO takes it and where it is now; drag sideways for the
// frequency, up and down (or the wheel) for the feedback.
bool peigneDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live, const Setter& set,
                   const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    auto changed = false;
    const auto values = studioValues<resonanced::peigneParameterCount>(parameters, pane, "peigne");
    const auto mode = resonanced::peigneModeOf(values[resonanced::peigneMode]);
    const Box plot { ImVec2(box.min.x + dp(4.0f), box.min.y + dp(large ? 26.0f : 17.0f)), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.45f), dp(3.0f));
    frequencyGrid(list, plot, large);
    spectrumBehind(list, plot, live.after, colour);
    constexpr auto topDb = 24.0;
    constexpr auto bottomDb = -30.0;
    const auto yOf = [&](const double db) {
        return plot.min.y + static_cast<float>((topDb - std::clamp(db, bottomDb, topDb)) / (topDb - bottomDb)) * plot.height();
    };
    dashedLine(list, ImVec2(plot.min.x, yOf(0.0)), ImVec2(plot.max.x, yOf(0.0)), theme::alpha(p.text, 0.25f));
    if (large)
        for (const auto db : { -24.0, -12.0, 12.0 }) {
            char mark[8];
            std::snprintf(mark, sizeof(mark), "%+.0f", db);
            text(list, ImVec2(plot.min.x + dp(3.0f), yOf(db) - ImGui::GetFontSize() * 0.66f), p.textFaint, mark, 0.56f);
        }
    // The response.
    const auto shape = resonanced::peigneShapeOf(values.data(), 48'000.0);
    std::vector<ImVec2> highs;
    const auto step = dp(1.5f);
    for (auto x = plot.min.x; x < plot.max.x; x += step) {
        const auto from = hzOfX(plot, x);
        const auto to = hzOfX(plot, x + step);
        auto highest = -200.0;
        auto lowest = 200.0;
        for (int sample = 0; sample < 6; ++sample) {
            const auto hz = from * std::pow(to / from, static_cast<double>(sample) / 5.0);
            const auto db = resonanced::peigneResponseDbOf(shape, hz);
            highest = std::max(highest, db);
            lowest = std::min(lowest, db);
        }
        if (highest - lowest > 3.0)
            list->AddLine(ImVec2(x, yOf(highest)), ImVec2(x, yOf(lowest)), theme::alpha(colour, 0.3f), std::max(1.0f, step));
        highs.emplace_back(x, yOf(highest));
    }
    fillUnder(list, highs, plot.max.y, theme::alpha(colour, 0.12f));
    list->AddPolyline(highs.data(), static_cast<int>(highs.size()), colour, dp(large ? 1.8f : 1.3f));

    // Where the LFO takes it, as a band along the top; where it is now.
    const auto frequency = static_cast<double>(values[resonanced::peigneFrequency]);
    const auto depth = static_cast<double>(values[resonanced::peigneLfoDepth]) * resonanced::peigneLfoOctaves;
    if (depth > 0.001)
        list->AddRectFilled(ImVec2(xOfHz(plot, frequency * std::exp2(-depth)), plot.min.y),
                            ImVec2(xOfHz(plot, frequency * std::exp2(depth)), plot.min.y + dp(4.0f)), theme::alpha(colour, 0.45f));
    if (live.running && live.readings != nullptr && live.readingCount >= resonanced::peigneReadingCount)
        for (int side = 0; side < 2; ++side) {
            const auto x = xOfHz(plot, static_cast<double>(live.readings[side]));
            const auto tint = side == 0 ? colour : theme::mix(colour, p.text, 0.5f);
            list->AddTriangleFilled(ImVec2(x - dp(4.0f), plot.min.y), ImVec2(x + dp(4.0f), plot.min.y), ImVec2(x, plot.min.y + dp(7.0f)), tint);
        }
    const auto handleX = xOfHz(plot, frequency);
    dashedLine(list, ImVec2(handleX, plot.min.y), ImVec2(handleX, plot.max.y), theme::alpha(p.text, 0.4f));

    // What it is set to; a negative feedback sounds an octave lower.
    const auto feedback = static_cast<double>(values[resonanced::peigneFeedback]);
    char info[96];
    if (mode == 0 && feedback < 0.0)
        std::snprintf(info, sizeof(info), "%.0f Hz  \xc2\xb7  %+.0f %%  \xc2\xb7  %s %.0f Hz", frequency, feedback * 100.0, L("sonne à", "sounds at"),
                      frequency * 0.5);
    else
        std::snprintf(info, sizeof(info), "%.0f Hz  \xc2\xb7  %+.0f %%", frequency, feedback * 100.0);
    text(list, ImVec2(plot.min.x + dp(4.0f), plot.min.y + dp(6.0f)), p.textDim, info, large ? 0.66f : 0.56f);

    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton("##peigneResponse", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    const auto hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (ImGui::IsItemActive()) {
        if (io.MouseDelta.x != 0.0f)
            changed |= apply(at(parameters, pane, resonanced::peigneFrequency), hzOfX(plot, handleX + io.MouseDelta.x), set);
        if (io.MouseDelta.y != 0.0f)
            changed |= apply(at(parameters, pane, resonanced::peigneFeedback), feedback - 0.005 * static_cast<double>(io.MouseDelta.y), set);
    }
    if (ImGui::IsItemHovered()) {
        if (io.MouseWheel != 0.0f) {
            changed |= apply(at(parameters, pane, resonanced::peigneFeedback), feedback + 0.02 * static_cast<double>(io.MouseWheel), set);
            ui::takeWheel();
        }
        if (!ImGui::IsItemActive())
            ImGui::SetTooltip("%s", L("Glissez de côté : la fréquence ; de haut en bas (ou la molette) : la rétroaction.",
                                      "Drag sideways: the frequency; up and down (or the wheel): the feedback."));
    }
    // The handle on the curve at the frequency: up and down is the feedback.
    const auto handleY = std::max(plot.min.y + dp(9.0f), std::min(yOf(resonanced::peigneResponseDbOf(shape, frequency)), plot.max.y - dp(9.0f)));
    handleDot(list, ImVec2(handleX, handleY), colour, hot, "F");

    changed |= resonanceCornerChooser("##peigneMode", at(parameters, pane, resonanced::peigneMode), resonanced::peigneModeCount, peigneModeName,
                                      peigneModeHint, mode, box, colour, set, large);
    return changed;
}

// ---- The five entry points -----------------------------------------------------------------------

// A card's display (large: the window's), true when a value changed.
bool resonanceDisplay(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour,
                    const Live& live, const Setter& set, const bool large)
{
    switch (kind) {
    case PaneKind::resonateurs: return resonateursDisplay(parameters, pane, box, colour, live, set, large);
    case PaneKind::corps: return corpsDisplay(parameters, pane, box, colour, live, set, large);
    case PaneKind::peigne: return peigneDisplay(parameters, pane, box, colour, live, set, large);
    default: return false;
    }
}

// The window: its display large and every control.
bool resonanceLarge(const PaneKind kind, const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size,
                  const ImU32 colour, const Live& live, const Setter& set)
{
    switch (kind) {
    case PaneKind::resonateurs: return resonateursLarge(pane, parameters, position, size, colour, live, set);
    case PaneKind::corps:
        // The object's type is chosen in the display's corner.
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("CORPS  ·  un objet que le son fait résonner", "BODY  ·  an object the sound makes resonate"),
                          { { L("OBJET", "OBJECT"), { resonanced::corpsTune, resonanced::corpsInharmonic, resonanced::corpsPosition } },
                            { L("SON", "SOUND"), { resonanced::corpsDecay, resonanced::corpsMaterial, resonanced::corpsLowCut, resonanced::corpsWidth } },
                            { L("SORTIE", "OUTPUT"), { resonanced::corpsMix, resonanced::corpsOutput } } },
                          [&](const Box& inside) { return corpsDisplay(parameters, pane, inside, colour, live, set, true); });
    case PaneKind::peigne:
        // Its mode is chosen in the display's corner.
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("PEIGNE  ·  un filtre en peigne accordé", "COMB  ·  a tuned comb filter"),
                          { { L("PEIGNE", "COMB"), { resonanced::peigneFrequency, resonanced::peigneFeedback, resonanced::peigneDamping } },
                            { L("MOUVEMENT", "MOTION"),
                              { resonanced::peigneLfoDepth, resonanced::peigneLfoRate, resonanced::peigneSync, resonanced::peigneSpread } },
                            { L("SORTIE", "OUTPUT"), { resonanced::peigneMix, resonanced::peigneOutput } } },
                          [&](const Box& inside) { return peigneDisplay(parameters, pane, inside, colour, live, set, true); });
    default: return false;
    }
}

// The values a card shows as knobs (pane indices).
std::vector<int> resonanceCardFields(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane)
{
    juce::ignoreUnused(parameters, pane);
    switch (kind) {
    case PaneKind::resonateurs:
        return { resonanced::resonateursNote, resonanced::resonateursDecay, resonanced::resonateursColour, resonanced::resonateursMix };
    case PaneKind::corps: return { resonanced::corpsTune, resonanced::corpsDecay, resonanced::corpsMaterial, resonanced::corpsMix };
    case PaneKind::peigne: return { resonanced::peigneFrequency, resonanced::peigneFeedback, resonanced::peigneDamping, resonanced::peigneMix };
    default: return {};
    }
}

// The suffix of one of these effects' bindings ("note" for "….resonateurs.note"), empty when it is not theirs.
juce::String resonanceSuffixOf(const juce::String& binding, const char* prefix)
{
    const auto marker = juce::String(prefix) + ".";
    const auto start = binding.lastIndexOf(marker);
    if (start < 0 || (start > 0 && binding[start - 1] != '.'))
        return {};
    return binding.substring(start + marker.length());
}

juce::String resonanceSigned(const double value)
{
    return (value > 0.0 ? juce::String("+") : value < 0.0 ? juce::String("-") : juce::String()) + percent(std::abs(value));
}

// A value as these effects show it; empty for none of theirs.
juce::String resonanceFormat(const juce::String& binding, const double value)
{
    if (const auto suffix = resonanceSuffixOf(binding, "resonateurs"); suffix.isNotEmpty()) {
        if (suffix == "note")
            return resonanceNoteName(static_cast<int>(std::lround(value)));
        if (suffix.startsWith("interval")) {
            const auto steps = static_cast<int>(std::lround(value));
            return (steps > 0 ? juce::String("+") : juce::String()) + juce::String(steps) + (ui::french() ? " dt" : " st");
        }
        if (suffix == "mode")
            return resonateursModeName(static_cast<int>(std::lround(value)));
        if (suffix.startsWith("gain"))
            return value <= 0.0005 ? ui::Ls("Coupé", "Off") : percent(value);
        if (suffix == "tone") {
            if (std::abs(value) < 0.005)
                return ui::Ls("Neutre", "Flat");
            return (value < 0.0 ? ui::Ls("Sombre ", "Dark ") : ui::Ls("Clair ", "Bright ")) + percent(std::abs(value));
        }
        if (suffix == "decay" || suffix == "colour" || suffix == "width" || suffix == "mix")
            return percent(value);
        return {};
    }
    if (const auto suffix = resonanceSuffixOf(binding, "corps"); suffix.isNotEmpty()) {
        if (suffix == "type")
            return corpsTypeName(static_cast<int>(std::lround(value)));
        if (suffix == "decay") {
            const auto seconds = resonanced::corpsSecondsOf(static_cast<float>(value));
            return juce::String(seconds, seconds < 1.0 ? 2 : 1) + " s";
        }
        if (suffix == "material") {
            if (value <= 0.02)
                return ui::Ls("Bois", "Wood");
            if (value >= 0.98)
                return ui::Ls("Métal", "Metal");
            return percent(value);
        }
        if (suffix == "inharmonic")
            return std::abs(value) < 0.005 ? ui::Ls("Juste", "Pure") : resonanceSigned(value);
        if (suffix == "position" || suffix == "width" || suffix == "mix")
            return percent(value);
        return {};
    }
    if (const auto suffix = resonanceSuffixOf(binding, "peigne"); suffix.isNotEmpty()) {
        if (suffix == "mode")
            return peigneModeName(static_cast<int>(std::lround(value)));
        if (suffix == "sync") {
            if (const auto* division = engine::noteDivisionFor(static_cast<float>(value)))
                return juce::String(std::string(division->name));
            return ui::Ls("Libre", "Free");
        }
        if (suffix == "feedback")
            return resonanceSigned(value);
        if (suffix == "damping")
            return value >= 19'900.0 ? ui::Ls("Aucun", "Off") : juce::String();
        if (suffix == "lfoDepth")
            return value <= 0.0005 ? ui::Ls("Coupé", "Off")
                                   : juce::String::fromUTF8("\xc2\xb1") + juce::String(value * resonanced::peigneLfoOctaves, 2) + " oct";
        if (suffix == "spread" || suffix == "mix")
            return percent(value);
        return {};
    }
    return {};
}

// A parameter of theirs that takes whole steps (a choice, a switch).
bool resonanceStepped(const juce::String& binding)
{
    for (const auto* suffix : { "resonateurs.note", "resonateurs.mode", "resonateurs.interval2", "resonateurs.interval3", "resonateurs.interval4",
                                "resonateurs.interval5", "corps.type", "peigne.mode", "peigne.sync" })
        if (binding.endsWith(suffix))
            return true;
    return false;
}
