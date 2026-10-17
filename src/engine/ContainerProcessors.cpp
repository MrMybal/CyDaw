#include <cydaw/engine/ContainerProcessors.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cydaw::engine {

namespace {

float decibels(const float db) noexcept
{
    return db <= -59.5f ? 0.0f : std::pow(10.0f, db / 20.0f);
}

// One pole toward a level: its coefficient for a time constant.
float follow(const double seconds, const double sampleRate) noexcept
{
    return static_cast<float>(1.0 - std::exp(-1.0 / std::max(1.0, seconds * sampleRate)));
}

} // namespace

// ---- Head ----------------------------------------------------------------------------------------

ContainerHeadProcessor::ContainerHeadProcessor(const EffectDescriptor& descriptor) noexcept : EffectProcessor(descriptor) {}

std::size_t ContainerHeadProcessor::indexOf(const std::string_view key) const noexcept
{
    const auto& parameters = descriptor().parameters;
    for (std::size_t index = 0; index < parameters.size(); ++index)
        if (parameters[index].key == key)
            return index;
    return static_cast<std::size_t>(-1);
}

float ContainerHeadProcessor::setting(const std::string_view key, const float fallback) const noexcept
{
    const auto index = indexOf(key);
    return index < descriptor().parameters.size() ? value(index) : fallback;
}

float ContainerHeadProcessor::branchSetting(const std::string_view prefix, const int branch, const float fallback) const noexcept
{
    // "volume" and 0: "volume1".
    std::array<char, 24> key {};
    const auto length = std::min<std::size_t>(prefix.size(), key.size() - 3);
    std::copy_n(prefix.data(), length, key.data());
    key[length] = static_cast<char>('1' + std::clamp(branch, 0, 8));
    return setting(std::string_view(key.data(), length + 1), fallback);
}

int ContainerHeadProcessor::branchCount() const noexcept
{
    if (container::variableBranches(kind()))
        return std::clamp(static_cast<int>(std::lround(setting("chains", 2.0f))), 1, container::maximumBranches);
    return container::fixedBranches(kind());
}

void ContainerHeadProcessor::Crossover::design(const float hz, const double sampleRate) noexcept
{
    if (hz == frequency)
        return;
    frequency = hz;
    // Butterworth, Q 1/sqrt(2), twice each side: Linkwitz-Riley.
    const auto w = 2.0 * std::numbers::pi * std::clamp(static_cast<double>(hz), 10.0, sampleRate * 0.45) / sampleRate;
    const auto cosine = std::cos(w);
    const auto alpha = std::sin(w) / (2.0 * std::numbers::sqrt2 * 0.5);
    const auto a0 = 1.0 + alpha;
    const auto setLow = [&](Biquad& biquad) {
        biquad.b0 = static_cast<float>((1.0 - cosine) * 0.5 / a0);
        biquad.b1 = static_cast<float>((1.0 - cosine) / a0);
        biquad.b2 = biquad.b0;
        biquad.a1 = static_cast<float>(-2.0 * cosine / a0);
        biquad.a2 = static_cast<float>((1.0 - alpha) / a0);
    };
    const auto setHigh = [&](Biquad& biquad) {
        biquad.b0 = static_cast<float>((1.0 + cosine) * 0.5 / a0);
        biquad.b1 = static_cast<float>(-(1.0 + cosine) / a0);
        biquad.b2 = biquad.b0;
        biquad.a1 = static_cast<float>(-2.0 * cosine / a0);
        biquad.a2 = static_cast<float>((1.0 - alpha) / a0);
    };
    for (auto& stage : low)
        setLow(stage);
    for (auto& stage : high)
        setHigh(stage);
}

void ContainerHeadProcessor::Crossover::clear() noexcept
{
    for (auto& stage : low)
        stage.clear();
    for (auto& stage : high)
        stage.clear();
}

void ContainerHeadProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    ringSize_ = std::max<std::uint32_t>(8'192, spec.maximumBlockSize * 4);
    // Three parts of two channels.
    ring_.prepare(6, ringSize_);
    for (auto& crossover : crossovers_)
        crossover.frequency = -1.0f;
    align_.frequency = -1.0f;
    reset();
}

void ContainerHeadProcessor::reset() noexcept
{
    ring_.clear(ringSize_);
    written_ = 0;
    for (auto& crossover : crossovers_)
        crossover.clear();
    align_.clear();
    fast_ = 0.0f;
    slow_ = 0.0f;
    sinceTrigger_ = 1.0e9;
    voiceTime_ = 1.0e9;
    voicePhase_ = 0.0;
    voiceLevel_ = 0.0f;
}

std::size_t ContainerHeadProcessor::readings(float* into, const std::size_t count) const noexcept
{
    if (into == nullptr || count == 0 || kind() != NativeDspNodeKind::replacer)
        return 0;
    into[0] = static_cast<float>(triggers_.load(std::memory_order_relaxed));
    if (count > 1)
        into[1] = lastStrength_.load(std::memory_order_relaxed);
    return std::min<std::size_t>(count, 2);
}

void ContainerHeadProcessor::process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept
{
    copyAudio(output, input);
    const auto at = static_cast<std::uint32_t>(written_ % ringSize_);
    switch (kind()) {
    case NativeDspNodeKind::midSideSplit:
    case NativeDspNodeKind::stereoSplit:
    case NativeDspNodeKind::multiband2:
    case NativeDspNodeKind::multiband3:
        split(input, at);
        break;
    case NativeDspNodeKind::replacer:
        replace(input, at);
        break;
    default:
        break;
    }
    written_ += input.frameCount;
}

void ContainerHeadProcessor::split(const AudioBlockView input, const std::uint32_t at) noexcept
{
    const auto frames = input.frameCount;
    const auto channels = input.channelCount;
    const auto* left = channels > 0 ? input.channel(0) : nullptr;
    const auto* right = channels > 1 ? input.channel(1) : left;
    if (left == nullptr)
        return;
    const auto ring = ring_.view(ringSize_);
    const auto kind = this->kind();
    if (kind == NativeDspNodeKind::multiband2) {
        crossovers_[0].design(setting("crossover", 400.0f), sampleRate_);
    } else if (kind == NativeDspNodeKind::multiband3) {
        const auto low = setting("crossover-low", 250.0f);
        const auto high = std::max(low * 1.05f, setting("crossover-high", 2'500.0f));
        crossovers_[0].design(low, sampleRate_);
        crossovers_[1].design(high, sampleRate_);
        align_.design(high, sampleRate_);
    }
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto index = (at + frame) % ringSize_;
        const float sides[2] { left[frame], right[frame] };
        const auto write = [&](const int part, const std::size_t channel, const float value) {
            ring.channel(static_cast<std::uint32_t>(part * 2) + static_cast<std::uint32_t>(channel))[index] = value;
        };
        switch (kind) {
        case NativeDspNodeKind::midSideSplit: {
            const auto mid = (sides[0] + sides[1]) * 0.5f;
            const auto side = (sides[0] - sides[1]) * 0.5f;
            write(0, 0, mid);
            write(0, 1, mid);
            write(1, 0, side);
            write(1, 1, side);
            break;
        }
        case NativeDspNodeKind::stereoSplit:
            write(0, 0, sides[0]);
            write(0, 1, sides[0]);
            write(1, 0, sides[1]);
            write(1, 1, sides[1]);
            break;
        case NativeDspNodeKind::multiband2:
            for (std::size_t channel = 0; channel < 2; ++channel) {
                auto& crossover = crossovers_[0];
                const auto x = sides[channel];
                write(0, channel, crossover.low[1].run(channel, crossover.low[0].run(channel, x)));
                write(1, channel, crossover.high[1].run(channel, crossover.high[0].run(channel, x)));
            }
            break;
        case NativeDspNodeKind::multiband3:
            for (std::size_t channel = 0; channel < 2; ++channel) {
                auto& first = crossovers_[0];
                auto& second = crossovers_[1];
                const auto x = sides[channel];
                const auto lowBand = first.low[1].run(channel, first.low[0].run(channel, x));
                const auto rest = first.high[1].run(channel, first.high[0].run(channel, x));
                // The low band through the second split, summed: an allpass
                // there, as the other two bands have.
                const auto aligned = align_.low[1].run(channel, align_.low[0].run(channel, lowBand))
                                     + align_.high[1].run(channel, align_.high[0].run(channel, lowBand));
                write(0, channel, aligned);
                write(1, channel, second.low[1].run(channel, second.low[0].run(channel, rest)));
                write(2, channel, second.high[1].run(channel, second.high[0].run(channel, rest)));
            }
            break;
        default:
            break;
        }
    }
}

void ContainerHeadProcessor::replace(const AudioBlockView input, const std::uint32_t at) noexcept
{
    const auto frames = input.frameCount;
    const auto* left = input.channelCount > 0 ? input.channel(0) : nullptr;
    const auto* right = input.channelCount > 1 ? input.channel(1) : left;
    const auto ring = ring_.view(ringSize_);
    const auto threshold = setting("threshold", -24.0f);
    const auto sensitivity = setting("sensitivity", 6.0f);
    const auto hold = setting("hold", 60.0f) * 0.001;
    const auto generator = std::clamp(static_cast<int>(std::lround(setting("generator", 0.0f))), 0, container::generatorCount - 1);
    const auto pitch = static_cast<double>(std::max(20.0f, setting("pitch", 55.0f)));
    const auto decay = std::max(0.001, static_cast<double>(setting("decay", 250.0f)) * 0.001);
    const auto velocity = std::clamp(setting("velocity", 0.5f), 0.0f, 1.0f);
    const auto fastUp = follow(0.0003, sampleRate_);
    const auto fastDown = follow(0.008, sampleRate_);
    const auto slowUp = follow(0.015, sampleRate_);
    const auto slowDown = follow(0.1, sampleRate_);
    const auto step = 1.0 / sampleRate_;
    constexpr auto twoPi = 2.0 * std::numbers::pi;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto level = left == nullptr ? 0.0f : std::max(std::abs(left[frame]), std::abs(right[frame]));
        fast_ += (level - fast_) * (level > fast_ ? fastUp : fastDown);
        slow_ += (level - slow_) * (level > slow_ ? slowUp : slowDown);
        sinceTrigger_ += step;
        const auto fastDb = 20.0f * std::log10(fast_ + 1.0e-9f);
        const auto slowDb = 20.0f * std::log10(slow_ + 1.0e-9f);
        if (fastDb > threshold && fastDb - slowDb > sensitivity && sinceTrigger_ > hold) {
            // A transient: the sound starts again, as loud as it struck.
            const auto strength = std::clamp((fastDb - threshold) / 24.0f, 0.0f, 1.0f);
            voiceLevel_ = 1.0f - velocity + velocity * strength;
            voiceTime_ = 0.0;
            voicePhase_ = 0.0;
            sinceTrigger_ = 0.0;
            triggers_.fetch_add(1, std::memory_order_relaxed);
            lastStrength_.store(strength, std::memory_order_relaxed);
        }
        auto sound = 0.0f;
        if (voiceTime_ < decay * 8.0) {
            const auto envelope = static_cast<float>(std::exp(-voiceTime_ / decay)) * voiceLevel_;
            switch (generator) {
            case 0: { // a kick: its pitch falls from four times to itself
                const auto hz = pitch * (1.0 + 3.0 * std::exp(-voiceTime_ / 0.03));
                voicePhase_ += hz * step;
                sound = static_cast<float>(std::sin(twoPi * voicePhase_)) * envelope;
                break;
            }
            case 1: // a tone
                voicePhase_ += pitch * step;
                sound = static_cast<float>(std::sin(twoPi * voicePhase_)) * envelope;
                break;
            case 2: { // noise
                noise_ ^= noise_ << 13;
                noise_ ^= noise_ >> 17;
                noise_ ^= noise_ << 5;
                sound = (static_cast<float>(noise_) / 2'147'483'648.0f - 1.0f) * envelope;
                break;
            }
            default: { // a click: noise of a few milliseconds
                noise_ ^= noise_ << 13;
                noise_ ^= noise_ >> 17;
                noise_ ^= noise_ << 5;
                sound = (static_cast<float>(noise_) / 2'147'483'648.0f - 1.0f) * voiceLevel_
                        * static_cast<float>(std::exp(-voiceTime_ / 0.0025));
                break;
            }
            }
            voiceTime_ += step;
        }
        const auto index = (at + frame) % ringSize_;
        ring.channel(0)[index] = sound;
        ring.channel(1)[index] = sound;
    }
}

void ContainerHeadProcessor::readPart(const int branch, std::uint64_t& cursor, const AudioBlockView into) const noexcept
{
    if (branch < 0 || branch >= 3 || ringSize_ == 0) {
        clearAudio(into);
        return;
    }
    auto ring = const_cast<AudioBuffer&>(ring_).view(ringSize_);
    for (std::uint32_t channel = 0; channel < into.channelCount; ++channel) {
        const auto* from = ring.channel(static_cast<std::uint32_t>(branch * 2) + std::min<std::uint32_t>(channel, 1));
        auto* to = into.channel(channel);
        for (std::uint32_t frame = 0; frame < into.frameCount; ++frame)
            to[frame] = from[(cursor + frame) % ringSize_];
    }
    cursor += into.frameCount;
}

std::unique_ptr<EffectProcessor> makeContainerHead(const EffectDescriptor& descriptor)
{
    if (!container::isHead(descriptor.kind))
        return {};
    return std::make_unique<ContainerHeadProcessor>(descriptor);
}

// ---- Tap -----------------------------------------------------------------------------------------

void ContainerTapProcessor::process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept
{
    if (head_ == nullptr || container::partOf(head_->kind(), branch_) == container::Part::whole) {
        copyAudio(output, input);
        cursor_ += input.frameCount;
        return;
    }
    head_->readPart(branch_, cursor_, output);
}

// ---- End -----------------------------------------------------------------------------------------

void ContainerEndProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    reset();
}

void ContainerEndProcessor::reset() noexcept
{
    current_ = -1;
    previous_ = -1;
    fade_ = 1.0f;
    gains_.fill(-1.0f);
    pans_.fill(0.0f);
    mix_ = -1.0f;
}

void ContainerEndProcessor::process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept
{
    copyAudio(output, input);
}

void ContainerEndProcessor::processAudioBuses(const std::span<const AudioInputBusView> inputs, const AudioBlockView output,
                                              const ProcessContext&) noexcept
{
    clearAudio(output);
    const AudioBlockView* dry = nullptr;
    const AudioBlockView* post = nullptr;
    std::array<const AudioBlockView*, container::maximumBranches> branches {};
    for (const auto& bus : inputs) {
        if (bus.portId == container::endDryPort)
            dry = &bus.audio;
        else if (bus.portId == container::postPort)
            post = &bus.audio;
        else if (const auto branch = container::branchOfPort(bus.portId); branch >= 0)
            branches[static_cast<std::size_t>(branch)] = &bus.audio;
    }
    if (head_ == nullptr) {
        if (dry != nullptr)
            copyAudio(output, *dry);
        return;
    }
    const auto frames = output.frameCount;
    const auto channels = output.channelCount;
    if (frames == 0 || channels == 0)
        return;
    // The last of two ends: the post chain's sound, mixed with the dry.
    if (post != nullptr) {
        copyAudio(output, *post);
        mixDry(dry, output);
        return;
    }
    const auto kind = head_->kind();
    const auto count = std::min(head_->branchCount(), container::maximumBranches);
    const auto sample = [](const AudioBlockView* view, const std::uint32_t channel, const std::uint32_t frame) {
        if (view == nullptr || view->channelCount == 0)
            return 0.0f;
        return view->channel(std::min(channel, view->channelCount - 1))[frame];
    };
    const auto mono = [&sample](const AudioBlockView* view, const std::uint32_t frame) {
        if (view == nullptr || view->channelCount == 0)
            return 0.0f;
        return view->channelCount > 1 ? (sample(view, 0, frame) + sample(view, 1, frame)) * 0.5f : sample(view, 0, frame);
    };
    // A value from where it was heard to where it is, across the block.
    const auto ramp = [frames](float& heard, const float target) {
        const auto from = heard < -0.5f ? target : heard;
        heard = target;
        return std::pair { from, (target - from) / static_cast<float>(frames) };
    };

    // Each branch's gain (layers and bands: volume, mute, solo; the others 1).
    std::array<float, container::maximumBranches> gains {};
    std::array<float, container::maximumBranches> pans {};
    const auto layered = kind == NativeDspNodeKind::fxLayer || kind == NativeDspNodeKind::multiband2
                         || kind == NativeDspNodeKind::multiband3 || kind == NativeDspNodeKind::fxSelector;
    auto soloed = false;
    for (int branch = 0; branch < count; ++branch)
        soloed = soloed || (layered && head_->branchSetting("solo", branch) >= 0.5f);
    for (int branch = 0; branch < count; ++branch) {
        auto gain = 1.0f;
        if (layered) {
            const auto muted = head_->branchSetting("mute", branch) >= 0.5f;
            const auto solo = head_->branchSetting("solo", branch) >= 0.5f;
            gain = (soloed ? solo : !muted) ? decibels(head_->branchSetting("volume", branch)) : 0.0f;
        }
        gains[static_cast<std::size_t>(branch)] = gain;
        pans[static_cast<std::size_t>(branch)] = kind == NativeDspNodeKind::fxLayer ? std::clamp(head_->branchSetting("pan", branch), -1.0f, 1.0f) : 0.0f;
    }
    // The corners' weights.
    if (kind == NativeDspNodeKind::xyFx) {
        const auto x = std::clamp(head_->setting("x", 0.5f), 0.0f, 1.0f);
        const auto y = std::clamp(head_->setting("y", 0.5f), 0.0f, 1.0f);
        // A at the top left, B top right, C bottom left, D bottom right.
        gains = {};
        gains[0] = (1.0f - x) * y;
        gains[1] = x * y;
        gains[2] = (1.0f - x) * (1.0f - y);
        gains[3] = x * (1.0f - y);
    }
    // Each branch's gain from where it was heard to where it is now.
    std::array<std::pair<float, float>, container::maximumBranches> ramps {};
    for (int branch = 0; branch < count; ++branch)
        ramps[static_cast<std::size_t>(branch)] = ramp(gains_[static_cast<std::size_t>(branch)], gains[static_cast<std::size_t>(branch)]);
    // The selector: the chain chosen, faded in over the other.
    if (kind == NativeDspNodeKind::fxSelector) {
        const auto chosen = std::clamp(static_cast<int>(std::lround(head_->setting("selected", 0.0f))), 0, count - 1);
        if (current_ < 0) {
            current_ = chosen;
            fade_ = 1.0f;
        } else if (chosen != current_) {
            previous_ = current_;
            current_ = chosen;
            fade_ = 0.0f;
        }
    }

    const auto fadeStep = static_cast<float>(
        1.0 / std::max(1.0, static_cast<double>(head_->setting("fade", 20.0f)) * 0.001 * sampleRate_));
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        auto* out = output.channel(channel);
        const auto side = channel == 0 ? -1.0f : channel == 1 ? 1.0f : 0.0f;
        switch (kind) {
        case NativeDspNodeKind::midSideSplit: {
            const auto midGain = decibels(head_->setting("mid-gain", 0.0f));
            const auto sideGain = decibels(head_->setting("side-gain", 0.0f));
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                const auto mid = mono(branches[0], frame) * midGain;
                const auto sideValue = mono(branches[1], frame) * sideGain;
                out[frame] = channel == 0 ? mid + sideValue : channel == 1 ? mid - sideValue : mid;
            }
            break;
        }
        case NativeDspNodeKind::stereoSplit: {
            const auto leftGain = decibels(head_->setting("left-gain", 0.0f));
            const auto rightGain = decibels(head_->setting("right-gain", 0.0f));
            for (std::uint32_t frame = 0; frame < frames; ++frame)
                out[frame] = channel == 0 ? mono(branches[0], frame) * leftGain
                             : channel == 1 ? mono(branches[1], frame) * rightGain
                                            : (mono(branches[0], frame) + mono(branches[1], frame)) * 0.5f;
            break;
        }
        case NativeDspNodeKind::fxSelector: {
            auto fade = fade_;
            const auto step = fadeStep;
            const auto currentGain = gains[static_cast<std::size_t>(current_)];
            const auto previousGain = previous_ >= 0 && previous_ < count ? gains[static_cast<std::size_t>(previous_)] : 0.0f;
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                fade = std::min(1.0f, fade + step);
                out[frame] = sample(branches[static_cast<std::size_t>(current_)], channel, frame) * currentGain * fade
                             + (previous_ >= 0 ? sample(branches[static_cast<std::size_t>(previous_)], channel, frame) * previousGain
                                                     * (1.0f - fade)
                                               : 0.0f);
            }
            break;
        }
        default:
            // The branches summed, each by its gain (and a layer's pan).
            for (int branch = 0; branch < count; ++branch) {
                const auto index = static_cast<std::size_t>(branch);
                const auto pan = pans[index];
                const auto panGain = side == 0.0f ? 1.0f : std::min(1.0f, 1.0f + side * pan);
                auto [gain, gainStep] = ramps[index];
                for (std::uint32_t frame = 0; frame < frames; ++frame) {
                    out[frame] += sample(branches[index], channel, frame) * gain * panGain;
                    gain += gainStep;
                }
            }
            break;
        }
    }
    // The fade moves once a block, whatever the channels.
    if (kind == NativeDspNodeKind::fxSelector) {
        fade_ = std::min(1.0f, fade_ + fadeStep * static_cast<float>(frames));
        if (fade_ >= 1.0f)
            previous_ = -1;
    }
    // The first of two ends gives the branches' mix as it is: a post chain
    // follows, the last end mixes.
    if (!blendOnly_)
        mixDry(dry, output);
}

void ContainerEndProcessor::mixDry(const AudioBlockView* dry, const AudioBlockView output) noexcept
{
    // Wet gain, then the dry sound under it by Mix, gliding across the block.
    const auto frames = output.frameCount;
    const auto target = std::clamp(head_->setting("mix", 1.0f), 0.0f, 1.0f);
    const auto mixFrom = mix_ < -0.5f ? target : mix_;
    const auto mixStep = (target - mixFrom) / static_cast<float>(std::max<std::uint32_t>(1, frames));
    mix_ = target;
    const auto wetGain = decibels(head_->setting("wet-gain", 0.0f) <= -36.0f ? -60.0f : head_->setting("wet-gain", 0.0f));
    for (std::uint32_t channel = 0; channel < output.channelCount; ++channel) {
        auto* out = output.channel(channel);
        const auto* from = dry != nullptr && dry->channelCount > 0 ? dry->channel(std::min(channel, dry->channelCount - 1)) : nullptr;
        auto mix = mixFrom;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            out[frame] = out[frame] * wetGain * mix + (from != nullptr ? from[frame] : 0.0f) * (1.0f - mix);
            mix += mixStep;
        }
    }
}

} // namespace cydaw::engine
