// The displays and large views of Empreinte, Givre
// (SpaceDesign.h computes what they draw). Included by StudioEffectsView.cpp
// inside its anonymous namespace: it has its helpers (knobCell, segmented,
// chooser, comboCell, switchCell, frame, title, section, text, fillUnder…)
// and addedLarge(), the common window layout.

namespace spacefxd = engine::spacefx;

// The response's early part, warm; its tail takes the card's colour.
constexpr ImU32 kEmpreinteEarly = IM_COL32(255, 196, 120, 255);
// Frost.
constexpr ImU32 kGivreFrost = IM_COL32(176, 226, 255, 255);

juce::String empreinteSpaceName(const int index)
{
    const auto& room = spacefxd::empreinteRooms[static_cast<std::size_t>(std::clamp(index, 0, spacefxd::empreinteSpaceCount - 1))];
    return juce::String::fromUTF8(ui::french() ? room.french : room.english);
}

juce::String empreinteSpaceHint(const int index)
{
    const auto& room = spacefxd::empreinteRooms[static_cast<std::size_t>(std::clamp(index, 0, spacefxd::empreinteSpaceCount - 1))];
    return juce::String::fromUTF8(ui::french() ? room.frenchHint : room.englishHint);
}

// The card's response file: its name (a click loads another) and a cross,
// or a button to load one. Drawn from `origin`; the width it takes (0 when
// the card holds no file of its own).
float empreinteFileRow(const ImVec2 origin, const bool large, const ImU32 colour, const bool draw)
{
    if (cardFile == nullptr)
        return 0.0f;
    const auto fontScale = large ? 0.84f : 0.7f;
    const auto padding = dp(5.0f);
    auto name = cardFile->name;
    const auto longest = large ? 34 : 16;
    if (name.length() > longest)
        name = name.substring(0, longest - 1) + juce::String::fromUTF8("\xe2\x80\xa6");
    const auto loadLabel = ui::Ls("Charger\xe2\x80\xa6", "Load\xe2\x80\xa6");
    const auto cross = juce::String::fromUTF8("\xc3\x97");
    const auto width = cardFile->name.isEmpty()
                           ? textSize(loadLabel.toRawUTF8(), fontScale).x + padding * 2.0f
                           : textSize(name.toRawUTF8(), fontScale).x + textSize(cross.toRawUTF8(), fontScale).x + padding * 4.0f + dp(2.0f);
    if (!draw)
        return width;
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushFont(nullptr, ui::currentFontBase() * fontScale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding, dp(1.0f)));
    if (cardFile->name.isEmpty()) {
        if (ImGui::Button((loadLabel + "##empreinteLoad").toRawUTF8()) && cardFile->load)
            cardFile->load();
        ui::tooltip(L("Charger une réponse impulsionnelle (.wav) : une vraie salle, jouée à la place des espaces intégrés",
                      "Load an impulse response (.wav): a real space, played instead of the built-in ones"));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::alpha(colour, 0.35f));
        if (ImGui::Button((name + "##empreinteChange").toRawUTF8()) && cardFile->load)
            cardFile->load();
        ImGui::PopStyleColor();
        ui::tooltip(L("Réponse chargée : un clic en charge une autre", "Response loaded: a click loads another"));
        ImGui::SameLine(0.0f, dp(2.0f));
        if (ImGui::Button((cross + "##empreinteClear").toRawUTF8()) && cardFile->clear)
            cardFile->clear();
        ui::tooltip(L("Revenir aux espaces intégrés", "Back to the built-in spaces"));
    }
    ImGui::PopStyleVar();
    ImGui::PopFont();
    return width;
}

// The response's envelope over time after the predelay: its first 80 ms
// warm, its tail in the card's colour, each at its level; drag the
// predelay's line sideways, either part up and down. The space's name to
// choose, or the file's.
bool empreinteDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                      const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    auto changed = false;
    const auto values = studioValues<spacefxd::empreinteParameterCount>(parameters, pane, "empreinte");
    const auto spaceIndex = whole(values[spacefxd::empreinteSpace], spacefxd::empreinteSpaceCount);
    const auto& room = spacefxd::empreinteRooms[static_cast<std::size_t>(spaceIndex)];
    const auto* readings = live.readings != nullptr && live.readingCount >= spacefxd::empreinteReadingCount ? live.readings : nullptr;
    const auto hasFile = cardFile != nullptr && cardFile->name.isNotEmpty();
    const auto fromFile = readings != nullptr ? readings[spacefxd::empreinteReadingFile] >= 0.5f : hasFile;
    const auto building = readings != nullptr && readings[spacefxd::empreinteReadingBuilding] >= 0.5f;
    const auto labelScale = large ? 0.7f : 0.58f;
    const auto top = dp(large ? 28.0f : 20.0f);
    const Box plot { ImVec2(box.min.x + dp(4.0f), box.min.y + top), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    if (plot.height() < dp(10.0f) || plot.width() < dp(20.0f))
        return false;
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.5f), dp(3.0f));

    // Its time: the response as read (else as the space makes it), after the predelay.
    const auto share = spacefxd::empreinteShareOf(values[spacefxd::empreinteLength]);
    const auto reverse = values[spacefxd::empreinteReverse] >= 0.5f;
    auto length = readings != nullptr ? static_cast<double>(readings[spacefxd::empreinteReadingSeconds]) : 0.0;
    if (length <= 0.0)
        length = spacefxd::empreinteFullSecondsOf(room) * share;
    const auto predelay = std::clamp(static_cast<double>(values[spacefxd::empreintePredelay]), 0.0, 250.0) * 0.001;
    const auto span = std::max(0.25, (predelay + length) * 1.06);
    const auto xOfSeconds = [&](const double seconds) { return plot.min.x + static_cast<float>(seconds / span) * plot.width(); };
    const auto secondsOfX = [&](const float x) {
        return static_cast<double>(std::clamp((x - plot.min.x) / std::max(1.0f, plot.width()), 0.0f, 1.0f)) * span;
    };
    const auto step = span <= 0.6 ? 0.1 : span <= 2.0 ? 0.25 : span <= 6.0 ? 1.0 : 2.0;
    for (auto t = step; t < span; t += step) {
        const auto x = xOfSeconds(t);
        list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y), p.grid);
        if (large) {
            char mark[16];
            std::snprintf(mark, sizeof(mark), "%.4g s", t);
            text(list, ImVec2(x + dp(2.0f), plot.max.y - ImGui::GetFontSize() * 0.75f), p.textFaint, mark, 0.56f);
        }
    }
    constexpr auto topDb = 9.0;
    constexpr auto floorDb = -66.0;
    const auto yOfDb = [&](const double db) {
        return plot.max.y - static_cast<float>(std::clamp((db - floorDb) / (topDb - floorDb), 0.0, 1.0)) * plot.height();
    };
    const auto earlyDb = static_cast<double>(values[spacefxd::empreinteEarly]);
    const auto tailDb = static_cast<double>(values[spacefxd::empreinteTail]);

    // The envelope: the processor's, else the space's as its parts make it.
    std::array<double, spacefxd::empreinteEnvelopePoints> envelope {};
    for (std::size_t point = 0; point < envelope.size(); ++point) {
        if (readings != nullptr) {
            envelope[point] = readings[static_cast<std::size_t>(spacefxd::empreinteReadingEnvelope) + point];
        } else {
            const auto t = (static_cast<double>(point) + 0.5) / static_cast<double>(envelope.size()) * length;
            envelope[point] = spacefxd::empreinteRoomEnvelopeDb(room, reverse ? length - t : t);
        }
    }
    std::vector<ImVec2> early;
    std::vector<ImVec2> tail;
    for (std::size_t point = 0; point < envelope.size(); ++point) {
        const auto t = (static_cast<double>(point) + 0.5) / static_cast<double>(envelope.size()) * length;
        const auto isEarly = t < spacefxd::empreinteEarlySeconds;
        const ImVec2 spot(xOfSeconds(predelay + t), yOfDb(envelope[point] + (isEarly ? earlyDb : tailDb)));
        (isEarly ? early : tail).push_back(spot);
        if (point == 0)
            early.insert(early.begin(), ImVec2(xOfSeconds(predelay), spot.y));
    }
    // Where the early part ends, both lines meet.
    if (!early.empty() && !tail.empty()) {
        const auto boundary = xOfSeconds(predelay + spacefxd::empreinteEarlySeconds);
        early.emplace_back(boundary, early.back().y);
        tail.insert(tail.begin(), ImVec2(boundary, tail.front().y));
    }
    if (!tail.empty()) {
        fillUnder(list, tail, plot.max.y, theme::alpha(colour, 0.18f));
        list->AddPolyline(tail.data(), static_cast<int>(tail.size()), colour, dp(large ? 2.0f : 1.4f));
    }
    if (!early.empty()) {
        fillUnder(list, early, plot.max.y, theme::alpha(kEmpreinteEarly, 0.22f));
        list->AddPolyline(early.data(), static_cast<int>(early.size()), kEmpreinteEarly, dp(large ? 2.0f : 1.4f));
    }
    // The predelay: the gap before it, its line.
    const auto predelayX = xOfSeconds(predelay);
    if (predelay > 0.0)
        list->AddRectFilled(plot.min, ImVec2(predelayX, plot.max.y), theme::alpha(kDark, 0.35f));
    const auto boundaryX = xOfSeconds(predelay + spacefxd::empreinteEarlySeconds);
    dashedLine(list, ImVec2(boundaryX, plot.min.y + dp(4.0f)), ImVec2(boundaryX, plot.max.y), theme::alpha(p.text, 0.18f));
    if (building) {
        const auto pulse = 0.4f + 0.3f * static_cast<float>(std::sin(ImGui::GetTime() * 6.0));
        list->AddRect(plot.min, plot.max, theme::alpha(colour, pulse), dp(3.0f), dp(1.5f));
    }

    // Dragged: the predelay sideways, the early part or the tail up and down.
    auto& state = dragState(parameters, pane);
    ImGui::PushID("##empreinte");
    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton("##empreintePlot", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    const auto hovered = ImGui::IsItemHovered();
    auto hot = -1;
    if (hovered || ImGui::IsItemActive())
        hot = std::abs(io.MousePos.x - predelayX) < dp(6.0f) ? 0 : io.MousePos.x < boundaryX ? 1 : 2;
    if (ImGui::IsItemActivated())
        state.active = hot;
    const auto dbPerPixel = (topDb - floorDb) / static_cast<double>(std::max(1.0f, plot.height()));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
        if (state.active == 0)
            changed |= apply(at(parameters, pane, spacefxd::empreintePredelay), secondsOfX(io.MousePos.x) * 1'000.0, set);
        else if (state.active == 1)
            changed |= apply(at(parameters, pane, spacefxd::empreinteEarly), earlyDb - io.MouseDelta.y * dbPerPixel, set);
        else if (state.active == 2)
            changed |= apply(at(parameters, pane, spacefxd::empreinteTail), tailDb - io.MouseDelta.y * dbPerPixel, set);
    }
    if (ImGui::IsItemDeactivated())
        state.active = -1;
    if (hovered && io.MouseWheel != 0.0f) {
        if (hot == 0)
            changed |= apply(at(parameters, pane, spacefxd::empreintePredelay), values[spacefxd::empreintePredelay] + io.MouseWheel * 5.0, set);
        else
            changed |= apply(at(parameters, pane, hot == 1 ? spacefxd::empreinteEarly : spacefxd::empreinteTail),
                             (hot == 1 ? earlyDb : tailDb) + io.MouseWheel * 0.5, set);
        ui::takeWheel();
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const auto which = hot == 0 ? spacefxd::empreintePredelay : hot == 1 ? spacefxd::empreinteEarly : spacefxd::empreinteTail;
        changed |= apply(at(parameters, pane, which), 0.0, set);
    }
    if (hovered)
        ImGui::SetMouseCursor(hot == 0 ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (hovered && large)
        ImGui::SetTooltip("%s", hot == 0   ? L("Pré-délai : glisser de côté", "Predelay: drag sideways")
                                : hot == 1 ? L("Début (80 premières ms) : glisser en hauteur", "Early part (first 80 ms): drag up and down")
                                           : L("Queue : glisser en hauteur", "Tail: drag up and down"));
    ImGui::PopID();
    const auto handleHot = state.active == 0 || (state.active < 0 && hot == 0);
    list->AddLine(ImVec2(predelayX, plot.min.y), ImVec2(predelayX, plot.max.y), handleHot ? p.text : theme::alpha(p.text, 0.45f),
                  dp(handleHot ? 2.0f : 1.2f));

    // What it is: its length, its levels, the predelay.
    char label[96];
    std::snprintf(label, sizeof(label), "%.2f s%s", length, reverse ? L("  à l'envers", "  reversed") : "");
    text(list, ImVec2(plot.min.x + dp(4.0f), plot.min.y + dp(2.0f)), p.textDim, label, labelScale);
    if (large) {
        std::snprintf(label, sizeof(label), "%s %+.1f dB   %s %+.1f dB   %s %.0f ms", L("début", "early"), earlyDb, L("queue", "tail"), tailDb,
                      L("pré-délai", "predelay"), predelay * 1'000.0);
        const auto size = textSize(label, 0.62f);
        text(list, ImVec2(plot.max.x - size.x - dp(6.0f), plot.min.y + dp(2.0f)), p.textFaint, label, 0.62f);
    }

    // Its header: the file (the card's own), or the space to choose.
    const auto chooserScale = large ? 0.8f : 0.64f;
    auto right = box.max.x - dp(4.0f);
    if (!hasFile) {
        const auto chooserWidth = textSize((empreinteSpaceName(spaceIndex) + "  ").toRawUTF8(), chooserScale).x + dp(16.0f);
        right -= chooserWidth;
        changed |= chooser("##empreinteSpace", at(parameters, pane, spacefxd::empreinteSpace), spacefxd::empreinteSpaceCount, empreinteSpaceName,
                           empreinteSpaceHint, ImVec2(right, box.min.y + dp(2.0f)), colour, set, chooserScale);
        right -= dp(8.0f);
    } else if (!fromFile) {
        // Loaded, not yet heard: the space plays until the new one is made.
        const auto waiting = L("\xe2\x80\xa6", "\xe2\x80\xa6");
        text(list, ImVec2(right - dp(12.0f), box.min.y + dp(3.0f)), p.textFaint, waiting, chooserScale);
        right -= dp(16.0f);
    }
    const auto fileWidth = empreinteFileRow(ImVec2(0.0f, 0.0f), large, colour, false);
    if (fileWidth > 0.0f) {
        // At the right, on a card too: the title holds the top left.
        const auto x = right - fileWidth;
        empreinteFileRow(ImVec2(std::max(box.min.x + dp(4.0f), x), box.min.y + dp(1.0f)), large, colour, true);
    }
    return changed;
}

// ---- Givre -------------------------------------------------------------------------------------

// A spectrum level (dB) where the cards' analysers draw it: tilted 4.5 dB an
// octave about 1 kHz, from -72 dB to 0.
float givreYOf(const Box& box, const double hz, const double db)
{
    const auto tilted = db + 4.5 * std::log2(std::max(hz, 1.0) / 1'000.0);
    const auto bottom = static_cast<double>(Spectrum::floorDb);
    const auto level = std::clamp((tilted - bottom) / (static_cast<double>(Spectrum::ceilingDb) - bottom), 0.0, 1.0);
    return box.max.y - static_cast<float>(level) * box.height();
}

// A small six-armed crystal.
void givreCrystal(ImDrawList* list, const ImVec2 centre, const float radius, const ImU32 colour)
{
    for (auto arm = 0; arm < 3; ++arm) {
        const auto angle = static_cast<float>(arm) * std::numbers::pi_v<float> / 3.0f + std::numbers::pi_v<float> / 6.0f;
        const ImVec2 d(std::cos(angle) * radius, std::sin(angle) * radius);
        list->AddLine(ImVec2(centre.x - d.x, centre.y - d.y), ImVec2(centre.x + d.x, centre.y + d.y), colour, dp(1.0f));
    }
}

// The live input's spectrum behind; what comes out (or what is frozen) over
// it as frost; the freeze to click; the shift as an arrow to drag.
bool givreDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                  const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    auto changed = false;
    const auto values = studioValues<spacefxd::givreParameterCount>(parameters, pane, "givre");
    const auto* readings = live.readings != nullptr && live.readingCount >= spacefxd::givreReadingCount ? live.readings : nullptr;
    const auto freezeOn = values[spacefxd::givreFreeze] >= 0.5f;
    const auto frozen = readings != nullptr ? readings[spacefxd::givreReadingFrozen] >= 0.5f : freezeOn;
    const auto top = dp(large ? 28.0f : 20.0f);
    const Box plot { ImVec2(box.min.x + dp(4.0f), box.min.y + top), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    if (plot.height() < dp(10.0f) || plot.width() < dp(20.0f))
        return false;
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.5f), dp(3.0f));
    frequencyGrid(list, plot, large);
    list->PushClipRect(plot.min, plot.max, true);
    spectrumBehind(list, plot, live.before, theme::mix(colour, p.textDim, 0.5f));

    // The output's spectrum, as frost.
    if (readings != nullptr) {
        std::vector<ImVec2> frost;
        frost.reserve(static_cast<std::size_t>(spacefxd::givreBands));
        for (int band = 0; band < spacefxd::givreBands; ++band) {
            const auto hz = spacefxd::givreBandHzOf(band);
            frost.emplace_back(xOfHz(plot, hz), givreYOf(plot, hz, readings[static_cast<std::size_t>(spacefxd::givreReadingSpectrum + band)]));
        }
        fillUnder(list, frost, plot.max.y, theme::alpha(kGivreFrost, frozen ? 0.32f : 0.18f));
        list->AddPolyline(frost.data(), static_cast<int>(frost.size()), theme::alpha(kGivreFrost, 0.25f), dp(large ? 5.0f : 3.5f));
        list->AddPolyline(frost.data(), static_cast<int>(frost.size()), kGivreFrost, dp(large ? 1.8f : 1.3f));
        if (frozen) {
            // Crystals on its peaks.
            for (std::size_t band = 1; band + 1 < frost.size(); ++band)
                if (frost[band].y < frost[band - 1].y && frost[band].y <= frost[band + 1].y && frost[band].y < plot.max.y - dp(8.0f))
                    givreCrystal(list, ImVec2(frost[band].x, frost[band].y - dp(5.0f)), dp(large ? 4.0f : 3.0f), theme::alpha(p.text, 0.8f));
        }
    } else {
        spectrumBehind(list, plot, live.after, kGivreFrost);
    }
    // The tone's tilt, about the middle.
    const auto tone = values[spacefxd::givreTone];
    if (std::abs(tone) > 0.01f) {
        const auto middle = (plot.min.y + plot.max.y) * 0.5f;
        const auto rise = static_cast<float>(spacefxd::givreToneDbOf(tone, 20'000.0) / 24.0) * plot.height() * 0.5f;
        dashedLine(list, ImVec2(plot.min.x, middle + rise), ImVec2(plot.max.x, middle - rise), theme::alpha(kGivreFrost, 0.45f));
    }
    list->PopClipRect();

    // The shift: an arrow from the middle of the range to where it goes.
    constexpr auto baseHz = 632.455532;
    const auto shift = static_cast<double>(values[spacefxd::givreShift]);
    const auto baseX = xOfHz(plot, baseHz);
    const auto headX = xOfHz(plot, baseHz * spacefxd::givreShiftRatioOf(static_cast<float>(shift)));
    const auto arrowY = plot.min.y + dp(large ? 16.0f : 12.0f);
    auto& state = dragState(parameters, pane);
    ImGui::PushID("##givre");
    ImGui::SetCursorScreenPos(ImVec2(headX - dp(9.0f), arrowY - dp(9.0f)));
    ImGui::InvisibleButton("##givreShift", ImVec2(dp(18.0f), dp(18.0f)));
    const auto arrowHovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated())
        state.active = 10;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        changed |= apply(at(parameters, pane, spacefxd::givreShift), 12.0 * std::log2(hzOfX(plot, io.MousePos.x) / baseHz), set);
    if (ImGui::IsItemDeactivated())
        state.active = -1;
    if (arrowHovered && io.MouseWheel != 0.0f) {
        changed |= apply(at(parameters, pane, spacefxd::givreShift), std::round(shift) + (io.MouseWheel > 0.0f ? 1.0 : -1.0), set);
        ui::takeWheel();
    }
    if (arrowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        changed |= apply(at(parameters, pane, spacefxd::givreShift), 0.0, set);
    if (arrowHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::SetTooltip("%s", L("Décalage : glisser de côté (double clic : aucun)", "Shift: drag sideways (double click: none)"));
    }
    const auto arrowHot = arrowHovered || state.active == 10;
    const auto arrowColour = arrowHot ? p.text : theme::alpha(kGivreFrost, 0.9f);
    list->AddCircleFilled(ImVec2(baseX, arrowY), dp(2.5f), theme::alpha(arrowColour, 0.7f));
    if (std::abs(headX - baseX) > dp(3.0f)) {
        const auto direction = headX > baseX ? 1.0f : -1.0f;
        list->AddLine(ImVec2(baseX, arrowY), ImVec2(headX - direction * dp(5.0f), arrowY), arrowColour, dp(1.6f));
        list->AddTriangleFilled(ImVec2(headX, arrowY), ImVec2(headX - direction * dp(7.0f), arrowY - dp(4.0f)),
                                ImVec2(headX - direction * dp(7.0f), arrowY + dp(4.0f)), arrowColour);
    } else {
        handleDot(list, ImVec2(headX, arrowY), theme::alpha(kGivreFrost, 0.8f), arrowHot);
    }
    if (large || std::abs(shift) >= 0.05) {
        char shown[24];
        std::snprintf(shown, sizeof(shown), std::abs(shift - std::round(shift)) < 0.05 ? "%+.0f st" : "%+.1f st", shift);
        text(list, ImVec2(headX + dp(8.0f), arrowY - ImGui::GetFontSize() * 0.36f), arrowColour, shown, large ? 0.64f : 0.56f);
    }

    // The freeze, to click.
    const auto freezeLabel = L("GEL", "FREEZE");
    const auto labelScale = large ? 0.72f : 0.6f;
    const auto labelSize = textSize(freezeLabel, labelScale);
    const ImVec2 buttonSize(labelSize.x + dp(24.0f), std::max(labelSize.y + dp(4.0f), dp(large ? 20.0f : 16.0f)));
    const ImVec2 buttonMin(box.max.x - dp(4.0f) - buttonSize.x, box.min.y + dp(2.0f));
    ImGui::SetCursorScreenPos(buttonMin);
    if (ImGui::InvisibleButton("##givreFreeze", buttonSize))
        changed |= apply(at(parameters, pane, spacefxd::givreFreeze), freezeOn ? 0.0 : 1.0, set);
    const auto buttonHovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        if (auto* parameter = at(parameters, pane, spacefxd::givreFreeze))
            openParameterMenu(*parameter);
    if (buttonHovered)
        ImGui::SetTooltip("%s", L("Gel : fige le spectre et le fait durer sans fin", "Freeze: holds the spectrum and makes it last for ever"));
    ImGui::PopID();
    const ImVec2 buttonMax(buttonMin.x + buttonSize.x, buttonMin.y + buttonSize.y);
    list->AddRectFilled(buttonMin, buttonMax, freezeOn ? kGivreFrost : buttonHovered ? p.bg4 : p.bg3, dp(4.0f));
    list->AddRect(buttonMin, buttonMax, theme::alpha(kGivreFrost, 0.7f), dp(4.0f));
    givreCrystal(list, ImVec2(buttonMin.x + dp(9.0f), (buttonMin.y + buttonMax.y) * 0.5f), dp(4.0f), freezeOn ? kDark : kGivreFrost);
    text(list, ImVec2(buttonMin.x + dp(17.0f), (buttonMin.y + buttonMax.y) * 0.5f - labelSize.y * 0.5f), freezeOn ? kDark : p.text,
         freezeLabel, labelScale);

    // What it does: the blur, the smear, the sync.
    char shownValues[96];
    const auto blur = spacefxd::givreBlurSecondsOf(values[spacefxd::givreBlur]);
    const auto smear = spacefxd::givreSmearOctavesOf(values[spacefxd::givreSmear]);
    const auto* division = engine::noteDivisionFor(values[spacefxd::givreSync]);
    if (large)
        std::snprintf(shownValues, sizeof(shownValues), "%s %.2f s   %s %.2f oct%s%s", L("flou", "blur"), blur, L("étalement", "smear"), smear,
                      division != nullptr ? L("   synchro ", "   sync ") : "", division != nullptr ? std::string(division->name).c_str() : "");
    else
        std::snprintf(shownValues, sizeof(shownValues), "%s %.2f s", L("flou", "blur"), blur);
    const auto shownX = large ? box.min.x + std::min(box.width() * 0.45f, dp(330.0f))
                              : buttonMin.x - textSize(shownValues, 0.58f).x - dp(8.0f);
    text(list, ImVec2(shownX, box.min.y + dp(large ? 7.0f : 4.0f)), frozen ? kGivreFrost : p.textDim, shownValues, large ? 0.66f : 0.58f);
    return changed;
}

// A card's display (large: the window's), true when a value changed.
bool spacefxDisplay(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour,
                    const Live& live, const Setter& set, const bool large)
{
    if (kind == PaneKind::empreinte)
        return empreinteDisplay(parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::givre)
        return givreDisplay(parameters, pane, box, colour, live, set, large);
    return false;
}

// The window: its display large and every control.
bool spacefxLarge(const PaneKind kind, const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size,
                  const ImU32 colour, const Live& live, const Setter& set)
{
    const auto display = [&](const Box& box) { return spacefxDisplay(kind, parameters, pane, box, colour, live, set, true); };
    if (kind == PaneKind::empreinte)
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("EMPREINTE  ·  la réverbe par convolution", "EMPREINTE  ·  the convolution reverb"),
                          { { L("ESPACE", "SPACE"), { spacefxd::empreinteSpace, spacefxd::empreinteLength, spacefxd::empreinteReverse } },
                            { L("NIVEAUX", "LEVELS"), { spacefxd::empreintePredelay, spacefxd::empreinteEarly, spacefxd::empreinteTail } },
                            { L("COULEUR", "COLOUR"), { spacefxd::empreinteLowCut, spacefxd::empreinteHighCut, spacefxd::empreinteWidth } },
                            { L("SORTIE", "OUTPUT"), { spacefxd::empreinteMix, spacefxd::empreinteOutput } } },
                          display);
    if (kind == PaneKind::givre)
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("GIVRE  ·  le spectre figé, flouté, décalé", "GIVRE  ·  the spectrum frozen, blurred, shifted"),
                          { { L("GEL", "FREEZE"), { spacefxd::givreFreeze, spacefxd::givreSync, spacefxd::givreScatter } },
                            { L("SPECTRE", "SPECTRUM"), { spacefxd::givreBlur, spacefxd::givreSmear, spacefxd::givreShift, spacefxd::givreTone } },
                            { L("SORTIE", "OUTPUT"), { spacefxd::givreMix, spacefxd::givreOutput } } },
                          display);
    return false;
}

// The values a card shows as knobs (pane indices).
std::vector<int> spacefxCardFields(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane)
{
    juce::ignoreUnused(parameters, pane);
    if (kind == PaneKind::empreinte)
        return { spacefxd::empreinteMix, spacefxd::empreintePredelay, spacefxd::empreinteTail, spacefxd::empreinteHighCut };
    if (kind == PaneKind::givre)
        return { spacefxd::givreBlur, spacefxd::givreScatter, spacefxd::givreShift, spacefxd::givreMix };
    return {};
}

// A value as these effects show it; empty for none of theirs.
juce::String spacefxFormat(const juce::String& binding, const double value)
{
    const auto onOff = [](const double v) { return v >= 0.5 ? ui::Ls("Actif", "On") : ui::Ls("Coupé", "Off"); };
    if (binding.contains("empreinte.")) {
        const auto suffix = binding.fromLastOccurrenceOf("empreinte.", false, false);
        if (suffix == "space")
            return empreinteSpaceName(whole(static_cast<float>(value), spacefxd::empreinteSpaceCount));
        if (suffix == "reverse")
            return value >= 0.5 ? ui::Ls("À l'envers", "Reversed") : ui::Ls("À l'endroit", "Forward");
        if (suffix == "mix" || suffix == "width" || suffix == "length")
            return percent(value);
        if (suffix == "predelay")
            return value < 0.5 ? ui::Ls("Aucun", "None") : juce::String(juce::roundToInt(value)) + " ms";
        if (suffix == "lowCut")
            return !spacefxd::empreinteLowCutApplied(static_cast<float>(value)) ? ui::Ls("Désactivé", "Off")
                                                                                 : juce::String(juce::roundToInt(value)) + " Hz";
        if (suffix == "highCut")
            return !spacefxd::empreinteHighCutApplied(static_cast<float>(value)) ? ui::Ls("Désactivé", "Off")
                                                                                   : juce::String(value / 1'000.0, 1) + " kHz";
        return {};
    }
    if (binding.contains("givre.")) {
        const auto suffix = binding.fromLastOccurrenceOf("givre.", false, false);
        if (suffix == "freeze")
            return onOff(value);
        if (suffix == "sync") {
            const auto* division = engine::noteDivisionFor(static_cast<float>(value));
            return division == nullptr ? ui::Ls("Libre", "Free") : juce::String(std::string(division->name));
        }
        if (suffix == "blur") {
            const auto seconds = spacefxd::givreBlurSecondsOf(static_cast<float>(value));
            return seconds < 1.0e-3 ? ui::Ls("Aucun", "None") : juce::String(seconds, seconds < 1.0 ? 2 : 1) + " s";
        }
        if (suffix == "smear") {
            const auto octaves = spacefxd::givreSmearOctavesOf(static_cast<float>(value));
            return octaves < 1.0e-3 ? ui::Ls("Aucun", "None") : juce::String::fromUTF8("\xc2\xb1") + juce::String(octaves, 2) + " oct";
        }
        if (suffix == "shift") {
            const auto integral = std::abs(value - std::round(value)) < 0.05;
            return (value > 0.0 ? "+" : "") + juce::String(value, integral ? 0 : 1) + " st";
        }
        if (suffix == "tone")
            return std::abs(value) < 0.005 ? ui::Ls("Neutre", "Flat")
                                           : (value > 0.0 ? ui::Ls("Clair ", "Bright ") : ui::Ls("Sombre ", "Dark ")) + percent(std::abs(value));
        if (suffix == "scatter" || suffix == "mix")
            return percent(value);
        return {};
    }
    return {};
}

// A parameter of theirs that takes whole steps (a choice, a switch).
bool spacefxStepped(const juce::String& binding)
{
    for (const auto* suffix : { "empreinte.space", "empreinte.reverse", "givre.freeze", "givre.sync" })
        if (binding.endsWith(suffix))
            return true;
    return false;
}
