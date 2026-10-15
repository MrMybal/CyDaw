// The displays and large views of Utilitaire, Stéréoscope
// (UtilityDesign.h computes what they draw). Included by StudioEffectsView.cpp
// inside its anonymous namespace: it has its helpers (knobCell, segmented,
// chooser, comboCell, switchCell, frame, title, section, text, fillUnder…)
// and addedLarge(), the common window layout.

namespace utilityd = engine::utility;

constexpr ImU32 kUtilityInPhase = IM_COL32(96, 214, 128, 255);
constexpr ImU32 kUtilityOutOfPhase = IM_COL32(255, 92, 92, 255);

juce::String utilitaireModeName(const int index)
{
    const auto& mode = utilityd::utilitaireChannelModes[static_cast<std::size_t>(std::clamp(index, 0, utilityd::utilitaireChannelModeCount - 1))];
    return juce::String::fromUTF8(ui::french() ? mode.french : mode.english);
}
juce::String utilitaireModeHint(const int index)
{
    const auto& mode = utilityd::utilitaireChannelModes[static_cast<std::size_t>(std::clamp(index, 0, utilityd::utilitaireChannelModeCount - 1))];
    return juce::String::fromUTF8(ui::french() ? mode.frenchHint : mode.englishHint);
}
juce::String stereoscopeModeName(const int index)
{
    const auto& mode = utilityd::stereoscopeDisplayModes[static_cast<std::size_t>(std::clamp(index, 0, utilityd::stereoscopeDisplayModeCount - 1))];
    return juce::String::fromUTF8(ui::french() ? mode.french : mode.english);
}
juce::String stereoscopeModeHint(const int index)
{
    const auto& mode = utilityd::stereoscopeDisplayModes[static_cast<std::size_t>(std::clamp(index, 0, utilityd::stereoscopeDisplayModeCount - 1))];
    return juce::String::fromUTF8(ui::french() ? mode.frenchHint : mode.englishHint);
}

// The processor's readings when they are there and fresh.
const float* utilityReadingsOf(const Live& live, const int count)
{
    return live.running && live.readings != nullptr && live.readingCount >= count ? live.readings : nullptr;
}

// ---- Utilitaire --------------------------------------------------------------------------------

// A side's output level, upright: -60 dB at the bottom, +6 dB at the top,
// 0 dB marked.
void utilitaireLevelBar(ImDrawList* list, const Box& bar, const float peak)
{
    const auto& p = theme::palette();
    list->AddRectFilled(bar.min, bar.max, theme::alpha(kDark, 0.7f), dp(2.0f));
    const auto db = peak > 1.0e-6f ? 20.0f * std::log10(peak) : -120.0f;
    const auto t = std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f);
    if (t > 0.0f)
        list->AddRectFilled(ImVec2(bar.min.x, bar.max.y - t * bar.height()), bar.max,
                            db > 0.0f ? kUtilityOutOfPhase : db > -6.0f ? p.meterMid : p.meterLow, dp(2.0f));
    const auto zero = bar.max.y - (60.0f / 66.0f) * bar.height();
    list->AddLine(ImVec2(bar.min.x - dp(1.0f), zero), ImVec2(bar.max.x + dp(1.0f), zero), p.textDim);
}

// A polarity switch drawn as its sign (a circle crossed out), lit while it
// turns its side over; a click flips it.
bool utilitairePolarityMark(const char* id, Param* parameter, const ImVec2 centre, const float radius, const ImU32 colour,
                            const Setter& set, const char* tooltip)
{
    if (parameter == nullptr)
        return false;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto on = parameter->value >= 0.5;
    ImGui::SetCursorScreenPos(ImVec2(centre.x - radius - dp(2.0f), centre.y - radius - dp(2.0f)));
    ImGui::PushID(id);
    const auto clicked = ImGui::InvisibleButton("##polarity", ImVec2(radius * 2.0f + dp(4.0f), radius * 2.0f + dp(4.0f)));
    const auto hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        openParameterMenu(*parameter);
    ImGui::PopID();
    if (on)
        list->AddCircleFilled(centre, radius, colour);
    else if (hovered)
        list->AddCircleFilled(centre, radius, theme::alpha(colour, 0.25f));
    else
        list->AddCircle(centre, radius, theme::alpha(p.textDim, 0.6f), 0, dp(1.0f));
    const auto ink = on ? kDark : hovered ? p.text : p.textDim;
    list->AddCircle(centre, radius * 0.55f, ink, 0, dp(1.3f));
    list->AddLine(ImVec2(centre.x - radius * 0.72f, centre.y + radius * 0.72f), ImVec2(centre.x + radius * 0.72f, centre.y - radius * 0.72f), ink,
                  dp(1.3f));
    if (hovered)
        ImGui::SetTooltip("%s", tooltip);
    return clicked && apply(parameter, on ? 0.0 : 1.0, set);
}

// The utility's picture: each side's output level at the edges, the stereo
// field as a fan (its spread the width, its edges' lengths the balance, the
// lows made mono a stalk at its foot), the polarity marks to click, the
// mono-bass frequency to drag under it.
bool utilitaireView(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live, const Setter& set,
                    const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    const auto values = studioValues<utilityd::utilitaireParameterCount>(parameters, pane, "utilitaire");
    const auto scale = large ? 0.7f : 0.56f;
    const auto font = ImGui::GetFontSize() * scale;
    auto changed = false;
    const auto top = box.min.y + dp(large ? 30.0f : 6.0f);
    const auto bottom = box.max.y - dp(large ? 8.0f : 4.0f);
    // The two output levels.
    const auto barWidth = dp(large ? 10.0f : 6.0f);
    const Box leftBar { ImVec2(box.min.x + dp(6.0f), top), ImVec2(box.min.x + dp(6.0f) + barWidth, bottom) };
    const Box rightBar { ImVec2(box.max.x - dp(6.0f) - barWidth, top), ImVec2(box.max.x - dp(6.0f), bottom) };
    std::array<float, 2> peaks { 0.0f, 0.0f };
    if (const auto* readings = utilityReadingsOf(live, utilityd::utilitaireReadingCount)) {
        peaks = { readings[utilityd::utilitaireReadingLeftPeak], readings[utilityd::utilitaireReadingRightPeak] };
    } else if (live.running) {
        const auto level = std::pow(10.0f, live.outputDb / 20.0f);
        peaks = { level, level };
    }
    utilitaireLevelBar(list, leftBar, peaks[0]);
    utilitaireLevelBar(list, rightBar, peaks[1]);
    if (large) {
        centred(list, ImVec2((leftBar.min.x + leftBar.max.x) * 0.5f, top - font * 0.8f), p.textDim, L("G", "L"), scale);
        centred(list, ImVec2((rightBar.min.x + rightBar.max.x) * 0.5f, top - font * 0.8f), p.textDim, L("D", "R"), scale);
    }
    // The polarity marks, beside the bars (drawn first: they win the mouse).
    const auto mark = dp(large ? 9.0f : 6.5f);
    changed |= utilitairePolarityMark("##invertLeft", at(parameters, pane, utilityd::utilitaireInvertLeft),
                                      ImVec2(leftBar.max.x + dp(6.0f) + mark, top + mark), mark, colour, set,
                                      L("Polarité gauche : un clic l'inverse", "Left polarity: a click flips it"));
    changed |= utilitairePolarityMark("##invertRight", at(parameters, pane, utilityd::utilitaireInvertRight),
                                      ImVec2(rightBar.min.x - dp(6.0f) - mark, top + mark), mark, colour, set,
                                      L("Polarité droite : un clic l'inverse", "Right polarity: a click flips it"));
    // The mono-bass frequency, under the field: dragged sideways.
    const auto bassHz = values[utilityd::utilitaireMonoBass];
    const auto bassOn = utilityd::utilitaireMonoBassOn(bassHz);
    char bassLabel[48];
    if (bassOn)
        std::snprintf(bassLabel, sizeof(bassLabel), "%s %.0f Hz", L("Mono sous", "Mono below"), static_cast<double>(bassHz));
    else
        std::snprintf(bassLabel, sizeof(bassLabel), "%s", L("Graves stéréo", "Stereo bass"));
    const auto bassSize = textSize(bassLabel, scale);
    const auto middleX = (box.min.x + box.max.x) * 0.5f;
    const Box chip { ImVec2(middleX - bassSize.x * 0.5f - dp(6.0f), bottom - bassSize.y - dp(4.0f)),
                     ImVec2(middleX + bassSize.x * 0.5f + dp(6.0f), bottom) };
    if (auto* bass = at(parameters, pane, utilityd::utilitaireMonoBass)) {
        ImGui::SetCursorScreenPos(chip.min);
        ImGui::InvisibleButton("##monoBass", ImVec2(std::max(1.0f, chip.width()), std::max(1.0f, chip.height())));
        const auto hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) && io.MouseDelta.x != 0.0f)
            changed |= apply(bass, bass->value * std::exp(static_cast<double>(io.MouseDelta.x) * 0.012), set);
        if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
            changed |= apply(bass, bass->value * std::exp(static_cast<double>(io.MouseWheel) * 0.06), set);
            ui::takeWheel();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            changed |= apply(bass, static_cast<double>(utilityd::utilitaireMonoBassOff), set);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            openParameterMenu(*bass);
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ImGui::SetTooltip("%s", L("Sous cette fréquence le son devient mono.\nGlisser de côté : la changer  ·  double clic : arrêter",
                                      "Under this frequency the sound turns mono.\nDrag sideways: change it  ·  double click: turn it off"));
        }
        list->AddRectFilled(chip.min, chip.max, theme::alpha(colour, bassOn ? (hovered ? 0.4f : 0.26f) : (hovered ? 0.2f : 0.1f)), dp(3.0f));
        text(list, ImVec2(chip.min.x + dp(6.0f), chip.min.y + dp(2.0f)), bassOn ? p.text : p.textDim, bassLabel, scale);
    }
    // The field.
    const Box field { ImVec2(leftBar.max.x + dp(8.0f), top), ImVec2(rightBar.min.x - dp(8.0f), chip.min.y - dp(4.0f)) };
    const auto mode = whole(values[utilityd::utilitaireChannelMode], utilityd::utilitaireChannelModeCount);
    const auto monoImage = mode == utilityd::utilitaireLeftOnlyMode || mode == utilityd::utilitaireRightOnlyMode || mode == utilityd::utilitaireMonoMode;
    const auto muted = values[utilityd::utilitaireMute] >= 0.5f;
    const auto tint = muted ? p.textFaint : colour;
    const auto balance = utilityd::utilitaireBalanceOf(values[utilityd::utilitairePan]);
    constexpr auto pi = std::numbers::pi_v<float>;
    const ImVec2 base(middleX, field.max.y - dp(2.0f));
    const auto radius = std::min(field.width() * 0.5f, field.height()) - dp(2.0f);
    if (radius > dp(8.0f)) {
        list->PathArcTo(base, radius, pi, 2.0f * pi, 48);
        list->PathStroke(p.gridStrong, dp(1.0f));
        list->AddLine(ImVec2(base.x - radius, base.y), ImVec2(base.x + radius, base.y), p.grid);
        list->AddLine(base, ImVec2(base.x, base.y - radius), p.grid);
        for (const auto side : { -1.0f, 1.0f })
            dashedLine(list, base, ImVec2(base.x + side * radius * 0.7071f, base.y - radius * 0.7071f), p.grid, dp(1.0f));
        const auto spread = monoImage ? 0.0f
                                      : std::min(0.48f * pi, std::clamp(values[utilityd::utilitaireWidth], 0.0f, 2.0f) * 0.25f * pi);
        const auto up = -0.5f * pi;
        const auto leftLength = radius * static_cast<float>(balance[0]);
        const auto rightLength = radius * static_cast<float>(balance[1]);
        const ImVec2 leftEnd(base.x + std::cos(up - spread) * leftLength, base.y + std::sin(up - spread) * leftLength);
        const ImVec2 rightEnd(base.x + std::cos(up + spread) * rightLength, base.y + std::sin(up + spread) * rightLength);
        if (spread > 0.01f) {
            std::vector<ImVec2> fan { base };
            constexpr int steps = 40;
            for (int step = 0; step <= steps; ++step) {
                const auto t = static_cast<float>(step) / static_cast<float>(steps);
                const auto angle = up - spread + 2.0f * spread * t;
                const auto length = leftLength + (rightLength - leftLength) * t;
                fan.emplace_back(base.x + std::cos(angle) * length, base.y + std::sin(angle) * length);
            }
            list->AddConcavePolyFilled(fan.data(), static_cast<int>(fan.size()), theme::alpha(tint, 0.24f));
            list->AddPolyline(fan.data() + 1, static_cast<int>(fan.size()) - 1, theme::alpha(tint, 0.6f), dp(1.0f));
            list->AddLine(base, leftEnd, tint, dp(large ? 2.4f : 1.8f));
            list->AddLine(base, rightEnd, tint, dp(large ? 2.4f : 1.8f));
            // The edges named, the other way round when swapped.
            const auto swapped = mode == utilityd::utilitaireSwappedMode;
            centred(list, ImVec2(leftEnd.x - font * 0.6f, leftEnd.y - font * 0.5f), swapped ? colour : p.textDim, swapped ? L("D", "R") : L("G", "L"),
                    scale);
            centred(list, ImVec2(rightEnd.x + font * 0.6f, rightEnd.y - font * 0.5f), swapped ? colour : p.textDim, swapped ? L("G", "L") : L("D", "R"),
                    scale);
        } else {
            // One sound in the middle: a single line.
            const auto length = radius * static_cast<float>(0.5 * (balance[0] + balance[1]));
            const ImVec2 end(base.x, base.y - length);
            list->AddLine(base, end, tint, dp(large ? 3.0f : 2.2f));
            const auto* name = mode == utilityd::utilitaireLeftOnlyMode ? L("G", "L") : mode == utilityd::utilitaireRightOnlyMode ? L("D", "R") : "M";
            centred(list, ImVec2(end.x + font * 0.8f, end.y + font * 0.4f), p.textDim, name, scale);
        }
        // The lows made mono: a stalk at the fan's foot, as high as the frequency.
        if (bassOn) {
            const auto t = static_cast<float>(std::log(static_cast<double>(bassHz) / 20.0) / std::log(500.0 / 20.0));
            const auto stalk = radius * (0.12f + 0.38f * std::clamp(t, 0.0f, 1.0f));
            list->PathArcTo(base, stalk, pi, 2.0f * pi, 24);
            list->PathFillConvex(theme::alpha(kDark, 0.85f));
            list->PathArcTo(base, stalk, pi, 2.0f * pi, 24);
            list->PathStroke(theme::alpha(tint, 0.7f), dp(1.0f));
            list->AddLine(base, ImVec2(base.x, base.y - stalk), tint, dp(large ? 3.0f : 2.2f));
        }
        if (muted)
            centred(list, ImVec2(base.x, base.y - radius * 0.45f), kUtilityOutOfPhase, L("MUET", "MUTED"), large ? 1.1f : 0.8f);
    }
    // What else is set, in a line over the field (the card's knobs show the gain).
    std::string line;
    char piece[48];
    const auto add = [&line](const std::string& item) { line += (line.empty() ? "" : "   ") + item; };
    if (large) {
        std::snprintf(piece, sizeof(piece), "%+.1f dB", static_cast<double>(values[utilityd::utilitaireGain]));
        add(piece);
    }
    if (values[utilityd::utilitaireDelay] > 0.0f) {
        std::snprintf(piece, sizeof(piece), "%s %.2f ms", L("retard", "delay"), static_cast<double>(values[utilityd::utilitaireDelay]));
        add(piece);
    }
    if (values[utilityd::utilitaireDcFilter] >= 0.5f)
        add("DC");
    if (mode != utilityd::utilitaireStereoMode)
        add(utilitaireModeName(mode).toStdString());
    if (!line.empty()) {
        const auto lineSize = textSize(line.c_str(), scale);
        text(list, ImVec2(middleX - lineSize.x * 0.5f, top - (large ? font * 1.3f : 0.0f)), large ? p.text : p.textDim, line.c_str(), scale);
    }
    // The field dragged: sideways the balance, up and down the width.
    if (field.width() > 1.0f && field.height() > 1.0f) {
        ImGui::SetCursorScreenPos(field.min);
        ImGui::InvisibleButton("##utilitaireField", ImVec2(field.width(), field.height()));
        auto& drag = dragState(parameters, pane);
        if (ImGui::IsItemActivated())
            drag.active = 0;
        if (ImGui::IsItemActive() && drag.active == 0 && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
            drag.active = std::abs(io.MouseDelta.x) >= std::abs(io.MouseDelta.y) ? 1 : 2;
        if (ImGui::IsItemActive() && drag.active == 1)
            changed |= apply(at(parameters, pane, utilityd::utilitairePan),
                             static_cast<double>(values[utilityd::utilitairePan]) + io.MouseDelta.x / std::max(1.0f, field.width() * 0.5f), set);
        if (ImGui::IsItemActive() && drag.active == 2)
            changed |= apply(at(parameters, pane, utilityd::utilitaireWidth),
                             static_cast<double>(values[utilityd::utilitaireWidth]) - io.MouseDelta.y / std::max(1.0f, field.height() * 0.5f), set);
        if (ImGui::IsItemDeactivated())
            drag.active = -1;
        if (ImGui::IsItemHovered()) {
            if (io.MouseWheel != 0.0f) {
                changed |= apply(at(parameters, pane, utilityd::utilitaireWidth),
                                 static_cast<double>(values[utilityd::utilitaireWidth]) + io.MouseWheel * 0.05, set);
                ui::takeWheel();
            }
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                changed |= apply(at(parameters, pane, utilityd::utilitairePan), 0.0, set);
                changed |= apply(at(parameters, pane, utilityd::utilitaireWidth), 1.0, set);
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            ImGui::SetTooltip("%s", L("Glisser de côté : la balance  ·  vers le haut : la largeur\nDouble clic : au centre, largeur d'origine",
                                      "Drag sideways: the balance  ·  upwards: the width\nDouble click: centred, width as it was"));
        }
    }
    return changed;
}

// ---- Stéréoscope -------------------------------------------------------------------------------

// What the display keeps between frames, by its parameters: the trail of
// pairs, the polar view's angles, the correlation over time.
struct StereoscopeMemory {
    static constexpr int historySize = 640;
    std::array<std::array<float, 2 * utilityd::stereoscopePairCount>, utilityd::stereoscopeTrailMost> frames {};
    std::array<double, utilityd::stereoscopeTrailMost> times {};
    int newest { -1 };
    int stored {};
    double fingerprint {};
    double lastAdded {};
    std::array<float, utilityd::stereoscopeAngleBins> angles {};
    std::array<float, historySize> history {};
    std::array<double, historySize> historyTimes {};
    int historyWrite {};
    int historyCount {};
    double lastHistory {};
};
StereoscopeMemory& stereoscopeMemoryOf(std::vector<Param>& parameters, const Pane& pane)
{
    static std::map<juce::String, StereoscopeMemory> memories;
    const auto* first = at(parameters, pane, 0);
    return memories[first != nullptr ? first->binding : juce::String("?")];
}

// The newest readings into the memory: a new set of pairs (at most ~70 a
// second), the angles they spread over, the correlation every 50 ms.
void stereoscopeFeed(StereoscopeMemory& memory, const Live& live)
{
    const auto* readings = utilityReadingsOf(live, utilityd::stereoscopeReadingCount);
    if (readings == nullptr)
        return;
    const auto now = ImGui::GetTime();
    const auto* pairs = readings + utilityd::stereoscopeReadingPairs;
    constexpr auto values = 2 * utilityd::stereoscopePairCount;
    auto fingerprint = static_cast<double>(readings[utilityd::stereoscopeReadingCorrelation]);
    for (int index = 0; index < values; index += 7)
        fingerprint += static_cast<double>(pairs[index]) * (index + 1);
    if (fingerprint != memory.fingerprint && now - memory.lastAdded >= 1.0 / 70.0) {
        memory.fingerprint = fingerprint;
        memory.lastAdded = now;
        memory.newest = (memory.newest + 1) % utilityd::stereoscopeTrailMost;
        memory.stored = std::min(memory.stored + 1, utilityd::stereoscopeTrailMost);
        auto& frame = memory.frames[static_cast<std::size_t>(memory.newest)];
        std::copy_n(pairs, values, frame.begin());
        memory.times[static_cast<std::size_t>(memory.newest)] = now;
        std::array<double, utilityd::stereoscopeAngleBins> energy {};
        auto total = 0.0;
        for (int pair = 0; pair < utilityd::stereoscopePairCount; ++pair) {
            const auto point = utilityd::stereoscopePointOf(pairs[2 * pair], pairs[2 * pair + 1]);
            const auto power = point.side * point.side + point.middle * point.middle;
            if (power < 1.0e-12)
                continue;
            const auto bin = std::clamp(static_cast<int>(utilityd::stereoscopeAngleOf(point) / std::numbers::pi * utilityd::stereoscopeAngleBins), 0,
                                        utilityd::stereoscopeAngleBins - 1);
            energy[static_cast<std::size_t>(bin)] += power;
            total += power;
        }
        for (std::size_t bin = 0; bin < energy.size(); ++bin) {
            const auto share = total > 0.0 ? static_cast<float>(energy[bin] / total) : 0.0f;
            memory.angles[bin] += (share - memory.angles[bin]) * 0.3f;
        }
    }
    if (now - memory.lastHistory >= 1.0 / utilityd::stereoscopeHistoryRate) {
        memory.lastHistory = now;
        memory.history[static_cast<std::size_t>(memory.historyWrite)] = readings[utilityd::stereoscopeReadingCorrelation];
        memory.historyTimes[static_cast<std::size_t>(memory.historyWrite)] = now;
        memory.historyWrite = (memory.historyWrite + 1) % StereoscopeMemory::historySize;
        memory.historyCount = std::min(memory.historyCount + 1, StereoscopeMemory::historySize);
    }
}

// A correlation as a bar from -1 to +1: red under 0, green over; `average`
// a mark on it.
void stereoscopeCorrelationBar(ImDrawList* list, const Box& bar, const float now, const float average, const bool known, const bool marks)
{
    const auto& p = theme::palette();
    list->AddRectFilled(bar.min, bar.max, theme::alpha(kDark, 0.7f), dp(2.0f));
    const auto middle = (bar.min.x + bar.max.x) * 0.5f;
    const auto xOf = [&bar](const float value) { return bar.min.x + (std::clamp(value, -1.0f, 1.0f) + 1.0f) * 0.5f * bar.width(); };
    if (known) {
        const auto x = xOf(now);
        list->AddRectFilled(ImVec2(std::min(x, middle), bar.min.y + dp(1.0f)), ImVec2(std::max(x, middle), bar.max.y - dp(1.0f)),
                            now < 0.0f ? kUtilityOutOfPhase : kUtilityInPhase, dp(1.5f));
        const auto mark = xOf(average);
        list->AddLine(ImVec2(mark, bar.min.y - dp(2.0f)), ImVec2(mark, bar.max.y + dp(2.0f)), p.text, dp(1.5f));
    }
    list->AddLine(ImVec2(middle, bar.min.y), ImVec2(middle, bar.max.y), p.textDim);
    if (marks)
        for (const auto value : { -0.5f, 0.5f })
            list->AddLine(ImVec2(xOf(value), bar.max.y - dp(3.0f)), ImVec2(xOf(value), bar.max.y), p.textFaint);
}

// The goniometer (or its polar view), its trail fading by the persistence;
// the wheel zooms.
bool stereoscopeScope(std::vector<Param>& parameters, const Pane& pane, const Box& square,
                      const std::array<float, utilityd::stereoscopeParameterCount>& values, const StereoscopeMemory& memory, const ImU32 colour,
                      const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    constexpr auto pi = std::numbers::pi_v<float>;
    const auto scale = large ? 0.7f : 0.56f;
    list->AddRectFilled(square.min, square.max, theme::alpha(kDark, 0.55f), dp(3.0f));
    const auto polar = whole(values[utilityd::stereoscopeDisplayMode], utilityd::stereoscopeDisplayModeCount) == 1;
    const auto zoom = std::clamp(values[utilityd::stereoscopeZoom], 0.5f, 4.0f);
    const auto size = square.width();
    // Where a pair goes: the middle up from `origin`, the side across.
    ImVec2 origin((square.min.x + square.max.x) * 0.5f, (square.min.y + square.max.y) * 0.5f);
    auto reach = size * 0.5f - dp(4.0f);
    if (polar) {
        reach = size * 0.5f - dp(6.0f);
        origin.y = square.min.y + (size + reach) * 0.5f;
        list->PathArcTo(origin, reach, pi, 2.0f * pi, 48);
        list->PathStroke(p.gridStrong, dp(1.0f));
        list->AddLine(ImVec2(origin.x - reach, origin.y), ImVec2(origin.x + reach, origin.y), p.gridStrong);
        list->AddLine(origin, ImVec2(origin.x, origin.y - reach), p.grid);
        for (const auto side : { -1.0f, 1.0f })
            dashedLine(list, origin, ImVec2(origin.x + side * reach * 0.7071f, origin.y - reach * 0.7071f), p.grid, dp(1.0f));
        // Where the energy goes: its share at each angle, from the foot.
        auto most = 0.0f;
        for (const auto share : memory.angles)
            most = std::max(most, share);
        if (most > 1.0e-6f) {
            std::vector<ImVec2> shape { origin };
            for (int bin = 0; bin < utilityd::stereoscopeAngleBins; ++bin) {
                const auto angle = (static_cast<float>(bin) + 0.5f) / static_cast<float>(utilityd::stereoscopeAngleBins) * pi;
                const auto length = reach * std::sqrt(memory.angles[static_cast<std::size_t>(bin)] / most);
                shape.emplace_back(origin.x + std::cos(angle) * length, origin.y - std::sin(angle) * length);
            }
            list->AddConcavePolyFilled(shape.data(), static_cast<int>(shape.size()), theme::alpha(colour, 0.3f));
            list->AddPolyline(shape.data(), static_cast<int>(shape.size()), colour, dp(large ? 1.6f : 1.2f), ImDrawFlags_Closed);
        }
    } else {
        list->AddCircle(origin, reach, p.grid, 64, dp(1.0f));
        list->AddLine(ImVec2(origin.x, origin.y - reach), ImVec2(origin.x, origin.y + reach), p.grid);
        list->AddLine(ImVec2(origin.x - reach, origin.y), ImVec2(origin.x + reach, origin.y), p.grid);
        for (const auto side : { -1.0f, 1.0f })
            dashedLine(list, ImVec2(origin.x - side * reach * 0.7071f, origin.y + reach * 0.7071f),
                       ImVec2(origin.x + side * reach * 0.7071f, origin.y - reach * 0.7071f), p.grid, dp(1.0f));
    }
    // The axes named.
    const auto font = ImGui::GetFontSize() * scale;
    if (large || size > dp(90.0f)) {
        centred(list, ImVec2(origin.x, origin.y - reach + font * 0.6f), p.textFaint, "M", scale);
        centred(list, ImVec2(origin.x - reach * 0.7071f + font * 0.7f, origin.y - reach * 0.7071f + font * 0.2f), p.textFaint, L("G", "L"), scale);
        centred(list, ImVec2(origin.x + reach * 0.7071f - font * 0.7f, origin.y - reach * 0.7071f + font * 0.2f), p.textFaint, L("D", "R"), scale);
        if (large) {
            centred(list, ImVec2(origin.x + reach - font * 0.8f, origin.y - font * 0.6f), p.textFaint, "+S", scale);
            centred(list, ImVec2(origin.x - reach + font * 0.8f, origin.y - font * 0.6f), p.textFaint, "-S", scale);
        }
    }
    // The pairs: full scale of a centred sound at the edge, times the zoom.
    const auto pixels = reach / std::numbers::sqrt2_v<float> * zoom;
    const auto window = utilityd::stereoscopeTrailSecondsOf(values[utilityd::stereoscopePersistence]);
    const auto now = ImGui::GetTime();
    const auto dot = dp(large ? 1.1f : 0.8f);
    list->PushClipRect(square.min, square.max, true);
    for (int age = memory.stored - 1; age >= 0; --age) {
        const auto index = static_cast<std::size_t>((memory.newest - age + utilityd::stereoscopeTrailMost) % utilityd::stereoscopeTrailMost);
        const auto fade = utilityd::stereoscopeFadeOf(now - memory.times[index], window);
        if (fade <= 0.0f)
            continue;
        const auto tint = theme::alpha(polar ? p.text : colour, (polar ? 0.35f : 0.9f) * fade);
        const auto& frame = memory.frames[index];
        for (int pair = 0; pair < utilityd::stereoscopePairCount; ++pair) {
            auto point = utilityd::stereoscopePointOf(frame[static_cast<std::size_t>(2 * pair)], frame[static_cast<std::size_t>(2 * pair + 1)]);
            if (polar && point.middle < 0.0) {
                point.side = -point.side;
                point.middle = -point.middle;
            }
            const ImVec2 place(origin.x + static_cast<float>(point.side) * pixels, origin.y - static_cast<float>(point.middle) * pixels);
            list->AddRectFilled(ImVec2(place.x - dot, place.y - dot), ImVec2(place.x + dot, place.y + dot), tint);
        }
    }
    list->PopClipRect();
    // The wheel zooms, a double click shows it whole again.
    auto changed = false;
    ImGui::SetCursorScreenPos(square.min);
    ImGui::InvisibleButton("##stereoscopeScope", ImVec2(std::max(1.0f, square.width()), std::max(1.0f, square.height())));
    if (ImGui::IsItemHovered()) {
        auto* zoomParameter = at(parameters, pane, utilityd::stereoscopeZoom);
        if (io.MouseWheel != 0.0f && zoomParameter != nullptr) {
            changed |= apply(zoomParameter, zoomParameter->value * std::exp(static_cast<double>(io.MouseWheel) * 0.1), set);
            ui::takeWheel();
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && zoomParameter != nullptr)
            changed |= apply(zoomParameter, zoomParameter->defaultValue, set);
        ImGui::SetTooltip("%s", L("Molette : zoom  ·  double clic : zoom d'origine", "Wheel: zoom  ·  double click: zoom back"));
    }
    return changed;
}

// The correlation over the last seconds: green over 0, red under.
void stereoscopeHistoryPlot(ImDrawList* list, const Box& plot, const StereoscopeMemory& memory, const double seconds, const ImU32 colour,
                            const bool large)
{
    const auto& p = theme::palette();
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.55f), dp(3.0f));
    const auto middle = (plot.min.y + plot.max.y) * 0.5f;
    const auto yOf = [&plot, middle](const float value) { return middle - std::clamp(value, -1.0f, 1.0f) * plot.height() * 0.45f; };
    list->AddLine(ImVec2(plot.min.x, middle), ImVec2(plot.max.x, middle), p.gridStrong);
    for (const auto value : { -1.0f, 1.0f })
        list->AddLine(ImVec2(plot.min.x, yOf(value)), ImVec2(plot.max.x, yOf(value)), p.grid);
    if (large) {
        text(list, ImVec2(plot.min.x + dp(3.0f), yOf(1.0f) - ImGui::GetFontSize() * 0.3f), p.textFaint, "+1", 0.56f);
        text(list, ImVec2(plot.min.x + dp(3.0f), yOf(-1.0f) - ImGui::GetFontSize() * 0.3f), p.textFaint, "-1", 0.56f);
    }
    const auto now = ImGui::GetTime();
    const auto span = std::max(1.0, seconds);
    // Oldest first, a gap (the sound stopped) breaks the line.
    std::vector<ImVec2> points;
    const auto flush = [&]() {
        if (points.size() >= 2) {
            std::vector<ImVec2> over;
            std::vector<ImVec2> under;
            for (const auto& point : points) {
                over.emplace_back(point.x, std::min(point.y, middle));
                under.emplace_back(point.x, std::max(point.y, middle));
            }
            fillUnder(list, over, middle, theme::alpha(kUtilityInPhase, 0.25f));
            fillUnder(list, under, middle, theme::alpha(kUtilityOutOfPhase, 0.3f));
            list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(large ? 1.5f : 1.1f));
        }
        points.clear();
    };
    auto last = -1.0;
    for (int index = 0; index < memory.historyCount; ++index) {
        const auto slot = static_cast<std::size_t>((memory.historyWrite - memory.historyCount + index + StereoscopeMemory::historySize)
                                                   % StereoscopeMemory::historySize);
        const auto time = memory.historyTimes[slot];
        if (now - time > span)
            continue;
        if (last >= 0.0 && time - last > 0.3)
            flush();
        last = time;
        points.emplace_back(plot.max.x - static_cast<float>((now - time) / span) * plot.width(), yOf(memory.history[slot]));
    }
    flush();
}

// The stereoscope's picture: the goniometer (or its polar view), the
// correlation bar and the bands', the balance and the width, and, large,
// the correlation over time.
bool stereoscopeView(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live, const Setter& set,
                     const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<utilityd::stereoscopeParameterCount>(parameters, pane, "stereoscope");
    auto& memory = stereoscopeMemoryOf(parameters, pane);
    stereoscopeFeed(memory, live);
    const auto* readings = utilityReadingsOf(live, utilityd::stereoscopeReadingCount);
    const auto scale = large ? 0.7f : 0.56f;
    const auto font = ImGui::GetFontSize() * scale;
    auto changed = false;
    const auto top = box.min.y + dp(large ? 28.0f : 4.0f);
    const auto bottom = box.max.y - dp(large ? 8.0f : 4.0f);
    const auto side = std::max(dp(20.0f), std::min(bottom - top, box.width() * (large ? 0.5f : 0.48f)));
    const Box square { ImVec2(box.min.x + dp(large ? 8.0f : 4.0f), top), ImVec2(box.min.x + dp(large ? 8.0f : 4.0f) + side, top + side) };
    changed |= stereoscopeScope(parameters, pane, square, values, memory, colour, set, large);
    if (readings == nullptr && large)
        centred(list, ImVec2((square.min.x + square.max.x) * 0.5f, square.max.y - font * 1.4f), p.textFaint,
                L("En attente du son", "Waiting for sound"), scale);
    // The column beside it, when there is room for one.
    const Box column { ImVec2(square.max.x + dp(large ? 16.0f : 8.0f), top), ImVec2(box.max.x - dp(large ? 8.0f : 4.0f), bottom) };
    if (column.width() < dp(40.0f))
        return changed;
    auto y = column.min.y;
    // The view, to choose.
    if (large) {
        changed |= segmented("##stereoscopeMode", at(parameters, pane, utilityd::stereoscopeDisplayMode),
                             { L("Goniomètre", "Goniometer"), L("Polaire", "Polar") }, ImVec2(column.min.x, y),
                             std::min(dp(240.0f), column.width()), dp(22.0f), colour, set,
                             L("Goniomètre : les échantillons en nuage. Polaire : la répartition de l'énergie selon l'angle.",
                               "Goniometer: the samples as a cloud. Polar: how the energy spreads over the angles."));
        y += dp(32.0f);
    } else {
        changed |= chooser("##stereoscopeMode", at(parameters, pane, utilityd::stereoscopeDisplayMode), utilityd::stereoscopeDisplayModeCount,
                           stereoscopeModeName, stereoscopeModeHint, ImVec2(column.min.x, y), colour, set, 0.56f);
        y += font + dp(8.0f);
    }
    // The correlation, now and averaged.
    const auto correlation = readings != nullptr ? readings[utilityd::stereoscopeReadingCorrelation] : 0.0f;
    const auto average = readings != nullptr ? readings[utilityd::stereoscopeReadingCorrelationAverage] : 0.0f;
    char label[64];
    if (readings != nullptr)
        std::snprintf(label, sizeof(label), "%s %+.2f", L("Corrélation", "Correlation"), static_cast<double>(correlation));
    else
        std::snprintf(label, sizeof(label), "%s --", L("Corrélation", "Correlation"));
    text(list, ImVec2(column.min.x, y), readings != nullptr && correlation < 0.0f ? kUtilityOutOfPhase : p.text, label, large ? 0.8f : scale);
    if (large && readings != nullptr) {
        std::snprintf(label, sizeof(label), "%s %+.2f", L("moyenne", "average"), static_cast<double>(average));
        const auto size = textSize(label, scale);
        text(list, ImVec2(column.max.x - size.x, y + dp(2.0f)), p.textDim, label, scale);
    }
    y += (large ? ImGui::GetFontSize() * 0.8f : font) + dp(4.0f);
    stereoscopeCorrelationBar(list, Box { ImVec2(column.min.x, y), ImVec2(column.max.x, y + dp(large ? 16.0f : 10.0f)) }, correlation, average,
                              readings != nullptr, large);
    if (large) {
        y += dp(18.0f);
        text(list, ImVec2(column.min.x, y), p.textFaint, "-1", 0.56f);
        centred(list, ImVec2((column.min.x + column.max.x) * 0.5f, y + ImGui::GetFontSize() * 0.3f), p.textFaint, "0", 0.56f);
        const auto plus = textSize("+1", 0.56f);
        text(list, ImVec2(column.max.x - plus.x, y), p.textFaint, "+1", 0.56f);
        y += ImGui::GetFontSize() * 0.6f + dp(8.0f);
    } else {
        y += dp(14.0f);
    }
    // The bands' correlations.
    const std::array<const char*, 3> bandNames { L("Graves", "Low"), L("Médiums", "Mid"), L("Aigus", "High") };
    const auto labelWidth = large ? dp(64.0f) : dp(34.0f);
    const auto bandHeight = dp(large ? 10.0f : 6.0f);
    for (std::size_t band = 0; band < bandNames.size(); ++band) {
        if (y + bandHeight > column.max.y)
            break;
        const auto value = readings != nullptr ? readings[utilityd::stereoscopeReadingLowBand + static_cast<int>(band)] : 0.0f;
        text(list, ImVec2(column.min.x, y + bandHeight * 0.5f - font * 0.55f), p.textDim, bandNames[band], scale);
        stereoscopeCorrelationBar(list, Box { ImVec2(column.min.x + labelWidth, y), ImVec2(column.max.x, y + bandHeight) }, value, value,
                                  readings != nullptr, false);
        y += bandHeight + dp(large ? 8.0f : 4.0f);
    }
    // The balance and the width.
    if (y + font < column.max.y) {
        if (readings != nullptr) {
            const auto balance = readings[utilityd::stereoscopeReadingBalance];
            const auto* louder = std::abs(balance) < 0.05f ? "" : balance > 0.0f ? L(" D", " R") : L(" G", " L");
            std::snprintf(label, sizeof(label), "%s %.1f dB%s   %s %.2f", L("Balance", "Balance"), static_cast<double>(std::abs(balance)), louder,
                          L("Largeur", "Width"), static_cast<double>(readings[utilityd::stereoscopeReadingWidth]));
        } else {
            std::snprintf(label, sizeof(label), "%s --   %s --", L("Balance", "Balance"), L("Largeur", "Width"));
        }
        text(list, ImVec2(column.min.x, y), p.textDim, label, scale);
        y += font + dp(large ? 10.0f : 4.0f);
    }
    // The correlation over time.
    if (large && column.max.y - y > dp(50.0f)) {
        const auto seconds = std::clamp(static_cast<double>(values[utilityd::stereoscopeHistory]), 1.0, 30.0);
        std::snprintf(label, sizeof(label), "%s  %.0f s", L("CORRÉLATION DANS LE TEMPS", "CORRELATION OVER TIME"), seconds);
        section(list, ImVec2(column.min.x, y), label);
        y += ImGui::GetFontSize() * 0.8f + dp(4.0f);
        stereoscopeHistoryPlot(list, Box { ImVec2(column.min.x, y), column.max }, memory, seconds, colour, large);
    }
    return changed;
}

// ---- The entry points --------------------------------------------------------------------------

// A card's display (large: the window's), true when a value changed.
bool utilityDisplay(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour,
                    const Live& live, const Setter& set, const bool large)
{
    if (kind == PaneKind::utilitaire)
        return utilitaireView(parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::stereoscope)
        return stereoscopeView(parameters, pane, box, colour, live, set, large);
    return false;
}

// The window: its display large and every control.
bool utilityLarge(const PaneKind kind, const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size,
                  const ImU32 colour, const Live& live, const Setter& set)
{
    const auto display = [&](const Box& box) { return utilityDisplay(kind, parameters, pane, box, colour, live, set, true); };
    if (kind == PaneKind::utilitaire)
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("UTILITAIRE  ·  les réglages de base d'une piste", "UTILITY  ·  a track's basic settings"),
                          { { L("CANAUX", "CHANNELS"),
                              { utilityd::utilitaireChannelMode, utilityd::utilitaireInvertLeft, utilityd::utilitaireInvertRight } },
                            { L("IMAGE", "IMAGE"), { utilityd::utilitaireWidth, utilityd::utilitairePan, utilityd::utilitaireMonoBass } },
                            { L("NIVEAU", "LEVEL"), { utilityd::utilitaireGain, utilityd::utilitaireMute } },
                            { L("NETTOYAGE ET CALAGE", "CLEAN-UP AND ALIGNMENT"), { utilityd::utilitaireDcFilter, utilityd::utilitaireDelay } } },
                          display, 230.0f);
    if (kind == PaneKind::stereoscope)
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("STÉRÉOSCOPE  ·  l'image stéréo", "STEREOSCOPE  ·  the stereo image"),
                          { { L("AFFICHAGE", "DISPLAY"),
                              { utilityd::stereoscopeZoom, utilityd::stereoscopePersistence, utilityd::stereoscopeHistory } } },
                          display, 300.0f);
    return false;
}

// The values a card shows as knobs (pane indices).
std::vector<int> utilityCardFields(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane)
{
    juce::ignoreUnused(parameters, pane);
    if (kind == PaneKind::utilitaire)
        return { utilityd::utilitaireGain, utilityd::utilitairePan, utilityd::utilitaireWidth, utilityd::utilitaireMonoBass };
    if (kind == PaneKind::stereoscope)
        return { utilityd::stereoscopeZoom, utilityd::stereoscopePersistence };
    return {};
}

// A value as these effects show it; empty for none of theirs.
juce::String utilityFormat(const juce::String& binding, const double value)
{
    if (binding.contains("utilitaire.")) {
        const auto suffix = binding.fromLastOccurrenceOf("utilitaire.", false, false);
        if (suffix == "channelMode")
            return utilitaireModeName(whole(static_cast<float>(value), utilityd::utilitaireChannelModeCount));
        if (suffix == "invertLeft" || suffix == "invertRight")
            return value >= 0.5 ? ui::Ls("Inversée", "Inverted") : ui::Ls("Normale", "Normal");
        if (suffix == "dcFilter")
            return value >= 0.5 ? ui::Ls("Actif", "On") : ui::Ls("Coupé", "Off");
        if (suffix == "mute")
            return value >= 0.5 ? ui::Ls("Muet", "Muted") : ui::Ls("Joue", "Playing");
        if (suffix == "monoBass")
            return utilityd::utilitaireMonoBassOn(static_cast<float>(value)) ? juce::String(juce::roundToInt(value)) + " Hz"
                                                                              : ui::Ls("Coupé", "Off");
        if (suffix == "pan") {
            if (std::abs(value) < 0.005)
                return "C";
            return juce::String(juce::roundToInt(std::abs(value) * 100.0)) + (value < 0.0 ? ui::Ls(" G", " L") : ui::Ls(" D", " R"));
        }
        if (suffix == "width")
            return value < 0.005 ? juce::String("Mono") : percent(value);
        if (suffix == "delay")
            return value <= 0.0 ? ui::Ls("Aucun", "None") : juce::String(value, 2) + " ms";
        return {};
    }
    if (binding.contains("stereoscope.")) {
        const auto suffix = binding.fromLastOccurrenceOf("stereoscope.", false, false);
        if (suffix == "displayMode")
            return stereoscopeModeName(whole(static_cast<float>(value), utilityd::stereoscopeDisplayModeCount));
        if (suffix == "zoom")
            return juce::String::fromUTF8("\xc3\x97") + juce::String(value, 2);
        if (suffix == "persistence")
            return percent(value);
        return {};
    }
    return {};
}

// A parameter of theirs that takes whole steps (a choice, a switch).
bool utilityStepped(const juce::String& binding)
{
    for (const auto* suffix : { "utilitaire.channelMode", "utilitaire.invertLeft", "utilitaire.invertRight", "utilitaire.dcFilter",
                                "utilitaire.mute", "stereoscope.displayMode" })
        if (binding.endsWith(suffix))
            return true;
    return false;
}
