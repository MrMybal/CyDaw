#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cydaw::app {

// One playhead over every lane of the arrangement, a sibling of the lanes
// rather than a child of each. Moving it invalidates nothing the lanes
// cache, so a tick blits their images where the line was and where it is;
// a line inside each lane re-rendered a strip of every lane, twice.
class ArrangementPlayheadComponent final : public juce::Component {
public:
    ArrangementPlayheadComponent()
    {
        setInterceptsMouseClicks(false, false);
        setAlwaysOnTop(true);
    }

    // The line's x in the host's coordinates, or below zero to hide it.
    void setPosition(const int x, const int height)
    {
        if (x < 0) {
            setVisible(false);
            return;
        }
        setVisible(true);
        setBounds(x - 1, 0, 2, height);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(juce::Colour(0xffff8a00));
        graphics.fillAll();
    }
};

} // namespace cydaw::app
