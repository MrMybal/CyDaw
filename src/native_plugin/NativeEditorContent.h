#pragma once
#include "InstrumentPanels.h"
#include "NativeProcessor.h"
#include "DeviceVisuals.h"
#include "Theme.h"
#include "Widgets.h"
#include <imgui.h>
#include <set>
namespace cydaw::native {
namespace visual = cydaw::v2::visual;
namespace instrument_ui = cydaw::v2::instrument_ui;
namespace ui = cydaw::v2::ui;
namespace theme = cydaw::v2::theme;
class EditorContent {
protected:
    EditorModel &plugin_;
    ImGuiContext *context_{};
    std::string page_;
    struct Display
    {
        cydaw::v2::Spectrum before, after;
        std::uint64_t sequence{};
        double changed{};
    };
    std::map<std::uint64_t, Display> displays_;
    engine::AudioAnalysisFrame outputFrame_;
    visual::Live liveFor(const ControlDefinition &c, engine::AudioAnalysisFrame &before,
                         engine::AudioAnalysisFrame &after)
    {
        std::uint64_t node = 0;
        if (!c.extra.empty())
            if (const auto *p = parameter(c.extra[0]))
                node = p->node;
        plugin_.analysis(node, true, before);
        plugin_.analysis(node, false, after);
        auto &display = displays_[c.id];
        const auto now = ImGui::GetTime();
        if (display.sequence != after.sequence)
        {
            display.sequence = after.sequence;
            display.changed = now;
        }
        display.before.update(&before, now);
        display.after.update(&after, now);
        visual::Live live;
        live.before = &display.before;
        live.after = &display.after;
        live.beforeFrame = &before;
        live.afterFrame = &after;
        live.running = after.sequence > 0 && now - display.changed < .25;
        live.inputDb = cydaw::v2::recentRmsDb(before);
        live.outputDb = cydaw::v2::recentRmsDb(after);
        return live;
    }
    const ParameterDefinition *parameter(const std::string &key) const
    {
        for (const auto &p : plugin_.definition().parameters)
            if (p.key == key)
                return &p;
        return nullptr;
    }
    void control(const ControlDefinition &c, ImVec2 origin, float sx, float sy)
    {
        if (c.hidden || (!c.page.empty() && c.page != page_) || c.kind == "input-port" ||
            c.kind == "output-port")
            return;
        const ImVec2 at(origin.x + c.x * sx, origin.y + c.y * sy),
            size(std::max(1.0f, c.width * sx), std::max(1.0f, c.height * sy));
        const auto cursor = ImGui::GetCursorScreenPos();
        auto *list = ImGui::GetWindowDrawList();
        ImGui::PushID(static_cast<int>(c.id));
        ImGui::SetCursorScreenPos(at);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::fromArgb(c.text));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::fromArgb(c.background));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, theme::fromArgb(c.accent));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, c.rounding);
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * c.fontScale);
        const auto *p = parameter(c.primary);
        const auto colour = theme::fromArgb(c.accent);
        list->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), theme::fromArgb(c.background),
                            c.rounding);
        if (c.showValue && p)
        {
            char text[96];
            std::snprintf(text, sizeof(text), "%.3f %s", plugin_.value(p->key.c_str()), p->unit.c_str());
            list->AddText(ImVec2(at.x + 4, at.y + 2), theme::fromArgb(c.text), text);
        }
        if (c.kind == "text")
            list->AddText(at, theme::fromArgb(c.text), c.label.c_str());
        else if (c.kind == "group")
        {
            list->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), theme::fromArgb(c.background),
                                c.rounding);
            list->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), colour, c.rounding);
            list->AddText(ImVec2(at.x + 6, at.y + 4), colour, c.label.c_str());
        }
        else if (c.kind == "custom" && c.custom)
        {
            CyInterfaceContext api;
            api.imgui = context_;
            api.user = &plugin_;
            api.primary = c.primary.c_str();
            api.secondary = c.secondary.c_str();
            api.tertiary = c.tertiary.c_str();
            api.quaternary = c.quaternary.c_str();
            if (p)
            {
                api.minimum = p->minimum;
                api.maximum = p->maximum;
            }
            api.get = [](void *user, const char *key)
            { return static_cast<EditorModel *>(user)->value(key); };
            api.set = [](void *user, const char *key, double v)
            { static_cast<EditorModel *>(user)->edit(key, v); };
            ImGui::BeginChild("##custom", size, 0,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
            c.custom(&api);
            ImGui::EndChild();
        }
        else if (c.kind == "native-panel")
        {
            std::vector<visual::Param> params;
            for (const auto &key : c.extra)
                if (const auto *a = parameter(key))
                    params.push_back({a->key, a->name, a->unit, a->minimum, a->maximum,
                                      plugin_.value(key.c_str()), a->initial});
                else
                    params.emplace_back();
            if (!c.instrumentType.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::fromArgb(c.background));
                if (const auto *panel =
                        instrument_ui::panel(c.instrumentType, c.instrumentPanel, c.instrumentCompact))
                    instrument_ui::draw(
                        "##instrument", c.instrumentType, *panel, c.instrumentKeys, params, at, size, colour,
                        [this](const auto &param, double v) { plugin_.edit(param.binding.toRawUTF8(), v); },
                        true, 1.0f, c.label.c_str());
                ImGui::PopStyleColor();
            }
            else
            {
                visual::Pane pane{static_cast<visual::PaneKind>(c.pane), c.indices, c.variant};
                engine::AudioAnalysisFrame before, after;
                auto live = liveFor(c, before, after);
                const visual::Setter set = [this](const visual::Param &a, double value)
                { plugin_.edit(a.binding.toRawUTF8(), value); };
                if (c.large)
                    visual::drawLargePane("##native", pane, params, at, size, colour, live, set);
                else
                    visual::drawPane("##native", pane, params, at, size, colour, live, set, true);
            }
        }
        else if (c.kind == "filter-response" || c.kind == "dynamics-response" || c.kind == "eq-curve" ||
                 c.kind == "envelope" || c.kind == "wavetable")
        {
            std::vector<visual::Param> params;
            std::vector<int> indices;
            for (const auto &key : {c.primary, c.secondary, c.tertiary, c.quaternary})
                if (const auto *a = parameter(key))
                {
                    indices.push_back(static_cast<int>(params.size()));
                    params.push_back({a->key, a->name, a->unit, a->minimum, a->maximum,
                                      plugin_.value(key.c_str()), a->initial});
                }
                else
                    indices.push_back(-1);
            const auto kind = c.kind == "envelope"            ? visual::PaneKind::envelope
                              : c.kind == "dynamics-response" ? visual::PaneKind::dynamics
                              : c.kind == "wavetable"         ? visual::PaneKind::wavetable
                              : c.kind == "eq-curve"          ? visual::PaneKind::equalizer
                                                              : visual::PaneKind::filter;
            visual::Pane pane{kind, indices, 0};
            visual::Live live;
            visual::drawPane(
                "##visual", pane, params, at, size, colour, live,
                [this](const visual::Param &a, double v) { plugin_.edit(a.binding.toRawUTF8(), v); }, false);
        }
        else if (c.kind == "waveform" || c.kind == "spectrum" || c.kind == "level-meter")
        {
            list->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), theme::fromArgb(c.background),
                                c.rounding);
            if (c.kind == "waveform")
            {
                auto previous = ImVec2(at.x, at.y + size.y * .5f);
                const auto count = outputFrame_.waveformCount;
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    auto point = ImVec2(at.x + size.x * i / std::max(1u, count - 1),
                                        at.y + size.y * (.5f - .45f * outputFrame_.waveform[i]));
                    if (i)
                        list->AddLine(previous, point, colour);
                    previous = point;
                }
            }
            else if (c.kind == "level-meter")
            {
                const auto db = cydaw::v2::recentRmsDb(outputFrame_);
                const auto h = std::clamp((db + 72) / 72, 0.0f, 1.0f) * size.y;
                list->AddRectFilled(ImVec2(at.x, at.y + size.y - h), ImVec2(at.x + size.x, at.y + size.y),
                                    colour, 2);
            }
            else
            {
                auto &spectrum = displays_[c.id].after;
                spectrum.update(&outputFrame_, ImGui::GetTime());
                for (int i = 1; i < cydaw::v2::Spectrum::binCount; ++i)
                    list->AddLine(ImVec2(at.x + size.x * (i - 1) / (cydaw::v2::Spectrum::binCount - 1),
                                         at.y + size.y * (1 - spectrum.levels()[i - 1])),
                                  ImVec2(at.x + size.x * i / (cydaw::v2::Spectrum::binCount - 1),
                                         at.y + size.y * (1 - spectrum.levels()[i])),
                                  colour, 1.5f);
            }
        }
        else if (p)
        {
            auto value = static_cast<float>(plugin_.value(p->key.c_str()));
            bool changed = false;
            ImGui::SetNextItemWidth(size.x);
            if (c.kind == "knob")
            {
                ui::KnobStyle style;
                style.diameter = std::max(16.0f, std::min(size.x, size.y - 30));
                style.label = c.label.c_str();
                style.colour = colour;
                style.unit = p->unit.c_str();
                changed = ui::knob("##knob", &value, static_cast<float>(p->minimum),
                                   static_cast<float>(p->maximum), static_cast<float>(p->initial), style);
            }
            else if (c.kind == "toggle")
            {
                bool on = value > (p->minimum + p->maximum) * .5;
                changed = ImGui::Checkbox(c.label.c_str(), &on);
                value = static_cast<float>(on ? p->maximum : p->minimum);
            }
            else if (c.kind == "selector")
            {
                int v = static_cast<int>(std::lround(value));
                changed = ImGui::SliderInt(c.label.c_str(), &v, static_cast<int>(p->minimum),
                                           static_cast<int>(p->maximum));
                value = static_cast<float>(v);
            }
            else if (c.kind == "value-display")
            {
                ImGui::TextUnformatted(c.label.c_str());
                changed = ImGui::DragFloat("##value", &value, (p->maximum - p->minimum) * .002f, p->minimum,
                                           p->maximum, "%.3f");
            }
            else if (c.kind == "xy-pad")
            {
                ImGui::InvisibleButton("##xy", size);
                list->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), colour, c.rounding);
                const auto *other = parameter(c.secondary);
                const auto y = other ? (plugin_.value(other->key.c_str()) - other->minimum) /
                                           (other->maximum - other->minimum)
                                     : .5;
                const auto x = (value - p->minimum) / (p->maximum - p->minimum);
                list->AddCircleFilled(ImVec2(at.x + size.x * x, at.y + size.y * (1 - y)), 5, colour);
                if (ImGui::IsItemActive())
                {
                    auto mouse = ImGui::GetIO().MousePos;
                    plugin_.edit(p->key.c_str(),
                                 p->minimum + std::clamp((mouse.x - at.x) / size.x, 0.0f, 1.0f) *
                                                  (p->maximum - p->minimum));
                    if (other)
                        plugin_.edit(other->key.c_str(),
                                     other->minimum +
                                         (1 - std::clamp((mouse.y - at.y) / size.y, 0.0f, 1.0f)) *
                                             (other->maximum - other->minimum));
                }
            }
            else
                changed = ImGui::VSliderFloat("##fader", size, &value, p->minimum, p->maximum, "%.2f");
            if (changed)
                plugin_.edit(p->key.c_str(), value);
        }
        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::PopID();
        ImGui::SetCursorScreenPos(cursor);
        ImGui::Dummy(ImVec2(0, 0));
    }
    void drawContent() {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::fromArgb(plugin_.definition().background));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::fromArgb(plugin_.definition().text));
        ImGui::Begin("##plugin", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        plugin_.analysis(0, false, outputFrame_);
        const auto &d = plugin_.definition();
        std::set<std::string> pages;
        for (const auto &c : d.controls)
            if (!c.page.empty())
                pages.insert(c.page);
        if (!pages.empty())
        {
            if (page_.empty())
                page_ = *pages.begin();
            for (const auto &name : pages)
            {
                if (ImGui::Button(name.c_str()))
                    page_ = name;
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }
        const auto origin = ImGui::GetCursorScreenPos(), space = ImGui::GetContentRegionAvail();
        for (const auto &c : d.controls)
            control(c, origin, space.x, space.y);
        ImGui::End();
        ImGui::PopStyleColor(2);
    }
    explicit EditorContent(EditorModel &model) : plugin_(model) {}
};
}
