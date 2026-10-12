#pragma once

#include <cstdint>
#include <vector>

namespace cydaw::engine {

struct AudioBlockView {
    float* data {};
    std::uint32_t channelCount {};
    std::uint32_t frameCount {};
    std::uint32_t channelStride {};

    [[nodiscard]] float* channel(std::uint32_t index) const noexcept;
    [[nodiscard]] bool empty() const noexcept;
};

class AudioBuffer final {
public:
    AudioBuffer() = default;
    AudioBuffer(std::uint32_t channels, std::uint32_t maximumFrames);

    // Control-thread only. All storage used by process() is allocated here.
    void prepare(std::uint32_t channels, std::uint32_t maximumFrames);
    void clear(std::uint32_t frameCount) noexcept;

    [[nodiscard]] AudioBlockView view(std::uint32_t frameCount) noexcept;
    [[nodiscard]] std::uint32_t channelCount() const noexcept;
    [[nodiscard]] std::uint32_t maximumFrameCount() const noexcept;

private:
    std::vector<float> storage_;
    std::uint32_t channelCount_ {};
    std::uint32_t maximumFrameCount_ {};
};

void clearAudio(AudioBlockView block) noexcept;
void copyAudio(AudioBlockView destination, AudioBlockView source) noexcept;
void addAudio(AudioBlockView destination, AudioBlockView source) noexcept;

} // namespace cydaw::engine

