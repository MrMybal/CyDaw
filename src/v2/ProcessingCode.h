#pragma once

#include <cydaw/engine/AudioRepair.h>

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

// The code a processing is written in: JSFX for sound (REAPER's effect
// language, run offline here through ysfx), and note scripts for MIDI clips
// in EEL2 (JSFX's own language) over a clip's notes.
//
// A note script:
//
//     param1:20<0,100,1>Timing (ms)        // up to 16 settings: param1..param16
//     param2:10<0,50,1>Velocity (%)
//     @init                                // once, before the notes
//     @note                                // each note, in order of start
//     start += gauss() * ms_to_beats(param1) / 3;
//     velocity *= 1 + (random() * 2 - 1) * param2 / 100;
//     @end                                 // once, after them
//
// Per note: pitch (0..127), start and length (beats from the clip's start),
// velocity and chance (0..1), index, keep (set to 0: the note goes); around
// it: count, tempo, clip_length, prev_start, next_start (the starts before and
// after its own; -1 when none), prev_pitch, next_pitch. Functions: random()
// (0..1), gauss() (mean 0, deviation 1), ms_to_beats(ms), note_start(i),
// note_pitch(i), note_length(i), note_velocity(i) (the notes as they were),
// add_note(pitch, start, length, velocity). The random draws follow the run's
// seed (the same seed, the same result).

namespace cydaw::v2::processing {

struct Setting {
    int index {};            // 0-based: param1 / slider1 is 0
    juce::String name;
    double minimum {};
    double maximum { 1.0 };
    double step {};
    double defaultValue {};
    juce::StringArray choices; // a JSFX slider's names, else empty
};

// ---- Note scripts ----

struct ScriptNote {
    int pitch { 60 };
    double start {};
    double length { 1.0 };
    float velocity { 0.8f };
    float chance { 1.0f };
    int source { -1 };       // the index of the note it came from (-1: added)
};

struct NoteScriptResult {
    bool ok {};
    juce::String error;
    std::vector<ScriptNote> notes;
};

// Its settings (the param lines) and whether it compiles (an error, else empty).
[[nodiscard]] std::vector<Setting> noteScriptSettings(const juce::String& source);
[[nodiscard]] juce::String checkNoteScript(const juce::String& source);
[[nodiscard]] NoteScriptResult runNoteScript(const juce::String& source, std::vector<ScriptNote> notes,
                                             const std::map<int, double>& values, double tempo, double clipLength,
                                             std::uint32_t seed);

// ---- JSFX ----

struct JsfxInfo {
    bool ok {};
    juce::String error;
    juce::String name;
    std::vector<Setting> sliders;
};
[[nodiscard]] JsfxInfo inspectJsfx(const juce::String& source);
// The sound through the script (stereo in and out), `frames` long (past the
// input: silence fed in, a tail let ring); its delay taken off.
[[nodiscard]] juce::Result renderJsfx(const juce::String& source, const std::map<int, double>& values,
                                      engine::repair::Channels& channels, double rate, std::int64_t frames,
                                      const std::atomic<bool>* cancel = nullptr);

} // namespace cydaw::v2::processing
