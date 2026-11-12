#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

namespace cydaw::integration {
// Transitional boundary only: sample decoding is CyDSP. JUCE's reader/source
// interfaces remain until the desktop source graph has been ported. Metadata
// uses the legacy parser explicitly to preserve existing markers/tags/loops.
void registerCyDSPReaders(juce::AudioFormatManager&);
// CyDSP writes untagged WAV audio. Tagged exports deliberately retain the
// established writer until CyDSP's LIST/adtl roundtrip is fixed.
class WaveFormat final : public juce::WavAudioFormat {
public:
    using juce::WavAudioFormat::createWriterFor;
    std::unique_ptr<juce::AudioFormatWriter> createWriterFor(
        std::unique_ptr<juce::OutputStream>&, const juce::AudioFormatWriterOptions&) override;
};
} // namespace cydaw::integration
