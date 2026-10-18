#pragma once

#include <cydaw/engine/SynthFamily.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace cydaw::engine::synth_match {

// A synth patch found for a recorded note, as Synplant's Genopatch does: the
// settings of one of CyDAW's own synths (Opale, subtractive; Quartz, FM) that
// make the closest sound, searched by playing candidates through the synth's
// very voice (what the track will play) and comparing them with the recording.
//
// The comparison: log band energies at two sizes (2048 and 512 points, a
// quarter overlap), the level every 10 ms, and how tonal each band is over a
// few stretches of the note (its harmonics' peaks above its floor: what band
// energies alone cannot tell from noise), the candidate first brought to the
// recording's level (that gain is part of the answer: the track's fader).
// The search: CMA-ES (Hansen) over the model's chosen settings, each scaled
// to 0..1 as its knob is; a second run from where the first ended, with a
// wider reach; candidates of a generation rendered on several threads.

struct Target {
    std::vector<float> samples; // mono, the note from its start
    double rate { 48'000.0 };
    int note { 60 };            // the key that plays it
    double cents {};            // how far from that key it is
    float velocity { 0.8f };
};

// A recording made a target: its silence trimmed, at most `seconds` kept, its
// pitch found (YIN) unless `note` is given (>= 0).
[[nodiscard]] Target prepareTarget(const std::vector<float>& mono, double rate, int note = -1, double seconds = 2.5);

struct Options {
    synth::Model model { synth::Model::opale };
    int evaluations { 1'600 };  // the candidates heard, all runs together
    unsigned seed { 1 };
    int threads { 0 };          // 0: the machine's cores but one
    // How far it is (0..1) and the best loss yet; false stops it.
    std::function<bool(double progress, double best)> progress;
    const std::atomic<bool>* cancel { nullptr };
};

struct Result {
    synth::Values values {};
    // The settings it chose, by key (what a track is given).
    std::vector<std::pair<std::string, float>> settings;
    double loss {};
    double gainDb {};           // the level that matches (for the track's fader)
    int evaluations {};
    bool cancelled {};
};

[[nodiscard]] Result match(const Target& target, const Options& options);

// The settings searched for a model (its keys); empty: a model it does not search.
[[nodiscard]] std::vector<std::string> searchedSettings(synth::Model model);

// A candidate's values rendered at the target's note: mono, `frames` long.
[[nodiscard]] std::vector<float> render(synth::Model model, const synth::Values& values, const Target& target, int frames);

// How far a rendering is from the target (and the gain that brings it closest).
struct Distance {
    double loss {};
    double gainDb {};
};
class Comparison final {
public:
    explicit Comparison(const Target& target);
    [[nodiscard]] Distance distance(const std::vector<float>& candidate) const;
    // The level over time alone, at the gain that matches it best: how far a
    // candidate's envelope is from the note's.
    [[nodiscard]] double envelopeLoss(const std::vector<float>& candidate) const;
    [[nodiscard]] int frames() const noexcept { return frames_; }

private:
    struct Scale {
        int size {};
        int hop {};
        std::vector<float> target; // frames x bins, natural log magnitude
        std::vector<float> weights; // per frame: how loud the note is there (its amplitude's square root)
        int columns {};
        int rows {};
        float floor {};
    };
    [[nodiscard]] static std::vector<float> logSpectrogram(const std::vector<float>& signal, int size, int hop, int& rows);
    [[nodiscard]] static std::vector<float> envelope(const std::vector<float>& signal, int hop);
    // Per stretch and band: its tonality (-1: a band too narrow to say), and
    // its level in `level`.
    [[nodiscard]] static std::vector<float> tonality(const std::vector<float>& signal, double rate, std::vector<float>& level);
    std::vector<Scale> scales_;
    std::vector<float> tonal_;
    std::vector<float> tonalLevel_;
    float tonalFloor_ {};
    double rate_ { 48'000.0 };
    std::vector<float> envelope_;
    std::vector<float> envelopeWeights_;
    int envelopeHop_ {};
    int frames_ {};
};

} // namespace cydaw::engine::synth_match
