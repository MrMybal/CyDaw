#include <cydaw/base/Image.h>
#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#pragma once

#include <cydaw/base/Geometry.h>

#include <functional>
#include <memory>

// A native child window showing Dear ImGui with Direct3D 11. Everything — input, the
// interface code, the rendering — runs on the message thread, so the
// interface can call the engine directly.
//
// Frames use the selected 30/60/120 FPS ceiling (60 by default) while
// something moves, and drop to 12 FPS at rest, independently of the monitor.

namespace cydaw::v2 {

class ImGuiHost final {
public:
    ImGuiHost();
    ~ImGuiHost();

    // Builds the interface; called between ImGui::NewFrame() and Render().
    std::function<void()> onFrame;
    // Input added after the system's, before ImGui::NewFrame() (the control
    // port's scripted mouse).
    std::function<void()> beforeNewFrame;
    // Files dropped from the system file manager, at a point in interface
    // coordinates (physical pixels of the host).
    std::function<void(const cydaw::base::TextList&, float x, float y)> onFilesDropped;
    // Called once the ImGui context and fonts exist, before the first frame.
    std::function<void()> onInitialised;
    // Panels may leave the window as windows of their own (the studio);
    // a plug-in's window keeps them all. Read when the context is made.
    bool ownWindows { true };
    // In the background (an automated session): never takes the keyboard on
    // its own, which would make its window the active one.
    bool background {};

    // Keeps the selected frame rate for a while (input, a change on screen).
    void wake(int frames = 12);
    // Selected rate for as long as it is set (playback, meters moving).
    void setAnimating(bool animating);
    // A maximum, not a requirement to redraw an idle interface at full rate.
    void setFrameRateLimit(int framesPerSecond) noexcept;
    [[nodiscard]] int frameRateLimit() const noexcept { return static_cast<int>(frameRateHz_); }

    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] float dpiScale() const noexcept;
    [[nodiscard]] cydaw::base::Text rendererDescription() const;
    // The next rendered frame, written as PNG (probes and snapshots).
    void captureNextFrame(const cydaw::base::File& target, std::function<void(bool)> done = {});
    // Renders one frame now, outside normal pacing (captures/scripted input).
    void renderNow();
    // An image the interface can draw (ImGui::Image), made once per key and
    // kept; 0 until the graphics device exists.
    [[nodiscard]] unsigned long long texture(const cydaw::base::Text& key, const cydaw::base::Image& image);

    [[nodiscard]] unsigned long long updateTexture(const cydaw::base::Text& key,const cydaw::base::Image& image);
    void releaseTexture(const cydaw::base::Text& key);

    // The caller owns the parent window. This object owns its native child.
    void create(void* parent, int width, int height);
    void setBounds(cydaw::base::Rectangle<int> bounds);
    [[nodiscard]] void* nativeHandle() const noexcept;
    void poll();

    struct Native;

private:
    void createNativeWindow();
    void vblank(double timestampSeconds);
    void frame();

    std::unique_ptr<Native> native_;
    void* parent_{};
    cydaw::base::Rectangle<int> bounds_{0, 0, 1600, 960};
    double frameRateHz_{60};
    int wakeFrames_ { 30 };
    bool animating_ {};
    double nextFrameSeconds_ {};
    cydaw::base::File captureTarget_;
    std::function<void(bool)> captureDone_;

    friend struct NativeAccess;
};

} // namespace cydaw::v2
