// The displays and large views of Grains, Saccade, Boucleur
// (RhythmDesign.h computes what they draw). Included by StudioEffectsView.cpp
// inside its anonymous namespace: it has its helpers (knobCell, segmented,
// chooser, comboCell, switchCell, frame, title, section, text, fillUnder…)
// and addedLarge(), the common window layout.

namespace rhythmd = engine::rhythm;

// The grains' dots, and what plays backwards.
constexpr ImU32 kRhythmGrain = IM_COL32(255, 214, 140, 255);
constexpr ImU32 kRhythmBackwards = IM_COL32(140, 190, 255, 255);

juce::String rhythmOnOff(const double value)
{
    return value >= 0.5 ? ui::Ls("Actif", "On") : ui::Ls("Coupé", "Off");
}

// A note value's name ("1/16"); 0 is free where a value may be.
juce::String rhythmNoteName(const double value)
{
    if (const auto* division = engine::noteDivisionFor(static_cast<float>(value)))
        return juce::String(std::string(division->name));
    return ui::Ls("Libre", "Free");
}

juce::String rhythmChoiceName(const rhythmd::RhythmChoice& choice)
{
    return juce::String::fromUTF8(ui::french() ? choice.french : choice.english);
}

// The processor's readings when it gave them all, else none.
const float* rhythmReadings(const Live& live, const int count)
{
    return live.readings != nullptr && live.readingCount >= count ? live.readings : nullptr;
}

// A small clickable label in a display's top band (a switch, a button);
// true when clicked. `lit` fills it.
bool rhythmChip(const char* id, const char* label, const ImVec2 min, const float scale, const ImU32 colour, const bool lit,
                const char* tooltip, Param* parameter)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto size = textSize(label, scale);
    const ImVec2 max(min.x + size.x + dp(12.0f), min.y + size.y + dp(4.0f));
    ImGui::SetCursorScreenPos(min);
    const auto clicked = ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
    const auto hovered = ImGui::IsItemHovered();
    if (hovered && tooltip != nullptr)
        ImGui::SetTooltip("%s", tooltip);
    if (parameter != nullptr && ImGui::IsItemClicked(ImGuiMouseButton_Right))
        openParameterMenu(*parameter);
    list->AddRectFilled(min, max, lit ? colour : theme::alpha(colour, hovered ? 0.35f : 0.18f), dp(3.0f));
    text(list, ImVec2(min.x + dp(6.0f), min.y + dp(2.0f)), lit ? kDark : p.text, label, scale);
    return clicked;
}

// ---- Grains ---------------------------------------------------------------------------------------

// The past from now (right) back (left): where the grains start (the time, a
// line to drag at the top) and how far they scatter (the spray, its edge to
// drag at the bottom); the grains themselves as dots, as loud as they are big,
// high or low as they sit left or right.
bool rhythmGrainsDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                         const Setter& set, const bool large)
{
    using namespace rhythmd;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<grainsParameterCount>(parameters, pane, "grains");
    const auto delayMs = grainsDelayMsOf(values[grainsTime], values[grainsSync], live.tempo);
    const auto sprayMs = static_cast<double>(std::max(0.0f, values[grainsSpray]));
    const auto sizeMs = static_cast<double>(std::max(10.0f, values[grainsSize]));
    const auto frozen = values[grainsFreeze] >= 0.5f;
    const auto scale = large ? 0.68f : 0.56f;
    auto changed = false;
    // Freeze, a switch at the top right.
    auto chipLeft = box.max.x;
    {
        auto* freeze = at(parameters, pane, grainsFreeze);
        const auto* label = L("GEL", "FREEZE");
        const ImVec2 min(box.max.x - textSize(label, scale).x - dp(18.0f), box.min.y + dp(2.0f));
        chipLeft = min.x;
        if (rhythmChip("##grainsFreeze", label, min, scale, kRhythmBackwards, frozen,
                       L("Gel : plus rien n'entre, le tampon tourne tel quel", "Freeze: nothing more comes in, the buffer loops as it is"),
                       freeze))
            changed |= apply(freeze, frozen ? 0.0 : 1.0, set);
    }
    const Box plot { ImVec2(box.min.x + dp(6.0f), box.min.y + dp(large ? 28.0f : 20.0f)),
                     ImVec2(box.max.x - dp(6.0f), box.max.y - dp(large ? 18.0f : 5.0f)) };
    if (plot.width() < dp(24.0f) || plot.height() < dp(16.0f))
        return changed;
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.5f), dp(3.0f));
    if (frozen)
        list->AddRectFilled(plot.min, plot.max, theme::alpha(kRhythmBackwards, 0.08f), dp(3.0f));
    // As far back as the grains read, and a little more.
    const auto span = std::clamp((delayMs + sprayMs + sizeMs) * 1.3 * 0.001, 0.25, grainsBufferSeconds);
    const auto xOf = [&plot, span](const double seconds) {
        return plot.max.x - static_cast<float>(std::clamp(seconds / span, 0.0, 1.0)) * plot.width();
    };
    const auto secondsAt = [&plot, span](const float x) {
        return static_cast<double>(std::clamp((plot.max.x - x) / std::max(1.0f, plot.width()), 0.0f, 1.0f)) * span;
    };
    const auto step = span <= 0.3 ? 0.05 : span <= 0.8 ? 0.1 : span <= 2.0 ? 0.25 : 0.5;
    for (auto t = step; t < span - 1.0e-6; t += step) {
        const auto x = xOf(t);
        list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y), p.grid);
        if (large) {
            char mark[16];
            std::snprintf(mark, sizeof(mark), t < 1.0 ? "%.0f ms" : "%.2g s", t < 1.0 ? t * 1'000.0 : t);
            text(list, ImVec2(x - textSize(mark, 0.56f).x * 0.5f, plot.max.y + dp(2.0f)), p.textFaint, mark, 0.56f);
        }
    }
    if (large)
        text(list, ImVec2(plot.max.x - textSize(L("maintenant", "now"), 0.56f).x, plot.max.y + dp(2.0f)), p.textDim, L("maintenant", "now"),
             0.56f);
    // The stereo field: left above the middle, right below.
    const auto middle = (plot.min.y + plot.max.y) * 0.5f;
    const auto reach = std::max(dp(2.0f), plot.height() * 0.5f - dp(large ? 9.0f : 6.0f));
    dashedLine(list, ImVec2(plot.min.x, middle), ImVec2(plot.max.x, middle), theme::alpha(p.textFaint, 0.45f));
    // The spray band, from the time back.
    const auto stereo = std::clamp(values[grainsStereo], 0.0f, 1.0f);
    const auto delayX = xOf(delayMs * 0.001);
    const auto sprayX = xOf((delayMs + sprayMs) * 0.001);
    const auto bandReach = reach * std::max(0.12f, stereo) + dp(4.0f);
    list->AddRectFilled(ImVec2(std::min(sprayX, delayX - dp(1.0f)), middle - bandReach), ImVec2(delayX, middle + bandReach),
                        theme::alpha(colour, 0.16f), dp(2.0f));
    list->AddLine(ImVec2(sprayX, middle - bandReach), ImVec2(sprayX, middle + bandReach), theme::alpha(colour, 0.5f), dp(1.0f));
    list->AddLine(ImVec2(delayX, plot.min.y), ImVec2(delayX, plot.max.y), theme::alpha(colour, 0.95f), dp(1.6f));
    // The grains: live while the sound runs, else how they would scatter.
    const auto* readings = live.running ? rhythmReadings(live, grainsReadingCount) : nullptr;
    auto sounding = 0;
    if (readings != nullptr) {
        sounding = static_cast<int>(std::lround(readings[0]));
        for (int grain = 0; grain < grainsPoolSize; ++grain) {
            const auto level = std::clamp(readings[grainsReadingIndexOf(grain, grainsReadingLevel)], 0.0f, 1.0f);
            if (level <= 0.002f)
                continue;
            const auto seconds = static_cast<double>(readings[grainsReadingIndexOf(grain, grainsReadingPosition)]) * grainsBufferSeconds;
            const auto pan = std::clamp(readings[grainsReadingIndexOf(grain, grainsReadingPan)], -1.0f, 1.0f);
            list->AddCircleFilled(ImVec2(xOf(seconds), middle + pan * reach), dp(1.5f + level * (large ? 6.0f : 3.5f)),
                                  theme::alpha(kRhythmGrain, 0.3f + 0.65f * level));
        }
    } else {
        const auto density = std::clamp(static_cast<double>(values[grainsDensity]), 1.0, 100.0);
        const auto shown = std::clamp(static_cast<int>(density * sizeMs * 0.001 * 3.0) + 4, 4, 24);
        for (int grain = 0; grain < shown; ++grain) {
            const auto along = std::fmod(static_cast<double>(grain) * 0.6180339887 + 0.11, 1.0);
            const auto side = std::fmod(static_cast<double>(grain) * 0.4142135624 + 0.37, 1.0) * 2.0 - 1.0;
            const auto level = 0.35 + 0.6 * std::fmod(static_cast<double>(grain) * 0.7320508076 + 0.5, 1.0);
            const auto seconds = (delayMs + sprayMs * along) * 0.001;
            list->AddCircleFilled(ImVec2(xOf(seconds), middle + static_cast<float>(side) * stereo * reach),
                                  dp(1.5f + static_cast<float>(level) * (large ? 4.0f : 2.5f)), theme::alpha(kRhythmGrain, 0.3f));
        }
    }
    // The handles: the time at the top, the spray's edge at the bottom.
    const auto mouse = ImGui::GetIO().MousePos;
    auto& drag = dragState(parameters, pane);
    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton("##grainsPlot", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    const auto hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated())
        drag.active = mouse.y < middle ? 0 : 1;
    if (ImGui::IsItemActive() && drag.active >= 0) {
        const auto seconds = secondsAt(mouse.x);
        if (drag.active == 0) {
            if (engine::noteDivisionFor(values[grainsSync]) != nullptr) {
                // Synced: the note value nearest.
                auto nearest = 1;
                auto distance = 1.0e9;
                for (int index = 1; index <= static_cast<int>(engine::noteDivisions.size()); ++index) {
                    const auto ms = engine::noteDivisions[static_cast<std::size_t>(index - 1)].beats * 60'000.0 / std::max(20.0, live.tempo);
                    const auto apart = std::abs(std::log(ms) - std::log(std::max(1.0, seconds * 1'000.0)));
                    if (apart < distance) {
                        distance = apart;
                        nearest = index;
                    }
                }
                changed |= apply(at(parameters, pane, grainsSync), nearest, set);
            } else {
                changed |= apply(at(parameters, pane, grainsTime), seconds * 1'000.0, set);
            }
        } else {
            changed |= apply(at(parameters, pane, grainsSpray), seconds * 1'000.0 - delayMs, set);
        }
    } else if (!ImGui::IsItemActive()) {
        drag.active = -1;
    }
    handleDot(list, ImVec2(delayX, plot.min.y + dp(7.0f)), colour, hovered && mouse.y < middle);
    handleDot(list, ImVec2(sprayX, plot.max.y - dp(7.0f)), theme::alpha(colour, 0.7f), hovered && mouse.y >= middle);
    if (hovered && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", L("Glisser en haut : le temps ; en bas : la dispersion", "Drag at the top: the time; at the bottom: the spray"));
    // What it says: the time, the pitch, the grains sounding.
    char label[96];
    const auto* division = engine::noteDivisionFor(values[grainsSync]);
    const auto pitch = values[grainsPitch];
    const auto pitchRandom = values[grainsPitchRandom];
    char pitchText[32];
    if (pitchRandom > 0.05f)
        std::snprintf(pitchText, sizeof(pitchText), "%+.1f st \xc2\xb1%.1f", static_cast<double>(pitch), static_cast<double>(pitchRandom));
    else
        std::snprintf(pitchText, sizeof(pitchText), "%+.1f st", static_cast<double>(pitch));
    if (division != nullptr)
        std::snprintf(label, sizeof(label), "%s  %.0f ms   %s", std::string(division->name).c_str(), delayMs, pitchText);
    else
        std::snprintf(label, sizeof(label), "%.0f ms   %s", delayMs, pitchText);
    // The top band's left holds the title: the label goes beside the switch.
    text(list, ImVec2(chipLeft - textSize(label, scale).x - dp(large ? 12.0f : 6.0f), box.min.y + dp(large ? 4.0f : 3.0f)), p.textDim, label,
         scale);
    if (large && readings != nullptr) {
        char count[32];
        std::snprintf(count, sizeof(count), "%d grains", sounding);
        text(list, ImVec2(plot.min.x + dp(4.0f), plot.min.y + dp(3.0f)), p.textFaint, count, 0.6f);
    }
    return changed;
}

// ---- Saccade --------------------------------------------------------------------------------------

// One interval as its sixteenths: where the repeats begin (the offset, to drag
// at the top), how long they last (the gate, its end to drag), the slices
// that fill it as loud as each repeat (the first the sound itself, outlined;
// those that may play backwards hatched; the pitch falling as a line), and the
// play head while the transport runs.
bool rhythmSaccadeDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                          const Setter& set, const bool large)
{
    using namespace rhythmd;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<saccadeParameterCount>(parameters, pane, "saccade");
    const auto intervalBeats = std::max(0.125, saccadeIntervalBeatsOf(values[saccadeInterval], 4, 4));
    const auto offsetBeats = saccadeOffsetBeatsOf(values[saccadeOffset], intervalBeats);
    const auto gateBeats = saccadeGateBeatsOf(values[saccadeGate], intervalBeats);
    const auto gridBeats = std::max(0.05, saccadeNoteBeatsOf(values[saccadeGrid]));
    const auto* readings = rhythmReadings(live, saccadeReadingCount);
    const auto repeating = readings != nullptr && readings[saccadeReadingRepeating] >= 0.5f;
    const auto scale = large ? 0.68f : 0.56f;
    auto changed = false;
    // The repeat button: held, it repeats. Big at the right in the window, a
    // chip at the top on the card.
    auto* repeat = at(parameters, pane, saccadeRepeat);
    const auto held = values[saccadeRepeat] >= 0.5f;
    const auto buttonWidth = large ? std::clamp(box.width() * 0.2f, dp(96.0f), dp(160.0f)) : 0.0f;
    {
        const auto* label = L("RÉPÉTER", "REPEAT");
        Box button;
        if (large) {
            button = { ImVec2(box.max.x - dp(6.0f) - buttonWidth, box.min.y + dp(28.0f)), ImVec2(box.max.x - dp(6.0f), box.max.y - dp(18.0f)) };
        } else {
            const auto size = textSize(label, scale);
            button = { ImVec2(box.max.x - size.x - dp(18.0f), box.min.y + dp(2.0f)), ImVec2(box.max.x - dp(6.0f), box.min.y + dp(6.0f) + size.y) };
        }
        if (button.width() > dp(4.0f) && button.height() > dp(4.0f)) {
            ImGui::SetCursorScreenPos(button.min);
            ImGui::InvisibleButton("##saccadeRepeat", ImVec2(button.width(), button.height()));
            const auto pressing = ImGui::IsItemActive();
            if (pressing && !held)
                changed |= apply(repeat, 1.0, set);
            if (ImGui::IsItemDeactivated())
                changed |= apply(repeat, 0.0, set);
            const auto hot = ImGui::IsItemHovered();
            if (hot && !pressing)
                ImGui::SetTooltip("%s", L("Maintenir : répète tout de suite, transport lancé ou non", "Hold: repeats at once, transport running or not"));
            if (repeat != nullptr && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                openParameterMenu(*repeat);
            const auto lit = held || pressing;
            list->AddRectFilled(button.min, button.max, lit ? colour : hot ? p.bg4 : p.bg3, dp(large ? 8.0f : 3.0f));
            if (large)
                list->AddRect(button.min, button.max, theme::alpha(colour, 0.7f), dp(8.0f), dp(1.5f));
            const ImVec2 centre((button.min.x + button.max.x) * 0.5f, (button.min.y + button.max.y) * 0.5f);
            centred(list, ImVec2(centre.x, centre.y - (large && repeating ? dp(9.0f) : 0.0f)), lit ? kDark : p.text, label, large ? 0.9f : scale);
            if (large && repeating) {
                char done[24];
                std::snprintf(done, sizeof(done), "\xc3\x97 %d", static_cast<int>(std::lround(readings[saccadeReadingRepeats])));
                centred(list, ImVec2(centre.x, centre.y + dp(11.0f)), lit ? kDark : p.textDim, done, 0.7f);
            }
        }
    }
    const Box plot { ImVec2(box.min.x + dp(6.0f), box.min.y + dp(large ? 28.0f : 20.0f)),
                     ImVec2(box.max.x - dp(6.0f) - (large ? buttonWidth + dp(10.0f) : 0.0f), box.max.y - dp(large ? 18.0f : 5.0f)) };
    if (plot.width() < dp(24.0f) || plot.height() < dp(16.0f))
        return changed;
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.5f), dp(3.0f));
    const auto xOf = [&plot, intervalBeats](const double beats) {
        return plot.min.x + static_cast<float>(std::clamp(beats / intervalBeats, 0.0, 1.0)) * plot.width();
    };
    const auto beatsAt = [&plot, intervalBeats](const float x) {
        return static_cast<double>(std::clamp((x - plot.min.x) / std::max(1.0f, plot.width()), 0.0f, 1.0f)) * intervalBeats;
    };
    // The sixteenths, the beats stronger (numbered in the window).
    const auto sixteenths = static_cast<int>(std::ceil(intervalBeats * 4.0 - 1.0e-6));
    if (sixteenths <= 64)
        for (int at16 = 1; at16 < sixteenths; ++at16) {
            const auto x = xOf(at16 * 0.25);
            list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y), at16 % 4 == 0 ? p.gridStrong : p.grid);
        }
    if (large)
        for (int beat = 0; beat * 1.0 < intervalBeats - 1.0e-6; ++beat) {
            char number[8];
            std::snprintf(number, sizeof(number), "%d", beat + 1);
            text(list, ImVec2(xOf(beat) + dp(3.0f), plot.max.y + dp(2.0f)), p.textFaint, number, 0.56f);
        }
    // The region: from the offset for the gate (what runs past the interval's
    // end shown at its start).
    const auto end = offsetBeats + gateBeats;
    const auto regionTint = theme::alpha(colour, repeating ? 0.2f : 0.09f);
    list->AddRectFilled(ImVec2(xOf(offsetBeats), plot.min.y), ImVec2(xOf(std::min(end, intervalBeats)), plot.max.y), regionTint);
    if (end > intervalBeats + 1.0e-6)
        list->AddRectFilled(ImVec2(xOf(0.0), plot.min.y), ImVec2(xOf(end - intervalBeats), plot.max.y), theme::alpha(colour, 0.06f));
    const auto bottom = plot.max.y - dp(3.0f);
    const auto tallest = std::max(dp(4.0f), plot.height() - dp(large ? 20.0f : 13.0f));
    std::vector<ImVec2> pitchLine;
    auto repeat16 = 0;
    for (auto start = offsetBeats; start < end - 1.0e-9 && repeat16 < 64; start += gridBeats, ++repeat16) {
        const auto stop = std::min(start + gridBeats, end);
        const auto from = std::fmod(start, intervalBeats);
        const auto to = std::min(from + (stop - start), intervalBeats);
        const auto x0 = xOf(from) + dp(1.0f);
        const auto x1 = std::max(x0 + dp(1.0f), xOf(to) - dp(1.0f));
        const auto level = static_cast<float>(saccadeRepeatGainOf(values[saccadeDecay], repeat16));
        const auto top = bottom - std::max(dp(1.0f), level * tallest);
        if (repeat16 == 0) {
            // The slice caught: the sound itself.
            list->AddRect(ImVec2(x0, top), ImVec2(x1, bottom), theme::alpha(colour, 0.95f), dp(2.0f), dp(1.3f));
        } else {
            list->AddRectFilled(ImVec2(x0, top), ImVec2(x1, bottom), theme::alpha(colour, 0.25f + 0.55f * level), dp(2.0f));
            if (saccadeShownReversedOf(values[saccadeReverseChance], repeat16)) {
                list->PushClipRect(ImVec2(x0, top), ImVec2(x1, bottom), true);
                const auto rise = bottom - top;
                for (auto x = x0 - rise; x < x1; x += dp(5.0f))
                    list->AddLine(ImVec2(x, bottom), ImVec2(x + rise, top), theme::alpha(kRhythmBackwards, 0.85f), dp(1.2f));
                list->PopClipRect();
            }
        }
        if (values[saccadePitchDecay] > 0.001f) {
            const auto drop = -12.0 * std::log2(saccadeRepeatRateOf(values[saccadePitchDecay], repeat16));
            const auto y = plot.min.y + dp(5.0f) + static_cast<float>(std::clamp(drop / 36.0, 0.0, 1.0)) * (tallest - dp(4.0f));
            pitchLine.emplace_back(x0, y);
            pitchLine.emplace_back(x1, y);
        }
    }
    if (pitchLine.size() >= 2)
        list->AddPolyline(pitchLine.data(), static_cast<int>(pitchLine.size()), theme::alpha(kRhythmBackwards, 0.9f), dp(1.4f));
    // The play head.
    if (readings != nullptr && live.running) {
        const auto x = xOf(static_cast<double>(std::clamp(readings[saccadeReadingPosition], 0.0f, 1.0f)) * intervalBeats);
        list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y), p.text, dp(1.5f));
    }
    // The handles: the offset and the gate's end, at the top.
    const auto offsetX = xOf(offsetBeats);
    const auto endX = xOf(end > intervalBeats + 1.0e-6 ? end - intervalBeats : end);
    const auto mouse = ImGui::GetIO().MousePos;
    auto& drag = dragState(parameters, pane);
    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton("##saccadePlot", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    const auto hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated())
        drag.active = std::abs(mouse.x - offsetX) <= std::abs(mouse.x - endX) ? 0 : 1;
    if (ImGui::IsItemActive() && drag.active >= 0) {
        const auto beats = beatsAt(mouse.x);
        if (drag.active == 0) {
            const auto most = std::clamp(std::ceil(intervalBeats * 4.0 - 1.0e-6) - 1.0, 0.0, 15.0);
            changed |= apply(at(parameters, pane, saccadeOffset), std::clamp(std::round(beats * 4.0), 0.0, most), set);
        } else {
            auto length = beats - offsetBeats;
            if (length <= 0.0)
                length += intervalBeats;
            changed |= apply(at(parameters, pane, saccadeGate), std::clamp(std::round(length * 4.0), 1.0, 16.0), set);
        }
    } else if (!ImGui::IsItemActive()) {
        drag.active = -1;
    }
    handleDot(list, ImVec2(offsetX, plot.min.y + dp(7.0f)), colour, hovered && std::abs(mouse.x - offsetX) <= std::abs(mouse.x - endX));
    handleDot(list, ImVec2(endX, plot.min.y + dp(7.0f)), theme::alpha(colour, 0.7f),
              hovered && std::abs(mouse.x - offsetX) > std::abs(mouse.x - endX));
    if (hovered && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", L("Glisser : le début des répétitions et leur fin", "Drag: where the repeats begin and where they end"));
    // What it says: the slice, the mode (in the window), the chance; at the
    // top band's right (its left holds the title), on the card beside the
    // repeat button.
    char label[96];
    if (large)
        std::snprintf(label, sizeof(label), "%s  \xc2\xb7  %s  \xc2\xb7  %d %%", rhythmNoteName(values[saccadeGrid]).toRawUTF8(),
                      rhythmChoiceName(saccadeModeOf(values[saccadeMode])).toRawUTF8(),
                      static_cast<int>(std::lround(values[saccadeChance] * 100.0f)));
    else
        std::snprintf(label, sizeof(label), "%s  \xc2\xb7  %d %%", rhythmNoteName(values[saccadeGrid]).toRawUTF8(),
                      static_cast<int>(std::lround(values[saccadeChance] * 100.0f)));
    const auto right = large ? box.max.x - dp(8.0f) : box.max.x - textSize(L("RÉPÉTER", "REPEAT"), scale).x - dp(24.0f);
    text(list, ImVec2(right - textSize(label, scale).x, box.min.y + dp(large ? 4.0f : 3.0f)), p.textDim, label, scale);
    return changed;
}

// ---- Boucleur -------------------------------------------------------------------------------------

// A press waiting for its second value (see rhythmLooperPress), by binding.
struct RhythmPending {
    int target {};
    double since {};
};
std::map<juce::String, RhythmPending>& rhythmPending()
{
    static std::map<juce::String, RhythmPending> pending;
    return pending;
}

// A transport button pressed: the state set to it. The looper acts on a
// change of the value, so when the value already says so (a loop of bars
// went on to play by itself) it goes through another value first, the one
// wanted a moment later (rhythmPendingPress).
bool rhythmLooperPress(Param* state, const int target, const int actual, const Setter& set)
{
    if (state == nullptr)
        return false;
    // Record pressed while waiting for the bar: cancelled.
    if (target == 1 && actual == rhythmd::boucleurWaiting)
        return apply(state, 0.0, set);
    if (static_cast<int>(std::lround(state->value)) != target)
        return apply(state, target, set);
    if (actual == target)
        return false;
    rhythmPending()[state->binding] = { target, ImGui::GetTime() };
    return apply(state, target == 0 ? 2.0 : 0.0, set);
}

bool rhythmPendingPress(Param* state, const Setter& set)
{
    if (state == nullptr)
        return false;
    auto& pending = rhythmPending();
    const auto found = pending.find(state->binding);
    if (found == pending.end() || ImGui::GetTime() - found->second.since < 0.08)
        return false;
    const auto target = found->second.target;
    pending.erase(found);
    return apply(state, target, set);
}

// A transport button's sign, drawn (not every font has the glyphs): 0 stop,
// 1 record, 2 play, 3 overdub, 4 clear.
void rhythmSign(ImDrawList* list, const int sign, const ImVec2 centre, const float size, const ImU32 tint)
{
    const auto r = size * 0.5f;
    switch (sign) {
    case 0: list->AddRectFilled(ImVec2(centre.x - r * 0.75f, centre.y - r * 0.75f), ImVec2(centre.x + r * 0.75f, centre.y + r * 0.75f), tint, dp(1.0f)); break;
    case 1: list->AddCircleFilled(centre, r * 0.85f, tint); break;
    case 2:
        list->AddTriangleFilled(ImVec2(centre.x - r * 0.65f, centre.y - r * 0.85f), ImVec2(centre.x - r * 0.65f, centre.y + r * 0.85f),
                                ImVec2(centre.x + r * 0.9f, centre.y), tint);
        break;
    case 3:
        list->AddCircle(centre, r * 0.85f, tint, 0, std::max(1.0f, r * 0.18f));
        list->AddLine(ImVec2(centre.x - r * 0.45f, centre.y), ImVec2(centre.x + r * 0.45f, centre.y), tint, std::max(1.0f, r * 0.18f));
        list->AddLine(ImVec2(centre.x, centre.y - r * 0.45f), ImVec2(centre.x, centre.y + r * 0.45f), tint, std::max(1.0f, r * 0.18f));
        break;
    default:
        list->AddLine(ImVec2(centre.x - r * 0.6f, centre.y - r * 0.6f), ImVec2(centre.x + r * 0.6f, centre.y + r * 0.6f), tint, std::max(1.0f, r * 0.2f));
        list->AddLine(ImVec2(centre.x - r * 0.6f, centre.y + r * 0.6f), ImVec2(centre.x + r * 0.6f, centre.y - r * 0.6f), tint, std::max(1.0f, r * 0.2f));
        break;
    }
}

// The loop as a strip: its outline (each point the loudest of its stretch),
// the play head; under it the transport (record, play, overdub, stop) and
// clear. What is lit is what the looper does, from its readings.
bool rhythmBoucleurDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                           const Setter& set, const bool large)
{
    using namespace rhythmd;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<boucleurParameterCount>(parameters, pane, "boucleur");
    auto* state = at(parameters, pane, boucleurState);
    auto changed = rhythmPendingPress(state, set);
    const auto* readings = rhythmReadings(live, boucleurReadingCount);
    const auto commanded = whole(values[boucleurState], boucleurStateCount);
    const auto actual = readings != nullptr ? std::clamp(static_cast<int>(std::lround(readings[boucleurReadingState])), 0, boucleurWaiting)
                                            : commanded;
    const auto length = readings != nullptr ? std::max(0.0f, readings[boucleurReadingLength]) : 0.0f;
    const auto place = readings != nullptr ? std::clamp(readings[boucleurReadingPosition], 0.0f, 1.0f) : 0.0f;
    const auto layers = readings != nullptr ? static_cast<int>(std::lround(readings[boucleurReadingLayers])) : 0;
    const auto scale = large ? 0.68f : 0.56f;
    const auto buttonHeight = dp(large ? 36.0f : 20.0f);
    const Box strip { ImVec2(box.min.x + dp(6.0f), box.min.y + dp(large ? 28.0f : 20.0f)),
                      ImVec2(box.max.x - dp(6.0f), box.max.y - buttonHeight - dp(large ? 14.0f : 7.0f)) };
    // What it says: what it does, the loop's length and layers, at the top
    // band's right (its left holds the title).
    {
        const auto dot = juce::String::fromUTF8("  \xc2\xb7  ");
        juce::String said = rhythmChoiceName(boucleurStateOf(static_cast<float>(actual)));
        if (length > 0.0f) {
            said << dot << juce::String(length, 2) << " s";
            if (layers > 0)
                said << dot << layers << (ui::french() ? (layers > 1 ? " couches" : " couche") : (layers > 1 ? " layers" : " layer"));
        }
        const auto bars = boucleurBarsOf(values[boucleurBars]);
        if (large)
            said << dot
                 << (bars <= 0 ? ui::Ls("libre", "free")
                               : juce::String(bars) + (ui::french() ? (bars > 1 ? " mesures" : " mesure") : (bars > 1 ? " bars" : " bar")));
        text(list, ImVec2(box.max.x - dp(8.0f) - textSize(said.toRawUTF8(), scale).x, box.min.y + dp(large ? 4.0f : 3.0f)),
             actual == 1 ? p.record : p.textDim, said.toRawUTF8(), scale);
    }
    if (strip.width() > dp(24.0f) && strip.height() > dp(10.0f)) {
        list->AddRectFilled(strip.min, strip.max, theme::alpha(kDark, 0.5f), dp(3.0f));
        const auto middle = (strip.min.y + strip.max.y) * 0.5f;
        const auto reach = strip.height() * 0.5f - dp(3.0f);
        list->AddLine(ImVec2(strip.min.x, middle), ImVec2(strip.max.x, middle), p.grid);
        const auto recording = actual == 1;
        const auto tint = recording ? p.record : colour;
        if (readings != nullptr && length > 0.0f) {
            const auto width = strip.width() / static_cast<float>(boucleurOutlinePoints);
            for (int point = 0; point < boucleurOutlinePoints; ++point) {
                const auto peak = std::clamp(readings[boucleurReadingOutline + point], 0.0f, 1.0f);
                const auto x = strip.min.x + (static_cast<float>(point) + 0.5f) * width;
                const auto h = std::max(dp(0.5f), peak * reach);
                // A loop of bars being recorded: what is still to come dim.
                const auto ahead = recording && (static_cast<float>(point) + 0.5f) / static_cast<float>(boucleurOutlinePoints) > place;
                list->AddLine(ImVec2(x, middle - h), ImVec2(x, middle + h), theme::alpha(tint, ahead ? 0.2f : 0.85f), std::max(dp(1.0f), width - dp(0.5f)));
            }
            if (actual == 1 || actual == 2 || actual == 3) {
                const auto x = strip.min.x + place * strip.width();
                list->AddLine(ImVec2(x, strip.min.y), ImVec2(x, strip.max.y), recording ? p.record : p.text, dp(1.6f));
            }
        } else {
            const auto* empty = actual == boucleurWaiting ? L("En attente de la mesure…", "Waiting for the bar…")
                                : actual == 1        ? L("Enregistre…", "Recording…")
                                                     : L("Vide  \xc2\xb7  enregistrer pour commencer", "Empty  \xc2\xb7  record to begin");
            centred(list, ImVec2((strip.min.x + strip.max.x) * 0.5f, middle), recording ? p.record : p.textFaint, empty, scale);
        }
        if (actual == boucleurWaiting && std::fmod(ImGui::GetTime(), 0.8) < 0.4)
            list->AddRect(strip.min, strip.max, theme::alpha(p.record, 0.9f), dp(3.0f), dp(1.5f));
        if (values[boucleurReverse] >= 0.5f || std::abs(boucleurSpeedOf(values[boucleurSpeed]) - 1.0) > 1.0e-6) {
            juce::String how = values[boucleurReverse] >= 0.5f ? ui::Ls("inversé ", "reversed ") : juce::String();
            how << (boucleurSpeedOf(values[boucleurSpeed]) < 1.0 ? juce::String::fromUTF8("\xc2\xbd\xc3\x97")
                                                                 : boucleurSpeedOf(values[boucleurSpeed]) > 1.0 ? juce::String::fromUTF8("2\xc3\x97")
                                                                                                                : juce::String());
            text(list, ImVec2(strip.max.x - textSize(how.toRawUTF8(), 0.56f).x - dp(4.0f), strip.min.y + dp(2.0f)), p.textFaint, how.toRawUTF8(),
                 0.56f);
        }
    }
    // The transport, then clear.
    struct Button {
        int sign;
        int target;
        const char* label;
        const char* tip;
        ImU32 tint;
    };
    const std::array<Button, 5> buttons { {
        { 1, 1, L("ENREG.", "REC"), L("Enregistrer une nouvelle boucle", "Record a new loop"), p.record },
        { 2, 2, L("JOUER", "PLAY"), L("Jouer la boucle", "Play the loop"), p.play },
        { 3, 3, L("SUPERP.", "DUB"), L("Superposer : jouer et ajouter ce qui entre", "Overdub: play and add what comes in"), p.warn },
        { 0, 0, L("STOP", "STOP"), L("Arrêter (la boucle reste)", "Stop (the loop stays)"), p.text },
        { 4, -1, L("EFFACER", "CLEAR"), L("Effacer la boucle", "Clear the loop"), p.textDim },
    } };
    const auto gap = dp(large ? 8.0f : 4.0f);
    const auto row = box.width() - dp(12.0f);
    const auto cell = std::min(dp(large ? 120.0f : 48.0f), (row - gap * 4.0f) / 5.0f);
    if (cell > dp(8.0f)) {
        const auto left = box.min.x + dp(6.0f) + (row - (cell * 5.0f + gap * 4.0f)) * 0.5f;
        const auto top = box.max.y - buttonHeight - dp(large ? 6.0f : 3.0f);
        for (std::size_t index = 0; index < buttons.size(); ++index) {
            const auto& button = buttons[index];
            const ImVec2 min(left + static_cast<float>(index) * (cell + gap), top);
            const ImVec2 max(min.x + cell, top + buttonHeight);
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetCursorScreenPos(min);
            const auto clicked = ImGui::InvisibleButton("##looperButton", ImVec2(cell, buttonHeight));
            const auto hot = ImGui::IsItemHovered();
            if (hot)
                ImGui::SetTooltip("%s", button.tip);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                if (auto* target = at(parameters, pane, button.target >= 0 ? boucleurState : boucleurClear))
                    openParameterMenu(*target);
            }
            ImGui::PopID();
            auto lit = button.target >= 0 && actual == button.target;
            if (button.target == 1 && actual == boucleurWaiting)
                lit = std::fmod(ImGui::GetTime(), 0.8) < 0.4;
            list->AddRectFilled(min, max, lit ? button.tint : hot ? p.bg4 : p.bg3, dp(large ? 6.0f : 3.0f));
            const auto glyph = buttonHeight * (large ? 0.5f : 0.6f);
            const auto withLabel = large && cell >= dp(72.0f);
            const auto labelWidth = withLabel ? textSize(button.label, 0.62f).x + dp(6.0f) : 0.0f;
            const ImVec2 centre((min.x + max.x) * 0.5f - labelWidth * 0.5f, (min.y + max.y) * 0.5f);
            rhythmSign(list, button.sign, centre, glyph, lit ? kDark : button.tint);
            if (withLabel)
                text(list, ImVec2(centre.x + glyph * 0.5f + dp(6.0f), centre.y - textSize(button.label, 0.62f).y * 0.5f), lit ? kDark : p.textDim,
                     button.label, 0.62f);
            if (!clicked)
                continue;
            if (button.target >= 0) {
                changed |= rhythmLooperPress(state, button.target, actual, set);
            } else if (auto* clear = at(parameters, pane, boucleurClear)) {
                changed |= apply(clear, std::fmod(std::round(clear->value) + 1.0, 100.0), set);
            }
        }
    }
    return changed;
}

// ---- Entry points ---------------------------------------------------------------------------------

// A card's display (large: the window's), true when a value changed.
bool rhythmDisplay(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour,
                    const Live& live, const Setter& set, const bool large)
{
    ImGui::PushID(kind == PaneKind::grains ? "rhythmGrains" : kind == PaneKind::saccade ? "rhythmSaccade" : "rhythmBoucleur");
    auto changed = false;
    if (kind == PaneKind::grains)
        changed = rhythmGrainsDisplay(parameters, pane, box, colour, live, set, large);
    else if (kind == PaneKind::saccade)
        changed = rhythmSaccadeDisplay(parameters, pane, box, colour, live, set, large);
    else if (kind == PaneKind::boucleur)
        changed = rhythmBoucleurDisplay(parameters, pane, box, colour, live, set, large);
    ImGui::PopID();
    return changed;
}

// The window: its display large and every control.
bool rhythmLarge(const PaneKind kind, const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size,
                  const ImU32 colour, const Live& live, const Setter& set)
{
    using namespace rhythmd;
    const auto display = [&](const Box& box) { return rhythmDisplay(kind, parameters, pane, box, colour, live, set, true); };
    if (kind == PaneKind::grains)
        return addedLarge(pane, parameters, position, size, colour, live, set, L("GRAINS  ·  l'écho en grains", "GRAINS  ·  the granular delay"),
                          { { L("TEMPS", "TIME"), { grainsTime, grainsSync, grainsSpray, grainsFeedback } },
                            { L("GRAINS", "GRAINS"), { grainsDensity, grainsSize, grainsPitch, grainsPitchRandom, grainsReverseChance, grainsStereo } },
                            { L("SORTIE", "OUTPUT"), { grainsFreeze, grainsHighCut, grainsMix, grainsOutput } } },
                          display);
    if (kind == PaneKind::saccade)
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("SACCADE  ·  le répéteur de temps", "SACCADE  ·  the beat repeater"),
                          { { L("QUAND", "WHEN"), { saccadeInterval, saccadeOffset, saccadeGate, saccadeChance } },
                            { L("RÉPÉTITIONS", "REPEATS"),
                              { saccadeGrid, saccadeVariation, saccadeDecay, saccadePitchDecay, saccadeReverseChance, saccadeFilter } },
                            { L("SORTIE", "OUTPUT"), { saccadeMode, saccadeRepeat, saccadeMix, saccadeOutput } } },
                          display);
    if (kind == PaneKind::boucleur)
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("BOUCLEUR  ·  enregistrer, jouer, superposer", "LOOPER  ·  record, play, overdub"),
                          { { L("BOUCLE", "LOOP"), { boucleurBars, boucleurQuantize, boucleurSpeed, boucleurReverse } },
                            { L("NIVEAUX", "LEVELS"), { boucleurLevel, boucleurDry, boucleurFeedback, boucleurOutput } } },
                          display, 220.0f);
    return false;
}

// The values a card shows as knobs (pane indices).
std::vector<int> rhythmCardFields(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane)
{
    juce::ignoreUnused(parameters, pane);
    using namespace rhythmd;
    switch (kind) {
    case PaneKind::grains: return { grainsTime, grainsPitch, grainsSpray, grainsMix };
    case PaneKind::saccade: return { saccadeChance, saccadeGrid, saccadeDecay, saccadeMix };
    case PaneKind::boucleur: return { boucleurLevel, boucleurDry, boucleurFeedback };
    default: return {};
    }
}

// A value as these effects show it; empty for none of theirs.
juce::String rhythmFormat(const juce::String& binding, const double value)
{
    using namespace rhythmd;
    const auto whole16 = [](const double v) { return juce::String(juce::roundToInt(v)) + "/16"; };
    const auto semitones = [](const double v) {
        const auto rounded = std::round(v * 10.0) / 10.0;
        return (rounded > 0.0 ? "+" : "") + juce::String(rounded, std::abs(rounded - std::round(rounded)) < 1.0e-6 ? 0 : 1) + " st";
    };
    // Grains.
    if (binding.endsWith("grains.pitch"))
        return semitones(value);
    if (binding.endsWith("grains.pitchRandom"))
        return value < 0.05 ? ui::Ls("Aucun", "Off") : juce::String::fromUTF8("\xc2\xb1") + juce::String(value, 1) + " st";
    if (binding.endsWith("grains.sync"))
        return rhythmNoteName(value);
    if (binding.endsWith("grains.freeze"))
        return rhythmOnOff(value);
    if (binding.endsWith("grains.density"))
        return juce::String(value, value < 10.0 ? 1 : 0) + ui::Ls(" grains/s", " grains/s");
    if (binding.endsWith("grains.feedback") || binding.endsWith("grains.mix") || binding.endsWith("grains.stereo")
        || binding.endsWith("grains.reverseChance"))
        return percent(value);
    // Saccade.
    if (binding.endsWith("saccade.interval") || binding.endsWith("saccade.grid"))
        return rhythmNoteName(std::max(1.0, value));
    if (binding.endsWith("saccade.offset") || binding.endsWith("saccade.gate"))
        return whole16(value);
    if (binding.endsWith("saccade.mode"))
        return rhythmChoiceName(saccadeModeOf(static_cast<float>(value)));
    if (binding.endsWith("saccade.repeat"))
        return rhythmOnOff(value);
    if (binding.endsWith("saccade.filter") && saccadeFilterOpenOf(static_cast<float>(value)))
        return ui::Ls("Ouvert", "Open");
    if (binding.endsWith("saccade.chance") || binding.endsWith("saccade.variation") || binding.endsWith("saccade.decay")
        || binding.endsWith("saccade.pitchDecay") || binding.endsWith("saccade.reverseChance") || binding.endsWith("saccade.mix"))
        return percent(value);
    // Boucleur.
    if (binding.endsWith("boucleur.state"))
        return rhythmChoiceName(boucleurStateOf(static_cast<float>(std::clamp(value, 0.0, static_cast<double>(boucleurStateCount - 1)))));
    if (binding.endsWith("boucleur.bars")) {
        const auto bars = boucleurBarsOf(static_cast<float>(value));
        if (bars <= 0)
            return ui::Ls("Libre", "Free");
        return juce::String(bars) + (ui::french() ? (bars > 1 ? " mesures" : " mesure") : (bars > 1 ? " bars" : " bar"));
    }
    if (binding.endsWith("boucleur.speed")) {
        const auto speed = boucleurSpeedOf(static_cast<float>(value));
        return speed < 1.0 ? juce::String::fromUTF8("\xc2\xbd\xc3\x97") : speed > 1.0 ? juce::String::fromUTF8("2\xc3\x97")
                                                                                     : juce::String::fromUTF8("1\xc3\x97");
    }
    if (binding.endsWith("boucleur.quantize"))
        return value >= 0.5 ? ui::Ls("À la mesure", "On the bar") : ui::Ls("Aussitôt", "At once");
    if (binding.endsWith("boucleur.reverse"))
        return rhythmOnOff(value);
    if (binding.endsWith("boucleur.feedback") || binding.endsWith("boucleur.level") || binding.endsWith("boucleur.dry"))
        return percent(value);
    return {};
}

// A parameter of theirs that takes whole steps (a choice, a switch).
bool rhythmStepped(const juce::String& binding)
{
    for (const auto* suffix : { "grains.sync", "grains.freeze", "saccade.interval", "saccade.offset", "saccade.gate", "saccade.grid",
                                "saccade.mode", "saccade.repeat", "boucleur.state", "boucleur.bars", "boucleur.quantize", "boucleur.speed",
                                "boucleur.reverse", "boucleur.clear" })
        if (binding.endsWith(suffix))
            return true;
    return false;
}
