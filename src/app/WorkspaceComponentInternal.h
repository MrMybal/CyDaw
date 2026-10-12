#pragma once

#include "WorkspaceComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

// Helpers shared by WorkspaceComponent.cpp and the workspace self-tests,
// which live in their own translation unit.
namespace cydaw::app::detail {

[[nodiscard]] inline bool isNodeGraphState(const juce::var& state)
{
    return state.getDynamicObject() != nullptr || state.getArray() != nullptr;
}

// Windows can hold a just-closed file open for a moment; a directory that
// is being deleted right after use needs a few tries.
[[nodiscard]] inline bool deleteRecursivelyWithRetry(const juce::File& directory)
{
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (!directory.exists())
            return true;
        if (directory.deleteRecursively())
            return true;
        juce::Thread::sleep(25);
    }
    return !directory.exists();
}

} // namespace cydaw::app::detail

namespace cydaw::app {

// A detached panel's window. The docking self-test checks these windows,
// so the class is visible to both translation units.
class WorkspaceComponent::FloatingPanelWindow final : public juce::DocumentWindow {
public:
    FloatingPanelWindow(juce::String title,
                        std::function<void()> closeHandler,
                        std::function<void()> resizeHandler)
        : DocumentWindow(std::move(title), juce::Colour(0xff111318), closeButton),
          closeHandler_(std::move(closeHandler)),
          resizeHandler_(std::move(resizeHandler))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
    }

    void resized() override
    {
        juce::DocumentWindow::resized();
        if (resizeHandler_)
            resizeHandler_();
    }

    void closeButtonPressed() override
    {
        if (closeHandler_)
            closeHandler_();
    }

private:
    std::function<void()> closeHandler_;
    std::function<void()> resizeHandler_;
};

} // namespace cydaw::app
