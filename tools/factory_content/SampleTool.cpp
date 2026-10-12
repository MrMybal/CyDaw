// The factory content's sample tool.
//
//     cydaw_sample_tool convert <in> <out.flac> [--bits 16] [--floor-db -70]
//                       [--fade-ms 60] [--pre-ms 3] [--tail-ms 120]
//     cydaw_sample_tool wav <in> <out.wav>
//
// Reads a recorded note (any format CyDAW reads), cuts the silence before it
// (from a few milliseconds before the first sound above -50 dB of its peak)
// and after it (past the last 20 ms window above `floor-db` of the loudest
// one, faded out), and writes it as FLAC, dithered to 16 bits by default.
// The levels are kept: velocity layers stay as loud as they were played.
// Prints one JSON line: frames, rate, channels, the frames cut at the start,
// the peak in dB.
//
// `wav` writes any file CyDAW reads (FLAC, AIFF, OGG...) as a 24-bit WAV,
// as it is, for the tools that read only WAV (the companion's analysis).

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

namespace {

int fail(const juce::String& message)
{
    std::cout << "{\"error\": " << juce::JSON::toString(message).toStdString() << "}" << std::endl;
    return 1;
}

double option(const juce::StringArray& arguments, const juce::String& name, const double fallback)
{
    const auto at = arguments.indexOf(name);
    return at >= 0 && at + 1 < arguments.size() ? arguments[at + 1].getDoubleValue() : fallback;
}

int convert(const juce::StringArray& arguments)
{
    if (arguments.size() < 3)
        return fail("usage: convert <in> <out.flac> [options]");
    const juce::File input(arguments[1]);
    const juce::File output(arguments[2]);
    const auto bits = static_cast<int>(option(arguments, "--bits", 16.0));
    const auto floorDb = option(arguments, "--floor-db", -70.0);
    const auto fadeMs = option(arguments, "--fade-ms", 60.0);
    const auto preMs = option(arguments, "--pre-ms", 3.0);
    const auto tailMs = option(arguments, "--tail-ms", 120.0);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(input));
    if (reader == nullptr)
        return fail("cannot read " + input.getFullPathName());
    const auto channels = std::clamp(static_cast<int>(reader->numChannels), 1, 2);
    const auto frames = static_cast<int>(reader->lengthInSamples);
    const auto rate = reader->sampleRate;
    if (frames <= 0 || rate <= 0.0)
        return fail("empty " + input.getFullPathName());
    juce::AudioBuffer<float> audio(channels, frames);
    reader->read(&audio, 0, frames, 0, true, channels > 1);
    reader.reset();

    auto peak = 0.0f;
    for (int channel = 0; channel < channels; ++channel)
        peak = std::max(peak, audio.getMagnitude(channel, 0, frames));
    if (peak <= 0.0f)
        return fail("silent " + input.getFullPathName());

    // The start: a few milliseconds before the first sound.
    const auto onsetLevel = peak * std::pow(10.0f, -50.0f / 20.0f);
    auto onset = 0;
    for (auto frame = 0; frame < frames && onset == 0; ++frame)
        for (int channel = 0; channel < channels; ++channel)
            if (std::abs(audio.getSample(channel, frame)) > onsetLevel) {
                onset = std::max(1, frame);
                break;
            }
    const auto start = std::max(0, onset - static_cast<int>(rate * preMs * 0.001));

    // The end: past the last window above the floor, faded out.
    const auto window = std::max(1, static_cast<int>(rate * 0.02));
    std::vector<double> levels;
    for (auto at = 0; at < frames; at += window) {
        const auto count = std::min(window, frames - at);
        auto level = 0.0;
        for (int channel = 0; channel < channels; ++channel)
            level = std::max(level, static_cast<double>(audio.getRMSLevel(channel, at, count)));
        levels.push_back(level);
    }
    const auto loudest = *std::ranges::max_element(levels);
    const auto threshold = loudest * std::pow(10.0, floorDb / 20.0);
    auto lastWindow = 0;
    for (auto index = 0; index < static_cast<int>(levels.size()); ++index)
        if (levels[static_cast<std::size_t>(index)] > threshold)
            lastWindow = index;
    const auto end = std::min(frames, (lastWindow + 1) * window + static_cast<int>(rate * tailMs * 0.001));
    const auto length = std::max(1, end - start);
    juce::AudioBuffer<float> kept(channels, length);
    for (int channel = 0; channel < channels; ++channel)
        kept.copyFrom(channel, 0, audio, channel, start, length);
    const auto fade = std::min(length / 2, static_cast<int>(rate * fadeMs * 0.001));
    for (auto index = 0; index < fade; ++index) {
        const auto gain = static_cast<float>(0.5 + 0.5 * std::cos(juce::MathConstants<double>::pi
                                                                  * static_cast<double>(index + 1) / fade));
        for (int channel = 0; channel < channels; ++channel)
            kept.setSample(channel, length - fade + index, kept.getSample(channel, length - fade + index) * gain);
    }
    // Two-LSB triangular dither before the writer rounds to its bits.
    if (bits < 24) {
        std::mt19937 random(12345u);
        std::uniform_real_distribution<float> uniform(-0.5f, 0.5f);
        const auto lsb = 1.0f / static_cast<float>(1 << (bits - 1));
        for (int channel = 0; channel < channels; ++channel)
            for (auto index = 0; index < length; ++index)
                kept.setSample(channel, index, kept.getSample(channel, index) + (uniform(random) + uniform(random)) * lsb);
    }

    output.getParentDirectory().createDirectory();
    output.deleteFile();
    juce::FlacAudioFormat flac;
    auto stream = output.createOutputStream();
    if (stream == nullptr)
        return fail("cannot write " + output.getFullPathName());
    std::unique_ptr<juce::OutputStream> owned(stream.release());
    auto options = juce::AudioFormatWriterOptions {}
                       .withSampleRate(rate)
                       .withNumChannels(channels)
                       .withBitsPerSample(bits)
                       .withQualityOptionIndex(5);
    auto writer = flac.createWriterFor(owned, options);
    if (writer == nullptr)
        return fail("no FLAC writer for " + output.getFullPathName());
    writer->writeFromAudioSampleBuffer(kept, 0, length);
    writer.reset();

    auto* result = new juce::DynamicObject();
    result->setProperty("frames", length);
    result->setProperty("rate", rate);
    result->setProperty("channels", channels);
    result->setProperty("cut", start);
    result->setProperty("peakDb", juce::Decibels::gainToDecibels(peak));
    result->setProperty("bytes", output.getSize());
    std::cout << juce::JSON::toString(juce::var(result), true).toStdString() << std::endl;
    return 0;
}

int toWav(const juce::StringArray& arguments)
{
    if (arguments.size() < 3)
        return fail("usage: wav <in> <out.wav>");
    const juce::File input(arguments[1]);
    const juce::File output(arguments[2]);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(input));
    if (reader == nullptr)
        return fail("cannot read " + input.getFullPathName());
    const auto channels = static_cast<int>(reader->numChannels);
    const auto frames = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> audio(channels, frames);
    reader->read(&audio, 0, frames, 0, true, true);
    output.getParentDirectory().createDirectory();
    output.deleteFile();
    auto stream = output.createOutputStream();
    if (stream == nullptr)
        return fail("cannot write " + output.getFullPathName());
    std::unique_ptr<juce::OutputStream> owned(stream.release());
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(owned, juce::AudioFormatWriterOptions {}
                                                 .withSampleRate(reader->sampleRate)
                                                 .withNumChannels(channels)
                                                 .withBitsPerSample(24));
    if (writer == nullptr)
        return fail("no WAV writer for " + output.getFullPathName());
    writer->writeFromAudioSampleBuffer(audio, 0, frames);
    writer.reset();
    auto* result = new juce::DynamicObject();
    result->setProperty("frames", frames);
    result->setProperty("rate", reader->sampleRate);
    result->setProperty("channels", channels);
    std::cout << juce::JSON::toString(juce::var(result), true).toStdString() << std::endl;
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    juce::StringArray arguments;
    for (auto index = 1; index < argc; ++index)
        arguments.add(juce::String::fromUTF8(argv[index]));
    if (arguments.isEmpty())
        return fail("usage: cydaw_sample_tool convert <in> <out.flac> [options]");
    if (arguments[0] == "convert")
        return convert(arguments);
    if (arguments[0] == "wav")
        return toWav(arguments);
    return fail("unknown command " + arguments[0]);
}
