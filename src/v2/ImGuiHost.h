#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>

// A JUCE component showing a Dear ImGui interface. On Windows it embeds a
// native child window rendered with Direct3D 11; everything — input, the
// interface code, the rendering — runs on the message thread, paced by the
// display's vertical blank, so the interface can call the engine directly.
//
// Frames run at the display rate while something moves (input, playback,
// animation) and drop to a low idle rate otherwise.

namespace cydaw::v2 {

class ImGuiHost final : public juce::Component {
public:
    ImGuiHost();
    ~ImGuiHost() override;

    // Builds the interface; called between ImGui::NewFrame() and Render().
    std::function<void()> onFrame;
    // Input added after the system's, before ImGui::NewFrame() (the control
    // port's scripted mouse).
    std::function<void()> beforeNewFrame;
    // Files dropped from the system file manager, at a point in interface
    // coordinates (physical pixels of the host).
    std::function<void(const juce::StringArray&, float x, float y)> onFilesDropped;
    // Called once the ImGui context and fonts exist, before the first frame.
    std::function<void()> onInitialised;
    // Panels may leave the window as windows of their own (the studio);
    // a plug-in's window keeps them all. Read when the context is made.
    bool ownWindows { true };
    // In the background (an automated session): never takes the keyboard on
    // its own, which would make its window the active one.
    bool background {};

    // Keeps the display rate for a while (input, a change on screen).
    void wake(int frames = 12);
    // Full rate for as long as it is set (playback, meters moving).
    void setAnimating(bool animating);

    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] float dpiScale() const noexcept;
    [[nodiscard]] juce::String rendererDescription() const;
    // The next rendered frame, written as PNG (probes and snapshots).
    void captureNextFrame(const juce::File& target, std::function<void(bool)> done = {});
    // Renders one frame now, outside the vertical blank pacing.
    void renderNow();
    // An image the interface can draw (ImGui::Image), made once per key and
    // kept; 0 until the graphics device exists.
    [[nodiscard]] unsigned long long texture(const juce::String& key, const juce::Image& image);

    [[nodiscard]] unsigned long long updateTexture(const juce::String& key,const juce::Image& image);
    void releaseTexture(const juce::String& key);

    void resized() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;
    void focusGained(FocusChangeType) override;

    struct Native;

private:
    void createNativeWindow();
    void vblank(double timestampSeconds);
    void frame();

    std::unique_ptr<Native> native_;
    juce::HWNDComponent embed_;
    double lastVBlankMs_ {};
    juce::VBlankAttachment vblank_;
    // Frames when no vertical blank comes (a window on no screen, a host
    // that has none to give): a timer paces them instead.
    struct Fallback final : juce::Timer {
        explicit Fallback(ImGuiHost& owner) : host(owner) {}
        void timerCallback() override;
        ImGuiHost& host;
    };
    Fallback fallback_ { *this };
    int wakeFrames_ { 30 };
    bool animating_ {};
    double lastFrameSeconds_ {};
    juce::File captureTarget_;
    std::function<void(bool)> captureDone_;

    friend struct NativeAccess;
};

// An automated session's window (--hidden): kept off every screen, without a
// taskbar button, never made the active window, so it never comes in front of
// what the person is doing. It renders as any other (snapshots, input sent
// through the control port). Called once the window is on the desktop.
void keepInBackground(juce::Component& window);

} // namespace cydaw::v2
