#pragma once

#include <cydaw/engine/AudioRenderPlan.h>
#include <cydaw/realtime/SpscRing.h>

#include <cstdint>
#include <memory>

namespace cydaw::engine {

enum class AudioCommandType : std::uint8_t {
    play,
    record,
    stop,
    seekSamples,
    setTempo,
    setTimeSignature
};

struct AudioCommand {
    AudioCommandType type { AudioCommandType::stop };
    std::int64_t integerValue {};
    double floatingValue {};
    std::uint16_t smallValue {};

    [[nodiscard]] static AudioCommand play() noexcept;
    [[nodiscard]] static AudioCommand record() noexcept;
    [[nodiscard]] static AudioCommand stop() noexcept;
    [[nodiscard]] static AudioCommand seek(std::int64_t samplePosition) noexcept;
    [[nodiscard]] static AudioCommand tempo(double beatsPerMinute) noexcept;
    [[nodiscard]] static AudioCommand timeSignature(std::uint16_t numerator,
                                                    std::uint16_t denominator) noexcept;
};

class RealtimeEngine final {
public:
    static constexpr std::size_t commandCapacity = 256;

    explicit RealtimeEngine(ProcessSpec spec) noexcept;

    // Install before audio processing starts. A later lock-free plan exchange
    // will make live graph replacement safe without stopping the device.
    [[nodiscard]] bool installPlan(std::unique_ptr<AudioRenderPlan> plan) noexcept;
    [[nodiscard]] bool postCommand(const AudioCommand& command) noexcept;

    void process(AudioBlockView input, AudioBlockView output) noexcept;
    void reset() noexcept;

    [[nodiscard]] TransportSnapshot transportSnapshot() const noexcept;
    [[nodiscard]] const ProcessSpec& spec() const noexcept;
    [[nodiscard]] std::size_t queuedCommandCountApprox() const noexcept;

private:
    void applyCommand(const AudioCommand& command) noexcept;

    ProcessSpec spec_;
    TransportClock transport_;
    std::unique_ptr<AudioRenderPlan> plan_;
    realtime::SpscRing<AudioCommand, commandCapacity> commands_;
};

} // namespace cydaw::engine

