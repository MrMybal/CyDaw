#include <cydaw/base/File.h>
#pragma once
#include <algorithm>
#include <cydaw/engine/FoundrySession.h>
#include <juce_core/juce_core.h>
namespace cydaw::v2::foundry_json
{
inline cydaw::base::Value automation(const std::vector<engine::foundry::AutomationLane> &lanes)
{
    cydaw::base::Array<cydaw::base::Value> out;
    for (const auto &lane : lanes)
    {
        auto *item = new cydaw::base::Object();
        item->setProperty("binding", juce::String::fromUTF8(lane.binding.c_str()));
        cydaw::base::Array<cydaw::base::Value> points;
        for (const auto &point : lane.points)
        {
            auto *p = new cydaw::base::Object();
            p->setProperty("frame", point.frame);
            p->setProperty("value", point.value);
            points.add(cydaw::base::Value(p));
        }
        item->setProperty("points", points);
        out.add(cydaw::base::Value(item));
    }
    return out;
}
inline std::vector<engine::foundry::AutomationLane> automation(const cydaw::base::Value &data)
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
inline cydaw::base::Value notes(const std::vector<engine::foundry::Note> &notes)
{
    cydaw::base::Array<cydaw::base::Value> out;
    for (const auto &n : notes)
    {
        auto *p = new cydaw::base::Object();
        p->setProperty("id", static_cast<juce::int64>(n.id));
        p->setProperty("start", n.start);
        p->setProperty("length", n.length);
        p->setProperty("key", n.key);
        p->setProperty("velocity", n.velocity);
        out.add(cydaw::base::Value(p));
    }
    return out;
}
inline std::vector<engine::foundry::Note> notes(const cydaw::base::Value &data, engine::foundry::Session &session)
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
