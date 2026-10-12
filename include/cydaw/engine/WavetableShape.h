#pragma once

#include <array>
#include <cstddef>
#include <memory>

namespace cydaw::engine {

// A wavetable, stored as the amplitudes of its harmonics — one set per frame.
//
// Storing the harmonics rather than the samples is what keeps every frame
// band-limited no matter what is drawn into it: a shape drawn with a mouse has
// corners, and corners are infinite harmonics. It is also what makes a table
// small enough to sit in a project file — three hundred and twenty numbers
// instead of sixteen thousand.
//
// The rendered form is a separate type, built once off the audio thread and
// shared by pointer, because the render plan is copied per voice per block.
struct WavetableShape final {
    static constexpr std::size_t frameCount = 8;
    // The built-in bank's brightest frame holds forty harmonics, so an edited
    // table can be seeded from it without losing anything.
    static constexpr std::size_t harmonicCount = 40;

    using HarmonicRow = std::array<float, harmonicCount>;
    std::array<HarmonicRow, frameCount> harmonics {};

    [[nodiscard]] bool operator==(const WavetableShape&) const = default;
};

// One cycle per frame, ready to read. Sixty-four kilobytes, so it is never
// copied: the graph hands out a pointer to one of these and the voices read
// through it.
struct WavetableBank final {
    static constexpr std::size_t frameCount = WavetableShape::frameCount;
    static constexpr std::size_t frameLength = 2'048;

    std::array<std::array<float, frameLength>, frameCount> frames {};
};

using WavetableBankRef = std::shared_ptr<const WavetableBank>;

// The bank every project has been using: frame n holds the first
// harmonics[n] partials of a sawtooth at 1/h, normalised. A table that has
// never been edited is exactly this, which is why an untouched project keeps
// the sound it had.
[[nodiscard]] WavetableShape defaultWavetableShape();

// Additive synthesis, once, off the audio thread.
[[nodiscard]] WavetableBankRef buildWavetableBank(const WavetableShape& shape);

// One frame's cycle at whatever resolution the caller draws at. The editor
// keeps what this returns and redraws from it, because forty sines per point
// is not something to do inside a paint.
void renderWavetableFrame(const WavetableShape::HarmonicRow& harmonics,
                          float* samples, std::size_t count);

// How a run of samples was read as a table, so the editor can say what it
// found rather than leaving the user to guess.
struct WavetableImport final {
    WavetableShape shape;
    std::size_t frameLength {};
    std::size_t framesFound {};
    bool singleCycle {};
    bool ok {};
};

// Turns a run of samples into a table.
//
// A wavetable file is a row of single cycles laid end to end — two thousand
// and forty-eight samples each by convention, but a thousand and twenty-four,
// five hundred and twelve and two hundred and fifty-six are all in the wild.
// Whichever of those divides the file into two or more cycles is what it is,
// and eight of those cycles are taken evenly across it, so a sixty-four frame
// table arrives as the sweep it was meant to be rather than its first eighth.
//
// A file that divides into nothing is one cycle. Repeating it eight times
// would leave the morph nothing to travel through, so the table is that one
// cycle at the rising brightness the built-in bank uses: the position knob
// then sweeps the imported timbre from its fundamental to all of it.
[[nodiscard]] WavetableImport wavetableShapeFromSamples(const float* samples,
                                                        std::size_t count);

// The harmonics of a drawn cycle: a real DFT of one period. What comes back
// is band-limited by construction, so drawing a square gives a usable square
// rather than a wall of aliasing.
[[nodiscard]] WavetableShape::HarmonicRow analyseWavetableFrame(
    const float* samples, std::size_t count);

} // namespace cydaw::engine
