#include <cydaw/base/File.h>
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <limits>

namespace cydaw::app {

inline constexpr auto sampleDragPayloadPrefix = "cydaw-sample:";
inline constexpr auto launcherClipDragPayloadPrefix =
    "cydaw-launcher-clip:";
inline constexpr auto factoryDeviceDragPayloadPrefix = "cydaw-node:";
inline constexpr auto graphPresetDragPayloadPrefix = "cydaw-graph-preset:";
inline constexpr auto graphPresetFileDragPayloadPrefix =
    "cydaw-graph-file:";
inline constexpr auto pluginDragPayloadPrefix = "cydaw-plugin:";

struct LauncherClipDragSource {
    std::size_t trackIndex {};
    int sceneIndex {};
};

[[nodiscard]] inline cydaw::base::Value makeSampleDragPayload(
    const juce::File& file)
{
    if (file == juce::File {})
        return {};
    return juce::String(sampleDragPayloadPrefix)
           + file.getFullPathName();
}

[[nodiscard]] inline std::optional<juce::File> sampleFileFromDragPayload(
    const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(sampleDragPayloadPrefix))
        return std::nullopt;
    const auto path = description.substring(
        static_cast<int>(juce::String(sampleDragPayloadPrefix).length()));
    if (path.isEmpty())
        return std::nullopt;
    return juce::File(path);
}

[[nodiscard]] inline cydaw::base::Value makeLauncherClipDragPayload(
    const std::size_t trackIndex, const int sceneIndex)
{
    if (sceneIndex < 0)
        return {};
    return juce::String(launcherClipDragPayloadPrefix)
           + juce::String(static_cast<juce::int64>(trackIndex)) + ":"
           + juce::String(sceneIndex);
}

[[nodiscard]] inline std::optional<LauncherClipDragSource>
launcherClipFromDragPayload(const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(launcherClipDragPayloadPrefix))
        return std::nullopt;
    const auto encoded = description.substring(static_cast<int>(
        juce::String(launcherClipDragPayloadPrefix).length()));
    const auto separator = encoded.indexOfChar(':');
    if (separator <= 0 || separator == encoded.length() - 1
        || encoded.indexOfChar(separator + 1, ':') >= 0)
        return std::nullopt;
    const auto trackText = encoded.substring(0, separator);
    const auto sceneText = encoded.substring(separator + 1);
    if (!trackText.containsOnly("0123456789")
        || !sceneText.containsOnly("0123456789"))
        return std::nullopt;
    const auto trackValue = trackText.getLargeIntValue();
    const auto sceneValue = sceneText.getLargeIntValue();
    if (trackValue < 0 || sceneValue < 0
        || static_cast<unsigned long long>(trackValue)
               > static_cast<unsigned long long>(
                   std::numeric_limits<std::size_t>::max())
        || sceneValue > std::numeric_limits<int>::max())
        return std::nullopt;
    return LauncherClipDragSource {
        static_cast<std::size_t>(trackValue),
        static_cast<int>(sceneValue)
    };
}

[[nodiscard]] inline cydaw::base::Value makeFactoryDeviceDragPayload(
    const juce::String& deviceKey)
{
    if (deviceKey.isEmpty())
        return {};
    return juce::String(factoryDeviceDragPayloadPrefix) + deviceKey;
}

[[nodiscard]] inline std::optional<juce::String>
factoryDeviceFromDragPayload(const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(factoryDeviceDragPayloadPrefix))
        return std::nullopt;
    const auto key = description.substring(static_cast<int>(
        juce::String(factoryDeviceDragPayloadPrefix).length()));
    if (key.isEmpty())
        return std::nullopt;
    return juce::String::fromUTF8(key.toRawUTF8());
}

[[nodiscard]] inline cydaw::base::Value makeGraphPresetDragPayload(
    const juce::String& presetKey)
{
    if (presetKey.isEmpty())
        return {};
    return juce::String(graphPresetDragPayloadPrefix) + presetKey;
}

[[nodiscard]] inline std::optional<juce::String>
graphPresetFromDragPayload(const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(graphPresetDragPayloadPrefix))
        return std::nullopt;
    const auto key = description.substring(static_cast<int>(
        juce::String(graphPresetDragPayloadPrefix).length()));
    if (key.isEmpty())
        return std::nullopt;
    return juce::String::fromUTF8(key.toRawUTF8());
}

[[nodiscard]] inline cydaw::base::Value makeGraphPresetFileDragPayload(
    const juce::File& file)
{
    if (file == juce::File {})
        return {};
    return juce::String(graphPresetFileDragPayloadPrefix)
           + file.getFullPathName();
}

[[nodiscard]] inline std::optional<juce::File>
graphPresetFileFromDragPayload(const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(graphPresetFileDragPayloadPrefix))
        return std::nullopt;
    const auto path = description.substring(static_cast<int>(
        juce::String(graphPresetFileDragPayloadPrefix).length()));
    if (path.isEmpty())
        return std::nullopt;
    return juce::File(path);
}

[[nodiscard]] inline cydaw::base::Value makePluginDragPayload(
    const juce::File& file)
{
    if (file == juce::File {})
        return {};
    return juce::String(pluginDragPayloadPrefix)
           + file.getFullPathName();
}

[[nodiscard]] inline std::optional<juce::File> pluginFileFromDragPayload(
    const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(pluginDragPayloadPrefix))
        return std::nullopt;
    const auto path = description.substring(static_cast<int>(
        juce::String(pluginDragPayloadPrefix).length()));
    if (path.isEmpty())
        return std::nullopt;
    return juce::File(path);
}

} // namespace cydaw::app
