#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cydaw::engine {

// How an automation lane's point shapes the curve around it. Every lane of
// the engine (a track's volume and pan, a device's knob, a plug-in's
// parameter, a clip's lanes, the master's clips) and every editor that draws
// one reads its curve here, so what is drawn is what is heard.
enum class CurveShape : int {
    // Straight to the next point.
    straight = 0,
    // The segment after it bent by its amount (-1..1).
    bend = 1,
    // A B-spline's control point: the curve passes near it, smooth, pulled
    // by it and its neighbours rather than through it.
    spline = 2,
    // A Bezier point with one tangent: its two handles in a line, the same
    // length on each side.
    tangent = 3,
    // A Bezier point with a handle on each side, each free.
    tangents = 4
};
inline constexpr int curveShapeCount = 5;

[[nodiscard]] inline constexpr int clampedCurveShape(const int shape) noexcept
{
    return shape < 0 || shape >= curveShapeCount ? 0 : shape;
}

// A Bezier point's handles: how far before (in) and after (out) it they
// reach, in the lane's time unit, and how much higher they are, in the lane's
// units (on a logarithmic lane, in the natural log of the value). A tangent
// point uses only its out handle, mirrored before it.
struct CurveHandles {
    float inTime {};
    float inValue {};
    float outTime {};
    float outValue {};
    bool operator==(const CurveHandles&) const = default;
};

// A point as the curve sees it: what each lane's own point gives.
struct CurveKnot {
    double time {};
    double value {};
    int shape {};
    float bend {};
    CurveHandles handles {};
};

namespace curve_detail {

struct Vec {
    double t {};
    double v {};
};

// A segment of the curve: a cubic Bezier from where it passes at a point to
// where it passes at the next, and the bend of the first when it is flat.
struct Segment {
    Vec b0, b1, b2, b3;
    bool straight {};
    bool bent {};
    float bend {};
};

[[nodiscard]] inline double space(const double value, const bool logarithmic) noexcept
{
    return logarithmic ? std::log(std::max(value, 1.0e-12)) : value;
}

[[nodiscard]] inline double unspace(const double value, const bool logarithmic) noexcept
{
    return logarithmic ? std::exp(value) : value;
}

// Where the curve passes near point `index`, and the ends of its handles.
template <typename KnotAt>
void anchorOf(const std::size_t count, KnotAt& knotAt, const std::size_t index, const bool logarithmic, Vec& anchor, Vec& in, Vec& out)
{
    const CurveKnot knot = knotAt(index);
    const Vec at { knot.time, space(knot.value, logarithmic) };
    anchor = in = out = at;
    switch (static_cast<CurveShape>(clampedCurveShape(knot.shape))) {
    case CurveShape::spline: {
        // A uniform cubic B-spline over the points: the curve meets the
        // thirds towards each neighbour, and passes at their middle. The
        // first and the last are where the curve starts and ends.
        const auto hasBefore = index > 0;
        const auto hasAfter = index + 1 < count;
        if (hasBefore) {
            const CurveKnot before = knotAt(index - 1);
            in = { (2.0 * at.t + before.time) / 3.0, (2.0 * at.v + space(before.value, logarithmic)) / 3.0 };
        }
        if (hasAfter) {
            const CurveKnot after = knotAt(index + 1);
            out = { (2.0 * at.t + after.time) / 3.0, (2.0 * at.v + space(after.value, logarithmic)) / 3.0 };
        }
        if (hasBefore && hasAfter)
            anchor = { (in.t + out.t) * 0.5, (in.v + out.v) * 0.5 };
        break;
    }
    case CurveShape::tangent: {
        const auto reach = std::max(0.0, static_cast<double>(knot.handles.outTime));
        out = { at.t + reach, at.v + knot.handles.outValue };
        in = { at.t - reach, at.v - knot.handles.outValue };
        break;
    }
    case CurveShape::tangents:
        out = { at.t + std::max(0.0, static_cast<double>(knot.handles.outTime)), at.v + knot.handles.outValue };
        in = { at.t - std::max(0.0, static_cast<double>(knot.handles.inTime)), at.v + knot.handles.inValue };
        break;
    default:
        break;
    }
}

template <typename KnotAt>
[[nodiscard]] Segment segmentOf(const std::size_t count, KnotAt& knotAt, const std::size_t index, const bool logarithmic)
{
    Segment segment;
    Vec in0, out1;
    anchorOf(count, knotAt, index, logarithmic, segment.b0, in0, segment.b1);
    anchorOf(count, knotAt, index + 1, logarithmic, segment.b3, segment.b2, out1);
    const auto first = knotAt(index);
    segment.straight = segment.b1.t == segment.b0.t && segment.b1.v == segment.b0.v && segment.b2.t == segment.b3.t
                       && segment.b2.v == segment.b3.v;
    segment.bent = static_cast<CurveShape>(clampedCurveShape(first.shape)) == CurveShape::bend;
    segment.bend = std::clamp(first.bend, -1.0f, 1.0f);
    if (!segment.straight) {
        // Handles longer than the segment are shortened together, so the
        // curve still goes forward in time.
        const auto span = std::max(0.0, segment.b3.t - segment.b0.t);
        segment.b1.t = std::max(segment.b1.t, segment.b0.t);
        segment.b2.t = std::min(segment.b2.t, segment.b3.t);
        const auto reach = (segment.b1.t - segment.b0.t) + (segment.b3.t - segment.b2.t);
        if (reach > span && reach > 0.0) {
            const auto scale = span / reach;
            segment.b1 = { segment.b0.t + (segment.b1.t - segment.b0.t) * scale, segment.b0.v + (segment.b1.v - segment.b0.v) * scale };
            segment.b2 = { segment.b3.t + (segment.b2.t - segment.b3.t) * scale, segment.b3.v + (segment.b2.v - segment.b3.v) * scale };
        }
    }
    return segment;
}

[[nodiscard]] inline double cubic(const double a, const double b, const double c, const double d, const double u) noexcept
{
    const auto w = 1.0 - u;
    return a * w * w * w + 3.0 * b * u * w * w + 3.0 * c * u * u * w + d * u * u * u;
}

// The value of a segment at a time between its ends.
[[nodiscard]] inline double segmentValue(const Segment& segment, const double time) noexcept
{
    const auto span = segment.b3.t - segment.b0.t;
    if (span <= 1.0e-12)
        return segment.b3.v;
    auto u = std::clamp((time - segment.b0.t) / span, 0.0, 1.0);
    if (segment.straight) {
        if (segment.bent)
            // A bounded cubic bend: monotonic, a single handle's worth.
            u = std::clamp(u + static_cast<double>(segment.bend) * 2.0 * u * (1.0 - u), 0.0, 1.0);
        return segment.b0.v + (segment.b3.v - segment.b0.v) * u;
    }
    // The Bezier's parameter at that time: Newton, kept inside a bracket.
    auto low = 0.0;
    auto high = 1.0;
    for (auto step = 0; step < 12; ++step) {
        const auto x = cubic(segment.b0.t, segment.b1.t, segment.b2.t, segment.b3.t, u) - time;
        if (std::abs(x) <= span * 1.0e-7)
            break;
        if (x > 0.0)
            high = u;
        else
            low = u;
        const auto w = 1.0 - u;
        const auto slope = 3.0 * (w * w * (segment.b1.t - segment.b0.t) + 2.0 * u * w * (segment.b2.t - segment.b1.t)
                                  + u * u * (segment.b3.t - segment.b2.t));
        auto next = slope > 1.0e-12 ? u - x / slope : (low + high) * 0.5;
        if (next <= low || next >= high)
            next = (low + high) * 0.5;
        u = next;
    }
    return cubic(segment.b0.v, segment.b1.v, segment.b2.v, segment.b3.v, u);
}

} // namespace curve_detail

// The curve's value at `time`, over `count` points in time order, each read
// by `knotAt(index)` as a CurveKnot. Before the first and after the last,
// their values held. `hint`, when given, is the segment last used (read and
// updated: a block walking forward finds its segment at once).
template <typename KnotAt>
[[nodiscard]] double curveValueAt(const std::size_t count, KnotAt&& knotAt, const double time, const bool logarithmic = false,
                                  std::size_t* hint = nullptr)
{
    using namespace curve_detail;
    if (count == 0)
        return 0.0;
    // The first and the last points are where the curve starts and ends,
    // whatever their shape.
    if (count == 1 || time <= knotAt(0).time)
        return knotAt(0).value;
    if (time >= knotAt(count - 1).time)
        return knotAt(count - 1).value;
    // Which points it lies between.
    std::size_t index {};
    if (hint != nullptr && *hint + 1 < count && knotAt(*hint).time <= time) {
        index = *hint;
        while (index + 2 < count && knotAt(index + 1).time <= time)
            ++index;
    } else {
        std::size_t low = 0;
        std::size_t high = count - 1;
        while (high - low > 1) {
            const auto middle = (low + high) / 2;
            if (knotAt(middle).time <= time)
                low = middle;
            else
                high = middle;
        }
        index = low;
    }
    if (hint != nullptr)
        *hint = index;
    // A smooth point's curve passes off its time: the segment before or
    // after may hold it.
    auto segment = segmentOf(count, knotAt, index, logarithmic);
    if (time < segment.b0.t && index > 0)
        segment = segmentOf(count, knotAt, index - 1, logarithmic);
    else if (time > segment.b3.t && index + 2 < count)
        segment = segmentOf(count, knotAt, index + 1, logarithmic);
    return unspace(segmentValue(segment, time), logarithmic);
}

// The same over a vector of a lane's own points, given how to read one.
template <typename Points, typename ToKnot>
[[nodiscard]] double curveValueAt(const Points& points, ToKnot&& toKnot, const double time, const bool logarithmic = false,
                                  std::size_t* hint = nullptr)
{
    return curveValueAt(
        points.size(), [&](const std::size_t index) { return toKnot(points[index]); }, time, logarithmic, hint);
}

// The automatic handles of a point turned into a Bezier one: along the line
// through its neighbours, a third of the way to each (time in the lane's
// unit, values in its interpolation space).
template <typename KnotAt>
[[nodiscard]] CurveHandles smoothHandles(const std::size_t count, KnotAt&& knotAt, const std::size_t index, const bool logarithmic = false)
{
    using namespace curve_detail;
    CurveHandles handles;
    if (count < 2 || index >= count)
        return handles;
    const CurveKnot at = knotAt(index);
    const CurveKnot before = knotAt(index > 0 ? index - 1 : index);
    const CurveKnot after = knotAt(index + 1 < count ? index + 1 : index);
    const auto span = after.time - before.time;
    const auto slope = span > 1.0e-12 ? (space(after.value, logarithmic) - space(before.value, logarithmic)) / span : 0.0;
    const auto reachBefore = (at.time - before.time) / 3.0;
    const auto reachAfter = (after.time - at.time) / 3.0;
    handles.inTime = static_cast<float>(reachBefore);
    handles.inValue = static_cast<float>(-slope * reachBefore);
    handles.outTime = static_cast<float>(reachAfter);
    handles.outValue = static_cast<float>(slope * reachAfter);
    return handles;
}

} // namespace cydaw::engine
