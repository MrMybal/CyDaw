// Shared by the Foundry timeline and its visible details pane.
void FoundryView::finishControlGesture(Tab &tab)
{
    if (!tab.controlBefore)
        return;
    if (tab.controlChanged)
    {
        auto next = tab.document->session();
        tab.document->replace(std::move(*tab.controlBefore));
        edit(tab, std::move(next), ui::Ls("Édition", "Edit"));
    }
    tab.controlBefore.reset();
    tab.controlChanged = false;
}
void FoundryView::trackMenu(Tab &tab)
{
    if (ImGui::MenuItem(L("Piste audio", "Audio track")))
        addTrack(tab, false);
    if (ImGui::MenuItem(L("Piste MIDI / instrument", "MIDI / instrument track")))
        addTrack(tab, true);
}
void FoundryView::createMidiClip(Tab &tab, const int track, const std::int64_t at)
{
    auto next = tab.document->session();
    if (track < 0 || track >= static_cast<int>(next.tracks.size()) || !next.tracks[track].midi)
        return;
    Clip clip;
    clip.id = next.newId();
    clip.start = at;
    clip.length = static_cast<std::int64_t>(next.rate * 60 / next.tempo * 4);
    clip.name = "MIDI";
    tab.clip = clip.id;
    tab.note = 0;
    next.tracks[track].clips.push_back(std::move(clip));
    edit(tab, std::move(next), ui::Ls("Créer un clip MIDI", "Create MIDI clip"));
    pickTrack(tab, track, false);
    tab.detailsFocus = true;
    showMidiEditor(tab);
}
void FoundryView::reverseClip(Tab &tab)
{
    auto next = tab.document->session();
    int t{}, c{};
    if (!next.findClip(tab.clip, t, c))
        return;
    auto &clip = next.tracks[t].clips[c];
    if (next.tracks[t].midi)
    {
        for (auto &note : clip.notes)
            note.start = 2 * clip.offset + clip.length - note.start - note.length;
    }
    else if (clip.sound)
    {
        clip.sound = std::make_shared<const Sound>(engine::foundry::reversed(*clip.sound, clip.offset, clip.offset + clip.length, 0xffu));
        clip.edited = true;
    }
    std::swap(clip.fadeIn, clip.fadeOut);
    edit(tab, std::move(next), ui::Ls("Inverser le clip", "Reverse clip"));
}
void FoundryView::drawAutomation(Tab &tab, const int track, const ImVec2 min, const ImVec2 max)
{
    if (max.y <= min.y || max.x <= min.x)
        return;
    const auto snapshot = tab.document->shared();
    const auto id = track >= 0 ? snapshot->tracks[track].id : 0;
    ImGui::PushID(static_cast<int>(id));
    ImGui::PushID("automation");
    auto parameters = tab.processing->parameters(id);
    auto &binding = tab.shownAutomation[id];
    auto param = parameters.front();
    for (const auto &p : parameters)
        if (p.binding == binding)
            param = p;
    auto *draw = ImGui::GetWindowDrawList();
    const auto &palette = theme::palette();
    draw->AddRectFilled(min, max, palette.bg1);
    draw->AddLine(min, ImVec2(max.x, min.y), palette.line);
    ImGui::SetCursorScreenPos(ImVec2(min.x + dp(3), min.y + dp(2)));
    ImGui::SetNextItemWidth(std::min(dp(250), max.x - min.x - dp(8)));
    if (ImGui::BeginCombo("##lane", param.label.c_str(), ImGuiComboFlags_HeightLarge))
    {
        for (const auto &p : parameters)
            if (ImGui::Selectable(p.label.c_str(), p.binding == binding))
                binding = p.binding;
        ImGui::EndCombo();
    }
    const float top = min.y + ImGui::GetFrameHeight() + dp(7), bottom = max.y - dp(5);
    if (bottom <= top)
    {
        ImGui::PopID();
        ImGui::PopID();
        return;
    }
    const auto &lanes = track < 0 ? snapshot->masterAutomation : snapshot->tracks[track].automation;
    const auto found = std::find_if(lanes.begin(), lanes.end(), [&](const auto &l) { return l.binding == binding; });
    const auto xOf = [&](double frame) { return lanesMin_.x + static_cast<float>((frame - tab.viewStart) / tab.framesPerPixel); };
    const auto yOf = [&](float value) {
        return bottom - std::clamp((value - param.minimum) / std::max(.000001f, param.maximum - param.minimum), 0.f, 1.f) * (bottom - top);
    };
    const auto frameAt = [&](float x) {
        return std::max<std::int64_t>(0, static_cast<std::int64_t>(tab.viewStart + (x - lanesMin_.x) * tab.framesPerPixel));
    };
    const auto valueAt = [&](float y) {
        return param.minimum + std::clamp((bottom - y) / (bottom - top), 0.f, 1.f) * (param.maximum - param.minimum);
    };
    draw->PushClipRect(ImVec2(min.x, top), max, true);
    if (found != lanes.end() && !found->points.empty())
    {
        auto previous = ImVec2(min.x, yOf(found->points.front().value));
        for (const auto &point : found->points)
        {
            ImVec2 next(xOf(point.frame), yOf(point.value));
            draw->AddLine(previous, next, palette.accent, dp(1.5f));
            previous = next;
        }
        draw->AddLine(previous, ImVec2(max.x, previous.y), palette.accent, dp(1.5f));
    }
    else
        draw->AddLine(ImVec2(min.x, yOf(param.value)), ImVec2(max.x, yOf(param.value)), palette.textFaint);
    draw->PopClipRect();
    const auto change = [&](auto &&fn, bool undo) {
        auto next = tab.document->session();
        auto &all = track < 0 ? next.masterAutomation : next.tracks[track].automation;
        auto lane = std::find_if(all.begin(), all.end(), [&](const auto &l) { return l.binding == binding; });
        if (lane == all.end())
        {
            all.push_back({binding, {}});
            lane = std::prev(all.end());
        }
        fn(lane->points);
        std::stable_sort(lane->points.begin(), lane->points.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
        if (undo)
            edit(tab, std::move(next), ui::Ls("Automation", "Automation"));
        else
        {
            tab.document->replace(std::move(next));
            if (playingTab_ == current_)
                this->player().setSession(tab.document->shared());
        }
    };
    // Points own their gestures; double click the background adds one.
    bool overPoint = false;
    if (found != lanes.end())
        for (std::size_t i = 0; i < found->points.size(); ++i)
        {
            const auto &point = found->points[i];
            const auto x = xOf(point.frame), y = yOf(point.value);
            if (x < min.x || x > max.x)
                continue;
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetCursorScreenPos(ImVec2(x - dp(5), y - dp(5)));
            ImGui::InvisibleButton("##point", ImVec2(dp(10), dp(10)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            overPoint = overPoint || ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (ImGui::IsItemActivated())
            {
                tab.controlBefore = *snapshot;
                tab.controlChanged = false;
            }
            draw->AddCircleFilled(ImVec2(x, y), dp(3.5f), palette.accent);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                change([i](auto &points) { points.erase(points.begin() + i); }, true);
                ImGui::PopID();
                break;
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
            {
                // Keep index stable until mouse release; adjacent points bound its time.
                const auto frame = frameAt(ImGui::GetIO().MousePos.x);
                const auto value = valueAt(ImGui::GetIO().MousePos.y);
                tab.controlChanged = true;
                change(
                    [&](auto &points) {
                        const auto lo = i ? points[i - 1].frame + 1 : 0;
                        const auto hi = i + 1 < points.size() ? points[i + 1].frame - 1 : std::numeric_limits<std::int64_t>::max();
                        points[i] = {std::clamp(frame, lo, std::max(lo, hi)), value};
                    },
                    false);
            }
            if (ImGui::IsItemDeactivated())
                finishControlGesture(tab);
            ui::tooltip((juce::String(point.value, 3) + " @ " + juce::String(point.frame / snapshot->rate, 3) + " s").toRawUTF8());
            ImGui::PopID();
        }
    if (!overPoint && ImGui::IsMouseHoveringRect(ImVec2(min.x, top), ImVec2(max.x, bottom)) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseDoubleClicked(0))
    {
        const auto frame = frameAt(ImGui::GetIO().MousePos.x);
        const auto value = valueAt(ImGui::GetIO().MousePos.y);
        change(
            [&](auto &points) {
                auto it = std::find_if(points.begin(), points.end(), [&](const auto &p) { return p.frame == frame; });
                if (it == points.end())
                    points.push_back({frame, value});
                else
                    it->value = value;
            },
            true);
    }
    ImGui::PopID();
    ImGui::PopID();
}
void FoundryView::drawMidiEditor(Tab &tab)
{
    const auto snapshot = tab.document->shared();
    int t{}, c{};
    if (!snapshot->findClip(tab.clip, t, c) || !snapshot->tracks[t].midi)
    {
        drawClipInspector(tab);
        return;
    }
    const auto clip = snapshot->tracks[t].clips[c];
    const auto &palette = theme::palette();
    ImGui::Text("MIDI : %s", clip.name.c_str());
    ImGui::SameLine();
    if (ImGui::Button(L("Instrument…", "Instrument…")))
        tab.processing->chooseInstrument(snapshot->tracks[t].id);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(85));
    ImGui::SliderInt(L("Octave", "Octave"), &tab.midiOctave, 0, 9);
    ImGui::SameLine();
    const char *grids[] = {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"};
    int grid = std::clamp(tab.midiGrid, 0, 5);
    ImGui::SetNextItemWidth(dp(80));
    if (ImGui::Combo(L("Grille", "Grid"), &grid, grids, 6))
        tab.midiGrid = grid;
    ImGui::TextDisabled("%s", L("Double-clic : ajouter une note · glisser : déplacer · bord droit : durée · clic droit : supprimer",
                                "Double click: add note · drag: move · right edge: length · right click: delete"));
    const auto area = ImGui::GetContentRegionAvail();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto width = std::max(dp(100), area.x - dp(42));
    const auto height = std::max(dp(70), area.y);
    const float keyWidth = dp(42), row = height / 12;
    const auto base = std::clamp((tab.midiOctave + 1) * 12, 0, 116);
    const auto left = origin.x + keyWidth;
    const auto right = left + width;
    const auto framesPerBeat = snapshot->rate * 60 / snapshot->tempo;
    const auto step = std::max<std::int64_t>(1, static_cast<std::int64_t>(framesPerBeat * 4 / (1 << grid)));
    const auto snap = [&](double f) { return std::max<std::int64_t>(0, static_cast<std::int64_t>(std::round(f / step)) * step); };
    const auto xOf = [&](std::int64_t frame) {
        return left + static_cast<float>((frame - clip.offset) / static_cast<double>(clip.length)) * width;
    };
    const auto frameAt = [&](float x) { return snap(clip.offset + (x - left) / width * clip.length); };
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(right, origin.y + height), palette.bg0);
    for (int k = 0; k < 12; ++k)
    {
        const auto key = base + 11 - k;
        const auto y = origin.y + k * row;
        const auto pc = key % 12;
        const auto black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        draw->AddRectFilled(ImVec2(origin.x, y), ImVec2(left, y + row), black ? palette.bg0 : palette.bg4);
        draw->AddLine(ImVec2(left, y), ImVec2(right, y), palette.grid);
        if (pc == 0)
            draw->AddText(ImVec2(origin.x + dp(2), y), palette.text, ("C" + juce::String(key / 12 - 1)).toRawUTF8());
    }
    for (std::int64_t f = clip.offset; f <= clip.offset + clip.length; f += step)
        draw->AddLine(ImVec2(xOf(f), origin.y), ImVec2(xOf(f), origin.y + height), palette.grid);
    const auto update = [&](Clip next, bool undo) {
        auto session = tab.document->session();
        session.tracks[t].clips[c] = std::move(next);
        if (undo)
            edit(tab, std::move(session), ui::Ls("Notes MIDI", "MIDI notes"));
        else
        {
            tab.document->replace(std::move(session));
            if (playingTab_ == current_)
                this->player().setSession(tab.document->shared());
        }
    };
    bool over = false;
    for (std::size_t i = 0; i < clip.notes.size(); ++i)
    {
        const auto &n = clip.notes[i];
        if (n.key < base || n.key >= base + 12)
            continue;
        const auto x0 = std::max(left, xOf(n.start)), x1 = std::min(right, xOf(n.start + n.length));
        if (x1 <= x0)
            continue;
        const auto y = origin.y + (base + 11 - n.key) * row;
        ImGui::PushID(static_cast<int>(n.id));
        ImGui::SetCursorScreenPos(ImVec2(x0, y + 1));
        ImGui::InvisibleButton("##note", ImVec2(std::max(2.f, x1 - x0), std::max(2.f, row - 2)),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        draw->AddRectFilled(ImVec2(x0, y + 1), ImVec2(x1, y + row - 1), tab.note == n.id ? palette.accent : palette.play, dp(2));
        over = over || ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (ImGui::IsItemActivated())
        {
            tab.controlBefore = *snapshot;
            tab.controlChanged = false;
            tab.noteOriginal = n;
            tab.noteResize = ImGui::GetIO().MousePos.x > x1 - dp(9);
            tab.noteGrab = frameAt(ImGui::GetIO().MousePos.x) - n.start;
        }
        if (ImGui::IsItemClicked(0))
            tab.note = n.id;
        if (ImGui::IsItemClicked(1))
        {
            auto next = clip;
            next.notes.erase(next.notes.begin() + i);
            update(std::move(next), true);
            ImGui::PopID();
            break;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
        {
            auto next = clip;
            auto &note = next.notes[i];
            const bool resize = tab.noteResize;
            tab.controlChanged = true;
            if (resize)
                note.length = std::max<std::int64_t>(step, frameAt(ImGui::GetIO().MousePos.x) - note.start);
            else
            {
                note.start =
                    std::clamp<std::int64_t>(frameAt(ImGui::GetIO().MousePos.x) - tab.noteGrab, clip.offset, clip.offset + clip.length - 1);
                note.key = std::clamp(base + 11 - static_cast<int>((ImGui::GetIO().MousePos.y - origin.y) / row), 0, 127);
            }
            update(std::move(next), false);
        }
        if (ImGui::IsItemDeactivated())
            finishControlGesture(tab);
        ImGui::PopID();
    }
    if (!over && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(left, origin.y), ImVec2(right, origin.y + height)) &&
        ImGui::IsMouseDoubleClicked(0))
    {
        auto next = clip;
        engine::foundry::Note note;
        auto session = tab.document->session();
        note.id = session.newId();
        note.start = std::min(frameAt(ImGui::GetIO().MousePos.x), clip.offset + clip.length - 1);
        note.length = std::min(step, clip.offset + clip.length - note.start);
        note.key = std::clamp(base + 11 - static_cast<int>((ImGui::GetIO().MousePos.y - origin.y) / row), 0, 127);
        next.notes.push_back(note);
        tab.note = note.id;
        session.tracks[t].clips[c] = std::move(next);
        edit(tab, std::move(session), ui::Ls("Ajouter une note", "Add note"));
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
    ImGui::Dummy(ImVec2(width + keyWidth, 0));
}
