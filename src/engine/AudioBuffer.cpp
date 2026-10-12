#include <cydaw/engine/AudioBuffer.h>

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace cydaw::engine {

float* AudioBlockView::channel(const std::uint32_t index) const noexcept
{
    assert(index < channelCount);
    return data + static_cast<std::size_t>(index) * channelStride;
}

bool AudioBlockView::empty() const noexcept
{
    return data == nullptr || channelCount == 0 || frameCount == 0;
}

AudioBuffer::AudioBuffer(const std::uint32_t channels, const std::uint32_t maximumFrames)
{
    prepare(channels, maximumFrames);
}

void AudioBuffer::prepare(const std::uint32_t channels, const std::uint32_t maximumFrames)
{
    channelCount_ = channels;
    maximumFrameCount_ = maximumFrames;
    storage_.assign(static_cast<std::size_t>(channels) * maximumFrames, 0.0f);
}

void AudioBuffer::clear(const std::uint32_t frameCount) noexcept
{
    clearAudio(view(frameCount));
}

AudioBlockView AudioBuffer::view(const std::uint32_t frameCount) noexcept
{
    assert(frameCount <= maximumFrameCount_);
    const auto safeFrameCount = std::min(frameCount, maximumFrameCount_);
    return { storage_.data(), channelCount_, safeFrameCount, maximumFrameCount_ };
}

std::uint32_t AudioBuffer::channelCount() const noexcept { return channelCount_; }
std::uint32_t AudioBuffer::maximumFrameCount() const noexcept { return maximumFrameCount_; }

void clearAudio(const AudioBlockView block) noexcept
{
    for (std::uint32_t channel = 0; channel < block.channelCount; ++channel)
        std::fill_n(block.channel(channel), block.frameCount, 0.0f);
}

void copyAudio(const AudioBlockView destination, const AudioBlockView source) noexcept
{
    clearAudio(destination);
    const auto channels = std::min(destination.channelCount, source.channelCount);
    const auto frames = std::min(destination.frameCount, source.frameCount);
    for (std::uint32_t channel = 0; channel < channels; ++channel)
        std::copy_n(source.channel(channel), frames, destination.channel(channel));
}

void addAudio(const AudioBlockView destination, const AudioBlockView source) noexcept
{
    const auto channels = std::min(destination.channelCount, source.channelCount);
    const auto frames = std::min(destination.frameCount, source.frameCount);
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        auto* output = destination.channel(channel);
        const auto* input = source.channel(channel);
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            output[frame] += input[frame];
    }
}

} // namespace cydaw::engine

