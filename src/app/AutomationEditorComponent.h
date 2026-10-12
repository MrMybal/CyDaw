#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace cydaw::app {

class AutomationEditorComponent final : public juce::Component,
                                        public juce::SettableTooltipClient,
                                        private juce::AsyncUpdater {
public:
    struct Point {
        std::uint64_t id {};
        double timeSeconds {};
        float value {};
        int curveType {};
        float curveAmount {};
    };

    struct Target {
        juce::String id;
        juce::String name;
        float minimumValue {};
        float maximumValue { 1.0f };
        juce::String suffix;
    };

    AutomationEditorComponent();

    void setPoints(std::vector<Point> points, double durationSeconds);
    void setVisibleRange(double startSeconds, double endSeconds);
    // The lane itself, excluding the header controls. Public so the
    // performance self test can measure the drawing separately from the
    // six child controls that share the component.
    [[nodiscard]] juce::Rectangle<float> graphBounds() const;
    void setMusicalGrid(double tempoBpm, int gridDivision, bool enabled);
    void setTimeAxisInset(float leftPixels);
    void setCompactMode(bool compact);
    void setTargets(std::vector<Target> targets, const juce::String& selectedTargetId);
    void setPlayhead(double seconds);
    // The host draws the playhead over this lane; it keeps the time only.
    void setPlayheadDrawnByHost(bool drawnByHost);
    void refreshTranslations();
    void setAddHandler(std::function<void(double, float)> handler);
    void setUpdateHandler(std::function<void(std::uint64_t, double, float)> handler);
    void setLiveUpdateHandler(
        std::function<void(std::uint64_t, double, float)> handler);
    void setRemoveHandler(std::function<void(std::uint64_t)> handler);
    void setCurveHandler(
        std::function<void(std::uint64_t, int, float)> handler);
    void setLiveCurveHandler(
        std::function<void(std::uint64_t, int, float)> handler);
    void setSeekHandler(std::function<void(double)> handler);
    void setTargetChangedHandler(std::function<void(const juce::String&)> handler);
    void setEditGestureHandlers(std::function<void()> beginHandler,
                                std::function<void()> endHandler);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:

    [[nodiscard]] juce::Point<float> pointPosition(const Point& point) const;
    [[nodiscard]] const Point* pointAt(juce::Point<float> position) const;
    [[nodiscard]] double timeAt(float x) const;
    [[nodiscard]] double snappedTime(double seconds) const noexcept;
    [[nodiscard]] float valueAt(float y) const;
    [[nodiscard]] juce::Point<float> curveHandlePosition(
        const Point& point, const Point& next) const;
    void updateCurveSelector();
    void updatePointEditors();
    void applyPointEditors();

    std::vector<Point> points_;
    std::vector<Target> targets_;
    juce::ComboBox targetSelector_;
    juce::ComboBox curveSelector_;
    juce::Label pointTimeLabel_;
    juce::Label pointValueLabel_;
    juce::TextEditor pointTimeEditor_;
    juce::TextEditor pointValueEditor_;
    double durationSeconds_ { 1.0 };
    double visibleStartSeconds_ {};
    double visibleEndSeconds_ { 1.0 };
    double playheadSeconds_ {};
    bool playheadDrawnByHost_ {};
    std::uint64_t selectedPointId_ {};
    // A drag emits mouse-move events far faster than the display refreshes, and
    // each one used to repaint the whole lane and republish the entire automation
    // vector to the audio thread. The edit is applied immediately; the redraw and
    // the publication are folded into one per message-loop turn.
    void handleAsyncUpdate() override;
    struct PendingLiveEdit {
        std::uint64_t id {};
        double timeSeconds {};
        float value {};
        int curveKind { -1 };
        float curveAmount {};
    };
    std::optional<PendingLiveEdit> pendingLiveEdit_;

    std::optional<Point> draggedPoint_;
    std::optional<Point> draggedCurvePoint_;
    bool draggedPointChanged_ {};
    bool draggedCurveChanged_ {};
    juce::Point<float> dragStart_;
    juce::String emptyMessage_;
    std::function<void(double, float)> addHandler_;
    std::function<void(std::uint64_t, double, float)> updateHandler_;
    std::function<void(std::uint64_t, double, float)> liveUpdateHandler_;
    std::function<void(std::uint64_t)> removeHandler_;
    std::function<void(std::uint64_t, int, float)> curveHandler_;
    std::function<void(std::uint64_t, int, float)> liveCurveHandler_;
    std::function<void(double)> seekHandler_;
    std::function<void(const juce::String&)> targetChangedHandler_;
    std::function<void()> editGestureBeginHandler_;
    std::function<void()> editGestureEndHandler_;
    float minimumValue_ { -60.0f };
    float maximumValue_ { 12.0f };
    juce::String valueSuffix_ { " dB" };
    bool compactMode_ {};
    double tempoBpm_ { 120.0 };
    int gridDivision_ { 16 };
    bool musicalGridEnabled_ { true };
    float timeAxisInset_ {};
    bool updatingPointEditors_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationEditorComponent)
};

} // namespace cydaw::app
