#pragma once

#include <cydaw/engine/Sampler.h>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <map>
#include <memory>

// Sample maps: the zones of a sampler card ("cydaw-sampler" JSON, .cysampler
// files), SFZ files read and written, files dropped on a card mapped to keys,
// and the store that decodes the samples off the audio thread.
//
//   { "format": "cydaw-sampler", "version": 1, "name": "Steinway B",
//     "zones": [ { "file": "factory:Instruments/Steinway B/A0-v2.flac",
//                  "root": 21, "keys": [0, 22], "velocities": [1, 63],
//                  "tune": -3.5, "gain": 0, "pan": 0,
//                  "start": 0, "end": -1, "loop": [-1, -1], "loopCrossfade": 0,
//                  "sequence": [1, 1] } ],
//     "settings": { "velocity-crossfade": 1, ... } }
//
// A zone's file is absolute, "factory:" + a path under the factory content
// folder (what CyDAW ships), or "bank:" + a path under the sound banks'
// folder (the banks CyDAW downloads, "bank:vcsl/Keys/…"), so a project opens
// on another machine where CyDAW and its banks are elsewhere. Only "file" and
// "root" are required. "endFade" (frames) fades out a note cut from a longer
// recording. "settings" (optional) are the card's knobs the map was made for
// (an SFZ's one-shot, a factory instrument's crossfade).
//
// An instrument made from an SFZ keeps what moves it (Sampler.h): a zone's
// "trigger" ("release", "first", "legato"), "releaseDecay" (dB per second
// held), "keyswitch" (the key that chose it), "rules" ({"cc": 70, "level":
// 100}, {"cc": 4, "range": [0, 63]}, {"cc": 1, "fadeIn": [0, 64]}, "pan",
// "tune", "volume", "offset", and "curve": one of the map's); the map's
// "controllers" ({"70": 100}: where they start), "controls" ([{"cc": 70,
// "label": "Kick mic"}]: what a card shows), "curves" (128 values each),
// "keyswitches" ({"keys": [24, 26], "start": 24, "labels": {"24": "Sustain"}}).

namespace cydaw::app {

// ---- Paths ------------------------------------------------------------------------
// Where the factory content lives: "Factory" next to the executable, else the
// source tree's (a development build); an exported plug-in is told.
[[nodiscard]] juce::File factoryContentFolder();
void setFactoryContentFolder(const juce::File& folder);
// Where the downloaded sound banks are: as CyDAW was told (Options ›
// Emplacements; an exported plug-in, by its state), else CYDAW_BANKS_DIR,
// else %LOCALAPPDATA%/CyDAW/Banks.
[[nodiscard]] juce::File banksFolder();
void setBanksFolder(const juce::File& folder);
[[nodiscard]] juce::File resolveSamplePath(const juce::String& stored);
// "factory:…" for a file inside the factory folder, "bank:…" for one inside
// the banks' folder, else its full path.
[[nodiscard]] juce::String storedSamplePath(const juce::File& file);
// The bank a stored path is in ("vcsl" for "bank:vcsl/…"), else empty; and
// the path inside it.
[[nodiscard]] juce::String bankOfSamplePath(const juce::String& stored);
[[nodiscard]] juce::String pathInBank(const juce::String& stored);

// ---- The store ----------------------------------------------------------------------
// Every decoded file once, shared by the zones, cards and tracks that play
// it, freed when the last of them goes. Decoding runs on threads of its own;
// a sample is playable once its `ready` is set. Deleted when JUCE shuts down
// (an exported plug-in: when its last instance goes), not when a DLL is
// unloaded, where Windows lets no thread finish.
class SampleStore final : public juce::DeletedAtShutdown {
public:
    static SampleStore& instance();
    ~SampleStore() override;
    JUCE_DECLARE_SINGLETON(SampleStore, false)

    // `deferred`: not decoded until something asks for it (SampleData::
    // request(): a note that reaches its zone, or want()); a file already
    // decoded or asked for is shared as it is. `first` (0 and up): where its
    // zone starts; a recording long past it keeps only its head in memory
    // (from there, through `until`: its loop) and is read from the disk as
    // it plays (CYDAW_SAMPLE_STREAMING=0: every file whole).
    [[nodiscard]] cydaw::engine::SampleDataRef acquire(const juce::File& file, bool deferred = false,
                                                       juce::int64 first = -1, juce::int64 until = -1);
    // Asks for every sample of the zones a map reaches with these controllers
    // and articulation (a card's settings moved: a microphone raised).
    void want(const cydaw::engine::SamplerMap& map, const cydaw::engine::SamplerCues& cues);
    // Waits until every sample of the map is decoded (or failed), at most
    // `timeoutMs`; those not asked for are left. True when they all are.
    bool waitUntilReady(const cydaw::engine::SamplerMap& map, int timeoutMs);
    struct Stats {
        int pending {};
        int loaded {};
        int failed {};
        // Not asked for yet.
        int deferred {};
        // Long recordings with only their head in memory.
        int streamed {};
        juce::int64 bytes {};
    };
    [[nodiscard]] Stats stats();

private:
    SampleStore();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Decodes a file now, on this thread, into `data` (the store's loader; a
// test). False with `error` set when it cannot be read.
bool decodeSampleFile(const juce::File& file, cydaw::engine::SampleData& data, juce::String& error);

// ---- Maps -----------------------------------------------------------------------------
// The map as this version plays it: the zones of factory instruments made
// when CyDAW shipped their notes as FLAC files ("factory:Instruments/…flac",
// no longer there) on the banks' recordings they were cut from, where
// Factory/Instruments/legacy.json says. The same var when nothing changes.
[[nodiscard]] juce::var upgradedSamplerMap(const juce::var& json);
// The JSON as parsed, every sample asked of the store. Null with `error` set
// when it holds no zone this version can play.
[[nodiscard]] cydaw::engine::SamplerMapRef samplerMapFromVar(const juce::var& json, juce::String& error);
[[nodiscard]] juce::var loadSamplerMapFile(const juce::File& file, juce::String& error);
// "Steinway B · 63 zones · A0–C8 · 3 layers".
[[nodiscard]] juce::String describeSamplerMap(const juce::var& json);

// Files made into a map: each file's note from its name ("C#4", "A-1",
// "_60_") checked against the pitch heard in it, or the pitch alone; files
// on the same note become velocity layers (by the name's "v1"/"pp"/"ff", or
// by loudness) or round robins ("rr2"); every key between two notes goes to
// the nearer. A file with no pitch (a drum) gets one key of its own, from C1
// up. `name` names the map.
[[nodiscard]] juce::var samplerMapFromFiles(const juce::Array<juce::File>& files, const juce::String& name);
// The zones of `json` with these files added, mapped as above among the
// notes already there.
[[nodiscard]] juce::var withFilesAdded(const juce::var& json, const juce::Array<juce::File>& files);
// Every zone's keys spread again around the roots (after a root moved).
[[nodiscard]] juce::var withKeysSpread(const juce::var& json);

// An SFZ instrument read: its regions (sample, keys, velocities, tune,
// volume, pan, offset, end, loops, round robins, choke groups), with
// #define, #include, default_path and <control>/<global>/<master>/<group>
// inheritance; what its controllers do kept as rules played as they move
// (locc/hicc, xfin/xfout, amplitude/volume/pan/tune/offset on a CC, their
// <curve>s), their starting values (set_ccN, or `controllers` given here)
// and names (label_ccN); its keyswitches (sw_last, sw_lokey/hikey,
// sw_default, sw_label); its release, first and legato regions (trigger,
// rt_decay). Regions of a held keyswitch (sw_down/up/previous) are skipped.
// A sample not where the SFZ says is looked for without an undefined $name,
// then in ../Samples (a mapping meant to be included).
[[nodiscard]] juce::var samplerMapFromSfz(const juce::File& file, juce::String& error,
                                          const std::map<int, double>& controllers = {});
// The map written as SFZ, its samples by path relative to the SFZ's folder
// when they are under it.
[[nodiscard]] juce::String samplerMapToSfz(const juce::var& json, const juce::File& sfzFile);

// The note a name holds ("C#4" 61, "Db3" 49, "A-1" 9; "60" in "_60_"), or -1.
[[nodiscard]] int noteFromName(const juce::String& name);
// The pitch heard in a decoded sample, in MIDI notes (fractional), or -1.
[[nodiscard]] double detectSamplePitch(const cydaw::engine::SampleData& data);

} // namespace cydaw::app
