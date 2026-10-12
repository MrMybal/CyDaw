// The mixing effects' displays and large views: Pression, Fournaise, Écho
// Bande, Multibande Pro, Velours, Patine, Sonomètre (MixDesign.h computes what
// they draw). Included by
// StudioEffectsView.cpp inside its anonymous namespace, for its helpers.

namespace mixd = engine::mix;

// ---- Pression ---

constexpr std::array<ImU32, mixd::pressionBands> kBandColours { IM_COL32(255, 120, 150, 255), IM_COL32(255, 196, 90, 255),
                                                                IM_COL32(110, 190, 255, 255) };
constexpr ImU32 kHeldDown = IM_COL32(255, 92, 92, 255);
constexpr ImU32 kRaisedUp = IM_COL32(92, 150, 255, 255);
// A band's column: 0 dB at its top, this at its foot.
constexpr double kColumnFloorDb = -72.0;

const char* bandName(const int band)
{
    return band == 0 ? L("GRAVES", "LOW") : band == 1 ? L("MÉDIUMS", "MID") : L("AIGUS", "HIGH");
}

float yOfLevel(const Box& box, const double db)
{
    const auto t = std::clamp((db - kColumnFloorDb) / -kColumnFloorDb, 0.0, 1.0);
    return box.max.y - static_cast<float>(t) * box.height();
}

double levelOfY(const Box& box, const float y)
{
    const auto t = std::clamp((box.max.y - y) / std::max(1.0f, box.height()), 0.0f, 1.0f);
    return kColumnFloorDb * (1.0 - static_cast<double>(t));
}

juce::String hertz(const double hz)
{
    return hz >= 1'000.0 ? juce::String(hz / 1'000.0, hz >= 10'000.0 ? 0 : 1) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz";
}

// The bands' levels in a tapped frame, as the processor splits them.
std::array<double, mixd::pressionBands> bandLevels(const engine::AudioAnalysisFrame* frame, const double low, const double high)
{
    if (frame == nullptr || frame->waveformCount < 256)
        return { -120.0, -120.0, -120.0 };
    return mixd::pressionBandLevelsDb(frame->waveform.data(), static_cast<int>(frame->waveformCount),
                                      std::max(8'000.0, static_cast<double>(frame->sampleRate)), low, high);
}

// Three columns, one a band: what comes in (grey) and goes out (its colour),
// held down above its upper line (red), raised below its lower one (blue).
// Drag a line to move its threshold.
bool pressionDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                     const Setter& set, const bool large)
{
    juce::ignoreUnused(colour);
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<mixd::pressionParameterCount>(parameters, pane, "pression");
    auto& drag = dragState(parameters, pane);
    const auto top = box.min.y + dp(large ? 26.0f : 18.0f);
    const auto foot = box.max.y - ImGui::GetFontSize() * (large ? 1.4f : 1.0f);
    const auto width = box.width() / static_cast<float>(mixd::pressionBands);
    const auto low = static_cast<double>(values[mixd::pressionCrossLow]);
    const auto high = static_cast<double>(values[mixd::pressionCrossHigh]);
    const auto in = live.running ? bandLevels(live.beforeFrame, low, high) : std::array<double, 3> { -120.0, -120.0, -120.0 };
    const auto out = live.running ? bandLevels(live.afterFrame, low, high) : std::array<double, 3> { -120.0, -120.0, -120.0 };
    const auto depth = static_cast<double>(values[mixd::pressionDepth]);
    const auto solo = whole(values[mixd::pressionSolo], mixd::pressionBands + 1);
    auto changed = false;
    for (int band = 0; band < mixd::pressionBands; ++band) {
        const auto b = static_cast<std::size_t>(band);
        const Box column { ImVec2(box.min.x + width * static_cast<float>(band) + dp(5.0f), top),
                           ImVec2(box.min.x + width * static_cast<float>(band + 1) - dp(5.0f), foot) };
        const auto quiet = solo != 0 && solo != band + 1;
        list->AddRectFilled(column.min, column.max, theme::alpha(kDark, quiet ? 0.3f : 0.6f), dp(3.0f));
        const auto bandValues = mixd::pressionBand(values.data(), band);
        const auto yDown = yOfLevel(column, bandValues.downThreshold);
        const auto yUp = yOfLevel(column, bandValues.upThreshold);
        const auto heldShare = static_cast<float>(depth * values[mixd::pressionDownward]);
        const auto raisedShare = static_cast<float>(depth * values[mixd::pressionUpward]);
        const auto tint = large ? 0.12f : 0.2f;
        list->AddRectFilled(column.min, ImVec2(column.max.x, yDown), theme::alpha(kHeldDown, 0.05f + tint * heldShare), dp(3.0f));
        list->AddRectFilled(ImVec2(column.min.x, yUp), column.max, theme::alpha(kRaisedUp, 0.05f + tint * raisedShare), dp(3.0f));
        // Its scale, every 12 dB.
        for (auto db = -12.0; db > kColumnFloorDb; db -= 12.0) {
            const auto y = yOfLevel(column, db);
            list->AddLine(ImVec2(column.min.x, y), ImVec2(column.max.x, y), theme::alpha(p.grid, 0.8f));
            if (large && band == 0) {
                char mark[8];
                std::snprintf(mark, sizeof(mark), "%.0f", db);
                text(list, ImVec2(column.max.x - dp(22.0f), y - ImGui::GetFontSize() * 0.66f), p.textFaint, mark, 0.56f);
            }
        }
        // The levels: in on the left, out on the right.
        const auto barWidth = column.width() * 0.28f;
        const auto middle = (column.min.x + column.max.x) * 0.5f;
        if (in[b] > kColumnFloorDb)
            list->AddRectFilled(ImVec2(middle - barWidth - dp(2.0f), yOfLevel(column, in[b])), ImVec2(middle - dp(2.0f), column.max.y),
                                theme::alpha(p.textDim, 0.6f), dp(2.0f));
        if (out[b] > kColumnFloorDb)
            list->AddRectFilled(ImVec2(middle + dp(2.0f), yOfLevel(column, out[b])), ImVec2(middle + barWidth + dp(2.0f), column.max.y),
                                theme::alpha(kBandColours[b], quiet ? 0.35f : 0.9f), dp(2.0f));
        // The thresholds, to drag.
        ImGui::SetCursorScreenPos(column.min);
        ImGui::PushID(band + 900);
        ImGui::InvisibleButton("##band", ImVec2(std::max(1.0f, column.width()), std::max(1.0f, column.height())));
        const auto mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsItemActivated())
            drag.active = band * 2 + (std::abs(mouse.y - yDown) <= std::abs(mouse.y - yUp) ? 0 : 1);
        if (ImGui::IsItemActive() && drag.active >= 0 && drag.active / 2 == band) {
            const auto field = drag.active % 2 == 0 ? mixd::bandDownThreshold : mixd::bandUpThreshold;
            changed |= apply(at(parameters, pane, mixd::pressionBandIndex(band, field)), levelOfY(column, mouse.y), set);
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && drag.active / 2 == band) {
            drag.active = -1;
        }
        const auto hovered = ImGui::IsItemHovered();
        if (hovered && !ImGui::IsItemActive())
            ImGui::SetTooltip("%s", L("Glissez la ligne rouge (au-dessus, le son est tenu vers le bas) ou la bleue (en dessous, il est "
                                      "relevé).",
                                      "Drag the red line (above it, the sound is held down) or the blue one (below it, it is raised)."));
        ImGui::PopID();
        const auto hotDown = hovered && std::abs(mouse.y - yDown) <= std::abs(mouse.y - yUp);
        list->AddLine(ImVec2(column.min.x, yDown), ImVec2(column.max.x, yDown), kHeldDown, dp(hotDown ? 2.6f : 1.6f));
        list->AddLine(ImVec2(column.min.x, yUp), ImVec2(column.max.x, yUp), kRaisedUp, dp(hovered && !hotDown ? 2.6f : 1.6f));
        if (large) {
            char label[24];
            std::snprintf(label, sizeof(label), "%.1f dB", bandValues.downThreshold);
            text(list, ImVec2(column.min.x + dp(4.0f), yDown - ImGui::GetFontSize() * 0.75f), kHeldDown, label, 0.58f);
            std::snprintf(label, sizeof(label), "%.1f dB", bandValues.upThreshold);
            text(list, ImVec2(column.min.x + dp(4.0f), yUp + dp(2.0f)), kRaisedUp, label, 0.58f);
        }
        // Its name and its gain under it.
        char label[48];
        std::snprintf(label, sizeof(label), "%s  %+.1f dB", bandName(band), values[static_cast<std::size_t>(mixd::pressionBandGain[b])]);
        text(list, ImVec2(column.min.x + dp(2.0f), foot + dp(1.0f)), quiet ? p.textFaint : kBandColours[b], label, large ? 0.66f : 0.56f);
    }
    // Where the bands part.
    if (large) {
        text(list, ImVec2(box.min.x + width - dp(18.0f), top - ImGui::GetFontSize() * 0.8f), p.textFaint, hertz(low).toRawUTF8(), 0.58f);
        text(list, ImVec2(box.min.x + width * 2.0f - dp(18.0f), top - ImGui::GetFontSize() * 0.8f), p.textFaint, hertz(high).toRawUTF8(),
             0.58f);
    }
    return changed;
}

bool pressionLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                   const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto diameter = 34.0f;
    const auto panelHeight = dp(30.0f) + knobHeight(diameter) * 2.0f + dp(28.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(200.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= pressionDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("PRESSION  ·  trois bandes, vers le haut et vers le bas", "PRESSION  ·  three bands, upward and downward"));
    list->PopClipRect();

    auto y = outer.max.y + dp(8.0f);
    struct Item {
        int index;
        const char* label;
    };
    section(list, ImVec2(position.x + dp(4.0f), y + dp(3.0f)), L("ÉCOUTER", "LISTEN"));
    changed |= segmented("##solo", at(parameters, pane, mixd::pressionSolo),
                         { L("Tout", "All"), L("Graves", "Low"), L("Médiums", "Mid"), L("Aigus", "High") },
                         ImVec2(position.x + dp(64.0f), y), dp(260.0f), dp(20.0f), colour, set,
                         L("Écouter une bande seule, pour régler ses seuils.", "Hear one band alone, to set its thresholds."));
    y += dp(28.0f);
    const std::array<Item, 8> globals { { { mixd::pressionDepth, L("Profondeur", "Depth") },
                                          { mixd::pressionTime, L("Temps", "Time") },
                                          { mixd::pressionUpward, L("Vers le haut", "Upward") },
                                          { mixd::pressionDownward, L("Vers le bas", "Downward") },
                                          { mixd::pressionInput, L("Entrée", "Input") },
                                          { mixd::pressionOutput, L("Sortie", "Output") },
                                          { mixd::pressionCrossLow, L("Coupure basse", "Low split") },
                                          { mixd::pressionCrossHigh, L("Coupure haute", "High split") } } };
    auto cell = std::min(dp(100.0f), (size.x - dp(8.0f)) / static_cast<float>(globals.size()));
    for (std::size_t index = 0; index < globals.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(4.0f) + cell * static_cast<float>(index), y), cell, at(parameters, pane, globals[index].index),
                            globals[index].label, colour, live, set, diameter);
    y += knobHeight(diameter) + dp(4.0f);
    // Each band's own knobs, under its column.
    const auto width = size.x / static_cast<float>(mixd::pressionBands);
    for (int band = 0; band < mixd::pressionBands; ++band) {
        const std::array<Item, 5> knobs { { { mixd::pressionBandGain[static_cast<std::size_t>(band)], L("Gain", "Gain") },
                                            { mixd::pressionBandIndex(band, mixd::bandDownRatio), L("Ratio bas", "Down ratio") },
                                            { mixd::pressionBandIndex(band, mixd::bandUpRatio), L("Ratio haut", "Up ratio") },
                                            { mixd::pressionBandIndex(band, mixd::bandAttack), L("Attaque", "Attack") },
                                            { mixd::pressionBandIndex(band, mixd::bandRelease), L("Relâch.", "Release") } } };
        cell = (width - dp(8.0f)) / static_cast<float>(knobs.size());
        const auto x = position.x + width * static_cast<float>(band) + dp(4.0f);
        ImGui::PushID(band + 950);
        for (std::size_t index = 0; index < knobs.size(); ++index)
            changed |= knobCell(ImVec2(x + cell * static_cast<float>(index), y), cell, at(parameters, pane, knobs[index].index),
                                knobs[index].label, kBandColours[static_cast<std::size_t>(band)], live, set, 30.0f);
        ImGui::PopID();
    }
    return changed;
}

// ---- Fournaise ---

constexpr ImU32 kHeat = IM_COL32(255, 110, 40, 255);

juce::String fournaiseName(const int index)
{
    const auto& style = mixd::fournaiseCharacter(index);
    return juce::String::fromUTF8(ui::french() ? style.french : style.english);
}
juce::String fournaiseHint(const int index)
{
    const auto& style = mixd::fournaiseCharacter(index);
    return juce::String::fromUTF8(ui::french() ? style.frenchHint : style.englishHint);
}

// Its filters' curve over the spectrum, the saturation's curve beside it,
// the heat of the drive underneath; the character to choose.
bool fournaiseDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                      const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<mixd::fournaiseParameterCount>(parameters, pane, "fournaise");
    const auto style = whole(values[mixd::fournaiseStyle], mixd::fournaiseStyleCount);
    const auto drive = std::clamp(static_cast<double>(values[mixd::fournaiseDrive]) / 10.0, 0.0, 1.0);
    const auto punished = values[mixd::fournaisePunish] >= 0.5f;
    const Box plot { ImVec2(box.min.x, box.min.y + dp(large ? 24.0f : 18.0f)), box.max };
    // The heat, from the bottom: how hard it is driven.
    const auto heat = static_cast<float>(std::min(1.0, drive * 0.8 + (punished ? 0.25 : 0.0)));
    list->AddRectFilledMultiColor(ImVec2(plot.min.x, plot.max.y - plot.height() * (0.25f + 0.5f * heat)), plot.max, IM_COL32(0, 0, 0, 0),
                                  IM_COL32(0, 0, 0, 0), theme::alpha(kHeat, 0.28f * heat), theme::alpha(kHeat, 0.28f * heat));
    frequencyGrid(list, plot, large);
    constexpr auto range = 18.0;
    levelGrid(list, plot, range, large);
    spectrumBehind(list, plot, live.after, colour);
    const auto filters = mixd::fournaiseFilters(values.data(), kCurveRate);
    const auto points = curveOver(plot, range, [&](const double hz) { return mixd::fournaiseToneDb(filters, hz, kCurveRate); });
    fillUnder(list, points, (plot.min.y + plot.max.y) * 0.5f, theme::alpha(colour, 0.16f));
    list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(large ? 2.2f : 1.6f));
    // The saturation: what the drive makes of a wave's shape.
    const auto side = std::min(plot.height() * 0.62f, dp(large ? 150.0f : 70.0f));
    const Box inset { ImVec2(plot.max.x - side - dp(8.0f), plot.max.y - side - dp(8.0f)), ImVec2(plot.max.x - dp(8.0f), plot.max.y - dp(8.0f)) };
    list->AddRectFilled(inset.min, inset.max, theme::alpha(kDark, 0.85f), dp(3.0f));
    list->AddRect(inset.min, inset.max, theme::alpha(p.line, 0.8f), dp(3.0f));
    dashedLine(list, ImVec2(inset.min.x, inset.max.y), ImVec2(inset.max.x, inset.min.y), theme::alpha(p.textFaint, 0.6f));
    const auto gain = std::pow(10.0, mixd::fournaiseDriveDb(style, values[mixd::fournaiseDrive], punished) / 20.0);
    auto peak = 1.0e-3;
    for (int i = -40; i <= 40; ++i)
        peak = std::max(peak, std::abs(mixd::fournaiseShape(style, gain * i / 80.0)));
    std::vector<ImVec2> transfer;
    for (int i = -40; i <= 40; ++i) {
        const auto y = mixd::fournaiseShape(style, gain * i / 80.0) / peak;
        transfer.emplace_back(inset.min.x + (i + 40) / 80.0f * inset.width(),
                              (inset.min.y + inset.max.y) * 0.5f - static_cast<float>(y) * inset.height() * 0.45f);
    }
    list->AddPolyline(transfer.data(), static_cast<int>(transfer.size()), kHeat, dp(1.6f));
    text(list, ImVec2(inset.min.x + dp(3.0f), inset.min.y + dp(2.0f)), p.textFaint, L("saturation", "saturation"), 0.56f);
    if (punished)
        text(list, ImVec2(inset.min.x + dp(3.0f), inset.max.y - ImGui::GetFontSize() * 0.75f), kHeat, "+20 dB", 0.56f);
    // The character, to choose.
    const auto changed = chooser("##fournaiseStyle", at(parameters, pane, mixd::fournaiseStyle), mixd::fournaiseStyleCount, fournaiseName,
                                 fournaiseHint,
                                 ImVec2(box.max.x - textSize((fournaiseName(style) + "  ").toRawUTF8(), large ? 0.82f : 0.7f).x - dp(16.0f),
                                        box.min.y + dp(2.0f)),
                                 colour, set, large ? 0.82f : 0.7f);
    if (large)
        text(list, ImVec2(box.min.x + dp(6.0f), box.min.y + dp(22.0f)), p.textDim, fournaiseHint(style).toRawUTF8(), 0.66f);
    return changed;
}

bool fournaiseLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                    const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto big = 46.0f;
    const auto panelHeight = dp(34.0f) + knobHeight(big) + dp(20.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(190.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= fournaiseDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("FOURNAISE  ·  filtres et saturation", "FOURNAISE  ·  filters and saturation"));
    list->PopClipRect();

    // The faceplate.
    auto y = outer.max.y + dp(8.0f);
    const Box plate { ImVec2(position.x, y), ImVec2(position.x + size.x, position.y + size.y) };
    list->AddRectFilled(plate.min, plate.max, theme::mix(colour, p.bg1, 0.88f), dp(6.0f));
    list->AddRect(plate.min, plate.max, theme::alpha(colour, 0.5f), dp(6.0f));
    y += dp(6.0f);
    std::vector<const char*> styles;
    for (int index = 0; index < mixd::fournaiseStyleCount; ++index)
        styles.push_back(ui::french() ? mixd::fournaiseCharacter(index).french : mixd::fournaiseCharacter(index).english);
    const auto stylesWidth = std::min(dp(420.0f), size.x * 0.42f);
    changed |= segmented("##styles", at(parameters, pane, mixd::fournaiseStyle), styles, ImVec2(position.x + dp(8.0f), y), stylesWidth,
                         dp(22.0f), colour, set,
                         L("Le caractère : bande (ronde), console (douce), classe A (chaude, harmoniques paires), lampe (mordante), "
                           "pentode (agressive).",
                           "The character: tape (round), console (soft), class A (warm, even harmonics), valve (biting), pentode "
                           "(aggressive)."));
    auto x = position.x + stylesWidth + dp(16.0f);
    const auto option = [&](const char* label, const int index, const float width, const char* tip, const ImU32 tint) {
        changed |= switchCell(label, at(parameters, pane, index), ImVec2(x, y), ImVec2(width, dp(22.0f)), tint, set, tip);
        x += width + dp(6.0f);
    };
    option(L("Punition", "Punish"), mixd::fournaisePunish, dp(84.0f),
           L("20 dB de plus dans la saturation : pour détruire.", "20 dB more into the saturation: to destroy."), kHeat);
    option(L("Niveau auto", "Auto level"), mixd::fournaiseAuto, dp(96.0f),
           L("Ramène le niveau de sortie à celui d'entrée : on entend le caractère, pas le volume.",
             "Brings the output back to the input's level: you hear the character, not the loudness."),
           colour);
    option(L("Bosse", "Thump"), mixd::fournaiseThump, dp(70.0f),
           L("Une bosse de grave juste au-dessus du coupe-bas.", "A bump of lows just above the low cut."), colour);
    option(L("Raide", "Steep"), mixd::fournaiseSteep, dp(70.0f),
           L("Coupe-haut à 24 dB par octave au lieu de 12.", "High cut at 24 dB per octave instead of 12."), colour);
    changed |= segmented("##oversampling", at(parameters, pane, mixd::fournaiseOversampling), { "1x", "2x", "4x" },
                         ImVec2(std::max(x, position.x + size.x - dp(130.0f)), y), dp(122.0f), dp(22.0f), colour, set,
                         L("Suréchantillonnage : moins de repliement dans les aigus saturés, plus de calcul.",
                           "Oversampling: less aliasing in saturated highs, more computing."));
    y += dp(30.0f);
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, 6> knobs { { { mixd::fournaiseDrive, "DRIVE" },
                                        { mixd::fournaiseTone, L("TONALITÉ", "TONE") },
                                        { mixd::fournaiseLowCut, L("COUPE-BAS", "LOW CUT") },
                                        { mixd::fournaiseHighCut, L("COUPE-HAUT", "HIGH CUT") },
                                        { mixd::fournaiseMix, L("MÉLANGE", "MIX") },
                                        { mixd::fournaiseOutput, L("SORTIE", "OUTPUT") } } };
    const auto cell = std::min(dp(130.0f), (size.x - dp(16.0f)) / static_cast<float>(knobs.size()));
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(8.0f) + cell * static_cast<float>(index), y), cell,
                            at(parameters, pane, knobs[index].index), knobs[index].label, index == 0 ? kHeat : colour, live, set, big);
    return changed;
}

// ---- Écho Bande ---

constexpr ImU32 kTape = IM_COL32(150, 112, 70, 255);
constexpr ImU32 kSpring = IM_COL32(150, 220, 200, 255);

juce::String echoModeName(const int index)
{
    static const std::array<const char*, mixd::echoModeCount> french {
        "1 : tête 1", "2 : tête 2", "3 : tête 3", "4 : têtes 2 et 3", "5 : tête 1 + ressort", "6 : tête 2 + ressort",
        "7 : tête 3 + ressort", "8 : têtes 1 et 2 + ressort", "9 : têtes 2 et 3 + ressort", "10 : têtes 1 et 3 + ressort",
        "11 : trois têtes + ressort", "12 : ressort seul"
    };
    static const std::array<const char*, mixd::echoModeCount> english {
        "1: head 1", "2: head 2", "3: head 3", "4: heads 2 and 3", "5: head 1 + spring", "6: head 2 + spring", "7: head 3 + spring",
        "8: heads 1 and 2 + spring", "9: heads 2 and 3 + spring", "10: heads 1 and 3 + spring", "11: three heads + spring",
        "12: spring alone"
    };
    const auto at = static_cast<std::size_t>(std::clamp(index, 0, mixd::echoModeCount - 1));
    return juce::String::fromUTF8(ui::french() ? french[at] : english[at]);
}
juce::String echoModeHint(const int index)
{
    juce::ignoreUnused(index);
    return ui::Ls("Les têtes de lecture qui jouent (la 2 et la 3 deux et trois fois plus tard que la 1), et le ressort.",
                  "The playback heads that play (2 and 3 twice and three times as late as 1), and the spring.");
}

// The tape machine: the tape past its heads, the ones that play lit, the
// spring; under it, the echoes an impulse makes.
bool echoDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                 const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<mixd::echoParameterCount>(parameters, pane, "echo-bande");
    const auto modeIndex = whole(values[mixd::echoMode], mixd::echoModeCount);
    const auto& mode = mixd::echoModes[static_cast<std::size_t>(modeIndex)];
    const auto* division = engine::noteDivisionFor(values[mixd::echoSync]);
    const auto head = mixd::echoHeadSeconds(values.data(), live.tempo, division != nullptr ? division->beats : 0.0);
    const Box area { ImVec2(box.min.x + dp(6.0f), box.min.y + dp(large ? 26.0f : 18.0f)), ImVec2(box.max.x - dp(6.0f), box.max.y - dp(4.0f)) };
    // The machine, in the upper part.
    const auto machineBottom = area.min.y + area.height() * 0.42f;
    const Box loop { ImVec2(area.min.x + dp(8.0f), area.min.y + dp(4.0f)), ImVec2(area.max.x - dp(large ? 90.0f : 46.0f), machineBottom) };
    list->AddRect(loop.min, loop.max, theme::alpha(kTape, 0.9f), loop.height() * 0.45f, dp(large ? 5.0f : 3.0f));
    // The record head, then the three playback heads where they sit.
    const auto headY = loop.max.y;
    const auto headSize = ImVec2(dp(large ? 22.0f : 12.0f), dp(large ? 18.0f : 10.0f));
    const auto start = loop.min.x + loop.width() * 0.12f;
    const auto span = loop.width() * 0.8f;
    const auto drawHead = [&](const float x, const bool on, const char* label, const ImU32 tint) {
        const ImVec2 min(x - headSize.x * 0.5f, headY - headSize.y * 0.5f);
        const ImVec2 max(x + headSize.x * 0.5f, headY + headSize.y * 0.5f);
        list->AddRectFilled(min, max, on ? tint : p.bg3, dp(2.0f));
        list->AddRect(min, max, theme::alpha(p.line, 0.9f), dp(2.0f));
        centred(list, ImVec2(x, headY), on ? kDark : p.textFaint, label, large ? 0.6f : 0.48f);
    };
    drawHead(start, true, L("E", "R"), IM_COL32(255, 92, 92, 255));
    for (int index = 0; index < 3; ++index)
        drawHead(start + span * static_cast<float>(mixd::echoHeadRatio[static_cast<std::size_t>(index)] / mixd::echoHeadRatio[2]),
                 mode.heads[static_cast<std::size_t>(index)], std::array<const char*, 3> { "1", "2", "3" }[static_cast<std::size_t>(index)],
                 colour);
    // The spring, coiled at the right.
    {
        const auto x0 = loop.max.x + dp(large ? 14.0f : 8.0f);
        const auto x1 = area.max.x - dp(4.0f);
        const auto y = (loop.min.y + loop.max.y) * 0.5f;
        std::vector<ImVec2> coil;
        for (int i = 0; i <= 48; ++i) {
            const auto t = static_cast<float>(i) / 48.0f;
            coil.emplace_back(x0 + (x1 - x0) * t, y + std::sin(t * 12.0f * std::numbers::pi_v<float>) * dp(large ? 8.0f : 4.0f));
        }
        list->AddPolyline(coil.data(), static_cast<int>(coil.size()), mode.spring ? kSpring : theme::alpha(p.textFaint, 0.5f),
                          dp(mode.spring ? 1.8f : 1.0f));
    }
    // The echoes, below: a bar a repeat, as loud as it comes back.
    const Box echoes { ImVec2(area.min.x, machineBottom + dp(large ? 18.0f : 10.0f)), area.max };
    list->AddLine(ImVec2(echoes.min.x, echoes.max.y), ImVec2(echoes.max.x, echoes.max.y), p.gridStrong);
    const auto seconds = std::max(1.0, head * mixd::echoHeadRatio[2] * 2.2);
    {
        const auto step = seconds <= 1.5 ? 0.25 : seconds <= 3.0 ? 0.5 : 1.0;
        for (auto t = step; t < seconds; t += step) {
            const auto x = echoes.min.x + static_cast<float>(t / seconds) * echoes.width();
            list->AddLine(ImVec2(x, echoes.min.y), ImVec2(x, echoes.max.y), p.grid);
            if (large) {
                char mark[12];
                std::snprintf(mark, sizeof(mark), t < 1.0 ? "%.0f ms" : "%.2g s", t < 1.0 ? t * 1'000.0 : t);
                text(list, ImVec2(x + dp(2.0f), echoes.max.y - ImGui::GetFontSize() * 0.75f), p.textFaint, mark, 0.56f);
            }
        }
    }
    std::array<mixd::EchoTap, 64> taps {};
    const auto count = mixd::echoPattern(values.data(), head, taps);
    const auto echoLevel = static_cast<double>(values[mixd::echoLevel]);
    for (int index = 0; index < count; ++index) {
        const auto& tap = taps[static_cast<std::size_t>(index)];
        if (tap.seconds > seconds)
            break;
        const auto x = echoes.min.x + static_cast<float>(tap.seconds / seconds) * echoes.width();
        const auto height = static_cast<float>(std::clamp(tap.level * std::max(0.15, echoLevel), 0.0, 1.0)) * echoes.height();
        list->AddLine(ImVec2(x, echoes.max.y), ImVec2(x, echoes.max.y - height), colour, dp(large ? 3.0f : 2.0f));
    }
    if (mode.spring && values[mixd::echoReverb] > 0.01f) {
        // The spring's tail: a haze dying away.
        const auto tail = static_cast<float>(std::min(1.0, 0.8 / seconds)) * echoes.width();
        list->AddRectFilledMultiColor(ImVec2(echoes.min.x, echoes.max.y - echoes.height() * 0.4f * values[mixd::echoReverb]),
                                      ImVec2(echoes.min.x + tail, echoes.max.y), theme::alpha(kSpring, 0.35f), IM_COL32(0, 0, 0, 0),
                                      IM_COL32(0, 0, 0, 0), theme::alpha(kSpring, 0.35f));
    }
    char label[48];
    if (division != nullptr)
        std::snprintf(label, sizeof(label), "%s  %.0f ms", std::string(division->name).c_str(), head * 1'000.0);
    else
        std::snprintf(label, sizeof(label), "%.0f ms", head * 1'000.0);
    text(list, ImVec2(echoes.min.x + dp(2.0f), echoes.min.y - ImGui::GetFontSize() * 0.2f), p.textDim, label, large ? 0.66f : 0.56f);
    // The mode, to choose.
    return chooser("##echoMode", at(parameters, pane, mixd::echoMode), mixd::echoModeCount, echoModeName, echoModeHint,
                   ImVec2(box.max.x - textSize((echoModeName(modeIndex) + "  ").toRawUTF8(), large ? 0.82f : 0.66f).x - dp(16.0f),
                          box.min.y + dp(2.0f)),
                   colour, set, large ? 0.82f : 0.66f);
}

bool echoLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
               const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto diameter = 38.0f;
    constexpr std::size_t knobCount = 11;
    const auto knobRows = (knobCount + knobColumns(size.x - dp(16.0f), knobCount, dp(82.0f)) - 1)
                          / knobColumns(size.x - dp(16.0f), knobCount, dp(82.0f));
    const auto panelHeight = dp(62.0f) + knobHeight(diameter) * static_cast<float>(knobRows) + dp(16.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(190.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= echoDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("ÉCHO BANDE  ·  têtes, bande et ressort", "ECHO BANDE  ·  heads, tape and spring"));
    list->PopClipRect();

    auto y = outer.max.y + dp(8.0f);
    const Box plate { ImVec2(position.x, y), ImVec2(position.x + size.x, position.y + size.y) };
    list->AddRectFilled(plate.min, plate.max, theme::mix(colour, p.bg1, 0.9f), dp(6.0f));
    list->AddRect(plate.min, plate.max, theme::alpha(colour, 0.5f), dp(6.0f));
    y += dp(6.0f);
    // The mode selector: twelve positions on two rows.
    {
        auto* mode = at(parameters, pane, mixd::echoMode);
        const auto current = mode != nullptr ? whole(static_cast<float>(mode->value), mixd::echoModeCount) : 0;
        const auto cellWidth = std::min(dp(150.0f), (size.x - dp(16.0f)) / 6.0f);
        ImGui::PushID("##modes");
        for (int index = 0; index < mixd::echoModeCount; ++index) {
            const ImVec2 min(position.x + dp(8.0f) + cellWidth * static_cast<float>(index % 6), y + dp(24.0f) * static_cast<float>(index / 6));
            const ImVec2 max(min.x + cellWidth - dp(3.0f), min.y + dp(21.0f));
            ImGui::SetCursorScreenPos(min);
            ImGui::PushID(index);
            if (ImGui::InvisibleButton("##mode", ImVec2(max.x - min.x, max.y - min.y)) && mode != nullptr)
                changed |= apply(mode, static_cast<double>(index), set);
            const auto hovered = ImGui::IsItemHovered();
            if (mode != nullptr && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                openParameterMenu(*mode);
            ImGui::PopID();
            const auto on = index == current;
            list->AddRectFilled(min, max, on ? colour : hovered ? p.bg4 : p.bg3, dp(3.0f));
            list->PushClipRect(min, max, true);
            centred(list, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), on ? kDark : p.textDim, echoModeName(index).toRawUTF8(),
                    0.64f);
            list->PopClipRect();
        }
        ImGui::PopID();
        // The sync beside it.
        std::vector<juce::String> syncs { ui::Ls("Temps libre", "Free time") };
        for (const auto& note : engine::noteDivisions)
            syncs.push_back(juce::String(std::string(note.name)));
        const auto syncX = position.x + dp(16.0f) + cellWidth * 6.0f;
        if (syncX + dp(110.0f) < position.x + size.x) {
            section(list, ImVec2(syncX, y + dp(2.0f)), L("SYNCHRO", "SYNC"));
            changed |= comboCell("##sync", at(parameters, pane, mixd::echoSync), syncs, ImVec2(syncX, y + dp(20.0f)),
                                 std::min(dp(140.0f), position.x + size.x - syncX - dp(8.0f)), set);
        }
    }
    y += dp(54.0f);
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, knobCount> knobs { { { mixd::echoTime, L("TEMPS", "TIME") },
                                         { mixd::echoIntensity, L("INTENSITÉ", "INTENSITY") },
                                         { mixd::echoLevel, L("ÉCHOS", "ECHO") },
                                         { mixd::echoReverb, L("RESSORT", "SPRING") },
                                         { mixd::echoBass, L("BASSES", "BASS") },
                                         { mixd::echoTreble, L("AIGUS", "TREBLE") },
                                         { mixd::echoWow, L("PLEURAGE", "WOW") },
                                         { mixd::echoDrive, L("ENTRÉE", "INPUT") },
                                         { mixd::echoAge, L("USURE", "WEAR") },
                                         { mixd::echoWidth, L("LARGEUR", "WIDTH") },
                                         { mixd::echoMix, L("MÉLANGE", "MIX") } } };
    const auto columns = knobColumns(size.x - dp(16.0f), knobs.size(), dp(82.0f));
    const auto cell = std::min(dp(110.0f), (size.x - dp(16.0f)) / static_cast<float>(columns));
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(8.0f) + cell * static_cast<float>(index % columns),
                                   y + knobHeight(diameter) * static_cast<float>(index / columns)),
                            cell, at(parameters, pane, knobs[index].index), knobs[index].label, index == 1 ? IM_COL32(255, 92, 92, 255) : colour,
                            live, set, diameter);
    return changed;
}

// ---- Multibande Pro ---

constexpr std::array<ImU32, mixd::multibandBands> kQuadColours { IM_COL32(255, 120, 150, 255), IM_COL32(255, 196, 90, 255),
                                                                 IM_COL32(120, 220, 140, 255), IM_COL32(110, 190, 255, 255) };
// The thresholds' scale: 0 dB at the top, this at the foot.
constexpr double kThresholdFloorDb = -60.0;

const char* multibandModeName(const int mode, const bool shortName)
{
    switch (mode) {
    case 0: return shortName ? L("Compr.", "Comp.") : L("Compresser", "Compress");
    case 1: return shortName ? L("Expan.", "Expand") : L("Expanser", "Expand");
    case 2: return shortName ? L("Relever", "Raise") : L("Relever le faible", "Raise the quiet");
    default: return shortName ? L("Éclater", "Burst") : L("Éclater le fort", "Burst the loud");
    }
}

// The spectrum split in four, each band its colour; in each its threshold
// (drag it up and down) and what it does now; the crossovers between them
// (drag them sideways).
bool multibandDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                      const Setter& set, const bool large)
{
    juce::ignoreUnused(colour);
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<mixd::multibandParameterCount>(parameters, pane, "multiband-pro");
    auto& drag = dragState(parameters, pane);
    const Box plot { ImVec2(box.min.x, box.min.y + dp(large ? 24.0f : 16.0f)), ImVec2(box.max.x, box.max.y - ImGui::GetFontSize() * 0.9f) };
    frequencyGrid(list, plot, large);
    spectrumBehind(list, plot, live.before, p.textDim);
    spectrumBehind(list, plot, live.after, colour);
    const std::array<double, 3> crossovers { values[mixd::multibandCross1], values[mixd::multibandCross2], values[mixd::multibandCross3] };
    std::array<float, 5> edges { plot.min.x, xOfHz(plot, crossovers[0]), xOfHz(plot, crossovers[1]), xOfHz(plot, crossovers[2]), plot.max.x };
    const auto yOf = [&plot](const double db) {
        return plot.min.y + static_cast<float>(std::clamp(db / kThresholdFloorDb, 0.0, 1.0)) * plot.height();
    };
    // The thresholds' scale, every 12 dB.
    for (auto db = -12.0; db > kThresholdFloorDb; db -= 12.0) {
        list->AddLine(ImVec2(plot.min.x, yOf(db)), ImVec2(plot.max.x, yOf(db)), theme::alpha(p.grid, 0.7f));
        if (large) {
            char mark[8];
            std::snprintf(mark, sizeof(mark), "%.0f", db);
            text(list, ImVec2(plot.max.x - dp(22.0f), yOf(db) - ImGui::GetFontSize() * 0.66f), p.textFaint, mark, 0.56f);
        }
    }
    std::array<double, mixd::multibandBands> levels { -120.0, -120.0, -120.0, -120.0 };
    if (live.running && live.beforeFrame != nullptr && live.beforeFrame->waveformCount >= 256)
        levels = mixd::multibandLevelsDb(live.beforeFrame->waveform.data(), static_cast<int>(live.beforeFrame->waveformCount),
                                         std::max(8'000.0, static_cast<double>(live.beforeFrame->sampleRate)), crossovers);
    auto anySolo = false;
    for (int band = 0; band < mixd::multibandBands; ++band)
        anySolo = anySolo || values[static_cast<std::size_t>(mixd::multibandIndex(band, mixd::multibandSolo))] >= 0.5f;
    const auto amount = static_cast<double>(values[mixd::multibandAmount]);
    for (int band = 0; band < mixd::multibandBands; ++band) {
        const auto b = static_cast<std::size_t>(band);
        const auto field = [&](const int which) { return values[static_cast<std::size_t>(mixd::multibandIndex(band, which))]; };
        const auto on = field(mixd::multibandOn) >= 0.5f;
        const auto heard = !anySolo || field(mixd::multibandSolo) >= 0.5f;
        const auto tint = kQuadColours[b];
        const Box region { ImVec2(edges[b] + dp(1.0f), plot.min.y), ImVec2(edges[b + 1] - dp(1.0f), plot.max.y) };
        list->AddRectFilled(region.min, region.max, theme::alpha(tint, on && heard ? 0.08f : 0.03f));
        const auto threshold = static_cast<double>(values[static_cast<std::size_t>(mixd::multibandThreshold(band))]);
        const auto y = yOf(threshold);
        // Where it acts: above its threshold or below.
        const auto mode = whole(field(mixd::multibandMode), mixd::multibandModeCount);
        const auto above = mode == 0 || mode == 3;
        list->AddRectFilled(ImVec2(region.min.x, above ? region.min.y : y), ImVec2(region.max.x, above ? y : region.max.y),
                            theme::alpha(tint, on && heard ? 0.16f : 0.05f));
        list->AddLine(ImVec2(region.min.x, y), ImVec2(region.max.x, y), on ? tint : theme::alpha(tint, 0.4f), dp(2.0f));
        // What comes in now, and what it does to it.
        if (levels[b] > kThresholdFloorDb) {
            const auto level = yOf(levels[b]);
            const auto centre = (region.min.x + region.max.x) * 0.5f;
            list->AddLine(ImVec2(centre - dp(10.0f), level), ImVec2(centre + dp(10.0f), level), p.text, dp(2.0f));
            if (on) {
                const auto change = amount * mixd::multibandGainDb(values.data(), band, levels[b]);
                if (std::abs(change) > 0.05) {
                    const auto end = yOf(levels[b] + change);
                    list->AddLine(ImVec2(centre, level), ImVec2(centre, end), change < 0.0 ? kReduction : IM_COL32(110, 220, 140, 255), dp(4.0f));
                }
            }
        }
        char label[48];
        std::snprintf(label, sizeof(label), "%d  %s", band + 1, multibandModeName(mode, true));
        text(list, ImVec2(region.min.x + dp(3.0f), plot.min.y + dp(2.0f)), on ? tint : p.textFaint, label, large ? 0.64f : 0.54f);
        if (large) {
            std::snprintf(label, sizeof(label), "%.1f dB", threshold);
            text(list, ImVec2(region.min.x + dp(3.0f), y + dp(2.0f)), tint, label, 0.58f);
        }
    }
    // The crossovers.
    for (std::size_t c = 0; c < 3; ++c)
        list->AddLine(ImVec2(edges[c + 1], plot.min.y), ImVec2(edges[c + 1], plot.max.y), theme::alpha(p.text, 0.6f), dp(1.5f));
    // Dragging: a crossover when near one, else the threshold of the band under the mouse.
    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton("##multiband", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    const auto mouse = ImGui::GetIO().MousePos;
    auto changed = false;
    if (ImGui::IsItemActivated()) {
        drag.active = -1;
        for (int c = 0; c < 3; ++c)
            if (std::abs(mouse.x - edges[static_cast<std::size_t>(c + 1)]) < dp(6.0f))
                drag.active = c;
        if (drag.active < 0)
            for (int band = 0; band < mixd::multibandBands; ++band)
                if (mouse.x >= edges[static_cast<std::size_t>(band)] && mouse.x < edges[static_cast<std::size_t>(band + 1)])
                    drag.active = 10 + band;
    }
    if (ImGui::IsItemActive() && drag.active >= 0) {
        if (drag.active < 3)
            changed |= apply(at(parameters, pane, mixd::multibandCross1 + drag.active), hzOfX(plot, mouse.x), set);
        else
            changed |= apply(at(parameters, pane, mixd::multibandThreshold(drag.active - 10)),
                             kThresholdFloorDb * std::clamp((mouse.y - plot.min.y) / std::max(1.0f, plot.height()), 0.0f, 1.0f), set);
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        drag.active = -1;
    }
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", L("Glissez une coupure de côté, ou le seuil d'une bande de haut en bas.",
                                  "Drag a crossover sideways, or a band's threshold up and down."));
    return changed;
}

bool multibandLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                    const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto panelHeight = dp(16.0f) + knobHeight(34.0f) + dp(34.0f) + knobHeight(28.0f) * 2.0f + dp(10.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(200.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= multibandDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("MULTIBANDE PRO  ·  quatre bandes", "MULTIBANDE PRO  ·  four bands"));
    list->PopClipRect();

    auto y = outer.max.y + dp(8.0f);
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, 7> globals { { { mixd::multibandInput, L("Entrée", "Input") },
                                          { mixd::multibandOutput, L("Sortie", "Output") },
                                          { mixd::multibandAmount, L("Quantité", "Amount") },
                                          { mixd::multibandLink, L("Lien stéréo", "Stereo link") },
                                          { mixd::multibandCross1, L("Coupure 1", "Split 1") },
                                          { mixd::multibandCross2, L("Coupure 2", "Split 2") },
                                          { mixd::multibandCross3, L("Coupure 3", "Split 3") } } };
    auto cell = std::min(dp(100.0f), (size.x - dp(8.0f)) / static_cast<float>(globals.size()));
    for (std::size_t index = 0; index < globals.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(4.0f) + cell * static_cast<float>(index), y), cell, at(parameters, pane, globals[index].index),
                            globals[index].label, colour, live, set, 34.0f);
    y += knobHeight(34.0f) + dp(6.0f);
    // Each band: its mode, on and alone, then its knobs on two rows.
    const auto width = size.x / static_cast<float>(mixd::multibandBands);
    for (int band = 0; band < mixd::multibandBands; ++band) {
        const auto tint = kQuadColours[static_cast<std::size_t>(band)];
        const auto x = position.x + width * static_cast<float>(band) + dp(4.0f);
        const auto inside = width - dp(8.0f);
        ImGui::PushID(band + 1'200);
        changed |= segmented("##mode", at(parameters, pane, mixd::multibandIndex(band, mixd::multibandMode)),
                             { multibandModeName(0, true), multibandModeName(1, true), multibandModeName(2, true), multibandModeName(3, true) },
                             ImVec2(x, y), inside - dp(96.0f), dp(20.0f), tint, set,
                             L("Compresser (le fort tenu), expanser (le faible poussé vers le bas, comme un gate), relever le faible, éclater "
                               "le fort (les attaques en avant).",
                               "Compress (the loud held), expand (the quiet pushed down, like a gate), raise the quiet, burst the loud "
                               "(the attacks brought out)."));
        changed |= switchCell(L("On", "On"), at(parameters, pane, mixd::multibandIndex(band, mixd::multibandOn)),
                              ImVec2(x + inside - dp(92.0f), y), ImVec2(dp(42.0f), dp(20.0f)), tint, set);
        changed |= switchCell(L("Solo", "Solo"), at(parameters, pane, mixd::multibandIndex(band, mixd::multibandSolo)),
                              ImVec2(x + inside - dp(46.0f), y), ImVec2(dp(46.0f), dp(20.0f)), tint, set);
        const std::array<Item, 8> knobs { { { mixd::multibandThreshold(band), L("Seuil", "Threshold") },
                                            { mixd::multibandIndex(band, mixd::multibandRatio), "Ratio" },
                                            { mixd::multibandIndex(band, mixd::multibandRange), L("Plage", "Range") },
                                            { mixd::multibandIndex(band, mixd::multibandMakeup), L("Gain", "Gain") },
                                            { mixd::multibandIndex(band, mixd::multibandAttack), L("Attaque", "Attack") },
                                            { mixd::multibandIndex(band, mixd::multibandRelease), L("Relâch.", "Release") },
                                            { mixd::multibandIndex(band, mixd::multibandKnee), L("Genou", "Knee") } } };
        cell = inside / 4.0f;
        for (std::size_t index = 0; index < 7; ++index)
            changed |= knobCell(ImVec2(x + cell * static_cast<float>(index % 4), y + dp(26.0f) + knobHeight(28.0f) * static_cast<float>(index / 4)),
                                cell, at(parameters, pane, knobs[index].index), knobs[index].label, tint, live, set, 28.0f);
        ImGui::PopID();
    }
    return changed;
}

// ---- Velours ---

constexpr ImU32 kCalmed = IM_COL32(255, 110, 140, 255);

// What Velours would take from the sound now: the same reckoning on the
// tapped frame (without its smoothing in time).
std::vector<float> veloursNow(const engine::AudioAnalysisFrame* frame, const std::array<float, mixd::veloursParameterCount>& values)
{
    static engine::Fft fft;
    static std::vector<std::complex<float>> spectrum;
    static std::vector<float> window;
    static std::vector<float> magnitude;
    static std::vector<double> scratch;
    if (fft.size() != static_cast<std::size_t>(mixd::veloursFrame)) {
        fft.prepare(mixd::veloursFrame);
        window.resize(mixd::veloursFrame);
        auto sum = 0.0;
        for (int n = 0; n < mixd::veloursFrame; ++n)
            sum += window[static_cast<std::size_t>(n)] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / mixd::veloursFrame)));
        for (auto& w : window)
            w *= static_cast<float>(2.0 / sum);
        spectrum.resize(mixd::veloursFrame);
        magnitude.resize(mixd::veloursBins);
        scratch.resize(mixd::veloursBins + 1);
    }
    std::vector<float> reduction(mixd::veloursBins, 0.0f);
    if (frame == nullptr || frame->waveformCount < static_cast<std::uint32_t>(mixd::veloursFrame))
        return reduction;
    for (std::size_t n = 0; n < spectrum.size(); ++n)
        spectrum[n] = { frame->waveform[n] * window[n], 0.0f };
    fft.forward(spectrum.data());
    for (std::size_t k = 0; k < magnitude.size(); ++k)
        magnitude[k] = static_cast<float>(20.0 * std::log10(std::max(1.0e-12, static_cast<double>(std::abs(spectrum[k])))));
    mixd::veloursReduction(magnitude.data(), mixd::veloursBins, std::max(8'000.0, static_cast<double>(frame->sampleRate)) / mixd::veloursFrame,
                           values.data(), scratch.data(), reduction.data());
    return reduction;
}

// The spectrum in and out; what is calmed, hanging from the top (red); the
// zone it works in, its edges to drag.
bool veloursDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                    const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<mixd::veloursParameterCount>(parameters, pane, "velours");
    auto& drag = dragState(parameters, pane);
    const Box plot { ImVec2(box.min.x, box.min.y + dp(large ? 24.0f : 16.0f)), box.max };
    frequencyGrid(list, plot, large);
    spectrumBehind(list, plot, live.before, p.textDim);
    spectrumBehind(list, plot, live.after, colour);
    // Outside the zone, darker.
    const auto low = xOfHz(plot, values[mixd::veloursFocusLow]);
    const auto high = xOfHz(plot, values[mixd::veloursFocusHigh]);
    list->AddRectFilled(plot.min, ImVec2(low, plot.max.y), theme::alpha(kDark, 0.55f));
    list->AddRectFilled(ImVec2(high, plot.min.y), plot.max, theme::alpha(kDark, 0.55f));
    for (const auto x : { low, high })
        list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y), theme::alpha(colour, 0.9f), dp(1.6f));
    // What it takes now: 24 dB at most down the upper half.
    if (live.running) {
        const auto reduction = veloursNow(live.beforeFrame, values);
        const auto rate = live.beforeFrame != nullptr ? std::max(8'000.0, static_cast<double>(live.beforeFrame->sampleRate)) : 48'000.0;
        const auto acting = std::any_of(reduction.begin(), reduction.end(), [](const float cut) { return cut > 0.05f; });
        std::vector<ImVec2> points;
        for (auto x = plot.min.x; x <= plot.max.x; x += dp(2.0f)) {
            const auto bin = std::clamp(static_cast<int>(std::lround(hzOfX(plot, x) * mixd::veloursFrame / rate)), 0, mixd::veloursBins - 1);
            // The loudest cut around the bin the pixel covers.
            auto most = 0.0f;
            for (int k = std::max(0, bin - 2); k <= std::min(mixd::veloursBins - 1, bin + 2); ++k)
                most = std::max(most, reduction[static_cast<std::size_t>(k)]);
            points.emplace_back(x, plot.min.y + std::min(1.0f, most / 24.0f) * plot.height() * 0.5f);
        }
        if (acting) {
            fillUnder(list, points, plot.min.y, theme::alpha(kCalmed, 0.35f));
            list->AddPolyline(points.data(), static_cast<int>(points.size()), kCalmed, dp(1.4f));
        }
    }
    char label[48];
    std::snprintf(label, sizeof(label), "%s %.1f dB", L("profondeur", "depth"), values[mixd::veloursDepth]);
    text(list, ImVec2(plot.min.x + dp(4.0f), plot.max.y - ImGui::GetFontSize() * (large ? 1.6f : 0.95f)), p.textDim, label,
         large ? 0.66f : 0.56f);
    if (values[mixd::veloursDelta] >= 0.5f)
        text(list, ImVec2(plot.max.x - dp(large ? 180.0f : 110.0f), plot.min.y + dp(2.0f)), kCalmed,
             L("ON ENTEND CE QUI EST RETIRÉ", "HEARING WHAT IS TAKEN"), large ? 0.66f : 0.52f);
    // The zone's edges, to drag.
    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton("##velours", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    const auto mouse = ImGui::GetIO().MousePos;
    auto changed = false;
    if (ImGui::IsItemActivated())
        drag.active = std::abs(mouse.x - low) <= std::abs(mouse.x - high) ? 0 : 1;
    if (ImGui::IsItemActive() && drag.active >= 0)
        changed |= apply(at(parameters, pane, drag.active == 0 ? mixd::veloursFocusLow : mixd::veloursFocusHigh), hzOfX(plot, mouse.x), set);
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        drag.active = -1;
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", L("Glissez les bords de la zone où Velours travaille. En rouge : ce qu'il retire maintenant.",
                                  "Drag the edges of the zone Velours works in. In red: what it takes now."));
    return changed;
}

bool veloursLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                  const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto diameter = 40.0f;
    const auto panelHeight = dp(34.0f) + knobHeight(diameter) + dp(14.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(200.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= veloursDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("VELOURS  ·  les résonances calmées", "VELOURS  ·  resonances calmed"));
    list->PopClipRect();

    auto y = outer.max.y + dp(8.0f);
    changed |= segmented("##mode", at(parameters, pane, mixd::veloursMode), { L("Doux", "Soft"), L("Dur", "Hard") },
                         ImVec2(position.x + dp(4.0f), y), dp(130.0f), dp(22.0f), colour, set,
                         L("Doux : il effleure les résonances. Dur : il les coupe plus nettement.",
                           "Soft: it touches the resonances lightly. Hard: it cuts them more firmly."));
    changed |= switchCell(L("Delta (ce qui est retiré)", "Delta (what is taken)"), at(parameters, pane, mixd::veloursDelta),
                          ImVec2(position.x + dp(144.0f), y), ImVec2(dp(190.0f), dp(22.0f)), kCalmed, set,
                          L("N'écouter que ce que Velours retire, pour régler sa profondeur.", "Hear only what Velours takes, to set its depth."));
    y += dp(30.0f);
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, 10> knobs { { { mixd::veloursDepth, L("PROFONDEUR", "DEPTH") },
                                         { mixd::veloursSharpness, L("NETTETÉ", "SHARPNESS") },
                                         { mixd::veloursSelectivity, L("SÉLECTIVITÉ", "SELECTIVITY") },
                                         { mixd::veloursAttack, L("ATTAQUE", "ATTACK") },
                                         { mixd::veloursRelease, L("RELÂCHEMENT", "RELEASE") },
                                         { mixd::veloursFocusLow, L("ZONE BAS", "FOCUS LOW") },
                                         { mixd::veloursFocusHigh, L("ZONE HAUT", "FOCUS HIGH") },
                                         { mixd::veloursMix, L("MÉLANGE", "MIX") },
                                         { mixd::veloursLink, L("LIEN", "LINK") },
                                         { mixd::veloursOutput, L("SORTIE", "OUTPUT") } } };
    const auto cell = std::min(dp(110.0f), (size.x - dp(8.0f)) / static_cast<float>(knobs.size()));
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(4.0f) + cell * static_cast<float>(index), y), cell, at(parameters, pane, knobs[index].index),
                            knobs[index].label, index == 0 ? kCalmed : colour, live, set, diameter);
    return changed;
}

// ---- Patine ---

constexpr std::array<ImU32, mixd::patineModules> kModuleColours { IM_COL32(220, 200, 150, 255), IM_COL32(150, 200, 255, 255),
                                                                  IM_COL32(255, 130, 80, 255),  IM_COL32(120, 230, 170, 255),
                                                                  IM_COL32(190, 150, 255, 255), IM_COL32(230, 170, 110, 255) };

const char* patineModuleName(const int module)
{
    switch (module) {
    case 0: return L("BRUIT", "NOISE");
    case 1: return L("ONDULATION", "WOBBLE");
    case 2: return L("DISTORSION", "DISTORT");
    case 3: return L("NUMÉRIQUE", "DIGITAL");
    case 4: return L("ESPACE", "SPACE");
    default: return L("MAGNÉTIQUE", "MAGNETIC");
    }
}

// Six modules side by side, each its amount (drag it up and down); the
// magnitude dims them all.
bool patineDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                   const Setter& set, const bool large)
{
    juce::ignoreUnused(colour, live);
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<mixd::patineParameterCount>(parameters, pane, "patine");
    const auto top = box.min.y + dp(large ? 26.0f : 18.0f);
    const auto foot = box.max.y - ImGui::GetFontSize() * (large ? 1.5f : 1.1f);
    const auto width = box.width() / static_cast<float>(mixd::patineModules);
    const auto magnitude = static_cast<float>(std::clamp(values[mixd::patineMagnitude], 0.0f, 1.0f));
    auto changed = false;
    for (int module = 0; module < mixd::patineModules; ++module) {
        const auto m = static_cast<std::size_t>(module);
        const Box column { ImVec2(box.min.x + width * static_cast<float>(module) + dp(5.0f), top),
                           ImVec2(box.min.x + width * static_cast<float>(module + 1) - dp(5.0f), foot) };
        list->AddRectFilled(column.min, column.max, theme::alpha(kDark, 0.6f), dp(4.0f));
        const auto amount = std::clamp(values[m], 0.0f, 1.0f);
        // The knob's reach, and what the magnitude leaves of it.
        const auto reach = column.max.y - amount * column.height();
        const auto heard = column.max.y - amount * magnitude * column.height();
        list->AddRectFilled(ImVec2(column.min.x, reach), column.max, theme::alpha(kModuleColours[m], 0.18f), dp(4.0f));
        list->AddRectFilled(ImVec2(column.min.x, heard), column.max, theme::alpha(kModuleColours[m], amount > 0.0f ? 0.7f : 0.0f), dp(4.0f));
        // Its kind, where it has one.
        const char* kind = nullptr;
        if (module == 0)
            kind = std::array<const char*, 3> { L("disque", "record"), L("bande", "tape"), L("secteur", "mains") }[static_cast<std::size_t>(
                whole(values[mixd::patineNoiseType], 3))];
        else if (module == 2)
            kind = std::array<const char*, 3> { L("lampe", "valve"), L("bande", "tape"), "transistor" }[static_cast<std::size_t>(
                whole(values[mixd::patineDistortType], 3))];
        if (kind != nullptr)
            centred(list, ImVec2((column.min.x + column.max.x) * 0.5f, column.min.y + ImGui::GetFontSize() * 0.6f), p.textDim, kind,
                    large ? 0.62f : 0.5f);
        ImGui::SetCursorScreenPos(column.min);
        ImGui::PushID(module + 1'300);
        ImGui::InvisibleButton("##module", ImVec2(std::max(1.0f, column.width()), std::max(1.0f, column.height())));
        if (ImGui::IsItemActive())
            changed |= apply(at(parameters, pane, module),
                             std::clamp((column.max.y - ImGui::GetIO().MousePos.y) / std::max(1.0f, column.height()), 0.0f, 1.0f), set);
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            ImGui::SetTooltip("%s", L("Glissez de haut en bas : la quantité du module.", "Drag up and down: the module's amount."));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            if (auto* parameter = at(parameters, pane, module))
                openParameterMenu(*parameter);
        ImGui::PopID();
        centred(list, ImVec2((column.min.x + column.max.x) * 0.5f, foot + ImGui::GetFontSize() * (large ? 0.7f : 0.5f)),
                amount > 0.0f ? kModuleColours[m] : p.textFaint, patineModuleName(module), large ? 0.66f : 0.5f);
    }
    return changed;
}

bool patineLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                 const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto panelHeight = dp(30.0f) + knobHeight(34.0f) * 2.0f + dp(20.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(190.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= patineDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("PATINE  ·  le son vieilli", "PATINE  ·  the sound made old"));
    list->PopClipRect();

    auto y = outer.max.y + dp(8.0f);
    const auto width = size.x / static_cast<float>(mixd::patineModules);
    // Under each module: its amount, and its kind or its own setting.
    for (int module = 0; module < mixd::patineModules; ++module) {
        const auto x = position.x + width * static_cast<float>(module) + dp(4.0f);
        const auto inside = width - dp(8.0f);
        const auto tint = kModuleColours[static_cast<std::size_t>(module)];
        ImGui::PushID(module + 1'400);
        if (module == 0)
            changed |= segmented("##noise", at(parameters, pane, mixd::patineNoiseType), { L("Disque", "Record"), L("Bande", "Tape"),
                                                                                          L("Secteur", "Mains") },
                                 ImVec2(x, y), inside, dp(20.0f), tint, set,
                                 L("Le bruit : d'un disque (souffle, craquements), d'une bande (souffle), du secteur (ronflement à 50 Hz).",
                                   "The noise: a record's (hiss, crackles), a tape's (hiss), the mains' (a 50 Hz hum)."));
        else if (module == 2)
            changed |= segmented("##distort", at(parameters, pane, mixd::patineDistortType), { L("Lampe", "Valve"), L("Bande", "Tape"),
                                                                                              "Transistor" },
                                 ImVec2(x, y), inside, dp(20.0f), tint, set);
        const auto knobY = y + dp(26.0f);
        const auto half = inside / 2.0f;
        changed |= knobCell(ImVec2(x, knobY), module == 1 || module == 4 ? half : inside, at(parameters, pane, module),
                            L("Quantité", "Amount"), tint, live, set, 34.0f);
        if (module == 1)
            changed |= knobCell(ImVec2(x + half, knobY), half, at(parameters, pane, mixd::patineWobbleRate), L("Vitesse", "Rate"), tint, live, set,
                                34.0f);
        if (module == 4)
            changed |= knobCell(ImVec2(x + half, knobY), half, at(parameters, pane, mixd::patineSpaceSize), L("Taille", "Size"), tint, live, set,
                                34.0f);
        ImGui::PopID();
    }
    y += dp(26.0f) + knobHeight(34.0f) + dp(4.0f);
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, 5> globals { { { mixd::patineMagnitude, L("AMPLEUR", "MAGNITUDE") },
                                          { mixd::patineTone, L("TONALITÉ", "TONE") },
                                          { mixd::patineWidth, L("LARGEUR", "WIDTH") },
                                          { mixd::patineMix, L("MÉLANGE", "MIX") },
                                          { mixd::patineOutput, L("SORTIE", "OUTPUT") } } };
    const auto cell = std::min(dp(110.0f), (size.x - dp(200.0f)) / static_cast<float>(globals.size()));
    for (std::size_t index = 0; index < globals.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(4.0f) + cell * static_cast<float>(index), y), cell, at(parameters, pane, globals[index].index),
                            globals[index].label, colour, live, set, 34.0f);
    changed |= switchCell(L("Le bruit suit le son", "Noise follows the sound"), at(parameters, pane, mixd::patineNoiseFollow),
                          ImVec2(position.x + size.x - dp(190.0f), y + dp(12.0f)), ImVec2(dp(184.0f), dp(22.0f)), kModuleColours[0], set,
                          L("Le bruit monte et descend avec le son, au lieu de rester sous lui.",
                            "The noise rises and falls with the sound instead of staying under it."));
    return changed;
}

// ---- Sonomètre ---

constexpr ImU32 kOnTarget = IM_COL32(110, 220, 140, 255);
constexpr ImU32 kOverTarget = IM_COL32(255, 160, 70, 255);
constexpr ImU32 kUnderTarget = IM_COL32(110, 170, 255, 255);

juce::String targetName(const int index)
{
    const auto& target = mixd::loudnessTargets[static_cast<std::size_t>(std::clamp(index, 0, mixd::loudnessTargetCount - 1))];
    char text[96];
    std::snprintf(text, sizeof(text), "%s  %.0f LUFS", ui::french() ? target.french : target.english, target.lufs);
    return juce::String::fromUTF8(text);
}
juce::String targetHint(const int index)
{
    const auto& target = mixd::loudnessTargets[static_cast<std::size_t>(std::clamp(index, 0, mixd::loudnessTargetCount - 1))];
    char text[96];
    std::snprintf(text, sizeof(text), ui::french() ? "Sonie intégrée visée %.0f LUFS, crête vraie %.1f dB au plus"
                                                   : "Integrated loudness aimed at %.0f LUFS, true peak %.1f dB at most",
                  target.lufs, target.truePeakDb);
    return juce::String::fromUTF8(text);
}

ImU32 againstTarget(const double value, const double target)
{
    if (value < -99.0)
        return theme::palette().textFaint;
    return value > target + 1.0 ? kOverTarget : value < target - 1.0 ? kUnderTarget : kOnTarget;
}

// The loudness: the integrated, large, against its target; momentary,
// short-term, true peak and range; the short-term loudness over the last
// minutes, the target across it.
bool sonometreDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                      const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<mixd::sonometreParameterCount>(parameters, pane, "sonometre");
    auto& meter = loudnessOf(parameters, pane);
    meter.feed(live.afterFrame, ImGui::GetTime());
    const auto targetIndex = whole(values[mixd::sonometreTarget], mixd::loudnessTargetCount);
    const auto& target = mixd::loudnessTargets[static_cast<std::size_t>(targetIndex)];
    const auto peakLimit = static_cast<double>(values[mixd::sonometreTruePeak]);
    const auto integrated = meter.integrated();
    // The numbers, on the left.
    const auto numbers = Box { ImVec2(box.min.x + dp(6.0f), box.min.y + dp(large ? 26.0f : 18.0f)),
                               ImVec2(box.min.x + box.width() * (large ? 0.3f : 0.38f), box.max.y - dp(4.0f)) };
    char value[32];
    std::snprintf(value, sizeof(value), integrated < -99.0 ? "--" : "%.1f", integrated);
    text(list, numbers.min, againstTarget(integrated, target.lufs), value, large ? 2.4f : 1.5f);
    const auto below = numbers.min.y + ImGui::GetFontSize() * (large ? 2.5f : 1.6f);
    text(list, ImVec2(numbers.min.x, below), p.textDim, L("LUFS intégrés", "LUFS integrated"), large ? 0.7f : 0.56f);
    struct Row {
        const char* label;
        double value;
        const char* unit;
        ImU32 tint;
    };
    const auto truePeak = meter.truePeakDb();
    const std::array<Row, 4> rows { { { L("Momentané", "Momentary"), meter.momentary(), "LUFS", p.text },
                                      { L("Court terme", "Short-term"), meter.shortTerm(), "LUFS", againstTarget(meter.shortTerm(), target.lufs) },
                                      { L("Crête vraie", "True peak"), truePeak, "dB", truePeak > peakLimit ? kReduction : p.text },
                                      { L("Plage (LRA)", "Range (LRA)"), meter.loudnessRange(), "LU", p.text } } };
    auto y = below + ImGui::GetFontSize() * (large ? 1.4f : 1.0f);
    for (const auto& row : rows) {
        if (y > numbers.max.y - ImGui::GetFontSize() * 0.8f)
            break;
        char line[64];
        std::snprintf(line, sizeof(line), row.value < -99.0 ? "%s  --" : "%s  %.1f %s", row.label, row.value, row.unit);
        text(list, ImVec2(numbers.min.x, y), row.tint, line, large ? 0.72f : 0.56f);
        y += ImGui::GetFontSize() * (large ? 1.1f : 0.8f);
    }
    // The history, on the right.
    const Box graph { ImVec2(numbers.max.x + dp(6.0f), box.min.y + dp(large ? 26.0f : 18.0f)), ImVec2(box.max.x - dp(6.0f), box.max.y - dp(6.0f)) };
    list->AddRectFilled(graph.min, graph.max, theme::alpha(kDark, 0.6f), dp(3.0f));
    constexpr auto lowest = -45.0;
    const auto yOf = [&graph](const double lufs) {
        return graph.max.y - static_cast<float>(std::clamp((lufs - lowest) / -lowest, 0.0, 1.0)) * graph.height();
    };
    for (const auto mark : { -36.0, -27.0, -18.0, -9.0 }) {
        list->AddLine(ImVec2(graph.min.x, yOf(mark)), ImVec2(graph.max.x, yOf(mark)), p.grid);
        if (large) {
            char label[8];
            std::snprintf(label, sizeof(label), "%.0f", mark);
            text(list, ImVec2(graph.min.x + dp(2.0f), yOf(mark) - ImGui::GetFontSize() * 0.66f), p.textFaint, label, 0.56f);
        }
    }
    dashedLine(list, ImVec2(graph.min.x, yOf(target.lufs)), ImVec2(graph.max.x, yOf(target.lufs)), kOnTarget, dp(1.4f));
    const auto& history = meter.history();
    const auto shown = static_cast<std::size_t>(std::clamp(static_cast<double>(values[mixd::sonometreHistory]), 10.0, 600.0) * 10.0);
    if (!history.empty()) {
        const auto first = history.size() > shown ? history.size() - shown : 0;
        std::vector<ImVec2> points;
        for (auto index = first; index < history.size(); ++index) {
            const auto x = graph.max.x - static_cast<float>(history.size() - 1 - index) / static_cast<float>(std::max<std::size_t>(1, shown - 1))
                                             * graph.width();
            points.emplace_back(x, yOf(history[index]));
        }
        fillUnder(list, points, graph.max.y, theme::alpha(colour, 0.15f));
        list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(1.6f));
    }
    // The target, to choose.
    return chooser("##target", at(parameters, pane, mixd::sonometreTarget), mixd::loudnessTargetCount, targetName, targetHint,
                   ImVec2(box.max.x - textSize((targetName(targetIndex) + "  ").toRawUTF8(), large ? 0.78f : 0.62f).x - dp(16.0f),
                          box.min.y + dp(2.0f)),
                   colour, set, large ? 0.78f : 0.62f);
}

bool sonometreLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                    const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto panelHeight = dp(70.0f) + knobHeight(34.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(220.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= sonometreDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("SONOMÈTRE  ·  sonie et crêtes", "SONOMETRE  ·  loudness and peaks"));
    list->PopClipRect();

    auto& meter = loudnessOf(parameters, pane);
    const auto values = studioValues<mixd::sonometreParameterCount>(parameters, pane, "sonometre");
    const auto& target = mixd::loudnessTarget(values[mixd::sonometreTarget]);
    auto y = outer.max.y + dp(8.0f);
    // Every figure, in a row.
    struct Figure {
        const char* label;
        double value;
        const char* unit;
    };
    const auto integrated = meter.integrated();
    const auto truePeak = meter.truePeakDb();
    const std::array<Figure, 7> figures { { { L("Intégré", "Integrated"), integrated, "LUFS" },
                                            { L("Court terme max", "Short-term max"), meter.shortTermMax(), "LUFS" },
                                            { L("Momentané max", "Momentary max"), meter.momentaryMax(), "LUFS" },
                                            { L("Plage (LRA)", "Range (LRA)"), meter.loudnessRange(), "LU" },
                                            { L("Crête vraie", "True peak"), truePeak, "dB" },
                                            { "PLR", integrated < -99.0 ? -120.0 : truePeak - integrated, "dB" },
                                            { L("Écart à la cible", "From the target"), integrated < -99.0 ? -120.0 : integrated - target.lufs, "LU" } } };
    const auto cell = (size.x - dp(130.0f)) / static_cast<float>(figures.size());
    for (std::size_t index = 0; index < figures.size(); ++index) {
        const auto x = position.x + dp(4.0f) + cell * static_cast<float>(index);
        list->AddRectFilled(ImVec2(x, y), ImVec2(x + cell - dp(6.0f), y + dp(52.0f)), theme::alpha(kDark, 0.6f), dp(4.0f));
        text(list, ImVec2(x + dp(6.0f), y + dp(4.0f)), p.textDim, figures[index].label, 0.64f);
        char value[32];
        std::snprintf(value, sizeof(value), figures[index].value < -99.0 ? "--" : "%+.1f %s", figures[index].value, figures[index].unit);
        if (index != 5 && index != 6 && index != 3)
            std::snprintf(value, sizeof(value), figures[index].value < -99.0 ? "--" : "%.1f %s", figures[index].value, figures[index].unit);
        auto tint = p.text;
        if (index == 0)
            tint = againstTarget(integrated, target.lufs);
        else if (index == 4 && truePeak > static_cast<double>(values[mixd::sonometreTruePeak]))
            tint = kReduction;
        text(list, ImVec2(x + dp(6.0f), y + dp(22.0f)), tint, value, 1.0f);
    }
    // Start again.
    ImGui::SetCursorScreenPos(ImVec2(position.x + size.x - dp(120.0f), y + dp(12.0f)));
    if (ImGui::Button(L("Remettre à zéro", "Reset"), ImVec2(dp(114.0f), dp(28.0f))))
        meter.reset();
    y += dp(62.0f);
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, 2> knobs { { { mixd::sonometreHistory, L("HISTORIQUE", "HISTORY") },
                                        { mixd::sonometreTruePeak, L("CRÊTE PERMISE", "PEAK ALLOWED") } } };
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(4.0f) + dp(110.0f) * static_cast<float>(index), y), dp(110.0f),
                            at(parameters, pane, knobs[index].index), knobs[index].label, colour, live, set, 34.0f);
    std::vector<juce::String> targets;
    for (int index = 0; index < mixd::loudnessTargetCount; ++index)
        targets.push_back(targetName(index));
    section(list, ImVec2(position.x + dp(240.0f), y + dp(4.0f)), L("CIBLE", "TARGET"));
    changed |= comboCell("##targets", at(parameters, pane, mixd::sonometreTarget), targets, ImVec2(position.x + dp(240.0f), y + dp(22.0f)),
                         std::min(dp(360.0f), size.x - dp(250.0f)), set);
    return changed;
}
