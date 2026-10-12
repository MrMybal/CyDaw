#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace cydaw::app {

class TimelineRulerComponent final : public juce::Component,
                                    public juce::SettableTooltipClient {
public:
    enum class NavigationTool { pointer, zoom, hand };

    void setTimeline(double lengthSeconds, double tempoBpm);
    void setVisibleRange(double startSeconds, double endSeconds);
    void setPlayhead(double seconds);
    void setSeekHandler(std::function<void(double)> handler);
    void setLoopRange(double startSeconds, double endSeconds, bool enabled);
    void setTimeSelection(double startSeconds, double endSeconds,
                          bool visible);
    void setLoopRangeHandler(
        std::function<void(double, double)> handler);
    void setZoomHandler(
        std::function<void(double, double)> handler);
    void setPanHandler(std::function<void(double)> handler);
    void setNavigationTool(NavigationTool tool);

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

    static constexpr int trackHeaderWidth = 194;

private:
    enum class DragMode { none, pan, zoom, createLoop, loopStart, loopEnd };

    void seek(float x);
    void updateLoopRange(float x);
    [[nodiscard]] double timeFromX(float x) const noexcept;
    [[nodiscard]] float xFromTime(double seconds) const noexcept;

    double lengthSeconds_ { 8.0 };
    double visibleStartSeconds_ {};
    double visibleEndSeconds_ { 8.0 };
    double tempoBpm_ { 120.0 };
    double playheadSeconds_ {};
    double loopStartSeconds_ {};
    double loopEndSeconds_ { 4.0 };
    double loopDragAnchorSeconds_ {};
    bool loopEnabled_ {};
    bool timeSelectionVisible_ {};
    double timeSelectionStartSeconds_ {};
    double timeSelectionEndSeconds_ {};
    DragMode dragMode_ { DragMode::none };
    float lastPanX_ {};
    int lastZoomScreenY_ {};
    double zoomAnchorSeconds_ {};
    bool zoomGestureMoved_ {};
    NavigationTool navigationTool_ { NavigationTool::pointer };
    std::function<void(double)> seekHandler_;
    std::function<void(double, double)> loopRangeHandler_;
    std::function<void(double, double)> zoomHandler_;
    std::function<void(double)> panHandler_;
};

} // namespace cydaw::app
