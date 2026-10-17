#pragma once

#include <cydaw/engine/Sampler.h>
#include <cydaw/engine/SoundModel.h>

#include <functional>
#include <iosfwd>
#include <string>

// What an exported instrument plays beyond its numbers: its sampler's zones
// with every sample's audio, its sound model. CyDAW writes them when it
// exports a native plug-in (a .cyvoice file beside the binary, in its
// bundle); the plug-in reads them back without JUCE, before it plays.
//
// Binary, little-endian: "CYVA", a version, which parts follow, then the
// map (its samples, 16-bit frames as the sampler holds them, then its zones,
// controllers, curves and keyswitches) and the model (its zones' tables).

namespace cydaw::engine {

struct VoiceAssets {
    SamplerMapRef sampler;
    SoundModelRef soundModel;

    [[nodiscard]] bool empty() const noexcept { return sampler == nullptr && soundModel == nullptr; }
};

// A zone's sample with every frame in memory (decoded again when the one
// playing holds only its head); null leaves the zone silent.
using WholeSample = std::function<SampleDataRef(const SampleDataRef&)>;

// False with `error` set when the stream fails or a sample cannot be had.
bool writeVoiceAssets(std::ostream& out, const SamplerMap* sampler, const WholeSample& whole, const SoundModel* soundModel,
                      std::string& error);
// Every sample ready to play. Empty with `error` set when the data is not
// a voice this version reads (or is cut short).
[[nodiscard]] VoiceAssets readVoiceAssets(std::istream& in, std::string& error);

} // namespace cydaw::engine
