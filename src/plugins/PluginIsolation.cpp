#include <cydaw/plugins/PluginIsolation.h>

namespace cydaw::plugins {

PluginWorkerState::PluginWorkerState(const CrashPolicy policy)
    : policy_(policy)
{
}

void PluginWorkerState::beginStart() noexcept
{
    if (state_ == WorkerState::stopped)
        state_ = WorkerState::starting;
}

void PluginWorkerState::markRunning() noexcept
{
    if (state_ == WorkerState::starting || state_ == WorkerState::restarting)
        state_ = WorkerState::running;
}

void PluginWorkerState::markUnresponsive() noexcept
{
    if (state_ == WorkerState::running)
        state_ = WorkerState::unresponsive;
}

void PluginWorkerState::markCrashed() noexcept
{
    if (state_ != WorkerState::quarantined && state_ != WorkerState::stopped) {
        ++consecutiveCrashes_;
        state_ = WorkerState::crashed;
    }
}

bool PluginWorkerState::beginAutomaticRestart() noexcept
{
    if (state_ != WorkerState::crashed)
        return false;

    if (consecutiveCrashes_ > policy_.automaticRestartLimit) {
        state_ = WorkerState::quarantined;
        return false;
    }

    state_ = WorkerState::restarting;
    return true;
}

void PluginWorkerState::stop() noexcept
{
    state_ = WorkerState::stopped;
    consecutiveCrashes_ = 0;
}

WorkerState PluginWorkerState::state() const noexcept
{
    return state_;
}

std::uint32_t PluginWorkerState::consecutiveCrashes() const noexcept
{
    return consecutiveCrashes_;
}

} // namespace cydaw::plugins

