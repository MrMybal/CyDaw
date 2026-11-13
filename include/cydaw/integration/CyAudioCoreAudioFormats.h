#pragma once
#include <cydaw/integration/AudioFiles.h>
#include <cyaudio/io/AudioFile.h>

namespace cydaw::integration {
// Transitional boundary only: sample decoding is CyAudioCore. JUCE's reader/source
// interfaces remain until the desktop source graph has been ported. Metadata
// exposes typed WAV markers/regions/loops through the stable application vocabulary.
// Supplemental legacy parsing preserves other formats and non-typed fields.
void registerCyAudioCoreReaders(audio::AudioFormatManager&);
// CyAudioCore writes WAV audio and supported cue/region/loop/INFO metadata. Unsupported
// legacy metadata retains the established writer explicitly, without losing fields.
class WaveFormat final : public audio::AudioFormat {
public:
    WaveFormat():audio::AudioFormat("WAV", ".wav .bwf"){}
    using audio::AudioFormat::createWriterFor;
    audio::AudioFormatReader* createReaderFor(base::InputStream*, bool) override;
    std::unique_ptr<audio::AudioFormatWriter> createWriterFor(
        std::unique_ptr<base::OutputStream>&, const audio::AudioFormatWriterOptions&) override;
};
// File codecs only; keeps the current source/writer ABI while replacing the
// underlying AIFF, FLAC and Vorbis encoders in Fonderie.
class FileFormat : public audio::AudioFormat {
public:
    explicit FileFormat(cyaudio::AudioFileFormat);
    using audio::AudioFormat::createWriterFor;
    base::Array<int> getPossibleSampleRates() override;
    base::Array<int> getPossibleBitDepths() override;
    base::TextList getQualityOptions() override;
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }
    audio::AudioFormatReader* createReaderFor(base::InputStream*, bool) override;
    std::unique_ptr<audio::AudioFormatWriter> createWriterFor(
        std::unique_ptr<base::OutputStream>&, const audio::AudioFormatWriterOptions&) override;
private:
    cyaudio::AudioFileFormat format_;
};
class AiffFormat final : public FileFormat { public: AiffFormat() : FileFormat(cyaudio::AudioFileFormat::aiff) {} };
class FlacFormat final : public FileFormat { public: FlacFormat() : FileFormat(cyaudio::AudioFileFormat::flac) {} };
class OggFormat final : public FileFormat { public: OggFormat() : FileFormat(cyaudio::AudioFileFormat::oggVorbis) {} };
} // namespace cydaw::integration
