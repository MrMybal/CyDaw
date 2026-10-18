#pragma once

#include <cydaw/engine/AutomationCurve.h>

#include <juce_core/juce_core.h>

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <vector>

// The curve editor every automation lane uses: a lane of points drawn on its
// editor's timeline, and the bar that says and edits what is selected.
//   Points: a double click puts one (on a point: removes it); a click picks
//   one (Ctrl: one more or one less, Shift: one more); a drag in the empty
//   lane picks those in its frame; the picked ones move together (the
//   grid's magnet, Alt: free); Alt+drag between two points bends the
//   segment; a Bezier point picked shows its handles, dragged; Delete
//   removes the picked; Ctrl+A picks all; Ctrl+C / Ctrl+V copy and paste
//   them between any curve editors (values carried over as a part of each
//   lane's range, times in seconds); a right click: the menu.
//   Each point has a shape (engine::CurveShape): straight, bent, a
//   B-spline's (the curve passes near it, smooth), a Bezier one with one
//   tangent, or with one on each side. The curve drawn is the engine's.
//   The bar: one point picked, its shape, time, value (and bend or handles)
//   typed or dragged; several, their shape, moved together, scaled, spread
//   at random, tilted, flipped, put on the grid (Bitwig's multi-edit).

namespace cydaw::v2::curves {

struct Point {
    std::uint64_t id {};
    double time {};  // the editor's own unit: seconds, beats
    double value {}; // the lane's units
    int shape {};    // engine::CurveShape
    float bend {};   // a bent segment's amount (-1..1)
    // A Bezier point's handles: time in the editor's unit, values in the
    // lane's (their natural log on a logarithmic lane).
    engine::CurveHandles handles {};
};

// What a lane stands for: its range, its scale, how a value reads, and how
// its editor shows time.
struct Lane {
    double minimum {};
    double maximum { 1.0 };
    // It glides on a logarithmic scale (as the engine plays it), and is
    // shown on one unless toPart says otherwise.
    bool logarithmic {};
    double fallback {}; // drawn when it has no point
    // Where a value stands on the lane's height (0..1) and back, when not on
    // its range's straight or logarithmic scale (the volume's, squared).
    std::function<double(double)> toPart;
    std::function<double(double)> fromPart;
    std::function<juce::String(double)> formatValue;
    std::function<juce::String(double)> formatTime;
    ImU32 colour {};
    // The curve filled under (the master's clips).
    bool filled {};
};

// Where the editor's timeline is.
struct View {
    std::function<float(double)> xOf;
    std::function<double(float)> timeAt;
    std::function<double(double)> snap; // a time on the grid (the magnet, Alt)
    double cursor {};
    // Where points may go in time.
    double minimumTime { -1.0e300 };
    double maximumTime { 1.0e300 };
    // Seconds per unit of time (the copy carries seconds between editors).
    double secondsPerUnit { 1.0 };
    // A host whose canvas already takes the pointer (the arrangement) says
    // when the lane is under it; the editor then has no item of its own.
    bool hostInput {};
    bool hostHovered {};
    // The host's own items at the end of the right click's menu.
    std::function<void()> menu;
};

// What the copy holds: each point's time from the first one's (seconds),
// its value as a part of its lane's range, its shape and handles (their
// values as parts of the range on the lane's interpolation scale).
struct Copied {
    double seconds {};
    double part {};
    int shape {};
    float bend {};
    double inSeconds {};
    double inPart {};
    double outSeconds {};
    double outPart {};
};
std::vector<Copied>& clipboard();

class Editor final {
public:
    // The lane drawn in [min, max] and edited: true when `points` changed
    // (let go of a drag, a point put, removed, pasted: one step).
    bool draw(const char* id, ImVec2 min, ImVec2 max, std::vector<Point>& points, const Lane& lane, const View& view);
    // The bar of what is picked in this lane (its shape, time and value, or
    // the multi-edit): true when `points` changed.
    bool drawBar(std::vector<Point>& points, const Lane& lane, double gridStep);
    [[nodiscard]] bool hasSelection() const noexcept { return !selected_.empty(); }
    void clearSelection() noexcept { selected_.clear(); }
    // The lane the keys go to (the one last clicked).
    [[nodiscard]] bool focused() const noexcept { return focused_; }
    void setFocused(bool focused) noexcept { focused_ = focused; }
    // In the middle of a gesture: the host leaves the pointer to it.
    [[nodiscard]] bool busy() const noexcept { return pressed_ || dragging_ || framing_ || bending_ || handleSide_ != 0; }

    // The lane's value at a time, as the engine plays it.
    [[nodiscard]] static double valueAt(const std::vector<Point>& points, const Lane& lane, double time);
    // A value as a part of the lane's height, and back.
    [[nodiscard]] static double partOf(const Lane& lane, double value);
    [[nodiscard]] static double valueOf(const Lane& lane, double part);
    [[nodiscard]] static const char* shapeName(int shape);

private:
    [[nodiscard]] static std::uint64_t freshId(const std::vector<Point>& points);
    [[nodiscard]] static bool onLogScale(const Lane& lane);
    // A value on the lane's interpolation scale (the log on a logarithmic one).
    [[nodiscard]] static double scaled(const Lane& lane, double value);
    [[nodiscard]] static double unscaled(const Lane& lane, double value);
    // The interpolation scale's span (a handle's value as a part of it).
    [[nodiscard]] static double scaleSpan(const Lane& lane);
    // The points as shown: with the gesture under way.
    [[nodiscard]] std::vector<Point> shownPoints(const std::vector<Point>& points, const Lane& lane) const;
    bool copy(const std::vector<Point>& points, const Lane& lane, const View& view);
    bool paste(std::vector<Point>& points, const Lane& lane, const View& view, double at);
    bool removeSelected(std::vector<Point>& points);
    bool setShape(std::vector<Point>& points, const Lane& lane, int shape);
    bool shapeButtons(std::vector<Point>& points, const Lane& lane);
    // A number of the picked point's, dragged or typed: kept aside while it
    // moves, given back when let go.
    std::optional<double> numberField(const char* id, int field, std::uint64_t point, double current, float speed, const double* minimum,
                                      const double* maximum, const char* format, float width);

    std::set<std::uint64_t> selected_;
    bool focused_ {};
    // The pointer pressed on the lane (a host's input).
    bool pressed_ {};
    // A drag of the picked points: where it started and how far it went.
    bool dragging_ {};
    bool moved_ {};
    double grabTime_ {};
    double grabPart_ {};
    double dragTime_ {};
    double dragPart_ {};
    std::vector<Point> before_;
    double dragFrom_ {};
    // A frame being drawn in the empty lane; where a right click was.
    bool framing_ {};
    ImVec2 frameFrom_ {};
    double menuTime_ {};
    // A segment being bent (Alt+drag): its first point, from what amount.
    bool bending_ {};
    std::uint64_t bendPoint_ {};
    float bendFrom_ {};
    float bendNow_ {};
    float bendDirection_ { 1.0f };
    float bendY_ {};
    // A handle dragged: its point, its side (1 after, -1 before), as it is.
    std::uint64_t handlePoint_ {};
    int handleSide_ {};
    engine::CurveHandles handleNow_ {};
    // The picked point's field being dragged or typed.
    std::uint64_t editPoint_ {};
    int editField_ { -1 };
    double editBuffer_ {};
    float valueShift_ {};
    float timeShift_ {};
    // The multi-edit's amounts.
    float randomAmount_ { 0.1f };
    float scaleAmount_ { 1.0f };
    float tiltAmount_ { 0.1f };
};

} // namespace cydaw::v2::curves
