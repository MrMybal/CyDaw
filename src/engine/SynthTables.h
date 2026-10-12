#pragma once

// The wavetables the synth family shares (Colosse, Mosaïque): eight sets
// (Classic, Harmonic, Voices, Metal, Folded, Digital, Teeth, Sync) of eight
// frames a position moves across, each made once without aliasing at eleven
// levels of harmonics (the n-th keeps 1024 >> n), read through eight ways of
// bending the phase (Plain, Bend, Mirror, Formant, Sync, Steps, Asymmetry,
// Fold). Internal to the engine.

namespace cydaw::engine::synth::detail::tables {

inline constexpr int size = 2'048;
inline constexpr int levels = 11;
inline constexpr int frames = 8;
inline constexpr int sets = 8;
inline constexpr int modes = 8;

// Makes the tables (once, off the audio thread: synth::warmUp). Until then a
// sample is silence.
void prepare();

// The phase bent by a mode, and what multiplies the sample (the formant's window).
struct Warp {
    double phase {};
    double gain { 1.0 };
};
[[nodiscard]] Warp warpPhase(int mode, double phase, double warp) noexcept;
// How much faster a mode makes the phase run (for the level chosen).
[[nodiscard]] double warpStretch(int mode, double warp) noexcept;
// The level for a phase increment (cycles a sample) run `stretch` faster.
[[nodiscard]] int levelFor(double increment, double stretch) noexcept;
// One sample: the frames either side of the position, at `level`, the phase bent.
[[nodiscard]] double sample(int set, double position, int mode, double warp, int level, double phase) noexcept;

} // namespace cydaw::engine::synth::detail::tables
