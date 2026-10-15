// The MIDI effects' displays and large views (Arpège, Accords, Gamme, Écho de
// notes, Aléa, Durée, Séquenceur, Roulement). Included by
// StudioEffectsView.cpp inside its anonymous namespace. What they draw comes
// from the engine's own functions (MidiEffects.h), so a card shows what the
// notes will do.

namespace midid = engine::midifx;

constexpr ImU32 kMidiNote = IM_COL32(212, 123, 232, 255);
constexpr ImU32 kMidiGhost = IM_COL32(212, 123, 232, 90);

juce::String midiNoteName(const int note)
{
    if (note < 0 || note > 127)
        return juce::String::fromUTF8("\xe2\x80\x94");
    return juce::String(midid::keyNames[static_cast<std::size_t>(note % 12)]) + juce::String(note / 12 - 1);
}

juce::String namedOf(const midid::Named& named)
{
    return juce::String::fromUTF8(ui::french() ? named.french : named.english);
}

juce::String divisionName(const double value, const bool freeAtZero)
{
    if (const auto* division = engine::noteDivisionFor(static_cast<float>(value)))
        return juce::String(std::string(division->name));
    return freeAtZero ? ui::Ls("Libre", "Free") : juce::String("1/16");
}

juce::String semitones(const double value)
{
    const auto whole = static_cast<int>(std::lround(value));
    return (whole > 0 ? "+" : "") + juce::String(whole) + " st";
}

// The settings the engine reads, from the card's values.
template <std::size_t N>
engine::MidiEffectSettings midiSettings(std::vector<Param>& parameters, const Pane& pane, const char* typeKey)
{
    engine::MidiEffectSettings settings;
    if (const auto* effect = engine::effectForType(typeKey))
        settings.kind = effect->kind;
    const auto values = studioValues<N>(parameters, pane, typeKey);
    for (std::size_t index = 0; index < N && index < settings.values.size(); ++index)
        settings.values[index] = values[index];
    return settings;
}

juce::String arpModeName(const int index)
{
    return namedOf(midid::arpModes[static_cast<std::size_t>(std::clamp(index, 0, 8))]);
}
juce::String chordName(const int index)
{
    return namedOf(midid::chordShapes[static_cast<std::size_t>(std::clamp(index, 0, 11))].name);
}
juce::String scaleName(const int index)
{
    return namedOf(midid::scaleShapes[static_cast<std::size_t>(std::clamp(index, 0, 11))].name);
}
juce::String keyName(const int index)
{
    return juce::String(midid::keyNames[static_cast<std::size_t>(std::clamp(index, 0, 11))]);
}
juce::String noHint(int)
{
    return {};
}

// A keyboard from `low` for `keys` semitones; `lit(note)` gives each key's
// light (0 none … 1 full), `marked` one drawn as the root.
template <typename Lit>
void keyboard(ImDrawList* list, const Box& box, const int low, const int keys, Lit&& lit, const int marked, const ImU32 colour)
{
    const auto& p = theme::palette();
    auto whites = 0;
    for (int note = low; note < low + keys; ++note)
        whites += midid::keyNames[static_cast<std::size_t>(note % 12)][1] == '\0' ? 1 : 0;
    const auto width = box.width() / static_cast<float>(std::max(1, whites));
    auto x = box.min.x;
    std::vector<std::pair<int, float>> blacks;
    for (int note = low; note < low + keys; ++note) {
        if (midid::keyNames[static_cast<std::size_t>(note % 12)][1] != '\0') {
            blacks.emplace_back(note, x);
            continue;
        }
        const ImVec2 min(x + dp(0.5f), box.min.y);
        const ImVec2 max(x + width - dp(0.5f), box.max.y);
        const auto light = std::clamp(lit(note), 0.0f, 1.0f);
        list->AddRectFilled(min, max, light > 0.0f ? theme::mix(colour, IM_COL32(235, 235, 240, 255), 1.0f - light * 0.85f)
                                                   : IM_COL32(222, 224, 230, 255),
                            dp(2.0f));
        if (note == marked)
            list->AddCircleFilled(ImVec2((min.x + max.x) * 0.5f, max.y - dp(7.0f)), dp(3.0f), kDark);
        if (note % 12 == 0 && box.height() > dp(40.0f))
            centred(list, ImVec2((min.x + max.x) * 0.5f, max.y - dp(16.0f)), p.textFaint, midiNoteName(note).toRawUTF8(), 0.52f);
        x += width;
    }
    for (const auto& [note, at] : blacks) {
        const ImVec2 min(at - width * 0.32f, box.min.y);
        const ImVec2 max(at + width * 0.32f, box.min.y + box.height() * 0.6f);
        const auto light = std::clamp(lit(note), 0.0f, 1.0f);
        list->AddRectFilled(min, max, light > 0.0f ? theme::mix(colour, kDark, 1.0f - light * 0.9f) : IM_COL32(34, 36, 42, 255), dp(2.0f));
        if (note == marked)
            list->AddCircleFilled(ImVec2((min.x + max.x) * 0.5f, max.y - dp(6.0f)), dp(2.5f), IM_COL32(235, 235, 240, 255));
    }
}

// A note's bar on a lane: from `start` to `end` (0..1 of the lane), at a
// height of `row` among `rows` (0 the lowest), its velocity its brightness.
void noteBar(ImDrawList* list, const Box& lane, const float start, const float end, const float row, const float rows, const float velocity,
             const ImU32 colour)
{
    const auto rowHeight = lane.height() / std::max(1.0f, rows);
    const auto y = lane.max.y - (row + 1.0f) * rowHeight;
    const ImVec2 min(lane.min.x + std::clamp(start, 0.0f, 1.0f) * lane.width(), y + rowHeight * 0.12f);
    const ImVec2 max(lane.min.x + std::clamp(end, 0.0f, 1.0f) * lane.width(), y + rowHeight * 0.88f);
    if (max.x - min.x < 1.0f)
        return;
    list->AddRectFilled(min, max, theme::alpha(colour, 0.35f + 0.65f * std::clamp(velocity, 0.0f, 1.0f)), dp(2.0f));
    list->AddRect(min, max, theme::alpha(kDark, 0.6f), dp(2.0f));
}

void gridLines(ImDrawList* list, const Box& lane, const int columns, const int every)
{
    const auto& p = theme::palette();
    for (int column = 1; column < columns; ++column) {
        const auto x = lane.min.x + lane.width() * static_cast<float>(column) / static_cast<float>(columns);
        list->AddLine(ImVec2(x, lane.min.y), ImVec2(x, lane.max.y), column % every == 0 ? p.gridStrong : p.grid);
    }
}

// A display's line of information, at the top right (left of its chooser,
// `gap` wide, when it has one): its title and button are at the top left.
void infoLabel(ImDrawList* list, const Box& box, const char* value, const bool large, const float gap = 0.0f)
{
    const auto scale = large ? 0.72f : 0.58f;
    const auto size = textSize(value, scale);
    text(list, ImVec2(box.max.x - gap - size.x - dp(8.0f), box.min.y + dp(large ? 4.0f : 3.0f)), theme::palette().textDim, value, scale);
}

// Where a chooser at the top right begins, for that name.
float chooserLeft(const Box& box, const juce::String& name, const bool large)
{
    return box.max.x - textSize((name + "  ").toRawUTF8(), large ? 0.8f : 0.62f).x - dp(16.0f);
}

// The lane a display draws its notes in, under its title line.
Box laneOf(const Box& box, const bool large)
{
    return { ImVec2(box.min.x + dp(6.0f), box.min.y + dp(large ? 28.0f : 18.0f)), ImVec2(box.max.x - dp(6.0f), box.max.y - dp(6.0f)) };
}

// ---- Arpège -------------------------------------------------------------------------------------

bool arpegeDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Setter& set, const bool large)
{
    auto* list = ImGui::GetWindowDrawList();
    const auto settings = midiSettings<midid::arpegeParameterCount>(parameters, pane, "arpege");
    const auto& v = settings.values;
    // An example: a C major chord held for sixteen steps.
    const std::array<int, 3> held { 60, 64, 67 };
    std::array<int, 160> sequence {};
    const auto count = engine::midiArpeggioNotes(settings, held.data(), static_cast<int>(held.size()), sequence);
    const auto lane = laneOf(box, large);
    list->AddRectFilled(lane.min, lane.max, theme::alpha(kDark, 0.45f), dp(3.0f));
    constexpr int steps = 16;
    gridLines(list, lane, steps, 4);
    const auto octaves = whole(v[midid::arpegeOctaves] - 1.0f, 4) + 1;
    const auto lowest = 60;
    const auto rows = static_cast<float>(12 * octaves + 1);
    const auto gate = std::clamp(v[midid::arpegeGate], 0.05f, 2.0f);
    const auto swing = std::clamp(v[midid::arpegeSwing], 0.0f, 0.75f);
    const auto mode = whole(v[midid::arpegeMode], 9);
    for (int step = 0; step < steps && count > 0; ++step) {
        const auto late = step % 2 != 0 ? swing * 0.5f : 0.0f;
        const auto start = (static_cast<float>(step) + late) / static_cast<float>(steps);
        const auto end = start + gate / static_cast<float>(steps);
        const auto velocityMode = whole(v[midid::arpegeVelocityMode], 3);
        const auto velocity = velocityMode == 1 ? v[midid::arpegeVelocity] / 127.0f
                              : velocityMode == 2 ? (step % 4 == 0 ? 1.0f : 0.55f)
                                                  : 0.8f;
        if (mode == 6) {
            for (const auto note : held)
                noteBar(list, lane, start, end, static_cast<float>(note - lowest + 12 * (step % octaves)), rows, velocity, colour);
            continue;
        }
        // Random: a fixed example draw.
        const auto pick = mode == 5 ? static_cast<std::size_t>((step * 7 + 3) % count) : static_cast<std::size_t>(step % count);
        noteBar(list, lane, start, end, static_cast<float>(sequence[pick] - lowest), rows, velocity, colour);
    }
    char label[96];
    std::snprintf(label, sizeof(label), "%s  %s  %d oct", divisionName(v[midid::arpegeRate], false).toRawUTF8(),
                  ui::french() ? "pas" : "step", octaves);
    const auto left = chooserLeft(box, arpModeName(mode), large);
    infoLabel(list, box, label, large, box.max.x - left + dp(6.0f));
    return chooser("##arpMode", at(parameters, pane, midid::arpegeMode), 9, arpModeName, noHint, ImVec2(left, box.min.y + dp(2.0f)), colour,
                   set, large ? 0.8f : 0.62f);
}

// ---- Accords -------------------------------------------------------------------------------------

bool accordsDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Setter& set, const bool large)
{
    auto* list = ImGui::GetWindowDrawList();
    const auto settings = midiSettings<midid::accordsParameterCount>(parameters, pane, "accords");
    const auto& v = settings.values;
    std::array<int, 8> notes {};
    const auto root = 60;
    const auto count = engine::midiChordNotes(settings, root, notes);
    const auto lane = laneOf(box, large);
    const auto low = count > 0 ? std::min(48, notes[0] - notes[0] % 12) : 48;
    const auto high = count > 0 ? std::max(low + 24, notes[static_cast<std::size_t>(count - 1)] + 2) : low + 24;
    keyboard(list, lane, low, high - low,
             [&](const int note) {
                 for (int index = 0; index < count; ++index)
                     if (notes[static_cast<std::size_t>(index)] == note)
                         return note == root ? 1.0f : 0.25f + 0.75f * std::clamp(v[midid::accordsVelocity], 0.0f, 1.0f);
                 return 0.0f;
             },
             root, colour);
    juce::String names;
    for (int index = 0; index < count; ++index)
        names << (index > 0 ? " " : "") << midiNoteName(notes[static_cast<std::size_t>(index)]);
    const auto strum = v[midid::accordsStrum];
    if (strum > 0.5f)
        names << (ui::french() ? "   égrené " : "   strummed ") << juce::String(juce::roundToInt(strum)) << " ms";
    const auto type = whole(v[midid::accordsChord], 12);
    const auto left = chooserLeft(box, chordName(type), large);
    if (large)
        infoLabel(list, box, names.toRawUTF8(), large, box.max.x - left + dp(6.0f));
    return chooser("##chord", at(parameters, pane, midid::accordsChord), 12, chordName, noHint, ImVec2(left, box.min.y + dp(2.0f)), colour,
                   set, large ? 0.8f : 0.62f);
}

// ---- Gamme ---------------------------------------------------------------------------------------

bool gammeDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Setter& set, const bool large)
{
    auto* list = ImGui::GetWindowDrawList();
    const auto& p = theme::palette();
    const auto values = studioValues<midid::gammeParameterCount>(parameters, pane, "gamme");
    const auto key = whole(values[midid::gammeKey], 12);
    const auto scale = whole(values[midid::gammeScale], 12);
    const auto mode = whole(values[midid::gammeMode], 4);
    const auto lane = laneOf(box, large);
    const auto keysBox = Box { lane.min, ImVec2(lane.max.x, lane.min.y + lane.height() * (large ? 0.62f : 0.7f)) };
    const auto low = 60 + key - (key > 6 ? 12 : 0);
    keyboard(list, keysBox, low - (low % 12), 24, [&](const int note) { return midid::inScale(note, key, scale) ? 0.85f : 0.0f; },
             low, colour);
    // Where each note outside it goes.
    if (scale > 0) {
        const auto first = low - low % 12;
        const auto columns = 14.0f;
        auto white = 0;
        for (int note = first; note < first + 24 && large; ++note) {
            const auto black = midid::keyNames[static_cast<std::size_t>(note % 12)][1] != '\0';
            if (!black)
                ++white;
            if (midid::inScale(note, key, scale))
                continue;
            const auto target = engine::midiScaleNote(note, key, scale, mode, 0, 0);
            const auto x = keysBox.min.x + (static_cast<float>(white) - (black ? 0.0f : 0.5f)) * keysBox.width() / columns;
            const auto y = keysBox.max.y + dp(10.0f);
            const auto label = target < 0 ? juce::String::fromUTF8("\xc3\x97") : (target > note ? juce::String::fromUTF8("\xe2\x86\x92") : juce::String::fromUTF8("\xe2\x86\x90"));
            centred(list, ImVec2(x, y), target < 0 ? p.textFaint : colour, label.toRawUTF8(), 0.6f);
        }
    }
    const auto left = chooserLeft(box, scaleName(scale), large);
    infoLabel(list, box, keyName(key).toRawUTF8(), large, box.max.x - left + dp(6.0f));
    const auto transpose = whole(values[midid::gammeTranspose] + 24.0f, 49) - 24;
    const auto degrees = whole(values[midid::gammeDegrees] + 7.0f, 15) - 7;
    if (transpose != 0 || degrees != 0) {
        const auto moved = (transpose != 0 ? semitones(transpose) : juce::String()) + (transpose != 0 && degrees != 0 ? "  " : "")
                           + (degrees != 0 ? (degrees > 0 ? "+" : "") + juce::String(degrees) + ui::Ls(" degrés", " degrees") : juce::String());
        text(list, ImVec2(lane.min.x, lane.max.y - ImGui::GetFontSize() * 0.75f), colour, moved.toRawUTF8(), large ? 0.7f : 0.56f);
    }
    return chooser("##scale", at(parameters, pane, midid::gammeScale), 12, scaleName, noHint, ImVec2(left, box.min.y + dp(2.0f)), colour,
                   set, large ? 0.8f : 0.62f);
}

// ---- Écho de notes -------------------------------------------------------------------------------

bool echoNotesDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live, const bool large)
{
    auto* list = ImGui::GetWindowDrawList();
    const auto& p = theme::palette();
    const auto values = studioValues<midid::echoNotesParameterCount>(parameters, pane, "echo-notes");
    const auto lane = laneOf(box, large);
    list->AddRectFilled(lane.min, lane.max, theme::alpha(kDark, 0.45f), dp(3.0f));
    // Two bars long at the tempo: the note and its echoes.
    const auto beats = 8.0;
    const auto* division = engine::noteDivisionFor(values[midid::echoNotesSync]);
    const auto spacingBeats = division != nullptr ? division->beats : values[midid::echoNotesTime] * 0.001 * live.tempo / 60.0;
    gridLines(list, lane, 8, 4);
    const auto repeats = whole(values[midid::echoNotesRepeats] - 1.0f, 16) + 1;
    const auto pitch = whole(values[midid::echoNotesPitch] + 12.0f, 25) - 12;
    const auto decay = std::clamp(values[midid::echoNotesDecay], 0.0f, 1.0f);
    const auto rows = static_cast<float>(std::max(12, std::abs(pitch) * repeats + 1));
    const auto base = pitch < 0 ? rows - 1.0f : 0.0f;
    auto velocity = 1.0f;
    for (int repeat = 0; repeat <= repeats; ++repeat) {
        const auto start = static_cast<float>(repeat * spacingBeats / beats);
        if (start >= 1.0f || velocity < 0.01f)
            break;
        noteBar(list, lane, start, start + static_cast<float>(spacingBeats * 0.5 / beats), base + static_cast<float>(pitch * repeat), rows,
                velocity, repeat == 0 ? p.text : colour);
        velocity *= 1.0f - decay;
    }
    const auto label = (division != nullptr ? juce::String(std::string(division->name)) : juce::String(juce::roundToInt(values[midid::echoNotesTime])) + " ms")
                       + juce::String::fromUTF8("  \xc3\x97") + juce::String(repeats);
    infoLabel(list, box, label.toRawUTF8(), large);
    return false;
}

// ---- Aléa ----------------------------------------------------------------------------------------

bool aleaDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const bool large)
{
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<midid::aleaParameterCount>(parameters, pane, "alea");
    const auto lane = laneOf(box, large);
    list->AddRectFilled(lane.min, lane.max, theme::alpha(kDark, 0.45f), dp(3.0f));
    gridLines(list, lane, 8, 4);
    // Eight notes as written, and the room each may wander in.
    const auto timing = std::clamp(values[midid::aleaTiming], 0.0f, 50.0f) / 250.0f; // of an eighth at 120
    const auto spread = std::clamp(values[midid::aleaVelocity], 0.0f, 1.0f);
    const auto chance = std::clamp(values[midid::aleaChance], 0.0f, 1.0f);
    const auto range = whole(values[midid::aleaPitchRange], 13);
    const auto pitchChance = std::clamp(values[midid::aleaPitchChance], 0.0f, 1.0f);
    const auto rows = 12.0f + 2.0f * static_cast<float>(range);
    constexpr std::array<int, 8> melody { 0, 2, 4, 5, 7, 5, 4, 2 };
    for (std::size_t index = 0; index < melody.size(); ++index) {
        const auto start = static_cast<float>(index) / 8.0f;
        const auto row = static_cast<float>(range + melody[index]);
        // The window it may move in, then the note.
        const auto rowHeight = lane.height() / rows;
        const auto y = lane.max.y - (row + 1.0f) * rowHeight;
        list->AddRectFilled(ImVec2(lane.min.x + start * lane.width(), y - rowHeight * spread * 2.0f),
                            ImVec2(lane.min.x + (start + 0.06f + timing * 0.125f) * lane.width(), y + rowHeight * (1.0f + spread * 2.0f)),
                            theme::alpha(colour, 0.12f), dp(2.0f));
        noteBar(list, lane, start, start + 0.06f, row, rows, chance, colour);
        if (range > 0 && pitchChance > 0.0f) {
            noteBar(list, lane, start, start + 0.06f, row + static_cast<float>(range), rows, pitchChance * 0.4f, kMidiGhost);
            noteBar(list, lane, start, start + 0.06f, row - static_cast<float>(range), rows, pitchChance * 0.4f, kMidiGhost);
        }
    }
    char label[96];
    std::snprintf(label, sizeof(label), "%s %d %%", ui::french() ? "joue" : "plays", juce::roundToInt(chance * 100.0f));
    infoLabel(list, box, label, large);
    return false;
}

// ---- Durée ---------------------------------------------------------------------------------------

bool dureeDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live, const Setter& set,
                  const bool large)
{
    auto* list = ImGui::GetWindowDrawList();
    const auto& p = theme::palette();
    const auto values = studioValues<midid::dureeParameterCount>(parameters, pane, "duree");
    const auto lane = laneOf(box, large);
    list->AddRectFilled(lane.min, lane.max, theme::alpha(kDark, 0.45f), dp(3.0f));
    gridLines(list, lane, 8, 4);
    const auto mode = whole(values[midid::dureeMode], 4);
    // Four notes as played (short, long, short, medium) and as they come out,
    // over two bars.
    constexpr std::array<float, 4> starts { 0.0f, 0.25f, 0.5f, 0.75f };
    constexpr std::array<float, 4> played { 0.04f, 0.2f, 0.02f, 0.1f };
    const auto barSeconds = 4.0 * 60.0 / std::max(20.0, live.tempo);
    const auto twoBars = static_cast<float>(barSeconds * 2.0);
    const auto fixed = static_cast<float>(values[midid::dureeLength] * 0.001 / twoBars);
    const auto* division = engine::noteDivisionFor(values[midid::dureeSync]);
    const auto synced = division != nullptr ? static_cast<float>(division->beats / 8.0) : 0.0625f;
    const auto factor = std::clamp(values[midid::dureeFactor], 1.0f, 4.0f);
    const auto top = Box { lane.min, ImVec2(lane.max.x, lane.min.y + lane.height() * 0.42f) };
    const auto bottom = Box { ImVec2(lane.min.x, lane.min.y + lane.height() * 0.55f), lane.max };
    for (std::size_t index = 0; index < starts.size(); ++index) {
        noteBar(list, top, starts[index], starts[index] + played[index], 0.0f, 1.0f, 0.5f, p.textDim);
        const auto next = index + 1 < starts.size() ? starts[index + 1] : 1.0f;
        const auto length = mode == 0 ? fixed : mode == 1 ? synced : mode == 2 ? played[index] * factor : next - starts[index];
        noteBar(list, bottom, starts[index], starts[index] + length, 0.0f, 1.0f, 0.9f, colour);
    }
    if (large) {
        section(list, ImVec2(top.min.x + dp(2.0f), top.min.y - dp(1.0f)), L("JOUÉES", "PLAYED"));
        section(list, ImVec2(bottom.min.x + dp(2.0f), bottom.min.y - dp(1.0f)), L("SORTIE", "OUT"));
    }
    std::vector<const char*> modes;
    for (const auto& named : midid::lengthModes)
        modes.push_back(ui::french() ? named.french : named.english);
    auto changed = false;
    if (large)
        changed |= segmented("##lengthMode", at(parameters, pane, midid::dureeMode), modes,
                             ImVec2(box.max.x - dp(420.0f), box.min.y + dp(3.0f)), dp(414.0f), dp(20.0f), colour, set);
    else
        infoLabel(list, box, modes[static_cast<std::size_t>(mode)], false);
    return changed;
}

// ---- Séquenceur ----------------------------------------------------------------------------------

// Its sixteen steps, each on or off (a click on its square), its note (drag
// up or down on its column), its velocity and its length (drag on their
// lanes, in the window).
bool sequenceurDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Setter& set, const bool large)
{
    auto* list = ImGui::GetWindowDrawList();
    const auto& p = theme::palette();
    const auto values = studioValues<midid::sequenceurParameterCount>(parameters, pane, "sequenceur");
    const auto steps = whole(values[midid::sequenceurSteps] - 1.0f, 16) + 1;
    const auto lane = laneOf(box, large);
    const auto toggles = large ? dp(20.0f) : dp(12.0f);
    const auto lanes = large ? 3 : 1;
    const auto gap = dp(6.0f);
    const auto noteHeight = large ? (lane.height() - toggles - gap * 3.0f) * 0.56f : lane.height() - toggles - gap;
    const auto barHeight = large ? (lane.height() - toggles - gap * 3.0f - noteHeight) * 0.5f : 0.0f;
    const Box notes { ImVec2(lane.min.x, lane.min.y + toggles + gap), ImVec2(lane.max.x, lane.min.y + toggles + gap + noteHeight) };
    const auto column = lane.width() / 16.0f;
    auto changed = false;
    auto& drag = dragState(parameters, pane);
    list->AddRectFilled(notes.min, notes.max, theme::alpha(kDark, 0.45f), dp(3.0f));
    // The middle line: the key itself (offset 0); a line an octave each way.
    const auto yOfOffset = [&](const float offset) { return notes.min.y + (0.5f - offset / 48.0f) * notes.height(); };
    list->AddLine(ImVec2(notes.min.x, yOfOffset(0.0f)), ImVec2(notes.max.x, yOfOffset(0.0f)), p.gridStrong);
    for (const auto octave : { -12.0f, 12.0f })
        list->AddLine(ImVec2(notes.min.x, yOfOffset(octave)), ImVec2(notes.max.x, yOfOffset(octave)), p.grid);
    for (int step = 0; step < 16; ++step) {
        const auto x0 = lane.min.x + column * static_cast<float>(step);
        const auto inside = step < steps;
        const auto on = values[static_cast<std::size_t>(midid::sequencerStep(step, 0))] >= 0.5f;
        const auto offset = values[static_cast<std::size_t>(midid::sequencerStep(step, 1))];
        const auto velocity = values[static_cast<std::size_t>(midid::sequencerStep(step, 2))] / 127.0f;
        const auto stepLength = values[static_cast<std::size_t>(midid::sequencerStep(step, 3))];
        const auto shade = inside ? 1.0f : 0.3f;
        // Its square.
        const ImVec2 squareMin(x0 + dp(2.0f), lane.min.y);
        const ImVec2 squareMax(x0 + column - dp(2.0f), lane.min.y + toggles);
        list->AddRectFilled(squareMin, squareMax, on ? theme::alpha(colour, shade) : theme::alpha(p.bg3, shade), dp(2.0f));
        if (step % 4 == 0)
            list->AddRect(squareMin, squareMax, theme::alpha(p.textFaint, 0.6f * shade), dp(2.0f));
        ImGui::SetCursorScreenPos(squareMin);
        ImGui::PushID(step);
        if (ImGui::InvisibleButton("##on", ImVec2(std::max(1.0f, squareMax.x - squareMin.x), toggles)))
            changed |= apply(at(parameters, pane, midid::sequencerStep(step, 0)), on ? 0.0 : 1.0, set);
        // Its note.
        const auto y = yOfOffset(offset);
        const auto barWidth = (column - dp(4.0f)) * std::clamp(stepLength, 0.1f, 1.0f);
        list->AddRectFilled(ImVec2(x0 + dp(2.0f), y - dp(3.0f)), ImVec2(x0 + dp(2.0f) + barWidth, y + dp(3.0f)),
                            on ? theme::alpha(colour, (0.4f + 0.6f * velocity) * shade) : theme::alpha(p.textFaint, 0.35f * shade), dp(2.0f));
        ImGui::SetCursorScreenPos(ImVec2(x0, notes.min.y));
        ImGui::InvisibleButton("##note", ImVec2(std::max(1.0f, column), notes.height()));
        if (ImGui::IsItemActive()) {
            const auto semitone = std::round((0.5f - (ImGui::GetIO().MousePos.y - notes.min.y) / notes.height()) * 48.0f);
            changed |= apply(at(parameters, pane, midid::sequencerStep(step, 1)), std::clamp(semitone, -24.0f, 24.0f), set);
            drag.active = step;
        }
        if (ImGui::IsItemHovered() || drag.active == step) {
            const auto shown = semitones(offset);
            ImGui::SetTooltip("%s %d  %s", ui::french() ? "Pas" : "Step", step + 1, shown.toRawUTF8());
        }
        // Velocity and length, in the window.
        for (int laneIndex = 1; laneIndex < lanes; ++laneIndex) {
            const auto top = notes.max.y + gap + static_cast<float>(laneIndex - 1) * (barHeight + gap);
            const Box bar { ImVec2(x0 + dp(2.0f), top), ImVec2(x0 + column - dp(2.0f), top + barHeight) };
            const auto amount = laneIndex == 1 ? velocity : std::clamp(stepLength, 0.1f, 1.0f);
            list->AddRectFilled(bar.min, bar.max, theme::alpha(p.bg2, shade), dp(2.0f));
            list->AddRectFilled(ImVec2(bar.min.x, bar.max.y - bar.height() * amount), bar.max,
                                theme::alpha(laneIndex == 1 ? colour : theme::mix(colour, p.text, 0.4f), (on ? 0.85f : 0.3f) * shade), dp(2.0f));
            ImGui::SetCursorScreenPos(bar.min);
            ImGui::PushID(laneIndex);
            ImGui::InvisibleButton("##bar", ImVec2(std::max(1.0f, bar.width()), std::max(1.0f, bar.height())));
            if (ImGui::IsItemActive()) {
                const auto share = std::clamp((bar.max.y - ImGui::GetIO().MousePos.y) / std::max(1.0f, bar.height()), 0.0f, 1.0f);
                changed |= laneIndex == 1 ? apply(at(parameters, pane, midid::sequencerStep(step, 2)), std::round(1.0f + share * 126.0f), set)
                                          : apply(at(parameters, pane, midid::sequencerStep(step, 3)), std::max(0.1f, share), set);
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        drag.active = -1;
    if (large) {
        section(list, ImVec2(lane.min.x + dp(2.0f), notes.max.y + gap - dp(11.0f)), L("VÉLOCITÉ", "VELOCITY"));
        section(list, ImVec2(lane.min.x + dp(2.0f), notes.max.y + gap * 2.0f + barHeight - dp(11.0f)), L("LONGUEUR", "LENGTH"));
    }
    char label[96];
    std::snprintf(label, sizeof(label), "%d %s  %s", steps, ui::french() ? "pas" : "steps",
                  divisionName(values[midid::sequenceurRate], false).toRawUTF8());
    infoLabel(list, box, label, large);
    return changed;
}

// ---- Roulement -----------------------------------------------------------------------------------

bool roulementDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live, const bool large)
{
    auto* list = ImGui::GetWindowDrawList();
    const auto& p = theme::palette();
    const auto values = studioValues<midid::roulementParameterCount>(parameters, pane, "roulement");
    const auto lane = laneOf(box, large);
    list->AddRectFilled(lane.min, lane.max, theme::alpha(kDark, 0.45f), dp(3.0f));
    gridLines(list, lane, 4, 4);
    // A note held for a beat and a half of a bar of four: its hits.
    const auto barSeconds = 4.0 * 60.0 / std::max(20.0, live.tempo);
    const auto* division = engine::noteDivisionFor(values[midid::roulementSync]);
    const auto interval = division != nullptr ? division->beats / 4.0 : 1.0 / std::max(1.0f, values[midid::roulementRate]) / barSeconds;
    const auto gate = std::clamp(values[midid::roulementGate], 0.1f, 1.0f);
    const auto ramp = std::clamp(values[midid::roulementRamp], -1.0f, 1.0f);
    const auto held = 0.875;
    list->AddRectFilled(ImVec2(lane.min.x, lane.max.y - dp(4.0f)), ImVec2(lane.min.x + static_cast<float>(held) * lane.width(), lane.max.y),
                        theme::alpha(p.textDim, 0.6f), dp(1.0f));
    auto hits = 0;
    for (auto start = 0.0; start < held && hits < 256; start += interval, ++hits) {
        const auto velocity = std::clamp(1.0f + ramp * std::min(1.0f, static_cast<float>(hits) / 8.0f), 0.1f, 1.6f) / 1.6f;
        const auto x0 = lane.min.x + static_cast<float>(start) * lane.width();
        const auto x1 = x0 + std::max(1.0f, static_cast<float>(interval * gate) * lane.width());
        const auto top = lane.max.y - dp(6.0f) - (lane.height() - dp(10.0f)) * velocity;
        list->AddRectFilled(ImVec2(x0, top), ImVec2(x1, lane.max.y - dp(6.0f)), theme::alpha(colour, 0.85f), dp(1.5f));
    }
    const auto label = division != nullptr ? juce::String(std::string(division->name))
                                            : juce::String(values[midid::roulementRate], 1) + " Hz";
    infoLabel(list, box, label.toRawUTF8(), large);
    return false;
}

// ---- The five entry points ------------------------------------------------------------------------

bool midifxDisplay(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour,
                   const Live& live, const Setter& set, const bool large)
{
    switch (kind) {
    case PaneKind::arpege: return arpegeDisplay(parameters, pane, box, colour, set, large);
    case PaneKind::accords: return accordsDisplay(parameters, pane, box, colour, set, large);
    case PaneKind::gamme: return gammeDisplay(parameters, pane, box, colour, set, large);
    case PaneKind::echoNotes: return echoNotesDisplay(parameters, pane, box, colour, live, large);
    case PaneKind::alea: return aleaDisplay(parameters, pane, box, colour, large);
    case PaneKind::duree: return dureeDisplay(parameters, pane, box, colour, live, set, large);
    case PaneKind::sequenceur: return sequenceurDisplay(parameters, pane, box, colour, set, large);
    case PaneKind::roulement: return roulementDisplay(parameters, pane, box, colour, live, large);
    default: return false;
    }
}

bool midifxLarge(const PaneKind kind, const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size,
                 const ImU32 colour, const Live& live, const Setter& set)
{
    const auto display = [&](const Box& box) { return midifxDisplay(kind, parameters, pane, box, colour, live, set, true); };
    switch (kind) {
    case PaneKind::arpege:
        return addedLarge(pane, parameters, position, size, colour, live, set, L("ARPÈGE  ·  les notes tenues, une à une", "ARPEGGIATOR  ·  the held notes, one by one"),
                          { { L("MOTIF", "PATTERN"), { midid::arpegeMode, midid::arpegeOctaves, midid::arpegeRate, midid::arpegeSwing } },
                            { L("NOTES", "NOTES"), { midid::arpegeGate, midid::arpegeVelocityMode, midid::arpegeVelocity } },
                            { L("JEU", "PLAYING"), { midid::arpegeLatch, midid::arpegeRetrigger } } },
                          display);
    case PaneKind::accords:
        return addedLarge(pane, parameters, position, size, colour, live, set, L("ACCORDS  ·  une note devient un accord", "CHORDS  ·  a note becomes a chord"),
                          { { L("ACCORD", "CHORD"), { midid::accordsChord, midid::accordsInversion, midid::accordsSpread, midid::accordsVelocity } },
                            { L("INTERVALLES (PERSONNALISÉ)", "INTERVALS (CUSTOM)"),
                              { midid::accordsInterval1, midid::accordsInterval2, midid::accordsInterval3, midid::accordsInterval4,
                                midid::accordsInterval5, midid::accordsInterval6 } },
                            { L("ÉGRENER", "STRUM"), { midid::accordsStrum, midid::accordsStrumMode } },
                            { L("SUIVRE UNE GAMME", "FOLLOW A SCALE"), { midid::accordsKey, midid::accordsScale } } },
                          display);
    case PaneKind::gamme:
        return addedLarge(pane, parameters, position, size, colour, live, set, L("GAMME  ·  les notes dans la tonalité", "SCALE  ·  the notes in the key"),
                          { { L("GAMME", "SCALE"), { midid::gammeKey, midid::gammeScale, midid::gammeMode } },
                            { L("TRANSPOSITION", "TRANSPOSE"), { midid::gammeTranspose, midid::gammeDegrees } } },
                          display);
    case PaneKind::echoNotes:
        return addedLarge(pane, parameters, position, size, colour, live, set, L("ÉCHO DE NOTES  ·  chaque note répétée", "NOTE ECHO  ·  each note repeated"),
                          { { L("ÉCHOS", "ECHOES"), { midid::echoNotesSync, midid::echoNotesTime, midid::echoNotesRepeats } },
                            { L("CHAQUE ÉCHO", "EACH ECHO"), { midid::echoNotesDecay, midid::echoNotesPitch } } },
                          display);
    case PaneKind::alea:
        return addedLarge(pane, parameters, position, size, colour, live, set, L("ALÉA  ·  un jeu humain ou imprévisible", "RANDOM  ·  human or unpredictable playing"),
                          { { L("TEMPS ET FORCE", "TIME AND STRENGTH"), { midid::aleaTiming, midid::aleaVelocity } },
                            { L("NOTES", "NOTES"), { midid::aleaChance, midid::aleaPitchChance, midid::aleaPitchRange } } },
                          display);
    case PaneKind::duree:
        return addedLarge(pane, parameters, position, size, colour, live, set, L("DURÉE  ·  la longueur des notes", "NOTE LENGTH  ·  how long the notes last"),
                          { { L("LONGUEUR", "LENGTH"), { midid::dureeMode, midid::dureeLength, midid::dureeSync, midid::dureeFactor } } }, display);
    case PaneKind::sequenceur:
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("SÉQUENCEUR  ·  seize pas, chacun sa note", "STEP SEQUENCER  ·  sixteen steps, each its note"),
                          { { L("LECTURE", "PLAYBACK"),
                              { midid::sequenceurMode, midid::sequenceurRate, midid::sequenceurSteps, midid::sequenceurDirection,
                                midid::sequenceurSwing, midid::sequenceurGate, midid::sequenceurRoot } } },
                          display, 300.0f);
    case PaneKind::roulement:
        return addedLarge(pane, parameters, position, size, colour, live, set, L("ROULEMENT  ·  une note répétée tant qu'elle est tenue", "NOTE REPEAT  ·  a note repeated while held"),
                          { { L("RÉPÉTITION", "REPEAT"), { midid::roulementSync, midid::roulementRate, midid::roulementGate, midid::roulementRamp } } },
                          display);
    default: return false;
    }
}

std::vector<int> midifxCardFields(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane)
{
    juce::ignoreUnused(parameters, pane);
    switch (kind) {
    case PaneKind::arpege: return { midid::arpegeRate, midid::arpegeOctaves, midid::arpegeGate, midid::arpegeSwing };
    case PaneKind::accords: return { midid::accordsInversion, midid::accordsStrum, midid::accordsVelocity };
    case PaneKind::gamme: return { midid::gammeKey, midid::gammeMode, midid::gammeTranspose };
    case PaneKind::echoNotes: return { midid::echoNotesSync, midid::echoNotesRepeats, midid::echoNotesDecay, midid::echoNotesPitch };
    case PaneKind::alea: return { midid::aleaTiming, midid::aleaVelocity, midid::aleaChance, midid::aleaPitchChance };
    case PaneKind::duree: return { midid::dureeMode, midid::dureeLength, midid::dureeSync, midid::dureeFactor };
    case PaneKind::sequenceur: return { midid::sequenceurRate, midid::sequenceurSteps, midid::sequenceurMode, midid::sequenceurGate };
    case PaneKind::roulement: return { midid::roulementSync, midid::roulementRate, midid::roulementGate, midid::roulementRamp };
    default: return {};
    }
}

juce::String midifxFormat(const juce::String& binding, const double value)
{
    const auto suffixOf = [&binding](const char* prefix) { return binding.fromLastOccurrenceOf(juce::String(prefix) + ".", false, false); };
    const auto index = static_cast<int>(std::lround(value));
    const auto onOffText = [](const double v) { return v >= 0.5 ? ui::Ls("Actif", "On") : ui::Ls("Coupé", "Off"); };
    if (binding.contains("arpege.")) {
        const auto suffix = suffixOf("arpege");
        if (suffix == "rate")
            return divisionName(value, false);
        if (suffix == "mode")
            return arpModeName(index);
        if (suffix == "octaves")
            return juce::String(std::clamp(index, 1, 4)) + " oct";
        if (suffix == "velocityMode")
            return namedOf(midid::arpVelocities[static_cast<std::size_t>(std::clamp(index, 0, 2))]);
        if (suffix == "latch" || suffix == "retrigger")
            return onOffText(value);
        if (suffix == "gate" || suffix == "swing")
            return percent(value);
        if (suffix == "velocity")
            return juce::String(index);
        return {};
    }
    if (binding.contains("accords.")) {
        const auto suffix = suffixOf("accords");
        if (suffix == "chord")
            return chordName(index);
        if (suffix.startsWith("interval"))
            return index == 0 ? juce::String::fromUTF8("\xe2\x80\x94") : semitones(value);
        if (suffix == "inversion")
            return (index > 0 ? "+" : "") + juce::String(index);
        if (suffix == "spread")
            return value >= 0.5 ? ui::Ls("Ouvert", "Open") : ui::Ls("Serré", "Close");
        if (suffix == "strumMode")
            return namedOf(midid::strumDirections[static_cast<std::size_t>(std::clamp(index, 0, 2))]);
        if (suffix == "key")
            return keyName(index);
        if (suffix == "scale")
            return index == 0 ? ui::Ls("Non (intervalles)", "No (intervals)") : scaleName(index);
        if (suffix == "velocity")
            return percent(value);
        return {};
    }
    if (binding.contains("gamme.")) {
        const auto suffix = suffixOf("gamme");
        if (suffix == "key")
            return keyName(index);
        if (suffix == "scale")
            return scaleName(index);
        if (suffix == "mode")
            return namedOf(midid::scaleModes[static_cast<std::size_t>(std::clamp(index, 0, 3))]);
        if (suffix == "transpose")
            return semitones(value);
        if (suffix == "degrees")
            return (index > 0 ? "+" : "") + juce::String(index) + ui::Ls(" degrés", " degrees");
        return {};
    }
    if (binding.contains("echonotes.")) {
        const auto suffix = suffixOf("echonotes");
        if (suffix == "sync")
            return divisionName(value, true);
        if (suffix == "repeats")
            return juce::String::fromUTF8("\xc3\x97") + juce::String(index);
        if (suffix == "decay")
            return percent(value);
        if (suffix == "pitch")
            return semitones(value);
        return {};
    }
    if (binding.contains("alea.")) {
        const auto suffix = suffixOf("alea");
        if (suffix == "velocity" || suffix == "chance" || suffix == "pitchChance")
            return percent(value);
        if (suffix == "pitchRange")
            return juce::String::fromUTF8("\xc2\xb1") + juce::String(index) + " st";
        return {};
    }
    if (binding.contains("duree.")) {
        const auto suffix = suffixOf("duree");
        if (suffix == "mode")
            return namedOf(midid::lengthModes[static_cast<std::size_t>(std::clamp(index, 0, 3))]);
        if (suffix == "sync")
            return divisionName(value, false);
        if (suffix == "factor")
            return juce::String::fromUTF8("\xc3\x97") + juce::String(value, 2);
        return {};
    }
    if (binding.contains("sequenceur.")) {
        const auto suffix = suffixOf("sequenceur");
        if (suffix == "mode")
            return namedOf(midid::sequencerModes[static_cast<std::size_t>(std::clamp(index, 0, 1))]);
        if (suffix == "rate")
            return divisionName(value, false);
        if (suffix == "direction")
            return namedOf(midid::sequencerDirections[static_cast<std::size_t>(std::clamp(index, 0, 3))]);
        if (suffix == "root")
            return midiNoteName(index);
        if (suffix == "steps")
            return juce::String(index);
        if (suffix == "swing" || suffix == "gate")
            return percent(value);
        if (suffix.endsWith("On"))
            return onOffText(value);
        if (suffix.endsWith("Note"))
            return semitones(value);
        if (suffix.endsWith("Velocity"))
            return juce::String(index);
        if (suffix.endsWith("Length"))
            return percent(value);
        return {};
    }
    if (binding.contains("roulement.")) {
        const auto suffix = suffixOf("roulement");
        if (suffix == "sync")
            return divisionName(value, true);
        if (suffix == "gate")
            return percent(value);
        if (suffix == "ramp")
            return (value > 0.0 ? "+" : "") + percent(value);
        return {};
    }
    return {};
}

bool midifxStepped(const juce::String& binding)
{
    static const char* stepped[] { "arpege.rate", "arpege.mode", "arpege.octaves", "arpege.velocityMode", "arpege.latch", "arpege.retrigger",
                                   "accords.chord", "accords.strumMode", "accords.key", "accords.scale", "gamme.key", "gamme.scale",
                                   "gamme.mode", "echonotes.sync", "duree.mode", "duree.sync", "sequenceur.mode", "sequenceur.rate",
                                   "sequenceur.direction", "roulement.sync" };
    for (const auto* suffix : stepped)
        if (binding.endsWith(suffix))
            return true;
    return binding.contains("sequenceur.step") && binding.endsWith("On");
}
