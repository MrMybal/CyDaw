#pragma once
#include <algorithm>
#include <cydaw/engine/FoundrySession.h>
#include <juce_core/juce_core.h>
namespace cydaw::v2::foundry_json
{
inline juce::var automation(const std::vector<engine::foundry::AutomationLane> &lanes)
{
    juce::Array<juce::var> out;
    for (const auto &lane : lanes)
    {
        auto *item = new juce::DynamicObject();
        item->setProperty("binding", juce::String::fromUTF8(lane.binding.c_str()));
        juce::Array<juce::var> points;
        for (const auto &point : lane.points)
        {
            auto *p = new juce::DynamicObject();
            p->setProperty("frame", point.frame);
            p->setProperty("value", point.value);
            points.add(juce::var(p));
        }
        item->setProperty("points", points);
        out.add(juce::var(item));
    }
    return out;
}
inline std::vector<engine::foundry::AutomationLane> automation(const juce::var &data)
{
    std::vector<engine::foundry::AutomationLane> out;
    if (const auto *lanes = data.getArray())
        for (const auto &item : *lanes)
        {
            engine::foundry::AutomationLane lane;
            lane.binding = item.getProperty("binding", {}).toString().toStdString();
            if (const auto *points = item.getProperty("points", {}).getArray())
                for (const auto &p : *points)
                {
                    const auto value = static_cast<float>(p.getProperty("value", 0));
                    if (std::isfinite(value))
                        lane.points.push_back({std::max<juce::int64>(0, static_cast<juce::int64>(p.getProperty("frame", 0))), value});
                }
            std::stable_sort(lane.points.begin(), lane.points.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
            out.push_back(std::move(lane));
        }
    return out;
}
inline juce::var notes(const std::vector<engine::foundry::Note> &notes)
{
    juce::Array<juce::var> out;
    for (const auto &n : notes)
    {
        auto *p = new juce::DynamicObject();
        p->setProperty("id", static_cast<juce::int64>(n.id));
        p->setProperty("start", n.start);
        p->setProperty("length", n.length);
        p->setProperty("key", n.key);
        p->setProperty("velocity", n.velocity);
        out.add(juce::var(p));
    }
    return out;
}
inline std::vector<engine::foundry::Note> notes(const juce::var &data, engine::foundry::Session &session)
{
    std::vector<engine::foundry::Note> out;
    if (const auto *notes = data.getArray())
        for (const auto &p : *notes)
        {
            engine::foundry::Note n;
            n.id = static_cast<juce::int64>(p.getProperty("id", 0));
            if (!n.id)
                n.id = session.newId();
            session.nextId = std::max(session.nextId, n.id + 1);
            n.start = std::max<juce::int64>(0, static_cast<juce::int64>(p.getProperty("start", 0)));
            n.length = std::max<juce::int64>(1, static_cast<juce::int64>(p.getProperty("length", 1)));
            n.key = std::clamp(static_cast<int>(p.getProperty("key", 60)), 0, 127);
            n.velocity = std::clamp(static_cast<float>(p.getProperty("velocity", .8)), .001f, 1.f);
            out.push_back(n);
        }
    return out;
}
} // namespace cydaw::v2::foundry_json
