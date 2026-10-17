#include "../src/v2/TransportTimecode.h"
#include <iostream>
#include <stdexcept>
using namespace cydaw::v2::timecode;
void require(bool ok)
{
    if (!ok)
        throw std::runtime_error("timecode check failed");
}
int main()
{
    for (int rate = 0; rate < 4; ++rate) {
        const auto count = rate == 2 ? 2589408 : static_cast<int>(fps(rate) * 86400);
        // Every code over a complete 24-hour cycle, including drop-frame minute boundaries.
        for (int i = 0; i < count; ++i) {
            const double t = i / fps(rate);
            auto c = fromSeconds(t, rate);
            if (!valid(c) || std::abs(seconds(c) - t) >= 0.0001) {
                std::cerr << rate << " " << i << " " << t << " " << c.hours << ":" << c.minutes << ":" << c.seconds
                          << ":" << c.frames << " -> " << seconds(c) << "\n";
                return 1;
            }
            auto d = decode(nibbles(c));
            require(std::abs(seconds(d) - t) < 0.0001);
        }
    }
    require(!valid({ 0, 1, 0, 0, 2 }));
    require(!valid({ 0, 1, 0, 1, 2 }));
    require(valid({ 0, 10, 0, 0, 2 }));
    require(!valid({ 24, 0, 0, 0, 1 }));
    require(!valid({ 0, 0, 0, 24, 0 }));
    require(std::abs(seconds({ 1, 0, 0, 0, 2 }) - 3599.9964) < 0.0001);
    std::cout << "timecode roundtrips: all frames of 24 hours at 24/25/29.97 drop/30 fps passed\n";
}
