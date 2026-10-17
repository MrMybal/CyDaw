#include "AudioTrackController.h"
#include "AudioEditorComponent.h"
#include "ClipLauncherComponent.h"
#include "MixerComponent.h"
#include "GraphPresetFile.h"
#include "GraphPolySynthRenderer.h"
#include "LibraryBrowserComponent.h"
#include "LibraryDragPayload.h"
#include "Localization.h"
#include "NativeDspRuntime.h"
#include "NativeDspGraphState.h"
#include "JsonValue.h"
#include "NodeGraphComponent.h"
#include "PianoRollComponent.h"
#include "ProjectRecovery.h"
#include "ProjectImport.h"
#include "WindowsPluginBridge.h"

#include <cydaw/engine/NativeDspCompiler.h>
#include <cydaw/engine/GraphDefinitionCompiler.h>
#include <cydaw/graph/PrimitiveRegistry.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <set>
#include <memory>

namespace {

bool writeToneFile(const juce::File& file, const double frequency,
                   const double sampleRate = 48'000.0)
{
    auto fileOutput = file.createOutputStream();
    if (fileOutput == nullptr || !fileOutput->setPosition(0) || fileOutput->truncate().failed())
        return false;

    std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(output,
                                         juce::AudioFormatWriterOptions()
                                             .withSampleRate(sampleRate)
                                             .withNumChannels(2)
                                             .withBitsPerSample(24));
    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> buffer(
        2, static_cast<int>(std::llround(sampleRate)));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        const auto value = static_cast<float>(0.2 * std::sin(juce::MathConstants<double>::twoPi
                                                             * frequency
                                                             * static_cast<double>(sample) / sampleRate));
        buffer.setSample(0, sample, value);
        buffer.setSample(1, sample, value);
    }
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

bool writeMidiFixture(const juce::File& file)
{
    constexpr int ticksPerQuarter = 960;
    juce::MidiMessageSequence conductor;
    auto conductorName = juce::MidiMessage::textMetaEvent(3, "Conductor");
    conductorName.setTimeStamp(0.0);
    conductor.addEvent(std::move(conductorName));
    auto tempo = juce::MidiMessage::tempoMetaEvent(600'000);
    tempo.setTimeStamp(0.0);
    conductor.addEvent(std::move(tempo));
    auto signature = juce::MidiMessage::timeSignatureMetaEvent(4, 4);
    signature.setTimeStamp(0.0);
    conductor.addEvent(std::move(signature));
    auto conductorEnd = juce::MidiMessage::endOfTrack();
    conductorEnd.setTimeStamp(4.0 * ticksPerQuarter);
    conductor.addEvent(std::move(conductorEnd));

    const auto addNote = [](juce::MidiMessageSequence& sequence,
                            const int channel,
                            const int note,
                            const double start,
                            const double end,
                            const float velocity) {
        auto noteOn = juce::MidiMessage::noteOn(channel, note, velocity);
        noteOn.setTimeStamp(start);
        sequence.addEvent(std::move(noteOn));
        auto noteOff = juce::MidiMessage::noteOff(channel, note);
        noteOff.setTimeStamp(end);
        sequence.addEvent(std::move(noteOff));
    };
    juce::MidiMessageSequence keys;
    auto keysName = juce::MidiMessage::textMetaEvent(3, "Keys");
    keysName.setTimeStamp(0.0);
    keys.addEvent(std::move(keysName));
    addNote(keys, 1, 60, 0.0, 960.0, 0.5f);
    addNote(keys, 2, 67, 480.0, 1'440.0, 0.75f);
    keys.updateMatchedPairs();
    auto keysEnd = juce::MidiMessage::endOfTrack();
    keysEnd.setTimeStamp(1'920.0);
    keys.addEvent(std::move(keysEnd));

    juce::MidiMessageSequence bass;
    auto bassName = juce::MidiMessage::textMetaEvent(3, "Bass");
    bassName.setTimeStamp(0.0);
    bass.addEvent(std::move(bassName));
    addNote(bass, 1, 36, 1'920.0, 2'880.0, 0.9f);
    bass.updateMatchedPairs();
    auto bassEnd = juce::MidiMessage::endOfTrack();
    bassEnd.setTimeStamp(3'840.0);
    bass.addEvent(std::move(bassEnd));

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ticksPerQuarter);
    midiFile.addTrack(conductor);
    midiFile.addTrack(keys);
    midiFile.addTrack(bass);
    auto output = file.createOutputStream();
    if (output == nullptr || !output->setPosition(0)
        || output->truncate().failed())
        return false;
    return midiFile.writeTo(*output, 1);
}

bool writeAudioEditFixture(const juce::File& file,
                           const int channelCount = 2,
                           const double sampleRate = 48'000.0)
{
    auto fileOutput = file.createOutputStream();
    if (fileOutput == nullptr || !fileOutput->setPosition(0)
        || fileOutput->truncate().failed())
        return false;
    std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(
        output, juce::AudioFormatWriterOptions()
                    .withSampleRate(sampleRate)
                    .withNumChannels(channelCount)
                    .withBitsPerSample(24));
    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> buffer(
        channelCount, static_cast<int>(std::llround(sampleRate)));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        const auto phase = juce::MathConstants<double>::twoPi * 100.0
                           * static_cast<double>(sample) / sampleRate;
        buffer.setSample(0, sample,
                         static_cast<float>(0.1 + 0.1 * std::sin(phase)));
        if (channelCount > 1)
            buffer.setSample(1, sample,
                             static_cast<float>(-0.05 + 0.05 * std::cos(phase)));
    }
    return writer->writeFromAudioSampleBuffer(buffer, 0,
                                               buffer.getNumSamples());
}

bool writeTempoFixture(const juce::File& file,
                       const double bpm,
                       const double sampleRate = 48'000.0)
{
    auto fileOutput = file.createOutputStream();
    if (fileOutput == nullptr || !fileOutput->setPosition(0)
        || fileOutput->truncate().failed())
        return false;
    std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(
        output, juce::AudioFormatWriterOptions()
                    .withSampleRate(sampleRate)
                    .withNumChannels(2)
                    .withBitsPerSample(24));
    if (writer == nullptr)
        return false;
    const auto sampleCount = static_cast<int>(std::llround(sampleRate * 12.0));
    const auto beatSamples = static_cast<int>(std::llround(
        sampleRate * 60.0 / bpm));
    const auto clickSamples = static_cast<int>(std::llround(sampleRate * 0.025));
    juce::AudioBuffer<float> buffer(2, sampleCount);
    buffer.clear();
    for (int beat = 0; beat < sampleCount; beat += beatSamples)
        for (int offset = 0; offset < clickSamples
                             && beat + offset < sampleCount; ++offset) {
            const auto envelope = std::exp(
                -8.0 * static_cast<double>(offset)
                / static_cast<double>(clickSamples));
            const auto value = static_cast<float>(
                envelope * std::sin(juce::MathConstants<double>::twoPi
                                    * 1'200.0
                                    * static_cast<double>(offset)
                                    / sampleRate));
            buffer.setSample(0, beat + offset, value);
            buffer.setSample(1, beat + offset, value);
        }
    return writer->writeFromAudioSampleBuffer(buffer, 0, sampleCount);
}

bool readAudioBlock(const juce::File& file,
                    const juce::int64 startSample,
                    const int sampleCount,
                    juce::AudioBuffer<float>& destination)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(file));
    if (reader == nullptr || reader->numChannels == 0)
        return false;
    destination.setSize(static_cast<int>(reader->numChannels), sampleCount,
                        false, true, false);
    destination.clear();
    return reader->read(&destination, 0, sampleCount, startSample, true, true);
}

bool writeBinaryMagic(const juce::File& file,
                      const std::array<std::uint8_t, 4>& magic)
{
    auto output = file.createOutputStream();
    return output != nullptr
           && output->write(magic.data(), static_cast<std::size_t>(magic.size()));
}

// What a graph is, as opposed to how it happens to be written down: its
// nodes, its links and their ports. Two serialisations of the same graph
// differ in how completely they spell their classes out, so comparing the
// text answers a question nobody asked.
juce::String graphSignature(cydaw::app::NodeGraphComponent& graph)
{
    const auto state = graph.serialise();
    const auto* object = state.getDynamicObject();
    if (object == nullptr)
        return "no graph";
    juce::String signature;
    for (const auto* key : { "nodes", "connections" })
        if (const auto* values = object->getProperty(key).getArray()) {
            signature << key << "(" << values->size() << ")";
            for (const auto& value : *values)
                if (const auto* entry = value.getDynamicObject())
                    for (const auto* field : { "id", "type", "source",
                                               "destination", "sourcePort",
                                               "destinationPort" })
                        if (entry->hasProperty(field))
                            signature << " " << field << "="
                                      << entry->getProperty(field).toString();
            signature << " | ";
        }
    return signature;
}

bool expect(const bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

class CountingAudioSource final : public juce::AudioSource {
public:
    void prepareToPlay(int, double) override {}
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& output) override
    {
        for (int sample = 0; sample < output.numSamples; ++sample) {
            const auto value = static_cast<float>(++nextSample_);
            for (int channel = 0; channel < output.buffer->getNumChannels(); ++channel)
                output.buffer->setSample(channel, output.startSample + sample, value);
        }
    }

private:
    int nextSample_ {};
};

// A sine at one frequency, the probe the curve tests send through a graph.
class SineAudioSource final : public juce::AudioSource {
public:
    SineAudioSource(const double frequencyHz, const float amplitude)
        : frequencyHz_(frequencyHz), amplitude_(amplitude) {}
    void prepareToPlay(int, const double sampleRate) override
    {
        sampleRate_ = sampleRate;
        phase_ = 0.0;
    }
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& output) override
    {
        for (int sample = 0; sample < output.numSamples; ++sample) {
            const auto value = amplitude_ * static_cast<float>(std::sin(phase_));
            phase_ += 2.0 * juce::MathConstants<double>::pi * frequencyHz_
                      / sampleRate_;
            if (phase_ > 2.0 * juce::MathConstants<double>::pi)
                phase_ -= 2.0 * juce::MathConstants<double>::pi;
            for (int channel = 0; channel < output.buffer->getNumChannels(); ++channel)
                output.buffer->setSample(channel, output.startSample + sample, value);
        }
    }

private:
    double frequencyHz_ {};
    float amplitude_ {};
    double sampleRate_ { 48'000.0 };
    double phase_ {};
};

// The gain a native graph applies to a sine, in dB: a second of it rendered
// through the same source a track uses, the first half discarded while the
// filters settle.
double measuredGainDb(const cydaw::engine::NativeDspGraph& graph,
                      const double frequencyHz)
{
    constexpr auto amplitude = 0.25f;
    SineAudioSource sine(frequencyHz, amplitude);
    cydaw::app::GainPanAudioSource renderer(sine);
    renderer.prepareToPlay(512, 48'000.0);
    if (renderer.setNativeDspGraph(graph).failed())
        return -999.0;
    juce::AudioBuffer<float> buffer(2, 512);
    auto sumSquares = 0.0;
    auto counted = 0;
    for (int block = 0; block < 96; ++block) {
        juce::AudioSourceChannelInfo info(&buffer, 0, 512);
        renderer.getNextAudioBlock(info);
        if (block < 48)
            continue;
        for (int sample = 0; sample < 512; ++sample) {
            const auto value = static_cast<double>(buffer.getSample(0, sample));
            sumSquares += value * value;
            ++counted;
        }
    }
    const auto rmsOut = std::sqrt(sumSquares / std::max(1, counted));
    const auto rmsIn = static_cast<double>(amplitude) / std::sqrt(2.0);
    return 20.0 * std::log10(std::max(1.0e-9, rmsOut / rmsIn));
}

// The peak a native graph leaves on a sine of a given amplitude, in dB:
// what a compressor's detector reads and what its card's axis means.
double measuredPeakDb(const cydaw::engine::NativeDspGraph& graph,
                      const double frequencyHz, const float amplitude)
{
    SineAudioSource sine(frequencyHz, amplitude);
    cydaw::app::GainPanAudioSource renderer(sine);
    renderer.prepareToPlay(512, 48'000.0);
    if (renderer.setNativeDspGraph(graph).failed())
        return -999.0;
    juce::AudioBuffer<float> buffer(2, 512);
    auto peak = 0.0f;
    for (int block = 0; block < 96; ++block) {
        juce::AudioSourceChannelInfo info(&buffer, 0, 512);
        renderer.getNextAudioBlock(info);
        if (block < 48)
            continue;
        peak = std::max(peak, buffer.getMagnitude(0, 0, 512));
    }
    return 20.0 * std::log10(std::max(1.0e-9, static_cast<double>(peak)));
}

class ConstantAudioSource final : public juce::AudioSource {
public:
    explicit ConstantAudioSource(const float value) : value_(value) {}
    void prepareToPlay(int, double) override {}
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& output) override
    {
        for (int channel = 0; channel < output.buffer->getNumChannels(); ++channel)
            for (int sample = 0; sample < output.numSamples; ++sample)
                output.buffer->setSample(channel, output.startSample + sample, value_);
    }

private:
    float value_ {};
};

} // namespace


// The importers on fixtures written here: a DAWproject zip, an Ableton
// set, a Bitwig project assembled byte by byte.
class BigEndianWriter {
public:
    void u8(const int value) { bytes_.push_back(static_cast<std::uint8_t>(value)); }
    void i32(const std::int32_t value)
    {
        const auto raw = static_cast<std::uint32_t>(value);
        u8(static_cast<int>(raw >> 24)); u8(static_cast<int>((raw >> 16) & 0xff));
        u8(static_cast<int>((raw >> 8) & 0xff)); u8(static_cast<int>(raw & 0xff));
    }
    void f64(const double value)
    {
        std::uint64_t raw {};
        std::memcpy(&raw, &value, sizeof(raw));
        for (auto shift = 56; shift >= 0; shift -= 8)
            u8(static_cast<int>((raw >> shift) & 0xff));
    }
    void text(const juce::String& value)
    {
        i32(static_cast<std::int32_t>(value.getNumBytesAsUTF8()));
        for (const auto* p = value.toRawUTF8(); *p != 0; ++p)
            u8(static_cast<std::uint8_t>(*p));
    }
    void field(const int id, const int type) { i32(id); u8(type); }
    void stringField(const int id, const juce::String& value) { field(id, 0x08); text(value); }
    void doubleField(const int id, const double value) { field(id, 0x07); f64(value); }
    void boolField(const int id, const bool value) { field(id, 0x05); u8(value ? 1 : 0); }
    void int8Field(const int id, const int value) { field(id, 0x01); u8(value); }
    void beginObject(const int id, const int classId) { field(id, 0x09); i32(classId); }
    void beginRootObject(const int classId) { i32(classId); }
    void endObject() { i32(0); }
    void beginList(const int id) { field(id, 0x12); }
    void listObject(const int classId) { i32(classId); }
    void endList() { i32(3); }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    [[nodiscard]] std::size_t size() const { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
};

bool writeBitwigFixture(const juce::File& file, const juce::File& audioFile)
{
    BigEndianWriter meta;
    meta.i32(4);
    meta.text("meta");
    meta.i32(1); meta.text("application_version_name"); meta.u8(0x08); meta.text("5.0.4");
    meta.i32(0);
    const auto contentOffset = 42 + meta.size();

    BigEndianWriter body;
    body.beginRootObject(46);
    body.stringField(68, "Fixture");
    body.beginObject(3033, 215);
    body.beginObject(529, 1217); body.doubleField(712, 95.0); body.endObject();
    body.beginObject(530, 2036); body.field(7358, 0x03); body.i32(0x10000023); body.endObject();
    body.endObject();
    body.beginObject(1245, 477);
    body.beginList(1246);
    // An instrument track with an arrangement clip of two notes, looped
    // over its length.
    body.listObject(144);
    body.stringField(347, "Lead");
    body.int8Field(2343, 0);
    body.beginObject(357, 171);
    body.beginObject(420, 1220); body.doubleField(712, 1.0); body.endObject();
    body.beginObject(421, 1219); body.doubleField(712, 0.25); body.endObject();
    body.beginObject(422, 1230); body.boolField(210, false); body.endObject();
    body.boolField(424, true);
    body.endObject();
    body.beginObject(349, 225);
    body.beginObject(568, 82);
    body.beginList(543);
    body.listObject(238);
    body.stringField(646, "Melody");
    body.doubleField(687, 8.0);
    body.doubleField(38, 4.0);
    body.beginObject(648, 261);
    body.beginObject(2447, 615);
    body.boolField(2449, true);
    body.beginObject(2450, 88); body.doubleField(687, 0.0); body.doubleField(38, 2.0); body.endObject();
    body.beginObject(2444, 617); body.doubleField(687, 0.0); body.endObject();
    body.endObject();
    body.beginObject(1180, 1742);
    body.beginList(6347);
    body.listObject(206);
    body.int8Field(238, 62);
    body.beginList(543);
    body.listObject(900); body.doubleField(687, 0.0); body.doubleField(38, 1.0); body.doubleField(239, 0.75); body.endObject();
    body.listObject(900); body.doubleField(687, 1.0); body.doubleField(38, 0.5); body.doubleField(239, 0.5); body.endObject();
    body.endList();
    body.endObject();
    body.endList();
    body.endObject();
    body.endObject();
    body.endObject();
    body.endList();
    body.endObject();
    body.endObject();
    body.endObject();
    // An audio track with a launcher clip playing the fixture raw.
    body.listObject(144);
    body.stringField(347, "");
    body.int8Field(2343, 1);
    body.beginObject(357, 171);
    body.beginObject(420, 1220); body.doubleField(712, 0.6812920690579614); body.endObject();
    body.endObject();
    body.beginObject(350, 30);
    body.beginList(561);
    body.listObject(227);
    body.beginObject(576, 238);
    body.stringField(646, "Hit");
    body.doubleField(687, 0.0);
    body.doubleField(38, 4.0);
    body.beginObject(648, 261);
    body.beginObject(1180, 1742);
    body.beginList(6347);
    body.listObject(206);
    body.beginList(543);
    body.listObject(212);
    body.doubleField(687, 0.0);
    body.doubleField(38, 4.0);
    body.doubleField(3076, 0.0);
    body.doubleField(3077, 1.0);
    body.beginObject(648, 159);
    body.beginObject(525, 475);
    body.beginObject(1204, 1193);
    body.stringField(4767, audioFile.getFileName());
    body.beginObject(4766, 1190);
    body.stringField(1298, audioFile.getFileName());
    body.stringField(3386, "");
    body.beginObject(3387, 42); body.stringField(59, audioFile.getFullPathName()); body.endObject();
    body.endObject();
    body.endObject();
    body.endObject();
    body.doubleField(149, 1.0);
    body.int8Field(1200, 0);
    body.endObject();
    body.endObject();
    body.endList();
    body.endObject();
    body.endList();
    body.endObject();
    body.endObject();
    body.endObject();
    body.endObject();
    body.endList();
    body.endObject();
    body.endObject();
    body.endList();
    body.endObject();
    body.endObject();

    juce::MemoryOutputStream out;
    // "BtWg", then hex: format, flags, a count, and at characters 16 to 24
    // the offset of the object tree.
    const auto header = "BtWg000300040077" + juce::String::toHexString(static_cast<int>(contentOffset)).paddedLeft('0', 8)
                        + "000000000000000000";
    out.write(header.toRawUTF8(), 42);
    out.write(meta.bytes().data(), meta.size());
    out.write(body.bytes().data(), body.size());
    return file.replaceWithData(out.getData(), out.getDataSize());
}

bool runProjectImportTests(const juce::File& tempRoot, const juce::File& toneFile)
{
    using namespace cydaw::app::project_import;
    auto passed = true;
    // CYDAW_IMPORT_PROBE=<project file>: read that file and print what the
    // importer makes of it, for a look at a real project.
    if (const auto probe = juce::SystemStats::getEnvironmentVariable("CYDAW_IMPORT_PROBE", {});
        probe.isNotEmpty()) {
        ImportedProject project;
        const auto result = importProjectFile(juce::File(probe), project);
        std::cout << "probe " << probe << ": " << (result.wasOk() ? "ok" : result.getErrorMessage())
                  << std::endl << describeImportedProject(project) << std::endl;
        for (const auto& track : project.tracks)
            for (const auto& clip : track.audioClips)
                std::cout << "  audio " << track.name << " scene " << clip.launcherScene
                          << " start " << clip.startSeconds << " dur " << clip.durationSeconds
                          << " src " << clip.sourceOffsetSeconds << "+" << clip.sourceDurationSeconds
                          << " " << clip.file.getFullPathName() << std::endl;
        for (const auto& track : project.tracks)
            for (const auto& device : track.chain) {
                std::cout << "  device " << track.name << ": " << device.name << " [" << device.type << "]"
                          << (device.plugin ? " plugin" : "") << (device.instrument ? " instrument" : "")
                          << (device.noteEffect ? " note-effect" : "") << std::endl;
                for (const auto& [key, value] : device.settings)
                    std::cout << "    " << key << " = " << value << std::endl;
            }
    }
    const auto near = [](const double a, const double b, const double tolerance = 0.02) {
        return std::abs(a - b) <= tolerance;
    };

    // DAWproject: two tracks, one master, notes, a warped audio clip, a
    // launcher clip.
    {
        const auto folder = tempRoot.getChildFile("import-dawproject");
        (void) folder.createDirectory();
        const auto file = folder.getChildFile("fixture.dawproject");
        const juce::String xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0"><Application name="Test" version="1"/>
<Transport><Tempo unit="bpm" value="100"/><TimeSignature numerator="3" denominator="4"/></Transport>
<Structure>
 <Track contentType="notes" id="t1" name="Keys" color="#ff8800"><Channel id="c1" role="regular" solo="true"><Volume unit="linear" value="0.5"/><Pan unit="normalized" value="0.25"/><Mute value="false"/><Devices><Vst3Plugin deviceRole="instrument" deviceName="Synth"/></Devices></Channel></Track>
 <Track contentType="audio" id="t2" name="Drums"><Channel id="c2" role="regular"><Volume unit="linear" value="1"/></Channel></Track>
 <Track id="master"><Channel role="master"><Volume unit="linear" value="0.7"/></Channel></Track>
</Structure>
<Arrangement><Lanes timeUnit="beats">
 <Lanes track="t1"><Clips><Clip time="4" duration="8" name="Riff" playStart="0"><Notes><Note time="0" duration="1" key="60" vel="0.9"/><Note time="2" duration="0.5" key="64" vel="0.5"/><Note time="9" duration="1" key="67" vel="0.5"/></Notes></Clip></Clips></Lanes>
 <Lanes track="t2"><Clips><Clip time="0" duration="4" name="Loop"><Warps contentTimeUnit="seconds" timeUnit="beats"><Audio algorithm="raw" channels="1" duration="1" sampleRate="44100"><File path="audio/tone.wav"/></Audio><Warp time="0" contentTime="0"/><Warp time="4" contentTime="1"/></Warps></Clip></Clips></Lanes>
</Lanes></Arrangement>
<Scenes><Scene name="S1"><Lanes><ClipSlot track="t1"><Clip time="0" duration="4" name="Launch"><Notes><Note time="0" duration="1" key="48" vel="1"/></Notes></Clip></ClipSlot></Lanes></Scene></Scenes>
</Project>)";
        juce::ZipFile::Builder builder;
        builder.addEntry(std::make_unique<juce::MemoryInputStream>(xml.toRawUTF8(), xml.getNumBytesAsUTF8(), true), 9,
                         "project.xml", juce::Time::getCurrentTime());
        builder.addFile(toneFile, 0, "audio/tone.wav");
        {
            juce::FileOutputStream out(file);
            passed = expect(out.openedOk() && builder.writeToStream(out, nullptr), "write dawproject fixture") && passed;
        }
        ImportedProject project;
        const auto result = importProjectFile(file, project);
        passed = expect(result.wasOk(), ("dawproject read: " + result.getErrorMessage()).toRawUTF8()) && passed;
        passed = expect(project.format == "dawproject" && near(project.tempo, 100.0) && project.numerator == 3,
                        "dawproject tempo and signature") && passed;
        passed = expect(project.tracks.size() == 2, "dawproject: two tracks, the master apart") && passed;
        if (project.tracks.size() == 2) {
            const auto& keys = project.tracks[0];
            const auto& drums = project.tracks[1];
            passed = expect(keys.midi && keys.name == "Keys" && keys.hadInstrument && keys.solo
                                && near(keys.gainDb, -6.02, 0.1) && near(keys.pan, -0.5)
                                && keys.colour == 0xffff8800u,
                            "dawproject: MIDI track mixer and colour") && passed;
            passed = expect(keys.midiClips.size() == 2, "dawproject: arrangement and launcher clips") && passed;
            if (keys.midiClips.size() == 2) {
                const auto& riff = keys.midiClips[0];
                passed = expect(near(riff.startBeats, 4.0) && near(riff.lengthBeats, 8.0)
                                    && riff.notes.size() == 2 && riff.notes[0].note == 60
                                    && near(riff.notes[0].velocity, 0.9) && riff.launcherScene < 0,
                                "dawproject: notes inside the clip only") && passed;
                const auto& launch = keys.midiClips[1];
                passed = expect(launch.launcherScene == 0 && near(launch.startBeats, 0.0)
                                    && launch.notes.size() == 1 && launch.notes[0].note == 48,
                                "dawproject: launcher clip on scene 0") && passed;
            }
            passed = expect(!drums.midi && drums.audioClips.size() == 1, "dawproject: one audio clip") && passed;
            if (drums.audioClips.size() == 1) {
                const auto& loop = drums.audioClips[0];
                passed = expect(loop.file.existsAsFile() && near(loop.durationSeconds, 2.4)
                                    && near(loop.sourceDurationSeconds, 1.0) && near(loop.sourceOffsetSeconds, 0.0),
                                "dawproject: warped clip keeps its timeline length and source length") && passed;
                passed = expect(loop.file.getParentDirectory().getFileName() == "fixture-media",
                                "dawproject: embedded audio extracted next to the project") && passed;
            }
        }
        passed = expect(near(project.masterGainDb, -3.1, 0.1), "dawproject: master gain") && passed;
        passed = expect(project.missingFiles.isEmpty(), "dawproject: nothing missing") && passed;
    }

    // Ableton: a looped MIDI clip unrolled, a session clip, an unwarped
    // audio clip found by its relative path.
    {
        const auto folder = tempRoot.getChildFile("import-ableton");
        (void) folder.getChildFile("Samples").createDirectory();
        passed = expect(toneFile.copyFileTo(folder.getChildFile("Samples").getChildFile("tone.wav")),
                        "copy tone into the Live project folder") && passed;
        const juce::String xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Ableton MajorVersion="5" Creator="Ableton Live 11.3.13"><LiveSet>
<MasterTrack><DeviceChain><Mixer><Tempo><Manual Value="90"/></Tempo><Volume><Manual Value="1"/></Volume></Mixer></DeviceChain></MasterTrack>
<Tracks>
<MidiTrack Id="1"><Name><EffectiveName Value="Bass"/></Name><Color Value="13"/><DeviceChain><Mixer><Volume><Manual Value="0.5"/></Volume><Pan><Manual Value="0.3"/></Pan><Speaker><Manual Value="false"/></Speaker></Mixer><DeviceChain><Devices><OriginalSimpler/></Devices></DeviceChain>
<MainSequencer><ClipTimeable><ArrangerAutomation><Events><MidiClip Time="8"><CurrentStart Value="0"/><CurrentEnd Value="4"/><Name Value="Line"/><Loop><LoopStart Value="0"/><LoopEnd Value="2"/><LoopOn Value="true"/></Loop><Notes><KeyTracks><KeyTrack><Notes><MidiNoteEvent Time="0" Duration="0.5" Velocity="100" IsEnabled="true"/><MidiNoteEvent Time="1" Duration="0.5" Velocity="64"/><MidiNoteEvent Time="1.5" Duration="0.5" Velocity="64" IsEnabled="false"/></Notes><MidiKey Value="36"/></KeyTrack></KeyTracks></Notes></MidiClip></Events></ArrangerAutomation></ClipTimeable>
<ClipSlotList><ClipSlot><ClipSlot><Value><MidiClip Time="0"><CurrentStart Value="0"/><CurrentEnd Value="4"/><Name Value="Slot"/><Notes><KeyTracks><KeyTrack><Notes><MidiNoteEvent Time="0" Duration="1" Velocity="127"/></Notes><MidiKey Value="40"/></KeyTrack></KeyTracks></Notes></MidiClip></Value></ClipSlot></ClipSlot><ClipSlot><ClipSlot><Value/></ClipSlot></ClipSlot></ClipSlotList></MainSequencer></DeviceChain></MidiTrack>
<AudioTrack Id="2"><Name><EffectiveName Value="Vox"/></Name><DeviceChain><Mixer><Volume><Manual Value="1"/></Volume></Mixer><MainSequencer><ClipTimeable><ArrangerAutomation><Events><AudioClip Time="2"><CurrentStart Value="0"/><CurrentEnd Value="3"/><Name Value="Take"/><IsWarped Value="false"/><SampleRef><FileRef><Name Value="tone.wav"/><RelativePath Value="Samples/tone.wav"/><Path Value="C:/nowhere/tone.wav"/></FileRef></SampleRef></AudioClip></Events></ArrangerAutomation></ClipTimeable></MainSequencer></DeviceChain></AudioTrack>
<ReturnTrack Id="3"><Name><EffectiveName Value="Reverb"/></Name></ReturnTrack>
</Tracks></LiveSet></Ableton>)";
        const auto file = folder.getChildFile("fixture.als");
        {
            juce::FileOutputStream out(file);
            juce::GZIPCompressorOutputStream gz(out, 9, 31);
            gz.write(xml.toRawUTF8(), xml.getNumBytesAsUTF8());
            gz.flush();
        }
        ImportedProject project;
        const auto result = importProjectFile(file, project);
        passed = expect(result.wasOk(), ("ableton read: " + result.getErrorMessage()).toRawUTF8()) && passed;
        passed = expect(project.format == "ableton" && near(project.tempo, 90.0)
                            && project.application.contains("11.3.13"),
                        "ableton: tempo and creator") && passed;
        passed = expect(project.tracks.size() == 2 && project.warnings.size() == 1,
                        "ableton: two tracks, the return track reported") && passed;
        if (project.tracks.size() == 2) {
            const auto& bass = project.tracks[0];
            passed = expect(bass.midi && bass.name == "Bass" && bass.muted && near(bass.pan, 0.3)
                                && near(bass.gainDb, -6.02, 0.1) && bass.hadInstrument
                                && bass.colour == 0xffffffffu,
                            "ableton: MIDI track mixer, colour 13 is white") && passed;
            passed = expect(bass.midiClips.size() == 2, "ableton: arrangement and session clips") && passed;
            if (bass.midiClips.size() == 2) {
                const auto& line = bass.midiClips[0];
                passed = expect(near(line.startBeats, 8.0) && near(line.lengthBeats, 4.0)
                                    && line.notes.size() == 4 && near(line.notes[2].startBeats, 2.0)
                                    && near(line.notes[3].startBeats, 3.0) && near(line.notes[0].velocity, 100.0f / 127.0f),
                                "ableton: two-beat loop unrolled over four beats, disabled note skipped") && passed;
                passed = expect(bass.midiClips[1].launcherScene == 0 && bass.midiClips[1].notes.size() == 1
                                    && bass.midiClips[1].notes[0].note == 40,
                                "ableton: session clip on scene 0") && passed;
            }
            const auto& vox = project.tracks[1];
            passed = expect(!vox.midi && vox.audioClips.size() == 1, "ableton: one audio clip") && passed;
            if (vox.audioClips.size() == 1) {
                const auto& take = vox.audioClips[0];
                passed = expect(take.file.existsAsFile() && near(take.startSeconds, 2.0 * 60.0 / 90.0)
                                    && near(take.durationSeconds, 3.0 * 60.0 / 90.0)
                                    && near(take.sourceDurationSeconds, 0.0),
                                "ableton: unwarped clip at native speed, found by relative path") && passed;
            }
        }
    }

    // Bitwig: the binary tree, with references, lists and a raw audio event.
    {
        const auto folder = tempRoot.getChildFile("import-bitwig");
        (void) folder.createDirectory();
        const auto file = folder.getChildFile("fixture.bwproject");
        passed = expect(writeBitwigFixture(file, toneFile), "write bitwig fixture") && passed;
        ImportedProject project;
        const auto result = importProjectFile(file, project);
        passed = expect(result.wasOk(), ("bitwig read: " + result.getErrorMessage()).toRawUTF8()) && passed;
        passed = expect(project.format == "bitwig" && project.application == "Bitwig Studio 5.0.4"
                            && near(project.tempo, 95.0) && project.numerator == 3 && project.denominator == 4
                            && project.name == "Fixture",
                        "bitwig: meta, tempo, packed time signature") && passed;
        passed = expect(project.tracks.size() == 2, "bitwig: two tracks") && passed;
        if (project.tracks.size() == 2) {
            const auto& lead = project.tracks[0];
            passed = expect(lead.midi && lead.name == "Lead" && lead.solo && near(lead.pan, 0.25)
                                && near(lead.gainDb, 0.0) && lead.hadInstrument,
                            "bitwig: instrument track mixer") && passed;
            passed = expect(lead.midiClips.size() == 1 && lead.midiClips[0].notes.size() == 4
                                && near(lead.midiClips[0].startBeats, 8.0)
                                && lead.midiClips[0].notes[0].note == 62
                                && near(lead.midiClips[0].notes[0].velocity, 0.75)
                                && near(lead.midiClips[0].notes[3].startBeats, 3.0),
                            "bitwig: looped note clip unrolled, named velocity") && passed;
            const auto& audio = project.tracks[1];
            // Unnamed, it is named as Bitwig shows it: its kind and its place.
            passed = expect(!audio.midi && audio.name == "Audio 2" && near(audio.gainDb, -10.0, 0.05),
                            "bitwig: an unnamed audio track named as Bitwig shows it, -10 dB default") && passed;
            passed = expect(audio.audioClips.size() == 1 && audio.audioClips[0].launcherScene == 0
                                && audio.audioClips[0].file.existsAsFile()
                                && near(audio.audioClips[0].durationSeconds, 1.0),
                            "bitwig: launcher audio clip cut to the sample length") && passed;
        }
    }

    // What is not a project.
    {
        const auto file = tempRoot.getChildFile("not-a-project.txt");
        (void) file.replaceWithText("hello");
        ImportedProject project;
        passed = expect(importProjectFile(file, project).failed(), "unknown extension refused") && passed;
        const auto broken = tempRoot.getChildFile("broken.als");
        (void) broken.replaceWithText("not gzip, not xml");
        passed = expect(importProjectFile(broken, project).failed(), "broken .als refused") && passed;
        const auto emptyZip = tempRoot.getChildFile("empty.dawproject");
        (void) emptyZip.replaceWithText("");
        passed = expect(importProjectFile(emptyZip, project).failed(), "empty .dawproject refused") && passed;
    }
    return passed;
}

int main(const int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI guiInitialiser;
    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getNonexistentChildFile("cydaw-desktop-tests", {}, true);
    if (!tempRoot.createDirectory()) {
        std::cerr << "FAILED: could not create temporary test directory\n";
        return 1;
    }

    const auto firstFile = tempRoot.getChildFile("tone-a.wav");
    const auto secondFile = tempRoot.getChildFile("tone-b.wav");
    const auto editFixtureFile = tempRoot.getChildFile("audio-edit-fixture.wav");
    const auto monoEditFixtureFile = tempRoot.getChildFile("audio-edit-mono.wav");
    const auto tempoFixtureFile = tempRoot.getChildFile("loop-120-bpm.wav");
    juce::File testPluginBundle;
    auto passed = expect(writeToneFile(firstFile, 220.0), "write first WAV")
                  && expect(writeToneFile(secondFile, 440.0, 44'100.0),
                            "write second WAV at 44.1 kHz")
                  && expect(writeAudioEditFixture(editFixtureFile),
                            "write stereo sample-editor fixture")
                   && expect(writeAudioEditFixture(monoEditFixtureFile, 1),
                             "write mono sample-editor fixture")
                   && expect(writeTempoFixture(tempoFixtureFile, 120.0),
                             "write tempo detection fixture");

    const auto detectedTempo
        = cydaw::app::AudioTrackController::detectAudioTempo(tempoFixtureFile);
    passed = expect(detectedTempo.has_value()
                        && std::abs(detectedTempo->bpm - 120.0) <= 1.0,
                    "detect sample BPM from audio transients") && passed;

    {
        ConstantAudioSource source(1.0f);
        cydaw::app::VirtualBusRouter router(source);
        auto buses = router.buses();
        const auto output = std::find_if(
            buses.begin(), buses.end(), [](const auto& bus) {
                return bus.direction
                       == cydaw::app::VirtualBusDirection::output;
            });
        auto configured = output != buses.end() ? *output
                                                : cydaw::app::VirtualAudioBus {};
        configured.channels = 2;
        configured.destinationChannelA = 2;
        configured.destinationChannelB = 3;
        configured.gainDb = -6.0f;
        const auto busUpdated = router.updateBus(configured);
        router.prepareToPlay(64, 48'000.0);
        juce::AudioBuffer<float> outputBuffer(4, 64);
        outputBuffer.clear();
        router.getNextAudioBlock({ &outputBuffer, 0, 64 });
        const auto expectedGain = juce::Decibels::decibelsToGain(-6.0f);
        passed = expect(
                     busUpdated
                         && std::abs(outputBuffer.getSample(2, 32) - expectedGain)
                             < 0.0001f
                         && std::abs(outputBuffer.getSample(3, 32)
                                     - expectedGain) < 0.0001f
                         && std::abs(outputBuffer.getSample(0, 32)) < 0.0001f
                         && router.meterLevels(configured.id)[0] > 0.49f,
                     "virtual output maps stereo master audio to selected hardware channels")
                 && passed;
        router.releaseResources();

        cydaw::app::VirtualAudioBus input;
        input.channels = 2;
        input.physicalChannelA = 1;
        input.physicalChannelB = 0;
        input.gainDb = -6.0f;
        std::array<float, 16> hardwareA {};
        std::array<float, 16> hardwareB {};
        hardwareA.fill(0.25f);
        hardwareB.fill(0.75f);
        const float* hardware[] { hardwareA.data(), hardwareB.data() };
        std::array<float, 16> routedA {};
        std::array<float, 16> routedB {};
        cydaw::app::VirtualBusRouter::renderInputBus(
            input, hardware, 2, routedA.data(), routedB.data(),
            static_cast<int>(routedA.size()));
        passed = expect(
                     std::abs(routedA[8] - 0.75f * expectedGain) < 0.0001f
                         && std::abs(routedB[8] - 0.25f * expectedGain)
                                < 0.0001f,
                     "virtual input maps physical channels and applies processing before recording")
                 && passed;
    }

    {
        // Tracks in their own formats: a mono one sent straight to a quad
        // output of the sound card (that does not carry the master), a quad
        // one into the stereo master, folded.
        ConstantAudioSource mono(0.5f);
        ConstantAudioSource quad(0.25f);
        cydaw::app::TrackMixer mixer;
        mixer.addInputSource(&mono, false);
        mixer.addInputSource(&quad, false);
        cydaw::app::VirtualBusRouter router(mixer);
        router.setDirectOutputs(&mixer);
        auto buses = router.buses();
        auto output = *std::find_if(buses.begin(), buses.end(), [](const auto& bus) {
            return bus.direction == cydaw::app::VirtualBusDirection::output;
        });
        output.channels = 4;
        for (auto channel = 0; channel < 4; ++channel)
            output.setDeviceChannel(channel, channel + 2);
        output.carriesMaster = false;
        const auto quadBus = router.updateBus(output);
        auto plan = std::make_shared<cydaw::app::TrackMixer::Plan>();
        cydaw::app::TrackMixer::Entry sent;
        sent.source = &mono;
        sent.channels = 1;
        cydaw::app::TrackMixer::Output direct;
        direct.channels = 4;
        direct.hardware = 0;
        sent.outputs.push_back(direct);
        plan->hardware.push_back({ output.id, 4, false });
        plan->entries.push_back(sent);
        cydaw::app::TrackMixer::Entry folded;
        folded.source = &quad;
        folded.channels = 4;
        plan->entries.push_back(folded);
        mixer.setPlan(plan);
        router.prepareToPlay(64, 48'000.0);
        juce::AudioBuffer<float> device(6, 64);
        device.clear();
        router.getNextAudioBlock({ &device, 0, 64 });
        passed = expect(quadBus && std::abs(device.getSample(2, 10) - 0.5f) < 0.0001f
                            && std::abs(device.getSample(3, 10) - 0.5f) < 0.0001f
                            && std::abs(device.getSample(4, 10)) < 0.0001f
                            && std::abs(device.getSample(5, 10)) < 0.0001f
                            && std::abs(device.getSample(0, 10)) < 0.0001f,
                        "a mono track sent straight to a quad output of the sound card spreads to its front pair")
                 && passed;

        output.carriesMaster = true;
        (void) router.updateBus(output);
        device.clear();
        router.getNextAudioBlock({ &device, 0, 64 });
        // The quad track folded: L + Ls at -3 dB on the left (0.25 * 1.707),
        // beside the mono one sent there directly.
        const auto foldedLeft = 0.25f * (1.0f + 0.70710678f);
        passed = expect(std::abs(device.getSample(2, 10) - (0.5f + foldedLeft)) < 0.0001f
                            && std::abs(device.getSample(4, 10)) < 0.0001f,
                        "an output carrying the master gets the tracks folded to stereo, then what is sent to it")
                 && passed;
        router.releaseResources();
    }

    {
        // A virtual input runs every block, heard or not: the recorder takes
        // it before its effects (the card's sound, at its level) or after.
        ConstantAudioSource nothing(0.0f);
        cydaw::app::VirtualBusRouter router(nothing);
        auto buses = router.buses();
        auto input = *std::find_if(buses.begin(), buses.end(), [](const auto& bus) {
            return bus.direction == cydaw::app::VirtualBusDirection::input;
        });
        input.channels = 1;
        input.physicalChannelA = 1;
        const auto updated = router.updateBus(input);
        auto graph = cydaw::app::makePassthroughAudioGraph();
        cydaw::engine::NativeDspNode drive { 3, cydaw::engine::NativeDspNodeKind::drive };
        drive.parameters.driveDb = 12.0f;
        graph.nodes.insert(graph.nodes.begin() + 1, drive);
        graph.connections = { { 1, 3 }, { 3, 2 } };
        const auto graphSet = router.setGraph(input.id, graph).wasOk();
        router.prepareToPlay(64, 48'000.0);
        std::array<float, 64> left {};
        std::array<float, 64> right {};
        right.fill(0.25f);
        const float* hardware[] { left.data(), right.data() };
        juce::AudioIODeviceCallbackContext context {};
        router.audioDeviceIOCallbackWithContext(hardware, 2, nullptr, 0, 64, context);
        std::array<const float*, 8> before {};
        std::array<const float*, 8> after {};
        auto beforeCount = 0;
        auto afterCount = 0;
        const auto taken = router.inputBusBlock(input.id, false, 64, before, beforeCount)
                           && router.inputBusBlock(input.id, true, 64, after, afterCount);
        passed = expect(updated && graphSet && taken && beforeCount == 1 && afterCount == 1
                            && std::abs(before[0][10] - 0.25f) < 0.0001f && after[0][10] > 0.5f
                            && !router.inputBusBlock(input.id, true, 32, after, afterCount),
                        "a virtual input runs its effects every block, taken before or after them")
                 && passed;
        router.releaseResources();
    }

    {
        cydaw::app::NativeDspRuntime busRuntime;
        busRuntime.prepare(48'000.0, 64, 2);
        juce::AudioBuffer<float> busBuffer(2, 64);
        for (auto channel = 0; channel < busBuffer.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill(
                busBuffer.getWritePointer(channel), 0.25f, 64);
        const auto transparent = busRuntime.process(busBuffer, 0, 64)
            && std::abs(busBuffer.getSample(0, 32) - 0.25f) < 0.0001f
            && std::abs(busBuffer.getSample(1, 32) - 0.25f) < 0.0001f;
        auto graph = cydaw::app::makePassthroughAudioGraph();
        cydaw::engine::NativeDspNode drive {
            3, cydaw::engine::NativeDspNodeKind::drive
        };
        drive.parameters.driveDb = 12.0f;
        graph.nodes.insert(graph.nodes.begin() + 1, drive);
        graph.connections = { { 1, 3 }, { 3, 2 } };
        for (auto channel = 0; channel < busBuffer.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill(
                busBuffer.getWritePointer(channel), 0.25f, 64);
        const auto explicitEffect = busRuntime.setGraph(graph).wasOk()
            && busRuntime.process(busBuffer, 0, 64)
            && busBuffer.getSample(0, 32) > 0.5f;
        passed = expect(
                     transparent && explicitEffect,
                     "virtual buses are transparent by default and process only explicit graph nodes")
                 && passed;
    }

    {
        auto controller
            = std::make_unique<cydaw::app::AudioTrackController>();
        const auto defaults = controller->virtualBuses();
        const auto hasDefaultInput = std::any_of(
            defaults.begin(), defaults.end(), [](const auto& bus) {
                return bus.direction
                       == cydaw::app::VirtualBusDirection::input;
            });
        const auto hasDefaultOutput = std::any_of(
            defaults.begin(), defaults.end(), [](const auto& bus) {
                return bus.direction
                       == cydaw::app::VirtualBusDirection::output;
            });
        const auto vocalBus = controller->addVirtualBus(
            cydaw::app::VirtualBusDirection::input);
        auto configured = controller->virtualBus(vocalBus);
        if (configured.has_value()) {
            configured->name = "Vocal input";
            configured->channels = 1;
            configured->physicalChannelA = 3;
            (void) controller->updateVirtualBus(*configured);
        }
        cydaw::engine::NativeDspGraph busGraph;
        busGraph.nodes = {
            { 1, cydaw::engine::NativeDspNodeKind::input },
            { 3, cydaw::engine::NativeDspNodeKind::compressor },
            { 2, cydaw::engine::NativeDspNodeKind::output }
        };
        busGraph.connections = { { 1, 3 }, { 3, 2 } };
        busGraph.outputNodeId = 2;
        const auto graphConfigured = controller->setVirtualBusGraph(
            vocalBus, busGraph).wasOk();
        std::size_t trackIndex {};
        const auto added = controller->addEmptyAudioTrack(&trackIndex);
        const auto assigned = controller->setTrackInputBus(trackIndex,
                                                            vocalBus);
        const auto assignedInfo = controller->trackInfo(trackIndex);
        const auto removed = controller->removeVirtualBus(vocalBus);
        const auto fallbackInfo = controller->trackInfo(trackIndex);
        passed = expect(
                     hasDefaultInput && hasDefaultOutput && added.wasOk()
                         && configured.has_value() && assigned
                         && graphConfigured
                         && assignedInfo.inputBusId == vocalBus && removed
                         && fallbackInfo.inputBusId
                                == controller->defaultInputBusId(),
                     "tracks retain a valid virtual input assignment when a bus is removed")
                 && passed;
    }

    {
        auto project = juce::DynamicObject::Ptr(new juce::DynamicObject());
        project->setProperty("format", "CyDAW Project");
        project->setProperty("version", 20);
        project->setProperty("tempo", 128.0);
        const auto original = tempRoot.getChildFile("Original Session.cydaw");
        constexpr juce::int64 recoveryTime = 1'723'456'789'000;
        auto recovery = cydaw::app::ProjectRecovery::makeRecoveryDocument(
            juce::var(project.get()), original, juce::Time(recoveryTime));
        const auto recoveryFile = tempRoot.getChildFile("recovery")
                                      .getChildFile("session.cydaw-recovery");
        passed = expect(
                     cydaw::app::ProjectRecovery::isRecoveryDocument(recovery)
                         && cydaw::app::ProjectRecovery::originalProject(recovery)
                                == original
                         && cydaw::app::ProjectRecovery::recoveryTimestamp(
                                recovery).toMilliseconds() == recoveryTime
                         && cydaw::app::ProjectRecovery::writeDocumentAtomically(
                                recoveryFile, recovery).wasOk(),
                     "write a versioned project recovery snapshot atomically")
                 && passed;
        juce::var restoredRecovery;
        passed = expect(
                     cydaw::app::ProjectRecovery::readDocument(
                         recoveryFile, restoredRecovery).wasOk()
                         && cydaw::app::ProjectRecovery::isRecoveryDocument(
                             restoredRecovery)
                         && std::abs(static_cast<double>(
                                restoredRecovery.getProperty("tempo", 0.0))
                                     - 128.0) < 0.000001,
                     "validate and restore a recovery document")
                 && passed;
        const auto knownGood = recoveryFile.loadFileAsString();
        auto invalid = juce::DynamicObject::Ptr(new juce::DynamicObject());
        invalid->setProperty("format", "Not CyDAW");
        invalid->setProperty("version", 20);
        passed = expect(
                     cydaw::app::ProjectRecovery::writeDocumentAtomically(
                         recoveryFile, juce::var(invalid.get())).failed()
                         && recoveryFile.loadFileAsString() == knownGood,
                     "reject an invalid snapshot without replacing known-good recovery")
                 && passed;
        passed = expect(cydaw::app::ProjectRecovery::discard(recoveryFile)
                            && !recoveryFile.exists(),
                        "discard a confirmed recovery snapshot")
                  && passed;
    }

    {
        auto recordingSession =
            std::make_unique<cydaw::app::AudioTrackController>();
        passed = expect(
                     cydaw::app::AudioTrackController::selfTestAudioTakeRendering(
                         firstFile, secondFile).wasOk(),
                     "render the selected alternate take and switch back sample-accurately")
                 && passed;
        passed = expect(
                     cydaw::app::AudioTrackController::selfTestStretchRendering(
                         firstFile).wasOk(),
                     "render repitch and pitch-preserving stretch modes")
                 && passed;
        std::size_t baseTrack {};
        passed = expect(
                     recordingSession->addAudioTrack(firstFile, &baseTrack).wasOk()
                         && baseTrack == 0,
                     "create the target track for an audio take")
                 && passed;
        recordingSession->setTrackArmed(baseTrack, true);
        cydaw::app::AudioTrackController::RecordedTakeResult insertedTake;
        const auto insert = recordingSession->commitRecordedTake(
            secondFile, recordingSession->armedAudioTrack(), 0.375,
            insertedTake);
        const auto insertedClip = recordingSession->audioClip(
            insertedTake.trackIndex, insertedTake.clipId);
        passed = expect(
                     insert.wasOk() && !insertedTake.createdTrack
                         && insertedTake.trackIndex == 0
                         && insertedClip.has_value()
                         && insertedClip->sourceFile == secondFile
                         && std::abs(insertedClip->startSeconds - 0.375)
                                < 0.000001
                         && std::abs(insertedTake.durationSeconds - 1.0)
                                < 0.000001
                         && recordingSession->canUndoArrangementEdit(),
                     "insert a recorded take on the armed track at the transport position")
                 && passed;
        passed = expect(
                     recordingSession->undoArrangementEdit()
                         && recordingSession->audioClips(0).size() == 1
                         && recordingSession->redoArrangementEdit()
                         && recordingSession->audioClips(0).size() == 2,
                     "undo and redo a committed audio take as one arrangement edit")
                 && passed;

        recordingSession->setTrackArmed(0, false);
        cydaw::app::AudioTrackController::RecordedTakeResult newTrackTake;
        const auto create = recordingSession->commitRecordedTake(
            secondFile, std::nullopt, 1.25, newTrackTake);
        const auto createdClip = recordingSession->audioClip(
            newTrackTake.trackIndex, newTrackTake.clipId);
        passed = expect(
                     create.wasOk() && newTrackTake.createdTrack
                         && newTrackTake.trackIndex == 1
                         && recordingSession->trackCount() == 2
                         && recordingSession->armedAudioTrack() == 1
                         && createdClip.has_value()
                         && std::abs(createdClip->startSeconds - 1.25)
                                < 0.000001,
                     "create and arm a new audio track when recording an empty target")
                 && passed;

        cydaw::app::AudioTrackController::RecordedTakeResult alternateTake;
        const auto alternate = recordingSession->commitRecordedTake(
            firstFile, newTrackTake.trackIndex, 1.25, alternateTake,
            newTrackTake.clipId);
        const auto alternateClip = recordingSession->audioClip(
            newTrackTake.trackIndex, newTrackTake.clipId);
        passed = expect(
                     alternate.wasOk() && !alternateTake.createdTrack
                         && alternateTake.clipId == newTrackTake.clipId
                         && alternateClip.has_value()
                         && alternateClip->takes.size() == 2
                         && alternateClip->activeTakeIndex == 1
                         && alternateClip->sourceFile == firstFile
                         && std::abs(alternateClip->durationSeconds - 1.0)
                                < 0.000001,
                     "record and activate an aligned alternate take inside an audio region")
                 && passed;
        passed = expect(
                     recordingSession->undoArrangementEdit()
                         && recordingSession->audioClip(
                                newTrackTake.trackIndex,
                                newTrackTake.clipId)->takes.size() == 1
                         && recordingSession->redoArrangementEdit()
                         && recordingSession->audioClip(
                                newTrackTake.trackIndex,
                                newTrackTake.clipId)->takes.size() == 2,
                     "undo and redo an alternate take as one arrangement edit")
                 && passed;
        passed = expect(
                     recordingSession->setActiveAudioTake(
                         newTrackTake.trackIndex, newTrackTake.clipId, 0)
                         && recordingSession->audioClip(
                                newTrackTake.trackIndex,
                                newTrackTake.clipId)->sourceFile == secondFile
                         && recordingSession->removeAudioTake(
                                newTrackTake.trackIndex,
                                newTrackTake.clipId, 1)
                         && recordingSession->audioClip(
                                newTrackTake.trackIndex,
                                newTrackTake.clipId)->takes.size() == 1
                         && !recordingSession->removeAudioTake(
                                newTrackTake.trackIndex,
                                newTrackTake.clipId, 0),
                     "switch and remove alternate takes while retaining one source")
                 && passed;

        const auto trackCountBeforeFailure = recordingSession->trackCount();
        cydaw::app::AudioTrackController::RecordedTakeResult invalidTake;
        passed = expect(
                     recordingSession->commitRecordedTake(
                         tempRoot.getChildFile("missing-take.wav"),
                         std::nullopt, 0.0, invalidTake).failed()
                         && recordingSession->trackCount()
                                == trackCountBeforeFailure,
                     "reject an invalid take before mutating the arrangement")
                  && passed;
    }

    {
        const auto libraryA = tempRoot.getChildFile("Library A");
        const auto drums = libraryA.getChildFile("Drums");
        const auto libraryB = tempRoot.getChildFile("Library B");
        passed = expect(drums.createDirectory()
                            && libraryB.createDirectory()
                            && firstFile.copyFileTo(
                                drums.getChildFile("Warm Kick.wav"))
                            && secondFile.copyFileTo(
                                libraryB.getChildFile("Air Pad.wav")),
                        "create two sample-library fixtures") && passed;
        if (auto invalid = libraryB.getChildFile("Broken.wav")
                               .createOutputStream()) {
            static constexpr char invalidAudio[] = "not audio";
            (void) invalid->write(invalidAudio, sizeof(invalidAudio) - 1);
        }
        const std::vector<juce::File> roots { libraryA, drums, libraryB };
        const auto samples =
            cydaw::app::LibraryBrowserComponent::scanSampleLibraries(roots);
        const auto kick = std::find_if(
            samples.begin(), samples.end(), [](const auto& sample) {
                return sample.name == "Warm Kick";
            });
        const auto pad = std::find_if(
            samples.begin(), samples.end(), [](const auto& sample) {
                return sample.name == "Air Pad";
            });
        passed = expect(samples.size() == 2
                            && kick != samples.end()
                            && pad != samples.end()
                            && kick->libraryRoot == libraryA
                            && std::abs(kick->durationSeconds - 1.0) < 0.000001
                            && std::abs(kick->sampleRate - 48'000.0) < 0.001
                            && kick->channels == 2
                            && pad->sampleRate == 44'100.0,
                        "index multiple libraries, deduplicate nested roots and read audio metadata")
                 && passed;
        const auto encodedCache =
            cydaw::app::LibraryBrowserComponent::serialiseSampleIndexCache(
                samples, roots);
        const auto parsedCache = juce::JSON::parse(
            juce::JSON::toString(encodedCache, false));
        const auto cachedSamples =
            cydaw::app::LibraryBrowserComponent::restoreSampleIndexCache(
                parsedCache, roots);
        auto corruptCache = juce::JSON::parse(
            juce::JSON::toString(encodedCache, false));
        if (auto* cacheObject = corruptCache.getDynamicObject())
            cacheObject->setProperty("schema", 99);
        passed = expect(
                     cachedSamples.size() == samples.size()
                         && !cachedSamples.empty()
                         && cachedSamples.front().file == samples.front().file
                         && cachedSamples.front().libraryRoot
                                == samples.front().libraryRoot
                         && std::abs(cachedSamples.front().durationSeconds
                                     - samples.front().durationSeconds)
                                < 0.000001
                         && cydaw::app::LibraryBrowserComponent::restoreSampleIndexCache(
                                parsedCache,
                                std::vector<juce::File> { libraryA }).empty()
                         && cydaw::app::LibraryBrowserComponent::restoreSampleIndexCache(
                                corruptCache, roots).empty(),
                     "round-trip a versioned sample index cache and reject stale or corrupt caches")
                 && passed;
        if (kick != samples.end()) {
            const auto detail =
                cydaw::app::LibraryBrowserComponent::formatSampleMetadata(*kick);
            using FilterMode =
                cydaw::app::LibraryBrowserComponent::SampleFilterMode;
            auto mediumMono = *kick;
            mediumMono.channels = 1;
            mediumMono.durationSeconds = 8.0;
            passed = expect(
                         cydaw::app::LibraryBrowserComponent::sampleMatchesSearch(
                             *kick, "drums kick 48.0")
                             && !cydaw::app::LibraryBrowserComponent::sampleMatchesSearch(
                                 *kick, "kick 44.1")
                             && detail.contains("WAV")
                             && detail.contains("00:01.000")
                             && detail.contains("48.0 kHz")
                             && detail.contains("2 ch")
                             && cydaw::app::LibraryBrowserComponent::sampleMatchesFilter(
                                 *kick, FilterMode::stereo)
                             && cydaw::app::LibraryBrowserComponent::sampleMatchesFilter(
                                 *kick, FilterMode::shortDuration)
                             && !cydaw::app::LibraryBrowserComponent::sampleMatchesFilter(
                                 *kick, FilterMode::mono)
                             && cydaw::app::LibraryBrowserComponent::sampleMatchesFilter(
                                 mediumMono, FilterMode::mono)
                             && cydaw::app::LibraryBrowserComponent::sampleMatchesFilter(
                                 mediumMono, FilterMode::mediumDuration)
                             && !cydaw::app::LibraryBrowserComponent::sampleMatchesFilter(
                                 mediumMono, FilterMode::longDuration),
                         "search metadata and filter indexed sample characteristics")
                     && passed;
        }
        if (kick != samples.end() && pad != samples.end()) {
            auto firstSortSample = *kick;
            auto secondSortSample = *pad;
            firstSortSample.name = "Zulu";
            firstSortSample.modifiedTimeMs = 20;
            firstSortSample.durationSeconds = 1.0;
            firstSortSample.fileSize = 10;
            secondSortSample.name = "Alpha";
            secondSortSample.modifiedTimeMs = 10;
            secondSortSample.durationSeconds = 2.0;
            secondSortSample.fileSize = 20;
            using SortMode =
                cydaw::app::LibraryBrowserComponent::SampleSortMode;
            passed = expect(
                         cydaw::app::LibraryBrowserComponent::sampleSortsBefore(
                             secondSortSample, firstSortSample,
                             SortMode::name)
                             && cydaw::app::LibraryBrowserComponent::sampleSortsBefore(
                                 firstSortSample, secondSortSample,
                                 SortMode::newest)
                             && cydaw::app::LibraryBrowserComponent::sampleSortsBefore(
                                 firstSortSample, secondSortSample,
                                 SortMode::duration)
                             && cydaw::app::LibraryBrowserComponent::sampleSortsBefore(
                                 secondSortSample, firstSortSample,
                                 SortMode::size),
                         "sort filtered browser samples deterministically")
                     && passed;
        }
        passed = expect(
                     cydaw::app::LibraryBrowserComponent::scanSampleLibraries(
                         roots, 1).size() == 1
                         && cydaw::app::LibraryBrowserComponent::scanSampleLibraries(
                             roots, 50'000, [] { return true; }).empty(),
                     "bound and cancel sample-library scans")
                 && passed;

        const auto dragPayload = cydaw::app::makeSampleDragPayload(firstFile);
        const auto draggedSample =
            cydaw::app::sampleFileFromDragPayload(dragPayload);
        const auto launcherPayload =
            cydaw::app::makeLauncherClipDragPayload(3, 7);
        const auto draggedLauncherClip =
            cydaw::app::launcherClipFromDragPayload(launcherPayload);
        const auto devicePayload =
            cydaw::app::makeFactoryDeviceDragPayload("poly-graph-synth");
        const auto draggedDevice =
            cydaw::app::factoryDeviceFromDragPayload(devicePayload);
        const auto graphPresetPayload =
            cydaw::app::makeGraphPresetDragPayload("warm-channel");
        const auto draggedGraphPreset =
            cydaw::app::graphPresetFromDragPayload(graphPresetPayload);
        const auto graphFilePayload =
            cydaw::app::makeGraphPresetFileDragPayload(firstFile);
        const auto draggedGraphFile =
            cydaw::app::graphPresetFileFromDragPayload(graphFilePayload);
        const auto pluginPayload =
            cydaw::app::makePluginDragPayload(firstFile);
        const auto draggedPlugin =
            cydaw::app::pluginFileFromDragPayload(pluginPayload);
        passed = expect(
                     draggedSample.has_value()
                         && *draggedSample == firstFile
                         && draggedLauncherClip.has_value()
                         && draggedLauncherClip->trackIndex == 3
                         && draggedLauncherClip->sceneIndex == 7
                         && draggedDevice.has_value()
                         && *draggedDevice == "poly-graph-synth"
                         && draggedGraphPreset.has_value()
                         && *draggedGraphPreset == "warm-channel"
                         && draggedGraphFile.has_value()
                         && *draggedGraphFile == firstFile
                         && draggedPlugin.has_value()
                         && *draggedPlugin == firstFile
                         && !cydaw::app::sampleFileFromDragPayload(
                                 "cydaw-node:drive").has_value()
                         && !cydaw::app::sampleFileFromDragPayload(
                                 "cydaw-sample:").has_value()
                         && !cydaw::app::launcherClipFromDragPayload(
                                 "cydaw-launcher-clip:x:7").has_value()
                         && !cydaw::app::launcherClipFromDragPayload(
                                 "cydaw-launcher-clip:3:7:9").has_value()
                         && !cydaw::app::factoryDeviceFromDragPayload(
                                 "cydaw-node:").has_value()
                         && !cydaw::app::graphPresetFromDragPayload(
                                 "cydaw-graph-preset:").has_value()
                         && !cydaw::app::factoryDeviceFromDragPayload(
                                 graphPresetPayload).has_value()
                         && !cydaw::app::graphPresetFileFromDragPayload(
                                 "cydaw-graph-file:").has_value()
                         && !cydaw::app::pluginFileFromDragPayload(
                                 "cydaw-plugin:").has_value(),
                     "round-trip typed browser and launcher drag payloads safely")
                 && passed;

        juce::StringArray favorites;
        const auto favoriteFile = drums.getChildFile("Warm Kick.wav");
        passed = expect(
                     cydaw::app::LibraryBrowserComponent::updateFavoriteList(
                         favorites, favoriteFile, true)
                         && !cydaw::app::LibraryBrowserComponent::updateFavoriteList(
                             favorites, favoriteFile, true)
                         && cydaw::app::LibraryBrowserComponent::favoriteListContains(
                             juce::StringArray::fromLines(
                                 favorites.joinIntoString("\n")),
                             favoriteFile)
                         && cydaw::app::LibraryBrowserComponent::updateFavoriteList(
                             favorites, favoriteFile, false)
                         && !cydaw::app::LibraryBrowserComponent::favoriteListContains(
                             favorites, favoriteFile),
                     "persist browser favorites without duplicate paths")
                 && passed;

        cydaw::app::AudioTrackController previewSession;
        previewSession.setSamplePreviewGainDb(-12.0f);
        const auto previewResult = previewSession.startSamplePreview(firstFile);
        passed = expect(std::abs(previewSession.samplePreviewGainDb() + 12.0f)
                                < 0.001f
                            && previewResult.wasOk()
                            && previewSession.isSamplePreviewing()
                            && previewSession.startSamplePreview(firstFile, 0.5)
                                   .wasOk()
                            && previewSession.samplePreviewPositionSeconds()
                                   >= 0.49
                            && previewSession.startSamplePreview(
                                   tempRoot.getChildFile("missing-preview.wav"))
                                   .failed(),
                        "start or seek sample audition without adding a project track")
                 && passed;
        previewSession.setSamplePreviewGainDb(-90.0f);
        passed = expect(std::abs(previewSession.samplePreviewGainDb() + 48.0f)
                                < 0.001f,
                        "clamp the independent browser audition gain")
                 && passed;
        previewSession.stopSamplePreview();
        passed = expect(!previewSession.isSamplePreviewing()
                            && previewSession.trackCount() == 0,
                        "stop browser audition without mutating the session")
                 && passed;
    }

    {
        juce::AudioFormatManager editorFormats;
        editorFormats.registerBasicFormats();
        juce::AudioThumbnailCache editorCache(4);
        cydaw::app::AudioEditorComponent editor(editorFormats, editorCache);
        cydaw::app::AudioEditRequest dispatched;
        auto dispatchCount = 0;
        auto dispatchedStart = 0.0;
        auto dispatchedEnd = 0.0;
        editor.setProcessHandler(
            [&](const cydaw::app::AudioEditRequest request,
                const double start,
                const double end) {
                dispatched = request;
                dispatchedStart = start;
                dispatchedEnd = end;
                ++dispatchCount;
            });
        editor.setTrack(firstFile, 1.0, 2.0, 0.2, 0.4);
        passed = expect(editor.processSelection(
                            { cydaw::app::AudioEditOperation::normalisePeak,
                              -1.0f })
                            && dispatchCount == 1
                            && dispatched.operation
                                   == cydaw::app::AudioEditOperation::normalisePeak
                            && std::abs(dispatched.levelDb + 1.0f) < 0.001f
                            && std::abs(dispatchedStart - 0.2) < 0.000001
                            && std::abs(dispatchedEnd - 0.6) < 0.000001,
                        "sample editor dispatches its exact source selection") && passed;
        editor.setBusy(true);
        passed = expect(!editor.processSelection(
                            { cydaw::app::AudioEditOperation::silence, 0.0f })
                            && dispatchCount == 1,
                        "sample editor blocks concurrent renders") && passed;
        editor.setBusy(false);
        passed = expect(editor.applyCurrentGain()
                            && dispatchCount == 2
                            && dispatched.operation
                                   == cydaw::app::AudioEditOperation::amplify
                            && std::abs(dispatched.levelDb) < 0.001f,
                        "sample editor dispatches parameterised gain") && passed;
        editor.setSize(900, 320);
        juce::Image editorImage(juce::Image::RGB, 900, 320, true);
        juce::Graphics editorGraphics(editorImage);
        editor.paint(editorGraphics);
        editor.clear();
        passed = expect(!editor.processSelection(
                            { cydaw::app::AudioEditOperation::reverse, 0.0f }),
                        "sample editor rejects an empty source") && passed;
    }

    {
        constexpr double sampleRate = 48'000.0;
        constexpr double durationSeconds = 0.05;
        constexpr int blockSize = 128;
        const auto masterFile = tempRoot.getChildFile("master-capture.wav");
        ConstantAudioSource mix(0.25f);
        cydaw::app::MasterOutputAudioSource master(mix);
        master.prepareToPlay(blockSize, sampleRate);
        passed = expect(master.startCapture(masterFile, durationSeconds).wasOk(),
                        "start stereo master capture") && passed;
        juce::AudioBuffer<float> output(2, blockSize);
        for (int block = 0; block < 100 && !master.captureComplete(); ++block) {
            output.clear();
            master.getNextAudioBlock({ &output, 0, blockSize });
        }
        passed = expect(master.captureComplete()
                            && std::abs(master.captureProgress() - 1.0) < 0.000001
                            && master.droppedBlockCount() == 0,
                        "master capture reaches its exact target without dropped blocks") && passed;
        master.stopCapture();
        master.releaseResources();

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formats.createReaderFor(masterFile));
        juce::AudioBuffer<float> captured(2, 1);
        const auto expectedSamples = static_cast<juce::int64>(
            std::llround(sampleRate * durationSeconds));
        const auto readable = reader != nullptr
                              && reader->read(&captured, 0, 1, 64, true, true);
        passed = expect(reader != nullptr
                            && reader->numChannels == 2
                            && reader->bitsPerSample == 24
                            && reader->lengthInSamples == expectedSamples
                            && readable
                            && std::abs(captured.getSample(0, 0) - 0.25f) < 0.0001f
                            && std::abs(captured.getSample(1, 0) - 0.25f) < 0.0001f,
                        "master capture writes an exact 24-bit stereo WAV") && passed;
    }

    const auto windowsBundle = tempRoot.getChildFile("Windows Test.vst3");
    const auto windowsArmBundle = tempRoot.getChildFile("Windows ARM Test.vst3");
    const auto linuxBundle = tempRoot.getChildFile("Linux Test.vst3");
    const auto windowsVst2 = tempRoot.getChildFile("Windows Test.dll");
    const auto linuxVst2 = tempRoot.getChildFile("Linux Test.so");
    passed = expect(windowsBundle.getChildFile("Contents").getChildFile("x86_64-win")
                        .createDirectory(),
                    "create Windows VST3 fixture") && passed;
    passed = expect(windowsArmBundle.getChildFile("Contents").getChildFile("arm64-win")
                        .createDirectory(),
                    "create Windows ARM VST3 fixture") && passed;
    passed = expect(linuxBundle.getChildFile("Contents").getChildFile("x86_64-linux")
                        .createDirectory(),
                    "create Linux VST3 fixture") && passed;
    passed = expect(cydaw::app::WindowsPluginBridge::isWindowsVst3Bundle(windowsBundle)
                        && cydaw::app::WindowsPluginBridge::isWindowsVst3Bundle(windowsArmBundle)
                        && !cydaw::app::WindowsPluginBridge::isLinuxVst3Bundle(windowsBundle)
                        && cydaw::app::WindowsPluginBridge::isLinuxVst3Bundle(linuxBundle),
                    "classify native and Windows VST3 bundles") && passed;
    passed = expect(writeBinaryMagic(windowsVst2, { 'M', 'Z', 0, 0 })
                        && writeBinaryMagic(linuxVst2, { 0x7f, 'E', 'L', 'F' })
                        && cydaw::app::WindowsPluginBridge::isWindowsVst2Binary(windowsVst2)
                        && !cydaw::app::WindowsPluginBridge::isLinuxVst2Binary(windowsVst2)
                        && cydaw::app::WindowsPluginBridge::isLinuxVst2Binary(linuxVst2)
                        && !cydaw::app::WindowsPluginBridge::isWindowsVst2Binary(linuxVst2),
                    "classify native and Windows VST2 binaries") && passed;
    const auto guestPath = cydaw::app::WindowsPluginBridge::pathForWindowsGuest(firstFile);
    passed = expect(guestPath.containsChar('\\') && !guestPath.containsChar('/'),
                    "translate host paths for the Wine worker") && passed;
    passed = expect(cydaw::app::WindowsPluginBridge::pathForWindowsGuest(
                        "/home/cydaw/Windows Test.vst3")
                        == "Z:\\home\\cydaw\\Windows Test.vst3",
                    "translate POSIX paths to the Wine Z drive") && passed;

    const auto nativeWorkerFixture = tempRoot.getChildFile("CyDAWPluginWorker");
    const auto windowsWorkerFixture = tempRoot.getChildFile("CyDAWPluginWorker.exe");
    const auto wineFixture = tempRoot.getChildFile("wine64");
    const auto umuFixture = tempRoot.getChildFile("umu-run");
    passed = expect(writeBinaryMagic(nativeWorkerFixture, { 0x7f, 'E', 'L', 'F' })
                        && writeBinaryMagic(windowsWorkerFixture, { 'M', 'Z', 0, 0 })
                        && writeBinaryMagic(wineFixture, { 1, 2, 3, 4 })
                        && writeBinaryMagic(umuFixture, { 4, 3, 2, 1 }),
                    "create bridge planning fixtures") && passed;
    cydaw::app::PluginWorkerToolchain bridgeTools;
    bridgeTools.nativeWorker = nativeWorkerFixture;
    bridgeTools.windowsWorker = windowsWorkerFixture;
    bridgeTools.wineRunner = wineFixture;
    bridgeTools.umuRunner = umuFixture;

    const auto nativeLaunch = cydaw::app::WindowsPluginBridge::planWorkerLaunch(
        false, bridgeTools);
    const auto wineLaunch = cydaw::app::WindowsPluginBridge::planWorkerLaunch(
        true, bridgeTools);
    passed = expect(nativeLaunch.runtime == cydaw::app::PluginWorkerRuntime::native
                        && nativeLaunch.command.size() == 1
                        && nativeLaunch.command[0] == nativeWorkerFixture.getFullPathName()
                        && wineLaunch.runtime == cydaw::app::PluginWorkerRuntime::wine
                        && wineLaunch.command.size() == 2
                        && wineLaunch.command[0] == wineFixture.getFullPathName()
                        && wineLaunch.command[1] == windowsWorkerFixture.getFullPathName(),
                    "plan native and direct Wine workers deterministically") && passed;

    bridgeTools.preferUmu = true;
    const auto protonLaunch = cydaw::app::WindowsPluginBridge::planWorkerLaunch(
        true, bridgeTools);
    passed = expect(protonLaunch.runtime == cydaw::app::PluginWorkerRuntime::protonViaUmu
                        && protonLaunch.command.size() == 2
                        && protonLaunch.command[0] == umuFixture.getFullPathName(),
                    "prefer the UMU Proton runner only when requested") && passed;

    bridgeTools.configuredRunner = wineFixture;
    const auto configuredWineLaunch = cydaw::app::WindowsPluginBridge::planWorkerLaunch(
        true, bridgeTools);
    passed = expect(configuredWineLaunch.runtime == cydaw::app::PluginWorkerRuntime::wine
                        && configuredWineLaunch.command[0] == wineFixture.getFullPathName(),
                    "a configured Wine runner takes priority over automatic UMU selection") && passed;

    bridgeTools.configuredRunner = {};
    bridgeTools.umuRunner = {};
    const auto missingUmuLaunch = cydaw::app::WindowsPluginBridge::planWorkerLaunch(
        true, bridgeTools);
    passed = expect(!missingUmuLaunch.isValid()
                        && missingUmuLaunch.error.containsIgnoreCase("umu-run"),
                    "report an explicit error when requested UMU is missing") && passed;
    passed = expect(cydaw::app::WindowsPluginBridge::inspect().nativeWorker.existsAsFile(),
                    "discover the native isolated plugin worker") && passed;
    const auto scheduledRecordingResult =
        cydaw::app::InputRecorder::selfTestScheduledRecording(
            tempRoot.getChildFile("scheduled-input.wav"));
    if (scheduledRecordingResult.failed())
        std::cerr << "Scheduled recording diagnostic: "
                  << scheduledRecordingResult.getErrorMessage() << '\n';
    passed = expect(scheduledRecordingResult.wasOk(),
                    "sample-accurate quantized input recording boundaries")
             && passed;
    const auto doubleDeckResult =
        cydaw::app::AudioTrackController::selfTestDoubleDeckLauncher(
            firstFile, secondFile);
    if (doubleDeckResult.failed())
        std::cerr << "Double-deck diagnostic: "
                  << doubleDeckResult.getErrorMessage() << '\n';
    passed = expect(doubleDeckResult.wasOk(),
                    "sample-exact preloaded double-deck switching") && passed;
    const auto launcherTimelineResult =
        cydaw::app::AudioTrackController::selfTestLauncherTimeline(
            firstFile);
    if (launcherTimelineResult.failed())
        std::cerr << "Launcher timeline diagnostic: "
                  << launcherTimelineResult.getErrorMessage() << '\n';
    passed = expect(launcherTimelineResult.wasOk(),
                    "launcher timelines keep their duration on empty audio tracks")
             && passed;
    const auto midiLauncherResult =
        cydaw::app::AudioTrackController::selfTestMidiLauncherClip();
    if (midiLauncherResult.failed())
        std::cerr << "MIDI launcher diagnostic: "
                  << midiLauncherResult.getErrorMessage() << '\n';
    passed = expect(midiLauncherResult.wasOk(),
                    "MIDI launcher clips play in clip-local time") && passed;
    passed = expect(
                 cydaw::app::ClipLauncherComponent::selfTestInputRouting(),
                 "launcher + Track and armed empty-slot double-click routing")
             && passed;
    passed = expect(cydaw::app::MixerComponent::selfTestInputRouting(),
                    "mixer strips route faders, pan and mute to their handlers")
             && passed;
    const auto plumbing = cydaw::app::NodeGraphComponent::selfTestNativeParameterPlumbing();
    if (plumbing.failed())
        std::cerr << "Parameter plumbing diagnostic: " << plumbing.getErrorMessage() << '\n';
    passed = expect(plumbing.wasOk(),
                    "every node parameter survives a reload and every "
                    "published parameter can be written")
             && passed;
    const auto multiclipResult =
        cydaw::app::AudioTrackController::selfTestMulticlipArrangement(
            firstFile, secondFile);
    if (multiclipResult.failed())
        std::cerr << "Multiclip diagnostic: "
                  << multiclipResult.getErrorMessage() << '\n';
    passed = expect(multiclipResult.wasOk(),
                    "prefetched audio regions render gaps, overlaps and moves")
             && passed;

    {
        CountingAudioSource source;
        cydaw::app::LatencyCompensatingAudioSource compensated(source);
        compensated.prepareToPlay(8, 48'000.0);
        compensated.setDelaySamples(3);
        juce::AudioBuffer<float> delayedBuffer(2, 8);
        compensated.getNextAudioBlock({ &delayedBuffer, 0, delayedBuffer.getNumSamples() });
        passed = expect(delayedBuffer.getSample(0, 0) == 0.0f
                            && delayedBuffer.getSample(0, 2) == 0.0f
                            && delayedBuffer.getSample(0, 3) == 1.0f
                            && delayedBuffer.getSample(1, 7) == 5.0f,
                        "sample-accurate latency compensation delay") && passed;
        compensated.setDelaySamples(0);
        compensated.getNextAudioBlock({ &delayedBuffer, 0, delayedBuffer.getNumSamples() });
        passed = expect(delayedBuffer.getSample(0, 0) == 9.0f
                            && delayedBuffer.getSample(1, 7) == 16.0f,
                        "zero-delay latency compensation passthrough") && passed;
        compensated.setDelaySamples(cydaw::app::LatencyCompensatingAudioSource::maximumDelaySamples + 1);
        passed = expect(compensated.delaySamples()
                            == cydaw::app::LatencyCompensatingAudioSource::maximumDelaySamples,
                        "latency compensation bound") && passed;
        compensated.releaseResources();
    }

    {
        using Kind = cydaw::engine::NativeDspNodeKind;
        ConstantAudioSource source(0.5f);
        cydaw::app::GainPanAudioSource graphSource(source);
        graphSource.prepareToPlay(64, 48'000.0);
        cydaw::engine::NativeDspGraph graph;
        graph.nodes = { { 1, Kind::input }, { 2, Kind::drive },
                        { 3, Kind::lowpass }, { 4, Kind::output } };
        graph.connections = { { 1, 2 }, { 2, 3 }, { 3, 4 } };
        graph.outputNodeId = 4;

        cydaw::engine::NativeDspGraph portAwareGraph;
        portAwareGraph.nodes = { { 11, Kind::input },
                                 { 12, Kind::mixer },
                                 { 13, Kind::output } };
        portAwareGraph.connections = {
            { 11, 12, 101, 201 },
            { 11, 12, 102, 202 },
            { 12, 13, 301, 401 }
        };
        portAwareGraph.outputNodeId = 13;
        const auto portAwarePlan = cydaw::engine::compileNativeDspGraph(
            portAwareGraph, { 48'000.0, 64, 2 });
        passed = expect(portAwarePlan
                            && portAwarePlan.plan->connectionCount() == 3
                            && portAwarePlan.plan->inputBusCount(12) == 2
                            && portAwarePlan.plan->hasConnection(
                                11, 101, 12, 201)
                            && portAwarePlan.plan->hasConnection(
                                11, 102, 12, 202),
                        "native render plan preserves exact multi-port routing")
                 && passed;

        passed = expect(graphSource.setNativeDspGraph(graph).wasOk()
                            && graphSource.nativeDspNodeCount() == 4,
                        "compile native DSP DAG") && passed;
        juce::AudioBuffer<float> nativeBuffer(2, 64);
        graphSource.getNextAudioBlock({ &nativeBuffer, 0, 64 });
        passed = expect(std::abs(nativeBuffer.getSample(0, 63) - 0.5f) < 0.01f,
                        "native DSP input-to-output processing") && passed;
        cydaw::engine::AudioAnalysisFrame analysisFrame;
        const auto renderedPeak = nativeBuffer.getMagnitude(0, 0, 64);
        const auto renderedRms = nativeBuffer.getRMSLevel(1, 0, 64);
        passed = expect(graphSource.tryPopAnalysisFrame(analysisFrame)
                            && analysisFrame.waveformCount == 64
                            && analysisFrame.channelCount == 2
                            && std::abs(analysisFrame.peak[0] - renderedPeak)
                                   < 0.01f
                            && std::abs(analysisFrame.rms[1] - renderedRms)
                                   < 0.01f,
                        "lock-free post-DSP waveform, peak and RMS telemetry") && passed;

        auto atomicUpdate = graph;
        atomicUpdate.nodes[1].parameters.driveDb = 12.0f;
        atomicUpdate.nodes[2].parameters.cutoffHz = 12'000.0f;
        passed = expect(graphSource.updateNativeDspParameters(atomicUpdate).wasOk()
                            && graphSource.nativeDspNodeCount() == 4,
                        "update native parameters without rebuilding topology") && passed;
        graphSource.getNextAudioBlock({ &nativeBuffer, 0, 64 });
        passed = expect(nativeBuffer.getSample(0, 63) > 0.85f,
                        "atomic graph parameter update reaches active processors") && passed;
        auto invalidAtomicUpdate = atomicUpdate;
        invalidAtomicUpdate.connections.pop_back();
        passed = expect(graphSource.updateNativeDspParameters(invalidAtomicUpdate).failed()
                            && graphSource.nativeDspNodeCount() == 4,
                        "atomic parameter path rejects topology changes") && passed;

        graphSource.setDrive(12.0f);
        graphSource.getNextAudioBlock({ &nativeBuffer, 0, 64 });
        passed = expect(nativeBuffer.getSample(0, 63) > 0.85f,
                        "live atomic parameter reaches compiled drive node") && passed;

        graph.connections.clear();
        passed = expect(graphSource.setNativeDspGraph(graph).wasOk(),
                        "publish disconnected native graph") && passed;
        graphSource.getNextAudioBlock({ &nativeBuffer, 0, 64 });
        passed = expect(std::abs(nativeBuffer.getMagnitude(0, 64)) < 0.000001f,
                        "disconnected graph produces silence") && passed;

        graph.nodes = { { 5, Kind::oscillator }, { 6, Kind::output } };
        graph.connections = { { 5, 6 } };
        graph.outputNodeId = 6;
        passed = expect(graphSource.setNativeDspGraph(graph).wasOk(),
                        "publish generator graph") && passed;
        graphSource.getNextAudioBlock({ &nativeBuffer, 0, 64 });
        passed = expect(nativeBuffer.getMagnitude(0, 64) > 0.1f,
                        "connected oscillator node generates audio") && passed;
        graphSource.releaseResources();
    }

    {
        // An audio track's chain went through the engine compiler, which
        // skips every control node and knows nothing of modulations: an LFO
        // dropped on a knob of an audio track was compiled nowhere. The
        // track runs the runtime a bus or an instrument's chain runs: the
        // gain swings with the LFO, and the track reports both control nodes.
        using Kind = cydaw::engine::NativeDspNodeKind;
        ConstantAudioSource source(0.5f);
        cydaw::app::GainPanAudioSource track(source);
        track.prepareToPlay(64, 48'000.0);
        cydaw::engine::NativeDspGraph graph;
        graph.nodes = { { 1, Kind::input }, { 2, Kind::gain },
                        { 3, Kind::output }, { 4, Kind::controlLfo },
                        { 5, Kind::controlMapRange } };
        auto& lfo = graph.nodes[3].parameters;
        lfo.modulationRateHz = 150.0f;
        lfo.modulationDepth = 1.0f;
        lfo.modulationOffset = 0.0f;
        lfo.modulationShape = 0.0f;
        auto& range = graph.nodes[4].parameters;
        range.mapInputMinimum = -1.0f;
        range.mapInputMaximum = 1.0f;
        range.mapOutputMinimum = -40.0f;
        range.mapOutputMaximum = 0.0f;
        graph.connections = { { 1, 2 }, { 2, 3 } };
        graph.modulations = { { 4, 5, 0, 1, "" }, { 5, 2, 0, 0, "gain-db" } };
        graph.outputNodeId = 3;
        const auto published = track.setNativeDspGraph(graph).wasOk();
        juce::AudioBuffer<float> buffer(2, 64);
        auto loudest = 0.0f;
        auto quietest = 10.0f;
        for (int block = 0; block < 32; ++block) {
            track.getNextAudioBlock({ &buffer, 0, 64 });
            const auto level = std::abs(buffer.getSample(0, 63));
            loudest = std::max(loudest, level);
            quietest = std::min(quietest, level);
        }
        const auto values = track.controlValues();
        const auto reported = [&values](const std::uint64_t id) {
            return std::any_of(values.begin(), values.end(),
                               [id](const auto& entry) {
                                   return entry.first == id;
                               });
        };
        const auto label = juce::String(
            "an audio track's chain runs its LFO (nodes=")
            + juce::String(track.nativeDspNodeCount())
            + " loudest=" + juce::String(loudest, 3)
            + " quietest=" + juce::String(quietest, 3)
            + " controls=" + juce::String(values.size()) + ")";
        passed = expect(published && track.nativeDspNodeCount() == 3
                            && loudest > 0.3f && quietest < 0.15f
                            && reported(4) && reported(5),
                        label.toRawUTF8())
                 && passed;
        track.releaseResources();
    }

    {
        ConstantAudioSource source(0.4f);
        cydaw::app::MasterOutputAudioSource master(source);
        master.prepareToPlay(64, 48'000.0);
        master.setGain(0.5f);
        juce::AudioBuffer<float> masterBuffer(2, 64);
        master.getNextAudioBlock({ &masterBuffer, 0, 64 });
        cydaw::engine::AudioAnalysisFrame frame;
        passed = expect(master.tryPopAnalysisFrame(frame)
                            && frame.channelCount == 2
                            && std::abs(masterBuffer.getSample(0, 0) - 0.2f)
                                   < 0.001f
                            && std::abs(frame.peak[0] - 0.2f) < 0.001f
                            && std::abs(frame.rms[1] - 0.2f) < 0.001f,
                        "master gain feeds output, peak and RMS telemetry") && passed;
        juce::AudioBuffer<float> surroundMasterBuffer(6, 64);
        master.getNextAudioBlock({ &surroundMasterBuffer, 0, 64 });
        passed = expect(master.tryPopAnalysisFrame(frame)
                            && frame.channelCount == 6
                            && std::abs(frame.peak[5] - 0.2f) < 0.001f
                            && std::abs(frame.rms[5] - 0.2f) < 0.001f,
                        "master analysis preserves exact 5.1 channel telemetry")
                 && passed;
        master.releaseResources();
    }

    {
        cydaw::app::NodeGraphComponent graphView;
        cydaw::engine::AudioAnalysisFrame frame;
        frame.waveformCount = static_cast<std::uint32_t>(frame.waveform.size());
        frame.channelCount = 2;
        frame.peak = { 0.8f, 0.6f };
        frame.rms = { 0.35f, 0.25f };
        for (std::size_t sample = 0; sample < frame.waveform.size(); ++sample)
            frame.waveform[sample] = static_cast<float>(
                0.6 * std::sin(juce::MathConstants<double>::twoPi * 8.0
                               * static_cast<double>(sample)
                               / static_cast<double>(frame.waveform.size())));
        graphView.setAnalysisFrame(frame);
        passed = expect(graphView.hasAnalysisFrame()
                            && graphView.maximumSpectrumMagnitude() > 0.4f,
                        "UI-thread spectrum analysis for node visualisation") && passed;
        graphView.clearAnalysis();
        passed = expect(!graphView.hasAnalysisFrame()
                            && graphView.maximumSpectrumMagnitude() == 0.0f,
                        "clear node-analysis visual state") && passed;
    }

    if (argc >= 2) {
        testPluginBundle = juce::File(juce::String::fromUTF8(argv[1]));
        if (testPluginBundle.getParentDirectory().getFileName().containsIgnoreCase("x86_64"))
            testPluginBundle = testPluginBundle.getParentDirectory().getParentDirectory().getParentDirectory();

        ConstantAudioSource source(0.4f);
        cydaw::app::GainPanAudioSource chain(source);
        chain.prepareToPlay(64, 48'000.0);
        std::size_t firstPlugin {};
        std::size_t secondPlugin {};
        passed = expect(chain.addPlugin(testPluginBundle, 48'000.0, 64, 2, &firstPlugin).wasOk()
                            && chain.addPlugin(testPluginBundle, 48'000.0, 64, 2, &secondPlugin).wasOk(),
                        "append two isolated VST3 instances") && passed;
        passed = expect(firstPlugin == 0 && secondPlugin == 1 && chain.pluginCount() == 2,
                        "stable plugin-chain indices") && passed;
        passed = expect(chain.setPluginParameter(0, 0, 0.25f).wasOk()
                            && chain.setPluginParameter(1, 0, 0.75f).wasOk(),
                        "independent parameter state per worker") && passed;
        passed = expect(chain.totalPluginLatencySamples() >= 128,
                        "plugin-chain latency is cumulative") && passed;

        juce::AudioBuffer<float> chainedBuffer(2, 64);
        auto processedByBoth = false;
        for (int attempt = 0; attempt < 100 && !processedByBoth; ++attempt) {
            chain.getNextAudioBlock({ &chainedBuffer, 0, chainedBuffer.getNumSamples() });
            processedByBoth = std::abs(chainedBuffer.getSample(0, 12) - 0.075f) < 0.0001f;
            if (!processedByBoth)
                juce::Thread::sleep(2);
        }
        passed = expect(processedByBoth,
                        "two isolated VST3 workers process audio sequentially") && passed;

        passed = expect(chain.movePlugin(0, 1), "move plugin later in chain") && passed;
        std::vector<cydaw::app::IsolatedPluginHost::ParameterSnapshot> firstParameters;
        std::vector<cydaw::app::IsolatedPluginHost::ParameterSnapshot> secondParameters;
        passed = expect(chain.refreshPluginParameters(0, firstParameters).wasOk()
                            && chain.refreshPluginParameters(1, secondParameters).wasOk()
                            && !firstParameters.empty() && !secondParameters.empty()
                            && std::abs(firstParameters.front().value - 0.75f) < 0.001f
                            && std::abs(secondParameters.front().value - 0.25f) < 0.001f,
                        "reorder preserves worker identity and state") && passed;

        chain.removePlugin(0);
        passed = expect(chain.pluginCount() == 1,
                        "remove one plugin without stopping the remaining worker") && passed;
        firstParameters.clear();
        passed = expect(chain.refreshPluginParameters(0, firstParameters).wasOk()
                            && !firstParameters.empty()
                            && std::abs(firstParameters.front().value - 0.25f) < 0.001f,
                        "remaining plugin shifts into the selected slot") && passed;
        chain.clearPlugins();
        passed = expect(chain.pluginCount() == 0 && chain.totalPluginLatencySamples() == 0,
                        "clear isolated plugin chain") && passed;
        chain.releaseResources();
    } else {
        passed = expect(false, "VST3 test plugin argument") && passed;
    }

    {
        cydaw::app::AudioTrackController session;
        cydaw::engine::NativeDspGraph storedGraph;
        storedGraph.nodes = {
            { 1, cydaw::engine::NativeDspNodeKind::input },
            { 2, cydaw::engine::NativeDspNodeKind::output }
        };
        storedGraph.connections = { { 1, 2 } };
        storedGraph.outputNodeId = 2;
        passed = expect(session.setNativeDspGraph(storedGraph).wasOk(),
                        "store native DSP graph before tracks exist") && passed;
        std::size_t firstIndex {};
        std::size_t secondIndex {};
        passed = expect(session.addAudioTrack(firstFile, &firstIndex).wasOk(), "add first track") && passed;
        passed = expect(session.addAudioTrack(secondFile, &secondIndex).wasOk(), "add second track") && passed;
        passed = expect(firstIndex == 0 && secondIndex == 1, "stable track indices") && passed;
        passed = expect(session.trackCount() == 2, "multipiste count") && passed;
        passed = expect(session.nativeDspNodeCount(0) == 2
                            && session.nativeDspNodeCount(1) == 2,
                        "new tracks inherit the compiled native DSP graph") && passed;
        auto firstTrackGraph = storedGraph;
        firstTrackGraph.nodes = {
            { 1, cydaw::engine::NativeDspNodeKind::input },
            { 3, cydaw::engine::NativeDspNodeKind::drive },
            { 2, cydaw::engine::NativeDspNodeKind::output }
        };
        firstTrackGraph.connections = { { 1, 3 }, { 3, 2 } };
        passed = expect(session.setTrackNativeDspGraph(0, firstTrackGraph).wasOk(),
                        "publish native DSP graph to one track") && passed;
        auto firstTrackParameterUpdate = firstTrackGraph;
        firstTrackParameterUpdate.nodes[1].parameters.driveDb = 15.0f;
        passed = expect(session.updateTrackNativeDspParameters(
                                0, firstTrackParameterUpdate).wasOk()
                            && std::abs(session.nativeDspGraph(0).nodes[1]
                                            .parameters.driveDb
                                        - 15.0f) < 0.001f,
                        "session publishes parameter-only updates to one active plan") && passed;
        auto rejectedTrackParameterUpdate = firstTrackParameterUpdate;
        rejectedTrackParameterUpdate.outputNodeId = 3;
        passed = expect(session.updateTrackNativeDspParameters(
                                0, rejectedTrackParameterUpdate).failed(),
                        "session parameter path rejects output-topology changes") && passed;
        passed = expect(session.nativeDspNodeCount(0) == 3
                            && session.nativeDspNodeCount(1) == 2,
                        "audio tracks keep independent compiled DSP topologies") && passed;
        const auto storedFirstTrackGraph = session.nativeDspGraph(0);
        const auto storedSecondTrackGraph = session.nativeDspGraph(1);
        passed = expect(storedFirstTrackGraph.nodes.size() == 3
                            && storedFirstTrackGraph.connections.size() == 2
                            && storedSecondTrackGraph.nodes.size() == 2
                            && storedSecondTrackGraph.connections.size() == 1,
                        "per-track native DSP descriptions remain independently queryable") && passed;
        passed = expect(session.setTrackNativeDspGraph(9, firstTrackGraph).failed(),
                        "reject native DSP graph for a missing track") && passed;
        passed = expect(std::abs(session.length() - 1.0) < 0.001, "session length") && passed;
        const auto initialAudioClips = session.audioClips(0);
        passed = expect(initialAudioClips.size() == 1
                            && initialAudioClips.front().id != 0
                            && std::abs(initialAudioClips.front().durationSeconds
                                        - 1.0) < 0.001,
                        "new audio track owns one full-source arrangement region") && passed;
        session.clearArrangementEditHistory(0);
        passed = expect(session.setAudioClipStretchSettings(
                                0, initialAudioClips.front().id, 120.0,
                                cydaw::app::AudioTrackController::StretchMode::beats)
                            && session.audioClip(
                                   0, initialAudioClips.front().id)
                                   ->sourceTempoBpm == 120.0
                            && session.audioClip(
                                   0, initialAudioClips.front().id)
                                   ->stretchMode
                                   == cydaw::app::AudioTrackController::StretchMode::beats
                            && session.undoArrangementEdit(0),
                        "clip tempo and stretch mode are editable and undoable") && passed;
        session.clearArrangementEditHistory(0);
        passed = expect(session.stretchAudioClip(
                                0, initialAudioClips.front().id, 1.5)
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->durationSeconds - 1.5) < 0.001
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->sourceDurationSeconds - 1.0) < 0.001
                            && std::abs(session.length() - 1.5) < 0.001
                            && session.undoArrangementEdit(0)
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->durationSeconds - 1.0) < 0.001
                            && session.redoArrangementEdit(0)
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->durationSeconds - 1.5) < 0.001
                            && session.undoArrangementEdit(0),
                        "non-destructive time stretch keeps the source span and supports undo") && passed;
        session.clearArrangementEditHistory(0);
        const auto stretchMarkerId = session.addAudioClipStretchMarker(
            0, initialAudioClips.front().id, 0.5, 0.25);
        passed = expect(
            stretchMarkerId != 0
                && session.audioClip(0, initialAudioClips.front().id)
                       ->stretchMarkers.size() == 1
                && session.updateAudioClipStretchMarker(
                    0, initialAudioClips.front().id, stretchMarkerId, 0.35)
                && std::abs(session.audioClip(
                                0, initialAudioClips.front().id)
                                ->stretchMarkers.front().timelineSeconds
                            - 0.35) < 0.001
                && session.undoArrangementEdit(0)
                && std::abs(session.audioClip(
                                0, initialAudioClips.front().id)
                                ->stretchMarkers.front().timelineSeconds
                            - 0.25) < 0.001
                && session.redoArrangementEdit(0)
                && session.removeAudioClipStretchMarker(
                    0, initialAudioClips.front().id, stretchMarkerId)
                && session.audioClip(0, initialAudioClips.front().id)
                       ->stretchMarkers.empty()
                && std::abs(session.length() - 1.0) < 0.001,
            "piecewise stretch markers are editable, rendered and undoable")
                 && passed;
        session.clearArrangementEditHistory(0);
        const auto clipAutomationLate
            = session.addAudioClipAutomationPoint(
                0, initialAudioClips.front().id, 0.75, -12.0f);
        const auto clipAutomationEarly
            = session.addAudioClipAutomationPoint(
                0, initialAudioClips.front().id, 0.25, 3.0f);
        auto clipAutomation = session.audioClipAutomationPoints(
            0, initialAudioClips.front().id);
        passed = expect(
            clipAutomationLate != 0 && clipAutomationEarly != 0
                && clipAutomation.size() == 2
                && std::abs(clipAutomation.front().timeSeconds - 0.25)
                       < 0.000001
                && std::abs(clipAutomation.back().timeSeconds - 0.75)
                       < 0.000001
                && session.automationPoints(0).empty(),
            "audio clip automation keeps clip-local time instead of track time")
                 && passed;
        passed = expect(
            session.updateAudioClipAutomationPoint(
                0, initialAudioClips.front().id,
                clipAutomationLate, 0.5, -6.0f)
                && session.setAudioClipAutomationPointCurve(
                    0, initialAudioClips.front().id,
                    clipAutomationLate,
                    cydaw::app::AudioTrackController::AutomationCurve::bezier,
                    0.35f)
                && session.audioClipAutomationPoints(
                       0, initialAudioClips.front().id).back().curve
                       == cydaw::app::AudioTrackController::AutomationCurve::bezier
                && session.removeAudioClipAutomationPoint(
                    0, initialAudioClips.front().id,
                    clipAutomationEarly)
                && session.audioClipAutomationPoints(
                       0, initialAudioClips.front().id).size() == 1,
            "audio clip automation edits, curves and deletes independently")
                 && passed;
        const auto secondTrackClips = session.audioClips(1);
        std::uint64_t compoundEventId {};
        const auto compoundResult = session.addAudioClipEventFromFile(
            1, secondTrackClips.front().id, firstFile, 0.4,
            &compoundEventId);
        auto compoundEvents = session.audioClipEvents(
            1, secondTrackClips.front().id);
        const auto compoundMarker = compoundEvents.empty()
            ? 0
            : session.addAudioClipEventStretchMarker(
                1, secondTrackClips.front().id,
                compoundEvents.front().id, 0.5, 0.35);
        const auto splitCompoundEvent = session.splitAudioClipEvent(
            1, secondTrackClips.front().id, compoundEventId, 0.75);
        passed = expect(
            compoundResult.wasOk() && compoundEventId != 0
                && compoundEvents.size() == 2
                && compoundMarker != 0
                && session.updateAudioClipEventStretchMarker(
                    1, secondTrackClips.front().id,
                    compoundEvents.front().id, compoundMarker, 0.42)
                && splitCompoundEvent != 0
                && session.audioClipEvents(
                       1, secondTrackClips.front().id).size() == 3
                && session.removeAudioClipEvent(
                    1, secondTrackClips.front().id, splitCompoundEvent),
            "clip-local timeline stores, warps, splits and removes multiple audio events")
                 && passed;
        session.clearArrangementEditHistory(0);
        const auto duplicateClipId = session.duplicateAudioClip(
            0, initialAudioClips.front().id, 1.25);
        passed = expect(duplicateClipId != 0
                            && session.audioClips(0).size() == 2
                            && std::abs(session.length() - 2.25) < 0.001,
                        "duplicate audio region extends the arrangement") && passed;
        const auto splitClipId = session.splitAudioClip(0, duplicateClipId, 1.75);
        passed = expect(splitClipId != 0
                            && session.audioClips(0).size() == 3,
                        "split audio region preserves both source halves") && passed;
        passed = expect(session.moveAudioClip(0, splitClipId, 2.0)
                            && std::abs(session.length() - 2.5) < 0.001,
                        "move audio region updates timeline length without rebuilding readers") && passed;
        passed = expect(session.updateAudioClip(
                                0, initialAudioClips.front().id,
                                0.1, 0.2, 0.4)
                            && session.audioClip(0, initialAudioClips.front().id)
                                   .has_value()
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->sourceOffsetSeconds - 0.2) < 0.001,
                        "trim one audio region independently") && passed;
        passed = expect(session.removeAudioClip(0, duplicateClipId)
                            && session.audioClips(0).size() == 2,
                        "remove one audio region without removing its track") && passed;
        std::uint64_t importedClipId {};
        passed = expect(session.addAudioClipFromFile(
                                0, secondFile, 3.0, &importedClipId).wasOk()
                            && importedClipId != 0
                            && session.audioClip(0, importedClipId).has_value()
                            && session.audioClip(0, importedClipId)->sourceFile
                                   == secondFile
                            && std::abs(session.audioClip(0, importedClipId)
                                            ->sourceLengthSeconds - 1.0) < 0.001
                            && std::abs(session.length() - 4.0) < 0.001,
                        "import a 44.1 kHz source as a region on a 48 kHz track")
                 && passed;
        passed = expect(session.removeAudioClip(0, importedClipId)
                            && session.audioClips(0).size() == 2,
                        "remove imported multi-source region") && passed;
        session.clearArrangementEditHistory(0);
        passed = expect(session.moveAudioClip(0, splitClipId, 0.3)
                            && session.canUndoArrangementEdit(0)
                            && session.undoArrangementEdit(0)
                            && std::abs(session.audioClip(0, splitClipId)
                                            ->startSeconds - 2.0) < 0.001
                            && session.canRedoArrangementEdit(0)
                            && session.redoArrangementEdit(0)
                            && std::abs(session.audioClip(0, splitClipId)
                                            ->startSeconds - 0.3) < 0.001,
                        "move region undo and redo as one transaction") && passed;
        passed = expect(session.autoCrossfadeAudioClip(
                                   0, initialAudioClips.front().id)
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->fadeOutSeconds - 0.2) < 0.001
                            && std::abs(session.audioClip(0, splitClipId)
                                            ->fadeInSeconds - 0.2) < 0.001,
                        "create matching fades across overlapping regions") && passed;
        passed = expect(session.undoArrangementEdit(0)
                            && session.audioClip(
                                   0, initialAudioClips.front().id)
                                   ->fadeOutSeconds == 0.0
                            && session.audioClip(0, splitClipId)
                                   ->fadeInSeconds == 0.0
                            && session.redoArrangementEdit(0)
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->fadeOutSeconds - 0.2) < 0.001,
                        "crossfade undo and redo as one transaction") && passed;
        passed = expect(session.updateAudioClip(
                                0, initialAudioClips.front().id,
                                0.15, 0.25, 0.3, 0.05, 0.1)
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->fadeInSeconds - 0.05) < 0.001
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->fadeOutSeconds - 0.1) < 0.001,
                        "commit direct trim geometry and fade handles") && passed;
        passed = expect(session.undoArrangementEdit(0)
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->startSeconds - 0.1) < 0.001
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->fadeOutSeconds - 0.2) < 0.001
                            && session.redoArrangementEdit(0)
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->startSeconds - 0.15) < 0.001,
                        "trim and fade handles undo and redo as one transaction") && passed;
        session.clearArrangementEditHistory(0);
        const auto historyDuplicateId = session.duplicateAudioClip(
            0, initialAudioClips.front().id, 1.1);
        passed = expect(historyDuplicateId != 0
                            && session.audioClips(0).size() == 3
                            && session.undoArrangementEdit(0)
                            && session.audioClips(0).size() == 2
                            && session.redoArrangementEdit(0)
                            && session.audioClips(0).size() == 3,
                        "duplicate region undo and redo as one transaction") && passed;
        passed = expect(session.removeAudioClip(0, historyDuplicateId)
                            && session.audioClips(0).size() == 2
                            && session.undoArrangementEdit(0)
                            && session.audioClips(0).size() == 3
                            && session.redoArrangementEdit(0)
                            && session.audioClips(0).size() == 2,
                        "delete region undo and redo as one transaction") && passed;
        session.clearArrangementEditHistory(0);
        const auto historySplitId = session.splitAudioClip(
            0, initialAudioClips.front().id, 0.3);
        passed = expect(historySplitId != 0
                            && session.audioClips(0).size() == 3
                            && session.undoArrangementEdit(0)
                            && session.audioClips(0).size() == 2
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->durationSeconds - 0.3) < 0.001
                            && session.redoArrangementEdit(0)
                            && session.audioClips(0).size() == 3
                            && session.undoArrangementEdit(0)
                            && session.audioClips(0).size() == 2,
                        "split region undo and redo as one transaction") && passed;
        session.clearArrangementEditHistory(0);
        const std::vector<std::uint64_t> groupIds {
            initialAudioClips.front().id, splitClipId
        };
        passed = expect(session.moveAudioClips(0, groupIds, 0.125)
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->startSeconds - 0.275) < 0.001
                            && std::abs(session.audioClip(0, splitClipId)
                                            ->startSeconds - 0.425) < 0.001
                            && session.undoArrangementEdit(0)
                            && std::abs(session.audioClip(
                                            0, initialAudioClips.front().id)
                                            ->startSeconds - 0.15) < 0.001
                            && session.redoArrangementEdit(0)
                            && std::abs(session.audioClip(0, splitClipId)
                                            ->startSeconds - 0.425) < 0.001
                            && session.undoArrangementEdit(0),
                        "move a region group with preserved spacing in one transaction") && passed;
        session.clearArrangementEditHistory(0);
        const auto groupDuplicates = session.duplicateAudioClips(0, groupIds);
        passed = expect(groupDuplicates.size() == 2
                            && session.audioClips(0).size() == 4
                            && std::abs(session.audioClip(0, groupDuplicates[1])
                                            ->startSeconds
                                        - session.audioClip(0, groupDuplicates[0])
                                              ->startSeconds
                                        - 0.15) < 0.001
                            && session.undoArrangementEdit(0)
                            && session.audioClips(0).size() == 2
                            && session.redoArrangementEdit(0)
                            && session.audioClips(0).size() == 4
                            && session.undoArrangementEdit(0)
                            && session.audioClips(0).size() == 2,
                        "duplicate a region group in one transaction") && passed;
        session.clearArrangementEditHistory(0);
        passed = expect(session.removeAudioClips(0, groupIds)
                            && session.audioClips(0).empty()
                            && session.undoArrangementEdit(0)
                            && session.audioClips(0).size() == 2
                            && session.redoArrangementEdit(0)
                            && session.audioClips(0).empty()
                            && session.undoArrangementEdit(0)
                            && session.audioClips(0).size() == 2,
                        "delete a region group in one transaction") && passed;
        session.clearArrangementEditHistory(0);
        std::vector<cydaw::app::AudioTrackController::AudioClip> clipboardClips;
        for (const auto id : groupIds)
            if (const auto clip = session.audioClip(0, id))
                clipboardClips.push_back(*clip);
        const auto pastedIds = session.insertAudioClips(
            0, clipboardClips, 1.5);
        passed = expect(pastedIds.size() == 2
                            && session.audioClips(0).size() == 4
                            && std::abs(session.audioClip(0, pastedIds[0])
                                            ->startSeconds - 1.5) < 0.001
                            && std::abs(session.audioClip(0, pastedIds[1])
                                            ->startSeconds - 1.65) < 0.001
                            && session.audioClip(0, pastedIds[0])->sourceFile
                                   == clipboardClips[0].sourceFile
                            && session.undoArrangementEdit(0)
                            && session.audioClips(0).size() == 2
                            && session.redoArrangementEdit(0)
                            && session.audioClips(0).size() == 4
                            && session.undoArrangementEdit(0)
                            && session.audioClips(0).size() == 2,
                        "paste a region group at a new anchor in one transaction") && passed;
        session.clearArrangementEditHistory(0);
        const auto secondTrackClipCount = session.audioClips(1).size();
        const auto crossTrackPastedIds = session.insertAudioClips(
            1, clipboardClips, 0.5);
        passed = expect(crossTrackPastedIds.size() == 2
                            && session.audioClips(1).size() == secondTrackClipCount + 2
                            && std::abs(session.audioClip(1, crossTrackPastedIds[0])
                                            ->startSeconds - 0.5) < 0.001
                            && std::abs(session.audioClip(1, crossTrackPastedIds[1])
                                            ->startSeconds - 0.65) < 0.001
                            && session.audioClip(1, crossTrackPastedIds[0])->sourceFile
                                   == clipboardClips[0].sourceFile
                            && session.undoArrangementEdit(1)
                            && session.audioClips(1).size() == secondTrackClipCount
                            && session.redoArrangementEdit(1)
                            && session.audioClips(1).size() == secondTrackClipCount + 2
                            && session.undoArrangementEdit(1)
                            && session.audioClips(1).size() == secondTrackClipCount,
                        "paste a region group across tracks in one transaction") && passed;
        session.clearArrangementEditHistory(1);
        const auto firstTrackBeforeBatch = session.audioClips(0).size();
        const auto secondTrackBeforeBatch = session.audioClips(1).size();
        const auto secondTrackBatchClip = session.audioClips(1).front().id;
        session.clearArrangementEditHistory();
        session.beginArrangementEditBatch();
        const auto removedFirstBatchClip = session.removeAudioClips(
            0, { groupIds.front() });
        const auto removedSecondBatchClip = session.removeAudioClips(
            1, { secondTrackBatchClip });
        const auto committedBatch = session.endArrangementEditBatch();
        passed = expect(removedFirstBatchClip && removedSecondBatchClip
                            && committedBatch
                            && session.audioClips(0).size()
                                   == firstTrackBeforeBatch - 1
                            && session.audioClips(1).size()
                                   == secondTrackBeforeBatch - 1
                            && session.canUndoArrangementEdit()
                            && session.undoArrangementEdit()
                            && session.audioClips(0).size()
                                   == firstTrackBeforeBatch
                            && session.audioClips(1).size()
                                   == secondTrackBeforeBatch
                            && session.canRedoArrangementEdit()
                            && session.redoArrangementEdit()
                            && session.audioClips(0).size()
                                   == firstTrackBeforeBatch - 1
                            && session.audioClips(1).size()
                                   == secondTrackBeforeBatch - 1
                            && session.undoArrangementEdit()
                            && session.audioClips(0).size()
                                   == firstTrackBeforeBatch
                            && session.audioClips(1).size()
                                   == secondTrackBeforeBatch,
                        "undo and redo a cross-track arrangement batch atomically") && passed;
        session.clearArrangementEditHistory();

        const auto rangeSourceId = session.addAudioClip(
            0, 5.0, 0.0, 1.0, "Range edit source");
        const auto rangeSourceCount = session.audioClips(0).size();
        session.clearArrangementEditHistory();
        const auto splitRange = session.editAudioClipRange(
            0, { rangeSourceId }, 5.25, 5.75,
            cydaw::app::AudioTrackController::ArrangementRangeOperation::
                splitAtBoundaries);
        const auto splitMiddle = splitRange.affectedClipIds.size() == 1
                                     ? session.audioClip(
                                           0, splitRange.affectedClipIds.front())
                                     : std::nullopt;
        passed = expect(rangeSourceId != 0
                            && splitRange.succeeded && splitRange.changed
                            && splitRange.affectedClipIds.size() == 1
                            && session.audioClips(0).size()
                                   == rangeSourceCount + 2
                            && splitMiddle.has_value()
                            && std::abs(splitMiddle->startSeconds - 5.25)
                                   < 0.001
                            && std::abs(splitMiddle->durationSeconds - 0.5)
                                   < 0.001
                            && session.undoArrangementEdit()
                            && session.audioClips(0).size()
                                   == rangeSourceCount
                            && session.redoArrangementEdit()
                            && session.audioClips(0).size()
                                   == rangeSourceCount + 2
                            && session.undoArrangementEdit(),
                        "split both time-range boundaries in one transaction")
                 && passed;

        session.clearArrangementEditHistory();
        const auto croppedRange = session.editAudioClipRange(
            0, { rangeSourceId }, 5.2, 5.8,
            cydaw::app::AudioTrackController::ArrangementRangeOperation::
                cropToRange);
        const auto croppedClip = session.audioClip(0, rangeSourceId);
        passed = expect(croppedRange.succeeded && croppedRange.changed
                            && croppedRange.affectedClipIds
                                   == std::vector<std::uint64_t> {
                                          rangeSourceId }
                            && croppedClip.has_value()
                            && std::abs(croppedClip->startSeconds - 5.2)
                                   < 0.001
                            && std::abs(croppedClip->sourceOffsetSeconds - 0.2)
                                   < 0.001
                            && std::abs(croppedClip->durationSeconds - 0.6)
                                   < 0.001
                            && session.undoArrangementEdit()
                            && std::abs(session.audioClip(0, rangeSourceId)
                                            ->startSeconds
                                        - 5.0) < 0.001,
                        "crop a region non-destructively to a time range")
                 && passed;

        session.clearArrangementEditHistory();
        const auto silencedRange = session.editAudioClipRange(
            0, { rangeSourceId }, 5.25, 5.75,
            cydaw::app::AudioTrackController::ArrangementRangeOperation::
                silenceRange);
        const auto silencedClips = session.audioClips(0);
        const auto leftRangePart = session.audioClip(0, rangeSourceId);
        const auto rightRangePart = std::find_if(
            silencedClips.begin(), silencedClips.end(),
            [rangeSourceId](const auto& clip) {
                return clip.id != rangeSourceId
                       && std::abs(clip.startSeconds - 5.75) < 0.001;
            });
        passed = expect(silencedRange.succeeded && silencedRange.changed
                            && silencedRange.affectedClipIds.empty()
                            && silencedClips.size() == rangeSourceCount + 1
                            && leftRangePart.has_value()
                            && std::abs(leftRangePart->durationSeconds - 0.25)
                                   < 0.001
                            && rightRangePart != silencedClips.end()
                            && std::abs(rightRangePart->sourceOffsetSeconds
                                        - 0.75) < 0.001
                            && std::abs(rightRangePart->durationSeconds - 0.25)
                                   < 0.001
                            && session.undoArrangementEdit()
                            && session.audioClips(0).size()
                                   == rangeSourceCount
                            && session.redoArrangementEdit()
                            && session.audioClips(0).size()
                                   == rangeSourceCount + 1
                            && session.undoArrangementEdit(),
                        "silence only the selected timeline range") && passed;
        passed = expect(session.removeAudioClip(0, rangeSourceId),
                        "clean range editing test region") && passed;

        const auto firstRangeBatchId = session.addAudioClip(
            0, 4.0, 0.0, 1.0, "Range batch A");
        const auto secondRangeBatchId = session.addAudioClip(
            1, 4.0, 0.0, 1.0, "Range batch B");
        const auto firstRangeBatchCount = session.audioClips(0).size();
        const auto secondRangeBatchCount = session.audioClips(1).size();
        session.clearArrangementEditHistory();
        session.beginArrangementEditBatch();
        const auto firstSilenceBatch = session.editAudioClipRange(
            0, { firstRangeBatchId }, 4.25, 4.75,
            cydaw::app::AudioTrackController::ArrangementRangeOperation::
                silenceRange);
        const auto secondSilenceBatch = session.editAudioClipRange(
            1, { secondRangeBatchId }, 4.25, 4.75,
            cydaw::app::AudioTrackController::ArrangementRangeOperation::
                silenceRange);
        const auto committedRangeBatch = session.endArrangementEditBatch();
        passed = expect(firstSilenceBatch.succeeded
                            && firstSilenceBatch.changed
                            && secondSilenceBatch.succeeded
                            && secondSilenceBatch.changed
                            && committedRangeBatch
                            && session.audioClips(0).size()
                                   == firstRangeBatchCount + 1
                            && session.audioClips(1).size()
                                   == secondRangeBatchCount + 1
                            && session.undoArrangementEdit()
                            && session.audioClips(0).size()
                                   == firstRangeBatchCount
                            && session.audioClips(1).size()
                                   == secondRangeBatchCount
                            && session.redoArrangementEdit()
                            && session.audioClips(0).size()
                                   == firstRangeBatchCount + 1
                            && session.audioClips(1).size()
                                   == secondRangeBatchCount + 1
                            && session.undoArrangementEdit(),
                        "silence a multi-track range with atomic undo and redo")
                 && passed;
        passed = expect(session.removeAudioClip(0, firstRangeBatchId)
                            && session.removeAudioClip(1, secondRangeBatchId),
                        "clean multi-track range editing regions") && passed;
        session.clearArrangementEditHistory();

        const auto verticalMoveId = session.addAudioClip(
            0, 3.4, 0.1, 0.5, "Vertical move source");
        const auto verticalSourceCount = session.audioClips(0).size();
        const auto verticalDestinationCount = session.audioClips(1).size();
        session.clearArrangementEditHistory();
        std::vector<std::vector<std::uint64_t>> verticalSelection(
            session.trackCount());
        verticalSelection[0] = { verticalMoveId };
        const auto verticalMove = session.moveAudioClipsAcrossTracks(
            verticalSelection, 1, 0.2);
        const auto movedVerticalId = verticalMove.clipIdsByTrack.size() > 1
                                         && !verticalMove.clipIdsByTrack[1].empty()
                                     ? verticalMove.clipIdsByTrack[1].front()
                                     : 0;
        const auto movedVerticalClip = session.audioClip(1, movedVerticalId);
        passed = expect(verticalMoveId != 0
                            && verticalMove.succeeded && verticalMove.changed
                            && movedVerticalId != 0
                            && !session.audioClip(0, verticalMoveId).has_value()
                            && session.audioClips(0).size()
                                   == verticalSourceCount - 1
                            && session.audioClips(1).size()
                                   == verticalDestinationCount + 1
                            && movedVerticalClip.has_value()
                            && std::abs(movedVerticalClip->startSeconds - 3.6)
                                   < 0.001
                            && std::abs(movedVerticalClip->sourceOffsetSeconds
                                        - 0.1) < 0.001
                            && session.canUndoArrangementEdit()
                            && session.undoArrangementEdit()
                            && session.audioClip(0, verticalMoveId).has_value()
                            && session.audioClips(0).size()
                                   == verticalSourceCount
                            && session.audioClips(1).size()
                                   == verticalDestinationCount
                            && session.redoArrangementEdit()
                            && !session.audioClip(0, verticalMoveId).has_value()
                            && session.audioClip(1, movedVerticalId).has_value()
                            && session.undoArrangementEdit(),
                        "move an arrangement region vertically with atomic undo")
                 && passed;
        passed = expect(session.removeAudioClip(0, verticalMoveId),
                        "clean vertical move test region") && passed;
        session.clearArrangementEditHistory();
        const auto contentEnd = session.length();
        session.setLoopRange(0.125, contentEnd + 0.5);
        passed = expect(std::abs(session.loopStart() - 0.125) < 0.000001
                            && std::abs(session.loopEnd()
                                        - (contentEnd + 0.5)) < 0.000001
                            && std::abs(session.length()
                                        - (contentEnd + 0.5)) < 0.000001,
                        "explicit loop range extends the shared session timeline") && passed;
        session.setLooping(true);
        session.setLooping(false);
        session.resetLoopRange();
        passed = expect(std::abs(session.loopStart()) < 0.000001
                            && std::abs(session.loopEnd()
                                        - session.length()) < 0.000001
                            && std::abs(session.length() - contentEnd) < 0.000001,
                        "reset loop range follows the arrangement length") && passed;
        float normalisedGainDb {};
        passed = expect(session.normaliseTrackRange(0, 0.0, 1.0, -6.0f,
                                                     &normalisedGainDb).wasOk()
                            && normalisedGainDb > 7.5f
                            && normalisedGainDb < 8.5f
                            && std::abs(session.trackInfo(0).gainDb - normalisedGainDb) < 0.001f,
                        "non-destructive peak normalisation") && passed;
        passed = expect(session.normaliseTrackRange(0, 0.0, 0.0).failed(),
                        "reject empty normalisation range") && passed;
        float backgroundGainDb {};
        std::atomic<bool> cancelledAnalysis { false };
        passed = expect(cydaw::app::AudioTrackController::analyseAudioRangeGain(
                            firstFile, 0.0, 1.0, -6.0f,
                            backgroundGainDb, &cancelledAnalysis).wasOk()
                            && std::abs(backgroundGainDb - normalisedGainDb) < 0.001f,
                        "background-safe normalisation analysis") && passed;
        cancelledAnalysis.store(true, std::memory_order_release);
        passed = expect(cydaw::app::AudioTrackController::analyseAudioRangeGain(
                            firstFile, 0.0, 1.0, -6.0f,
                            backgroundGainDb, &cancelledAnalysis).failed(),
                        "cancel background normalisation analysis") && passed;
        const auto exportedSelection = tempRoot.getChildFile("selection.wav");
        passed = expect(session.exportTrackRange(0, 0.25, 0.25,
                                                  exportedSelection).wasOk(),
                        "export audio selection") && passed;
        auto exportedReader = std::unique_ptr<juce::AudioFormatReader>(
            session.formatManager().createReaderFor(exportedSelection));
        passed = expect(exportedReader != nullptr
                            && exportedReader->numChannels == 2
                            && std::abs(static_cast<double>(exportedReader->lengthInSamples)
                                            / exportedReader->sampleRate
                                        - 0.25) < 0.001,
                        "exported selection format and duration") && passed;
        passed = expect(session.exportTrackRange(0, 0.0, 0.25, firstFile).failed(),
                        "protect source audio from export overwrite") && passed;
        std::atomic<bool> cancelledExport { true };
        const auto cancelledExportFile = tempRoot.getChildFile(
            "cancelled-selection.wav");
        passed = expect(cydaw::app::AudioTrackController::exportAudioRange(
                            firstFile, 0.0, 0.25, cancelledExportFile,
                            &cancelledExport).failed()
                            && !cancelledExportFile.existsAsFile(),
                        "cancel background export and remove partial file") && passed;

        using AudioEditOperation = cydaw::app::AudioEditOperation;
        const auto fadeInFile = tempRoot.getChildFile("fade-in.wav");
        const auto fadeOutFile = tempRoot.getChildFile("fade-out.wav");
        const auto silenceFile = tempRoot.getChildFile("silence.wav");
        const auto reverseFile = tempRoot.getChildFile("reverse.wav");
        const auto amplifyFile = tempRoot.getChildFile("amplify.wav");
        const auto normaliseFile = tempRoot.getChildFile("normalise.wav");
        const auto removeDcFile = tempRoot.getChildFile("remove-dc.wav");
        const auto invertFile = tempRoot.getChildFile("invert.wav");
        const auto swapStereoFile = tempRoot.getChildFile("swap-stereo.wav");
        const auto invalidSwapFile = tempRoot.getChildFile("invalid-swap.wav");
        passed = expect(cydaw::app::AudioTrackController::renderAudioEdit(
                            firstFile, 0.0, 0.1, AudioEditOperation::fadeIn,
                            fadeInFile).wasOk(),
                        "render fade-in selection") && passed;
        passed = expect(cydaw::app::AudioTrackController::renderAudioEdit(
                            firstFile, 0.0, 0.1, AudioEditOperation::fadeOut,
                            fadeOutFile).wasOk(),
                        "render fade-out selection") && passed;
        passed = expect(cydaw::app::AudioTrackController::renderAudioEdit(
                            firstFile, 0.1, 0.1, AudioEditOperation::silence,
                            silenceFile).wasOk(),
                        "render silence selection") && passed;
        passed = expect(cydaw::app::AudioTrackController::renderAudioEdit(
                            firstFile, 0.0, 0.01, AudioEditOperation::reverse,
                            reverseFile).wasOk(),
                        "render reversed selection") && passed;
        passed = expect(cydaw::app::AudioTrackController::renderAudioEdit(
                            firstFile, 0.0, 0.1,
                            { AudioEditOperation::amplify, 6.0206f },
                            amplifyFile).wasOk(),
                        "render parameterised selection gain") && passed;
        passed = expect(cydaw::app::AudioTrackController::renderAudioEdit(
                            firstFile, 0.0, 0.1,
                            { AudioEditOperation::normalisePeak, -6.0f },
                            normaliseFile).wasOk(),
                        "render peak normalisation into a new source") && passed;
        passed = expect(cydaw::app::AudioTrackController::renderAudioEdit(
                            editFixtureFile, 0.0, 0.1,
                            { AudioEditOperation::removeDcOffset, 0.0f },
                            removeDcFile).wasOk(),
                        "render per-channel DC offset removal") && passed;
        passed = expect(cydaw::app::AudioTrackController::renderAudioEdit(
                            editFixtureFile, 0.0, 0.1,
                            { AudioEditOperation::invertPolarity, 0.0f },
                            invertFile).wasOk(),
                        "render polarity inversion") && passed;
        passed = expect(cydaw::app::AudioTrackController::renderAudioEdit(
                            editFixtureFile, 0.0, 0.1,
                            { AudioEditOperation::swapStereo, 0.0f },
                            swapStereoFile).wasOk(),
                        "render stereo channel swap") && passed;
        passed = expect(cydaw::app::AudioTrackController::renderAudioEdit(
                            monoEditFixtureFile, 0.0, 0.1,
                            { AudioEditOperation::swapStereo, 0.0f },
                            invalidSwapFile).failed()
                            && !invalidSwapFile.existsAsFile(),
                        "reject channel swap for a mono source") && passed;

        juce::AudioBuffer<float> originalStart;
        juce::AudioBuffer<float> fadeInStart;
        juce::AudioBuffer<float> fadeOutStart;
        juce::AudioBuffer<float> silenceMiddle;
        juce::AudioBuffer<float> originalMiddle;
        juce::AudioBuffer<float> reverseStart;
        juce::AudioBuffer<float> amplifiedStart;
        juce::AudioBuffer<float> normalisedStart;
        juce::AudioBuffer<float> editOriginalStart;
        juce::AudioBuffer<float> dcRemovedStart;
        juce::AudioBuffer<float> invertedStart;
        juce::AudioBuffer<float> swappedStart;
        passed = expect(readAudioBlock(firstFile, 0, 4'800, originalStart)
                            && readAudioBlock(fadeInFile, 0, 4'800, fadeInStart)
                            && readAudioBlock(fadeOutFile, 0, 4'800, fadeOutStart),
                        "read rendered fade fixtures") && passed;
        const auto fadePosition = 123;
        const auto fadeAmount = static_cast<float>(fadePosition) / 4'799.0f;
        passed = expect(std::abs(fadeInStart.getSample(0, fadePosition)
                                     - originalStart.getSample(0, fadePosition)
                                           * fadeAmount) < 0.00002f
                            && std::abs(fadeOutStart.getSample(0, fadePosition)
                                            - originalStart.getSample(0, fadePosition)
                                                  * (1.0f - fadeAmount)) < 0.00002f
                            && std::abs(fadeOutStart.getSample(0, 4'799)) < 0.00002f,
                        "sample-accurate linear fade curves") && passed;
        passed = expect(readAudioBlock(firstFile, 4'700, 400, originalMiddle)
                            && readAudioBlock(silenceFile, 4'700, 400,
                                              silenceMiddle)
                            && std::abs(silenceMiddle.getSample(0, 50)
                                        - originalMiddle.getSample(0, 50)) < 0.00002f
                            && std::abs(silenceMiddle.getSample(0, 150)) < 0.00002f,
                        "silence affects only the selected samples") && passed;
        passed = expect(readAudioBlock(reverseFile, 0, 480, reverseStart)
                            && std::abs(reverseStart.getSample(0, 0)
                                        - originalStart.getSample(0, 479)) < 0.00002f
                            && std::abs(reverseStart.getSample(0, 100)
                                        - originalStart.getSample(0, 379)) < 0.00002f,
                        "reverse maps selection samples in exact reverse order") && passed;
        passed = expect(readAudioBlock(amplifyFile, 0, 4'900, amplifiedStart)
                            && readAudioBlock(normaliseFile, 0, 4'900,
                                              normalisedStart),
                        "read gain and normalisation renders") && passed;
        const auto originalPeak = originalStart.getMagnitude(0, 0, 4'800);
        const auto normalisedPeak = normalisedStart.getMagnitude(0, 0, 4'800);
        passed = expect(std::abs(amplifiedStart.getSample(0, 1'234)
                                     - originalStart.getSample(0, 1'234) * 2.0f)
                                < 0.00003f
                            && std::abs(normalisedPeak
                                        - juce::Decibels::decibelsToGain(-6.0f))
                                   < 0.00005f
                            && originalPeak > 0.19f
                            && std::abs(amplifiedStart.getSample(0, 4'850)
                                        - normalisedStart.getSample(0, 4'850))
                                   < 0.00003f,
                        "apply sample gain, exact target peak, and preserve samples outside selection") && passed;
        passed = expect(readAudioBlock(editFixtureFile, 0, 4'900,
                                       editOriginalStart)
                            && readAudioBlock(removeDcFile, 0, 4'900,
                                              dcRemovedStart)
                            && readAudioBlock(invertFile, 0, 4'900,
                                              invertedStart)
                            && readAudioBlock(swapStereoFile, 0, 4'900,
                                              swappedStart),
                        "read detailed sample-editor renders") && passed;
        auto dcLeft = 0.0;
        auto dcRight = 0.0;
        for (int sample = 0; sample < 4'800; ++sample) {
            dcLeft += dcRemovedStart.getSample(0, sample);
            dcRight += dcRemovedStart.getSample(1, sample);
        }
        dcLeft /= 4'800.0;
        dcRight /= 4'800.0;
        passed = expect(std::abs(dcLeft) < 0.000002
                            && std::abs(dcRight) < 0.000002
                            && std::abs(invertedStart.getSample(0, 731)
                                        + editOriginalStart.getSample(0, 731))
                                   < 0.00003f
                            && std::abs(swappedStart.getSample(0, 731)
                                        - editOriginalStart.getSample(1, 731))
                                   < 0.00003f
                            && std::abs(swappedStart.getSample(1, 731)
                                        - editOriginalStart.getSample(0, 731))
                                   < 0.00003f
                            && std::abs(dcRemovedStart.getSample(0, 4'850)
                                        - editOriginalStart.getSample(0, 4'850))
                                   < 0.00003f,
                        "remove DC per channel, invert polarity, swap stereo, and preserve unselected audio") && passed;

        passed = expect(session.commitAudioClipEdit(
                                0, splitClipId, reverseFile).wasOk()
                            && session.audioClip(0, splitClipId)->sourceFile
                                   == reverseFile
                            && session.audioClip(0, initialAudioClips.front().id)
                                   ->sourceFile == firstFile
                            && session.trackInfo(0).file == firstFile
                            && session.canUndoTrackAudioEdit(0)
                            && !session.canUndoArrangementEdit(0)
                            && !session.canRedoArrangementEdit(0),
                        "commit rendered edit to only one arrangement region") && passed;
        passed = expect(session.undoTrackAudioEdit(0)
                            && session.audioClip(0, splitClipId)->sourceFile
                                   == firstFile
                            && session.canRedoTrackAudioEdit(0),
                        "undo one region source without changing its neighbours") && passed;
        passed = expect(session.redoTrackAudioEdit(0)
                            && session.audioClip(0, splitClipId)->sourceFile
                                   == reverseFile
                            && session.audioClip(0, initialAudioClips.front().id)
                                   ->sourceFile == firstFile,
                        "redo one region source without changing its neighbours") && passed;
        passed = expect(session.undoTrackAudioEdit(0),
                        "restore region source before whole-track edit") && passed;

        std::atomic<bool> cancelledEdit { true };
        const auto cancelledFile = tempRoot.getChildFile("cancelled.wav");
        passed = expect(cydaw::app::AudioTrackController::renderAudioEdit(
                            firstFile, 0.0, 0.25, AudioEditOperation::silence,
                            cancelledFile, &cancelledEdit).failed()
                            && !cancelledFile.existsAsFile(),
                        "cancel rendered audio edit and remove partial output") && passed;

        passed = expect(session.commitTrackAudioEdit(0, reverseFile).wasOk()
                            && session.trackInfo(0).file == reverseFile
                            && session.canUndoTrackAudioEdit(0)
                            && !session.canRedoTrackAudioEdit(0),
                        "commit rendered edit without overwriting source") && passed;
        passed = expect(session.undoTrackAudioEdit(0)
                            && session.trackInfo(0).file == firstFile
                            && session.canRedoTrackAudioEdit(0),
                        "undo rendered audio source") && passed;
        passed = expect(session.redoTrackAudioEdit(0)
                            && session.trackInfo(0).file == reverseFile
                            && session.canUndoTrackAudioEdit(0),
                        "redo rendered audio source") && passed;

        {
            cydaw::app::AudioTrackController emptyMidiSession;
            const auto emptyTracks = emptyMidiSession.midiTracks();
            const auto emptyTrackId = emptyTracks.front().id;
            const auto emptyClipId = emptyTracks.front().clips.front().id;
            passed = expect(
                         emptyMidiSession.length() == 0.0
                             && emptyMidiSession.startMidiRecording(
                                    emptyTrackId, emptyClipId, true,
                                    cydaw::app::AudioTrackController::
                                        MidiRecordQuantization::none).wasOk()
                             && std::abs(emptyMidiSession.length() - 8.0)
                                    < 0.000001,
                         "empty MIDI clip creates a finite recording transport")
                     && passed;
            emptyMidiSession.play();
            passed = expect(emptyMidiSession.isPlaying(),
                            "record MIDI into a project with no existing notes or audio")
                     && passed;
            emptyMidiSession.stopMidiRecording(false);
            emptyMidiSession.stop();
        }

        {
            const auto expressionResult = cydaw::app::AudioTrackController::selfTestNoteExpression();
            const auto expressionMessage = ("per-note expressions move their note's pitch and gain alone "
                                            + expressionResult.getErrorMessage()).toStdString();
            passed = expect(expressionResult.wasOk(), expressionMessage.c_str()) && passed;
        }

        {
            const auto legatoResult = cydaw::app::AudioTrackController::selfTestLegatoVoicing();
            const auto legatoMessage = ("mono and legato play one note at a time, sliding "
                                        + legatoResult.getErrorMessage()).toStdString();
            passed = expect(legatoResult.wasOk(), legatoMessage.c_str()) && passed;
        }

        {
            const auto polyResult = cydaw::app::AudioTrackController::selfTestPolySynthGraph();
            const auto polyMessage = ("polyphonic node graph renders waveform, chord voices and ADSR release "
                                      + polyResult.getErrorMessage()).toStdString();
            passed = expect(polyResult.wasOk(), polyMessage.c_str()) && passed;
        }

        session.keyboardState().noteOn(1, 60, 0.8f);
        session.keyboardState().noteOff(1, 60, 0.0f);
        const auto firstMidiNote = session.addMidiNote(60, 0.0, 4.0, 0.8f);
        const auto secondMidiNote = session.addMidiNote(64, 4.0, 1.0, 0.7f);
        passed = expect(session.midiNotes().size() == 2, "MIDI note creation") && passed;
        passed = expect(std::abs(session.midiLengthSeconds() - 2.5) < 0.001,
                        "MIDI sequence length at 120 BPM") && passed;
        session.setTempo(60.0);
        passed = expect(std::abs(session.midiLengthSeconds() - 5.0) < 0.001,
                        "tempo updates MIDI timing") && passed;
        passed = expect(session.updateMidiNote(firstMidiNote, 62, 0.5, 2.0, 0.6f),
                        "MIDI note update") && passed;
        passed = expect(session.removeMidiNote(secondMidiNote), "MIDI note deletion") && passed;

        const auto originalMidiTrack = session.midiTracks().front();
        const auto bassTrackId = session.addMidiTrack("Bass Instrument");
        const auto bassClipId = session.addMidiClip(
            bassTrackId, "Bass Verse", 8.0, 4.0, true, 0xff8e9dff);
        const auto bassNoteId = session.addMidiNote(
            bassTrackId, bassClipId, 36, 0.0, 2.0, 0.9f);
        const auto independentTracks = session.midiTracks();
        passed = expect(independentTracks.size() == 2
                            && independentTracks.front().id == originalMidiTrack.id
                            && independentTracks.back().id == bassTrackId
                            && session.midiNotes(originalMidiTrack.id,
                                                 originalMidiTrack.clips.front().id).size() == 1
                            && session.midiNotes(bassTrackId, bassClipId).size() == 1
                            && bassNoteId != 0,
                        "independent MIDI tracks and clips") && passed;
        cydaw::engine::PolySynthGraphPlan bassInstrument;
        bassInstrument.enabled = true;
        bassInstrument.waveform = cydaw::engine::PolyOscillatorWaveform::square;
        bassInstrument.attackSeconds = 0.025f;
        bassInstrument.sustainLevel = 0.55f;
        bassInstrument.filterCutoffHz = 1'400.0f;
        passed = expect(session.setMidiTrackInstrumentGraph(
                                bassTrackId, bassInstrument).wasOk()
                            && session.midiTrackInstrumentGraph(bassTrackId)
                                   == bassInstrument
                            && session.midiTrackInstrumentGraph(
                                   originalMidiTrack.id).waveform
                                   == cydaw::engine::PolyOscillatorWaveform::sine,
                        "polyphonic instrument graph is compiled independently per MIDI track")
                 && passed;
        passed = expect(std::abs(session.midiLengthSeconds() - 10.0) < 0.001,
                        "MIDI clip arrangement offset contributes to transport length") && passed;
        session.setMidiTrackMixer(bassTrackId, -6.0f, 0.25f, true, true);
        passed = expect(session.setLiveMidiTrack(bassTrackId)
                            && session.liveMidiTrackId() == bassTrackId,
                        "MIDI controller routes to selected instrument track") && passed;
        const auto routedTracks = session.midiTracks();
        passed = expect(!routedTracks.front().armed && routedTracks.back().armed
                            && routedTracks.back().muted && routedTracks.back().solo
                            && std::abs(routedTracks.back().gainDb + 6.0f) < 0.001f
                            && std::abs(routedTracks.back().pan - 0.25f) < 0.001f,
                        "per-instrument mixer and arm state") && passed;
        passed = expect(session.removeMidiTrack(bassTrackId)
                            && session.midiTracks().size() == 1
                            && session.liveMidiTrackId() == originalMidiTrack.id
                            && std::abs(session.midiLengthSeconds() - 2.5) < 0.001,
                        "remove MIDI track and restore live routing") && passed;

        const auto originalMidiClipId = originalMidiTrack.clips.front().id;
        const auto originalMidiLength
            = originalMidiTrack.clips.front().lengthBeats;
        const auto resizedMidiLength = originalMidiLength + 2.0;
        passed = expect(session.setMidiClipTimelineBounds(
                            originalMidiTrack.id, originalMidiClipId,
                            resizedMidiLength, 1.0, 6.0, 2.0, 5.0,
                            true),
                        "MIDI clip duration, play and loop bounds") && passed;
        const auto boundedMidiClip = session.midiTracks().front().clips.front();
        passed = expect(std::abs(boundedMidiClip.lengthBeats
                                 - resizedMidiLength) < 0.001
                            && std::abs(boundedMidiClip.playStartBeats - 1.0) < 0.001
                            && std::abs(boundedMidiClip.playEndBeats - 6.0) < 0.001
                            && std::abs(boundedMidiClip.loopStartBeats - 2.0) < 0.001
                            && std::abs(boundedMidiClip.loopEndBeats - 5.0) < 0.001
                            && boundedMidiClip.looping,
                        "MIDI clip bounds are published to the editor model") && passed;
        (void) session.setMidiClipTimelineBounds(
            originalMidiTrack.id, originalMidiClipId,
            originalMidiLength, 0.0, originalMidiLength,
            0.0, originalMidiLength, false);
        session.clearMidiEditHistory();
        const auto editNoteId = session.addMidiNote(
            originalMidiTrack.id, originalMidiClipId,
            67, 3.0, 0.5, 0.75f);
        passed = expect(editNoteId != 0 && session.canUndoMidiEdit()
                            && !session.canRedoMidiEdit()
                            && session.midiNotes(originalMidiTrack.id,
                                                 originalMidiClipId).size() == 2,
                        "piano-roll note edit enters MIDI history") && passed;
        passed = expect(session.undoMidiEdit()
                            && session.midiNotes(originalMidiTrack.id,
                                                 originalMidiClipId).size() == 1
                            && session.canRedoMidiEdit(),
                        "undo piano-roll note edit") && passed;
        passed = expect(session.redoMidiEdit()
                            && session.midiNotes(originalMidiTrack.id,
                                                 originalMidiClipId).size() == 2,
                        "redo piano-roll note edit") && passed;

        session.clearMidiEditHistory();
        passed = expect(session.beginMidiEdit(originalMidiTrack.id,
                                               originalMidiClipId)
                            && session.updateMidiNote(
                                originalMidiTrack.id, originalMidiClipId,
                                editNoteId, 68, 3.25, 0.75, 0.7f)
                            && session.updateMidiNote(
                                originalMidiTrack.id, originalMidiClipId,
                                editNoteId, 69, 3.5, 1.0, 0.65f)
                            && session.endMidiEdit()
                            && session.canUndoMidiEdit(),
                        "multi-step piano-roll drag creates one history gesture") && passed;
        passed = expect(session.undoMidiEdit(),
                        "undo complete piano-roll drag gesture") && passed;
        auto draggedNotes = session.midiNotes(originalMidiTrack.id,
                                              originalMidiClipId);
        auto draggedNote = std::find_if(
            draggedNotes.begin(), draggedNotes.end(),
            [editNoteId](const auto& note) { return note.id == editNoteId; });
        passed = expect(draggedNote != draggedNotes.end()
                            && draggedNote->noteNumber == 67
                            && std::abs(draggedNote->startBeats - 3.0) < 0.000001
                            && session.redoMidiEdit(),
                        "drag undo restores initial note and redo reapplies final state") && passed;
        draggedNotes = session.midiNotes(originalMidiTrack.id,
                                         originalMidiClipId);
        draggedNote = std::find_if(
            draggedNotes.begin(), draggedNotes.end(),
            [editNoteId](const auto& note) { return note.id == editNoteId; });
        passed = expect(draggedNote != draggedNotes.end()
                            && draggedNote->noteNumber == 69
                            && std::abs(draggedNote->startBeats - 3.5) < 0.000001,
                        "redo restores final dragged MIDI note") && passed;

        const auto notesBeforeReplacement = draggedNotes;
        auto replacementNotes = notesBeforeReplacement;
        replacementNotes.erase(replacementNotes.begin());
        cydaw::app::AudioTrackController::MidiNote insertedNote;
        insertedNote.noteNumber = 140;
        insertedNote.startBeats = 19.75;
        insertedNote.durationBeats = 2.0;
        insertedNote.velocity = 2.0f;
        replacementNotes.push_back(insertedNote);
        session.clearMidiEditHistory();
        passed = expect(session.replaceMidiNotes(
                                originalMidiTrack.id, originalMidiClipId,
                                replacementNotes)
                            && session.canUndoMidiEdit(),
                        "replace complete MIDI note set as one history edit")
                 && passed;
        const auto replacedNotes = session.midiNotes(
            originalMidiTrack.id, originalMidiClipId);
        const auto clampedInserted = std::find_if(
            replacedNotes.begin(), replacedNotes.end(),
            [](const auto& note) { return note.noteNumber == 127; });
        passed = expect(replacedNotes.size() == 2
                            && clampedInserted != replacedNotes.end()
                            && clampedInserted->id != 0
                            && std::abs(clampedInserted->velocity - 1.0f)
                                   < 0.000001f,
                        "assign new MIDI IDs and clamp replacement values")
                 && passed;
        passed = expect(session.undoMidiEdit()
                            && session.midiNotes(originalMidiTrack.id,
                                                 originalMidiClipId).size()
                                   == notesBeforeReplacement.size()
                            && session.redoMidiEdit(),
                        "undo and redo whole-note-set replacement")
                 && passed;
        const auto notesAfterReplacement = session.midiNotes(
            originalMidiTrack.id, originalMidiClipId);
        session.clearMidiEditHistory();
        passed = expect(!session.replaceMidiNotes(
                                originalMidiTrack.id, originalMidiClipId,
                                notesAfterReplacement)
                            && !session.canUndoMidiEdit(),
                        "ignore identical MIDI note-set replacement")
                 && passed;

        {
            cydaw::app::AudioTrackController editorSession;
            const auto editorTrack = editorSession.midiTracks().front();
            const auto editorTrackId = editorTrack.id;
            const auto editorClipId = editorTrack.clips.front().id;
            (void) editorSession.addMidiNote(editorTrackId, editorClipId,
                                             60, 0.0, 1.0, 0.50f);
            (void) editorSession.addMidiNote(editorTrackId, editorClipId,
                                             64, 1.0, 0.5, 0.70f);
            editorSession.clearMidiEditHistory();
            cydaw::app::PianoRollComponent editor;
            editor.setSize(900, 560);
            editor.setNotes(editorSession.midiNotes(editorTrackId,
                                                    editorClipId));
            editor.setNotesEditHandler(
                [&editorSession, &editor, editorTrackId, editorClipId](
                    std::vector<cydaw::app::AudioTrackController::MidiNote>
                        notes) {
                    if (editorSession.replaceMidiNotes(
                            editorTrackId, editorClipId, std::move(notes)))
                        editor.setNotes(editorSession.midiNotes(
                            editorTrackId, editorClipId));
                });
            editor.selectAllNotes();
            const auto initialEditorNotes = editorSession.midiNotes(
                editorTrackId, editorClipId);
            passed = expect(editor.selectedNoteCount() == 2
                                && editor.keyPressed(juce::KeyPress(
                                    juce::KeyPress::rightKey)),
                            "select and nudge several piano-roll notes")
                     && passed;
            auto editedUiNotes = editorSession.midiNotes(editorTrackId,
                                                         editorClipId);
            passed = expect(
                         std::abs(editedUiNotes.front().startBeats
                                      - initialEditorNotes.front().startBeats
                                      - 0.25)
                             < 0.000001
                             && editor.keyPressed(juce::KeyPress(
                                 juce::KeyPress::rightKey,
                                 juce::ModifierKeys::shiftModifier, 0)),
                         "move group on grid and resize it from keyboard")
                     && passed;
            editedUiNotes = editorSession.midiNotes(editorTrackId,
                                                    editorClipId);
            passed = expect(
                         std::abs(editedUiNotes.front().durationBeats
                                      - initialEditorNotes.front().durationBeats
                                      - 0.25)
                             < 0.000001
                             && editor.keyPressed(juce::KeyPress(
                                 juce::KeyPress::upKey,
                                 juce::ModifierKeys::altModifier, 0)),
                         "resize selected notes and edit their velocity")
                     && passed;
            editedUiNotes = editorSession.midiNotes(editorTrackId,
                                                    editorClipId);
            passed = expect(
                         std::abs(editedUiNotes.front().velocity - 0.55f)
                                 < 0.000001f
                             && editor.copySelectedNotes()
                             && editor.deleteSelectedNotes()
                             && editorSession.midiNotes(editorTrackId,
                                                        editorClipId).empty()
                             && editorSession.undoMidiEdit(),
                         "copy, delete and undo a piano-roll multi-selection")
                     && passed;
            editor.setNotes(editorSession.midiNotes(editorTrackId,
                                                    editorClipId));
            passed = expect(editor.pasteNotes()
                                && editor.selectedNoteCount() == 2
                                && editorSession.midiNotes(editorTrackId,
                                                           editorClipId).size()
                                       == 4
                                && editor.duplicateSelectedNotes()
                                && editorSession.midiNotes(editorTrackId,
                                                           editorClipId).size()
                                       == 6,
                            "paste and duplicate notes with fresh persistent IDs")
                     && passed;
        }

        {
            const auto midiFixture = tempRoot.getChildFile(
                "multitrack-import.mid");
            const auto exportedClip = tempRoot.getChildFile(
                "exported-clip.mid");
            passed = expect(writeMidiFixture(midiFixture),
                            "write multi-track Standard MIDI File fixture")
                     && passed;
            cydaw::app::AudioTrackController importedSession;
            cydaw::app::AudioTrackController::MidiFileImportSummary summary;
            const auto importResult = importedSession.importMidiFile(
                midiFixture, summary, true);
            const auto importedTracks = importedSession.midiTracks();
            passed = expect(importResult.wasOk()
                                && summary.trackIds.size() == 3
                                && summary.noteCount == 3
                                && importedTracks.size() == 4
                                && std::abs(summary.tempoBpm - 100.0)
                                       < 0.000001
                                && std::abs(importedSession.tempo() - 100.0)
                                       < 0.000001,
                            "import SMF tempo and split source MIDI channels into tracks")
                     && passed;
            const auto firstImported = std::find_if(
                importedTracks.begin(), importedTracks.end(),
                [&summary](const auto& track) {
                    return !summary.trackIds.empty()
                           && track.id == summary.trackIds.front();
                });
            const auto secondImported = std::find_if(
                importedTracks.begin(), importedTracks.end(),
                [&summary](const auto& track) {
                    return summary.trackIds.size() > 1
                           && track.id == summary.trackIds[1];
                });
            const auto thirdImported = std::find_if(
                importedTracks.begin(), importedTracks.end(),
                [&summary](const auto& track) {
                    return summary.trackIds.size() > 2
                           && track.id == summary.trackIds[2];
                });
            passed = expect(firstImported != importedTracks.end()
                                && secondImported != importedTracks.end()
                                && thirdImported != importedTracks.end()
                                && firstImported->name == "Keys - Ch 1"
                                && secondImported->name == "Keys - Ch 2"
                                && thirdImported->name == "Bass"
                                && firstImported->clips.size() == 1
                                && secondImported->clips.size() == 1
                                && thirdImported->clips.size() == 1,
                            "preserve MIDI track names and channel identity")
                     && passed;
            const auto firstImportedNotes = firstImported
                                                 != importedTracks.end()
                                             ? firstImported->clips.front().notes
                                             : std::vector<cydaw::app::
                                                   AudioTrackController::MidiNote> {};
            const auto secondImportedNotes = secondImported
                                                  != importedTracks.end()
                                              ? secondImported->clips.front().notes
                                              : std::vector<cydaw::app::
                                                    AudioTrackController::MidiNote> {};
            const auto thirdImportedNotes = thirdImported
                                                 != importedTracks.end()
                                             ? thirdImported->clips.front().notes
                                             : std::vector<cydaw::app::
                                                   AudioTrackController::MidiNote> {};
            passed = expect(firstImportedNotes.size() == 1
                                && secondImportedNotes.size() == 1
                                && thirdImportedNotes.size() == 1
                                && firstImportedNotes.front().noteNumber == 60
                                && std::abs(firstImportedNotes.front().startBeats)
                                       < 0.000001
                                && std::abs(firstImportedNotes.front().durationBeats
                                            - 1.0)
                                       < 0.000001
                                && std::abs(secondImportedNotes.front().startBeats
                                            - 0.5)
                                       < 0.000001
                                && std::abs(thirdImportedNotes.front().startBeats
                                            - 2.0)
                                       < 0.000001,
                            "convert SMF ticks to exact local beat positions")
                     && passed;
            const auto exportResult = firstImported != importedTracks.end()
                                          ? importedSession.exportMidiClipToFile(
                                                firstImported->id,
                                                firstImported->clips.front().id,
                                                exportedClip)
                                          : juce::Result::fail("missing import");
            juce::MidiFile exportedMidi;
            auto exportedInput = exportedClip.createInputStream();
            const auto exportedRead = exportedInput != nullptr
                                      && exportedMidi.readFrom(*exportedInput);
            auto exportedNoteOns = 0;
            auto exportedTempoBpm = 0.0;
            if (exportedRead && exportedMidi.getTrack(0) != nullptr)
                for (int eventIndex = 0;
                     eventIndex < exportedMidi.getTrack(0)->getNumEvents();
                     ++eventIndex) {
                    const auto* event = exportedMidi.getTrack(0)
                                            ->getEventPointer(eventIndex);
                    if (event->message.isNoteOn())
                        ++exportedNoteOns;
                    if (event->message.isTempoMetaEvent())
                        exportedTempoBpm = 60.0
                            / event->message
                                  .getTempoSecondsPerQuarterNote();
                }
            passed = expect(exportResult.wasOk() && exportedRead
                                && exportedMidi.getTimeFormat() == 960
                                && exportedMidi.getNumTracks() == 1
                                && exportedNoteOns == 1
                                && std::abs(exportedTempoBpm - 100.0)
                                       < 0.000001,
                            "export selected clip as tempo-tagged 960 PPQ SMF")
                     && passed;

            cydaw::app::AudioTrackController roundTripSession;
            roundTripSession.setTempo(140.0);
            cydaw::app::AudioTrackController::MidiFileImportSummary
                roundTripSummary;
            passed = expect(roundTripSession.importMidiFile(
                                exportedClip, roundTripSummary, false).wasOk()
                                && roundTripSummary.trackIds.size() == 1
                                && roundTripSummary.noteCount == 1
                                && std::abs(roundTripSummary.tempoBpm - 100.0)
                                       < 0.000001
                                && std::abs(roundTripSession.tempo() - 140.0)
                                       < 0.000001,
                            "round-trip MIDI clip and optionally preserve project tempo")
                     && passed;
            const auto roundTripTracks = roundTripSession.midiTracks();
            const auto roundTripTrack = std::find_if(
                roundTripTracks.begin(), roundTripTracks.end(),
                [&roundTripSummary](const auto& track) {
                    return !roundTripSummary.trackIds.empty()
                           && track.id == roundTripSummary.trackIds.front();
                });
            const auto roundTripNotes = roundTripTrack
                                                != roundTripTracks.end()
                                            ? roundTripTrack->clips.front().notes
                                            : std::vector<cydaw::app::
                                                  AudioTrackController::MidiNote> {};
            passed = expect(roundTripNotes.size() == 1
                                && roundTripNotes.front().noteNumber == 60
                                && std::abs(roundTripNotes.front().startBeats)
                                       < 0.000001
                                && std::abs(roundTripNotes.front().durationBeats
                                            - 1.0)
                                       < 0.000001
                                && std::abs(roundTripNotes.front().velocity
                                            - 0.5f)
                                       < 0.01f,
                            "preserve MIDI note pitch, timing and velocity through export/import")
                     && passed;
            const auto emptyTrack = roundTripSession.midiTracks().front();
            const auto invalidMidi = tempRoot.getChildFile("invalid.mid");
            if (auto invalidOutput = invalidMidi.createOutputStream()) {
                static constexpr char invalidData[] = "not-a-midi-file";
                (void) invalidOutput->write(invalidData,
                                             sizeof(invalidData) - 1);
                invalidOutput->flush();
            }
            const auto tracksBeforeInvalidImport = importedSession.midiTracks()
                                                       .size();
            passed = expect(roundTripSession.exportMidiClipToFile(
                                emptyTrack.id, emptyTrack.clips.front().id,
                                tempRoot.getChildFile("empty.mid")).failed()
                                && importedSession.importMidiFile(
                                       tempRoot.getChildFile("missing.mid"),
                                       summary, true).failed()
                                && importedSession.importMidiFile(
                                       invalidMidi, summary, true).failed()
                                && importedSession.midiTracks().size()
                                       == tracksBeforeInvalidImport,
                            "reject empty export and invalid MIDI import without mutation")
                     && passed;
        }

        using MidiQuantization =
            cydaw::app::AudioTrackController::MidiRecordQuantization;
        session.setTempo(120.0);
        const auto recordingTrackId = session.addMidiTrack("Recorded Instrument");
        const auto recordingClipId = session.addMidiClip(
            recordingTrackId, "Offset Take", 8.0, 4.0, false, 0xffff657a);
        const auto existingRecordingNote = session.addMidiNote(
            recordingTrackId, recordingClipId, 48, 0.0, 0.5, 0.6f);
        session.clearMidiEditHistory();
        passed = expect(!session.enqueueMidiRecordingMessage(
                                juce::MidiMessage::noteOn(1, 67, 0.75f), 4.12)
                            && session.startMidiRecording(
                                   recordingTrackId, recordingClipId, true,
                                   MidiQuantization::sixteenthBeat).wasOk()
                            && session.isMidiRecording()
                            && !session.startMidiRecording(
                                    recordingTrackId, recordingClipId, true,
                                    MidiQuantization::none).wasOk()
                            && !session.setLiveMidiTrack(originalMidiTrack.id)
                            && !session.enqueueMidiRecordingMessage(
                                juce::MidiMessage::controllerEvent(1, 1, 64),
                                4.1),
                        "arm one isolated MIDI take and reject conflicting routing") && passed;
        passed = expect(session.enqueueMidiRecordingMessage(
                                juce::MidiMessage::noteOn(1, 67, 0.75f), 4.12)
                            && session.enqueueMidiRecordingMessage(
                                juce::MidiMessage::noteOff(1, 67), 4.39)
                            && session.pollMidiRecording(),
                        "capture timestamped MIDI note-on and note-off events") && passed;
        session.stopMidiRecording(true);
        auto recordedNotes = session.midiNotes(recordingTrackId,
                                               recordingClipId);
        const auto quantizedNote = std::find_if(
            recordedNotes.begin(), recordedNotes.end(),
            [](const auto& note) { return note.noteNumber == 67; });
        passed = expect(!session.isMidiRecording()
                            && session.midiRecordingNoteCount() == 1
                            && session.droppedMidiRecordingEvents() == 0,
                        "finish MIDI take without dropped input events") && passed;
        passed = expect(recordedNotes.size() == 2
                            && quantizedNote != recordedNotes.end(),
                        "overdub recorded note into offset MIDI clip") && passed;
        if (quantizedNote != recordedNotes.end()) {
            passed = expect(
                         std::abs(quantizedNote->startBeats - 0.25) < 0.000001,
                         "quantize MIDI note start relative to offset clip")
                     && passed;
            passed = expect(
                         std::abs(quantizedNote->durationBeats - 0.5) < 0.000001,
                         "quantize MIDI note duration from note-off")
                     && passed;
            passed = expect(
                         std::abs(quantizedNote->velocity - 0.75f) < 0.01f,
                         "preserve recorded 7-bit MIDI velocity")
                     && passed;
        }
        passed = expect(session.canUndoMidiEdit(),
                        "commit complete MIDI take to history") && passed;
        passed = expect(session.undoMidiEdit()
                            && session.midiNotes(recordingTrackId,
                                                 recordingClipId).size() == 1
                            && session.midiNotes(recordingTrackId,
                                                 recordingClipId).front().id
                                   == existingRecordingNote
                            && session.redoMidiEdit()
                            && session.midiNotes(recordingTrackId,
                                                 recordingClipId).size() == 2,
                        "undo and redo the complete overdub take atomically") && passed;

        session.clearMidiEditHistory();
        passed = expect(session.startMidiRecording(
                                recordingTrackId, recordingClipId, false,
                                MidiQuantization::none).wasOk()
                            && session.enqueueMidiRecordingMessage(
                                juce::MidiMessage::noteOn(2, 72, 0.9f), 4.5)
                            && session.enqueueMidiRecordingMessage(
                                juce::MidiMessage::noteOff(2, 72), 4.75)
                            && session.pollMidiRecording(),
                        "capture replacement take on an independent MIDI channel") && passed;
        session.stopMidiRecording(true);
        recordedNotes = session.midiNotes(recordingTrackId, recordingClipId);
        passed = expect(recordedNotes.size() == 1
                            && recordedNotes.front().noteNumber == 72
                            && std::abs(recordedNotes.front().startBeats - 1.0) < 0.000001
                            && std::abs(recordedNotes.front().durationBeats - 0.5) < 0.000001
                            && session.undoMidiEdit()
                            && session.midiNotes(recordingTrackId,
                                                 recordingClipId).size() == 2
                            && session.redoMidiEdit()
                            && session.midiNotes(recordingTrackId,
                                                 recordingClipId).size() == 1,
                        "replace clip contents and restore them with MIDI history") && passed;

        const auto beforeCancelledTake = session.midiNotes(
            recordingTrackId, recordingClipId);
        session.clearMidiEditHistory();
        passed = expect(session.startMidiRecording(
                                recordingTrackId, recordingClipId, false,
                                MidiQuantization::quarterBeat).wasOk()
                            && session.enqueueMidiRecordingMessage(
                                juce::MidiMessage::noteOn(1, 76, 0.8f), 4.15)
                            && session.pollMidiRecording(),
                        "begin cancellable MIDI replacement take") && passed;
        session.stopMidiRecording(false);
        const auto afterCancelledTake = session.midiNotes(
            recordingTrackId, recordingClipId);
        passed = expect(afterCancelledTake.size() == beforeCancelledTake.size()
                            && !afterCancelledTake.empty()
                            && afterCancelledTake.front().id
                                   == beforeCancelledTake.front().id
                            && !session.canUndoMidiEdit()
                            && !session.enqueueMidiRecordingMessage(
                                juce::MidiMessage::noteOff(1, 76), 4.3),
                        "cancel MIDI take and restore clip without history residue") && passed;
        passed = expect(session.removeMidiTrack(recordingTrackId)
                            && session.liveMidiTrackId() == originalMidiTrack.id,
                        "remove recorded track and restore controller routing") && passed;

        session.setTrackMixer(1, -6.0f, 0.35f, true);
        session.setTrackSolo(0, true);
        session.setTrackClip(0, 0.5, 0.1, 0.4);
        session.setTrackEffects(0, 12.0f, 1'200.0f);
        const auto automationA = session.addAutomationPoint(0, 0.75, -12.0f);
        const auto automationB = session.addAutomationPoint(0, 0.25, 3.0f);
        const auto automation = session.automationPoints(0);
        passed = expect(automationA != 0 && automationB != 0 && automation.size() == 2,
                        "gain automation creation") && passed;
        passed = expect(automation.front().timeSeconds <= automation.back().timeSeconds,
                        "gain automation sorting") && passed;
        passed = expect(session.updateAutomationPoint(0, automationA, 0.5, -6.0f),
                        "gain automation update") && passed;
        passed = expect(session.setAutomationPointCurve(
                            0, automationA,
                            cydaw::app::AudioTrackController::AutomationCurve::bezier,
                            0.4f)
                            && session.automationPoints(0).back().curve
                                   == cydaw::app::AudioTrackController::AutomationCurve::bezier
                            && std::abs(session.automationPoints(0).back().curveAmount
                                        - 0.4f) < 0.001f,
                        "gain automation Bezier curve") && passed;
        passed = expect(session.removeAutomationPoint(0, automationB),
                        "gain automation deletion") && passed;
        const auto midiAutomationA = session.addMidiAutomationPoint(
            originalMidiTrack.id, 1.5, -18.0f);
        const auto midiAutomationB = session.addMidiAutomationPoint(
            originalMidiTrack.id, 0.5, 4.0f);
        const auto midiAutomation = session.midiAutomationPoints(
            originalMidiTrack.id);
        passed = expect(midiAutomationA != 0 && midiAutomationB != 0
                            && midiAutomation.size() == 2
                            && midiAutomation.front().timeSeconds
                                   <= midiAutomation.back().timeSeconds,
                        "MIDI track gain automation creation and sorting")
                 && passed;
        passed = expect(session.updateMidiAutomationPoint(
                            originalMidiTrack.id, midiAutomationA, 1.0,
                            -9.0f),
                        "MIDI track gain automation update")
                 && passed;
        passed = expect(session.setMidiAutomationPointCurve(
                            originalMidiTrack.id, midiAutomationA,
                            cydaw::app::AudioTrackController::AutomationCurve::bezier,
                            -0.35f),
                        "MIDI automation Bezier curve") && passed;
        passed = expect(session.removeMidiAutomationPoint(
                            originalMidiTrack.id, midiAutomationB)
                            && session.midiAutomationPoints(
                                   originalMidiTrack.id).size() == 1,
                        "MIDI track gain automation deletion")
                 && passed;
        const auto midiClipAutomationA
            = session.addMidiClipAutomationPoint(
                originalMidiTrack.id, originalMidiClipId, 1.5, -18.0f);
        const auto midiClipAutomationB
            = session.addMidiClipAutomationPoint(
                originalMidiTrack.id, originalMidiClipId, 0.5, 4.0f);
        auto midiClipAutomation = session.midiClipAutomationPoints(
            originalMidiTrack.id, originalMidiClipId);
        passed = expect(
            midiClipAutomationA != 0 && midiClipAutomationB != 0
                && midiClipAutomation.size() == 2
                && std::abs(midiClipAutomation.front().timeSeconds - 0.5)
                       < 0.000001
                && session.midiAutomationPoints(
                       originalMidiTrack.id).size() == 1
                && session.updateMidiClipAutomationPoint(
                    originalMidiTrack.id, originalMidiClipId,
                    midiClipAutomationA, 1.0, -9.0f)
                && session.setMidiClipAutomationPointCurve(
                    originalMidiTrack.id, originalMidiClipId,
                    midiClipAutomationA,
                    cydaw::app::AudioTrackController::AutomationCurve::bezier,
                    -0.25f)
                && session.removeMidiClipAutomationPoint(
                    originalMidiTrack.id, originalMidiClipId,
                    midiClipAutomationB),
            "MIDI clip automation is sorted and isolated from track automation")
                 && passed;
        using LaunchQuantization = cydaw::app::AudioTrackController::LaunchQuantization;
        passed = expect(std::abs(cydaw::app::AudioTrackController::quantizedLaunchDelaySeconds(
                                     0.75, 120.0, LaunchQuantization::oneBar)
                                 - 1.25) < 0.000001,
                        "one-bar launcher quantization") && passed;
        passed = expect(std::abs(cydaw::app::AudioTrackController::quantizedLaunchDelaySeconds(
                                     0.1, 120.0, LaunchQuantization::quarterBeat)
                                 - 0.025) < 0.000001,
                        "fractional-beat launcher quantization") && passed;
        passed = expect(cydaw::app::AudioTrackController::quantizedLaunchDelaySeconds(
                            1.0, 120.0, LaunchQuantization::oneBeat) == 0.0
                            && cydaw::app::AudioTrackController::quantizedLaunchDelaySeconds(
                                   0.37, 120.0, LaunchQuantization::none) == 0.0,
                        "immediate launch on boundary or disabled quantization") && passed;
        session.setLaunchQuantization(LaunchQuantization::halfBeat);
        passed = expect(session.launchQuantization() == LaunchQuantization::halfBeat,
                        "launcher quantization state") && passed;
        passed = expect(session.setLauncherClip(0, 0, "Verse", 0.1, 0.25,
                                                 true, 0xff4fd3ae,
                                                 secondFile),
                        "launcher clip creation") && passed;
        passed = expect(session.launcherUsesDoubleDeckReadAhead(0),
                        "launcher uses two background read-ahead decks") && passed;
        const auto launcherClips = session.launcherClips(0);
        passed = expect(launcherClips.size() == 1
                            && launcherClips.front().sceneIndex == 0
                            && launcherClips.front().name == "Verse"
                            && launcherClips.front().sourceFile == secondFile
                            && launcherClips.front().looping,
                        "launcher clip state with independent source") && passed;
        passed = expect(
                     session.transferLauncherClip(0, 0, 1, 1, true),
                     "copy launcher clip between tracks") && passed;
        auto transferredClips = session.launcherClips(1);
        auto transferred = std::find_if(
            transferredClips.begin(), transferredClips.end(),
            [](const auto& clip) { return clip.sceneIndex == 1; });
        passed = expect(
                     transferred != transferredClips.end()
                         && transferred->name == "Verse"
                         && transferred->sourceFile == secondFile
                         && transferred->sourceOffsetSeconds == 0.1
                         && transferred->durationSeconds == 0.25
                         && transferred->colour == 0xff4fd3ae
                         && session.launcherClips(0).size() == 1,
                     "preserve launcher clip state when copying") && passed;
        passed = expect(
                     session.transferLauncherClip(1, 1, 1, 2, false),
                     "move launcher clip between scenes") && passed;
        transferredClips = session.launcherClips(1);
        passed = expect(
                     std::none_of(
                         transferredClips.begin(), transferredClips.end(),
                         [](const auto& clip) { return clip.sceneIndex == 1; })
                         && std::any_of(
                             transferredClips.begin(), transferredClips.end(),
                             [](const auto& clip) {
                                 return clip.sceneIndex == 2
                                        && clip.name == "Verse";
                             })
                         && session.clearLauncherClip(1, 2),
                     "remove the source only after a successful launcher move")
                 && passed;
        session.stop();
        session.setPosition(0.1);
        passed = expect(session.launchClip(0, 0)
                            && session.isPlaying()
                            && !session.arrangementPlaybackEnabled()
                            && session.pendingLauncherScene(0) == 0
                            && session.activeLauncherScene(0) == -1,
                        "individual clip starts transport and schedules on BPM grid") && passed;
        session.stopLauncher();
        passed = expect(session.activeLauncherScene(0) == -1
                            && session.pendingLauncherScene(0) == -1,
                        "stop clip launcher") && passed;
        passed = expect(session.setLauncherClip(1, 0, "Bass", 0.0, 0.5,
                                                 true, 0xffff8a00)
                            && session.launchScene(0)
                            && session.pendingLauncherScene(0) == 0
                            && session.pendingLauncherScene(1) == 0,
                        "synchronised scene launch scheduling") && passed;
        passed = expect(session.clearLauncherClip(0, 0)
                            && session.launcherClips(0).empty()
                            && session.activeLauncherScene(0) == -1,
                        "launcher clip deletion") && passed;
        session.stopLauncher();

        passed = expect(session.createEmptyLauncherClip(
                            0, 3, "Timeline clip", 2.0, 0xff53d0c2),
                        "create an empty launcher timeline container")
                 && passed;
        auto timelineClip = session.launcherClip(0, 3);
        std::uint64_t timelineEventId {};
        passed = expect(timelineClip.has_value()
                            && timelineClip->usesEventTimeline
                            && timelineClip->events.empty()
                            && session.addLauncherClipEventFromFile(
                                   0, 3, secondFile, 0.25,
                                   &timelineEventId).wasOk()
                            && timelineEventId != 0,
                        "insert audio into a launcher clip timeline")
                 && passed;
        passed = expect(session.updateLauncherClipEvent(
                            0, 3, timelineEventId, 0.25, 0.0,
                            0.75, 0.75),
                        "move and resize an event inside a launcher clip")
                 && passed;
        const auto warpId = session.addLauncherClipEventStretchMarker(
            0, 3, timelineEventId, 0.25, 0.20);
        passed = expect(warpId != 0
                            && session.updateLauncherClipEventStretchMarker(
                                   0, 3, timelineEventId, warpId, 0.24),
                        "add and move a launcher clip warp anchor")
                 && passed;
        const auto splitEventId = session.splitLauncherClipEvent(
            0, 3, timelineEventId, 0.625);
        passed = expect(splitEventId != 0,
                        "split an audio event inside a launcher clip")
                 && passed;
        passed = expect(session.setLauncherClipTimelineBounds(
                            0, 3, 2.0, 0.1, 1.8, 0.25, 1.25, true),
                        "set launcher clip start/end and loop bounds")
                 && passed;
        const auto launcherAutomationA
            = session.addLauncherClipAutomationPoint(0, 3, 1.25, -15.0f);
        const auto launcherAutomationB
            = session.addLauncherClipAutomationPoint(0, 3, 0.25, 2.0f);
        auto launcherAutomation = session.launcherClipAutomationPoints(0, 3);
        passed = expect(
            launcherAutomationA != 0 && launcherAutomationB != 0
                && launcherAutomation.size() == 2
                && std::abs(launcherAutomation.front().timeSeconds - 0.25)
                       < 0.000001
                && session.updateLauncherClipAutomationPoint(
                    0, 3, launcherAutomationA, 1.0, -8.0f)
                && session.setLauncherClipAutomationPointCurve(
                    0, 3, launcherAutomationA,
                    cydaw::app::AudioTrackController::AutomationCurve::bezier,
                    0.4f)
                && session.removeLauncherClipAutomationPoint(
                    0, 3, launcherAutomationB),
            "launcher clip automation keeps independent clip-local time")
                 && passed;
        timelineClip = session.launcherClip(0, 3);
        passed = expect(timelineClip.has_value()
                            && timelineClip->events.size() == 2
                            && std::abs(timelineClip->playStartSeconds - 0.1)
                                   < 0.000001
                            && std::abs(timelineClip->playEndSeconds - 1.8)
                                   < 0.000001
                            && std::abs(timelineClip->loopStartSeconds - 0.25)
                                   < 0.000001
                            && std::abs(timelineClip->loopEndSeconds - 1.25)
                                   < 0.000001,
                        "retain multi-event launcher clip timeline state")
                 && passed;
        passed = expect(session.removeLauncherClipEvent(
                            0, 3, splitEventId)
                            && session.removeLauncherClipEvent(
                                0, 3, timelineEventId)
                            && session.launcherClip(0, 3).has_value()
                            && session.launcherClip(0, 3)->events.empty()
                            && session.clearLauncherClip(0, 3),
                        "empty a launcher clip without deleting its container")
                 && passed;

        {
            auto emptyTrackSession =
                std::make_unique<cydaw::app::AudioTrackController>();
            std::size_t emptyTrackIndex {};
            passed = expect(emptyTrackSession->addEmptyAudioTrack(&emptyTrackIndex).wasOk()
                                && emptyTrackIndex == 0
                                && emptyTrackSession->trackCount() == 1
                                && emptyTrackSession->audioClips(0).empty(),
                            "create a real empty audio track") && passed;
            emptyTrackSession->setArrangementPlaybackEnabled(false);
            passed = expect(!emptyTrackSession->arrangementPlaybackEnabled(),
                            "launcher-only playback mode") && passed;
            emptyTrackSession->play();
            passed = expect(emptyTrackSession->isPlaying(),
                            "transport runs without an arrangement clip") && passed;
            passed = expect(emptyTrackSession->stopLauncherTrack(0, false),
                            "empty launcher slot stops its track") && passed;
            emptyTrackSession->stop();
        }

        if (testPluginBundle.exists()) {
            std::size_t pluginIndex {};
            passed = expect(session.setTrackPlugin(0, testPluginBundle, &pluginIndex).wasOk()
                                && pluginIndex == 0,
                            "plugin automation target instance") && passed;
            const auto pluginAutomationA = session.addPluginAutomationPoint(
                0, pluginIndex, 0, "Gain", 0.75, 0.8f);
            const auto pluginAutomationB = session.addPluginAutomationPoint(
                0, pluginIndex, 0, "Gain", 0.25, 0.2f);
            const auto pluginAutomation = session.pluginAutomationPoints(0, pluginIndex, 0);
            passed = expect(pluginAutomationA != 0 && pluginAutomationB != 0
                                && pluginAutomation.size() == 2
                                && pluginAutomation.front().timeSeconds
                                       <= pluginAutomation.back().timeSeconds,
                            "VST parameter automation creation and sorting") && passed;
            passed = expect(session.updatePluginAutomationPoint(
                                0, pluginIndex, 0, pluginAutomationA, 0.5, 0.6f),
                            "VST parameter automation update") && passed;
            passed = expect(session.setPluginAutomationPointCurve(
                                0, pluginIndex, 0, pluginAutomationA,
                                cydaw::app::AudioTrackController::AutomationCurve::bezier,
                                0.3f),
                            "VST parameter automation Bezier curve") && passed;
            passed = expect(session.removePluginAutomationPoint(
                                0, pluginIndex, 0, pluginAutomationB)
                                && session.pluginAutomationPoints(0, pluginIndex, 0).size() == 1,
                            "VST parameter automation deletion") && passed;
            session.clearTrackPlugins(0);
            passed = expect(session.pluginAutomationLanes(0).empty(),
                            "removing plugin chain removes its automation") && passed;
        }
        const auto first = session.trackInfo(0);
        const auto second = session.trackInfo(1);
        passed = expect(first.solo, "solo state") && passed;
        passed = expect(std::abs(first.driveDb - 12.0f) < 0.001f
                            && std::abs(first.filterCutoffHz - 1'200.0f) < 0.001f,
                        "node-controlled track DSP state") && passed;
        passed = expect(std::abs(first.clipStartSeconds - 0.5) < 0.001
                            && std::abs(first.sourceOffsetSeconds - 0.1) < 0.001
                            && std::abs(first.clipDurationSeconds - 0.4) < 0.001,
                        "non-destructive clip range") && passed;
        passed = expect(second.muted && std::abs(second.gainDb + 6.0f) < 0.001f,
                        "mixer state") && passed;

        session.setPosition(0.25);
        passed = expect(std::abs(session.position() - 0.25) < 0.001, "synchronised seek") && passed;
        session.clearTracks();
        passed = expect(session.trackCount() == 0, "clear tracks") && passed;
        session.clearMidiNotes();
        passed = expect(session.midiNotes().empty(), "clear MIDI notes") && passed;
    }

    passed = expect(cydaw::app::tr("toolbar.openAudio").isNotEmpty()
                        && cydaw::app::tr("command.placeholder").isNotEmpty()
                        && cydaw::app::tr("command.category.workspace").isNotEmpty(),
                    "embedded translations and command-palette catalogue") && passed;


    {
        // A long alternation of edits and undos, checked at every step. The
        // history shares its trees with the editor rather than copying them,
        // and copies elsewhere are structural walks rather than round trips
        // through JSON text: both are invariants argued from reading the
        // code, and this is what turns the argument into a check. The engine
        // has to still compile at the end of it.
        cydaw::app::NodeGraphComponent stress;
        stress.setSize(1000, 700);
        std::vector<juce::String> expectedSteps { graphSignature(stress) };
        for (const auto& key : { juce::String("gain"), juce::String("drive"),
                                 juce::String("lowpass"),
                                 juce::String("hard-clip"),
                                 juce::String("stereo-width"),
                                 juce::String("eq") }) {
            if (stress.insertNode(key).failed())
                continue;
            expectedSteps.push_back(graphSignature(stress));
        }
        auto walked = expectedSteps.size() > 4;
        for (auto step = static_cast<int>(expectedSteps.size()) - 2;
             step >= 0 && walked; --step) {
            stress.undoForTest();
            walked = graphSignature(stress)
                     == expectedSteps[static_cast<std::size_t>(step)];
        }
        for (std::size_t step = 1; step < expectedSteps.size() && walked;
             ++step) {
            stress.redoForTest();
            walked = graphSignature(stress) == expectedSteps[step];
        }
        const auto compiled = stress.nativeDspGraph();
        passed = expect(walked && !compiled.nodes.empty(),
                        "a graph walks its whole history and still compiles")
                 && passed;
    }

    {
        // Undo and redo hand their stored trees straight to the restore,
        // which keeps them and reads them again on the next pass. Nothing may
        // write to them in between: a corrupted entry shows up as a graph
        // that comes back different the second time it is visited.
        //
        // What has to come back is the graph — its nodes, its links, their
        // ports. Not the bytes: the same graph serialises eleven kilobytes
        // larger after a round trip, because its factory classes are rebuilt
        // from their live objects instead of re-emitted from the description
        // they were parsed from, and those spell out defaults the original
        // left implicit.
        cydaw::app::NodeGraphComponent history;
        history.setSize(1000, 700);
        const auto describe = [&history] {
            return graphSignature(history);
        };
        const auto before = describe();
        (void) history.insertNode("gain");
        const auto after = describe();
        auto stable = before != after && before.isNotEmpty();
        for (auto pass = 0; pass < 3 && stable; ++pass) {
            history.undoForTest();
            stable = stable && describe() == before;
            history.redoForTest();
            stable = stable && describe() == after;
        }
        history.undoForTest();
        passed = expect(stable && describe() == before,
                        "undo and redo return the same graph every time")
                 && passed;
    }

    {
        // Dragging a card used to repaint the whole editor. It now repaints
        // only what it claims to change, and a claim that forgets something
        // leaves that something drawn at its old place. So: render, drag,
        // repaint only the claimed region over the old image, and demand the
        // result be pixel for pixel what a full repaint gives.
        cydaw::app::NodeGraphComponent dragged;
        dragged.setSize(1000, 700);
        for (const auto& key : { juce::String("gain"), juce::String("drive"),
                                 juce::String("lowpass"),
                                 juce::String("compressor") })
            (void) dragged.insertNode(key);

        const auto render = [&dragged](juce::Image& target,
                                       juce::Rectangle<int> clip) {
            juce::Graphics graphics(target);
            if (!clip.isEmpty())
                graphics.reduceClipRegion(clip);
            dragged.paintEntireComponent(graphics, true);
        };
        juce::Image partial(juce::Image::ARGB, dragged.getWidth(),
                            dragged.getHeight(), true);
        render(partial, {});

        auto clean = true;
        auto moves = 0;
        for (const auto offset : { juce::Point<float>(37.0f, 23.0f),
                                   juce::Point<float>(-52.0f, -31.0f),
                                   juce::Point<float>(11.0f, -64.0f) }) {
            const auto regions = dragged.dragNodeForTest("compressor",
                                                         offset);
            if (regions.isEmpty())
                continue;
            ++moves;
            for (const auto& region : regions)
                render(partial, region);
            juce::Image whole(juce::Image::ARGB, dragged.getWidth(),
                              dragged.getHeight(), true);
            render(whole, {});
            const juce::Image::BitmapData painted(
                partial, juce::Image::BitmapData::readOnly);
            const juce::Image::BitmapData expected(
                whole, juce::Image::BitmapData::readOnly);
            for (auto y = 0; y < whole.getHeight() && clean; ++y)
                for (auto x = 0; x < whole.getWidth(); ++x)
                    if (painted.getPixelColour(x, y)
                        != expected.getPixelColour(x, y)) {
                        clean = false;
                        break;
                    }
        }
        passed = expect(moves == 3 && clean,
                        "a drag repaints everything it moves")
                 && passed;
    }

    {
        // Sockets are drawn where the card can space them, and a click has to
        // land on the one it is aimed at. On a four-input card framed out to a
        // third of full size the sockets sit closer together than the click
        // radius, so their hit circles necessarily overlap: what must hold is
        // that a socket's own point resolves to that socket and not a
        // neighbour. Taking the first match inside the radius broke exactly
        // that, and nothing was watching.
        cydaw::app::NodeGraphComponent sockets;
        sockets.setSize(1200, 800);
        auto reachable = true;
        auto ports = 0;
        for (const auto zoom : { 0.3f, 0.55f, 1.0f }) {
            sockets.setCanvasZoom(zoom);
            for (const auto& typeKey : { juce::String("compressor"),
                                         juce::String("eq") }) {
                if (sockets.insertNode(typeKey).failed())
                    continue;
                for (const auto input : { true, false })
                    for (const auto& portKey :
                         sockets.nodePortKeys(typeKey, input)) {
                        ++ports;
                        reachable = reachable
                                    && sockets.portKeyAtPoint(
                                           sockets.portSocketPoint(typeKey,
                                                                    portKey),
                                           input)
                                           == portKey;
                    }
            }
        }
        passed = expect(ports > 8 && reachable,
                        "every socket answers a click aimed at it")
                 && passed;
    }

    {
        // A shaped table has to survive the file and reach the voice. It is
        // stored as harmonics rather than samples, which is what keeps it
        // small enough to serialise and band-limited whatever is drawn.
        cydaw::app::NodeGraphComponent shaped;
        shaped.setSize(1000, 700);
        auto built = shaped.insertFactoryDevice("table-synth").wasOk();
        auto shape = shaped.nodeWavetable("wavetable");
        // A square: odd harmonics only. Distinctive enough that a table which
        // silently fell back to the built-in sawtooth would not pass.
        for (std::size_t frame = 0;
             frame < cydaw::engine::WavetableShape::frameCount; ++frame)
            for (std::size_t harmonic = 0;
                 harmonic < cydaw::engine::WavetableShape::harmonicCount;
                 ++harmonic)
                shape.harmonics[frame][harmonic]
                    = harmonic % 2 == 0
                          ? 1.0f / static_cast<float>(harmonic + 1)
                          : 0.0f;
        built = built && shaped.setNodeWavetable("wavetable", shape);

        const auto saved = shaped.serialise();
        cydaw::app::NodeGraphComponent reloaded;
        reloaded.setSize(1000, 700);
        reloaded.restore(saved);
        const auto sameShape
            = reloaded.nodeWavetable("wavetable") == shape;

        // And it must be what the voice is handed, not just what the file
        // holds: the plan is where the sound actually comes from.
        cydaw::engine::PolySynthGraphPlan plan;
        const auto compiled = reloaded.compilePolySynthGraph(plan).wasOk();
        const auto reachedVoice = plan.wavetable != nullptr;
        // The even harmonics are zero, so the cycle's second half mirrors the
        // first. A sawtooth bank would not do that.
        auto mirrored = true;
        if (reachedVoice)
            for (std::size_t index = 0; index < 64; ++index) {
                const auto& frame = plan.wavetable->frames[4];
                const auto half
                    = cydaw::engine::WavetableBank::frameLength / 2;
                mirrored = mirrored
                           && std::abs(frame[index] + frame[index + half])
                                  < 0.02f;
            }
        passed = expect(built && sameShape && compiled && reachedVoice
                            && mirrored,
                        "a shaped wavetable survives the file and reaches the "
                        "voice")
                 && passed;
    }

    {
        // Drawing is an edit like any other: one gesture leaves one undo
        // entry, and undoing it brings back the table that was there before.
        // Without this a drawn table could only be un-drawn by hand.
        cydaw::app::NodeGraphComponent drawn;
        drawn.setSize(1000, 700);
        auto built = drawn.insertNode("wavetable").wasOk();
        const auto before = drawn.nodeWavetable("wavetable");
        auto shape = before;
        // A frame reduced to its fundamental: nothing else in the table
        // looks like that, so a stale read would be obvious.
        shape.harmonics[3] = {};
        shape.harmonics[3][0] = 1.0f;
        built = built && drawn.drawWavetableForTest("wavetable", shape);
        const auto after = drawn.nodeWavetable("wavetable");
        drawn.undoForTest();
        const auto undone = drawn.nodeWavetable("wavetable");
        drawn.redoForTest();
        const auto redone = drawn.nodeWavetable("wavetable");
        passed = expect(built && after == shape && undone == before
                            && !(before == shape) && redone == shape,
                        "one drawing gesture is one undo")
                 && passed;
    }

    {
        // A control cable dropped on a knob: the source, a range node set to
        // base ± depth, the knob's value port — one gesture, one undo. The
        // knob then shows the span as a ring and, turned, moves its centre.
        cydaw::app::NodeGraphComponent mod;
        mod.setSize(1000, 700);
        // A gain card: its level is a plain knob with a value input, and it
        // sits at 0 dB in the middle of -120..+24, so a quarter of the range
        // either side stays inside it. A low-pass publishes its cutoff at
        // the top of its range behind a response plot, which is not a knob.
        auto built = mod.insertNode("lfo-core").wasOk()
                     && mod.insertNode("gain").wasOk();
        mod.rebuildEmbeddedControlsForTest();
        const auto countBefore = mod.nodeCount();
        auto gainBefore = std::numeric_limits<double>::quiet_NaN();
        for (const auto& parameter : mod.graphInterfaceState().parameters)
            if (parameter.key.endsWith(".gain.db"))
                gainBefore = parameter.value;
        built = built && std::isfinite(gainBefore)
                && mod.modulateParameterForTest("lfo-core", "gain", "gain.db");
        const auto countAfter = mod.nodeCount();
        const auto ring = mod.modulationRingForTest("gain", "gain.db");
        const auto native = mod.nativeDspGraph();
        // The typed compiler names the destination by the primitive's
        // parameter, which for a gain is "gain-db".
        juce::StringArray modulationKeys;
        for (const auto& modulation : native.modulations)
            modulationKeys.add(juce::String(modulation.destinationParameterKey));
        const auto reachesEngine = std::any_of(
            native.modulations.begin(), native.modulations.end(),
            [](const auto& modulation) {
                return modulation.destinationParameterKey == "gain-db";
            });
        const auto brackets = !ring.isEmpty() && ring.getStart() < gainBefore
                              && ring.getEnd() > gainBefore;
        // The ring itself sets the depth: dragged up it widens the sweep
        // about the same centre and the knob's value stays; through zero
        // the sweep turns over, which the engine sees as a range whose low
        // end is above its high end. One drag, one undo, each time.
        // Before the turn below: a synthetic turn leaves its history entry
        // pending, and a gesture that ends here would flush the two as one.
        const auto ringBefore = ring;
        const auto hit = mod.modulationRingHitForTest("gain", "gain.db");
        const auto centreOf = [](const juce::Range<double>& range) {
            return (range.getStart() + range.getEnd()) * 0.5;
        };
        const auto rangeSpanInGraph = [&mod] {
            const auto graph = mod.nativeDspGraph();
            for (const auto& node : graph.nodes)
                if (node.kind == cydaw::engine::NativeDspNodeKind::controlMapRange)
                    return static_cast<double>(node.parameters.mapOutputMaximum
                                               - node.parameters.mapOutputMinimum);
            return 0.0;
        };
        const auto widenedDrag = mod.dragModulationRingForTest("gain", "gain.db",
                                                               -60.0f);
        // The gesture leaves its readout beside the knob, in the knob's
        // units, with the depth the ring now has.
        const auto bubbleShown = mod.modulationBubbleShownForTest("gain", "gain.db");
        const auto wideReadout = mod.modulationReadoutForTest("gain", "gain.db");
        const auto ringWide = mod.modulationRingForTest("gain", "gain.db");
        const auto widened = widenedDrag
                             && ringWide.getLength() > ringBefore.getLength() + 20.0
                             && std::abs(centreOf(ringWide) - centreOf(ringBefore))
                                    < 0.01;
        const auto wideInEngine = std::abs(rangeSpanInGraph() - ringWide.getLength())
                                  < 0.01;
        mod.undoForTest();
        mod.rebuildEmbeddedControlsForTest();
        const auto depthUndone = mod.modulationRingForTest("gain", "gain.db")
                                 == ringBefore;
        const auto invertedDrag = mod.dragModulationRingForTest("gain", "gain.db",
                                                                200.0f);
        const auto inverted = invertedDrag
                              && mod.modulationInvertedForTest("gain", "gain.db")
                              && rangeSpanInGraph() < 0.0;
        mod.undoForTest();
        mod.rebuildEmbeddedControlsForTest();
        const auto invertUndone = mod.modulationRingForTest("gain", "gain.db")
                                      == ringBefore
                                  && !mod.modulationInvertedForTest("gain", "gain.db");
        const auto readsOut = bubbleShown
                              && wideReadout.contains(juce::String(
                                  std::lround(ringWide.getLength() * 0.5)))
                              && wideReadout.contains("dB")
                              && !wideReadout.containsIgnoreCase("retourn")
                              && !wideReadout.containsIgnoreCase("turned");
        // The ring's menu: turned over, then removed — one undo each. The
        // knob keeps the centre it was showing when the cable goes.
        const auto menuInvertDone = mod.invertModulationForTest("gain", "gain.db");
        const auto invertedReadout = mod.modulationReadoutForTest("gain", "gain.db");
        const auto menuInverted = menuInvertDone
                                  && mod.modulationInvertedForTest("gain", "gain.db")
                                  && rangeSpanInGraph() < 0.0
                                  && (invertedReadout.containsIgnoreCase("retourn")
                                      || invertedReadout.containsIgnoreCase("turned"));
        mod.undoForTest();
        mod.rebuildEmbeddedControlsForTest();
        const auto menuInvertUndone = mod.modulationRingForTest("gain", "gain.db")
                                          == ringBefore
                                      && !mod.modulationInvertedForTest("gain", "gain.db");
        const auto countModulated = mod.nodeCount();
        const auto removed = mod.removeModulationForTest("gain", "gain.db")
                             && mod.nodeCount() == countModulated - 1
                             && mod.modulationRingForTest("gain", "gain.db").isEmpty()
                             && mod.nativeDspGraph().modulations.empty();
        auto gainAfterRemoval = std::numeric_limits<double>::quiet_NaN();
        for (const auto& parameter : mod.graphInterfaceState().parameters)
            if (parameter.key.endsWith(".gain.db"))
                gainAfterRemoval = parameter.value;
        const auto keptValue = std::abs(gainAfterRemoval - centreOf(ringBefore)) < 0.01;
        mod.undoForTest();
        mod.rebuildEmbeddedControlsForTest();
        const auto removeUndone = mod.nodeCount() == countModulated
                                  && mod.modulationRingForTest("gain", "gain.db")
                                         == ringBefore;
        // The drop keeps the knob's value as the centre of the sweep: a
        // quarter of the range clipped at the top edge used to move it.
        const auto keptCentre = std::abs(centreOf(ringBefore) - gainBefore) < 0.01;
        const auto ringDetail = juce::String(" [ringBefore=")
            + juce::String(ringBefore.getStart(), 1) + ".."
            + juce::String(ringBefore.getEnd(), 1)
            + " ringWide=" + juce::String(ringWide.getStart(), 1) + ".."
            + juce::String(ringWide.getEnd(), 1)
            + " engineSpan=" + juce::String(rangeSpanInGraph(), 1)
            + " readout='" + wideReadout + "'"
            + " gainAfterRemoval=" + juce::String(gainAfterRemoval, 1) + "]";
        // Turning the knob moves the whole span, not the parameter.
        const auto turned = mod.turnModulatedKnobForTest("gain", "gain.db",
                                                         gainBefore - 12.0);
        const auto moved = mod.modulationRingForTest("gain", "gain.db");
        const auto centred = !moved.isEmpty()
                             && std::abs((moved.getStart() + moved.getEnd())
                                             * 0.5 - (gainBefore - 12.0))
                                    < 0.5;
        // While the sound runs, the range node reports the parameter's
        // value at each instant; the knob shows it as a dot on the ring, and
        // clears it when the values stop coming.
        const auto fed = mod.feedModulationPositionForTest("gain", "gain.db",
                                                           -30.0);
        const auto shown = mod.modulationPositionForTest("gain", "gain.db");
        mod.setControlValues({});
        const auto cleared = !mod.modulationPositionForTest("gain", "gain.db")
                                  .has_value();
        // Two edits, two undos: the turn is its own entry, as any knob
        // gesture is, and the drop is the one before it.
        mod.undoForTest();
        mod.rebuildEmbeddedControlsForTest();
        const auto turnUndone = mod.modulationRingForTest("gain", "gain.db")
                                == ring;
        mod.undoForTest();
        mod.rebuildEmbeddedControlsForTest();
        const auto undone = turnUndone && mod.nodeCount() == countBefore
                            && mod.modulationRingForTest("gain", "gain.db")
                                   .isEmpty();
        const auto detail = juce::String(" (nodes=") + juce::String(mod.nodeCount())
            + "/" + juce::String(countBefore)
            + " gainBefore=" + juce::String(gainBefore, 1)
            + " ring=" + juce::String(ring.getStart(), 1) + ".."
            + juce::String(ring.getEnd(), 1)
            + " moved=" + juce::String(moved.getStart(), 1) + ".."
            + juce::String(moved.getEnd(), 1)
            + " modulations=" + modulationKeys.joinIntoString("|")
            + " afterDrop=" + juce::String(countAfter)
            + " refused='" + mod.modulateFailureForTest() + "')";
        const auto label = [&detail](const char* claim) {
            return juce::String(claim) + detail;
        };
        passed = expect(built && countAfter == countBefore + 1,
                        label("a cable on a knob inserts a range node").toRawUTF8())
                 && passed;
        passed = expect(reachesEngine,
                        label("a cable on a knob reaches the engine as a modulation").toRawUTF8())
                 && passed;
        passed = expect(brackets,
                        label("the ring brackets the value the knob had").toRawUTF8())
                 && passed;
        passed = expect(turned && centred,
                        label("turning a modulated knob moves its sweep").toRawUTF8())
                 && passed;
        passed = expect(hit, (label("the ring is hit where it is drawn") + ringDetail)
                                 .toRawUTF8())
                 && passed;
        passed = expect(widened && wideInEngine,
                        (label("dragging the ring sets the depth") + ringDetail)
                            .toRawUTF8())
                 && passed;
        passed = expect(depthUndone && invertUndone,
                        (label("one ring drag is one undo") + ringDetail).toRawUTF8())
                 && passed;
        passed = expect(inverted,
                        (label("through zero the sweep turns over") + ringDetail)
                            .toRawUTF8())
                 && passed;
        passed = expect(keptCentre,
                        (label("a dropped cable keeps the knob's value as its centre")
                         + ringDetail)
                            .toRawUTF8())
                 && passed;
        passed = expect(readsOut,
                        (label("the ring reads out its depth") + ringDetail).toRawUTF8())
                 && passed;
        passed = expect(menuInverted && menuInvertUndone,
                        (label("the ring's menu turns the sweep over") + ringDetail)
                            .toRawUTF8())
                 && passed;
        passed = expect(removed && keptValue && removeUndone,
                        (label("the ring's menu removes the modulation") + ringDetail)
                            .toRawUTF8())
                 && passed;
        passed = expect(undone,
                        label("one undo removes the modulation").toRawUTF8())
                 && passed;
        passed = expect(fed && shown.has_value() && std::abs(*shown + 30.0) < 0.01
                            && cleared,
                        label("the ring shows where the modulation is").toRawUTF8())
                 && passed;
    }

    {
        // Card positions are fractions of the canvas, so the graph stretches
        // with the window while a pixel offset does not: a view framed on a
        // wide window opened on empty canvas in a narrow one. The view is
        // saved as a point of the graph and lands on the same card.
        cydaw::app::NodeGraphComponent wide;
        wide.setSize(1600, 900);
        const auto built = wide.insertFactoryDevice("table-synth").wasOk()
                           && wide.insertNodeOnConnection("voice-filter", "master",
                                                          "gain").wasOk();
        wide.arrangeNodes();
        wide.frameNode("gain");
        const auto state = wide.serialise();
        cydaw::app::NodeGraphComponent narrow;
        narrow.setSize(1000, 700);
        narrow.restore(state);
        narrow.runPendingLayoutForTest();
        const auto card = narrow.nodeBoundsForTest("gain");
        const auto canvas = narrow.canvasBoundsForTest();
        const auto centre = canvas.getCentre();
        const auto dx = std::abs(card.getCentreX() - centre.x);
        const auto dy = std::abs(card.getCentreY() - centre.y);
        const auto* saved = state.getDynamicObject();
        const auto label = juce::String(
            "a framed view survives a narrower window (dx=") + juce::String(dx)
            + " dy=" + juce::String(dy)
            + " wideCanvas=" + wide.canvasBoundsForTest().toString()
            + " wideCard=" + wide.nodeBoundsForTest("gain").toString()
            + " narrowCanvas=" + canvas.toString()
            + " narrowCard=" + card.toString()
            + " centre=" + (saved != nullptr
                                ? saved->getProperty("viewCentreX").toString()
                                      + "," + saved->getProperty("viewCentreY").toString()
                                : juce::String("none"))
            + " zoom=" + (saved != nullptr
                              ? saved->getProperty("viewZoom").toString()
                              : juce::String("none"))
            + ")";
        passed = expect(built && !card.isEmpty() && dx < 4 && dy < 4,
                        label.toRawUTF8())
                 && passed;
        // And the window itself: opening a side panel, detaching the editor,
        // or the DAW sizing its window after the project loaded all stretch
        // the world under the view. The card at the centre stays there.
        narrow.setSize(1400, 900);
        const auto grown = narrow.nodeBoundsForTest("gain");
        const auto grownCentre = narrow.canvasBoundsForTest().getCentre();
        const auto gdx = std::abs(grown.getCentreX() - grownCentre.x);
        const auto gdy = std::abs(grown.getCentreY() - grownCentre.y);
        const auto grownLabel = juce::String(
            "the view follows the cards when the window changes (dx=")
            + juce::String(gdx) + " dy=" + juce::String(gdy) + ")";
        passed = expect(gdx < 4 && gdy < 4, grownLabel.toRawUTF8()) && passed;
        // The workspace serialises and restores the editor between a load
        // and its first layout. An editor that has not laid out yet hands
        // the view on as it came, instead of a centre measured on nothing.
        cydaw::app::NodeGraphComponent unsized;
        unsized.restore(state);
        cydaw::app::NodeGraphComponent later;
        later.setSize(1000, 700);
        later.restore(unsized.serialise());
        later.runPendingLayoutForTest();
        const auto handed = later.nodeBoundsForTest("gain");
        const auto handedCentre = later.canvasBoundsForTest().getCentre();
        const auto hdx = std::abs(handed.getCentreX() - handedCentre.x);
        const auto hdy = std::abs(handed.getCentreY() - handedCentre.y);
        const auto handedLabel = juce::String(
            "an editor that has not laid out hands the view on (dx=")
            + juce::String(hdx) + " dy=" + juce::String(hdy) + ")";
        passed = expect(hdx < 4 && hdy < 4, handedLabel.toRawUTF8()) && passed;
    }

    {
        // On an instrument's chain the native graph comes from the boundary
        // builder, not the typed compiler, and that builder skipped every
        // node without audio ports: an LFO dropped on a band's knob was
        // drawn, saved, and compiled nowhere. The chain has to carry the
        // control nodes and their cables as modulations.
        cydaw::app::NodeGraphComponent inst;
        inst.setSize(1000, 700);
        auto built = inst.insertFactoryDevice("table-synth").wasOk()
                     && inst.insertNodeOnConnection("voice-filter", "master",
                                                    "eq-band").wasOk()
                     && inst.insertNode("lfo-core").wasOk();
        inst.rebuildEmbeddedControlsForTest();
        built = built && inst.modulateParameterForTest("lfo-core", "eq-band",
                                                       "eqBand.frequency");
        const auto chain = inst.nativeDspGraph();
        const auto controlNodes = std::count_if(
            chain.nodes.begin(), chain.nodes.end(), [](const auto& node) {
                return cydaw::app::isControlOnlyNativeKind(node.kind);
            });
        const auto named = std::count_if(
            chain.modulations.begin(), chain.modulations.end(),
            [](const auto& modulation) {
                return !modulation.destinationParameterKey.empty();
            });
        const auto detail = juce::String(" (controlNodes=") + juce::String(controlNodes)
            + " modulations=" + juce::String(static_cast<int>(chain.modulations.size()))
            + " named=" + juce::String(named)
            + " refused='" + inst.modulateFailureForTest() + "')";
        const auto label = juce::String(
            "an instrument's chain carries its control nodes and cables") + detail;
        passed = expect(built && controlNodes == 2 && chain.modulations.size() == 2
                            && named == 1,
                        label.toRawUTF8())
                 && passed;
    }

    {
        // A spectrum that is not calibrated still looks like a spectrum. Send
        // a sine at a known frequency and ask where the peak is read, in
        // hertz — at a thousand, and down at a hundred, where a decimated
        // block could never have resolved it.
        const auto peakOf = [](const double hz) {
            cydaw::engine::AudioAnalysisFrame frame;
            frame.sampleRate = 48'000.0f;
            frame.channelCount = 2;
            frame.waveformCount
                = cydaw::engine::AudioAnalysisFrame::waveformCapacity;
            for (std::size_t index = 0; index < frame.waveformCount; ++index)
                frame.waveform[index] = static_cast<float>(
                    0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * hz
                                   * static_cast<double>(index) / 48'000.0));
            std::array<float, 96> bins {};
            cydaw::engine::computeLogSpectrum(frame, 20.0, 20'000.0,
                                              bins.data(), bins.size());
            const auto peak = static_cast<std::size_t>(std::distance(
                bins.begin(), std::max_element(bins.begin(), bins.end())));
            const auto position = static_cast<double>(peak) / 95.0;
            return 20.0 * std::pow(1'000.0, position);
        };
        const auto atThousand = peakOf(1'000.0);
        const auto atHundred = peakOf(100.0);
        // Ninety-six bins over three decades put neighbours seven and a half
        // per cent apart; the peak must land on the right one.
        passed = expect(std::abs(atThousand - 1'000.0) < 80.0
                            && std::abs(atHundred - 100.0) < 8.0,
                        "the spectrum reads a sine at the frequency it was")
                 && passed;
    }

    {
        // And through the graph, all the way to the EQ curve on a card: that
        // is what is looked at, and a frame that never reached it would leave
        // the curve drawn over nothing.
        cydaw::app::NodeGraphComponent eq;
        eq.setSize(1000, 700);
        auto built = eq.insertFactoryDevice("precision-eq").wasOk();
        eq.rebuildEmbeddedControlsForTest();
        cydaw::engine::AudioAnalysisFrame frame;
        frame.sampleRate = 48'000.0f;
        frame.channelCount = 2;
        frame.waveformCount = cydaw::engine::AudioAnalysisFrame::waveformCapacity;
        for (std::size_t index = 0; index < frame.waveformCount; ++index)
            frame.waveform[index] = static_cast<float>(
                0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 2'000.0
                               * static_cast<double>(index) / 48'000.0));
        eq.setAnalysisFrame(frame);
        const auto peak = eq.eqCurveSpectrumPeakHzForTest();
        passed = expect(built && std::abs(peak - 2'000.0) < 160.0,
                        "the signal reaches the EQ curve on the card")
                 && passed;
    }

    {
        // A band tuned in the file — the way the demo tunes its four — has
        // to be where the card's live curve shows it once the graph is
        // restored. The details panel said 110 Hz; the plot said 1000.
        cydaw::app::NodeGraphComponent tuned;
        tuned.setSize(1000, 700);
        auto built = tuned.insertNode("eq-band").wasOk();
        auto state = cydaw::app::cloneJsonValue(tuned.serialise());
        auto written = false;
        if (auto* root = state.getDynamicObject())
            if (auto* nodes = root->getProperty("nodes").getArray())
                for (auto& node : *nodes) {
                    auto* body = node.getDynamicObject();
                    if (body == nullptr
                        || body->getProperty("type").toString() != "eq-band")
                        continue;
                    if (auto* parameters
                        = body->getProperty("parameters").getDynamicObject()) {
                        parameters->setProperty("bandFrequencyHz", 110.0);
                        written = true;
                    }
                }
        tuned.restore(state);
        tuned.rebuildEmbeddedControlsForTest();
        const auto shown = tuned.eqCurveFirstBandFrequencyForTest("eq-band");
        // And its socket, which read the file's port value: "frequency =
        // 1000" beside a band the plot and the details panel put at 110.
        const auto socket = tuned.inputPortDefaultForTest("eq-band", "frequency")
                                .value_or(0.0);
        auto published = 0.0;
        for (const auto& parameter : tuned.graphInterfaceState().parameters)
            if (parameter.key.endsWith(".eqBand.frequency"))
                published = parameter.value;
        const auto label = juce::String(
            "a band tuned in the file is where the live curve shows it (shown=")
            + juce::String(shown, 1) + " socket=" + juce::String(socket, 1)
            + " published=" + juce::String(published, 1) + ")";
        passed = expect(built && written && std::abs(shown - 110.0) < 0.5
                            && std::abs(socket - 110.0) < 0.5,
                        label.toRawUTF8())
                 && passed;
    }

    {
        // A value typed into a band's socket — the details panel's slider —
        // used to change the socket's caption and nothing else: the filter
        // ran the parameter, which the socket did not write.
        cydaw::app::NodeGraphComponent typed;
        typed.setSize(1000, 700);
        auto built = typed.insertNode("eq-band").wasOk();
        typed.rebuildEmbeddedControlsForTest();
        built = built
                && typed.setInputPortDefaultForTest("eq-band", "frequency", 440.0);
        typed.rebuildEmbeddedControlsForTest();
        const auto shown = typed.eqCurveFirstBandFrequencyForTest("eq-band");
        auto published = 0.0;
        for (const auto& parameter : typed.graphInterfaceState().parameters)
            if (parameter.key.endsWith(".eqBand.frequency"))
                published = parameter.value;
        const auto chain = typed.nativeDspGraph();
        const auto runs = std::any_of(
            chain.nodes.begin(), chain.nodes.end(), [](const auto& node) {
                return std::abs(node.parameters.bandFrequencyHz - 440.0f) < 0.5f;
            });
        const auto label = juce::String(
            "a value typed into a band's socket reaches the filter (shown=")
            + juce::String(shown, 1) + " published=" + juce::String(published, 1)
            + " runs=" + (runs ? "yes" : "no") + ")";
        passed = expect(built && std::abs(shown - 440.0) < 0.5
                            && std::abs(published - 440.0) < 0.5 && runs,
                        label.toRawUTF8())
                 && passed;
    }

    {
        // Zooming rebuilds the cards; a curve built between two frames sat
        // over nothing until the next one, which a capture never waited for.
        // The frame comes first here, before any curve exists to hand it to
        // — zoomed out, the graph has none, and framing a card makes one.
        cydaw::app::NodeGraphComponent kept;
        kept.setSize(1000, 700);
        auto built = kept.insertFactoryDevice("precision-eq").wasOk();
        cydaw::engine::AudioAnalysisFrame frame;
        frame.sampleRate = 48'000.0f;
        frame.channelCount = 2;
        frame.waveformCount = cydaw::engine::AudioAnalysisFrame::waveformCapacity;
        for (std::size_t index = 0; index < frame.waveformCount; ++index)
            frame.waveform[index] = static_cast<float>(
                0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 2'000.0
                               * static_cast<double>(index) / 48'000.0));
        kept.setAnalysisFrame(frame);
        kept.rebuildEmbeddedControlsForTest();
        const auto first = kept.eqCurveSpectrumPeakHzForTest();
        kept.rebuildEmbeddedControlsForTest();
        const auto again = kept.eqCurveSpectrumPeakHzForTest();
        const auto label = juce::String(
            "the spectrum reaches a curve built after the frame and outlives a rebuild (first=")
            + juce::String(first, 0) + " again=" + juce::String(again, 0) + ")";
        passed = expect(built && std::abs(first - 2'000.0) < 160.0
                            && std::abs(again - 2'000.0) < 160.0,
                        label.toRawUTF8())
                 && passed;
    }

    {
        // The curves exist zoomed out, hidden; the frame fed only the shown
        // ones. Framing a card shows one between two frames, empty until
        // the next — which the capture right after framing never waited
        // for. A window too small for the plot hides it the same way.
        cydaw::app::NodeGraphComponent shown;
        shown.setSize(220, 150);
        auto built = shown.insertFactoryDevice("precision-eq").wasOk();
        shown.rebuildEmbeddedControlsForTest();
        shown.runPendingLayoutForTest();
        cydaw::engine::AudioAnalysisFrame frame;
        frame.sampleRate = 48'000.0f;
        frame.channelCount = 2;
        frame.waveformCount = cydaw::engine::AudioAnalysisFrame::waveformCapacity;
        for (std::size_t index = 0; index < frame.waveformCount; ++index)
            frame.waveform[index] = static_cast<float>(
                0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 2'000.0
                               * static_cast<double>(index) / 48'000.0));
        shown.setAnalysisFrame(frame);
        const auto hidden = shown.eqCurveSpectrumPeakHzForTest();
        shown.setSize(1000, 700);
        shown.runPendingLayoutForTest();
        const auto peak = shown.eqCurveSpectrumPeakHzForTest();
        const auto label = juce::String(
            "a curve shown between two frames reads the last one (hidden=")
            + juce::String(hidden, 0) + " shown=" + juce::String(peak, 0) + ")";
        passed = expect(built && hidden < 1.0 && std::abs(peak - 2'000.0) < 160.0,
                        label.toRawUTF8())
                 && passed;
    }

    {
        // The drive card names its socket "drive" and its parameter
        // "drive-db"; asked with the socket's name, the mapping between them
        // found nothing, and the caption sat at the schema's value whatever
        // the knob said. Turning the knob has to move the caption.
        cydaw::app::NodeGraphComponent drive;
        drive.setSize(1000, 700);
        auto built = drive.insertNodeBeforeOutput("drive").wasOk();
        juce::String key;
        for (const auto& parameter : drive.graphInterfaceState().parameters)
            if (parameter.key.endsWith(".drive.db"))
                key = parameter.key;
        built = built && key.isNotEmpty()
                && drive.setGraphInterfaceParameter(key, 12.0);
        const auto socket = drive.inputPortDefaultForTest("drive", "drive")
                                .value_or(-999.0);
        const auto chain = drive.nativeDspGraph();
        const auto runs = std::any_of(
            chain.nodes.begin(), chain.nodes.end(), [](const auto& node) {
                return std::abs(node.parameters.driveDb - 12.0f) < 0.01f;
            });
        const auto label = juce::String(
            "a drive knob's socket says what the knob says (socket=")
            + juce::String(socket, 1) + " runs=" + (runs ? "yes" : "no") + ")";
        passed = expect(built && std::abs(socket - 12.0) < 0.01 && runs,
                        label.toRawUTF8())
                 && passed;
    }

    {
        // The renderer itself, on the simplest card: a gain of -6 dB must
        // be heard as -6 dB, or nothing measured below means anything.
        cydaw::app::NodeGraphComponent trim;
        trim.setSize(1000, 700);
        auto built = trim.insertNodeBeforeOutput("gain").wasOk()
                     && trim.setInputPortDefaultForTest("gain", "gain", -6.0);
        const auto graph = trim.nativeDspGraph();
        const auto heard = measuredGainDb(graph, 1'000.0);
        const auto label = juce::String("a gain card at -6 dB is heard at -6 dB (heard=")
            + juce::String(heard, 2) + " nodes="
            + juce::String(static_cast<int>(graph.nodes.size())) + " links="
            + juce::String(static_cast<int>(graph.connections.size())) + " out="
            + juce::String(static_cast<juce::int64>(graph.outputNodeId)) + ")";
        passed = expect(built && std::abs(heard + 6.0) < 0.3, label.toRawUTF8())
                 && passed;
    }

    {
        // What the card draws is what the filter does: a sine through the
        // band's own native graph, measured, against the curve's magnitude
        // at that frequency — on the peak and on both skirts.
        cydaw::app::NodeGraphComponent eq;
        eq.setSize(1000, 700);
        auto built = eq.insertNodeBeforeOutput("eq-band").wasOk()
                     && eq.setInputPortDefaultForTest("eq-band", "frequency", 110.0)
                     && eq.setInputPortDefaultForTest("eq-band", "gain", 6.0)
                     && eq.setInputPortDefaultForTest("eq-band", "q", 1.0);
        eq.rebuildEmbeddedControlsForTest();
        const auto graph = eq.nativeDspGraph();
        juce::String detail;
        detail << " nodes=" << static_cast<int>(graph.nodes.size())
               << " links=" << static_cast<int>(graph.connections.size())
               << " out=" << juce::String(static_cast<juce::int64>(graph.outputNodeId));
        auto worst = 0.0;
        for (const auto hz : { 55.0, 110.0, 220.0, 440.0 }) {
            const auto drawn = eq.eqCurveMagnitudeDbForTest("eq-band", hz);
            const auto heard = measuredGainDb(graph, hz);
            const auto gap = drawn ? std::abs(*drawn - heard) : 999.0;
            worst = std::max(worst, gap);
            detail << " " << juce::String(hz, 0) << "Hz:drawn="
                   << juce::String(drawn.value_or(-999.0), 2) << "/heard="
                   << juce::String(heard, 2);
        }
        const auto label = juce::String(
            "the EQ card's curve is the band's measured response (") + detail.trim()
            + ")";
        passed = expect(built && worst < 0.6, label.toRawUTF8()) && passed;
    }

    {
        // The same for the low-pass card: its response drawn with the
        // resonance as Q, against the state-variable filter it stands for,
        // below, on and above the cutoff.
        cydaw::app::NodeGraphComponent filter;
        filter.setSize(1000, 700);
        auto built = filter.insertNodeBeforeOutput("lowpass").wasOk()
                     && filter.setInputPortDefaultForTest("lowpass", "cutoff", 1'000.0)
                     && filter.setInputPortDefaultForTest("lowpass", "resonance", 2.0);
        filter.rebuildEmbeddedControlsForTest();
        const auto graph = filter.nativeDspGraph();
        juce::String detail;
        auto worst = 0.0;
        for (const auto hz : { 250.0, 1'000.0, 2'000.0, 4'000.0 }) {
            const auto drawn = filter.filterResponseMagnitudeDbForTest("lowpass", hz);
            const auto heard = measuredGainDb(graph, hz);
            const auto gap = drawn ? std::abs(*drawn - heard) : 999.0;
            worst = std::max(worst, gap);
            detail << " " << juce::String(hz, 0) << "Hz:drawn="
                   << juce::String(drawn.value_or(-999.0), 2) << "/heard="
                   << juce::String(heard, 2);
        }
        const auto label = juce::String(
            "the filter card's curve is the low-pass's measured response (")
            + detail.trim() + ")";
        passed = expect(built && worst < 0.6, label.toRawUTF8()) && passed;
    }

    {
        // And the compressor card: a sine whose peak sits twelve decibels
        // over the threshold, the output peak measured once the detector
        // has settled, against the transfer the curve draws at that input.
        cydaw::app::NodeGraphComponent dynamics;
        dynamics.setSize(1000, 700);
        auto built = dynamics.insertNodeBeforeOutput("compressor").wasOk()
                     && dynamics.setInputPortDefaultForTest("compressor", "threshold", -18.0)
                     && dynamics.setInputPortDefaultForTest("compressor", "ratio", 4.0);
        dynamics.rebuildEmbeddedControlsForTest();
        const auto graph = dynamics.nativeDspGraph();
        juce::String detail;
        auto worst = 0.0;
        for (const auto amplitude : { 0.05f, 0.5f, 1.0f }) {
            const auto inputDb = 20.0 * std::log10(static_cast<double>(amplitude));
            const auto drawn = dynamics.dynamicsResponseOutputDbForTest("compressor",
                                                                        inputDb);
            const auto heard = measuredPeakDb(graph, 1'000.0, amplitude);
            const auto gap = drawn ? std::abs(*drawn - heard) : 999.0;
            worst = std::max(worst, gap);
            detail << " in=" << juce::String(inputDb, 1) << ":drawn="
                   << juce::String(drawn.value_or(-999.0), 2) << "/heard="
                   << juce::String(heard, 2);
        }
        const auto label = juce::String(
            "the compressor card's curve is the measured transfer (")
            + detail.trim() + ")";
        // The detector is a follower with an eight-millisecond attack, and
        // on a sine it settles about a decibel under the true peak; the
        // knee it applies sits that much above the one drawn. A broken
        // threshold or ratio would miss by several.
        passed = expect(built && worst < 1.2, label.toRawUTF8()) && passed;
    }

    {
        // An analysis frame changes how a spectrum card looks and leaves a
        // gain card exactly as it was. Thirty times a second, it used to
        // repaint the whole component — palette and inspector included — to
        // animate the few cards that show the signal.
        cydaw::app::NodeGraphComponent meters;
        meters.setSize(1000, 700);
        auto built = meters.insertNode("gain").wasOk()
                     && meters.insertNode("spectrum").wasOk();
        meters.spreadNodesForTest();
        // Two cards on a two-card grid sit next to each other, and a wide
        // spectrum card overlaps its neighbour. Move the gain well clear so
        // an intersection can only mean the region included it.
        (void) meters.dragNodeForTest("gain", { -300.0f, 320.0f });
        cydaw::engine::AudioAnalysisFrame frame;
        frame.channelCount = 2;
        frame.waveformCount = cydaw::engine::AudioAnalysisFrame::waveformCapacity;
        for (std::size_t index = 0; index < frame.waveformCount; ++index)
            frame.waveform[index] = static_cast<float>(
                0.5 * std::sin(0.1 * static_cast<double>(index)));
        frame.peak = { 0.5f, 0.5f };
        frame.rms = { 0.3f, 0.3f };
        meters.setAnalysisFrame(frame);
        const auto region = meters.analysisRepaintRegionForTest();
        const auto spectrum = meters.nodeBoundsForTest("spectrum");
        const auto gain = meters.nodeBoundsForTest("gain");
        // A gain card carries no signal visual; if it did, this would be
        // the test to say so rather than the region.
        const auto detail = juce::String(" (rects=")
            + juce::String(region.getNumRectangles()) + " spectrum="
            + spectrum.toString() + (region.intersectsRectangle(spectrum)
                                         ? " hit" : " miss")
            + " gain=" + gain.toString()
            + (region.intersectsRectangle(gain) ? " hit" : " miss") + ")";
        const auto label = juce::String(
            "an analysis frame repaints the cards that show it") + detail;
        passed = expect(built && !region.isEmpty()
                            && region.intersectsRectangle(spectrum)
                            && !region.intersectsRectangle(gain),
                        label.toRawUTF8())
                 && passed;
    }

    {
        // A knob notch changes values, not shape. The graph the engine is
        // handed after one must be what a full rebuild would produce, and
        // must not have cost a full rebuild; an insert must be seen without
        // anyone remembering to say so.
        cydaw::app::NodeGraphComponent cached;
        cached.setSize(1000, 700);
        auto built = cached.insertFactoryDevice("warm-channel").wasOk();
        const auto before = cached.exportRebuilds();
        (void) cached.nativeDspGraph();
        const auto primed = cached.exportRebuilds();
        const auto published = cached.graphInterfaceState().parameters;
        built = built && !published.empty();
        if (built) {
            const auto& knob = published.front();
            built = cached.setGraphInterfaceParameter(
                knob.key, knob.minimum + (knob.maximum - knob.minimum) * 0.37);
        }
        const auto patched = cached.nativeDspGraph();
        const auto afterNotch = cached.exportRebuilds();
        const auto fresh = cached.nativeDspGraphWithoutCache();
        (void) cached.insertNode("delay");
        (void) cached.nativeDspGraph();
        const auto afterInsert = cached.exportRebuilds();
        // Three claims, named apart: which one fails says where to look.
        passed = expect(built && primed >= before,
                        "the export cache primes on first use")
                 && passed;
        passed = expect(built && afterNotch == primed,
                        "a knob notch reuses the export")
                 && passed;
        // Which node and which field disagree, in the label: the payload is
        // all floats, so it can be walked the way the plumbing test walks it.
        juce::String where;
        if (patched.nodes.size() != fresh.nodes.size()
            || patched.connections != fresh.connections
            || patched.outputNodeId != fresh.outputNodeId) {
            where = " (shape)";
        } else {
            for (std::size_t index = 0;
                 index < patched.nodes.size() && where.isEmpty(); ++index) {
                const auto& a = patched.nodes[index];
                const auto& b = fresh.nodes[index];
                if (a.id != b.id || a.kind != b.kind) {
                    where = " (node " + juce::String(index) + " identity)";
                    break;
                }
                const auto* fa = reinterpret_cast<const float*>(&a.parameters);
                const auto* fb = reinterpret_cast<const float*>(&b.parameters);
                constexpr auto fields = sizeof(a.parameters) / sizeof(float);
                for (std::size_t field = 0; field < fields; ++field)
                    if (fa[field] != fb[field]) {
                        where = " (node " + juce::String(index) + " id "
                                + juce::String(static_cast<juce::int64>(a.id))
                                + " kind " + juce::String(static_cast<int>(a.kind))
                                + " field " + juce::String(field) + ": "
                                + juce::String(fa[field]) + " vs "
                                + juce::String(fb[field]) + ")";
                        break;
                    }
            }
        }
        const auto label = juce::String("a patched export equals a rebuilt one")
                           + where;
        passed = expect(built && patched == fresh, label.toRawUTF8())
                 && passed;
        passed = expect(built && afterInsert == primed + 1,
                        "an insert rebuilds the export")
                 && passed;
    }

    {
        // A wavetable file is a row of single cycles. Reading it must find
        // how long a cycle is, and then travel the whole file: the first
        // eight cycles of a sixty-four frame table are nearly the same, so
        // taking them from the head gives a table whose morph does nothing —
        // which looks entirely correct until it is played.
        constexpr std::size_t cycle = 2'048;
        constexpr std::size_t frames = 64;
        std::vector<float> file(cycle * frames);
        for (std::size_t frame = 0; frame < frames; ++frame)
            for (std::size_t index = 0; index < cycle; ++index) {
                // Frame n holds the nth harmonic alone, so which frames were
                // read can be recovered exactly from the harmonics.
                const auto phase = 2.0 * juce::MathConstants<double>::pi
                                   * static_cast<double>(index)
                                   / static_cast<double>(cycle);
                file[frame * cycle + index] = static_cast<float>(
                    std::sin(phase * static_cast<double>(frame + 1)));
            }
        const auto swept = cydaw::engine::wavetableShapeFromSamples(
            file.data(), file.size());
        const auto loudest = [](const auto& row) {
            return static_cast<std::size_t>(std::distance(
                row.begin(), std::max_element(row.begin(), row.end())));
        };
        // Eight frames taken evenly across sixty-four: the first and the
        // last of the file, and rising in between.
        const auto readWholeFile = swept.ok && swept.frameLength == cycle
                                   && swept.framesFound == frames
                                   && !swept.singleCycle
                                   && loudest(swept.shape.harmonics.front())
                                          == 0
                                   && loudest(swept.shape.harmonics.back())
                                          == 39
                                   && loudest(swept.shape.harmonics[1])
                                          > loudest(
                                              swept.shape.harmonics[0]);

        // A file that is one cycle has no sweep of its own, so repeating it
        // eight times would leave the position knob with nothing to do. It
        // becomes that cycle at rising brightness instead.
        std::vector<float> single(1'000);
        for (std::size_t index = 0; index < single.size(); ++index) {
            const auto phase = 2.0 * juce::MathConstants<double>::pi
                               * static_cast<double>(index)
                               / static_cast<double>(single.size());
            single[index] = static_cast<float>(std::sin(phase)
                                               + 0.5 * std::sin(phase * 3.0));
        }
        const auto one = cydaw::engine::wavetableShapeFromSamples(
            single.data(), single.size());
        const auto counted = [](const auto& row) {
            return std::count_if(row.begin(), row.end(),
                                 [](const float value) {
                                     return std::abs(value) > 0.001f;
                                 });
        };
        const auto ramped = one.ok && one.singleCycle
                            && one.framesFound == 1
                            && counted(one.shape.harmonics.front()) == 1
                            && counted(one.shape.harmonics.back()) == 2;

        passed = expect(readWholeFile && ramped,
                        "a wavetable file is read across its whole sweep")
                 && passed;
    }

    {
        // Two strokes are two edits. The history merges parameter changes
        // made within a fraction of a second — a rule meant for wheels and
        // typed values, which have no gesture to bound them. A stroke has
        // one, so undoing after two strokes must leave the first one
        // standing rather than clearing both.
        cydaw::app::NodeGraphComponent strokes;
        strokes.setSize(1000, 700);
        auto built = strokes.insertNode("wavetable").wasOk();
        const auto start = strokes.nodeWavetable("wavetable");
        auto first = start;
        first.harmonics[2] = {};
        first.harmonics[2][0] = 1.0f;
        built = built && strokes.drawWavetableForTest("wavetable", first);
        auto second = first;
        second.harmonics[5] = {};
        second.harmonics[5][0] = 1.0f;
        built = built && strokes.drawWavetableForTest("wavetable", second);
        strokes.undoForTest();
        passed = expect(built
                            && strokes.nodeWavetable("wavetable") == first,
                        "two strokes are two undos")
                 && passed;
    }

    {
        // The classic shapes have to be reachable without drawing them. And
        // filling every frame with one family must still leave the morph
        // something to travel through: a table of eight identical frames
        // would make the position knob do nothing.
        cydaw::app::ModuleWavetableComponent editor;
        editor.setSize(240, 140);
        auto notified = 0;
        editor.onShapeChanged = [&notified](const auto&) { ++notified; };
        editor.applyPreset(
            cydaw::app::ModuleWavetableComponent::Preset::square, true);
        const auto& square = editor.shape();
        const auto nonZero = [](const auto& row) {
            return std::count_if(row.begin(), row.end(),
                                 [](const float value) {
                                     return value != 0.0f;
                                 });
        };
        // A square has no even harmonics, whichever frame it is in.
        auto evenSilent = true;
        for (const auto& row : square.harmonics)
            for (std::size_t harmonic = 1;
                 harmonic < cydaw::engine::WavetableShape::harmonicCount;
                 harmonic += 2)
                evenSilent = evenSilent && row[harmonic] == 0.0f;
        const auto darkest = nonZero(square.harmonics.front());
        const auto brightest = nonZero(square.harmonics.back());

        // One frame only leaves its neighbours alone.
        editor.selectFrame(2);
        editor.applyPreset(
            cydaw::app::ModuleWavetableComponent::Preset::sine, false);
        const auto& mixed = editor.shape();
        const auto sineOnly = nonZero(mixed.harmonics[2]) == 1
                              && nonZero(mixed.harmonics[3]) > 1;

        passed = expect(notified == 2 && evenSilent && darkest == 1
                            && brightest > darkest && sineOnly,
                        "the classic shapes fill a table without flattening "
                        "it")
                 && passed;
    }

    {
        // A cycle drawn with a mouse has corners, and a corner is an infinite
        // number of harmonics. Storing what was drawn would alias; storing
        // its harmonics cannot. A drawn square must come back as a square
        // built from the harmonics it is allowed to have.
        std::array<float, 256> drawn {};
        for (std::size_t index = 0; index < drawn.size(); ++index)
            drawn[index] = index < drawn.size() / 2 ? 1.0f : -1.0f;
        const auto harmonics = cydaw::engine::analyseWavetableFrame(
            drawn.data(), drawn.size());
        // A square is odd harmonics at 4/(pi n): the first is about 1.27, the
        // second is nothing, the third about a third of the first.
        const auto first = harmonics[0];
        const auto second = std::abs(harmonics[1]);
        const auto third = harmonics[2];
        std::array<float, 256> rebuilt {};
        cydaw::engine::renderWavetableFrame(harmonics, rebuilt.data(),
                                            rebuilt.size());
        // Rebuilt from a bounded number of harmonics it overshoots at the
        // step — Gibbs — but it must stay within the range a sample can hold.
        auto bounded = true;
        for (const auto value : rebuilt)
            bounded = bounded && std::abs(value) <= 1.0001f;
        passed = expect(std::abs(first - 1.27f) < 0.05f && second < 0.02f
                            && std::abs(third - first / 3.0f) < 0.05f
                            && bounded,
                        "a drawn cycle is stored as harmonics it can hold")
                 && passed;
    }

    {
        // An instrument, then a chain behind it — the thing an Ableton or
        // Bitwig user does without thinking. The polyphonic renderer plays
        // the voice per note; what follows has to reach the track as a plain
        // audio graph, and the voice must not appear in it as well or the
        // oscillator and its envelope would be rendered a second time over
        // the summed output.
        cydaw::app::NodeGraphComponent chain;
        chain.setSize(1000, 700);
        auto built = chain.insertFactoryDevice("table-synth").wasOk();
        built = built
                && chain.insertNodeOnConnection("voice-filter", "master",
                                                "delay")
                       .wasOk();
        const auto native = chain.nativeDspGraph();
        using Kind = cydaw::engine::NativeDspNodeKind;
        const auto counts = [&native](const Kind kind) {
            return std::count_if(native.nodes.begin(), native.nodes.end(),
                                 [kind](const auto& node) {
                                     return node.kind == kind;
                                 });
        };
        passed = expect(built
                            && counts(Kind::delay) == 1
                            && counts(Kind::input) == 1
                            && counts(Kind::wavetable) == 0
                            && counts(Kind::gain) == 0,
                        "a chain behind an instrument reaches the engine once")
                 && passed;
    }

    {
        // A fader notch changes two values, not the shape of the interface,
        // so it copies them onto the parameters that carry them instead of
        // rebuilding the published list from every card. The shortcut is only
        // worth having if it lands in the same place as the long way round: a
        // published value left behind would show up first in a saved project.
        cydaw::app::NodeGraphComponent faders;
        faders.setSize(1000, 700);
        (void) faders.insertNode("drive");
        (void) faders.insertNode("lowpass");
        for (auto index = 0; index < 12; ++index)
            (void) faders.insertNode("gain");
        faders.setEffectParameters(14.0f, 3'200.0f, true);
        const auto quick = faders.serialise();
        faders.resynchroniseGraphInterfaceForTest();
        const auto full = faders.serialiseWithoutCache();
        passed = expect(cydaw::app::jsonValuesMatch(quick, full)
                            && juce::JSON::toString(quick).contains("3200"),
                        "a fader notch lands where a full rebuild would")
                 && passed;
    }

    {
        // The point of the cache is not that a re-description is equal but
        // that it never happens: the untouched cards hand back the very
        // objects they produced before. That sharing is what makes an edit on
        // a large patch cheap, and what lets the undo history hold ninety-six
        // states without ninety-six copies of every node.
        cydaw::app::NodeGraphComponent shared;
        shared.setSize(1000, 700);
        for (auto index = 0; index < 40; ++index)
            (void) shared.insertNode("gain");
        const auto bodies = [](const juce::var& state) {
            std::set<const void*> objects;
            if (auto* root = state.getDynamicObject())
                if (const auto* array
                    = root->getProperty("nodes").getArray())
                    for (const auto& node : *array)
                        if (auto* body = node.getDynamicObject())
                            objects.insert(body);
            return objects;
        };
        const auto before = bodies(shared.serialise());
        (void) shared.insertNode("drive");
        const auto after = bodies(shared.serialise());
        auto reused = 0;
        for (const auto* body : before)
            if (after.contains(body))
                ++reused;
        passed = expect(before.size() >= 40
                            && reused >= static_cast<int>(before.size()) - 1,
                        "adding a card leaves the others' descriptions alone")
                 && passed;
    }

    {
        // Each node keeps what it last serialised to, so that changing one
        // card on a three-hundred-card patch does not re-describe the other
        // two hundred and ninety-nine. A cache that goes stale produces a
        // wrong document — a bad save, a bad undo — and says nothing at the
        // time. After every kind of edit, what the graph serialises must
        // equal what it would serialise with every cache dropped.
        cydaw::app::NodeGraphComponent cached;
        cached.setSize(1000, 700);
        auto agrees = true;
        auto steps = 0;
        const auto check = [&cached, &agrees, &steps] {
            ++steps;
            const auto withCache = cached.serialise();
            const auto fresh = cached.serialiseWithoutCache();
            agrees = agrees
                     && cydaw::app::jsonValuesMatch(withCache, fresh);
        };
        check();
        (void) cached.insertNode("gain");
        check();
        (void) cached.insertNode("drive");
        check();
        (void) cached.connectNodePorts("gain", "audio-out", "drive",
                                       "audio-in", true);
        check();
        (void) cached.dragNodeForTest("gain", { 40.0f, 25.0f });
        check();
        (void) cached.setNodeCollapsed("drive", true);
        check();
        // A real published key: the bindings are "node.<id>.<suffix>", and a
        // made-up one changes nothing, which makes the step prove nothing.
        {
            const auto published = cached.graphInterfaceState().parameters;
            if (!published.empty())
                (void) cached.setGraphInterfaceParameter(
                    published.front().key,
                    published.front().minimum
                        + (published.front().maximum
                           - published.front().minimum) * 0.31);
        }
        check();
        cached.undoForTest();
        check();
        cached.redoForTest();
        check();
        // The presentation is cached per entry too: a change to how a
        // parameter is exposed, not only to its value, has to show.
        {
            const auto published = cached.graphInterfaceState().parameters;
            if (!published.empty())
                (void) cached.setPublishedParameterExposure(
                    published.front().key, !published.front().showInDevice,
                    published.front().publishToHost);
        }
        check();
        passed = expect(steps == 10 && agrees,
                        "a node's cached description never goes stale")
                 && passed;
    }

    {
        // Down and up inside the search box move the highlight over entries
        // and step over headings, which is what makes the palette usable
        // without leaving the keyboard. Built and announced without ever
        // being tried.
        cydaw::app::NodeGraphComponent keys;
        keys.setSize(1000, 700);
        juce::StringArray rows;
        rows.addLines(keys.paletteOutline());
        rows.removeEmptyStrings();
        auto entryRows = 0;
        for (const auto& line : rows)
            if (line.trimStart().startsWith("- "))
                ++entryRows;

        auto landedOnHeading = false;
        auto wentBackwards = false;
        std::set<juce::String> visited;
        auto previous = keys.paletteSelectedRow();
        const auto start = previous;
        // The row already highlighted counts: the walk starts on it.
        visited.insert(keys.paletteRowKey(start));
        for (auto step = 0; step < entryRows + 4; ++step) {
            keys.movePaletteSelection(1);
            const auto row = keys.paletteSelectedRow();
            landedOnHeading = landedOnHeading
                              || keys.paletteRowKey(row).isEmpty();
            wentBackwards = wentBackwards || row < previous;
            visited.insert(keys.paletteRowKey(row));
            previous = row;
        }
        // The last entry is the floor: pressing down there keeps it.
        const auto atBottom = keys.paletteSelectedRow();
        keys.movePaletteSelection(1);
        const auto stillAtBottom = keys.paletteSelectedRow() == atBottom;
        for (auto step = 0; step < entryRows + 4; ++step)
            keys.movePaletteSelection(-1);
        const auto backAtTop = keys.paletteSelectedRow() == start;

        passed = expect(entryRows > 20 && !landedOnHeading && !wentBackwards
                            && static_cast<int>(visited.size()) == entryRows
                            && stillAtBottom && backAtTop,
                        "the palette arrows walk every entry and no heading")
                 && passed;
    }

    {
        // The palette became a tree, and everything that turned a row number
        // into a catalogue entry was rewired — including what a drag onto the
        // canvas hands over. A heading must hand over nothing; an entry must
        // hand over something the canvas accepts. Folded and unfolded, since
        // folding is what changes which row is which.
        cydaw::app::NodeGraphComponent dragged;
        dragged.setSize(1000, 700);
        auto headingsGiveNothing = true;
        auto entriesGiveSomething = true;
        auto entries = 0;
        auto headings = 0;
        for (auto pass = 0; pass < 2; ++pass) {
            if (pass == 1)
                dragged.togglePaletteFold("DSP");
            juce::StringArray lines;
            lines.addLines(dragged.paletteOutline());
            lines.removeEmptyStrings();
            for (int row = 0; row < lines.size(); ++row) {
                const auto text = lines[row].trimStart();
                const auto payload = dragged.paletteDragPayload(row);
                if (text.startsWith("- ")) {
                    ++entries;
                    entriesGiveSomething = entriesGiveSomething
                        && payload.startsWith("cydaw-node:")
                        && dragged.acceptsDragPayload(payload);
                } else {
                    ++headings;
                    headingsGiveNothing = headingsGiveNothing
                                          && payload.isEmpty();
                }
            }
        }
        passed = expect(entries > 20 && headings > 4 && headingsGiveNothing
                            && entriesGiveSomething,
                        "every palette entry can be dragged and no heading can")
                 && passed;
    }

    {
        // The palette used to be a flat list of seventy entries that repeated
        // its category on every row. It is a tree now, and a tree can lie:
        // a heading can claim a count it does not hold, a search can leave a
        // match buried under a fold the user left closed.
        cydaw::app::NodeGraphComponent palette;
        juce::StringArray lines;
        lines.addLines(palette.paletteOutline());
        lines.removeEmptyStrings();
        const auto depthOf = [](const juce::String& line) {
            return (line.length() - line.trimStart().length()) / 2;
        };
        const auto entryCount = [](const juce::StringArray& rows) {
            auto found = 0;
            for (const auto& line : rows)
                if (line.trimStart().startsWith("- "))
                    ++found;
            return found;
        };

        auto countsAgree = true;
        auto headings = 0;
        for (int index = 0; index < lines.size(); ++index) {
            const auto text = lines[index].trimStart();
            if (!text.startsWith("v "))
                continue;
            ++headings;
            const auto depth = depthOf(lines[index]);
            const auto declared = text.fromLastOccurrenceOf("(", false, false)
                                      .upToFirstOccurrenceOf(")", false, false)
                                      .getIntValue();
            auto found = 0;
            for (int scan = index + 1; scan < lines.size(); ++scan) {
                if (depthOf(lines[scan]) <= depth)
                    break;
                if (lines[scan].trimStart().startsWith("- "))
                    ++found;
            }
            countsAgree = countsAgree && found == declared;
        }
        passed = expect(headings > 0 && entryCount(lines) > 0 && countsAgree,
                        "every palette heading counts the entries it holds")
                 && passed;

        palette.setPaletteSearch("gain");
        juce::StringArray searched;
        searched.addLines(palette.paletteOutline());
        searched.removeEmptyStrings();
        auto everyFoldOpen = true;
        auto matchVisible = false;
        for (const auto& line : searched) {
            const auto text = line.trimStart();
            if (text.startsWith("> "))
                everyFoldOpen = false;
            if (text.startsWith("- ") && text.containsIgnoreCase("gain"))
                matchVisible = true;
        }
        passed = expect(matchVisible && everyFoldOpen
                            && entryCount(searched) < entryCount(lines),
                        "a palette search narrows the tree and opens its folds")
                 && passed;

        palette.setPaletteSearch({});
        juce::String firstFold;
        for (const auto& line : lines)
            if (const auto text = line.trimStart(); text.startsWith("v ")) {
                firstFold = text.substring(2)
                                .upToLastOccurrenceOf(" (", false, false);
                break;
            }
        palette.togglePaletteFold(firstFold);
        juce::StringArray folded;
        folded.addLines(palette.paletteOutline());
        folded.removeEmptyStrings();
        auto headingKept = false;
        for (const auto& line : folded)
            if (line.trimStart().startsWith("> " + firstFold + " ("))
                headingKept = true;
        passed = expect(firstFold.isNotEmpty() && headingKept
                            && entryCount(folded) < entryCount(lines),
                        "folding a palette heading hides its entries")
                 && passed;
    }

    {
        cydaw::app::NodeGraphComponent graph;
        const cydaw::graph::PluginIdentity expectedExportIdentity {
            "CyDAW Export Test",
            "CyDAW Tests",
            "CYTS",
            "EX01",
            { 2, 3, 4 }
        };
        graph.setPluginExportIdentity(expectedExportIdentity);
        auto state = graph.serialise();
        const auto nativeGraph = graph.nativeDspGraph();
        passed = expect(nativeGraph.nodes.size() == 2
                            && nativeGraph.connections.size() == 1
                            && nativeGraph.outputNodeId != 0,
                        "factory visual graph exports native audio DAG") && passed;
        auto* object = state.getDynamicObject();
        passed = expect(object != nullptr, "node graph serialises as versioned object") && passed;
        if (object != nullptr) {
            passed = expect(static_cast<int>(object->getProperty("schema"))
                                == cydaw::app::nodeGraphStateSchema,
                            "node graph schema version") && passed;
            const auto* nodes = object->getProperty("nodes").getArray();
            const auto* connections = object->getProperty("connections").getArray();
            const auto* modules = object->getProperty("modules").getArray();
            const auto* presentation = object->getProperty("presentation")
                                           .getDynamicObject();
            const auto* presentationControls = presentation != nullptr
                                                   ? presentation
                                                         ->getProperty("controls")
                                                         .getArray()
                                                   : nullptr;
            passed = expect(presentation != nullptr
                                && presentationControls != nullptr
                                && !presentationControls->isEmpty(),
                            "graph/device presentation persists as reusable UI")
                     && passed;
            passed = expect(std::abs(static_cast<double>(
                                       object->getProperty("viewZoom")) - 1.0)
                                       < 0.001,
                            "node graph persists its zoomable view transform")
                     && passed;
            passed = expect(nodes != nullptr && nodes->size() == 2,
                            "factory graph node persistence") && passed;
            if (nodes != nullptr && !nodes->isEmpty()) {
                auto extendedState = juce::JSON::parse(
                    juce::JSON::toString(state, false));
                auto* extendedRoot = extendedState.getDynamicObject();
                auto* extendedNodes = extendedRoot != nullptr
                    ? extendedRoot->getProperty("nodes").getArray() : nullptr;
                if (extendedNodes != nullptr && !extendedNodes->isEmpty())
                    if (auto* firstNode = extendedNodes->getReference(0)
                                              .getDynamicObject()) {
                        firstNode->setProperty("x", -0.75);
                        firstNode->setProperty("y", 1.5);
                    }
                cydaw::app::NodeGraphComponent extendedGraph;
                extendedGraph.restore(extendedState);
                const auto extendedRoundTrip = extendedGraph.serialise();
                const auto* extendedRoundTripRoot =
                    extendedRoundTrip.getDynamicObject();
                const auto* extendedRoundTripNodes =
                    extendedRoundTripRoot != nullptr
                        ? extendedRoundTripRoot->getProperty("nodes").getArray()
                        : nullptr;
                const auto* extendedFirst =
                    extendedRoundTripNodes != nullptr
                            && !extendedRoundTripNodes->isEmpty()
                        ? extendedRoundTripNodes->getReference(0)
                              .getDynamicObject()
                        : nullptr;
                passed = expect(
                             extendedFirst != nullptr
                                 && std::abs(static_cast<double>(
                                        extendedFirst->getProperty("x"))
                                             + 0.75) < 0.001
                                 && std::abs(static_cast<double>(
                                        extendedFirst->getProperty("y"))
                                             - 1.5) < 0.001,
                             "schema-22 graph positions persist beyond the visible canvas")
                         && passed;
            }
            passed = expect(connections != nullptr && connections->size() == 1,
                            "typed graph connection persistence") && passed;
            const juce::DynamicObject* polyModule = nullptr;
            const juce::DynamicObject* lfoModule = nullptr;
            if (modules != nullptr)
                for (const auto& moduleValue : *modules)
                    if (const auto* candidate = moduleValue.getDynamicObject();
                        candidate != nullptr
                        && candidate->getProperty("key").toString()
                               == "poly-sine") {
                        polyModule = candidate;
                    } else if (candidate != nullptr
                               && candidate->getProperty("key").toString()
                                      == "lfo")
                        lfoModule = candidate;
            const auto* polyChildren = polyModule != nullptr
                                           ? polyModule->getProperty("nodes")
                                                 .getArray()
                                           : nullptr;
            passed = expect(polyModule != nullptr
                                && polyChildren != nullptr
                                && polyChildren->size() == 7
                                && static_cast<bool>(polyModule->getProperty(
                                    "hasInput"))
                                && static_cast<int>(polyModule->getProperty(
                                    "inputKind")) == 1
                                && static_cast<int>(polyModule->getProperty(
                                    "outputKind")) == 0
                                && polyModule->getProperty("role").toString()
                                       == "instrument"
                                && polyModule->getProperty("tags").getArray()
                                       != nullptr,
                            "Poly Sine persists as an inspectable MIDI-to-audio subgraph")
                     && passed;
            const auto* lfoChildren = lfoModule != nullptr
                                          ? lfoModule->getProperty("nodes")
                                                .getArray()
                                          : nullptr;
            auto lfoHasPrimitiveCore = false;
            if (lfoChildren != nullptr)
                for (const auto& childValue : *lfoChildren)
                    if (const auto* child = childValue.getDynamicObject();
                        child != nullptr
                        && child->getProperty("type").toString()
                               == "lfo-core")
                        lfoHasPrimitiveCore = true;
            passed = expect(lfoModule != nullptr
                                && lfoChildren != nullptr
                                && lfoChildren->size() == 2
                                && lfoHasPrimitiveCore
                                && static_cast<bool>(
                                    lfoModule->getProperty("factory"))
                                && !static_cast<bool>(
                                    lfoModule->getProperty("locked"))
                                && lfoModule->getProperty("role").toString()
                                       == "modulator",
                            "LFO is an editable reusable subgraph built from a primitive core")
                     && passed;
            const std::set<juce::String> expectedFactoryLibrary {
                "lfo", "poly-sine", "poly-pad", "poly-bass",
                "reverb-rack", "vocal-strip", "clean-channel",
                "warm-drive", "echo-space", "stereo-finisher",
                "parallel-drive", "arp-motion", "note-shaper",
                "unipolar-lfo", "audio-follower", "scope-suite",
                "spectrometer-analyzer", "drum-punch", "wide-echo", "dark-room", "mono-focus",
                "master-guard", "tremolo", "auto-pan",
                "dynamic-filter", "octave-up", "bass-zone",
                "poly-pluck", "poly-lead", "poly-keys",
                "ambient-glass", "electric-organ", "percussive-bell"
            };
            std::set<juce::String> discoveredFactoryLibrary;
            auto factoryMetadataComplete = modules != nullptr;
            const std::set<juce::String> completeInstrumentExamples {
                "poly-keys", "ambient-glass", "electric-organ",
                "percussive-bell"
            };
            auto completeInstrumentExamplesValid = modules != nullptr;
            std::size_t discoveredCompleteInstrumentExamples {};
            if (modules != nullptr)
                for (const auto& moduleValue : *modules)
                    if (const auto* candidate = moduleValue.getDynamicObject();
                        candidate != nullptr
                        && static_cast<bool>(candidate->getProperty("factory"))) {
                        const auto key = candidate->getProperty("key")
                                             .toString();
                        discoveredFactoryLibrary.insert(key);
                        factoryMetadataComplete = factoryMetadataComplete
                            && candidate->getProperty("category").toString()
                                   .isNotEmpty()
                            && candidate->getProperty("author").toString()
                                   .isNotEmpty()
                            && candidate->getProperty("description").toString()
                                   .isNotEmpty()
                            && candidate->getProperty("tags").getArray()
                                   != nullptr;
                        if (completeInstrumentExamples.contains(key)) {
                            ++discoveredCompleteInstrumentExamples;
                            const auto* exampleNodes = candidate
                                ->getProperty("nodes").getArray();
                            const auto* exampleInterface = candidate
                                ->getProperty("interface").getDynamicObject();
                            const auto* exampleControls = exampleInterface != nullptr
                                ? exampleInterface->getProperty("controls").getArray()
                                : nullptr;
                            completeInstrumentExamplesValid =
                                completeInstrumentExamplesValid
                                && candidate->getProperty("role").toString()
                                       == "instrument"
                                && static_cast<bool>(candidate->getProperty("hasInput"))
                                && static_cast<int>(candidate->getProperty("inputKind")) == 1
                                && static_cast<int>(candidate->getProperty("outputKind")) == 0
                                && exampleNodes != nullptr
                                && exampleNodes->size() >= 10
                                && exampleControls != nullptr
                                && exampleControls->size() >= 3;
                        }
                    }
            passed = expect(
                         discoveredFactoryLibrary == expectedFactoryLibrary
                             && factoryMetadataComplete,
                         "factory Graph library exposes categorized instruments, effects, MIDI tools, modulators and analyzers")
                     && passed;
            passed = expect(
                         completeInstrumentExamplesValid
                             && discoveredCompleteInstrumentExamples
                                    == completeInstrumentExamples.size(),
                         "complete factory instruments remain editable MIDI-to-audio graphs with authored control facades")
                     && passed;
            if (nodes != nullptr && !nodes->isEmpty()) {
                const auto* firstNode = nodes->getReference(0).getDynamicObject();
                passed = expect(firstNode != nullptr
                                    && firstNode->getProperty("parameters")
                                           .getDynamicObject() != nullptr
                                    && firstNode->getProperty("interface")
                                           .getDynamicObject() != nullptr
                                    && firstNode->hasProperty("alias")
                                    && firstNode->hasProperty("inputChannels")
                                    && firstNode->hasProperty("outputChannels")
                                    && firstNode->hasProperty(
                                        "interfaceLayoutOverridden"),
                                "per-node parameters, local alias and audio format persist") && passed;
            }
        }

        if (object != nullptr) {
            object->setProperty("graphName", "Exportable Stereo Tool");
            object->setProperty("graphDescription",
                                "Reusable graph metadata for CyDAW and VST3");
            juce::Array<juce::var> graphTags;
            graphTags.add("production");
            graphTags.add("stereo");
            object->setProperty("graphTags", graphTags);
            object->setProperty("selectionPanelVisible", false);
            object->setProperty("configurationPanelVisible", true);
            object->setProperty("selectionPanelWidth", 318);
            object->setProperty("configurationPanelWidth", 372);
            if (auto* editableNodes = object->getProperty("nodes").getArray();
                editableNodes != nullptr && !editableNodes->isEmpty())
                if (auto* editableNode = editableNodes->getReference(0)
                                             .getDynamicObject())
                    editableNode->setProperty(
                        "description",
                        "Editable node tooltip persisted with the graph asset");
        }

        const auto json = juce::JSON::toString(state, true);
        cydaw::app::NodeGraphComponent restored;
        restored.restore(juce::JSON::parse(json));
        const auto restoredState = restored.serialise();
        const auto* restoredObject = restoredState.getDynamicObject();
        const auto* restoredNodes = restoredObject != nullptr
                                        ? restoredObject->getProperty("nodes")
                                              .getArray()
                                        : nullptr;
        const auto* restoredFirstNode = restoredNodes != nullptr
                                             && !restoredNodes->isEmpty()
                                             ? restoredNodes->getReference(0)
                                                   .getDynamicObject()
                                             : nullptr;
        passed = expect(restoredObject != nullptr
                            && restoredNodes != nullptr
                            && restoredNodes->size() == 2
                            && restoredObject->getProperty("presentation")
                                   .getDynamicObject() != nullptr,
                        "node graph JSON round trip") && passed;
        passed = expect(
                     restoredObject != nullptr
                         && restoredObject->getProperty("graphName").toString()
                                == "Exportable Stereo Tool"
                         && restoredObject->getProperty("graphDescription")
                                .toString()
                                == "Reusable graph metadata for CyDAW and VST3"
                         && restoredObject->getProperty("graphTags").getArray()
                                != nullptr
                         && restoredObject->getProperty("graphTags").getArray()
                                ->size() == 2
                         && !static_cast<bool>(restoredObject->getProperty(
                             "selectionPanelVisible"))
                         && static_cast<bool>(restoredObject->getProperty(
                             "configurationPanelVisible"))
                         && static_cast<int>(restoredObject->getProperty(
                                "selectionPanelWidth")) == 318
                         && static_cast<int>(restoredObject->getProperty(
                                "configurationPanelWidth")) == 372
                         && restoredFirstNode != nullptr
                         && restoredFirstNode->getProperty("description")
                                .toString()
                                == "Editable node tooltip persisted with the graph asset",
                     "modular Graph editor metadata and panel layout round trip")
                 && passed;

        const auto restoredExportIdentity = restored.pluginExportIdentity();
        const auto restoredExportPlan = restored.planVst3Export();
        const auto exportManifest = restored.createVst3ExportManifest();
        const auto* manifestRoot = exportManifest.getDynamicObject();
        const auto* manifestIdentity = manifestRoot != nullptr
                                           ? manifestRoot->getProperty("identity")
                                                 .getDynamicObject()
                                           : nullptr;
        const auto* manifestContract = manifestRoot != nullptr
                                           ? manifestRoot->getProperty("contract")
                                                 .getDynamicObject()
                                           : nullptr;
        const auto* manifestEditorState = manifestRoot != nullptr
                                              ? manifestRoot
                                                    ->getProperty("graphEditorState")
                                                    .getDynamicObject()
                                              : nullptr;
        const auto* manifestBuses = manifestContract != nullptr
                                        ? manifestContract->getProperty("buses")
                                              .getArray()
                                        : nullptr;
        const auto* manifestParameters = manifestContract != nullptr
                                             ? manifestContract
                                                   ->getProperty("parameters")
                                                   .getArray()
                                             : nullptr;
        passed = expect(
                     restoredExportIdentity.productName
                             == expectedExportIdentity.productName
                         && restoredExportIdentity.companyName
                                == expectedExportIdentity.companyName
                         && restoredExportIdentity.manufacturerCode
                                == expectedExportIdentity.manufacturerCode
                         && restoredExportIdentity.pluginCode
                                == expectedExportIdentity.pluginCode
                         && restoredExportIdentity.version
                                == expectedExportIdentity.version,
                     "VST3 export identity persists with the editable graph")
                 && passed;
        passed = expect(
                     restoredExportPlan.ok()
                         && restoredExportPlan.plan.has_value()
                         && manifestRoot != nullptr
                         && static_cast<int>(manifestRoot->getProperty("schema"))
                                == 1
                         && manifestIdentity != nullptr
                         && manifestIdentity->getProperty("productName").toString()
                                == "CyDAW Export Test"
                         && manifestContract != nullptr
                         && manifestBuses != nullptr
                         && !manifestBuses->isEmpty()
                         && manifestParameters != nullptr
                         && manifestEditorState != nullptr
                         && static_cast<int>(manifestEditorState->getProperty(
                                "schema")) == cydaw::app::nodeGraphStateSchema,
                     "VST3 manifest contains the validated runtime contract and full editor state")
                 && passed;
        const auto runtimeState = restored.createVst3RuntimeState();
        const auto* runtimeRoot = runtimeState.getDynamicObject();
        const auto generatedProject = tempRoot.getChildFile(
            "Generated Graph VST3 Project");
        const auto projectResult = restored.generateVst3Project(
            generatedProject);
        // The native project: the fixed plan as sources, its metadata, the
        // build that picks a format, the editable graph in the manifest.
        const auto generatedDefinition = generatedProject.getChildFile(
            "generated/Definition.cpp");
        const auto generatedMetadata = generatedProject.getChildFile(
            "generated/Metadata.json");
        const auto generatedCmake = generatedProject.getChildFile(
            "CMakeLists.txt");
        const auto generatedManifest = generatedProject.getChildFile(
            "GraphManifest.cydawvst3.json");
        const auto definitionText = generatedDefinition.loadFileAsString();
        const auto metadata = juce::JSON::parse(generatedMetadata.loadFileAsString());
        passed = expect(
                     runtimeRoot != nullptr
                         && static_cast<int>(runtimeRoot->getProperty("schema"))
                                == 2
                         && runtimeRoot->getProperty("native")
                                .getDynamicObject() != nullptr
                         && runtimeRoot->getProperty("editor")
                                .getDynamicObject() != nullptr
                         && projectResult.wasOk()
                         && generatedDefinition.existsAsFile()
                         && generatedCmake.existsAsFile()
                         && generatedManifest.existsAsFile()
                         && definitionText.contains("Definition makeDefinition()")
                         && definitionText.contains("engine::NativeDspGraph makeAudioGraph()")
                         && definitionText.contains("engine::PolySynthGraphPlan makeInstrumentPlan()")
                         && metadata.getProperty("parameters", {}).isArray()
                         && metadata.getProperty("buses", {}).isArray()
                         && generatedCmake.loadFileAsString().contains(
                             "src/native_plugin/NativeFormats.cmake")
                         && juce::JSON::parse(
                                generatedManifest.loadFileAsString())
                                .getDynamicObject() != nullptr,
                     "Graph export generates a reproducible native project: its plan as sources, the editable graph in its manifest")
                 && passed;

        cydaw::app::NodeGraphComponent browserInsertGraph;
        const auto cleanPreset = browserInsertGraph.loadFactoryPreset(1);
        const auto insertDelay = browserInsertGraph.insertFactoryDevice("delay");
        const auto browserInsertState = browserInsertGraph.serialise();
        const auto* browserInsertRoot = browserInsertState.getDynamicObject();
        const auto* browserInsertNodes = browserInsertRoot != nullptr
                                             ? browserInsertRoot
                                                   ->getProperty("nodes").getArray()
                                             : nullptr;
        const auto* browserInsertConnections = browserInsertRoot != nullptr
                                                   ? browserInsertRoot
                                                         ->getProperty("connections")
                                                         .getArray()
                                                   : nullptr;
        passed = expect(cleanPreset.wasOk() && insertDelay.wasOk()
                            && browserInsertNodes != nullptr
                            && browserInsertNodes->size() == 5
                            && browserInsertConnections != nullptr
                            && browserInsertConnections->size() == 4,
                        "browser device insertion splices a factory effect into the audio DAG")
                 && passed;

        cydaw::app::NodeGraphComponent independentInstances;
        const auto firstReverb = independentInstances.insertFactoryDevice(
            "reverb-rack");
        const auto secondReverb = independentInstances.insertFactoryDevice(
            "reverb-rack");
        const auto instanceInterface = independentInstances.graphInterfaceState();
        std::vector<juce::String> reverbMixKeys;
        for (const auto& parameter : instanceInterface.parameters)
            if (parameter.key.endsWith("reverb.mix"))
                reverbMixKeys.push_back(parameter.key);
        auto independentValues = firstReverb.wasOk() && secondReverb.wasOk()
            && reverbMixKeys.size() == 2
            && reverbMixKeys[0] != reverbMixKeys[1]
            && independentInstances.setGraphInterfaceParameter(
                reverbMixKeys[0], 0.12)
            && independentInstances.setGraphInterfaceParameter(
                reverbMixKeys[1], 0.76);
        const auto independentDefinitions = independentInstances.graphDefinitions();
        cydaw::graph::GraphLibrary independentLibrary;
        const auto independentInstall = independentLibrary.installBatch(
            independentDefinitions);
        const auto independentCompile = independentInstall.ok()
            ? cydaw::engine::compileGraphDefinitionToNativeDsp(
                  independentLibrary, independentDefinitions.back().reference)
            : cydaw::engine::GraphDefinitionCompileResult {};
        auto foundLowMix = false;
        auto foundHighMix = false;
        for (const auto& node : independentCompile.graph.nodes)
            if (node.kind == cydaw::engine::NativeDspNodeKind::reverb) {
                foundLowMix = foundLowMix
                    || std::abs(node.parameters.mix - 0.12f) < 0.001f;
                foundHighMix = foundHighMix
                    || std::abs(node.parameters.mix - 0.76f) < 0.001f;
            }
        cydaw::app::NodeGraphComponent restoredInstances;
        restoredInstances.restore(juce::JSON::parse(juce::JSON::toString(
            independentInstances.serialise(), false)));
        const auto restoredInterface = restoredInstances.graphInterfaceState();
        independentValues = independentValues
            && restoredInterface.parameters.size()
                   == instanceInterface.parameters.size();
        passed = expect(independentValues && independentCompile.ok()
                            && foundLowMix && foundHighMix,
                        "reusable Graph instances keep independent parameter overrides across compilation and save/restore")
                 && passed;

        cydaw::app::NodeGraphComponent multiPortGraph;
        const auto multiPortPreset = multiPortGraph.loadFactoryPreset(1);
        const auto insertMixer = multiPortGraph.insertFactoryDevice("mixer-4");
        const auto multiPortState = multiPortGraph.serialise();
        const auto* multiPortRoot = multiPortState.getDynamicObject();
        const auto* multiPortConnections = multiPortRoot != nullptr
            ? multiPortRoot->getProperty("connections").getArray() : nullptr;
        auto serialisedPortsAreExplicit = multiPortConnections != nullptr;
        if (multiPortConnections != nullptr)
            for (const auto& connectionValue : *multiPortConnections)
                if (const auto* connection = connectionValue.getDynamicObject();
                    connection == nullptr
                    || static_cast<juce::int64>(
                           connection->getProperty("sourcePort")) == 0
                    || static_cast<juce::int64>(
                           connection->getProperty("destinationPort")) == 0) {
                    serialisedPortsAreExplicit = false;
                    break;
                }
        const auto multiPortDefinitions = multiPortGraph.graphDefinitions();
        const auto& multiPortRootDefinition = multiPortDefinitions.back();
        const auto mixerNode = std::ranges::find_if(
            multiPortRootDefinition.nodes, [](const auto& node) {
                return node.processorKey == "core.mixer-4";
            });
        const auto mixerHasFourDistinctInputs =
            mixerNode != multiPortRootDefinition.nodes.end()
            && std::ranges::count_if(
                   mixerNode->ports, [](const auto& port) {
                       return port.direction
                                  == cydaw::graph::PortDirection::input
                              && port.type == cydaw::graph::PortType::audio
                              && port.id >= 1 && port.id <= 4;
                   }) == 4
            && std::ranges::any_of(
                   mixerNode->ports, [](const auto& port) {
                       return port.direction
                                  == cydaw::graph::PortDirection::output
                              && port.type == cydaw::graph::PortType::audio
                              && port.id == 5 && port.channels == 2;
                   });
        const auto mixerConnectionUsesExactSocket =
            mixerNode != multiPortRootDefinition.nodes.end()
            && std::ranges::any_of(
                   multiPortRootDefinition.connections,
                   [&mixerNode](const auto& connection) {
                       return connection.destination.nodeId == mixerNode->id
                              && connection.destination.portId == 1;
                   })
            && std::ranges::any_of(
                   multiPortRootDefinition.connections,
                   [&mixerNode](const auto& connection) {
                       return connection.source.nodeId == mixerNode->id
                              && connection.source.portId == 5;
                   });
        const auto multiPortNative = multiPortGraph.nativeDspGraph();
        const auto nativeMixerEndpointsArePreserved =
            mixerNode != multiPortRootDefinition.nodes.end()
            && std::ranges::any_of(
                   multiPortNative.connections,
                   [&mixerNode](const auto& connection) {
                       return connection.destination == mixerNode->id
                              && connection.destinationPort == 1;
                   })
            && std::ranges::any_of(
                   multiPortNative.connections,
                   [&mixerNode](const auto& connection) {
                       return connection.source == mixerNode->id
                              && connection.sourcePort == 5;
                   });
        cydaw::app::NodeGraphComponent restoredMultiPortGraph;
        restoredMultiPortGraph.restore(multiPortState);
        const auto restoredMultiPortDefinitions =
            restoredMultiPortGraph.graphDefinitions();
        const auto& restoredMultiPortRoot = restoredMultiPortDefinitions.back();
        const auto restoredMixer = std::ranges::find_if(
            restoredMultiPortRoot.nodes, [](const auto& node) {
                return node.processorKey == "core.mixer-4";
            });
        passed = expect(
                     multiPortPreset.wasOk() && insertMixer.wasOk()
                         && serialisedPortsAreExplicit
                         && mixerHasFourDistinctInputs
                         && mixerConnectionUsesExactSocket
                         && nativeMixerEndpointsArePreserved
                         && restoredMixer != restoredMultiPortRoot.nodes.end()
                         && std::ranges::count_if(
                                restoredMixer->ports, [](const auto& port) {
                                    return port.direction
                                               == cydaw::graph::PortDirection::input
                                           && port.type
                                               == cydaw::graph::PortType::audio
                                           && port.id >= 1 && port.id <= 4;
                                }) == 4
                         && std::ranges::any_of(
                                restoredMixer->ports, [](const auto& port) {
                                    return port.direction
                                               == cydaw::graph::PortDirection::output
                                           && port.id == 5;
                                })
                         && restoredMultiPortGraph.validateGraphAsset().ok(),
                     "multi-port mixer preserves four typed input sockets and exact cable endpoints")
                 && passed;

        const auto undoHandled = browserInsertGraph.keyPressed(juce::KeyPress(
            'Z', juce::ModifierKeys::commandModifier, 'Z'));
        const auto undoState = browserInsertGraph.serialise();
        const auto* undoRoot = undoState.getDynamicObject();
        const auto* undoNodes = undoRoot != nullptr
                                    ? undoRoot->getProperty("nodes").getArray()
                                    : nullptr;
        const auto undoRuntime = browserInsertGraph.runtimeGraphExport();
        const auto redoHandled = browserInsertGraph.keyPressed(juce::KeyPress(
            'Z', juce::ModifierKeys::commandModifier
                     | juce::ModifierKeys::shiftModifier,
            'Z'));
        const auto redoState = browserInsertGraph.serialise();
        const auto* redoRoot = redoState.getDynamicObject();
        const auto* redoNodes = redoRoot != nullptr
                                    ? redoRoot->getProperty("nodes").getArray()
                                    : nullptr;
        const auto redoRuntime = browserInsertGraph.runtimeGraphExport();
        passed = expect(
                     undoHandled && redoHandled && undoNodes != nullptr
                         && undoNodes->size() == 4
                         && undoRuntime.compilationResult.wasOk()
                         && redoNodes != nullptr && redoNodes->size() == 5
                         && redoRuntime.compilationResult.wasOk()
                         && redoRuntime.nativeGraph.nodes.size()
                                == browserInsertGraph.nativeDspGraph().nodes.size(),
                     "Graph editor undo/redo restores an exact compilable topology")
                 && passed;

        const auto userGraphPresetFile =
            tempRoot.getChildFile("Warm Test.cydawgraph");
        const auto saveUserPreset = browserInsertGraph.saveUserGraphPreset(
            userGraphPresetFile, "Warm Test");
        const auto parsedUserPreset =
            cydaw::app::parseGraphPresetDocument(juce::JSON::parse(
                userGraphPresetFile.loadFileAsString()));
        cydaw::app::GraphPresetMetadata richMetadata;
        richMetadata.definitionId = "user.test.rich-metadata";
        richMetadata.version = { 2, 3, 4 };
        richMetadata.role = cydaw::graph::GraphRole::instrument;
        richMetadata.scope = cydaw::graph::GraphScope::user;
        richMetadata.tags = { "polyphonic", "bright" };
        richMetadata.category = "Instruments / Synthesizers";
        richMetadata.description = "Reusable test instrument";
        richMetadata.author = "CyDAW Tests";
        const auto richMetadataDocument =
            cydaw::app::parseGraphPresetDocument(
                cydaw::app::makeGraphPresetDocument(
                    "Rich Graph", browserInsertGraph.serialise(),
                    richMetadata));
        const auto typedGraphDefinitions = browserInsertGraph.graphDefinitions();
        const auto typedGraphValidation = browserInsertGraph.validateGraphAsset();
        auto loadedUserPreset =
            std::make_unique<cydaw::app::NodeGraphComponent>();
        const auto loadUserPreset =
            loadedUserPreset->loadUserGraphPreset(userGraphPresetFile);
        const auto loadedUserState = loadedUserPreset->serialise();
        const auto* loadedUserRoot = loadedUserState.getDynamicObject();
        const auto* loadedUserNodes = loadedUserRoot != nullptr
            ? loadedUserRoot->getProperty("nodes").getArray() : nullptr;
        const auto* loadedUserConnections = loadedUserRoot != nullptr
            ? loadedUserRoot->getProperty("connections").getArray() : nullptr;
        const auto stableUserState = juce::JSON::toString(
            loadedUserState, false);
        auto invalidGraph = juce::DynamicObject::Ptr(
            new juce::DynamicObject());
        invalidGraph->setProperty("schema", 10);
        invalidGraph->setProperty("nodes", juce::Array<juce::var> {});
        invalidGraph->setProperty("connections", juce::Array<juce::var> {});
        invalidGraph->setProperty("modules", juce::Array<juce::var> {});
        const auto invalidDocument = cydaw::app::makeGraphPresetDocument(
            "Broken", juce::var(invalidGraph.get()));
        const auto invalidUserPresetFile =
            tempRoot.getChildFile("Broken.cydawgraph");
        (void) invalidUserPresetFile.replaceWithText(
            juce::JSON::toString(invalidDocument, false));
        const auto invalidLoad =
            loadedUserPreset->loadUserGraphPreset(invalidUserPresetFile);
        passed = expect(
                     saveUserPreset.wasOk()
                         && parsedUserPreset.has_value()
                         && parsedUserPreset->name == "Warm Test"
                         && parsedUserPreset->metadata.definitionId.isNotEmpty()
                         && parsedUserPreset->metadata.role
                                == cydaw::graph::GraphRole::audioEffect
                         && richMetadataDocument.has_value()
                         && richMetadataDocument->metadata.definitionId
                                == "user.test.rich-metadata"
                         && richMetadataDocument->metadata.version
                                == cydaw::graph::SemanticVersion { 2, 3, 4 }
                         && richMetadataDocument->metadata.category
                                == "Instruments / Synthesizers"
                         && richMetadataDocument->metadata.description
                                == "Reusable test instrument"
                         && richMetadataDocument->metadata.author
                                == "CyDAW Tests"
                         && richMetadataDocument->metadata.tags.size() == 2
                         && typedGraphDefinitions.size() >= 2
                         && typedGraphValidation.ok()
                         && loadUserPreset.wasOk()
                         && loadedUserNodes != nullptr
                         && loadedUserNodes->size() == 5
                         && loadedUserConnections != nullptr
                         && loadedUserConnections->size() == 4
                          && invalidLoad.failed()
                          && juce::JSON::toString(
                                 loadedUserPreset->serialise(), false)
                                == stableUserState,
                     "save, validate and transactionally load user graph presets")
                 && passed;

        cydaw::app::NodeGraphComponent publishedIoGraph;
        publishedIoGraph.addPublishedIoPair(
            cydaw::engine::PortKind::transport);
        publishedIoGraph.addPublishedIoPair(
            cydaw::engine::PortKind::audio, 6);
        publishedIoGraph.showGraphMembersPanel();
        publishedIoGraph.setGraphMemberSearch("Transport");
        const auto filteredGraphMembers =
            publishedIoGraph.visibleGraphMemberCount();
        publishedIoGraph.setGraphMemberSearch({});
        const auto publishedIoState = publishedIoGraph.serialise();
        const auto* publishedIoRoot = publishedIoState.getDynamicObject();
        const auto* publishedIoNodes = publishedIoRoot != nullptr
                                           ? publishedIoRoot
                                                 ->getProperty("nodes").getArray()
                                           : nullptr;
        const auto* publishedIoConnections = publishedIoRoot != nullptr
                                                 ? publishedIoRoot
                                                       ->getProperty("connections")
                                                       .getArray()
                                                 : nullptr;
        const auto* publishedPresentation = publishedIoRoot != nullptr
                                                ? publishedIoRoot
                                                      ->getProperty("presentation")
                                                      .getDynamicObject()
                                                : nullptr;
        const auto* publishedGraphControls = publishedPresentation != nullptr
                                                 ? publishedPresentation
                                                       ->getProperty("controls")
                                                       .getArray()
                                                 : nullptr;
        auto hasTransportInput = false;
        auto hasTransportOutput = false;
        if (publishedGraphControls != nullptr)
            for (const auto& controlValue : *publishedGraphControls)
                if (const auto* control = controlValue.getDynamicObject()) {
                    const auto kind = control->getProperty("kind").toString();
                    const auto portKind = static_cast<int>(
                        control->getProperty("portKind"));
                    hasTransportInput = hasTransportInput
                                        || (kind == "input-port" && portKind == 3);
                    hasTransportOutput = hasTransportOutput
                                         || (kind == "output-port" && portKind == 3);
                }
        const auto publishedDefinitions = publishedIoGraph.graphDefinitions();
        const auto& publishedRootDefinition = publishedDefinitions.back();
        const auto hasExactSurroundInput = std::ranges::any_of(
            publishedRootDefinition.ports, [](const auto& port) {
                return port.direction
                           == cydaw::graph::PortDirection::input
                       && port.type == cydaw::graph::PortType::audio
                       && port.channels == 6
                       && port.audioType
                              == cydaw::graph::AudioPortType::channels6;
            });
        const auto hasExactSurroundOutput = std::ranges::any_of(
            publishedRootDefinition.ports, [](const auto& port) {
                return port.direction
                           == cydaw::graph::PortDirection::output
                       && port.type == cydaw::graph::PortType::audio
                       && port.channels == 6
                       && port.audioType
                              == cydaw::graph::AudioPortType::channels6;
            });
        passed = expect(publishedIoNodes != nullptr
                            && publishedIoNodes->size() == 6
                            && publishedIoConnections != nullptr
                            && publishedIoConnections->size() == 3
                            && publishedIoGraph.graphMemberCount() == 6
                            && filteredGraphMembers == 2
                            && publishedIoGraph.visibleGraphMemberCount() == 6
                            && publishedIoRoot != nullptr
                            && static_cast<bool>(publishedIoRoot->getProperty(
                                "configurationPanelShowsMembers"))
                            && hasTransportInput && hasTransportOutput
                            && hasExactSurroundInput
                            && hasExactSurroundOutput,
                        "published Transport and exact 5.1 I/O presets create connected typed graph ports")
                 && passed;

        juce::String transportInputMember;
        juce::String surroundInputMember;
        std::uint64_t transportInputId {};
        std::uint64_t surroundInputId {};
        if (publishedIoNodes != nullptr)
            for (const auto& nodeValue : *publishedIoNodes)
                if (const auto* node = nodeValue.getDynamicObject()) {
                    const auto type = node->getProperty("type").toString();
                    const auto id = static_cast<std::uint64_t>(
                        static_cast<juce::int64>(node->getProperty("id")));
                    if (type == "graph-transport-input") {
                        transportInputId = id;
                        transportInputMember = "node." + juce::String(
                            static_cast<juce::int64>(id));
                    } else if (type == "graph-audio-input-6") {
                        surroundInputId = id;
                        surroundInputMember = "node." + juce::String(
                            static_cast<juce::int64>(id));
                    }
                }
        const auto renamedTransport = transportInputMember.isNotEmpty()
            && publishedIoGraph.setGraphMemberMetadata(
                transportInputMember, "Clock input",
                "Project transport and tempo source");
        const auto movedTransport = transportInputMember.isNotEmpty()
            && publishedIoGraph.moveGraphMember(transportInputMember, 1);
        const auto retypedSurround = surroundInputMember.isNotEmpty()
            && publishedIoGraph.setGraphMemberType(
                surroundInputMember, cydaw::engine::PortKind::audio, 4);
        const auto editedContractState = publishedIoGraph.serialise();
        const auto editedContractDefinitions =
            publishedIoGraph.graphDefinitions();
        const auto& editedContractRoot = editedContractDefinitions.back();
        const auto clockInput = std::ranges::find_if(
            editedContractRoot.ports, [transportInputId](const auto& port) {
                return port.id == transportInputId
                    && port.direction == cydaw::graph::PortDirection::input;
            });
        const auto quadPorts = std::ranges::count_if(
            editedContractRoot.ports, [](const auto& port) {
                return port.type == cydaw::graph::PortType::audio
                    && port.channels == 4;
            });
        std::vector<std::uint64_t> editedInputOrder;
        for (const auto& port : editedContractRoot.ports)
            if (port.direction == cydaw::graph::PortDirection::input)
                editedInputOrder.push_back(port.id);
        const auto transportOrder = std::ranges::find(
            editedInputOrder, transportInputId);
        const auto surroundOrder = std::ranges::find(
            editedInputOrder, surroundInputId);
        const auto transportMovedAfterSurround =
            transportOrder != editedInputOrder.end()
            && surroundOrder != editedInputOrder.end()
            && std::distance(editedInputOrder.begin(), transportOrder)
                   > std::distance(editedInputOrder.begin(), surroundOrder);
        const auto* editedContractObject =
            editedContractState.getDynamicObject();
        const auto* editedContractNodes = editedContractObject != nullptr
            ? editedContractObject->getProperty("nodes").getArray() : nullptr;
        const auto metadataPersisted = editedContractNodes != nullptr
            && std::ranges::any_of(*editedContractNodes,
                [transportInputId](const juce::var& value) {
                    const auto* node = value.getDynamicObject();
                    return node != nullptr
                        && static_cast<std::uint64_t>(static_cast<juce::int64>(
                               node->getProperty("id"))) == transportInputId
                        && node->getProperty("alias").toString()
                               == "Clock input"
                        && node->getProperty("description").toString()
                               == "Project transport and tempo source";
                });
        passed = expect(renamedTransport && movedTransport
                            && retypedSurround
                            && clockInput != editedContractRoot.ports.end()
                            && clockInput->displayName == "Clock input"
                            && quadPorts == 2
                            && transportMovedAfterSurround
                            && metadataPersisted,
                        "Graph Members edits names, descriptions, exact audio contracts and public port order")
                 && passed;

        {
            cydaw::app::NodeGraphComponent exposureGraph;
            const auto inserted = exposureGraph.insertNode("compressor");
            const auto initial = exposureGraph.graphInterfaceState();
            const auto parameterKey = initial.parameters.empty()
                ? juce::String {} : initial.parameters.front().key;
            const auto changed = parameterKey.isNotEmpty()
                && exposureGraph.setPublishedParameterExposure(
                    parameterKey, false, false);
            const auto renamed = parameterKey.isNotEmpty()
                && exposureGraph.setGraphMemberMetadata(
                    parameterKey, "Threshold published",
                    "Public compressor threshold");
            const auto reordered = parameterKey.isNotEmpty()
                && exposureGraph.moveGraphMember(parameterKey, 1);
            const auto updated = exposureGraph.graphInterfaceState();
            const auto updatedParameter = std::ranges::find_if(
                updated.parameters, [&parameterKey](const auto& parameter) {
                    return parameter.key == parameterKey;
                });
            cydaw::app::NodeGraphComponent restoredExposureGraph;
            restoredExposureGraph.restore(exposureGraph.serialise());
            const auto restoredExposureState =
                restoredExposureGraph.graphInterfaceState();
            const auto restoredParameter = std::ranges::find_if(
                restoredExposureState.parameters,
                [&parameterKey](const auto& parameter) {
                    return parameter.key == parameterKey;
                });
            const auto restoredFirst = restoredExposureState.parameters.empty()
                ? juce::String {}
                : restoredExposureState.parameters.front().key;
            passed = expect(
                         inserted.wasOk() && changed && renamed && reordered
                             && updatedParameter != updated.parameters.end()
                             && !updatedParameter->showInDevice
                             && !updatedParameter->publishToHost
                             && updatedParameter->label
                                    == "Threshold published"
                             && updatedParameter->description
                                    == "Public compressor threshold"
                             && !updated.parameters.empty()
                             && updated.parameters.front().key != parameterKey
                             && restoredParameter
                                    != restoredExposureState.parameters.end()
                             && !restoredParameter->showInDevice
                             && !restoredParameter->publishToHost
                             && restoredParameter->label
                                    == "Threshold published"
                             && restoredFirst != parameterKey,
                         "Graph member metadata, order and exposure persist for device and VST3 contracts")
                     && passed;
        }

        {
            cydaw::app::GraphManagerComponent manager;
            cydaw::app::GraphManagerComponent::Asset first;
            first.key = "factory.first";
            first.name = "First Graph";
            first.category = "Effects";
            first.scope = "Factory";
            first.previewNodes = {
                { 1, "Input", { 0.1f, 0.5f }, juce::Colour(0xff67b9ff) },
                { 2, "Output", { 0.9f, 0.5f }, juce::Colour(0xff67b9ff) }
            };
            first.previewConnections = {
                { 1, 2, juce::Colour(0xff67b9ff) }
            };
            cydaw::app::GraphManagerComponent::Asset second = first;
            second.key = "factory.second";
            second.name = "Second Graph";
            second.previewNodes.push_back(
                { 3, "Meter", { 0.5f, 0.5f }, juce::Colour(0xff55cce8) });
            second.previewConnections.push_back(
                { 1, 3, juce::Colour(0xff67b9ff) });
            manager.setAssets({ first, second });
            const auto selectedFirst = manager.selectedAssetKey();
            const auto firstPreviewReady =
                manager.selectedPreviewNodeCount() == 2
                && manager.selectedPreviewConnectionCount() == 1;
            const auto selectedSecond = manager.selectAssetByKey(second.key);
            const auto secondPreviewReady =
                manager.selectedPreviewNodeCount() == 3
                && manager.selectedPreviewConnectionCount() == 2;
            manager.setAssets({ first, second });
            passed = expect(selectedFirst == first.key && selectedSecond
                                && manager.selectedAssetKey() == second.key
                                && firstPreviewReady && secondPreviewReady,
                            "Graph Manager preserves selection and exposes topology previews")
                     && passed;
        }

        {
            cydaw::app::NodeGraphComponent versionedClasses;
            const auto created = versionedClasses.createEditableGraphModule(
                cydaw::graph::GraphRole::audioEffect, false, 2);
            auto classState = versionedClasses.serialise();
            auto* classRoot = classState.getDynamicObject();
            auto* modules = classRoot != nullptr
                ? classRoot->getProperty("modules").getArray() : nullptr;
            juce::String versionedModuleKey;
            if (modules != nullptr)
                for (auto& value : *modules)
                    if (auto* module = value.getDynamicObject();
                        module != nullptr
                        && static_cast<bool>(module->getProperty("inline"))) {
                        versionedModuleKey =
                            module->getProperty("key").toString();
                        module->setProperty("version", "2.3.4");
                        break;
                    }
            cydaw::app::NodeGraphComponent restoredClasses;
            restoredClasses.restore(classState);
            const auto definitions = restoredClasses.graphDefinitions();
            const auto versionedDefinition = std::ranges::find_if(
                definitions, [&versionedModuleKey](const auto& definition) {
                    return !versionedModuleKey.isEmpty()
                        && juce::String(definition.reference.id)
                               .endsWith(".module." + versionedModuleKey);
                });
            const auto restoredClassState = restoredClasses.serialise();
            const auto* restoredClassRoot =
                restoredClassState.getDynamicObject();
            const auto* restoredClassModules = restoredClassRoot != nullptr
                ? restoredClassRoot->getProperty("modules").getArray()
                : nullptr;
            const auto versionPersisted = restoredClassModules != nullptr
                && std::ranges::any_of(*restoredClassModules,
                    [&versionedModuleKey](const juce::var& value) {
                        const auto* module = value.getDynamicObject();
                        return module != nullptr
                            && module->getProperty("key").toString()
                                   == versionedModuleKey
                            && module->getProperty("version").toString()
                                   == "2.3.4";
                    });
            passed = expect(created.wasOk()
                                && versionedDefinition != definitions.end()
                                && versionedDefinition->reference.version
                                       == cydaw::graph::SemanticVersion {
                                              2, 3, 4 }
                                && versionPersisted,
                            "reusable Graph classes own independent semantic versions used by their instances")
                     && passed;
        }

        {
            cydaw::app::NodeGraphComponent classVariants;
            const auto created = classVariants.createEditableGraphModule(
                cydaw::graph::GraphRole::audioEffect, false, 2);
            const auto before = classVariants.serialise();
            const auto* beforeRoot = before.getDynamicObject();
            const auto* beforeModules = beforeRoot != nullptr
                ? beforeRoot->getProperty("modules").getArray() : nullptr;
            juce::String sourceKey;
            auto sourceNodeCount = 0;
            if (beforeModules != nullptr)
                for (const auto& value : *beforeModules)
                    if (const auto* module = value.getDynamicObject();
                        module != nullptr
                        && static_cast<bool>(module->getProperty("inline"))) {
                        sourceKey = module->getProperty("key").toString();
                        if (const auto* children = module->getProperty(
                                "nodes").getArray())
                            sourceNodeCount = children->size();
                        break;
                    }
            const auto variantCreated = classVariants.createGraphVariant(
                sourceKey, "Wide Variant", true, false);
            const auto after = classVariants.serialise();
            const auto* afterRoot = after.getDynamicObject();
            const auto* afterModules = afterRoot != nullptr
                ? afterRoot->getProperty("modules").getArray() : nullptr;
            const auto* rootNodes = afterRoot != nullptr
                ? afterRoot->getProperty("nodes").getArray() : nullptr;
            juce::String variantKey;
            auto originalPreserved = false;
            auto variantPreserved = false;
            if (afterModules != nullptr)
                for (const auto& value : *afterModules)
                    if (const auto* module = value.getDynamicObject()) {
                        const auto key = module->getProperty("key").toString();
                        const auto* children = module->getProperty(
                            "nodes").getArray();
                        if (key == sourceKey)
                            originalPreserved = children != nullptr
                                && children->size() == sourceNodeCount
                                && module->getProperty("version").toString()
                                       == "1.0.0";
                        if (module->getProperty("parentClassKey").toString()
                                == sourceKey) {
                            variantKey = key;
                            variantPreserved = module->getProperty(
                                    "name").toString() == "Wide Variant"
                                && module->getProperty("version").toString()
                                       == "1.0.0"
                                && !static_cast<bool>(
                                    module->getProperty("locked"))
                                && children != nullptr
                                && children->size() == sourceNodeCount;
                        }
                    }
            const auto instanceReplaced = rootNodes != nullptr
                && std::ranges::any_of(*rootNodes,
                    [&variantKey](const juce::var& value) {
                        const auto* node = value.getDynamicObject();
                        return node != nullptr && variantKey.isNotEmpty()
                            && node->getProperty("type").toString()
                                   == variantKey;
                    });
            const auto definitions = classVariants.graphDefinitions();
            const auto sourceDefinition = std::ranges::find_if(
                definitions, [&sourceKey](const auto& definition) {
                    return juce::String(definition.reference.id)
                        .endsWith(".module." + sourceKey);
                });
            const auto variantDefinition = std::ranges::find_if(
                definitions, [&variantKey](const auto& definition) {
                    return juce::String(definition.reference.id)
                        .endsWith(".module." + variantKey);
                });
            passed = expect(created.wasOk() && !sourceKey.isEmpty()
                                && variantCreated.wasOk()
                                && originalPreserved && variantPreserved
                                && instanceReplaced
                                && sourceDefinition != definitions.end()
                                && variantDefinition != definitions.end(),
                            "Graph class variants preserve the source class and switch only the selected instance")
                     && passed;
        }

        cydaw::app::NodeGraphComponent surroundPrimitiveGraph;
        surroundPrimitiveGraph.addPublishedIoPair(
            cydaw::engine::PortKind::audio, 6);
        const auto insertedSurroundGain =
            surroundPrimitiveGraph.insertNode("gain");
        const auto connectedSurroundInput =
            surroundPrimitiveGraph.connectNodePorts(
                "graph-audio-input-6", "output", "gain", "audio-in");
        const auto connectedSurroundOutput =
            surroundPrimitiveGraph.connectNodePorts(
                "gain", "audio-out", "graph-audio-output-6", "input", true);
        const auto surroundDefinitions =
            surroundPrimitiveGraph.graphDefinitions();
        const auto& surroundRoot = surroundDefinitions.back();
        const auto surroundGain = std::ranges::find(
            surroundRoot.nodes, std::string("core.gain"),
            &cydaw::graph::GraphNode::processorKey);
        const auto surroundValidation =
            surroundPrimitiveGraph.validateGraphAsset();
        passed = expect(insertedSurroundGain.wasOk(),
                        "insert a DSP primitive into an exact 5.1 Graph")
                 && passed;
        passed = expect(connectedSurroundInput.wasOk()
                            && connectedSurroundOutput.wasOk(),
                        "wire the exact 5.1 Graph through the DSP primitive")
                 && passed;
        passed = expect(
                     surroundGain != surroundRoot.nodes.end()
                         && std::ranges::all_of(
                             surroundGain->ports, [](const auto& port) {
                                 return port.type
                                            != cydaw::graph::PortType::audio
                                        || (port.channels == 6
                                            && port.audioType
                                                == cydaw::graph::AudioPortType::channels6);
                             }),
                     "DSP primitives inherit the exact 5.1 Graph format")
                 && passed;
        passed = expect(surroundValidation.ok(),
                        "an exact 5.1 DSP primitive Graph validates")
                 && passed;

        cydaw::app::NodeGraphComponent cableAdaptedGraph;
        cableAdaptedGraph.addPublishedIoPair(
            cydaw::engine::PortKind::audio, 6);
        const auto selectedStereoBoundary = cableAdaptedGraph.selectNodePort(
            "graph-audio-input-2", "output");
        const auto insertedStereoGain = cableAdaptedGraph.insertNode("gain");
        const auto adaptedByFirstCable = cableAdaptedGraph.connectNodePorts(
            "graph-audio-input-6", "output", "gain", "audio-in");
        const auto adaptedOutputCable = cableAdaptedGraph.connectNodePorts(
            "gain", "audio-out", "graph-audio-output-6", "input", true);
        const auto adaptedDefinitions = cableAdaptedGraph.graphDefinitions();
        const auto& adaptedRoot = adaptedDefinitions.back();
        const auto adaptedGain = std::ranges::find(
            adaptedRoot.nodes, std::string("core.gain"),
            &cydaw::graph::GraphNode::processorKey);
        passed = expect(
                     selectedStereoBoundary && insertedStereoGain.wasOk()
                         && adaptedByFirstCable.wasOk()
                         && adaptedOutputCable.wasOk()
                         && adaptedGain != adaptedRoot.nodes.end()
                         && std::ranges::all_of(
                             adaptedGain->ports, [](const auto& port) {
                                 return port.type
                                            != cydaw::graph::PortType::audio
                                        || (port.channels == 6
                                            && port.audioType
                                                == cydaw::graph::AudioPortType::channels6);
                             }),
                     "first exact audio cable safely specializes an unconnected DSP primitive")
                 && passed;

        auto legacyInput = juce::DynamicObject::Ptr(new juce::DynamicObject());
        legacyInput->setProperty("key", "audio-clips");
        legacyInput->setProperty("x", 0.13);
        legacyInput->setProperty("y", 0.42);
        juce::Array<juce::var> legacyNodes;
        legacyNodes.add(juce::var(legacyInput.get()));
        restored.restore(juce::var(legacyNodes));
        const auto migrated = restored.serialise();
        const auto* migratedObject = migrated.getDynamicObject();
        bool migratedPosition = false;
        if (migratedObject != nullptr) {
            if (const auto* nodes = migratedObject->getProperty("nodes").getArray()) {
                for (const auto& nodeValue : *nodes) {
                    if (const auto* node = nodeValue.getDynamicObject();
                        node != nullptr
                        && (node->getProperty("type").toString()
                                == "audio-clips"
                            || node->getProperty("type").toString()
                                   .startsWith("graph-audio-input-"))) {
                        migratedPosition = std::abs(static_cast<double>(node->getProperty("x")) - 0.13) < 0.001
                                           && std::abs(static_cast<double>(node->getProperty("y")) - 0.42) < 0.001;
                    }
                }
            }
        }
        passed = expect(migratedPosition, "legacy node layout migration") && passed;

        auto legacyModuleState = graph.serialise();
        auto* legacyModuleRoot = legacyModuleState.getDynamicObject();
        auto* legacyModules = legacyModuleRoot != nullptr
                                  ? legacyModuleRoot->getProperty("modules").getArray()
                                  : nullptr;
        if (legacyModuleRoot != nullptr && legacyModules != nullptr) {
            legacyModuleRoot->setProperty("schema", 4);
            auto child = juce::DynamicObject::Ptr(new juce::DynamicObject());
            child->setProperty("id", static_cast<juce::int64>(90));
            child->setProperty("type", "drive");
            child->setProperty("title", "Drive");
            child->setProperty("x", 0.2);
            child->setProperty("y", 0.4);
            juce::Array<juce::var> children;
            children.add(juce::var(child.get()));
            cydaw::app::ModuleInterfaceState oldInterface;
            oldInterface.nextControlId = 2;
            oldInterface.parameters.push_back(
                { "drive.db", "Drive", "dB", 0.0, 36.0, 12.0 });
            oldInterface.controls.push_back(
                { 1, cydaw::app::ModuleControlKind::knob, "Drive", "drive.db",
                  0.05f, 0.05f, 0.20f, 0.30f });
            auto module = juce::DynamicObject::Ptr(new juce::DynamicObject());
            module->setProperty("key", "legacy-drive-module");
            module->setProperty("name", "Legacy drive module");
            module->setProperty("nodes", children);
            module->setProperty("connections", juce::Array<juce::var> {});
            module->setProperty("interface",
                                cydaw::app::serialiseModuleInterface(oldInterface));
            legacyModules->add(juce::var(module.get()));
        }
        cydaw::app::NodeGraphComponent migratedModuleGraph;
        migratedModuleGraph.restore(legacyModuleState);
        const auto migratedModuleState = migratedModuleGraph.serialise();
        const auto* migratedModuleRoot = migratedModuleState.getDynamicObject();
        const auto* migratedModules = migratedModuleRoot != nullptr
                                          ? migratedModuleRoot
                                                ->getProperty("modules").getArray()
                                          : nullptr;
        const juce::DynamicObject* migratedModule = nullptr;
        if (migratedModules != nullptr)
            for (const auto& moduleValue : *migratedModules)
                if (const auto* candidate = moduleValue.getDynamicObject();
                    candidate != nullptr
                    && candidate->getProperty("key").toString()
                           == "legacy-drive-module") {
                    migratedModule = candidate;
                    break;
                }
        const auto* migratedChildren = migratedModule != nullptr
                                           ? migratedModule->getProperty("nodes").getArray()
                                           : nullptr;
        const auto* migratedChild = migratedChildren != nullptr
                                        && !migratedChildren->isEmpty()
                                    ? migratedChildren->getReference(0).getDynamicObject()
                                    : nullptr;
        const auto* migratedParameters = migratedChild != nullptr
                                             ? migratedChild
                                                   ->getProperty("parameters")
                                                   .getDynamicObject()
                                             : nullptr;
        passed = expect(migratedParameters != nullptr
                            && std::abs(static_cast<double>(
                                   migratedParameters->getProperty("driveDb"))
                                       - 12.0) < 0.001,
                        "schema-4 module controls migrate into per-node DSP parameters") && passed;

        // 52: the sound model synth (a recorded note made playable), then the
        // sampler, joined; 54: Kick, after Prisme; 58: the synth family's
        // four (Ambre, Colosse, Mosaïque, Nébuleuse); 59: Batterie; 60: the
        // simple sampler; 69: Opale, Onyx, Tirette, Alliage, Quartz, Argile,
        // Saphir, Dérive, Bronze; 72: Nuée, Silex, the pad rack and Jade, the
        // de-esser chain gone (the De-esseur is an effect of its own).
        passed = expect(graph.factoryPresetCount() == 72,
                        "factory node-graph preset catalogue") && passed;
        cydaw::app::NodeGraphComponent emptyInstrumentGraph;
        emptyInstrumentGraph.initialiseEmptyMidiTrackGraph();
        cydaw::engine::PolySynthGraphPlan emptyInstrumentPlan;
        passed = expect(
                     emptyInstrumentGraph.compilePolySynthGraph(
                         emptyInstrumentPlan).wasOk()
                         && !emptyInstrumentPlan.enabled
                         && emptyInstrumentPlan.graphNodeCount == 2,
                     "MIDI tracks stay silent until an instrument graph is added")
                 && passed;
        cydaw::app::NodeGraphComponent editableAudioGraph;
        const auto editableEffectResult = editableAudioGraph.createEditableGraphModule(
            cydaw::graph::GraphRole::audioEffect, false, 6);
        const auto editableAudioState = editableAudioGraph.serialise();
        const auto* editableAudioRoot = editableAudioState.getDynamicObject();
        const auto* editableModules = editableAudioRoot != nullptr
                                          ? editableAudioRoot
                                                ->getProperty("modules").getArray()
                                          : nullptr;
        auto hasInlineModule = false;
        if (editableModules != nullptr)
            hasInlineModule = std::any_of(
                editableModules->begin(), editableModules->end(),
                [](const auto& value) {
                    const auto* object = value.getDynamicObject();
                    return object != nullptr
                           && static_cast<bool>(object->getProperty("inline"));
                });
        const auto editableDefinitions = editableAudioGraph.graphDefinitions();
        const auto hasInlineDefinition = std::ranges::any_of(
            editableDefinitions, [](const auto& definition) {
                return definition.scope
                       == cydaw::graph::GraphScope::inlineTrack;
            });
        const auto hasSixChannelInlineContract = std::ranges::any_of(
            editableDefinitions, [](const auto& definition) {
                return definition.scope
                           == cydaw::graph::GraphScope::inlineTrack
                       && definition.role
                           == cydaw::graph::GraphRole::audioEffect
                       && definition.ports.size() == 2
                       && std::ranges::all_of(
                           definition.ports, [](const auto& port) {
                               return port.type
                                          == cydaw::graph::PortType::audio
                                      && port.audioType
                                          == cydaw::graph::AudioPortType::channels6
                                      && port.channels == 6;
                           });
            });
        ConstantAudioSource editableInput(0.1f);
        cydaw::app::GainPanAudioSource editableRenderer(editableInput);
        editableRenderer.prepareToPlay(512, 48'000.0);
        const auto editableNative = editableAudioGraph.nativeDspGraph();
        passed = expect(
                     editableEffectResult.wasOk()
                         && editableModules != nullptr
                         && hasInlineModule
                         && hasInlineDefinition
                         && hasSixChannelInlineContract
                         && editableRenderer.setNativeDspGraph(editableNative)
                                .wasOk()
                         && editableNative.outputNodeId != 0,
                     "editable inline audio Graph is inserted and compiles live")
                 && passed;

        cydaw::app::NodeGraphComponent editableInstrumentGraph;
        editableInstrumentGraph.initialiseEmptyMidiTrackGraph();
        const auto editableInstrumentResult =
            editableInstrumentGraph.createEditableGraphModule(
                cydaw::graph::GraphRole::instrument, false);
        cydaw::engine::PolySynthGraphPlan editableInstrumentPlan;
        const auto editableInstrumentCompile =
            editableInstrumentGraph.compilePolySynthGraph(
                editableInstrumentPlan);
        const auto editableInstrumentDefinitions =
            editableInstrumentGraph.graphDefinitions();
        const auto hasBlankInstrumentContract = std::ranges::any_of(
            editableInstrumentDefinitions, [](const auto& definition) {
                if (definition.scope
                        != cydaw::graph::GraphScope::inlineTrack
                    || definition.role
                        != cydaw::graph::GraphRole::instrument
                    || !definition.nodes.empty()
                    || definition.ports.size() != 2)
                    return false;
                const auto hasMidiInput = std::ranges::any_of(
                    definition.ports, [](const auto& port) {
                        return port.type == cydaw::graph::PortType::midi
                               && port.direction
                                   == cydaw::graph::PortDirection::input;
                    });
                const auto hasStereoOutput = std::ranges::any_of(
                    definition.ports, [](const auto& port) {
                        return port.type == cydaw::graph::PortType::audio
                               && port.audioType
                                   == cydaw::graph::AudioPortType::stereo
                               && port.direction
                                   == cydaw::graph::PortDirection::output;
                    });
                return hasMidiInput && hasStereoOutput;
            });
        passed = expect(
                     editableInstrumentResult.wasOk()
                         && editableInstrumentCompile.wasOk()
                         && !editableInstrumentPlan.enabled
                         && hasBlankInstrumentContract,
                     "new instrument Graph publishes typed MIDI/audio ports without a hidden synth")
                 && passed;

        cydaw::app::NodeGraphComponent instrumentEffectGraph;
        instrumentEffectGraph.initialiseEmptyMidiTrackGraph();
        const auto insertInstrument =
            instrumentEffectGraph.insertFactoryDevice("poly-sine");
        const auto insertDrive =
            instrumentEffectGraph.insertFactoryDevice("drive");
        const auto openPolySubgraph =
            instrumentEffectGraph.openSubgraphEditor("poly-sine");
        const auto rejectUnknownSubgraph =
            instrumentEffectGraph.openSubgraphEditor("missing-subgraph");
        const auto instrumentEffectExport =
            instrumentEffectGraph.runtimeGraphExport();
        const auto instrumentEffectDefinitions =
            instrumentEffectGraph.graphDefinitions();
        const auto hasRuntimeNodeKind = [&instrumentEffectExport](
                                            const auto kind) {
            return std::ranges::any_of(
                instrumentEffectExport.nativeGraph.nodes,
                [kind](const auto& node) { return node.kind == kind; });
        };
        const auto hasPublishedTarget = [&instrumentEffectExport](
                                            const std::string_view suffix) {
            return std::ranges::any_of(
                instrumentEffectExport.publishedParameters,
                [suffix](const auto& parameter) {
                    return parameter.exportedToHost
                           && parameter.targetParameterKey.ends_with(suffix);
                });
        };
        passed = expect(
                     insertInstrument.wasOk() && insertDrive.wasOk()
                         && openPolySubgraph.wasOk()
                         && rejectUnknownSubgraph.failed()
                         && instrumentEffectExport.compilationResult.wasOk()
                         && instrumentEffectExport.instrumentPlan.enabled
                         && instrumentEffectExport.nativeGraph.nodes.size() == 3
                         && instrumentEffectExport.nativeGraph.connections.size() == 2
                         && hasRuntimeNodeKind(
                             cydaw::engine::NativeDspNodeKind::input)
                         && hasRuntimeNodeKind(
                             cydaw::engine::NativeDspNodeKind::drive)
                         && hasRuntimeNodeKind(
                             cydaw::engine::NativeDspNodeKind::output)
                         && hasPublishedTarget("level")
                         && hasPublishedTarget("drive-db"),
                     "instrument Graph exports its synth renderer and downstream native effects")
                 && passed;
        const auto polyDefinition = std::ranges::find_if(
            instrumentEffectDefinitions, [](const auto& definition) {
                return definition.reference.id.ends_with(
                    ".module.poly-sine");
            });
        const auto& instrumentRoot = instrumentEffectDefinitions.back();
        const auto polyInstance = std::ranges::find_if(
            instrumentRoot.nodes, [](const auto& node) {
                return node.graph.has_value()
                       && node.graph->id.ends_with(".module.poly-sine");
            });
        const auto hasExactPolyPorts = [](const auto& ports) {
            const auto midiInput = std::ranges::find_if(
                ports, [](const auto& port) {
                    return port.id == 10'001
                           && port.direction
                                  == cydaw::graph::PortDirection::input
                           && port.type == cydaw::graph::PortType::midi;
                });
            const auto stereoOutput = std::ranges::find_if(
                ports, [](const auto& port) {
                    return port.id == 10'006
                           && port.direction
                                  == cydaw::graph::PortDirection::output
                           && port.type == cydaw::graph::PortType::audio
                           && port.audioType
                                  == cydaw::graph::AudioPortType::stereo
                           && port.channels == 2;
                });
            return midiInput != ports.end() && stereoOutput != ports.end();
        };
        const auto hasBoundAdsrInterface = [](const auto& definition) {
            const auto full = std::ranges::find_if(
                definition.interfaces, [](const auto& interfaceDefinition) {
                    return interfaceDefinition.mode
                           == cydaw::graph::InterfaceLayoutMode::freeform;
                });
            if (full == definition.interfaces.end())
                return false;
            const auto envelope = std::ranges::find_if(
                full->controls, [](const auto& control) {
                    return control.kind
                           == cydaw::graph::InterfaceControlKind::envelope;
                });
            if (envelope == full->controls.end()
                || !envelope->parameterBinding.has_value()
                || !envelope->secondaryParameterBinding.has_value()
                || !envelope->tertiaryParameterBinding.has_value()
                || !envelope->quaternaryParameterBinding.has_value())
                return false;
            const std::set<cydaw::graph::ParameterId> bindings {
                *envelope->parameterBinding,
                *envelope->secondaryParameterBinding,
                *envelope->tertiaryParameterBinding,
                *envelope->quaternaryParameterBinding
            };
            return bindings.size() == 4;
        };
        passed = expect(
                     polyDefinition != instrumentEffectDefinitions.end()
                         && polyInstance != instrumentRoot.nodes.end()
                         && hasExactPolyPorts(polyDefinition->ports)
                         && hasExactPolyPorts(polyInstance->ports)
                         && hasBoundAdsrInterface(*polyDefinition),
                     "reusable Graph instruments preserve typed ports and expose a bound ADSR interface")
                 && passed;
        cydaw::engine::PolySynthGraphPlan polyPlan;
        const auto polyPresetLoad = graph.loadFactoryPreset(0);
        const auto polyCompile = graph.compilePolySynthGraph(polyPlan);
        passed = expect(polyPresetLoad.wasOk()
                            && polyCompile.wasOk()
                            && polyPlan.enabled
                            && polyPlan.graphNodeCount == 7
                            && polyPlan.voiceCount == 16
                            && std::abs(polyPlan.sustainLevel - 0.72f) < 0.001f,
                        "polyphonic factory instrument compiles from visible graph nodes")
                 && passed;
        const auto polyInterface = graph.graphInterfaceState();
        const auto waveformParameter = std::find_if(
            polyInterface.parameters.begin(), polyInterface.parameters.end(),
            [](const auto& parameter) {
                return parameter.key.endsWith("polyOscillator.waveform");
            });
        auto polyInterfaceControlsDsp = waveformParameter
                                       != polyInterface.parameters.end()
                                       && graph.setGraphInterfaceParameter(
                                           waveformParameter->key, 3.0)
                                       && graph.compilePolySynthGraph(polyPlan).wasOk()
                                       && polyPlan.waveform
                                              == cydaw::engine::PolyOscillatorWaveform::square;
        passed = expect(polyInterfaceControlsDsp,
                        "graph presentation controls the compiled poly synth DSP")
                 && passed;
        ConstantAudioSource presetInput(0.1f);
        cydaw::app::GainPanAudioSource presetRenderer(presetInput);
        presetRenderer.prepareToPlay(512, 48'000.0);
        for (std::size_t presetIndex = 0;
             presetIndex < graph.factoryPresetCount();
             ++presetIndex) {
            const auto loadResult = graph.loadFactoryPreset(presetIndex);
            const auto presetGraph = graph.nativeDspGraph();
            const auto compileResult = presetRenderer.setNativeDspGraph(presetGraph);
            if (loadResult.failed())
                std::cerr << "Factory preset load diagnostic: "
                          << loadResult.getErrorMessage() << '\n';
            if (compileResult.failed())
                std::cerr << "Factory preset compile diagnostic: "
                          << compileResult.getErrorMessage() << '\n';
            passed = expect(loadResult.wasOk()
                                && compileResult.wasOk()
                                && !presetGraph.nodes.empty()
                                && presetGraph.outputNodeId != 0,
                            "factory preset loads and compiles to native DSP") && passed;
        }
        passed = expect(graph.loadFactoryPreset(graph.factoryPresetCount()).failed(),
                        "invalid factory preset index is rejected") && passed;

        const auto noisePresetReload = graph.insertFactoryDevice(
            "noise-atmosphere");
        const auto presetState = graph.serialise();
        const auto* presetObject = presetState.getDynamicObject();
        auto delayInterfaceReady = false;
        if (presetObject != nullptr) {
            if (const auto* presetNodes = presetObject->getProperty("nodes").getArray()) {
                for (const auto& nodeValue : *presetNodes) {
                    const auto* node = nodeValue.getDynamicObject();
                    if (node == nullptr || node->getProperty("type").toString() != "delay")
                        continue;
                    const auto* parameters = node->getProperty("parameters").getDynamicObject();
                    const auto* interfaceObject = node->getProperty("interface").getDynamicObject();
                    const auto* published = interfaceObject != nullptr
                                                ? interfaceObject
                                                      ->getProperty("parameters").getArray()
                                                : nullptr;
                    delayInterfaceReady = parameters != nullptr
                                          && published != nullptr
                                          && published->size() == 3
                                          && std::abs(static_cast<double>(
                                                  parameters->getProperty("delayMilliseconds"))
                                                      - 520.0) < 0.001;
                    break;
                }
            }
        }
        cydaw::app::NodeGraphComponent restoredPreset;
        restoredPreset.restore(juce::JSON::parse(juce::JSON::toString(presetState, false)));
        passed = expect(presetObject != nullptr
                            && noisePresetReload.wasOk()
                            && delayInterfaceReady
                            && presetObject->getProperty("factoryPreset").toString()
                                   == "noise-atmosphere"
                            && restoredPreset.nativeDspGraph() == graph.nativeDspGraph(),
                        "parameterised factory node UI and topology survive JSON round trip") && passed;

        auto nestedState = cydaw::app::NodeGraphComponent().serialise();
        auto* nestedRoot = nestedState.getDynamicObject();
        auto* nestedModules = nestedRoot != nullptr
                                  ? nestedRoot->getProperty("modules").getArray()
                                  : nullptr;
        auto* nestedNodes = nestedRoot != nullptr
                                ? nestedRoot->getProperty("nodes").getArray()
                                : nullptr;
        auto* nestedConnections = nestedRoot != nullptr
                                      ? nestedRoot->getProperty("connections").getArray()
                                      : nullptr;
        const auto makeParameters = [](const std::initializer_list<
                                           std::pair<const char*, double>>& values) {
            auto parameters = juce::DynamicObject::Ptr(new juce::DynamicObject());
            for (const auto& [key, value] : values)
                parameters->setProperty(key, value);
            return juce::var(parameters.get());
        };
        const auto makeModuleNode = [](const juce::int64 id,
                                       const juce::String& type,
                                       const juce::var& parameters = {}) {
            auto node = juce::DynamicObject::Ptr(new juce::DynamicObject());
            node->setProperty("id", id);
            node->setProperty("type", type);
            node->setProperty("title", type);
            node->setProperty("x", 0.2);
            node->setProperty("y", 0.4);
            if (parameters.getDynamicObject() != nullptr)
                node->setProperty("parameters", parameters);
            return juce::var(node.get());
        };
        const auto makeConnection = [](const juce::int64 source,
                                       const juce::int64 destination) {
            auto connection = juce::DynamicObject::Ptr(new juce::DynamicObject());
            connection->setProperty("source", source);
            connection->setProperty("destination", destination);
            connection->setProperty("kind", 0);
            return juce::var(connection.get());
        };
        const auto makeModule = [](const juce::String& key,
                                   juce::Array<juce::var> nodes,
                                   juce::Array<juce::var> connections) {
            auto module = juce::DynamicObject::Ptr(new juce::DynamicObject());
            module->setProperty("key", key);
            module->setProperty("name", key);
            module->setProperty("nodes", std::move(nodes));
            module->setProperty("connections", std::move(connections));
            return juce::var(module.get());
        };
        if (nestedRoot != nullptr && nestedModules != nullptr
            && nestedNodes != nullptr && nestedConnections != nullptr) {
            juce::Array<juce::var> firstChildren;
            firstChildren.add(makeModuleNode(
                100, "drive", makeParameters({ { "driveDb", 17.0 } })));
            firstChildren.add(makeModuleNode(
                101, "delay", makeParameters({ { "delayMilliseconds", 480.0 },
                                                  { "feedback", 0.55 },
                                                  { "mix", 0.4 } })));
            juce::Array<juce::var> firstLinks;
            firstLinks.add(makeConnection(100, 101));
            nestedModules->add(makeModule("nested-unit-a", std::move(firstChildren),
                                          std::move(firstLinks)));

            juce::Array<juce::var> secondChildren;
            secondChildren.add(makeModuleNode(200, "nested-unit-a"));
            secondChildren.add(makeModuleNode(
                201, "reverb", makeParameters({ { "roomSize", 0.82 },
                                                   { "mix", 0.41 } })));
            juce::Array<juce::var> secondLinks;
            secondLinks.add(makeConnection(200, 201));
            nestedModules->add(makeModule("nested-unit-b", std::move(secondChildren),
                                          std::move(secondLinks)));

            nestedNodes->add(makeModuleNode(12, "nested-unit-b"));
            nestedConnections->clear();
            nestedConnections->add(makeConnection(1, 12));
            nestedConnections->add(makeConnection(12, 2));
            nestedRoot->setProperty("nextNodeId", static_cast<juce::int64>(13));
        }
        cydaw::app::NodeGraphComponent nestedGraph;
        nestedGraph.restore(nestedState);
        const auto nestedDefinitions = nestedGraph.graphDefinitions();
        cydaw::graph::GraphLibrary nestedTypedLibrary;
        const auto nestedTypedInstall = nestedTypedLibrary.installBatch(
            nestedDefinitions);
        const auto nestedTypedCompile = nestedTypedInstall.ok()
            && !nestedDefinitions.empty()
                ? cydaw::engine::compileGraphDefinitionToNativeDsp(
                      nestedTypedLibrary, nestedDefinitions.back().reference)
                : cydaw::engine::GraphDefinitionCompileResult {};
        if (!nestedTypedInstall.ok() || !nestedTypedCompile.ok()) {
            for (const auto& definition : nestedDefinitions) {
                std::cerr << "  definition " << definition.displayName
                          << " ports=" << definition.ports.size()
                          << " nodes=" << definition.nodes.size()
                          << " links=" << definition.connections.size()
                          << '\n';
                for (const auto& port : definition.ports)
                    std::cerr << "    port " << port.id << " "
                              << (port.direction
                                      == cydaw::graph::PortDirection::input
                                      ? "in" : "out")
                              << " type=" << static_cast<int>(port.type)
                              << '\n';
                for (const auto& link : definition.connections)
                    std::cerr << "    link " << link.source.nodeId << ":"
                              << link.source.portId << " -> "
                              << link.destination.nodeId << ":"
                              << link.destination.portId << '\n';
            }
            for (const auto& issue : nestedTypedInstall.issues)
                std::cerr << "Nested typed install: " << issue.message << '\n';
            for (const auto& issue : nestedTypedCompile.validation.issues)
                std::cerr << "Nested typed compile: " << issue.message << '\n';
        }
        const auto expandedGraph = nestedGraph.nativeDspGraph();
        const auto nestedRuntimeExport = nestedGraph.runtimeGraphExport();
        const auto hasExpandedParameters = std::any_of(
            expandedGraph.nodes.begin(), expandedGraph.nodes.end(),
            [](const auto& node) {
                return node.kind == cydaw::engine::NativeDspNodeKind::delay
                       && std::abs(node.parameters.delayMilliseconds - 480.0f) < 0.001f
                       && std::abs(node.parameters.feedback - 0.55f) < 0.001f
                       && std::abs(node.parameters.mix - 0.4f) < 0.001f;
            });
        const auto allPublishedTargetsAreNative = std::all_of(
            nestedRuntimeExport.publishedParameters.begin(),
            nestedRuntimeExport.publishedParameters.end(),
            [&nestedRuntimeExport](const auto& parameter) {
                return !parameter.exportedToHost
                    || std::ranges::any_of(
                        nestedRuntimeExport.nativeGraph.nodes,
                        [&parameter](const auto& node) {
                            return node.id == parameter.targetNodeId;
                        });
            });
        const auto nestedParameterWasRemapped = std::ranges::any_of(
            nestedRuntimeExport.publishedParameters,
            [](const auto& parameter) {
                return parameter.exportedToHost
                    && parameter.targetNodeId != 12
                    && parameter.targetParameterKey.find("node.100.")
                           != std::string::npos;
            });
        const auto nestedCompileResult = presetRenderer.setNativeDspGraph(expandedGraph);
        if (expandedGraph.nodes.size() != 5
            || expandedGraph.connections.size() != 4
            || !hasExpandedParameters
            || nestedCompileResult.failed())
            std::cerr << "Nested Graph diagnostic: nodes="
                      << expandedGraph.nodes.size()
                      << ", connections=" << expandedGraph.connections.size()
                      << ", parameters=" << (hasExpandedParameters ? "ok" : "missing")
                      << ", compile="
                      << nestedCompileResult.getErrorMessage().toStdString()
                      << '\n';
        if (nestedRuntimeExport.publishedParameters.empty()
            || !allPublishedTargetsAreNative
            || !nestedParameterWasRemapped) {
            std::cerr << "Nested published parameter diagnostic: count="
                      << nestedRuntimeExport.publishedParameters.size()
                      << ", nativeTargets="
                      << (allPublishedTargetsAreNative ? "yes" : "no")
                      << ", remapped="
                      << (nestedParameterWasRemapped ? "yes" : "no") << '\n';
            for (const auto& parameter :
                 nestedRuntimeExport.publishedParameters)
                std::cerr << "  id=" << parameter.id
                          << " node=" << parameter.targetNodeId
                          << " key=" << parameter.targetParameterKey << '\n';
            cydaw::graph::GraphLibrary diagnosticLibrary;
            const auto diagnosticDefinitions = nestedGraph.graphDefinitions();
            const auto install = diagnosticLibrary.installBatch(
                diagnosticDefinitions);
            std::cerr << "  typed install=" << (install.ok() ? "yes" : "no")
                      << '\n';
            for (const auto& issue : install.issues)
                std::cerr << "    install: " << issue.message << '\n';
            if (install.ok() && !diagnosticDefinitions.empty()) {
                const auto compiled =
                    cydaw::engine::compileGraphDefinitionToNativeDsp(
                        diagnosticLibrary,
                        diagnosticDefinitions.back().reference);
                std::cerr << "  typed compile="
                          << (compiled.ok() ? "yes" : "no") << '\n';
                for (const auto& issue : compiled.validation.issues)
                    std::cerr << "    compile: " << issue.message << '\n';
            }
        }
        passed = expect(expandedGraph.nodes.size() == 5
                            && expandedGraph.connections.size() == 4
                            && hasExpandedParameters
                            && nestedCompileResult.wasOk(),
                        "nested reusable modules expand to their real parameterised DSP DAG") && passed;
        passed = expect(nestedTypedInstall.ok() && nestedTypedCompile.ok(),
                        "nested reusable modules compile through the typed Graph runtime")
                 && passed;
        passed = expect(!nestedRuntimeExport.publishedParameters.empty()
                            && allPublishedTargetsAreNative
                            && nestedParameterWasRemapped,
                        "published parameters of nested Graph instances resolve to expanded native DSP nodes") && passed;
        const auto nestedRoundTrip = nestedGraph.serialise();
        const auto* nestedRoundTripRoot = nestedRoundTrip.getDynamicObject();
        const auto* nestedRoundTripModules = nestedRoundTripRoot != nullptr
                                                ? nestedRoundTripRoot
                                                      ->getProperty("modules").getArray()
                                                : nullptr;
        const juce::DynamicObject* secondModule = nullptr;
        if (nestedRoundTripModules != nullptr)
            for (const auto& moduleValue : *nestedRoundTripModules)
                if (const auto* candidate = moduleValue.getDynamicObject();
                    candidate != nullptr
                    && candidate->getProperty("key").toString()
                           == "nested-unit-b") {
                    secondModule = candidate;
                    break;
                }
        const auto* nestedInterface = secondModule != nullptr
                                          ? secondModule->getProperty("interface")
                                                .getDynamicObject()
                                          : nullptr;
        const auto* nestedParameters = nestedInterface != nullptr
                                           ? nestedInterface->getProperty("parameters")
                                                 .getArray()
                                           : nullptr;
        passed = expect(nestedParameters != nullptr && nestedParameters->size() == 6,
                        "parent module publishes nested effect parameters") && passed;

        auto invalidAudioState = nestedGraph.serialise();
        auto* invalidAudioRoot = invalidAudioState.getDynamicObject();
        auto* invalidAudioConnections = invalidAudioRoot != nullptr
            ? invalidAudioRoot->getProperty("connections").getArray()
            : nullptr;
        if (invalidAudioConnections != nullptr)
            for (int index = invalidAudioConnections->size(); --index >= 0;) {
                const auto* connection = invalidAudioConnections->getReference(index)
                                             .getDynamicObject();
                if (connection != nullptr
                    && static_cast<juce::int64>(
                           connection->getProperty("destination")) == 2)
                    invalidAudioConnections->remove(index);
            }
        cydaw::app::NodeGraphComponent invalidAudioGraph;
        invalidAudioGraph.restore(invalidAudioState);
        const auto invalidAudioRuntime = invalidAudioGraph.runtimeGraphExport();
        passed = expect(invalidAudioRuntime.compilationResult.failed()
                            && !invalidAudioRuntime.nativeGraph.nodes.empty(),
                        "invalid typed audio Graph reports its DSP error while retaining a recoverable fallback graph")
                 && passed;
        presetRenderer.releaseResources();
    }

    {
        cydaw::app::NodeGraphComponent modulationGraph;
        modulationGraph.initialiseValueModulationGraph();
        const auto interface = modulationGraph.graphInterfaceState();
        const auto rate = std::find_if(
            interface.parameters.begin(), interface.parameters.end(),
            [](const auto& parameter) {
                return parameter.key.endsWith("lfo.rate");
            });
        const auto state = modulationGraph.serialise();
        const auto* root = state.getDynamicObject();
        const auto modulationRuntimeExport =
            modulationGraph.runtimeGraphExport();
        cydaw::app::NativeDspRuntime modulationRuntime;
        modulationRuntime.prepare(48'000.0, 64, 2);
        const auto modulationRuntimeResult = modulationRuntime.setGraph(
            modulationRuntimeExport.nativeGraph);
        juce::AudioBuffer<float> modulationClock(2, 64);
        modulationClock.clear();
        const auto modulationProcessed = modulationRuntime.process(
            modulationClock, 0, modulationClock.getNumSamples());
        const auto modulationValues = modulationRuntime.controlValues();
        const auto modulationStateNodes = root != nullptr
            ? root->getProperty("nodes").getArray() : nullptr;
        const auto visibleLfoNodeId = modulationStateNodes != nullptr
                && !modulationStateNodes->isEmpty()
            ? static_cast<std::uint64_t>(static_cast<juce::int64>(
                  modulationStateNodes->getReference(0)
                      .getProperty("id", juce::var {})))
            : std::uint64_t {};
        const auto hasVisibleLfoTelemetry = std::ranges::any_of(
            modulationRuntimeExport.controlTelemetryTargets,
            [visibleLfoNodeId](const auto& target) {
                return target.authoredNodeId == visibleLfoNodeId;
            });
        const auto hasPublishedOutputTelemetry = std::ranges::any_of(
            modulationRuntimeExport.controlTelemetryTargets,
            [&modulationStateNodes](const auto& target) {
                if (modulationStateNodes == nullptr
                    || modulationStateNodes->size() < 2)
                    return false;
                return target.authoredNodeId
                       == static_cast<std::uint64_t>(
                           static_cast<juce::int64>(
                               modulationStateNodes->getReference(1)
                                   .getProperty("id", juce::var {})));
            });
        passed = expect(
                     rate != interface.parameters.end()
                         && modulationGraph.setGraphInterfaceParameter(
                             rate->key, 2.0)
                         && std::abs(modulationGraph.evaluateValueOutput(0.125)
                                     - 1.0)
                                < 0.001
                         && root != nullptr
                         && static_cast<bool>(root->getProperty("valueGraph"))
                         && modulationRuntimeExport.compilationResult.wasOk()
                         && modulationRuntimeResult.wasOk()
                         && modulationProcessed
                         && !modulationValues.empty()
                         && hasVisibleLfoTelemetry
                         && hasPublishedOutputTelemetry,
                     "Value graph LFO evaluates a reusable modulation output")
                 && passed;

        const auto makeValueNode = [](const juce::int64 id,
                                      const juce::String& type,
                                      const double operand) {
            auto parameters = juce::DynamicObject::Ptr(new juce::DynamicObject());
            parameters->setProperty("valueOperand", operand);
            parameters->setProperty("mapInputMinimum", 0.0);
            parameters->setProperty("mapInputMaximum", 1.0);
            parameters->setProperty("mapOutputMinimum", -1.0);
            parameters->setProperty("mapOutputMaximum", 1.0);
            auto node = juce::DynamicObject::Ptr(new juce::DynamicObject());
            node->setProperty("id", id);
            node->setProperty("type", type);
            node->setProperty("title", type);
            node->setProperty("x", 0.1 + 0.18 * static_cast<double>(id - 1));
            node->setProperty("y", 0.4);
            node->setProperty("parameters", juce::var(parameters.get()));
            return juce::var(node.get());
        };
        const auto makeControlLink = [](const juce::int64 source,
                                        const juce::int64 destination) {
            auto link = juce::DynamicObject::Ptr(new juce::DynamicObject());
            link->setProperty("source", source);
            link->setProperty("destination", destination);
            link->setProperty("kind", 2);
            return juce::var(link.get());
        };
        auto operatorState = modulationGraph.serialise();
        auto* operatorRoot = operatorState.getDynamicObject();
        auto* operatorNodes = operatorRoot != nullptr
                                  ? operatorRoot->getProperty("nodes").getArray()
                                  : nullptr;
        auto* operatorLinks = operatorRoot != nullptr
                                  ? operatorRoot->getProperty("connections").getArray()
                                  : nullptr;
        if (operatorNodes != nullptr && operatorLinks != nullptr) {
            operatorNodes->clear();
            operatorLinks->clear();
            operatorNodes->add(makeValueNode(1, "value-constant", 0.25));
            operatorNodes->add(makeValueNode(2, "map-range", 0.0));
            operatorNodes->add(makeValueNode(3, "logic-greater", -0.75));
            operatorNodes->add(makeValueNode(4, "graph-value-output", 0.0));
            operatorLinks->add(makeControlLink(1, 2));
            operatorLinks->add(makeControlLink(2, 3));
            operatorLinks->add(makeControlLink(3, 4));
            operatorRoot->setProperty("nextNodeId", static_cast<juce::int64>(5));
        }
        cydaw::app::NodeGraphComponent operatorGraph;
        operatorGraph.restore(operatorState);
        passed = expect(std::abs(operatorGraph.evaluateValueOutput(0.0) - 1.0)
                                < 0.001,
                        "mapping, float arithmetic and boolean nodes evaluate through a typed Value graph")
                 && passed;

        auto trigonometryState = modulationGraph.serialise();
        auto* trigonometryRoot = trigonometryState.getDynamicObject();
        auto* trigonometryNodes = trigonometryRoot != nullptr
            ? trigonometryRoot->getProperty("nodes").getArray() : nullptr;
        auto* trigonometryLinks = trigonometryRoot != nullptr
            ? trigonometryRoot->getProperty("connections").getArray()
            : nullptr;
        if (trigonometryNodes != nullptr && trigonometryLinks != nullptr) {
            trigonometryNodes->clear();
            trigonometryLinks->clear();
            trigonometryNodes->add(makeValueNode(
                1, "value-constant",
                juce::MathConstants<double>::halfPi));
            trigonometryNodes->add(makeValueNode(2, "value-sine", 0.0));
            trigonometryNodes->add(makeValueNode(
                3, "graph-value-output", 0.0));
            trigonometryLinks->add(makeControlLink(1, 2));
            trigonometryLinks->add(makeControlLink(2, 3));
            trigonometryRoot->setProperty("nextNodeId",
                                           static_cast<juce::int64>(4));
        }
        cydaw::app::NodeGraphComponent trigonometryGraph;
        trigonometryGraph.restore(trigonometryState);
        passed = expect(
                     std::abs(trigonometryGraph.evaluateValueOutput(0.0) - 1.0)
                         < 0.001,
                     "Graph editor live Value preview evaluates sine/cosine operator nodes consistently with the runtime")
                 && passed;

        cydaw::app::NodeGraphComponent dynamicsPortGraph;
        const auto insertedCompressor =
            dynamicsPortGraph.insertNode("compressor");
        const auto dynamicsInterface =
            dynamicsPortGraph.graphInterfaceState();
        const auto thresholdParameter = std::ranges::find_if(
            dynamicsInterface.parameters, [](const auto& parameter) {
                return parameter.key.endsWith("compressor.threshold");
            });
        const auto ratioParameter = std::ranges::find_if(
            dynamicsInterface.parameters, [](const auto& parameter) {
                return parameter.key.endsWith("compressor.ratio");
            });
        const auto changedDynamics = insertedCompressor.wasOk()
            && thresholdParameter != dynamicsInterface.parameters.end()
            && ratioParameter != dynamicsInterface.parameters.end()
            && dynamicsPortGraph.setGraphInterfaceParameter(
                thresholdParameter->key, -24.0)
            && dynamicsPortGraph.setGraphInterfaceParameter(
                ratioParameter->key, 6.0);
        const auto dynamicsDefinitions = dynamicsPortGraph.graphDefinitions();
        const auto* compiledCompressor = !dynamicsDefinitions.empty()
            ? [&dynamicsDefinitions]() -> const cydaw::graph::GraphNode* {
                  const auto& definition = dynamicsDefinitions.back();
                  const auto found = std::ranges::find(
                      definition.nodes, std::string("core.compressor"),
                      &cydaw::graph::GraphNode::processorKey);
                  return found != definition.nodes.end() ? &*found : nullptr;
              }()
            : nullptr;
        const auto thresholdPort = compiledCompressor != nullptr
            ? std::ranges::find(compiledCompressor->ports,
                                std::string("threshold"),
                                &cydaw::graph::GraphPort::key)
            : std::vector<cydaw::graph::GraphPort>::const_iterator {};
        const auto ratioPort = compiledCompressor != nullptr
            ? std::ranges::find(compiledCompressor->ports,
                                std::string("ratio"),
                                &cydaw::graph::GraphPort::key)
            : std::vector<cydaw::graph::GraphPort>::const_iterator {};
        passed = expect(
                     changedDynamics && compiledCompressor != nullptr
                         && thresholdPort != compiledCompressor->ports.end()
                         && ratioPort != compiledCompressor->ports.end()
                         && std::abs(thresholdPort->defaultValue + 24.0) < 0.001
                         && std::abs(ratioPort->defaultValue - 6.0) < 0.001,
                     "Graph dynamics UI, modulatable ports and exported DSP share one live parameter value")
                 && passed;

        cydaw::app::NodeGraphComponent portDefaultsGraph;
        portDefaultsGraph.initialiseValueModulationGraph();
        const auto insertedAdd = portDefaultsGraph.insertNode("value-add");
        auto authoredDefaultsState = portDefaultsGraph.serialise();
        auto* authoredDefaultsRoot = authoredDefaultsState.getDynamicObject();
        auto* authoredDefaultsNodes = authoredDefaultsRoot != nullptr
            ? authoredDefaultsRoot->getProperty("nodes").getArray() : nullptr;
        if (authoredDefaultsNodes != nullptr)
            for (auto& nodeValue : *authoredDefaultsNodes)
                if (auto* node = nodeValue.getDynamicObject();
                    node != nullptr
                    && node->getProperty("type").toString() == "value-add") {
                    juce::Array<juce::var> defaults;
                    for (const auto [id, value] : {
                             std::pair<juce::int64, double> { 1, 0.25 },
                             std::pair<juce::int64, double> { 2, 0.75 } }) {
                        auto item = juce::DynamicObject::Ptr(
                            new juce::DynamicObject());
                        item->setProperty("id", id);
                        item->setProperty("value", value);
                        defaults.add(juce::var(item.get()));
                    }
                    node->setProperty("portDefaults", defaults);
                    break;
                }
        cydaw::app::NodeGraphComponent restoredDefaultsGraph;
        restoredDefaultsGraph.restore(authoredDefaultsState);
        const auto restoredDefaultDefinitions =
            restoredDefaultsGraph.graphDefinitions();
        const auto* restoredAdd = !restoredDefaultDefinitions.empty()
            ? [&restoredDefaultDefinitions]()
                  -> const cydaw::graph::GraphNode* {
                  const auto& definition = restoredDefaultDefinitions.back();
                  const auto found = std::ranges::find(
                      definition.nodes, std::string("core.add"),
                      &cydaw::graph::GraphNode::processorKey);
                  return found != definition.nodes.end() ? &*found : nullptr;
              }()
            : nullptr;
        const auto restoredA = restoredAdd != nullptr
            ? std::ranges::find(restoredAdd->ports,
                                cydaw::graph::PortId { 1 },
                                &cydaw::graph::GraphPort::id)
            : std::vector<cydaw::graph::GraphPort>::const_iterator {};
        const auto restoredB = restoredAdd != nullptr
            ? std::ranges::find(restoredAdd->ports,
                                cydaw::graph::PortId { 2 },
                                &cydaw::graph::GraphPort::id)
            : std::vector<cydaw::graph::GraphPort>::const_iterator {};
        const auto restoredRoundTrip = restoredDefaultsGraph.serialise();
        const auto* restoredRoundTripRoot = restoredRoundTrip.getDynamicObject();
        const auto* restoredRoundTripNodes = restoredRoundTripRoot != nullptr
            ? restoredRoundTripRoot->getProperty("nodes").getArray() : nullptr;
        auto roundTripDefaults = false;
        if (restoredRoundTripNodes != nullptr)
            for (const auto& nodeValue : *restoredRoundTripNodes)
                if (const auto* node = nodeValue.getDynamicObject();
                    node != nullptr
                    && node->getProperty("type").toString() == "value-add") {
                    const auto* values = node->getProperty("portDefaults")
                                             .getArray();
                    roundTripDefaults = values != nullptr
                        && values->size() == 2;
                    break;
                }
        const auto selectedAddPort = restoredDefaultsGraph.selectNodePort(
            "value-add", "a");
        const auto copiedAdd = restoredDefaultsGraph.keyPressed(
            juce::KeyPress('C', juce::ModifierKeys::commandModifier, 'C'));
        const auto pastedAdd = restoredDefaultsGraph.keyPressed(
            juce::KeyPress('V', juce::ModifierKeys::commandModifier, 'V'));
        const auto pastedState = restoredDefaultsGraph.serialise();
        const auto* pastedRoot = pastedState.getDynamicObject();
        const auto* pastedNodes = pastedRoot != nullptr
            ? pastedRoot->getProperty("nodes").getArray() : nullptr;
        auto pastedAddCount = 0;
        auto pastedDefaultsPreserved = true;
        if (pastedNodes != nullptr)
            for (const auto& nodeValue : *pastedNodes)
                if (const auto* node = nodeValue.getDynamicObject();
                    node != nullptr
                    && node->getProperty("type").toString() == "value-add") {
                    ++pastedAddCount;
                    const auto* values = node->getProperty("portDefaults")
                                             .getArray();
                    if (values == nullptr || values->size() != 2)
                        pastedDefaultsPreserved = false;
                }
        passed = expect(
                     insertedAdd.wasOk() && restoredAdd != nullptr
                         && restoredA != restoredAdd->ports.end()
                         && restoredB != restoredAdd->ports.end()
                         && std::abs(restoredA->defaultValue - 0.25) < 0.001
                         && std::abs(restoredB->defaultValue - 0.75) < 0.001
                         && roundTripDefaults && selectedAddPort && copiedAdd
                         && pastedAdd && pastedAddCount == 2
                         && pastedDefaultsPreserved,
                     "Graph editor preserves independent pin defaults through schema-24 save, compilation and copy/paste")
                 && passed;

        auto nodePresetState = authoredDefaultsState.clone();
        if (auto* rootObject = nodePresetState.getDynamicObject())
            if (auto* values = rootObject->getProperty("nodes").getArray())
                for (auto& nodeValue : *values)
                    if (auto* node = nodeValue.getDynamicObject();
                        node != nullptr
                        && node->getProperty("type").toString()
                               == "value-add") {
                        if (auto* parameters = node->getProperty("parameters")
                                                   .getDynamicObject())
                            parameters->setProperty("valueOperand", 0.625);
                        break;
                    }
        cydaw::app::NodeGraphComponent nodePresetGraph;
        nodePresetGraph.restore(nodePresetState);
        const auto nodePresetConstant = nodePresetGraph.insertNode(
            "value-constant");
        const auto nodePresetCable = nodePresetGraph.connectNodePorts(
            "value-constant", "value", "value-add", "a", true);
        const auto nodePresetFile = tempRoot.getChildFile(
            "Creative Add.cydawnode");
        const auto savedNodePreset = nodePresetGraph.saveNodeParameterPreset(
            "value-add", nodePresetFile, "Creative Add");
        auto changedNodePresetState = nodePresetGraph.serialise();
        auto* changedPresetRoot = changedNodePresetState.getDynamicObject();
        auto* changedPresetNodes = changedPresetRoot != nullptr
            ? changedPresetRoot->getProperty("nodes").getArray() : nullptr;
        if (changedPresetNodes != nullptr)
            for (auto& nodeValue : *changedPresetNodes)
                if (auto* node = nodeValue.getDynamicObject();
                    node != nullptr
                    && node->getProperty("type").toString() == "value-add") {
                    if (auto* parameters = node->getProperty("parameters")
                                               .getDynamicObject())
                        parameters->setProperty("valueOperand", -0.875);
                    if (auto* defaults = node->getProperty("portDefaults")
                                             .getArray())
                        for (auto& defaultValue : *defaults)
                            if (auto* item = defaultValue.getDynamicObject())
                                item->setProperty("value", -0.5);
                    break;
                }
        nodePresetGraph.restore(changedNodePresetState);
        const auto linksBeforeNodePreset = nodePresetGraph.serialise()
            .getProperty("connections", juce::var {}).getArray()->size();
        const auto loadedNodePreset = nodePresetGraph.loadNodeParameterPreset(
            "value-add", nodePresetFile);
        const auto appliedNodePresetState = nodePresetGraph.serialise();
        const auto* appliedPresetRoot = appliedNodePresetState.getDynamicObject();
        const auto* appliedPresetNodes = appliedPresetRoot != nullptr
            ? appliedPresetRoot->getProperty("nodes").getArray() : nullptr;
        const auto* appliedPresetLinks = appliedPresetRoot != nullptr
            ? appliedPresetRoot->getProperty("connections").getArray()
            : nullptr;
        auto appliedOperand = 0.0;
        std::array<double, 2> appliedDefaults {};
        auto appliedDefaultCount = 0;
        if (appliedPresetNodes != nullptr)
            for (const auto& nodeValue : *appliedPresetNodes)
                if (const auto* node = nodeValue.getDynamicObject();
                    node != nullptr
                    && node->getProperty("type").toString() == "value-add") {
                    if (const auto* parameters = node->getProperty("parameters")
                                                     .getDynamicObject())
                        appliedOperand = static_cast<double>(
                            parameters->getProperty("valueOperand"));
                    if (const auto* defaults = node->getProperty("portDefaults")
                                                   .getArray())
                        for (const auto& defaultValue : *defaults)
                            if (const auto* item = defaultValue.getDynamicObject();
                                item != nullptr && appliedDefaultCount < 2)
                                appliedDefaults[static_cast<std::size_t>(
                                    appliedDefaultCount++)]
                                    = static_cast<double>(
                                        item->getProperty("value"));
                    break;
                }
        const auto nodePresetJson = juce::JSON::parse(
            nodePresetFile.loadFileAsString());
        const auto invalidNodePresetTarget =
            nodePresetGraph.insertNode("lfo-core");
        const auto incompatibleNodePreset =
            nodePresetGraph.loadNodeParameterPreset("lfo-core",
                                                    nodePresetFile);
        auto beforeNodePresetDrop = appliedNodePresetState.clone();
        if (auto* dropPreparationRoot =
                beforeNodePresetDrop.getDynamicObject())
            if (auto* values = dropPreparationRoot->getProperty("nodes")
                                   .getArray())
                for (auto& nodeValue : *values)
                    if (auto* node = nodeValue.getDynamicObject();
                        node != nullptr
                        && node->getProperty("type").toString()
                               == "value-add") {
                        if (auto* parameters = node->getProperty("parameters")
                                                   .getDynamicObject())
                            parameters->setProperty("valueOperand", -0.25);
                        break;
                    }
        nodePresetGraph.restore(beforeNodePresetDrop);
        nodePresetGraph.setSize(1'000, 700);
        const auto selectedNodePresetDropTarget =
            nodePresetGraph.selectNodePort("value-add", "a");
        juce::StringArray nodePresetDropFiles;
        nodePresetDropFiles.add(nodePresetFile.getFullPathName());
        const auto acceptsNodePresetDrop =
            nodePresetGraph.isInterestedInFileDrag(nodePresetDropFiles);
        nodePresetGraph.fileDragEnter(nodePresetDropFiles, 500, 600);
        nodePresetGraph.filesDropped(nodePresetDropFiles, 500, 600);
        const auto afterNodePresetDrop = nodePresetGraph.serialise();
        const auto* dropRoot = afterNodePresetDrop.getDynamicObject();
        const auto* dropNodes = dropRoot != nullptr
            ? dropRoot->getProperty("nodes").getArray() : nullptr;
        const auto* dropConnections = dropRoot != nullptr
            ? dropRoot->getProperty("connections").getArray() : nullptr;
        auto droppedOperand = 0.0;
        if (dropNodes != nullptr)
            for (const auto& nodeValue : *dropNodes)
                if (const auto* node = nodeValue.getDynamicObject();
                    node != nullptr
                    && node->getProperty("type").toString() == "value-add") {
                    if (const auto* parameters = node->getProperty("parameters")
                                                     .getDynamicObject())
                        droppedOperand = static_cast<double>(
                            parameters->getProperty("valueOperand"));
                    break;
                }
        const auto undidNodePresetDrop = nodePresetGraph.keyPressed(
            juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 'Z'));
        const auto undoNodePresetDropState = nodePresetGraph.serialise();
        auto undoNodePresetDropOperand = 0.0;
        if (const auto* undoDropRoot =
                undoNodePresetDropState.getDynamicObject())
            if (const auto* values = undoDropRoot->getProperty("nodes")
                                         .getArray())
                for (const auto& nodeValue : *values)
                    if (const auto* node = nodeValue.getDynamicObject();
                        node != nullptr
                        && node->getProperty("type").toString()
                               == "value-add") {
                        if (const auto* parameters =
                                node->getProperty("parameters")
                                    .getDynamicObject())
                            undoNodePresetDropOperand = static_cast<double>(
                                parameters->getProperty("valueOperand"));
                        break;
                    }
        const auto redidNodePresetDrop = nodePresetGraph.keyPressed(
            juce::KeyPress('Z', juce::ModifierKeys::commandModifier
                                    | juce::ModifierKeys::shiftModifier,
                           'Z'));
        const auto redoNodePresetDropState = nodePresetGraph.serialise();
        auto redoNodePresetDropOperand = 0.0;
        if (const auto* redoDropRoot =
                redoNodePresetDropState.getDynamicObject())
            if (const auto* values = redoDropRoot->getProperty("nodes")
                                         .getArray())
                for (const auto& nodeValue : *values)
                    if (const auto* node = nodeValue.getDynamicObject();
                        node != nullptr
                        && node->getProperty("type").toString()
                               == "value-add") {
                        if (const auto* parameters =
                                node->getProperty("parameters")
                                    .getDynamicObject())
                            redoNodePresetDropOperand = static_cast<double>(
                                parameters->getProperty("valueOperand"));
                        break;
                    }
        passed = expect(
                     nodePresetConstant.wasOk() && nodePresetCable.wasOk()
                         && savedNodePreset.wasOk() && loadedNodePreset.wasOk()
                         && nodePresetJson.getProperty("format", juce::var {})
                                .toString()
                                == "CyDAW Node Parameter Preset"
                         && std::abs(appliedOperand - 0.625) < 0.001
                         && appliedDefaultCount == 2
                         && std::abs(appliedDefaults[0] - 0.25) < 0.001
                         && std::abs(appliedDefaults[1] - 0.75) < 0.001
                         && appliedPresetLinks != nullptr
                         && appliedPresetLinks->size()
                                == linksBeforeNodePreset
                         && invalidNodePresetTarget.wasOk()
                         && incompatibleNodePreset.failed()
                         && selectedNodePresetDropTarget
                         && acceptsNodePresetDrop
                         && std::abs(droppedOperand - 0.625) < 0.001
                         && dropConnections != nullptr
                         && dropConnections->size() == linksBeforeNodePreset
                         && undidNodePresetDrop
                         && std::abs(undoNodePresetDropOperand + 0.25) < 0.001
                         && redidNodePresetDrop
                         && std::abs(redoNodePresetDropOperand - 0.625) < 0.001,
                     "portable node presets restore DSP and literal pin values through API and file drop without modifying typed cables")
                 && passed;

        cydaw::app::NodeGraphComponent connectionGraph;
        connectionGraph.initialiseValueModulationGraph();
        const auto insertedConstant = connectionGraph.insertNode(
            "value-constant");
        const auto insertedLfo = connectionGraph.insertNode("lfo-core");
        const auto insertedConnectionAdd = connectionGraph.insertNode(
            "value-add");
        const auto initialConnection = connectionGraph.connectNodePorts(
            "value-constant", "value", "value-add", "a");
        const auto replacedConnection = connectionGraph.connectNodePorts(
            "lfo-core", "value", "value-add", "a", true);
        const auto connectionState = connectionGraph.serialise();
        const auto* connectionRoot = connectionState.getDynamicObject();
        const auto* connectionNodes = connectionRoot != nullptr
            ? connectionRoot->getProperty("nodes").getArray() : nullptr;
        const auto* connectionLinks = connectionRoot != nullptr
            ? connectionRoot->getProperty("connections").getArray() : nullptr;
        juce::int64 lfoNodeId {};
        juce::int64 addNodeId {};
        if (connectionNodes != nullptr)
            for (const auto& nodeValue : *connectionNodes)
                if (const auto* node = nodeValue.getDynamicObject();
                    node != nullptr) {
                    const auto type = node->getProperty("type").toString();
                    if (type == "lfo-core")
                        lfoNodeId = static_cast<juce::int64>(
                            node->getProperty("id"));
                    else if (type == "value-add")
                        addNodeId = static_cast<juce::int64>(
                            node->getProperty("id"));
                }
        auto incomingAddConnections = 0;
        auto replacementSourceIsLfo = false;
        if (connectionLinks != nullptr)
            for (const auto& linkValue : *connectionLinks)
                if (const auto* link = linkValue.getDynamicObject();
                    link != nullptr
                    && static_cast<juce::int64>(
                           link->getProperty("destination")) == addNodeId
                    && static_cast<juce::int64>(
                           link->getProperty("destinationPort")) == 1) {
                    ++incomingAddConnections;
                    replacementSourceIsLfo = static_cast<juce::int64>(
                        link->getProperty("source")) == lfoNodeId;
                }
        passed = expect(
                     insertedConstant.wasOk() && insertedLfo.wasOk()
                         && insertedConnectionAdd.wasOk()
                         && initialConnection.wasOk()
                         && replacedConnection.wasOk()
                         && incomingAddConnections == 1
                         && replacementSourceIsLfo,
                     "Graph editor replaces a single-input cable atomically while preserving typed port routing")
                 && passed;

        cydaw::app::NodeGraphComponent replacementGraph;
        replacementGraph.initialiseValueModulationGraph();
        const auto replacementLfo = replacementGraph.insertNode("lfo-core");
        const auto replacementAdd = replacementGraph.insertNode("value-add");
        const auto replacementCable = replacementGraph.connectNodePorts(
            "lfo-core", "value", "value-add", "a", true);
        const auto replacedNode = replacementGraph.replaceNode(
            "value-add", "value-multiply");
        const auto replacementState = replacementGraph.serialise();
        const auto* replacementRoot = replacementState.getDynamicObject();
        const auto* replacementNodes = replacementRoot != nullptr
            ? replacementRoot->getProperty("nodes").getArray() : nullptr;
        const auto* replacementLinks = replacementRoot != nullptr
            ? replacementRoot->getProperty("connections").getArray() : nullptr;
        juce::int64 multiplyId {};
        if (replacementNodes != nullptr)
            for (const auto& nodeValue : *replacementNodes)
                if (const auto* node = nodeValue.getDynamicObject();
                    node != nullptr
                    && node->getProperty("type").toString()
                           == "value-multiply")
                    multiplyId = static_cast<juce::int64>(
                        node->getProperty("id"));
        auto replacementCablePreserved = false;
        if (replacementLinks != nullptr)
            for (const auto& linkValue : *replacementLinks)
                if (const auto* link = linkValue.getDynamicObject();
                    link != nullptr
                    && static_cast<juce::int64>(
                           link->getProperty("destination")) == multiplyId
                    && static_cast<juce::int64>(
                           link->getProperty("destinationPort")) == 1)
                    replacementCablePreserved = true;
        passed = expect(
                     replacementLfo.wasOk() && replacementAdd.wasOk()
                         && replacementCable.wasOk()
                         && replacedNode.wasOk() && multiplyId != 0
                         && replacementCablePreserved,
                     "Graph editor replaces a node while preserving its exact typed cable contract")
                 && passed;

        const auto splicedNode = replacementGraph.insertNodeOnConnection(
            "lfo-core", "value-multiply", "value-sine");
        const auto splicedState = replacementGraph.serialise();
        const auto* splicedRoot = splicedState.getDynamicObject();
        const auto* splicedNodes = splicedRoot != nullptr
            ? splicedRoot->getProperty("nodes").getArray() : nullptr;
        const auto* splicedLinks = splicedRoot != nullptr
            ? splicedRoot->getProperty("connections").getArray() : nullptr;
        juce::int64 sineId {};
        if (splicedNodes != nullptr)
            for (const auto& nodeValue : *splicedNodes)
                if (const auto* node = nodeValue.getDynamicObject();
                    node != nullptr
                    && node->getProperty("type").toString() == "value-sine")
                    sineId = static_cast<juce::int64>(
                        node->getProperty("id"));
        auto lfoToSine = false;
        auto sineToMultiply = false;
        if (splicedLinks != nullptr)
            for (const auto& linkValue : *splicedLinks)
                if (const auto* link = linkValue.getDynamicObject();
                    link != nullptr) {
                    const auto source = static_cast<juce::int64>(
                        link->getProperty("source"));
                    const auto destination = static_cast<juce::int64>(
                        link->getProperty("destination"));
                    lfoToSine = lfoToSine
                        || (destination == sineId
                            && static_cast<juce::int64>(
                                   link->getProperty("destinationPort")) == 1);
                    sineToMultiply = sineToMultiply
                        || (source == sineId && destination == multiplyId
                            && static_cast<juce::int64>(
                                   link->getProperty("sourcePort")) == 2
                            && static_cast<juce::int64>(
                                   link->getProperty("destinationPort")) == 1);
                }
        passed = expect(
                     splicedNode.wasOk() && sineId != 0 && lfoToSine
                         && sineToMultiply,
                     "Graph editor atomically inserts a compatible node into a typed cable")
                 && passed;

        const auto addedCableRoute = connectionGraph.addConnectionRoutingPoint(
            "lfo-core", "value-add", { 0.46f, 0.72f });
        const auto routedState = connectionGraph.serialise();
        cydaw::app::NodeGraphComponent restoredRoutedGraph;
        restoredRoutedGraph.restore(routedState);
        const auto restoredRoutedState = restoredRoutedGraph.serialise();
        const auto* routedRoot = restoredRoutedState.getDynamicObject();
        const auto* routedLinks = routedRoot != nullptr
            ? routedRoot->getProperty("connections").getArray() : nullptr;
        auto persistedRoute = false;
        if (routedLinks != nullptr)
            for (const auto& linkValue : *routedLinks)
                if (const auto* link = linkValue.getDynamicObject())
                    if (const auto* routes = link->getProperty("routing")
                                                 .getArray();
                        routes != nullptr && routes->size() == 1)
                        persistedRoute = true;
        passed = expect(
                     addedCableRoute.wasOk() && persistedRoute,
                     "Graph editor persists editor-only cable routing points without changing DSP topology")
                 && passed;

        auto compactState = connectionGraph.serialise();
        auto compactFlagInjected = false;
        if (auto* compactRoot = compactState.getDynamicObject())
            if (auto* compactNodes = compactRoot->getProperty("nodes")
                                         .getArray())
                for (auto& nodeValue : *compactNodes)
                    if (auto* node = nodeValue.getDynamicObject();
                        node != nullptr
                        && node->getProperty("type").toString()
                               == "lfo-core") {
                        node->setProperty("collapsed", true);
                        compactFlagInjected = true;
                        break;
                    }
        cydaw::app::NodeGraphComponent restoredCompactGraph;
        restoredCompactGraph.restore(compactState);
        const auto compactRoundTrip = restoredCompactGraph.serialise();
        const auto* compactRoot = compactRoundTrip.getDynamicObject();
        const auto* compactNodes = compactRoot != nullptr
            ? compactRoot->getProperty("nodes").getArray() : nullptr;
        auto compactFlagPersisted = false;
        if (compactNodes != nullptr)
            for (const auto& nodeValue : *compactNodes)
                if (const auto* node = nodeValue.getDynamicObject();
                    node != nullptr
                    && node->getProperty("type").toString() == "lfo-core") {
                    compactFlagPersisted = static_cast<bool>(
                        node->getProperty("collapsed"));
                    break;
                }
        passed = expect(
                     compactFlagInjected && compactFlagPersisted,
                     "Graph editor persists compact node presentation without affecting the graph contract")
                 && passed;

        const auto selectedCommentNode = connectionGraph.selectNodePort(
            "value-add", "a");
        const auto createdComment = connectionGraph.createCommentFromSelection(
            "Routing section");
        const auto commentState = connectionGraph.serialise();
        cydaw::app::NodeGraphComponent restoredCommentGraph;
        restoredCommentGraph.restore(commentState);
        const auto restoredCommentState = restoredCommentGraph.serialise();
        const auto* restoredCommentRoot = restoredCommentState.getDynamicObject();
        const auto* restoredComments = restoredCommentRoot != nullptr
            ? restoredCommentRoot->getProperty("comments").getArray() : nullptr;
        const auto* restoredComment = restoredComments != nullptr
                                         && restoredComments->size() == 1
            ? restoredComments->getReference(0).getDynamicObject() : nullptr;
        passed = expect(
                     selectedCommentNode && createdComment.wasOk()
                         && restoredComment != nullptr
                         && restoredComment->getProperty("title").toString()
                                == "Routing section"
                         && static_cast<double>(
                                restoredComment->getProperty("width")) > 0.1
                         && static_cast<double>(
                                restoredComment->getProperty("height")) > 0.1,
                     "Graph editor persists MetaSound-style comment regions around selected nodes")
                 && passed;

        const auto savedView = connectionGraph.saveViewBookmark(3);
        const auto viewState = connectionGraph.serialise();
        const auto* viewRoot = viewState.getDynamicObject();
        const auto* savedViews = viewRoot != nullptr
            ? viewRoot->getProperty("viewBookmarks").getArray() : nullptr;
        cydaw::app::NodeGraphComponent restoredViewGraph;
        restoredViewGraph.restore(viewState);
        const auto recalledView = restoredViewGraph.recallViewBookmark(3);
        const auto clearedView = restoredViewGraph.clearViewBookmark(3);
        const auto recallAfterClear = restoredViewGraph.recallViewBookmark(3);
        passed = expect(
                     savedView.wasOk() && savedViews != nullptr
                         && savedViews->size() == 1
                         && savedViews->getReference(0).getDynamicObject()
                                != nullptr
                         && static_cast<int>(savedViews->getReference(0)
                                .getDynamicObject()->getProperty("slot")) == 3
                         && recalledView.wasOk() && clearedView.wasOk()
                         && recallAfterClear.failed(),
                     "Graph editor persists and recalls per-Graph canvas view bookmarks")
                 && passed;
    }

    {
        using namespace cydaw::graph;
        const auto primitives = createFactoryPrimitiveRegistry();
        const auto* addDescriptor = primitives.find("core.add");
        GraphDefinition nested;
        nested.reference = { "test.value-instance", { 1, 0, 0 } };
        nested.displayName = "Value Instance";
        nested.role = GraphRole::modulator;
        nested.ports = {
            { 100, "input", "Input", PortType::value,
              AudioPortType::unspecified, PortDirection::input,
              PortRate::control, 1, true, false, 0.0 },
            { 101, "output", "Output", PortType::value,
              AudioPortType::unspecified, PortDirection::output,
              PortRate::control, 1, false, false, 0.0 }
        };
        GraphNode add;
        add.id = 1;
        add.displayName = "Add";
        add.processorKey = "core.add";
        if (addDescriptor != nullptr) {
            add.ports = addDescriptor->ports;
            add.parameters = addDescriptor->parameters;
        }
        if (add.ports.size() >= 2)
            add.ports[1].defaultValue = 0.6;
        nested.nodes.push_back(add);
        nested.connections = {
            { { boundaryNodeId, 100 }, { 1, 1 } },
            { { 1, 3 }, { boundaryNodeId, 101 } }
        };

        GraphDefinition root;
        root.reference = { "test.value-instance-root", { 1, 0, 0 } };
        root.displayName = "Value Instance Root";
        root.role = GraphRole::modulator;
        root.ports = {
            { 201, "output", "Output", PortType::value,
              AudioPortType::unspecified, PortDirection::output,
              PortRate::control, 1, false, false, 0.0 }
        };
        GraphNode instance;
        instance.id = 7;
        instance.displayName = "Instance";
        instance.kind = GraphNodeKind::graphInstance;
        instance.graph = nested.reference;
        instance.ports = nested.ports;
        instance.ports[0].defaultValue = 0.4;
        root.nodes.push_back(instance);
        root.connections = {
            { { 7, 101 }, { boundaryNodeId, 201 } }
        };

        GraphLibrary library;
        const auto installed = library.installBatch({ nested, root });
        const auto compiled = installed.ok()
            ? cydaw::engine::compileGraphDefinitionToNativeDsp(
                  library, root.reference)
            : cydaw::engine::GraphDefinitionCompileResult {};
        cydaw::app::NativeDspRuntime runtime;
        runtime.prepare(48'000.0, 64, 2);
        const auto runtimeResult = compiled.ok()
            ? runtime.setGraph(compiled.graph)
            : juce::Result::fail("Graph did not compile");
        juce::AudioBuffer<float> clock(2, 64);
        clock.clear();
        const auto processed = runtimeResult.wasOk()
            && runtime.process(clock, 0, clock.getNumSamples());
        const auto values = runtime.controlValues();
        const auto addNode = std::ranges::find(
            compiled.graph.nodes, cydaw::engine::NativeDspNodeKind::controlAdd,
            &cydaw::engine::NativeDspNode::kind);
        const auto live = addNode != compiled.graph.nodes.end()
            ? std::ranges::find(
                  values, addNode->id,
                  &std::pair<std::uint64_t, float>::first)
            : values.end();
        passed = expect(addDescriptor != nullptr && installed.ok()
                            && compiled.ok() && runtimeResult.wasOk()
                            && processed && live != values.end()
                            && std::abs(live->second - 1.0f) < 0.001f,
                        "reusable Graph instances preserve their own exposed Value pin defaults")
                 && passed;
    }

    {
        cydaw::app::NodeGraphComponent midiGraph;
        passed = expect(midiGraph.loadFactoryPreset(0).wasOk(),
                        "polyphonic graph preset loads for MIDI primitive compilation")
                 && passed;
        auto midiState = midiGraph.serialise();
        auto* root = midiState.getDynamicObject();
        auto* nodes = root != nullptr ? root->getProperty("nodes").getArray()
                                      : nullptr;
        auto* links = root != nullptr
                          ? root->getProperty("connections").getArray()
                          : nullptr;
        juce::int64 midiInput {};
        juce::int64 allocator {};
        if (nodes != nullptr)
            for (const auto& value : *nodes)
                if (const auto* node = value.getDynamicObject()) {
                    const auto type = node->getProperty("type").toString();
                    if (type == "midi-input" || type == "graph-midi-input")
                        midiInput = static_cast<juce::int64>(node->getProperty("id"));
                    else if (type == "voice-allocator")
                        allocator = static_cast<juce::int64>(node->getProperty("id"));
                }
        if (allocator == 0 && root != nullptr) {
            if (auto* modules = root->getProperty("modules").getArray())
                for (auto& moduleValue : *modules) {
                    auto* module = moduleValue.getDynamicObject();
                    if (module == nullptr
                        || module->getProperty("key").toString() != "poly-sine")
                        continue;
                    nodes = module->getProperty("nodes").getArray();
                    links = module->getProperty("connections").getArray();
                    if (nodes != nullptr)
                        for (const auto& value : *nodes)
                            if (const auto* node = value.getDynamicObject()) {
                                const auto type = node->getProperty("type").toString();
                                if (type == "midi-input"
                                    || type == "graph-midi-input")
                                    midiInput = static_cast<juce::int64>(
                                        node->getProperty("id"));
                                else if (type == "voice-allocator")
                                    allocator = static_cast<juce::int64>(
                                        node->getProperty("id"));
                            }
                    break;
                }
        }
        const auto makeMidiNode = [](const juce::int64 id,
                                     const juce::String& type,
                                     const std::initializer_list<
                                         std::pair<const char*, double>>& values) {
            auto parameters = juce::DynamicObject::Ptr(new juce::DynamicObject());
            for (const auto& [key, value] : values)
                parameters->setProperty(key, value);
            auto node = juce::DynamicObject::Ptr(new juce::DynamicObject());
            node->setProperty("id", id);
            node->setProperty("type", type);
            node->setProperty("title", type);
            node->setProperty("x", 0.12 + 0.12 * static_cast<double>(id - 100));
            node->setProperty("y", 0.42);
            node->setProperty("parameters", juce::var(parameters.get()));
            return juce::var(node.get());
        };
        const auto makeMidiLink = [](const juce::int64 source,
                                     const juce::int64 destination) {
            auto link = juce::DynamicObject::Ptr(new juce::DynamicObject());
            link->setProperty("source", source);
            link->setProperty("destination", destination);
            link->setProperty("kind", 1);
            return juce::var(link.get());
        };
        if (nodes != nullptr && links != nullptr && midiInput != 0
            && allocator != 0) {
            for (int index = links->size(); --index >= 0;) {
                const auto* link = links->getReference(index).getDynamicObject();
                if (link != nullptr
                    && static_cast<juce::int64>(link->getProperty("source"))
                           == midiInput
                    && static_cast<juce::int64>(link->getProperty("destination"))
                           == allocator)
                    links->remove(index);
            }
            nodes->add(makeMidiNode(100, "midi-transpose",
                                    { { "midiTranspose", 12.0 } }));
            nodes->add(makeMidiNode(101, "note-filter",
                                    { { "midiMinimumNote", 36.0 },
                                      { "midiMaximumNote", 84.0 } }));
            nodes->add(makeMidiNode(102, "velocity-map",
                                    { { "midiVelocityScale", 0.5 } }));
            links->add(makeMidiLink(midiInput, 100));
            links->add(makeMidiLink(100, 101));
            links->add(makeMidiLink(101, 102));
            links->add(makeMidiLink(102, allocator));
            root->setProperty("nextNodeId", static_cast<juce::int64>(103));
        }
        cydaw::app::NodeGraphComponent transformedMidiGraph;
        transformedMidiGraph.restore(midiState);
        cydaw::engine::PolySynthGraphPlan midiPlan;
        passed = expect(
                     transformedMidiGraph.compilePolySynthGraph(midiPlan).wasOk()
                         && midiPlan.enabled && midiPlan.transposeSemitones == 12
                         && midiPlan.minimumMidiNote == 36
                         && midiPlan.maximumMidiNote == 84
                         && std::abs(midiPlan.velocityScale - 0.5f) < 0.001f,
                     "MIDI transpose, note filter and velocity nodes compile into the live poly synth")
                 && passed;
    }

    {
        cydaw::engine::PolySynthGraphPlan plan;
        plan.enabled = true;
        plan.voiceCount = 12;
        plan.waveform = cydaw::engine::PolyOscillatorWaveform::saw;
        plan.oscillatorLevel = 0.12f;
        plan.attackSeconds = 0.001f;
        plan.decaySeconds = 0.04f;
        plan.sustainLevel = 0.8f;
        plan.releaseSeconds = 0.08f;
        plan.filterCutoffHz = 8'000.0f;
        plan.filterResonance = 2.5f;

        const auto encoded = cydaw::app::serialisePolySynthGraphPlan(plan);
        juce::String restoreError;
        const auto restored = cydaw::app::restorePolySynthGraphPlan(
            juce::JSON::parse(juce::JSON::toString(encoded, false)),
            restoreError);

        cydaw::app::GraphPolySynthRenderer renderer;
        renderer.prepare(48'000.0);
        renderer.setPlan(restored.value_or(
            cydaw::engine::PolySynthGraphPlan {}));
        juce::AudioBuffer<float> audio(2, 2'048);
        audio.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.65f), 256);
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 1'536);
        midi.addEvent(juce::MidiMessage::noteOff(1, 67), 1'792);
        renderer.render(audio, midi, 0, audio.getNumSamples());
        const auto peak = audio.getMagnitude(0, audio.getNumSamples());
        renderer.release();
        audio.clear();
        juce::MidiBuffer automatedMidi;
        automatedMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        auto automatedPlan = plan;
        automatedPlan.oscillatorLevel = 0.48f;
        renderer.renderWithPlan(audio, automatedMidi, 0,
                                audio.getNumSamples(), automatedPlan);
        const auto automatedPeak = audio.getMagnitude(
            0, audio.getNumSamples());
        passed = expect(restored.has_value() && restoreError.isEmpty()
                            && *restored == plan && renderer.enabled()
                            && std::isfinite(peak) && peak > 0.001f
                            && automatedPeak > peak * 1.8f,
                        "polyphonic instrument Graph state round trips, renders MIDI and follows real-time parameters")
                 && passed;
        renderer.release();
    }

    {
        using namespace cydaw::engine;
        NativeDspNodeParameters gain;
        gain.gainDb = -6.0206f;
        gain.mapInputMinimum = -2.0f;
        gain.mapInputMaximum = 2.0f;
        gain.mapOutputMinimum = -24.0f;
        gain.mapOutputMaximum = 6.0f;
        gain.followerAttackSeconds = 0.025f;
        gain.followerReleaseSeconds = 0.42f;
        NativeDspGraph graph;
        graph.nodes = {
            { 1, NativeDspNodeKind::input, {} },
            { 2, NativeDspNodeKind::gain, gain },
            { 3, NativeDspNodeKind::output, {} }
        };
        graph.connections = { { 1, 2, 7, 11 }, { 2, 3, 13, 17 } };
        graph.outputNodeId = 3;
        const auto encoded = cydaw::app::serialiseNativeDspGraphState(graph);
        juce::String stateError;
        const auto decoded = cydaw::app::restoreNativeDspGraphState(
            juce::JSON::parse(juce::JSON::toString(encoded, false)),
            stateError);
        passed = expect(decoded.has_value() && *decoded == graph,
                        "native Graph VST state round trips without UI dependencies")
                 && passed;

        cydaw::app::NativeDspRuntime runtime;
        runtime.prepare(48'000.0, 64, 8);
        auto result = runtime.setGraph(graph);
        juce::AudioBuffer<float> multichannel(8, 64);
        const auto fillInput = [&multichannel] {
            for (auto channel = 0;
                 channel < multichannel.getNumChannels(); ++channel)
                for (auto sample = 0;
                     sample < multichannel.getNumSamples(); ++sample)
                    multichannel.setSample(channel, sample, 0.5f);
        };
        for (int block = 0; block < 8; ++block) {
            fillInput();
            (void) runtime.process(multichannel, 0,
                                   multichannel.getNumSamples());
        }
        gain.gainDb = -12.0412f;
        graph.nodes[1].parameters = gain;
        result = result.wasOk() ? runtime.updateParameters(graph) : result;
        for (int block = 0; block < 12; ++block) {
            fillInput();
            (void) runtime.process(multichannel, 0,
                                   multichannel.getNumSamples());
        }
        passed = expect(result.wasOk()
                            && std::abs(multichannel.getSample(7, 63) - 0.125f)
                                   < 0.01f,
                        "native Graph parameters update live on an eight-channel runtime")
                 && passed;

        gain.gainDb = -18.0618f;
        graph.nodes[1].parameters = gain;
        const auto realtimeUpdate = runtime.updateNodeParametersRealtime(
            graph.nodes[1]);
        for (int block = 0; block < 12; ++block) {
            fillInput();
            (void) runtime.process(multichannel, 0,
                                   multichannel.getNumSamples());
        }
        passed = expect(realtimeUpdate
                            && std::abs(multichannel.getSample(7, 63) - 0.0625f)
                                   < 0.01f,
                        "host-style parameter update reaches DSP without topology rebuild or configuration lock")
                 && passed;

        NativeDspNodeParameters constant;
        constant.valueOperand = -12.0412f;
        NativeDspGraph modulatedGraph;
        modulatedGraph.nodes = {
            { 11, NativeDspNodeKind::input, {} },
            { 12, NativeDspNodeKind::gain, {} },
            { 13, NativeDspNodeKind::output, {} },
            { 14, NativeDspNodeKind::controlConstant, constant }
        };
        modulatedGraph.connections = { { 11, 12, 1, 1 },
                                       { 12, 13, 3, 1 } };
        modulatedGraph.modulations = {
            { 14, 12, 1, 2, "gain-db" }
        };
        modulatedGraph.outputNodeId = 13;
        const auto modulationState = cydaw::app::serialiseNativeDspGraphState(
            modulatedGraph);
        const auto restoredModulation =
            cydaw::app::restoreNativeDspGraphState(
                juce::JSON::parse(juce::JSON::toString(modulationState, false)),
                stateError);
        result = runtime.setGraph(modulatedGraph);
        for (int block = 0; block < 12; ++block) {
            fillInput();
            (void) runtime.process(multichannel, 0,
                                   multichannel.getNumSamples());
        }
        passed = expect(result.wasOk() && restoredModulation.has_value()
                            && *restoredModulation == modulatedGraph
                            && std::abs(multichannel.getSample(7, 63) - 0.125f)
                                   < 0.01f,
                        "control-rate cables survive state restore and drive audio parameters")
                 && passed;

        constant.valueOperand = -6.0206f;
        modulatedGraph.nodes.back().parameters = constant;
        const auto controlRealtimeUpdate =
            runtime.updateNodeParametersRealtime(modulatedGraph.nodes.back());
        for (int block = 0; block < 12; ++block) {
            fillInput();
            (void) runtime.process(multichannel, 0,
                                   multichannel.getNumSamples());
        }
        const auto liveControlValues = runtime.controlValues();
        const auto constantTelemetry = std::ranges::find(
            liveControlValues, std::uint64_t { 14 },
            &std::pair<std::uint64_t, float>::first);
        passed = expect(controlRealtimeUpdate
                            && constantTelemetry != liveControlValues.end()
                            && std::abs(constantTelemetry->second + 6.0206f)
                                   < 0.001f
                            && std::abs(multichannel.getSample(7, 63) - 0.25f)
                                   < 0.01f,
                        "control-node edits update audio and publish live telemetry without rebuilding the runtime")
                 && passed;

        NativeDspNodeParameters addDefaults;
        addDefaults.valueInputA = 0.25f;
        addDefaults.valueInputB = 0.75f;
        NativeDspNodeParameters cableConstant;
        cableConstant.valueOperand = -0.25f;
        NativeDspNodeParameters moduloDefaults;
        moduloDefaults.valueInputA = 5.5f;
        moduloDefaults.valueInputB = 2.0f;
        NativeDspNodeParameters powerDefaults;
        powerDefaults.valueInputA = 3.0f;
        powerDefaults.valueInputB = 2.0f;
        NativeDspNodeParameters sineDefaults;
        sineDefaults.valueInputA = juce::MathConstants<float>::halfPi;
        NativeDspNodeParameters cosineDefaults;
        cosineDefaults.valueInputA = juce::MathConstants<float>::pi;
        NativeDspNodeParameters squareRootDefaults;
        squareRootDefaults.valueInputA = 9.0f;
        NativeDspNodeParameters floorDefaults;
        floorDefaults.valueInputA = 1.9f;
        NativeDspNodeParameters ceilDefaults;
        ceilDefaults.valueInputA = 1.1f;
        NativeDspNodeParameters roundDefaults;
        roundDefaults.valueInputA = 1.6f;
        NativeDspGraph valuePinGraph;
        valuePinGraph.nodes = {
            { 21, NativeDspNodeKind::input, {} },
            { 22, NativeDspNodeKind::output, {} },
            { 23, NativeDspNodeKind::controlAdd, addDefaults },
            { 24, NativeDspNodeKind::controlConstant, cableConstant },
            { 25, NativeDspNodeKind::controlModulo, moduloDefaults },
            { 26, NativeDspNodeKind::controlPower, powerDefaults },
            { 27, NativeDspNodeKind::controlSine, sineDefaults },
            { 28, NativeDspNodeKind::controlCosine, cosineDefaults },
            { 29, NativeDspNodeKind::controlSquareRoot, squareRootDefaults },
            { 30, NativeDspNodeKind::controlFloor, floorDefaults },
            { 31, NativeDspNodeKind::controlCeil, ceilDefaults },
            { 32, NativeDspNodeKind::controlRound, roundDefaults }
        };
        valuePinGraph.connections = { { 21, 22, 1, 1 } };
        valuePinGraph.outputNodeId = 22;
        result = runtime.setGraph(valuePinGraph);
        fillInput();
        const auto fallbackProcessed = runtime.process(
            multichannel, 0, multichannel.getNumSamples());
        const auto fallbackValues = runtime.controlValues();
        const auto fallbackAdd = std::ranges::find(
            fallbackValues, std::uint64_t { 23 },
            &std::pair<std::uint64_t, float>::first);
        const auto fallbackModulo = std::ranges::find(
            fallbackValues, std::uint64_t { 25 },
            &std::pair<std::uint64_t, float>::first);
        const auto fallbackPower = std::ranges::find(
            fallbackValues, std::uint64_t { 26 },
            &std::pair<std::uint64_t, float>::first);
        const auto fallbackSine = std::ranges::find(
            fallbackValues, std::uint64_t { 27 },
            &std::pair<std::uint64_t, float>::first);
        const auto fallbackCosine = std::ranges::find(
            fallbackValues, std::uint64_t { 28 },
            &std::pair<std::uint64_t, float>::first);
        const auto fallbackSquareRoot = std::ranges::find(
            fallbackValues, std::uint64_t { 29 },
            &std::pair<std::uint64_t, float>::first);
        const auto fallbackFloor = std::ranges::find(
            fallbackValues, std::uint64_t { 30 },
            &std::pair<std::uint64_t, float>::first);
        const auto fallbackCeil = std::ranges::find(
            fallbackValues, std::uint64_t { 31 },
            &std::pair<std::uint64_t, float>::first);
        const auto fallbackRound = std::ranges::find(
            fallbackValues, std::uint64_t { 32 },
            &std::pair<std::uint64_t, float>::first);

        valuePinGraph.modulations = {
            { 24, 23, 1, 1, {} }
        };
        result = result.wasOk() ? runtime.setGraph(valuePinGraph) : result;
        fillInput();
        const auto cableProcessed = runtime.process(
            multichannel, 0, multichannel.getNumSamples());
        const auto cableValues = runtime.controlValues();
        const auto cableAdd = std::ranges::find(
            cableValues, std::uint64_t { 23 },
            &std::pair<std::uint64_t, float>::first);
        const auto pinState = cydaw::app::serialiseNativeDspGraphState(
            valuePinGraph);
        const auto restoredPins = cydaw::app::restoreNativeDspGraphState(
            juce::JSON::parse(juce::JSON::toString(pinState, false)),
            stateError);
        passed = expect(
                     result.wasOk() && fallbackProcessed && cableProcessed
                         && fallbackAdd != fallbackValues.end()
                         && fallbackModulo != fallbackValues.end()
                         && fallbackPower != fallbackValues.end()
                         && fallbackSine != fallbackValues.end()
                         && fallbackCosine != fallbackValues.end()
                         && fallbackSquareRoot != fallbackValues.end()
                         && fallbackFloor != fallbackValues.end()
                         && fallbackCeil != fallbackValues.end()
                         && fallbackRound != fallbackValues.end()
                         && cableAdd != cableValues.end()
                         && std::abs(fallbackAdd->second - 1.0f) < 0.001f
                         && std::abs(fallbackModulo->second - 1.5f) < 0.001f
                         && std::abs(fallbackPower->second - 9.0f) < 0.001f
                         && std::abs(fallbackSine->second - 1.0f) < 0.001f
                         && std::abs(fallbackCosine->second + 1.0f) < 0.001f
                         && std::abs(fallbackSquareRoot->second - 3.0f) < 0.001f
                         && std::abs(fallbackFloor->second - 1.0f) < 0.001f
                         && std::abs(fallbackCeil->second - 2.0f) < 0.001f
                         && std::abs(fallbackRound->second - 2.0f) < 0.001f
                         && std::abs(cableAdd->second - 0.5f) < 0.001f
                         && restoredPins.has_value()
                         && *restoredPins == valuePinGraph,
                     "Value pins, math primitives, and cable overrides execute and survive state restore")
                 && passed;
    }

    {
        cydaw::app::ModuleInterfaceState interfaceState;
        interfaceState.nextControlId = 5;
        interfaceState.snapToGrid = false;
        interfaceState.gridColumns = 32;
        interfaceState.gridRows = 20;
        interfaceState.parameters = {
            { "drive.db", "Drive", "dB", 0.0, 36.0, 12.0, true },
            { "lowpass.cutoff", "Cutoff", "Hz", 20.0, 20'000.0, 1'200.0, false },
            { "lowpass.resonance", "Resonance", "Q", 0.1, 20.0, 0.707, false },
            { "envelope.attack", "Attack", "ms", 0.1, 5'000.0, 15.0, false },
            { "envelope.decay", "Decay", "ms", 0.1, 5'000.0, 180.0, false },
            { "envelope.sustain", "Sustain", "%", 0.0, 1.0, 0.72, false },
            { "envelope.release", "Release", "ms", 0.1, 10'000.0, 420.0, false },
            { "compressor.threshold", "Threshold", "dB", -60.0, 0.0, -18.0, false },
            { "compressor.ratio", "Ratio", ":1", 1.0, 20.0, 4.0, false }
        };
        interfaceState.parameters[0].hostParameterId = 0x1020304u;
        interfaceState.parameters[0].description = "Drive amount exposed to the host";
        interfaceState.parameters[1].publishToHost = false;
        interfaceState.parameters[1].automatable = false;
        interfaceState.parameters[1].modulatable = false;
        interfaceState.controls = {
            { 1, cydaw::app::ModuleControlKind::knob, "Drive", "drive.db",
              0.04f, 0.06f, 0.20f, 0.32f },
            { 2, cydaw::app::ModuleControlKind::fader, "Cutoff", "lowpass.cutoff",
              0.28f, 0.06f, 0.20f, 0.32f },
            { 3, cydaw::app::ModuleControlKind::spectrum, "Spectrum", {},
              0.52f, 0.06f, 0.44f, 0.34f },
            { 4, cydaw::app::ModuleControlKind::levelMeter, "Meter", {},
              0.74f, 0.50f, 0.22f, 0.34f, 0, 6 },
            { 8, cydaw::app::ModuleControlKind::inputPort, "5.1 Input",
              "port.audio.5_1", 0.0f, 0.72f, 0.13f, 0.17f, 0, 6 },
            { 9, cydaw::app::ModuleControlKind::toggle, "Drive enabled",
              "drive.db", 0.18f, 0.76f, 0.18f, 0.14f },
            { 10, cydaw::app::ModuleControlKind::envelope, "Envelope",
              "envelope.attack", 0.38f, 0.68f, 0.28f, 0.22f, 0, 1,
              "envelope.decay", "envelope.sustain", "envelope.release" },
            { 11, cydaw::app::ModuleControlKind::group, "Output section", {},
              0.70f, 0.66f, 0.27f, 0.26f },
            { 12, cydaw::app::ModuleControlKind::selector, "Cutoff steps",
              "lowpass.cutoff", 0.30f, 0.46f, 0.24f, 0.14f },
            { 13, cydaw::app::ModuleControlKind::xyPad, "Drive / Cutoff",
              "drive.db", 0.04f, 0.42f, 0.34f, 0.26f, 0, 1,
              "lowpass.cutoff" },
            { 14, cydaw::app::ModuleControlKind::valueDisplay, "Drive value",
              "drive.db", 0.40f, 0.42f, 0.24f, 0.14f },
            { 15, cydaw::app::ModuleControlKind::filterResponse, "Filter",
              "lowpass.cutoff", 0.66f, 0.42f, 0.30f, 0.22f, 0, 1,
              "lowpass.resonance" },
            { 16, cydaw::app::ModuleControlKind::dynamicsResponse, "Dynamics",
              "compressor.threshold", 0.04f, 0.04f, 0.40f, 0.24f, 0, 1,
              "compressor.ratio" }
        };
        interfaceState.deviceControls = {
            { 5, cydaw::app::ModuleControlKind::knob, "Drive", "drive.db",
              0.04f, 0.08f, 0.20f, 0.70f },
            { 6, cydaw::app::ModuleControlKind::inputPort, "Transport In", "port.transport.in",
              0.0f, 0.40f, 0.13f, 0.17f, 3 },
            { 7, cydaw::app::ModuleControlKind::outputPort, "Transport Out", "port.transport.out",
              0.87f, 0.40f, 0.13f, 0.17f, 3 }
        };
        interfaceState.nextControlId = 17;
        passed = expect(cydaw::app::validateModuleInterface(interfaceState).wasOk(),
                        "custom module interface validates") && passed;
        const auto encoded = cydaw::app::serialiseModuleInterface(interfaceState);
        const auto decoded = cydaw::app::restoreModuleInterface(
            juce::JSON::parse(juce::JSON::toString(encoded, false)));
        // Schema 16 added the extra parameter bindings a multi-parameter
        // control needs; an EQ curve binds three per band.
        passed = expect(decoded.schema == 16
                            && decoded.parameters.size() == 9
                            && decoded.controls.size() == 13
                            && decoded.deviceControls.size() == 1
                            && decoded.nextControlId == 17
                            && !decoded.snapToGrid
                            && decoded.gridColumns == 32
                            && decoded.gridRows == 20
                            && decoded.controls[1].parameterBinding == "lowpass.cutoff"
                            && decoded.controls[4].portKind == 0
                            && decoded.controls[4].portChannels == 6
                            && decoded.controls[3].portChannels == 6
                            && decoded.controls[5].kind
                                   == cydaw::app::ModuleControlKind::toggle
                            && decoded.controls[6].kind
                                   == cydaw::app::ModuleControlKind::envelope
                            && decoded.controls[6].parameterBinding
                                   == "envelope.attack"
                            && decoded.controls[6].secondaryParameterBinding
                                   == "envelope.decay"
                            && decoded.controls[6].tertiaryParameterBinding
                                   == "envelope.sustain"
                            && decoded.controls[6].quaternaryParameterBinding
                                   == "envelope.release"
                            && decoded.controls[7].kind
                                   == cydaw::app::ModuleControlKind::group
                            && decoded.controls[8].kind
                                   == cydaw::app::ModuleControlKind::selector
                            && decoded.controls[9].kind
                                   == cydaw::app::ModuleControlKind::xyPad
                            && decoded.controls[9].parameterBinding == "drive.db"
                            && decoded.controls[9].secondaryParameterBinding
                                   == "lowpass.cutoff"
                            && decoded.controls[10].kind
                                   == cydaw::app::ModuleControlKind::valueDisplay
                            && decoded.controls[10].parameterBinding == "drive.db"
                            && decoded.controls[11].kind
                                   == cydaw::app::ModuleControlKind::filterResponse
                            && decoded.controls[11].parameterBinding
                                   == "lowpass.cutoff"
                            && decoded.controls[11].secondaryParameterBinding
                                   == "lowpass.resonance"
                            && decoded.controls[12].kind
                                   == cydaw::app::ModuleControlKind::dynamicsResponse
                            && decoded.controls[12].parameterBinding
                                   == "compressor.threshold"
                            && decoded.controls[12].secondaryParameterBinding
                                   == "compressor.ratio"
                            && decoded.deviceControls[0].parameterBinding == "drive.db"
                            && decoded.parameters[0].showInDevice
                            && !decoded.parameters[1].showInDevice
                            && decoded.parameters[0].hostParameterId
                                   == 0x1020304u
                            && decoded.parameters[0].publishToHost
                            && decoded.parameters[0].automatable
                            && decoded.parameters[0].modulatable
                            && decoded.parameters[0].description
                                   == "Drive amount exposed to the host"
                            && !decoded.parameters[1].publishToHost
                            && !decoded.parameters[1].automatable
                            && !decoded.parameters[1].modulatable
                            && cydaw::app::validateModuleInterface(decoded).wasOk(),
                        "free Graph UI and stable host publication metadata round trip") && passed;

        const auto selectorValues = cydaw::app::moduleSelectorValues(
            decoded.parameters[1]);
        passed = expect(selectorValues.size() == 5
                            && cydaw::app::closestModuleSelectorValue(
                                   selectorValues, 1'200.0) < selectorValues.size()
                            && cydaw::app::moduleSelectorValueLabel(
                                   decoded.parameters[1], selectorValues.front())
                                   .contains("Hz"),
                        "Graph selector derives stable discrete values and labels")
                 && passed;

        cydaw::app::ModuleXyPadComponent xyPad;
        auto xyNotifications = 0;
        xyPad.onValueChange = [&xyNotifications](double, double) {
            ++xyNotifications;
        };
        xyPad.setNormalisedValues(0.25, 0.75,
                                  juce::sendNotification);
        const auto keyboardMoved = xyPad.keyPressed(juce::KeyPress(
            juce::KeyPress::rightKey, juce::ModifierKeys {}, 0));
        passed = expect(keyboardMoved && xyNotifications == 2
                            && std::abs(xyPad.normalisedX() - 0.26) < 1.0e-9
                            && std::abs(xyPad.normalisedY() - 0.75) < 1.0e-9,
                        "XY control supports deterministic live and keyboard gestures")
                 && passed;

        cydaw::app::ModuleFilterResponseComponent filterResponse;
        auto filterNotifications = 0;
        filterResponse.setParameterRanges(20.0, 20'000.0, 0.1, 20.0);
        filterResponse.onValueChange = [&filterNotifications](double, double) {
            ++filterNotifications;
        };
        filterResponse.setValues(2'000.0, 2.5, juce::sendNotification);
        const auto filterKeyboardMoved = filterResponse.keyPressed(
            juce::KeyPress(juce::KeyPress::rightKey,
                           juce::ModifierKeys {}, 0));
        passed = expect(filterKeyboardMoved && filterNotifications == 2
                            && filterResponse.cutoff() > 2'000.0
                            && std::abs(filterResponse.resonance() - 2.5)
                                   < 1.0e-9,
                        "filter response control edits cutoff and resonance together")
                 && passed;

        cydaw::app::ModuleDynamicsResponseComponent dynamicsResponse;
        auto dynamicsNotifications = 0;
        dynamicsResponse.setParameterRanges(-60.0, 0.0, 1.0, 20.0);
        dynamicsResponse.onValueChange = [&dynamicsNotifications](double,
                                                                   double) {
            ++dynamicsNotifications;
        };
        dynamicsResponse.setValues(-24.0, 3.0, juce::sendNotification);
        const auto dynamicsKeyboardMoved = dynamicsResponse.keyPressed(
            juce::KeyPress(juce::KeyPress::rightKey,
                           juce::ModifierKeys {}, 0));
        const auto dynamicsSelectedRatio = dynamicsResponse.keyPressed(
            juce::KeyPress(juce::KeyPress::tabKey,
                           juce::ModifierKeys {}, 0));
        const auto ratioKeyboardMoved = dynamicsResponse.keyPressed(
            juce::KeyPress(juce::KeyPress::upKey,
                           juce::ModifierKeys {}, 0));
        passed = expect(dynamicsKeyboardMoved && dynamicsSelectedRatio
                            && ratioKeyboardMoved
                            && dynamicsNotifications == 3
                            && dynamicsResponse.threshold() > -24.0
                            && dynamicsResponse.ratio() > 3.0,
                        "dynamics response control edits threshold and ratio together")
                 && passed;

        cydaw::app::ModuleEnvelopeComponent envelope;
        auto envelopeNotifications = 0;
        envelope.onValueChange = [&envelopeNotifications](
            double, double, double, double) { ++envelopeNotifications; };
        envelope.setNormalisedValues(0.2, 0.3, 0.7, 0.4,
                                     juce::sendNotification);
        const auto envelopeMoved = envelope.keyPressed(juce::KeyPress(
            juce::KeyPress::rightKey, juce::ModifierKeys {}, 0));
        passed = expect(envelopeMoved && envelopeNotifications == 2,
                        "ADSR control supports deterministic live and keyboard gestures")
                 && passed;

        auto legacyEncoded = encoded;
        if (auto* legacyObject = legacyEncoded.getDynamicObject()) {
            legacyObject->setProperty("schema", 1);
            legacyObject->removeProperty("deviceControls");
        }
        const auto migratedInterface = cydaw::app::restoreModuleInterface(
            juce::JSON::parse(juce::JSON::toString(legacyEncoded, false)));
        passed = expect(migratedInterface.deviceControls.size() == 3
                            && cydaw::app::validateModuleInterface(
                                   migratedInterface).wasOk(),
                        "schema-1 interface migrates to automatic parameter facade")
                 && passed;

        auto invalidInterface = decoded;
        invalidInterface.controls[0].parameterBinding = "missing.parameter";
        passed = expect(cydaw::app::validateModuleInterface(invalidInterface).failed(),
                        "unknown custom UI parameter binding is rejected") && passed;

        auto invalidXyInterface = decoded;
        invalidXyInterface.controls[9].secondaryParameterBinding = "drive.db";
        passed = expect(
                     cydaw::app::validateModuleInterface(invalidXyInterface).failed(),
                     "XY controls reject duplicate parameter bindings")
                 && passed;

        auto invalidEnvelopeInterface = decoded;
        invalidEnvelopeInterface.controls[6].quaternaryParameterBinding =
            "envelope.sustain";
        passed = expect(
                     cydaw::app::validateModuleInterface(
                         invalidEnvelopeInterface).failed(),
                     "ADSR controls reject duplicate parameter bindings")
                 && passed;

        cydaw::app::ModuleInterfaceEditorComponent editor(decoded);
        editor.setSize(720, 440);
        editor.setLayoutMode(cydaw::app::ModuleInterfaceLayoutMode::device);
        const auto liveParameterUpdated = editor.setParameterValue(
            "compressor.threshold", -30.0);
        const auto unchangedLiveParameter = editor.setParameterValue(
            "compressor.threshold", -30.0);
        const auto updatedParameter = std::find_if(
            editor.state().parameters.begin(),
            editor.state().parameters.end(),
            [](const auto& parameter) {
                return parameter.key == "compressor.threshold";
            });
        passed = expect(liveParameterUpdated && !unchangedLiveParameter
                            && updatedParameter
                                   != editor.state().parameters.end()
                            && std::abs(updatedParameter->value + 30.0)
                                   < 1.0e-9,
                        "published Graph controls follow live host automation")
                 && passed;
        cydaw::engine::AudioAnalysisFrame analysis;
        analysis.waveformCount = 64;
        analysis.channelCount = 6;
        analysis.peak = { 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f };
        analysis.rms = { 0.4f, 0.35f, 0.3f, 0.25f, 0.2f, 0.15f };
        for (std::size_t sample = 0; sample < analysis.waveformCount; ++sample)
            analysis.waveform[sample] = static_cast<float>(
                0.5 * std::sin(juce::MathConstants<double>::twoPi
                               * static_cast<double>(sample) / 16.0));
        editor.setAnalysisFrame(analysis);
        const auto preview = editor.createComponentSnapshot(editor.getLocalBounds());
        passed = expect(preview.isValid()
                            && preview.getWidth() == 720
                            && preview.getHeight() == 440,
                        "custom module editor renders controls and live visuals") && passed;
    }
    passed = runProjectImportTests(tempRoot, firstFile) && passed;
    tempRoot.deleteRecursively();
    return passed ? 0 : 1;
}
