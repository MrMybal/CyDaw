#pragma once

#include <string_view>
#include <vector>

namespace cydaw::platform {

enum class AudioBackend {
    asio,
    wasapiExclusive,
    wasapiShared,
    coreAudio,
    alsa,
    pipeWire,
    jack
};

struct AudioBackendInfo {
    AudioBackend id;
    std::string_view displayName;
    bool professionalLowLatency;
    bool supportsInterApplicationRouting;
    bool compileTimeOptional;
};

[[nodiscard]] const std::vector<AudioBackendInfo>& knownAudioBackends();

} // namespace cydaw::platform

