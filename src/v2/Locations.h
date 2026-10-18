#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <mutex>
#include <optional>

// Where CyDAW keeps what it downloads, builds and makes. Each folder is chosen
// in Options › Emplacements and kept in %APPDATA%/CyDAW/Locations.json (only
// those moved from their default):
//
//   banks          the sound banks' samples                %LOCALAPPDATA%/CyDAW/Banks
//   downloads      files on their way (.part)              %LOCALAPPDATA%/CyDAW/Downloads
//   cache          the sample index, VST3 builds           %LOCALAPPDATA%/CyDAW/Cache
//   companion      the Python companion's environment      %LOCALAPPDATA%/CyDAW/companion
//   aiModels       the models the semantic search runs     %LOCALAPPDATA%/CyDAW/Models
//   soundModels    the sound models made here              Documents/CyDAW/Sound Models
//   devices        My devices                              Documents/CyDAW/Devices
//   pluginExports  exported plug-ins' projects             Documents/CyDAW/Plugin Exports
//   presets        the presets made here                   Documents/CyDAW/Presets
//   mixes          the DJ mixes recorded                   Documents/CyDAW/Mixes
//   foundry        the Foundry's new sound files           Documents/CyDAW/Foundry
//
// CYDAW_BANKS_DIR moves the banks' default (tools/factory_content reads it,
// and this file, too); CYDAW_LOCATIONS_FILE names another file (a test's).
// "All in one folder" (Studio::setAllLocations) puts each in its own folder
// inside one chosen (inside()), the downloaded ones and, if asked, the
// person's creations too. An automated session (control port) never saves.

namespace cydaw::v2 {

enum class Location { banks, downloads, cache, companion, aiModels, soundModels, devices, pluginExports, presets, mixes,
                      foundry };
inline constexpr std::array allLocations { Location::banks,       Location::downloads, Location::cache,
                                           Location::companion,   Location::aiModels,  Location::soundModels,
                                           Location::devices,     Location::presets,   Location::pluginExports,
                                           Location::mixes,       Location::foundry };

namespace locations {

[[nodiscard]] juce::File folder(Location location);
[[nodiscard]] juce::File defaultFolder(Location location);
[[nodiscard]] bool isDefault(Location location);
[[nodiscard]] const char* key(Location location);
[[nodiscard]] std::optional<Location> fromKey(const juce::String& key);
[[nodiscard]] juce::File settingsFile();
// Read once, at the start.
void load();
// Off for an automated session without a file of its own: it reads the
// person's choices and never changes them.
void setSaving(bool saving);
// Chosen ({}: back to the default), nothing moved; saved at once.
void set(Location location, const juce::File& folder);

// What CyDAW put in a folder of this kind: what a move takes along, never
// anything else the folder may hold. Empty for the kinds nothing is moved
// from (files on their way, exports).
[[nodiscard]] juce::Array<juce::File> contentOf(Location location, const juce::File& folder);
[[nodiscard]] bool movable(Location location);
// What CyDAW downloads, builds and installs (banks, files on their way, cache,
// companion, AI models), as opposed to what the person makes with it.
[[nodiscard]] bool downloaded(Location location);
// Its folder inside a folder that holds them all ("Banks", "Cache"...).
[[nodiscard]] juce::File inside(Location location, const juce::File& root);

// A location's content taken to another folder on a thread of its own:
// renamed on the same drive, else copied, then the originals deleted once
// every copy is there (cancelled: the copies go, the originals stay).
class Move final : private juce::Thread {
public:
    Move(Location location, juce::File from, juce::File to);
    ~Move() override;

    struct Progress {
        juce::int64 done {};
        juce::int64 total {};
        juce::String current;
        bool finished {};
        bool cancelled {};
        juce::String error;
    };
    [[nodiscard]] Progress progress() const;
    [[nodiscard]] Location location() const noexcept { return location_; }
    [[nodiscard]] const juce::File& destination() const noexcept { return to_; }
    void cancel();

private:
    void run() override;
    void fail(const juce::String& error);

    const Location location_;
    const juce::File from_;
    const juce::File to_;
    mutable std::mutex mutex_;
    Progress progress_;
};

} // namespace locations
} // namespace cydaw::v2
