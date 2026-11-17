#include "FramePacing.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

int main()
{
    using cydaw::v2::detail::frameDue;
    for (const int fps : {12, 30, 60, 120}) {
        double next = 0.0;
        int frames = 0;
        for (int ms = 0; ms < 10000; ++ms)
            frames += frameDue(ms * 0.001, fps, next) ? 1 : 0;
        require(std::abs(frames - fps * 10) <= 1, "Fractional pacing drifts at 1-ms polling");
        next = 0.0;
        frames = 0;
        int ms = 0;
        for (int poll = 0; ms < 10000; ++poll) {
            frames += frameDue(ms * 0.001, fps, next) ? 1 : 0;
            ms += poll % 97 == 0 ? 5 : 1;
        }
        require(std::abs(frames - fps * 10) <= 1, "Pacing drifts with uneven polls");
        require(frameDue(30.0, fps, next), "No frame after a long stall");
        require(!frameDue(30.0, fps, next), "Catch-up burst after a long stall");
        require(!frameDue(30.0 + 0.5 / fps, fps, next), "Missed frames were queued");
        require(frameDue(30.0 + 1.0 / fps, fps, next), "Deadline not reached after stall");
    }
    std::cout << "Frame pacing: 12/30/60/120 FPS, jitter and stall checks passed\n";
}
