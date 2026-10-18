#include "CurveEditor.h"

#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace cydaw::v2::curves {

using ui::dp;
using ui::L;

namespace {

using engine::CurveShape;

bool isBezier(const int shape)
{
    return shape == static_cast<int>(CurveShape::tangent) || shape == static_cast<int>(CurveShape::tangents);
}

void sortByTime(std::vector<Point>& points)
{
    std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
}

engine::CurveKnot knotOf(const Point& point)
{
    return { point.time, point.value, point.shape, point.bend, point.handles };
}

// ---- The shapes' pictures: the curve each makes around a point ----------------------------

float strokeOf(const ImVec2 min, const ImVec2 max)
{
    return std::max(1.2f, (max.y - min.y) * 0.11f);
}

void dot(ImDrawList* list, const ImVec2 at, const ImVec2 min, const ImVec2 max, const ImU32 colour, const float scale = 1.0f)
{
    list->AddCircleFilled(at, std::max(1.5f, (max.y - min.y) * 0.15f) * scale, colour, 10);
}

ImVec2 inBox(const ImVec2 a, const ImVec2 b, const float x, const float y)
{
    return { a.x + (b.x - a.x) * x, a.y + (b.y - a.y) * y };
}

// Straight from point to point.
void pictureStraight(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    const ImVec2 points[] = { inBox(min, max, 0.0f, 1.0f), inBox(min, max, 0.5f, 0.05f), inBox(min, max, 1.0f, 0.55f) };
    list->AddPolyline(points, 3, colour, strokeOf(min, max));
    for (const auto& point : points)
        dot(list, point, min, max, colour);
}

// A segment bent between its two points.
void pictureBent(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    const auto from = inBox(min, max, 0.0f, 1.0f);
    const auto to = inBox(min, max, 1.0f, 0.0f);
    list->AddBezierCubic(from, inBox(min, max, 0.8f, 1.0f), inBox(min, max, 1.0f, 0.75f), to, colour, strokeOf(min, max), 16);
    dot(list, from, min, max, colour);
    dot(list, to, min, max, colour);
}

// A B-spline: its point (a diamond) above, pulling the smooth curve
// without lying on it.
void pictureSpline(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    const auto from = inBox(min, max, 0.0f, 0.95f);
    const auto to = inBox(min, max, 1.0f, 0.95f);
    const auto pull = inBox(min, max, 0.5f, 0.12f);
    list->AddLine(from, pull, theme::alpha(colour, 0.45f), 1.0f);
    list->AddLine(pull, to, theme::alpha(colour, 0.45f), 1.0f);
    list->AddBezierCubic(from, inBox(min, max, 0.3f, 0.45f), inBox(min, max, 0.7f, 0.45f), to, colour, strokeOf(min, max), 16);
    const auto size = std::max(2.0f, (max.y - min.y) * 0.22f);
    const ImVec2 diamond[] = { ImVec2(pull.x, pull.y - size), ImVec2(pull.x + size, pull.y), ImVec2(pull.x, pull.y + size),
                               ImVec2(pull.x - size, pull.y) };
    list->AddConvexPolyFilled(diamond, 4, colour);
}

// A Bezier point with one tangent: its two handles in a line.
void pictureTangent(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    const auto centre = inBox(min, max, 0.5f, 0.5f);
    const ImVec2 reach((max.x - min.x) * 0.3f, -(max.y - min.y) * 0.32f);
    const auto stroke = strokeOf(min, max);
    list->AddBezierCubic(inBox(min, max, 0.0f, 0.95f), inBox(min, max, 0.25f, 0.95f), ImVec2(centre.x - reach.x, centre.y - reach.y), centre,
                         colour, stroke, 12);
    list->AddBezierCubic(centre, ImVec2(centre.x + reach.x, centre.y + reach.y), inBox(min, max, 0.75f, 0.05f), inBox(min, max, 1.0f, 0.05f),
                         colour, stroke, 12);
    const ImVec2 before(centre.x - reach.x * 1.15f, centre.y - reach.y * 1.15f);
    const ImVec2 after(centre.x + reach.x * 1.15f, centre.y + reach.y * 1.15f);
    list->AddLine(before, after, colour, 1.0f);
    dot(list, before, min, max, colour, 0.8f);
    dot(list, after, min, max, colour, 0.8f);
    dot(list, centre, min, max, colour, 1.2f);
}

// A Bezier point with a free handle on each side: a corner, the curve
// coming in steep and leaving gently, each along its own handle.
void pictureTangents(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    const auto centre = inBox(min, max, 0.42f, 0.88f);
    const auto before = inBox(min, max, 0.26f, 0.22f);
    const auto after = inBox(min, max, 0.9f, 0.62f);
    const auto stroke = strokeOf(min, max);
    list->AddBezierCubic(inBox(min, max, 0.0f, 0.0f), inBox(min, max, 0.12f, 0.35f), inBox(min, max, 0.32f, 0.45f), centre, colour, stroke, 12);
    list->AddBezierCubic(centre, inBox(min, max, 0.7f, 0.72f), inBox(min, max, 0.92f, 0.3f), inBox(min, max, 1.0f, 0.0f), colour, stroke, 12);
    list->AddLine(centre, before, theme::alpha(colour, 0.8f), 1.0f);
    list->AddLine(centre, after, theme::alpha(colour, 0.8f), 1.0f);
    dot(list, before, min, max, colour, 0.8f);
    dot(list, after, min, max, colour, 0.8f);
    dot(list, centre, min, max, colour, 1.2f);
}

constexpr ui::Picture shapePictures[] = { pictureStraight, pictureBent, pictureSpline, pictureTangent, pictureTangents };

} // namespace

std::vector<Copied>& clipboard()
{
    static std::vector<Copied> copied;
    return copied;
}

const char* Editor::shapeName(const int shape)
{
    switch (static_cast<CurveShape>(engine::clampedCurveShape(shape))) {
    case CurveShape::bend:
        return L("Courbe", "Bent");
    case CurveShape::spline:
        return "B-Spline";
    case CurveShape::tangent:
        return L("Bézier", "Bezier");
    case CurveShape::tangents:
        return L("Bézier libre", "Free Bezier");
    default:
        return L("Droit", "Straight");
    }
}

bool Editor::onLogScale(const Lane& lane)
{
    return lane.logarithmic && lane.minimum > 0.0;
}

double Editor::scaled(const Lane& lane, const double value)
{
    return onLogScale(lane) ? std::log(std::max(value, 1.0e-12)) : value;
}

double Editor::unscaled(const Lane& lane, const double value)
{
    return onLogScale(lane) ? std::exp(value) : value;
}

double Editor::scaleSpan(const Lane& lane)
{
    const auto maximum = std::max(lane.minimum + 1.0e-9, lane.maximum);
    return onLogScale(lane) ? std::log(maximum / lane.minimum) : maximum - lane.minimum;
}

double Editor::partOf(const Lane& lane, const double value)
{
    if (lane.toPart)
        return std::clamp(lane.toPart(value), 0.0, 1.0);
    const auto minimum = lane.minimum;
    const auto maximum = std::max(lane.minimum + 1.0e-9, lane.maximum);
    const auto v = std::clamp(value, minimum, maximum);
    if (onLogScale(lane))
        return std::log(v / minimum) / std::log(maximum / minimum);
    return (v - minimum) / (maximum - minimum);
}

double Editor::valueOf(const Lane& lane, const double part)
{
    const auto n = std::clamp(part, 0.0, 1.0);
    if (lane.fromPart)
        return std::clamp(lane.fromPart(n), std::min(lane.minimum, lane.maximum), std::max(lane.minimum, lane.maximum));
    const auto minimum = lane.minimum;
    const auto maximum = std::max(lane.minimum + 1.0e-9, lane.maximum);
    if (onLogScale(lane))
        return minimum * std::pow(maximum / minimum, n);
    return minimum + n * (maximum - minimum);
}

double Editor::valueAt(const std::vector<Point>& points, const Lane& lane, const double time)
{
    if (points.empty())
        return lane.fallback;
    return engine::curveValueAt(points, knotOf, time, onLogScale(lane));
}

std::uint64_t Editor::freshId(const std::vector<Point>& points)
{
    std::uint64_t id = 1;
    for (const auto& point : points)
        id = std::max(id, point.id + 1);
    return id;
}

bool Editor::copy(const std::vector<Point>& points, const Lane& lane, const View& view)
{
    std::vector<Point> picked;
    for (const auto& point : points)
        if (selected_.contains(point.id))
            picked.push_back(point);
    if (picked.empty())
        return false;
    sortByTime(picked);
    auto& copied = clipboard();
    copied.clear();
    const auto span = scaleSpan(lane);
    for (const auto& point : picked)
        copied.push_back({ (point.time - picked.front().time) * view.secondsPerUnit, partOf(lane, point.value), point.shape, point.bend,
                           point.handles.inTime * view.secondsPerUnit, point.handles.inValue / span,
                           point.handles.outTime * view.secondsPerUnit, point.handles.outValue / span });
    return true;
}

bool Editor::paste(std::vector<Point>& points, const Lane& lane, const View& view, const double at)
{
    const auto& copied = clipboard();
    if (copied.empty())
        return false;
    const auto unit = std::max(1.0e-9, view.secondsPerUnit);
    // What was there over the pasted span gives way.
    const auto end = at + copied.back().seconds / unit;
    std::erase_if(points, [&](const Point& point) { return point.time >= at - 1.0e-9 && point.time <= end + 1.0e-9; });
    selected_.clear();
    const auto span = scaleSpan(lane);
    for (const auto& item : copied) {
        Point point { freshId(points), std::clamp(at + item.seconds / unit, view.minimumTime, view.maximumTime), valueOf(lane, item.part),
                      engine::clampedCurveShape(item.shape), item.bend };
        point.handles = { static_cast<float>(item.inSeconds / unit), static_cast<float>(item.inPart * span),
                          static_cast<float>(item.outSeconds / unit), static_cast<float>(item.outPart * span) };
        points.push_back(point);
        selected_.insert(point.id);
    }
    sortByTime(points);
    return true;
}

bool Editor::removeSelected(std::vector<Point>& points)
{
    const auto before = points.size();
    std::erase_if(points, [this](const Point& point) { return selected_.contains(point.id); });
    selected_.clear();
    return points.size() != before;
}

bool Editor::setShape(std::vector<Point>& points, const Lane& lane, const int shape)
{
    sortByTime(points);
    auto changed = false;
    for (std::size_t index = 0; index < points.size(); ++index) {
        auto& point = points[index];
        if (!selected_.contains(point.id) || point.shape == shape)
            continue;
        const auto was = point.shape;
        if (isBezier(shape) && !isBezier(was)) {
            // A Bezier point starts smooth: its handles along its
            // neighbours' line.
            point.handles = engine::smoothHandles(
                points.size(), [&points](const std::size_t at) { return knotOf(points[at]); }, index, onLogScale(lane));
        } else if (shape == static_cast<int>(CurveShape::tangents) && was == static_cast<int>(CurveShape::tangent)) {
            // Its one tangent, now a handle on each side.
            point.handles.inTime = point.handles.outTime;
            point.handles.inValue = -point.handles.outValue;
        }
        if (shape == static_cast<int>(CurveShape::bend) && std::abs(point.bend) < 0.02f)
            point.bend = 0.5f;
        point.shape = shape;
        changed = true;
    }
    return changed;
}

std::vector<Point> Editor::shownPoints(const std::vector<Point>& points, const Lane& lane) const
{
    if (dragging_ && moved_) {
        auto shown = before_;
        for (auto& point : shown)
            if (selected_.contains(point.id)) {
                point.time += dragTime_;
                point.value = valueOf(lane, partOf(lane, point.value) + dragPart_);
            }
        sortByTime(shown);
        return shown;
    }
    auto shown = points;
    if (bending_)
        for (auto& point : shown)
            if (point.id == bendPoint_) {
                point.shape = std::abs(bendNow_) < 0.02f ? static_cast<int>(CurveShape::straight) : static_cast<int>(CurveShape::bend);
                point.bend = std::abs(bendNow_) < 0.02f ? 0.0f : bendNow_;
            }
    if (handleSide_ != 0)
        for (auto& point : shown)
            if (point.id == handlePoint_)
                point.handles = handleNow_;
    return shown;
}

bool Editor::draw(const char* id, const ImVec2 min, const ImVec2 max, std::vector<Point>& points, const Lane& lane, const View& view)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto& io = ImGui::GetIO();
    const auto mouse = io.MousePos;
    const auto pad = dp(5.0f);
    const auto tall = std::max(1.0f, max.y - min.y - pad * 2.0f);
    const auto yOf = [&](const double value) { return max.y - pad - static_cast<float>(partOf(lane, value)) * tall; };
    const auto partAt = [&](const float y) { return std::clamp(static_cast<double>(max.y - pad - y) / tall, 0.0, 1.0); };
    sortByTime(points);
    std::erase_if(selected_, [&points](const std::uint64_t picked) {
        return std::none_of(points.begin(), points.end(), [picked](const Point& point) { return point.id == picked; });
    });
    const auto shown = shownPoints(points, lane);
    const auto colour = lane.colour != 0 ? lane.colour : p.modulation;
    // A Bezier point's handle ends on screen.
    const auto handleEnds = [&](const Point& point, ImVec2& before, ImVec2& after) {
        const auto value = scaled(lane, point.value);
        const auto& h = point.handles;
        const auto tangent = point.shape == static_cast<int>(CurveShape::tangent);
        const auto inTime = tangent ? h.outTime : h.inTime;
        const auto inValue = tangent ? -h.outValue : h.inValue;
        before = ImVec2(view.xOf(point.time - std::max(0.0f, inTime)), yOf(unscaled(lane, value + inValue)));
        after = ImVec2(view.xOf(point.time + std::max(0.0f, h.outTime)), yOf(unscaled(lane, value + h.outValue)));
    };

    list->PushClipRect(min, max, true);
    // The curve as the engine plays it, sampled across the lane.
    {
        std::vector<ImVec2> line;
        const auto step = dp(2.0f);
        line.reserve(static_cast<std::size_t>((max.x - min.x) / step) + 2);
        for (auto x = min.x; x < max.x + step; x += step) {
            const auto at = std::min(x, max.x);
            line.emplace_back(at, yOf(valueAt(shown, lane, view.timeAt(at))));
        }
        if (lane.filled)
            for (std::size_t index = 1; index < line.size(); ++index)
                list->AddQuadFilled(line[index - 1], line[index], ImVec2(line[index].x, max.y), ImVec2(line[index - 1].x, max.y),
                                    theme::alpha(colour, 0.18f));
        list->AddPolyline(line.data(), static_cast<int>(line.size()), shown.empty() ? theme::alpha(colour, 0.35f) : colour,
                          shown.empty() ? dp(1.0f) : dp(1.5f));
    }
    // A B-spline's control polygon, faint: its points pull the curve.
    for (std::size_t index = 1; index < shown.size(); ++index)
        if (shown[index - 1].shape == static_cast<int>(CurveShape::spline) || shown[index].shape == static_cast<int>(CurveShape::spline))
            list->AddLine(ImVec2(view.xOf(shown[index - 1].time), yOf(shown[index - 1].value)),
                          ImVec2(view.xOf(shown[index].time), yOf(shown[index].value)), theme::alpha(colour, 0.3f), dp(1.0f));
    // The picked Bezier points' handles.
    std::optional<std::pair<std::uint64_t, int>> hoveredHandle;
    for (const auto& point : shown) {
        if (!selected_.contains(point.id) || !isBezier(point.shape))
            continue;
        const ImVec2 at(view.xOf(point.time), yOf(point.value));
        ImVec2 before, after;
        handleEnds(point, before, after);
        for (const auto& [end, side] : { std::pair { before, -1 }, std::pair { after, 1 } }) {
            const auto near = std::abs(mouse.x - end.x) <= dp(5.0f) && std::abs(mouse.y - end.y) <= dp(5.0f);
            if (near && !framing_ && !dragging_)
                hoveredHandle = std::pair { point.id, side };
            const auto active = handleSide_ == side && handlePoint_ == point.id;
            list->AddLine(at, end, theme::alpha(p.text, 0.55f), dp(1.0f));
            list->AddCircleFilled(end, dp(near || active ? 4.0f : 3.0f), near || active ? p.text : theme::alpha(p.text, 0.7f), 10);
        }
    }
    std::optional<std::uint64_t> hovered;
    for (const auto& point : shown) {
        const ImVec2 at(view.xOf(point.time), yOf(point.value));
        const auto near = std::abs(mouse.x - at.x) <= dp(6.0f) && std::abs(mouse.y - at.y) <= dp(6.0f);
        if (near && !framing_ && !hoveredHandle)
            hovered = point.id;
        const auto picked = selected_.contains(point.id);
        const auto radius = picked ? dp(5.0f) : dp(near ? 4.5f : 3.5f);
        if (point.shape == static_cast<int>(CurveShape::spline)) {
            // A B-spline's point: a diamond, off the curve.
            const ImVec2 corners[] = { ImVec2(at.x, at.y - radius), ImVec2(at.x + radius, at.y), ImVec2(at.x, at.y + radius),
                                       ImVec2(at.x - radius, at.y) };
            list->AddConvexPolyFilled(corners, 4, picked || near ? p.text : theme::alpha(colour, 0.85f));
            if (picked)
                list->AddPolyline(corners, 4, colour, dp(1.5f), ImDrawFlags_Closed);
        } else if (picked) {
            list->AddCircleFilled(at, radius, p.text, 12);
            list->AddCircle(at, radius, colour, 12, dp(1.5f));
        } else {
            list->AddCircleFilled(at, radius, near ? p.text : colour, 12);
        }
    }
    if (framing_) {
        const ImVec2 a(std::min(frameFrom_.x, mouse.x), std::min(frameFrom_.y, mouse.y));
        const ImVec2 b(std::max(frameFrom_.x, mouse.x), std::max(frameFrom_.y, mouse.y));
        list->AddRectFilled(a, b, theme::alpha(p.accent, 0.12f));
        list->AddRect(a, b, theme::alpha(p.accent, 0.7f));
    }
    if (focused_)
        list->AddRect(min, max, theme::alpha(colour, 0.5f));
    list->PopClipRect();

    // Input: its own item, or the host's pointer.
    ImGui::PushID(id);
    bool laneHovered {};
    bool activated {};
    bool active {};
    bool deactivated {};
    if (!view.hostInput) {
        ImGui::SetCursorScreenPos(min);
        ImGui::InvisibleButton("##curveLane", ImVec2(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y)),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        laneHovered = ImGui::IsItemHovered();
        activated = ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        active = ImGui::IsItemActive();
        deactivated = ImGui::IsItemDeactivated();
    } else {
        laneHovered = (view.hostHovered || pressed_) && ImGui::IsMouseHoveringRect(min, max, false);
        activated = laneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (activated)
            pressed_ = true;
        active = pressed_ && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        deactivated = pressed_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (deactivated)
            pressed_ = false;
    }
    const auto here = view.timeAt(mouse.x);
    const auto clampTime = [&view](const double time) { return std::clamp(time, view.minimumTime, view.maximumTime); };
    auto changed = false;
    if (laneHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !hoveredHandle) {
        if (hovered) {
            // A double click on a point removes it.
            std::erase_if(points, [&](const Point& point) { return point.id == *hovered; });
            selected_.erase(*hovered);
        } else {
            // A point where the double click is.
            const Point point { freshId(points), clampTime(view.snap(here)), valueOf(lane, partAt(mouse.y)) };
            points.push_back(point);
            selected_ = { point.id };
        }
        dragging_ = false;
        framing_ = false;
        focused_ = true;
        changed = true;
    } else if (activated) {
        focused_ = true;
        if (hoveredHandle) {
            // A handle taken: its point's, on its side.
            handlePoint_ = hoveredHandle->first;
            handleSide_ = hoveredHandle->second;
            for (const auto& point : points)
                if (point.id == handlePoint_)
                    handleNow_ = point.handles;
        } else if (hovered) {
            if (io.KeyCtrl) {
                if (!selected_.erase(*hovered))
                    selected_.insert(*hovered);
            } else if (io.KeyShift) {
                selected_.insert(*hovered);
            } else if (!selected_.contains(*hovered)) {
                selected_ = { *hovered };
            }
            if (selected_.contains(*hovered)) {
                // The picked ones go together; the one taken lands on the grid.
                dragging_ = true;
                moved_ = false;
                before_ = points;
                for (const auto& point : points)
                    if (point.id == *hovered)
                        grabTime_ = point.time;
                grabPart_ = partAt(mouse.y);
                dragTime_ = 0.0;
                dragPart_ = 0.0;
                dragFrom_ = here;
            }
        } else if (io.KeyAlt && points.size() >= 2 && here >= points.front().time && here < points.back().time) {
            // Alt+drag between two points: the segment bent.
            for (std::size_t index = 0; index + 1 < points.size(); ++index)
                if (points[index].time <= here && here < points[index + 1].time) {
                    bending_ = true;
                    bendPoint_ = points[index].id;
                    bendFrom_ = points[index].shape == static_cast<int>(CurveShape::bend) ? points[index].bend : 0.0f;
                    bendNow_ = bendFrom_;
                    bendY_ = mouse.y;
                    // A segment rising on screen bends up with the pointer,
                    // a falling one too.
                    bendDirection_ = yOf(points[index + 1].value) > yOf(points[index].value) ? -1.0f : 1.0f;
                    selected_ = { points[index].id };
                    break;
                }
        } else {
            framing_ = true;
            frameFrom_ = mouse;
            if (!io.KeyCtrl && !io.KeyShift)
                selected_.clear();
        }
    }
    if (active && dragging_) {
        dragTime_ = clampTime(view.snap(grabTime_ + (here - dragFrom_))) - grabTime_;
        dragPart_ = partAt(mouse.y) - grabPart_;
        moved_ = moved_ || std::abs(dragTime_) > 1.0e-9 || std::abs(dragPart_) > 1.0e-4;
    }
    if (active && bending_) {
        bendNow_ = std::clamp(bendFrom_ + bendDirection_ * (bendY_ - mouse.y) / tall * 2.0f, -1.0f, 1.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (active && handleSide_ != 0)
        for (const auto& point : points)
            if (point.id == handlePoint_) {
                // Where the pointer is, from the point: the handle's reach.
                const auto value = scaled(lane, valueOf(lane, partAt(mouse.y))) - scaled(lane, point.value);
                const auto reach = static_cast<float>(handleSide_ > 0 ? std::max(0.0, here - point.time) : std::max(0.0, point.time - here));
                if (point.shape == static_cast<int>(CurveShape::tangent)) {
                    handleNow_.outTime = reach;
                    handleNow_.outValue = static_cast<float>(handleSide_ > 0 ? value : -value);
                } else if (handleSide_ > 0) {
                    handleNow_.outTime = reach;
                    handleNow_.outValue = static_cast<float>(value);
                } else {
                    handleNow_.inTime = reach;
                    handleNow_.inValue = static_cast<float>(value);
                }
            }
    if (deactivated) {
        if ((dragging_ && moved_) || bending_ || handleSide_ != 0) {
            points = shownPoints(points, lane);
            changed = true;
        }
        dragging_ = false;
        moved_ = false;
        bending_ = false;
        handleSide_ = 0;
        if (framing_) {
            framing_ = false;
            const ImVec2 a(std::min(frameFrom_.x, mouse.x), std::min(frameFrom_.y, mouse.y));
            const ImVec2 b(std::max(frameFrom_.x, mouse.x), std::max(frameFrom_.y, mouse.y));
            for (const auto& point : points) {
                const ImVec2 at(view.xOf(point.time), yOf(point.value));
                if (at.x >= a.x && at.x <= b.x && at.y >= a.y && at.y <= b.y)
                    selected_.insert(point.id);
            }
        }
    }
    if (laneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        focused_ = true;
        if (hovered && !selected_.contains(*hovered))
            selected_ = { *hovered };
        menuTime_ = clampTime(view.snap(here));
        ImGui::OpenPopup("##curveMenu");
    }
    if (ImGui::BeginPopup("##curveMenu")) {
        if (ImGui::BeginMenu(L("Forme", "Shape"), !selected_.empty())) {
            for (auto shape = 0; shape < engine::curveShapeCount; ++shape)
                if (ImGui::MenuItem(shapeName(shape)))
                    changed |= setShape(points, lane, shape);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Copier", "Copy"), "Ctrl+C", false, !selected_.empty()))
            (void) copy(points, lane, view);
        if (ImGui::MenuItem(L("Coller ici", "Paste here"), "Ctrl+V", false, !clipboard().empty()))
            changed |= paste(points, lane, view, menuTime_);
        if (ImGui::MenuItem(L("Supprimer", "Delete"), L("Suppr", "Delete"), false, !selected_.empty()))
            changed |= removeSelected(points);
        ImGui::Separator();
        if (ImGui::MenuItem(L("Tout sélectionner", "Select all"), "Ctrl+A"))
            for (const auto& point : points)
                selected_.insert(point.id);
        if (view.menu) {
            ImGui::Separator();
            view.menu();
        }
        ImGui::EndPopup();
    }
    // Its keys, while it is the lane last clicked.
    if (focused_ && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
            changed |= removeSelected(points);
        else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
            (void) copy(points, lane, view);
        else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
            changed |= paste(points, lane, view, clampTime(view.snap(view.cursor)));
        else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
            for (const auto& point : points)
                selected_.insert(point.id);
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            selected_.clear();
    }
    const auto valueText = [&lane](const double value) {
        return lane.formatValue ? lane.formatValue(value) : juce::String(value, 3);
    };
    if (laneHovered && !ImGui::IsPopupOpen("##curveMenu")) {
        if (hoveredHandle) {
            ui::tooltip(L("Poignée : glisser pour courber", "Handle: drag to shape"));
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        } else if (hovered) {
            for (const auto& point : shown)
                if (point.id == *hovered) {
                    const auto text = valueText(point.value) + "   " + (lane.formatTime ? lane.formatTime(point.time) : juce::String(point.time, 3))
                                      + "   " + shapeName(point.shape);
                    ui::tooltip(text.toRawUTF8());
                }
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        } else if (!dragging_ && !framing_ && !bending_ && handleSide_ == 0) {
            ui::tooltip(L("Double-clic : un point (sur un point : le retirer) · clic : le choisir (Ctrl : un de plus) · glisser dans le vide : encadrer · "
                          "glisser : déplacer les points choisis (Alt : sans aimant) · Alt+glisser entre deux points : courber · Suppr · Ctrl+C / Ctrl+V · "
                          "clic droit : forme, menu",
                          "Double click: a point (on a point: remove it) · click: pick it (Ctrl: one more) · drag in the empty lane: frame · drag: move "
                          "the picked points (Alt: no snap) · Alt+drag between two points: bend · Delete · Ctrl+C / Ctrl+V · right click: shape, menu"));
        }
    }
    if (dragging_ && moved_)
        for (const auto& point : shown)
            if (selected_.size() == 1 && selected_.contains(point.id))
                ui::tooltip(valueText(point.value).toRawUTF8());
    ImGui::PopID();
    if (changed)
        sortByTime(points);
    return changed;
}

std::optional<double> Editor::numberField(const char* id, const int field, const std::uint64_t point, const double current, const float speed,
                                          const double* minimum, const double* maximum, const char* format, const float width)
{
    auto value = editField_ == field && editPoint_ == point ? editBuffer_ : current;
    ImGui::SetNextItemWidth(width);
    ImGui::DragScalar(id, ImGuiDataType_Double, &value, speed, minimum, maximum, format);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        editField_ = -1;
        if (minimum != nullptr)
            value = std::max(value, *minimum);
        if (maximum != nullptr)
            value = std::min(value, *maximum);
        return value;
    }
    if (ImGui::IsItemActive()) {
        editField_ = field;
        editPoint_ = point;
        editBuffer_ = value;
    } else if (editField_ == field && editPoint_ == point) {
        editField_ = -1;
    }
    return std::nullopt;
}

bool Editor::shapeButtons(std::vector<Point>& points, const Lane& lane)
{
    const auto& p = theme::palette();
    // The picked points' shape, when they share one.
    std::optional<int> shared;
    auto mixed = false;
    for (const auto& point : points)
        if (selected_.contains(point.id)) {
            if (shared && *shared != point.shape)
                mixed = true;
            shared = point.shape;
        }
    auto changed = false;
    static const char* const tips[][2] = {
        { "Droit : droit jusqu'au point suivant", "Straight: straight to the next point" },
        { "Courbe : le segment suivant courbé (Alt+glisser sur le segment)", "Bent: the next segment bent (Alt+drag on the segment)" },
        { "B-Spline : la courbe passe près du point, lisse, tirée par lui et ses voisins",
          "B-Spline: the curve passes near the point, smooth, pulled by it and its neighbours" },
        { "Bézier : une tangente, deux poignées alignées de même longueur", "Bezier: one tangent, two handles in a line, the same length" },
        { "Bézier libre : une poignée libre de chaque côté", "Free Bezier: a free handle on each side" },
    };
    for (auto shape = 0; shape < engine::curveShapeCount; ++shape) {
        ImGui::SameLine(0.0f, shape == 0 ? dp(4.0f) : dp(2.0f));
        auto on = !mixed && shared && *shared == shape;
        ImGui::PushID(shape);
        if (ui::pictureToggle("##shape", &on, p.accent, shapePictures[shape], L(tips[shape][0], tips[shape][1])))
            changed |= setShape(points, lane, shape);
        ImGui::PopID();
    }
    return changed;
}

bool Editor::drawBar(std::vector<Point>& points, const Lane& lane, const double gridStep)
{
    const auto& p = theme::palette();
    if (selected_.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", L("Aucun point choisi : cliquez un point, ou encadrez-en plusieurs.",
                                    "No point picked: click a point, or frame several."));
        return false;
    }
    auto changed = false;
    const auto speed = static_cast<float>(std::abs(lane.maximum - lane.minimum) / 400.0);
    const auto minimum = std::min(lane.minimum, lane.maximum);
    const auto maximum = std::max(lane.minimum, lane.maximum);
    const auto valueText = [&lane](const double value) {
        return lane.formatValue ? lane.formatValue(value) : juce::String(value, 3);
    };
    if (selected_.size() == 1) {
        // One point: its shape, its time and its value, typed or dragged.
        auto found = std::find_if(points.begin(), points.end(), [this](const Point& point) { return selected_.contains(point.id); });
        if (found == points.end())
            return false;
        const auto pointId = found->id;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Point", "Point"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", (lane.formatTime ? lane.formatTime(found->time) : juce::String(found->time, 3)).toRawUTF8());
        ImGui::SameLine();
        if (const auto time = numberField("##pointTime", 0, pointId, found->time, 0.005f, nullptr, nullptr, "%.3f", dp(84.0f))) {
            found->time = *time;
            changed = true;
        }
        ui::tooltip(L("Son temps : glisser, ou Ctrl+clic pour le taper", "Its time: drag, or Ctrl+click to type it"));
        ImGui::SameLine(0.0f, dp(10.0f));
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Valeur", "Value"));
        ImGui::SameLine();
        if (const auto value = numberField("##pointValue", 1, pointId, found->value, speed, &minimum, &maximum, "%.3f", dp(96.0f))) {
            found->value = *value;
            changed = true;
        }
        ui::tooltip(L("Sa valeur : glisser, ou Ctrl+clic pour la taper", "Its value: drag, or Ctrl+click to type it"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", valueText(editField_ == 1 && editPoint_ == pointId ? editBuffer_ : found->value).toRawUTF8());
        ImGui::SameLine(0.0f, dp(12.0f));
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Forme", "Shape"));
        changed |= shapeButtons(points, lane);
        found = std::find_if(points.begin(), points.end(), [pointId](const Point& point) { return point.id == pointId; });
        if (found == points.end())
            return changed;
        // Its bend, or its handles.
        if (found->shape == static_cast<int>(CurveShape::bend)) {
            ImGui::SameLine(0.0f, dp(10.0f));
            static constexpr double low = -1.0;
            static constexpr double high = 1.0;
            if (const auto bend = numberField("##pointBend", 2, pointId, found->bend, 0.01f, &low, &high, L("courbure %+.2f", "bend %+.2f"),
                                              dp(110.0f))) {
                found->bend = static_cast<float>(*bend);
                changed = true;
            }
            ui::tooltip(L("La courbure du segment suivant (-1..1)", "The next segment's bend (-1..1)"));
        } else if (isBezier(found->shape)) {
            const auto tangents = found->shape == static_cast<int>(CurveShape::tangents);
            static constexpr double none = 0.0;
            const auto handle = [&](const char* label, const int field, float& time, float& value) {
                ImGui::SameLine(0.0f, dp(10.0f));
                ImGui::TextColored(theme::vec(p.textDim), "%s", label);
                ImGui::SameLine();
                ImGui::PushID(field);
                if (const auto reach = numberField("##reach", field, pointId, time, 0.005f, &none, nullptr, "t %.3f", dp(70.0f))) {
                    time = static_cast<float>(*reach);
                    changed = true;
                }
                ui::tooltip(L("La longueur de la poignée, en temps", "The handle's length, in time"));
                ImGui::SameLine(0.0f, dp(2.0f));
                if (const auto rise = numberField("##rise", field + 1, pointId, value, speed, nullptr, nullptr,
                                                  onLogScale(lane) ? "log %+.3f" : "%+.3f", dp(80.0f))) {
                    value = static_cast<float>(*rise);
                    changed = true;
                }
                ui::tooltip(L("Sa hauteur, depuis le point", "Its height, from the point"));
                ImGui::PopID();
            };
            if (tangents)
                handle(L("Avant", "Before"), 4, found->handles.inTime, found->handles.inValue);
            handle(tangents ? L("Après", "After") : L("Tangente", "Tangent"), 6, found->handles.outTime, found->handles.outValue);
            ImGui::SameLine(0.0f, dp(6.0f));
            if (ImGui::SmallButton(L("Lisser", "Smooth"))) {
                sortByTime(points);
                const auto index = static_cast<std::size_t>(std::distance(
                    points.begin(), std::find_if(points.begin(), points.end(), [pointId](const Point& point) { return point.id == pointId; })));
                points[index].handles = engine::smoothHandles(
                    points.size(), [&points](const std::size_t at) { return knotOf(points[at]); }, index, onLogScale(lane));
                changed = true;
            }
            ui::tooltip(L("Les poignées le long de la ligne de ses voisins", "The handles along its neighbours' line"));
        }
        if (changed)
            sortByTime(points);
        return changed;
    }
    // Several: their shape, and Bitwig's multi-edit.
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%d %s", static_cast<int>(selected_.size()), L("points", "points"));
    ImGui::SameLine(0.0f, dp(8.0f));
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Forme", "Shape"));
    changed |= shapeButtons(points, lane);
    ImGui::SameLine(0.0f, dp(12.0f));
    std::vector<Point*> picked;
    for (auto& point : points)
        if (selected_.contains(point.id))
            picked.push_back(&point);
    std::sort(picked.begin(), picked.end(), [](const Point* a, const Point* b) { return a->time < b->time; });
    // Moved together: a value added to all, a time added to all.
    ImGui::SetNextItemWidth(dp(80.0f));
    ImGui::DragFloat("##shiftValue", &valueShift_, 0.002f, -1.0f, 1.0f, L("valeur %+.0f%%", "value %+.0f%%"), ImGuiSliderFlags_None);
    ui::tooltip(L("Monte ou baisse tous les points (en part de l'étendue) ; appliqué en lâchant",
                  "Raises or lowers every point (as a part of the range); applied when let go"));
    if (ImGui::IsItemDeactivatedAfterEdit() && std::abs(valueShift_) > 1.0e-6) {
        for (auto* point : picked)
            point->value = valueOf(lane, partOf(lane, point->value) + valueShift_);
        valueShift_ = 0.0f;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(80.0f));
    ImGui::DragFloat("##shiftTime", &timeShift_, 0.005f, -1000.0f, 1000.0f, L("temps %+.3f", "time %+.3f"));
    ui::tooltip(L("Déplace tous les points dans le temps ; appliqué en lâchant", "Moves every point in time; applied when let go"));
    if (ImGui::IsItemDeactivatedAfterEdit() && std::abs(timeShift_) > 1.0e-9) {
        for (auto* point : picked)
            point->time += timeShift_;
        timeShift_ = 0.0f;
        changed = true;
    }
    ImGui::SameLine(0.0f, dp(10.0f));
    // Their spread around their middle.
    auto mean = 0.0;
    for (const auto* point : picked)
        mean += partOf(lane, point->value);
    mean /= static_cast<double>(std::max<std::size_t>(1, picked.size()));
    ImGui::SetNextItemWidth(dp(64.0f));
    ImGui::DragFloat("##scale", &scaleAmount_, 0.01f, 0.0f, 4.0f, "x%.2f");
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ImGui::SmallButton(L("Échelle", "Scale"))) {
        for (auto* point : picked)
            point->value = valueOf(lane, mean + (partOf(lane, point->value) - mean) * scaleAmount_);
        changed = true;
    }
    ui::tooltip(L("Écarte ou resserre les valeurs autour de leur milieu", "Spreads or narrows the values around their middle"));
    ImGui::SameLine(0.0f, dp(8.0f));
    ImGui::SetNextItemWidth(dp(56.0f));
    ImGui::DragFloat("##random", &randomAmount_, 0.005f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_None);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ImGui::SmallButton(L("Hasard", "Random"))) {
        auto& random = juce::Random::getSystemRandom();
        for (auto* point : picked)
            point->value = valueOf(lane, partOf(lane, point->value) + (random.nextDouble() * 2.0 - 1.0) * randomAmount_);
        changed = true;
    }
    ui::tooltip(L("Ajoute à chaque point un écart au hasard (en part de l'étendue)", "Adds a random offset to each point (as a part of the range)"));
    ImGui::SameLine(0.0f, dp(8.0f));
    ImGui::SetNextItemWidth(dp(56.0f));
    ImGui::DragFloat("##tilt", &tiltAmount_, 0.005f, -1.0f, 1.0f, "%+.2f", ImGuiSliderFlags_None);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ImGui::SmallButton(L("Rampe", "Tilt"))) {
        const auto first = picked.front()->time;
        const auto span = std::max(1.0e-9, picked.back()->time - first);
        for (auto* point : picked)
            point->value = valueOf(lane, partOf(lane, point->value) + tiltAmount_ * ((point->time - first) / span * 2.0 - 1.0));
        changed = true;
    }
    ui::tooltip(L("Incline les valeurs : le début baisse, la fin monte (ou l'inverse)", "Tilts the values: the start goes down, the end up (or the other way)"));
    ImGui::SameLine(0.0f, dp(8.0f));
    if (ImGui::SmallButton(L("Inverser", "Flip"))) {
        for (auto* point : picked)
            point->value = valueOf(lane, 2.0 * mean - partOf(lane, point->value));
        changed = true;
    }
    ui::tooltip(L("Le haut devient le bas, autour de leur milieu", "Top becomes bottom, around their middle"));
    ImGui::SameLine(0.0f, dp(4.0f));
    if (ImGui::SmallButton(L("Sur la grille", "On the grid")) && gridStep > 0.0) {
        for (auto* point : picked)
            point->time = std::round(point->time / gridStep) * gridStep;
        changed = true;
    }
    ui::tooltip(L("Pose chaque point sur la ligne de grille la plus proche", "Puts each point on the nearest grid line"));
    if (changed) {
        // Values that came back to a whole step of the range read clean.
        const auto grain = std::abs(lane.maximum - lane.minimum) * 1.0e-9;
        for (auto& point : points)
            if (std::abs(point.value - std::round(point.value)) < grain)
                point.value = std::round(point.value);
        sortByTime(points);
    }
    return changed;
}

} // namespace cydaw::v2::curves
