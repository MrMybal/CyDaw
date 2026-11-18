#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace cydaw::engine {

// Beat positions are quarter notes. Audio time remains in seconds.
class TempoMap {
public:
    struct Point { double beat {}, bpm { 120.0 }; int numerator { 4 }, denominator { 4 }; };
    struct Measure { double start {}, end {}; int number { 1 }, numerator { 4 }, denominator { 4 }; };

    TempoMap() = default;
    TempoMap(Point initial, const std::vector<Point>& changes)
    {
        segments_.front().point = initial;
        for (const auto& point : changes) {
            const auto& previous = segments_.back();
            Segment next { point };
            next.seconds = previous.seconds + (point.beat - previous.point.beat) * 60.0 / previous.point.bpm;
            next.measureStart = previous.measureStart;
            next.measureNumber = previous.measureNumber;
            if (point.numerator != previous.point.numerator || point.denominator != previous.point.denominator) {
                next.measureStart = point.beat;
                next.measureNumber += static_cast<int>(std::ceil((point.beat - previous.measureStart) / width(previous.point) - 1.0e-9));
            }
            segments_.push_back(next);
        }
    }

    [[nodiscard]] double secondsAt(double beat) const noexcept
    {
        const auto& segment = forBeat(beat);
        return segment.seconds + (beat - segment.point.beat) * 60.0 / segment.point.bpm;
    }

    [[nodiscard]] double beatAt(double seconds) const noexcept
    {
        const auto it = std::ranges::upper_bound(segments_, seconds, {}, &Segment::seconds);
        const auto& segment = it == segments_.begin() ? segments_.front() : *std::prev(it);
        return segment.point.beat + (seconds - segment.seconds) * segment.point.bpm / 60.0;
    }

    [[nodiscard]] const Point& atBeat(double beat) const noexcept { return forBeat(beat).point; }

    [[nodiscard]] Measure measureAt(double beat) const noexcept
    {
        const auto& segment = forBeat(beat);
        const auto count = std::floor((beat - segment.measureStart) / width(segment.point) + 1.0e-9);
        const auto start = segment.measureStart + count * width(segment.point);
        auto end = start + width(segment.point);
        for (auto it = &segment + 1; it != segments_.data() + segments_.size() && it->point.beat < end; ++it)
            if (it->measureStart != segment.measureStart) {
                end = it->point.beat;
                break;
            }
        return { start, end, segment.measureNumber + static_cast<int>(count), segment.point.numerator, segment.point.denominator };
    }

private:
    struct Segment { Point point; double seconds {}, measureStart {}; int measureNumber { 1 }; };
    [[nodiscard]] static double width(const Point& p) noexcept { return 4.0 * p.numerator / p.denominator; }

    [[nodiscard]] const Segment& forBeat(double beat) const noexcept
    {
        const auto it = std::ranges::upper_bound(segments_, beat, {}, [](const Segment& s) { return s.point.beat; });
        return it == segments_.begin() ? segments_.front() : *std::prev(it);
    }

    std::vector<Segment> segments_ { Segment {} };
};

} // namespace cydaw::engine
