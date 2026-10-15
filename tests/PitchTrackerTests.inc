// The shared pitch tracker (PitchTracker.h): what the tuner, the vocoder's
// following carrier, Harmonie and Justesse hear. Included by EngineTests.cpp
// inside its anonymous namespace.

void pitchTrackerReadsInTune()
{
    for (const auto hz : { 41.2, 82.41, 220.0, 440.0, 988.0, 1'500.0 }) {
        cydaw::engine::PitchTracker tracker;
        tracker.prepare(48'000.0, 35.0, 1'600.0);
        auto phase = 0.0;
        auto heard = 0.0f;
        for (int sample = 0; sample < 48'000; ++sample) {
            phase += 2.0 * std::numbers::pi * hz / 48'000.0;
            if (tracker.push(static_cast<float>(0.5 * std::sin(phase))))
                heard = tracker.frequency();
        }
        const auto cents = heard > 0.0f ? 1'200.0 * std::log2(static_cast<double>(heard) / hz) : 1'000.0;
        expect(std::abs(cents) < 2.0, "pitch tracker: a sine of " + std::to_string(static_cast<int>(hz)) + " Hz reads within 2 cents");
    }
    // Silence is no pitch.
    cydaw::engine::PitchTracker tracker;
    tracker.prepare(48'000.0);
    for (int sample = 0; sample < 12'000; ++sample)
        (void) tracker.push(0.0f);
    expect(tracker.frequency() == 0.0f, "pitch tracker: silence has no pitch");
}
