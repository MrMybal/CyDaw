#pragma once

namespace cydaw::app {

// The renderer owns a separate session and drives its prepared sources from
// this thread. Hardware callbacks never enter this scope.
struct OfflineRender {
    inline static thread_local OfflineRender* current {};
    OfflineRender* previous { current };
    bool pluginFailed {};
    OfflineRender() noexcept { current = this; }
    ~OfflineRender() { current = previous; }
    OfflineRender(const OfflineRender&) = delete;
    OfflineRender& operator=(const OfflineRender&) = delete;
};

} // namespace cydaw::app
