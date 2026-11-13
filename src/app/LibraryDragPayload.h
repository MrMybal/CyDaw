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
    const cydaw::base::File& file)
{
    if (file == cydaw::base::File {})
        return {};
    return cydaw::base::Text(sampleDragPayloadPrefix)
           + file.getFullPathName();
}

[[nodiscard]] inline std::optional<cydaw::base::File> sampleFileFromDragPayload(
    const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(sampleDragPayloadPrefix))
        return std::nullopt;
    const auto path = description.substring(
        static_cast<int>(cydaw::base::Text(sampleDragPayloadPrefix).length()));
    if (path.isEmpty())
        return std::nullopt;
    return cydaw::base::File(path);
}

[[nodiscard]] inline cydaw::base::Value makeLauncherClipDragPayload(
    const std::size_t trackIndex, const int sceneIndex)
{
    if (sceneIndex < 0)
        return {};
    return cydaw::base::Text(launcherClipDragPayloadPrefix)
           + cydaw::base::Text(static_cast<juce::int64>(trackIndex)) + ":"
           + cydaw::base::Text(sceneIndex);
}

[[nodiscard]] inline std::optional<LauncherClipDragSource>
launcherClipFromDragPayload(const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(launcherClipDragPayloadPrefix))
        return std::nullopt;
    const auto encoded = description.substring(static_cast<int>(
        cydaw::base::Text(launcherClipDragPayloadPrefix).length()));
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
    const cydaw::base::Text& deviceKey)
{
    if (deviceKey.isEmpty())
        return {};
    return cydaw::base::Text(factoryDeviceDragPayloadPrefix) + deviceKey;
}

[[nodiscard]] inline std::optional<cydaw::base::Text>
factoryDeviceFromDragPayload(const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(factoryDeviceDragPayloadPrefix))
        return std::nullopt;
    const auto key = description.substring(static_cast<int>(
        cydaw::base::Text(factoryDeviceDragPayloadPrefix).length()));
    if (key.isEmpty())
        return std::nullopt;
    return cydaw::base::Text::fromUTF8(key.toRawUTF8());
}

[[nodiscard]] inline cydaw::base::Value makeGraphPresetDragPayload(
    const cydaw::base::Text& presetKey)
{
    if (presetKey.isEmpty())
        return {};
    return cydaw::base::Text(graphPresetDragPayloadPrefix) + presetKey;
}

[[nodiscard]] inline std::optional<cydaw::base::Text>
graphPresetFromDragPayload(const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(graphPresetDragPayloadPrefix))
        return std::nullopt;
    const auto key = description.substring(static_cast<int>(
        cydaw::base::Text(graphPresetDragPayloadPrefix).length()));
    if (key.isEmpty())
        return std::nullopt;
    return cydaw::base::Text::fromUTF8(key.toRawUTF8());
}

[[nodiscard]] inline cydaw::base::Value makeGraphPresetFileDragPayload(
    const cydaw::base::File& file)
{
    if (file == cydaw::base::File {})
        return {};
    return cydaw::base::Text(graphPresetFileDragPayloadPrefix)
           + file.getFullPathName();
}

[[nodiscard]] inline std::optional<cydaw::base::File>
graphPresetFileFromDragPayload(const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(graphPresetFileDragPayloadPrefix))
        return std::nullopt;
    const auto path = description.substring(static_cast<int>(
        cydaw::base::Text(graphPresetFileDragPayloadPrefix).length()));
    if (path.isEmpty())
        return std::nullopt;
    return cydaw::base::File(path);
}

[[nodiscard]] inline cydaw::base::Value makePluginDragPayload(
    const cydaw::base::File& file)
{
    if (file == cydaw::base::File {})
        return {};
    return cydaw::base::Text(pluginDragPayloadPrefix)
           + file.getFullPathName();
}

[[nodiscard]] inline std::optional<cydaw::base::File> pluginFileFromDragPayload(
    const cydaw::base::Value& payload)
{
    const auto description = payload.toString();
    if (!description.startsWith(pluginDragPayloadPrefix))
        return std::nullopt;
    const auto path = description.substring(static_cast<int>(
        cydaw::base::Text(pluginDragPayloadPrefix).length()));
    if (path.isEmpty())
        return std::nullopt;
    return cydaw::base::File(path);
}

} // namespace cydaw::app
