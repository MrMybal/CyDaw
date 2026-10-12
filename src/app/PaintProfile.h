#pragma once

#include <juce_core/juce_core.h>

namespace cydaw::app::paint_profile {

// Time spent in each surface's paint since the last reset. The hitch probe
// reads and resets it at the end of every workspace paint it notes, so a
// slow paint says which surface it was. Off the probe this is a few
// additions per paint on the message thread.
struct Counters {
    double waveform {};
    double automation {};
    double lane {};
    double ruler {};
    double roll {};
    double graph {};
    double device {};
    double chain {};
    double mixer {};
    double launcher {};
    int paints {};
};

inline Counters& counters()
{
    static Counters instance;
    return instance;
}

class Scoped {
public:
    explicit Scoped(double& slot) noexcept
        : slot_(slot), start_(juce::Time::getMillisecondCounterHiRes())
    {
        ++counters().paints;
    }

    ~Scoped() noexcept
    {
        slot_ += juce::Time::getMillisecondCounterHiRes() - start_;
    }

private:
    double& slot_;
    double start_;
};

inline Counters& operator+=(Counters& total, const Counters& part)
{
    total.waveform += part.waveform;
    total.automation += part.automation;
    total.lane += part.lane;
    total.ruler += part.ruler;
    total.roll += part.roll;
    total.graph += part.graph;
    total.device += part.device;
    total.chain += part.chain;
    total.mixer += part.mixer;
    total.launcher += part.launcher;
    total.paints += part.paints;
    return total;
}

inline juce::String describe(const Counters& current)
{
    juce::String text;
    const auto add = [&text](const char* name, const double ms) {
        if (ms >= 0.05)
            text << name << "=" << juce::String(ms, 1) << " ";
    };
    add("waveform", current.waveform);
    add("automation", current.automation);
    add("lane", current.lane);
    add("ruler", current.ruler);
    add("roll", current.roll);
    add("graph", current.graph);
    add("device", current.device);
    add("chain", current.chain);
    add("mixer", current.mixer);
    add("launcher", current.launcher);
    text << "paints=" << current.paints;
    return text;
}

inline juce::String describeAndReset()
{
    auto& current = counters();
    auto text = describe(current);
    current = Counters {};
    return text;
}

} // namespace cydaw::app::paint_profile
