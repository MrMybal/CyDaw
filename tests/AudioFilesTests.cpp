#include <array>
#include <cyaudio/io/AudioFile.h>
#include <cyaudio/io/AudioMetadata.h>
#include <cydaw/integration/CyAudioCoreAudioFormats.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int main() {
  try {
    auto file = base::File::createTempFile(".wav");
    std::array<float, 512> left{}, right{};
    for (int i = 0; i < 512; ++i) {
      left[i] = float(i) / 1024;
      right[i] = -left[i];
    }
    const float *input[] = {left.data(), right.data()};
    integration::WaveFormat format;
    std::unique_ptr<base::OutputStream> stream = file.createOutputStream();
    auto options = audio::AudioFormatWriterOptions()
                       .withSampleRate(48000)
                       .withNumChannels(2)
                       .withBitsPerSample(24)
                       .withMetadata("NumCuePoints", "1")
                       .withMetadata("Cue0Identifier", "9")
                       .withMetadata("Cue0Offset", "123")
                       .withMetadata("NumCueLabels", "1")
                       .withMetadata("CueLabel0Identifier", "9")
                       .withMetadata("CueLabel0Text", "Impact");
    auto writer = format.createWriterFor(stream, options);
    require(bool(writer) && !stream, "native writer owns output");
    {
      audio::AudioFormatWriter::ThreadedWriter recorder(writer.release(), 8192);
      require(recorder.write(input, 512), "recorder accepts prepared block");
    } // drains and finalises
    audio::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<audio::AudioFormatReader> reader(
        formats.createReaderFor(file));
    require(reader && reader->sampleRate == 48000 &&
                reader->lengthInSamples == 512 && reader->numChannels == 2,
            "native decoder header");
    std::array<float, 520> a{}, b{};
    float *output[] = {a.data(), b.data()};
    require(reader->read(output, 2, -4, 520),
            "bounded reader handles both ends");
    for (int i = 0; i < 4; ++i)
      require(a[i] == 0 && a[516 + i] == 0, "out-of-file samples are silent");
    for (int i = 0; i < 512; ++i)
      require(std::abs(a[i + 4] - left[i]) < 1e-6 &&
                  std::abs(b[i + 4] - right[i]) < 1e-6,
              "24-bit PCM stereo roundtrip");
    require(reader->metadataValues.getValue("CueLabel0Text") == "Impact" &&
                reader->metadataValues.getValue("Cue0Offset") == "123",
            "markers remain available to editor");
    reader.reset();
    require(file.deleteFile(), "remove audio fixture");
    std::cout << "Native CyAudioCore files, recorder, bounds and markers "
                 "passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
