#include <cydaw/platform/AudioBackend.h>

namespace cydaw::platform {

const std::vector<AudioBackendInfo>& knownAudioBackends()
{
    static const std::vector<AudioBackendInfo> backends {
        { AudioBackend::asio, "ASIO", true, false, true },
        { AudioBackend::wasapiExclusive, "WASAPI Exclusive", true, false, false },
        { AudioBackend::wasapiShared, "WASAPI Shared", false, false, false },
        { AudioBackend::coreAudio, "CoreAudio", true, false, false },
        { AudioBackend::alsa, "ALSA", true, false, false },
        { AudioBackend::pipeWire, "PipeWire", true, true, true },
        { AudioBackend::jack, "JACK", true, true, true },
    };
    return backends;
}

} // namespace cydaw::platform

