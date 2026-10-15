// The suites' displays and large views: Tranche and Finition (SuiteDesign.h
// computes what they draw), with their assistants. Included by
// StudioEffectsView.cpp inside its anonymous namespace, after
// MixEffectsPanes.inc (whose helpers they use).

namespace suited = engine::suite;

// ---- What the assistants hear ---

// The level (dB) of each octave, 31.5 Hz to 16 kHz, in a tapped frame.
constexpr int kOctaves = 10;
constexpr std::array<double, kOctaves> kOctaveHz { 31.5, 63.0, 125.0, 250.0, 500.0, 1'000.0, 2'000.0, 4'000.0, 8'000.0, 16'000.0 };

std::array<double, kOctaves> octaveLevels(const engine::AudioAnalysisFrame& frame)
{
    static engine::Fft fft;
    static std::vector<std::complex<float>> spectrum;
    static std::vector<float> window;
    constexpr auto size = static_cast<std::size_t>(engine::AudioAnalysisFrame::waveformCapacity);
    if (fft.size() != size) {
        fft.prepare(size);
        spectrum.resize(size);
        window.resize(size);
        for (std::size_t n = 0; n < size; ++n)
            window[n] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / static_cast<double>(size)));
    }
    for (std::size_t n = 0; n < size; ++n)
        spectrum[n] = { frame.waveform[n] * window[n], 0.0f };
    fft.forward(spectrum.data());
    const auto binHz = std::max(8'000.0, static_cast<double>(frame.sampleRate)) / static_cast<double>(size);
    std::array<double, kOctaves> levels {};
    for (int octave = 0; octave < kOctaves; ++octave) {
        const auto low = kOctaveHz[static_cast<std::size_t>(octave)] / std::numbers::sqrt2;
        const auto high = kOctaveHz[static_cast<std::size_t>(octave)] * std::numbers::sqrt2;
        auto power = 0.0;
        for (auto k = std::max<std::size_t>(1, static_cast<std::size_t>(low / binHz)); k <= std::min(size / 2, static_cast<std::size_t>(high / binHz));
             ++k)
            power += std::norm(spectrum[k]);
        levels[static_cast<std::size_t>(octave)] = 10.0 * std::log10(power + 1.0e-20);
    }
    return levels;
}

// An assistant's listening: how long, what it heard, what it concluded.
struct Assistant {
    bool listening {};
    double heard {};
    double last {};
    std::uint64_t sequence {};
    int frames {};
    std::array<double, kOctaves> energy {};
    double power {};
    double peak {};
    Loudness loudness;
    juce::String verdict;
};
Assistant& assistantOf(std::vector<Param>& parameters, const Pane& pane)
{
    static std::map<juce::String, Assistant> assistants;
    const auto* first = at(parameters, pane, 0);
    return assistants[first != nullptr ? first->binding : juce::String("?")];
}

// What the assistant hears while it listens; true once it has heard enough.
bool listen(Assistant& assistant, const engine::AudioAnalysisFrame* frame, const Live& live, const double seconds)
{
    if (!assistant.listening)
        return false;
    // The time it listens is the time the music plays, however often its
    // frames come.
    const auto now = ImGui::GetTime();
    if (live.running && assistant.last > 0.0)
        assistant.heard += std::min(0.25, now - assistant.last);
    assistant.last = now;
    if (frame == nullptr || !live.running || frame->sequence == assistant.sequence)
        return assistant.heard >= seconds && assistant.frames > 0;
    assistant.sequence = frame->sequence;
    const auto levels = octaveLevels(*frame);
    for (int octave = 0; octave < kOctaves; ++octave)
        assistant.energy[static_cast<std::size_t>(octave)] += std::pow(10.0, levels[static_cast<std::size_t>(octave)] / 10.0);
    auto sum = 0.0;
    for (std::uint32_t i = 0; i < frame->waveformCount; ++i) {
        sum += static_cast<double>(frame->waveform[i]) * frame->waveform[i];
        assistant.peak = std::max(assistant.peak, static_cast<double>(std::abs(frame->waveform[i])));
    }
    assistant.power += sum / std::max<std::uint32_t>(1, frame->waveformCount);
    assistant.loudness.feed(frame, now);
    ++assistant.frames;
    return assistant.heard >= seconds;
}

// How far each octave stands from a balanced mix's slope (-4.5 dB an octave),
// the average put at 0.
std::array<double, kOctaves> balance(const Assistant& assistant)
{
    std::array<double, kOctaves> deviation {};
    auto mean = 0.0;
    for (int octave = 0; octave < kOctaves; ++octave) {
        deviation[static_cast<std::size_t>(octave)]
            = 10.0 * std::log10(assistant.energy[static_cast<std::size_t>(octave)] / std::max(1, assistant.frames) + 1.0e-20) + 4.5 * octave;
        mean += deviation[static_cast<std::size_t>(octave)];
    }
    mean /= kOctaves;
    for (auto& value : deviation)
        value -= mean;
    return deviation;
}

// The assistant's button and what it says.
bool assistantButton(Assistant& assistant, const ImVec2 origin, const float width, const ImU32 colour, const Live& live, const double seconds)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleColor(ImGuiCol_Button, theme::alpha(colour, assistant.listening ? 0.55f : 0.3f));
    const auto label = assistant.listening ? juce::String(ui::Ls("Écoute… ", "Listening… ")) + juce::String(std::max(0, static_cast<int>(std::ceil(seconds - assistant.heard)))) + " s"
                                           : juce::String(ui::Ls("Assistant : écouter", "Assistant: listen"));
    const auto clicked = ImGui::Button(label.toRawUTF8(), ImVec2(width, dp(24.0f)));
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", L("Lancez la lecture : l'assistant écoute quelques secondes puis règle le module. Ses réglages restent modifiables.",
                                  "Start playback: the assistant listens a few seconds then sets the module. Its settings stay yours to change."));
    if (clicked) {
        const auto verdict = assistant.verdict;
        assistant = Assistant {};
        assistant.listening = true;
        assistant.verdict = verdict;
    }
    const auto said = assistant.listening && !live.running ? juce::String(ui::Ls("Lancez la lecture pour qu'il écoute.", "Start playback for it to listen."))
                                                           : assistant.verdict;
    if (said.isNotEmpty())
        text(list, ImVec2(origin.x + width + dp(8.0f), origin.y + dp(5.0f)), p.textDim, said.toRawUTF8(), 0.66f);
    return clicked;
}

// A stage's chip: its name, lit when on; a click switches it.
bool stageChip(const char* id, const char* name, Param* parameter, const ImVec2 origin, const ImVec2 size, const ImU32 colour, const Setter& set)
{
    if (parameter == nullptr)
        return false;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushID(id);
    const auto clicked = ImGui::InvisibleButton("##chip", size);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        openParameterMenu(*parameter);
    ImGui::PopID();
    const auto on = parameter->value >= 0.5;
    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), on ? colour : p.bg3, dp(3.0f));
    centred(list, ImVec2(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f), on ? kDark : p.textFaint, name, 0.56f);
    return clicked && apply(parameter, on ? 0.0 : 1.0, set);
}

// An equaliser's handle: dragged sideways for its frequency, up and down for
// its gain.
bool eqHandle(std::vector<Param>& parameters, const Pane& pane, const Box& plot, const double range, const int hzIndex, const int dbIndex,
              const char* label, const ImU32 colour, const Setter& set, const int id)
{
    auto* hz = at(parameters, pane, hzIndex);
    auto* db = dbIndex >= 0 ? at(parameters, pane, dbIndex) : nullptr;
    if (hz == nullptr)
        return false;
    auto* list = ImGui::GetWindowDrawList();
    const auto gain = db != nullptr ? db->value : 0.0;
    const ImVec2 centre(xOfHz(plot, hz->value),
                        (plot.min.y + plot.max.y) * 0.5f - static_cast<float>(std::clamp(gain / range, -1.0, 1.0)) * plot.height() * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(centre.x - dp(8.0f), centre.y - dp(8.0f)));
    ImGui::PushID(id);
    ImGui::InvisibleButton("##handle", ImVec2(dp(16.0f), dp(16.0f)));
    const auto active = ImGui::IsItemActive();
    auto changed = false;
    if (active) {
        const auto mouse = ImGui::GetIO().MousePos;
        changed |= apply(hz, hzOfX(plot, mouse.x), set);
        if (db != nullptr)
            changed |= apply(db, (((plot.min.y + plot.max.y) * 0.5f - mouse.y) / (plot.height() * 0.5f)) * range, set);
    }
    // Double-click: a band flat again, a cut out of the way.
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        changed |= db != nullptr ? apply(db, 0.0, set) : apply(hz, hz->defaultValue, set);
    if (ImGui::IsItemHovered() && !active)
        ImGui::SetTooltip("%s", db != nullptr ? L("Glissez : fréquence de côté, gain de haut en bas. Double-clic : à plat. Clic droit : ses réglages.",
                                                  "Drag: frequency sideways, gain up and down. Double click: flat. Right click: its settings.")
                                              : L("Glissez : sa fréquence. Double-clic : hors de la bande. Clic droit : ses réglages.",
                                                  "Drag: its frequency. Double click: out of the way. Right click: its settings."));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        openParameterMenu(*hz);
    ImGui::PopID();
    handleDot(list, centre, colour, active || ImGui::IsItemHovered(), label);
    return changed;
}

// Tranche's equaliser: a double-click in its curve puts a band there. At
// the far ends, a cut; elsewhere the band that suits that place best among
// those still flat (a shelf toward the ends, a bell in between), else the
// nearest. The equaliser is switched on if it was off.
bool placeTrancheBand(std::vector<Param>& parameters, const Pane& pane, const Box& plot, const double range, const ImVec2 mouse,
                      const Setter& set)
{
    const auto hz = hzOfX(plot, mouse.x);
    const auto db = std::clamp((((plot.min.y + plot.max.y) * 0.5f - mouse.y) / (plot.height() * 0.5f)) * range, -range, range);
    auto changed = false;
    if (auto* on = at(parameters, pane, suited::trancheEqOn); on != nullptr && on->value < 0.5)
        changed |= apply(on, 1.0, set);
    if (hz < 40.0)
        return apply(at(parameters, pane, suited::trancheLowCut), hz, set) || changed;
    if (hz > 15'000.0)
        return apply(at(parameters, pane, suited::trancheHighCut), hz, set) || changed;
    struct Band {
        int hz;
        int db;
        double below; // a shelf suits below this frequency (low) ...
        double above; // ... or above this one (high); a bell anywhere
    };
    const std::array<Band, 4> bands { { { suited::trancheLowShelfHz, suited::trancheLowShelfDb, 220.0, 0.0 },
                                        { suited::trancheBell1Hz, suited::trancheBell1Db, 0.0, 0.0 },
                                        { suited::trancheBell2Hz, suited::trancheBell2Db, 0.0, 0.0 },
                                        { suited::trancheHighShelfHz, suited::trancheHighShelfDb, 0.0, 5'000.0 } } };
    const Band* best {};
    auto bestScore = 1.0e9;
    for (const auto& band : bands) {
        auto* frequency = at(parameters, pane, band.hz);
        auto* gain = at(parameters, pane, band.db);
        if (frequency == nullptr || gain == nullptr)
            continue;
        const auto shelf = band.below > 0.0 || band.above > 0.0;
        const auto suits = !shelf || (band.below > 0.0 && hz < band.below) || (band.above > 0.0 && hz > band.above);
        const auto score = (std::abs(gain->value) < 0.05 ? 0.0 : 100.0) + (suits ? 0.0 : 50.0)
                           + std::abs(std::log2(std::max(1.0, frequency->value) / hz));
        if (score < bestScore) {
            bestScore = score;
            best = &band;
        }
    }
    if (best == nullptr)
        return changed;
    changed |= apply(at(parameters, pane, best->hz), hz, set);
    changed |= apply(at(parameters, pane, best->db), db, set);
    return changed;
}

// ---- Tranche ---

constexpr std::array<ImU32, 5> kStageColours { IM_COL32(120, 210, 140, 255), IM_COL32(95, 200, 255, 255), IM_COL32(255, 160, 70, 255),
                                               IM_COL32(255, 110, 150, 255), IM_COL32(255, 210, 90, 255) };

// The equaliser's curve over the spectrum, its handles to drag; the stages
// on top, lit when on; what the compressor takes now.
bool trancheDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live, const Setter& set,
                    const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<suited::trancheParameterCount>(parameters, pane, "tranche");
    auto changed = false;
    // The stages.
    const std::array<const char*, 5> names { "GATE", "EQ", "COMP", L("TRANS.", "TRANS."), L("EXCIT.", "EXCITE") };
    const auto chipWidth = dp(large ? 62.0f : 44.0f);
    for (int stage = 0; stage < 5; ++stage)
        changed |= stageChip(names[static_cast<std::size_t>(stage)], names[static_cast<std::size_t>(stage)],
                             at(parameters, pane, suited::trancheGateOn + stage),
                             ImVec2(box.max.x - (chipWidth + dp(3.0f)) * static_cast<float>(5 - stage) - dp(4.0f), box.min.y + dp(2.0f)),
                             ImVec2(chipWidth, dp(large ? 18.0f : 14.0f)), kStageColours[static_cast<std::size_t>(stage)], set);
    const Box plot { ImVec2(box.min.x, box.min.y + dp(large ? 24.0f : 18.0f)), box.max };
    frequencyGrid(list, plot, large);
    constexpr auto range = 15.0;
    levelGrid(list, plot, range, large);
    spectrumBehind(list, plot, live.before, p.textDim);
    spectrumBehind(list, plot, live.after, colour);
    const auto eqOn = values[suited::trancheEqOn] >= 0.5f;
    const auto sections = suited::trancheEq(values.data(), kCurveRate);
    const auto points = curveOver(plot, range, [&](const double hz) { return eqOn ? suited::trancheEqDb(sections, hz, kCurveRate) : 0.0; });
    fillUnder(list, points, (plot.min.y + plot.max.y) * 0.5f, theme::alpha(colour, eqOn ? 0.16f : 0.05f));
    list->AddPolyline(points.data(), static_cast<int>(points.size()), eqOn ? colour : theme::alpha(colour, 0.4f), dp(large ? 2.2f : 1.5f));
    if (large) {
        // Under the handles: a double-click in the curve puts a band there.
        ImGui::SetCursorScreenPos(plot.min);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##trancheCurve", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            changed |= placeTrancheBand(parameters, pane, plot, range, ImGui::GetIO().MousePos, set);
        else if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", L("Double-clic : une bande à cet endroit (les poignées se glissent)",
                                      "Double click: a band here (the handles can be dragged)"));
    }
    if (large && eqOn) {
        changed |= eqHandle(parameters, pane, plot, range, suited::trancheLowCut, -1, "C", p.textDim, set, 1);
        changed |= eqHandle(parameters, pane, plot, range, suited::trancheLowShelfHz, suited::trancheLowShelfDb, "G", colour, set, 2);
        changed |= eqHandle(parameters, pane, plot, range, suited::trancheBell1Hz, suited::trancheBell1Db, "1", colour, set, 3);
        changed |= eqHandle(parameters, pane, plot, range, suited::trancheBell2Hz, suited::trancheBell2Db, "2", colour, set, 4);
        changed |= eqHandle(parameters, pane, plot, range, suited::trancheHighShelfHz, suited::trancheHighShelfDb, "A", colour, set, 5);
        changed |= eqHandle(parameters, pane, plot, range, suited::trancheHighCut, -1, "C", p.textDim, set, 6);
    }
    // What the compressor takes now, from the level coming in.
    if (live.running && values[suited::trancheCompOn] >= 0.5f && live.beforeFrame != nullptr) {
        auto peak = 0.0;
        for (std::uint32_t i = 0; i < live.beforeFrame->waveformCount; ++i)
            peak = std::max(peak, static_cast<double>(std::abs(live.beforeFrame->waveform[i])));
        const auto taken = suited::compressorGainDb(20.0 * std::log10(peak + 1.0e-12), values[suited::trancheCompThreshold],
                                                    values[suited::trancheCompRatio], values[suited::trancheCompKnee]);
        char label[48];
        std::snprintf(label, sizeof(label), "COMP %.1f dB", taken);
        text(list, ImVec2(plot.min.x + dp(4.0f), plot.max.y - ImGui::GetFontSize() * (large ? 1.6f : 0.95f)),
             taken < -0.1 ? kReduction : p.textDim, label, large ? 0.7f : 0.56f);
    }
    return changed;
}

// A shelf the assistant sets for how far its octaves stand from the
// balance (dB): an excess brought halfway down (6 dB at most); a lack lifted
// a little (3 dB at most), and not at all where the sound simply has nothing.
double shelfFor(const double deviation)
{
    if (deviation > 3.0)
        return -std::clamp((deviation - 3.0) * 0.5, 0.0, 6.0);
    if (deviation < -3.0 && deviation > -9.0)
        return std::clamp((-deviation - 3.0) / 3.0, 0.0, 3.0);
    return 0.0;
}

// How far an octave stands above its two neighbours (dB): a resonance, not
// the sound's own slope.
double prominence(const std::array<double, kOctaves>& deviation, const int octave)
{
    const auto left = deviation[static_cast<std::size_t>(octave > 0 ? octave - 1 : octave + 1)];
    const auto right = deviation[static_cast<std::size_t>(octave < kOctaves - 1 ? octave + 1 : octave - 1)];
    return deviation[static_cast<std::size_t>(octave)] - 0.5 * (left + right);
}

// The assistant's conclusions for a track: its equaliser and compressor,
// judged on the track's own shape (a hi-hat has no lows to lift).
juce::String trancheSuggest(std::vector<Param>& parameters, const Pane& pane, const Assistant& assistant, const Setter& set)
{
    const auto deviation = balance(assistant);
    const auto put = [&](const int index, const double value) { apply(at(parameters, pane, index), value, set); };
    // The lowest octaves far under the rest: only rumble lives there.
    const auto lowCut = deviation[0] < -12.0 && deviation[1] < -9.0 ? 80.0 : deviation[0] < -12.0 ? 40.0 : 10.0;
    put(suited::trancheLowCut, lowCut);
    const auto lowShelf = shelfFor(0.5 * (deviation[1] + deviation[2]));
    const auto highShelf = shelfFor(0.5 * (deviation[8] + deviation[9]));
    put(suited::trancheLowShelfDb, lowShelf);
    put(suited::trancheHighShelfDb, highShelf);
    // The bells on the octave standing out most, in the mids, then in the
    // presence; none where nothing stands out.
    const auto standing = [&deviation](const int from, const int to, const int skip) {
        auto best = -1;
        for (int octave = from; octave <= to; ++octave)
            if (octave != skip && (best < 0 || prominence(deviation, octave) > prominence(deviation, best)))
                best = octave;
        return best;
    };
    const auto mids = standing(3, 6, -1);
    const auto presence = standing(6, 8, mids);
    const auto cutFor = [&deviation](const int octave) {
        const auto rise = prominence(deviation, octave);
        return rise > 2.0 ? -std::clamp((rise - 2.0) * 0.6, 0.0, 6.0) : 0.0;
    };
    put(suited::trancheBell1Hz, kOctaveHz[static_cast<std::size_t>(mids)]);
    put(suited::trancheBell1Db, cutFor(mids));
    put(suited::trancheBell1Q, 1.4);
    put(suited::trancheBell2Hz, kOctaveHz[static_cast<std::size_t>(presence)]);
    put(suited::trancheBell2Db, cutFor(presence));
    put(suited::trancheBell2Q, 1.4);
    // The compressor about 6 dB under the peaks, half of what it takes given back.
    const auto rms = 10.0 * std::log10(assistant.power / std::max(1, assistant.frames) + 1.0e-20);
    const auto peak = 20.0 * std::log10(assistant.peak + 1.0e-12);
    const auto threshold = std::clamp(std::max(rms + 3.0, peak - 9.0), -50.0, -3.0);
    put(suited::trancheCompThreshold, threshold);
    put(suited::trancheCompRatio, 3.0);
    put(suited::trancheCompMakeup, std::clamp(-0.5 * suited::compressorGainDb(peak, threshold, 3.0, 6.0), 0.0, 9.0));
    put(suited::trancheEqOn, 1.0);
    put(suited::trancheCompOn, 1.0);
    juce::StringArray done;
    if (lowCut > 10.5)
        done.add(juce::String::fromUTF8(ui::french() ? "coupe-bas " : "low cut ") + juce::String(juce::roundToInt(lowCut)) + " Hz");
    const auto describe = [&done](const char* french, const char* english, const double db) {
        if (std::abs(db) > 0.05)
            done.add(juce::String::fromUTF8(ui::french() ? french : english) + " " + (db > 0.0 ? "+" : "") + juce::String(db, 1) + " dB");
    };
    describe("graves", "lows", lowShelf);
    describe("aigus", "highs", highShelf);
    if (cutFor(mids) < -0.05)
        done.add(juce::String::fromUTF8(ui::french() ? "cloche " : "bell ") + juce::String(juce::roundToInt(kOctaveHz[static_cast<std::size_t>(mids)]))
                 + " Hz " + juce::String(cutFor(mids), 1) + " dB");
    if (cutFor(presence) < -0.05)
        done.add(juce::String::fromUTF8(ui::french() ? "cloche " : "bell ")
                 + juce::String(juce::roundToInt(kOctaveHz[static_cast<std::size_t>(presence)])) + " Hz " + juce::String(cutFor(presence), 1)
                 + " dB");
    done.add(juce::String::fromUTF8(ui::french() ? "compresseur à " : "compressor at ") + juce::String(juce::roundToInt(threshold)) + " dB");
    return juce::String::fromUTF8(ui::french() ? "Réglé : " : "Set: ") + done.joinIntoString(", ") + ".";
}

bool trancheLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                  const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto small = 28.0f;
    const auto eqColumns = knobColumns(size.x - dp(8.0f), 12, dp(68.0f));
    const auto eqRows = static_cast<float>((12 + eqColumns - 1) / eqColumns);
    const auto panelHeight = dp(34.0f) + knobHeight(small) * eqRows + dp(22.0f) + knobHeight(small) * 2.0f + dp(16.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(180.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= trancheDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("TRANCHE  ·  la tranche d'une piste", "TRANCHE  ·  a track's channel strip"));
    list->PopClipRect();

    auto& assistant = assistantOf(parameters, pane);
    if (listen(assistant, live.beforeFrame, live, 6.0)) {
        assistant.listening = false;
        assistant.verdict = trancheSuggest(parameters, pane, assistant, set);
        changed = true;
    }
    auto y = outer.max.y + dp(6.0f);
    assistantButton(assistant, ImVec2(position.x + dp(4.0f), y), dp(190.0f), colour, live, 6.0);
    y += dp(30.0f);
    struct Item {
        int index;
        const char* label;
    };
    // The equaliser's knobs.
    const std::array<Item, 12> eq { { { suited::trancheLowCut, L("Coupe-bas", "Low cut") },
                                      { suited::trancheLowShelfHz, L("Graves Hz", "Low Hz") },
                                      { suited::trancheLowShelfDb, L("Graves dB", "Low dB") },
                                      { suited::trancheBell1Hz, L("Cloche 1 Hz", "Bell 1 Hz") },
                                      { suited::trancheBell1Db, L("Cloche 1 dB", "Bell 1 dB") },
                                      { suited::trancheBell1Q, L("Cloche 1 Q", "Bell 1 Q") },
                                      { suited::trancheBell2Hz, L("Cloche 2 Hz", "Bell 2 Hz") },
                                      { suited::trancheBell2Db, L("Cloche 2 dB", "Bell 2 dB") },
                                      { suited::trancheBell2Q, L("Cloche 2 Q", "Bell 2 Q") },
                                      { suited::trancheHighShelfHz, L("Aigus Hz", "High Hz") },
                                      { suited::trancheHighShelfDb, L("Aigus dB", "High dB") },
                                      { suited::trancheHighCut, L("Coupe-haut", "High cut") } } };
    auto cell = std::min(dp(96.0f), (size.x - dp(8.0f)) / static_cast<float>(eqColumns));
    for (std::size_t index = 0; index < eq.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(4.0f) + cell * static_cast<float>(index % eqColumns),
                                   y + knobHeight(small) * static_cast<float>(index / eqColumns)),
                            cell, at(parameters, pane, eq[index].index), eq[index].label, kStageColours[1], live, set, small);
    y += knobHeight(small) * eqRows + dp(4.0f);
    // The other stages side by side, each its knobs on two rows.
    struct Stage {
        const char* name;
        int on;
        ImU32 tint;
        std::vector<Item> knobs;
    };
    const std::array<Stage, 5> stages { {
        { "GATE", suited::trancheGateOn, kStageColours[0],
          { { suited::trancheGateThreshold, L("Seuil", "Threshold") }, { suited::trancheGateRange, L("Plage", "Range") },
            { suited::trancheGateAttack, L("Attaque", "Attack") }, { suited::trancheGateRelease, L("Relâch.", "Release") } } },
        { L("COMPRESSEUR", "COMPRESSOR"), suited::trancheCompOn, kStageColours[2],
          { { suited::trancheCompThreshold, L("Seuil", "Threshold") }, { suited::trancheCompRatio, "Ratio" },
            { suited::trancheCompMakeup, L("Gain", "Makeup") }, { suited::trancheCompAttack, L("Attaque", "Attack") },
            { suited::trancheCompRelease, L("Relâch.", "Release") }, { suited::trancheCompKnee, L("Genou", "Knee") } } },
        { L("TRANSITOIRES", "TRANSIENTS"), suited::trancheTransientOn, kStageColours[3],
          { { suited::trancheAttack, L("Attaque", "Attack") }, { suited::trancheSustain, L("Maintien", "Sustain") } } },
        { L("EXCITATEUR", "EXCITER"), suited::trancheExciterOn, kStageColours[4],
          { { suited::trancheExcite, L("Quantité", "Amount") }, { suited::trancheExciterHz, L("Dès", "From") } } },
        { L("SORTIE", "OUTPUT"), -1, colour,
          { { suited::trancheWidth, L("Largeur", "Width") }, { suited::trancheOutput, L("Sortie", "Output") },
            { suited::trancheMix, L("Mélange", "Mix") } } },
    } };
    const std::array<float, 5> shares { 0.22f, 0.3f, 0.14f, 0.17f, 0.17f };
    auto x = position.x + dp(4.0f);
    for (std::size_t index = 0; index < stages.size(); ++index) {
        const auto& stage = stages[index];
        const auto width = size.x * shares[index] - dp(6.0f);
        list->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + dp(20.0f) + knobHeight(small) * 2.0f), theme::alpha(stage.tint, 0.06f), dp(4.0f));
        if (stage.on >= 0)
            changed |= switchCell(stage.name, at(parameters, pane, stage.on), ImVec2(x + dp(2.0f), y + dp(1.0f)), ImVec2(width - dp(4.0f), dp(18.0f)),
                                  stage.tint, set);
        else
            section(list, ImVec2(x + dp(6.0f), y + dp(3.0f)), stage.name);
        const auto perRow = index == 3 ? stage.knobs.size() : static_cast<std::size_t>(std::max<std::size_t>(1, (stage.knobs.size() + 1) / 2));
        cell = width / static_cast<float>(perRow);
        ImGui::PushID(static_cast<int>(index) + 1'500);
        for (std::size_t knob = 0; knob < stage.knobs.size(); ++knob)
            changed |= knobCell(ImVec2(x + cell * static_cast<float>(knob % perRow), y + dp(22.0f) + knobHeight(small) * static_cast<float>(knob / perRow)),
                                cell, at(parameters, pane, stage.knobs[knob].index), stage.knobs[knob].label, stage.tint, live, set, small);
        ImGui::PopID();
        if (index == 3)
            changed |= segmented("##style", at(parameters, pane, suited::trancheExciterStyle), { L("Lampe", "Valve"), L("Bande", "Tape"), "Trans." },
                                 ImVec2(x + dp(2.0f), y + dp(22.0f) + knobHeight(small) + dp(4.0f)), width - dp(4.0f), dp(18.0f), stage.tint, set);
        x += width + dp(6.0f);
    }
    juce::ignoreUnused(p);
    return changed;
}

// ---- Finition ---

constexpr std::array<ImU32, suited::finitionBands> kTrioColours { IM_COL32(255, 120, 150, 255), IM_COL32(255, 196, 90, 255),
                                                                  IM_COL32(110, 190, 255, 255) };

// The equaliser's curve over the spectrum and the three bands it splits
// into; the loudness out against the target; the stages, lit when on.
bool finitionDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live, const Setter& set,
                     const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<suited::finitionParameterCount>(parameters, pane, "finition");
    auto changed = false;
    const std::array<const char*, 5> names { "EQ", "DYN", L("EXCIT.", "EXCITE"), "IMAGE", "MAX" };
    const auto chipWidth = dp(large ? 58.0f : 40.0f);
    for (int stage = 0; stage < 5; ++stage)
        changed |= stageChip(names[static_cast<std::size_t>(stage)], names[static_cast<std::size_t>(stage)],
                             at(parameters, pane, suited::finitionEqOn + stage),
                             ImVec2(box.max.x - (chipWidth + dp(3.0f)) * static_cast<float>(5 - stage) - dp(4.0f), box.min.y + dp(2.0f)),
                             ImVec2(chipWidth, dp(large ? 18.0f : 14.0f)), kStageColours[static_cast<std::size_t>(stage)], set);
    // The loudness on the right.
    auto& meter = loudnessOf(parameters, pane);
    meter.feed(live.afterFrame, ImGui::GetTime());
    const auto meterWidth = large ? dp(150.0f) : dp(64.0f);
    const Box plot { ImVec2(box.min.x, box.min.y + dp(large ? 24.0f : 18.0f)), ImVec2(box.max.x - meterWidth - dp(6.0f), box.max.y) };
    frequencyGrid(list, plot, large);
    constexpr auto range = 9.0;
    levelGrid(list, plot, range, large);
    spectrumBehind(list, plot, live.before, p.textDim);
    spectrumBehind(list, plot, live.after, colour);
    // The three bands, their width shown in their tint.
    const std::array<float, 4> edges { plot.min.x, xOfHz(plot, values[suited::finitionCrossLow]), xOfHz(plot, values[suited::finitionCrossHigh]),
                                       plot.max.x };
    const std::array<float, suited::finitionBands> widths { values[suited::finitionLowWidth], values[suited::finitionMidWidth],
                                                            values[suited::finitionHighWidth] };
    for (std::size_t band = 0; band < suited::finitionBands; ++band) {
        const auto share = std::clamp(widths[band] * values[suited::finitionWidth] / 2.0f, 0.0f, 1.0f);
        list->AddRectFilled(ImVec2(edges[band] + dp(1.0f), plot.max.y - dp(6.0f)), ImVec2(edges[band + 1] - dp(1.0f), plot.max.y),
                            theme::alpha(kTrioColours[band], 0.25f + 0.6f * share));
    }
    for (std::size_t c = 1; c < 3; ++c)
        dashedLine(list, ImVec2(edges[c], plot.min.y), ImVec2(edges[c], plot.max.y), theme::alpha(p.text, 0.35f));
    const auto eqOn = values[suited::finitionEqOn] >= 0.5f;
    const auto sections = suited::finitionEq(values.data(), kCurveRate);
    const auto points = curveOver(plot, range, [&](const double hz) { return eqOn ? suited::finitionEqDb(sections, hz, kCurveRate) : 0.0; });
    fillUnder(list, points, (plot.min.y + plot.max.y) * 0.5f, theme::alpha(colour, eqOn ? 0.16f : 0.05f));
    list->AddPolyline(points.data(), static_cast<int>(points.size()), eqOn ? colour : theme::alpha(colour, 0.4f), dp(large ? 2.2f : 1.5f));
    if (large && eqOn) {
        changed |= eqHandle(parameters, pane, plot, range, suited::finitionLowHz, suited::finitionLow, "G", colour, set, 11);
        changed |= eqHandle(parameters, pane, plot, range, suited::finitionMidHz, suited::finitionMid, "M", colour, set, 12);
        changed |= eqHandle(parameters, pane, plot, range, suited::finitionHighHz, suited::finitionHigh, "A", colour, set, 13);
    }
    // Integrated loudness against the target, and the ceiling.
    const Box gauge { ImVec2(plot.max.x + dp(6.0f), plot.min.y), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    list->AddRectFilled(gauge.min, gauge.max, theme::alpha(kDark, 0.6f), dp(4.0f));
    const auto& target = mixd::loudnessTarget(values[suited::finitionTarget]);
    const auto integrated = meter.integrated();
    char value[32];
    std::snprintf(value, sizeof(value), integrated < -99.0 ? "--" : "%.1f", integrated);
    text(list, ImVec2(gauge.min.x + dp(4.0f), gauge.min.y + dp(4.0f)), againstTarget(integrated, target.lufs), value, large ? 1.5f : 0.9f);
    std::snprintf(value, sizeof(value), "LUFS / %.0f", target.lufs);
    text(list, ImVec2(gauge.min.x + dp(4.0f), gauge.min.y + ImGui::GetFontSize() * (large ? 1.6f : 1.0f)), p.textDim, value, large ? 0.62f : 0.5f);
    if (large) {
        const auto truePeak = meter.truePeakDb();
        std::snprintf(value, sizeof(value), truePeak < -99.0 ? "TP --" : "TP %.1f dB", truePeak);
        text(list, ImVec2(gauge.min.x + dp(4.0f), gauge.min.y + ImGui::GetFontSize() * 2.6f),
             truePeak > values[suited::finitionCeiling] + 0.5 ? kReduction : p.text, value, 0.7f);
        std::snprintf(value, sizeof(value), "%s %+.1f dB", L("Gain", "Gain"), values[suited::finitionGain]);
        text(list, ImVec2(gauge.min.x + dp(4.0f), gauge.min.y + ImGui::GetFontSize() * 3.6f), p.text, value, 0.7f);
        std::snprintf(value, sizeof(value), "%s %.1f dB", L("Plafond", "Ceiling"), values[suited::finitionCeiling]);
        text(list, ImVec2(gauge.min.x + dp(4.0f), gauge.min.y + ImGui::GetFontSize() * 4.6f), p.text, value, 0.7f);
    }
    return changed;
}

// The assistant's conclusions for a master: the gain to the target, the tone
// a little towards the balance.
juce::String finitionSuggest(std::vector<Param>& parameters, const Pane& pane, const Assistant& assistant, const Setter& set)
{
    const auto values = studioValues<suited::finitionParameterCount>(parameters, pane, "finition");
    const auto put = [&](const int index, const double value) { apply(at(parameters, pane, index), value, set); };
    const auto& target = mixd::loudnessTarget(values[suited::finitionTarget]);
    const auto heard = assistant.loudness.integrated();
    if (heard < -70.0)
        return ui::Ls("Trop peu de son entendu : relancez avec la musique.", "Too little sound heard: listen again with the music playing.");
    // It heard what comes out with today's gain: the more the gain, the
    // more the maximizer holds, so a little past the difference.
    const auto gain = std::clamp(static_cast<double>(values[suited::finitionGain]) + (target.lufs - heard) * 1.15, 0.0, 18.0);
    put(suited::finitionGain, gain);
    put(suited::finitionCeiling, std::min(static_cast<double>(values[suited::finitionCeiling]), target.truePeakDb));
    const auto deviation = balance(assistant);
    put(suited::finitionLow, std::clamp(shelfFor(0.5 * (deviation[1] + deviation[2])) * 0.5, -3.0, 3.0));
    put(suited::finitionHigh, std::clamp(shelfFor(0.5 * (deviation[8] + deviation[9])) * 0.5, -3.0, 3.0));
    put(suited::finitionMaximizerOn, 1.0);
    char said[160];
    std::snprintf(said, sizeof(said), ui::french() ? "Entendu %.1f LUFS : gain %.1f dB pour viser %.0f LUFS. Relancez pour affiner."
                                                   : "Heard %.1f LUFS: gain %.1f dB to aim at %.0f LUFS. Listen again to refine.",
                  heard, gain, target.lufs);
    return juce::String::fromUTF8(said);
}

bool finitionLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                   const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto small = 28.0f;
    const auto panelHeight = dp(34.0f) + dp(22.0f) + knobHeight(small) * 2.0f + dp(16.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(200.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= finitionDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("FINITION  ·  le mastering", "FINITION  ·  mastering"));
    list->PopClipRect();

    auto& assistant = assistantOf(parameters, pane);
    // It listens to what comes out, the gain as it is.
    if (listen(assistant, live.afterFrame, live, 10.0)) {
        assistant.listening = false;
        assistant.verdict = finitionSuggest(parameters, pane, assistant, set);
        changed = true;
    }
    auto y = outer.max.y + dp(6.0f);
    assistantButton(assistant, ImVec2(position.x + dp(4.0f), y), dp(190.0f), colour, live, 10.0);
    std::vector<juce::String> targets;
    for (int index = 0; index < mixd::loudnessTargetCount; ++index)
        targets.push_back(targetName(index));
    const auto comboWidth = std::min(dp(320.0f), size.x * 0.3f);
    changed |= comboCell("##target", at(parameters, pane, suited::finitionTarget), targets, ImVec2(position.x + size.x - comboWidth - dp(4.0f), y),
                         comboWidth, set);
    y += dp(30.0f);
    struct Item {
        int index;
        const char* label;
    };
    struct Stage {
        const char* name;
        int on;
        ImU32 tint;
        std::vector<Item> knobs;
    };
    const std::array<Stage, 5> stages { {
        { L("ÉGALISEUR", "EQUALISER"), suited::finitionEqOn, kStageColours[0],
          { { suited::finitionLowCut, L("Coupe-bas", "Low cut") }, { suited::finitionLow, L("Graves", "Low") },
            { suited::finitionMid, L("Médiums", "Mid") }, { suited::finitionHigh, L("Aigus", "High") },
            { suited::finitionLowHz, L("Graves Hz", "Low Hz") }, { suited::finitionMidHz, L("Médiums Hz", "Mid Hz") },
            { suited::finitionMidQ, "Q" }, { suited::finitionHighHz, L("Aigus Hz", "High Hz") } } },
        { L("DYNAMIQUE", "DYNAMICS"), suited::finitionDynamicsOn, kStageColours[1],
          { { suited::finitionDynamics, L("Quantité", "Amount") }, { suited::finitionThreshold, L("Seuil", "Threshold") },
            { suited::finitionRatio, "Ratio" }, { suited::finitionCrossLow, L("Coupure 1", "Split 1") },
            { suited::finitionAttack, L("Attaque", "Attack") }, { suited::finitionRelease, L("Relâch.", "Release") },
            { suited::finitionCrossHigh, L("Coupure 2", "Split 2") } } },
        { L("EXCITATEUR", "EXCITER"), suited::finitionExciterOn, kStageColours[2],
          { { suited::finitionExcite, L("Quantité", "Amount") }, { suited::finitionLowDrive, L("Graves", "Low") },
            { suited::finitionMidDrive, L("Médiums", "Mid") }, { suited::finitionHighDrive, L("Aigus", "High") } } },
        { L("IMAGE STÉRÉO", "STEREO IMAGE"), suited::finitionImagerOn, kStageColours[3],
          { { suited::finitionWidth, L("Largeur", "Width") }, { suited::finitionLowWidth, L("Graves", "Low") },
            { suited::finitionMidWidth, L("Médiums", "Mid") }, { suited::finitionHighWidth, L("Aigus", "High") } } },
        { L("MAXIMISEUR", "MAXIMIZER"), suited::finitionMaximizerOn, kStageColours[4],
          { { suited::finitionGain, L("Gain", "Gain") }, { suited::finitionCeiling, L("Plafond", "Ceiling") },
            { suited::finitionMaximizerRelease, L("Relâch.", "Release") } } },
    } };
    const std::array<float, 5> shares { 0.27f, 0.25f, 0.16f, 0.16f, 0.16f };
    auto x = position.x + dp(4.0f);
    for (std::size_t index = 0; index < stages.size(); ++index) {
        const auto& stage = stages[index];
        const auto width = size.x * shares[index] - dp(6.0f);
        list->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + dp(20.0f) + knobHeight(small) * 2.0f), theme::alpha(stage.tint, 0.06f), dp(4.0f));
        changed |= switchCell(stage.name, at(parameters, pane, stage.on), ImVec2(x + dp(2.0f), y + dp(1.0f)), ImVec2(width - dp(4.0f), dp(18.0f)),
                              stage.tint, set);
        const auto perRow = static_cast<std::size_t>(std::max<std::size_t>(1, (stage.knobs.size() + 1) / 2));
        const auto cell = width / static_cast<float>(perRow);
        ImGui::PushID(static_cast<int>(index) + 1'600);
        for (std::size_t knob = 0; knob < stage.knobs.size(); ++knob)
            changed |= knobCell(ImVec2(x + cell * static_cast<float>(knob % perRow), y + dp(22.0f) + knobHeight(small) * static_cast<float>(knob / perRow)),
                                cell, at(parameters, pane, stage.knobs[knob].index), stage.knobs[knob].label, stage.tint, live, set, small);
        ImGui::PopID();
        x += width + dp(6.0f);
    }
    return changed;
}
