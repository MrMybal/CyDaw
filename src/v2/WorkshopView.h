#pragma once

#include "Studio.h"
#include "UiState.h"

#include <imgui.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

// The Plugin workshop: a mode of the whole window, apart from the studio
// (tracks, arrangement), for building one device made of a graph, an effect
// or an instrument, as a plugin is built.
//
// On the left the nodes to build with and My devices; the device's graph in
// the middle, the devices inside it opened in place; on the right what it
// says of itself, the knobs it publishes (in the device panel, and to a VST3
// host), and its VST3 export, built here when CMake and CyDAW's sources are
// at hand. Tried on a track, the device stays linked to it: every change is
// heard there at once, with the transport's play.

namespace cydaw::v2 {

class DeviceSignals;
class GraphEditor;
class InterfaceEditor;
class ImGuiHost;
class Vst3Build;

class WorkshopView final {
public:
    WorkshopView(Studio& studio, UiState& state, ImGuiHost& host, DeviceSignals& signals, Vst3Build& build);
    ~WorkshopView();
    void draw();
    // Before the application quits: asks about a device that would be lost
    // and runs `then` unless the person cancels. False when there is
    // nothing to ask (the caller goes on).
    bool askBeforeQuit(std::function<void()> then);

private:
    void perform(const WorkshopRequest& request);
    void drawTopBar();
    void drawLibrary();
    void drawPalette();
    void drawSide();
    void drawInfo(app::NodeGraphComponent& graph);
    void drawParameters(app::NodeGraphComponent& graph);
    void drawExport(GraphSlot& slot);
    void drawDiscard();
    // The devices open, one tab each; + makes another.
    void drawTabs();
    // Under the graph: the simulator's sources, its keyboard, its output.
    void drawSimulator();
    void drawKeyboard(ImVec2 size);
    void chooseSample();
    void chooseDevice(bool exporting);
    // Before quitting: the next device that would be lost comes forward.
    bool askAboutNextAtRisk();
    void refreshLibrary(bool force);
    void save(bool asNewFile);
    void report(const juce::String& message, bool error);
    // A text field kept in step with the model while it is not being typed in.
    bool textField(const char* id, std::vector<char>& buffer, const juce::String& current, float width, bool multiline = false,
                   float height = 0.0f);

    Studio& studio_;
    UiState& state_;
    ImGuiHost& host_;
    std::unique_ptr<GraphEditor> graph_;
    std::unique_ptr<InterfaceEditor> interface_;
    // The middle shows the graph, or the interface it opens as a plugin.
    bool interfaceMode_ {};
    Vst3Build& build_;

    // A request that waits for the person to say what becomes of the device
    // not saved.
    std::optional<WorkshopRequest> waiting_;
    // The tab whose closing waits for the answer.
    std::uint64_t closingTab_ {};
    // The tab ImGui was last told to show.
    std::uint64_t tabShown_ {};
    bool openNewMenu_ {};
    bool showSimulator_ { true };
    // The note the mouse holds down on the keyboard.
    int mouseNote_ { -1 };
    std::unique_ptr<juce::FileChooser> chooser_;
    std::function<void()> quitWaiting_;
    bool askDiscard_ {};
    juce::File removing_;
    bool askRemove_ {};

    std::vector<Studio::LibraryDevice> library_;
    double libraryTime_ { -100.0 };
    std::vector<char> search_ = std::vector<char>(96, 0);
    std::vector<char> nodeSearch_ = std::vector<char>(96, 0);
    std::vector<app::NodeGraphComponent::EditorNodeType> nodeTypes_;
    const void* nodeTypesSlot_ {};
    int nodeTypesLanguage_ { -1 };
    bool showLibrary_ { true };
    bool showSide_ { true };

    std::vector<char> name_ = std::vector<char>(128, 0);
    std::vector<char> topName_ = std::vector<char>(128, 0);
    std::vector<char> author_ = std::vector<char>(128, 0);
    std::vector<char> category_ = std::vector<char>(128, 0);
    std::vector<char> description_ = std::vector<char>(2048, 0);
    std::vector<char> product_ = std::vector<char>(128, 0);
    std::vector<char> company_ = std::vector<char>(128, 0);
    std::vector<char> manufacturer_ = std::vector<char>(8, 0);
    std::vector<char> pluginCode_ = std::vector<char>(8, 0);
    std::vector<char> version_ = std::vector<char>(24, 0);
    std::vector<char> sourceRoot_ = std::vector<char>(512, 0);
    std::vector<std::vector<char>> labels_;
    // The export check, done again when the device changes.
    const void* planSlot_ {};
    std::uint64_t planRevision_ {};
    double planTime_ {};
    bool planReady_ {};
    juce::StringArray planIssues_;
    juce::StringArray planDetails_;
    int planParameters_ {};
    juce::String exported_;
    juce::String deviceTransferMessage_;
    bool deviceTransferWarning_ {};
    bool exportFormats_[3] {true, false, false};
    bool exportWindows_ {true}, exportLinux_ {};
};

} // namespace cydaw::v2
