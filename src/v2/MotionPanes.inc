// The displays and large views of Pompe, Décaleur, Anneau, Rotor
// (MotionDesign.h computes what they draw). Included by StudioEffectsView.cpp
// inside its anonymous namespace: it has its helpers (knobCell, segmented,
// chooser, comboCell, switchCell, frame, title, section, text, fillUnder…)
// and addedLarge(), the common window layout.

namespace motiond = engine::motion;

constexpr ImU32 kMotionLeft = IM_COL32(110, 200, 255, 255);
constexpr ImU32 kMotionRight = IM_COL32(255, 150, 110, 255);
constexpr ImU32 kMotionHorn = IM_COL32(255, 200, 100, 255);
constexpr ImU32 kMotionDrum = IM_COL32(170, 130, 255, 255);
constexpr ImU32 kMotionFloor = IM_COL32(255, 92, 92, 255);

juce::String motionChoiceName(const motiond::MotionChoice& choice)
{
    return juce::String::fromUTF8(ui::french() ? choice.french : choice.english);
}

juce::String motionChoiceHint(const motiond::MotionChoice& choice)
{
    return juce::String::fromUTF8(ui::french() ? choice.frenchHint : choice.englishHint);
}

template <std::size_t N>
const motiond::MotionChoice& motionChoiceAt(const std::array<motiond::MotionChoice, N>& choices, const double value)
{
    return choices[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(N) - 1))];
}

juce::String motionPompeShapeName(const int index)
{
    return motionChoiceName(motionChoiceAt(motiond::pompeShapes, index));
}
juce::String motionPompeShapeHint(const int index)
{
    return motionChoiceHint(motionChoiceAt(motiond::pompeShapes, index));
}
juce::String motionAnneauShapeName(const int index)
{
    return motionChoiceName(motionChoiceAt(motiond::anneauShapes, index));
}
juce::String motionAnneauShapeHint(const int index)
{
    return motionChoiceHint(motionChoiceAt(motiond::anneauShapes, index));
}

// "220 Hz", "1.10 kHz".
juce::String motionHertz(const double hz)
{
    const auto size = std::abs(hz);
    if (size >= 1'000.0)
        return juce::String(hz / 1'000.0, size >= 10'000.0 ? 1 : 2) + " kHz";
    return juce::String(hz, size < 10.0 ? 2 : size < 100.0 ? 1 : 0) + " Hz";
}

// "+100 Hz", "-12.5 Hz".
juce::String motionSignedHertz(const double hz)
{
    return (hz > 0.0 ? "+" : "") + juce::String(hz, std::abs(hz) < 10.0 ? 2 : std::abs(hz) < 100.0 ? 1 : 0) + " Hz";
}

// A sync value: free, or its note value.
juce::String motionSyncName(const double value)
{
    if (const auto* division = engine::noteDivisionFor(static_cast<float>(value)))
        return juce::String(std::string(division->name));
    return ui::Ls("Libre", "Free");
}

// What a cycle runs at: its note value, or its rate.
juce::String motionCycleText(const float sync, const float rate)
{
    if (engine::noteDivisionFor(sync) != nullptr)
        return motionSyncName(sync);
    return juce::String(rate, rate < 10.0f ? 2 : 1) + " Hz";
}

juce::String motionDegrees(const double turns)
{
    return juce::String(juce::roundToInt(turns * 360.0)) + juce::String::fromUTF8("\xc2\xb0");
}

// The processor's readings, when they are fresh and complete.
bool motionReads(const Live& live, const int count)
{
    return live.running && live.readings != nullptr && live.readingCount >= count;
}

// An arrow from one point to another, its head at the end.
void motionArrow(ImDrawList* list, const ImVec2 from, const ImVec2 to, const ImU32 colour, const float thickness, const bool dashed)
{
    if (dashed)
        dashedLine(list, from, to, colour, thickness);
    else
        list->AddLine(from, to, colour, thickness);
    const auto dx = to.x - from.x;
    const auto dy = to.y - from.y;
    const auto length = std::max(1.0f, std::hypot(dx, dy));
    const ImVec2 along(dx / length, dy / length);
    const ImVec2 across(-along.y, along.x);
    const auto head = dp(6.0f);
    list->AddTriangleFilled(to, ImVec2(to.x - along.x * head + across.x * head * 0.5f, to.y - along.y * head + across.y * head * 0.5f),
                            ImVec2(to.x - along.x * head - across.x * head * 0.5f, to.y - along.y * head - across.y * head * 0.5f), colour);
}

// ---- Pompe ---

// One cycle of the gain (filled), the depth as its floor, the drawn shape's
// sixteen points to drag (drawing across them sets each in turn; a double
// click puts the default curve back), the place now as a dot moving along.
// Elsewhere a vertical drag sets the depth, the wheel bends the curve.
bool motionPompeDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                        const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto values = studioValues<motiond::pompeParameterCount>(parameters, pane, "pompe");
    const auto* v = values.data();
    const auto scale = large ? 0.82f : 0.66f;
    auto changed = false;
    // The shape to choose and what starts a cycle, in the top corner.
    const auto shapeNow = whole(values[motiond::pompeShape], motiond::pompeShapeCount);
    const auto chooserX = box.max.x - textSize((motionPompeShapeName(shapeNow) + "  ").toRawUTF8(), scale).x - dp(16.0f);
    changed |= chooser("##pompeShape", at(parameters, pane, motiond::pompeShape), motiond::pompeShapeCount, motionPompeShapeName,
                       motionPompeShapeHint, ImVec2(chooserX, box.min.y + dp(2.0f)), colour, set, scale);
    const auto triggerWidth = dp(large ? 130.0f : 84.0f);
    changed |= segmented("##pompeTrigger", at(parameters, pane, motiond::pompeTrigger), { "Tempo", L("Touche", "Key") },
                         ImVec2(chooserX - triggerWidth - dp(8.0f), box.min.y + dp(2.0f)), triggerWidth, dp(large ? 20.0f : 15.0f), colour,
                         set,
                         L("Tempo : un cycle par valeur de note (ou à la vitesse libre). Touche : chaque coup sur l'entrée sidechain "
                           "(sinon sur le son lui-même) relance un cycle, puis le son reste en haut.",
                           "Tempo: a cycle per note value (or at the free rate). Key: each hit on the sidechain input (else on the sound "
                           "itself) starts one cycle, then the sound stays at the top."));
    values = studioValues<motiond::pompeParameterCount>(parameters, pane, "pompe");
    const auto shape = whole(values[motiond::pompeShape], motiond::pompeShapeCount);
    const auto keyMode = whole(values[motiond::pompeTrigger], 2) == 1;
    const auto drawn = shape == motiond::pompeDrawn;
    const Box plot { ImVec2(box.min.x + dp(4.0f), box.min.y + dp(large ? 28.0f : 21.0f)),
                     ImVec2(box.max.x - dp(4.0f), box.max.y - dp(large ? 20.0f : 4.0f)) };
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.5f), dp(3.0f));
    const auto xOf = [&plot](const double place) { return plot.min.x + static_cast<float>(place) * plot.width(); };
    const auto yOf = [&plot](const double level) { return plot.max.y - static_cast<float>(std::clamp(level, 0.0, 1.0)) * plot.height(); };
    for (int quarter = 1; quarter < 4; ++quarter) {
        const auto x = xOf(quarter / 4.0);
        list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y), quarter == 2 ? p.gridStrong : p.grid);
    }
    for (const auto level : { 0.25, 0.5, 0.75 })
        list->AddLine(ImVec2(plot.min.x, yOf(level)), ImVec2(plot.max.x, yOf(level)), theme::alpha(p.grid, 0.6f));

    // The interaction first, so what is drawn is what was just set.
    auto& drag = dragState(parameters, pane);
    const auto& io = ImGui::GetIO();
    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton("##pompePlot", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    const auto hovered = ImGui::IsItemHovered();
    // The drawn point nearest the pointer across.
    const auto nearest = [&](const float x) {
        auto best = 0;
        auto distance = 1.0e9f;
        for (int point = 0; point < motiond::pompePointCount; ++point) {
            const auto d = std::abs(xOf(motiond::pompePointPlace(v, point, !keyMode)) - x);
            if (d < distance) {
                distance = d;
                best = point;
            }
        }
        return best;
    };
    const auto hot = drawn && (hovered || ImGui::IsItemActive()) ? nearest(io.MousePos.x) : -1;
    if (ImGui::IsItemActivated())
        drag.active = hot;
    if (ImGui::IsItemActive()) {
        if (drawn && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const auto level = 1.0 - static_cast<double>((io.MousePos.y - plot.min.y) / std::max(1.0f, plot.height()));
            changed |= apply(at(parameters, pane, motiond::pompePoint1 + nearest(io.MousePos.x)), level, set);
        } else if (!drawn && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
            changed |= apply(at(parameters, pane, motiond::pompeDepth),
                             static_cast<double>((io.MousePos.y - plot.min.y) / std::max(1.0f, plot.height())), set);
        }
    }
    if (ImGui::IsItemDeactivated())
        drag.active = -1;
    if (hovered && drawn && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        for (int point = 0; point < motiond::pompePointCount; ++point)
            changed |= apply(at(parameters, pane, motiond::pompePoint1 + point),
                             static_cast<double>(motiond::pompeDefaultPoints[static_cast<std::size_t>(point)]), set);
    if (hovered && io.MouseWheel != 0.0f) {
        changed |= apply(at(parameters, pane, motiond::pompeCurve), static_cast<double>(values[motiond::pompeCurve] + io.MouseWheel * 0.05f), set);
        ui::takeWheel();
    }
    if (hovered && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", drawn ? L("Dessinez la courbe : cliquez ou glissez sur les points. Double-clic : la courbe par défaut. "
                                          "Molette : la courbure.",
                                          "Draw the curve: click or drag over the points. Double click: the default curve. Wheel: the bend.")
                                      : L("Glissez vers le bas pour plus de profondeur. Molette : la courbure.",
                                          "Drag down for more depth. Wheel: the bend."));
    values = studioValues<motiond::pompeParameterCount>(parameters, pane, "pompe");

    // One cycle of the gain.
    std::vector<ImVec2> points;
    for (auto x = plot.min.x; x <= plot.max.x + 0.5f; x += dp(1.0f)) {
        const auto place = std::clamp(static_cast<double>((x - plot.min.x) / std::max(1.0f, plot.width())), 0.0, 0.9999);
        points.emplace_back(std::min(x, plot.max.x), yOf(keyMode ? motiond::pompeKeyGainAt(v, place) : motiond::pompeGainAt(v, place)));
    }
    fillUnder(list, points, plot.max.y, theme::alpha(colour, 0.22f));
    list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(large ? 2.2f : 1.5f));
    // The depth: how far down the curve can go.
    const auto depth = std::clamp(static_cast<double>(values[motiond::pompeDepth]), 0.0, 1.0);
    const auto floorY = yOf(1.0 - depth);
    dashedLine(list, ImVec2(plot.min.x, floorY), ImVec2(plot.max.x, floorY), theme::alpha(kMotionFloor, 0.8f), dp(1.2f));
    if (large) {
        char label[32];
        if (depth >= 0.9995)
            std::snprintf(label, sizeof(label), "%s", L("fond : silence", "floor: silence"));
        else
            std::snprintf(label, sizeof(label), "%s %.1f dB", L("fond", "floor"), 20.0 * std::log10(1.0 - depth));
        text(list, ImVec2(plot.max.x - textSize(label, 0.6f).x - dp(8.0f), std::max(plot.min.y, floorY - ImGui::GetFontSize() * 0.7f)),
             kMotionFloor, label, 0.6f);
    }
    // The drawn shape: its points as they are (before depth and bend).
    if (drawn) {
        std::array<ImVec2, motiond::pompePointCount> handles {};
        for (int point = 0; point < motiond::pompePointCount; ++point)
            handles[static_cast<std::size_t>(point)] =
                ImVec2(xOf(motiond::pompePointPlace(v, point, !keyMode)), yOf(values[static_cast<std::size_t>(motiond::pompePoint1 + point)]));
        for (std::size_t point = 0; point + 1 < handles.size(); ++point)
            if (handles[point + 1].x >= handles[point].x)
                list->AddLine(handles[point], handles[point + 1], theme::alpha(p.text, 0.45f), dp(1.0f));
        for (int point = 0; point < motiond::pompePointCount; ++point) {
            const auto& spot = handles[static_cast<std::size_t>(point)];
            if (spot.x > plot.max.x + 0.5f)
                continue;
            if (large)
                handleDot(list, spot, point == hot ? p.text : colour, point == hot);
            else
                list->AddCircleFilled(spot, dp(point == hot ? 3.5f : 2.5f), point == hot ? p.text : colour);
        }
    }
    // Where it is now, and the gain it gives.
    if (motionReads(live, motiond::pompeReadingPhase + 1)) {
        const auto phase = std::clamp(static_cast<double>(live.readings[motiond::pompeReadingPhase]), 0.0, 1.0);
        const auto gainNow = std::clamp(static_cast<double>(live.readings[motiond::pompeReadingGain]), 0.0, 1.0);
        const ImVec2 dot(xOf(phase), yOf(gainNow));
        list->AddLine(ImVec2(dot.x, plot.min.y), ImVec2(dot.x, plot.max.y), theme::alpha(p.text, 0.25f));
        list->AddCircleFilled(dot, dp(large ? 5.0f : 3.5f), p.text);
        list->AddRectFilled(ImVec2(plot.max.x - dp(4.0f), dot.y), plot.max, theme::alpha(colour, 0.9f));
        // The key's level against its threshold.
        if (large && keyMode && live.readingCount > motiond::pompeReadingKeyDb) {
            const auto levelOf = [&plot](const double db) {
                return plot.max.y - static_cast<float>(std::clamp((db + 60.0) / 60.0, 0.0, 1.0)) * plot.height();
            };
            const auto keyDb = static_cast<double>(live.readings[motiond::pompeReadingKeyDb]);
            list->AddRectFilled(ImVec2(plot.min.x, levelOf(keyDb)), ImVec2(plot.min.x + dp(5.0f), plot.max.y), theme::alpha(p.textDim, 0.8f));
            const auto threshold = levelOf(values[motiond::pompeThreshold]);
            list->AddLine(ImVec2(plot.min.x, threshold), ImVec2(plot.min.x + dp(10.0f), threshold), kMotionFloor, dp(1.5f));
        }
    }
    // What runs a cycle.
    auto cycle = motionCycleText(values[motiond::pompeSync], values[motiond::pompeRate]);
    if (keyMode)
        cycle = ui::Ls("Touche  ·  ", "Key  ·  ") + cycle;
    if (large) {
        const auto hint = motionPompeShapeHint(shape);
        text(list, ImVec2(plot.min.x + dp(2.0f), plot.max.y + dp(3.0f)), p.textDim, (cycle + "   " + hint).toRawUTF8(), 0.64f);
    } else {
        text(list, ImVec2(plot.min.x + dp(4.0f), plot.min.y + dp(2.0f)), p.textDim, cycle.toRawUTF8(), 0.58f);
    }
    return changed;
}

bool motionPompeLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                      const Live& live, const Setter& set)
{
    return addedLarge(pane, parameters, position, size, colour, live, set,
                      L("POMPE  ·  le volume qui respire avec le tempo", "POMPE  ·  the volume breathing with the tempo"),
                      { { L("COURBE", "CURVE"), { motiond::pompeDepth, motiond::pompeCurve, motiond::pompeHold, motiond::pompePhase } },
                        { L("CYCLE", "CYCLE"),
                          { motiond::pompeSync, motiond::pompeRate, motiond::pompeStereoOffset, motiond::pompeSmoothing } },
                        { L("TOUCHE", "KEY"), { motiond::pompeThreshold } },
                        { L("SORTIE", "OUTPUT"), { motiond::pompeMix, motiond::pompeOutput } } },
                      [&](const Box& inner) { return motionPompeDisplay(parameters, pane, inner, colour, live, set, true); });
}

// ---- Décaleur ---

// On the spectrum (the sound coming in behind), three frequencies and where
// the shift takes them: the same hertz added to each, so the octave between
// 100 and 200 Hz is no longer an octave. Through zero a frequency comes back
// up (dashed). The LFO's reach as a band round each arrival; the shift now,
// large. Drag across to shift, the wheel for fine, a double click for none.
bool motionDecaleurDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                           const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto modesWidth = dp(large ? 330.0f : 156.0f);
    const auto modeNames = large ? std::vector<const char*> { L("Une bande", "Single sideband"), L("Deux bandes", "Both sidebands"),
                                                              L("Côtés opposés", "Opposite sides") }
                                 : std::vector<const char*> { L("Une", "Single"), L("Deux", "Both"), L("Opposés", "Opposite") };
    changed |= segmented("##decaleurMode", at(parameters, pane, motiond::decaleurMode), modeNames,
                         ImVec2(box.max.x - modesWidth - dp(4.0f), box.min.y + dp(2.0f)), modesWidth, dp(large ? 20.0f : 15.0f), colour,
                         set,
                         L("Une bande : tout monte (ou descend) du même nombre de hertz. Deux bandes : vers le haut et vers le bas à la "
                           "fois. Côtés opposés : la gauche monte, la droite descend.",
                           "Single: everything moves up (or down) by the same hertz. Both: up and down at once. Opposite: the left goes "
                           "up, the right goes down."));
    const auto values = studioValues<motiond::decaleurParameterCount>(parameters, pane, "decaleur");
    const auto* v = values.data();
    const auto mode = whole(values[motiond::decaleurMode], motiond::decaleurModeCount);
    const Box plot { ImVec2(box.min.x + dp(4.0f), box.min.y + dp(large ? 28.0f : 21.0f)), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.5f), dp(3.0f));
    frequencyGrid(list, plot, large);
    spectrumBehind(list, plot, live.before, p.textDim);
    // The shift now: the LFO's swing in it while it plays.
    auto shiftNow = motiond::decaleurShiftAt(v, 0.0);
    auto shifts = motiond::decaleurShiftsOf(v, 0.0);
    if (motionReads(live, motiond::decaleurReadingCount)) {
        shiftNow = live.readings[motiond::decaleurReadingShift];
        shifts = { live.readings[motiond::decaleurReadingLeft], live.readings[motiond::decaleurReadingRight] };
    }
    const auto reach = static_cast<double>(values[motiond::decaleurLfoDepth]);
    const auto fromY = plot.max.y - dp(large ? 30.0f : 12.0f);
    const auto toY = plot.min.y + dp(large ? 52.0f : 26.0f);
    struct Way {
        double shift;
        ImU32 tint;
    };
    std::vector<Way> ways;
    const auto sided = std::abs(shifts.left - shifts.right) > 0.01;
    if (mode == 1) {
        ways.push_back({ shifts.left, colour });
        ways.push_back({ -shifts.left, colour });
    } else {
        ways.push_back({ shifts.left, sided ? kMotionLeft : colour });
        if (sided)
            ways.push_back({ shifts.right, kMotionRight });
    }
    for (const auto hz : { 100.0, 1'000.0, 5'000.0 }) {
        const auto x0 = xOfHz(plot, hz);
        list->AddCircleFilled(ImVec2(x0, fromY), dp(large ? 4.0f : 3.0f), p.text);
        if (large)
            centred(list, ImVec2(x0, fromY + dp(12.0f)), p.textDim, motionHertz(hz).toRawUTF8(), 0.6f);
        for (const auto& way : ways) {
            const auto arrival = hz + way.shift;
            const auto heard = std::max(20.0, std::abs(arrival));
            const auto x1 = xOfHz(plot, heard);
            // The LFO's reach round it.
            if (reach > 0.5) {
                const auto low = arrival - reach;
                const auto high = arrival + reach;
                const auto nearest = low < 0.0 && high > 0.0 ? 0.0 : std::min(std::abs(low), std::abs(high));
                const auto farthest = std::max(std::abs(low), std::abs(high));
                list->AddRectFilled(ImVec2(xOfHz(plot, std::max(20.0, nearest)), toY - dp(5.0f)),
                                    ImVec2(xOfHz(plot, std::max(20.0, farthest)), toY + dp(5.0f)), theme::alpha(way.tint, 0.2f), dp(2.0f));
            }
            motionArrow(list, ImVec2(x0, fromY - dp(5.0f)), ImVec2(x1, toY + dp(7.0f)), theme::alpha(way.tint, 0.85f), dp(large ? 1.6f : 1.2f),
                        arrival < 0.0);
            list->AddCircleFilled(ImVec2(x1, toY), dp(large ? 4.0f : 3.0f), way.tint);
            if (large)
                centred(list, ImVec2(x1, toY - dp(13.0f)), way.tint, motionHertz(std::abs(arrival)).toRawUTF8(), 0.6f);
        }
    }
    // The shift, large.
    const auto shown = motionSignedHertz(shiftNow);
    text(list, ImVec2(plot.min.x + dp(6.0f), plot.min.y + dp(3.0f)), colour, shown.toRawUTF8(), large ? 1.5f : 0.76f);
    if (large && (sided || mode == 2)) {
        const auto sides = ui::Ls("gauche ", "left ") + motionSignedHertz(shifts.left) + ui::Ls("   droite ", "   right ")
                           + motionSignedHertz(shifts.right);
        text(list, ImVec2(plot.min.x + dp(6.0f), plot.min.y + dp(3.0f) + ImGui::GetFontSize() * 1.6f), p.textDim, sides.toRawUTF8(), 0.64f);
    }
    if (large && values[motiond::decaleurFeedback] > 0.01f) {
        const auto spiral = ui::Ls("spirale ", "spiral ") + percent(values[motiond::decaleurFeedback]);
        text(list, ImVec2(plot.max.x - textSize(spiral.toRawUTF8(), 0.64f).x - dp(8.0f), plot.min.y + dp(4.0f)), p.textDim, spiral.toRawUTF8(),
             0.64f);
    }
    // Drag across to shift; the wheel moves the fine shift; a double click, none.
    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton("##decaleurPlot", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    const auto hovered = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    if (auto* shift = at(parameters, pane, motiond::decaleurShift); shift != nullptr) {
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
            changed |= apply(shift, shift->value + static_cast<double>(io.MouseDelta.x) * 2'000.0 / std::max(1.0f, plot.width()), set);
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            changed |= apply(shift, 0.0, set);
    }
    if (hovered && io.MouseWheel != 0.0f) {
        if (auto* fine = at(parameters, pane, motiond::decaleurFine); fine != nullptr)
            changed |= apply(fine, fine->value + static_cast<double>(io.MouseWheel), set);
        ui::takeWheel();
    }
    if (hovered && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", L("Glissez de côté pour décaler ; molette : le réglage fin ; double-clic : aucun décalage.",
                                  "Drag sideways to shift; wheel: the fine shift; double click: no shift."));
    return changed;
}

bool motionDecaleurLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                         const Live& live, const Setter& set)
{
    return addedLarge(pane, parameters, position, size, colour, live, set,
                      L("DÉCALEUR  ·  le décaleur de fréquence", "DÉCALEUR  ·  the frequency shifter"),
                      { { L("DÉCALAGE", "SHIFT"), { motiond::decaleurShift, motiond::decaleurFine, motiond::decaleurSpread } },
                        { L("LFO", "LFO"),
                          { motiond::decaleurLfoShape, motiond::decaleurSync, motiond::decaleurLfoRate, motiond::decaleurLfoDepth } },
                        { L("SPIRALE", "SPIRAL"), { motiond::decaleurFeedback, motiond::decaleurDelay } },
                        { L("SORTIE", "OUTPUT"), { motiond::decaleurMix, motiond::decaleurOutput } } },
                      [&](const Box& inner) { return motionDecaleurDisplay(parameters, pane, inner, colour, live, set, true); });
}

// ---- Anneau ---

// The carrier's wave (what the sound is multiplied by: -1..1 for the ring,
// 0..1 for the amplitude) and its frequency on the spectrum, the LFO's reach
// in octaves as a band round it, where it is now as marks (left and right).
// Drag on the spectrum to set the frequency, the wheel for semitones.
bool motionAnneauDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                         const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto scale = large ? 0.82f : 0.66f;
    const auto shapeNow = whole(static_cast<float>(get(parameters, pane, motiond::anneauShape, 0.0)), motiond::anneauShapeCount);
    const auto chooserX = box.max.x - textSize((motionAnneauShapeName(shapeNow) + "  ").toRawUTF8(), scale).x - dp(16.0f);
    changed |= chooser("##anneauShape", at(parameters, pane, motiond::anneauShape), motiond::anneauShapeCount, motionAnneauShapeName,
                       motionAnneauShapeHint, ImVec2(chooserX, box.min.y + dp(2.0f)), colour, set, scale);
    const auto modeWidth = dp(large ? 180.0f : 104.0f);
    changed |= segmented("##anneauMode", at(parameters, pane, motiond::anneauMode), { L("Anneau", "Ring"), "Amplitude" },
                         ImVec2(chooserX - modeWidth - dp(8.0f), box.min.y + dp(2.0f)), modeWidth, dp(large ? 20.0f : 15.0f), colour, set,
                         L("Anneau : le son fois la porteuse, l'original disparaît. Amplitude : la porteuse fait varier le volume, "
                           "l'original reste.",
                           "Ring: the sound times the carrier, the original goes. Amplitude: the carrier moves the volume, the original "
                           "stays."));
    const auto values = studioValues<motiond::anneauParameterCount>(parameters, pane, "anneau");
    const auto* v = values.data();
    const auto shape = whole(values[motiond::anneauShape], motiond::anneauShapeCount);
    const auto ring = whole(values[motiond::anneauMode], motiond::anneauModeCount) == 0;
    const Box plot { ImVec2(box.min.x + dp(4.0f), box.min.y + dp(large ? 28.0f : 21.0f)), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    const auto waveWidth = plot.width() * (large ? 0.28f : 0.34f);
    const Box wave { plot.min, ImVec2(plot.min.x + waveWidth, plot.max.y) };
    const Box axis { ImVec2(wave.max.x + dp(6.0f), plot.min.y), plot.max };
    list->AddRectFilled(wave.min, wave.max, theme::alpha(kDark, 0.5f), dp(3.0f));
    list->AddRectFilled(axis.min, axis.max, theme::alpha(kDark, 0.5f), dp(3.0f));
    // Two periods of the carrier.
    const auto middle = ring ? (wave.min.y + wave.max.y) * 0.5f : wave.max.y - wave.height() * 0.08f;
    const auto swing = wave.height() * (ring ? 0.4f : 0.8f);
    list->AddLine(ImVec2(wave.min.x, middle), ImVec2(wave.max.x, middle), p.gridStrong);
    std::vector<ImVec2> points;
    for (auto x = wave.min.x + dp(3.0f); x <= wave.max.x - dp(3.0f); x += dp(1.0f)) {
        const auto t = 2.0 * static_cast<double>((x - wave.min.x - dp(3.0f)) / std::max(1.0f, wave.width() - dp(6.0f)));
        const auto c = motiond::anneauWaveAt(shape, t);
        points.emplace_back(x, middle - static_cast<float>(ring ? c : (1.0 + c) * 0.5) * swing);
    }
    list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(large ? 2.0f : 1.4f));
    text(list, ImVec2(wave.min.x + dp(4.0f), wave.min.y + dp(2.0f)), p.textFaint, L("porteuse", "carrier"), large ? 0.62f : 0.56f);
    // The carrier on the spectrum.
    frequencyGrid(list, axis, large);
    spectrumBehind(list, axis, live.before, p.textDim);
    if (large)
        spectrumBehind(list, axis, live.after, colour);
    const auto hz = static_cast<double>(values[motiond::anneauFrequency]);
    const auto reach = static_cast<double>(values[motiond::anneauLfoDepth]);
    if (reach > 0.005)
        list->AddRectFilled(ImVec2(xOfHz(axis, motiond::anneauFrequencyAt(v, -1.0)), axis.min.y),
                            ImVec2(xOfHz(axis, motiond::anneauFrequencyAt(v, 1.0)), axis.max.y), theme::alpha(colour, 0.16f));
    const auto x = xOfHz(axis, hz);
    list->AddLine(ImVec2(x, axis.min.y), ImVec2(x, axis.max.y), colour, dp(large ? 2.0f : 1.5f));
    if (motionReads(live, motiond::anneauReadingCount))
        for (int side = 0; side < 2; ++side) {
            const auto now = xOfHz(axis, static_cast<double>(live.readings[side == 0 ? motiond::anneauReadingLeftHz : motiond::anneauReadingRightHz]));
            const auto tint = side == 0 ? kMotionLeft : kMotionRight;
            const auto y = side == 0 ? axis.min.y : axis.max.y;
            const auto tip = side == 0 ? dp(7.0f) : -dp(7.0f);
            list->AddTriangleFilled(ImVec2(now - dp(5.0f), y), ImVec2(now + dp(5.0f), y), ImVec2(now, y + tip), tint);
        }
    const auto label = motionHertz(hz);
    text(list, ImVec2(std::min(x + dp(6.0f), axis.max.x - textSize(label.toRawUTF8(), large ? 1.3f : 0.72f).x - dp(4.0f)), axis.min.y + dp(3.0f)),
         colour, label.toRawUTF8(), large ? 1.3f : 0.72f);
    if (large && reach > 0.005) {
        const auto lfo = ui::Ls("LFO ±", "LFO ±") + juce::String(reach, 2) + " oct  ·  "
                         + motionCycleText(values[motiond::anneauSync], values[motiond::anneauLfoRate]);
        text(list, ImVec2(axis.min.x + dp(6.0f), axis.max.y - ImGui::GetFontSize() * 1.6f), p.textDim, lfo.toRawUTF8(), 0.62f);
    }
    // Drag on the spectrum for the frequency; the wheel by semitones.
    ImGui::SetCursorScreenPos(axis.min);
    ImGui::InvisibleButton("##anneauAxis", ImVec2(std::max(1.0f, axis.width()), std::max(1.0f, axis.height())));
    const auto hovered = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    if (auto* frequency = at(parameters, pane, motiond::anneauFrequency); frequency != nullptr) {
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            changed |= apply(frequency, hzOfX(axis, io.MousePos.x), set);
        if (hovered && io.MouseWheel != 0.0f) {
            changed |= apply(frequency, frequency->value * std::exp2(static_cast<double>(io.MouseWheel) / 12.0), set);
            ui::takeWheel();
        }
    }
    if (hovered && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", L("Cliquez ou glissez pour la fréquence de la porteuse ; molette : par demi-tons.",
                                  "Click or drag for the carrier's frequency; wheel: by semitones."));
    return changed;
}

bool motionAnneauLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                       const Live& live, const Setter& set)
{
    return addedLarge(pane, parameters, position, size, colour, live, set,
                      L("ANNEAU  ·  le son multiplié par une porteuse", "ANNEAU  ·  the sound times a carrier"),
                      { { L("PORTEUSE", "CARRIER"), { motiond::anneauFrequency } },
                        { L("LFO", "LFO"),
                          { motiond::anneauLfoShape, motiond::anneauSync, motiond::anneauLfoRate, motiond::anneauLfoDepth,
                            motiond::anneauStereo } },
                        { L("ENTRÉE", "INPUT"), { motiond::anneauLowCut, motiond::anneauDrive } },
                        { L("SORTIE", "OUTPUT"), { motiond::anneauMix, motiond::anneauOutput } } },
                      [&](const Box& inner) { return motionAnneauDisplay(parameters, pane, inner, colour, live, set, true); });
}

// ---- Rotor ---

// The cabinet seen from above, its front down: the drum (its opening) and on
// it the horn (its mouth and, across, its dummy) at the angles the processor
// reads, the two microphones round it. The speed as buttons; a click on the
// cabinet switches slow and fast; drag a microphone round for the width.
bool motionRotorDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                        const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto speedsWidth = dp(large ? 240.0f : 132.0f);
    changed |= segmented("##rotorSpeed", at(parameters, pane, motiond::rotorSpeed),
                         { L("Arrêt", "Stop"), L("Lent", "Slow"), L("Rapide", "Fast") },
                         ImVec2(box.max.x - speedsWidth - dp(4.0f), box.min.y + dp(2.0f)), speedsWidth, dp(large ? 20.0f : 15.0f), colour,
                         set,
                         L("La vitesse des rotors : ils y vont avec leur inertie (le tambour plus lentement que le pavillon).",
                           "The rotors' speed: they get there with their inertia (the drum slower than the horn)."));
    const auto values = studioValues<motiond::rotorParameterCount>(parameters, pane, "rotor");
    const auto* v = values.data();
    const Box area { ImVec2(box.min.x + dp(4.0f), box.min.y + dp(large ? 28.0f : 21.0f)), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    list->AddRectFilled(area.min, area.max, theme::alpha(kDark, 0.5f), dp(3.0f));
    const auto radius = std::max(dp(8.0f), std::min(area.height() * 0.5f - dp(large ? 16.0f : 9.0f), area.width() * 0.28f));
    const ImVec2 centre((area.min.x + area.max.x) * 0.5f, (area.min.y + area.max.y) * 0.5f - dp(large ? 4.0f : 0.0f));
    // Where the rotors point (the processor's, else at rest).
    const auto reads = motionReads(live, motiond::rotorReadingCount);
    const auto hornTurns = reads ? static_cast<double>(live.readings[motiond::rotorReadingHornAngle]) : 0.0;
    const auto drumTurns = reads ? static_cast<double>(live.readings[motiond::rotorReadingDrumAngle]) : 0.25;
    const auto hornHz = reads ? static_cast<double>(live.readings[motiond::rotorReadingHornHz]) : motiond::rotorHornTarget(v);
    const auto drumHz = reads ? static_cast<double>(live.readings[motiond::rotorReadingDrumHz]) : motiond::rotorDrumTarget(v);
    // A direction on screen for an angle round the cabinet (0: its front, down).
    const auto towards = [](const double radians) {
        return ImVec2(static_cast<float>(std::sin(radians)), static_cast<float>(std::cos(radians)));
    };
    const auto out = [&centre](const ImVec2 direction, const float length) {
        return ImVec2(centre.x + direction.x * length, centre.y + direction.y * length);
    };
    // The cabinet.
    const auto half = radius * 1.12f;
    list->AddRectFilled(ImVec2(centre.x - half, centre.y - half), ImVec2(centre.x + half, centre.y + half), theme::mix(p.bg2, colour, 0.12f),
                        dp(6.0f));
    list->AddRect(ImVec2(centre.x - half, centre.y - half), ImVec2(centre.x + half, centre.y + half), theme::alpha(colour, 0.6f), dp(6.0f),
                  dp(1.5f));
    // The drum and its opening.
    const auto drumRadius = radius * 0.92f;
    list->AddCircleFilled(centre, drumRadius, theme::alpha(kMotionDrum, 0.16f), 48);
    list->AddCircle(centre, drumRadius, theme::alpha(kMotionDrum, 0.7f), 48, dp(1.4f));
    const auto drumAngle = motiond::motionTwoPi * drumTurns;
    list->AddTriangleFilled(centre, out(towards(drumAngle - 0.45), drumRadius), out(towards(drumAngle + 0.45), drumRadius),
                            theme::alpha(kMotionDrum, 0.55f));
    // The horn: its mouth one way, its dummy the other.
    const auto hornAngle = motiond::motionTwoPi * hornTurns;
    const auto along = towards(hornAngle);
    const ImVec2 across(-along.y, along.x);
    const auto length = radius * 0.86f;
    const auto bell = [&](const float sign, const bool mouth) {
        const auto neck = dp(large ? 3.0f : 2.0f);
        const auto flare = length * 0.3f;
        const ImVec2 a(centre.x + along.x * sign * length * 0.25f + across.x * neck, centre.y + along.y * sign * length * 0.25f + across.y * neck);
        const ImVec2 b(centre.x + along.x * sign * length * 0.25f - across.x * neck, centre.y + along.y * sign * length * 0.25f - across.y * neck);
        const ImVec2 c(centre.x + along.x * sign * length - across.x * flare, centre.y + along.y * sign * length - across.y * flare);
        const ImVec2 d(centre.x + along.x * sign * length + across.x * flare, centre.y + along.y * sign * length + across.y * flare);
        if (mouth)
            list->AddQuadFilled(a, b, c, d, kMotionHorn);
        else
            list->AddQuad(a, b, c, d, theme::alpha(kMotionHorn, 0.7f), dp(1.3f));
    };
    list->AddLine(out(along, -length * 0.25f), out(along, length * 0.25f), kMotionHorn, dp(large ? 4.0f : 3.0f));
    bell(1.0f, true);
    bell(-1.0f, false);
    list->AddCircleFilled(centre, dp(large ? 5.0f : 3.5f), p.text);
    // The microphones.
    const auto micDistance = radius * 1.38f;
    std::array<ImVec2, 2> mics {};
    for (int mic = 0; mic < 2; ++mic)
        mics[static_cast<std::size_t>(mic)] = out(towards(motiond::rotorMicAngle(v, mic == 1)), micDistance);
    // Clicks and drags: a microphone round for the width, the cabinet for slow and fast.
    auto& drag = dragState(parameters, pane);
    const auto& io = ImGui::GetIO();
    ImGui::SetCursorScreenPos(area.min);
    ImGui::InvisibleButton("##rotorCabinet", ImVec2(std::max(1.0f, area.width()), std::max(1.0f, area.height())));
    const auto hovered = ImGui::IsItemHovered();
    auto hot = -1;
    if (hovered || ImGui::IsItemActive())
        for (int mic = 0; mic < 2; ++mic) {
            const auto& spot = mics[static_cast<std::size_t>(mic)];
            if (std::hypot(spot.x - io.MousePos.x, spot.y - io.MousePos.y) < dp(12.0f))
                hot = mic;
        }
    if (ImGui::IsItemActivated()) {
        drag.active = hot;
        const auto inside = std::abs(io.MousePos.x - centre.x) <= half && std::abs(io.MousePos.y - centre.y) <= half;
        if (hot < 0 && inside) {
            const auto speed = whole(values[motiond::rotorSpeed], motiond::rotorSpeedCount);
            changed |= apply(at(parameters, pane, motiond::rotorSpeed), speed == 2 ? 1.0 : 2.0, set);
        }
    }
    if (ImGui::IsItemActive() && drag.active >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
        const auto angle = std::abs(std::atan2(static_cast<double>(io.MousePos.x - centre.x), static_cast<double>(io.MousePos.y - centre.y)));
        changed |= apply(at(parameters, pane, motiond::rotorWidth), angle / (std::numbers::pi * 0.5), set);
    }
    if (ImGui::IsItemDeactivated())
        drag.active = -1;
    if (hovered && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", hot >= 0 ? L("Glissez le micro autour de la cabine : l'écart des deux micros (la largeur).",
                                             "Drag the microphone round the cabinet: how far apart the two are (the width).")
                                         : L("Cliquez sur la cabine : lent ou rapide.", "Click the cabinet: slow or fast."));
    for (int mic = 0; mic < 2; ++mic) {
        const auto& spot = mics[static_cast<std::size_t>(mic)];
        const auto tint = mic == 0 ? kMotionLeft : kMotionRight;
        if (large)
            handleDot(list, spot, tint, hot == mic || drag.active == mic, mic == 0 ? "L" : "R");
        else
            list->AddCircleFilled(spot, dp(hot == mic ? 4.5f : 3.5f), tint);
    }
    // The speeds, the horn's over the drum's.
    const auto textScale = large ? 0.78f : 0.56f;
    char speed[48];
    std::snprintf(speed, sizeof(speed), "%s %.2f Hz", L("Pavillon", "Horn"), hornHz);
    text(list, ImVec2(area.min.x + dp(large ? 8.0f : 4.0f), area.min.y + dp(large ? 6.0f : 3.0f)), kMotionHorn, speed, textScale);
    std::snprintf(speed, sizeof(speed), "%s %.2f Hz", L("Tambour", "Drum"), drumHz);
    text(list, ImVec2(area.min.x + dp(large ? 8.0f : 4.0f), area.min.y + dp(large ? 6.0f : 3.0f) + ImGui::GetFontSize() * textScale * 1.15f),
         kMotionDrum, speed, textScale);
    if (large)
        centred(list, ImVec2(centre.x, std::min(area.max.y - dp(8.0f), centre.y + half + dp(10.0f))), p.textFaint, L("avant", "front"), 0.6f);
    return changed;
}

bool motionRotorLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                      const Live& live, const Setter& set)
{
    return addedLarge(pane, parameters, position, size, colour, live, set,
                      L("ROTOR  ·  la cabine à haut-parleurs tournants", "ROTOR  ·  the rotating speaker cabinet"),
                      { { L("VITESSES", "SPEEDS"), { motiond::rotorSlow, motiond::rotorFast, motiond::rotorAcceleration } },
                        { L("CABINE", "CABINET"),
                          { motiond::rotorCrossover, motiond::rotorBalance, motiond::rotorDoppler, motiond::rotorWidth, motiond::rotorRoom } },
                        { L("PRÉAMPLI", "PREAMP"), { motiond::rotorDrive } },
                        { L("SORTIE", "OUTPUT"), { motiond::rotorMix, motiond::rotorOutput } } },
                      [&](const Box& inner) { return motionRotorDisplay(parameters, pane, inner, colour, live, set, true); });
}

// A card's display (large: the window's), true when a value changed.
bool motionDisplay(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour,
                    const Live& live, const Setter& set, const bool large)
{
    switch (kind) {
    case PaneKind::pompe: return motionPompeDisplay(parameters, pane, box, colour, live, set, large);
    case PaneKind::decaleur: return motionDecaleurDisplay(parameters, pane, box, colour, live, set, large);
    case PaneKind::anneau: return motionAnneauDisplay(parameters, pane, box, colour, live, set, large);
    case PaneKind::rotor: return motionRotorDisplay(parameters, pane, box, colour, live, set, large);
    default: return false;
    }
}

// The window: its display large and every control.
bool motionLarge(const PaneKind kind, const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size,
                  const ImU32 colour, const Live& live, const Setter& set)
{
    switch (kind) {
    case PaneKind::pompe: return motionPompeLarge(pane, parameters, position, size, colour, live, set);
    case PaneKind::decaleur: return motionDecaleurLarge(pane, parameters, position, size, colour, live, set);
    case PaneKind::anneau: return motionAnneauLarge(pane, parameters, position, size, colour, live, set);
    case PaneKind::rotor: return motionRotorLarge(pane, parameters, position, size, colour, live, set);
    default: return false;
    }
}

// The values a card shows as knobs (pane indices).
std::vector<int> motionCardFields(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane)
{
    juce::ignoreUnused(parameters, pane);
    switch (kind) {
    case PaneKind::pompe: return { motiond::pompeDepth, motiond::pompeCurve, motiond::pompeHold, motiond::pompeMix };
    case PaneKind::decaleur: return { motiond::decaleurShift, motiond::decaleurFine, motiond::decaleurFeedback, motiond::decaleurMix };
    case PaneKind::anneau: return { motiond::anneauFrequency, motiond::anneauMix, motiond::anneauLfoDepth, motiond::anneauDrive };
    case PaneKind::rotor: return { motiond::rotorDrive, motiond::rotorBalance, motiond::rotorDoppler, motiond::rotorMix };
    default: return {};
    }
}

// A value as these effects show it; empty for none of theirs.
juce::String motionFormat(const juce::String& binding, const double value)
{
    const auto off = [] { return ui::Ls("Coupé", "Off"); };
    if (binding.contains("pompe.")) {
        const auto suffix = binding.fromLastOccurrenceOf("pompe.", false, false);
        if (suffix == "shape")
            return motionChoiceName(motionChoiceAt(motiond::pompeShapes, value));
        if (suffix == "trigger")
            return value >= 0.5 ? ui::Ls("Touche", "Key") : juce::String("Tempo");
        if (suffix == "sync")
            return motionSyncName(value);
        if (suffix == "depth" || suffix == "mix" || suffix == "hold" || suffix.startsWith("point"))
            return percent(value);
        if (suffix == "phase" || suffix == "stereoOffset")
            return motionDegrees(value);
        if (suffix == "curve")
            return (value > 0.0 ? "+" : "") + juce::String(value, 2);
        if (suffix == "smoothing" && value < 0.05)
            return off();
        return {};
    }
    if (binding.contains("decaleur.")) {
        const auto suffix = binding.fromLastOccurrenceOf("decaleur.", false, false);
        if (suffix == "mode")
            return motionChoiceName(motionChoiceAt(motiond::decaleurModes, value));
        if (suffix == "lfoShape")
            return motionChoiceName(motionChoiceAt(motiond::lfoWaves, value));
        if (suffix == "sync")
            return motionSyncName(value);
        if (suffix == "shift" || suffix == "fine")
            return motionSignedHertz(value);
        if (suffix == "spread" || suffix == "lfoDepth")
            return value < 0.05 ? off() : juce::String::fromUTF8("\xc2\xb1") + motionHertz(value);
        if (suffix == "feedback" || suffix == "mix")
            return percent(value);
        if (suffix == "delay" && value < 0.5)
            return ui::Ls("Le plus court", "Shortest");
        return {};
    }
    if (binding.contains("anneau.")) {
        const auto suffix = binding.fromLastOccurrenceOf("anneau.", false, false);
        if (suffix == "shape")
            return motionChoiceName(motionChoiceAt(motiond::anneauShapes, value));
        if (suffix == "mode")
            return motionChoiceName(motionChoiceAt(motiond::anneauModes, value));
        if (suffix == "lfoShape")
            return motionChoiceName(motionChoiceAt(motiond::lfoWaves, value));
        if (suffix == "sync")
            return motionSyncName(value);
        if (suffix == "lfoDepth")
            return value < 0.005 ? off() : juce::String::fromUTF8("\xc2\xb1") + juce::String(value, 2) + " oct";
        if (suffix == "stereo")
            return motionDegrees(value);
        if (suffix == "mix")
            return percent(value);
        if (suffix == "lowCut" && value <= 20.5)
            return off();
        if (suffix == "frequency")
            return motionHertz(value);
        return {};
    }
    if (binding.contains("rotor.")) {
        const auto suffix = binding.fromLastOccurrenceOf("rotor.", false, false);
        if (suffix == "speed")
            return motionChoiceName(motionChoiceAt(motiond::rotorSpeeds, value));
        if (suffix == "balance") {
            if (std::abs(value) < 0.005)
                return ui::Ls("Égal", "Even");
            return (value > 0.0 ? ui::Ls("Pavillon ", "Horn ") : ui::Ls("Tambour ", "Drum ")) + percent(std::abs(value));
        }
        if (suffix == "doppler" || suffix == "width" || suffix == "room" || suffix == "mix")
            return percent(value);
        return {};
    }
    return {};
}

// A parameter of theirs that takes whole steps (a choice, a switch).
bool motionStepped(const juce::String& binding)
{
    for (const auto* suffix : { "pompe.shape", "pompe.sync", "pompe.trigger", "decaleur.mode", "decaleur.sync", "decaleur.lfoShape",
                                "anneau.shape", "anneau.mode", "anneau.sync", "anneau.lfoShape", "rotor.speed" })
        if (binding.endsWith(suffix))
            return true;
    return false;
}
