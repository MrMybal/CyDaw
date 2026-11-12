#include <cydaw/engine/AudioBuffer.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

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

AudioBuffer::AudioBuffer(const AudioBuffer& other)
{
    prepare(other.channelCount_, other.maximumFrameCount_);
    const auto input = other.storage_.block();
    auto output = storage_.block();
    for (std::size_t ch = 0; ch < input.channels(); ++ch)
        std::copy(input.channel(ch).begin(), input.channel(ch).end(), output.channel(ch).begin());
}

AudioBuffer& AudioBuffer::operator=(const AudioBuffer& other)
{
    if (this != &other) {
        AudioBuffer next(other);
        *this = std::move(next);
    }
    return *this;
}

AudioBuffer::AudioBuffer(AudioBuffer&& other) noexcept
    : storage_(std::move(other.storage_)),
      channelStride_(std::exchange(other.channelStride_, 0)),
      channelCount_(std::exchange(other.channelCount_, 0)),
      maximumFrameCount_(std::exchange(other.maximumFrameCount_, 0)) {}

AudioBuffer& AudioBuffer::operator=(AudioBuffer&& other) noexcept
{
    if (this != &other) {
        storage_ = std::move(other.storage_);
        channelStride_ = std::exchange(other.channelStride_, 0);
        channelCount_ = std::exchange(other.channelCount_, 0);
        maximumFrameCount_ = std::exchange(other.maximumFrameCount_, 0);
    }
    return *this;
}

void AudioBuffer::prepare(const std::uint32_t channels, const std::uint32_t maximumFrames)
{
    // AudioBlockView's stride is 32-bit; CyDSP pads each channel to 64 bytes.
    const auto stride = (std::uint64_t(maximumFrames) + 15u) & ~std::uint64_t(15u);
    if (stride > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("Audio channel stride exceeds AudioBlockView");
    if (channels && maximumFrames) {
        const auto status = storage_.resize(channels, maximumFrames);
        if (status == cydsp::Status::allocation_failed)
            throw std::bad_alloc();
        if (status != cydsp::Status::ok)
            throw std::length_error("Invalid CyDSP audio storage size");
    } else {
        storage_ = {};
    }
    channelCount_ = channels;
    maximumFrameCount_ = maximumFrames;
    channelStride_ = static_cast<std::uint32_t>(stride);
}

void AudioBuffer::clear(const std::uint32_t frameCount) noexcept
{
    clearAudio(view(frameCount));
}

AudioBlockView AudioBuffer::view(const std::uint32_t frameCount) noexcept
{
    assert(frameCount <= maximumFrameCount_);
    const auto safeFrameCount = std::min(frameCount, maximumFrameCount_);
    auto block = storage_.block();
    return { block.channels() && block.frames() ? block.channel(0).data() : nullptr,
             channelCount_, safeFrameCount, channelStride_ };
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

