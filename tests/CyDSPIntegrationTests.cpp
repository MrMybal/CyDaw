#include <cydaw/integration/CyDSPAudioFormats.h>
#include <cydaw/engine/AudioBuffer.h>
#include <cydsp/io/AudioFile.h>
#include <cydsp/io/AudioDeviceManager.h>
#include <cydsp/midi/MidiEventCollector.h>
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
    cydaw::integration::registerCyDSPReaders(manager);
    require(manager.getNumKnownFormats() == 1, "CyDSP must be sole sample decoder");
    std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(
        std::make_unique<juce::MemoryInputStream>(bytes, true)));
    require(reader && reader->getFormatName() == "CyDSP", "CyDSP reader selected");
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
        require(writer && !output && writer->getFormatName() == "CyDSP WAV", "CyDSP WAV encoder selected");
        juce::AudioBuffer<float> audio(2, 9017);
        for (int n = 0; n < audio.getNumSamples(); ++n) {
            audio.setSample(0, n, .6f * float(std::sin(n * .02)));
            audio.setSample(1, n, -.2f);
        }
        require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "multi-chunk WAV export");
        require(writer->flush(), "CyDSP writer flush");
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

void midiClock() {
    cydsp::MidiEventCollector collector;
    cydsp::MidiBuffer midi;
    cydsp::UmpBuffer ump;
    require(collector.prepare({8, 64}) == cydsp::Status::ok, "collector prepare");
    require(midi.prepare(8, 64) == cydsp::Status::ok && ump.prepare(8) == cydsp::Status::ok, "event storage");
    const auto note = cydsp::midiNoteOn(1, 60, 100);
    require(collector.pushMidi(1000000000, note.bytes()) == cydsp::Status::ok, "queue first event");
    require(collector.pushMidi(1002000000, note.bytes()) == cydsp::Status::ok, "queue future event");
    require(collector.collect({1000000000, 100, 48000}, 64, midi, ump) == cydsp::Status::ok, "first block");
    require(midi.size() == 1 && midi.event(0).frame == 0, "future event must remain queued");
    midi.reset(); ump.reset();
    require(collector.collect({1001333333, 164, 48000}, 64, midi, ump) == cydsp::Status::ok, "second block");
    require(midi.size() == 1 && midi.event(0).frame >= 31 && midi.event(0).frame <= 32, "MIDI block placement");
}

// Diagnostics are deliberately separate from the passing adapter regressions.
// A failure here blocks removal of the legacy metadata parser, not audio decoding.
int probes(const juce::File& artifactDirectory) {
    const auto data = fixture();
    auto input = std::make_shared<std::istringstream>(std::string(static_cast<const char*>(data.getData()), data.getSize()), std::ios::binary);
    cydsp::AudioFileReader reader;
    require(reader.open(input) == cydsp::Status::ok, "probe open");
    const auto raw = juce::JSON::parse(reader.info()->metadataJson);
    bool typedLabels = false;
    if (const auto* chunks = raw.getArray())
        for (const auto& chunk : *chunks)
            if (chunk.getProperty("type", {}).toString() == "LIST"
                && chunk.hasProperty("listType") && chunk.getProperty("listType", {}).toString() == "adtl")
                typedLabels = true;
    auto encoded = std::make_shared<std::stringstream>(std::ios::in | std::ios::out | std::ios::binary);
    cydsp::AudioFileWriter writer;
    auto info = *reader.info();
    bool roundtrip = writer.open(encoded, cydsp::AudioFileFormat::wav, info) == cydsp::Status::ok;
    cydsp::AudioBuffer<float> block;
    require(block.resize(2, 4096) == cydsp::Status::ok, "probe samples");
    for (std::uint64_t at = 0; roundtrip && at < info.frames;) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(4096, info.frames - at));
        std::size_t got = 0;
        const auto status = reader.read(at, block.block(count), got);
        roundtrip = (status == cydsp::Status::ok || status == cydsp::Status::end_of_stream)
                    && got == count && writer.write(block.block(count)) == cydsp::Status::ok;
        at += count;
    }
    if (roundtrip) roundtrip = writer.close() == cydsp::Status::ok;
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
        storage(); readers(); writers(); midiClock();
        std::cout << "CyDSP application integration checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
