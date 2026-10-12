// Halo's display and large view (HaloDesign.h computes what they draw).
// Included by StudioEffectsView.cpp inside its anonymous namespace.

namespace halod = engine::halo;

constexpr ImU32 kHaloLow = IM_COL32(255, 170, 90, 255);
constexpr ImU32 kHaloHigh = IM_COL32(150, 200, 255, 255);

juce::String haloModeName(const int index)
{
    const auto& mode = halod::haloModes[static_cast<std::size_t>(std::clamp(index, 0, halod::haloModeCount - 1))];
    return juce::String::fromUTF8(ui::french() ? mode.french : mode.english);
}
juce::String haloModeHint(const int index)
{
    const auto& mode = halod::haloModes[static_cast<std::size_t>(std::clamp(index, 0, halod::haloModeCount - 1))];
    return juce::String::fromUTF8(ui::french() ? mode.frenchHint : mode.englishHint);
}

// The tail over time, its lows (orange) and its highs (blue), from the
// predelay on; the early reflections as ticks; the colour as its era.
bool haloDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live, const Setter& set,
                 const bool large)
{
    juce::ignoreUnused(live);
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<halod::haloParameterCount>(parameters, pane, "halo");
    const auto modeIndex = whole(values[halod::haloMode], halod::haloModeCount);
    const auto& mode = halod::haloModes[static_cast<std::size_t>(modeIndex)];
    const Box plot { ImVec2(box.min.x + dp(4.0f), box.min.y + dp(large ? 26.0f : 18.0f)), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.5f), dp(3.0f));
    const auto seconds = std::max(1.0, static_cast<double>(values[halod::haloDecay]) * 1.3 + values[halod::haloPredelay] * 0.001);
    // Its time marks.
    const auto step = seconds <= 2.0 ? 0.25 : seconds <= 6.0 ? 1.0 : seconds <= 15.0 ? 2.0 : 5.0;
    for (auto t = step; t < seconds; t += step) {
        const auto x = plot.min.x + static_cast<float>(t / seconds) * plot.width();
        list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y), p.grid);
        if (large) {
            char mark[16];
            std::snprintf(mark, sizeof(mark), "%.4g s", t);
            text(list, ImVec2(x + dp(2.0f), plot.max.y - ImGui::GetFontSize() * 0.75f), p.textFaint, mark, 0.56f);
        }
    }
    constexpr auto floor = -70.0;
    const auto yOf = [&plot](const double db) {
        return plot.max.y - static_cast<float>(std::clamp((db - floor) / -floor, 0.0, 1.0)) * plot.height();
    };
    for (const auto low : { true, false }) {
        std::vector<ImVec2> points;
        for (auto x = plot.min.x; x <= plot.max.x; x += dp(2.0f)) {
            const auto t = static_cast<double>((x - plot.min.x) / std::max(1.0f, plot.width())) * seconds;
            points.emplace_back(x, yOf(halod::haloTailDb(values.data(), t, low)));
        }
        const auto tint = low ? kHaloLow : kHaloHigh;
        fillUnder(list, points, plot.max.y, theme::alpha(tint, 0.14f));
        list->AddPolyline(points.data(), static_cast<int>(points.size()), tint, dp(large ? 2.0f : 1.4f));
    }
    // The early reflections.
    if (mode.early > 0.0) {
        const auto scale = halod::haloScale(values.data());
        const auto start = static_cast<double>(values[halod::haloPredelay]) * 0.001;
        for (const auto& [ms, gain] : halod::haloEarlyTaps) {
            const auto x = plot.min.x + static_cast<float>((start + ms * scale * 0.001) / seconds) * plot.width();
            const auto height = static_cast<float>(std::abs(gain) * mode.early) * plot.height() * 0.6f;
            list->AddLine(ImVec2(x, plot.max.y), ImVec2(x, plot.max.y - height), theme::alpha(p.text, 0.7f), dp(1.5f));
        }
    }
    char label[64];
    std::snprintf(label, sizeof(label), "%.2f s  %s", values[halod::haloDecay],
                  ui::french() ? halod::haloColours[static_cast<std::size_t>(whole(values[halod::haloColour], halod::haloColourCount))].french
                               : halod::haloColours[static_cast<std::size_t>(whole(values[halod::haloColour], halod::haloColourCount))].english);
    text(list, ImVec2(plot.min.x + dp(4.0f), plot.min.y + dp(2.0f)), p.textDim, label, large ? 0.7f : 0.56f);
    return chooser("##haloMode", at(parameters, pane, halod::haloMode), halod::haloModeCount, haloModeName, haloModeHint,
                   ImVec2(box.max.x - textSize((haloModeName(modeIndex) + "  ").toRawUTF8(), large ? 0.82f : 0.66f).x - dp(16.0f),
                          box.min.y + dp(2.0f)),
                   colour, set, large ? 0.82f : 0.66f);
}

bool haloLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
               const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto diameter = 36.0f;
    constexpr std::size_t knobCount = 15;
    const auto columns = knobColumns(size.x - dp(16.0f), knobCount, dp(80.0f));
    const auto rows = static_cast<float>((knobCount + columns - 1) / columns);
    const auto panelHeight = dp(36.0f) + knobHeight(diameter) * rows + dp(16.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(190.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= haloDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("HALO  ·  la réverbe de caractère", "HALO  ·  the reverb of characters"));
    list->PopClipRect();

    auto y = outer.max.y + dp(8.0f);
    const Box plate { ImVec2(position.x, y), ImVec2(position.x + size.x, position.y + size.y) };
    list->AddRectFilled(plate.min, plate.max, theme::mix(colour, p.bg1, 0.9f), dp(6.0f));
    list->AddRect(plate.min, plate.max, theme::alpha(colour, 0.5f), dp(6.0f));
    y += dp(6.0f);
    section(list, ImVec2(position.x + dp(10.0f), y + dp(4.0f)), L("COULEUR", "COLOUR"));
    changed |= segmented("##colour", at(parameters, pane, halod::haloColour), { L("Années 70", "1970s"), L("Années 80", "1980s"),
                                                                              L("Maintenant", "Now") },
                         ImVec2(position.x + dp(80.0f), y), dp(300.0f), dp(22.0f), colour, set,
                         L("L'époque : les années 70 sombres et granuleuses, les années 80 avec leur grain numérique, ou la clarté d'aujourd'hui.",
                           "The era: the dark, grainy 1970s, the 1980s and their digital grain, or today's clarity."));
    std::vector<juce::String> syncs { ui::Ls("Pré-délai libre", "Free predelay") };
    for (const auto& note : engine::noteDivisions)
        syncs.push_back(juce::String(std::string(note.name)));
    section(list, ImVec2(position.x + dp(400.0f), y + dp(4.0f)), L("SYNCHRO", "SYNC"));
    changed |= comboCell("##sync", at(parameters, pane, halod::haloSync), syncs, ImVec2(position.x + dp(460.0f), y), dp(150.0f), set);
    y += dp(30.0f);
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, knobCount> knobs { { { halod::haloMix, L("MÉLANGE", "MIX") },
                                                { halod::haloDecay, L("DURÉE", "DECAY") },
                                                { halod::haloPredelay, L("PRÉ-DÉLAI", "PREDELAY") },
                                                { halod::haloSize, L("TAILLE", "SIZE") },
                                                { halod::haloAttack, L("ATTAQUE", "ATTACK") },
                                                { halod::haloEarlyDiffusion, L("DIFF. DÉBUT", "EARLY DIFF.") },
                                                { halod::haloLateDiffusion, L("DIFF. QUEUE", "LATE DIFF.") },
                                                { halod::haloModDepth, L("MODULATION", "MOD DEPTH") },
                                                { halod::haloModRate, L("VITESSE MOD.", "MOD RATE") },
                                                { halod::haloBassMultiplier, L("GRAVES ×", "BASS ×") },
                                                { halod::haloBassCrossover, L("GRAVES JUSQU'À", "BASS UP TO") },
                                                { halod::haloDamping, L("AMORTISSEMENT", "DAMPING") },
                                                { halod::haloHighCut, L("COUPE-HAUT", "HIGH CUT") },
                                                { halod::haloLowCut, L("COUPE-BAS", "LOW CUT") },
                                                { halod::haloWidth, L("LARGEUR", "WIDTH") } } };
    const auto cell = std::min(dp(100.0f), (size.x - dp(16.0f)) / static_cast<float>(columns));
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(8.0f) + cell * static_cast<float>(index % columns),
                                   y + knobHeight(diameter) * static_cast<float>(index / columns)),
                            cell, at(parameters, pane, knobs[index].index), knobs[index].label,
                            index == 9 || index == 10 ? kHaloLow : index == 11 ? kHaloHigh : colour, live, set, diameter);
    return changed;
}
