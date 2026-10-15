#include "SampleLibrary.h"

#include "AudioTrackController.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <regex>

namespace cydaw::app {

namespace {
constexpr auto overviewBins = 128;
// Raised when the rules naming a file's category change.
constexpr auto categoryRulesVersion = 7;
constexpr auto indexFileName = "sample-library.json";
constexpr auto classifyBatch = 24;
// A detected tempo below this confidence is not written down: a one-shot
// with a tail has no tempo, whatever the detector's best guess.
constexpr auto tempoConfidenceFloor = 0.3;

juce::String lower(const juce::String& text)
{
    return text.toLowerCase();
}

struct Keyword {
    const char* word;
    const char* category;
};

// What a file name usually says about a drum or an instrument. Read in
// order, the first found wins: the longer, more specific words first —
// "hihat" before "hat", "bassoon" before "bass", "harpsichord" before
// "harp", "woodwind" before "wood". A word of three letters or fewer counts
// only as a word of its own ("tom" in "Floor Tom 2", "FloorTom", not in
// "atomic"; "arp" not in "harp").
constexpr Keyword keywords[] = {
    // Drums, the words that cannot be anything else first.
    { "sub drop", "fx" }, { "cowbell", "perc" },
    { "hihat", "hat" }, { "hi-hat", "hat" }, { "hi_hat", "hat" }, { "hi hat", "hat" },
    { "openhat", "hat" }, { "closedhat", "hat" }, { "charley", "hat" }, { "charleston", "hat" },
    { "kick", "kick" }, { "bassdrum", "kick" }, { "bass drum", "kick" }, { "bass_drum", "kick" },
    { "grosse caisse", "kick" }, { "kik", "kick" }, { "bd", "kick" },
    { "snare", "snare" }, { "caisse claire", "snare" }, { "snr", "snare" }, { "rimshot", "snare" },
    { "rim", "snare" }, { "sd", "snare" }, { "frankensnare", "snare" },
    { "handclap", "clap" }, { "clap", "clap" }, { "snap", "clap" },
    { "crash", "cymbal" }, { "ride", "cymbal" }, { "cymbal", "cymbal" }, { "cymbale", "cymbal" },
    { "splash", "cymbal" }, { "china", "cymbal" }, { "sizzle", "cymbal" },
    { "hat", "hat" }, { "hh", "hat" },
    { "floortom", "tom" }, { "floor tom", "tom" }, { "tom", "tom" }, { "toms", "tom" }, { "ltom", "tom" },
    { "mtom", "tom" }, { "htom", "tom" }, { "ftom", "tom" }, { "rtom", "tom" }, { "sn", "snare" },
    { "drum loop", "drums" }, { "drumloop", "drums" }, { "breakbeat", "drums" }, { "drum kit", "drums" },
    { "darbouka", "perc" }, { "darbuka", "perc" }, { "doumbek", "perc" }, { "tabla", "perc" },
    { "beat", "drums" },
    // Instruments, the long names before the short ones they contain.
    { "harpsichord", "keys" }, { "clavecin", "keys" }, { "clavinet", "keys" },
    { "piano", "keys" }, { "rhodes", "keys" }, { "wurli", "keys" }, { "organ", "keys" },
    { "orgue", "keys" }, { "keys", "keys" },
    { "celesta", "mallets" }, { "glockenspiel", "mallets" }, { "glock", "mallets" },
    { "marimba", "mallets" }, { "xylophone", "mallets" }, { "vibraphone", "mallets" },
    { "vibes", "mallets" }, { "chimes", "mallets" }, { "steel drum", "mallets" },
    { "steeldrum", "mallets" }, { "steel-drum", "mallets" }, { "kalimba", "mallets" },
    { "music box", "mallets" }, { "bells", "mallets" }, { "carillon", "mallets" },
    { "woodwind", "woodwinds" }, { "bassoon", "woodwinds" }, { "basson", "woodwinds" },
    { "contrabassoon", "woodwinds" }, { "clarinet", "woodwinds" }, { "clarinette", "woodwinds" },
    { "oboe", "woodwinds" }, { "hautbois", "woodwinds" }, { "piccolo", "woodwinds" },
    { "flute", "woodwinds" }, { "recorder", "woodwinds" }, { "ocarina", "woodwinds" },
    { "saxophone", "woodwinds" }, { "saxo", "woodwinds" }, { "sax", "woodwinds" },
    { "whistle", "woodwinds" }, { "pipes", "woodwinds" }, { "lip aerophones", "brass" },
    { "aerophone", "woodwinds" },
    { "trumpet", "brass" }, { "trompette", "brass" }, { "trombone", "brass" }, { "tuba", "brass" },
    { "cornet", "brass" }, { "flugel", "brass" }, { "horn", "brass" },
    { "euphonium", "brass" }, { "brass", "brass" }, { "cuivre", "brass" },
    { "harmonica", "folk" }, { "accordion", "folk" }, { "accordeon", "folk" },
    { "bagpipe", "folk" }, { "banjo", "folk" }, { "ganjo", "folk" }, { "zither", "folk" },
    { "cithara", "folk" }, { "dan tranh", "folk" }, { "sitar", "folk" }, { "erhu", "folk" },
    { "mandolin", "folk" }, { "ukulele", "folk" }, { "dulcimer", "folk" }, { "tagelharpa", "folk" },
    { "phu", "folk" }, { "didgeridoo", "folk" }, { "strumstick", "folk" },
    { "double bass", "bass" }, { "doublebass", "bass" }, { "contrabass", "bass" },
    { "contrebasse", "bass" }, { "bass guitar", "bass" },
    { "violin", "strings" }, { "violon", "strings" }, { "viola", "strings" },
    { "cello", "strings" }, { "harp", "strings" }, { "harpe", "strings" }, { "string", "strings" },
    { "cordes", "strings" }, { "pizz", "strings" }, { "chordophone", "strings" },
    { "guitar", "guitar" }, { "guitare", "guitar" }, { "gtr", "guitar" },
    { "vocal", "vocal" }, { "vox", "vocal" }, { "voice", "vocal" }, { "voix", "vocal" },
    { "choir", "vocal" }, { "choeur", "vocal" }, { "vowel", "vocal" }, { "singing", "vocal" },
    // Percussion of every other kind.
    { "shaker", "perc" }, { "tamb", "perc" }, { "conga", "perc" }, { "bongo", "perc" },
    { "cowbell", "perc" }, { "perc", "perc" }, { "woodblock", "perc" }, { "wood", "perc" },
    { "click", "perc" }, { "clave", "perc" }, { "cajon", "perc" }, { "djembe", "perc" },
    { "timpani", "perc" }, { "triangle", "perc" }, { "guiro", "perc" }, { "cabasa", "perc" },
    { "body", "perc" }, { "idiophone", "perc" }, { "membranophone", "perc" },
    // Synths and bass.
    { "808", "bass" }, { "sub", "bass" }, { "bass", "bass" }, { "basse", "bass" }, { "reese", "bass" },
    { "lead", "lead" }, { "pluck", "pluck" }, { "pad", "pad" }, { "nappe", "pad" },
    { "atmos", "pad" }, { "ambien", "pad" }, { "drone", "pad" }, { "texture", "pad" },
    { "synth", "synth" }, { "saw", "synth" }, { "arp", "synth" }, { "chord", "synth" },
    { "stab", "synth" }, { "electrophone", "synth" },
    // Effects.
    { "riser", "fx" }, { "uplifter", "fx" }, { "downlifter", "fx" }, { "impact", "fx" },
    { "sweep", "fx" }, { "whoosh", "fx" }, { "laser", "fx" }, { "zap", "fx" }, { "pew", "fx" }, { "noise", "fx" },
    { "fx", "fx" }, { "sfx", "fx" }, { "glitch", "fx" }, { "siren", "fx" },
};

// Whether a word is in a text: anywhere for a long word, as a word of its
// own (split at what is not a letter or a digit, and at a capital following
// a small letter) for a short one.
bool containsKeyword(const juce::String& original, const juce::String& lowered, const juce::String& word)
{
    if (word.length() > 3)
        return lowered.contains(word);
    auto start = 0;
    while (start < original.length()) {
        while (start < original.length() && !juce::CharacterFunctions::isLetterOrDigit(original[start]))
            ++start;
        auto end = start;
        while (end < original.length() && juce::CharacterFunctions::isLetterOrDigit(original[end])) {
            // FloorTom, 808Kick: a capital after a small letter, a letter
            // after a digit, begin a word.
            if (end > start) {
                const auto previous = original[end - 1];
                const auto current = original[end];
                if ((juce::CharacterFunctions::isLowerCase(previous) && juce::CharacterFunctions::isUpperCase(current))
                    || (juce::CharacterFunctions::isDigit(previous) != juce::CharacterFunctions::isDigit(current)))
                    break;
            }
            ++end;
        }
        if (end > start && original.substring(start, end).toLowerCase() == word)
            return true;
        start = std::max(end, start + 1);
    }
    return false;
}

const std::vector<SampleCategory>& taxonomy()
{
    static const std::vector<SampleCategory> list {
        { "kick", "drums", "Grosses caisses (kicks)", "Kicks", { "a kick drum", "a bass drum hit", "an electronic 808 kick" } },
        { "snare", "drums", "Caisses claires", "Snares", { "a snare drum hit", "a rimshot" } },
        { "clap", "drums", "Claps", "Claps", { "a hand clap", "an electronic clap" } },
        { "hat", "drums", "Charlestons (hi-hats)", "Hi-hats", { "a closed hi-hat", "an open hi-hat" } },
        { "cymbal", "drums", "Cymbales", "Cymbals", { "a crash cymbal", "a ride cymbal" } },
        { "tom", "drums", "Toms", "Toms", { "a tom drum hit" } },
        { "perc", "drums", "Percussions", "Percussion", { "a percussion hit", "a shaker", "a cowbell", "a hand drum" } },
        { "drums", "drums", "Boucles et kits de batterie", "Drum loops and kits", { "a drum loop", "a drum beat" } },
        { "keys", "instruments", "Claviers", "Keys", { "a piano", "an electric piano", "an organ" } },
        { "guitar", "instruments", "Guitares", "Guitars", { "an acoustic guitar", "an electric guitar" } },
        { "bass", "instruments", "Basses", "Basses", { "a bass guitar", "a synth bass", "a double bass" } },
        { "strings", "instruments", "Cordes", "Strings", { "a violin", "a cello", "a string section", "a harp" } },
        { "brass", "instruments", "Cuivres", "Brass", { "a trumpet", "a trombone", "a french horn", "a tuba" } },
        { "woodwinds", "instruments", "Bois", "Woodwinds", { "a flute", "a clarinet", "a saxophone", "an oboe" } },
        { "mallets", "instruments", "Percussions mélodiques", "Tuned percussion",
          { "a marimba", "a xylophone", "a vibraphone", "a glockenspiel", "bells" } },
        { "folk", "instruments", "Instruments traditionnels", "Folk instruments",
          { "a folk instrument", "a zither", "a banjo", "an accordion", "bagpipes" } },
        { "vocal", "instruments", "Voix", "Vocals", { "a singing voice", "a choir", "a vocal phrase" } },
        { "synth", "synths", "Synthés", "Synths", { "a synthesizer", "a synth chord" } },
        { "lead", "synths", "Leads", "Leads", { "a synth lead melody" } },
        { "pad", "synths", "Nappes (pads)", "Pads", { "a synth pad", "an ambient drone" } },
        { "pluck", "synths", "Plucks", "Plucks", { "a plucked synth note" } },
        { "fx", "fx", "Effets sonores", "Sound effects",
          { "a sound effect", "a riser", "a whoosh", "an impact", "noise" } },
        { "other", "other", "Autres", "Other", {} },
    };
    return list;
}

const std::vector<SampleFamily>& families()
{
    static const std::vector<SampleFamily> list {
        { "drums", "Batterie et percussions", "Drums and percussion" },
        { "instruments", "Instruments", "Instruments" },
        { "synths", "Synthés", "Synths" },
        { "fx", "Effets sonores", "Sound effects" },
        { "other", "Autres", "Other" },
    };
    return list;
}

constexpr const char* tags[] = {
    "acoustic", "electronic", "analog", "distorted", "clean", "punchy", "soft", "dark",
    "bright", "warm", "metallic", "wooden", "reverb", "dry", "short", "long", "lofi",
    "vintage", "aggressive", "melodic", "rhythmic", "noisy", "layered", "processed"
};

std::mutex& formatManagerMutex()
{
    static std::mutex mutex;
    return mutex;
}

// One format manager per analysis thread: registering formats is not a
// thing to do twice, and a manager is not a thing to share between readers
// being created at once.
juce::AudioFormatManager& threadFormatManager()
{
    thread_local std::unique_ptr<juce::AudioFormatManager> manager;
    if (manager == nullptr) {
        std::lock_guard<std::mutex> lock(formatManagerMutex());
        manager = std::make_unique<juce::AudioFormatManager>();
        manager->registerBasicFormats();
    }
    return *manager;
}

// What the audio pass found for the naming pass, per analysis thread.
thread_local SampleLibrary::KeyEstimate detectedKey_;

float toDb(const double amplitude)
{
    return amplitude > 1.0e-6 ? static_cast<float>(20.0 * std::log10(amplitude)) : -100.0f;
}
} // namespace

// ------------------------------------------------------------------ helpers

juce::StringArray SampleLibrary::supportedExtensions()
{
    return { "wav", "wave", "aif", "aiff", "flac", "ogg", "mp3", "wv", "caf" };
}

juce::StringArray SampleLibrary::categoryVocabulary()
{
    juce::StringArray out;
    for (const auto& category : taxonomy())
        out.add(category.key);
    return out;
}

const std::vector<SampleCategory>& SampleLibrary::categoryTaxonomy()
{
    return taxonomy();
}

const std::vector<SampleFamily>& SampleLibrary::categoryFamilies()
{
    return families();
}

const SampleCategory* SampleLibrary::categoryInfo(const juce::String& key)
{
    for (const auto& category : taxonomy())
        if (key == category.key)
            return &category;
    return nullptr;
}

juce::String SampleLibrary::familyOf(const juce::String& category)
{
    if (const auto* info = categoryInfo(category))
        return info->family;
    return "other";
}

juce::String SampleLibrary::categoryFromCatalogue(const juce::String& catalogueCategory, const juce::String& name,
                                                  const juce::String& folder)
{
    static const std::map<juce::String, juce::String> mapped {
        { "pianos", "keys" },       { "electric-pianos", "keys" }, { "harpsichords", "keys" },
        { "organs", "keys" },       { "harps", "strings" },        { "strings", "strings" },
        { "string-sections", "strings" }, { "brass", "brass" },   { "woodwinds", "woodwinds" },
        { "mallets", "mallets" },   { "folk", "folk" },            { "guitars", "guitar" },
        { "basses", "bass" },       { "synths", "synth" },         { "voices", "vocal" },
    };
    if (catalogueCategory == "drums" || catalogueCategory == "electro-drums") {
        // A kit's file is what its name says it is, else a percussion.
        const auto named = categoryFromName(name, folder);
        return familyOf(named) == "drums" ? named : juce::String("perc");
    }
    if (catalogueCategory == "synths") {
        // A synth's pad stays a pad.
        const auto named = categoryFromName(name, folder);
        if (familyOf(named) == "synths" || named == "bass")
            return named;
    }
    if (const auto found = mapped.find(catalogueCategory); found != mapped.end())
        return found->second;
    return categoryFromName(name, folder);
}

juce::StringArray SampleLibrary::tagVocabulary()
{
    juce::StringArray out;
    for (const auto* tag : tags)
        out.add(tag);
    return out;
}

juce::String SampleLibrary::categoryFromName(const juce::String& name,
                                             const juce::String& folder)
{
    // The file name first, then the folders it sits in, nearest first:
    // "Kicks/808/deep.wav" is a kick before it is an 808.
    juce::StringArray places { name };
    {
        juce::StringArray parts;
        parts.addTokens(folder.replaceCharacters("\\", "/"), "/", "");
        parts.removeEmptyStrings();
        for (auto index = parts.size(); --index >= 0;)
            places.add(parts[index]);
    }
    for (const auto& place : places) {
        const auto lowered = lower(place);
        for (const auto& keyword : keywords)
            if (containsKeyword(place, lowered, keyword.word))
                return keyword.category;
    }
    if (lower(name).contains("loop") || lower(folder).contains("loop"))
        return lower(name).contains("drum") || lower(folder).contains("drum") ? "drums"
                                                                                : "";
    return {};
}

juce::StringArray SampleLibrary::tagsFromPath(const juce::String& name,
                                              const juce::String& relativeFolder)
{
    juce::StringArray out;
    juce::StringArray parts;
    parts.addTokens(lower(relativeFolder).replaceCharacters("\\", "/"), "/", "");
    for (auto part : parts) {
        part = part.trim().removeCharacters("[](){}");
        if (part.length() >= 3 && part.length() <= 24 && !part.containsOnly("0123456789")
            && !out.contains(part))
            out.add(part);
        if (out.size() >= 5)
            break;
    }
    const auto lowered = lower(name);
    for (const auto* tag : tags)
        if (lowered.contains(tag) && !out.contains(tag))
            out.add(tag);
    return out;
}

juce::String SampleLibrary::keyFromName(const juce::String& name)
{
    // "F#m", "Cmin", "Bbmaj", "A minor": a letter, an accidental, a mode;
    // a bare letter says nothing, "A_kick" is not in A.
    static const std::regex pattern(
        "(?:^|[ _\\-\\.\\(])([A-G])(#|b)?[ _]?(min|maj|minor|major|m|M)?(?:$|[ _\\-\\.\\)])");
    const auto text = name.toStdString();
    std::smatch match;
    auto search = text.cbegin();
    while (std::regex_search(search, text.cend(), match, pattern)) {
        const auto letter = match[1].str();
        const auto accidental = match[2].str();
        const auto mode = match[3].str();
        if (!accidental.empty() || !mode.empty()) {
            const auto minor = mode == "m" || mode == "min" || mode == "minor";
            return juce::String(letter) + juce::String(accidental)
                   + (minor ? "m" : juce::String());
        }
        search = match.suffix().first;
    }
    return {};
}

double SampleLibrary::bpmFromName(const juce::String& name)
{
    static const std::regex withUnit("(\\d{2,3})\\s?bpm", std::regex::icase);
    static const std::regex bare("(?:^|[ _\\-])(\\d{2,3})(?:$|[ _\\-\\.])");
    const auto text = name.toStdString();
    std::smatch match;
    if (std::regex_search(text, match, withUnit))
        return std::stod(match[1].str());
    if (std::regex_search(text, match, bare)) {
        const auto value = std::stod(match[1].str());
        if (value >= 60.0 && value <= 200.0)
            return value;
    }
    return 0.0;
}

SampleLibrary::TempoEstimate SampleLibrary::estimateTempo(const double detectedBpm,
                                                          const double detectedConfidence,
                                                          const double lengthSeconds,
                                                          const bool looksLikeLoop)
{
    TempoEstimate estimate;
    if (lengthSeconds <= 0.0)
        return estimate;
    // What a whole number of bars of four beats would make of this length.
    std::vector<double> candidates;
    for (const auto bars : { 1, 2, 4, 8, 16, 32 }) {
        const auto candidate = bars * 240.0 / lengthSeconds;
        if (candidate >= 55.0 && candidate <= 210.0)
            candidates.push_back(candidate);
    }
    const auto trusted = detectedBpm > 0.0 && detectedConfidence >= tempoConfidenceFloor;
    if (trusted) {
        // The candidate nearest the detector's estimate, up to a ratio of
        // 1.4 either way — the dotted-note errors the detector makes.
        auto best = 0.0;
        auto bestDistance = 1.0e9;
        for (const auto candidate : candidates) {
            const auto distance = std::abs(std::log(candidate / detectedBpm));
            if (distance < bestDistance) {
                best = candidate;
                bestDistance = distance;
            }
        }
        const auto tolerance = std::log(1.4);
        if (best > 0.0 && bestDistance <= tolerance) {
            estimate.bpm = std::round(best * 10.0) / 10.0;
            estimate.confidence = static_cast<float>(
                detectedConfidence * (1.0 - 0.5 * bestDistance / tolerance));
            estimate.source = "detected";
            return estimate;
        }
        estimate.bpm = std::round(detectedBpm * 10.0) / 10.0;
        estimate.confidence = static_cast<float>(detectedConfidence);
        estimate.source = "detected";
        return estimate;
    }
    if (looksLikeLoop)
        for (const auto candidate : candidates)
            if (candidate >= 80.0 && candidate <= 160.0) {
                estimate.bpm = std::round(candidate * 10.0) / 10.0;
                estimate.confidence = 0.35f;
                estimate.source = "length";
                return estimate;
            }
    return estimate;
}

std::vector<float> SampleLibrary::chromaOf(const juce::AudioBuffer<float>& audio,
                                           const double sampleRate)
{
    std::vector<float> chroma(12, 0.0f);
    constexpr auto order = 12;
    constexpr auto frameSize = 1 << order;
    if (audio.getNumSamples() < frameSize || sampleRate <= 0.0)
        return chroma;
    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window(frameSize,
                                               juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> frame(frameSize * 2, 0.0f);
    const auto hop = frameSize / 2;
    const auto lowestBin = static_cast<int>(std::ceil(55.0 * frameSize / sampleRate));
    const auto highestBin = std::min(frameSize / 2 - 1,
                                     static_cast<int>(5'000.0 * frameSize / sampleRate));
    std::vector<int> pitchClassOfBin(static_cast<std::size_t>(highestBin) + 1, 0);
    for (auto bin = lowestBin; bin <= highestBin; ++bin) {
        const auto frequency = bin * sampleRate / frameSize;
        const auto midi = 69.0 + 12.0 * std::log2(frequency / 440.0);
        pitchClassOfBin[static_cast<std::size_t>(bin)]
            = ((static_cast<int>(std::lround(midi)) % 12) + 12) % 12;
    }
    const auto edgeSamples = static_cast<int>(sampleRate * 0.4);
    for (auto start = 0; start + frameSize <= audio.getNumSamples(); start += hop) {
        std::fill(frame.begin(), frame.end(), 0.0f);
        for (auto index = 0; index < frameSize; ++index) {
            auto value = 0.0f;
            for (auto channel = 0; channel < audio.getNumChannels(); ++channel)
                value += audio.getSample(channel, start + index);
            frame[static_cast<std::size_t>(index)] = value / static_cast<float>(audio.getNumChannels());
        }
        window.multiplyWithWindowingTable(frame.data(), frameSize);
        fft.performFrequencyOnlyForwardTransform(frame.data());
        // The first and last moments carry the tonic more often than not.
        const auto edge = start < edgeSamples
                          || start > audio.getNumSamples() - frameSize - edgeSamples;
        const auto weight = edge ? 3.0f : 1.0f;
        for (auto bin = lowestBin; bin <= highestBin; ++bin) {
            auto magnitude = frame[static_cast<std::size_t>(bin)];
            if (magnitude <= 1.0e-5f)
                continue;
            // A partial of a lower note — a bin whose half, third, quarter
            // or fifth carries most of its energy — says little about the
            // key: the fifth harmonic of every C is an E.
            for (const auto harmonic : { 2, 3, 4, 5 }) {
                const auto fundamental = juce::roundToInt(static_cast<float>(bin) / harmonic);
                if (fundamental >= lowestBin
                    && frame[static_cast<std::size_t>(fundamental)] >= 0.6f * magnitude) {
                    magnitude *= 0.3f;
                    break;
                }
            }
            chroma[static_cast<std::size_t>(pitchClassOfBin[static_cast<std::size_t>(bin)])]
                += weight * std::sqrt(magnitude);
        }
    }
    return chroma;
}

SampleLibrary::KeyEstimate SampleLibrary::estimateKey(const std::vector<float>& chroma)
{
    KeyEstimate estimate;
    if (chroma.size() != 12)
        return estimate;
    auto total = 0.0f;
    auto loudest = 0.0f;
    for (const auto value : chroma) {
        total += value;
        loudest = std::max(loudest, value);
    }
    if (total <= 1.0e-6f)
        return estimate;
    // A key needs several notes: one sustained pitch fits every key that
    // holds it.
    auto spread = 0;
    for (const auto value : chroma)
        if (value >= 0.25f * loudest)
            ++spread;
    if (spread < 4)
        return estimate;
    // Krumhansl-Kessler profiles: how much each degree belongs to a key.
    static constexpr std::array<float, 12> major {
        6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
    };
    static constexpr std::array<float, 12> minor {
        6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
    };
    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const auto correlation = [&chroma](const std::array<float, 12>& profile, const int tonic) {
        auto meanChroma = 0.0f;
        auto meanProfile = 0.0f;
        for (auto index = 0; index < 12; ++index) {
            meanChroma += chroma[static_cast<std::size_t>(index)] / 12.0f;
            meanProfile += profile[static_cast<std::size_t>(index)] / 12.0f;
        }
        auto numerator = 0.0f;
        auto chromaNorm = 0.0f;
        auto profileNorm = 0.0f;
        for (auto index = 0; index < 12; ++index) {
            const auto c = chroma[static_cast<std::size_t>((index + tonic) % 12)] - meanChroma;
            const auto p = profile[static_cast<std::size_t>(index)] - meanProfile;
            numerator += c * p;
            chromaNorm += c * c;
            profileNorm += p * p;
        }
        return chromaNorm > 0.0f && profileNorm > 0.0f
                   ? numerator / std::sqrt(chromaNorm * profileNorm) : 0.0f;
    };
    auto best = -2.0f;
    auto second = -2.0f;
    juce::String bestKey;
    for (auto tonic = 0; tonic < 12; ++tonic) {
        for (const auto isMinor : { false, true }) {
            const auto score = correlation(isMinor ? minor : major, tonic);
            if (score > best) {
                second = best;
                best = score;
                bestKey = juce::String(names[static_cast<std::size_t>(tonic)])
                          + (isMinor ? "m" : "");
            } else if (score > second) {
                second = score;
            }
        }
    }
    // A key is a claim only when its profile fits and beats the runner-up:
    // a single sustained pitch fits every key holding that note.
    if (best < 0.6f || best - second < 0.06f)
        return estimate;
    estimate.key = bestKey;
    estimate.confidence = juce::jlimit(0.0f, 1.0f, (best - 0.5f) / 0.45f);
    return estimate;
}

bool SampleLibrary::isDrumHit(const juce::String& category, const juce::String& name, const juce::String& folder,
                              const double lengthSeconds)
{
    if (familyOf(category) != "drums" || category == "drums")
        return false;
    const auto words = lower(name) + " " + lower(folder);
    // "Kick Loop 120", "Loops/Hats", "tipper-kick" (20 s): loops of one drum,
    // with a tempo.
    if (words.contains("loop") || words.contains("groove") || words.contains("pattern") || bpmFromName(name) > 0.0)
        return false;
    if (lengthSeconds > 0.0 && lengthSeconds < 3.0)
        return true;
    // A long decay (a cymbal, a big drum), said to be one hit.
    for (const auto* word : { "hit", "_rr", " rr", "roll", "one shot", "oneshot", "one-shot", "single", "_vl", "velocity" })
        if (words.contains(word))
            return true;
    return false;
}

juce::String SampleLibrary::typeFor(const double lengthSeconds, const juce::String& name,
                                    const double bpm)
{
    // Short, a single hit; but a bar named for its tempo, or a loop, is one.
    if (lengthSeconds < 2.5 && !(bpmFromName(name) > 0.0 || lower(name).contains("loop")))
        return "one-shot";
    if (lengthSeconds > 60.0)
        return "long";
    if (bpm > 0.0 || lower(name).contains("loop"))
        return "loop";
    return lengthSeconds < 8.0 ? "one-shot" : "loop";
}

void SampleLibrary::applyHint(SampleRecord& record, const SampleHint& hint)
{
    if (hint.weak) {
        // A bank's: what the name and the folders did not say.
        if (!record.userEdited && hint.category.isNotEmpty() && (record.category.isEmpty() || record.classifier == "bank")) {
            record.category = hint.category;
            record.classifier = "bank";
            record.confidence = 0.6f;
        }
        // Its instrument, for the files of its kind only (a bass bank's kick
        // is not the bass); a kit's, for all its drums.
        const auto ofItsKind = hint.category.isEmpty() || record.category == hint.category
                               || (familyOf(hint.category) == "drums" && familyOf(record.category) == "drums");
        if (record.instrument.isEmpty() || record.classifier == "bank")
            record.instrument = ofItsKind ? hint.instrument : juce::String();
        return;
    }
    record.instrument = hint.instrument;
    if (record.userEdited || hint.category.isEmpty())
        return;
    record.category = hint.category;
    record.classifier = "bank";
    record.confidence = 1.0f;
    if (hint.note) {
        // One note of an instrument: it has no tempo, and is no loop.
        record.bpm = 0.0;
        record.bpmSource.clear();
        record.bpmConfidence = 0.0f;
        record.type = record.length > 60.0 ? "long" : "one-shot";
    }
}

SampleRecord SampleLibrary::analyseFile(const juce::File& file, const juce::File& root,
                                        const SampleRecord* previous, const SampleHint* hint)
{
    SampleRecord record;
    if (previous != nullptr)
        record = *previous;
    record.path = file.getFullPathName();
    record.name = file.getFileNameWithoutExtension();
    record.extension = file.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    record.root = root.getFullPathName();
    record.size = file.getSize();
    record.modified = file.getLastModificationTime().toMilliseconds();
    record.analysed = false;
    record.overview.clear();

    std::unique_ptr<juce::AudioFormatReader> reader(
        threadFormatManager().createReaderFor(file));
    if (reader != nullptr && reader->lengthInSamples > 0 && reader->sampleRate > 0.0) {
        record.sampleRate = reader->sampleRate;
        record.channels = static_cast<int>(reader->numChannels);
        record.bitDepth = static_cast<int>(reader->bitsPerSample);
        record.length = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        // The measurements read the whole file up to ten minutes; a longer
        // one is measured on its first two.
        const auto measured = std::min<juce::int64>(
            reader->lengthInSamples,
            record.length > 600.0 ? static_cast<juce::int64>(reader->sampleRate * 120.0)
                                  : reader->lengthInSamples);
        const auto channels = static_cast<int>(std::min<unsigned>(reader->numChannels, 8));
        juce::AudioBuffer<float> buffer(channels, 65'536);
        // The first thirty seconds, kept for the chroma the key is read from.
        const auto keptForChroma = static_cast<int>(std::min<juce::int64>(
            measured, static_cast<juce::int64>(reader->sampleRate * 30.0)));
        juce::AudioBuffer<float> forChroma(channels, keptForChroma);
        forChroma.clear();
        std::vector<float> bins(overviewBins, 0.0f);
        double peak = 0.0;
        double sumSquares = 0.0;
        juce::int64 position = 0;
        while (position < measured) {
            const auto count = static_cast<int>(std::min<juce::int64>(
                buffer.getNumSamples(), measured - position));
            if (!reader->read(&buffer, 0, count, position, true, true))
                break;
            if (position < keptForChroma)
                for (auto channel = 0; channel < channels; ++channel)
                    forChroma.copyFrom(channel, static_cast<int>(position), buffer, channel, 0,
                                       static_cast<int>(std::min<juce::int64>(
                                           count, keptForChroma - position)));
            for (auto frame = 0; frame < count; ++frame) {
                auto value = 0.0f;
                for (auto channel = 0; channel < channels; ++channel)
                    value = std::max(value, std::abs(buffer.getSample(channel, frame)));
                peak = std::max(peak, static_cast<double>(value));
                sumSquares += static_cast<double>(value) * value;
                const auto bin = static_cast<std::size_t>(
                    std::min<juce::int64>(overviewBins - 1,
                                          (position + frame) * overviewBins / measured));
                bins[bin] = std::max(bins[bin], value);
            }
            position += count;
        }
        record.peakDb = toDb(peak);
        record.rmsDb = toDb(std::sqrt(sumSquares / static_cast<double>(std::max<juce::int64>(1, measured))));
        record.overview.resize(overviewBins);
        for (std::size_t bin = 0; bin < bins.size(); ++bin)
            record.overview[bin] = static_cast<std::uint8_t>(
                juce::jlimit(0, 255, juce::roundToInt(bins[bin] * 255.0f)));
        record.analysed = true;
        // The key of tonal sounds; drums and very short hits are left out,
        // and an instrument's notes (their name says it, the map plays it).
        detectedKey_ = {};
        if (record.length >= 0.35 && !record.userEdited && (hint == nullptr || !hint->note))
            detectedKey_ = estimateKey(chromaOf(forChroma, reader->sampleRate));
    }
    if (!record.userEdited) {
        const auto relative = file.getParentDirectory().getRelativePathFrom(root);
        // One note of an instrument, one hit of a drum: no tempo to find (a
        // long cymbal's decay is no loop).
        const auto hit = isDrumHit(categoryFromName(record.name, relative), record.name, relative, record.length)
                         || (hint != nullptr && isDrumHit(hint->category, record.name, relative, record.length));
        const auto note = (hint != nullptr && hint->note) || hit;
        const auto guessedBpm = note ? 0.0 : bpmFromName(record.name);
        if (note) {
            record.bpm = 0.0;
            record.bpmSource.clear();
            record.bpmConfidence = 0.0f;
        } else if (guessedBpm > 0.0) {
            record.bpm = guessedBpm;
            record.bpmSource = "name";
            record.bpmConfidence = 1.0f;
        } else if (record.analysed && record.length >= 1.5 && record.length <= 180.0) {
            // The DAW's own detector, the one the clip inspector runs,
            // reconciled with the length: a loop gets its tempo at the
            // scan, a one-shot gets none.
            record.bpm = 0.0;
            record.bpmSource.clear();
            record.bpmConfidence = 0.0f;
            const auto detected = AudioTrackController::detectAudioTempo(file);
            const auto looksLikeLoop = lower(record.name).contains("loop")
                                       || lower(relative).contains("loop")
                                       || (record.length >= 2.5 && record.length <= 60.0);
            const auto estimate = estimateTempo(detected ? detected->bpm : 0.0,
                                                detected ? detected->confidence : 0.0,
                                                record.length, looksLikeLoop);
            if (estimate.bpm > 0.0) {
                record.bpm = estimate.bpm;
                record.bpmSource = estimate.source;
                record.bpmConfidence = estimate.confidence;
            }
        }
        record.key = keyFromName(record.name);
        record.keySource = record.key.isNotEmpty() ? "name" : juce::String();
        record.keyConfidence = record.key.isNotEmpty() ? 1.0f : 0.0f;
        if (record.key.isEmpty() && detectedKey_.key.isNotEmpty()) {
            static const std::array<const char*, 8> drums {
                "kick", "snare", "clap", "hat", "cymbal", "tom", "perc", "drums"
            };
            // A drum by its name, or by what the bank says of it.
            const auto category = hint != nullptr && hint->category.isNotEmpty() ? hint->category
                                                                                 : categoryFromName(record.name, relative);
            const auto isDrum = std::any_of(drums.begin(), drums.end(),
                                            [&category](const char* drum) {
                                                return category == drum;
                                            });
            if (!isDrum) {
                record.key = detectedKey_.key;
                record.keySource = "detected";
                record.keyConfidence = detectedKey_.confidence;
            }
        }
        record.type = note ? (record.length > 60.0 ? "long" : "one-shot") : typeFor(record.length, record.name, record.bpm);
        if (record.classifier != "clap") {
            const auto category = categoryFromName(record.name, relative);
            record.category = category;
            record.classifier = category.isNotEmpty() ? "name" : juce::String();
            record.confidence = category.isNotEmpty() ? 1.0f : 0.0f;
            auto guessed = tagsFromPath(record.name, relative);
            if (record.channels == 1 && !guessed.contains("mono"))
                guessed.add("mono");
            record.tags = guessed;
        }
    }
    if (hint != nullptr)
        applyHint(record, *hint);
    else
        record.instrument.clear();
    return record;
}

juce::var SampleLibrary::serialise(const SampleRecord& record)
{
    auto* object = new juce::DynamicObject();
    juce::var value(object);
    object->setProperty("path", record.path);
    object->setProperty("root", record.root);
    object->setProperty("size", record.size);
    object->setProperty("modified", record.modified);
    object->setProperty("sampleRate", record.sampleRate);
    object->setProperty("channels", record.channels);
    object->setProperty("bitDepth", record.bitDepth);
    object->setProperty("length", record.length);
    object->setProperty("peakDb", record.peakDb);
    object->setProperty("rmsDb", record.rmsDb);
    object->setProperty("type", record.type);
    object->setProperty("category", record.category);
    object->setProperty("tags", record.tags.joinIntoString(","));
    object->setProperty("key", record.key);
    object->setProperty("keySource", record.keySource);
    object->setProperty("keyConfidence", record.keyConfidence);
    object->setProperty("bpm", record.bpm);
    object->setProperty("bpmSource", record.bpmSource);
    object->setProperty("bpmConfidence", record.bpmConfidence);
    object->setProperty("classifier", record.classifier);
    object->setProperty("confidence", record.confidence);
    object->setProperty("favourite", record.favourite);
    object->setProperty("rating", record.rating);
    object->setProperty("userEdited", record.userEdited);
    object->setProperty("analysed", record.analysed);
    object->setProperty("source", record.source);
    object->setProperty("instrument", record.instrument);
    if (!record.overview.empty())
        object->setProperty("overview", juce::Base64::toBase64(
                                            record.overview.data(), record.overview.size()));
    return value;
}

std::optional<SampleRecord> SampleLibrary::restore(const juce::var& value)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr)
        return std::nullopt;
    SampleRecord record;
    record.path = object->getProperty("path").toString();
    if (record.path.isEmpty())
        return std::nullopt;
    const juce::File file(record.path);
    record.name = file.getFileNameWithoutExtension();
    record.extension = file.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    record.root = object->getProperty("root").toString();
    record.size = static_cast<juce::int64>(object->getProperty("size"));
    record.modified = static_cast<juce::int64>(object->getProperty("modified"));
    record.sampleRate = static_cast<double>(object->getProperty("sampleRate"));
    record.channels = static_cast<int>(object->getProperty("channels"));
    record.bitDepth = static_cast<int>(object->getProperty("bitDepth"));
    record.length = static_cast<double>(object->getProperty("length"));
    record.peakDb = static_cast<float>(static_cast<double>(object->getProperty("peakDb")));
    record.rmsDb = static_cast<float>(static_cast<double>(object->getProperty("rmsDb")));
    record.type = object->getProperty("type").toString();
    record.category = object->getProperty("category").toString();
    record.tags.addTokens(object->getProperty("tags").toString(), ",", "");
    record.tags.removeEmptyStrings();
    record.key = object->getProperty("key").toString();
    record.keySource = object->getProperty("keySource").toString();
    record.keyConfidence = static_cast<float>(
        static_cast<double>(object->getProperty("keyConfidence")));
    record.bpm = static_cast<double>(object->getProperty("bpm"));
    record.bpmSource = object->getProperty("bpmSource").toString();
    record.bpmConfidence = static_cast<float>(
        static_cast<double>(object->getProperty("bpmConfidence")));
    record.classifier = object->getProperty("classifier").toString();
    record.confidence = static_cast<float>(static_cast<double>(object->getProperty("confidence")));
    record.favourite = static_cast<bool>(object->getProperty("favourite"));
    record.rating = static_cast<int>(object->getProperty("rating"));
    record.userEdited = static_cast<bool>(object->getProperty("userEdited"));
    record.analysed = static_cast<bool>(object->getProperty("analysed"));
    record.source = object->getProperty("source").toString();
    record.instrument = object->getProperty("instrument").toString();
    juce::MemoryOutputStream decoded;
    if (juce::Base64::convertFromBase64(decoded, object->getProperty("overview").toString())
        && decoded.getDataSize() == overviewBins) {
        record.overview.assign(static_cast<const std::uint8_t*>(decoded.getData()),
                               static_cast<const std::uint8_t*>(decoded.getData()) + overviewBins);
    }
    return record;
}

void SampleLibrary::write(juce::OutputStream& out, const SampleRecord& record)
{
    // In this order, read back by read(); a field added goes at the end, with
    // the cache's version raised.
    out.writeString(record.path);
    out.writeString(record.root);
    out.writeInt64(record.size);
    out.writeInt64(record.modified);
    out.writeDouble(record.sampleRate);
    out.writeInt(record.channels);
    out.writeInt(record.bitDepth);
    out.writeDouble(record.length);
    out.writeFloat(record.peakDb);
    out.writeFloat(record.rmsDb);
    out.writeString(record.type);
    out.writeString(record.category);
    out.writeString(record.tags.joinIntoString(","));
    out.writeString(record.key);
    out.writeString(record.keySource);
    out.writeFloat(record.keyConfidence);
    out.writeDouble(record.bpm);
    out.writeString(record.bpmSource);
    out.writeFloat(record.bpmConfidence);
    out.writeString(record.classifier);
    out.writeFloat(record.confidence);
    out.writeBool(record.favourite);
    out.writeInt(record.rating);
    out.writeBool(record.userEdited);
    out.writeBool(record.analysed);
    out.writeString(record.source);
    out.writeString(record.instrument);
    out.writeInt(static_cast<int>(record.overview.size()));
    if (!record.overview.empty())
        out.write(record.overview.data(), record.overview.size());
}

std::optional<SampleRecord> SampleLibrary::read(juce::InputStream& in)
{
    SampleRecord record;
    record.path = in.readString();
    if (record.path.isEmpty() || in.isExhausted())
        return std::nullopt;
    const juce::File file(record.path);
    record.name = file.getFileNameWithoutExtension();
    record.extension = file.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    record.root = in.readString();
    record.size = in.readInt64();
    record.modified = in.readInt64();
    record.sampleRate = in.readDouble();
    record.channels = in.readInt();
    record.bitDepth = in.readInt();
    record.length = in.readDouble();
    record.peakDb = in.readFloat();
    record.rmsDb = in.readFloat();
    record.type = in.readString();
    record.category = in.readString();
    record.tags.addTokens(in.readString(), ",", "");
    record.tags.removeEmptyStrings();
    record.key = in.readString();
    record.keySource = in.readString();
    record.keyConfidence = in.readFloat();
    record.bpm = in.readDouble();
    record.bpmSource = in.readString();
    record.bpmConfidence = in.readFloat();
    record.classifier = in.readString();
    record.confidence = in.readFloat();
    record.favourite = in.readBool();
    record.rating = in.readInt();
    record.userEdited = in.readBool();
    record.analysed = in.readBool();
    record.source = in.readString();
    record.instrument = in.readString();
    const auto bins = in.readInt();
    if (bins < 0 || bins > 4096)
        return std::nullopt;
    record.overview.resize(static_cast<std::size_t>(bins));
    if (bins > 0 && in.read(record.overview.data(), bins) != bins)
        return std::nullopt;
    return record;
}

// ------------------------------------------------------------------ scanner

class SampleLibrary::Scanner final : public juce::Thread {
public:
    Scanner(SampleLibrary& owner, std::vector<Root> roots, std::map<juce::String, SampleRecord> known,
            std::shared_ptr<const std::map<juce::String, SampleHint>> hints, const bool full, const bool rename)
        : juce::Thread("CyDAW sample scan"),
          owner_(owner), library_(&owner), roots_(std::move(roots)), known_(std::move(known)), hints_(std::move(hints)),
          full_(full), rename_(rename)
    {
    }

    void run() override
    {
        const auto extensions = supportedExtensions();
        struct Candidate {
            juce::File file;
            const Root* root;
        };
        std::vector<Candidate> candidates;
        std::set<juce::String> present;
        for (const auto& root : roots_) {
            if (!root.folder.isDirectory())
                continue;
            for (const auto& entry : juce::RangedDirectoryIterator(root.folder, true, "*", juce::File::findFiles)) {
                if (threadShouldExit())
                    return finish(std::move(present), false);
                const auto file = entry.getFile();
                if (!extensions.contains(file.getFileExtension().trimCharactersAtStart(".").toLowerCase()))
                    continue;
                candidates.push_back({ file, &root });
                present.insert(file.getFullPathName());
                owner_.found_.store(static_cast<int>(candidates.size()));
            }
        }
        std::vector<SampleRecord> kept;
        std::vector<Candidate> toAnalyse;
        for (const auto& candidate : candidates) {
            const auto found = known_.find(candidate.file.getFullPathName());
            if (!full_ && found != known_.end() && found->second.analysed && found->second.size == candidate.file.getSize()
                && found->second.modified == candidate.file.getLastModificationTime().toMilliseconds()) {
                // Known as it is: what the catalogue says of it now, and its
                // source, are all that may have changed.
                auto record = found->second;
                record.source = candidate.root->source;
                record.root = candidate.root->folder.getFullPathName();
                if (rename_ && !record.userEdited && (record.classifier.isEmpty() || record.classifier == "name"
                                                      || record.classifier == "bank")) {
                    const auto relative = candidate.file.getParentDirectory().getRelativePathFrom(candidate.root->folder);
                    record.category = categoryFromName(record.name, relative);
                    record.classifier = record.category.isNotEmpty() ? "name" : juce::String();
                    record.instrument.clear();
                    if (isDrumHit(record.category, record.name, relative, record.length) && record.bpmSource != "user") {
                        record.bpm = 0.0;
                        record.bpmSource.clear();
                        record.bpmConfidence = 0.0f;
                        record.type = record.length > 60.0 ? "long" : "one-shot";
                    }
                }
                if (const auto* hint = hintFor(record.path, record.source);
                    rename_ && !record.userEdited && record.keySource == "detected"
                    && familyOf(hint != nullptr && hint->category.isNotEmpty() ? hint->category : record.category) == "drums") {
                    record.key.clear();
                    record.keySource.clear();
                    record.keyConfidence = 0.0f;
                }
                if (const auto* hint = hintFor(record.path, record.source))
                    applyHint(record, *hint);
                else if (record.classifier == "bank") {
                    record.instrument.clear();
                    if (!record.userEdited) {
                        record.category = categoryFromName(record.name, candidate.file.getParentDirectory().getRelativePathFrom(
                                                                            candidate.root->folder));
                        record.classifier = record.category.isNotEmpty() ? "name" : juce::String();
                    }
                }
                kept.push_back(std::move(record));
            } else {
                toAnalyse.push_back(candidate);
            }
        }
        owner_.toAnalyse_.store(static_cast<int>(toAnalyse.size()));
        owner_.analysed_.store(0);
        post(std::move(kept), false, false, {});

        // A pool for the reading: every core but one, so the interface and
        // the audio keep theirs.
        juce::ThreadPool pool(std::max(2, juce::SystemStats::getNumCpus() - 1));
        for (const auto& candidate : toAnalyse) {
            const auto found = known_.find(candidate.file.getFullPathName());
            pool.addJob(new AnalyseJob(*this, candidate.file, *candidate.root,
                                       found != known_.end() ? std::optional(found->second) : std::nullopt),
                        true);
        }
        while (pool.getNumJobs() > 0 && !threadShouldExit()) {
            wait(250);
            flush();
        }
        const auto complete = !threadShouldExit();
        if (!complete)
            pool.removeAllJobs(true, 10'000);
        finish(std::move(present), complete);
    }

private:
    class AnalyseJob final : public juce::ThreadPoolJob {
    public:
        AnalyseJob(Scanner& scanner, juce::File file, Root root, std::optional<SampleRecord> previous)
            : juce::ThreadPoolJob(file.getFileName()),
              scanner_(scanner), file_(std::move(file)), root_(std::move(root)), previous_(std::move(previous))
        {
        }

        JobStatus runJob() override
        {
            if (shouldExit())
                return jobHasFinished;
            auto record = SampleLibrary::analyseFile(file_, root_.folder, previous_.has_value() ? &*previous_ : nullptr,
                                                     scanner_.hintFor(file_.getFullPathName(), root_.source));
            record.source = root_.source;
            {
                const std::lock_guard<std::mutex> lock(scanner_.resultsMutex_);
                scanner_.results_.push_back(std::move(record));
            }
            scanner_.owner_.analysed_.fetch_add(1);
            return jobHasFinished;
        }

    private:
        Scanner& scanner_;
        juce::File file_;
        Root root_;
        std::optional<SampleRecord> previous_;
    };

    const SampleHint* hintFor(const juce::String& path, const juce::String& source) const
    {
        return hints_ != nullptr ? hintOf(*hints_, path, source) : nullptr;
    }

    void flush()
    {
        std::vector<SampleRecord> batch;
        {
            const std::lock_guard<std::mutex> lock(resultsMutex_);
            batch.swap(results_);
        }
        if (!batch.empty())
            post(std::move(batch), false, false, {});
    }

    // `complete`: every folder was read to its end, so what was not found is
    // gone; an interrupted scan forgets nothing.
    void finish(std::set<juce::String> present, const bool complete)
    {
        std::vector<SampleRecord> batch;
        {
            const std::lock_guard<std::mutex> lock(resultsMutex_);
            batch.swap(results_);
        }
        post(std::move(batch), true, complete, std::move(present));
    }

    void post(std::vector<SampleRecord> batch, const bool finished, const bool complete, std::set<juce::String> present)
    {
        std::vector<juce::File> roots;
        for (const auto& root : roots_)
            roots.push_back(root.folder);
        juce::MessageManager::callAsync([library = library_, batch = std::move(batch), finished, complete,
                                         present = std::move(present), roots = std::move(roots)]() mutable {
            if (auto* owner = library.get())
                owner->mergeRecords(std::move(batch), finished, complete, std::move(present), std::move(roots));
        });
    }

    SampleLibrary& owner_;
    juce::WeakReference<SampleLibrary> library_;
    std::vector<Root> roots_;
    std::map<juce::String, SampleRecord> known_;
    std::shared_ptr<const std::map<juce::String, SampleHint>> hints_;
    bool full_ {};
    bool rename_ {};
    std::mutex resultsMutex_;
    std::vector<SampleRecord> results_;
};

// --------------------------------------------------------------- classifier

class SampleLibrary::Classifier final : public juce::Thread {
public:
    Classifier(SampleLibrary& owner, juce::StringArray paths, juce::File script,
               juce::String python, juce::File workFolder)
        : juce::Thread("CyDAW sample classifier"),
          owner_(owner), library_(&owner), paths_(std::move(paths)), script_(std::move(script)),
          python_(std::move(python)), workFolder_(std::move(workFolder))
    {
    }

    void run() override
    {
        if (!script_.existsAsFile())
            return report({}, true, "script introuvable : " + script_.getFullPathName());
        juce::String message;
        for (auto start = 0; start < paths_.size() && !threadShouldExit();
             start += classifyBatch) {
            juce::Array<juce::var> files;
            for (auto index = start; index < std::min(paths_.size(), start + classifyBatch); ++index)
                files.add(paths_[index]);
            auto* request = new juce::DynamicObject();
            request->setProperty("files", files);
            juce::Array<juce::var> categoryList;
            for (const auto& category : categoryVocabulary())
                categoryList.add(category);
            juce::Array<juce::var> tagList;
            for (const auto& tag : tagVocabulary())
                tagList.add(tag);
            request->setProperty("categories", categoryList);
            request->setProperty("tags", tagList);
            const auto requestFile = workFolder_.getChildFile("clap-request.json");
            requestFile.replaceWithText(juce::JSON::toString(juce::var(request)));
            juce::ChildProcess process;
            juce::StringArray command { python_, script_.getFullPathName(),
                                        requestFile.getFullPathName() };
            if (!process.start(command, juce::ChildProcess::wantStdOut))
                return report({}, true, "impossible de lancer " + python_);
            const auto output = process.readAllProcessOutput();
            std::vector<SampleRecord> results;
            juce::StringArray lines;
            lines.addLines(output);
            for (const auto& line : lines) {
                const auto value = juce::JSON::parse(line);
                const auto* object = value.getDynamicObject();
                if (object == nullptr)
                    continue;
                if (object->hasProperty("classifier") && !object->hasProperty("path")) {
                    message = object->getProperty("message").toString();
                    if (object->getProperty("classifier").toString() != "clap")
                        return report({}, true, message.isNotEmpty()
                                                    ? message : "CLAP indisponible");
                    continue;
                }
                SampleRecord result;
                result.path = object->getProperty("path").toString();
                result.category = object->getProperty("category").toString();
                result.tags.addTokens(object->getProperty("tags").toString(), ",", "");
                result.tags.removeEmptyStrings();
                result.confidence = static_cast<float>(
                    static_cast<double>(object->getProperty("confidence")));
                result.classifier = "clap";
                results.push_back(std::move(result));
            }
            owner_.classified_.fetch_add(static_cast<int>(results.size()));
            report(std::move(results), false, message);
        }
        report({}, true, message);
    }

private:
    void report(std::vector<SampleRecord> results, const bool finished, juce::String message)
    {
        juce::MessageManager::callAsync(
            [library = library_, results = std::move(results), finished, message = std::move(message)]() mutable {
                if (auto* owner = library.get())
                    owner->applyClassification(std::move(results), finished, std::move(message));
            });
    }

    SampleLibrary& owner_;
    juce::WeakReference<SampleLibrary> library_;
    juce::StringArray paths_;
    juce::File script_;
    juce::String python_;
    juce::File workFolder_;
};

// ------------------------------------------------------------------ library

SampleLibrary::SampleLibrary(const bool startNow)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "CyDAW";
    options.filenameSuffix = ".samples.settings";
    options.folderName = "CyDAW";
    options.osxLibrarySubFolder = "Application Support";
    properties_.setStorageParameters(options);
    // A test's own settings: its folders never become the person's.
    if (const auto other = juce::SystemStats::getEnvironmentVariable("CYDAW_SAMPLES_SETTINGS", {});
        other.isNotEmpty() && juce::File::isAbsolutePath(other)) {
        options.folderName = {};
        properties_.setStorageParameters(options);
        properties_.closeFiles();
        otherSettings_ = std::make_unique<juce::PropertiesFile>(juce::File(other), options);
    }
    loadSettings();
    if (startNow)
        start();
}

SampleLibrary::~SampleLibrary()
{
    stopTimer();
    cancel();
    // A save on its way ends before the last one is written.
    io_.removeAllJobs(false, 30'000);
    if (loaded_ && (dirty_ || saveAgain_ || saving_.load())) {
        const auto snapshot = samples_;
        (void) writeCacheFile(cacheFolder_, snapshot);
    }
    saveSettings();
}

void SampleLibrary::start()
{
    if (started_)
        return;
    started_ = true;
    loadCache();
    startTimer(2'000);
}

juce::File SampleLibrary::defaultCacheFolder() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("CyDAW").getChildFile("samples-cache");
}

juce::File SampleLibrary::indexFile() const
{
    return cacheFolder_.getChildFile(indexFileName);
}

juce::File SampleLibrary::cacheFolder() const
{
    return cacheFolder_;
}

void SampleLibrary::setCacheFolder(const juce::File& folder)
{
    if (folder == juce::File {} || folder == cacheFolder_)
        return;
    (void) folder.createDirectory();
    if (!folder.isDirectory())
        return;
    // The index follows: a cache moved is not a library lost.
    for (const auto* name : { indexFileName, "sample-library.bin" })
        if (const auto previous = cacheFolder_.getChildFile(name);
            previous.existsAsFile() && !folder.getChildFile(name).existsAsFile())
            (void) previous.copyFileTo(folder.getChildFile(name));
    cacheFolder_ = folder;
    saveSettings();
    markDirty();
    // Read from there while it was read elsewhere: read again.
    if (started_ && !loaded_)
        loadCache();
}

void SampleLibrary::useCacheFolder(const juce::File& folder)
{
    cacheFolderGiven_ = true;
    setCacheFolder(folder);
}

std::vector<juce::File> SampleLibrary::folders() const
{
    return folders_;
}

void SampleLibrary::addFolder(const juce::File& folder)
{
    if (!folder.isDirectory())
        return;
    for (const auto& existing : folders_)
        if (existing == folder)
            return;
    folders_.push_back(folder);
    saveSettings();
    notify(true);
    rescan(false);
}

void SampleLibrary::removeFolder(const juce::File& folder)
{
    const auto before = folders_.size();
    std::erase(folders_, folder);
    if (folders_.size() == before)
        return;
    saveSettings();
    pruneToRoots();
}

void SampleLibrary::setBanks(std::vector<Bank> banks)
{
    std::erase_if(banks, [](const Bank& bank) { return bank.id.isEmpty() || bank.folder == juce::File {}; });
    const auto same = banks.size() == banks_.size()
                      && std::equal(banks.begin(), banks.end(), banks_.begin(), [](const Bank& a, const Bank& b) {
                             return a.id == b.id && a.folder == b.folder && a.title == b.title;
                         });
    if (same)
        return;
    banks_ = std::move(banks);
    if (!loaded_)
        return;
    pruneToRoots();
    if (includeBanks_)
        rescan(false);
}

void SampleLibrary::setIncludeBanks(const bool include)
{
    if (include == includeBanks_)
        return;
    includeBanks_ = include;
    saveSettings();
    if (!loaded_)
        return;
    pruneToRoots();
    if (include)
        rescan(false);
}

void SampleLibrary::setHints(std::map<juce::String, SampleHint> hints)
{
    hints_ = std::make_shared<const std::map<juce::String, SampleHint>>(std::move(hints));
    // What is known now takes them at once; the next scan keeps them.
    auto changed = false;
    for (auto& record : samples_)
        if (const auto* hint = hintOf(*hints_, record.path, record.source)) {
            const auto before = record.category + "|" + record.instrument + "|" + record.classifier;
            applyHint(record, *hint);
            changed = changed || before != record.category + "|" + record.instrument + "|" + record.classifier;
        }
    if (changed) {
        markDirty();
        notify(false);
    }
}

const SampleHint* SampleLibrary::hintOf(const std::map<juce::String, SampleHint>& hints, const juce::String& path,
                                        const juce::String& source)
{
    if (const auto found = hints.find(hintKey(path)); found != hints.end())
        return &found->second;
    if (source.isNotEmpty())
        if (const auto found = hints.find("bank:" + source); found != hints.end())
            return &found->second;
    return nullptr;
}

juce::String SampleLibrary::hintKey(const juce::String& path)
{
    return path.replaceCharacter('\\', '/').toLowerCase();
}

juce::String SampleLibrary::sourceTitle(const SampleRecord& record) const
{
    if (record.source.isNotEmpty()) {
        for (const auto& bank : banks_)
            if (bank.id == record.source)
                return bank.title.isNotEmpty() ? bank.title : bank.id;
        return record.source;
    }
    return juce::File(record.root).getFileName();
}

std::vector<SampleLibrary::Root> SampleLibrary::roots() const
{
    std::vector<Root> out;
    for (const auto& folder : folders_)
        out.push_back({ folder, {} });
    if (includeBanks_)
        for (const auto& bank : banks_) {
            // A folder of the person's holding the banks scans them already.
            const auto inside = std::any_of(folders_.begin(), folders_.end(), [&bank](const juce::File& folder) {
                return bank.folder == folder || bank.folder.isAChildOf(folder);
            });
            if (!inside)
                out.push_back({ bank.folder, bank.id });
        }
    return out;
}

void SampleLibrary::reindex()
{
    index_.clear();
    for (std::size_t position = 0; position < samples_.size(); ++position)
        index_[samples_[position].path] = position;
}

void SampleLibrary::restartScan()
{
    if (!isScanning())
        return;
    // It ends on its own (nothing forgotten), then the owed one starts.
    scanner_->signalThreadShouldExit();
    pendingRescan_ = true;
}

void SampleLibrary::pruneToRoots()
{
    restartScan();
    std::set<juce::String> kept;
    for (const auto& root : roots())
        kept.insert(root.folder.getFullPathName());
    const auto before = samples_.size();
    std::erase_if(samples_, [&kept](const SampleRecord& record) { return !kept.contains(record.root); });
    if (samples_.size() == before)
        return;
    reindex();
    markDirty();
    notify(true);
}

int SampleLibrary::applyClassified(const std::vector<Classified>& results)
{
    auto count = 0;
    for (const auto& result : results) {
        auto* record = findMutable(result.path);
        // What the person, the bank or the name said stays.
        if (record == nullptr || record->userEdited || result.category.isEmpty()
            || (record->category.isNotEmpty() && record->classifier != "clap"))
            continue;
        record->category = result.category;
        record->confidence = result.confidence;
        record->classifier = "clap";
        ++count;
    }
    if (count > 0) {
        markDirty();
        notify(false);
    }
    return count;
}

void SampleLibrary::setAutoRescanMinutes(const int minutes)
{
    autoRescanMinutes_ = std::max(0, minutes);
    saveSettings();
}

void SampleLibrary::setClassifierEnabled(const bool enabled)
{
    classifierEnabled_ = enabled;
    saveSettings();
    if (enabled)
        classify(true);
}

juce::File SampleLibrary::classifierScript() const
{
    if (classifierScript_.existsAsFile())
        return classifierScript_;
    // Next to the executable when shipped, in the source tree otherwise.
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    for (const auto& candidate : {
             executable.getSiblingFile("tools").getChildFile("samples")
                 .getChildFile("classify_samples.py"),
             executable.getParentDirectory().getParentDirectory().getParentDirectory()
                 .getParentDirectory().getParentDirectory().getChildFile("tools")
                 .getChildFile("samples").getChildFile("classify_samples.py") })
        if (candidate.existsAsFile())
            return candidate;
    return classifierScript_;
}

void SampleLibrary::setClassifierScript(const juce::File& script)
{
    classifierScript_ = script;
    saveSettings();
}

void SampleLibrary::setPythonCommand(const juce::String& command)
{
    pythonCommand_ = command.trim().isNotEmpty() ? command.trim() : "python";
    saveSettings();
}

void SampleLibrary::rescan(const bool full)
{
    // Asked before the cache is read: once it is.
    if (!loaded_) {
        pendingRescan_ = true;
        pendingFull_ = pendingFull_ || full;
        return;
    }
    if (scanner_ != nullptr) {
        if (!scanner_->isThreadRunning()) {
            scanner_.reset();
        } else {
            pendingRescan_ = true;
            pendingFull_ = pendingFull_ || full;
            return;
        }
    }
    pendingRescan_ = false;
    pendingFull_ = false;
    std::map<juce::String, SampleRecord> known;
    for (const auto& record : samples_)
        known[record.path] = record;
    found_.store(0);
    analysed_.store(0);
    toAnalyse_.store(0);
    scanner_ = std::make_unique<Scanner>(*this, roots(), std::move(known), hints_, full, rulesOutdated_);
    scanner_->startThread(juce::Thread::Priority::low);
    lastScanMs_ = juce::Time::currentTimeMillis();
    notifyProgress();
}

void SampleLibrary::cancel()
{
    if (scanner_ != nullptr) {
        scanner_->stopThread(15'000);
        scanner_.reset();
    }
    if (classifier_ != nullptr) {
        classifier_->stopThread(15'000);
        classifier_.reset();
    }
}

bool SampleLibrary::isScanning() const noexcept
{
    return scanner_ != nullptr && scanner_->isThreadRunning();
}

bool SampleLibrary::isClassifying() const noexcept
{
    return classifier_ != nullptr && classifier_->isThreadRunning();
}

SampleLibrary::Progress SampleLibrary::progress() const
{
    Progress progress;
    progress.scanning = isScanning();
    progress.classifying = isClassifying();
    progress.found = found_.load();
    progress.analysed = analysed_.load();
    progress.toAnalyse = toAnalyse_.load();
    progress.classified = classified_.load();
    progress.toClassify = toClassify_.load();
    progress.message = classifierMessage_;
    return progress;
}

juce::String SampleLibrary::classifierStatus() const
{
    return classifierMessage_;
}

const SampleRecord* SampleLibrary::find(const juce::String& path) const
{
    const auto found = index_.find(path);
    return found != index_.end() && found->second < samples_.size()
               ? &samples_[found->second] : nullptr;
}

SampleRecord* SampleLibrary::findMutable(const juce::String& path)
{
    const auto found = index_.find(path);
    return found != index_.end() && found->second < samples_.size()
               ? &samples_[found->second] : nullptr;
}

bool SampleLibrary::setFavourite(const juce::String& path, const bool favourite)
{
    auto* record = findMutable(path);
    if (record == nullptr)
        return false;
    record->favourite = favourite;
    markDirty();
    notify(false);
    return true;
}

bool SampleLibrary::setRating(const juce::String& path, const int rating)
{
    auto* record = findMutable(path);
    if (record == nullptr)
        return false;
    record->rating = juce::jlimit(0, 5, rating);
    markDirty();
    notify(false);
    return true;
}

bool SampleLibrary::setCategory(const juce::String& path, const juce::String& category)
{
    auto* record = findMutable(path);
    if (record == nullptr)
        return false;
    record->category = category.trim().toLowerCase();
    record->userEdited = true;
    markDirty();
    notify(false);
    return true;
}

bool SampleLibrary::setType(const juce::String& path, const juce::String& type)
{
    auto* record = findMutable(path);
    if (record == nullptr)
        return false;
    record->type = type.trim().toLowerCase();
    record->userEdited = true;
    markDirty();
    notify(false);
    return true;
}

bool SampleLibrary::setBpm(const juce::String& path, const double bpm)
{
    auto* record = findMutable(path);
    if (record == nullptr)
        return false;
    record->bpm = bpm > 0.0 ? juce::jlimit(20.0, 400.0, std::round(bpm * 10.0) / 10.0) : 0.0;
    record->bpmSource = bpm > 0.0 ? "user" : juce::String();
    record->bpmConfidence = bpm > 0.0 ? 1.0f : 0.0f;
    record->type = typeFor(record->length, record->name, record->bpm);
    record->userEdited = true;
    markDirty();
    notify(false);
    return true;
}

bool SampleLibrary::setTags(const juce::String& path, const juce::StringArray& tags)
{
    auto* record = findMutable(path);
    if (record == nullptr)
        return false;
    record->tags.clear();
    for (auto tag : tags) {
        tag = tag.trim().toLowerCase();
        if (tag.isNotEmpty() && !record->tags.contains(tag))
            record->tags.add(tag);
    }
    record->userEdited = true;
    markDirty();
    notify(false);
    return true;
}

juce::StringArray SampleLibrary::distinctCategories() const
{
    juce::StringArray out;
    for (const auto& record : samples_)
        if (record.category.isNotEmpty() && !out.contains(record.category))
            out.add(record.category);
    out.sort(true);
    return out;
}

juce::StringArray SampleLibrary::distinctTypes() const
{
    juce::StringArray out;
    for (const auto& record : samples_)
        if (record.type.isNotEmpty() && !out.contains(record.type))
            out.add(record.type);
    out.sort(true);
    return out;
}

juce::StringArray SampleLibrary::distinctTags() const
{
    std::set<juce::String> unique;
    for (const auto& record : samples_)
        for (const auto& tag : record.tags)
            unique.insert(tag);
    juce::StringArray out;
    for (const auto& tag : unique)
        out.add(tag);
    return out;
}

void SampleLibrary::classify(const bool onlyUnclassified)
{
    if (isClassifying())
        return;
    juce::StringArray paths;
    for (const auto& record : samples_)
        if (record.analysed && !record.userEdited
            && (!onlyUnclassified || record.classifier != "clap"))
            paths.add(record.path);
    classified_.store(0);
    toClassify_.store(paths.size());
    if (paths.isEmpty()) {
        classifierMessage_ = "rien a classer";
        notifyProgress();
        return;
    }
    classifierMessage_ = "classification CLAP en cours";
    classifier_ = std::make_unique<Classifier>(*this, std::move(paths), classifierScript(),
                                               pythonCommand_, cacheFolder_);
    classifier_->startThread(juce::Thread::Priority::low);
    notifyProgress();
}

void SampleLibrary::applyClassification(std::vector<SampleRecord> results, const bool finished,
                                        juce::String message)
{
    for (const auto& result : results) {
        auto* record = findMutable(result.path);
        if (record == nullptr || record->userEdited)
            continue;
        if (result.category.isNotEmpty())
            record->category = result.category;
        for (const auto& tag : result.tags)
            if (!record->tags.contains(tag))
                record->tags.add(tag);
        record->confidence = result.confidence;
        record->classifier = "clap";
    }
    if (!results.empty()) {
        markDirty();
        notify(false);
    }
    if (finished) {
        classifierMessage_ = message.isNotEmpty()
                                 ? message
                                 : juce::String(classified_.load()) + " classes par CLAP";
        if (classifier_ != nullptr && !classifier_->isThreadRunning())
            classifier_.reset();
    } else if (message.isNotEmpty()) {
        classifierMessage_ = message;
    }
    notifyProgress();
}

void SampleLibrary::mergeRecords(std::vector<SampleRecord> records, const bool finished, const bool complete,
                                 std::set<juce::String> present, std::vector<juce::File> roots)
{
    auto structure = false;
    // A folder removed (the banks left out) while it was scanned: what it
    // found is not kept.
    std::set<juce::String> current;
    for (const auto& root : this->roots())
        current.insert(root.folder.getFullPathName());
    std::erase_if(records, [&current](const SampleRecord& record) { return !current.contains(record.root); });
    for (auto& incoming : records) {
        if (auto* existing = findMutable(incoming.path)) {
            // What the person set stays theirs; what CLAP said stays until
            // the file itself changes.
            incoming.favourite = existing->favourite;
            incoming.rating = existing->rating;
            if (existing->userEdited) {
                incoming.userEdited = true;
                incoming.category = existing->category;
                incoming.type = existing->type;
                incoming.tags = existing->tags;
                incoming.bpm = existing->bpm;
                incoming.bpmSource = existing->bpmSource;
                incoming.bpmConfidence = existing->bpmConfidence;
                incoming.key = existing->key;
                incoming.keySource = existing->keySource;
                incoming.keyConfidence = existing->keyConfidence;
            } else if (existing->classifier == "clap" && incoming.classifier != "bank"
                       && existing->size == incoming.size
                       && existing->modified == incoming.modified) {
                incoming.classifier = existing->classifier;
                incoming.category = existing->category;
                incoming.tags = existing->tags;
                incoming.confidence = existing->confidence;
            }
            *existing = std::move(incoming);
        } else {
            index_[incoming.path] = samples_.size();
            samples_.push_back(std::move(incoming));
            structure = true;
        }
    }
    if (finished) {
        // Only a scan read to its end says what is gone.
        if (complete && rulesOutdated_) {
            rulesOutdated_ = false;
            setSetting("sampleCategoryRules", categoryRulesVersion);
        }
        if (complete) {
            std::set<juce::String> scanned;
            for (const auto& root : roots)
                scanned.insert(root.getFullPathName());
            const auto before = samples_.size();
            std::erase_if(samples_, [&present, &scanned](const SampleRecord& record) {
                return scanned.contains(record.root) && !present.contains(record.path);
            });
            if (samples_.size() != before) {
                structure = true;
                reindex();
            }
        }
        if (scanner_ != nullptr && !scanner_->isThreadRunning())
            scanner_.reset();
        markDirty();
        saveCache();
        if (pendingRescan_) {
            const auto full = pendingFull_;
            juce::MessageManager::callAsync([this, full] {
                if (!isScanning())
                    rescan(full);
            });
        } else if (classifierEnabled_) {
            classify(true);
        }
    } else if (!records.empty()) {
        markDirty();
    }
    notify(structure || !records.empty());
    notifyProgress();
}

void SampleLibrary::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void SampleLibrary::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void SampleLibrary::notify(const bool structure)
{
    listeners_.call([structure](Listener& listener) { listener.sampleLibraryChanged(structure); });
}

void SampleLibrary::notifyProgress()
{
    listeners_.call([](Listener& listener) { listener.sampleLibraryProgress(); });
}

void SampleLibrary::markDirty()
{
    dirty_ = true;
}

void SampleLibrary::saveNow()
{
    saveCache();
    saveSettings();
}

juce::var SampleLibrary::setting(const juce::String& key, const juce::var& fallback)
{
    auto& settings = settingsFile();
    if (!settings.containsKey(key))
        return fallback;
    const auto text = settings.getValue(key);
    if (fallback.isBool())
        return text == "1" || text.equalsIgnoreCase("true");
    if (fallback.isInt() || fallback.isInt64() || fallback.isDouble())
        return text.getDoubleValue();
    return text;
}

void SampleLibrary::setSetting(const juce::String& key, const juce::var& value)
{
    auto& settings = settingsFile();
    settings.setValue(key, value.isBool() ? juce::var(static_cast<bool>(value) ? 1 : 0) : value);
    settings.saveIfNeeded();
}

void SampleLibrary::timerCallback()
{
    if (loaded_ && dirty_ && !isScanning())
        saveCache();
    if (scanner_ != nullptr && !scanner_->isThreadRunning())
        scanner_.reset();
    if (pendingRescan_ && !isScanning())
        rescan(pendingFull_);
    if (classifier_ != nullptr && !classifier_->isThreadRunning())
        classifier_.reset();
    if (loaded_ && autoRescanMinutes_ > 0 && !isScanning() && !roots().empty()
        && juce::Time::currentTimeMillis() - lastScanMs_
               > static_cast<juce::int64>(autoRescanMinutes_) * 60'000)
        rescan(false);
}

juce::PropertiesFile& SampleLibrary::settingsFile()
{
    return otherSettings_ != nullptr ? *otherSettings_ : *properties_.getUserSettings();
}

void SampleLibrary::loadSettings()
{
    auto* settings = &settingsFile();
    cacheFolder_ = juce::File(settings->getValue("sampleCacheFolder"));
    if (cacheFolder_ == juce::File {} || !cacheFolder_.isDirectory()) {
        cacheFolder_ = defaultCacheFolder();
        (void) cacheFolder_.createDirectory();
    }
    juce::StringArray folders;
    folders.addLines(settings->getValue("sampleFolders"));
    if (folders.isEmpty() && otherSettings_ == nullptr) {
        // The folders the narrow browser already knows, so the explorer
        // opens on them.
        juce::PropertiesFile::Options options;
        options.applicationName = "CyDAW";
        options.filenameSuffix = ".library.settings";
        options.folderName = "CyDAW";
        options.osxLibrarySubFolder = "Application Support";
        juce::ApplicationProperties browser;
        browser.setStorageParameters(options);
        folders.addLines(browser.getUserSettings()->getValue("sampleFolders"));
    }
    folders.removeEmptyStrings();
    for (const auto& folder : folders)
        if (juce::File(folder).isDirectory())
            folders_.emplace_back(folder);
    autoRescanMinutes_ = settings->getIntValue("sampleAutoRescanMinutes", 10);
    includeBanks_ = settings->getBoolValue("sampleIncludeBanks", true);
    rulesOutdated_ = settings->getIntValue("sampleCategoryRules", 0) != categoryRulesVersion;
    classifierEnabled_ = settings->getBoolValue("sampleClassifier", false);
    classifierScript_ = juce::File(settings->getValue("sampleClassifierScript"));
    pythonCommand_ = settings->getValue("samplePython", "python");
}

void SampleLibrary::saveSettings()
{
    auto* settings = &settingsFile();
    if (!cacheFolderGiven_)
        settings->setValue("sampleCacheFolder", cacheFolder_.getFullPathName());
    juce::StringArray folders;
    for (const auto& folder : folders_)
        folders.add(folder.getFullPathName());
    settings->setValue("sampleFolders", folders.joinIntoString("\n"));
    settings->setValue("sampleAutoRescanMinutes", autoRescanMinutes_);
    settings->setValue("sampleIncludeBanks", includeBanks_);
    settings->setValue("sampleClassifier", classifierEnabled_);
    settings->setValue("sampleClassifierScript", classifierScript_.getFullPathName());
    settings->setValue("samplePython", pythonCommand_);
    settings->saveIfNeeded();
}

namespace {
constexpr auto binaryIndexName = "sample-library.bin";
constexpr int binaryMagic = 0x4c535943; // "CYSL"
constexpr int binaryVersion = 2;
} // namespace

std::vector<SampleRecord> SampleLibrary::readCacheFile(const juce::File& folder)
{
    std::vector<SampleRecord> records;
    if (juce::FileInputStream in(folder.getChildFile(binaryIndexName)); in.openedOk()) {
        juce::BufferedInputStream buffered(in, 1 << 16);
        if (buffered.readInt() == binaryMagic && buffered.readInt() == binaryVersion) {
            const auto count = buffered.readInt();
            records.reserve(static_cast<std::size_t>(std::clamp(count, 0, 1'000'000)));
            for (auto index = 0; index < count; ++index) {
                auto record = read(buffered);
                if (!record)
                    break;
                records.push_back(std::move(*record));
            }
            return records;
        }
    }
    // The first library, written as JSON.
    const auto document = juce::JSON::parse(folder.getChildFile(indexFileName));
    if (const auto* list = document.getProperty("samples", juce::var()).getArray())
        for (const auto& value : *list)
            if (auto record = restore(value))
                records.push_back(std::move(*record));
    return records;
}

bool SampleLibrary::writeCacheFile(const juce::File& folder, const std::vector<SampleRecord>& records)
{
    (void) folder.createDirectory();
    const auto target = folder.getChildFile(binaryIndexName);
    const auto temporary = target.getSiblingFile("sample-library.tmp");
    {
        juce::FileOutputStream out(temporary, 1 << 16);
        if (!out.openedOk())
            return false;
        out.setPosition(0);
        out.truncate();
        auto& buffered = out;
        buffered.writeInt(binaryMagic);
        buffered.writeInt(binaryVersion);
        buffered.writeInt(static_cast<int>(records.size()));
        for (const auto& record : records)
            write(buffered, record);
        out.flush();
        if (out.getStatus().failed())
            return false;
    }
    return temporary.moveFileTo(target);
}

void SampleLibrary::loadCache()
{
    samples_.clear();
    index_.clear();
    loaded_ = false;
    // Read off the message thread: a library of the banks holds tens of
    // thousands of sounds.
    const auto generation = ++loadGeneration_;
    io_.addJob([library = juce::WeakReference<SampleLibrary>(this), folder = cacheFolder_, generation] {
        auto records = readCacheFile(folder);
        juce::MessageManager::callAsync([library, records = std::move(records), generation]() mutable {
            if (auto* owner = library.get())
                owner->cacheLoaded(std::move(records), generation);
        });
    });
}

void SampleLibrary::cacheLoaded(std::vector<SampleRecord> records, const int generation)
{
    if (generation != loadGeneration_)
        return;
    std::set<juce::String> kept;
    for (const auto& root : roots())
        kept.insert(root.folder.getFullPathName());
    for (auto& record : records)
        if (kept.contains(record.root) && !index_.contains(record.path)) {
            index_[record.path] = samples_.size();
            samples_.push_back(std::move(record));
        }
    if (hints_ != nullptr)
        for (auto& record : samples_)
            if (const auto* hint = hintOf(*hints_, record.path, record.source))
                applyHint(record, *hint);
    loaded_ = true;
    dirty_ = false;
    notify(true);
    notifyProgress();
    if (pendingRescan_)
        rescan(pendingFull_);
}

void SampleLibrary::saveCache()
{
    if (!loaded_)
        return;
    // One write at a time; one asked for while one runs follows it.
    if (saving_.load()) {
        saveAgain_ = true;
        return;
    }
    saving_ = true;
    saveAgain_ = false;
    dirty_ = false;
    io_.addJob([library = juce::WeakReference<SampleLibrary>(this), folder = cacheFolder_, snapshot = samples_] {
        const auto written = writeCacheFile(folder, snapshot);
        juce::MessageManager::callAsync([library, written] {
            if (auto* owner = library.get()) {
                owner->saving_ = false;
                if (!written || owner->saveAgain_)
                    owner->dirty_ = true;
            }
        });
    });
}

} // namespace cydaw::app
