#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace cydaw::v2::timecode {
struct Code {
    int hours {}, minutes {}, seconds {}, frames {}, rate {};
};
inline double fps(int rate) { return rate == 0 ? 24.0 : rate == 1 ? 25.0 : rate == 2 ? 30000.0 / 1001.0 : 30.0; }
inline bool valid(const Code& c)
{
    const int nominal = c.rate == 0 ? 24 : c.rate == 1 ? 25 : 30;
    return c.rate >= 0 && c.rate < 4 && c.hours >= 0 && c.hours < 24 && c.minutes >= 0 && c.minutes < 60
        && c.seconds >= 0 && c.seconds < 60 && c.frames >= 0 && c.frames < nominal
        && !(c.rate == 2 && c.minutes % 10 != 0 && c.seconds == 0 && c.frames < 2);
}
inline double seconds(const Code& c)
{
    if (!valid(c))
        return -1.0;
    const auto minutes = c.hours * 60 + c.minutes;
    auto frames = ((c.hours * 3600 + c.minutes * 60 + c.seconds)
                      * (c.rate == 0        ? 24
                              : c.rate == 1 ? 25
                                            : 30))
        + c.frames;
    if (c.rate == 2)
        frames -= 2 * (minutes - minutes / 10);
    return frames / fps(c.rate);
}
inline Code fromSeconds(double seconds, int rate)
{
    const auto nominal = rate == 0 ? 24 : rate == 1 ? 25 : 30;
    auto frame = static_cast<std::int64_t>(std::floor(std::max(0.0, seconds) * fps(rate) + 1.0e-6));
    frame %= rate == 2 ? 2589408 : nominal * 86400;
    if (rate == 2) {
        const auto tenMinutes = frame / 17982;
        const auto remaining = frame % 17982;
        frame += 18 * tenMinutes + (remaining >= 2 ? 2 * ((remaining - 2) / 1798) : 0);
    }
    return { static_cast<int>(frame / (nominal * 3600)), static_cast<int>(frame / (nominal * 60) % 60),
        static_cast<int>(frame / nominal % 60), static_cast<int>(frame % nominal), rate };
}
inline std::array<int, 8> nibbles(const Code& c)
{
    return { c.frames & 15, c.frames >> 4, c.seconds & 15, c.seconds >> 4, c.minutes & 15, c.minutes >> 4, c.hours & 15,
        (c.hours >> 4) | (c.rate << 1) };
}
inline Code decode(const std::array<int, 8>& n)
{
    return { n[6] | ((n[7] & 1) << 4), n[4] | (n[5] << 4), n[2] | (n[3] << 4), n[0] | (n[1] << 4), (n[7] >> 1) & 3 };
}
}
