// The displays and large views of Vocodeur, Dé-Esseur, Accordeur
// (VoiceDesign.h computes what they draw). Included by StudioEffectsView.cpp
// inside its anonymous namespace: it has its helpers (knobCell, segmented,
// chooser, comboCell, switchCell, frame, title, section, text, fillUnder…)
// and addedLarge(), the common window layout.

namespace voiced = engine::voice;

constexpr ImU32 kVoiceLeft = IM_COL32(110, 180, 255, 255);
constexpr ImU32 kVoiceRight = IM_COL32(255, 170, 90, 255);
constexpr ImU32 kVoiceInTune = IM_COL32(90, 222, 130, 255);
constexpr ImU32 kVoiceNear = IM_COL32(240, 200, 90, 255);
constexpr ImU32 kVoiceTaken = IM_COL32(255, 92, 92, 255);
// The filters' shapes drawn as their analogue models have them (no bend
// near the top of the axis).
constexpr double kVoiceCurveRate = 96'000.0;

// ---- Shared ---

juce::String voiceChoiceText(const voiced::VoiceChoice& choice, const bool hint)
{
    if (hint)
        return juce::String::fromUTF8(ui::french() ? choice.frenchHint : choice.englishHint);
    return juce::String::fromUTF8(ui::french() ? choice.french : choice.english);
}
juce::String vocoderCarrierName(const int index)
{
    return voiceChoiceText(voiced::vocoderCarrierOf(static_cast<float>(index)), false);
}
juce::String vocoderCarrierHint(const int index)
{
    return voiceChoiceText(voiced::vocoderCarrierOf(static_cast<float>(index)), true);
}
juce::String deEsserModeName(const int index)
{
    return voiceChoiceText(voiced::deEsserModeOf(static_cast<float>(index)), false);
}
juce::String deEsserModeHint(const int index)
{
    return voiceChoiceText(voiced::deEsserModeOf(static_cast<float>(index)), true);
}
juce::String deEsserDetectorName(const int index)
{
    return voiceChoiceText(voiced::deEsserDetectorOf(static_cast<float>(index)), false);
}
juce::String deEsserDetectorHint(const int index)
{
    return voiceChoiceText(voiced::deEsserDetectorOf(static_cast<float>(index)), true);
}
juce::String tunerDisplayName(const int index)
{
    return voiceChoiceText(voiced::tunerDisplayOf(static_cast<float>(index)), false);
}

// "C#3" (in French "Do#3"); a dash for none.
juce::String voiceNoteText(const int note)
{
    if (note < 0 || note > 127)
        return juce::String::fromUTF8("\xe2\x80\x94");
    const auto pitchClass = static_cast<std::size_t>(voiced::voicePitchClassOf(note));
    return juce::String::fromUTF8(ui::french() ? voiced::voiceNoteNamesFrench[pitchClass] : voiced::voiceNoteNamesEnglish[pitchClass])
           + juce::String(voiced::voiceOctaveOf(note));
}

juce::String voiceHertz(const double hz)
{
    if (hz >= 1'000.0)
        return juce::String(hz / 1'000.0, hz >= 10'000.0 ? 1 : 2) + " kHz";
    return juce::String(hz, hz < 100.0 ? 1 : 0) + " Hz";
}

// "+3 st", "-0.5 st", "0 st".
juce::String voiceSemitones(const double value)
{
    if (std::abs(value) < 0.05)
        return "0 st";
    const auto rounded = std::round(value);
    const juce::String sign(value > 0.0 ? "+" : "");
    if (std::abs(value - rounded) < 0.05)
        return sign + juce::String(static_cast<int>(rounded)) + " st";
    return sign + juce::String(value, 1) + " st";
}

// The processor's readings while they are fresh and there are enough of
// them; null otherwise.
const float* voiceReadings(const Live& live, const int needed)
{
    return live.running && live.readings != nullptr && live.readingCount >= needed ? live.readings : nullptr;
}

// ---- Vocodeur ---

// The bands on a frequency axis, each as high as the voice is in it (live),
// tinted to the side it is panned to, the gate as a dashed line; the carrier
// (a name to click) and its note above, the formant shift as an arrow. The
// first and last bands' centres drag sideways (the span), the wheel changes
// the band count, the saw's note drags up and down.
bool voiceVocoderDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                         const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    auto& state = dragState(parameters, pane);
    const auto values = studioValues<voiced::vocoderParameterCount>(parameters, pane, "vocoder");
    auto changed = false;
    const auto carrier = whole(values[voiced::vocoderCarrier], voiced::vocoderCarrierCount);
    const auto count = voiced::vocoderBandCountOf(values[voiced::vocoderBands]);
    const auto lowest = voiced::vocoderLowestOf(values[voiced::vocoderLowBand], values[voiced::vocoderHighBand]);
    const auto highest = voiced::vocoderHighestOf(values[voiced::vocoderLowBand], values[voiced::vocoderHighBand]);
    const auto stereo = std::clamp(values[voiced::vocoderStereo], 0.0f, 1.0f);
    const auto gate = static_cast<double>(values[voiced::vocoderGate]);
    const auto* readings = voiceReadings(live, voiced::vocoderReadingEnvelopes);
    const auto shown = readings != nullptr ? std::min({ count, live.readingCount - voiced::vocoderReadingEnvelopes,
                                                         static_cast<int>(readings[voiced::vocoderReadingBandCount]) })
                                           : 0;

    // The carrier, and what it plays.
    const auto top = box.min.y + dp(large ? 22.0f : 2.0f);
    const auto chooserScale = large ? 0.78f : 0.6f;
    const auto chooserText = textSize(vocoderCarrierName(carrier).toRawUTF8(), chooserScale);
    const auto chooserWidth = chooserText.x + chooserText.y * 0.5f + dp(12.0f);
    const auto chooserX = large ? box.min.x + dp(4.0f) : box.max.x - chooserWidth - dp(6.0f);
    changed |= chooser("##vocoderCarrier", at(parameters, pane, voiced::vocoderCarrier), voiced::vocoderCarrierCount, vocoderCarrierName,
                       vocoderCarrierHint, ImVec2(chooserX, top), colour, set, chooserScale);
    juce::String playing;
    if (carrier == voiced::vocoderSawCarrier) {
        playing = voiceNoteText(static_cast<int>(std::lround(values[voiced::vocoderPitch])));
    } else if (carrier == voiced::vocoderFollowCarrier) {
        const auto hz = readings != nullptr ? static_cast<double>(readings[voiced::vocoderReadingPitch]) : 0.0;
        playing = hz > 0.0 ? voiceNoteText(static_cast<int>(std::lround(voiced::voiceNoteOfHz(hz)))) + "  " + voiceHertz(hz)
                           : voiceNoteText(-1);
    } else if (carrier == voiced::vocoderExternalCarrier) {
        playing = ui::Ls("entrée sidechain", "sidechain input");
    }
    const auto noteScale = large ? 0.86f : 0.56f;
    const auto noteSize = textSize(playing.toRawUTF8(), noteScale);
    const ImVec2 notePosition(large ? box.min.x + dp(12.0f) + chooserWidth : chooserX - noteSize.x - dp(8.0f), top + dp(large ? 1.0f : 2.0f));
    if (playing.isNotEmpty()) {
        const auto size = noteSize;
        text(list, notePosition, carrier == voiced::vocoderExternalCarrier ? p.textDim : p.text, playing.toRawUTF8(), noteScale);
        auto* pitch = at(parameters, pane, voiced::vocoderPitch);
        if (carrier == voiced::vocoderSawCarrier && pitch != nullptr) {
            ImGui::SetCursorScreenPos(notePosition);
            ImGui::InvisibleButton("##vocoderNote", ImVec2(std::max(1.0f, size.x + dp(6.0f)), std::max(1.0f, size.y)));
            if (ImGui::IsItemActivated()) {
                state.active = 2;
                state.selected = static_cast<int>(std::lround(pitch->value));
            }
            if (ImGui::IsItemActive() && state.active == 2) {
                const auto steps = std::round(static_cast<double>(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y) / dp(6.0f));
                changed |= apply(pitch, static_cast<double>(state.selected) - steps, set);
            }
            if (ImGui::IsItemDeactivated())
                state.active = -1;
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                if (io.MouseWheel != 0.0f) {
                    changed |= apply(pitch, pitch->value + (io.MouseWheel > 0.0f ? 1.0 : -1.0), set);
                    ui::takeWheel();
                }
                if (!ImGui::IsItemActive())
                    ImGui::SetTooltip("%s", L("Glisser ou molette : la note du porteur", "Drag or wheel: the carrier's note"));
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                openParameterMenu(*pitch);
        }
    }
    if (large) {
        char bandsLabel[48];
        std::snprintf(bandsLabel, sizeof(bandsLabel), "%d %s", count, L("bandes", "bands"));
        const auto size = textSize(bandsLabel, 0.7f);
        text(list, ImVec2(box.max.x - size.x - dp(8.0f), top + dp(2.0f)), p.textDim, bandsLabel, 0.7f);
    }

    // The bands.
    const Box plot { ImVec2(box.min.x + dp(4.0f), top + dp(large ? 26.0f : 17.0f)),
                     ImVec2(box.max.x - dp(4.0f), box.max.y - dp(large ? 4.0f : 2.0f)) };
    if (plot.width() < dp(40.0f) || plot.height() < dp(12.0f))
        return changed;
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.45f), dp(3.0f));
    frequencyGrid(list, plot, large);
    constexpr auto floorDb = -84.0;
    const auto yOfDb = [&plot](const double db) {
        return plot.max.y - static_cast<float>(std::clamp((db - floorDb) / -floorDb, 0.0, 1.0)) * plot.height();
    };
    const auto spacing = count > 1 ? std::pow(highest / lowest, 1.0 / static_cast<double>(count - 1)) : 2.0;
    const auto half = std::sqrt(spacing);
    for (int band = 0; band < count; ++band) {
        const auto centre = voiced::vocoderBandHz(band, count, lowest, highest);
        const auto left = xOfHz(plot, centre / half) + dp(0.5f);
        const auto right = std::max(left + dp(1.0f), xOfHz(plot, centre * half) - dp(0.5f));
        const auto tint = theme::mix(colour, band % 2 == 0 ? kVoiceLeft : kVoiceRight, 0.6f * stereo);
        list->AddRectFilled(ImVec2(left, plot.min.y), ImVec2(right, plot.max.y), theme::alpha(tint, 0.07f));
        if (band < shown) {
            const auto db = static_cast<double>(readings[voiced::vocoderReadingEnvelopes + band]);
            if (db > floorDb)
                list->AddRectFilled(ImVec2(left, yOfDb(db)), ImVec2(right, plot.max.y), theme::alpha(tint, db > gate ? 0.85f : 0.25f), dp(1.0f));
        }
    }
    // The carrier's tilt, about the middle.
    const auto tone = values[voiced::vocoderTone];
    if (std::abs(tone) > 0.01f) {
        std::vector<ImVec2> tilt;
        const auto middle = (plot.min.y + plot.max.y) * 0.5f;
        for (auto x = plot.min.x; x <= plot.max.x; x += dp(4.0f))
            tilt.emplace_back(x, middle - static_cast<float>(voiced::vocoderToneDb(hzOfX(plot, x), tone) / 18.0) * plot.height() * 0.3f);
        list->AddPolyline(tilt.data(), static_cast<int>(tilt.size()), theme::alpha(p.text, 0.3f), dp(1.0f));
    }
    // The gate.
    const auto gateY = yOfDb(gate);
    dashedLine(list, ImVec2(plot.min.x, gateY), ImVec2(plot.max.x, gateY), theme::alpha(p.text, 0.35f));
    if (large) {
        char gateLabel[32];
        std::snprintf(gateLabel, sizeof(gateLabel), "%s %.0f dB", L("porte", "gate"), gate);
        const auto size = textSize(gateLabel, 0.58f);
        text(list, ImVec2(plot.max.x - size.x - dp(4.0f), gateY - size.y - dp(1.0f)), p.textFaint, gateLabel, 0.58f);
    }
    // The formant shift: from where the bands are heard to where they are played.
    const auto formant = static_cast<double>(values[voiced::vocoderFormant]);
    if (std::abs(formant) > 0.05) {
        const auto from = xOfHz(plot, 1'000.0);
        const auto to = xOfHz(plot, 1'000.0 * voiced::vocoderFormantRatioOf(values[voiced::vocoderFormant]));
        const auto y = plot.min.y + dp(large ? 12.0f : 7.0f);
        const auto arrow = theme::brighter(colour, 0.25f);
        list->AddLine(ImVec2(from, y), ImVec2(to, y), arrow, dp(2.0f));
        list->AddCircleFilled(ImVec2(from, y), dp(2.5f), arrow);
        const auto head = dp(large ? 6.0f : 4.5f);
        const auto direction = to >= from ? 1.0f : -1.0f;
        list->AddTriangleFilled(ImVec2(to, y), ImVec2(to - head * direction, y + head * 0.7f * direction),
                                ImVec2(to - head * direction, y - head * 0.7f * direction), arrow);
        const auto label = juce::String("formants ") + voiceSemitones(formant);
        const auto size = textSize(label.toRawUTF8(), large ? 0.64f : 0.54f);
        const auto labelX = std::clamp(std::min(from, to), plot.min.x + dp(2.0f), plot.max.x - size.x - dp(2.0f));
        text(list, ImVec2(labelX, y + dp(3.0f)), arrow, label.toRawUTF8(), large ? 0.64f : 0.54f);
    }
    // The span: the first and last bands' centres.
    const auto lowX = xOfHz(plot, lowest);
    const auto highX = xOfHz(plot, highest);
    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton("##vocoderBands", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    const auto hovered = ImGui::IsItemHovered();
    auto hot = -1;
    if (hovered || ImGui::IsItemActive()) {
        if (std::abs(io.MousePos.x - lowX) < dp(7.0f))
            hot = 0;
        else if (std::abs(io.MousePos.x - highX) < dp(7.0f))
            hot = 1;
    }
    if (ImGui::IsItemActivated())
        state.active = hot;
    if (ImGui::IsItemActive() && (state.active == 0 || state.active == 1))
        changed |= apply(at(parameters, pane, state.active == 0 ? voiced::vocoderLowBand : voiced::vocoderHighBand), hzOfX(plot, io.MousePos.x),
                         set);
    if (ImGui::IsItemDeactivated())
        state.active = -1;
    if (hovered && (hot >= 0 || state.active == 0 || state.active == 1))
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (hovered && io.MouseWheel != 0.0f) {
        changed |= apply(at(parameters, pane, voiced::vocoderBands), static_cast<double>(count) + (io.MouseWheel > 0.0f ? 1.0 : -1.0), set);
        ui::takeWheel();
    }
    if (hovered && !ImGui::IsItemActive() && hot < 0)
        ImGui::SetTooltip("%s", L("Glisser la première ou la dernière bande : l'étendue ; molette : le nombre de bandes",
                                  "Drag the first or last band: the span; wheel: the number of bands"));
    for (const auto edge : { 0, 1 }) {
        const auto x = edge == 0 ? lowX : highX;
        const auto isHot = state.active == edge || (state.active < 0 && hot == edge);
        list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y), theme::alpha(colour, isHot ? 0.9f : 0.45f), dp(isHot ? 2.0f : 1.0f));
        handleDot(list, ImVec2(x, plot.max.y - dp(large ? 9.0f : 6.0f)), isHot ? theme::brighter(colour, 0.25f) : colour, isHot);
        if (large || isHot) {
            const auto label = voiceHertz(edge == 0 ? lowest : highest);
            const auto size = textSize(label.toRawUTF8(), 0.6f);
            const auto labelX = edge == 0 ? x + dp(8.0f) : x - size.x - dp(8.0f);
            text(list, ImVec2(labelX, plot.max.y - dp(large ? 30.0f : 22.0f)), p.textDim, label.toRawUTF8(), 0.6f);
        }
    }
    // The sides, when they part.
    if (large && stereo > 0.05f) {
        const auto leftLabel = L("G", "L");
        const auto rightLabel = L("D", "R");
        text(list, ImVec2(plot.max.x - dp(30.0f), plot.min.y + dp(4.0f)), kVoiceLeft, leftLabel, 0.62f);
        text(list, ImVec2(plot.max.x - dp(16.0f), plot.min.y + dp(4.0f)), kVoiceRight, rightLabel, 0.62f);
    }
    return changed;
}

// ---- Dé-Esseur ---

// What the detector hears, shaded on a frequency axis over the sound's
// spectrum, and what is taken now (the filter's dip, red); the frequency to
// drag sideways. Beside it, the detector's level against the threshold (a
// line to drag up and down) and the reduction falling from the top.
bool voiceDeEsserDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                         const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    auto& state = dragState(parameters, pane);
    const auto values = studioValues<voiced::deEsserParameterCount>(parameters, pane, "de-esser");
    auto changed = false;
    const auto type = whole(values[voiced::deEsserDetectorType], voiced::deEsserDetectorCount);
    const auto mode = whole(values[voiced::deEsserMode], voiced::deEsserModeCount);
    const auto frequency = static_cast<double>(values[voiced::deEsserFrequency]);
    const auto octaves = static_cast<double>(values[voiced::deEsserBandwidth]);
    const auto threshold = static_cast<double>(values[voiced::deEsserThreshold]);
    const auto range = static_cast<double>(values[voiced::deEsserRange]);
    const auto listening = values[voiced::deEsserListen] >= 0.5f;
    const auto* readings = voiceReadings(live, voiced::deEsserReadingCount);
    const auto taken = readings != nullptr ? std::clamp(static_cast<double>(readings[voiced::deEsserReadingReduction]), 0.0, 24.0) : 0.0;
    const auto heard = readings != nullptr ? static_cast<double>(readings[voiced::deEsserReadingLevel]) : -120.0;

    // How it listens and what it takes: buttons in the window, names to
    // click on the card.
    const auto top = box.min.y + dp(large ? 22.0f : 2.0f);
    if (large) {
        changed |= segmented("##deEsserDetector", at(parameters, pane, voiced::deEsserDetectorType),
                             { L("Passe-haut", "High-pass"), L("Bande", "Band") }, ImVec2(box.min.x + dp(4.0f), top), dp(160.0f), dp(20.0f),
                             colour, set,
                             L("Ce que le détecteur écoute : tout au-dessus de la fréquence, ou une bande autour d'elle",
                               "What the detector listens to: everything above the frequency, or a band around it"));
        changed |= segmented("##deEsserMode", at(parameters, pane, voiced::deEsserMode),
                             { L("Bande seule", "Band only"), L("Tout le son", "Whole sound") }, ImVec2(box.min.x + dp(176.0f), top),
                             dp(200.0f), dp(20.0f), colour, set,
                             L("Ce qui est baissé : seulement la sifflante (une étagère ou une cloche dynamique), ou tout le son",
                               "What is taken down: the sibilance alone (a dynamic shelf or bell), or the whole sound"));
        changed |= switchCell(L("Écoute", "Listen"), at(parameters, pane, voiced::deEsserListen), ImVec2(box.min.x + dp(388.0f), top),
                              ImVec2(dp(70.0f), dp(20.0f)), kVoiceNear, set,
                              L("Fait entendre ce que le détecteur écoute, pour régler la fréquence",
                                "Plays what the detector hears, to set the frequency"));
    } else {
        const auto modeText = textSize(deEsserModeName(mode).toRawUTF8(), 0.58f);
        const auto detectorText = textSize(deEsserDetectorName(type).toRawUTF8(), 0.58f);
        const auto detectorX = box.max.x - detectorText.x - detectorText.y * 0.5f - dp(18.0f);
        const auto modeX = detectorX - modeText.x - modeText.y * 0.5f - dp(16.0f);
        changed |= chooser("##deEsserMode", at(parameters, pane, voiced::deEsserMode), voiced::deEsserModeCount, deEsserModeName,
                           deEsserModeHint, ImVec2(modeX, top), colour, set, 0.58f);
        changed |= chooser("##deEsserDetector", at(parameters, pane, voiced::deEsserDetectorType), voiced::deEsserDetectorCount,
                           deEsserDetectorName, deEsserDetectorHint, ImVec2(detectorX, top), colour, set, 0.58f);
        if (listening) {
            const auto label = L("ÉCOUTE", "LISTEN");
            const auto size = textSize(label, 0.54f);
            const ImVec2 min(modeX - size.x - dp(14.0f), top);
            list->AddRectFilled(min, ImVec2(min.x + size.x + dp(6.0f), min.y + size.y + dp(3.0f)), kVoiceNear, dp(3.0f));
            text(list, ImVec2(min.x + dp(3.0f), min.y + dp(1.5f)), kDark, label, 0.54f);
        }
    }

    const auto meterWidth = dp(large ? 64.0f : 30.0f);
    const Box plot { ImVec2(box.min.x + dp(4.0f), top + dp(large ? 28.0f : 17.0f)),
                     ImVec2(box.max.x - meterWidth - dp(8.0f), box.max.y - dp(large ? 4.0f : 2.0f)) };
    const Box meter { ImVec2(plot.max.x + dp(4.0f), plot.min.y), ImVec2(box.max.x - dp(4.0f), plot.max.y) };
    if (plot.width() < dp(40.0f) || plot.height() < dp(12.0f))
        return changed;
    list->AddRectFilled(plot.min, plot.max, theme::alpha(kDark, 0.45f), dp(3.0f));
    frequencyGrid(list, plot, large);
    spectrumBehind(list, plot, live.after, colour);
    // The shapes: 0 dB at the top, -48 dB at the foot.
    constexpr auto depthDb = 48.0;
    const auto yOfGain = [&plot](const double db) {
        return plot.min.y + static_cast<float>(std::clamp(-db / depthDb, 0.0, 1.0)) * plot.height();
    };
    std::vector<ImVec2> shape;
    for (auto x = plot.min.x; x <= plot.max.x; x += dp(2.0f))
        shape.emplace_back(x, yOfGain(voiced::deEsserDetectorDb(type, frequency, octaves, hzOfX(plot, x), kVoiceCurveRate)));
    fillUnder(list, shape, plot.max.y, theme::alpha(colour, listening ? 0.32f : 0.2f));
    list->AddPolyline(shape.data(), static_cast<int>(shape.size()), theme::alpha(colour, 0.9f), dp(1.5f));
    // What is taken now, from the top.
    if (taken > 0.05 && !listening) {
        std::vector<ImVec2> dip;
        const auto filter = voiced::deEsserDynamicFilter(type, frequency, octaves, taken, kVoiceCurveRate);
        for (auto x = plot.min.x; x <= plot.max.x; x += dp(2.0f))
            dip.emplace_back(x, yOfGain(mode == voiced::deEsserWholeMode ? -taken
                                                                          : engine::biquadMagnitudeDb(filter, hzOfX(plot, x), kVoiceCurveRate)));
        fillUnder(list, dip, plot.min.y, theme::alpha(kVoiceTaken, 0.25f));
        list->AddPolyline(dip.data(), static_cast<int>(dip.size()), kVoiceTaken, dp(1.5f));
    }
    // The frequency, to drag sideways.
    const auto frequencyX = xOfHz(plot, frequency);
    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton("##deEsserFrequency", ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    const auto plotHovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated())
        state.active = 0;
    if (ImGui::IsItemActive() && state.active == 0)
        changed |= apply(at(parameters, pane, voiced::deEsserFrequency), hzOfX(plot, io.MousePos.x), set);
    if (ImGui::IsItemDeactivated())
        state.active = -1;
    if (plotHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (io.MouseWheel != 0.0f) {
            if (type == voiced::deEsserBandDetector)
                changed |= apply(at(parameters, pane, voiced::deEsserBandwidth), octaves + (io.MouseWheel > 0.0f ? 0.1 : -0.1), set);
            else
                changed |= apply(at(parameters, pane, voiced::deEsserFrequency), frequency * std::pow(2.0, io.MouseWheel > 0.0f ? 1.0 / 12.0 : -1.0 / 12.0),
                                 set);
            ui::takeWheel();
        }
        if (!ImGui::IsItemActive())
            ImGui::SetTooltip("%s", type == voiced::deEsserBandDetector
                                        ? L("Glisser : la fréquence ; molette : la largeur de la bande", "Drag: the frequency; wheel: the band's width")
                                        : L("Glisser : la fréquence ; molette : un demi-ton", "Drag: the frequency; wheel: a semitone"));
    }
    const auto hotFrequency = plotHovered || state.active == 0;
    list->AddLine(ImVec2(frequencyX, plot.min.y), ImVec2(frequencyX, plot.max.y), theme::alpha(colour, hotFrequency ? 1.0f : 0.7f),
                  dp(hotFrequency ? 2.0f : 1.2f));
    handleDot(list, ImVec2(frequencyX, plot.min.y + dp(large ? 10.0f : 7.0f)), hotFrequency ? theme::brighter(colour, 0.25f) : colour,
              hotFrequency);
    {
        const auto label = voiceHertz(frequency);
        const auto scale = large ? 0.66f : 0.56f;
        const auto size = textSize(label.toRawUTF8(), scale);
        const auto labelX = frequencyX + dp(10.0f) + size.x < plot.max.x ? frequencyX + dp(10.0f) : frequencyX - size.x - dp(10.0f);
        text(list, ImVec2(labelX, plot.min.y + dp(large ? 4.0f : 2.0f)), p.text, label.toRawUTF8(), scale);
    }

    // The meter: the detector's level and the threshold, the reduction.
    list->AddRectFilled(meter.min, meter.max, theme::alpha(kDark, 0.55f), dp(3.0f));
    constexpr auto meterFloor = -60.0;
    const auto yOfLevel = [&meter](const double db) {
        return meter.max.y - static_cast<float>(std::clamp((db - meterFloor) / -meterFloor, 0.0, 1.0)) * meter.height();
    };
    const auto levelOfY = [&meter](const float y) {
        return meterFloor * (1.0 - static_cast<double>(std::clamp((meter.max.y - y) / std::max(1.0f, meter.height()), 0.0f, 1.0f)));
    };
    const auto middle = (meter.min.x + meter.max.x) * 0.5f;
    if (heard > meterFloor)
        list->AddRectFilled(ImVec2(meter.min.x + dp(2.0f), yOfLevel(heard)), ImVec2(middle - dp(1.0f), meter.max.y - dp(1.0f)),
                            heard > threshold ? theme::brighter(colour, 0.2f) : theme::alpha(colour, 0.55f), dp(1.0f));
    const auto yOfTaken = [&meter](const double db) {
        return meter.min.y + static_cast<float>(std::clamp(db / 24.0, 0.0, 1.0)) * meter.height();
    };
    if (taken > 0.05)
        list->AddRectFilled(ImVec2(middle + dp(1.0f), meter.min.y + dp(1.0f)), ImVec2(meter.max.x - dp(2.0f), yOfTaken(taken)), kVoiceTaken,
                            dp(1.0f));
    list->AddLine(ImVec2(middle + dp(1.0f), yOfTaken(range)), ImVec2(meter.max.x - dp(1.0f), yOfTaken(range)), theme::alpha(kVoiceTaken, 0.6f),
                  dp(1.0f));
    ImGui::SetCursorScreenPos(meter.min);
    ImGui::InvisibleButton("##deEsserThreshold", ImVec2(std::max(1.0f, meter.width()), std::max(1.0f, meter.height())));
    const auto meterHovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated())
        state.active = 1;
    if (ImGui::IsItemActive() && state.active == 1)
        changed |= apply(at(parameters, pane, voiced::deEsserThreshold), levelOfY(io.MousePos.y), set);
    if (ImGui::IsItemDeactivated())
        state.active = -1;
    if (meterHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (!ImGui::IsItemActive())
            ImGui::SetTooltip("%s", L("Glisser : le seuil (à gauche, ce que le détecteur entend ; à droite, en rouge, ce qui est retiré)",
                                      "Drag: the threshold (left, what the detector hears; right, in red, what is taken)"));
    }
    if (auto* parameter = at(parameters, pane, voiced::deEsserThreshold); parameter != nullptr && meterHovered
                                                                            && ImGui::IsItemClicked(ImGuiMouseButton_Right))
        openParameterMenu(*parameter);
    const auto thresholdY = yOfLevel(threshold);
    const auto hotThreshold = meterHovered || state.active == 1;
    list->AddLine(ImVec2(meter.min.x, thresholdY), ImVec2(middle, thresholdY), p.text, dp(hotThreshold ? 2.5f : 1.5f));
    dashedLine(list, ImVec2(plot.min.x, thresholdY), ImVec2(plot.max.x, thresholdY), theme::alpha(p.text, hotThreshold ? 0.3f : 0.0f));
    if (large) {
        char label[24];
        std::snprintf(label, sizeof(label), "%.0f dB", threshold);
        text(list, ImVec2(meter.min.x + dp(3.0f), thresholdY + dp(2.0f)), p.text, label, 0.56f);
        std::snprintf(label, sizeof(label), "-%.1f", taken);
        const auto size = textSize(label, 0.62f);
        text(list, ImVec2(meter.max.x - size.x - dp(3.0f), meter.max.y - size.y - dp(2.0f)), kVoiceTaken, label, 0.62f);
    }
    return changed;
}

// ---- Accordeur ---

// A strobe's bands keep moving between frames: their phase, by device.
double voiceStrobePhase(std::vector<Param>& parameters, const Pane& pane, const double cents, const bool moving)
{
    struct Strobe {
        double phase {};
        double last {};
    };
    static std::map<juce::String, Strobe> strobes;
    const auto* first = at(parameters, pane, 0);
    auto& strobe = strobes[first != nullptr ? first->binding : juce::String("?")];
    const auto now = ImGui::GetTime();
    const auto elapsed = std::clamp(now - strobe.last, 0.0, 0.1);
    strobe.last = now;
    // A cent off moves them 4 dp a second, sharp to the right.
    if (moving)
        strobe.phase = std::fmod(strobe.phase + cents * static_cast<double>(dp(4.0f)) * elapsed, 1.0e6);
    return strobe.phase;
}

// The note big in the middle; how far off it is on a needle over ±50 cents
// (green within 3) or as a strobe's bands, still when in tune; the
// frequency under it; a dash when there is no pitch. The reference, the
// mute and the display above.
bool voiceTunerDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                       const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto values = studioValues<voiced::tunerParameterCount>(parameters, pane, "tuner");
    auto changed = false;
    const auto reference = static_cast<double>(values[voiced::tunerReference]);
    const auto strobe = whole(values[voiced::tunerDisplayMode], voiced::tunerDisplayCount) == 1;
    const auto muted = values[voiced::tunerMute] >= 0.5f;
    const auto* readings = voiceReadings(live, voiced::tunerReadingCount);
    const auto hz = readings != nullptr ? static_cast<double>(readings[voiced::tunerReadingHz]) : 0.0;
    const auto has = hz > 0.0;
    const auto note = has ? static_cast<int>(std::lround(readings[voiced::tunerReadingNote])) : -1;
    const auto cents = has ? std::clamp(static_cast<double>(readings[voiced::tunerReadingCents]), -50.0, 50.0) : 0.0;
    const auto inTune = has && std::abs(cents) <= voiced::tunerInTuneCents;
    const auto tint = !has ? p.textFaint : inTune ? kVoiceInTune : std::abs(cents) < 15.0 ? kVoiceNear : kVoiceTaken;

    // Above: the reference, the mute, the display.
    const auto top = box.min.y + dp(large ? 22.0f : 2.0f);
    const auto rowHeight = dp(large ? 20.0f : 15.0f);
    const auto labelScale = large ? 0.66f : 0.54f;
    const auto buttonsWidth = dp(large ? 170.0f : 96.0f);
    changed |= segmented("##tunerDisplay", at(parameters, pane, voiced::tunerDisplayMode), { L("Aiguille", "Needle"), L("Strobo", "Strobe") },
                         ImVec2(box.max.x - buttonsWidth - dp(4.0f), top), buttonsWidth, rowHeight, colour, set,
                         L("L'aiguille sur ±50 cents, ou le stroboscope : ses bandes défilent tant que la note est fausse",
                           "The needle over ±50 cents, or the strobe: its bands scroll while the note is off"));
    if (auto* mute = at(parameters, pane, voiced::tunerMute); mute != nullptr) {
        const auto label = L("MUET", "MUTE");
        const auto size = textSize(label, labelScale);
        const ImVec2 min(box.max.x - buttonsWidth - dp(12.0f) - size.x - dp(10.0f), top);
        const ImVec2 max(min.x + size.x + dp(10.0f), top + rowHeight);
        ImGui::SetCursorScreenPos(min);
        if (ImGui::InvisibleButton("##tunerMute", ImVec2(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y))))
            changed |= apply(mute, muted ? 0.0 : 1.0, set);
        const auto hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetTooltip("%s", L("Coupe le son pour s'accorder en silence (l'accordeur écoute toujours)",
                                      "Silences the sound to tune quietly (the tuner still listens)"));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            openParameterMenu(*mute);
        list->AddRectFilled(min, max, muted ? kVoiceTaken : hovered ? p.bg4 : p.bg3, dp(3.0f));
        centred(list, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), muted ? kDark : p.textDim, label, labelScale);
    }
    {
        const auto label = juce::String(L("La4 = ", "A4 = ")) + juce::String(reference, std::abs(reference - std::round(reference)) < 0.05 ? 0 : 1)
                           + " Hz";
        text(list, ImVec2(box.min.x + dp(6.0f), top + (rowHeight - textSize("A", labelScale).y) * 0.5f), p.textDim, label.toRawUTF8(),
             labelScale);
    }

    const Box area { ImVec2(box.min.x + dp(4.0f), top + rowHeight + dp(4.0f)), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(large ? 6.0f : 3.0f)) };
    if (area.width() < dp(40.0f) || area.height() < dp(24.0f))
        return changed;
    const auto centreX = (area.min.x + area.max.x) * 0.5f;
    char detail[64];
    if (has)
        std::snprintf(detail, sizeof(detail), "%.1f Hz   %+.1f ct", hz, cents);
    else
        std::snprintf(detail, sizeof(detail), "%s", L("jouez une note", "play a note"));
    const auto noteText = voiceNoteText(note);
    const auto detailScale = large ? 0.8f : 0.56f;

    if (!strobe) {
        // The needle: an arc of ±60 degrees, the note inside it.
        const auto radius = std::max(dp(10.0f), std::min(area.width() * 0.46f, area.height() * 0.95f));
        const ImVec2 pivot(centreX, area.max.y - dp(2.0f));
        const auto angleOf = [](const double c) { return static_cast<float>((-90.0 + std::clamp(c, -50.0, 50.0) / 50.0 * 60.0) * std::numbers::pi / 180.0); };
        const auto pointAt = [&pivot](const float angle, const float r) {
            return ImVec2(pivot.x + std::cos(angle) * r, pivot.y + std::sin(angle) * r);
        };
        std::vector<ImVec2> arc;
        for (auto c = -50.0; c <= 50.01; c += 2.0)
            arc.push_back(pointAt(angleOf(c), radius));
        list->AddPolyline(arc.data(), static_cast<int>(arc.size()), p.gridStrong, dp(1.5f));
        std::vector<ImVec2> zone;
        for (auto c = -voiced::tunerInTuneCents; c <= voiced::tunerInTuneCents + 0.01; c += 0.5)
            zone.push_back(pointAt(angleOf(c), radius - dp(3.0f)));
        list->AddPolyline(zone.data(), static_cast<int>(zone.size()), theme::alpha(kVoiceInTune, inTune ? 1.0f : 0.55f), dp(large ? 7.0f : 5.0f));
        for (auto c = -50; c <= 50; c += 10) {
            const auto major = c == 0 || c == 50 || c == -50;
            const auto angle = angleOf(static_cast<double>(c));
            list->AddLine(pointAt(angle, radius * (major ? 0.86f : 0.92f)), pointAt(angle, radius), major ? p.textDim : p.grid,
                          dp(major ? 1.6f : 1.0f));
            if (large && major) {
                char mark[8];
                std::snprintf(mark, sizeof(mark), "%+d", c);
                centred(list, pointAt(angle, radius + dp(10.0f)), p.textFaint, c == 0 ? "0" : mark, 0.56f);
            }
        }
        if (has) {
            const auto angle = angleOf(cents);
            list->AddLine(pointAt(angle, radius * 0.52f), pointAt(angle, radius * 1.03f), tint, dp(large ? 3.0f : 2.0f));
            list->AddCircleFilled(pointAt(angle, radius * 0.52f), dp(large ? 3.5f : 2.5f), tint);
        }
        const auto noteScale = std::clamp(radius * 0.3f / std::max(1.0f, ImGui::GetFontSize()), 0.9f, 4.0f);
        centred(list, ImVec2(centreX, pivot.y - radius * 0.3f), has ? (inTune ? kVoiceInTune : p.text) : p.textFaint, noteText.toRawUTF8(),
                noteScale);
        centred(list, ImVec2(centreX, pivot.y - radius * 0.1f), p.textDim, detail, detailScale);
        return changed;
    }

    // The strobe: three rows of bands, each twice as fine as the one above
    // and twice as fast, scrolling while the note is off.
    const auto noteScale = std::clamp(area.height() * 0.3f / std::max(1.0f, ImGui::GetFontSize()), 0.9f, 3.6f);
    const auto noteY = area.min.y + area.height() * 0.24f;
    centred(list, ImVec2(centreX, noteY), has ? (inTune ? kVoiceInTune : p.text) : p.textFaint, noteText.toRawUTF8(), noteScale);
    centred(list, ImVec2(centreX, area.min.y + area.height() * 0.5f), p.textDim, detail, detailScale);
    const Box strip { ImVec2(area.min.x + dp(6.0f), area.min.y + area.height() * 0.6f), ImVec2(area.max.x - dp(6.0f), area.max.y - dp(2.0f)) };
    if (strip.height() < dp(6.0f))
        return changed;
    list->AddRectFilled(strip.min, strip.max, theme::alpha(kDark, 0.6f), dp(3.0f));
    const auto phase = voiceStrobePhase(parameters, pane, cents, has && !inTune);
    list->PushClipRect(strip.min, strip.max, true);
    const auto rowHeightStrip = strip.height() / 3.0f;
    const auto stripe = has ? (inTune ? kVoiceInTune : colour) : theme::alpha(p.textFaint, 0.5f);
    for (int row = 0; row < 3; ++row) {
        const auto period = static_cast<double>(dp(large ? 48.0f : 26.0f)) / static_cast<double>(1 << row);
        const auto offset = static_cast<float>(std::fmod(phase * static_cast<double>(1 << row), period));
        const auto rowTop = strip.min.y + rowHeightStrip * static_cast<float>(row) + dp(1.0f);
        const auto rowBottom = rowTop + rowHeightStrip - dp(2.0f);
        for (auto x = strip.min.x - static_cast<float>(period) + offset; x < strip.max.x; x += static_cast<float>(period))
            list->AddRectFilled(ImVec2(x, rowTop), ImVec2(x + static_cast<float>(period * 0.5), rowBottom),
                                theme::alpha(stripe, 0.9f - 0.2f * static_cast<float>(row)));
    }
    list->PopClipRect();
    // The middle, for the eye.
    list->AddLine(ImVec2(centreX, strip.min.y), ImVec2(centreX, strip.max.y), theme::alpha(p.text, 0.25f), dp(1.0f));
    return changed;
}

// A card's display (large: the window's), true when a value changed.
bool voiceDisplay(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour,
                    const Live& live, const Setter& set, const bool large)
{
    if (kind == PaneKind::vocoder)
        return voiceVocoderDisplay(parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::deEsser)
        return voiceDeEsserDisplay(parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::tuner)
        return voiceTunerDisplay(parameters, pane, box, colour, live, set, large);
    return false;
}

// The window: its display large and every control.
bool voiceLarge(const PaneKind kind, const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size,
                  const ImU32 colour, const Live& live, const Setter& set)
{
    const auto display = [&](const Box& box) { return voiceDisplay(kind, parameters, pane, box, colour, live, set, true); };
    if (kind == PaneKind::vocoder)
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("VOCODEUR  ·  la voix qui fait parler un son", "VOCODER  ·  the voice that makes a sound speak"),
                          { { L("PORTEUR", "CARRIER"),
                              { voiced::vocoderCarrier, voiced::vocoderPitch, voiced::vocoderDetune, voiced::vocoderTone, voiced::vocoderUnvoiced } },
                            { L("BANDES", "BANDS"),
                              { voiced::vocoderBands, voiced::vocoderLowBand, voiced::vocoderHighBand, voiced::vocoderBandwidth,
                                voiced::vocoderFormant } },
                            { L("ENVELOPPE", "ENVELOPE"), { voiced::vocoderAttack, voiced::vocoderRelease, voiced::vocoderGate } },
                            { L("SORTIE", "OUTPUT"), { voiced::vocoderStereo, voiced::vocoderOutput, voiced::vocoderMix } } },
                          display, 220.0f);
    if (kind == PaneKind::deEsser)
        // The detector and the mode are buttons in the display.
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("DÉ-ESSEUR  ·  les sifflantes adoucies", "DE-ESSER  ·  the hiss of the S taken down"),
                          { { L("SEUIL", "THRESHOLD"), { voiced::deEsserThreshold, voiced::deEsserFrequency, voiced::deEsserRange } },
                            { L("DÉTECTION", "DETECTION"),
                              { voiced::deEsserListen, voiced::deEsserBandwidth, voiced::deEsserAttack, voiced::deEsserRelease,
                                voiced::deEsserLink } },
                            { L("SORTIE", "OUTPUT"), { voiced::deEsserOutput } } },
                          display, 220.0f);
    if (kind == PaneKind::tuner)
        // The display's choice is the buttons in its corner.
        return addedLarge(pane, parameters, position, size, colour, live, set,
                          L("ACCORDEUR  ·  la note et son écart", "TUNER  ·  the note and how far off it is"),
                          { { L("ACCORDAGE", "TUNING"), { voiced::tunerReference, voiced::tunerMute } } }, display, 260.0f);
    return false;
}

// The values a card shows as knobs (pane indices).
std::vector<int> voiceCardFields(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane)
{
    juce::ignoreUnused(parameters, pane);
    if (kind == PaneKind::vocoder)
        return { voiced::vocoderMix, voiced::vocoderFormant, voiced::vocoderRelease, voiced::vocoderUnvoiced };
    if (kind == PaneKind::deEsser)
        return { voiced::deEsserThreshold, voiced::deEsserFrequency, voiced::deEsserRange };
    if (kind == PaneKind::tuner)
        return { voiced::tunerReference };
    return {};
}

// A value as these effects show it; empty for none of theirs.
juce::String voiceFormat(const juce::String& binding, const double value)
{
    const auto is = [&binding](const char* suffix) { return binding.endsWith(suffix); };
    const auto onOff = [value] { return value >= 0.5 ? ui::Ls("Actif", "On") : ui::Ls("Coupé", "Off"); };
    if (is("vocoder.carrier"))
        return vocoderCarrierName(whole(static_cast<float>(value), voiced::vocoderCarrierCount));
    if (is("vocoder.pitch"))
        return voiceNoteText(static_cast<int>(std::lround(value)));
    if (is("vocoder.formant"))
        return voiceSemitones(value);
    if (is("vocoder.bands"))
        return juce::String(static_cast<int>(std::lround(value)));
    if (is("vocoder.detune"))
        return value < 0.005 ? ui::Ls("Aucun", "None")
                             : juce::String::fromUTF8("\xc2\xb1") + juce::String(voiced::vocoderDetuneCentsOf(static_cast<float>(value)), 1) + " ct";
    if (is("vocoder.tone")) {
        if (std::abs(value) < 0.005)
            return ui::Ls("Neutre", "Neutral");
        return (value < 0.0 ? ui::Ls("Sombre ", "Dark ") : ui::Ls("Clair ", "Bright ")) + percent(std::abs(value));
    }
    if (is("vocoder.mix") || is("vocoder.unvoiced") || is("vocoder.stereo") || is("deesser.link"))
        return percent(value);
    if (is("vocoder.bandwidth"))
        return juce::String::fromUTF8("\xc3\x97") + juce::String(value, 2);
    if (is("vocoder.gate"))
        return value <= -89.5 ? ui::Ls("Coupée", "Off") : juce::String(juce::roundToInt(value)) + " dB";
    if (is("deesser.mode"))
        return deEsserModeName(whole(static_cast<float>(value), voiced::deEsserModeCount));
    if (is("deesser.detectorType"))
        return deEsserDetectorName(whole(static_cast<float>(value), voiced::deEsserDetectorCount));
    if (is("deesser.listen") || is("tuner.mute"))
        return onOff();
    if (is("deesser.bandwidth"))
        return juce::String(value, 2) + " oct";
    if (is("tuner.displayMode"))
        return tunerDisplayName(whole(static_cast<float>(value), voiced::tunerDisplayCount));
    if (is("tuner.reference"))
        return juce::String(value, std::abs(value - std::round(value)) < 0.05 ? 0 : 1) + " Hz";
    return {};
}

// A parameter of theirs that takes whole steps (a choice, a switch).
bool voiceStepped(const juce::String& binding)
{
    for (const auto* suffix : { "vocoder.carrier", "vocoder.bands", "vocoder.pitch", "deesser.mode", "deesser.detectorType",
                                "deesser.listen", "tuner.mute", "tuner.displayMode" })
        if (binding.endsWith(suffix))
            return true;
    return false;
}
