#include <cydaw/integration/CyAudioCoreAudioFormats.h>
#include <cydaw/engine/AudioBuffer.h>
#include <cyaudio/io/AudioFile.h>
#include <cyaudio/io/AudioMetadata.h>
#include <cyaudio/io/AudioDeviceManager.h>
#include <cyaudio/midi/MidiEventCollector.h>
#include <array>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void storage() {
    cydaw::engine::AudioBuffer input(3, 17);
    auto view = input.view(17);
    require(view.channelStride >= 17, "padded channel stride");
    for (unsigned ch = 0; ch < 3; ++ch) {
        require(reinterpret_cast<std::uintptr_t>(view.channel(ch)) % 64 == 0, "channel alignment");
        for (unsigned n = 0; n < 17; ++n) view.channel(ch)[n] = float(ch * 100 + n);
    }
    auto copy = input;
    copy.view(17).channel(2)[16] = -1;
    require(input.view(17).channel(2)[16] == 216, "deep copy ownership");
    auto moved = std::move(copy);
    require(moved.view(17).channel(2)[16] == -1 && copy.channelCount() == 0, "move ownership");
    try { input.prepare(3, 0xffffffffu); require(false, "stride overflow accepted"); }
    catch (const std::length_error&) {}
    require(input.view(17).channel(2)[16] == 216, "failed prepare must preserve buffer");
}

juce::MemoryBlock fixture() {
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>();
    auto* memory = static_cast<juce::MemoryOutputStream*>(stream.get());
    const auto options = juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2)
        .withBitsPerSample(24).withMetadata("NumCuePoints", "1")
        .withMetadata("Cue0Identifier", "7").withMetadata("Cue0Offset", "93")
        .withMetadata("NumCueLabels", "1").withMetadata("CueLabel0Identifier", "7")
        .withMetadata("CueLabel0Text", "Door impact");
    auto writer = wav.createWriterFor(stream, options);
    require(writer != nullptr, "independent WAV fixture writer");
    juce::AudioBuffer<float> audio(2, 10037);
    for (int n = 0; n < audio.getNumSamples(); ++n) {
        audio.setSample(0, n, float(std::sin(n * .031) * .7));
        audio.setSample(1, n, float(std::cos(n * .019) * .4));
    }
    require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "fixture samples");
    require(writer->flush(), "fixture flush");
    // Finalize before capturing the bytes; the writer owns its stream.
    const auto bytes = memory->getMemoryBlock();
    return bytes;
}

void readers() {
    const auto bytes = fixture();
    juce::AudioFormatManager manager;
    cydaw::integration::registerCyAudioCoreReaders(manager);
    require(manager.getNumKnownFormats() == 1, "CyAudioCore must be sole sample decoder");
    std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(
        std::make_unique<juce::MemoryInputStream>(bytes, true)));
    require(reader && reader->getFormatName() == "CyAudioCore", "CyAudioCore reader selected");
    require(reader->numChannels == 2 && reader->lengthInSamples == 10037
            && reader->bitsPerSample == 24, "source shape and encoding preserved");
    require(reader->metadataValues.getValue("CueLabel0Text", {}) == "Door impact", "legacy cue labels preserved");
    juce::AudioBuffer<float> output(4, 10045);
    output.clear();
    require(reader->read(&output, 0, 10045, 0, true, true), "multi-chunk read and EOS");
    for (int n = 0; n < 10037; ++n) {
        require(std::abs(output.getSample(0, n) - float(std::sin(n * .031) * .7)) < 2e-6, "left samples");
        require(std::abs(output.getSample(1, n) - float(std::cos(n * .019) * .4)) < 2e-6, "right samples");
    }
    for (int n = 10037; n < 10045; ++n)
        require(output.getSample(0, n) == 0 && output.getSample(1, n) == 0, "EOS zero padding");
    require(reader->read(&output, 9, 34, 137, true, true), "random seek with destination offset");
    require(std::abs(output.getSample(0, 9) - float(std::sin(137 * .031) * .7)) < 2e-6, "seek alignment");
    require(reader->read(&output, 0, 20, -5, true, true), "negative source position");
    require(output.getSample(0, 0) == 0 && std::abs(output.getSample(1, 5) - .4f) < 2e-6, "leading silence position");
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("cydsp-integration", ".wav");
    struct Cleanup { juce::File file; ~Cleanup() { file.deleteFile(); } } cleanup {file};
    require(file.replaceWithData(bytes.getData(), bytes.getSize()), "path fixture");
    std::unique_ptr<juce::AudioFormatReader> pathReader(manager.createReaderFor(file));
    require(pathReader && pathReader->read(&output, 0, 17, 100, true, true), "native path read");
    require(std::abs(output.getSample(0, 0) - float(std::sin(100 * .031) * .7)) < 2e-6, "path seek");
}

void writers() {
    for (const int bits : {8, 16, 24, 32}) {
        juce::MemoryBlock bytes;
        std::unique_ptr<juce::OutputStream> output = std::make_unique<juce::MemoryOutputStream>(bytes, false);
        cydaw::integration::WaveFormat format;
        auto writer = format.createWriterFor(output, juce::AudioFormatWriterOptions()
            .withNumChannels(2).withSampleRate(44100).withBitsPerSample(bits));
        require(writer && !output && writer->getFormatName() == "CyAudioCore WAV", "CyAudioCore WAV encoder selected");
        juce::AudioBuffer<float> audio(2, 9017);
        for (int n = 0; n < audio.getNumSamples(); ++n) {
            audio.setSample(0, n, .6f * float(std::sin(n * .02)));
            audio.setSample(1, n, -.2f);
        }
        require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "multi-chunk WAV export");
        require(writer->flush(), "CyAudioCore writer flush");
        writer.reset();
        juce::AudioFormatManager independent;
        independent.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(independent.createReaderFor(
            std::make_unique<juce::MemoryInputStream>(bytes, true)));
        require(reader && reader->lengthInSamples == 9017 && reader->numChannels == 2
                && reader->bitsPerSample == unsigned(bits), "export independently decodable and finalized");
        juce::AudioBuffer<float> restored(2, 9017);
        require(reader->read(&restored, 0, 9017, 0, true, true), "independent exported samples");
        for (int n = 0; n < 9017; ++n) {
            const auto tolerance = bits == 8 ? .016f : bits == 16 ? .00007f : .000002f;
            require(std::abs(restored.getSample(0, n) - audio.getSample(0, n)) < tolerance, "export amplitude and sample count");
            require(std::abs(restored.getSample(1, n) + .2f) < tolerance, "export channel routing");
        }
    }
    std::unique_ptr<juce::OutputStream> unchanged = std::make_unique<juce::MemoryOutputStream>();
    const auto* original = unchanged.get();
    cydaw::integration::WaveFormat format;
    require(!format.createWriterFor(unchanged, juce::AudioFormatWriterOptions().withBitsPerSample(20))
            && unchanged.get() == original, "unsupported output preserves stream ownership");
}

void fileWriters() {
    for (const auto kind : {cyaudio::AudioFileFormat::aiff, cyaudio::AudioFileFormat::flac, cyaudio::AudioFileFormat::oggVorbis}) {
        cydaw::integration::FileFormat format(kind);
        juce::MemoryBlock bytes;
        std::unique_ptr<juce::OutputStream> output = std::make_unique<juce::MemoryOutputStream>(bytes, false);
        auto writer = format.createWriterFor(output, juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
        require(writer && writer->getFormatName().startsWith("CyAudioCore") && !output, "CyAudioCore AIFF/FLAC/Vorbis encoder selected");
        juce::AudioBuffer<float> audio(2, 10037);
        for (int n = 0; n < audio.getNumSamples(); ++n) {
            audio.setSample(0, n, float(std::sin(n * .031) * .7));
            audio.setSample(1, n, float(std::cos(n * .019) * .4));
        }
        require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "file export samples");
        require(writer->flush(), "file export flush"); writer.reset();
        juce::AudioFormatManager independent; independent.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(independent.createReaderFor(std::make_unique<juce::MemoryInputStream>(bytes, true)));
        require(reader && reader->numChannels == 2 && reader->lengthInSamples == 10037 && reader->sampleRate == 48000, "independent file shape");
        juce::AudioBuffer<float> restored(2, 10037); require(reader->read(&restored, 0, 10037, 0, true, true), "independent file decode");
        double error = 0;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < 10037; ++n) error += std::pow(restored.getSample(c, n) - audio.getSample(c, n), 2);
        require(std::sqrt(error / 20074) < (kind == cyaudio::AudioFileFormat::oggVorbis ? .035 : .000002), "export amplitude/channel alignment");
    }
}

void annotatedWriters() {
    // A CyAudioCore writer must produce externally readable labels, notes, regions and
    // inclusive WAV loop endpoints. The independent reader is intentionally JUCE.
    auto options = juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24)
        .withMetadata("NumCuePoints", "2").withMetadata("Cue0Identifier", "7").withMetadata("Cue0Offset", "93")
        .withMetadata("Cue1Identifier", "8").withMetadata("Cue1Offset", "200")
        .withMetadata("NumCueLabels", "1").withMetadata("CueLabel0Identifier", "7").withMetadata("CueLabel0Text", juce::String::fromUTF8("Porte fermée"))
        .withMetadata("NumCueNotes", "1").withMetadata("CueNote0Identifier", "7").withMetadata("CueNote0Text", "Sync 12")
        .withMetadata("NumCueRegions", "1").withMetadata("CueRegion0Identifier", "8").withMetadata("CueRegion0SampleLength", "101")
        .withMetadata("CueRegion0Purpose", "544106354").withMetadata("CueRegion0Text", "Ambience")
        .withMetadata("NumSampleLoops", "1").withMetadata("Loop0Identifier", "4").withMetadata("Loop0Start", "32")
        .withMetadata("Loop0End", "127").withMetadata("Loop0Type", "1").withMetadata("Loop0PlayCount", "3")
        .withMetadata("MidiUnityNote", "57").withMetadata("IART", "CyDaw test");
    juce::MemoryBlock bytes;
    std::unique_ptr<juce::OutputStream> output = std::make_unique<juce::MemoryOutputStream>(bytes, false);
    cydaw::integration::WaveFormat format;
    auto writer = format.createWriterFor(output, options);
    require(writer && writer->getFormatName() == "CyAudioCore WAV", "annotated WAV must use CyAudioCore, without writer fallback");
    juce::AudioBuffer<float> audio(2, 512); audio.clear(); audio.setSample(1, 201, .5f);
    require(writer->writeFromAudioSampleBuffer(audio, 0, 512), "annotated samples"); writer.reset();
    juce::AudioFormatManager independent; independent.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(independent.createReaderFor(std::make_unique<juce::MemoryInputStream>(bytes, true)));
    require(reader && reader->lengthInSamples == 512, "annotated independent reader");
    const auto& v = reader->metadataValues;
    require(v["CueLabel0Text"] == juce::String::fromUTF8("Porte fermée") && v["Cue0Offset"] == "93", "UTF8 label and marker offset");
    require(v["CueNote0Text"] == "Sync 12" && v["CueRegion0SampleLength"] == "101" && v["CueRegion0Text"] == "Ambience", "notes and regions");
    require(v["Loop0Start"] == "32" && v["Loop0End"] == "127" && v["Loop0Type"] == "1" && v["MidiUnityNote"] == "57", "inclusive sampler loop endpoints");
    require(v["IART"] == "CyDaw test", "INFO tag");
    juce::AudioFormatManager migrated; cydaw::integration::registerCyAudioCoreReaders(migrated);
    std::unique_ptr<juce::AudioFormatReader> typed(migrated.createReaderFor(std::make_unique<juce::MemoryInputStream>(bytes, true)));
    require(typed && typed->metadataValues["Loop0End"] == "127", "public metadata to legacy sampler vocabulary");
    cyaudio::AudioMetadata metadata;
    require(cyaudio::parseAudioMetadata(typed->metadataValues["cydsp.metadata"].toStdString(), metadata) == cyaudio::Status::ok, "opaque versioned preservation payload");
    require(metadata.markers.size() == 2 && metadata.loops.size() == 1 && metadata.loops[0].endFrame == 128, "exclusive public loop endpoint");
    juce::MemoryBlock preservedBytes;
    output = std::make_unique<juce::MemoryOutputStream>(preservedBytes, false);
    auto preserved = format.createWriterFor(output, juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2)
        .withBitsPerSample(24).withMetadata("cydsp.metadata", typed->metadataValues["cydsp.metadata"]));
    require(preserved && preserved->getFormatName() == "CyAudioCore WAV", "versioned metadata payload uses CyAudioCore");
    require(preserved->writeFromAudioSampleBuffer(audio, 0, 512), "preserved metadata samples"); preserved.reset();
    std::unique_ptr<juce::AudioFormatReader> preservedReader(independent.createReaderFor(std::make_unique<juce::MemoryInputStream>(preservedBytes, true)));
    require(preservedReader && preservedReader->metadataValues["CueNote0Text"] == "Sync 12"
            && preservedReader->metadataValues["Loop0End"] == "127", "opaque metadata survives export independently");
    output = std::make_unique<juce::MemoryOutputStream>();
    auto retained = format.createWriterFor(output, juce::AudioFormatWriterOptions().withMetadata("bwav description", "Keep broadcast metadata"));
    require(retained && retained->getFormatName() != "CyAudioCore WAV", "unsupported legacy fields retain explicit compatibility writer");
    output = std::make_unique<juce::MemoryOutputStream>();
    auto* original = output.get();
    require(!format.createWriterFor(output, juce::AudioFormatWriterOptions().withMetadata("cydsp.metadata", "invalid")) && output.get() == original,
            "invalid opaque metadata rejected without losing ownership");
}

void midiClock() {
    cyaudio::MidiEventCollector collector;
    cyaudio::MidiBuffer midi;
    cyaudio::UmpBuffer ump;
    require(collector.prepare({8, 64}) == cyaudio::Status::ok, "collector prepare");
    require(midi.prepare(8, 64) == cyaudio::Status::ok && ump.prepare(8) == cyaudio::Status::ok, "event storage");
    const auto note = cyaudio::midiNoteOn(1, 60, 100);
    require(collector.pushMidi(1000000000, note.bytes()) == cyaudio::Status::ok, "queue first event");
    require(collector.pushMidi(1002000000, note.bytes()) == cyaudio::Status::ok, "queue future event");
    require(collector.collect({1000000000, 100, 48000}, 64, midi, ump) == cyaudio::Status::ok, "first block");
    require(midi.size() == 1 && midi.event(0).frame == 0, "future event must remain queued");
    midi.reset(); ump.reset();
    require(collector.collect({1001333333, 164, 48000}, 64, midi, ump) == cyaudio::Status::ok, "second block");
    require(midi.size() == 1 && midi.event(0).frame >= 31 && midi.event(0).frame <= 32, "MIDI block placement");
}

// Diagnostics are deliberately separate from the passing adapter regressions.
// A failure here blocks removal of the legacy metadata parser, not audio decoding.
int probes(const juce::File& artifactDirectory) {
    const auto data = fixture();
    auto input = std::make_shared<std::istringstream>(std::string(static_cast<const char*>(data.getData()), data.getSize()), std::ios::binary);
    cyaudio::AudioFileReader reader;
    require(reader.open(input) == cyaudio::Status::ok, "probe open");
    const auto raw = juce::JSON::parse(reader.info()->metadataJson);
    bool typedLabels = false;
    if (const auto* chunks = raw.getArray())
        for (const auto& chunk : *chunks)
            if (chunk.getProperty("type", {}).toString() == "LIST"
                && chunk.hasProperty("listType") && chunk.getProperty("listType", {}).toString() == "adtl")
                typedLabels = true;
    auto encoded = std::make_shared<std::stringstream>(std::ios::in | std::ios::out | std::ios::binary);
    cyaudio::AudioFileWriter writer;
    auto info = *reader.info();
    bool roundtrip = writer.open(encoded, cyaudio::AudioFileFormat::wav, info) == cyaudio::Status::ok;
    cyaudio::AudioBuffer<float> block;
    require(block.resize(2, 4096) == cyaudio::Status::ok, "probe samples");
    for (std::uint64_t at = 0; roundtrip && at < info.frames;) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(4096, info.frames - at));
        std::size_t got = 0;
        const auto status = reader.read(at, block.block(count), got);
        roundtrip = (status == cyaudio::Status::ok || status == cyaudio::Status::end_of_stream)
                    && got == count && writer.write(block.block(count)) == cyaudio::Status::ok;
        at += count;
    }
    if (roundtrip) roundtrip = writer.close() == cyaudio::Status::ok;
    juce::AudioFormatManager legacy;
    legacy.registerBasicFormats();
    auto payload = encoded->str();
    std::unique_ptr<juce::AudioFormatReader> restored(legacy.createReaderFor(
        std::make_unique<juce::MemoryInputStream>(payload.data(), payload.size(), true)));
    roundtrip = roundtrip && restored && restored->metadataValues.getValue("CueLabel0Text", {}) == "Door impact";
    std::cout << "{\"wav_cue_label_roundtrip\":" << (roundtrip ? "true" : "false")
              << ",\"wav_typed_adtl_metadata\":" << (typedLabels ? "true" : "false")
              << ",\"metadata\":" << juce::JSON::toString(raw, true) << "}\n";
    if (artifactDirectory != juce::File()) {
        require(artifactDirectory.createDirectory().wasOk(), "probe artifact directory");
        require(artifactDirectory.getChildFile("original.wav").replaceWithData(data.getData(), data.getSize()), "original probe fixture");
        require(artifactDirectory.getChildFile("roundtrip.wav").replaceWithData(payload.data(), payload.size()), "roundtrip probe fixture");
    }
    return roundtrip ? 0 : 2;
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--upstream-probes") return probes(argc > 2 ? juce::File(juce::String::fromUTF8(argv[2])) : juce::File());
        storage(); readers(); writers(); fileWriters(); annotatedWriters(); midiClock();
        std::cout << "CyAudioCore application integration checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
