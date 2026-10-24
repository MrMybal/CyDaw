#include "UiState.h"

namespace cydaw::v2 {

WaveformCache::WaveformCache(juce::AudioFormatManager& formats) : formats_(formats) {}

WaveformCache::~WaveformCache() = default;

juce::AudioThumbnail* WaveformCache::get(const juce::File& file)
{
    const auto key = file.getFullPathName();
    // The disk asked whether the file is there once a second at most: asked
    // for every clip drawn, at every frame, it was a good part of a frame
    // (more with a virus scanner looking at each request).
    const auto now = juce::Time::getMillisecondCounterHiRes();
    auto& present = present_[key];
    if (present.first == 0.0 || now - present.first > 1'000.0)
        present = { now, file.existsAsFile() };
    if (!present.second)
        return nullptr;
    const auto found = thumbnails_.find(key);
    if (found != thumbnails_.end())
        return found->second.get();
    // 128 samples per peak: fine enough for a close zoom, light to hold.
    auto thumbnail = std::make_unique<juce::AudioThumbnail>(128, formats_, cache_);
    thumbnail->setSource(new juce::FileInputSource(file));
    auto* raw = thumbnail.get();
    thumbnails_.emplace(key, std::move(thumbnail));
    return raw;
}

bool WaveformCache::anyLoading() const
{
    for (const auto& [path, thumbnail] : thumbnails_)
        if (!thumbnail->isFullyLoaded())
            return true;
    return false;
}

void WaveformCache::clear()
{
    thumbnails_.clear();
    present_.clear();
}

} // namespace cydaw::v2
