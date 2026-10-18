#pragma once

// The controllers tool's inside, shared by Controllers.cpp (devices, routing,
// files, commands) and ControllersView.cpp (the window, learn mode).

#include "ControlTargets.h"
#include "Controllers.h"

#include <cydaw/control/Control.h>
#include <cydaw/control/ControlPluginABI.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_osc/juce_osc.h>

#include <deque>
#include <map>
#include <mutex>
#include <set>

namespace cydaw::v2 {

// The computer's keyboard as a raw device.
inline constexpr const char* computerKeyboardId = "keyboard:computer";

// A raw address as a person reads it: "CC 74 · ch. 1", "Key F1".
[[nodiscard]] juce::String readableAddress(const std::string& address);

class Controllers::Impl final : private juce::MidiInputCallback, private juce::AsyncUpdater, private juce::Timer {
public:
    Impl(Studio& studio, UiState& state, ImGuiHost& host, bool keepSettings);
    ~Impl() override;

    // ---- RAW devices ----------------------------------------------------------------------
    // The computer's keyboard is one too: its keys ("key/F1"), while a
    // device listens to it.
    enum class RawKind { midiIn, midiOut, osc, plugin, keyboard };
    struct OscPort;
    struct RawDevice {
        std::string id;
        juce::String name;
        RawKind kind { RawKind::midiIn };
        bool enabled { true };
        // Plugged in (a MIDI port listed now, a plug-in's device announced).
        bool present {};
        // A MIDI input: its notes and controllers reach the instruments too
        // (those an assignment takes do not).
        bool notes { true };
        juce::String identifier;
        // OSC: where it listens (0: nowhere), where it answers.
        int listenPort {};
        juce::String sendHost { "127.0.0.1" };
        int sendPort {};
        // OSC: /cydaw/<target> moves the target itself.
        bool cydawAddresses { true };
        // A plug-in's device: which plug-in, its own id.
        int plugin { -1 };
        std::string pluginDevice;
        // Now.
        double activity {};
        juce::String error;
        std::unique_ptr<juce::MidiOutput> output;
        std::unique_ptr<OscPort> osc;
        // OSC: the /cydaw/ targets it touched (told back when they move).
        std::set<std::string> touched;
    };
    std::vector<std::unique_ptr<RawDevice>> raw;
    [[nodiscard]] RawDevice* rawDevice(std::string_view id);
    [[nodiscard]] const RawDevice* rawDevice(std::string_view id) const;
    void scanMidi();
    void applyRaw(RawDevice& device);
    void closeRaw(RawDevice& device);
    RawDevice& addOsc(const juce::String& name, int listenPort, const juce::String& sendHost, int sendPort);
    void removeRaw(std::string_view id);
    // The output that answers an input (the MIDI port of the same name, an
    // OSC socket itself, a plug-in's device itself).
    [[nodiscard]] std::string feedbackFor(std::string_view inputDevice) const;
    void send(std::string_view device, const control::RawMessage& message);
    [[nodiscard]] static bool isInput(RawKind kind) noexcept { return kind != RawKind::midiOut; }
    [[nodiscard]] static bool isOutput(RawKind kind) noexcept { return kind != RawKind::midiIn && kind != RawKind::keyboard; }
    // A raw device's name as shown (the computer's keyboard in the language).
    [[nodiscard]] juce::String nameOf(std::string_view id) const;
    // The family it is shown under: MIDI, a plug-in's name (gamepads), OSC.
    [[nodiscard]] juce::String familyOf(const RawDevice& device) const;

    // ---- The computer's keyboard ------------------------------------------------------------
    // Each frame, before the shortcuts and the keyboard that plays notes:
    // the keys a control listens to (and every key while one is learnt)
    // are said as "key/<name>" and kept from the rest.
    void pollKeys();
    std::map<int, bool> keysDown;
    // Every key is listened to: a learn waits for the computer's keyboard.
    [[nodiscard]] bool keyboardWanted();

    // ---- Application plug-ins -------------------------------------------------------------
    struct Plugin {
        juce::File file;
        std::unique_ptr<juce::DynamicLibrary> library;
        const CyControlPlugin* api {};
        juce::String name;
        juce::String error;
        bool started {};
    };
    std::vector<std::unique_ptr<Plugin>> plugins;
    struct PluginThread;
    std::unique_ptr<PluginThread> pluginThread;
    CyControlHost pluginHost {};
    void loadPlugins();
    void unloadPlugins();
    [[nodiscard]] static juce::Array<juce::File> pluginFolders();

    // ---- What the devices heard -----------------------------------------------------------
    struct Incoming {
        enum class Kind { message, added, removed } kind { Kind::message };
        std::string device;
        control::RawMessage message;
        juce::String name;
        int plugin { -1 };
    };
    std::mutex queueLock;
    std::vector<Incoming> queue;
    void post(Incoming incoming);
    void handleAsyncUpdate() override;
    void process(const Incoming& incoming);
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    // MIDI identifiers to raw ids, for the MIDI thread.
    std::mutex midiNamesLock;
    std::map<juce::String, std::string> midiNames;
    // What the instruments do not hear: a device's every message, or the
    // addresses an assignment takes. Read on the MIDI thread.
    std::mutex gateLock;
    std::set<std::string> silentDevices;
    std::set<std::pair<std::string, std::string>> takenAddresses;
    void refreshGate();
    [[nodiscard]] bool gatePasses(const juce::String& identifier, const juce::MidiMessage& message);

    // ---- Virtual controllers and assignments ----------------------------------------------
    control::Router router;
    ControlTargets targets;
    void applyOrders(const std::vector<control::Router::Order>& orders);
    // Targets told by this router, a moment ago (no feedback echo).
    std::map<std::string, std::pair<double, double>> ordered;
    // Each target's value as last told (feedback when it moves).
    std::map<std::string, double> told;
    void timerCallback() override;
    void feedback(bool force);
    std::string addVirtualDevice(const juce::String& name, const std::string& input = {}, const std::string& output = {});
    std::string duplicateVirtualDevice(const std::string& id);
    // A device's hardware changed: the controls that listened to the old
    // input listen to the new one; those that answered the old output
    // answer the new one, at their own address.
    void setDeviceInput(control::VirtualDevice& device, const std::string& input);
    void setDeviceOutput(control::VirtualDevice& device, const std::string& output);
    std::string addControl(const std::string& device, const juce::String& name, control::ControlKind kind,
                           const std::string& sourceDevice, const std::string& sourceAddress);
    // Learning replaces what moved the target; by hand, a second control adds to it.
    void assign(const std::string& device, const std::string& control, const std::string& target, bool replace);
    void unassign(std::size_t index);
    void removeVirtualDevice(const std::string& id);
    void removeControl(const std::string& device, const std::string& control);
    // The virtual device a raw device's learnt controls go into: the one
    // with it as its input, else a new one.
    std::string virtualDeviceFor(const RawDevice& device);

    // ---- Learning -------------------------------------------------------------------------
    bool learnMode {};
    std::string learnTarget;
    juce::String learnTargetLabel;
    // A control's source being learnt in the window.
    std::optional<std::pair<std::string, std::string>> learnSource;
    // A device whose hardware is found by moving something on it.
    std::string learnHardware;
    // A device whose controls are learnt: each new one moved on its input
    // becomes a control.
    std::string learnControls;
    // A control whose target is picked by a click in the interface.
    std::optional<std::pair<std::string, std::string>> pickTarget;
    juce::String learnStatus;
    void learnFrom(const std::string& rawDevice, const control::RawMessage& message);
    // A message heard while a device's hardware or controls are learnt:
    // whether it was taken.
    bool learnDevice(const RawDevice& source, const control::RawMessage& message);
    void stopLearning();

    // ---- The monitor ----------------------------------------------------------------------
    struct Seen {
        double time {};
        bool out {};
        std::string device;
        control::RawMessage message;
    };
    std::deque<Seen> monitor;
    bool monitorPaused {};
    void see(bool out, const std::string& device, const control::RawMessage& message);

    // ---- Files ----------------------------------------------------------------------------
    [[nodiscard]] juce::var globalState() const;
    void restoreGlobal(const juce::var& state);
    [[nodiscard]] juce::var projectAssignments() const;
    void restoreProject(const juce::var& assignments);
    void changed(bool global, bool project);
    void saveNow();
    bool globalDirty {};
    std::uint64_t projectRevision {};

    // ---- The window (ControllersView.cpp) -------------------------------------------------
    void draw(bool& visible);
    void drawButton(bool& visible);
    void drawOverlay();
    // The devices tab: the list, and the picked device (its hardware, its
    // controls, the picked control and what it moves).
    void drawDeviceList();
    // A new device, its hardware found by moving something on it.
    void newDevice();
    void drawDevice(control::VirtualDevice& device);
    void drawHardware(control::VirtualDevice& device);
    void drawControls(control::VirtualDevice& device);
    void drawControl(control::VirtualDevice& device, control::VirtualControl& control);
    void drawControlTargets(control::VirtualDevice& device, control::VirtualControl& control);
    bool rawCombo(const char* id, std::string& value, bool outputs, float width);
    void drawOscSettings(RawDevice& device);
    // The hardware tab, every assignment, the monitor.
    void drawRaw();
    void drawAssignments();
    void drawMonitor(const control::VirtualDevice* only);
    std::string selectedRaw;
    std::string selectedDevice;
    std::string selectedControl;
    int tab {};
    int requestedTab { -1 };
    // Brought back over the main window at its next frame.
    bool centreWindow {};
    // The window in front (its keys are learnt only then).
    bool windowFocused {};
    bool monitorAll {};
    juce::String newOscName { "OSC" };
    int newOscPort { 9000 };
    char targetFilter[96] {};

    juce::var command(const juce::var& request);
    [[nodiscard]] juce::var describe() const;

    Studio& studio;
    UiState& uiState;
    ImGuiHost& host;
    const bool keepSettings;
};

} // namespace cydaw::v2
