#include <cydaw/engine/RealtimeEngine.h>

#include <algorithm>

namespace cydaw::engine {

AudioCommand AudioCommand::play() noexcept { return { AudioCommandType::play, 0, 0.0, 0 }; }
AudioCommand AudioCommand::record() noexcept { return { AudioCommandType::record, 0, 0.0, 0 }; }
AudioCommand AudioCommand::stop() noexcept { return { AudioCommandType::stop, 0, 0.0, 0 }; }

AudioCommand AudioCommand::seek(const std::int64_t samplePosition) noexcept
{
    return { AudioCommandType::seekSamples, samplePosition, 0.0, 0 };
}

AudioCommand AudioCommand::tempo(const double beatsPerMinute) noexcept
{
    return { AudioCommandType::setTempo, 0, beatsPerMinute, 0 };
}

AudioCommand AudioCommand::timeSignature(const std::uint16_t numerator,
                                         const std::uint16_t denominator) noexcept
{
    return { AudioCommandType::setTimeSignature, numerator, 0.0, denominator };
}

RealtimeEngine::RealtimeEngine(const ProcessSpec spec) noexcept
    : spec_(spec)
{
    transport_.prepare(spec.sampleRate);
}

bool RealtimeEngine::installPlan(std::unique_ptr<AudioRenderPlan> plan) noexcept
{
    if (plan == nullptr || plan->spec() != spec_)
        return false;
    plan_ = std::move(plan);
    return true;
}

bool RealtimeEngine::postCommand(const AudioCommand& command) noexcept
{
    return commands_.tryPush(command);
}

void RealtimeEngine::process(const AudioBlockView input, const AudioBlockView output) noexcept
{
    AudioCommand command;
    while (commands_.tryPop(command))
        applyCommand(command);

    const auto frames = std::min({ input.frameCount, output.frameCount, spec_.maximumBlockSize });
    const AudioBlockView safeInput { input.data, input.channelCount, frames, input.channelStride };
    const AudioBlockView safeOutput { output.data, output.channelCount, frames, output.channelStride };
    const ProcessContext context { transport_.snapshot() };
    if (plan_ != nullptr)
        plan_->process(safeInput, safeOutput, context);
    else
        clearAudio(safeOutput);
    transport_.advance(frames);
}

void RealtimeEngine::reset() noexcept
{
    transport_.stop();
    transport_.seek(0);
    if (plan_ != nullptr)
        plan_->reset();
}

TransportSnapshot RealtimeEngine::transportSnapshot() const noexcept
{
    return transport_.snapshot();
}

const ProcessSpec& RealtimeEngine::spec() const noexcept { return spec_; }

std::size_t RealtimeEngine::queuedCommandCountApprox() const noexcept
{
    return commands_.sizeApprox();
}

void RealtimeEngine::applyCommand(const AudioCommand& command) noexcept
{
    switch (command.type) {
        case AudioCommandType::play: transport_.play(); break;
        case AudioCommandType::record: transport_.record(); break;
        case AudioCommandType::stop: transport_.stop(); break;
        case AudioCommandType::seekSamples: transport_.seek(command.integerValue); break;
        case AudioCommandType::setTempo: (void) transport_.setTempo(command.floatingValue); break;
        case AudioCommandType::setTimeSignature:
            (void) transport_.setTimeSignature(static_cast<std::uint16_t>(command.integerValue), command.smallValue);
            break;
    }
}

} // namespace cydaw::engine

