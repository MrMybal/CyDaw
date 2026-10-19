#include "MidiEffectsHost.h"
#include "AudioTrackController.h"
#include "PlanSynthesiser.h"
#include "Localization.h"

#include <cydaw/engine/TimeStretch.h>
#include <cydaw/engine/PolyVoice.h>
#include <cydaw/engine/BuiltinProcessors.h>
#include <cydaw/engine/ChannelLayout.h>
#include <cydaw/engine/NativeDspCompiler.h>

#include <algorithm>
#include <bitset>
#include <cmath>
#include <limits>
#include <thread>
#include <unordered_set>

namespace cydaw::app {

namespace {
constexpr double transportHorizonSeconds = 24.0 * 60.0 * 60.0;

// Each lane's points as the shared curve reads them (engine/AutomationCurve.h).
cydaw::engine::CurveKnot knotOf(const GainAutomationPoint& point) noexcept
{
    return { point.timeSeconds, point.valueDb, point.curve, point.curveAmount, point.handles };
}

cydaw::engine::CurveKnot knotOf(const PluginParameterAutomationPoint& point) noexcept
{
    return { point.timeSeconds, point.normalisedValue, point.curve, point.curveAmount, point.handles };
}

cydaw::engine::CurveKnot knotOf(const AudioTrackController::AutomationPoint& point) noexcept
{
    return { point.timeSeconds, point.valueDb, static_cast<int>(point.curve), point.curveAmount, point.handles };
}

template <typename Points>
double laneValueAt(const Points& points, const double time, std::size_t* hint = nullptr)
{
    return cydaw::engine::curveValueAt(
        points, [](const auto& point) { return knotOf(point); }, time, false, hint);
}

bool sameAutomation(
    const std::vector<AudioTrackController::AutomationPoint>& first,
    const std::vector<AudioTrackController::AutomationPoint>& second)
{
    if (first.size() != second.size())
        return false;
    constexpr double tolerance = 0.0000001;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto& left = first[index];
        const auto& right = second[index];
        if (left.id != right.id
            || std::abs(left.timeSeconds - right.timeSeconds) > tolerance
            || std::abs(left.valueDb - right.valueDb) > 0.000001f
            || left.curve != right.curve
            || std::abs(left.curveAmount - right.curveAmount) > 0.000001f
            || !(left.handles == right.handles))
            return false;
    }
    return true;
}

float automationValueAt(
    const std::vector<AudioTrackController::AutomationPoint>& points,
    const double timeSeconds) noexcept
{
    if (points.empty())
        return 0.0f;
    return static_cast<float>(laneValueAt(points, timeSeconds));
}

void shiftAutomation(
    std::vector<AudioTrackController::AutomationPoint>& points,
    const double deltaSeconds)
{
    for (auto& point : points)
        point.timeSeconds += deltaSeconds;
}

void shiftLanes(std::map<juce::String, std::vector<AudioTrackController::AutomationPoint>>& lanes, const double deltaSeconds)
{
    for (auto& [name, points] : lanes)
        shiftAutomation(points, deltaSeconds);
}

bool sameAudioTakes(
    const std::vector<AudioTrackController::AudioTake>& first,
    const std::vector<AudioTrackController::AudioTake>& second)
{
    if (first.size() != second.size())
        return false;
    constexpr double tolerance = 0.0000001;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto& left = first[index];
        const auto& right = second[index];
        if (left.name != right.name || left.sourceFile != right.sourceFile
            || std::abs(left.sourceLengthSeconds
                        - right.sourceLengthSeconds) > tolerance
            || std::abs(left.shiftSeconds - right.shiftSeconds) > tolerance)
            return false;
    }
    return true;
}

bool sameStretchMarkers(
    const std::vector<AudioTrackController::StretchMarker>& first,
    const std::vector<AudioTrackController::StretchMarker>& second)
{
    if (first.size() != second.size())
        return false;
    constexpr double tolerance = 0.0000001;
    for (std::size_t index = 0; index < first.size(); ++index)
        if (first[index].id != second[index].id
            || std::abs(first[index].sourceSeconds
                        - second[index].sourceSeconds) > tolerance
            || std::abs(first[index].timelineSeconds
                        - second[index].timelineSeconds) > tolerance)
            return false;
    return true;
}

bool sameLanes(const std::map<juce::String, std::vector<AudioTrackController::AutomationPoint>>& first,
               const std::map<juce::String, std::vector<AudioTrackController::AutomationPoint>>& second)
{
    return first.size() == second.size()
           && std::equal(first.begin(), first.end(), second.begin(),
                         [](const auto& a, const auto& b) { return a.first == b.first && sameAutomation(a.second, b.second); });
}

bool sameAudioEvents(
    const std::vector<AudioTrackController::AudioEvent>& first,
    const std::vector<AudioTrackController::AudioEvent>& second)
{
    if (first.size() != second.size())
        return false;
    constexpr double tolerance = 0.0000001;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto& left = first[index];
        const auto& right = second[index];
        if (left.id != right.id || left.name != right.name
            || left.colour != right.colour
            || left.sourceFile != right.sourceFile
            || std::abs(left.startSeconds - right.startSeconds) > tolerance
            || std::abs(left.sourceOffsetSeconds
                        - right.sourceOffsetSeconds) > tolerance
            || std::abs(left.durationSeconds - right.durationSeconds)
                   > tolerance
            || std::abs(left.sourceDurationSeconds
                        - right.sourceDurationSeconds) > tolerance
            || std::abs(left.fadeInSeconds - right.fadeInSeconds) > tolerance
            || std::abs(left.fadeOutSeconds - right.fadeOutSeconds) > tolerance
            || std::abs(left.sourceTempoBpm - right.sourceTempoBpm) > tolerance
            || std::abs(left.gainDb - right.gainDb) > 0.000001f
            || left.stretchMode != right.stretchMode
            || std::abs(left.pitchSemitones - right.pitchSemitones) > tolerance
            || !sameLanes(left.lanes, right.lanes)
            || !sameStretchMarkers(left.stretchMarkers,
                                   right.stretchMarkers))
            return false;
    }
    return true;
}

void normaliseAudioClipTimeline(AudioTrackController::AudioClip& clip)
{
    clip.durationSeconds = juce::jlimit(
        0.001, 86'400.0, clip.durationSeconds);
    const auto end = clip.playEndSeconds > clip.playStartSeconds + 0.000001
                         ? clip.playEndSeconds
                         : clip.durationSeconds;
    clip.playStartSeconds = juce::jlimit(
        0.0, std::max(0.0, clip.durationSeconds - 0.001),
        clip.playStartSeconds);
    clip.playEndSeconds = juce::jlimit(
        clip.playStartSeconds + 0.001, clip.durationSeconds, end);
    clip.loopStartSeconds = juce::jlimit(
        clip.playStartSeconds,
        std::max(clip.playStartSeconds,
                 clip.playEndSeconds - 0.001),
        clip.loopStartSeconds);
    const auto loopEnd = clip.loopEndSeconds
                                 > clip.loopStartSeconds + 0.000001
                             ? clip.loopEndSeconds
                             : clip.playEndSeconds;
    clip.loopEndSeconds = juce::jlimit(
        clip.loopStartSeconds + 0.001, clip.playEndSeconds, loopEnd);
    for (auto& point : clip.automation)
        point.timeSeconds = juce::jlimit(
            0.0, clip.durationSeconds, point.timeSeconds);
    std::sort(clip.automation.begin(), clip.automation.end(),
              [](const auto& left, const auto& right) {
                  return left.timeSeconds < right.timeSeconds;
              });
}

void materialiseLegacyAudioEvent(AudioTrackController::AudioClip& clip)
{
    if (clip.usesEventTimeline)
        return;
    if (clip.sourceFile != juce::File {})
        clip.events.push_back({
            clip.id, clip.name, 0.0, clip.sourceOffsetSeconds,
            clip.durationSeconds, clip.sourceDurationSeconds,
            clip.colour, clip.sourceFile, clip.sourceLengthSeconds,
            clip.fadeInSeconds, clip.fadeOutSeconds,
            clip.sourceTempoBpm, clip.stretchMode,
            clip.stretchMarkers });
    clip.usesEventTimeline = true;
}

void normaliseLauncherClipTimeline(
    AudioTrackController::LauncherClip& clip)
{
    if (clip.usesEventTimeline) {
        if (clip.events.empty()) {
            clip.sourceFile = {};
            clip.sourceOffsetSeconds = 0.0;
        } else {
            clip.sourceFile = clip.events.front().sourceFile;
            clip.sourceOffsetSeconds
                = clip.events.front().sourceOffsetSeconds;
        }
    }
    clip.durationSeconds = juce::jlimit(
        0.001, 86'400.0, clip.durationSeconds);
    clip.playStartSeconds = juce::jlimit(
        0.0, std::max(0.0, clip.durationSeconds - 0.001),
        clip.playStartSeconds);
    const auto playEnd = clip.playEndSeconds
                                 > clip.playStartSeconds + 0.000001
                             ? clip.playEndSeconds
                             : clip.durationSeconds;
    clip.playEndSeconds = juce::jlimit(
        clip.playStartSeconds + 0.001, clip.durationSeconds, playEnd);
    clip.loopStartSeconds = juce::jlimit(
        clip.playStartSeconds,
        std::max(clip.playStartSeconds,
                 clip.playEndSeconds - 0.001),
        clip.loopStartSeconds);
    const auto loopEnd = clip.loopEndSeconds
                                 > clip.loopStartSeconds + 0.000001
                             ? clip.loopEndSeconds
                             : clip.playEndSeconds;
    clip.loopEndSeconds = juce::jlimit(
        clip.loopStartSeconds + 0.001, clip.playEndSeconds, loopEnd);
    for (auto& point : clip.automation)
        point.timeSeconds = juce::jlimit(
            0.0, clip.durationSeconds, point.timeSeconds);
    std::sort(clip.automation.begin(), clip.automation.end(),
              [](const auto& left, const auto& right) {
                  return left.timeSeconds < right.timeSeconds;
              });
}

void materialiseLegacyLauncherEvent(
    AudioTrackController::LauncherClip& clip)
{
    if (clip.usesEventTimeline)
        return;
    if (clip.sourceFile != juce::File {})
        clip.events.push_back({
            0, clip.name, 0.0, clip.sourceOffsetSeconds,
            clip.durationSeconds, clip.durationSeconds, clip.colour,
            clip.sourceFile, 0.0 });
    clip.usesEventTimeline = true;
}

bool sameArrangement(
    const std::vector<AudioTrackController::AudioClip>& first,
    const std::vector<AudioTrackController::AudioClip>& second)
{
    if (first.size() != second.size())
        return false;
    constexpr double tolerance = 0.0000001;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto& left = first[index];
        const auto& right = second[index];
        if (left.id != right.id || left.name != right.name
            || left.colour != right.colour
            || left.sourceFile != right.sourceFile
            || std::abs(left.startSeconds - right.startSeconds) > tolerance
            || std::abs(left.sourceOffsetSeconds
                        - right.sourceOffsetSeconds) > tolerance
            || std::abs(left.durationSeconds - right.durationSeconds)
                   > tolerance
            || std::abs(left.sourceDurationSeconds
                        - right.sourceDurationSeconds) > tolerance
            || std::abs(left.sourceTempoBpm - right.sourceTempoBpm) > tolerance
            || left.stretchMode != right.stretchMode
            || !sameStretchMarkers(left.stretchMarkers,
                                   right.stretchMarkers)
            || !sameAudioEvents(left.events, right.events)
            || !sameAutomation(left.automation, right.automation)
            || left.lanes.size() != right.lanes.size()
            || !std::equal(left.lanes.begin(), left.lanes.end(), right.lanes.begin(),
                           [](const auto& a, const auto& b) { return a.first == b.first && sameAutomation(a.second, b.second); })
            || std::abs(left.playStartSeconds
                        - right.playStartSeconds) > tolerance
            || std::abs(left.playEndSeconds
                        - right.playEndSeconds) > tolerance
            || std::abs(left.loopStartSeconds
                        - right.loopStartSeconds) > tolerance
            || std::abs(left.loopEndSeconds
                        - right.loopEndSeconds) > tolerance
            || left.looping != right.looping
            || left.usesEventTimeline != right.usesEventTimeline
            || std::abs(left.fadeInSeconds - right.fadeInSeconds) > tolerance
            || std::abs(left.fadeOutSeconds - right.fadeOutSeconds)
                   > tolerance
            || left.activeTakeIndex != right.activeTakeIndex
            || !sameAudioTakes(left.takes, right.takes)
            || std::abs(left.pitchSemitones - right.pitchSemitones) > tolerance
            || left.comp != right.comp
            || left.version != right.version
            || left.versions.size() != right.versions.size())
            return false;
    }
    return true;
}

double sourceTimeForClipTimeline(
    const AudioTrackController::AudioClip& clip,
    const double timelineSeconds)
{
    struct Point { double timeline; double source; };
    std::vector<Point> points;
    points.reserve(clip.stretchMarkers.size() + 2);
    points.push_back({ 0.0, clip.sourceOffsetSeconds });
    for (const auto& marker : clip.stretchMarkers)
        if (marker.timelineSeconds > 0.0
            && marker.timelineSeconds < clip.durationSeconds)
            points.push_back(
                { marker.timelineSeconds, marker.sourceSeconds });
    points.push_back(
        { clip.durationSeconds,
          clip.sourceOffsetSeconds + clip.sourceDurationSeconds });
    std::sort(points.begin(), points.end(),
              [](const auto& left, const auto& right) {
                  return left.timeline < right.timeline;
              });
    const auto safeTime = juce::jlimit(0.0, clip.durationSeconds,
                                       timelineSeconds);
    for (std::size_t index = 1; index < points.size(); ++index) {
        if (safeTime > points[index].timeline)
            continue;
        const auto amount = (safeTime - points[index - 1].timeline)
                            / std::max(0.000001,
                                       points[index].timeline
                                           - points[index - 1].timeline);
        return points[index - 1].source
               + amount * (points[index].source
                           - points[index - 1].source);
    }
    return clip.sourceOffsetSeconds + clip.sourceDurationSeconds;
}

std::vector<AudioTrackController::StretchMarker> stretchMarkersForSlice(
    const AudioTrackController::AudioClip& clip,
    const double sliceStart, const double sliceDuration)
{
    std::vector<AudioTrackController::StretchMarker> markers;
    const auto sliceEnd = sliceStart + sliceDuration;
    for (const auto& marker : clip.stretchMarkers)
        if (marker.timelineSeconds > sliceStart + 0.0005
            && marker.timelineSeconds < sliceEnd - 0.0005)
            markers.push_back(
                { marker.id, marker.sourceSeconds,
                  marker.timelineSeconds - sliceStart });
    return markers;
}

bool sameMidiNotes(
    const std::vector<AudioTrackController::MidiNote>& first,
    const std::vector<AudioTrackController::MidiNote>& second)
{
    if (first.size() != second.size())
        return false;
    constexpr double tolerance = 0.0000001;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto& left = first[index];
        const auto& right = second[index];
        if (left.id != right.id || left.noteNumber != right.noteNumber
            || std::abs(left.startBeats - right.startBeats) > tolerance
            || std::abs(left.durationBeats - right.durationBeats) > tolerance
            || std::abs(left.velocity - right.velocity) > 0.000001f
            || std::abs(left.chance - right.chance) > 0.000001f || left.pitch != right.pitch || left.gain != right.gain
            || left.timbre != right.timbre || left.pressure != right.pressure || left.pan != right.pan)
            return false;
    }
    return true;
}
} // namespace

LatencyCompensatingAudioSource::LatencyCompensatingAudioSource(juce::AudioSource& input) noexcept
    : input_(input)
{
}

void LatencyCompensatingAudioSource::setDelaySamples(const int delaySamples) noexcept
{
    const auto clamped = juce::jlimit(0, maximumDelaySamples, delaySamples);
    if (delaySamples_.exchange(clamped, std::memory_order_acq_rel) != clamped)
        requestReset();
}

void LatencyCompensatingAudioSource::requestReset() noexcept
{
    resetRequested_.store(true, std::memory_order_release);
}

int LatencyCompensatingAudioSource::delaySamples() const noexcept
{
    return delaySamples_.load(std::memory_order_acquire);
}

void LatencyCompensatingAudioSource::prepareToPlay(const int samplesPerBlockExpected,
                                                    const double sampleRate)
{
    delayBuffer_.setSize(channelCount_, maximumDelaySamples + 1,
                         false, true, false);
    delayBuffer_.clear();
    writePosition_ = 0;
    primedSamples_ = 0;
    resetRequested_.store(false, std::memory_order_release);
    input_.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void LatencyCompensatingAudioSource::releaseResources()
{
    input_.releaseResources();
    delayBuffer_.clear();
    writePosition_ = 0;
    primedSamples_ = 0;
    resetRequested_.store(true, std::memory_order_release);
}

void LatencyCompensatingAudioSource::getNextAudioBlock(
    const juce::AudioSourceChannelInfo& bufferToFill)
{
    input_.getNextAudioBlock(bufferToFill);
    if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0
        || delayBuffer_.getNumSamples() == 0)
        return;

    if (resetRequested_.exchange(false, std::memory_order_acq_rel)) {
        writePosition_ = 0;
        primedSamples_ = 0;
    }

    const auto delay = delaySamples_.load(std::memory_order_acquire);
    const auto channels = std::min(bufferToFill.buffer->getNumChannels(),
                                   delayBuffer_.getNumChannels());
    const auto ringSize = delayBuffer_.getNumSamples();
    for (int sample = 0; sample < bufferToFill.numSamples; ++sample) {
        auto readPosition = writePosition_ - delay;
        if (readPosition < 0)
            readPosition += ringSize;
        for (int channel = 0; channel < channels; ++channel) {
            auto* destination = bufferToFill.buffer->getWritePointer(
                channel, bufferToFill.startSample);
            const auto current = destination[sample];
            delayBuffer_.setSample(channel, writePosition_, current);
            destination[sample] = delay == 0
                                      ? current
                                      : (primedSamples_ >= delay
                                             ? delayBuffer_.getSample(channel, readPosition)
                                             : 0.0f);
        }
        if (++writePosition_ >= ringSize)
            writePosition_ = 0;
        primedSamples_ = std::min(maximumDelaySamples, primedSamples_ + 1);
    }
}

void AudioDelayLine::prepare(const int maximumDelaySamples, const int channels)
{
    const auto size = std::max(1, maximumDelaySamples) + 1;
    const auto width = juce::jlimit(1, LatencyCompensatingAudioSource::maximumChannelCount, channels);
    if (ring_.getNumSamples() >= size && ring_.getNumChannels() >= width)
        return;
    ring_.setSize(std::max(width, ring_.getNumChannels()), std::max(size, ring_.getNumSamples()), false, true, false);
    write_ = 0;
    reset_.store(true, std::memory_order_release);
}

void AudioDelayLine::setDelaySamples(const int delaySamples) noexcept
{
    const auto clamped = juce::jlimit(0, LatencyCompensatingAudioSource::maximumDelaySamples, delaySamples);
    if (delay_.exchange(clamped, std::memory_order_acq_rel) != clamped)
        requestReset();
}

void AudioDelayLine::process(juce::AudioBuffer<float>& buffer, const int startSample, const int numSamples) noexcept
{
    const auto size = ring_.getNumSamples();
    const auto delay = std::min(delay_.load(std::memory_order_acquire), size - 1);
    if (delay <= 0 || numSamples <= 0)
        return;
    if (reset_.exchange(false, std::memory_order_acq_rel)) {
        ring_.clear();
        write_ = 0;
    }
    const auto channels = std::min(buffer.getNumChannels(), ring_.getNumChannels());
    for (auto channel = 0; channel < channels; ++channel) {
        auto* samples = buffer.getWritePointer(channel, startSample);
        auto* ring = ring_.getWritePointer(channel);
        auto write = write_;
        for (auto index = 0; index < numSamples; ++index) {
            auto read = write - delay;
            if (read < 0)
                read += size;
            const auto incoming = samples[index];
            samples[index] = ring[read];
            ring[write] = incoming;
            if (++write >= size)
                write = 0;
        }
    }
    write_ = (write_ + numSamples) % size;
}

GainPanAudioSource::GainPanAudioSource(juce::AudioSource& input) noexcept
    : input_(input)
{
    automation_.store(std::make_shared<const std::vector<GainAutomationPoint>>());
    pluginAutomation_.store(
        std::make_shared<const std::vector<PluginParameterAutomationLane>>());
}

GainPanAudioSource::GainPanAudioSource(
    juce::AudioSource& input,
    const std::atomic<double>& timelinePosition) noexcept
    : input_(input), timelinePosition_(&timelinePosition)
{
    automation_.store(
        std::make_shared<const std::vector<GainAutomationPoint>>());
    pluginAutomation_.store(
        std::make_shared<const std::vector<PluginParameterAutomationLane>>());
}

GainPanAudioSource::GainPanAudioSource(juce::AudioTransportSource& input) noexcept
    : input_(input), transport_(&input)
{
    automation_.store(std::make_shared<const std::vector<GainAutomationPoint>>());
    pluginAutomation_.store(
        std::make_shared<const std::vector<PluginParameterAutomationLane>>());
}

GainPanAudioSource::~GainPanAudioSource() = default;

void GainPanAudioSource::setGain(const float linearGain) noexcept
{
    gain_.store(std::max(0.0f, linearGain), std::memory_order_relaxed);
}

void GainPanAudioSource::setPan(const float pan) noexcept
{
    pan_.store(juce::jlimit(-1.0f, 1.0f, pan), std::memory_order_relaxed);
}

void GainPanAudioSource::setChannelCount(const int channels) noexcept
{
    graphChannelCount_ = juce::jlimit(1, LatencyCompensatingAudioSource::maximumChannelCount, channels);
}

void GainPanAudioSource::setAutomation(
    std::shared_ptr<const std::vector<GainAutomationPoint>> automation)
{
    automation_.store(std::move(automation));
}

void GainPanAudioSource::setPanAutomation(
    std::shared_ptr<const std::vector<GainAutomationPoint>> lane)
{
    const auto on = lane != nullptr && !lane->empty();
    panAutomation_.store(std::move(lane));
    panAutomated_.store(on, std::memory_order_relaxed);
}

void GainPanAudioSource::setPluginAutomation(
    std::shared_ptr<const std::vector<PluginParameterAutomationLane>> automation)
{
    pluginAutomation_.store(std::move(automation));
}

void GainPanAudioSource::setDrive(const float driveDb) noexcept
{
    const auto value = juce::jlimit(0.0f, 36.0f, driveDb);
    driveDb_.store(value, std::memory_order_relaxed);
    // The track's drive fader reaches every drive card of the chain through
    // the runtime's lock-free path, as host automation does.
    for (auto node : nativeGraphDescription_.nodes)
        if (node.kind == cydaw::engine::NativeDspNodeKind::drive) {
            node.parameters.driveDb = value;
            (void) nativeGraph_.updateNodeParametersRealtime(node);
        }
}

void GainPanAudioSource::setFilterCutoff(const float cutoffHz) noexcept
{
    const auto value = juce::jlimit(20.0f, 20'000.0f, cutoffHz);
    filterCutoffHz_.store(value, std::memory_order_relaxed);
    for (auto node : nativeGraphDescription_.nodes)
        if (node.kind == cydaw::engine::NativeDspNodeKind::lowpass) {
            node.parameters.cutoffHz = value;
            (void) nativeGraph_.updateNodeParametersRealtime(node);
        }
}

juce::Result GainPanAudioSource::setNativeDspGraph(
    const cydaw::engine::NativeDspGraph& graph)
{
    const auto result = nativeGraph_.setGraph(graph);
    if (result.wasOk()) {
        nativeGraphDescription_ = graph;
        nativeGraphSet_.store(true, std::memory_order_release);
    }
    return result;
}

juce::Result GainPanAudioSource::updateNativeDspParameters(
    const cydaw::engine::NativeDspGraph& graph)
{
    // One parameter vocabulary for every chain: the runtime applies the
    // engine's own update, which knows every node the compiler can make. The
    // switch that lived here knew fewer.
    const auto result = nativeGraph_.updateParameters(graph);
    if (result.wasOk())
        nativeGraphDescription_ = graph;
    return result;
}

std::size_t GainPanAudioSource::nativeDspNodeCount() const noexcept
{
    return nativeGraphSet_.load(std::memory_order_acquire)
               ? nativeGraph_.audioNodeCount()
               : 0;
}

std::vector<std::pair<std::uint64_t, float>>
GainPanAudioSource::controlValues() const
{
    return nativeGraphSet_.load(std::memory_order_acquire)
               ? nativeGraph_.controlValues()
               : std::vector<std::pair<std::uint64_t, float>> {};
}

bool GainPanAudioSource::tryPopAnalysisFrame(
    cydaw::engine::AudioAnalysisFrame& frame) noexcept
{
    return analysisFrames_.tryPop(frame);
}

bool GainPanAudioSource::processNativeDspGraph(
    const juce::AudioSourceChannelInfo& bufferToFill) noexcept
{
    if (!nativeGraphSet_.load(std::memory_order_acquire)
        || bufferToFill.buffer == nullptr)
        return false;
    // The runtime is prepared for the device's block; an offline render can
    // hand over more at once, so the block is fed in pieces it can take.
    const auto step = std::max(1, maximumBlockSize_);
    // Where the block started on the timeline, for the automation: the
    // transport has already moved past it, as the volume lane reckons.
    const auto rate = std::max(1.0, sampleRate_);
    const auto blockEnd = modeTimeline_ != nullptr ? modeTimeline_->load(std::memory_order_relaxed) : transport_ != nullptr ? transport_->getCurrentPosition()
                          : timelinePosition_ != nullptr ? timelinePosition_->load(std::memory_order_relaxed)
                                                         : std::numeric_limits<double>::quiet_NaN();
    const auto blockStart = blockEnd - static_cast<double>(bufferToFill.numSamples) / rate;
    auto processed = false;
    for (int offset = 0; offset < bufferToFill.numSamples; offset += step)
        processed = nativeGraph_.process(
                        *bufferToFill.buffer, bufferToFill.startSample + offset,
                        std::min(step, bufferToFill.numSamples - offset),
                        blockStart + static_cast<double>(offset) / rate)
                    || processed;
    return processed;
}

juce::Result GainPanAudioSource::addPlugin(const juce::File& pluginFile,
                                           const double sampleRate,
                                           const int maximumBlockSize,
                                           const int channelCount,
                                           std::size_t* const pluginIndex)
{
    const auto currentCount = pluginCount_.load(std::memory_order_acquire);
    if (currentCount >= maximumPluginCount)
        return juce::Result::fail(tr("error.pluginChainFull",
                                     "count", juce::String(maximumPluginCount)));

    auto host = std::make_unique<IsolatedPluginHost>();
    const auto result = host->startPlugin(pluginFile, sampleRate, maximumBlockSize, channelCount);
    if (result.failed())
        return result;

    pluginsAvailable_.store(false, std::memory_order_release);
    while (pluginCallbacksInFlight_.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    pluginHosts_[currentCount] = std::move(host);
    pluginCount_.store(currentCount + 1, std::memory_order_release);
    pluginsAvailable_.store(true, std::memory_order_release);
    if (pluginIndex != nullptr)
        *pluginIndex = currentCount;
    return juce::Result::ok();
}

juce::Result GainPanAudioSource::reloadPlugin(std::size_t index, double rate, int block, int channels) {
    auto* old=pluginHost(index);if(!old)return juce::Result::fail("Unknown plugin");
    auto replacement=std::make_unique<IsolatedPluginHost>();
    auto result=replacement->startPlugin(old->pluginFile(),rate,block,channels);if(result.failed())return result;
    juce::MemoryBlock state;if(old->getState(state).wasOk()&&!state.isEmpty())(void)replacement->setState(state);
    pluginsAvailable_.store(false,std::memory_order_release);
    while(pluginCallbacksInFlight_.load(std::memory_order_acquire)!=0)std::this_thread::yield();
    auto removed=std::move(pluginHosts_[index]);pluginHosts_[index]=std::move(replacement);
    pluginsAvailable_.store(true,std::memory_order_release);removed->stop();return juce::Result::ok();
}

void GainPanAudioSource::removePlugin(const std::size_t pluginIndex)
{
    const auto currentCount = pluginCount_.load(std::memory_order_acquire);
    if (pluginIndex >= currentCount)
        return;

    pluginsAvailable_.store(false, std::memory_order_release);
    while (pluginCallbacksInFlight_.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    auto removed = std::move(pluginHosts_[pluginIndex]);
    for (auto index = pluginIndex; index + 1 < currentCount; ++index)
        pluginHosts_[index] = std::move(pluginHosts_[index + 1]);
    pluginHosts_[currentCount - 1].reset();
    pluginCount_.store(currentCount - 1, std::memory_order_release);
    pluginsAvailable_.store(true, std::memory_order_release);
    if (removed != nullptr)
        removed->stop();
}

void GainPanAudioSource::clearPlugins()
{
    pluginsAvailable_.store(false, std::memory_order_release);
    while (pluginCallbacksInFlight_.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    std::array<std::unique_ptr<IsolatedPluginHost>, maximumPluginCount> removed;
    for (std::size_t index = 0; index < pluginHosts_.size(); ++index)
        removed[index] = std::move(pluginHosts_[index]);
    pluginCount_.store(0, std::memory_order_release);
    pluginsAvailable_.store(true, std::memory_order_release);
    for (auto& host : removed)
        if (host != nullptr)
            host->stop();
}

bool GainPanAudioSource::movePlugin(const std::size_t pluginIndex, const int direction)
{
    const auto currentCount = pluginCount_.load(std::memory_order_acquire);
    if (pluginIndex >= currentCount || direction == 0)
        return false;
    const auto destination = static_cast<std::ptrdiff_t>(pluginIndex) + (direction < 0 ? -1 : 1);
    if (destination < 0 || destination >= static_cast<std::ptrdiff_t>(currentCount))
        return false;

    pluginsAvailable_.store(false, std::memory_order_release);
    while (pluginCallbacksInFlight_.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    std::swap(pluginHosts_[pluginIndex], pluginHosts_[static_cast<std::size_t>(destination)]);
    pluginsAvailable_.store(true, std::memory_order_release);
    return true;
}

void GainPanAudioSource::pollPluginHealth()
{
    const auto count = pluginCount_.load(std::memory_order_acquire);
    for (std::size_t index = 0; index < count; ++index)
        if (pluginHosts_[index] != nullptr)
            pluginHosts_[index]->pollWorkerHealth();
}

juce::Result GainPanAudioSource::getPluginState(const std::size_t pluginIndex,
                                                juce::MemoryBlock& destination)
{
    if (auto* host = pluginHost(pluginIndex))
        return host->getState(destination);
    return juce::Result::fail("Unknown plugin slot");
}

juce::Result GainPanAudioSource::getCachedPluginState(
    const std::size_t pluginIndex, juce::MemoryBlock& destination) const
{
    if (const auto* host = pluginHost(pluginIndex))
        return host->getCachedState(destination);
    return juce::Result::fail("Unknown plugin slot");
}

juce::Result GainPanAudioSource::setPluginState(const std::size_t pluginIndex,
                                                const juce::MemoryBlock& state)
{
    if (auto* host = pluginHost(pluginIndex))
        return host->setState(state);
    return juce::Result::fail("Unknown plugin slot");
}

juce::Result GainPanAudioSource::showPluginEditor(const std::size_t pluginIndex)
{
    if (auto* host = pluginHost(pluginIndex))
        return host->showEditor();
    return juce::Result::fail("Unknown plugin slot");
}

juce::Result GainPanAudioSource::refreshPluginParameters(
    const std::size_t pluginIndex,
    std::vector<IsolatedPluginHost::ParameterSnapshot>& destination)
{
    if (auto* host = pluginHost(pluginIndex))
        return host->refreshParameters(destination);
    return juce::Result::fail("Unknown plugin slot");
}

juce::Result GainPanAudioSource::setPluginParameter(const std::size_t pluginIndex,
                                                    const std::uint32_t parameterIndex,
                                                    const float normalisedValue)
{
    if (auto* host = pluginHost(pluginIndex))
        return host->setParameter(parameterIndex, normalisedValue);
    return juce::Result::fail("Unknown plugin slot");
}

std::size_t GainPanAudioSource::pluginCount() const noexcept
{
    return pluginCount_.load(std::memory_order_acquire);
}

IsolatedPluginHost* GainPanAudioSource::pluginHost(const std::size_t pluginIndex) noexcept
{
    return pluginIndex < pluginCount_.load(std::memory_order_acquire)
               ? pluginHosts_[pluginIndex].get() : nullptr;
}

const IsolatedPluginHost* GainPanAudioSource::pluginHost(const std::size_t pluginIndex) const noexcept
{
    return pluginIndex < pluginCount_.load(std::memory_order_acquire)
               ? pluginHosts_[pluginIndex].get() : nullptr;
}

int GainPanAudioSource::totalPluginLatencySamples() const noexcept
{
    auto latency = 0;
    const auto count = pluginCount_.load(std::memory_order_acquire);
    for (std::size_t index = 0; index < count; ++index)
        if (pluginHosts_[index] != nullptr)
            latency += pluginHosts_[index]->latencySamples();
    return latency;
}

int GainPanAudioSource::totalLatencySamples() const noexcept
{
    const auto graph = nativeGraphSet_.load(std::memory_order_acquire) ? nativeGraph_.latencySamples() : 0;
    return totalPluginLatencySamples() + graph;
}

void GainPanAudioSource::prepareToPlay(const int samplesPerBlockExpected, const double sampleRate)
{
    sampleRate_ = sampleRate;
    maximumBlockSize_ = std::max(1, samplesPerBlockExpected);
    nativeGraph_.prepare(sampleRate, maximumBlockSize_, graphChannelCount_);
    preFader_.setSize(graphChannelCount_, maximumBlockSize_, false, false, true);
    inputDelay_.prepare(LatencyCompensatingAudioSource::maximumDelaySamples, graphChannelCount_);
    filterState_.fill(0.0f);
    input_.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void GainPanAudioSource::releaseResources()
{
    filterState_.fill(0.0f);
    input_.releaseResources();
}

void GainPanAudioSource::publishAnalysisFrame(
    const juce::AudioSourceChannelInfo& bufferToFill) noexcept
{
    cydaw::engine::AudioAnalysisFrame frame;
    frame.sequence = ++analysisSequence_;
    frame.sampleRate = static_cast<float>(sampleRate_);
    if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0) {
        (void) analysisFrames_.tryPush(frame);
        return;
    }

    const auto channels = std::min(
        static_cast<int>(frame.maximumChannels),
        bufferToFill.buffer->getNumChannels());
    frame.channelCount = static_cast<std::uint32_t>(channels);
    for (int channel = 0; channel < channels; ++channel) {
        const auto* samples = bufferToFill.buffer->getReadPointer(
            channel, bufferToFill.startSample);
        double sumSquares {};
        float peak {};
        for (int sample = 0; sample < bufferToFill.numSamples; ++sample) {
            const auto value = samples[sample];
            peak = std::max(peak, std::abs(value));
            sumSquares += static_cast<double>(value) * value;
        }
        frame.peak[static_cast<std::size_t>(channel)] = peak;
        frame.rms[static_cast<std::size_t>(channel)] = static_cast<float>(
            std::sqrt(sumSquares / static_cast<double>(bufferToFill.numSamples)));
    }
    if (channels == 1) {
        frame.peak[1] = frame.peak[0];
        frame.rms[1] = frame.rms[0];
    }

    // Consecutive samples of the mono mix, kept so the frame can hold more
    // than one block. Decimating the block into a fixed number of points, as
    // this used to, made a display but not a spectrum.
    for (int sample = 0; sample < bufferToFill.numSamples; ++sample) {
        float value {};
        for (int channel = 0; channel < channels; ++channel)
            value += bufferToFill.buffer->getSample(
                channel, bufferToFill.startSample + sample);
        analysisRing_.push(channels > 0 ? value / static_cast<float>(channels)
                                        : 0.0f);
    }
    analysisRing_.fill(frame);
    (void) analysisFrames_.tryPush(frame);
}

void GainPanAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (auto* source = externalInput_.load(std::memory_order_acquire))
        source->getNextAudioBlock(bufferToFill);
    else
        input_.getNextAudioBlock(bufferToFill);
    // Its own sound waits for what is routed into it.
    if (bufferToFill.buffer != nullptr)
        inputDelay_.process(*bufferToFill.buffer, bufferToFill.startSample, bufferToFill.numSamples);
    // Other tracks routed into this one: into its main input, joined to its
    // sound before its devices; into its other inputs, the graph's own.
    if (const auto* main = routedInputs_[0]; main != nullptr && bufferToFill.buffer != nullptr) {
        const auto count = std::min(bufferToFill.numSamples, routedSamples_);
        const auto sourceChannels = main->getNumChannels();
        for (auto channel = 0; channel < bufferToFill.buffer->getNumChannels() && sourceChannels > 0; ++channel)
            bufferToFill.buffer->addFrom(channel, bufferToFill.startSample, *main, std::min(channel, sourceChannels - 1), 0, count);
    }
    // What enters it, before its devices: its meter's falling peak.
    if (bufferToFill.buffer != nullptr && bufferToFill.numSamples > 0) {
        const auto fall = std::pow(0.5f, static_cast<float>(bufferToFill.numSamples) / 9'600.0f);
        const auto now = bufferToFill.buffer->getMagnitude(bufferToFill.startSample, bufferToFill.numSamples);
        soundInLevel_.store(std::max(soundInLevel_.load(std::memory_order_relaxed) * fall, now), std::memory_order_relaxed);
    }
    nativeGraph_.setExtraInputs(routedInputs_.data() + 1, maximumBuses - 1);
    const auto gain = gain_.load(std::memory_order_relaxed);
    auto pan = pan_.load(std::memory_order_relaxed);
    // A clip's pan lane: read once a block, on the volume lane's clock.
    if (const auto lane = panAutomation_.read(); lane != nullptr && !lane->empty()
                                                 && (transport_ != nullptr || timelinePosition_ != nullptr)) {
        const auto time = modeTimeline_ != nullptr ? modeTimeline_->load(std::memory_order_relaxed)
                          : transport_ != nullptr  ? transport_->getCurrentPosition()
                                                   : timelinePosition_->load(std::memory_order_relaxed);
        pan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(laneValueAt(*lane, time)));
        automatedPan_.store(pan, std::memory_order_relaxed);
    }
    const auto channels = bufferToFill.buffer->getNumChannels();
    // The balance turns down the far side's speakers, whatever the format;
    // a mono track's is applied where the mixer spreads it.
    std::array<float, LatencyCompensatingAudioSource::maximumChannelCount> channelGains {};
    channelGains.fill(gain);
    for (auto channel = 0; channel < std::min(channels, static_cast<int>(channelGains.size())); ++channel)
        channelGains[static_cast<std::size_t>(channel)]
            = gain * cydaw::engine::balanceGain(cydaw::engine::speakerRole(channels, channel), channels > 1 ? pan : 0.0f);
    if (!processNativeDspGraph(bufferToFill)) {
        const auto driveDb = driveDb_.load(std::memory_order_relaxed);
        const auto cutoff = filterCutoffHz_.load(std::memory_order_relaxed);
        const auto driveGain = juce::Decibels::decibelsToGain(driveDb);
        const auto driveNormalisation = driveDb > 0.001f ? 1.0f / std::tanh(driveGain) : 1.0f;
        const auto shouldFilter = cutoff < static_cast<float>(sampleRate_ * 0.45);
        const auto filterAmount = shouldFilter
                                      ? static_cast<float>(1.0 - std::exp(-juce::MathConstants<double>::twoPi
                                                                          * cutoff / sampleRate_))
                                      : 1.0f;

        for (int channel = 0; channel < std::min(channels, static_cast<int>(filterState_.size())); ++channel) {
            auto* samples = bufferToFill.buffer->getWritePointer(channel, bufferToFill.startSample);
            auto state = filterState_[static_cast<std::size_t>(channel)];
            for (int sample = 0; sample < bufferToFill.numSamples; ++sample) {
                auto value = samples[sample];
                if (driveDb > 0.001f)
                    value = std::tanh(value * driveGain) * driveNormalisation;
                if (shouldFilter) {
                    state += filterAmount * (value - state);
                    value = state;
                } else {
                    state = value;
                }
                samples[sample] = value;
            }
            filterState_[static_cast<std::size_t>(channel)] = state;
        }
    }

    if (pluginsAvailable_.load(std::memory_order_acquire)) {
        pluginCallbacksInFlight_.fetch_add(1, std::memory_order_acq_rel);
        if (pluginsAvailable_.load(std::memory_order_acquire)) {
            const auto count = pluginCount_.load(std::memory_order_acquire);
            const auto pluginAutomation = pluginAutomation_.read();
            const auto blockEnd = modeTimeline_ != nullptr ? modeTimeline_->load(std::memory_order_relaxed) : transport_ != nullptr
                                      ? transport_->getCurrentPosition()
                                      : timelinePosition_ != nullptr
                                            ? timelinePosition_->load(
                                                  std::memory_order_relaxed)
                                            : 0.0;
            const auto blockStart = blockEnd
                                    - static_cast<double>(bufferToFill.numSamples) / sampleRate_;
            for (std::size_t index = 0; index < count; ++index) {
                if (pluginHosts_[index] != nullptr) {
                    std::array<IsolatedPluginHost::RealtimeParameterValue,
                               cydaw::plugins::maximumSharedAutomationParameters> values {};
                    std::size_t valueCount {};
                    if (pluginAutomation != nullptr) {
                        for (const auto& lane : *pluginAutomation) {
                            if (lane.pluginIndex != index || lane.points.empty()
                                || valueCount >= values.size())
                                continue;

                            const auto value = static_cast<float>(laneValueAt(lane.points, blockStart));
                            values[valueCount++] = { lane.parameterIndex,
                                                     juce::jlimit(0.0f, 1.0f, value) };
                        }
                    }
                    if (valueCount > 0)
                        pluginHosts_[index]->publishRealtimeParameters(values.data(), valueCount);
                    const auto endPosition = modeTimeline_ ? modeTimeline_->load(std::memory_order_relaxed)
                        : transport_ ? transport_->getCurrentPosition()
                        : timelinePosition_ ? timelinePosition_->load(std::memory_order_relaxed) : 0.0;
                    (void) pluginHosts_[index]->processBlock(*bufferToFill.buffer,
                        bufferToFill.startSample, bufferToFill.numSamples,
                        std::max(0.0,endPosition-bufferToFill.numSamples/std::max(1.0,sampleRate_)),
                        NativeDspRuntime::sessionTempo(), NativeDspRuntime::sessionPlaying());
                    // How loud it came out, for its meter.
                    for (int channel = 0; channel < std::min(2, bufferToFill.buffer->getNumChannels()); ++channel) {
                        const auto loudest = bufferToFill.buffer->getMagnitude(channel, bufferToFill.startSample, bufferToFill.numSamples);
                        auto& kept = pluginPeaks_[index * 2 + static_cast<std::size_t>(channel)];
                        if (loudest > kept.load(std::memory_order_relaxed))
                            kept.store(loudest, std::memory_order_relaxed);
                    }
                    if (bufferToFill.buffer->getNumChannels() == 1)
                        pluginPeaks_[index * 2 + 1].store(pluginPeaks_[index * 2].load(std::memory_order_relaxed),
                                                          std::memory_order_relaxed);
                }
            }
        }
        pluginCallbacksInFlight_.fetch_sub(1, std::memory_order_acq_rel);
    }

    nativeGraph_.setExtraInputs(nullptr, 0);
    preFaderValid_ = false;
    if (keepPreFader_.load(std::memory_order_relaxed) && bufferToFill.buffer != nullptr && channels > 0) {
        if (preFader_.getNumSamples() < bufferToFill.numSamples)
            preFader_.setSize(preFader_.getNumChannels(), bufferToFill.numSamples, false, false, true);
        const auto audible = sendsAudible_.load(std::memory_order_relaxed);
        for (auto channel = 0; channel < preFader_.getNumChannels(); ++channel) {
            if (audible)
                preFader_.copyFrom(channel, 0, *bufferToFill.buffer, std::min(channel, channels - 1), bufferToFill.startSample,
                                   bufferToFill.numSamples);
            else
                preFader_.clear(channel, 0, bufferToFill.numSamples);
        }
        preFaderValid_ = true;
    }

    const auto automation = automation_.read();
    if ((transport_ != nullptr || timelinePosition_ != nullptr)
        && automation != nullptr && !automation->empty()) {
        const auto blockEnd = modeTimeline_ != nullptr ? modeTimeline_->load(std::memory_order_relaxed) : transport_ != nullptr
                                  ? transport_->getCurrentPosition()
                                  : timelinePosition_->load(
                                        std::memory_order_relaxed);
        const auto blockStart = blockEnd - static_cast<double>(bufferToFill.numSamples) / sampleRate_;
        std::size_t pointIndex = 0;
        for (int sample = 0; sample < bufferToFill.numSamples; ++sample) {
            const auto time = blockStart + static_cast<double>(sample) / sampleRate_;
            // A curve swinging past its points stays in the lane's range.
            const auto automationDb = juce::jlimit(-60.0f, 12.0f, static_cast<float>(laneValueAt(*automation, time, &pointIndex)));

            const auto automationGain = juce::Decibels::decibelsToGain(automationDb, -60.0f);
            for (int channel = 0; channel < channels; ++channel)
                bufferToFill.buffer->getWritePointer(channel, bufferToFill.startSample)[sample]
                    *= channelGains[static_cast<std::size_t>(std::min(channel, static_cast<int>(channelGains.size()) - 1))]
                       * automationGain;
        }
        publishAnalysisFrame(bufferToFill);
        return;
    }

    for (int channel = 0; channel < channels; ++channel)
        bufferToFill.buffer->applyGain(channel, bufferToFill.startSample, bufferToFill.numSamples,
                                       channelGains[static_cast<std::size_t>(
                                           std::min(channel, static_cast<int>(channelGains.size()) - 1))]);
    publishAnalysisFrame(bufferToFill);
}

InputRecorder::InputRecorder()
{
    writerThread_.startThread(juce::Thread::Priority::normal);
}

InputRecorder::~InputRecorder()
{
    stop();
    writerThread_.stopThread(3'000);
}

juce::Result InputRecorder::start(const juce::File& destination,
                                  const double startDelaySeconds)
{
    stop();
    const auto sampleRate = sampleRate_.load(std::memory_order_acquire);
    const auto route = inputBus_.current();
    const auto channelCount = route != nullptr
        ? juce::jlimit(1, VirtualAudioBus::maximumChannels, route->channels)
        : inputChannelCount_.load(std::memory_order_acquire);
    if (sampleRate <= 0.0 || channelCount <= 0)
        return juce::Result::fail(tr("error.recordInput"));

    auto fileOutput = destination.createOutputStream();
    if (fileOutput == nullptr)
        return juce::Result::fail(tr("error.recordFile", "file", destination.getFullPathName()));

    if (!fileOutput->setPosition(0) || fileOutput->truncate().failed())
        return juce::Result::fail(tr("error.recordFile", "file", destination.getFullPathName()));

    std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate(sampleRate)
                             .withNumChannels(channelCount)
                             .withBitsPerSample(24);
    auto writer = wav.createWriterFor(output, options);
    if (writer == nullptr)
        return juce::Result::fail(tr("error.recordWriter"));

    droppedBlocks_.store(0, std::memory_order_relaxed);
    threadedWriter_ = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        writer.release(), writerThread_, static_cast<int>(sampleRate * 4.0));
    const auto startDelaySamples = static_cast<juce::int64>(std::llround(
        std::max(0.0, startDelaySeconds) * sampleRate));
    samplesUntilStart_.store(startDelaySamples, std::memory_order_release);
    samplesUntilStop_.store(-1, std::memory_order_release);
    started_.store(startDelaySamples == 0, std::memory_order_release);
    stopPending_.store(false, std::memory_order_release);
    recordingComplete_.store(false, std::memory_order_release);
    recordingChannelCount_.store(channelCount, std::memory_order_release);
    activeWriter_.store(threadedWriter_.get(), std::memory_order_release);
    return juce::Result::ok();
}

void InputRecorder::scheduleStop(const double delaySeconds)
{
    if (activeWriter_.load(std::memory_order_acquire) == nullptr
        || recordingComplete_.load(std::memory_order_acquire))
        return;
    const auto sampleRate = sampleRate_.load(std::memory_order_acquire);
    const auto delaySamples = static_cast<juce::int64>(std::llround(
        std::max(0.0, delaySeconds) * std::max(1.0, sampleRate)));
    samplesUntilStop_.store(delaySamples, std::memory_order_release);
    stopPending_.store(true, std::memory_order_release);
}

void InputRecorder::stop()
{
    activeWriter_.store(nullptr, std::memory_order_release);
    while (callbacksInFlight_.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    threadedWriter_.reset();
    recordingChannelCount_.store(0, std::memory_order_release);
    samplesUntilStart_.store(0, std::memory_order_release);
    samplesUntilStop_.store(-1, std::memory_order_release);
    started_.store(false, std::memory_order_release);
    stopPending_.store(false, std::memory_order_release);
    recordingComplete_.store(false, std::memory_order_release);
}

bool InputRecorder::isRecording() const noexcept
{
    return activeWriter_.load(std::memory_order_acquire) != nullptr
           && !recordingComplete_.load(std::memory_order_acquire);
}

bool InputRecorder::hasSession() const noexcept
{
    return activeWriter_.load(std::memory_order_acquire) != nullptr;
}

bool InputRecorder::hasStarted() const noexcept
{
    return started_.load(std::memory_order_acquire);
}

bool InputRecorder::stopPending() const noexcept
{
    return stopPending_.load(std::memory_order_acquire);
}

bool InputRecorder::recordingComplete() const noexcept
{
    return recordingComplete_.load(std::memory_order_acquire);
}

int InputRecorder::droppedBlockCount() const noexcept
{
    return droppedBlocks_.load(std::memory_order_relaxed);
}

void InputRecorder::setInputBus(
    std::optional<VirtualAudioBus> bus,
    cydaw::engine::NativeDspGraph graph)
{
    if (bus.has_value()) {
        inputBusGraph_.prepare(
            std::max(1.0, sampleRate_.load(std::memory_order_acquire)),
            std::max(1, routedInputBuffer_.getNumSamples()),
            bus->channels);
        (void) inputBusGraph_.setGraph(graph);
    }
    inputBus_.store(bus.has_value()
                        ? std::make_shared<const VirtualAudioBus>(*bus)
                        : std::shared_ptr<const VirtualAudioBus> {});
}

void InputRecorder::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                     const int numInputChannels,
                                                     float* const* outputChannelData,
                                                     const int numOutputChannels,
                                                     const int numSamples,
                                                     const juce::AudioIODeviceCallbackContext&)
{
    // AudioDeviceManager mixes secondary callbacks into the device output, so
    // a recorder callback must explicitly contribute silence.
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData != nullptr && outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    if (numInputChannels <= 0 || inputChannelData == nullptr)
        return;

    for (;;) {
        auto* writer = activeWriter_.load(std::memory_order_acquire);
        if (writer == nullptr)
            return;

        const auto inputBus = inputBus_.read();
        if (inputBus == nullptr
            && numInputChannels < recordingChannelCount_.load(std::memory_order_acquire)) {
            droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        callbacksInFlight_.fetch_add(1, std::memory_order_acq_rel);
        if (writer != activeWriter_.load(std::memory_order_acquire)) {
            callbacksInFlight_.fetch_sub(1, std::memory_order_release);
            continue;
        }

        if (recordingComplete_.load(std::memory_order_acquire)) {
            callbacksInFlight_.fetch_sub(1, std::memory_order_release);
            return;
        }

        auto sourceOffset = 0;
        const auto startRemaining = samplesUntilStart_.load(
            std::memory_order_acquire);
        if (startRemaining > 0) {
            if (startRemaining >= numSamples) {
                samplesUntilStart_.store(startRemaining - numSamples,
                                         std::memory_order_release);
                callbacksInFlight_.fetch_sub(1, std::memory_order_release);
                return;
            }
            sourceOffset = static_cast<int>(startRemaining);
            samplesUntilStart_.store(0, std::memory_order_release);
            started_.store(true, std::memory_order_release);
        } else if (!started_.load(std::memory_order_relaxed))
            started_.store(true, std::memory_order_release);

        auto samplesToWrite = numSamples - sourceOffset;
        auto completesAfterWrite = false;
        if (stopPending_.load(std::memory_order_acquire)) {
            const auto stopRemaining = samplesUntilStop_.load(
                std::memory_order_acquire);
            if (stopRemaining <= 0) {
                recordingComplete_.store(true, std::memory_order_release);
                callbacksInFlight_.fetch_sub(1, std::memory_order_release);
                return;
            }
            if (stopRemaining <= samplesToWrite) {
                samplesToWrite = static_cast<int>(stopRemaining);
                completesAfterWrite = true;
            }
        }

        std::array<const float*, 64> offsetChannels {};
        std::array<const float*, VirtualAudioBus::maximumChannels> routedChannels {};
        const float* const* channels = inputChannelData;
        auto routedChannelCount = numInputChannels;
        if (inputBus != nullptr) {
            if (numSamples > routedInputBuffer_.getNumSamples()) {
                droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
                callbacksInFlight_.fetch_sub(1, std::memory_order_release);
                return;
            }
            routedChannelCount = juce::jlimit(1, std::min(VirtualAudioBus::maximumChannels, routedInputBuffer_.getNumChannels()),
                                              inputBus->channels);
            const auto afterEffects = afterEffects_.load(std::memory_order_acquire);
            // As the router ran it this block (what is heard is what is
            // recorded); without it, read and treated here.
            auto taken = 0;
            if (router_ == nullptr
                || !router_->inputBusBlock(inputBus->id, afterEffects, numSamples, routedChannels, taken)
                || taken != routedChannelCount) {
                std::array<float*, VirtualAudioBus::maximumChannels> lanes {};
                for (auto channel = 0; channel < routedChannelCount; ++channel)
                    lanes[static_cast<std::size_t>(channel)] = routedInputBuffer_.getWritePointer(channel);
                auto bus = *inputBus;
                bus.channels = routedChannelCount;
                VirtualBusRouter::renderInputBus(bus, inputChannelData, numInputChannels, lanes.data(), numSamples);
                if (afterEffects) {
                    juce::AudioBuffer<float> view(lanes.data(), routedChannelCount, numSamples);
                    (void) inputBusGraph_.process(view, 0, numSamples);
                }
                for (auto channel = 0; channel < routedChannelCount; ++channel)
                    routedChannels[static_cast<std::size_t>(channel)] = lanes[static_cast<std::size_t>(channel)];
            }
            channels = routedChannels.data();
        }
        if (sourceOffset > 0) {
            if (routedChannelCount > static_cast<int>(offsetChannels.size())) {
                droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
                callbacksInFlight_.fetch_sub(1, std::memory_order_release);
                return;
            }
            for (int channel = 0; channel < routedChannelCount; ++channel)
                offsetChannels[static_cast<std::size_t>(channel)]
                    = channels[channel] + sourceOffset;
            channels = offsetChannels.data();
        }
        if (samplesToWrite > 0 && !writer->write(channels, samplesToWrite))
            droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
        if (stopPending_.load(std::memory_order_acquire)) {
            const auto stopRemaining = samplesUntilStop_.load(
                std::memory_order_acquire);
            const auto nextRemaining = std::max<juce::int64>(
                0, stopRemaining - samplesToWrite);
            samplesUntilStop_.store(nextRemaining,
                                    std::memory_order_release);
            if (completesAfterWrite || nextRemaining == 0)
                recordingComplete_.store(true, std::memory_order_release);
        }
        callbacksInFlight_.fetch_sub(1, std::memory_order_release);
        return;
    }
}

void InputRecorder::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    sampleRate_.store(device != nullptr ? device->getCurrentSampleRate() : 0.0, std::memory_order_release);
    inputChannelCount_.store(device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0,
                             std::memory_order_release);
    routedInputBuffer_.setSize(
        VirtualAudioBus::maximumChannels, device != nullptr
               ? std::max(1, device->getCurrentBufferSizeSamples()) : 1,
        false, false, true);
    if (const auto bus = inputBus_.current())
        inputBusGraph_.prepare(
            device != nullptr ? device->getCurrentSampleRate() : 48'000.0,
            device != nullptr
                ? std::max(1, device->getCurrentBufferSizeSamples()) : 1,
            bus->channels);
}

void InputRecorder::audioDeviceStopped()
{
    sampleRate_.store(0.0, std::memory_order_release);
    inputChannelCount_.store(0, std::memory_order_release);
}

juce::Result InputRecorder::selfTestScheduledRecording(
    const juce::File& destination)
{
    if (destination.existsAsFile())
        (void) destination.deleteFile();
    InputRecorder recorder;
    recorder.sampleRate_.store(48'000.0, std::memory_order_release);
    recorder.inputChannelCount_.store(2, std::memory_order_release);
    const auto started = recorder.start(destination, 3.0 / 48'000.0);
    if (started.failed())
        return started;

    std::array<float, 8> left {
        0.01f, 0.02f, 0.03f, 0.04f,
        0.05f, 0.06f, 0.07f, 0.08f
    };
    std::array<float, 8> right {
        -0.01f, -0.02f, -0.03f, -0.04f,
        -0.05f, -0.06f, -0.07f, -0.08f
    };
    const std::array<const float*, 2> inputs { left.data(), right.data() };
    juce::AudioIODeviceCallbackContext context {};
    recorder.audioDeviceIOCallbackWithContext(
        inputs.data(), 2, nullptr, 0, 8, context);
    if (!recorder.hasStarted() || recorder.recordingComplete()) {
        recorder.stop();
        return juce::Result::fail(
            "The scheduled recorder did not start inside the audio block");
    }
    recorder.scheduleStop(4.0 / 48'000.0);
    recorder.audioDeviceIOCallbackWithContext(
        inputs.data(), 2, nullptr, 0, 8, context);
    if (!recorder.recordingComplete()) {
        recorder.stop();
        return juce::Result::fail(
            "The scheduled recorder did not stop inside the audio block");
    }
    recorder.stop();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(destination));
    if (reader == nullptr || reader->lengthInSamples != 9
        || reader->numChannels != 2)
        return juce::Result::fail(
            "The scheduled recording has an incorrect length");
    juce::AudioBuffer<float> rendered(2, 9);
    if (!reader->read(&rendered, 0, 9, 0, true, true)
        || std::abs(rendered.getSample(0, 0) - left[3]) > 0.0001f
        || std::abs(rendered.getSample(1, 0) - right[3]) > 0.0001f
        || std::abs(rendered.getSample(0, 8) - left[3]) > 0.0001f)
        return juce::Result::fail(
            "The scheduled recording boundaries are not sample accurate");
    return juce::Result::ok();
}

// ---- The tracks' mixer ---------------------------------------------------------------------------

TrackMixer::TrackMixer()
{
    plan_.store(std::make_shared<const Plan>());
}

void TrackMixer::addInputSource(juce::AudioSource* source, const bool deleteWhenRemoved)
{
    jassert(!deleteWhenRemoved);
    juce::ignoreUnused(deleteWhenRemoved);
    if (source == nullptr)
        return;
    int block {};
    double rate {};
    bool prepared {};
    {
        const juce::ScopedLock lock(lock_);
        block = blockSize_;
        rate = sampleRate_;
        prepared = prepared_;
    }
    if (prepared)
        source->prepareToPlay(block, rate);
    juce::AudioBuffer<float> rendered(maximumChannels, std::max(1, block));
    std::vector<juce::AudioBuffer<float>> routed;
    for (std::size_t bus = 0; bus < buses; ++bus)
        routed.emplace_back(maximumChannels, std::max(1, block));
    const juce::ScopedLock lock(lock_);
    inputs_.push_back(source);
    rendered_.push_back(std::move(rendered));
    for (auto& buffer : routed) {
        routed_.push_back(std::move(buffer));
        routedViews_.emplace_back();
        routedUsed_.push_back(0);
    }
    done_.push_back(0);
}

void TrackMixer::removeInputSource(juce::AudioSource* source)
{
    {
        const juce::ScopedLock lock(lock_);
        const auto index = indexOf(source);
        if (index < 0)
            return;
        const auto at = static_cast<std::ptrdiff_t>(index);
        inputs_.erase(inputs_.begin() + at);
        rendered_.erase(rendered_.begin() + at);
        const auto first = at * static_cast<std::ptrdiff_t>(buses);
        routed_.erase(routed_.begin() + first, routed_.begin() + first + static_cast<std::ptrdiff_t>(buses));
        routedViews_.erase(routedViews_.begin() + first, routedViews_.begin() + first + static_cast<std::ptrdiff_t>(buses));
        routedUsed_.erase(routedUsed_.begin() + first, routedUsed_.begin() + first + static_cast<std::ptrdiff_t>(buses));
        done_.erase(done_.begin() + at);
    }
    // Nothing of the routing may name it any more: a track made later at the
    // same address would be taken for it.
    if (const auto current = plan_.current()) {
        auto pruned = std::make_shared<Plan>();
        pruned->hardware = current->hardware;
        for (const auto& entry : current->entries) {
            if (entry.source == source)
                continue;
            auto kept = entry;
            std::erase_if(kept.outputs, [source](const Output& output) { return output.destination == source; });
            pruned->entries.push_back(std::move(kept));
        }
        plan_.store(std::move(pruned));
    }
    source->releaseResources();
}

void TrackMixer::setPlan(std::shared_ptr<const Plan> plan)
{
    plan_.store(plan != nullptr ? std::move(plan) : std::make_shared<const Plan>());
}

int TrackMixer::indexOf(const juce::AudioSource* source) const noexcept
{
    for (std::size_t index = 0; index < inputs_.size(); ++index)
        if (inputs_[index] == source)
            return static_cast<int>(index);
    return -1;
}

void TrackMixer::prepareToPlay(const int samplesPerBlockExpected, const double sampleRate)
{
    std::vector<juce::AudioSource*> inputs;
    {
        const juce::ScopedLock lock(lock_);
        blockSize_ = std::max(1, samplesPerBlockExpected);
        sampleRate_ = sampleRate;
        prepared_ = true;
        for (auto& buffer : rendered_)
            buffer.setSize(maximumChannels, blockSize_, false, false, true);
        for (auto& buffer : routed_)
            buffer.setSize(maximumChannels, blockSize_, false, false, true);
        delayed_.setSize(maximumChannels, blockSize_, false, false, true);
        for (auto& buffer : direct_)
            buffer.setSize(maximumChannels, blockSize_, false, false, true);
        directSamples_ = 0;
        inputs = inputs_;
    }
    // (each track's inputs: `buses` buffers in a row)
    for (auto* input : inputs)
        input->prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void TrackMixer::releaseResources()
{
    std::vector<juce::AudioSource*> inputs;
    {
        const juce::ScopedLock lock(lock_);
        prepared_ = false;
        inputs = inputs_;
    }
    for (auto* input : inputs)
        input->releaseResources();
}

const juce::AudioBuffer<float>* TrackMixer::directOutput(const std::uint64_t busId, const int numSamples,
                                                         const bool beforeEffects) noexcept
{
    if (directSamples_ != numSamples || busId == 0)
        return nullptr;
    for (std::size_t slot = 0; slot < maximumDirectOutputs; ++slot)
        if (directBus_[slot] == busId && (directBefore_[slot] != 0) == beforeEffects && directUsed_[slot] != 0)
            return &directViews_[slot];
    return nullptr;
}

void TrackMixer::getNextAudioBlock(const juce::AudioSourceChannelInfo& output)
{
    const juce::ScopedLock lock(lock_);
    output.clearActiveBufferRegion();
    directSamples_ = 0;
    if (output.buffer == nullptr || output.numSamples <= 0)
        return;
    const auto count = output.numSamples;
    const auto masterChannels = std::min(2, output.buffer->getNumChannels());
    for (auto index = std::size_t {}; index < inputs_.size(); ++index) {
        // A block longer than prepared (rare): the buffers grow once.
        if (rendered_[index].getNumSamples() < count)
            rendered_[index].setSize(maximumChannels, count, false, false, true);
        done_[index] = 0;
    }
    for (auto index = std::size_t {}; index < routed_.size(); ++index) {
        if (routed_[index].getNumSamples() < count)
            routed_[index].setSize(maximumChannels, count, false, false, true);
        routedUsed_[index] = 0;
    }
    if (delayed_.getNumSamples() < count)
        delayed_.setSize(maximumChannels, count, false, false, true);
    const auto plan = plan_.read();
    // The sound card's outputs the tracks go to directly this block.
    directUsed_.fill(0);
    directBus_.fill(0);
    if (plan != nullptr)
        for (std::size_t slot = 0; slot < std::min(plan->hardware.size(), maximumDirectOutputs); ++slot) {
            if (direct_[slot].getNumSamples() < count)
                direct_[slot].setSize(maximumChannels, count, false, false, true);
            directBus_[slot] = plan->hardware[slot].busId;
            directBefore_[slot] = plan->hardware[slot].beforeEffects ? 1 : 0;
        }
    // `from` (its `fromChannels`) added into `into` (its `toChannels`) at a
    // level; a mono sound spreads by its track's pan.
    const auto send = [count](const float* const* from, const int fromChannels, float* const* into, const int toChannels,
                              const float gain, const float pan) {
        if (fromChannels == toChannels && (fromChannels != 1 || pan == 0.0f)) {
            for (auto channel = 0; channel < toChannels; ++channel)
                juce::FloatVectorOperations::addWithMultiply(into[channel], from[channel], gain, count);
            return;
        }
        cydaw::engine::convertChannels(cydaw::engine::channelConversion(fromChannels, toChannels), from, into, count, gain,
                                       true, fromChannels == 1 ? pan : 0.0f);
    };
    std::array<float*, maximumChannels> masterLanes {};
    for (auto channel = 0; channel < masterChannels; ++channel)
        masterLanes[static_cast<std::size_t>(channel)] = output.buffer->getWritePointer(channel, output.startSample);
    const auto render = [&](const int index, RoutedInputReceiver* receiver, GainPanAudioSource* fader,
                            const std::vector<Output>* outputs, const int ownChannels) {
        const auto slot = static_cast<std::size_t>(index);
        const auto channels = juce::jlimit(1, maximumChannels, ownChannels);
        juce::AudioBuffer<float> buffer(rendered_[slot].getArrayOfWritePointers(), channels, count);
        buffer.clear();
        std::array<const juce::AudioBuffer<float>*, buses> reaching {};
        for (std::size_t bus = 0; bus < buses; ++bus)
            if (routedUsed_[slot * buses + bus] != 0)
                reaching[bus] = &routedViews_[slot * buses + bus];
        if (receiver != nullptr)
            receiver->setRoutedInputs(reaching.data(), count);
        inputs_[slot]->getNextAudioBlock(juce::AudioSourceChannelInfo(&buffer, 0, count));
        if (receiver != nullptr)
            receiver->setRoutedInputs(nullptr, 0);
        done_[slot] = 1;
        const auto pan = channels == 1 && fader != nullptr ? fader->pan() : 0.0f;
        if (outputs == nullptr || outputs->empty()) {
            send(buffer.getArrayOfReadPointers(), channels, masterLanes.data(), masterChannels, 1.0f, pan);
            return;
        }
        const auto* beforeFader = fader != nullptr ? fader->preFader() : nullptr;
        for (const auto& out : *outputs) {
            const auto* source = out.preFader && beforeFader != nullptr ? beforeFader : &buffer;
            const auto sourceChannels = std::min(channels, source->getNumChannels());
            std::array<const float*, maximumChannels> from {};
            for (auto channel = 0; channel < sourceChannels; ++channel)
                from[static_cast<std::size_t>(channel)] = source->getReadPointer(channel);
            if (out.delay != nullptr) {
                // Lined up with the latest of what reaches its destination;
                // fed every block, whatever follows.
                juce::AudioBuffer<float> held(delayed_.getArrayOfWritePointers(), sourceChannels, count);
                for (auto channel = 0; channel < sourceChannels; ++channel)
                    held.copyFrom(channel, 0, *source, channel, 0, count);
                out.delay->process(held, 0, count);
                for (auto channel = 0; channel < sourceChannels; ++channel)
                    from[static_cast<std::size_t>(channel)] = held.getReadPointer(channel);
            }
            const auto spread = out.preFader ? 0.0f : pan;
            if (out.hardware >= 0) {
                const auto at = static_cast<std::size_t>(out.hardware);
                if (at >= maximumDirectOutputs)
                    continue;
                const auto width = juce::jlimit(1, maximumChannels, out.channels);
                if (directUsed_[at] == 0) {
                    directViews_[at].setDataToReferTo(direct_[at].getArrayOfWritePointers(), width, count);
                    directViews_[at].setNotClear(); // a view widened keeps its old flag
                    directViews_[at].clear();
                    directUsed_[at] = 1;
                }
                send(from.data(), sourceChannels, directViews_[at].getArrayOfWritePointers(), width, out.gain, spread);
                continue;
            }
            if (out.destination == nullptr) {
                send(from.data(), sourceChannels, masterLanes.data(), masterChannels, out.gain, spread);
                continue;
            }
            const auto target = indexOf(out.destination);
            if (target < 0 || done_[static_cast<std::size_t>(target)] != 0)
                continue;
            const auto at = static_cast<std::size_t>(target) * buses + std::min<std::size_t>(out.bus, buses - 1);
            const auto width = juce::jlimit(1, maximumChannels, out.channels);
            auto& into = routedViews_[at];
            if (routedUsed_[at] == 0) {
                into.setDataToReferTo(routed_[at].getArrayOfWritePointers(), width, count);
                into.setNotClear();
                into.clear();
                routedUsed_[at] = 1;
            }
            send(from.data(), sourceChannels, into.getArrayOfWritePointers(), into.getNumChannels(), out.gain, spread);
        }
    };
    if (plan != nullptr)
        for (const auto& entry : plan->entries)
            if (const auto index = indexOf(entry.source); index >= 0 && done_[static_cast<std::size_t>(index)] == 0)
                render(index, entry.receiver, entry.fader, &entry.outputs, entry.channels);
    // What the routing does not name (the simulator, a track just added).
    for (auto index = std::size_t {}; index < inputs_.size(); ++index)
        if (done_[index] == 0)
            render(static_cast<int>(index), nullptr, nullptr, nullptr, 2);
    directSamples_ = count;
}

MasterOutputAudioSource::MasterOutputAudioSource(juce::AudioSource& input)
    : input_(input),
      analysisFrames_(std::make_unique<cydaw::realtime::SpscRing<
          cydaw::engine::AudioAnalysisFrame, 8>>())
{
    writerThread_.startThread(juce::Thread::Priority::normal);
}

MasterOutputAudioSource::~MasterOutputAudioSource()
{
    stopCapture();
    writerThread_.stopThread(3'000);
}

juce::Result MasterOutputAudioSource::startCapture(
    const juce::File& destination,
    const double durationSeconds,
    const int skipSamples)
{
    stopCapture();
    const auto sampleRate = sampleRate_.load(std::memory_order_acquire);
    if (sampleRate <= 0.0)
        return juce::Result::fail(tr("error.masterExportDevice"));
    if (durationSeconds <= 0.0)
        return juce::Result::fail(tr("error.masterExportEmpty"));

    auto fileOutput = destination.createOutputStream();
    if (fileOutput == nullptr
        || !fileOutput->setPosition(0)
        || fileOutput->truncate().failed())
        return juce::Result::fail(tr("error.masterExportCreate", "file",
                                     destination.getFullPathName()));

    std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(
        output, juce::AudioFormatWriterOptions()
                    .withSampleRate(sampleRate)
                    .withNumChannels(2)
                    .withBitsPerSample(24));
    if (writer == nullptr)
        return juce::Result::fail(tr("error.masterExportCreate", "file",
                                     destination.getFullPathName()));

    targetSamples_.store(std::max<juce::int64>(
                             1, static_cast<juce::int64>(
                                    std::llround(durationSeconds * sampleRate))),
                         std::memory_order_release);
    capturedSamples_.store(0, std::memory_order_release);
    skipSamples_.store(std::max(0, skipSamples), std::memory_order_release);
    captureComplete_.store(false, std::memory_order_release);
    droppedBlocks_.store(0, std::memory_order_relaxed);
    threadedWriter_ = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        writer.release(), writerThread_, static_cast<int>(sampleRate * 4.0));
    activeWriter_.store(threadedWriter_.get(), std::memory_order_release);
    return juce::Result::ok();
}

void MasterOutputAudioSource::stopCapture()
{
    activeWriter_.store(nullptr, std::memory_order_release);
    while (callbacksInFlight_.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    threadedWriter_.reset();
}

bool MasterOutputAudioSource::isCapturing() const noexcept
{
    return activeWriter_.load(std::memory_order_acquire) != nullptr;
}

bool MasterOutputAudioSource::captureComplete() const noexcept
{
    return captureComplete_.load(std::memory_order_acquire);
}

double MasterOutputAudioSource::captureProgress() const noexcept
{
    const auto target = targetSamples_.load(std::memory_order_acquire);
    if (target <= 0)
        return 0.0;
    return juce::jlimit(
        0.0, 1.0,
        static_cast<double>(capturedSamples_.load(std::memory_order_acquire))
            / static_cast<double>(target));
}

int MasterOutputAudioSource::droppedBlockCount() const noexcept
{
    return droppedBlocks_.load(std::memory_order_relaxed);
}

bool MasterOutputAudioSource::tryPopAnalysisFrame(
    cydaw::engine::AudioAnalysisFrame& frame) noexcept
{
    return analysisFrames_->tryPop(frame);
}

void MasterOutputAudioSource::setGain(const float linearGain) noexcept
{
    gain_.store(juce::jlimit(0.0f, 4.0f, linearGain),
                std::memory_order_release);
}

void MasterOutputAudioSource::publishAnalysisFrame(
    const juce::AudioSourceChannelInfo& bufferToFill) noexcept
{
    cydaw::engine::AudioAnalysisFrame frame;
    frame.sequence = ++analysisSequence_;
    frame.sampleRate = static_cast<float>(
        sampleRate_.load(std::memory_order_relaxed));
    if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0) {
        (void) analysisFrames_->tryPush(frame);
        return;
    }

    const auto channels = std::min(
        static_cast<int>(frame.maximumChannels),
        bufferToFill.buffer->getNumChannels());
    frame.channelCount = static_cast<std::uint32_t>(channels);
    for (int channel = 0; channel < channels; ++channel) {
        const auto* samples = bufferToFill.buffer->getReadPointer(
            channel, bufferToFill.startSample);
        double sumSquares {};
        float peak {};
        for (int sample = 0; sample < bufferToFill.numSamples; ++sample) {
            const auto value = samples[sample];
            peak = std::max(peak, std::abs(value));
            sumSquares += static_cast<double>(value) * value;
        }
        frame.peak[static_cast<std::size_t>(channel)] = peak;
        frame.rms[static_cast<std::size_t>(channel)] = static_cast<float>(
            std::sqrt(sumSquares
                      / static_cast<double>(bufferToFill.numSamples)));
    }
    if (channels == 1) {
        frame.peak[1] = frame.peak[0];
        frame.rms[1] = frame.rms[0];
    }
    (void) analysisFrames_->tryPush(frame);
}

void MasterOutputAudioSource::prepareToPlay(const int samplesPerBlockExpected,
                                             const double sampleRate)
{
    input_.prepareToPlay(samplesPerBlockExpected, sampleRate);
    sampleRate_.store(sampleRate, std::memory_order_release);
    blockSize_.store(std::max(1, samplesPerBlockExpected), std::memory_order_release);
    chain_.prepare(sampleRate, std::max(1, samplesPerBlockExpected), 2);
}

juce::Result MasterOutputAudioSource::setChainGraph(const cydaw::engine::NativeDspGraph& graph)
{
    const auto rate = sampleRate_.load(std::memory_order_acquire);
    chain_.prepare(rate > 0.0 ? rate : 48'000.0, blockSize_.load(std::memory_order_acquire), 2);
    const auto result = chain_.setGraph(graph);
    chainActive_.store(result.wasOk() && !graph.nodes.empty(), std::memory_order_release);
    return result;
}

juce::Result MasterOutputAudioSource::updateChainParameters(const cydaw::engine::NativeDspGraph& graph)
{
    return chain_.updateParameters(graph);
}

void MasterOutputAudioSource::releaseResources()
{
    stopCapture();
    sampleRate_.store(0.0, std::memory_order_release);
    input_.releaseResources();
}

void MasterOutputAudioSource::getNextAudioBlock(
    const juce::AudioSourceChannelInfo& bufferToFill)
{
    input_.getNextAudioBlock(bufferToFill);
    // Where this block starts on the master's clock (none while stopped:
    // its clip's automation then rests), the clock moved on.
    auto blockStart = std::numeric_limits<double>::quiet_NaN();
    if (const auto rate = sampleRate_.load(std::memory_order_relaxed);
        clockRunning_.load(std::memory_order_acquire) && rate > 0.0 && bufferToFill.numSamples > 0) {
        blockStart = clock_.load(std::memory_order_relaxed);
        clock_.store(blockStart + static_cast<double>(bufferToFill.numSamples) / rate, std::memory_order_relaxed);
    }
    // The master track's devices, then its fader.
    if (bufferToFill.buffer != nullptr && bufferToFill.numSamples > 0 && chainActive_.load(std::memory_order_acquire))
        (void) chain_.process(*bufferToFill.buffer, bufferToFill.startSample, bufferToFill.numSamples, blockStart);
    if (bufferToFill.buffer != nullptr && bufferToFill.numSamples > 0)
        bufferToFill.buffer->applyGain(
            bufferToFill.startSample, bufferToFill.numSamples,
            gain_.load(std::memory_order_relaxed));
    publishAnalysisFrame(bufferToFill);
    if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0
        || bufferToFill.buffer->getNumChannels() <= 0
        || captureComplete_.load(std::memory_order_acquire))
        return;

    for (;;) {
        auto* writer = activeWriter_.load(std::memory_order_acquire);
        if (writer == nullptr)
            return;

        callbacksInFlight_.fetch_add(1, std::memory_order_acq_rel);
        if (writer != activeWriter_.load(std::memory_order_acquire)) {
            callbacksInFlight_.fetch_sub(1, std::memory_order_release);
            continue;
        }

        const auto captured = capturedSamples_.load(std::memory_order_relaxed);
        const auto remaining = targetSamples_.load(std::memory_order_acquire)
                               - captured;
        if (remaining <= 0) {
            captureComplete_.store(true, std::memory_order_release);
            callbacksInFlight_.fetch_sub(1, std::memory_order_release);
            return;
        }

        // The mix's latency first: what comes out before the song does.
        auto offset = 0;
        if (const auto skip = skipSamples_.load(std::memory_order_acquire); skip > 0) {
            offset = static_cast<int>(std::min<juce::int64>(skip, bufferToFill.numSamples));
            skipSamples_.store(skip - offset, std::memory_order_release);
        }
        if (offset >= bufferToFill.numSamples) {
            callbacksInFlight_.fetch_sub(1, std::memory_order_release);
            return;
        }
        const auto sampleCount = static_cast<int>(std::min<juce::int64>(
            remaining, bufferToFill.numSamples - offset));
        const auto* left = bufferToFill.buffer->getReadPointer(
            0, bufferToFill.startSample + offset);
        const auto* right = bufferToFill.buffer->getNumChannels() > 1
                                ? bufferToFill.buffer->getReadPointer(
                                      1, bufferToFill.startSample + offset)
                                : left;
        const std::array<const float*, 2> channels { left, right };
        if (!writer->write(channels.data(), sampleCount))
            droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
        const auto total = capturedSamples_.fetch_add(
                               sampleCount, std::memory_order_acq_rel)
                           + sampleCount;
        if (total >= targetSamples_.load(std::memory_order_acquire))
            captureComplete_.store(true, std::memory_order_release);
        callbacksInFlight_.fetch_sub(1, std::memory_order_release);
        return;
    }
}

// A stretch of a file, read at the output's rate (repitched or stretched,
// TimeStretch.h), its channels converted into those it is asked for: a mono
// file spreads on a stereo track, a 5.1 one folds onto it, each as
// ChannelLayout.h says.
namespace {

// The stretcher a mode plays with: raw plays the file at its speed (a
// repitch never sped up), the others their own.
cydaw::engine::StretchAlgorithm algorithmFor(const AudioTrackController::StretchMode mode) noexcept
{
    using Mode = AudioTrackController::StretchMode;
    using Algorithm = cydaw::engine::StretchAlgorithm;
    switch (mode) {
    case Mode::tonal: return Algorithm::tonal;
    case Mode::beats: return Algorithm::beats;
    case Mode::texture: return Algorithm::texture;
    case Mode::complex: return Algorithm::complex;
    default: return Algorithm::repitch;
    }
}

// A sound's pitch lane as the stretcher reads it: its semitones every 5 ms
// of the region from its start (none: no lane, or a mode whose pitch is its
// speed's).
std::shared_ptr<const std::vector<float>> soundPitchCurve(const AudioTrackController::AudioClip& clip, const double seconds)
{
    using Mode = AudioTrackController::StretchMode;
    if (clip.stretchMode == Mode::repitch || clip.stretchMode == Mode::raw)
        return {};
    const auto lane = clip.soundLanes.find(AudioTrackController::soundPitchLane);
    if (lane == clip.soundLanes.end() || lane->second.empty())
        return {};
    constexpr auto step = 0.005;
    const auto count = std::min<std::size_t>(static_cast<std::size_t>(std::ceil(std::max(0.0, seconds) / step)) + 2, 8'000'000);
    auto curve = std::make_shared<std::vector<float>>(count);
    std::size_t hint {};
    for (std::size_t index = 0; index < count; ++index)
        (*curve)[index] = static_cast<float>(laneValueAt(lane->second, static_cast<double>(index) * step, &hint));
    return curve;
}

// A sound's own volume and pan lanes over a block of it (`relative`: the
// block's first frame from its region's start): the volume on every
// channel, the pan as a balance of the first two.
void applySoundLanes(juce::AudioBuffer<float>& buffer, const int channels, const int samples, const juce::int64 relative,
                     const double rate, const AudioTrackController::AudioClip& clip) noexcept
{
    if (clip.soundLanes.empty() || samples <= 0 || rate <= 0.0)
        return;
    const auto volume = clip.soundLanes.find(AudioTrackController::soundVolumeLane);
    const auto pan = clip.soundLanes.find(AudioTrackController::soundPanLane);
    const auto* volumePoints = volume != clip.soundLanes.end() && !volume->second.empty() ? &volume->second : nullptr;
    const auto* panPoints = pan != clip.soundLanes.end() && !pan->second.empty() && channels >= 2 ? &pan->second : nullptr;
    if (volumePoints == nullptr && panPoints == nullptr)
        return;
    std::size_t volumeHint {};
    std::size_t panHint {};
    for (int sample = 0; sample < samples; ++sample) {
        const auto seconds = static_cast<double>(relative + sample) / rate;
        auto left = 1.0f;
        auto right = 1.0f;
        if (volumePoints != nullptr) {
            const auto gain = juce::Decibels::decibelsToGain(
                juce::jlimit(-60.0f, 12.0f, static_cast<float>(laneValueAt(*volumePoints, seconds, &volumeHint))), -60.0f);
            left *= gain;
            right *= gain;
        }
        if (panPoints != nullptr) {
            const auto balance = juce::jlimit(-1.0f, 1.0f, static_cast<float>(laneValueAt(*panPoints, seconds, &panHint)));
            if (balance > 0.0f)
                left *= 1.0f - balance;
            else
                right *= 1.0f + balance;
        }
        for (int channel = 0; channel < channels; ++channel)
            buffer.getWritePointer(channel)[sample] *= channel == 1 ? right : channel == 0 ? left : 0.5f * (left + right);
    }
}

} // namespace

class SegmentReaderSource final : public juce::PositionableAudioSource {
public:
    explicit SegmentReaderSource(std::unique_ptr<juce::AudioFormatReader> reader)
        : reader_(std::move(reader)),
          sourceLengthSamples_(reader_->lengthInSamples),
          readerSampleRate_(reader_->sampleRate),
          fileChannels_(juce::jlimit(1, cydaw::engine::maximumTrackChannels, static_cast<int>(reader_->numChannels)))
    {
        durationSamples_.store(std::max<juce::int64>(1, sourceLengthSamples_),
                               std::memory_order_relaxed);
    }

    void configure(const juce::int64 offset,
                   const juce::int64 duration,
                   const bool shouldLoop) noexcept
    {
        configureSeconds(
            static_cast<double>(offset) / readerSampleRate_,
            static_cast<double>(duration) / readerSampleRate_,
            static_cast<double>(duration) / readerSampleRate_,
            shouldLoop);
    }

    void configureSeconds(const double sourceOffsetSeconds,
                          const double sourceDurationSeconds,
                          const double timelineDurationSeconds,
                          const bool shouldLoop,
                          const AudioTrackController::StretchMode stretchMode
                              = AudioTrackController::StretchMode::repitch,
                          const double pitchSemitones = 0.0,
                          std::shared_ptr<const std::vector<float>> pitchCurve = {}) noexcept
    {
        pitchCurve_.store(std::move(pitchCurve), std::memory_order_release);
        const auto sourceLengthSeconds = static_cast<double>(
            sourceLengthSamples_) / readerSampleRate_;
        const auto minimumSourceDuration = 1.0 / readerSampleRate_;
        const auto safeOffset = juce::jlimit(
            0.0,
            std::max(0.0, sourceLengthSeconds - minimumSourceDuration),
            sourceOffsetSeconds);
        const auto safeSourceDuration = juce::jlimit(
            minimumSourceDuration,
            std::max(minimumSourceDuration,
                     sourceLengthSeconds - safeOffset),
            sourceDurationSeconds);
        const auto safeTimelineDuration = juce::jlimit(
            0.001, 86'400.0, timelineDurationSeconds);
        sourceOffsetSeconds_.store(safeOffset,
                                   std::memory_order_release);
        sourceDurationSeconds_.store(safeSourceDuration,
                                     std::memory_order_release);
        timelineDurationSeconds_.store(safeTimelineDuration,
                                       std::memory_order_release);
        stretchMode_.store(static_cast<int>(stretchMode),
                           std::memory_order_release);
        pitchSemitones_.store(juce::jlimit(-48.0, 48.0, pitchSemitones), std::memory_order_release);
        const auto outputSampleRate = std::max(
            1.0, outputSampleRate_.load(std::memory_order_relaxed));
        durationSamples_.store(
            std::max<juce::int64>(
                1, static_cast<juce::int64>(std::llround(
                       safeTimelineDuration * outputSampleRate))),
            std::memory_order_release);
        looping_.store(shouldLoop, std::memory_order_release);
        nextReadPosition_.store(0, std::memory_order_release);
        samplesReadAhead_.store(0, std::memory_order_release);
    }

    [[nodiscard]] bool hasReadAhead(const int minimumSamples) const noexcept
    {
        return samplesReadAhead_.load(std::memory_order_acquire)
               >= std::max(1, minimumSamples);
    }

    void prepareToPlay(const int samplesPerBlockExpected,
                       const double outputSampleRate) override
    {
        const auto safeOutputSampleRate = outputSampleRate > 0.0
                                              ? outputSampleRate
                                              : readerSampleRate_;
        outputSampleRate_.store(safeOutputSampleRate,
                                std::memory_order_release);
        const auto timelineDuration = std::max(
            0.001, timelineDurationSeconds_.load(
                       std::memory_order_acquire));
        durationSamples_.store(
            std::max<juce::int64>(
                1, static_cast<juce::int64>(std::llround(
                       timelineDuration * safeOutputSampleRate))),
            std::memory_order_release);
        rendered_.setSize(fileChannels_, std::max(4'096, samplesPerBlockExpected), false, false, true);
        sourceScratch_.setSize(fileChannels_, 8'192, false, false, true);
    }
    void releaseResources() override
    {
        sourceScratch_.setSize(0, 0);
        rendered_.setSize(0, 0);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& output) override
    {
        output.clearActiveBufferRegion();
        if (output.buffer == nullptr || output.numSamples <= 0)
            return;
        const auto duration = durationSamples_.load(std::memory_order_acquire);
        const auto shouldLoop = looping_.load(std::memory_order_acquire);
        // The stretcher follows the settings (made again when they change).
        cydaw::engine::StretchSettings settings;
        const auto mode = static_cast<AudioTrackController::StretchMode>(stretchMode_.load(std::memory_order_acquire));
        settings.algorithm = algorithmFor(mode);
        settings.sourceRate = readerSampleRate_;
        settings.outputRate = std::max(1.0, outputSampleRate_.load(std::memory_order_acquire));
        settings.sourceStart = sourceOffsetSeconds_.load(std::memory_order_acquire) * readerSampleRate_;
        // Raw: the file at its own speed, whatever the length.
        settings.speed = mode == AudioTrackController::StretchMode::raw
                             ? 1.0
                             : sourceDurationSeconds_.load(std::memory_order_acquire)
                                   / std::max(0.001, timelineDurationSeconds_.load(std::memory_order_acquire));
        settings.pitch = std::pow(2.0, pitchSemitones_.load(std::memory_order_acquire) / 12.0);
        settings.pitchCurve = pitchCurve_.load(std::memory_order_acquire);
        settings.channels = fileChannels_;
        if (!stretcherReady_ || !(settings == stretcherSettings_)) {
            stretcher_.prepare(settings);
            stretcherSettings_ = settings;
            stretcherReady_ = true;
        }
        if (rendered_.getNumChannels() < fileChannels_ || rendered_.getNumSamples() < output.numSamples)
            rendered_.setSize(fileChannels_, output.numSamples, false, false, true);
        const auto destinationChannels = std::min(output.buffer->getNumChannels(),
                                                  cydaw::engine::maximumTrackChannels);
        const auto matrix = cydaw::engine::channelConversion(fileChannels_, destinationChannels);
        const auto direct = fileChannels_ == destinationChannels;
        auto position = nextReadPosition_.load(std::memory_order_relaxed);
        auto written = 0;
        while (written < output.numSamples) {
            if (position >= duration) {
                if (!shouldLoop)
                    break;
                position %= duration;
            }
            const auto count = static_cast<int>(std::min<juce::int64>(
                output.numSamples - written, duration - position));
            if (count <= 0)
                break;
            stretcher_.render(fileSource_, position, count, rendered_.getArrayOfWritePointers());
            for (int sample = 0; sample < count; ++sample) {
                std::array<float, cydaw::engine::maximumTrackChannels> frame {};
                for (int channel = 0; channel < fileChannels_; ++channel)
                    frame[static_cast<std::size_t>(channel)] = rendered_.getSample(channel, sample);
                writeFrame(output, output.startSample + written + sample, frame, destinationChannels, matrix, direct);
            }
            written += count;
            position += count;
        }
        nextReadPosition_.store(position, std::memory_order_relaxed);
        samplesReadAhead_.fetch_add(output.numSamples, std::memory_order_release);
    }

    void setNextReadPosition(const juce::int64 position) override
    {
        nextReadPosition_.store(std::max<juce::int64>(0, position),
                                std::memory_order_release);
    }
    [[nodiscard]] juce::int64 getNextReadPosition() const override
    {
        return nextReadPosition_.load(std::memory_order_acquire);
    }
    [[nodiscard]] juce::int64 getTotalLength() const override
    {
        return durationSamples_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool isLooping() const override
    {
        return looping_.load(std::memory_order_acquire);
    }
    void setLooping(const bool shouldLoop) override
    {
        looping_.store(shouldLoop, std::memory_order_release);
    }

private:
    // The file as the stretcher reads it: silence before its start and
    // after its end.
    struct FileSource final : cydaw::engine::StretchSource {
        explicit FileSource(SegmentReaderSource& owner) : owner_(owner) {}
        void read(const std::int64_t start, const int count, float* const* channels, const int channelCount) override
        {
            auto& scratch = owner_.sourceScratch_;
            if (scratch.getNumChannels() < owner_.fileChannels_ || scratch.getNumSamples() < count)
                scratch.setSize(owner_.fileChannels_, count, false, false, true);
            owner_.reader_->read(&scratch, 0, count, start, true, true);
            for (int channel = 0; channel < channelCount; ++channel)
                std::copy_n(scratch.getReadPointer(std::min(channel, owner_.fileChannels_ - 1)), count, channels[channel]);
        }
        SegmentReaderSource& owner_;
    };

    // One frame of the file's channels, in the output's.
    static void writeFrame(const juce::AudioSourceChannelInfo& output, const int at,
                           const std::array<float, cydaw::engine::maximumTrackChannels>& frame,
                           const int destinationChannels, const cydaw::engine::ChannelMatrix& matrix,
                           const bool direct) noexcept
    {
        for (int channel = 0; channel < destinationChannels; ++channel) {
            auto value = 0.0f;
            if (direct) {
                value = frame[static_cast<std::size_t>(channel)];
            } else {
                for (int source = 0; source < matrix.from; ++source)
                    value += matrix.gain(channel, source) * frame[static_cast<std::size_t>(source)];
            }
            output.buffer->setSample(channel, at, value);
        }
    }

    std::unique_ptr<juce::AudioFormatReader> reader_;
    const juce::int64 sourceLengthSamples_ {};
    const double readerSampleRate_ {};
    const int fileChannels_ { 2 };
    std::atomic<double> outputSampleRate_ { 48'000.0 };
    juce::AudioBuffer<float> sourceScratch_;
    juce::AudioBuffer<float> rendered_;
    FileSource fileSource_ { *this };
    cydaw::engine::TimeStretcher stretcher_;
    cydaw::engine::StretchSettings stretcherSettings_;
    bool stretcherReady_ {};
    std::atomic<double> sourceOffsetSeconds_ {};
    std::atomic<double> sourceDurationSeconds_ { 1.0 };
    std::atomic<double> timelineDurationSeconds_ { 1.0 };
    std::atomic<int> stretchMode_ {
        static_cast<int>(AudioTrackController::StretchMode::repitch)
    };
    std::atomic<double> pitchSemitones_ {};
    // A sound's pitch lane, its semitones every 5 ms (soundPitchCurve).
    std::atomic<std::shared_ptr<const std::vector<float>>> pitchCurve_;
    std::atomic<juce::int64> durationSamples_ { 1 };
    std::atomic<juce::int64> nextReadPosition_ {};
    std::atomic<juce::int64> samplesReadAhead_ {};
    std::atomic<bool> looping_ {};
};

struct PreparedArrangementRegion {
    AudioTrackController::AudioClip clip;
    std::unique_ptr<juce::AudioFormatReader> reader;
};

class EmptyAudioFormatReader final : public juce::AudioFormatReader {
public:
    EmptyAudioFormatReader()
        : juce::AudioFormatReader(nullptr, "CyDAW empty audio track")
    {
        sampleRate = 48'000.0;
        bitsPerSample = 24;
        lengthInSamples = 1;
        numChannels = 2;
        usesFloatingPointData = false;
    }

    bool readSamples(int* const* const destinationSamples,
                     const int destinationChannelCount,
                     const int destinationOffset,
                     juce::int64,
                     const int sampleCount) override
    {
        for (int channel = 0; channel < destinationChannelCount; ++channel)
            if (destinationSamples[channel] != nullptr)
                juce::zeromem(destinationSamples[channel] + destinationOffset,
                              static_cast<std::size_t>(sampleCount)
                                  * sizeof(int));
        return true;
    }
};

class ClipAudioSource final : public juce::PositionableAudioSource {
public:
    // `channels`: its track's format, every deck's and every region's.
    ClipAudioSource(std::unique_ptr<juce::AudioFormatReader> firstReader,
                    std::unique_ptr<juce::AudioFormatReader> secondReader,
                    juce::TimeSliceThread& readAheadThread,
                    const int channels = 2)
        : sourceLengthSamples_(firstReader->lengthInSamples),
          sampleRate_(firstReader->sampleRate),
          readAheadThread_(readAheadThread),
          channels_(cydaw::engine::clampTrackChannels(channels))
    {
        decks_[0] = std::make_unique<Deck>(std::move(firstReader), readAheadThread, channels_);
        decks_[1] = std::make_unique<Deck>(std::move(secondReader), readAheadThread, channels_);
        clipDurationSamples_.store(sourceLengthSamples_, std::memory_order_relaxed);
        arrangementLengthSamples_.store(sourceLengthSamples_,
                                         std::memory_order_relaxed);
        sessionLengthSamples_.store(sourceLengthSamples_,
                                    std::memory_order_relaxed);
        configureArrangementDeck(0);
        configureArrangementDeck(1);
    }

    [[nodiscard]] juce::Result setArrangementRegions(
        std::vector<PreparedArrangementRegion> regions);
    [[nodiscard]] juce::Result setLauncherSceneSource(
        int sceneIndex,
        std::unique_ptr<juce::AudioFormatReader> reader,
        double sourceOffsetSeconds,
        double durationSeconds,
        bool looping);
    [[nodiscard]] juce::Result setLauncherSceneRegions(
        int sceneIndex,
        std::vector<PreparedArrangementRegion> regions,
        double durationSeconds, double playStartSeconds,
        double playEndSeconds, double loopStartSeconds,
        double loopEndSeconds, bool looping);
    void clearLauncherSceneSource(int sceneIndex);
    [[nodiscard]] bool moveArrangementRegion(
        std::uint64_t regionId, double startSeconds) noexcept;
    [[nodiscard]] bool arrangementHasReadAhead() const noexcept
    {
        if (arrangementPlan_ == nullptr || arrangementPlan_->regions.empty())
            return false;
        const auto samples = blockSize_.load(std::memory_order_acquire);
        return std::all_of(
            arrangementPlan_->regions.begin(), arrangementPlan_->regions.end(),
            [samples](const auto& region) {
                return region->deck.ready(samples);
            });
    }

    void setClipRange(const double startSeconds,
                      const double sourceOffsetSeconds,
                      const double durationSeconds) noexcept
    {
        const auto offset = juce::jlimit<juce::int64>(
            0, sourceLengthSamples_, secondsToSamples(sourceOffsetSeconds));
        const auto available = std::max<juce::int64>(1, sourceLengthSamples_ - offset);
        const auto duration = juce::jlimit<juce::int64>(
            1, available, secondsToSamples(durationSeconds));
        clipStartSamples_.store(secondsToSamples(startSeconds), std::memory_order_release);
        sourceOffsetSamples_.store(offset, std::memory_order_release);
        clipDurationSamples_.store(duration, std::memory_order_release);
        if (!launcherActive_.load(std::memory_order_acquire)
            && pendingScene_.load(std::memory_order_acquire) < 0) {
            configureArrangementDeck(activeDeck_.load(std::memory_order_acquire));
            configureArrangementDeck(1 - activeDeck_.load(std::memory_order_acquire));
        }
    }

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int channelCount() const noexcept { return channels_; }
    [[nodiscard]] double sourceLengthSeconds() const noexcept
    {
        return static_cast<double>(sourceLengthSamples_) / sampleRate_;
    }
    [[nodiscard]] double clipStartSeconds() const noexcept
    {
        return static_cast<double>(clipStartSamples_.load(std::memory_order_acquire))
               / sampleRate_;
    }
    [[nodiscard]] double sourceOffsetSeconds() const noexcept
    {
        return static_cast<double>(sourceOffsetSamples_.load(std::memory_order_acquire))
               / sampleRate_;
    }
    [[nodiscard]] double clipDurationSeconds() const noexcept
    {
        return static_cast<double>(clipDurationSamples_.load(std::memory_order_acquire))
               / sampleRate_;
    }
    [[nodiscard]] double arrangementLengthSeconds() const noexcept
    {
        return static_cast<double>(
                   arrangementLengthSamples_.load(std::memory_order_acquire))
               / sampleRate_;
    }
    void setTransportRange(const double sessionLengthSeconds,
                           const double loopStartSeconds,
                           const double loopEndSeconds) noexcept
    {
        const auto contentLength = arrangementLengthSamples_.load(
            std::memory_order_acquire);
        const auto sessionLength = std::max<juce::int64>(
            1, std::max(contentLength, secondsToSamples(sessionLengthSeconds)));
        const auto loopStart = juce::jlimit<juce::int64>(
            0, std::max<juce::int64>(0, sessionLength - 1),
            secondsToSamples(loopStartSeconds));
        const auto loopEnd = juce::jlimit<juce::int64>(
            loopStart + 1, sessionLength,
            secondsToSamples(loopEndSeconds));
        sessionLengthSamples_.store(sessionLength, std::memory_order_release);
        loopStartSamples_.store(loopStart, std::memory_order_release);
        loopEndSamples_.store(loopEnd, std::memory_order_release);
    }
    [[nodiscard]] bool usesDoubleDeckReadAhead() const noexcept { return true; }
    [[nodiscard]] bool pendingDeckHasReadAhead() const noexcept
    {
        const auto kind = pendingKind_.load(std::memory_order_acquire);
        if (kind != requestLauncher)
            return false;
        const auto scene = pendingScene_.load(std::memory_order_acquire);
        const auto planState = const_cast<ClipAudioSource*>(this)
                                   ->launcherPlanState(scene);
        if (planState != 0)
            return planState == 2;
        return decks_[static_cast<std::size_t>(
            pendingDeck_.load(std::memory_order_acquire))]->ready(
                blockSize_.load(std::memory_order_acquire));
    }

    void scheduleLauncherClip(const int sceneIndex,
                              const double sourceOffsetSeconds,
                              const double durationSeconds,
                              const bool shouldLoop,
                              const double delaySeconds,
                              const double localStartSeconds = 0.0) noexcept
    {
        const auto offset = juce::jlimit<juce::int64>(
            0, sourceLengthSamples_, secondsToSamples(sourceOffsetSeconds));
        const auto available = std::max<juce::int64>(1, sourceLengthSamples_ - offset);
        const auto planDuration = launcherPlanDurationSamples(sceneIndex);
        const auto duration = planDuration > 0
                                  ? planDuration
                                  : juce::jlimit<juce::int64>(
                                        1, available,
                                        secondsToSamples(std::max(
                                            1.0 / sampleRate_,
                                            durationSeconds)));
        const auto delay = secondsToSamples(delaySeconds);
        publishDeckRequest(sceneIndex, true, offset, duration, shouldLoop,
                           delay, secondsToSamples(localStartSeconds));
    }

    void scheduleLauncherStop(const double delaySeconds) noexcept
    {
        const auto writingGeneration = pendingGeneration_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        pendingKind_.store(requestStop, std::memory_order_relaxed);
        pendingDurationSamples_.store(0, std::memory_order_relaxed);
        pendingDelaySamples_.store(secondsToSamples(delaySeconds),
                                   std::memory_order_relaxed);
        pendingLooping_.store(false, std::memory_order_relaxed);
        pendingScene_.store(-2, std::memory_order_release);
        pendingGeneration_.store(writingGeneration + 1,
                                 std::memory_order_release);
    }

    void clearLauncherClip() noexcept
    {
        launcherStopped_.store(false, std::memory_order_release);
        const auto hadLauncher = launcherActive_.load(std::memory_order_acquire)
                                 || pendingScene_.load(std::memory_order_acquire) != -1;
        if (!hadLauncher)
            return;
        const auto writingGeneration = pendingGeneration_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        pendingKind_.store(requestNone, std::memory_order_relaxed);
        pendingScene_.store(-1, std::memory_order_release);
        pendingGeneration_.store(writingGeneration + 1, std::memory_order_release);
        launcherActive_.store(false, std::memory_order_release);
        activeLauncherUsesPlan_.store(false, std::memory_order_release);
        activeScene_.store(-1, std::memory_order_release);
        nextReadPosition_.store(0, std::memory_order_release);
        if (arrangementPlan_ == nullptr)
            configureArrangementDeck(activeDeck_.load(std::memory_order_acquire));
    }

    [[nodiscard]] int activeLauncherScene() const noexcept
    {
        return activeScene_.load(std::memory_order_acquire);
    }
    [[nodiscard]] int pendingLauncherScene() const noexcept
    {
        return pendingScene_.load(std::memory_order_acquire);
    }
    [[nodiscard]] float launcherProgress() const noexcept
    {
        if (!launcherActive_.load(std::memory_order_acquire))
            return 0.0f;
        const auto duration = std::max<juce::int64>(
            1, launcherDurationSamples_.load(std::memory_order_acquire));
        auto position = nextReadPosition_.load(std::memory_order_acquire);
        if (launcherLooping_.load(std::memory_order_acquire))
            position %= duration;
        return juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(position) / static_cast<float>(duration));
    }
    [[nodiscard]] double launcherPositionSeconds() const noexcept
    {
        return launcherActive_.load(std::memory_order_acquire)
                   ? static_cast<double>(nextReadPosition_.load(
                         std::memory_order_acquire)) / sampleRate_
                   : 0.0;
    }
    void setArrangementPlaybackEnabled(const bool enabled) noexcept
    {
        arrangementPlaybackEnabled_.store(enabled, std::memory_order_release);
    }

    void prepareToPlay(const int samplesPerBlockExpected,
                       const double outputSampleRate) override
    {
        blockSize_.store(std::max(1, samplesPerBlockExpected), std::memory_order_release);
        outputSampleRate_.store(outputSampleRate, std::memory_order_release);
        // Both decks are prepared here, ahead of any launch: a buffering
        // source's prepare sleeps five milliseconds, which is ten a track at
        // load, but a deck prepared only when a clip is launched onto it is
        // not sample-exact at the switch — its read-ahead has not filled.
        for (auto& deck : decks_)
            deck->prepare(samplesPerBlockExpected, outputSampleRate);
        for (auto& plan : launcherPlans_)
            if (plan != nullptr)
                plan->prepare(samplesPerBlockExpected, outputSampleRate);
        arrangementScratch_.setSize(channels_, std::max(1, samplesPerBlockExpected),
                                    false, false, true);
        if (arrangementPlan_ != nullptr)
            arrangementPlan_->prepare(samplesPerBlockExpected, outputSampleRate);
        prepared_.store(true, std::memory_order_release);
    }

    void releaseResources() override
    {
        prepared_.store(false, std::memory_order_release);
        for (auto& deck : decks_)
            deck->release();
        for (auto& plan : launcherPlans_)
            if (plan != nullptr)
                plan->release();
        if (arrangementPlan_ != nullptr)
            arrangementPlan_->release();
        arrangementScratch_.setSize(0, 0);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& output) override
    {
        output.clearActiveBufferRegion();
        if (output.buffer == nullptr || output.numSamples <= 0)
            return;

        refreshPendingRequest();
        if (scheduledKind_ == requestNone) {
            renderCurrent(output, 0, output.numSamples);
            return;
        }
        if (scheduledDelaySamples_ > output.numSamples) {
            renderCurrent(output, 0, output.numSamples);
            scheduledDelaySamples_ -= output.numSamples;
            return;
        }

        const auto prefixSamples = static_cast<int>(std::max<juce::int64>(
            0, std::min<juce::int64>(scheduledDelaySamples_, output.numSamples)));
        renderCurrent(output, 0, prefixSamples);
        refreshPendingRequest();
        if (scheduledKind_ != requestNone && !activateScheduledDeck()) {
            renderCurrent(output, prefixSamples, output.numSamples - prefixSamples);
            scheduledDelaySamples_ = 0;
            return;
        }
        renderCurrent(output, prefixSamples, output.numSamples - prefixSamples);
    }

    void setNextReadPosition(const juce::int64 position) override
    {
        const auto safePosition = std::max<juce::int64>(0, position);
        nextReadPosition_.store(safePosition, std::memory_order_release);
        if (!prepared_.load(std::memory_order_acquire))
            return;
        const auto launcherActive = launcherActive_.load(std::memory_order_acquire);
        if (!launcherActive && arrangementPlan_ != nullptr)
            return;
        const auto clipStart = launcherActive
                                   ? juce::int64 {}
                                   : clipStartSamples_.load(std::memory_order_acquire);
        const auto clipDuration = launcherActive
                                      ? launcherDurationSamples_.load(std::memory_order_acquire)
                                      : clipDurationSamples_.load(std::memory_order_acquire);
        const auto relative = juce::jlimit<juce::int64>(
            0, clipDuration, safePosition - std::min(safePosition, clipStart));
        if (launcherActive
            && activeLauncherUsesPlan_.load(std::memory_order_acquire)) {
            setLauncherPlanPosition(
                activeScene_.load(std::memory_order_acquire), relative);
            return;
        }
        decks_[activeDeck_.load(std::memory_order_acquire)]->setPosition(relative);
    }
    [[nodiscard]] juce::int64 getNextReadPosition() const override
    {
        return nextReadPosition_.load(std::memory_order_acquire);
    }
    [[nodiscard]] juce::int64 getTotalLength() const override
    {
        const auto currentLength = launcherActive_.load(std::memory_order_acquire)
                                       ? launcherDurationSamples_.load(std::memory_order_acquire)
                                       : sessionLengthSamples_.load(
                                             std::memory_order_acquire);
        if (pendingScene_.load(std::memory_order_acquire) < 0)
            return currentLength;
        return currentLength
               + pendingDelaySamples_.load(std::memory_order_relaxed)
               + pendingDurationSamples_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool isLooping() const override
    {
        return launcherActive_.load(std::memory_order_acquire)
                   ? launcherLooping_.load(std::memory_order_relaxed)
                   : looping_.load(std::memory_order_relaxed);
    }
    void setLooping(const bool shouldLoop) override
    {
        looping_.store(shouldLoop, std::memory_order_release);
    }

private:
    struct Deck {
        Deck(std::unique_ptr<juce::AudioFormatReader> reader,
             juce::TimeSliceThread& thread,
             const int channels,
             const int readAheadSamples = 131'072)
        {
            segment = std::make_unique<SegmentReaderSource>(std::move(reader));
            buffer = std::make_unique<juce::BufferingAudioSource>(
                segment.get(), thread, false, readAheadSamples, channels, false);
        }

        void configure(const juce::int64 offset,
                       const juce::int64 duration,
                       const bool looping,
                       const bool prepared,
                       const int blockSize,
                       const double sampleRate)
        {
            if (prepared)
                buffer->releaseResources();
            segment->configure(offset, duration, looping);
            buffer->setNextReadPosition(0);
            if (prepared)
                buffer->prepareToPlay(blockSize, sampleRate);
        }

        void configureSeconds(const double sourceOffsetSeconds,
                              const double sourceDurationSeconds,
                              const double timelineDurationSeconds,
                              const bool looping,
                              const bool prepared,
                              const int blockSize,
                              const double sampleRate,
                              const AudioTrackController::StretchMode stretchMode
                                  = AudioTrackController::StretchMode::repitch,
                              const double pitchSemitones = 0.0,
                              std::shared_ptr<const std::vector<float>> pitchCurve = {})
        {
            if (prepared)
                buffer->releaseResources();
            segment->configureSeconds(sourceOffsetSeconds,
                                      sourceDurationSeconds,
                                      timelineDurationSeconds,
                                      looping, stretchMode, pitchSemitones, std::move(pitchCurve));
            buffer->setNextReadPosition(0);
            if (prepared)
                buffer->prepareToPlay(blockSize, sampleRate);
        }

        void prepare(const int blockSize, const double sampleRate)
        {
            buffer->prepareToPlay(blockSize, sampleRate);
        }
        void release() { buffer->releaseResources(); }
        void setPosition(const juce::int64 position)
        {
            buffer->setNextReadPosition(position);
        }
        void setLooping(const bool shouldLoop)
        {
            segment->setLooping(shouldLoop);
        }
        [[nodiscard]] juce::int64 position() const
        {
            return buffer->getNextReadPosition();
        }
        [[nodiscard]] bool ready(const int samples) const noexcept
        {
            return segment->hasReadAhead(samples);
        }
        void render(const juce::AudioSourceChannelInfo& output)
        {
            buffer->getNextAudioBlock(output);
        }

        std::unique_ptr<SegmentReaderSource> segment;
        std::unique_ptr<juce::BufferingAudioSource> buffer;
    };

    struct ArrangementRegion {
        ArrangementRegion(AudioTrackController::AudioClip value,
                          std::unique_ptr<juce::AudioFormatReader> reader,
                          juce::TimeSliceThread& thread,
                          const juce::int64 start,
                          const double sourceOffsetSeconds,
                          const double sourceDurationSeconds,
                          const double timelineDurationSeconds,
                          const juce::int64 duration,
                          const juce::int64 fadeIn,
                          const juce::int64 fadeOut,
                          const bool prepared,
                          const int blockSize,
                          const double sampleRate,
                          const int channels)
            : clip(std::move(value)), startSamples(start),
              durationSamples(duration), fadeInSamples(fadeIn),
              fadeOutSamples(fadeOut), deck(std::move(reader), thread, channels, 32'768)
        {
            deck.configureSeconds(sourceOffsetSeconds,
                                  sourceDurationSeconds,
                                  timelineDurationSeconds,
                                  false, false, blockSize, sampleRate,
                                  clip.stretchMode, clip.pitchSemitones,
                                  soundPitchCurve(clip, timelineDurationSeconds));
            if (prepared)
                deck.prepare(blockSize, sampleRate);
        }

        AudioTrackController::AudioClip clip;
        std::atomic<juce::int64> startSamples {};
        juce::int64 durationSamples { 1 };
        juce::int64 fadeInSamples {};
        juce::int64 fadeOutSamples {};
        Deck deck;
    };

    struct ArrangementPlan {
        void prepare(const int blockSize, const double sampleRate)
        {
            if (prepared)
                return;
            for (auto& region : regions)
                region->deck.prepare(blockSize, sampleRate);
            prepared = true;
        }

        void release()
        {
            if (!prepared)
                return;
            for (auto& region : regions)
                region->deck.release();
            prepared = false;
        }

        std::vector<std::unique_ptr<ArrangementRegion>> regions;
        bool prepared {};
    };

    struct LauncherPlan {
        void prepare(const int blockSize, const double sampleRate)
        {
            if (prepared)
                return;
            for (auto& region : regions)
                region->deck.prepare(blockSize, sampleRate);
            prepared = true;
        }

        void release()
        {
            if (!prepared)
                return;
            for (auto& region : regions)
                region->deck.release();
            prepared = false;
        }

        juce::int64 durationSamples { 1 };
        juce::int64 playStartSamples {};
        juce::int64 playEndSamples { 1 };
        juce::int64 loopStartSamples {};
        juce::int64 loopEndSamples { 1 };
        bool looping { true };
        bool prepared {};
        std::vector<std::unique_ptr<ArrangementRegion>> regions;
    };

    static constexpr int requestNone = 0;
    static constexpr int requestLauncher = 1;
    static constexpr int requestArrangement = 2;
    static constexpr int requestStop = 3;

    [[nodiscard]] juce::int64 secondsToSamples(const double seconds) const noexcept
    {
        return static_cast<juce::int64>(std::llround(
            std::max(0.0, seconds) * sampleRate_));
    }

    void configureArrangementDeck(const int deckIndex) noexcept
    {
        decks_[static_cast<std::size_t>(deckIndex)]->configure(
            sourceOffsetSamples_.load(std::memory_order_acquire),
            clipDurationSamples_.load(std::memory_order_acquire),
            looping_.load(std::memory_order_acquire),
            prepared_.load(std::memory_order_acquire),
            blockSize_.load(std::memory_order_acquire),
            outputSampleRate_.load(std::memory_order_acquire));
    }

    [[nodiscard]] int launcherPlanState(int sceneIndex) noexcept;
    [[nodiscard]] juce::int64 launcherPlanDurationSamples(
        int sceneIndex) noexcept;
    void setLauncherPlanPosition(int sceneIndex,
                                 juce::int64 position) noexcept;
    void renderLauncherPlan(const juce::AudioSourceChannelInfo& output,
                            int outputOffset,
                            int sampleCount) noexcept;

    void publishDeckRequest(const int sceneIndex,
                            const bool launcher,
                            const juce::int64 offset,
                            const juce::int64 duration,
                            const bool shouldLoop,
                            const juce::int64 delay,
                            const juce::int64 start = 0) noexcept
    {
        const auto writingGeneration = pendingGeneration_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        const auto standbyDeck = 1 - activeDeck_.load(std::memory_order_acquire);
        if (!launcher || launcherPlanState(sceneIndex) == 0) {
            decks_[static_cast<std::size_t>(standbyDeck)]->configure(
                offset, duration, shouldLoop,
                prepared_.load(std::memory_order_acquire),
                blockSize_.load(std::memory_order_acquire),
                outputSampleRate_.load(std::memory_order_acquire));
        }
        pendingDeck_.store(standbyDeck, std::memory_order_relaxed);
        pendingKind_.store(launcher ? requestLauncher : requestArrangement,
                           std::memory_order_relaxed);
        pendingDurationSamples_.store(duration, std::memory_order_relaxed);
        pendingDelaySamples_.store(delay, std::memory_order_relaxed);
        pendingStartSamples_.store(start, std::memory_order_relaxed);
        pendingLooping_.store(shouldLoop, std::memory_order_relaxed);
        pendingScene_.store(launcher ? sceneIndex : -1, std::memory_order_release);
        pendingGeneration_.store(writingGeneration + 1, std::memory_order_release);
    }

    void refreshPendingRequest() noexcept
    {
        for (int attempt = 0; attempt < 2; ++attempt) {
            const auto generation = pendingGeneration_.load(std::memory_order_acquire);
            if ((generation & 1U) != 0U || generation == scheduledGeneration_)
                return;
            const auto kind = pendingKind_.load(std::memory_order_relaxed);
            const auto deck = pendingDeck_.load(std::memory_order_relaxed);
            const auto scene = pendingScene_.load(std::memory_order_acquire);
            const auto duration = pendingDurationSamples_.load(std::memory_order_relaxed);
            const auto delay = pendingDelaySamples_.load(std::memory_order_relaxed);
            const auto start = pendingStartSamples_.load(
                std::memory_order_relaxed);
            const auto shouldLoop = pendingLooping_.load(std::memory_order_relaxed);
            if (pendingGeneration_.load(std::memory_order_acquire) != generation)
                continue;
            scheduledGeneration_ = generation;
            scheduledKind_ = kind;
            scheduledDeck_ = deck;
            scheduledScene_ = scene;
            scheduledDurationSamples_ = duration;
            scheduledDelaySamples_ = delay;
            scheduledStartSamples_ = start;
            scheduledLooping_ = shouldLoop;
            return;
        }
    }

    [[nodiscard]] bool activateScheduledDeck() noexcept
    {
        if (scheduledKind_ == requestStop) {
            if (pendingGeneration_.load(std::memory_order_acquire)
                    != scheduledGeneration_)
                return false;
            launcherActive_.store(false, std::memory_order_release);
            activeLauncherUsesPlan_.store(false, std::memory_order_release);
            launcherStopped_.store(true, std::memory_order_release);
            activeScene_.store(-1, std::memory_order_release);
            pendingScene_.store(-1, std::memory_order_release);
            pendingKind_.store(requestNone, std::memory_order_release);
            scheduledKind_ = requestNone;
            return true;
        }
        const auto planState = scheduledKind_ == requestLauncher
                                   ? launcherPlanState(scheduledScene_)
                                   : 0;
        if (scheduledKind_ == requestNone
            || pendingGeneration_.load(std::memory_order_acquire)
                   != scheduledGeneration_)
            return false;
        if ((planState != 0 && planState != 2)
            || (planState == 0
                && !decks_[static_cast<std::size_t>(scheduledDeck_)]->ready(
                    blockSize_.load(std::memory_order_relaxed))))
            return false;
        activeDeck_.store(scheduledDeck_, std::memory_order_release);
        auto initialPosition = scheduledStartSamples_;
        if (planState == 2 && scheduledScene_ >= 0) {
            const auto& plan = launcherPlans_[static_cast<std::size_t>(
                scheduledScene_)];
            if (plan != nullptr)
                initialPosition = juce::jlimit<juce::int64>(
                    plan->playStartSamples,
                    std::max(plan->playStartSamples,
                             plan->playEndSamples - 1),
                    initialPosition <= 0 ? plan->playStartSamples
                                         : initialPosition);
        } else {
            initialPosition = juce::jlimit<juce::int64>(
                0, std::max<juce::int64>(0,
                                         scheduledDurationSamples_ - 1),
                initialPosition);
        }
        nextReadPosition_.store(initialPosition,
                                std::memory_order_relaxed);
        if (scheduledKind_ == requestLauncher) {
            launcherDurationSamples_.store(scheduledDurationSamples_,
                                           std::memory_order_relaxed);
            launcherLooping_.store(scheduledLooping_, std::memory_order_relaxed);
            activeScene_.store(scheduledScene_, std::memory_order_release);
            activeLauncherUsesPlan_.store(planState == 2,
                                          std::memory_order_release);
            if (planState == 2)
                setLauncherPlanPosition(scheduledScene_, initialPosition);
            launcherActive_.store(true, std::memory_order_release);
            launcherStopped_.store(false, std::memory_order_release);
        } else {
            activeScene_.store(-1, std::memory_order_release);
            activeLauncherUsesPlan_.store(false, std::memory_order_release);
            launcherActive_.store(false, std::memory_order_release);
        }
        pendingScene_.store(-1, std::memory_order_release);
        pendingKind_.store(requestNone, std::memory_order_release);
        scheduledKind_ = requestNone;
        return true;
    }

    void renderArrangement(const juce::AudioSourceChannelInfo& output,
                           const int outputOffset,
                           const int sampleCount)
    {
        if (sampleCount <= 0
            || !arrangementPlanAvailable_.load(std::memory_order_acquire))
            return;
        arrangementCallbacksInFlight_.fetch_add(1, std::memory_order_acq_rel);
        if (!arrangementPlanAvailable_.load(std::memory_order_acquire)
            || arrangementPlan_ == nullptr
            || arrangementScratch_.getNumSamples() <= 0) {
            arrangementCallbacksInFlight_.fetch_sub(1, std::memory_order_release);
            return;
        }

        const auto totalLength = sessionLengthSamples_.load(
            std::memory_order_acquire);
        if (totalLength <= 0) {
            arrangementCallbacksInFlight_.fetch_sub(1, std::memory_order_release);
            return;
        }

        auto position = nextReadPosition_.load(std::memory_order_relaxed);
        auto destinationOffset = 0;
        const auto shouldLoop = looping_.load(std::memory_order_relaxed);
        const auto loopStart = juce::jlimit<juce::int64>(
            0, std::max<juce::int64>(0, totalLength - 1),
            loopStartSamples_.load(std::memory_order_relaxed));
        const auto loopEnd = juce::jlimit<juce::int64>(
            loopStart + 1, totalLength,
            loopEndSamples_.load(std::memory_order_relaxed));
        const auto playbackEnd = shouldLoop ? loopEnd : totalLength;
        const auto outputChannels = output.buffer != nullptr
                                        ? output.buffer->getNumChannels()
                                        : 0;
        const auto channelCount = std::min(
            outputChannels, arrangementScratch_.getNumChannels());
        while (destinationOffset < sampleCount) {
            if (position >= playbackEnd) {
                if (!shouldLoop)
                    break;
                const auto loopLength = loopEnd - loopStart;
                position = loopStart
                           + (position - loopStart) % loopLength;
            }
            const auto remaining = sampleCount - destinationOffset;
            const auto segmentSamples = static_cast<int>(std::min<juce::int64>(
                { static_cast<juce::int64>(remaining),
                  playbackEnd - position,
                  arrangementScratch_.getNumSamples() }));
            if (segmentSamples <= 0)
                break;

            for (auto& region : arrangementPlan_->regions) {
                const auto regionStart = region->startSamples.load(
                    std::memory_order_acquire);
                const auto regionEnd = regionStart + region->durationSamples;
                const auto overlapStart = std::max(position, regionStart);
                const auto overlapEnd = std::min(position + segmentSamples,
                                                 regionEnd);
                if (overlapEnd <= overlapStart)
                    continue;
                const auto silenceBefore = static_cast<int>(overlapStart - position);
                const auto samplesToRead = static_cast<int>(overlapEnd - overlapStart);
                const auto relativePosition = overlapStart - regionStart;
                if (region->deck.position() != relativePosition)
                    region->deck.setPosition(relativePosition);
                arrangementScratch_.clear();
                region->deck.render({ &arrangementScratch_, 0, samplesToRead });
                if (region->fadeInSamples > 0
                    || region->fadeOutSamples > 0) {
                    for (int sample = 0; sample < samplesToRead; ++sample) {
                        const auto regionPosition = relativePosition + sample;
                        auto fadeGain = 1.0f;
                        if (region->fadeInSamples > 0
                            && regionPosition < region->fadeInSamples) {
                            const auto denominator = std::max<juce::int64>(
                                1, region->fadeInSamples - 1);
                            fadeGain *= static_cast<float>(regionPosition)
                                        / static_cast<float>(denominator);
                        }
                        const auto fadeOutStart = region->durationSamples
                                                  - region->fadeOutSamples;
                        if (region->fadeOutSamples > 0
                            && regionPosition >= fadeOutStart) {
                            const auto denominator = std::max<juce::int64>(
                                1, region->fadeOutSamples - 1);
                            fadeGain *= static_cast<float>(
                                            region->durationSamples - 1
                                            - regionPosition)
                                        / static_cast<float>(denominator);
                        }
                        const auto safeGain = juce::jlimit(0.0f, 1.0f,
                                                          fadeGain);
                        for (int channel = 0; channel < channelCount; ++channel)
                            arrangementScratch_.getWritePointer(channel)[sample] *= safeGain;
                    }
                }
                const auto eventGain = juce::Decibels::decibelsToGain(
                    region->clip.eventGainDb, -60.0f);
                if (std::abs(eventGain - 1.0f) > 0.000001f)
                    for (int channel = 0; channel < channelCount; ++channel)
                        arrangementScratch_.applyGain(
                            channel, 0, samplesToRead, eventGain);
                if (!region->clip.automation.empty()) {
                    for (int sample = 0; sample < samplesToRead; ++sample) {
                        const auto localSeconds
                            = static_cast<double>(relativePosition + sample)
                              / sampleRate_;
                        const auto automationGain
                            = juce::Decibels::decibelsToGain(
                                automationValueAt(region->clip.automation,
                                                  localSeconds),
                                -60.0f);
                        for (int channel = 0; channel < channelCount;
                             ++channel)
                            arrangementScratch_.getWritePointer(channel)[sample]
                                *= automationGain;
                    }
                }
                applySoundLanes(arrangementScratch_, channelCount, samplesToRead, relativePosition, sampleRate_, region->clip);
                for (int channel = 0; channel < channelCount; ++channel)
                    output.buffer->addFrom(
                        channel,
                        output.startSample + outputOffset + destinationOffset
                            + silenceBefore,
                        arrangementScratch_, channel, 0, samplesToRead);
            }
            destinationOffset += segmentSamples;
            position += segmentSamples;
        }
        nextReadPosition_.store(position, std::memory_order_relaxed);
        arrangementCallbacksInFlight_.fetch_sub(1, std::memory_order_release);
    }

    void renderCurrent(const juce::AudioSourceChannelInfo& output,
                       const int outputOffset,
                       const int sampleCount)
    {
        if (sampleCount <= 0)
            return;
        if (launcherStopped_.load(std::memory_order_acquire)
            || (!launcherActive_.load(std::memory_order_acquire)
                && !arrangementPlaybackEnabled_.load(
                    std::memory_order_acquire))) {
            auto position = nextReadPosition_.load(std::memory_order_relaxed);
            const auto totalLength = std::max<juce::int64>(
                1, sessionLengthSamples_.load(std::memory_order_acquire));
            position = std::min<juce::int64>(
                totalLength, position + sampleCount);
            nextReadPosition_.store(position, std::memory_order_relaxed);
            return;
        }
        const auto launcherActive = launcherActive_.load(std::memory_order_acquire);
        if (launcherActive
            && activeLauncherUsesPlan_.load(std::memory_order_acquire)) {
            renderLauncherPlan(output, outputOffset, sampleCount);
            return;
        }
        if (!launcherActive && arrangementPlan_ != nullptr) {
            renderArrangement(output, outputOffset, sampleCount);
            return;
        }
        const auto clipStart = launcherActive
                                   ? juce::int64 {}
                                   : clipStartSamples_.load(std::memory_order_acquire);
        const auto clipDuration = launcherActive
                                      ? launcherDurationSamples_.load(std::memory_order_relaxed)
                                      : clipDurationSamples_.load(std::memory_order_acquire);
        const auto shouldLoop = launcherActive
                                    ? launcherLooping_.load(std::memory_order_relaxed)
                                    : looping_.load(std::memory_order_relaxed);
        const auto clipEnd = clipStart + clipDuration;
        const auto totalLength = std::max<juce::int64>(1, clipEnd);
        auto position = nextReadPosition_.load(std::memory_order_relaxed);
        auto destinationOffset = 0;
        auto& deck = *decks_[static_cast<std::size_t>(
            activeDeck_.load(std::memory_order_acquire))];

        while (destinationOffset < sampleCount) {
            if (position >= totalLength) {
                if (!shouldLoop)
                    break;
                position = 0;
            }
            const auto remaining = sampleCount - destinationOffset;
            const auto untilEnd = static_cast<int>(std::min<juce::int64>(
                remaining, totalLength - position));
            const auto overlapStart = std::max(position, clipStart);
            const auto overlapEnd = std::min(position + untilEnd, clipEnd);
            if (overlapEnd > overlapStart) {
                const auto silenceBefore = static_cast<int>(overlapStart - position);
                const auto samplesToRead = static_cast<int>(overlapEnd - overlapStart);
                const auto relativePosition = overlapStart - clipStart;
                if (deck.position() != relativePosition)
                    deck.setPosition(relativePosition);
                deck.render({ output.buffer,
                              output.startSample + outputOffset + destinationOffset
                                  + silenceBefore,
                              samplesToRead });
            }
            destinationOffset += untilEnd;
            position += untilEnd;
        }
        nextReadPosition_.store(position, std::memory_order_relaxed);
    }

    std::array<std::unique_ptr<Deck>, 2> decks_;
    const juce::int64 sourceLengthSamples_ {};
    const double sampleRate_ {};
    juce::TimeSliceThread& readAheadThread_;
    const int channels_ { 2 };
    std::unique_ptr<ArrangementPlan> arrangementPlan_;
    std::array<std::unique_ptr<LauncherPlan>,
               AudioTrackController::maximumLauncherScenes> launcherPlans_;
    juce::AudioBuffer<float> arrangementScratch_;
    std::atomic<bool> arrangementPlanAvailable_ { true };
    std::atomic<int> arrangementCallbacksInFlight_ {};
    std::atomic<bool> launcherPlansAvailable_ { true };
    std::atomic<int> launcherCallbacksInFlight_ {};
    std::atomic<juce::int64> arrangementLengthSamples_ {};
    std::atomic<juce::int64> sessionLengthSamples_ { 1 };
    std::atomic<juce::int64> loopStartSamples_ {};
    std::atomic<juce::int64> loopEndSamples_ { 1 };
    std::atomic<int> blockSize_ { 512 };
    std::atomic<double> outputSampleRate_ { 48'000.0 };
    std::atomic<bool> prepared_ {};
    std::atomic<int> activeDeck_ {};
    std::atomic<juce::int64> nextReadPosition_ {};
    std::atomic<juce::int64> clipStartSamples_ {};
    std::atomic<juce::int64> sourceOffsetSamples_ {};
    std::atomic<juce::int64> clipDurationSamples_ {};
    std::atomic<bool> looping_ {};
    std::atomic<bool> launcherActive_ {};
    std::atomic<bool> launcherStopped_ {};
    std::atomic<bool> arrangementPlaybackEnabled_ { true };
    std::atomic<bool> activeLauncherUsesPlan_ {};
    std::atomic<juce::int64> launcherDurationSamples_ { 1 };
    std::atomic<bool> launcherLooping_ { true };
    std::atomic<int> activeScene_ { -1 };
    std::atomic<std::uint64_t> pendingGeneration_ {};
    std::atomic<int> pendingKind_ { requestNone };
    std::atomic<int> pendingDeck_ { 1 };
    std::atomic<int> pendingScene_ { -1 };
    std::atomic<juce::int64> pendingDurationSamples_ { 1 };
    std::atomic<juce::int64> pendingDelaySamples_ {};
    std::atomic<juce::int64> pendingStartSamples_ {};
    std::atomic<bool> pendingLooping_ { true };
    std::uint64_t scheduledGeneration_ {};
    int scheduledKind_ { requestNone };
    int scheduledDeck_ { 1 };
    int scheduledScene_ { -1 };
    juce::int64 scheduledDurationSamples_ { 1 };
    juce::int64 scheduledDelaySamples_ {};
    juce::int64 scheduledStartSamples_ {};
    bool scheduledLooping_ { true };
};

int ClipAudioSource::launcherPlanState(const int sceneIndex) noexcept
{
    if (sceneIndex < 0
        || sceneIndex >= AudioTrackController::maximumLauncherScenes)
        return 0;
    if (!launcherPlansAvailable_.load(std::memory_order_acquire))
        return 1;
    launcherCallbacksInFlight_.fetch_add(1, std::memory_order_acq_rel);
    if (!launcherPlansAvailable_.load(std::memory_order_acquire)) {
        launcherCallbacksInFlight_.fetch_sub(1, std::memory_order_release);
        return 1;
    }
    const auto& plan = launcherPlans_[static_cast<std::size_t>(sceneIndex)];
    const auto state = plan == nullptr
                           ? 0
                           : std::all_of(
                                 plan->regions.begin(), plan->regions.end(),
                                 [this](const auto& region) {
                                     return region->deck.ready(
                                         blockSize_.load(
                                             std::memory_order_relaxed));
                                 })
                                 ? 2
                                 : 1;
    launcherCallbacksInFlight_.fetch_sub(1, std::memory_order_release);
    return state;
}

juce::int64 ClipAudioSource::launcherPlanDurationSamples(
    const int sceneIndex) noexcept
{
    if (sceneIndex < 0
        || sceneIndex >= AudioTrackController::maximumLauncherScenes
        || !launcherPlansAvailable_.load(std::memory_order_acquire))
        return 0;
    launcherCallbacksInFlight_.fetch_add(1, std::memory_order_acq_rel);
    auto duration = juce::int64 {};
    if (launcherPlansAvailable_.load(std::memory_order_acquire)) {
        const auto& plan = launcherPlans_[static_cast<std::size_t>(
            sceneIndex)];
        if (plan != nullptr)
            duration = plan->durationSamples;
    }
    launcherCallbacksInFlight_.fetch_sub(1, std::memory_order_release);
    return duration;
}

void ClipAudioSource::setLauncherPlanPosition(
    const int sceneIndex, const juce::int64 position) noexcept
{
    if (sceneIndex < 0
        || sceneIndex >= AudioTrackController::maximumLauncherScenes
        || !launcherPlansAvailable_.load(std::memory_order_acquire))
        return;
    launcherCallbacksInFlight_.fetch_add(1, std::memory_order_acq_rel);
    if (launcherPlansAvailable_.load(std::memory_order_acquire)) {
        auto& plan = launcherPlans_[static_cast<std::size_t>(sceneIndex)];
        if (plan != nullptr) {
            const auto safe = juce::jlimit<juce::int64>(
                0, plan->durationSamples, position);
            for (auto& region : plan->regions) {
                const auto start = region->startSamples.load(
                    std::memory_order_relaxed);
                region->deck.setPosition(juce::jlimit<juce::int64>(
                    0, region->durationSamples, safe - start));
            }
        }
    }
    launcherCallbacksInFlight_.fetch_sub(1, std::memory_order_release);
}

void ClipAudioSource::renderLauncherPlan(
    const juce::AudioSourceChannelInfo& output, const int outputOffset,
    const int sampleCount) noexcept
{
    if (sampleCount <= 0
        || !launcherPlansAvailable_.load(std::memory_order_acquire))
        return;
    launcherCallbacksInFlight_.fetch_add(1, std::memory_order_acq_rel);
    if (!launcherPlansAvailable_.load(std::memory_order_acquire)) {
        launcherCallbacksInFlight_.fetch_sub(1, std::memory_order_release);
        return;
    }
    const auto sceneIndex = activeScene_.load(std::memory_order_acquire);
    if (sceneIndex < 0
        || sceneIndex >= AudioTrackController::maximumLauncherScenes) {
        launcherCallbacksInFlight_.fetch_sub(1, std::memory_order_release);
        return;
    }
    auto& plan = launcherPlans_[static_cast<std::size_t>(sceneIndex)];
    if (plan == nullptr) {
        launcherCallbacksInFlight_.fetch_sub(1, std::memory_order_release);
        return;
    }

    const auto playStart = juce::jlimit<juce::int64>(
        0, plan->durationSamples - 1, plan->playStartSamples);
    const auto playEnd = juce::jlimit<juce::int64>(
        playStart + 1, plan->durationSamples, plan->playEndSamples);
    const auto loopStart = juce::jlimit<juce::int64>(
        playStart, playEnd - 1, plan->loopStartSamples);
    const auto loopEnd = juce::jlimit<juce::int64>(
        loopStart + 1, playEnd, plan->loopEndSamples);
    auto position = nextReadPosition_.load(std::memory_order_relaxed);
    if (position < playStart)
        position = playStart;
    auto written = 0;
    while (written < sampleCount) {
        const auto playbackEnd = plan->looping ? loopEnd : playEnd;
        if (position >= playbackEnd) {
            if (!plan->looping)
                break;
            position = loopStart
                       + (position - loopStart)
                             % std::max<juce::int64>(1,
                                                     loopEnd - loopStart);
        }
        const auto count = static_cast<int>(std::min<juce::int64>(
            sampleCount - written, playbackEnd - position));
        if (count <= 0)
            break;
        for (auto& region : plan->regions) {
            const auto regionStart = region->startSamples.load(
                std::memory_order_acquire);
            const auto regionEnd = regionStart + region->durationSamples;
            const auto overlapStart = std::max(position, regionStart);
            const auto overlapEnd = std::min(position + count, regionEnd);
            if (overlapEnd <= overlapStart)
                continue;
            const auto silenceBefore = static_cast<int>(overlapStart
                                                        - position);
            const auto samplesToRead = static_cast<int>(overlapEnd
                                                        - overlapStart);
            const auto relative = overlapStart - regionStart;
            if (region->deck.position() != relative)
                region->deck.setPosition(relative);
            arrangementScratch_.clear();
            region->deck.render({ &arrangementScratch_, 0,
                                  samplesToRead });
            const auto channels = std::min(
                output.buffer->getNumChannels(),
                arrangementScratch_.getNumChannels());
            if (region->fadeInSamples > 0
                || region->fadeOutSamples > 0) {
                for (int sample = 0; sample < samplesToRead; ++sample) {
                    const auto regionPosition = relative + sample;
                    auto fadeGain = 1.0f;
                    if (region->fadeInSamples > 0
                        && regionPosition < region->fadeInSamples) {
                        const auto denominator = std::max<juce::int64>(
                            1, region->fadeInSamples - 1);
                        fadeGain *= static_cast<float>(regionPosition)
                                    / static_cast<float>(denominator);
                    }
                    const auto fadeOutStart = region->durationSamples
                                              - region->fadeOutSamples;
                    if (region->fadeOutSamples > 0
                        && regionPosition >= fadeOutStart) {
                        const auto denominator = std::max<juce::int64>(
                            1, region->fadeOutSamples - 1);
                        fadeGain *= static_cast<float>(
                                        region->durationSamples - 1
                                        - regionPosition)
                                    / static_cast<float>(denominator);
                    }
                    const auto safeGain = juce::jlimit(
                        0.0f, 1.0f, fadeGain);
                    for (int channel = 0; channel < channels; ++channel)
                        arrangementScratch_.getWritePointer(channel)[sample]
                            *= safeGain;
                }
            }
            const auto eventGain = juce::Decibels::decibelsToGain(
                region->clip.eventGainDb, -60.0f);
            if (std::abs(eventGain - 1.0f) > 0.000001f)
                for (auto channel = 0; channel < channels; ++channel)
                    arrangementScratch_.applyGain(
                        channel, 0, samplesToRead, eventGain);
            if (!region->clip.automation.empty()) {
                for (int sample = 0; sample < samplesToRead; ++sample) {
                    const auto localSeconds
                        = static_cast<double>(relative + sample)
                          / sampleRate_;
                    const auto automationGain
                        = juce::Decibels::decibelsToGain(
                            automationValueAt(region->clip.automation,
                                              localSeconds),
                            -60.0f);
                    for (int channel = 0; channel < channels; ++channel)
                        arrangementScratch_.getWritePointer(channel)[sample]
                            *= automationGain;
                }
            }
            applySoundLanes(arrangementScratch_, channels, samplesToRead, relative, sampleRate_, region->clip);
            for (auto channel = 0; channel < channels; ++channel)
                output.buffer->addFrom(
                    channel,
                    output.startSample + outputOffset + written
                        + silenceBefore,
                    arrangementScratch_, channel, 0, samplesToRead);
        }
        position += count;
        written += count;
    }
    nextReadPosition_.store(position, std::memory_order_relaxed);
    if (!plan->looping && position >= playEnd) {
        launcherActive_.store(false, std::memory_order_release);
        activeLauncherUsesPlan_.store(false, std::memory_order_release);
        launcherStopped_.store(true, std::memory_order_release);
        activeScene_.store(-1, std::memory_order_release);
    }
    launcherCallbacksInFlight_.fetch_sub(1, std::memory_order_release);
}

juce::Result ClipAudioSource::setLauncherSceneSource(
    const int sceneIndex,
    std::unique_ptr<juce::AudioFormatReader> reader,
    const double sourceOffsetSeconds,
    const double durationSeconds,
    const bool shouldLoop)
{
    if (sceneIndex < 0
        || sceneIndex >= AudioTrackController::maximumLauncherScenes
        || reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0)
        return juce::Result::fail("Cannot open a launcher clip source reader");

    const auto sourceLengthSeconds = static_cast<double>(
        reader->lengthInSamples) / reader->sampleRate;
    const auto minimumDuration = 1.0 / reader->sampleRate;
    const auto offset = juce::jlimit(
        0.0, std::max(0.0, sourceLengthSeconds - minimumDuration),
        sourceOffsetSeconds);
    const auto duration = juce::jlimit(
        minimumDuration,
        std::max(minimumDuration, sourceLengthSeconds - offset),
        durationSeconds);
    AudioTrackController::AudioClip clip;
    clip.startSeconds = 0.0;
    clip.sourceOffsetSeconds = offset;
    clip.durationSeconds = duration;
    clip.sourceDurationSeconds = duration;
    std::vector<PreparedArrangementRegion> regions;
    regions.push_back({ std::move(clip), std::move(reader) });
    return setLauncherSceneRegions(
        sceneIndex, std::move(regions), duration, 0.0, duration,
        0.0, duration, shouldLoop);
}

juce::Result ClipAudioSource::setLauncherSceneRegions(
    const int sceneIndex,
    std::vector<PreparedArrangementRegion> regions,
    const double durationSeconds, const double playStartSeconds,
    const double playEndSeconds, const double loopStartSeconds,
    const double loopEndSeconds, const bool shouldLoop)
{
    if (sceneIndex < 0
        || sceneIndex >= AudioTrackController::maximumLauncherScenes)
        return juce::Result::fail("Invalid launcher scene");

    const auto replacingActiveScene
        = activeScene_.load(std::memory_order_acquire) == sceneIndex;
    const auto activePosition = nextReadPosition_.load(
        std::memory_order_acquire);

    auto next = std::make_unique<LauncherPlan>();
    const auto duration = juce::jlimit(0.001, 86'400.0,
                                       durationSeconds);
    next->durationSamples = std::max<juce::int64>(
        1, secondsToSamples(duration));
    next->playStartSamples = secondsToSamples(juce::jlimit(
        0.0, std::max(0.0, duration - 0.001), playStartSeconds));
    next->playEndSamples = juce::jlimit<juce::int64>(
        next->playStartSamples + 1, next->durationSamples,
        secondsToSamples(playEndSeconds));
    next->loopStartSamples = juce::jlimit<juce::int64>(
        next->playStartSamples, next->playEndSamples - 1,
        secondsToSamples(loopStartSeconds));
    next->loopEndSamples = juce::jlimit<juce::int64>(
        next->loopStartSamples + 1, next->playEndSamples,
        secondsToSamples(loopEndSeconds));
    next->looping = shouldLoop;
    next->regions.reserve(regions.size());
    const auto isPrepared = prepared_.load(std::memory_order_acquire);
    const auto blockSize = blockSize_.load(std::memory_order_acquire);
    const auto outputSampleRate = outputSampleRate_.load(
        std::memory_order_acquire);
    for (auto& preparedRegion : regions) {
        if (preparedRegion.reader == nullptr
            || preparedRegion.reader->sampleRate <= 0.0
            || preparedRegion.reader->lengthInSamples <= 0)
            return juce::Result::fail(
                "Cannot open a launcher event source reader");
        const auto sourceLength = static_cast<double>(
            preparedRegion.reader->lengthInSamples)
                                  / preparedRegion.reader->sampleRate;
        const auto minimum = 1.0 / preparedRegion.reader->sampleRate;
        const auto offset = juce::jlimit(
            0.0, std::max(0.0, sourceLength - minimum),
            preparedRegion.clip.sourceOffsetSeconds);
        const auto sourceDuration = juce::jlimit(
            minimum, std::max(minimum, sourceLength - offset),
            preparedRegion.clip.sourceDurationSeconds);
        const auto timelineDuration = juce::jlimit(
            0.001, 86'400.0, preparedRegion.clip.durationSeconds);
        const auto timelineSamples = std::max<juce::int64>(
            1, secondsToSamples(timelineDuration));
        const auto start = secondsToSamples(
            preparedRegion.clip.startSeconds);
        const auto fadeIn = juce::jlimit<juce::int64>(
            0, timelineSamples,
            secondsToSamples(preparedRegion.clip.fadeInSeconds));
        const auto fadeOut = juce::jlimit<juce::int64>(
            0, timelineSamples,
            secondsToSamples(preparedRegion.clip.fadeOutSeconds));
        next->regions.push_back(std::make_unique<ArrangementRegion>(
            std::move(preparedRegion.clip),
            std::move(preparedRegion.reader), readAheadThread_, start,
            offset, sourceDuration, timelineDuration, timelineSamples,
            fadeIn, fadeOut, isPrepared, blockSize, outputSampleRate, channels_));
    }
    next->prepared = isPrepared;

    launcherPlansAvailable_.store(false, std::memory_order_release);
    while (launcherCallbacksInFlight_.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    auto& slot = launcherPlans_[static_cast<std::size_t>(sceneIndex)];
    auto retired = std::move(slot);
    slot = std::move(next);
    if (replacingActiveScene) {
        nextReadPosition_.store(juce::jlimit<juce::int64>(
                                    slot->playStartSamples,
                                    std::max(slot->playStartSamples,
                                             slot->playEndSamples - 1),
                                    activePosition),
                                std::memory_order_release);
        launcherDurationSamples_.store(slot->durationSamples,
                                       std::memory_order_release);
        launcherLooping_.store(shouldLoop, std::memory_order_release);
        activeLauncherUsesPlan_.store(true, std::memory_order_release);
    }
    if (retired != nullptr)
        retired->release();
    retired.reset();
    launcherPlansAvailable_.store(true, std::memory_order_release);
    return juce::Result::ok();
}

void ClipAudioSource::clearLauncherSceneSource(const int sceneIndex)
{
    if (sceneIndex < 0
        || sceneIndex >= AudioTrackController::maximumLauncherScenes)
        return;
    launcherPlansAvailable_.store(false, std::memory_order_release);
    while (launcherCallbacksInFlight_.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    auto& slot = launcherPlans_[static_cast<std::size_t>(sceneIndex)];
    auto retired = std::move(slot);
    if (retired != nullptr)
        retired->release();
    retired.reset();
    launcherPlansAvailable_.store(true, std::memory_order_release);
}

juce::Result ClipAudioSource::setArrangementRegions(
    std::vector<PreparedArrangementRegion> regions)
{
    if (regions.size() > AudioTrackController::maximumAudioClipsPerTrack * 4)
        return juce::Result::fail("The audio clip limit for this track was exceeded");

    auto next = std::make_unique<ArrangementPlan>();
    next->regions.reserve(regions.size());
    const auto isPrepared = prepared_.load(std::memory_order_acquire);
    const auto blockSize = blockSize_.load(std::memory_order_acquire);
    const auto outputSampleRate = outputSampleRate_.load(std::memory_order_acquire);
    auto totalLength = juce::int64 {};
    for (auto& preparedRegion : regions) {
        if (preparedRegion.reader == nullptr
            || preparedRegion.reader->sampleRate <= 0.0
            || preparedRegion.reader->lengthInSamples <= 0)
            return juce::Result::fail("Cannot open an audio clip source reader");
        const auto sourceLengthSeconds = static_cast<double>(
            preparedRegion.reader->lengthInSamples)
                                         / preparedRegion.reader->sampleRate;
        const auto minimumDuration = 1.0 / preparedRegion.reader->sampleRate;
        const auto offsetSeconds = juce::jlimit(
            0.0, std::max(0.0, sourceLengthSeconds - minimumDuration),
            preparedRegion.clip.sourceOffsetSeconds);
        const auto sourceDurationSeconds = juce::jlimit(
            minimumDuration,
            std::max(minimumDuration, sourceLengthSeconds - offsetSeconds),
            preparedRegion.clip.sourceDurationSeconds > 0.0
                ? preparedRegion.clip.sourceDurationSeconds
                : preparedRegion.clip.durationSeconds);
        const auto durationSeconds = juce::jlimit(
            0.001, 86'400.0, preparedRegion.clip.durationSeconds);
        const auto duration = std::max<juce::int64>(
            1, secondsToSamples(durationSeconds));
        const auto fadeInSeconds = juce::jlimit(
            0.0, durationSeconds, preparedRegion.clip.fadeInSeconds);
        const auto fadeOutSeconds = juce::jlimit(
            0.0, durationSeconds, preparedRegion.clip.fadeOutSeconds);
        const auto fadeIn = juce::jlimit<juce::int64>(
            0, duration, secondsToSamples(fadeInSeconds));
        const auto fadeOut = juce::jlimit<juce::int64>(
            0, duration, secondsToSamples(fadeOutSeconds));
        const auto start = secondsToSamples(preparedRegion.clip.startSeconds);
        preparedRegion.clip.startSeconds = static_cast<double>(start) / sampleRate_;
        preparedRegion.clip.sourceOffsetSeconds = offsetSeconds;
        preparedRegion.clip.sourceDurationSeconds = sourceDurationSeconds;
        preparedRegion.clip.durationSeconds = static_cast<double>(duration)
                                               / sampleRate_;
        preparedRegion.clip.fadeInSeconds = static_cast<double>(fadeIn)
                                             / sampleRate_;
        preparedRegion.clip.fadeOutSeconds = static_cast<double>(fadeOut)
                                              / sampleRate_;
        preparedRegion.clip.sourceLengthSeconds = sourceLengthSeconds;
        totalLength = std::max(totalLength, start + duration);
        next->regions.push_back(std::make_unique<ArrangementRegion>(
            std::move(preparedRegion.clip), std::move(preparedRegion.reader),
            readAheadThread_, start, offsetSeconds, sourceDurationSeconds,
            durationSeconds, duration, fadeIn, fadeOut, isPrepared,
            blockSize, outputSampleRate, channels_));
    }
    next->prepared = isPrepared;

    arrangementPlanAvailable_.store(false, std::memory_order_release);
    while (arrangementCallbacksInFlight_.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();
    auto retired = std::move(arrangementPlan_);
    arrangementPlan_ = std::move(next);
    arrangementLengthSamples_.store(totalLength, std::memory_order_release);
    sessionLengthSamples_.store(
        std::max(totalLength,
                 sessionLengthSamples_.load(std::memory_order_relaxed)),
        std::memory_order_release);
    if (arrangementPlan_ != nullptr && !arrangementPlan_->regions.empty()) {
        const auto& first = *arrangementPlan_->regions.front();
        clipStartSamples_.store(first.startSamples.load(std::memory_order_relaxed),
                                std::memory_order_release);
        sourceOffsetSamples_.store(secondsToSamples(
                                       first.clip.sourceOffsetSeconds),
                                   std::memory_order_release);
        clipDurationSamples_.store(first.durationSamples,
                                   std::memory_order_release);
    }
    arrangementPlanAvailable_.store(true, std::memory_order_release);
    if (retired != nullptr)
        retired->release();
    retired.reset();
    return juce::Result::ok();
}

bool ClipAudioSource::moveArrangementRegion(const std::uint64_t regionId,
                                            const double startSeconds) noexcept
{
    if (!arrangementPlanAvailable_.load(std::memory_order_acquire)
        || arrangementPlan_ == nullptr)
        return false;
    const auto start = secondsToSamples(startSeconds);
    auto moved = false;
    auto totalLength = juce::int64 {};
    for (auto& region : arrangementPlan_->regions) {
        if (region->clip.id == regionId) {
            region->startSamples.store(start, std::memory_order_release);
            moved = true;
        }
        totalLength = std::max(
            totalLength,
            region->startSamples.load(std::memory_order_acquire)
                + region->durationSamples);
    }
    if (moved)
        arrangementLengthSamples_.store(totalLength, std::memory_order_release);
    return moved;
}

class GraphSynthSound final : public juce::SynthesiserSound {
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

// A thin host: JUCE's voice bookkeeping around the engine's PolyVoice,
// which is the same one the graph editor's preview plays.
// What the instrument's control cables put on its voices this block. Written
// and read on the audio thread only, before the voices render.
struct VoiceModulationState {
    // Lanes and cables together: an instrument's settings automated, its
    // filter and table moved by the graph.
    static constexpr std::size_t capacity = 32;
    std::size_t count {};
    std::array<cydaw::engine::PolyVoiceTarget, capacity> targets {};
    std::array<std::uint16_t, capacity> indices {};
    std::array<float, capacity> values {};
    // 0: the value is set; otherwise it modulates the voice's own by that much.
    std::array<float, capacity> depths {};
    // An instrument setting's knob: its range, its scale.
    std::array<float, capacity> minimums {};
    std::array<float, capacity> maximums {};
    std::array<bool, capacity> logarithmic {};
};

namespace {

// Which of an instrument's settings jump when they are set rather than
// glide: its choices, switches and whole numbers. Built once, off the audio
// thread (the engine asks when it is made).
struct GlideKinds {
    std::array<bool, cydaw::engine::prism::parameterCount> prism {};
    std::array<bool, cydaw::engine::kick::parameterCount> kick {};
    std::array<std::array<bool, cydaw::engine::synth::valueCapacity>, 5> synth {};
    std::array<bool, cydaw::engine::drum::valueCount> drum {};
    std::array<bool, 16> sampler {};
};

const GlideKinds& glideKinds()
{
    static const GlideKinds kinds = [] {
        namespace engine = cydaw::engine;
        GlideKinds made;
        for (int index = 0; index < engine::prism::parameterCount; ++index)
            made.prism[static_cast<std::size_t>(index)] = engine::prism::parameter(index).kind != engine::prism::Kind::continuous;
        for (int index = 0; index < engine::kick::parameterCount; ++index)
            made.kick[static_cast<std::size_t>(index)] = !engine::kick::parameter(index).choices.empty();
        for (auto& jumps : made.synth)
            jumps.fill(true);
        for (int model = 1; model < 5; ++model) {
            const auto which = static_cast<engine::synth::Model>(model);
            auto& jumps = made.synth[static_cast<std::size_t>(model)];
            const auto count = std::min(engine::synth::parameterCount(which), engine::synth::valueCapacity);
            for (int index = 0; index < count; ++index)
                jumps[static_cast<std::size_t>(index)] = engine::synth::parameter(which, index).kind != engine::synth::Kind::continuous;
        }
        made.drum.fill(true);
        const auto& drums = engine::drum::parameters();
        for (std::size_t index = 0; index < drums.size() && index < made.drum.size(); ++index)
            made.drum[index] = drums[index].kind != engine::drum::Kind::continuous;
        made.sampler.fill(true);
        for (const auto setting : { engine::SamplerSetting::transpose, engine::SamplerSetting::fine, engine::SamplerSetting::velocityDb,
                                    engine::SamplerSetting::velocityCrossfade, engine::SamplerSetting::width })
            made.sampler[static_cast<std::size_t>(setting)] = false;
        return made;
    }();
    return kinds;
}

// What a track's newest voice plays, from the audio thread to the
// interface: a sequence lock over atomics (the writer never waits, a reader
// tries again while a write is under way).
class VoiceLiveFeed final {
public:
    void write(const cydaw::engine::VoiceLive* live) noexcept
    {
        sequence_.fetch_add(1, std::memory_order_acq_rel);
        playing_.store(live != nullptr, std::memory_order_relaxed);
        if (live != nullptr) {
            for (std::size_t index = 0; index < live->values.size(); ++index)
                values_[index].store(live->values[index], std::memory_order_relaxed);
            for (std::size_t index = 0; index < live->lfos.size(); ++index) {
                lfos_[index].store(live->lfos[index], std::memory_order_relaxed);
                phases_[index].store(live->lfoPhases[index], std::memory_order_relaxed);
            }
            for (std::size_t index = 0; index < live->envelopes.size(); ++index)
                envelopes_[index].store(live->envelopes[index], std::memory_order_relaxed);
        }
        sequence_.fetch_add(1, std::memory_order_release);
    }
    // False when no voice plays.
    bool read(cydaw::engine::VoiceLive& out) const noexcept
    {
        for (int attempt = 0; attempt < 8; ++attempt) {
            const auto before = sequence_.load(std::memory_order_acquire);
            if ((before & 1u) != 0)
                continue;
            const auto playing = playing_.load(std::memory_order_relaxed);
            if (playing) {
                for (std::size_t index = 0; index < out.values.size(); ++index)
                    out.values[index] = values_[index].load(std::memory_order_relaxed);
                for (std::size_t index = 0; index < out.lfos.size(); ++index) {
                    out.lfos[index] = lfos_[index].load(std::memory_order_relaxed);
                    out.lfoPhases[index] = phases_[index].load(std::memory_order_relaxed);
                }
                for (std::size_t index = 0; index < out.envelopes.size(); ++index)
                    out.envelopes[index] = envelopes_[index].load(std::memory_order_relaxed);
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence_.load(std::memory_order_relaxed) == before)
                return playing;
        }
        return false;
    }

private:
    std::atomic<std::uint32_t> sequence_ {};
    std::atomic<bool> playing_ {};
    std::array<std::atomic<float>, cydaw::engine::VoiceLive::valueCount> values_ {};
    std::array<std::atomic<float>, cydaw::engine::VoiceLive::lfoCount> lfos_ {};
    std::array<std::atomic<float>, cydaw::engine::VoiceLive::lfoCount> phases_ {};
    std::array<std::atomic<float>, cydaw::engine::VoiceLive::envelopeCount> envelopes_ {};
};

// An instrument's settings as heard: a knob turned glides there in a few
// milliseconds instead of jumping at the next block (a click on a level, a
// cutoff, a sustain). The engine renders its voices in short pieces while
// it glides. Audio thread only. Choices, switches, and a plan that plays
// something else (another model, other tables) are taken at once.
class PlanGlide final {
public:
    // At the start of a block, under its borrow of `plan`: whether the
    // values still move toward it.
    [[nodiscard]] bool begin(const cydaw::engine::PolySynthGraphPlan& plan) noexcept
    {
        const Identity identity { plan.enabled, plan.waveform, plan.synthModel, plan.drumKit,
                                  plan.wavetable.get(), plan.soundModel.get(), plan.sampler.get(), plan.prismTables.get() };
        if (!primed_ || !(identity == identity_)) {
            primed_ = true;
            identity_ = identity;
            take(plan);
            return false;
        }
        const auto& kinds = glideKinds();
        const auto differs = [](const auto& heard, const auto& wanted, const auto& jumps) {
            for (std::size_t index = 0; index < heard.size() && index < wanted.size(); ++index)
                if (!jumps[index] && heard[index] != wanted[index])
                    return true;
            return false;
        };
        const auto model = static_cast<std::size_t>(plan.synthModel);
        return heard_.oscillatorLevel != plan.oscillatorLevel || heard_.filterCutoffHz != plan.filterCutoffHz
               || heard_.filterResonance != plan.filterResonance || heard_.sustainLevel != plan.sustainLevel
               || heard_.wavetablePosition != plan.wavetablePosition || heard_.wavetableDetuneCents != plan.wavetableDetuneCents
               || heard_.stringDamping != plan.stringDamping || heard_.velocityScale != plan.velocityScale
               || differs(heard_.prismValues, plan.prismValues, kinds.prism) || differs(heard_.kickValues, plan.kickValues, kinds.kick)
               || (model < kinds.synth.size() && differs(heard_.synthValues, plan.synthValues, kinds.synth[model]))
               || differs(heard_.drumValues, plan.drumValues, kinds.drum)
               || differs(heard_.samplerValues, plan.samplerValues, kinds.sampler);
    }

    // `frames` further toward `plan`: what the voices play for them. It
    // shares the plan's tables, so it lives only within the block's borrow.
    [[nodiscard]] const cydaw::engine::PolySynthGraphPlan& step(const cydaw::engine::PolySynthGraphPlan& plan, const int frames,
                                                               const double sampleRate) noexcept
    {
        const auto amount = static_cast<float>(1.0 - std::exp(-static_cast<double>(frames) / (0.006 * std::max(1.0, sampleRate))));
        const auto& kinds = glideKinds();
        const auto move = [amount](float& heard, const float wanted, const float tolerance) {
            heard += (wanted - heard) * amount;
            if (std::abs(wanted - heard) <= tolerance)
                heard = wanted;
        };
        const auto moveAll = [&move](auto& heard, const auto& wanted, const auto& jumps) {
            for (std::size_t index = 0; index < heard.size() && index < wanted.size(); ++index) {
                if (jumps[index])
                    heard[index] = wanted[index];
                else
                    move(heard[index], wanted[index], 1.0e-5f * std::max(1.0f, std::abs(wanted[index])));
            }
        };
        move(heard_.oscillatorLevel, plan.oscillatorLevel, 1.0e-6f);
        move(heard_.filterCutoffHz, plan.filterCutoffHz, 0.01f);
        move(heard_.filterResonance, plan.filterResonance, 1.0e-5f);
        move(heard_.sustainLevel, plan.sustainLevel, 1.0e-6f);
        move(heard_.wavetablePosition, plan.wavetablePosition, 1.0e-6f);
        move(heard_.wavetableDetuneCents, plan.wavetableDetuneCents, 1.0e-4f);
        move(heard_.stringDamping, plan.stringDamping, 1.0e-6f);
        move(heard_.velocityScale, plan.velocityScale, 1.0e-6f);
        moveAll(heard_.prismValues, plan.prismValues, kinds.prism);
        moveAll(heard_.kickValues, plan.kickValues, kinds.kick);
        if (const auto model = static_cast<std::size_t>(plan.synthModel); model < kinds.synth.size())
            moveAll(heard_.synthValues, plan.synthValues, kinds.synth[model]);
        moveAll(heard_.drumValues, plan.drumValues, kinds.drum);
        moveAll(heard_.samplerValues, plan.samplerValues, kinds.sampler);
        playing_ = plan;
        playing_.oscillatorLevel = heard_.oscillatorLevel;
        playing_.filterCutoffHz = heard_.filterCutoffHz;
        playing_.filterResonance = heard_.filterResonance;
        playing_.sustainLevel = heard_.sustainLevel;
        playing_.wavetablePosition = heard_.wavetablePosition;
        playing_.wavetableDetuneCents = heard_.wavetableDetuneCents;
        playing_.stringDamping = heard_.stringDamping;
        playing_.velocityScale = heard_.velocityScale;
        playing_.prismValues = heard_.prismValues;
        playing_.kickValues = heard_.kickValues;
        playing_.synthValues = heard_.synthValues;
        playing_.drumValues = heard_.drumValues;
        playing_.samplerValues = heard_.samplerValues;
        return playing_;
    }

    // At the end of the block, still under its borrow: the plan's shared
    // parts let go of while the plan itself still holds them (so they are
    // never freed here).
    void end() noexcept
    {
        playing_.wavetable = {};
        playing_.soundModel = {};
        playing_.sampler = {};
        playing_.prismTables = {};
    }

private:
    struct Identity {
        bool enabled {};
        cydaw::engine::PolyOscillatorWaveform waveform {};
        cydaw::engine::synth::Model model {};
        bool drumKit {};
        const void* wavetable {};
        const void* soundModel {};
        const void* sampler {};
        const void* tables {};
        [[nodiscard]] bool operator==(const Identity&) const = default;
    };
    struct Heard {
        float oscillatorLevel {};
        float filterCutoffHz {};
        float filterResonance {};
        float sustainLevel {};
        float wavetablePosition {};
        float wavetableDetuneCents {};
        float stringDamping {};
        float velocityScale {};
        cydaw::engine::prism::PrismValues prismValues {};
        cydaw::engine::kick::KickValues kickValues {};
        cydaw::engine::synth::Values synthValues {};
        cydaw::engine::drum::Values drumValues {};
        cydaw::engine::SamplerValues samplerValues {};
    };

    void take(const cydaw::engine::PolySynthGraphPlan& plan) noexcept
    {
        heard_.oscillatorLevel = plan.oscillatorLevel;
        heard_.filterCutoffHz = plan.filterCutoffHz;
        heard_.filterResonance = plan.filterResonance;
        heard_.sustainLevel = plan.sustainLevel;
        heard_.wavetablePosition = plan.wavetablePosition;
        heard_.wavetableDetuneCents = plan.wavetableDetuneCents;
        heard_.stringDamping = plan.stringDamping;
        heard_.velocityScale = plan.velocityScale;
        heard_.prismValues = plan.prismValues;
        heard_.kickValues = plan.kickValues;
        heard_.synthValues = plan.synthValues;
        heard_.drumValues = plan.drumValues;
        heard_.samplerValues = plan.samplerValues;
    }

    bool primed_ {};
    Identity identity_ {};
    Heard heard_ {};
    cydaw::engine::PolySynthGraphPlan playing_ {};
};

} // namespace

class GraphSynthVoice final : public juce::SynthesiserVoice, public PlanVoice {
public:
    GraphSynthVoice(const cydaw::realtime::RtPublisher<cydaw::engine::PolySynthGraphPlan>& graph,
                    const VoiceModulationState& modulation, const cydaw::engine::prism::Context* context = nullptr)
        : graph_(graph), modulation_(modulation)
    {
        voice_.setPrismContext(context);
    }

    // The plan the engine points its voices at this block (null: they
    // borrow the publisher's themselves).
    void hearFrom(const cydaw::engine::PolySynthGraphPlan* const* heard) noexcept { heard_ = heard; }

    [[nodiscard]] double voicePitch() const noexcept override { return voice_.currentPitch(); }
    bool live(cydaw::engine::VoiceLive& out) const noexcept { return isVoiceActive() && voice_.live(out); }
    [[nodiscard]] int chokedBy() const noexcept override
    {
        if (heard_ != nullptr && *heard_ != nullptr)
            return voice_.chokedBy(**heard_);
        const auto graph = graph_.read();
        return graph != nullptr ? voice_.chokedBy(*graph) : 0;
    }

    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<GraphSynthSound*>(sound) != nullptr;
    }

    void startNote(const int midiNoteNumber,
                   const float velocity,
                   juce::SynthesiserSound*,
                   const int pitchWheel) override
    {
        // A note starts from the automated values too (what an envelope or
        // a unison reads once, at its start).
        const auto started = withPlan([&](const auto& current) {
            return withModulation(current, [&](const auto& plan) {
                return legato ? voice_.retarget(plan, midiNoteNumber, velocity, std::max(1.0, getSampleRate()))
                              : voice_.startNote(plan, midiNoteNumber, velocity, std::max(1.0, getSampleRate()), glideFrom,
                                                 softAttack);
            });
        });
        legato = false;
        glideFrom = -1.0;
        softAttack = false;
        expressive_ = !isPlayingChannel(1);
        expressionPitch_ = expressive_ ? (static_cast<double>(pitchWheel) - 8'192.0) / 8'192.0 * 24.0 : 0.0;
        expressionGainDb_ = 0.0;
        gainLow_ = 0;
        // Its pan: centred until its channel says (CC 10, right after it).
        pan_ = 0.0f;
        panNow_ = 0.0f;
        panFresh_ = true;
        voice_.setExpression(expressionPitch_, expressionGainDb_);
        if (!started)
            clearCurrentNote();
    }

    void setCurrentPlaybackSampleRate(const double rate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate(rate);
        // Off the audio thread: sizes the string's delay line and builds the
        // wavetable bank before any note can ask for either; and the room a
        // panned note is rendered in.
        voice_.prepare(std::max(1.0, rate));
        if (scratch_.getNumSamples() == 0)
            scratch_.setSize(2, 2'048);
    }

    void stopNote(float, const bool allowTailOff) override
    {
        // Legato: JUCE stops a voice before starting it again; this one goes on.
        if (legato)
            return;
        // Choked, or one note at a time: this one gives way, quickly.
        if ((choke || quickRelease) && allowTailOff) {
            voice_.fadeOut(choke ? 0.006 : 0.03);
            choke = false;
            quickRelease = false;
            if (!voice_.isActive())
                clearCurrentNote();
            return;
        }
        voice_.stopNote(allowTailOff);
        if (!voice_.isActive())
            clearCurrentNote();
    }

    // A note on a channel of its own (2..16) has an expression: its pitch
    // bend and its gain (CC 11 and 43) move it alone.
    void pitchWheelMoved(const int value) override
    {
        if (!expressive_)
            return;
        expressionPitch_ = (static_cast<double>(value) - 8'192.0) / 8'192.0 * 24.0;
        voice_.setExpression(expressionPitch_, expressionGainDb_);
    }
    void controllerMoved(const int controller, const int value) override
    {
        if (!expressive_)
            return;
        if (controller == 43) {
            gainLow_ = value;
        } else if (controller == 11) {
            expressionGainDb_ = static_cast<double>((value << 7) | gainLow_) / 16'383.0 * 60.0 - 48.0;
            voice_.setExpression(expressionPitch_, expressionGainDb_);
        } else if (controller == 10) {
            pan_ = std::clamp((static_cast<float>(value) - 64.0f) / 63.0f, -1.0f, 1.0f);
            // Its first: where it starts, no glide from the middle.
            if (panFresh_)
                panNow_ = pan_;
        }
    }

    void renderNextBlock(juce::AudioBuffer<float>& output,
                         int startSample,
                         const int numSamples) override
    {
        if (!voice_.isActive())
            return;
        panFresh_ = false;
        // Panned (a note's pan): rendered aside, then into the output.
        if (output.getNumChannels() >= 2 && (std::abs(pan_) > 1.0e-4f || std::abs(panNow_) > 1.0e-4f)
            && scratch_.getNumSamples() > 0) {
            auto rendered = true;
            for (auto done = 0; done < numSamples && rendered;) {
                const auto count = std::min(numSamples - done, scratch_.getNumSamples());
                scratch_.clear(0, count);
                rendered = withPlan([&](const auto& current) {
                    return withModulation(current, [&](const auto& plan) {
                        return voice_.render(plan, scratch_.getArrayOfWritePointers(), 2, 0, count, std::max(1.0, getSampleRate()));
                    });
                });
                const auto* left = scratch_.getReadPointer(0);
                const auto* right = scratch_.getReadPointer(1);
                auto* outLeft = output.getWritePointer(0, startSample + done);
                auto* outRight = output.getWritePointer(1, startSample + done);
                for (auto sample = 0; sample < count; ++sample) {
                    panNow_ += (pan_ - panNow_) * 0.002f;
                    // Balance: the far side lowered, the near one kept.
                    outLeft[sample] += left[sample] * std::min(1.0f, 1.0f - panNow_);
                    outRight[sample] += right[sample] * std::min(1.0f, 1.0f + panNow_);
                }
                done += count;
            }
            if (!rendered)
                clearCurrentNote();
            return;
        }
        const auto rendered = withPlan([&](const auto& current) {
            return withModulation(current, [&](const auto& plan) {
                return voice_.render(plan, output.getArrayOfWritePointers(), output.getNumChannels(), startSample, numSamples,
                                     std::max(1.0, getSampleRate()));
            });
        });
        if (!rendered)
            clearCurrentNote();
    }

private:
    // The plan this block: the engine's when it gives one, else the
    // publisher's, borrowed for the call.
    template <typename Use>
    auto withPlan(Use&& use)
    {
        if (heard_ != nullptr && *heard_ != nullptr)
            return use(**heard_);
        const auto graph = graph_.read();
        return use(graph != nullptr ? *graph : silentPlan());
    }

    // The plan as the lanes and the cables leave it this block, handed to
    // `use`; the plan itself when nothing moves it.
    template <typename Use>
    auto withModulation(const cydaw::engine::PolySynthGraphPlan& plan, Use&& use)
    {
        if (modulation_.count == 0)
            return use(plan);
        auto modulated = plan;
        for (std::size_t index = 0; index < modulation_.count; ++index) {
            if (!std::isfinite(modulation_.values[index]))
                continue;
            if (modulation_.depths[index] != 0.0f)
                cydaw::engine::modulatePolyVoice(modulated, modulation_.targets[index], modulation_.values[index],
                                                 modulation_.depths[index], modulation_.indices[index],
                                                 modulation_.minimums[index], modulation_.maximums[index],
                                                 modulation_.logarithmic[index]);
            else
                cydaw::engine::applyPolyVoiceModulation(modulated, modulation_.targets[index], modulation_.values[index],
                                                        modulation_.indices[index]);
        }
        return use(modulated);
    }

    // What a voice plays when no plan has been published: nothing.
    static const cydaw::engine::PolySynthGraphPlan& silentPlan()
    {
        static const cydaw::engine::PolySynthGraphPlan plan {};
        return plan;
    }

    // All voices share one publisher and borrow it in turn on the audio
    // thread, so an instrument edit never frees a plan mid-voice.
    const cydaw::realtime::RtPublisher<cydaw::engine::PolySynthGraphPlan>& graph_;
    const cydaw::engine::PolySynthGraphPlan* const* heard_ {};
    const VoiceModulationState& modulation_;
    cydaw::engine::PolyVoice voice_;
    bool expressive_ {};
    double expressionPitch_ {};
    double expressionGainDb_ {};
    int gainLow_ {};
    // Its pan (-1..1, from its channel's CC 10), as it moves now; its
    // first value not glided to.
    float pan_ {};
    float panNow_ {};
    bool panFresh_ {};
    juce::AudioBuffer<float> scratch_;
};

// A note's expression as the audio thread reads it.
struct AudioTrackController::MidiSynthEngineCurves {
    std::vector<NoteExpressionPoint> pitch;
    std::vector<NoteExpressionPoint> gain;
    std::vector<NoteExpressionPoint> timbre;
    std::vector<NoteExpressionPoint> pressure;
    std::vector<NoteExpressionPoint> pan;
};

struct AudioTrackController::MidiOutQueue {
    struct Event {
        std::array<std::uint8_t, 3> bytes {};
        std::uint8_t size {};
    };
    cydaw::realtime::SpscRing<Event, 1'024> ring;
    // The device it goes to (set by the outputs' thread's owner); none:
    // nothing is queued.
    std::atomic<juce::MidiOutput*> device {};
};

// The tracks' MIDI outputs: each device opened once, a thread sending what
// the tracks queued (a millisecond apart at most).
class AudioTrackController::MidiOutputHub final : juce::Thread {
public:
    explicit MidiOutputHub(std::atomic<std::uint32_t>& sent) : juce::Thread("CyDAW MIDI out"), sent_(sent) { startThread(); }
    ~MidiOutputHub() override
    {
        stopThread(2'000);
        const juce::ScopedLock hold(lock_);
        for (auto& [queue, identifier] : queues_)
            queue->device.store(nullptr, std::memory_order_release);
    }
    // Every track's queue and the device it sends to (empty: none).
    void setQueues(std::vector<std::pair<std::shared_ptr<MidiOutQueue>, juce::String>> queues)
    {
        const juce::ScopedLock hold(lock_);
        for (auto& [queue, identifier] : queues) {
            juce::MidiOutput* device {};
            if (identifier.isNotEmpty()) {
                auto& open = devices_[identifier];
                if (open == nullptr)
                    open = juce::MidiOutput::openDevice(identifier);
                device = open.get();
            }
            queue->device.store(device, std::memory_order_release);
        }
        // The devices no track sends to any more are closed.
        for (auto it = devices_.begin(); it != devices_.end();)
            if (std::none_of(queues.begin(), queues.end(), [&it](const auto& entry) { return entry.second == it->first; }))
                it = devices_.erase(it);
            else
                ++it;
        queues_ = std::move(queues);
    }

private:
    void run() override
    {
        while (!threadShouldExit()) {
            {
                const juce::ScopedLock hold(lock_);
                for (auto& [queue, identifier] : queues_) {
                    MidiOutQueue::Event event;
                    while (queue->ring.tryPop(event)) {
                        auto* device = queue->device.load(std::memory_order_acquire);
                        if (device == nullptr || event.size == 0)
                            continue;
                        device->sendMessageNow(juce::MidiMessage(event.bytes.data(), event.size));
                        sent_.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
            wait(1);
        }
    }

    std::atomic<std::uint32_t>& sent_;
    juce::CriticalSection lock_;
    std::vector<std::pair<std::shared_ptr<MidiOutQueue>, juce::String>> queues_;
    std::map<juce::String, std::unique_ptr<juce::MidiOutput>> devices_;
};

struct AudioTrackController::MidiSynthEngine final : public juce::AudioSource, public RoutedInputReceiver {
    void setRoutedInputs(const juce::AudioBuffer<float>* const* buses, const int numSamples) noexcept override
    {
        measureRoutedInputs(buses, numSamples);
        for (std::size_t bus = 0; bus < maximumBuses; ++bus)
            routedInputs[bus] = buses != nullptr ? buses[bus] : nullptr;
        routedSamples = numSamples;
    }
    std::array<const juce::AudioBuffer<float>*, RoutedInputReceiver::maximumBuses> routedInputs {};
    int routedSamples {};
    // Its voices held back by the latency of what is routed into it.
    AudioDelayLine voicesDelay;
    void setInputDelaySamples(const int delaySamples) noexcept { voicesDelay.setDelaySamples(delaySamples); }
    void resetInputDelay() noexcept { voicesDelay.requestReset(); }

    using NoteCurves = MidiSynthEngineCurves;

    struct ScheduledNote {
        int noteNumber {};
        double startSeconds {};
        double endSeconds {};
        float velocity {};
        // A note with an expression plays on a channel of its own (2..16),
        // where its pitch bend and gain move it alone, as MPE does.
        int channel { 1 };
        std::shared_ptr<const NoteCurves> curves;
        // How likely it plays each time it comes; whether it was left out
        // this time (the audio thread's alone, once published).
        float chance { 1.0f };
        mutable bool silenced {};
    };

    // A curve's value at a time of its note (0..1), between its points
    // (each way bent as its first point says).
    [[nodiscard]] static double curveAt(const std::vector<NoteExpressionPoint>& points, const double position) noexcept
    {
        return NoteExpressionPoint::valueAt(points, static_cast<float>(position));
    }

    MidiSynthEngine(AudioTrackController& controller,
                    juce::MidiMessageCollector& sharedCollector,
                    juce::MidiKeyboardState& sharedKeyboard)
        : owner(controller), collector(sharedCollector), keyboard(sharedKeyboard)
    {
        instrumentGraph.store(
            std::make_shared<const cydaw::engine::PolySynthGraphPlan>());
        voiceModulations.store(std::make_shared<const std::vector<cydaw::engine::PolyVoiceModulation>>());
        // As many as the exported plug-in has: a piano held by its pedal
        // rings on many keys at once.
        (void) glideKinds();
        for (int voice = 0; voice < 32; ++voice) {
            auto* made = new GraphSynthVoice(instrumentGraph, voiceModulation, &synthesiser.context);
            made->hearFrom(&heardPlan);
            synthesiser.addVoice(made);
        }
        synthesiser.addSound(new GraphSynthSound());
        // Prisme's voicing (mono, legato, glide, its voice limit), read off
        // the plan the voices play.
        synthesiser.voicing = [this] {
            if (heardPlan != nullptr)
                return PlanSynthesiser::voicingOf(*heardPlan);
            const auto plan = instrumentGraph.read();
            return plan != nullptr ? PlanSynthesiser::voicingOf(*plan) : PlanSynthesiser::Voicing {};
        };
        synthesiser.noteInfo = [this](const int note, const float velocity) {
            if (heardPlan != nullptr)
                return PlanSynthesiser::noteInfoOf(*heardPlan, note, velocity, synthesiser.context.sampler);
            const auto plan = instrumentGraph.read();
            return plan != nullptr ? PlanSynthesiser::noteInfoOf(*plan, note, velocity, synthesiser.context.sampler)
                                   : PlanSynthesiser::NoteInfo {};
        };
        sequence.store(std::make_shared<const std::vector<ScheduledNote>>());
    }

    void prepareToPlay(const int samplesPerBlockExpected,
                       const double sampleRate) override
    {
        synthesiser.setCurrentPlaybackSampleRate(sampleRate);
        blockSize.store(std::max(1, samplesPerBlockExpected),
                        std::memory_order_release);
        sampleRateHz.store(sampleRate, std::memory_order_release);
        effects.prepare(sampleRate, std::max(1, samplesPerBlockExpected), 2);
        voicesDelay.prepare(LatencyCompensatingAudioSource::maximumDelaySamples);
        midiEffects.prepare(sampleRate);
        for (auto& pad : padStore)
            if (pad != nullptr)
                pad->prepare(sampleRate, samplesPerBlockExpected);
        if (liveInput.load(std::memory_order_acquire))
            collector.reset(sampleRate);
    }

    void releaseResources() override
    {
        synthesiser.allNotesOff(0, false);
        if (liveInput.load(std::memory_order_acquire))
            collector.reset(44'100.0);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& output) override
    {
        output.clearActiveBufferRegion();
        if (output.buffer == nullptr || output.numSamples <= 0)
            return;

        if (owner.modeTimelineEnabled_.load(std::memory_order_acquire)) {
            const auto at = owner.modePosition_.load(std::memory_order_relaxed);
            const auto previous = positionSeconds.load(std::memory_order_relaxed);
            const auto active=owner.modeMidiPlaying_.load(std::memory_order_relaxed);
            if (playing.load(std::memory_order_relaxed)!=active || std::abs(at-previous) > 2.0 / std::max(1.0, synthesiser.getSampleRate())) {
                needsAllNotesOff.store(true, std::memory_order_relaxed);
                needsChase.store(active, std::memory_order_relaxed);
            }
            positionSeconds.store(at, std::memory_order_relaxed);
            playing.store(active, std::memory_order_relaxed);
        }
        juce::MidiBuffer midi;
        // Where this block starts, for the automation of the effects.
        const auto automationSeconds = positionSeconds.load(std::memory_order_relaxed);
        // The notes heard live, apart from the clips', for the lights.
        auto liveNoteOns = 0;
        if (liveInput.load(std::memory_order_acquire)) {
            collector.removeNextBlockOfMessages(midi, output.numSamples);
            if (auto* extra = hardware.load(std::memory_order_acquire)) {
                juce::MidiBuffer incoming;
                extra->removeNextBlockOfMessages(incoming, output.numSamples);
                midi.addEvents(incoming, 0, output.numSamples, 0);
            }
            keyboard.processNextMidiBuffer(midi, 0, output.numSamples, true);
            // What the keyboard holds down, to let go of it alone later.
            for (const auto metadata : midi) {
                const auto message = metadata.getMessage();
                if (message.isNoteOn())
                    ++liveNoteOns;
                const auto channel = static_cast<std::size_t>(juce::jlimit(1, 16, message.getChannel()) - 1);
                if (message.isNoteOn())
                    liveHeld[channel].set(static_cast<std::size_t>(message.getNoteNumber()));
                else if (message.isNoteOff())
                    liveHeld[channel].reset(static_cast<std::size_t>(message.getNoteNumber()));
                else if (message.isSustainPedalOn())
                    liveSustain.set(channel);
                else if (message.isSustainPedalOff())
                    liveSustain.reset(channel);
                else if (message.isAllNotesOff() || message.isAllSoundOff())
                    liveHeld[channel].reset();
            }
            const auto rate = std::max(1.0, synthesiser.getSampleRate());
            const auto blockPosition = positionSeconds.load(
                std::memory_order_acquire);
            const auto shouldWrap = looping.load(std::memory_order_relaxed);
            const auto loopStart = loopStartSeconds.load(
                std::memory_order_relaxed);
            const auto loopEnd = loopEndSeconds.load(std::memory_order_relaxed);
            for (const auto metadata : midi) {
                const auto message = metadata.getMessage();
                if (!message.isNoteOnOrOff())
                    continue;
                auto timelineSeconds = blockPosition
                                       + static_cast<double>(
                                             metadata.samplePosition)
                                             / rate;
                if (shouldWrap && loopEnd > loopStart
                    && timelineSeconds >= loopEnd)
                    timelineSeconds = loopStart
                                      + std::fmod(timelineSeconds - loopStart,
                                                  loopEnd - loopStart);
                (void) owner.enqueueMidiRecordingMessage(message,
                                                         timelineSeconds);
            }
        }

        if (needsAllNotesOff.exchange(false, std::memory_order_acq_rel)) {
            for (int channel = 1; channel <= 16; ++channel)
                midi.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
            for (auto& held : liveHeld)
                held.reset();
            liveSustain.reset();
        }
        if (needsLiveNotesOff.exchange(false, std::memory_order_acq_rel)) {
            for (std::size_t channel = 0; channel < liveHeld.size(); ++channel) {
                for (std::size_t note = 0; note < 128; ++note)
                    if (liveHeld[channel].test(note))
                        midi.addEvent(juce::MidiMessage::noteOff(static_cast<int>(channel) + 1, static_cast<int>(note)), 0);
                if (liveSustain.test(channel))
                    midi.addEvent(juce::MidiMessage::controllerEvent(static_cast<int>(channel) + 1, 64, 0), 0);
                liveHeld[channel].reset();
            }
            liveSustain.reset();
        }

        if (playing.load(std::memory_order_acquire)) {
            const auto notes = sequence.read();
            const auto rate = std::max(1.0, synthesiser.getSampleRate());
            const auto sequenceEnd = sequenceLengthSeconds.load(
                std::memory_order_acquire);
            const auto sessionEnd = std::max(
                sequenceEnd,
                transportLengthSeconds.load(std::memory_order_acquire));
            const auto loopStart = juce::jlimit(
                0.0, std::max(0.0, sessionEnd - 0.000001),
                loopStartSeconds.load(std::memory_order_relaxed));
            const auto loopEnd = juce::jlimit(
                loopStart + 0.000001, std::max(loopStart + 0.000001,
                                               sessionEnd),
                loopEndSeconds.load(std::memory_order_relaxed));
            const auto shouldLoop = looping.load(std::memory_order_relaxed)
                                    && loopEnd > loopStart;
            auto position = positionSeconds.load(std::memory_order_relaxed);
            auto destinationOffset = 0;
            auto remaining = output.numSamples;
            auto chase = needsChase.exchange(false, std::memory_order_acq_rel);

            // A note's expression at a time, as its channel carries it: its
            // pitch bend (24 semitones each way, 14 bits) and its gain (CC 11
            // then 43, 14 bits over -48..+12 dB).
            const auto bendAt = [](const ScheduledNote& note, const double time) {
                const auto position = (time - note.startSeconds) / std::max(1.0e-6, note.endSeconds - note.startSeconds);
                return juce::jlimit(0, 16'383, static_cast<int>(std::lround(8'192.0 + curveAt(note.curves->pitch, position) / 24.0 * 8'192.0)));
            };
            const auto gainAt = [](const ScheduledNote& note, const double time) {
                const auto position = (time - note.startSeconds) / std::max(1.0e-6, note.endSeconds - note.startSeconds);
                return juce::jlimit(0, 16'383, static_cast<int>(std::lround((curveAt(note.curves->gain, position) + 48.0) / 60.0 * 16'383.0)));
            };
            const auto sendBend = [&midi](const ScheduledNote& note, const int value, const int at) {
                midi.addEvent(juce::MidiMessage::pitchWheel(note.channel, value), at);
            };
            const auto sendGain = [&midi](const ScheduledNote& note, const int value, const int at) {
                midi.addEvent(juce::MidiMessage::controllerEvent(note.channel, 43, value & 127), at);
                midi.addEvent(juce::MidiMessage::controllerEvent(note.channel, 11, value >> 7), at);
            };
            // Its timbre (CC 74), pressure (the channel's) and pan (CC 10),
            // 0..127: what the curve says, its rest when it has none.
            const auto partAt = [](const std::vector<NoteExpressionPoint>& points, const ScheduledNote& note, const double time,
                                   const double rest, const double low, const double high) {
                const auto position = (time - note.startSeconds) / std::max(1.0e-6, note.endSeconds - note.startSeconds);
                const auto value = points.empty() ? rest : curveAt(points, position);
                return juce::jlimit(0, 127, static_cast<int>(std::lround((value - low) / (high - low) * 127.0)));
            };
            const auto timbreAt = [&partAt](const ScheduledNote& note, const double time) {
                return partAt(note.curves->timbre, note, time, 0.5, 0.0, 1.0);
            };
            const auto pressureAt = [&partAt](const ScheduledNote& note, const double time) {
                return partAt(note.curves->pressure, note, time, 0.0, 0.0, 1.0);
            };
            const auto panAt = [&partAt](const ScheduledNote& note, const double time) {
                return partAt(note.curves->pan, note, time, 0.0, -1.0, 1.0);
            };
            // Bend and timbre before the note-on (the voice starts on them),
            // gain, pressure and pan after it (its voice there to take them).
            const auto addExpression = [&](const ScheduledNote& note, const double time, const int at, const bool beforeNote) {
                if (beforeNote) {
                    sendBend(note, bendAt(note, time), at);
                    midi.addEvent(juce::MidiMessage::controllerEvent(note.channel, 74, timbreAt(note, time)), at);
                } else {
                    sendGain(note, gainAt(note, time), at);
                    midi.addEvent(juce::MidiMessage::channelPressureChange(note.channel, pressureAt(note, time)), at);
                    midi.addEvent(juce::MidiMessage::controllerEvent(note.channel, 10, panAt(note, time)), at);
                }
            };
            // A note with a chance plays this time or not: rolled as it comes.
            const auto roll = [this](const ScheduledNote& note) {
                if (note.chance >= 1.0f) {
                    note.silenced = false;
                    return;
                }
                chanceSeed ^= chanceSeed << 13;
                chanceSeed ^= chanceSeed >> 17;
                chanceSeed ^= chanceSeed << 5;
                note.silenced = static_cast<float>(chanceSeed & 0xffffffu) / 16'777'216.0f >= note.chance;
            };
            const auto addEvents = [&](const double start, const int count, const int offset, const bool shouldChase) {
                const auto end = start + static_cast<double>(count) / rate;
                if (shouldChase) {
                    for (int channel = 1; channel <= 16; ++channel)
                        midi.addEvent(juce::MidiMessage::allNotesOff(channel), offset);
                    for (const auto& note : *notes)
                        if (note.startSeconds < start && note.endSeconds > start) {
                            roll(note);
                            if (note.silenced)
                                continue;
                            if (note.curves != nullptr)
                                addExpression(note, start, offset, true);
                            midi.addEvent(juce::MidiMessage::noteOn(note.channel, note.noteNumber, note.velocity), offset);
                            if (note.curves != nullptr)
                                addExpression(note, start, offset, false);
                        }
                }

                for (const auto& note : *notes) {
                    if (note.startSeconds >= start && note.startSeconds < end)
                        roll(note);
                    if (note.silenced)
                        continue;
                    if (note.startSeconds >= start && note.startSeconds < end) {
                        const auto eventOffset = offset + juce::jlimit(
                            0, count - 1, static_cast<int>(std::llround((note.startSeconds - start) * rate)));
                        // The bend before the note (the voice starts on it),
                        // the gain once it is there.
                        if (note.curves != nullptr)
                            addExpression(note, note.startSeconds, eventOffset, true);
                        midi.addEvent(juce::MidiMessage::noteOn(note.channel, note.noteNumber, note.velocity), eventOffset);
                        if (note.curves != nullptr)
                            addExpression(note, note.startSeconds, eventOffset, false);
                    }
                    // Along it: every 64 samples of the timeline, what changed.
                    if (note.curves != nullptr && note.startSeconds < end && note.endSeconds > start) {
                        const auto tick = 64.0 / rate;
                        for (auto step = std::ceil(start / tick); step * tick < end; step += 1.0) {
                            const auto time = step * tick;
                            if (time <= note.startSeconds || time >= note.endSeconds)
                                continue;
                            const auto before = std::max(note.startSeconds, time - tick);
                            const auto at = offset + juce::jlimit(0, count - 1, static_cast<int>(std::llround((time - start) * rate)));
                            if (!note.curves->pitch.empty())
                                if (const auto value = bendAt(note, time); value != bendAt(note, before))
                                    sendBend(note, value, at);
                            if (!note.curves->gain.empty())
                                if (const auto value = gainAt(note, time); value != gainAt(note, before))
                                    sendGain(note, value, at);
                            if (!note.curves->timbre.empty())
                                if (const auto value = timbreAt(note, time); value != timbreAt(note, before))
                                    midi.addEvent(juce::MidiMessage::controllerEvent(note.channel, 74, value), at);
                            if (!note.curves->pressure.empty())
                                if (const auto value = pressureAt(note, time); value != pressureAt(note, before))
                                    midi.addEvent(juce::MidiMessage::channelPressureChange(note.channel, value), at);
                            if (!note.curves->pan.empty())
                                if (const auto value = panAt(note, time); value != panAt(note, before))
                                    midi.addEvent(juce::MidiMessage::controllerEvent(note.channel, 10, value), at);
                        }
                    }
                    if (note.endSeconds >= start && note.endSeconds < end) {
                        const auto eventOffset = offset + juce::jlimit(
                            0, count - 1, static_cast<int>(std::llround((note.endSeconds - start) * rate)));
                        midi.addEvent(juce::MidiMessage::noteOff(note.channel, note.noteNumber), eventOffset);
                    }
                }
            };

            while (remaining > 0) {
                if (shouldLoop && position >= loopEnd) {
                    position = loopStart
                               + std::fmod(position - loopStart,
                                           loopEnd - loopStart);
                    chase = true;
                }

                if (!shouldLoop && position >= sessionEnd) {
                    playing.store(false, std::memory_order_release);
                    for (int channel = 1; channel <= 16; ++channel)
                        midi.addEvent(juce::MidiMessage::allNotesOff(channel),
                                      destinationOffset);
                    break;
                }

                auto segment = remaining;
                const auto playbackEnd = shouldLoop ? loopEnd : sessionEnd;
                const auto samplesToEnd = juce::jmax(
                    1, static_cast<int>(std::ceil(
                           (playbackEnd - position) * rate)));
                segment = std::min(segment, samplesToEnd);
                addEvents(position, segment, destinationOffset, chase);
                chase = false;
                position += static_cast<double>(segment) / rate;
                destinationOffset += segment;
                remaining -= segment;

                if (shouldLoop && position >= loopEnd && remaining > 0) {
                    for (int channel = 1; channel <= 16; ++channel)
                        midi.addEvent(juce::MidiMessage::allNotesOff(channel), destinationOffset);
                    position = loopStart;
                    chase = true;
                }
            }
            positionSeconds.store(position, std::memory_order_relaxed);
        }
        readVoiceModulations(automationSeconds);
        synthesiser.advance(static_cast<double>(output.numSamples) / std::max(1.0, synthesiser.getSampleRate()),
                            NativeDspRuntime::sessionTempo());
        auto instrumentPlays = false;
        {
            // The block's one borrow of the plan: the voices, the voicing
            // and the sampler's card read it through heardPlan.
            const auto published = instrumentGraph.read();
            instrumentPlays = published != nullptr && published->enabled;
            followSamplerCard(published.get());
            // The notes its clips played this block, for the lights.
            {
                auto noteOns = 0;
                for (const auto metadata : midi)
                    if (metadata.numBytes == 3 && (metadata.data[0] & 0xf0) == 0x90 && metadata.data[2] > 0)
                        ++noteOns;
                if (noteOns > liveNoteOns)
                    clipNotes.fetch_add(static_cast<std::uint32_t>(noteOns - liveNoteOns), std::memory_order_relaxed);
            }
            // The MIDI effects between the notes and the voices, on the
            // transport's beat while it plays.
            {
                cydaw::engine::MidiEffectClock clock;
                clock.sampleRate = std::max(1.0, synthesiser.getSampleRate());
                clock.beatsPerMinute = NativeDspRuntime::sessionTempo();
                clock.playing = playing.load(std::memory_order_relaxed) && NativeDspRuntime::sessionPlaying();
                clock.quarterNotes = std::max(0.0, automationSeconds) * clock.beatsPerMinute / 60.0;
                midiEffects.apply(midi, 0, output.numSamples, published != nullptr ? published->midiEffects.get() : nullptr, clock);
            }
            // Its MIDI output: the notes as its voices hear them.
            if (outQueue->device.load(std::memory_order_acquire) != nullptr)
                for (const auto metadata : midi)
                    if (metadata.numBytes > 0 && metadata.numBytes <= 3) {
                        MidiOutQueue::Event event;
                        std::copy_n(metadata.data, metadata.numBytes, event.bytes.begin());
                        event.size = static_cast<std::uint8_t>(metadata.numBytes);
                        (void) outQueue->ring.tryPush(event);
                    }
            renderVoices(published.get(), *output.buffer, midi, output.startSample, output.numSamples);
            // An instrument container's pads: each its instrument and its effects.
            renderPads(published.get(), midi, output, automationSeconds);
            // What the notes say, for the modulators (as the voices heard them).
            followNotes(midi);
        }
        publishVoiceLive(output.numSamples);
        voicesDelay.process(*output.buffer, output.startSample, output.numSamples);
        // What is routed into its main input (a hybrid track's audio clips):
        // mixed after the instrument, before what follows it. Without an
        // instrument that chain has no sound to start from (it reads the
        // instrument's): the routed sound goes past it then.
        const auto mixRouted = [&] {
            if (const auto* main = routedInputs[0]; main != nullptr) {
                const auto count = std::min(output.numSamples, routedSamples);
                const auto sourceChannels = main->getNumChannels();
                for (auto channel = 0; channel < output.buffer->getNumChannels() && sourceChannels > 0; ++channel)
                    output.buffer->addFrom(channel, output.startSample, *main, std::min(channel, sourceChannels - 1), 0, count);
            }
        };
        if (instrumentPlays)
            mixRouted();
        effects.setExtraInputs(routedInputs.data() + 1, RoutedInputReceiver::maximumBuses - 1);
        effects.setControlNotes(controlNotes);
        // The region was cleared at the top of this block and only this
        // track's voices have written to it, so what follows the instrument
        // can be processed in place. No scratch buffer, no allocation.
        (void) effects.process(*output.buffer, output.startSample,
                               output.numSamples, automationSeconds);
        if (!instrumentPlays)
            mixRouted();
    }

    // Everything the graph places after the instrument. Compiled here, on the
    // message thread; the audio thread only ever reads the published plan.
    juce::Result setEffectGraph(const cydaw::engine::NativeDspGraph& graph)
    {
        effects.prepare(sampleRateHz.load(std::memory_order_acquire),
                        blockSize.load(std::memory_order_acquire), 2);
        return effects.setGraph(graph);
    }

    juce::Result updateEffectParameters(
        const cydaw::engine::NativeDspGraph& graph)
    {
        return effects.updateParameters(graph);
    }

    // How late what follows the instrument makes its sound.
    [[nodiscard]] int effectLatencySamples() const noexcept { return effects.latencySamples(); }

    void setSequence(std::shared_ptr<const std::vector<ScheduledNote>> notes,
                     const double lengthSeconds)
    {
        sequence.store(std::move(notes));
        sequenceLengthSeconds.store(std::max(0.0, lengthSeconds), std::memory_order_release);
        needsChase.store(true, std::memory_order_release);
    }

    void setTransportRange(const double sessionLength,
                           const double loopStart,
                           const double loopEnd) noexcept
    {
        const auto safeLength = std::max(0.000001, sessionLength);
        const auto safeStart = juce::jlimit(
            0.0, std::max(0.0, safeLength - 0.000001), loopStart);
        transportLengthSeconds.store(safeLength, std::memory_order_release);
        loopStartSeconds.store(safeStart, std::memory_order_release);
        loopEndSeconds.store(juce::jlimit(
                                 safeStart + 0.000001, safeLength, loopEnd),
                             std::memory_order_release);
    }

    void setPlaying(const bool shouldPlay) noexcept
    {
        playing.store(shouldPlay, std::memory_order_release);
        if (shouldPlay)
            needsChase.store(true, std::memory_order_release);
        else
            needsAllNotesOff.store(true, std::memory_order_release);
    }

    void setPosition(const double seconds) noexcept
    {
        positionSeconds.store(std::max(0.0, seconds), std::memory_order_release);
        needsChase.store(true, std::memory_order_release);
    }

    void setLooping(const bool shouldLoop) noexcept { looping.store(shouldLoop, std::memory_order_release); }
    void setLiveInput(const bool shouldAcceptInput) noexcept
    {
        if (liveInput.exchange(shouldAcceptInput, std::memory_order_acq_rel) == shouldAcceptInput)
            return;
        // Only what the keyboard holds is let go: the notes the track's clip
        // plays go on (they were cut when another track was picked).
        if (!shouldAcceptInput)
            needsLiveNotesOff.store(true, std::memory_order_release);
    }
    [[nodiscard]] bool isPlaying() const noexcept { return playing.load(std::memory_order_acquire); }
    [[nodiscard]] double position() const noexcept { return positionSeconds.load(std::memory_order_acquire); }
    [[nodiscard]] double length() const noexcept { return sequenceLengthSeconds.load(std::memory_order_acquire); }

    void setInstrumentGraph(cydaw::engine::PolySynthGraphPlan graph)
    {
        // The plan's count is a label (the plan as compared and saved); the
        // synthesiser has its 32 voices whatever it says.
        graph.voiceCount = 16;
        // A sound model and a sampler play at their recording's level: up to
        // unity.
        graph.oscillatorLevel = juce::jlimit(
            0.0f, cydaw::engine::playsRecordingLevel(graph.waveform) ? 1.0f : 0.5f, graph.oscillatorLevel);
        graph.attackSeconds = juce::jlimit(0.0001f, 5.0f, graph.attackSeconds);
        graph.decaySeconds = juce::jlimit(0.0001f, 5.0f, graph.decaySeconds);
        graph.sustainLevel = juce::jlimit(0.0f, 1.0f, graph.sustainLevel);
        graph.releaseSeconds = juce::jlimit(0.0001f, 10.0f, graph.releaseSeconds);
        graph.filterCutoffHz = juce::jlimit(20.0f, 20'000.0f,
                                            graph.filterCutoffHz);
        instrumentGraph.store(
            std::make_shared<const cydaw::engine::PolySynthGraphPlan>(graph));
    }

    [[nodiscard]] cydaw::engine::PolySynthGraphPlan instrumentPlan() const
    {
        if (const auto graph = instrumentGraph.current())
            return *graph;
        return {};
    }

    // The instrument's control cables that reach its voices, and what they
    // carry this block: the effect chain's control nodes, read before the
    // voices render (they were evaluated by the chain the block before).
    // What moves the voices this block: the automation lanes at the block's
    // place on the timeline, then the cables, which win on the same knob as
    // the voices apply them in order.
    // A sampler's controllers and articulation follow its card, as the lanes
    // leave them this block.
    void followSamplerCard(const cydaw::engine::PolySynthGraphPlan* plan) noexcept
    {
        if (plan == nullptr || plan->waveform != cydaw::engine::PolyOscillatorWaveform::sampler)
            return;
        auto card = plan->samplerControllers;
        auto articulation = cydaw::engine::samplerValue(plan->samplerValues, cydaw::engine::SamplerSetting::articulation);
        for (std::size_t index = 0; index < voiceModulation.count; ++index) {
            const auto value = voiceModulation.values[index];
            if (!std::isfinite(value) || voiceModulation.depths[index] != 0.0f)
                continue;
            const auto at = static_cast<std::size_t>(voiceModulation.indices[index]);
            if (voiceModulation.targets[index] == cydaw::engine::PolyVoiceTarget::samplerController && at < card.size())
                card[at] = std::clamp(value, 0.0f, 127.0f);
            else if (voiceModulation.targets[index] == cydaw::engine::PolyVoiceTarget::samplerValue
                     && at == static_cast<std::size_t>(cydaw::engine::SamplerSetting::articulation))
                articulation = value;
        }
        synthesiser.context.sampler.follow(plan->sampler.get(), card, articulation);
    }

    // What the newest voice plays (a Prisme's, a synth's of the family), for
    // the interface: about fifty times a second, the audio thread's to write.
    void publishVoiceLive(const int frames) noexcept
    {
        liveCountdown -= frames;
        if (liveCountdown > 0)
            return;
        liveCountdown = std::max(256, static_cast<int>(synthesiser.getSampleRate() / 50.0));
        const GraphSynthVoice* newest {};
        for (int index = 0; index < synthesiser.getNumVoices(); ++index) {
            const auto* voice = dynamic_cast<const GraphSynthVoice*>(synthesiser.getVoice(index));
            if (voice != nullptr && voice->isVoiceActive() && (newest == nullptr || newest->wasStartedBefore(*voice)))
                newest = voice;
        }
        const auto playing = newest != nullptr && newest->live(liveScratch);
        live.write(playing ? &liveScratch : nullptr);
    }

    // The voices, rendered from `plan`: in pieces of 32 frames while its
    // values glide toward it (a knob turned), whole otherwise.
    void renderVoices(const cydaw::engine::PolySynthGraphPlan* plan, juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi,
                      const int start, const int count) noexcept
    {
        if (plan == nullptr || !glide.begin(*plan)) {
            heardPlan = plan;
            synthesiser.renderNextBlock(buffer, midi, start, count);
        } else {
            const auto rate = std::max(1.0, synthesiser.getSampleRate());
            for (int done = 0; done < count; done += 32) {
                const auto frames = std::min(32, count - done);
                heardPlan = &glide.step(*plan, frames, rate);
                synthesiser.renderNextBlock(buffer, midi, start + done, frames);
            }
            glide.end();
        }
        heardPlan = nullptr;
    }

    // The block's notes and controllers into what the modulators read: the
    // keys held, the last note struck, the wheels, every controller.
    void followNotes(const juce::MidiBuffer& midi) noexcept
    {
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) {
                ++controlNotes.noteOns;
                keysDown.set(static_cast<std::size_t>(message.getNoteNumber() & 127));
                controlNotes.note = static_cast<float>(message.getNoteNumber());
                controlNotes.velocity = message.getFloatVelocity();
            } else if (message.isNoteOff()) {
                keysDown.reset(static_cast<std::size_t>(message.getNoteNumber() & 127));
            } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
                keysDown.reset();
            } else if (message.isController()) {
                const auto number = message.getControllerNumber() & 127;
                const auto value = static_cast<float>(message.getControllerValue()) / 127.0f;
                controlNotes.controllers[static_cast<std::size_t>(number)] = value;
                if (number == 1)
                    controlNotes.modWheel = value;
            } else if (message.isChannelPressure()) {
                controlNotes.aftertouch = static_cast<float>(message.getChannelPressureValue()) / 127.0f;
            } else if (message.isAftertouch()) {
                controlNotes.aftertouch = static_cast<float>(message.getAfterTouchValue()) / 127.0f;
            } else if (message.isPitchWheel()) {
                controlNotes.pitchBend = static_cast<float>(message.getPitchWheelValue() - 8'192) / 8'192.0f;
            }
        }
        controlNotes.held = static_cast<int>(keysDown.count());
    }

    void readVoiceModulations(const double seconds) noexcept
    {
        voiceModulation.count = 0;
        if (const auto lanes = voiceAutomation.read(); lanes != nullptr && std::isfinite(seconds))
            for (const auto& lane : *lanes) {
                if (voiceModulation.count >= VoiceModulationState::capacity)
                    break;
                voiceModulation.targets[voiceModulation.count] = lane.target;
                voiceModulation.indices[voiceModulation.count] = lane.index;
                voiceModulation.depths[voiceModulation.count] = 0.0f;
                voiceModulation.values[voiceModulation.count]
                    = std::clamp(NativeDspRuntime::automationValueAt(lane.points, seconds, lane.logarithmic), lane.minimum, lane.maximum);
                ++voiceModulation.count;
            }
        const auto routes = voiceModulations.read();
        if (routes == nullptr || routes->empty())
            return;
        const auto first = voiceModulation.count;
        std::array<std::uint64_t, VoiceModulationState::capacity> ids {};
        const auto count = std::min(routes->size(), VoiceModulationState::capacity - first);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& route = (*routes)[index];
            ids[index] = route.controlNodeId;
            voiceModulation.targets[first + index] = route.target;
            voiceModulation.indices[first + index] = route.index;
            voiceModulation.depths[first + index] = route.depth;
            voiceModulation.minimums[first + index] = route.minimum;
            voiceModulation.maximums[first + index] = route.maximum;
            voiceModulation.logarithmic[first + index] = route.logarithmic;
        }
        effects.controlValuesNow(ids.data(), voiceModulation.values.data() + first, count);
        voiceModulation.count = first + count;
    }

    cydaw::realtime::RtPublisher<cydaw::engine::PolySynthGraphPlan> instrumentGraph;
    // What the voices play this block, while it renders (audio thread).
    const cydaw::engine::PolySynthGraphPlan* heardPlan {};
    PlanGlide glide;
    // What the newest voice plays, published for the interface.
    VoiceLiveFeed live;
    cydaw::engine::VoiceLive liveScratch {};
    int liveCountdown {};
    cydaw::realtime::RtPublisher<std::vector<cydaw::engine::PolyVoiceModulation>> voiceModulations;
    cydaw::realtime::RtPublisher<std::vector<AudioTrackController::VoiceAutomationLane>> voiceAutomation;
    VoiceModulationState voiceModulation;
    NativeDspRuntime effects;
    MidiEffectsHost midiEffects;
    // The notes as the modulators hear them (audio thread).
    cydaw::engine::modulator::ControlNotes controlNotes;
    std::bitset<128> keysDown;

    // ---- A rack of pads (the "drum-rack" card) ----
    // A pad: an instrument and its effects of their own, played by the
    // track's notes of its key (as another note when it says so), summed
    // with the others before the track's effects. Made on the message
    // thread; never freed while the engine lives (the audio thread may hold
    // it), only emptied.
    struct RackPad {
        RackPad()
        {
            plan.store(std::make_shared<const cydaw::engine::PolySynthGraphPlan>());
            for (int voice = 0; voice < 12; ++voice)
                synthesiser.addVoice(new GraphSynthVoice(plan, modulation, &synthesiser.context));
            synthesiser.addSound(new GraphSynthSound());
            synthesiser.voicing = [this] {
                const auto current = plan.read();
                return current != nullptr ? PlanSynthesiser::voicingOf(*current) : PlanSynthesiser::Voicing {};
            };
            synthesiser.noteInfo = [this](const int note, const float velocity) {
                const auto current = plan.read();
                return current != nullptr ? PlanSynthesiser::noteInfoOf(*current, note, velocity, synthesiser.context.sampler)
                                          : PlanSynthesiser::NoteInfo {};
            };
            notes.ensureSize(2'048);
        }
        void prepare(const double rate, const int block)
        {
            synthesiser.setCurrentPlaybackSampleRate(std::max(1.0, rate));
            effects.prepare(std::max(1.0, rate), std::max(1, block), 2);
            buffer.setSize(2, std::max(8'192, block), false, true, true);
        }
        cydaw::realtime::RtPublisher<cydaw::engine::PolySynthGraphPlan> plan;
        VoiceModulationState modulation;
        PlanSynthesiser synthesiser;
        NativeDspRuntime effects;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer notes;
        std::atomic<int> note { -1 };   // the key that plays it; -1: none
        std::atomic<int> playAs { -1 }; // the note it plays; -1: the key's own
        std::atomic<float> gain { 1.0f };
        std::atomic<float> pan {};
        std::atomic<bool> muted {};
        std::atomic<bool> solo {};
        std::atomic<int> choke {};      // pads of one group cut each other; 0: none
        // A layer's keys and velocities.
        std::atomic<int> keyLow {};
        std::atomic<int> keyHigh { 127 };
        std::atomic<int> velocityLow { 1 };
        std::atomic<int> velocityHigh { 127 };
        std::atomic<float> peak {};     // for its meter
    };
    static constexpr std::size_t maximumPads = AudioTrackController::maximumRackPads;
    // An Instrument Selector's choice (audio thread): the layer a new note
    // goes to, where each sounding note began, the turn and the dice.
    int selectorLayer {};
    int selectorManual { -1 };
    int selectorTurn {};
    std::uint32_t selectorDice { 0x9e3779b9u };
    std::array<std::array<std::int8_t, 128>, 16> noteLayers = [] {
        std::array<std::array<std::int8_t, 128>, 16> layers {};
        for (auto& channel : layers)
            channel.fill(-1);
        return layers;
    }();
    // The layer it plays now, for the interface.
    std::atomic<int> selectedLayer { -1 };
    std::array<std::atomic<RackPad*>, maximumPads> pads {};
    std::array<std::unique_ptr<RackPad>, maximumPads> padStore;

    // The pad at `index`, made (and prepared) the first time it is asked for.
    RackPad* padAt(const std::size_t index)
    {
        if (index >= maximumPads)
            return nullptr;
        if (padStore[index] == nullptr) {
            auto made = std::make_unique<RackPad>();
            made->prepare(sampleRateHz.load(std::memory_order_acquire), blockSize.load(std::memory_order_acquire));
            padStore[index] = std::move(made);
            pads[index].store(padStore[index].get(), std::memory_order_release);
        }
        return padStore[index].get();
    }

    // The pads' sound, added into the block (before the track's effects):
    // as the track's instrument card has them hear its notes (`trackPlan`).
    void renderPads(const cydaw::engine::PolySynthGraphPlan* trackPlan, const juce::MidiBuffer& midi, const juce::AudioSourceChannelInfo& output,
                    const double timelineSeconds) noexcept
    {
        namespace container = cydaw::engine::instrument_container;
        std::array<RackPad*, maximumPads> playing {};
        // Each pad by its place (a layer's number), null when it is silent.
        std::array<RackPad*, maximumPads> byPlace {};
        std::size_t count = 0;
        auto layers = 0;
        auto soloed = false;
        for (std::size_t place = 0; place < maximumPads; ++place)
            if (auto* pad = pads[place].load(std::memory_order_acquire); pad != nullptr && pad->note.load(std::memory_order_relaxed) >= 0) {
                playing[count++] = pad;
                byPlace[place] = pad;
                layers = static_cast<int>(place) + 1;
                soloed = soloed || pad->solo.load(std::memory_order_relaxed);
            }
        const auto kind = trackPlan != nullptr && trackPlan->containerKind != container::Kind::none ? trackPlan->containerKind
                                                                                                    : container::Kind::drum;
        if (count == 0 || kind != container::Kind::selector)
            selectedLayer.store(-1, std::memory_order_relaxed);
        if (count == 0)
            return;
        const auto frames = output.numSamples;
        for (std::size_t index = 0; index < count; ++index)
            playing[index]->notes.clear();
        // An Instrument Layer: every layer each note in its keys and
        // velocities (the note's end to every layer of its keys), the
        // controllers to all.
        // An Instrument Selector: each note to one layer (its end where it
        // began), the controllers to all.
        if (kind == container::Kind::layer || kind == container::Kind::selector) {
            const auto& values = trackPlan->containerValues;
            const auto mode = static_cast<container::Selection>(std::clamp(static_cast<int>(values[container::mode]), 0,
                                                                            container::selectionCount - 1));
            const auto manual = std::clamp(static_cast<int>(values[container::layer]) - 1, 0, layers - 1);
            // The knob turned: it chooses (and is where a switch starts from).
            if (manual != selectorManual) {
                selectorManual = manual;
                selectorLayer = manual;
            }
            selectorLayer = std::clamp(selectorLayer, 0, layers - 1);
            const auto controllerNumber = static_cast<int>(values[container::controller]);
            const auto lowestSwitch = static_cast<int>(values[container::keyswitch]);
            const auto send = [](RackPad* pad, const std::uint8_t* data, const int size, const int at) {
                if (pad != nullptr)
                    pad->notes.addEvent(data, size, at);
            };
            for (const auto metadata : midi) {
                const auto* data = metadata.data;
                const auto size = metadata.numBytes;
                if (size < 1)
                    continue;
                const auto type = data[0] & 0xF0;
                const auto channel = data[0] & 0x0F;
                const auto isNote = (type == 0x90 || type == 0x80) && size >= 3;
                if (!isNote) {
                    if (kind == container::Kind::selector) {
                        if (mode == container::Selection::controller && type == 0xB0 && size >= 3 && data[1] == controllerNumber)
                            selectorLayer = std::min(layers - 1, data[2] * layers / 128);
                        if (mode == container::Selection::program && type == 0xC0 && size >= 2)
                            selectorLayer = data[1] % layers;
                        // Every note let go: none sounds anywhere any more.
                        if (type == 0xB0 && size >= 3 && (data[1] == 120 || data[1] == 123))
                            noteLayers[static_cast<std::size_t>(channel)].fill(-1);
                    }
                    for (std::size_t index = 0; index < count; ++index)
                        send(playing[index], data, size, metadata.samplePosition);
                    continue;
                }
                const auto key = static_cast<int>(data[1]);
                const auto on = type == 0x90 && data[2] > 0;
                if (kind == container::Kind::layer) {
                    for (std::size_t index = 0; index < count; ++index) {
                        auto* pad = playing[index];
                        if (key < pad->keyLow.load(std::memory_order_relaxed) || key > pad->keyHigh.load(std::memory_order_relaxed))
                            continue;
                        if (on && (data[2] < pad->velocityLow.load(std::memory_order_relaxed)
                                   || data[2] > pad->velocityHigh.load(std::memory_order_relaxed)))
                            continue;
                        send(pad, data, 3, metadata.samplePosition);
                    }
                    continue;
                }
                // A key switch chooses and plays nothing.
                if (mode == container::Selection::keyswitch && key >= lowestSwitch && key < lowestSwitch + layers) {
                    if (on)
                        selectorLayer = key - lowestSwitch;
                    continue;
                }
                auto& began = noteLayers[static_cast<std::size_t>(channel)][static_cast<std::size_t>(key)];
                if (!on) {
                    if (began >= 0)
                        send(byPlace[static_cast<std::size_t>(began)], data, 3, metadata.samplePosition);
                    began = -1;
                    continue;
                }
                auto chosen = selectorLayer;
                if (mode == container::Selection::manual)
                    chosen = manual;
                else if (mode == container::Selection::roundRobin) {
                    chosen = selectorTurn % layers;
                    selectorTurn = (selectorTurn + 1) % layers;
                } else if (mode == container::Selection::random && layers > 1) {
                    // Another layer than the last, at random.
                    selectorDice ^= selectorDice << 13;
                    selectorDice ^= selectorDice >> 17;
                    selectorDice ^= selectorDice << 5;
                    chosen = (selectorLayer + 1 + static_cast<int>(selectorDice % static_cast<std::uint32_t>(layers - 1))) % layers;
                }
                selectorLayer = chosen;
                // The same key still sounding elsewhere ends there first.
                if (began >= 0 && began != chosen) {
                    const std::uint8_t off[3] { static_cast<std::uint8_t>(0x80 | channel), data[1], 0 };
                    send(byPlace[static_cast<std::size_t>(began)], off, 3, metadata.samplePosition);
                }
                began = static_cast<std::int8_t>(chosen);
                send(byPlace[static_cast<std::size_t>(chosen)], data, 3, metadata.samplePosition);
            }
            if (kind == container::Kind::selector)
                selectedLayer.store(mode == container::Selection::manual ? manual : selectorLayer, std::memory_order_relaxed);
        }
        // A Drum Machine: each pad its key's notes (as its own note, or the
        // one it plays), the controllers to all; a pad's note cuts the others
        // of its group.
        for (const auto metadata : midi) {
            if (kind != container::Kind::drum)
                break;
            const auto* data = metadata.data;
            const auto size = metadata.numBytes;
            if (size < 1)
                continue;
            const auto type = data[0] & 0xF0;
            const auto isNote = (type == 0x90 || type == 0x80) && size >= 3;
            for (std::size_t index = 0; index < count; ++index) {
                auto* pad = playing[index];
                if (!isNote) {
                    pad->notes.addEvent(data, size, metadata.samplePosition);
                    continue;
                }
                if (data[1] != pad->note.load(std::memory_order_relaxed))
                    continue;
                const auto as = pad->playAs.load(std::memory_order_relaxed);
                const std::uint8_t raw[3] { data[0], static_cast<std::uint8_t>(as >= 0 ? std::min(as, 127) : data[1]), data[2] };
                pad->notes.addEvent(raw, 3, metadata.samplePosition);
                const auto group = pad->choke.load(std::memory_order_relaxed);
                if (type == 0x90 && data[2] > 0 && group > 0)
                    for (std::size_t other = 0; other < count; ++other)
                        if (other != index && playing[other]->choke.load(std::memory_order_relaxed) == group)
                            playing[other]->notes.addEvent(juce::MidiMessage::allNotesOff((data[0] & 0x0F) + 1), metadata.samplePosition);
            }
        }
        const auto rate = std::max(1.0, synthesiser.getSampleRate());
        for (std::size_t index = 0; index < count; ++index) {
            auto* pad = playing[index];
            if (frames > pad->buffer.getNumSamples())
                continue;
            const auto plan = pad->plan.read();
            pad->synthesiser.advance(static_cast<double>(frames) / rate, NativeDspRuntime::sessionTempo(), plan.get());
            pad->buffer.clear(0, frames);
            pad->synthesiser.renderNextBlock(pad->buffer, pad->notes, 0, frames);
            (void) pad->effects.process(pad->buffer, 0, frames, timelineSeconds);
            const auto audible = !pad->muted.load(std::memory_order_relaxed)
                                 && (!soloed || pad->solo.load(std::memory_order_relaxed));
            const auto gain = audible ? pad->gain.load(std::memory_order_relaxed) : 0.0f;
            const auto pan = std::clamp(pad->pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
            const auto left = gain * std::min(1.0f, 1.0f - pan);
            const auto right = gain * std::min(1.0f, 1.0f + pan);
            auto peak = 0.0f;
            const auto channels = output.buffer->getNumChannels();
            if (channels > 0) {
                output.buffer->addFrom(0, output.startSample, pad->buffer, 0, 0, frames, left);
                peak = std::max(peak, pad->buffer.getMagnitude(0, 0, frames) * left);
            }
            if (channels > 1) {
                output.buffer->addFrom(1, output.startSample, pad->buffer, 1, 0, frames, right);
                peak = std::max(peak, pad->buffer.getMagnitude(1, 0, frames) * right);
            }
            const auto falling = pad->peak.load(std::memory_order_relaxed) * std::pow(0.5f, static_cast<float>(frames) / static_cast<float>(rate * 0.15));
            pad->peak.store(std::max(peak, falling), std::memory_order_relaxed);
        }
    }

    std::atomic<int> blockSize { 512 };
    std::atomic<double> sampleRateHz { 48'000.0 };
    PlanSynthesiser synthesiser;
    AudioTrackController& owner;
    juce::MidiMessageCollector& collector;
    juce::MidiKeyboardState& keyboard;
    cydaw::realtime::RtPublisher<std::vector<ScheduledNote>> sequence;
    std::atomic<double> sequenceLengthSeconds {};
    std::atomic<double> transportLengthSeconds { 0.000001 };
    std::atomic<double> loopStartSeconds {};
    std::atomic<double> loopEndSeconds { 0.000001 };
    std::atomic<double> positionSeconds {};
    std::atomic<bool> playing {};
    std::atomic<bool> looping {};
    std::atomic<bool> needsChase {};
    // The notes' chance: a roll each time one comes (the audio thread's).
    std::uint32_t chanceSeed { 0x9e3779b9u };
    std::atomic<bool> needsAllNotesOff {};
    std::atomic<bool> liveInput {};
    // The notes and pedals the live input holds (audio thread only).
    std::array<std::bitset<128>, 16> liveHeld {};
    std::bitset<16> liveSustain {};
    std::atomic<bool> needsLiveNotesOff {};
    // The MIDI keyboards' collector as well as its own (the simulator's).
    std::atomic<juce::MidiMessageCollector*> hardware {};
    // The notes its clips played (a counter, for the lights).
    std::atomic<std::uint32_t> clipNotes {};
    // Its notes on their way to its MIDI output.
    std::shared_ptr<MidiOutQueue> outQueue { std::make_shared<MidiOutQueue>() };
};

struct AudioTrackController::MidiPlaybackTrack final {
    MidiPlaybackTrack(AudioTrackController& controller,
                      const std::uint64_t trackId,
                      juce::String trackName,
                      juce::MidiMessageCollector& collector,
                      juce::MidiKeyboardState& keyboard)
        : id(trackId), name(std::move(trackName)),
          synth(controller, collector, keyboard),
          gainPanSource(synth, synth.positionSeconds),
          latencySource(gainPanSource)
    {
    }

    std::uint64_t id {};
    juce::String name;
    // What it hears while it is the live track, and where its notes also go.
    std::vector<juce::String> midiInputs { AudioTrackController::allMidiInputs };
    juce::String midiOutput;
    float gainDb {};
    float pan {};
    bool muted {};
    bool solo {};
    bool armed {};
    std::vector<AutomationPoint> automation;
    // What the graph puts after the instrument, kept so the project can be
    // saved and queried without asking the audio thread.
    cydaw::engine::NativeDspGraph effectGraph;
    std::uint64_t nextAutomationId { 1 };
    std::atomic<int> activeLauncherScene { -1 };
    std::atomic<std::uint64_t> activeEditorClipId {};
    std::atomic<double> launcherStartSeconds {};
    std::atomic<double> launcherEndSeconds {};
    std::vector<MidiClip> clips;
    MidiSynthEngine synth;
    GainPanAudioSource gainPanSource;
    LatencyCompensatingAudioSource latencySource;
};

// ---- The workshop's simulator ---------------------------------------------------------------

// A device tried on its own. An effect hears a test signal made here, block
// by block; an instrument plays a looped pattern and the on-screen keyboard
// through the same synthesiser the MIDI tracks use. The result goes into the
// master's mixer with the tracks, which the workshop leaves stopped.
struct AudioTrackController::Simulation final : public juce::AudioSource {
    struct Sample {
        juce::AudioBuffer<float> audio;
        double sampleRate { 44'100.0 };
    };

    explicit Simulation(AudioTrackController& controller) : synth(controller, collector, keyboard)
    {
        samples.store(std::make_shared<const Sample>());
    }

    void prepareToPlay(const int samplesPerBlockExpected, const double sampleRate) override
    {
        rate = std::max(1.0, sampleRate);
        blockSize.store(std::max(1, samplesPerBlockExpected), std::memory_order_release);
        sampleRateHz.store(rate, std::memory_order_release);
        synth.prepareToPlay(samplesPerBlockExpected, sampleRate);
        effect.prepare(rate, std::max(1, samplesPerBlockExpected), 2);
        side.setSize(2, std::max(1, samplesPerBlockExpected), false, false, true);
        collector.reset(rate);
    }

    void releaseResources() override { synth.releaseResources(); }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& output) override
    {
        output.clearActiveBufferRegion();
        if (!active.load(std::memory_order_acquire) || output.buffer == nullptr || output.numSamples <= 0)
            return;
        auto& buffer = *output.buffer;
        const auto start = output.startSample;
        const auto count = output.numSamples;
        const auto running = playing.load(std::memory_order_acquire);
        if (restart.exchange(false, std::memory_order_acq_rel)) {
            phases.fill(0.0);
            lastStep = -1;
            kick = snare = snareTone = hat = click = 0.0f;
            samplePosition = 0.0;
            sidePhase = 0.0;
            sideKick = sideKickPitch = 0.0f;
            sideLastBeat = -1;
        }
        const auto at = seconds.load(std::memory_order_relaxed);
        if (instrument.load(std::memory_order_acquire)) {
            // Its other inputs after its voices.
            const auto* other = sideInput(buffer, start, count, at, running, true);
            const std::array<const juce::AudioBuffer<float>*, RoutedInputReceiver::maximumBuses> buses { nullptr, other, other, other };
            synth.setRoutedInputs(buses.data(), count);
            synth.getNextAudioBlock(output);
            synth.setRoutedInputs(nullptr, 0);
        } else {
            if (running)
                generate(buffer, start, count, at);
            const auto* other = sideInput(buffer, start, count, at, running, false);
            const std::array<const juce::AudioBuffer<float>*, NativeDspRuntime::maximumExtraInputs> buses { other, other, other };
            effect.setExtraInputs(buses.data(), buses.size());
            (void) effect.process(buffer, start, count, at);
            if (monoOutput.load(std::memory_order_relaxed) && buffer.getNumChannels() > 1)
                buffer.copyFrom(1, start, buffer, 0, start, count);
        }
        if (running)
            seconds.store(at + static_cast<double>(count) / rate, std::memory_order_relaxed);
        const auto gain = outputGain.load(std::memory_order_relaxed);
        auto peak = 0.0f;
        for (auto channel = 0; channel < buffer.getNumChannels(); ++channel) {
            buffer.applyGainRamp(channel, start, count, lastGain, gain);
            peak = std::max(peak, buffer.getMagnitude(channel, start, count));
        }
        lastGain = gain;
        auto previous = outputPeak.load(std::memory_order_relaxed);
        while (peak > previous && !outputPeak.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {
        }
    }

    // ---- The sources, on the audio thread ----
    float white() noexcept
    {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return static_cast<float>(noiseState) / 2'147'483'648.0f - 1.0f;
    }

    static double noteHz(const int note) noexcept { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

    // What the device's other inputs hear this block, from 0, or null.
    const juce::AudioBuffer<float>* sideInput(const juce::AudioBuffer<float>& buffer, const int start, const int count,
                                              const double at, const bool running, const bool forInstrument) noexcept
    {
        const auto kind = static_cast<SimulationSideSource>(sideSource.load(std::memory_order_relaxed));
        if (kind == SimulationSideSource::silence || count > side.getNumSamples())
            return nullptr;
        if (kind == SimulationSideSource::same) {
            // An instrument has no source to share.
            if (forInstrument)
                return nullptr;
            for (auto channel = 0; channel < 2; ++channel)
                side.copyFrom(channel, 0, buffer, std::min(channel, buffer.getNumChannels() - 1), start, count);
            return &side;
        }
        if (!running)
            return nullptr;
        // A kick on every beat, at the source's level.
        constexpr auto twoPi = 6.283185307179586;
        const auto beat = 60.0 / std::max(20.0, NativeDspRuntime::sessionTempo());
        const auto level = sourceGain.load(std::memory_order_relaxed);
        const auto decay = [this](const double seconds) { return static_cast<float>(std::exp(-1.0 / (seconds * rate))); };
        const auto kickDecay = decay(0.28);
        const auto pitchDecay = decay(0.035);
        auto* left = side.getWritePointer(0);
        auto* right = side.getWritePointer(1);
        for (auto index = 0; index < count; ++index) {
            const auto t = at + static_cast<double>(index) / rate;
            if (const auto step = static_cast<long long>(t / beat); step != sideLastBeat) {
                sideLastBeat = step;
                sideKick = 1.0f;
                sideKickPitch = 1.0f;
            }
            sidePhase = std::fmod(sidePhase + twoPi * (48.0 + 110.0 * static_cast<double>(sideKickPitch)) / rate, twoPi);
            const auto value = static_cast<float>(std::sin(sidePhase)) * sideKick * 0.9f * level;
            sideKick *= kickDecay;
            sideKickPitch *= pitchDecay;
            left[index] = right[index] = value;
        }
        return &side;
    }

    void generate(juce::AudioBuffer<float>& buffer, const int start, const int count, const double at) noexcept
    {
        const auto kind = static_cast<SimulationSource>(source.load(std::memory_order_relaxed));
        const auto level = sourceGain.load(std::memory_order_relaxed);
        const auto tone = static_cast<double>(toneHz.load(std::memory_order_relaxed));
        const auto beat = 60.0 / std::max(20.0, NativeDspRuntime::sessionTempo());
        auto* left = buffer.getWritePointer(0, start);
        auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1, start) : nullptr;
        const auto loaded = samples.read();
        const auto* sample = kind == SimulationSource::sample && loaded != nullptr && loaded->audio.getNumSamples() > 1
                                 ? &*loaded : nullptr;
        constexpr auto twoPi = 6.283185307179586;
        // Am, F, C, G: a bar each, three voices and the bass.
        static constexpr int chords[4][4] = { { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 48, 55, 60, 64 }, { 43, 55, 59, 62 } };
        for (auto index = 0; index < count; ++index) {
            const auto t = at + static_cast<double>(index) / rate;
            auto l = 0.0f;
            auto r = 0.0f;
            switch (kind) {
            case SimulationSource::sine: {
                const auto value = static_cast<float>(std::sin(phases[0])) * 0.5f;
                phases[0] = std::fmod(phases[0] + twoPi * tone / rate, twoPi);
                l = r = value;
                break;
            }
            case SimulationSource::noise:
                l = white() * 0.35f;
                r = white() * 0.35f;
                break;
            case SimulationSource::pinkNoise: {
                // Paul Kellet's filter, one per side.
                for (auto side = 0; side < 2; ++side) {
                    auto* b = pink[static_cast<std::size_t>(side)].data();
                    const auto w = white();
                    b[0] = 0.99886f * b[0] + w * 0.0555179f;
                    b[1] = 0.99332f * b[1] + w * 0.0750759f;
                    b[2] = 0.96900f * b[2] + w * 0.1538520f;
                    b[3] = 0.86650f * b[3] + w * 0.3104856f;
                    b[4] = 0.55000f * b[4] + w * 0.5329522f;
                    b[5] = -0.7616f * b[5] - w * 0.0168980f;
                    const auto value = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f) * 0.06f;
                    b[6] = w * 0.115926f;
                    (side == 0 ? l : r) = value;
                }
                break;
            }
            case SimulationSource::chords: {
                const auto bar = static_cast<long long>(t / (beat * 4.0));
                const auto inBar = t - static_cast<double>(bar) * beat * 4.0;
                const auto& chord = chords[static_cast<std::size_t>(bar % 4)];
                const auto envelope = static_cast<float>(std::min(1.0, inBar / 0.02) * std::min(1.0, (beat * 4.0 - inBar) / 0.05));
                for (std::size_t voice = 0; voice < 4; ++voice) {
                    const auto saw = static_cast<float>(phases[voice] / twoPi * 2.0 - 1.0);
                    phases[voice] = std::fmod(phases[voice] + twoPi * noteHz(chord[voice]) * (voice == 0 ? 1.0 : 1.0015) / rate, twoPi);
                    const auto pan = voice == 1 ? 0.25f : voice == 3 ? 0.75f : 0.5f;
                    l += saw * (1.0f - pan) * 0.3f;
                    r += saw * pan * 0.3f;
                }
                // Softened: a one-pole low-pass near 2 kHz.
                const auto coefficient = static_cast<float>(1.0 - std::exp(-twoPi * 2'000.0 / rate));
                smooth[0] += coefficient * (l * envelope - smooth[0]);
                smooth[1] += coefficient * (r * envelope - smooth[1]);
                l = smooth[0];
                r = smooth[1];
                break;
            }
            case SimulationSource::drums: {
                const auto step = static_cast<long long>(t / (beat * 0.25));
                if (step != lastStep) {
                    lastStep = step;
                    const auto inBar = static_cast<int>(step % 16);
                    if (inBar % 4 == 0) {
                        kick = 1.0f;
                        kickPitch = 1.0f;
                    }
                    if (inBar == 4 || inBar == 12) {
                        snare = 1.0f;
                        snareTone = 1.0f;
                    }
                    if (inBar % 2 == 0)
                        hat = inBar % 4 == 2 ? 0.9f : 0.45f;
                    hatOpen = inBar == 14;
                }
                const auto kickHz = 48.0 + 110.0 * static_cast<double>(kickPitch);
                phases[0] = std::fmod(phases[0] + twoPi * kickHz / rate, twoPi);
                const auto kickValue = static_cast<float>(std::sin(phases[0])) * kick * 0.9f;
                phases[1] = std::fmod(phases[1] + twoPi * 185.0 / rate, twoPi);
                const auto noise = white();
                const auto snareValue = noise * snare * 0.45f + static_cast<float>(std::sin(phases[1])) * snareTone * 0.35f;
                const auto bright = noise - hatLast;
                hatLast = noise;
                const auto hatValue = bright * hat * 0.18f;
                const auto decay = [this](const double seconds) {
                    return static_cast<float>(std::exp(-1.0 / (seconds * rate)));
                };
                kick *= decay(0.28);
                kickPitch *= decay(0.035);
                snare *= decay(0.12);
                snareTone *= decay(0.05);
                hat *= decay(hatOpen ? 0.22 : 0.035);
                l = kickValue + snareValue + hatValue * 0.8f;
                r = kickValue + snareValue + hatValue * 1.2f;
                break;
            }
            case SimulationSource::pulse: {
                const auto step = static_cast<long long>(t / beat);
                if (step != lastStep) {
                    lastStep = step;
                    click = 1.0f;
                    phases[0] = 0.0;
                }
                phases[0] = std::fmod(phases[0] + twoPi * 1'000.0 / rate, twoPi);
                l = r = static_cast<float>(std::sin(phases[0])) * click * 0.7f;
                click *= static_cast<float>(std::exp(-1.0 / (0.006 * rate)));
                break;
            }
            case SimulationSource::sample:
                if (sample != nullptr) {
                    const auto length = sample->audio.getNumSamples();
                    const auto at0 = static_cast<int>(samplePosition);
                    const auto fraction = static_cast<float>(samplePosition - at0);
                    const auto at1 = (at0 + 1) % length;
                    const auto read = [&](const int channel) {
                        const auto* data = sample->audio.getReadPointer(std::min(channel, sample->audio.getNumChannels() - 1));
                        return data[at0] + (data[at1] - data[at0]) * fraction;
                    };
                    l = read(0);
                    r = read(1);
                    samplePosition += sample->sampleRate / rate;
                    if (samplePosition >= length)
                        samplePosition = std::fmod(samplePosition, static_cast<double>(length));
                }
                break;
            case SimulationSource::none:
                break;
            }
            left[index] = l * level;
            if (right != nullptr)
                right[index] = r * level;
        }
    }

    juce::MidiMessageCollector collector;
    juce::MidiKeyboardState keyboard;
    MidiSynthEngine synth;
    NativeDspRuntime effect;
    cydaw::realtime::RtPublisher<Sample> samples;
    std::atomic<bool> active {};
    std::atomic<bool> playing {};
    std::atomic<bool> restart {};
    std::atomic<bool> instrument {};
    std::atomic<bool> monoOutput {};
    std::atomic<int> source { static_cast<int>(SimulationSource::chords) };
    std::atomic<int> sideSource { static_cast<int>(SimulationSideSource::same) };
    std::atomic<float> sourceGain { 0.25f };
    std::atomic<float> toneHz { 220.0f };
    std::atomic<float> outputGain { 1.0f };
    std::atomic<float> outputPeak {};
    std::atomic<double> seconds {};
    std::atomic<int> blockSize { 512 };
    std::atomic<double> sampleRateHz { 48'000.0 };
    // The audio thread's own.
    double rate { 48'000.0 };
    float lastGain { 1.0f };
    std::array<double, 4> phases {};
    std::array<std::array<float, 7>, 2> pink {};
    std::array<float, 2> smooth {};
    std::uint32_t noiseState { 0x2545F491u };
    long long lastStep { -1 };
    float kick {};
    float kickPitch {};
    float snare {};
    float snareTone {};
    float hat {};
    float hatLast {};
    bool hatOpen {};
    float click {};
    double samplePosition {};
    // The other inputs' sound, and their kick.
    juce::AudioBuffer<float> side;
    double sidePhase {};
    float sideKick {};
    float sideKickPitch {};
    long long sideLastBeat { -1 };
};

void AudioTrackController::setSimulationActive(const bool active) noexcept
{
    simulation_->active.store(active, std::memory_order_release);
    applySimulationMidi();
    if (!active)
        setSimulationPlaying(false);
}

bool AudioTrackController::isSimulationActive() const noexcept
{
    return simulation_->active.load(std::memory_order_acquire);
}

void AudioTrackController::setSimulationPlaying(const bool playing) noexcept
{
    if (playing) {
        simulation_->seconds.store(0.0, std::memory_order_relaxed);
        simulation_->restart.store(true, std::memory_order_release);
        simulation_->synth.setPosition(0.0);
    }
    simulation_->playing.store(playing, std::memory_order_release);
    simulation_->synth.setPlaying(playing && !simulationPattern_.empty());
}

bool AudioTrackController::isSimulationPlaying() const noexcept
{
    return simulation_->playing.load(std::memory_order_acquire);
}

double AudioTrackController::simulationSeconds() const noexcept
{
    return simulation_->seconds.load(std::memory_order_relaxed);
}

juce::Result AudioTrackController::setSimulationEffect(const cydaw::engine::NativeDspGraph& graph, const bool monoOutput)
{
    simulation_->effect.prepare(simulation_->sampleRateHz.load(std::memory_order_acquire),
                                simulation_->blockSize.load(std::memory_order_acquire), 2);
    const auto result = simulation_->effect.setGraph(graph);
    simulation_->monoOutput.store(monoOutput, std::memory_order_release);
    simulation_->instrument.store(false, std::memory_order_release);
    applySimulationMidi();
    return result;
}

juce::Result AudioTrackController::setSimulationInstrument(const cydaw::engine::PolySynthGraphPlan& plan,
                                                           const cydaw::engine::NativeDspGraph& chain,
                                                           std::vector<cydaw::engine::PolyVoiceModulation> modulations)
{
    auto& synth = simulation_->synth;
    synth.setInstrumentGraph(plan);
    synth.voiceModulations.store(
        std::make_shared<const std::vector<cydaw::engine::PolyVoiceModulation>>(std::move(modulations)));
    const auto result = synth.updateEffectParameters(chain).wasOk() ? juce::Result::ok() : synth.setEffectGraph(chain);
    simulation_->instrument.store(true, std::memory_order_release);
    applySimulationMidi();
    return result;
}

juce::Result AudioTrackController::updateSimulationParameters(const cydaw::engine::NativeDspGraph& graph)
{
    return simulation_->instrument.load(std::memory_order_acquire) ? simulation_->synth.updateEffectParameters(graph)
                                                                   : simulation_->effect.updateParameters(graph);
}

void AudioTrackController::setSimulationSideSource(const SimulationSideSource source) noexcept
{
    simulation_->sideSource.store(static_cast<int>(source), std::memory_order_relaxed);
}

void AudioTrackController::setSimulationSource(const SimulationSource source, const float levelDb, const float toneHz) noexcept
{
    simulation_->source.store(static_cast<int>(source), std::memory_order_relaxed);
    simulation_->sourceGain.store(juce::Decibels::decibelsToGain(juce::jlimit(-60.0f, 6.0f, levelDb), -60.0f),
                                  std::memory_order_relaxed);
    simulation_->toneHz.store(juce::jlimit(20.0f, 20'000.0f, toneHz), std::memory_order_relaxed);
}

juce::Result AudioTrackController::setSimulationSample(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (reader == nullptr)
        return juce::Result::fail("unreadable audio file: " + file.getFileName());
    const auto length = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples,
                                                               static_cast<juce::int64>(reader->sampleRate * 300.0)));
    if (length < 2)
        return juce::Result::fail("empty audio file: " + file.getFileName());
    auto sample = std::make_shared<Simulation::Sample>();
    sample->sampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44'100.0;
    sample->audio.setSize(static_cast<int>(std::clamp<unsigned int>(reader->numChannels, 1u, 2u)), length);
    reader->read(&sample->audio, 0, length, 0, true, true);
    simulation_->samples.store(std::move(sample));
    return juce::Result::ok();
}

void AudioTrackController::setSimulationPattern(std::vector<SimulationNote> notes, const double lengthBeats)
{
    simulationPattern_ = std::move(notes);
    simulationPatternBeats_ = std::max(0.25, lengthBeats);
    publishSimulationPattern();
    simulation_->synth.setPlaying(simulation_->playing.load(std::memory_order_acquire) && !simulationPattern_.empty());
}

void AudioTrackController::publishSimulationPattern()
{
    const auto beat = 60.0 / std::max(1.0, tempoBpm_);
    auto scheduled = std::make_shared<std::vector<MidiSynthEngine::ScheduledNote>>();
    for (const auto& note : simulationPattern_)
        scheduled->push_back({ juce::jlimit(0, 127, note.noteNumber), note.startBeats * beat,
                               (note.startBeats + std::max(0.01, note.lengthBeats)) * beat,
                               juce::jlimit(0.01f, 1.0f, note.velocity) });
    const auto length = simulationPatternBeats_ * beat;
    auto& synth = simulation_->synth;
    synth.setSequence(std::move(scheduled), length);
    synth.setTransportRange(length, 0.0, length);
    synth.setLooping(true);
}

bool AudioTrackController::simulationTakesMidi() const noexcept
{
    return simulation_ != nullptr && simulation_->active.load(std::memory_order_acquire)
           && simulation_->instrument.load(std::memory_order_acquire);
}

void AudioTrackController::applySimulationMidi() noexcept
{
    const auto takes = simulationTakesMidi();
    simulation_->synth.setLiveInput(takes);
    simulation_->synth.hardware.store(takes ? &midiCollector_ : nullptr, std::memory_order_release);
    refreshMidiInputs();
}

std::uint64_t AudioTrackController::midiInputTrackId() const noexcept
{
    const auto selected = liveMidiTrackId_.load(std::memory_order_acquire);
    if (armFollowsMidiInput_)
        return selected;
    // Recording: the track recorded. Else an armed track (the selected one
    // when it is armed), else the selected track.
    if (midiRecordingActive_.load(std::memory_order_acquire) && midiRecordingTrackId_ != 0)
        return midiRecordingTrackId_;
    std::uint64_t firstArmed {};
    for (const auto& track : midiTracks_)
        if (track->armed) {
            if (track->id == selected)
                return selected;
            if (firstArmed == 0)
                firstArmed = track->id;
        }
    return firstArmed != 0 ? firstArmed : selected;
}

void AudioTrackController::refreshMidiInputs() noexcept
{
    const auto takes = simulationTakesMidi();
    const auto target = midiInputTrackId();
    for (auto& track : midiTracks_)
        track->synth.setLiveInput(!takes && track->id == target);
    // What the live track hears (the simulator: everything).
    auto route = std::make_shared<LiveMidiRoute>();
    route->trackId = takes ? 0 : target;
    if (!takes)
        for (const auto& track : midiTracks_)
            if (track->id == target) {
                route->all = std::find(track->midiInputs.begin(), track->midiInputs.end(), juce::String(allMidiInputs)) != track->midiInputs.end();
                route->keyboard = route->all
                                  || std::find(track->midiInputs.begin(), track->midiInputs.end(), juce::String(computerKeyboardInput))
                                         != track->midiInputs.end();
                for (const auto& input : track->midiInputs)
                    if (input != allMidiInputs && input != computerKeyboardInput)
                        route->devices.push_back(input);
            }
    std::shared_ptr<const LiveMidiRoute> shared = std::move(route);
    const juce::SpinLock::ScopedLockType hold(midiGateLock_);
    liveRoute_.swap(shared);
}

void AudioTrackController::countMidiSource(const juce::String& identifier) noexcept
{
    midiInCount_.fetch_add(1, std::memory_order_relaxed);
    const auto key = static_cast<std::uint64_t>(identifier.hashCode64()) | 1u;
    for (auto& slot : midiSources_) {
        auto current = slot.key.load(std::memory_order_acquire);
        if (current == 0) {
            std::uint64_t expected {};
            if (slot.key.compare_exchange_strong(expected, key, std::memory_order_acq_rel))
                current = key;
            else
                current = expected;
        }
        if (current == key) {
            slot.count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}

std::uint32_t AudioTrackController::midiInputActivity(const juce::String& identifier) const noexcept
{
    const auto key = static_cast<std::uint64_t>(identifier.hashCode64()) | 1u;
    for (const auto& slot : midiSources_)
        if (slot.key.load(std::memory_order_acquire) == key)
            return slot.count.load(std::memory_order_relaxed);
    return 0;
}

std::uint32_t AudioTrackController::computerKeyboardActivity() const noexcept
{
    return keyboardCount_.load(std::memory_order_relaxed);
}

std::uint32_t AudioTrackController::midiInActivity() const noexcept
{
    return midiInCount_.load(std::memory_order_relaxed);
}

std::uint32_t AudioTrackController::midiOutActivity() const noexcept
{
    return midiOutCount_.load(std::memory_order_relaxed);
}

std::uint32_t AudioTrackController::midiTrackClipActivity(const std::uint64_t trackId) const noexcept
{
    for (const auto& track : midiTracks_)
        if (track->id == trackId)
            return track->synth.clipNotes.load(std::memory_order_relaxed);
    return 0;
}

bool AudioTrackController::midiTrackHearsInputs(const std::uint64_t trackId) const noexcept
{
    return !simulationTakesMidi() && trackId != 0 && midiInputTrackId() == trackId;
}

void AudioTrackController::setMidiTrackInputs(const std::uint64_t trackId, std::vector<juce::String> inputs)
{
    // Every input holds the computer's keyboard: it is not listed twice.
    if (std::find(inputs.begin(), inputs.end(), juce::String(allMidiInputs)) != inputs.end())
        std::erase(inputs, juce::String(computerKeyboardInput));
    std::vector<juce::String> unique;
    for (auto& input : inputs)
        if (std::find(unique.begin(), unique.end(), input) == unique.end())
            unique.push_back(std::move(input));
    inputs = std::move(unique);
    for (auto& track : midiTracks_)
        if (track->id == trackId)
            track->midiInputs = std::move(inputs);
    refreshMidiInputs();
}

std::vector<juce::String> AudioTrackController::midiTrackInputs(const std::uint64_t trackId) const
{
    for (const auto& track : midiTracks_)
        if (track->id == trackId)
            return track->midiInputs;
    return {};
}

void AudioTrackController::setMidiTrackOutput(const std::uint64_t trackId, const juce::String& identifier)
{
    for (auto& track : midiTracks_)
        if (track->id == trackId)
            track->midiOutput = identifier;
    publishMidiOutputs();
}

juce::String AudioTrackController::midiTrackOutput(const std::uint64_t trackId) const
{
    for (const auto& track : midiTracks_)
        if (track->id == trackId)
            return track->midiOutput;
    return {};
}

void AudioTrackController::publishMidiOutputs()
{
    std::vector<std::pair<std::shared_ptr<MidiOutQueue>, juce::String>> queues;
    for (const auto& track : midiTracks_)
        if (track->midiOutput.isNotEmpty())
            queues.emplace_back(track->synth.outQueue, track->midiOutput);
    if (queues.empty() && midiOutputs_ == nullptr)
        return;
    if (midiOutputs_ == nullptr)
        midiOutputs_ = std::make_unique<MidiOutputHub>(midiOutCount_);
    midiOutputs_->setQueues(std::move(queues));
}

void AudioTrackController::computerKeyboardNote(const int note, const float velocity, const bool on)
{
    keyboardCount_.fetch_add(1, std::memory_order_relaxed);
    midiInCount_.fetch_add(1, std::memory_order_relaxed);
    std::shared_ptr<const LiveMidiRoute> route;
    {
        const juce::SpinLock::ScopedLockType hold(midiGateLock_);
        route = liveRoute_;
    }
    // A key let go always reaches it (it may have been pressed before the
    // route changed).
    if (on && route != nullptr && !route->keyboard)
        return;
    if (on)
        midiKeyboardState_.noteOn(1, juce::jlimit(0, 127, note), juce::jlimit(0.0f, 1.0f, velocity));
    else
        midiKeyboardState_.noteOff(1, juce::jlimit(0, 127, note), 0.0f);
}

std::array<float, 4> AudioTrackController::midiTrackInputLevels(const std::uint64_t trackId) const noexcept
{
    std::array<float, 4> levels {};
    for (const auto& track : midiTracks_)
        if (track->id == trackId)
            for (std::size_t bus = 0; bus < levels.size(); ++bus)
                levels[bus] = track->synth.inputLevel(bus);
    return levels;
}

void AudioTrackController::setMidiInputGate(MidiInputGate gate)
{
    auto shared = gate ? std::make_shared<const MidiInputGate>(std::move(gate)) : nullptr;
    const juce::SpinLock::ScopedLockType hold(midiGateLock_);
    midiGate_.swap(shared);
}

void AudioTrackController::GatedMidiInput::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message)
{
    std::shared_ptr<const MidiInputGate> gate;
    {
        const juce::SpinLock::ScopedLockType hold(owner_.midiGateLock_);
        gate = owner_.midiGate_;
    }
    const auto identifier = source != nullptr ? source->getIdentifier() : juce::String();
    // Counted as it arrives (a controller's message too), for the lights.
    owner_.countMidiSource(identifier);
    if (gate != nullptr && *gate && !(*gate)(identifier, message))
        return;
    // The live track hears it when it listens to that keyboard.
    std::shared_ptr<const LiveMidiRoute> route;
    {
        const juce::SpinLock::ScopedLockType hold(owner_.midiGateLock_);
        route = owner_.liveRoute_;
    }
    if (route != nullptr && !route->all && std::find(route->devices.begin(), route->devices.end(), identifier) == route->devices.end())
        return;
    owner_.midiCollector_.handleIncomingMidiMessage(source, message);
}

void AudioTrackController::setArmFollowsMidiInput(const bool follows) noexcept
{
    armFollowsMidiInput_ = follows;
    refreshMidiInputs();
}

void AudioTrackController::simulationNoteOn(const int noteNumber, const float velocity)
{
    simulation_->keyboard.noteOn(1, juce::jlimit(0, 127, noteNumber), juce::jlimit(0.01f, 1.0f, velocity));
}

void AudioTrackController::simulationNoteOff(const int noteNumber)
{
    simulation_->keyboard.noteOff(1, juce::jlimit(0, 127, noteNumber), 0.0f);
}

void AudioTrackController::setSimulationOutputGainDb(const float gainDb) noexcept
{
    simulation_->outputGain.store(juce::Decibels::decibelsToGain(juce::jlimit(-60.0f, 12.0f, gainDb), -60.0f),
                                  std::memory_order_relaxed);
}

float AudioTrackController::takeSimulationPeak() noexcept
{
    return simulation_->outputPeak.exchange(0.0f, std::memory_order_relaxed);
}

void AudioTrackController::setSimulationTaps(const std::vector<NativeDspRuntime::TapRequest>& taps)
{
    const auto instrument = simulation_->instrument.load(std::memory_order_acquire);
    simulation_->synth.effects.setTaps(instrument ? taps : std::vector<NativeDspRuntime::TapRequest> {});
    simulation_->effect.setTaps(instrument ? std::vector<NativeDspRuntime::TapRequest> {} : taps);
}

bool AudioTrackController::readSimulationTap(const std::uint64_t nodeId, const bool input,
                                             cydaw::engine::AudioAnalysisFrame& frame) const
{
    return simulation_->instrument.load(std::memory_order_acquire) ? simulation_->synth.effects.readTap(nodeId, input, frame)
                                                                   : simulation_->effect.readTap(nodeId, input, frame);
}

std::vector<std::pair<std::uint64_t, float>> AudioTrackController::simulationControlValues() const
{
    return simulation_->instrument.load(std::memory_order_acquire) ? simulation_->synth.effects.controlValues()
                                                                   : simulation_->effect.controlValues();
}

juce::Result AudioTrackController::selfTestNoteExpression()
{
    AudioTrackController controller;
    controller.clearMidiTracks();
    const auto trackId = controller.addMidiTrack("Expression");
    if (const auto tracks = controller.midiTracks(); !tracks.empty())
        for (const auto& clip : tracks.front().clips)
            (void) controller.removeMidiClip(trackId, clip.id);
    auto* track = controller.findMidiTrack(trackId);
    if (track == nullptr)
        return juce::Result::fail("no MIDI track");
    auto graph = track->synth.instrumentPlan();
    graph.enabled = true;
    graph.waveform = cydaw::engine::PolyOscillatorWaveform::sine;
    graph.attackSeconds = 0.0001f;
    graph.decaySeconds = 0.0001f;
    graph.sustainLevel = 1.0f;
    graph.releaseSeconds = 0.01f;
    graph.oscillatorLevel = 0.2f;
    track->synth.setInstrumentGraph(graph);
    track->synth.prepareToPlay(512, 48'000.0);

    // The energy of a frequency in a mono run of samples.
    const auto energyAt = [](const std::vector<float>& samples, const double frequency) {
        const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / 48'000.0;
        const auto coefficient = 2.0 * std::cos(omega);
        double previous {};
        double before {};
        for (const auto sample : samples) {
            const auto now = static_cast<double>(sample) + coefficient * previous - before;
            before = previous;
            previous = now;
        }
        return std::sqrt(std::max(0.0, previous * previous + before * before - coefficient * previous * before))
               / static_cast<double>(samples.size());
    };
    // A clip of two notes heard from its start: A3 plain, A4 with curves.
    const auto render = [&](const std::vector<NoteExpressionPoint>& pitch, const std::vector<NoteExpressionPoint>& gain,
                            const bool withPlain) {
        const auto clipId = controller.addMidiClip(trackId, "Expression", 0.0, 8.0, false, 0xff4fd3ae);
        if (withPlain)
            (void) controller.addMidiNote(trackId, clipId, 57, 0.0, 4.0, 0.8f);
        const auto noteId = controller.addMidiNote(trackId, clipId, 69, 0.0, 4.0, 0.8f);
        (void) controller.setMidiNoteExpression(trackId, clipId, noteId, pitch, gain);
        std::vector<float> samples;
        if (controller.startMidiClipPreview(trackId, clipId, 0)) {
            juce::AudioBuffer<float> block(2, 512);
            for (int index = 0; index < 60; ++index) {
                block.clear();
                track->synth.getNextAudioBlock({ &block, 0, block.getNumSamples() });
                // Past the attack: a quarter second in.
                if (index >= 24)
                    samples.insert(samples.end(), block.getReadPointer(0), block.getReadPointer(0) + block.getNumSamples());
            }
            (void) controller.stopMidiClipPreview(trackId);
        }
        (void) controller.removeMidiClip(trackId, clipId);
        return samples;
    };
    const auto plain = render({}, {}, false);
    const auto octave = render({ { 0.0f, 12.0f }, { 1.0f, 12.0f } }, {}, true);
    const auto quiet = render({}, { { 0.0f, -12.0f }, { 1.0f, -12.0f } }, false);
    track->synth.releaseResources();
    if (plain.empty() || octave.empty() || quiet.empty())
        return juce::Result::fail("the clip did not play");
    const auto plainA4 = energyAt(plain, 440.0);
    if (plainA4 < 1.0e-3)
        return juce::Result::fail("a plain A4 is silent");
    // Bent an octave up: A5, and the A3 beside it untouched (its own channel).
    const auto bentA5 = energyAt(octave, 880.0);
    const auto bentA4 = energyAt(octave, 440.0);
    const auto keptA3 = energyAt(octave, 220.0);
    if (bentA5 < plainA4 * 0.5 || bentA4 > plainA4 * 0.1 || keptA3 < plainA4 * 0.5)
        return juce::Result::fail("pitch curve: A5 " + juce::String(bentA5 / plainA4, 3) + ", A4 " + juce::String(bentA4 / plainA4, 3)
                                  + ", A3 " + juce::String(keptA3 / plainA4, 3) + " of a plain note");
    // -12 dB: a quarter of the level.
    const auto ratio = energyAt(quiet, 440.0) / plainA4;
    if (std::abs(ratio - 0.251) > 0.04)
        return juce::Result::fail("gain curve: " + juce::String(ratio, 3) + " of a plain note, not 0.25");
    // A copy keeps the curves; the order of points is kept, values bounded.
    const auto clipId = controller.addMidiClip(trackId, "Copies", 0.0, 4.0, false, 0xff4fd3ae);
    const auto noteId = controller.addMidiNote(trackId, clipId, 60, 0.0, 1.0, 0.8f);
    (void) controller.setMidiNoteExpression(trackId, clipId, noteId, { { 0.8f, 1.0f }, { 0.2f, 99.0f } }, { { 0.5f, -3.0f } });
    MidiNote stored;
    if (const auto tracks = controller.midiTracks(); !tracks.empty())
        for (const auto& clip : tracks.front().clips)
            for (const auto& note : clip.notes)
                if (note.id == noteId)
                    stored = note;
    if (stored.pitch.size() != 2 || stored.pitch[0].position != 0.2f || stored.pitch[0].value != 24.0f || stored.gain.size() != 1)
        return juce::Result::fail("expression points were not tidied");
    const auto copyId = controller.copyMidiNote(trackId, clipId, stored, 2.0);
    for (const auto& clip : controller.midiTracks().front().clips)
        for (const auto& note : clip.notes)
            if (note.id == copyId && (note.pitch != stored.pitch || note.gain != stored.gain))
                return juce::Result::fail("a copied note lost its expression");
    // A bent way, as a glide arrives: quick first, on the key at its end,
    // never past it; the bend kept on the note, bounded.
    const std::vector<NoteExpressionPoint> glide { { 0.0f, 0.0f, 0.55f }, { 1.0f, 12.0f } };
    const auto halfway = NoteExpressionPoint::valueAt(glide, 0.5f);
    if (halfway < 9.0f || halfway > 11.0f || std::abs(NoteExpressionPoint::valueAt(glide, 1.0f) - 12.0f) > 1.0e-4f)
        return juce::Result::fail("a bent glide is at " + juce::String(halfway, 2) + " halfway, not about 10");
    auto previous = 0.0f;
    for (auto step = 0; step <= 100; ++step) {
        const auto value = NoteExpressionPoint::valueAt(glide, static_cast<float>(step) / 100.0f);
        if (value < previous - 1.0e-5f || value > 12.0f + 1.0e-5f)
            return juce::Result::fail("a bent glide goes back or past its key");
        previous = value;
    }
    (void) controller.setMidiNoteExpression(trackId, clipId, noteId, { { 0.0f, 0.0f, 3.0f }, { 1.0f, 5.0f } }, {});
    for (const auto& clip : controller.midiTracks().front().clips)
        for (const auto& note : clip.notes)
            if (note.id == noteId && (note.pitch.size() != 2 || note.pitch[0].bend != 1.0f))
                return juce::Result::fail("a note's bend was not kept within -1..1");
    return juce::Result::ok();
}

juce::Result AudioTrackController::selfTestLegatoVoicing()
{
    AudioTrackController controller;
    controller.clearMidiTracks();
    const auto trackId = controller.addMidiTrack("Legato");
    if (const auto tracks = controller.midiTracks(); !tracks.empty())
        for (const auto& clip : tracks.front().clips)
            (void) controller.removeMidiClip(trackId, clip.id);
    auto* track = controller.findMidiTrack(trackId);
    if (track == nullptr)
        return juce::Result::fail("no MIDI track");
    constexpr auto rate = 48'000.0;
    const auto energyAt = [](const std::vector<float>& samples, const double frequency, const double from, const double to) {
        const auto first = static_cast<std::size_t>(from * rate);
        const auto last = std::min(samples.size(), static_cast<std::size_t>(to * rate));
        const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / rate;
        const auto coefficient = 2.0 * std::cos(omega);
        double previous {};
        double before {};
        for (auto index = first; index < last; ++index) {
            const auto now = static_cast<double>(samples[index]) + coefficient * previous - before;
            before = previous;
            previous = now;
        }
        return std::sqrt(std::max(0.0, previous * previous + before * before - coefficient * previous * before))
               / static_cast<double>(std::max<std::size_t>(1, last - first));
    };
    // C4 for two beats, G4 from the second beat on (120 BPM: half a second
    // a beat): the two overlap for a second.
    const auto render = [&](const int mode, const float glideSeconds) {
        auto graph = track->synth.instrumentPlan();
        graph.enabled = true;
        graph.waveform = cydaw::engine::PolyOscillatorWaveform::sine;
        graph.attackSeconds = 0.0001f;
        graph.decaySeconds = 0.0001f;
        graph.sustainLevel = 1.0f;
        graph.releaseSeconds = 0.01f;
        graph.oscillatorLevel = 0.2f;
        graph.voiceMode = mode;
        graph.glideSeconds = glideSeconds;
        track->synth.setInstrumentGraph(graph);
        track->synth.prepareToPlay(512, rate);
        const auto clipId = controller.addMidiClip(trackId, "Line", 0.0, 8.0, false, 0xff4fd3ae);
        (void) controller.addMidiNote(trackId, clipId, 60, 0.0, 3.0, 0.8f);
        (void) controller.addMidiNote(trackId, clipId, 67, 1.0, 3.0, 0.8f);
        std::vector<float> samples;
        if (controller.startMidiClipPreview(trackId, clipId, 0)) {
            juce::AudioBuffer<float> block(2, 512);
            for (int index = 0; index < 150; ++index) {
                block.clear();
                track->synth.getNextAudioBlock({ &block, 0, block.getNumSamples() });
                samples.insert(samples.end(), block.getReadPointer(0), block.getReadPointer(0) + block.getNumSamples());
            }
            (void) controller.stopMidiClipPreview(trackId);
        }
        (void) controller.removeMidiClip(trackId, clipId);
        track->synth.releaseResources();
        return samples;
    };
    constexpr auto c4 = 261.626;
    constexpr auto g4 = 391.995;
    const auto poly = render(0, 0.0f);
    const auto legato = render(2, 0.25f);
    if (poly.size() < static_cast<std::size_t>(1.5 * rate) || legato.size() < static_cast<std::size_t>(1.5 * rate))
        return juce::Result::fail("the clip did not play");
    // Poly: a chord while they overlap.
    const auto polyC = energyAt(poly, c4, 0.9, 1.4);
    const auto polyG = energyAt(poly, g4, 0.9, 1.4);
    if (polyC < 1.0e-3 || polyG < 1.0e-3)
        return juce::Result::fail("poly: the two notes do not sound together");
    // Legato: the first gave way, the second sounds alone…
    const auto legatoC = energyAt(legato, c4, 0.9, 1.4);
    const auto legatoG = energyAt(legato, g4, 0.9, 1.4);
    if (legatoC > polyC * 0.05 || legatoG < polyG * 0.7)
        return juce::Result::fail("legato: C4 " + juce::String(legatoC / polyC, 3) + ", G4 " + juce::String(legatoG / polyG, 3)
                                  + " of poly while the notes overlap");
    // …having slid up to it: not there yet at its start.
    const auto sliding = energyAt(legato, g4, 0.5, 0.6);
    const auto arrived = energyAt(legato, g4, 1.0, 1.1);
    if (sliding > arrived * 0.5)
        return juce::Result::fail("legato: no glide (G4 at the start " + juce::String(sliding / arrived, 3) + " of later)");
    return juce::Result::ok();
}

juce::Result AudioTrackController::selfTestPolySynthGraph()
{
    constexpr auto sampleRate = 48'000.0;
    constexpr auto sampleCount = 4'800;
    const auto render = [](const cydaw::engine::PolySynthGraphPlan& plan,
                           const std::vector<int>& notes) {
        cydaw::realtime::RtPublisher<cydaw::engine::PolySynthGraphPlan> graph;
        graph.store(
            std::make_shared<const cydaw::engine::PolySynthGraphPlan>(plan));
        juce::Synthesiser synthesiser;
        const VoiceModulationState unmodulated;
        for (int voice = 0; voice < 16; ++voice)
            synthesiser.addVoice(new GraphSynthVoice(graph, unmodulated));
        synthesiser.addSound(new GraphSynthSound());
        synthesiser.setCurrentPlaybackSampleRate(sampleRate);
        juce::MidiBuffer midi;
        for (const auto note : notes) {
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.75f), 0);
            midi.addEvent(juce::MidiMessage::noteOff(1, note), sampleCount / 2);
        }
        juce::AudioBuffer<float> output(2, sampleCount);
        output.clear();
        synthesiser.renderNextBlock(output, midi, 0, sampleCount);
        return output;
    };

    cydaw::engine::PolySynthGraphPlan sinePlan;
    sinePlan.enabled = true;
    sinePlan.oscillatorLevel = 0.10f;
    sinePlan.attackSeconds = 0.0001f;
    sinePlan.decaySeconds = 0.0001f;
    sinePlan.sustainLevel = 1.0f;
    sinePlan.releaseSeconds = 0.03f;
    sinePlan.filterCutoffHz = 20'000.0f;
    auto squarePlan = sinePlan;
    squarePlan.waveform = cydaw::engine::PolyOscillatorWaveform::square;

    const auto sine = render(sinePlan, { 60 });
    const auto square = render(squarePlan, { 60 });
    const auto chord = render(squarePlan,
                              { 48, 52, 55, 60, 64, 67, 72, 76 });
    const auto sineRms = sine.getRMSLevel(0, 256, 1'800);
    const auto chordRms = chord.getRMSLevel(0, 256, 1'800);
    const auto releaseStart = square.getRMSLevel(0, sampleCount / 2, 240);
    const auto releaseEnd = square.getRMSLevel(0, sampleCount - 480, 480);
    double waveformDifference = 0.0;
    for (int sample = 256; sample < 2'056; ++sample)
        waveformDifference += std::abs(
            static_cast<double>(sine.getSample(0, sample)
                                - square.getSample(0, sample)));
    waveformDifference /= 1'800.0;

    if (sineRms <= 0.01f || chordRms <= sineRms
        || waveformDifference <= 0.01
        || releaseStart <= 0.0001f || releaseEnd >= releaseStart * 0.1f)
        return juce::Result::fail(
            "Poly synth graph did not render waveform, polyphony and ADSR release correctly.");

    // Every mode the waveform enum offers has to make a sound here, not only
    // in the graph editor's preview. This switch had four cases while the enum
    // had six, so a wavetable or string instrument was silent in the DAW and
    // nothing said so.
    for (auto mode = 0; mode <= static_cast<int>(
                            cydaw::engine::PolyOscillatorWaveform::string);
         ++mode) {
        auto plan = sinePlan;
        plan.waveform = static_cast<cydaw::engine::PolyOscillatorWaveform>(
            mode);
        const auto rendered = render(plan, { 60 });
        if (rendered.getRMSLevel(0, 480, 3'600) <= 0.001f)
            return juce::Result::fail(
                "Poly synth voice rendered silence for waveform "
                + juce::String(mode));
    }

    // Prisme's voicing: its voice limit, mono, legato (one voice however many
    // keys are held), the pitch it slides from.
    {
        namespace prism = cydaw::engine::prism;
        auto plan = sinePlan;
        plan.waveform = cydaw::engine::PolyOscillatorWaveform::prism;
        plan.prismValues = prism::defaultValues();
        plan.prismTables = prism::resolveTables(plan.prismValues);
        const auto voicesAfter = [&plan](const std::vector<int>& notes, const int mode, const int limit, const float glide,
                                         double* pitch) {
            auto voicing = plan;
            voicing.prismValues[static_cast<std::size_t>(prism::voiceBase + prism::voiceMode)] = static_cast<float>(mode);
            voicing.prismValues[static_cast<std::size_t>(prism::voiceBase + prism::voiceCount)] = static_cast<float>(limit);
            voicing.prismValues[static_cast<std::size_t>(prism::voiceBase + prism::voiceGlide)] = glide;
            cydaw::realtime::RtPublisher<cydaw::engine::PolySynthGraphPlan> graph;
            graph.store(std::make_shared<const cydaw::engine::PolySynthGraphPlan>(voicing));
            PlanSynthesiser synthesiser;
            const VoiceModulationState unmodulated;
            for (int voice = 0; voice < 16; ++voice)
                synthesiser.addVoice(new GraphSynthVoice(graph, unmodulated, &synthesiser.context));
            synthesiser.addSound(new GraphSynthSound());
            synthesiser.voicing = [&graph] {
                const auto current = graph.read();
                return current != nullptr ? PlanSynthesiser::voicingOf(*current) : PlanSynthesiser::Voicing {};
            };
            synthesiser.setCurrentPlaybackSampleRate(sampleRate);
            juce::MidiBuffer midi;
            auto at = 0;
            for (const auto note : notes) {
                midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), at);
                at += 480;
            }
            juce::AudioBuffer<float> output(2, sampleCount);
            output.clear();
            synthesiser.renderNextBlock(output, midi, 0, sampleCount);
            auto active = 0;
            for (int index = 0; index < synthesiser.getNumVoices(); ++index)
                if (auto* voice = synthesiser.getVoice(index); voice->isVoiceActive()) {
                    ++active;
                    if (pitch != nullptr)
                        *pitch = dynamic_cast<GraphSynthVoice*>(voice)->voicePitch();
                }
            return active;
        };
        if (voicesAfter({ 60, 64, 67, 72 }, 0, 16, 0.0f, nullptr) != 4)
            return juce::Result::fail("Prisme in poly did not play four held notes");
        if (const auto limited = voicesAfter({ 60, 64, 67, 72 }, 0, 2, 0.0f, nullptr); limited != 2)
            return juce::Result::fail("Prisme's voice limit did not hold (" + juce::String(limited) + " voices)");
        if (voicesAfter({ 60, 64, 67 }, 1, 16, 0.0f, nullptr) != 1)
            return juce::Result::fail("Prisme in mono played more than one voice");
        auto pitch = 0.0;
        if (voicesAfter({ 48, 60 }, 2, 16, 1.0f, &pitch) != 1 || !(pitch > 48.0 && pitch < 60.0))
            return juce::Result::fail("Prisme in legato did not slide on one voice (" + juce::String(pitch) + ")");
    }
    return juce::Result::ok();
}

struct AudioTrackController::PlaybackTrack final {
    struct ClipAudioEditState {
        AudioClip clip;
    };

    // A rebuild deferred by an arrangement batch, owed at its end.
    bool arrangementPlanPending {};

    PlaybackTrack(std::unique_ptr<juce::AudioFormatReader> firstReader,
                  std::unique_ptr<juce::AudioFormatReader> secondReader,
                  juce::File sourceFile,
                  juce::TimeSliceThread& readAheadThread,
                  const int channels = 2)
        : gainPanSource(transportSource), latencySource(gainPanSource), file(std::move(sourceFile)),
          channelCount(cydaw::engine::clampTrackChannels(channels))
    {
        gainPanSource.setChannelCount(channelCount);
        latencySource.setChannelCount(channelCount);
        clipSource = std::make_unique<ClipAudioSource>(
            std::move(firstReader), std::move(secondReader), readAheadThread, channelCount);
        transportSource.setSource(clipSource.get(), 0, nullptr,
                                  clipSource->sampleRate(), cydaw::engine::maximumTrackChannels);
    }

    ~PlaybackTrack()
    {
        transportSource.stop();
        transportSource.setSource(nullptr);
    }

    void replaceSource(std::unique_ptr<juce::AudioFormatReader> firstReader,
                       std::unique_ptr<juce::AudioFormatReader> secondReader,
                       juce::File sourceFile,
                       juce::TimeSliceThread& readAheadThread)
    {
        const auto wasPlaying = transportSource.isPlaying();
        const auto position = transportSource.getCurrentPosition();
        const auto clipStart = clipSource->clipStartSeconds();
        const auto sourceOffset = clipSource->sourceOffsetSeconds();
        const auto clipDuration = clipSource->clipDurationSeconds();
        const auto shouldLoop = clipSource->isLooping();
        transportSource.stop();
        transportSource.setSource(nullptr);
        clipSource = std::make_unique<ClipAudioSource>(
            std::move(firstReader), std::move(secondReader), readAheadThread, channelCount);
        clipSource->setLooping(shouldLoop);
        clipSource->setClipRange(clipStart, sourceOffset, clipDuration);
        transportSource.setSource(clipSource.get(), 0, nullptr,
                                  clipSource->sampleRate(), cydaw::engine::maximumTrackChannels);
        transportSource.setPosition(juce::jlimit(
            0.0, clipSource->arrangementLengthSeconds(), position));
        if (wasPlaying)
            transportSource.start();
        file = std::move(sourceFile);
    }

    juce::AudioTransportSource transportSource;
    GainPanAudioSource gainPanSource;
    LatencyCompensatingAudioSource latencySource;
    std::unique_ptr<ClipAudioSource> clipSource;
    juce::File file;
    // Its format: 1 mono to 8 (7.1).
    int channelCount { 2 };
    cydaw::engine::NativeDspGraph nativeDspGraph;
    float gainDb {};
    float pan {};
    bool muted {};
    bool solo {};
    bool armed {};
    std::uint64_t inputBusId {};
    // Its input bus taken after the bus's effects (else before them).
    bool inputAfterEffects { true };
    float driveDb {};
    float filterCutoffHz { 20'000.0f };
    std::vector<AutomationPoint> automation;
    std::uint64_t nextAutomationId { 1 };
    // Its clips' volume automation while they drive it (none: its own).
    std::optional<std::vector<AutomationPoint>> clipVolume;
    std::vector<PluginAutomationLane> pluginAutomation;
    std::uint64_t nextPluginAutomationId { 1 };
    std::vector<AudioClip> audioClips;
    std::vector<LauncherClip> launcherClips;
    std::vector<juce::File> audioEditUndoSources;
    std::vector<juce::File> audioEditRedoSources;
    std::vector<ClipAudioEditState> clipAudioEditUndoStates;
    std::vector<ClipAudioEditState> clipAudioEditRedoStates;
    std::vector<std::vector<AudioClip>> arrangementEditUndoStates;
    std::vector<std::vector<AudioClip>> arrangementEditRedoStates;
};

// ---- A loop auditioned at the tempo --------------------------------------------------------

bool LoopAuditionSource::clock(std::int64_t& frames, const std::function<void()>& readWithIt) const
{
    for (int tries = 0; tries < 200; ++tries) {
        const auto before = sequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0u) {
            juce::Thread::sleep(1);
            continue;
        }
        frames = frames_.load(std::memory_order_acquire);
        readWithIt();
        if (sequence_.load(std::memory_order_acquire) == before)
            return true;
    }
    return false;
}

void LoopAuditionSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    const auto blockStart = frames_.load(std::memory_order_relaxed);
    {
        const auto loop = loop_.read();
        if (loop == nullptr || loop->frames <= 0 || loop->channels == nullptr || loop->channels->empty()) {
            info.clearActiveBufferRegion();
            playing_.store(false, std::memory_order_release);
        } else {
            const auto frames = loop->frames;
            if (loop->generation != generation_) {
                // Where it comes in: on the transport's beat (as heard,
                // after the mix's latency), else where it was asked to.
                generation_ = loop->generation;
                if (loop->sync) {
                    const auto elapsed = static_cast<double>(blockStart - loop->clockFrames) / loop->rate;
                    const auto beats = (loop->positionSeconds + elapsed - loop->latencySeconds) * loop->tempo / 60.0;
                    auto inLoop = std::fmod(beats, static_cast<double>(loop->beats));
                    if (inLoop < 0.0)
                        inLoop += static_cast<double>(loop->beats);
                    phase_ = static_cast<std::int64_t>(std::llround(inLoop / static_cast<double>(loop->beats)
                                                                    * static_cast<double>(frames)))
                             % frames;
                } else {
                    phase_ = ((loop->startFrame % frames) + frames) % frames;
                }
            }
            const auto gain = juce::Decibels::decibelsToGain(gainDb_.load(std::memory_order_relaxed));
            const auto& rendered = *loop->channels;
            const auto channels = static_cast<int>(rendered.size());
            for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel) {
                const auto& from = rendered[static_cast<std::size_t>(std::min(channel, channels - 1))];
                auto* out = info.buffer->getWritePointer(channel, info.startSample);
                auto at = phase_;
                for (int i = 0; i < info.numSamples; ++i) {
                    out[i] = from[static_cast<std::size_t>(at)] * gain;
                    if (++at >= frames)
                        at = 0;
                }
            }
            phase_ = (phase_ + info.numSamples) % frames;
            fraction_.store(static_cast<double>(phase_) / static_cast<double>(frames), std::memory_order_relaxed);
            playing_.store(true, std::memory_order_release);
        }
    }
    frames_.fetch_add(info.numSamples, std::memory_order_acq_rel);
    sequence_.fetch_add(1, std::memory_order_acq_rel);
}

AudioTrackController::AudioTrackController()
{
    formatManager_.registerBasicFormats();
    readAheadThread_.startThread(juce::Thread::Priority::normal);
    (void) addMidiTrack("Instrument 1");
    simulation_ = std::make_unique<Simulation>(*this);
    mixerSource_.addInputSource(simulation_.get(), false);
    dj_ = std::make_unique<DjEngine>();
    mixerSource_.addInputSource(dj_.get(), false);
    foundryPlayer_ = std::make_unique<FoundryPlayer>();
    mixerSource_.addInputSource(foundryPlayer_.get(), false);
    cinemaPlayer_ = std::make_unique<FoundryPlayer>();
    mixerSource_.addInputSource(cinemaPlayer_.get(), false);
    processingPlayer_ = std::make_unique<FoundryPlayer>();
    mixerSource_.addInputSource(processingPlayer_.get(), false);
    // The clock around the mix: it starts before it, ends with the loop
    // auditioned after it.
    auditionMixer_.addInputSource(&loopAudition_.clockStart(), false);
    auditionMixer_.addInputSource(&masterOutput_, false);
    auditionMixer_.addInputSource(&samplePreviewTransport_, false);
    auditionMixer_.addInputSource(&loopAudition_, false);
    virtualBusRouter_.setDirectOutputs(&mixerSource_);
    recorder_.setRouter(&virtualBusRouter_);
    sourcePlayer_.setSource(&virtualBusRouter_);
}

AudioTrackController::~AudioTrackController()
{
    midiOutputs_.reset();
    stopMidiRecording(false);
    recorder_.stop();
    masterOutput_.stopCapture();
    deviceManager_.removeMidiInputDeviceCallback({}, &gatedMidiInput_);
    deviceManager_.removeAudioCallback(&recorder_);
    deviceManager_.removeAudioCallback(&virtualBusRouter_);
    deviceManager_.removeAudioCallback(&sourcePlayer_);
    sourcePlayer_.setSource(nullptr);
    virtualBusRouter_.setDirectOutputs(nullptr);
    stopAudioClipPreview();
    stopSamplePreview();
    auditionMixer_.removeInputSource(&loopAudition_);
    auditionMixer_.removeInputSource(&samplePreviewTransport_);
    auditionMixer_.removeInputSource(&masterOutput_);
    auditionMixer_.removeInputSource(&loopAudition_.clockStart());
    mixerSource_.removeInputSource(simulation_.get());
    mixerSource_.removeInputSource(dj_.get());
    mixerSource_.removeInputSource(foundryPlayer_.get());
    mixerSource_.removeInputSource(cinemaPlayer_.get());
    mixerSource_.removeInputSource(processingPlayer_.get());
    clearTracks();
    clearMidiTracks();
    readAheadThread_.stopThread(3'000);
}

juce::String AudioTrackController::initialiseAudio(
    const juce::XmlElement* const savedState)
{
    auto error = deviceManager_.initialise(
        savedState == nullptr ? 2 : 0, 2, savedState, true);
    if (error.isNotEmpty() && savedState == nullptr) {
        deviceManager_.closeAudioDevice();
        error = deviceManager_.initialise(0, 2, nullptr, true);
    }
    deviceManager_.addAudioCallback(&sourcePlayer_);
    deviceManager_.addAudioCallback(&virtualBusRouter_);
    deviceManager_.addAudioCallback(&recorder_);
    deviceManager_.addMidiInputDeviceCallback({}, &gatedMidiInput_);
    return error;
}

juce::Result AudioTrackController::addAudioTrack(
    const juce::File& file, std::size_t* const newTrackIndex,
    const double initialClipStartSeconds)
{
    auto firstReader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(file));
    auto secondReader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(file));
    if (firstReader == nullptr || secondReader == nullptr)
        return juce::Result::fail(tr("error.openAudio", "file", file.getFileName()));

    // A stereo track, unless the file has more channels (a quad or 5.1
    // recording keeps them); a mono file spreads on it.
    const auto fileChannels = static_cast<int>(firstReader->numChannels);
    auto track = std::make_unique<PlaybackTrack>(
        std::move(firstReader), std::move(secondReader), file, readAheadThread_,
        fileChannels > 2 ? cydaw::engine::clampTrackChannels(fileChannels) : 2);
    track->inputBusId = virtualBusRouter_.defaultInputBusId();
    static constexpr std::array<std::uint32_t, 8> clipColours {
        0xffff9b2f, 0xff66b5ff, 0xffd67cf2, 0xff74c96b,
        0xffffd15a, 0xff53d0c2, 0xffff7373, 0xff9d91ff
    };
    track->audioClips.push_back({
        nextAudioClipId_++, file.getFileNameWithoutExtension(),
        std::max(0.0, initialClipStartSeconds), 0.0,
        track->clipSource->sourceLengthSeconds(),
        track->clipSource->sourceLengthSeconds(),
        clipColours[tracks_.size() % clipColours.size()], file,
        track->clipSource->sourceLengthSeconds() });
    track->audioClips.back().playEndSeconds =
        track->audioClips.back().durationSeconds;
    track->audioClips.back().loopEndSeconds =
        track->audioClips.back().durationSeconds;
    normaliseAudioClipTimeline(track->audioClips.back());
    const auto arrangementResult = rebuildArrangementPlan(*track);
    if (arrangementResult.failed())
        return arrangementResult;
    track->clipSource->setLooping(looping_);
    track->clipSource->setArrangementPlaybackEnabled(
        arrangementPlaybackEnabled_.load(std::memory_order_acquire));
    mixerSource_.addInputSource(&track->latencySource, false);
    tracks_.push_back(std::move(track));
    refreshTransportRanges();
    if (!defaultNativeDspGraph_.nodes.empty()) {
        const auto graphResult = tracks_.back()->gainPanSource.setNativeDspGraph(
            defaultNativeDspGraph_);
        if (graphResult.wasOk())
            tracks_.back()->nativeDspGraph = defaultNativeDspGraph_;
    }
    refreshLatencyCompensation();
    if (newTrackIndex != nullptr)
        *newTrackIndex = tracks_.size() - 1;
    return juce::Result::ok();
}

void AudioTrackController::setModeTrackSource(const std::size_t index, juce::AudioSource* source)
{
    if (auto* track = findTrack(index)) {
        track->gainPanSource.setExternalTimeline(&modeEndPosition_);
        track->gainPanSource.setExternalInput(source);
    }
}

void AudioTrackController::setModeGainAutomation(const bool midi, const std::uint64_t id, std::vector<GainAutomationPoint> points)
{
    // Foundry audio tracks have no transport file: its length must not clamp
    // points to zero. Their authoritative timeline belongs to the session.
    auto published=std::make_shared<const std::vector<GainAutomationPoint>>(std::move(points));
    if(midi) {if(auto* track=findMidiTrack(id))track->gainPanSource.setAutomation(std::move(published));}
    else if(auto* track=findTrack(static_cast<std::size_t>(id)))track->gainPanSource.setAutomation(std::move(published));
}

juce::Result AudioTrackController::addEmptyAudioTrack(
    std::size_t* const newTrackIndex)
{
    auto track = std::make_unique<PlaybackTrack>(
        std::make_unique<EmptyAudioFormatReader>(),
        std::make_unique<EmptyAudioFormatReader>(), juce::File {},
        readAheadThread_);
    track->inputBusId = virtualBusRouter_.defaultInputBusId();
    const auto arrangementResult = rebuildArrangementPlan(*track);
    if (arrangementResult.failed())
        return arrangementResult;
    track->clipSource->setLooping(looping_);
    track->clipSource->setArrangementPlaybackEnabled(
        arrangementPlaybackEnabled_.load(std::memory_order_acquire));
    mixerSource_.addInputSource(&track->latencySource, false);
    tracks_.push_back(std::move(track));
    refreshTransportRanges();
    if (!defaultNativeDspGraph_.nodes.empty()) {
        const auto graphResult = tracks_.back()->gainPanSource.setNativeDspGraph(
            defaultNativeDspGraph_);
        if (graphResult.wasOk())
            tracks_.back()->nativeDspGraph = defaultNativeDspGraph_;
    }
    refreshLatencyCompensation();
    if (newTrackIndex != nullptr)
        *newTrackIndex = tracks_.size() - 1;
    return juce::Result::ok();
}

juce::Result AudioTrackController::startSamplePreview(
    const juce::File& file, const double startSeconds, const SamplePreviewTempo& tempo)
{
    if (tempo.sourceBpm > 0.0)
        if (startTempoPreview(file, startSeconds, tempo).wasOk())
            return juce::Result::ok();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0)
        return juce::Result::fail(
            tr("error.openAudio", "file", file.getFileName()));

    const auto sourceSampleRate = reader->sampleRate;
    const auto sourceLengthSeconds = static_cast<double>(reader->lengthInSamples)
                                     / sourceSampleRate;
    auto source = std::make_unique<juce::AudioFormatReaderSource>(
        reader.release(), true);
    stopSamplePreview();
    samplePreviewReader_ = std::move(source);
    samplePreviewTransport_.setSource(samplePreviewReader_.get(), 32'768,
                                      &readAheadThread_, sourceSampleRate);
    samplePreviewTransport_.setGain(juce::Decibels::decibelsToGain(
        samplePreviewGainDb_.load(std::memory_order_relaxed)));
    samplePreviewTransport_.setPosition(juce::jlimit(
        0.0, juce::jmax(0.0, sourceLengthSeconds - 0.001), startSeconds));
    samplePreviewTransport_.start();
    return juce::Result::ok();
}

juce::Result AudioTrackController::startTempoPreview(const juce::File& file, const double startSeconds,
                                                     const SamplePreviewTempo& tempo)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return juce::Result::fail(tr("error.openAudio", "file", file.getFileName()));
    // A loop of whole beats: the file's length in beats, rounded.
    const auto seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    const auto beats = static_cast<int>(std::lround(seconds * tempo.sourceBpm / 60.0));
    if (beats < 1 || seconds > 32.0)
        return juce::Result::fail("not a loop");
    const auto loopSeconds = static_cast<double>(beats) * 60.0 / tempo.sourceBpm;
    const auto frames = std::min<juce::int64>(reader->lengthInSamples,
                                              static_cast<juce::int64>(std::ceil(loopSeconds * reader->sampleRate)));
    const auto channels = std::clamp(static_cast<int>(reader->numChannels), 1, 2);
    juce::AudioBuffer<float> decoded(channels, static_cast<int>(frames));
    if (!reader->read(&decoded, 0, static_cast<int>(frames), 0, true, channels > 1))
        return juce::Result::fail(tr("error.openAudio", "file", file.getFileName()));
    std::vector<std::vector<float>> source(static_cast<std::size_t>(channels));
    for (int channel = 0; channel < channels; ++channel)
        source[static_cast<std::size_t>(channel)].assign(decoded.getReadPointer(channel),
                                                         decoded.getReadPointer(channel) + frames);
    const auto rate = loopAudition_.sampleRate();
    auto loop = std::make_shared<LoopAuditionSource::Loop>();
    auto rendered = std::make_shared<std::vector<std::vector<float>>>(cydaw::engine::renderStretchedLoop(
        source, reader->sampleRate, tempo.sourceBpm, beats, tempoBpm_, rate,
        static_cast<cydaw::engine::StretchAlgorithm>(std::clamp(tempo.algorithm, 0, 3))));
    if (rendered->empty() || rendered->front().empty())
        return juce::Result::fail("could not stretch " + file.getFileName());
    loop->frames = static_cast<std::int64_t>(rendered->front().size());
    loop->channels = std::move(rendered);
    loop->beats = beats;
    loop->tempo = tempoBpm_;
    loop->rate = rate;
    loop->generation = ++tempoPreview_.generation;
    // In step with the transport while it plays; else from where it was
    // asked (in the file's seconds).
    loop->sync = isPlaying();
    if (loop->sync) {
        std::int64_t clockFrames {};
        double position {};
        if (loopAudition_.clock(clockFrames, [this, &position] { position = this->position(); })) {
            loop->clockFrames = clockFrames;
            loop->positionSeconds = position;
            loop->latencySeconds = static_cast<double>(mixLatencySamples()) / rate;
        } else {
            loop->sync = false;
        }
    }
    loop->startFrame = static_cast<std::int64_t>(std::clamp(startSeconds / loopSeconds, 0.0, 1.0)
                                                 * static_cast<double>(loop->frames));
    samplePreviewTransport_.stop();
    samplePreviewTransport_.setSource(nullptr);
    samplePreviewReader_.reset();
    tempoPreview_.file = file;
    tempoPreview_.tempo = tempo;
    tempoPreview_.beats = beats;
    tempoPreview_.sourceSeconds = loopSeconds;
    tempoPreview_.loop = loop;
    loopAudition_.play(std::move(loop));
    return juce::Result::ok();
}

void AudioTrackController::followTempoPreview()
{
    const auto current = tempoPreview_.loop;
    if (tempoPreview_.file == juce::File() || current == nullptr || !isPlaying() || !loopAudition_.playing())
        return;
    // Five times a second is enough.
    const auto now = juce::Time::getMillisecondCounter();
    if (now - tempoPreview_.checked < 200)
        return;
    tempoPreview_.checked = now;
    // Where it is and where the transport is, at the end of the same block.
    std::int64_t clockFrames {};
    double position {};
    double fraction {};
    if (!loopAudition_.clock(clockFrames, [this, &position, &fraction] {
            position = this->position();
            fraction = loopAudition_.fraction();
        }))
        return;
    const auto latency = static_cast<double>(mixLatencySamples()) / current->rate;
    const auto beats = static_cast<double>(current->beats);
    auto expected = std::fmod((position - latency) * current->tempo / 60.0, beats) / beats;
    if (expected < 0.0)
        expected += 1.0;
    auto error = fraction - expected;
    error -= std::round(error);
    // Within a few milliseconds: in step.
    if (current->sync && std::abs(error) * beats * 60.0 / current->tempo < 0.004)
        return;
    auto again = std::make_shared<LoopAuditionSource::Loop>(*current);
    again->generation = ++tempoPreview_.generation;
    again->sync = true;
    again->clockFrames = clockFrames;
    again->positionSeconds = position;
    again->latencySeconds = latency;
    tempoPreview_.loop = again;
    loopAudition_.play(std::move(again));
}

int AudioTrackController::samplePreviewLoopBeats() const noexcept
{
    return tempoPreview_.file != juce::File() ? tempoPreview_.beats : 0;
}

void AudioTrackController::stopSamplePreview()
{
    samplePreviewTransport_.stop();
    samplePreviewTransport_.setSource(nullptr);
    samplePreviewReader_.reset();
    loopAudition_.stop();
    tempoPreview_.file = juce::File();
    tempoPreview_.loop.reset();
}

bool AudioTrackController::isSamplePreviewing() const noexcept
{
    return samplePreviewTransport_.isPlaying() || tempoPreview_.file != juce::File();
}

double AudioTrackController::samplePreviewPositionSeconds() const noexcept
{
    if (tempoPreview_.file != juce::File())
        return loopAudition_.fraction() * tempoPreview_.sourceSeconds;
    return samplePreviewTransport_.getCurrentPosition();
}

void AudioTrackController::setSamplePreviewGainDb(const float gainDb) noexcept
{
    const auto value = juce::jlimit(-48.0f, 6.0f, gainDb);
    samplePreviewGainDb_.store(value, std::memory_order_relaxed);
    samplePreviewTransport_.setGain(
        juce::Decibels::decibelsToGain(value));
}

float AudioTrackController::samplePreviewGainDb() const noexcept
{
    return samplePreviewGainDb_.load(std::memory_order_relaxed);
}

juce::Result AudioTrackController::startAudioClipPreview(
    const std::size_t trackIndex, const std::uint64_t clipId,
    const double localStartSeconds)
{
    const auto clipValue = audioClip(trackIndex, clipId);
    if (!clipValue.has_value())
        return juce::Result::fail("Clip not found");

    stopSamplePreview();
    stopAudioClipPreview();
    auto preview = std::make_unique<PlaybackTrack>(
        std::make_unique<EmptyAudioFormatReader>(),
        std::make_unique<EmptyAudioFormatReader>(), juce::File {},
        readAheadThread_);
    auto clip = *clipValue;
    normaliseAudioClipTimeline(clip);
    clip.startSeconds = 0.0;
    preview->audioClips.push_back(std::move(clip));
    if (const auto result = rebuildArrangementPlan(*preview);
        result.failed())
        return result;

    const auto& bounds = preview->audioClips.front();
    preview->clipSource->setTransportRange(
        bounds.playEndSeconds, bounds.loopStartSeconds,
        bounds.loopEndSeconds);
    preview->clipSource->setLooping(bounds.looping);
    preview->transportSource.setPosition(juce::jlimit(
        bounds.playStartSeconds, bounds.playEndSeconds,
        localStartSeconds));
    auditionMixer_.addInputSource(&preview->latencySource, false);
    clipPreviewTrack_ = std::move(preview);
    clipPreviewTrack_->transportSource.start();
    return juce::Result::ok();
}

juce::Result AudioTrackController::refreshAudioClipPreview(
    const std::size_t trackIndex, const std::uint64_t clipId)
{
    if (clipPreviewTrack_ == nullptr)
        return juce::Result::fail("Clip preview is not active");
    const auto clipValue = audioClip(trackIndex, clipId);
    if (!clipValue.has_value())
        return juce::Result::fail("Clip not found");

    auto clip = *clipValue;
    normaliseAudioClipTimeline(clip);
    clip.startSeconds = 0.0;
    const auto position = clipPreviewTrack_->transportSource
                              .getCurrentPosition();
    const auto wasPlaying = clipPreviewTrack_->transportSource.isPlaying();
    if (wasPlaying)
        clipPreviewTrack_->transportSource.stop();
    auto previous = clipPreviewTrack_->audioClips;
    clipPreviewTrack_->audioClips = { std::move(clip) };
    if (const auto result = rebuildArrangementPlan(*clipPreviewTrack_);
        result.failed()) {
        clipPreviewTrack_->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*clipPreviewTrack_);
        if (wasPlaying)
            clipPreviewTrack_->transportSource.start();
        return result;
    }

    const auto& bounds = clipPreviewTrack_->audioClips.front();
    clipPreviewTrack_->clipSource->setTransportRange(
        bounds.playEndSeconds, bounds.loopStartSeconds,
        bounds.loopEndSeconds);
    clipPreviewTrack_->clipSource->setLooping(bounds.looping);
    clipPreviewTrack_->transportSource.setPosition(juce::jlimit(
        bounds.playStartSeconds, bounds.playEndSeconds, position));
    if (wasPlaying)
        clipPreviewTrack_->transportSource.start();
    return juce::Result::ok();
}

void AudioTrackController::stopAudioClipPreview()
{
    if (clipPreviewTrack_ == nullptr)
        return;
    clipPreviewTrack_->transportSource.stop();
    auditionMixer_.removeInputSource(&clipPreviewTrack_->latencySource);
    clipPreviewTrack_.reset();
}

bool AudioTrackController::isAudioClipPreviewing() const noexcept
{
    return clipPreviewTrack_ != nullptr
           && clipPreviewTrack_->transportSource.isPlaying();
}

double AudioTrackController::audioClipPreviewPositionSeconds() const noexcept
{
    return clipPreviewTrack_ != nullptr
               ? clipPreviewTrack_->transportSource.getCurrentPosition()
               : 0.0;
}

juce::Result AudioTrackController::rebuildArrangementPlan(PlaybackTrack& track)
{
    if (arrangementBatchDepth_ > 0) {
        track.arrangementPlanPending = true;
        return juce::Result::ok();
    }
    std::vector<PreparedArrangementRegion> prepared;
    std::size_t renderClipCount {};
    for (const auto& clip : track.audioClips) {
        if (clip.events.empty()) {
            if (!clip.usesEventTimeline
                && (clip.sourceFile != juce::File {}
                    || track.file != juce::File {}))
                ++renderClipCount;
        } else {
            renderClipCount += clip.events.size();
        }
    }
    prepared.reserve(renderClipCount * 2);

    std::vector<AudioClip> eventClips;
    eventClips.reserve(renderClipCount);
    std::vector<AudioClip*> clipsToRender;
    clipsToRender.reserve(renderClipCount);
    for (auto& container : track.audioClips) {
        if (container.events.empty()) {
            if (!container.usesEventTimeline
                && (container.sourceFile != juce::File {}
                    || track.file != juce::File {}))
                clipsToRender.push_back(&container);
            continue;
        }
        container.startSeconds = std::max(0.0, container.startSeconds);
        for (const auto& event : container.events) {
            auto render = container;
            render.events.clear();
            render.id = event.id;
            render.name = event.name;
            render.startSeconds = container.startSeconds
                                  + std::max(0.0, event.startSeconds);
            render.sourceOffsetSeconds = event.sourceOffsetSeconds;
            render.durationSeconds = event.durationSeconds;
            render.sourceDurationSeconds = event.sourceDurationSeconds;
            render.colour = event.colour;
            render.sourceFile = event.sourceFile;
            render.sourceLengthSeconds = event.sourceLengthSeconds;
            render.fadeInSeconds = event.fadeInSeconds;
            render.fadeOutSeconds = event.fadeOutSeconds;
            render.sourceTempoBpm = event.sourceTempoBpm;
            render.stretchMode = event.stretchMode;
            render.pitchSemitones = event.pitchSemitones;
            render.stretchMarkers = event.stretchMarkers;
            render.eventGainDb = event.gainDb;
            render.soundLanes = event.lanes;
            // Container automation remains expressed in clip-local time.
            // Rendering this event uses event-local time, so translate the
            // immutable render copy without touching the editor model.
            shiftAutomation(render.automation, -event.startSeconds);
            render.takes.clear();
            render.activeTakeIndex = 0;
            eventClips.push_back(std::move(render));
        }
    }
    for (auto& eventClip : eventClips)
        clipsToRender.push_back(&eventClip);

    for (auto* clipPointer : clipsToRender) {
        auto& clip = *clipPointer;
        if (clip.sourceFile == juce::File {})
            clip.sourceFile = track.file;
        if (clip.takes.empty())
            clip.takes.push_back({
                clip.name.isNotEmpty()
                    ? clip.name
                    : clip.sourceFile.getFileNameWithoutExtension(),
                clip.sourceFile,
                clip.sourceLengthSeconds
            });
        clip.activeTakeIndex = std::min(
            clip.activeTakeIndex, clip.takes.size() - 1);
        auto& activeTake = clip.takes[clip.activeTakeIndex];
        if (activeTake.sourceFile == juce::File {})
            activeTake.sourceFile = clip.sourceFile;
        clip.sourceFile = activeTake.sourceFile;
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager_.createReaderFor(clip.sourceFile));
        if (reader == nullptr || reader->sampleRate <= 0.0
            || reader->lengthInSamples <= 0)
            return juce::Result::fail(tr("error.openAudio", "file",
                                         clip.sourceFile.getFileName()));
        const auto sourceLength = static_cast<double>(reader->lengthInSamples)
                                  / reader->sampleRate;
        const auto minimumDuration = 1.0 / reader->sampleRate;
        clip.startSeconds = std::max(0.0, clip.startSeconds);
        clip.sourceOffsetSeconds = juce::jlimit(
            0.0, std::max(0.0, sourceLength - minimumDuration),
            clip.sourceOffsetSeconds);
        clip.sourceDurationSeconds = juce::jlimit(
            minimumDuration,
            std::max(minimumDuration, sourceLength - clip.sourceOffsetSeconds),
            clip.sourceDurationSeconds > 0.0
                ? clip.sourceDurationSeconds
                : clip.durationSeconds);
        clip.durationSeconds = juce::jlimit(
            0.001, 86'400.0, clip.durationSeconds);
        clip.fadeInSeconds = juce::jlimit(
            0.0, clip.durationSeconds, clip.fadeInSeconds);
        clip.fadeOutSeconds = juce::jlimit(
            0.0, clip.durationSeconds, clip.fadeOutSeconds);
        if (clip.name.isEmpty())
            clip.name = clip.sourceFile.getFileNameWithoutExtension();
        clip.sourceLengthSeconds = sourceLength;
        activeTake.sourceLengthSeconds = sourceLength;
        if (activeTake.name.isEmpty())
            activeTake.name = clip.sourceFile.getFileNameWithoutExtension();
        struct WarpPoint {
            double source {};
            double timeline {};
        };
        std::vector<WarpPoint> warpPoints;
        warpPoints.reserve(clip.stretchMarkers.size() + 2);
        warpPoints.push_back({ clip.sourceOffsetSeconds, 0.0 });
        std::sort(clip.stretchMarkers.begin(), clip.stretchMarkers.end(),
                  [](const auto& left, const auto& right) {
                      return left.sourceSeconds < right.sourceSeconds;
                  });
        auto previousSource = clip.sourceOffsetSeconds;
        auto previousTimeline = 0.0;
        const auto sourceEnd = clip.sourceOffsetSeconds
                               + clip.sourceDurationSeconds;
        for (const auto& marker : clip.stretchMarkers) {
            // Raw: never stretched, its points (if any) left aside.
            if (clip.stretchMode == StretchMode::raw)
                break;
            if (marker.sourceSeconds <= previousSource + minimumDuration
                || marker.sourceSeconds >= sourceEnd - minimumDuration
                || marker.timelineSeconds <= previousTimeline + 0.0005
                || marker.timelineSeconds >= clip.durationSeconds - 0.0005)
                continue;
            warpPoints.push_back(
                { marker.sourceSeconds, marker.timelineSeconds });
            previousSource = marker.sourceSeconds;
            previousTimeline = marker.timelineSeconds;
        }
        warpPoints.push_back({ sourceEnd, clip.durationSeconds });
        reader.reset();

        // The passages it plays: its comp's (each from a take, meeting the
        // next with a short crossfade), or the active take throughout.
        struct Passage {
            double from {};
            double to {};
            std::size_t take {};
            double fadeIn {};
            double fadeOut {};
        };
        std::vector<Passage> passages;
        if (clip.comp.empty() || clip.takes.size() < 2) {
            passages.push_back({ 0.0, clip.durationSeconds, clip.activeTakeIndex, clip.fadeInSeconds, clip.fadeOutSeconds });
        } else {
            constexpr double crossfade = 0.01;
            for (const auto& part : clip.comp) {
                const auto from = juce::jlimit(0.0, clip.durationSeconds, part.startSeconds);
                const auto to = juce::jlimit(from, clip.durationSeconds, part.endSeconds);
                if (to - from < 0.0005)
                    continue;
                const auto atStart = from <= 1.0e-6;
                const auto atEnd = to >= clip.durationSeconds - 1.0e-6;
                passages.push_back({ atStart ? 0.0 : std::max(0.0, from - crossfade * 0.5),
                                     atEnd ? clip.durationSeconds : std::min(clip.durationSeconds, to + crossfade * 0.5),
                                     std::min(part.take, clip.takes.size() - 1),
                                     atStart ? clip.fadeInSeconds : crossfade,
                                     atEnd ? clip.fadeOutSeconds : crossfade });
            }
        }
        for (const auto& passage : passages) {
            const auto& take = clip.takes[passage.take];
            const auto file = take.sourceFile != juce::File {} ? take.sourceFile : clip.sourceFile;
            for (std::size_t segmentIndex = 1; segmentIndex < warpPoints.size(); ++segmentIndex) {
                const auto& first = warpPoints[segmentIndex - 1];
                const auto& last = warpPoints[segmentIndex];
                auto from = std::max(passage.from, first.timeline);
                const auto to = std::min(passage.to, last.timeline);
                if (to - from < 1.0e-6)
                    continue;
                const auto speed = (last.source - first.source) / std::max(1.0e-9, last.timeline - first.timeline);
                // In the take's file: where the clip's own source is, moved by
                // the take's shift (before the file's start: later).
                auto sourceFrom = first.source + (from - first.timeline) * speed + take.shiftSeconds;
                if (sourceFrom < 0.0) {
                    from += -sourceFrom / std::max(1.0e-9, speed);
                    sourceFrom = 0.0;
                    if (to - from < 1.0e-6)
                        continue;
                }
                auto segment = clip;
                segment.comp.clear();
                segment.versions.clear();
                segment.sourceFile = file;
                segment.startSeconds = clip.startSeconds + from;
                segment.sourceOffsetSeconds = sourceFrom;
                segment.sourceDurationSeconds = (to - from) * speed;
                segment.durationSeconds = to - from;
                segment.fadeInSeconds = from <= passage.from + 1.0e-9 ? passage.fadeIn : 0.0;
                segment.fadeOutSeconds = to >= passage.to - 1.0e-9 ? passage.fadeOut : 0.0;
                shiftAutomation(segment.automation, -from);
                shiftLanes(segment.soundLanes, -from);
                auto segmentReader = std::unique_ptr<juce::AudioFormatReader>(formatManager_.createReaderFor(file));
                if (segmentReader == nullptr)
                    return juce::Result::fail(tr("error.openAudio", "file", file.getFileName()));
                prepared.push_back({ std::move(segment), std::move(segmentReader) });
            }
        }
    }
    const auto result = track.clipSource->setArrangementRegions(
        std::move(prepared));
    if (result.wasOk())
        refreshTransportRanges();
    return result;
}

juce::Result AudioTrackController::rebuildLauncherPlans(PlaybackTrack& track)
{
    for (int sceneIndex = 0; sceneIndex < maximumLauncherScenes; ++sceneIndex)
        track.clipSource->clearLauncherSceneSource(sceneIndex);
    for (auto& clip : track.launcherClips) {
        if (clip.sourceFile == juce::File {})
            clip.sourceFile = track.file;
        normaliseLauncherClipTimeline(clip);
        std::vector<AudioEvent> events = clip.events;
        if (!clip.usesEventTimeline && events.empty()
            && clip.sourceFile != juce::File {})
            events.push_back({
                0, clip.name, 0.0, clip.sourceOffsetSeconds,
                clip.durationSeconds, clip.durationSeconds, clip.colour,
                clip.sourceFile, 0.0 });
        std::vector<PreparedArrangementRegion> prepared;
        for (const auto& event : events) {
            auto reader = std::unique_ptr<juce::AudioFormatReader>(
                formatManager_.createReaderFor(event.sourceFile));
            if (reader == nullptr || reader->sampleRate <= 0.0
                || reader->lengthInSamples <= 0)
                return juce::Result::fail(tr(
                    "error.openAudio", "file",
                    event.sourceFile.getFileName()));
            AudioClip render;
            render.id = event.id;
            render.name = event.name;
            render.startSeconds = std::max(0.0, event.startSeconds);
            render.sourceOffsetSeconds = event.sourceOffsetSeconds;
            render.durationSeconds = event.durationSeconds;
            render.sourceDurationSeconds = event.sourceDurationSeconds;
            render.colour = event.colour;
            render.sourceFile = event.sourceFile;
            render.sourceLengthSeconds = event.sourceLengthSeconds;
            render.fadeInSeconds = event.fadeInSeconds;
            render.fadeOutSeconds = event.fadeOutSeconds;
            render.sourceTempoBpm = event.sourceTempoBpm;
            render.stretchMode = event.stretchMode;
            render.pitchSemitones = event.pitchSemitones;
            render.stretchMarkers = event.stretchMarkers;
            render.eventGainDb = event.gainDb;
            render.soundLanes = event.lanes;
            render.automation = clip.automation;
            shiftAutomation(render.automation, -event.startSeconds);
            // Raw: never stretched, its points (if any) left aside.
            if (event.stretchMarkers.empty() || event.stretchMode == StretchMode::raw) {
                prepared.push_back({ std::move(render),
                                     std::move(reader) });
                continue;
            }
            struct WarpPoint { double source; double timeline; };
            std::vector<WarpPoint> points;
            points.push_back({ event.sourceOffsetSeconds, 0.0 });
            auto markers = event.stretchMarkers;
            std::sort(markers.begin(), markers.end(),
                      [](const auto& left, const auto& right) {
                          return left.sourceSeconds < right.sourceSeconds;
                      });
            for (const auto& marker : markers)
                if (marker.sourceSeconds > points.back().source + 0.000001
                    && marker.timelineSeconds
                           > points.back().timeline + 0.000001)
                    points.push_back({ marker.sourceSeconds,
                                       marker.timelineSeconds });
            points.push_back({ event.sourceOffsetSeconds
                                   + event.sourceDurationSeconds,
                               event.durationSeconds });
            for (std::size_t index = 1; index < points.size(); ++index) {
                auto segment = render;
                segment.startSeconds = render.startSeconds
                                       + points[index - 1].timeline;
                segment.sourceOffsetSeconds = points[index - 1].source;
                segment.sourceDurationSeconds = points[index].source
                                                - points[index - 1].source;
                segment.durationSeconds = points[index].timeline
                                          - points[index - 1].timeline;
                segment.fadeInSeconds = index == 1
                                            ? render.fadeInSeconds : 0.0;
                segment.fadeOutSeconds = index + 1 == points.size()
                                             ? render.fadeOutSeconds : 0.0;
                shiftAutomation(segment.automation,
                                -points[index - 1].timeline);
                shiftLanes(segment.soundLanes, -points[index - 1].timeline);
                auto segmentReader = index == 1
                                         ? std::move(reader)
                                         : std::unique_ptr<
                                               juce::AudioFormatReader>(
                                               formatManager_.createReaderFor(
                                                   event.sourceFile));
                prepared.push_back({ std::move(segment),
                                     std::move(segmentReader) });
            }
        }
        const auto result = track.clipSource->setLauncherSceneRegions(
            clip.sceneIndex, std::move(prepared), clip.durationSeconds,
            clip.playStartSeconds, clip.playEndSeconds,
            clip.loopStartSeconds, clip.loopEndSeconds, clip.looping);
        if (result.failed())
            return result;
    }
    return juce::Result::ok();
}

void AudioTrackController::recordArrangementEdit(
    PlaybackTrack& track,
    std::vector<AudioClip> previous)
{
    if (sameArrangement(previous, track.audioClips))
        return;
    static constexpr std::size_t maximumHistorySize = 32;
    const auto foundTrack = std::find_if(
        tracks_.begin(), tracks_.end(), [&track](const auto& candidate) {
            return candidate.get() == &track;
        });
    if (foundTrack != tracks_.end()) {
        const auto trackIndex = static_cast<std::size_t>(
            std::distance(tracks_.begin(), foundTrack));
        if (arrangementEditBatch_.has_value()) {
            const auto alreadyCaptured = std::any_of(
                arrangementEditBatch_->tracks.begin(),
                arrangementEditBatch_->tracks.end(),
                [trackIndex](const auto& state) {
                    return state.trackIndex == trackIndex;
                });
            if (!alreadyCaptured)
                arrangementEditBatch_->tracks.push_back(
                    { trackIndex, previous });
        } else {
            ArrangementSessionState state;
            state.tracks.push_back({ trackIndex, previous });
            arrangementSessionUndoStates_.push_back(std::move(state));
            if (arrangementSessionUndoStates_.size() > maximumHistorySize)
                arrangementSessionUndoStates_.erase(
                    arrangementSessionUndoStates_.begin());
            arrangementSessionRedoStates_.clear();
        }
    }
    track.arrangementEditUndoStates.push_back(std::move(previous));
    if (track.arrangementEditUndoStates.size() > maximumHistorySize)
        track.arrangementEditUndoStates.erase(
            track.arrangementEditUndoStates.begin());
    track.arrangementEditRedoStates.clear();
}

bool AudioTrackController::restoreArrangementSessionState(
    const ArrangementSessionState& state,
    ArrangementSessionState& inverse)
{
    inverse.tracks.clear();
    inverse.tracks.reserve(state.tracks.size());
    for (const auto& target : state.tracks) {
        const auto* track = findTrack(target.trackIndex);
        if (track == nullptr)
            return false;
        inverse.tracks.push_back(
            { target.trackIndex, track->audioClips });
    }

    std::size_t restoredCount {};
    for (const auto& target : state.tracks) {
        auto* track = findTrack(target.trackIndex);
        if (track == nullptr
            || !restoreArrangementEditState(*track, target.clips)) {
            for (std::size_t rollbackIndex = 0;
                 rollbackIndex < restoredCount; ++rollbackIndex) {
                if (auto* rollbackTrack = findTrack(
                        inverse.tracks[rollbackIndex].trackIndex))
                    (void) restoreArrangementEditState(
                        *rollbackTrack,
                        inverse.tracks[rollbackIndex].clips);
            }
            return false;
        }
        ++restoredCount;
    }
    return true;
}

bool AudioTrackController::restoreArrangementEditState(
    PlaybackTrack& track,
    const std::vector<AudioClip>& state)
{
    const auto previous = track.audioClips;
    const auto wasPlaying = track.transportSource.isPlaying();
    const auto currentPosition = track.transportSource.getCurrentPosition();
    if (wasPlaying)
        track.transportSource.stop();
    track.audioClips = state;
    if (rebuildArrangementPlan(track).failed()) {
        track.audioClips = previous;
        (void) rebuildArrangementPlan(track);
        if (wasPlaying)
            track.transportSource.start();
        return false;
    }
    const auto trackLength = track.clipSource->arrangementLengthSeconds();
    const auto sessionLength = transportLength();
    track.transportSource.setPosition(
        juce::jlimit(0.0, sessionLength, currentPosition));
    if (wasPlaying && currentPosition < sessionLength)
        track.transportSource.start();
    for (auto& point : track.automation)
        point.timeSeconds = std::min(point.timeSeconds, trackLength);
    for (auto& lane : track.pluginAutomation)
        for (auto& point : lane.points)
            point.timeSeconds = std::min(point.timeSeconds, trackLength);
    publishTrackAutomation(track);
    publishTrackPluginAutomation(track);
    return true;
}

juce::Result AudioTrackController::selfTestDoubleDeckLauncher(
    const juce::File& sourceFile,
    const juce::File& alternateSourceFile)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto firstReader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(sourceFile));
    auto secondReader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(sourceFile));
    auto expectedReader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(sourceFile));
    const auto alternate = alternateSourceFile.existsAsFile()
                               ? alternateSourceFile
                               : sourceFile;
    auto launcherReader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(alternate));
    auto launcherExpected = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(alternate));
    if (firstReader == nullptr || secondReader == nullptr || expectedReader == nullptr
        || launcherReader == nullptr || launcherExpected == nullptr
        || firstReader->sampleRate <= 0.0 || firstReader->lengthInSamples <= 0)
        return juce::Result::fail("Cannot open double-deck self-test audio");

    const auto sampleRate = firstReader->sampleRate;
    const auto firstOffsetSamples = juce::jlimit<juce::int64>(
        0, firstReader->lengthInSamples - 1,
        static_cast<juce::int64>(std::llround(sampleRate * 0.101)));
    const auto secondOffsetSamples = juce::jlimit<juce::int64>(
        0, firstReader->lengthInSamples - 1,
        static_cast<juce::int64>(std::llround(sampleRate * 0.207)));
    constexpr int blockSize = 64;
    juce::TimeSliceThread readAheadThread("CyDAW double-deck self-test");
    readAheadThread.startThread(juce::Thread::Priority::normal);
    auto result = juce::Result::ok();
    {
        ClipAudioSource source(std::move(firstReader), std::move(secondReader),
                               readAheadThread);
        source.prepareToPlay(blockSize, sampleRate);
        source.scheduleLauncherClip(
            3, static_cast<double>(firstOffsetSamples) / sampleRate,
            0.05, true, 16.0 / sampleRate);
        for (int attempt = 0;
             attempt < 500 && !source.pendingDeckHasReadAhead(); ++attempt)
            juce::Thread::sleep(2);
        if (!source.pendingDeckHasReadAhead()) {
            result = juce::Result::fail("Standby deck did not prefetch");
        } else {
            juce::AudioBuffer<float> rendered(2, blockSize);
            source.getNextAudioBlock({ &rendered, 0, blockSize });
            juce::AudioBuffer<float> expected(2, 2);
            expected.clear();
            (void) expectedReader->read(&expected, 0, 1, 5,
                                        true, true);
            (void) expectedReader->read(&expected, 1, 1, firstOffsetSamples,
                                        true, true);
            if (source.activeLauncherScene() != 3
                || std::abs(rendered.getSample(0, 5) - expected.getSample(0, 0))
                       > 0.000001f
                || std::abs(rendered.getSample(0, 16) - expected.getSample(0, 1))
                       > 0.000001f) {
                result = juce::Result::fail(
                    "First deck did not activate at the requested sample");
            } else {
                source.scheduleLauncherClip(
                    5, static_cast<double>(secondOffsetSamples) / sampleRate,
                    0.05, true, 0.0);
                for (int attempt = 0;
                     attempt < 500 && !source.pendingDeckHasReadAhead(); ++attempt)
                    juce::Thread::sleep(2);
                rendered.clear();
                source.getNextAudioBlock({ &rendered, 0, blockSize });
                expected.clear();
                (void) expectedReader->read(&expected, 0, 1,
                                            secondOffsetSamples, true, true);
                if (source.activeLauncherScene() != 5
                    || std::abs(rendered.getSample(0, 0)
                                - expected.getSample(0, 0)) > 0.000001f)
                    result = juce::Result::fail(
                        "Second deck did not replace the active deck exactly");
            }
        }
        if (result.wasOk()) {
            const auto alternateOffset = 0.113;
            const auto alternateOffsetSamples = juce::jlimit<juce::int64>(
                0, launcherExpected->lengthInSamples - 1,
                static_cast<juce::int64>(std::llround(
                    launcherExpected->sampleRate * alternateOffset)));
            const auto planResult = source.setLauncherSceneSource(
                7, std::move(launcherReader), alternateOffset, 0.05, true);
            source.scheduleLauncherClip(7, 0.0, 0.05, true, 0.0);
            for (int attempt = 0;
                 attempt < 500 && !source.pendingDeckHasReadAhead(); ++attempt)
                juce::Thread::sleep(2);
            juce::AudioBuffer<float> rendered(2, blockSize);
            rendered.clear();
            source.getNextAudioBlock({ &rendered, 0, blockSize });
            juce::AudioBuffer<float> expected(2, 1);
            expected.clear();
            (void) launcherExpected->read(
                &expected, 0, 1, alternateOffsetSamples, true, true);
            if (planResult.failed()
                || source.activeLauncherScene() != 7
                || std::abs(rendered.getSample(0, 0)
                            - expected.getSample(0, 0)) > 0.001f)
                result = juce::Result::fail(
                    "An independent launcher source did not prefetch or render: "
                    + planResult.getErrorMessage()
                    + " scene=" + juce::String(source.activeLauncherScene())
                    + " rendered=" + juce::String(
                          rendered.getSample(0, 0), 6)
                    + " expected=" + juce::String(
                          expected.getSample(0, 0), 6));
        }
        source.releaseResources();
    }
    readAheadThread.stopThread(3'000);
    return result;
}

juce::Result AudioTrackController::selfTestLauncherTimeline(
    const juce::File& eventSourceFile)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto eventReader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(eventSourceFile));
    auto expectedReader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(eventSourceFile));
    if (eventReader == nullptr || expectedReader == nullptr
        || eventReader->sampleRate <= 0.0
        || eventReader->lengthInSamples <= 0)
        return juce::Result::fail(
            "Cannot open launcher timeline self-test audio");

    constexpr int blockSize = 64;
    constexpr double timelineDuration = 0.25;
    juce::TimeSliceThread readAheadThread(
        "CyDAW launcher timeline self-test");
    readAheadThread.startThread(juce::Thread::Priority::normal);
    auto result = juce::Result::ok();
    {
        ClipAudioSource source(
            std::make_unique<EmptyAudioFormatReader>(),
            std::make_unique<EmptyAudioFormatReader>(), readAheadThread);
        AudioClip event;
        event.id = 1;
        event.name = "Timeline event";
        event.startSeconds = 0.0;
        event.sourceOffsetSeconds = 0.0;
        event.durationSeconds = 0.05;
        event.sourceDurationSeconds = 0.05;
        event.fadeInSeconds = 0.01;
        event.fadeOutSeconds = 0.01;
        event.colour = 0xff4fd3ae;
        std::vector<PreparedArrangementRegion> regions;
        regions.push_back({ std::move(event), std::move(eventReader) });
        source.prepareToPlay(blockSize, 48'000.0);
        result = source.setLauncherSceneRegions(
            6, std::move(regions), timelineDuration, 0.0,
            timelineDuration, 0.0, timelineDuration, false);
        for (int attempt = 0;
             result.wasOk() && attempt < 500
             && !source.pendingDeckHasReadAhead(); ++attempt) {
            source.scheduleLauncherClip(6, 0.0, 0.05, false, 0.0);
            juce::Thread::sleep(2);
        }
        if (result.wasOk()) {
            source.scheduleLauncherClip(6, 0.0, 0.05, false, 0.0);
            for (int attempt = 0;
                 attempt < 500 && !source.pendingDeckHasReadAhead(); ++attempt)
                juce::Thread::sleep(2);
            const auto eventSamples = static_cast<int>(std::llround(
                event.durationSeconds * source.sampleRate()));
            juce::AudioBuffer<float> rendered(2, eventSamples);
            rendered.clear();
            for (int offset = 0; offset < eventSamples;
                 offset += blockSize) {
                source.getNextAudioBlock({
                    &rendered, offset,
                    std::min(blockSize, eventSamples - offset) });
            }
            const auto expectedLength = static_cast<juce::int64>(
                std::llround(timelineDuration * source.sampleRate()));
            if (source.activeLauncherScene() != 6
                || source.getTotalLength() < expectedLength - 1)
                result = juce::Result::fail(
                    "An empty audio track truncated its launcher timeline");
            if (result.wasOk()) {
                juce::AudioBuffer<float> expected(2, eventSamples);
                expected.clear();
                (void) expectedReader->read(
                    &expected, 0, eventSamples, 0, true, true);
                const auto fadeSamples = std::max(
                    2, static_cast<int>(std::llround(
                           event.fadeInSeconds * source.sampleRate())));
                const auto early = std::min(32, eventSamples - 1);
                const auto late = std::max(0, eventSamples - 33);
                const auto earlyGain = static_cast<float>(early)
                                       / static_cast<float>(fadeSamples - 1);
                const auto lateGain = static_cast<float>(
                    eventSamples - 1 - late)
                                      / static_cast<float>(fadeSamples - 1);
                if (std::abs(rendered.getSample(0, early)
                             - expected.getSample(0, early) * earlyGain)
                            > 0.002f
                    || std::abs(rendered.getSample(0, late)
                                - expected.getSample(0, late) * lateGain)
                           > 0.002f)
                    result = juce::Result::fail(
                        "Launcher timeline ignored its audio event fades");
            }
        }
        source.releaseResources();
    }
    readAheadThread.stopThread(3'000);
    return result;
}

juce::Result AudioTrackController::selfTestMulticlipArrangement(
    const juce::File& sourceFile,
    const juce::File& alternateSourceFile)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto launcherReaderA = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(sourceFile));
    auto launcherReaderB = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(sourceFile));
    auto expectedReader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(sourceFile));
    if (launcherReaderA == nullptr || launcherReaderB == nullptr
        || expectedReader == nullptr || expectedReader->sampleRate <= 0.0)
        return juce::Result::fail("Cannot open multiclip self-test audio");

    constexpr int blockSize = 64;
    const auto sampleRate = expectedReader->sampleRate;
    juce::TimeSliceThread readAheadThread("CyDAW multiclip self-test");
    readAheadThread.startThread(juce::Thread::Priority::normal);
    auto result = juce::Result::ok();
    {
        ClipAudioSource source(std::move(launcherReaderA),
                               std::move(launcherReaderB), readAheadThread);
        std::vector<PreparedArrangementRegion> regions;
        const auto addRegion = [&](const std::uint64_t id,
                                   const double start,
                                   const double offset,
                                   const double duration) {
            regions.push_back({
                { id, "Region " + juce::String(static_cast<int>(id)),
                  start, offset, duration, duration, 0xffff9b2f },
                std::unique_ptr<juce::AudioFormatReader>(
                    formats.createReaderFor(sourceFile)) });
        };
        addRegion(1, 0.0, 0.10, 0.05);
        addRegion(2, 0.08, 0.20, 0.05);
        addRegion(3, 0.10, 0.30, 0.02);
        source.prepareToPlay(blockSize, sampleRate);
        result = source.setArrangementRegions(std::move(regions));
        for (int attempt = 0;
             result.wasOk() && attempt < 500
             && !source.arrangementHasReadAhead(); ++attempt)
            juce::Thread::sleep(2);
        if (result.wasOk() && !source.arrangementHasReadAhead())
            result = juce::Result::fail(
                "Arrangement regions did not prefetch");

        constexpr int renderedSamples = 7'000;
        juce::AudioBuffer<float> rendered(2, renderedSamples);
        rendered.clear();
        if (result.wasOk()) {
            source.setNextReadPosition(0);
            for (int offset = 0; offset < renderedSamples; offset += blockSize) {
                const auto count = std::min(blockSize, renderedSamples - offset);
                source.getNextAudioBlock({ &rendered, offset, count });
            }

            const auto readExpected = [&](const juce::int64 sample) {
                juce::AudioBuffer<float> value(2, 1);
                value.clear();
                (void) expectedReader->read(&value, 0, 1, sample,
                                            true, true);
                return value.getSample(0, 0);
            };
            const auto sampleAt = [sampleRate](const double seconds) {
                return static_cast<int>(std::llround(seconds * sampleRate));
            };
            const auto firstPosition = sampleAt(0.005);
            const auto gapPosition = sampleAt(0.060);
            const auto secondPosition = sampleAt(0.085);
            const auto overlapPosition = sampleAt(0.105);
            const auto firstExpected = readExpected(sampleAt(0.105));
            const auto secondExpected = readExpected(sampleAt(0.205));
            const auto overlapExpected = readExpected(sampleAt(0.225))
                                         + readExpected(sampleAt(0.305));
            if (std::abs(rendered.getSample(0, firstPosition) - firstExpected)
                    > 0.00001f
                || std::abs(rendered.getSample(0, gapPosition)) > 0.00001f
                || std::abs(rendered.getSample(0, secondPosition)
                            - secondExpected) > 0.00001f
                || std::abs(rendered.getSample(0, overlapPosition)
                            - overlapExpected) > 0.00002f
                || std::abs(source.arrangementLengthSeconds() - 0.13)
                       > 1.0 / sampleRate) {
                result = juce::Result::fail(
                    "Multiclip regions, gaps or overlaps rendered incorrectly");
            }
        }

        if (result.wasOk()) {
            if (!source.moveArrangementRegion(2, 0.06)
                || std::abs(source.arrangementLengthSeconds() - 0.12)
                       > 1.0 / sampleRate)
                result = juce::Result::fail(
                    "Moving a region did not update the arrangement atomically");
        }

        if (result.wasOk()) {
            constexpr double fadeDuration = 0.005;
            constexpr double regionDuration = 0.020;
            constexpr double sourceOffset = 0.100;
            std::vector<PreparedArrangementRegion> fadedRegions;
            fadedRegions.push_back({
                { 4, "Faded region", 0.0, sourceOffset, regionDuration,
                  regionDuration, 0xffff9b2f, sourceFile, 0.0,
                  fadeDuration, fadeDuration },
                std::unique_ptr<juce::AudioFormatReader>(
                    formats.createReaderFor(sourceFile)) });
            result = source.setArrangementRegions(std::move(fadedRegions));
            for (int attempt = 0;
                 result.wasOk() && attempt < 500
                 && !source.arrangementHasReadAhead(); ++attempt)
                juce::Thread::sleep(2);
            const auto durationSamples = static_cast<int>(std::llround(
                regionDuration * sampleRate));
            const auto fadeSamples = static_cast<int>(std::llround(
                fadeDuration * sampleRate));
            juce::AudioBuffer<float> faded(2, durationSamples);
            faded.clear();
            if (result.wasOk()) {
                source.setNextReadPosition(0);
                for (int offset = 0; offset < durationSamples;
                     offset += blockSize) {
                    const auto count = std::min(blockSize,
                                                durationSamples - offset);
                    source.getNextAudioBlock({ &faded, offset, count });
                }
                const auto sourceStart = static_cast<juce::int64>(
                    std::llround(sourceOffset * sampleRate));
                juce::AudioBuffer<float> expected(2, durationSamples);
                expected.clear();
                (void) expectedReader->read(&expected, 0, durationSamples,
                                            sourceStart, true, true);
                const auto fadeMid = fadeSamples / 2;
                const auto fadeGain = static_cast<float>(fadeMid)
                                      / static_cast<float>(fadeSamples - 1);
                if (std::abs(faded.getSample(0, 0)) > 0.000001f
                    || std::abs(faded.getSample(0, fadeMid)
                                - expected.getSample(0, fadeMid) * fadeGain)
                           > 0.00002f
                    || std::abs(faded.getSample(0, fadeSamples)
                                - expected.getSample(0, fadeSamples))
                           > 0.00002f
                    || std::abs(faded.getSample(0, durationSamples - 1))
                           > 0.000001f)
                    result = juce::Result::fail(
                        "Arrangement region fades were not sample-accurate");
            }
        }

        if (result.wasOk()) {
            constexpr double regionOffset = 0.100;
            constexpr double regionDuration = 0.050;
            constexpr double loopStart = 0.010;
            constexpr double loopEnd = 0.030;
            std::vector<PreparedArrangementRegion> loopRegions;
            loopRegions.push_back({
                { 5, "Loop region", 0.0, regionOffset, regionDuration,
                  regionDuration, 0xffff9b2f, sourceFile, 0.0 },
                std::unique_ptr<juce::AudioFormatReader>(
                    formats.createReaderFor(sourceFile)) });
            result = source.setArrangementRegions(std::move(loopRegions));
            source.setTransportRange(0.080, loopStart, loopEnd);
            source.setLooping(true);
            for (int attempt = 0;
                 result.wasOk() && attempt < 500
                 && !source.arrangementHasReadAhead(); ++attempt)
                juce::Thread::sleep(2);
            constexpr int loopRenderSamples = 6;
            juce::AudioBuffer<float> looped(2, loopRenderSamples);
            looped.clear();
            const auto loopStartSample = static_cast<juce::int64>(
                std::llround(loopStart * sampleRate));
            const auto loopEndSample = static_cast<juce::int64>(
                std::llround(loopEnd * sampleRate));
            if (result.wasOk()) {
                source.setNextReadPosition(loopEndSample - 2);
                source.getNextAudioBlock(
                    { &looped, 0, loopRenderSamples });
                for (int sample = 0; sample < loopRenderSamples; ++sample) {
                    const auto timelineSample = sample < 2
                                                    ? loopEndSample - 2 + sample
                                                    : loopStartSample + sample - 2;
                    juce::AudioBuffer<float> expected(2, 1);
                    expected.clear();
                    const auto sourceSample = static_cast<juce::int64>(
                        std::llround(regionOffset * sampleRate))
                                              + timelineSample;
                    (void) expectedReader->read(&expected, 0, 1,
                                                sourceSample, true, true);
                    if (std::abs(looped.getSample(0, sample)
                                 - expected.getSample(0, 0)) > 0.00001f) {
                        result = juce::Result::fail(
                            "Arrangement loop range did not wrap sample-accurately");
                        break;
                    }
                }
                if (result.wasOk()
                    && source.getNextReadPosition()
                           != loopStartSample + loopRenderSamples - 2)
                    result = juce::Result::fail(
                        "Arrangement loop range published the wrong playhead position");
            }
            source.setLooping(false);
            source.setTransportRange(0.080, 0.0, 0.080);
        }

        if (result.wasOk() && alternateSourceFile.existsAsFile()) {
            auto alternateExpected = std::unique_ptr<juce::AudioFormatReader>(
                formats.createReaderFor(alternateSourceFile));
            auto alternateReader = std::unique_ptr<juce::AudioFormatReader>(
                formats.createReaderFor(alternateSourceFile));
            if (alternateExpected == nullptr || alternateReader == nullptr
                || alternateExpected->sampleRate <= 0.0) {
                result = juce::Result::fail(
                    "Cannot open alternate multiclip source");
            } else {
                const auto alternateRate = alternateExpected->sampleRate;
                std::vector<PreparedArrangementRegion> alternateRegions;
                alternateRegions.push_back({
                    { 10, "Alternate rate", 0.0, 0.0, 0.05, 0.05,
                      0xff53d0c2, alternateSourceFile, 0.0 },
                    std::move(alternateReader) });
                result = source.setArrangementRegions(
                    std::move(alternateRegions));
                for (int attempt = 0;
                     result.wasOk() && attempt < 500
                     && !source.arrangementHasReadAhead(); ++attempt)
                    juce::Thread::sleep(2);
                juce::AudioBuffer<float> converted(2, blockSize);
                converted.clear();
                if (result.wasOk()) {
                    source.setNextReadPosition(0);
                    source.getNextAudioBlock(
                        { &converted, 0, converted.getNumSamples() });
                    juce::AudioBuffer<float> expected(2, blockSize + 4);
                    expected.clear();
                    (void) alternateExpected->read(
                        &expected, 0, expected.getNumSamples(), 0, true, true);
                    const auto ratio = alternateRate / sampleRate;
                    // The region is read through a windowed sinc
                    // (TimeStretch.h): away from the file's first samples
                    // (where the sinc sees the silence before them), a
                    // 440 Hz tone is where a straight line between its
                    // samples says, to its curvature.
                    for (const auto outputSample : { 30, 41, 52, 63 }) {
                        const auto sourcePosition = outputSample * ratio;
                        const auto sourceIndex = static_cast<int>(
                            std::floor(sourcePosition));
                        const auto fraction = static_cast<float>(
                            sourcePosition - sourceIndex);
                        const auto expectedValue = expected.getSample(
                                                       0, sourceIndex)
                                                   + (expected.getSample(
                                                          0, sourceIndex + 1)
                                                      - expected.getSample(
                                                          0, sourceIndex))
                                                         * fraction;
                        if (std::abs(converted.getSample(0, outputSample)
                                     - expectedValue) > 0.001f) {
                            result = juce::Result::fail(
                                "Alternate sample-rate region was not resampled correctly");
                            break;
                        }
                    }
                }
            }
        }
        source.releaseResources();
    }
    readAheadThread.stopThread(3'000);
    return result;
}

void AudioTrackController::clearTracks()
{
    stop();
    stopAudioClipPreview();
    arrangementEditBatch_.reset();
    arrangementSessionUndoStates_.clear();
    arrangementSessionRedoStates_.clear();
    for (auto& track : tracks_)
        mixerSource_.removeInputSource(&track->latencySource);
    tracks_.clear();
    refreshTransportRanges();
    refreshLatencyCompensation();
}

bool AudioTrackController::removeAudioTrack(const std::size_t index)
{
    if (index >= tracks_.size())
        return false;
    stopAudioClipPreview();
    arrangementEditBatch_.reset();
    arrangementSessionUndoStates_.clear();
    arrangementSessionRedoStates_.clear();
    mixerSource_.removeInputSource(&tracks_[index]->latencySource);
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));
    refreshTransportRanges();
    refreshLatencyCompensation();
    return true;
}

bool AudioTrackController::setMidiClipStartBeats(const std::uint64_t trackId,
                                                 const std::uint64_t clipId,
                                                 const double startBeats)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return false;
    clip->startBeats = std::max(0.0, startBeats);
    clearMidiEditHistory();
    publishMidiSequence(*track);
    return true;
}

bool AudioTrackController::setMidiClipPresentation(const std::uint64_t trackId,
                                                   const std::uint64_t clipId,
                                                   juce::String name,
                                                   const std::uint32_t colour)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return false;
    if (name.trim().isNotEmpty())
        clip->name = name.trim();
    if (colour != 0)
        clip->colour = colour;
    return true;
}

bool AudioTrackController::moveAudioTrack(const std::size_t fromIndex,
                                          const std::size_t toIndex)
{
    if (fromIndex >= tracks_.size() || toIndex >= tracks_.size())
        return false;
    if (fromIndex == toIndex)
        return true;
    auto moving = std::move(tracks_[fromIndex]);
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(fromIndex));
    tracks_.insert(tracks_.begin() + static_cast<std::ptrdiff_t>(toIndex),
                   std::move(moving));
    arrangementEditBatch_.reset();
    arrangementSessionUndoStates_.clear();
    arrangementSessionRedoStates_.clear();
    refreshLatencyCompensation();
    return true;
}

std::size_t AudioTrackController::trackCount() const noexcept
{
    return tracks_.size();
}

AudioTrackController::TrackInfo AudioTrackController::trackInfo(const std::size_t index) const
{
    const auto* track = findTrack(index);
    if (track == nullptr)
        return {};

    TrackInfo result;
    result.file = track->file;
    result.lengthSeconds = track->clipSource->arrangementLengthSeconds();
    result.gainDb = track->gainDb;
    result.pan = track->pan;
    result.muted = track->muted;
    result.solo = track->solo;
    result.armed = track->armed;
    result.inputBusId = track->inputBusId;
    result.clipStartSeconds = track->clipSource->clipStartSeconds();
    result.sourceOffsetSeconds = track->clipSource->sourceOffsetSeconds();
    result.clipDurationSeconds = track->clipSource->clipDurationSeconds();
    result.sourceLengthSeconds = track->clipSource->sourceLengthSeconds();
    result.driveDb = track->driveDb;
    result.filterCutoffHz = track->filterCutoffHz;
    result.latencyCompensationSamples = track->latencySource.delaySamples();
    result.clips = track->audioClips;
    result.plugins.reserve(track->gainPanSource.pluginCount());
    for (std::size_t pluginIndex = 0; pluginIndex < track->gainPanSource.pluginCount(); ++pluginIndex)
        if (const auto* host = track->gainPanSource.pluginHost(pluginIndex))
            result.plugins.push_back({ host->pluginFile(), host->pluginName(), host->isRunning(),
                                       host->missedBlockCount(), host->latencySamples() });
    return result;
}

AudioTrackController::TrackMixState AudioTrackController::trackMixState(const std::size_t index) const
{
    const auto* track = findTrack(index);
    if (track == nullptr)
        return {};
    return { track->gainDb, track->pan, track->muted, track->solo, track->armed, track->inputBusId, track->inputAfterEffects };
}

const std::vector<AudioTrackController::AudioClip>* AudioTrackController::audioClipsView(const std::size_t index) const
{
    const auto* track = findTrack(index);
    return track != nullptr ? &track->audioClips : nullptr;
}

juce::File AudioTrackController::trackFile(const std::size_t index) const
{
    const auto* track = findTrack(index);
    return track != nullptr ? track->file : juce::File {};
}

std::vector<AudioTrackController::MidiTrackSummary> AudioTrackController::midiTrackSummaries() const
{
    std::vector<MidiTrackSummary> result;
    result.reserve(midiTracks_.size());
    for (const auto& track : midiTracks_)
        result.push_back({ track->id, track->name, track->gainDb, track->pan, track->muted, track->solo,
                           track->armed });
    return result;
}

const std::vector<AudioTrackController::MidiClip>* AudioTrackController::midiClipsView(const std::uint64_t trackId) const
{
    for (const auto& track : midiTracks_)
        if (track->id == trackId)
            return &track->clips;
    return nullptr;
}

std::vector<AudioTrackController::TrackInfo::PluginInfo>
AudioTrackController::trackPlugins(const std::size_t index) const
{
    std::vector<TrackInfo::PluginInfo> plugins;
    const auto* track = findTrack(index);
    if (track == nullptr)
        return plugins;
    plugins.reserve(track->gainPanSource.pluginCount());
    for (std::size_t pluginIndex = 0;
         pluginIndex < track->gainPanSource.pluginCount(); ++pluginIndex)
        if (const auto* host = track->gainPanSource.pluginHost(pluginIndex))
            plugins.push_back({ host->pluginFile(), host->pluginName(),
                                host->isRunning(), host->missedBlockCount(),
                                host->latencySamples() });
    return plugins;
}

std::vector<VirtualAudioBus> AudioTrackController::virtualBuses() const
{
    return virtualBusRouter_.buses();
}

std::optional<VirtualAudioBus> AudioTrackController::virtualBus(
    const std::uint64_t busId) const
{
    return virtualBusRouter_.bus(busId);
}

std::uint64_t AudioTrackController::addVirtualBus(
    const VirtualBusDirection direction)
{
    return virtualBusRouter_.addBus(direction);
}

bool AudioTrackController::updateVirtualBus(const VirtualAudioBus& bus)
{
    if (!virtualBusRouter_.updateBus(bus))
        return false;
    if (recorder_.hasSession()) {
        const auto armed = armedAudioTrack();
        if (armed.has_value()
            && tracks_[*armed]->inputBusId == bus.id)
            recorder_.setInputBus(virtualBusRouter_.bus(bus.id),
                                  virtualBusRouter_.graph(bus.id));
    }
    return true;
}

bool AudioTrackController::removeVirtualBus(const std::uint64_t busId)
{
    if (!virtualBusRouter_.removeBus(busId))
        return false;
    for (auto& track : tracks_)
        if (track->inputBusId == busId)
            track->inputBusId = virtualBusRouter_.defaultInputBusId();
    if (recorder_.hasSession()) {
        const auto armed = armedAudioTrack();
        if (armed.has_value())
            recorder_.setInputBus(
                virtualBusRouter_.bus(tracks_[*armed]->inputBusId),
                virtualBusRouter_.graph(tracks_[*armed]->inputBusId));
    }
    return true;
}

void AudioTrackController::replaceVirtualBuses(
    std::vector<VirtualAudioBus> buses)
{
    virtualBusRouter_.replaceBuses(std::move(buses));
    for (auto& track : tracks_) {
        const auto bus = virtualBusRouter_.bus(track->inputBusId);
        if (!bus.has_value()
            || bus->direction != VirtualBusDirection::input)
            track->inputBusId = virtualBusRouter_.defaultInputBusId();
    }
    if (recorder_.hasSession()) {
        const auto armed = armedAudioTrack();
        if (armed.has_value())
            recorder_.setInputBus(
                virtualBusRouter_.bus(tracks_[*armed]->inputBusId),
                virtualBusRouter_.graph(tracks_[*armed]->inputBusId));
    }
}

void AudioTrackController::resetVirtualBuses()
{
    virtualBusRouter_.resetToDefaults();
    for (auto& track : tracks_)
        track->inputBusId = virtualBusRouter_.defaultInputBusId();
    if (recorder_.hasSession())
        recorder_.setInputBus(
            virtualBusRouter_.bus(virtualBusRouter_.defaultInputBusId()),
            virtualBusRouter_.graph(virtualBusRouter_.defaultInputBusId()));
}

std::uint64_t AudioTrackController::defaultInputBusId() const noexcept
{
    return virtualBusRouter_.defaultInputBusId();
}

std::array<float, 2> AudioTrackController::virtualBusMeterLevels(
    const std::uint64_t busId) const noexcept
{
    return virtualBusRouter_.meterLevels(busId);
}

juce::Result AudioTrackController::setVirtualBusGraph(
    const std::uint64_t busId,
    const cydaw::engine::NativeDspGraph& graph)
{
    const auto result = virtualBusRouter_.setGraph(busId, graph);
    if (result.wasOk() && recorder_.hasSession()) {
        const auto armed = armedAudioTrack();
        if (armed.has_value() && tracks_[*armed]->inputBusId == busId)
            recorder_.setInputBus(virtualBusRouter_.bus(busId), graph);
    }
    return result;
}

juce::Result AudioTrackController::updateVirtualBusGraphParameters(
    const std::uint64_t busId,
    const cydaw::engine::NativeDspGraph& graph)
{
    return virtualBusRouter_.updateGraphParameters(busId, graph);
}

cydaw::engine::NativeDspGraph AudioTrackController::virtualBusGraph(
    const std::uint64_t busId) const
{
    return virtualBusRouter_.graph(busId);
}

std::vector<std::pair<std::uint64_t, float>>
AudioTrackController::virtualBusControlValues(const std::uint64_t busId) const
{
    return virtualBusRouter_.controlValues(busId);
}

juce::StringArray AudioTrackController::physicalInputChannelNames() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getInputChannelNames();
    return {};
}

juce::StringArray AudioTrackController::physicalOutputChannelNames() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getOutputChannelNames();
    return {};
}

AudioTrackController::DeviceChannels AudioTrackController::deviceChannels() const
{
    DeviceChannels channels;
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr)
        return channels;
    // A bus counts the channels open, in order: those are the names it sees.
    const auto names = [](const juce::StringArray& all, const juce::BigInteger& active) {
        juce::StringArray open;
        for (auto index = 0; index < all.size(); ++index)
            if (active[index])
                open.add(all[index]);
        return open;
    };
    channels.inputNames = names(device->getInputChannelNames(), device->getActiveInputChannels());
    channels.outputNames = names(device->getOutputChannelNames(), device->getActiveOutputChannels());
    channels.activeInputs = channels.inputNames.size();
    channels.activeOutputs = channels.outputNames.size();
    channels.availableInputs = device->getInputChannelNames().size();
    channels.availableOutputs = device->getOutputChannelNames().size();
    return channels;
}

juce::Result AudioTrackController::setDeviceChannelCounts(const int inputs, const int outputs)
{
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr)
        return juce::Result::fail(tr("error.recordInput"));
    auto setup = deviceManager_.getAudioDeviceSetup();
    const auto inputCount = juce::jlimit(0, device->getInputChannelNames().size(), inputs);
    const auto outputCount = juce::jlimit(1, std::max(1, device->getOutputChannelNames().size()), outputs);
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    if (inputCount > 0)
        setup.inputChannels.setRange(0, inputCount, true);
    setup.outputChannels.setRange(0, outputCount, true);
    if (const auto error = deviceManager_.setAudioDeviceSetup(setup, true); error.isNotEmpty())
        return juce::Result::fail(error);
    return juce::Result::ok();
}

int AudioTrackController::trackChannelCount(const std::size_t index) const noexcept
{
    const auto* track = findTrack(index);
    return track != nullptr ? track->channelCount : 2;
}

juce::Result AudioTrackController::setTrackChannelCount(const std::size_t index, const int channels)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    const auto count = cydaw::engine::clampTrackChannels(channels);
    if (track->channelCount == count)
        return juce::Result::ok();
    std::unique_ptr<juce::AudioFormatReader> first;
    std::unique_ptr<juce::AudioFormatReader> second;
    if (track->file.existsAsFile()) {
        first.reset(formatManager_.createReaderFor(track->file));
        second.reset(formatManager_.createReaderFor(track->file));
    }
    if (first == nullptr || second == nullptr) {
        first = std::make_unique<EmptyAudioFormatReader>();
        second = std::make_unique<EmptyAudioFormatReader>();
    }
    // Out of the mixer while it changes: its chain is prepared again, in the
    // new format, as it goes back in; its clips and scenes are read again
    // into decks of that width.
    mixerSource_.removeInputSource(&track->latencySource);
    track->channelCount = count;
    track->gainPanSource.setChannelCount(count);
    track->latencySource.setChannelCount(count);
    track->replaceSource(std::move(first), std::move(second), track->file, readAheadThread_);
    track->clipSource->setArrangementPlaybackEnabled(arrangementPlaybackEnabled_.load(std::memory_order_acquire));
    auto result = rebuildArrangementPlan(*track);
    if (result.wasOk())
        result = rebuildLauncherPlans(*track);
    mixerSource_.addInputSource(&track->latencySource, false);
    refreshTransportRanges();
    (void) rebuildRouting();
    refreshLatencyCompensation();
    return result;
}

void AudioTrackController::setTrackInputAfterEffects(const std::size_t trackIndex, const bool afterEffects)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return;
    track->inputAfterEffects = afterEffects;
    if (recorder_.hasSession() && track->armed)
        recorder_.setAfterEffects(afterEffects);
}

void AudioTrackController::setVirtualBusTaps(const std::uint64_t busId,
                                             const std::vector<NativeDspRuntime::TapRequest>& taps)
{
    virtualBusRouter_.setTaps(busId, taps);
}

bool AudioTrackController::readVirtualBusTap(const std::uint64_t busId, const std::uint64_t nodeId, const bool input,
                                             cydaw::engine::AudioAnalysisFrame& frame) const
{
    return virtualBusRouter_.readTap(busId, nodeId, input, frame);
}

bool AudioTrackController::setTrackInputBus(const std::size_t trackIndex,
                                             const std::uint64_t busId)
{
    auto* track = findTrack(trackIndex);
    const auto bus = virtualBusRouter_.bus(busId);
    if (track == nullptr || !bus.has_value()
        || bus->direction != VirtualBusDirection::input)
        return false;
    track->inputBusId = busId;
    if (recorder_.hasSession() && track->armed)
        recorder_.setInputBus(bus, virtualBusRouter_.graph(busId));
    return true;
}

void AudioTrackController::setTrackMixer(const std::size_t index,
                                         const float gainDb,
                                         const float pan,
                                         const bool muted)
{
    if (auto* track = findTrack(index)) {
        track->gainDb = juce::jlimit(-60.0f, 12.0f, gainDb);
        track->pan = juce::jlimit(-1.0f, 1.0f, pan);
        track->muted = muted;
        track->gainPanSource.setPan(track->pan);
        refreshTrackGains();
    }
}

void AudioTrackController::setMasterGain(const float gainDb) noexcept
{
    const auto value = juce::jlimit(-60.0f, 12.0f, gainDb);
    masterGainDb_.store(value, std::memory_order_release);
    masterOutput_.setGain(masterMuted_.load(std::memory_order_acquire) ? 0.0f : juce::Decibels::decibelsToGain(value));
}

void AudioTrackController::setMasterMuted(const bool muted) noexcept
{
    masterMuted_.store(muted, std::memory_order_release);
    setMasterGain(masterGainDb_.load(std::memory_order_acquire));
}

bool AudioTrackController::isMasterMuted() const noexcept
{
    return masterMuted_.load(std::memory_order_acquire);
}

juce::Result AudioTrackController::setMasterNativeDspGraph(const cydaw::engine::NativeDspGraph& graph)
{
    return masterOutput_.setChainGraph(graph);
}

juce::Result AudioTrackController::updateMasterNativeDspParameters(const cydaw::engine::NativeDspGraph& graph)
{
    return masterOutput_.updateChainParameters(graph);
}

void AudioTrackController::masterNodePeaks(const std::vector<std::uint64_t>& nodes, std::vector<float>& left,
                                           std::vector<float>& right) const
{
    left.assign(nodes.size(), 0.0f);
    right.assign(nodes.size(), 0.0f);
    masterOutput_.chain().takeNodePeaks(nodes.data(), nodes.size(), left.data(), right.data());
}

void AudioTrackController::trackNodePeaks(const std::size_t trackIndex, const std::vector<std::uint64_t>& nodes,
                                          std::vector<float>& left, std::vector<float>& right) const
{
    left.assign(nodes.size(), 0.0f);
    right.assign(nodes.size(), 0.0f);
    if (const auto* track = findTrack(trackIndex))
        track->gainPanSource.takeNodePeaks(nodes.data(), nodes.size(), left.data(), right.data());
}

void AudioTrackController::trackPluginPeaks(const std::size_t trackIndex, std::vector<float>& left, std::vector<float>& right)
{
    left.clear();
    right.clear();
    if (auto* track = findTrack(trackIndex))
        track->gainPanSource.takePluginPeaks(left, right);
}

void AudioTrackController::midiTrackNodePeaks(const std::uint64_t trackId, const std::vector<std::uint64_t>& nodes,
                                              std::vector<float>& left, std::vector<float>& right) const
{
    left.assign(nodes.size(), 0.0f);
    right.assign(nodes.size(), 0.0f);
    if (const auto* track = findMidiTrack(trackId))
        track->synth.effects.takeNodePeaks(nodes.data(), nodes.size(), left.data(), right.data());
}

namespace {
constexpr std::size_t kMostReadings = 1'024;
}

void AudioTrackController::masterNodeReadings(const std::uint64_t node, std::vector<float>& into) const
{
    into.resize(kMostReadings);
    into.resize(masterOutput_.chain().nodeReadings(node, into.data(), into.size()));
}

void AudioTrackController::trackNodeReadings(const std::size_t trackIndex, const std::uint64_t node, std::vector<float>& into) const
{
    into.resize(kMostReadings);
    const auto* track = findTrack(trackIndex);
    into.resize(track != nullptr ? track->gainPanSource.nodeReadings(node, into.data(), into.size()) : 0);
}

void AudioTrackController::midiTrackNodeReadings(const std::uint64_t trackId, const std::uint64_t node, std::vector<float>& into) const
{
    into.resize(kMostReadings);
    const auto* track = findMidiTrack(trackId);
    into.resize(track != nullptr ? track->synth.effects.nodeReadings(node, into.data(), into.size()) : 0);
}

void AudioTrackController::setMasterNativeAutomation(std::vector<NativeDspRuntime::AutomationLane> lanes)
{
    masterOutput_.chain().setAutomation(std::move(lanes));
}

std::vector<std::pair<std::uint64_t, float>> AudioTrackController::masterControlValues() const
{
    return masterOutput_.chain().controlValues();
}

void AudioTrackController::setMasterNodeTaps(const std::vector<NativeDspRuntime::TapRequest>& taps)
{
    masterOutput_.chain().setTaps(taps);
}

bool AudioTrackController::readMasterNodeTap(const std::uint64_t nodeId, const bool input,
                                             cydaw::engine::AudioAnalysisFrame& frame) const
{
    return masterOutput_.chain().readTap(nodeId, input, frame);
}

void AudioTrackController::silenceMasterOutput() noexcept
{
    masterOutput_.setGain(0.0f);
}

float AudioTrackController::masterGainDb() const noexcept
{
    return masterGainDb_.load(std::memory_order_acquire);
}

void AudioTrackController::setTrackSolo(const std::size_t index, const bool solo)
{
    if (auto* track = findTrack(index)) {
        track->solo = solo;
        refreshTrackGains();
    }
}

void AudioTrackController::setTrackArmed(const std::size_t index,
                                         const bool armed)
{
    if (index >= tracks_.size())
        return;
    for (std::size_t trackIndex = 0; trackIndex < tracks_.size(); ++trackIndex)
        tracks_[trackIndex]->armed = armed && trackIndex == index;
}

std::optional<std::size_t> AudioTrackController::armedAudioTrack() const noexcept
{
    for (std::size_t index = 0; index < tracks_.size(); ++index)
        if (tracks_[index]->armed)
            return index;
    return std::nullopt;
}

void AudioTrackController::setTrackClip(const std::size_t index,
                                        const double clipStartSeconds,
                                        const double sourceOffsetSeconds,
                                        const double clipDurationSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return;
    if (track->audioClips.empty()) {
        (void) addAudioClip(index, clipStartSeconds, sourceOffsetSeconds,
                            clipDurationSeconds,
                            track->file.getFileNameWithoutExtension());
        return;
    }
    (void) updateAudioClip(index, track->audioClips.front().id,
                           clipStartSeconds, sourceOffsetSeconds,
                           clipDurationSeconds);
}

std::vector<AudioTrackController::AudioClip>
AudioTrackController::audioClips(const std::size_t index) const
{
    const auto* track = findTrack(index);
    auto clips = track != nullptr ? track->audioClips
                                  : std::vector<AudioClip> {};
    for (auto& clip : clips)
        normaliseAudioClipTimeline(clip);
    return clips;
}

std::optional<AudioTrackController::AudioClip> AudioTrackController::audioClip(
    const std::size_t index, const std::uint64_t clipId) const
{
    const auto* track = findTrack(index);
    if (track == nullptr)
        return std::nullopt;
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return std::nullopt;
    auto result = *found;
    normaliseAudioClipTimeline(result);
    return result;
}

std::vector<AudioTrackController::AudioEvent>
AudioTrackController::audioClipEvents(
    const std::size_t index, const std::uint64_t clipId) const
{
    const auto clip = audioClip(index, clipId);
    if (!clip.has_value())
        return {};
    if (clip->usesEventTimeline || !clip->events.empty())
        return clip->events;
    if (clip->sourceFile == juce::File {})
        return {};
    return { AudioEvent {
        clip->id, clip->name, 0.0, clip->sourceOffsetSeconds,
        clip->durationSeconds, clip->sourceDurationSeconds, clip->colour,
        clip->sourceFile, clip->sourceLengthSeconds, clip->fadeInSeconds,
        clip->fadeOutSeconds, clip->sourceTempoBpm, clip->stretchMode,
        clip->stretchMarkers } };
}

std::uint64_t AudioTrackController::addAudioClip(
    const std::size_t index,
    const double startSeconds,
    const double sourceOffsetSeconds,
    const double durationSeconds,
    juce::String name,
    const std::uint32_t colour,
    juce::File sourceFile,
    const double fadeInSeconds,
    const double fadeOutSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr
        || track->audioClips.size() >= maximumAudioClipsPerTrack)
        return 0;
    auto previous = track->audioClips;
    const auto id = nextAudioClipId_++;
    const auto emptyContainer = sourceFile == juce::File {}
                                && track->file == juce::File {};
    if (sourceFile == juce::File {})
        sourceFile = track->file;
    track->audioClips.push_back({ id, std::move(name), startSeconds,
                                  sourceOffsetSeconds, durationSeconds,
                                  durationSeconds,
                                  colour, std::move(sourceFile), 0.0,
                                  fadeInSeconds, fadeOutSeconds });
    auto& added = track->audioClips.back();
    added.playEndSeconds = added.durationSeconds;
    added.loopEndSeconds = added.durationSeconds;
    added.usesEventTimeline = emptyContainer;
    normaliseAudioClipTimeline(added);
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips.pop_back();
        return 0;
    }
    recordArrangementEdit(*track, std::move(previous));
    return id;
}

juce::Result AudioTrackController::addAudioClipFromFile(
    const std::size_t index,
    const juce::File& sourceFile,
    const double startSeconds,
    std::uint64_t* const newClipId)
{
    if (findTrack(index) == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(sourceFile));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0)
        return juce::Result::fail(tr("error.openAudio", "file",
                                     sourceFile.getFileName()));
    const auto duration = static_cast<double>(reader->lengthInSamples)
                          / reader->sampleRate;
    const auto id = addAudioClip(
        index, startSeconds, 0.0, duration,
        sourceFile.getFileNameWithoutExtension(), 0xff53d0c2, sourceFile);
    if (id == 0)
        return juce::Result::fail("Could not add the audio region to this track");
    if (newClipId != nullptr)
        *newClipId = id;
    return juce::Result::ok();
}

juce::Result AudioTrackController::addAudioTake(
    const std::size_t index,
    const std::uint64_t clipId,
    const juce::File& sourceFile,
    const bool activate,
    const bool fitClipToTake)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.recordedTakeTrack"));
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return juce::Result::fail(tr("error.audioTakeClip"));
    if (found->takes.size() >= maximumAudioTakesPerClip)
        return juce::Result::fail(
            tr("error.audioTakeLimit", "count",
               juce::String(static_cast<int>(maximumAudioTakesPerClip))));

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(sourceFile));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0)
        return juce::Result::fail(
            tr("error.openAudio", "file", sourceFile.getFileName()));
    const auto sourceLength = static_cast<double>(reader->lengthInSamples)
                              / reader->sampleRate;

    auto previous = track->audioClips;
    auto clipBefore = *found;
    auto takeName = sourceFile.getFileNameWithoutExtension();
    if (takeName.isEmpty())
        takeName = tr("audioTake.defaultName", "count",
                      juce::String(static_cast<int>(found->takes.size() + 1)));
    found->takes.push_back({ takeName, sourceFile, sourceLength });
    if (activate) {
        found->activeTakeIndex = found->takes.size() - 1;
        found->sourceFile = sourceFile;
        found->sourceLengthSeconds = sourceLength;
        if (fitClipToTake) {
            found->sourceOffsetSeconds = 0.0;
            found->durationSeconds = sourceLength;
            found->sourceDurationSeconds = sourceLength;
        }
    }
    if (const auto result = rebuildArrangementPlan(*track); result.failed()) {
        *found = std::move(clipBefore);
        (void) rebuildArrangementPlan(*track);
        return result;
    }
    recordArrangementEdit(*track, std::move(previous));
    return juce::Result::ok();
}

juce::Result AudioTrackController::replaceAudioTakes(
    const std::size_t index,
    const std::uint64_t clipId,
    std::vector<AudioTake> takes,
    const std::size_t activeTakeIndex)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.recordedTakeTrack"));
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return juce::Result::fail(tr("error.audioTakeClip"));
    if (takes.empty() || takes.size() > maximumAudioTakesPerClip
        || activeTakeIndex >= takes.size())
        return juce::Result::fail(tr("error.audioTakeState"));

    for (auto& take : takes) {
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager_.createReaderFor(take.sourceFile));
        if (reader == nullptr || reader->sampleRate <= 0.0
            || reader->lengthInSamples <= 0)
            return juce::Result::fail(
                tr("error.openAudio", "file",
                   take.sourceFile.getFileName()));
        take.sourceLengthSeconds = static_cast<double>(reader->lengthInSamples)
                                   / reader->sampleRate;
        if (take.name.isEmpty())
            take.name = take.sourceFile.getFileNameWithoutExtension();
    }

    auto previous = track->audioClips;
    const auto clipBefore = *found;
    found->takes = std::move(takes);
    found->activeTakeIndex = activeTakeIndex;
    found->sourceFile = found->takes[activeTakeIndex].sourceFile;
    found->sourceLengthSeconds =
        found->takes[activeTakeIndex].sourceLengthSeconds;
    if (const auto result = rebuildArrangementPlan(*track); result.failed()) {
        *found = clipBefore;
        (void) rebuildArrangementPlan(*track);
        return result;
    }
    recordArrangementEdit(*track, std::move(previous));
    return juce::Result::ok();
}

bool AudioTrackController::setActiveAudioTake(
    const std::size_t index,
    const std::uint64_t clipId,
    const std::size_t takeIndex)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end() || takeIndex >= found->takes.size())
        return false;
    if (found->activeTakeIndex == takeIndex)
        return true;

    auto previous = track->audioClips;
    const auto clipBefore = *found;
    found->activeTakeIndex = takeIndex;
    found->sourceFile = found->takes[takeIndex].sourceFile;
    found->sourceLengthSeconds = found->takes[takeIndex].sourceLengthSeconds;
    if (rebuildArrangementPlan(*track).failed()) {
        *found = clipBefore;
        (void) rebuildArrangementPlan(*track);
        return false;
    }
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

bool AudioTrackController::removeAudioTake(
    const std::size_t index,
    const std::uint64_t clipId,
    const std::size_t takeIndex)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end() || found->takes.size() <= 1
        || takeIndex >= found->takes.size())
        return false;

    auto previous = track->audioClips;
    const auto clipBefore = *found;
    found->takes.erase(found->takes.begin()
                       + static_cast<std::ptrdiff_t>(takeIndex));
    if (takeIndex < found->activeTakeIndex)
        --found->activeTakeIndex;
    else if (takeIndex == found->activeTakeIndex)
        found->activeTakeIndex = std::min(takeIndex, found->takes.size() - 1);
    found->sourceFile = found->takes[found->activeTakeIndex].sourceFile;
    found->sourceLengthSeconds =
        found->takes[found->activeTakeIndex].sourceLengthSeconds;
    if (rebuildArrangementPlan(*track).failed()) {
        *found = clipBefore;
        (void) rebuildArrangementPlan(*track);
        return false;
    }
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

juce::Result AudioTrackController::selfTestAudioTakeRendering(
    const juce::File& firstTake,
    const juce::File& secondTake)
{
    AudioTrackController session;
    std::size_t trackIndex {};
    if (const auto result = session.addAudioTrack(firstTake, &trackIndex);
        result.failed())
        return result;
    const auto clips = session.audioClips(trackIndex);
    if (clips.size() != 1)
        return juce::Result::fail("Audio take self-test did not create a clip");
    const auto clipId = clips.front().id;
    if (const auto result = session.addAudioTake(
            trackIndex, clipId, secondTake, true, true);
        result.failed())
        return result;
    auto* track = session.findTrack(trackIndex);
    if (track == nullptr || track->clipSource == nullptr)
        return juce::Result::fail("Audio take self-test track is unavailable");

    constexpr double outputSampleRate = 48'000.0;
    constexpr int blockSize = 256;
    constexpr int renderedSamples = 24'000;
    track->clipSource->prepareToPlay(blockSize, outputSampleRate);
    const auto waitForReadAhead = [track] {
        for (int attempt = 0;
             attempt < 1'000
             && !track->clipSource->arrangementHasReadAhead(); ++attempt)
            juce::Thread::sleep(2);
        return track->clipSource->arrangementHasReadAhead();
    };
    const auto renderCorrelation = [track, &waitForReadAhead](
                                       const double frequency) {
        if (!waitForReadAhead())
            return 0.0;
        juce::AudioBuffer<float> rendered(2, renderedSamples);
        rendered.clear();
        track->clipSource->setNextReadPosition(0);
        for (int offset = 0; offset < renderedSamples; offset += blockSize) {
            const auto count = std::min(blockSize, renderedSamples - offset);
            track->clipSource->getNextAudioBlock({ &rendered, offset, count });
        }
        auto correlation = 0.0;
        for (int sample = 0; sample < renderedSamples; ++sample)
            correlation += static_cast<double>(rendered.getSample(0, sample))
                           * std::sin(
                               juce::MathConstants<double>::twoPi
                               * frequency * static_cast<double>(sample)
                               / outputSampleRate);
        return std::abs(correlation);
    };

    const auto secondAt440 = renderCorrelation(440.0);
    const auto secondAt220 = renderCorrelation(220.0);
    if (!(secondAt440 > secondAt220 * 3.0)) {
        track->clipSource->releaseResources();
        return juce::Result::fail(
            "The active alternate take was not rendered");
    }
    if (!session.setActiveAudioTake(trackIndex, clipId, 0)) {
        track->clipSource->releaseResources();
        return juce::Result::fail("The original take could not be activated");
    }
    const auto firstAt220 = renderCorrelation(220.0);
    const auto firstAt440 = renderCorrelation(440.0);
    track->clipSource->releaseResources();
    if (!(firstAt220 > firstAt440 * 3.0))
        return juce::Result::fail(
            "Switching back did not update the rendered take");
    return juce::Result::ok();
}

juce::Result AudioTrackController::selfTestStretchRendering(
    const juce::File& sourceFile)
{
    constexpr double outputSampleRate = 48'000.0;
    constexpr int sampleCount = 8'192;
    const auto renderMode = [&](const StretchMode mode,
                                juce::AudioBuffer<float>& destination) {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formats.createReaderFor(sourceFile));
        if (reader == nullptr)
            return false;
        SegmentReaderSource source(std::move(reader));
        source.configureSeconds(0.0, 1.0, 2.0, false, mode);
        source.prepareToPlay(sampleCount, outputSampleRate);
        source.setNextReadPosition(static_cast<juce::int64>(
            outputSampleRate * 0.35));
        destination.setSize(2, sampleCount);
        destination.clear();
        juce::AudioSourceChannelInfo output(&destination, 0, sampleCount);
        source.getNextAudioBlock(output);
        source.releaseResources();
        return destination.getMagnitude(0, 0, sampleCount) > 0.001f;
    };
    const auto correlation = [](const juce::AudioBuffer<float>& buffer,
                                const double frequency) {
        auto result = 0.0;
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            result += static_cast<double>(buffer.getSample(0, sample))
                      * std::sin(juce::MathConstants<double>::twoPi
                                 * frequency * static_cast<double>(sample)
                                 / outputSampleRate);
        return std::abs(result);
    };

    juce::AudioBuffer<float> repitch;
    juce::AudioBuffer<float> tonal;
    juce::AudioBuffer<float> beats;
    juce::AudioBuffer<float> texture;
    if (!renderMode(StretchMode::repitch, repitch)
        || !renderMode(StretchMode::tonal, tonal)
        || !renderMode(StretchMode::beats, beats)
        || !renderMode(StretchMode::texture, texture))
        return juce::Result::fail("A stretch mode rendered silence");
    if (!(correlation(repitch, 110.0) > correlation(repitch, 220.0) * 2.0))
        return juce::Result::fail("Repitch did not follow the playback rate");
    if (!(correlation(tonal, 220.0) > correlation(tonal, 110.0) * 1.4))
        return juce::Result::fail("Tonal stretch did not preserve pitch");
    return juce::Result::ok();
}

bool AudioTrackController::updateAudioClip(
    const std::size_t index,
    const std::uint64_t clipId,
    const double startSeconds,
    const double sourceOffsetSeconds,
    const double durationSeconds,
    const double fadeInSeconds,
    const double fadeOutSeconds,
    const double sourceDurationSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return false;
    auto previousClips = track->audioClips;
    const auto previous = *found;
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (!found->events.empty()) {
        const auto safeDuration = juce::jlimit(0.001, 86'400.0,
                                              durationSeconds);
        const auto startDelta = startSeconds - found->startSeconds;
        const auto isWholeClipStretch
            = std::abs(startDelta) < 0.000001
              && sourceDurationSeconds >= 0.0
              && std::abs(sourceDurationSeconds
                          - found->sourceDurationSeconds) < 0.000001
              && std::abs(safeDuration - found->durationSeconds)
                     > 0.000001;
        if (isWholeClipStretch) {
            const auto factor = safeDuration
                                / std::max(0.001,
                                           found->durationSeconds);
            for (auto& event : found->events) {
                event.startSeconds *= factor;
                event.durationSeconds *= factor;
                for (auto& marker : event.stretchMarkers)
                    marker.timelineSeconds *= factor;
            }
        } else if (startDelta > 0.000001) {
            std::vector<AudioEvent> cropped;
            for (auto event : found->events) {
                const auto eventEnd = event.startSeconds
                                      + event.durationSeconds;
                if (eventEnd <= startDelta + 0.001)
                    continue;
                if (event.startSeconds < startDelta) {
                    const auto cut = startDelta - event.startSeconds;
                    AudioClip mapping;
                    mapping.durationSeconds = event.durationSeconds;
                    mapping.sourceOffsetSeconds
                        = event.sourceOffsetSeconds;
                    mapping.sourceDurationSeconds
                        = event.sourceDurationSeconds;
                    mapping.stretchMarkers = event.stretchMarkers;
                    const auto source = sourceTimeForClipTimeline(
                        mapping, cut);
                    event.sourceDurationSeconds
                        -= source - event.sourceOffsetSeconds;
                    event.sourceOffsetSeconds = source;
                    event.durationSeconds -= cut;
                    event.stretchMarkers = stretchMarkersForSlice(
                        mapping, cut, event.durationSeconds);
                    event.startSeconds = 0.0;
                    event.fadeInSeconds = 0.0;
                } else {
                    event.startSeconds -= startDelta;
                }
                cropped.push_back(std::move(event));
            }
            found->events = std::move(cropped);
        } else if (startDelta < -0.000001) {
            for (auto& event : found->events)
                event.startSeconds -= startDelta;
        }
        for (auto& event : found->events) {
            if (event.startSeconds >= safeDuration - 0.001) {
                event.durationSeconds = 0.0;
                continue;
            }
            const auto maximum = safeDuration - event.startSeconds;
            if (event.durationSeconds <= maximum)
                continue;
            AudioClip mapping;
            mapping.durationSeconds = event.durationSeconds;
            mapping.sourceOffsetSeconds = event.sourceOffsetSeconds;
            mapping.sourceDurationSeconds = event.sourceDurationSeconds;
            mapping.stretchMarkers = event.stretchMarkers;
            const auto sourceEnd = sourceTimeForClipTimeline(
                mapping, maximum);
            event.durationSeconds = maximum;
            event.sourceDurationSeconds = sourceEnd
                                          - event.sourceOffsetSeconds;
            event.stretchMarkers = stretchMarkersForSlice(
                mapping, 0.0, maximum);
            event.fadeOutSeconds = 0.0;
        }
        std::erase_if(found->events, [](const auto& event) {
            return event.durationSeconds < 0.001;
        });
        if (found->events.empty()) {
            *found = previous;
            if (wasPlaying)
                track->transportSource.start();
            return false;
        }
    }
    found->startSeconds = startSeconds;
    found->sourceOffsetSeconds = sourceOffsetSeconds;
    found->durationSeconds = durationSeconds;
    found->sourceDurationSeconds = sourceDurationSeconds >= 0.0
                                       ? sourceDurationSeconds
                                       : durationSeconds;
    std::erase_if(
        found->stretchMarkers,
        [found](const auto& marker) {
            return marker.sourceSeconds
                           <= found->sourceOffsetSeconds + 0.0005
                   || marker.sourceSeconds
                          >= found->sourceOffsetSeconds
                                 + found->sourceDurationSeconds - 0.0005
                   || marker.timelineSeconds <= 0.0005
                   || marker.timelineSeconds
                          >= found->durationSeconds - 0.0005;
        });
    if (fadeInSeconds >= 0.0)
        found->fadeInSeconds = fadeInSeconds;
    if (fadeOutSeconds >= 0.0)
        found->fadeOutSeconds = fadeOutSeconds;
    if (rebuildArrangementPlan(*track).failed()) {
        *found = previous;
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    const auto currentPosition = track->transportSource.getCurrentPosition();
    const auto trackLength = track->clipSource->arrangementLengthSeconds();
    const auto sessionLength = transportLength();
    track->transportSource.setPosition(
        juce::jlimit(0.0, sessionLength, currentPosition));
    if (wasPlaying && currentPosition < sessionLength)
        track->transportSource.start();
    for (auto& point : track->automation)
        point.timeSeconds = std::min(point.timeSeconds, trackLength);
    for (auto& lane : track->pluginAutomation)
        for (auto& point : lane.points)
            point.timeSeconds = std::min(point.timeSeconds, trackLength);
    publishTrackAutomation(*track);
    publishTrackPluginAutomation(*track);
    recordArrangementEdit(*track, std::move(previousClips));
    return true;
}

bool AudioTrackController::stretchAudioClip(
    const std::size_t index, const std::uint64_t clipId,
    const double timelineDurationSeconds)
{
    const auto clip = audioClip(index, clipId);
    if (!clip.has_value())
        return false;
    return updateAudioClip(index, clipId, clip->startSeconds,
                           clip->sourceOffsetSeconds,
                           timelineDurationSeconds,
                           clip->fadeInSeconds,
                           clip->fadeOutSeconds,
                           clip->sourceDurationSeconds);
}

bool AudioTrackController::setAudioClipStretchSettings(
    const std::size_t index, const std::uint64_t clipId,
    const double sourceTempoBpm, const StretchMode mode)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return false;
    const auto safeTempo = sourceTempoBpm > 0.0
                               ? juce::jlimit(30.0, 400.0, sourceTempoBpm)
                               : 0.0;
    if (std::abs(found->sourceTempoBpm - safeTempo) < 0.0000001
        && found->stretchMode == mode)
        return true;
    auto previousClips = track->audioClips;
    const auto previous = *found;
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    found->sourceTempoBpm = safeTempo;
    found->stretchMode = mode;
    if (rebuildArrangementPlan(*track).failed()) {
        *found = previous;
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previousClips));
    return true;
}

std::uint64_t AudioTrackController::addAudioClipStretchMarker(
    const std::size_t index, const std::uint64_t clipId,
    const double sourceSeconds, const double timelineSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return 0;
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return 0;
    const auto sourceMinimum = found->sourceOffsetSeconds + 0.001;
    const auto sourceMaximum = found->sourceOffsetSeconds
                               + found->sourceDurationSeconds - 0.001;
    if (sourceMaximum <= sourceMinimum)
        return 0;
    const auto safeSource = juce::jlimit(sourceMinimum, sourceMaximum,
                                         sourceSeconds);
    const auto globalRate = found->durationSeconds
                            / std::max(0.001,
                                       found->sourceDurationSeconds);
    auto safeTimeline = timelineSeconds >= 0.0
                            ? timelineSeconds
                            : (safeSource - found->sourceOffsetSeconds)
                                  * globalRate;
    safeTimeline = juce::jlimit(0.001,
                                found->durationSeconds - 0.001,
                                safeTimeline);

    auto previous = track->audioClips;
    const auto markerId = nextStretchMarkerId_++;
    found->stretchMarkers.push_back(
        { markerId, safeSource, safeTimeline });
    std::sort(found->stretchMarkers.begin(), found->stretchMarkers.end(),
              [](const auto& left, const auto& right) {
                  return left.sourceSeconds < right.sourceSeconds;
              });
    auto lastTimeline = 0.0;
    for (const auto& marker : found->stretchMarkers) {
        if (marker.timelineSeconds <= lastTimeline + 0.0005) {
            track->audioClips = std::move(previous);
            return 0;
        }
        lastTimeline = marker.timelineSeconds;
    }
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return 0;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return markerId;
}

bool AudioTrackController::updateAudioClipStretchMarker(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t markerId, const double timelineSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return false;
    const auto marker = std::find_if(
        clip->stretchMarkers.begin(), clip->stretchMarkers.end(),
        [markerId](const auto& value) { return value.id == markerId; });
    if (marker == clip->stretchMarkers.end())
        return false;
    const auto position = static_cast<std::size_t>(
        std::distance(clip->stretchMarkers.begin(), marker));
    const auto minimum = position == 0
                             ? 0.001
                             : clip->stretchMarkers[position - 1]
                                       .timelineSeconds
                                   + 0.001;
    const auto maximum = position + 1 == clip->stretchMarkers.size()
                             ? clip->durationSeconds - 0.001
                             : clip->stretchMarkers[position + 1]
                                       .timelineSeconds
                                   - 0.001;
    if (maximum <= minimum)
        return false;
    auto previous = track->audioClips;
    marker->timelineSeconds = juce::jlimit(minimum, maximum,
                                          timelineSeconds);
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

bool AudioTrackController::removeAudioClipStretchMarker(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t markerId)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return false;
    const auto marker = std::find_if(
        clip->stretchMarkers.begin(), clip->stretchMarkers.end(),
        [markerId](const auto& value) { return value.id == markerId; });
    if (marker == clip->stretchMarkers.end())
        return false;
    auto previous = track->audioClips;
    clip->stretchMarkers.erase(marker);
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

juce::Result AudioTrackController::addAudioClipEventFromFile(
    const std::size_t index, const std::uint64_t clipId,
    const juce::File& sourceFile, const double localStartSeconds,
    std::uint64_t* const newEventId)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return juce::Result::fail("Clip not found");
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(sourceFile));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0)
        return juce::Result::fail(
            tr("error.openAudio", "file", sourceFile.getFileName()));
    const auto sourceLength = static_cast<double>(reader->lengthInSamples)
                              / reader->sampleRate;
    auto previous = track->audioClips;
    materialiseLegacyAudioEvent(*clip);
    const auto eventId = nextAudioEventId_++;
    const auto safeStart = std::max(0.0, localStartSeconds);
    clip->events.push_back({
        eventId, sourceFile.getFileNameWithoutExtension(), safeStart,
        0.0, sourceLength, sourceLength, 0xff53d0c2, sourceFile,
        sourceLength });
    const auto previousDuration = clip->durationSeconds;
    clip->durationSeconds = std::max(previousDuration,
                                     safeStart + sourceLength);
    if (clip->playEndSeconds >= previousDuration - 0.001)
        clip->playEndSeconds = clip->durationSeconds;
    if (clip->loopEndSeconds >= previousDuration - 0.001)
        clip->loopEndSeconds = clip->durationSeconds;
    normaliseAudioClipTimeline(*clip);
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (const auto result = rebuildArrangementPlan(*track); result.failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return result;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    if (newEventId != nullptr)
        *newEventId = eventId;
    return juce::Result::ok();
}

juce::Result AudioTrackController::replaceAudioClipEvents(
    const std::size_t index, const std::uint64_t clipId,
    std::vector<AudioEvent> events)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return juce::Result::fail("Invalid clip content");
    for (auto& event : events) {
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager_.createReaderFor(event.sourceFile));
        if (reader == nullptr || reader->sampleRate <= 0.0
            || reader->lengthInSamples <= 0)
            return juce::Result::fail(
                tr("error.openAudio", "file",
                   event.sourceFile.getFileName()));
        event.sourceLengthSeconds
            = static_cast<double>(reader->lengthInSamples)
              / reader->sampleRate;
        if (event.id == 0)
            event.id = nextAudioEventId_++;
        else
            nextAudioEventId_ = std::max(nextAudioEventId_, event.id + 1);
        for (auto& marker : event.stretchMarkers) {
            if (marker.id == 0)
                marker.id = nextStretchMarkerId_++;
            else
                nextStretchMarkerId_ = std::max(nextStretchMarkerId_,
                                                marker.id + 1);
        }
    }
    auto previous = track->audioClips;
    clip->events = std::move(events);
    clip->usesEventTimeline = true;
    for (const auto& event : clip->events)
        clip->durationSeconds = std::max(
            clip->durationSeconds,
            event.startSeconds + event.durationSeconds);
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (const auto result = rebuildArrangementPlan(*track); result.failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return result;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return juce::Result::ok();
}

bool AudioTrackController::updateAudioClipEvent(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t eventId, const double localStartSeconds,
    const double sourceOffsetSeconds, const double durationSeconds,
    const double sourceDurationSeconds, const double fadeInSeconds,
    const double fadeOutSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return false;
    auto previous = track->audioClips;
    materialiseLegacyAudioEvent(*clip);
    auto event = std::find_if(
        clip->events.begin(), clip->events.end(),
        [eventId](const auto& value) { return value.id == eventId; });
    if (event == clip->events.end())
        return false;
    event->startSeconds = std::max(0.0, localStartSeconds);
    event->sourceOffsetSeconds = std::max(0.0, sourceOffsetSeconds);
    event->durationSeconds = juce::jlimit(0.001, 86'400.0,
                                         durationSeconds);
    event->sourceDurationSeconds = juce::jlimit(
        0.001, 86'400.0, sourceDurationSeconds);
    if (fadeInSeconds >= 0.0)
        event->fadeInSeconds = juce::jlimit(
            0.0, event->durationSeconds, fadeInSeconds);
    if (fadeOutSeconds >= 0.0)
        event->fadeOutSeconds = juce::jlimit(
            0.0, event->durationSeconds, fadeOutSeconds);
    std::erase_if(event->stretchMarkers, [event](const auto& marker) {
        return marker.sourceSeconds <= event->sourceOffsetSeconds + 0.0005
               || marker.sourceSeconds
                      >= event->sourceOffsetSeconds
                             + event->sourceDurationSeconds - 0.0005
               || marker.timelineSeconds <= 0.0005
               || marker.timelineSeconds
                      >= event->durationSeconds - 0.0005;
    });
    const auto previousDuration = clip->durationSeconds;
    clip->durationSeconds = std::max(previousDuration,
                                     event->startSeconds
                                         + event->durationSeconds);
    if (clip->playEndSeconds >= previousDuration - 0.001)
        clip->playEndSeconds = clip->durationSeconds;
    if (clip->loopEndSeconds >= previousDuration - 0.001)
        clip->loopEndSeconds = clip->durationSeconds;
    normaliseAudioClipTimeline(*clip);
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

bool AudioTrackController::setAudioClipEventProperties(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t eventId, const float gainDb,
    const double sourceTempoBpm, const StretchMode mode)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(track->audioClips.begin(), track->audioClips.end(),
                             [clipId](const auto& value) {
                                 return value.id == clipId;
                             });
    if (clip == track->audioClips.end())
        return false;
    auto previous = track->audioClips;
    materialiseLegacyAudioEvent(*clip);
    auto event = std::find_if(clip->events.begin(), clip->events.end(),
                              [eventId](const auto& value) {
                                  return value.id == eventId;
                              });
    if (event == clip->events.end() && clip->events.size() == 1
        && clip->events.front().id == 0) {
        clip->events.front().id = eventId;
        event = clip->events.begin();
    }
    if (event == clip->events.end())
        return false;
    event->gainDb = juce::jlimit(-60.0f, 24.0f, gainDb);
    event->sourceTempoBpm = juce::jlimit(0.0, 400.0, sourceTempoBpm);
    event->stretchMode = mode;
    if (mode == StretchMode::raw) {
        // Never stretched: as long as its part of the file, no stretch
        // point left.
        event->durationSeconds = std::max(0.001, event->sourceDurationSeconds);
        event->stretchMarkers.clear();
        event->fadeInSeconds = std::min(event->fadeInSeconds, event->durationSeconds * 0.5);
        event->fadeOutSeconds = std::min(event->fadeOutSeconds, event->durationSeconds * 0.5);
    }
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

std::uint64_t AudioTrackController::addAudioClipAutomationPoint(
    const std::size_t index, const std::uint64_t clipId,
    const double timeSeconds, const float valueDb)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return 0;
    const auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return 0;
    const auto previous = track->audioClips;
    const auto id = nextClipAutomationPointId_++;
    clip->automation.push_back(
        { id, juce::jlimit(0.0, clip->durationSeconds, timeSeconds),
          juce::jlimit(-60.0f, 12.0f, valueDb) });
    std::sort(clip->automation.begin(), clip->automation.end(),
              [](const auto& left, const auto& right) {
                  return left.timeSeconds < right.timeSeconds;
              });
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = previous;
        (void) rebuildArrangementPlan(*track);
        return 0;
    }
    recordArrangementEdit(*track, previous);
    return id;
}

bool AudioTrackController::updateAudioClipAutomationPoint(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t id, const double timeSeconds, const float valueDb)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return false;
    const auto point = std::find_if(
        clip->automation.begin(), clip->automation.end(),
        [id](const auto& value) { return value.id == id; });
    if (point == clip->automation.end())
        return false;
    const auto previous = track->audioClips;
    point->timeSeconds = juce::jlimit(0.0, clip->durationSeconds,
                                      timeSeconds);
    point->valueDb = juce::jlimit(-60.0f, 12.0f, valueDb);
    std::sort(clip->automation.begin(), clip->automation.end(),
              [](const auto& left, const auto& right) {
                  return left.timeSeconds < right.timeSeconds;
              });
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = previous;
        (void) rebuildArrangementPlan(*track);
        return false;
    }
    recordArrangementEdit(*track, previous);
    return true;
}

bool AudioTrackController::setAudioClipAutomationPointCurve(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t id, const AutomationCurve curve,
    const float curveAmount)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return false;
    const auto point = std::find_if(
        clip->automation.begin(), clip->automation.end(),
        [id](const auto& value) { return value.id == id; });
    if (point == clip->automation.end())
        return false;
    const auto previous = track->audioClips;
    point->curve = curve;
    point->curveAmount = juce::jlimit(-1.0f, 1.0f, curveAmount);
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = previous;
        (void) rebuildArrangementPlan(*track);
        return false;
    }
    recordArrangementEdit(*track, previous);
    return true;
}

bool AudioTrackController::removeAudioClipAutomationPoint(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t id)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return false;
    const auto previous = track->audioClips;
    const auto oldSize = clip->automation.size();
    std::erase_if(clip->automation, [id](const auto& point) {
        return point.id == id;
    });
    if (clip->automation.size() == oldSize)
        return false;
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = previous;
        (void) rebuildArrangementPlan(*track);
        return false;
    }
    recordArrangementEdit(*track, previous);
    return true;
}

std::vector<AudioTrackController::AutomationPoint>
AudioTrackController::audioClipAutomationPoints(
    const std::size_t index, const std::uint64_t clipId) const
{
    const auto clip = audioClip(index, clipId);
    return clip.has_value() ? clip->automation
                            : std::vector<AutomationPoint> {};
}

bool AudioTrackController::setAudioClipLanes(
    const std::size_t index, const std::uint64_t clipId,
    std::map<juce::String, std::vector<AutomationPoint>> lanes)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return false;
    auto previous = track->audioClips;
    for (auto& [name, points] : lanes)
        std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
    std::erase_if(lanes, [](const auto& lane) { return lane.second.empty(); });
    clip->lanes = std::move(lanes);
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

void AudioTrackController::setTrackClipAutomation(
    const std::size_t index,
    std::optional<std::vector<AutomationPoint>> volume,
    std::vector<AutomationPoint> pan)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return;
    track->clipVolume = std::move(volume);
    publishTrackAutomation(*track);
    std::sort(pan.begin(), pan.end(), [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
    auto lane = std::make_shared<std::vector<GainAutomationPoint>>();
    for (const auto& point : pan)
        lane->push_back({ point.timeSeconds, juce::jlimit(-1.0f, 1.0f, point.valueDb), static_cast<int>(point.curve), point.curveAmount,
                          point.handles });
    track->gainPanSource.setPanAutomation(std::move(lane));
}

bool AudioTrackController::setAudioClipTimelineBounds(
    const std::size_t index, const std::uint64_t clipId,
    const double durationSeconds, const double playStartSeconds,
    const double playEndSeconds, const double loopStartSeconds,
    const double loopEndSeconds, const bool looping)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return false;
    auto previous = track->audioClips;
    clip->durationSeconds = durationSeconds;
    clip->playStartSeconds = playStartSeconds;
    clip->playEndSeconds = playEndSeconds;
    clip->loopStartSeconds = loopStartSeconds;
    clip->loopEndSeconds = loopEndSeconds;
    clip->looping = looping;
    normaliseAudioClipTimeline(*clip);
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

std::uint64_t AudioTrackController::splitAudioClipEvent(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t eventId, const double localTimelineSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return 0;
    auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return 0;
    auto previous = track->audioClips;
    materialiseLegacyAudioEvent(*clip);
    auto event = std::find_if(
        clip->events.begin(), clip->events.end(),
        [eventId](const auto& value) { return value.id == eventId; });
    if (event == clip->events.end())
        return 0;
    const auto splitLocal = localTimelineSeconds - event->startSeconds;
    if (splitLocal <= 0.001
        || splitLocal >= event->durationSeconds - 0.001)
        return 0;
    AudioClip mapping;
    mapping.durationSeconds = event->durationSeconds;
    mapping.sourceOffsetSeconds = event->sourceOffsetSeconds;
    mapping.sourceDurationSeconds = event->sourceDurationSeconds;
    mapping.stretchMarkers = event->stretchMarkers;
    const auto splitSource = sourceTimeForClipTimeline(mapping, splitLocal);
    auto right = *event;
    right.id = nextAudioEventId_++;
    right.startSeconds = event->startSeconds + splitLocal;
    right.sourceOffsetSeconds = splitSource;
    right.durationSeconds = event->durationSeconds - splitLocal;
    right.sourceDurationSeconds = event->sourceOffsetSeconds
                                  + event->sourceDurationSeconds
                                  - splitSource;
    right.fadeInSeconds = 0.0;
    right.stretchMarkers = stretchMarkersForSlice(
        mapping, splitLocal, right.durationSeconds);
    event->durationSeconds = splitLocal;
    event->sourceDurationSeconds = splitSource
                                   - event->sourceOffsetSeconds;
    event->fadeOutSeconds = 0.0;
    event->stretchMarkers = stretchMarkersForSlice(
        mapping, 0.0, splitLocal);
    clip->events.push_back(std::move(right));
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return 0;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return clip->events.back().id;
}

bool AudioTrackController::removeAudioClipEvent(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t eventId)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return false;
    auto previous = track->audioClips;
    materialiseLegacyAudioEvent(*clip);
    const auto event = std::find_if(
        clip->events.begin(), clip->events.end(),
        [eventId](const auto& value) { return value.id == eventId; });
    if (event == clip->events.end())
        return false;
    clip->events.erase(event);
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

std::uint64_t AudioTrackController::addAudioClipEventStretchMarker(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t eventId, const double sourceSeconds,
    const double localTimelineSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return 0;
    auto clip = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return 0;
    auto previous = track->audioClips;
    materialiseLegacyAudioEvent(*clip);
    auto event = std::find_if(
        clip->events.begin(), clip->events.end(),
        [eventId](const auto& value) { return value.id == eventId; });
    if (event == clip->events.end())
        return 0;
    const auto markerId = nextStretchMarkerId_++;
    event->stretchMarkers.push_back({
        markerId,
        juce::jlimit(event->sourceOffsetSeconds + 0.001,
                     event->sourceOffsetSeconds
                         + event->sourceDurationSeconds - 0.001,
                     sourceSeconds),
        juce::jlimit(0.001, event->durationSeconds - 0.001,
                     localTimelineSeconds) });
    std::sort(event->stretchMarkers.begin(), event->stretchMarkers.end(),
              [](const auto& left, const auto& right) {
                  return left.sourceSeconds < right.sourceSeconds;
              });
    auto previousTimeline = 0.0;
    for (const auto& marker : event->stretchMarkers) {
        if (marker.timelineSeconds <= previousTimeline + 0.0005) {
            track->audioClips = std::move(previous);
            return 0;
        }
        previousTimeline = marker.timelineSeconds;
    }
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return 0;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return markerId;
}

bool AudioTrackController::updateAudioClipEventStretchMarker(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t eventId, const std::uint64_t markerId,
    const double localTimelineSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(track->audioClips.begin(),
                             track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return false;
    auto previous = track->audioClips;
    materialiseLegacyAudioEvent(*clip);
    auto event = std::find_if(clip->events.begin(), clip->events.end(),
        [eventId](const auto& value) { return value.id == eventId; });
    if (event == clip->events.end())
        return false;
    auto marker = std::find_if(event->stretchMarkers.begin(),
                               event->stretchMarkers.end(),
        [markerId](const auto& value) { return value.id == markerId; });
    if (marker == event->stretchMarkers.end())
        return false;
    const auto position = static_cast<std::size_t>(
        std::distance(event->stretchMarkers.begin(), marker));
    const auto minimum = position == 0 ? 0.001
        : event->stretchMarkers[position - 1].timelineSeconds + 0.001;
    const auto maximum = position + 1 == event->stretchMarkers.size()
        ? event->durationSeconds - 0.001
        : event->stretchMarkers[position + 1].timelineSeconds - 0.001;
    if (maximum <= minimum)
        return false;
    marker->timelineSeconds = juce::jlimit(minimum, maximum,
                                          localTimelineSeconds);
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

bool AudioTrackController::removeAudioClipEventStretchMarker(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t eventId, const std::uint64_t markerId)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(track->audioClips.begin(),
                             track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return false;
    auto previous = track->audioClips;
    materialiseLegacyAudioEvent(*clip);
    auto event = std::find_if(clip->events.begin(), clip->events.end(),
        [eventId](const auto& value) { return value.id == eventId; });
    if (event == clip->events.end())
        return false;
    const auto marker = std::find_if(event->stretchMarkers.begin(),
                                     event->stretchMarkers.end(),
        [markerId](const auto& value) { return value.id == markerId; });
    if (marker == event->stretchMarkers.end())
        return false;
    event->stretchMarkers.erase(marker);
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

juce::Result AudioTrackController::commitAudioClipEventEdit(
    const std::size_t index, const std::uint64_t clipId,
    const std::uint64_t eventId, const juce::File& renderedFile)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    auto clip = std::find_if(track->audioClips.begin(),
                             track->audioClips.end(),
        [clipId](const auto& value) { return value.id == clipId; });
    if (clip == track->audioClips.end())
        return juce::Result::fail("Clip not found");
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(renderedFile));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0)
        return juce::Result::fail(
            tr("error.openAudio", "file", renderedFile.getFileName()));
    auto previous = track->audioClips;
    materialiseLegacyAudioEvent(*clip);
    auto event = std::find_if(clip->events.begin(), clip->events.end(),
        [eventId](const auto& value) { return value.id == eventId; });
    if (event == clip->events.end())
        return juce::Result::fail("Audio event not found");
    event->sourceFile = renderedFile;
    event->sourceLengthSeconds
        = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (const auto result = rebuildArrangementPlan(*track); result.failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return result;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return juce::Result::ok();
}

bool AudioTrackController::moveAudioClip(const std::size_t index,
                                         const std::uint64_t clipId,
                                         const double startSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return false;
    const auto safeStart = std::max(0.0, startSeconds);
    if (std::abs(found->startSeconds - safeStart) < 0.0000001)
        return true;
    auto previous = track->audioClips;
    if (found->stretchMarkers.empty() && found->events.empty()) {
        if (!track->clipSource->moveArrangementRegion(clipId, safeStart))
            return false;
        found->startSeconds = safeStart;
    } else {
        const auto previousStart = found->startSeconds;
        const auto wasPlaying = track->transportSource.isPlaying();
        if (wasPlaying)
            track->transportSource.stop();
        found->startSeconds = safeStart;
        if (rebuildArrangementPlan(*track).failed()) {
            found->startSeconds = previousStart;
            (void) rebuildArrangementPlan(*track);
            if (wasPlaying)
                track->transportSource.start();
            return false;
        }
        if (wasPlaying)
            track->transportSource.start();
    }
    refreshTransportRanges();
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

bool AudioTrackController::moveAudioClips(
    const std::size_t index,
    const std::vector<std::uint64_t>& clipIds,
    const double deltaSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr || clipIds.empty())
        return false;
    std::vector<std::uint64_t> uniqueIds;
    uniqueIds.reserve(clipIds.size());
    for (const auto id : clipIds)
        if (id != 0
            && std::find(uniqueIds.begin(), uniqueIds.end(), id)
                   == uniqueIds.end())
            uniqueIds.push_back(id);
    if (uniqueIds.empty())
        return false;

    auto minimumStart = std::numeric_limits<double>::max();
    std::vector<AudioClip*> targets;
    targets.reserve(uniqueIds.size());
    for (const auto id : uniqueIds) {
        const auto found = std::find_if(
            track->audioClips.begin(), track->audioClips.end(),
            [id](const auto& clip) { return clip.id == id; });
        if (found == track->audioClips.end())
            return false;
        targets.push_back(&*found);
        minimumStart = std::min(minimumStart, found->startSeconds);
    }
    const auto safeDelta = std::max(deltaSeconds, -minimumStart);
    if (std::abs(safeDelta) < 0.0000001)
        return true;

    auto previous = track->audioClips;
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    for (auto* clip : targets)
        clip->startSeconds += safeDelta;
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    const auto currentPosition = track->transportSource.getCurrentPosition();
    const auto sessionLength = transportLength();
    track->transportSource.setPosition(
        juce::jlimit(0.0, sessionLength, currentPosition));
    if (wasPlaying && currentPosition < sessionLength)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

AudioTrackController::ArrangementSessionMoveResult
AudioTrackController::moveAudioClipsAcrossTracks(
    const std::vector<std::vector<std::uint64_t>>& clipIdsByTrack,
    const int trackDelta,
    const double deltaSeconds)
{
    ArrangementSessionMoveResult result;
    if (tracks_.empty() || clipIdsByTrack.empty())
        return result;

    struct MoveGroup {
        std::size_t sourceTrack {};
        std::size_t destinationTrack {};
        std::vector<std::uint64_t> ids;
        std::vector<AudioClip> clips;
    };
    std::vector<MoveGroup> groups;
    auto minimumStart = std::numeric_limits<double>::max();
    for (std::size_t sourceTrack = 0;
         sourceTrack < clipIdsByTrack.size(); ++sourceTrack) {
        if (clipIdsByTrack[sourceTrack].empty())
            continue;
        if (sourceTrack >= tracks_.size())
            return result;
        const auto destination = static_cast<long long>(sourceTrack)
                                 + static_cast<long long>(trackDelta);
        if (destination < 0
            || destination >= static_cast<long long>(tracks_.size()))
            return result;

        MoveGroup group;
        group.sourceTrack = sourceTrack;
        group.destinationTrack = static_cast<std::size_t>(destination);
        group.ids.reserve(clipIdsByTrack[sourceTrack].size());
        group.clips.reserve(clipIdsByTrack[sourceTrack].size());
        for (const auto id : clipIdsByTrack[sourceTrack]) {
            if (id == 0
                || std::find(group.ids.begin(), group.ids.end(), id)
                       != group.ids.end())
                continue;
            const auto found = std::find_if(
                tracks_[sourceTrack]->audioClips.begin(),
                tracks_[sourceTrack]->audioClips.end(),
                [id](const auto& clip) { return clip.id == id; });
            if (found == tracks_[sourceTrack]->audioClips.end())
                return result;
            group.ids.push_back(id);
            group.clips.push_back(*found);
            minimumStart = std::min(minimumStart, found->startSeconds);
        }
        if (!group.ids.empty())
            groups.push_back(std::move(group));
    }
    if (groups.empty()
        || minimumStart == std::numeric_limits<double>::max())
        return result;

    const auto safeDelta = std::max(deltaSeconds, -minimumStart);
    result.clipIdsByTrack.resize(tracks_.size());
    if (trackDelta == 0 && std::abs(safeDelta) < 0.0000001) {
        for (const auto& group : groups)
            result.clipIdsByTrack[group.sourceTrack] = group.ids;
        result.succeeded = true;
        return result;
    }

    std::vector<std::vector<AudioClip>> previousStates;
    previousStates.reserve(tracks_.size());
    for (const auto& track : tracks_)
        previousStates.push_back(track->audioClips);
    auto nextStates = previousStates;
    auto candidateNextClipId = nextAudioClipId_;

    if (trackDelta == 0) {
        for (const auto& group : groups) {
            for (const auto id : group.ids) {
                const auto found = std::find_if(
                    nextStates[group.sourceTrack].begin(),
                    nextStates[group.sourceTrack].end(),
                    [id](const auto& clip) { return clip.id == id; });
                if (found == nextStates[group.sourceTrack].end())
                    return {};
                found->startSeconds += safeDelta;
            }
            result.clipIdsByTrack[group.sourceTrack] = group.ids;
        }
    } else {
        for (const auto& group : groups)
            std::erase_if(nextStates[group.sourceTrack], [&group](const auto& clip) {
                return std::find(group.ids.begin(), group.ids.end(), clip.id)
                       != group.ids.end();
            });
        for (const auto& group : groups) {
            auto& destination = nextStates[group.destinationTrack];
            auto& movedIds = result.clipIdsByTrack[group.destinationTrack];
            movedIds.reserve(movedIds.size() + group.clips.size());
            for (const auto& source : group.clips) {
                auto moved = source;
                moved.id = candidateNextClipId++;
                moved.startSeconds += safeDelta;
                movedIds.push_back(moved.id);
                destination.push_back(std::move(moved));
            }
        }
    }

    if (std::any_of(nextStates.begin(), nextStates.end(), [](const auto& clips) {
            return clips.size() > maximumAudioClipsPerTrack;
        }))
        return {};

    std::vector<std::size_t> impactedTracks;
    for (std::size_t index = 0; index < tracks_.size(); ++index)
        if (!sameArrangement(previousStates[index], nextStates[index]))
            impactedTracks.push_back(index);
    if (impactedTracks.empty()) {
        result.succeeded = true;
        return result;
    }

    struct PlaybackState {
        bool wasPlaying {};
        double position {};
    };
    std::vector<PlaybackState> playbackStates(tracks_.size());
    for (const auto index : impactedTracks) {
        auto& track = *tracks_[index];
        playbackStates[index] = {
            track.transportSource.isPlaying(),
            track.transportSource.getCurrentPosition()
        };
        if (playbackStates[index].wasPlaying)
            track.transportSource.stop();
        track.audioClips = nextStates[index];
    }

    auto rebuiltCount = std::size_t {};
    for (const auto index : impactedTracks) {
        if (rebuildArrangementPlan(*tracks_[index]).failed())
            break;
        ++rebuiltCount;
    }
    if (rebuiltCount != impactedTracks.size()) {
        for (const auto index : impactedTracks) {
            tracks_[index]->audioClips = previousStates[index];
            (void) rebuildArrangementPlan(*tracks_[index]);
        }
        for (const auto index : impactedTracks)
            if (playbackStates[index].wasPlaying)
                tracks_[index]->transportSource.start();
        return {};
    }

    nextAudioClipId_ = candidateNextClipId;
    const auto sessionLength = transportLength();
    for (const auto index : impactedTracks) {
        auto& track = *tracks_[index];
        const auto trackLength = track.clipSource->arrangementLengthSeconds();
        track.transportSource.setPosition(
            juce::jlimit(0.0, sessionLength,
                         playbackStates[index].position));
        if (playbackStates[index].wasPlaying
            && playbackStates[index].position < sessionLength)
            track.transportSource.start();
        for (auto& point : track.automation)
            point.timeSeconds = std::min(point.timeSeconds, trackLength);
        for (auto& lane : track.pluginAutomation)
            for (auto& point : lane.points)
                point.timeSeconds = std::min(point.timeSeconds, trackLength);
        publishTrackAutomation(track);
        publishTrackPluginAutomation(track);
    }

    const auto ownsBatch = !arrangementEditBatch_.has_value();
    if (ownsBatch)
        beginArrangementEditBatch();
    for (const auto index : impactedTracks)
        recordArrangementEdit(*tracks_[index], previousStates[index]);
    if (ownsBatch && !endArrangementEditBatch())
        return {};

    result.succeeded = true;
    result.changed = true;
    return result;
}

std::uint64_t AudioTrackController::duplicateAudioClip(
    const std::size_t index,
    const std::uint64_t clipId,
    const double startSeconds)
{
    const auto clip = audioClip(index, clipId);
    if (!clip.has_value())
        return 0;
    const auto destination = startSeconds >= 0.0
                                 ? startSeconds
                                 : clip->startSeconds + clip->durationSeconds;
    if (!clip->events.empty()) {
        const auto inserted = insertAudioClips(index, { *clip }, destination);
        return inserted.empty() ? 0 : inserted.front();
    }
    return addAudioClip(index, destination, clip->sourceOffsetSeconds,
                        clip->durationSeconds, clip->name + " Copy",
                        clip->colour, clip->sourceFile,
                        clip->fadeInSeconds, clip->fadeOutSeconds);
}

std::vector<std::uint64_t> AudioTrackController::duplicateAudioClips(
    const std::size_t index,
    const std::vector<std::uint64_t>& clipIds)
{
    auto* track = findTrack(index);
    if (track == nullptr || clipIds.empty())
        return {};
    std::vector<AudioClip> sources;
    sources.reserve(clipIds.size());
    for (const auto id : clipIds) {
        if (id == 0
            || std::any_of(sources.begin(), sources.end(), [id](const auto& clip) {
                   return clip.id == id;
               }))
            continue;
        const auto found = std::find_if(
            track->audioClips.begin(), track->audioClips.end(),
            [id](const auto& clip) { return clip.id == id; });
        if (found == track->audioClips.end())
            return {};
        sources.push_back(*found);
    }
    if (sources.empty()
        || track->audioClips.size() + sources.size()
               > maximumAudioClipsPerTrack)
        return {};

    auto firstStart = std::numeric_limits<double>::max();
    auto lastEnd = 0.0;
    for (const auto& clip : sources) {
        firstStart = std::min(firstStart, clip.startSeconds);
        lastEnd = std::max(lastEnd,
                           clip.startSeconds + clip.durationSeconds);
    }
    const auto offset = std::max(0.001, lastEnd - firstStart);
    auto previous = track->audioClips;
    std::vector<std::uint64_t> duplicates;
    duplicates.reserve(sources.size());
    for (const auto& source : sources) {
        const auto id = nextAudioClipId_++;
        duplicates.push_back(id);
        auto copy = source;
        copy.id = id;
        for (auto& event : copy.events)
            event.id = nextAudioEventId_++;
        copy.name += " Copy";
        copy.startSeconds += offset;
        track->audioClips.push_back(std::move(copy));
    }
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        return {};
    }
    recordArrangementEdit(*track, std::move(previous));
    return duplicates;
}

std::vector<std::uint64_t> AudioTrackController::insertAudioClips(
    const std::size_t index,
    const std::vector<AudioClip>& clips,
    const double destinationStartSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr || clips.empty()
        || track->audioClips.size() + clips.size()
               > maximumAudioClipsPerTrack)
        return {};

    auto firstStart = std::numeric_limits<double>::max();
    for (const auto& clip : clips) {
        if (clip.durationSeconds <= 0.0
            || (clip.sourceFile == juce::File {}
                && track->file == juce::File {}))
            return {};
        firstStart = std::min(firstStart, clip.startSeconds);
    }

    auto previous = track->audioClips;
    std::vector<std::uint64_t> inserted;
    inserted.reserve(clips.size());
    const auto destination = std::max(0.0, destinationStartSeconds);
    for (const auto& source : clips) {
        auto copy = source;
        copy.id = nextAudioClipId_++;
        for (auto& event : copy.events)
            event.id = nextAudioEventId_++;
        copy.startSeconds = destination
                            + std::max(0.0, source.startSeconds - firstStart);
        copy.sourceLengthSeconds = 0.0;
        if (copy.sourceFile == juce::File {})
            copy.sourceFile = track->file;
        inserted.push_back(copy.id);
        track->audioClips.push_back(std::move(copy));
    }
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        return {};
    }
    recordArrangementEdit(*track, std::move(previous));
    return inserted;
}

std::uint64_t AudioTrackController::splitAudioClip(
    const std::size_t index,
    const std::uint64_t clipId,
    const double timelineSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr
        || track->audioClips.size() >= maximumAudioClipsPerTrack)
        return 0;
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return 0;
    const auto clip = *found;
    constexpr double minimumDuration = 0.001;
    const auto leftDuration = timelineSeconds - clip.startSeconds;
    const auto rightDuration = clip.durationSeconds - leftDuration;
    if (leftDuration < minimumDuration || rightDuration < minimumDuration)
        return 0;
    if (!clip.events.empty()) {
        std::vector<AudioEvent> leftEvents;
        std::vector<AudioEvent> rightEvents;
        for (const auto& sourceEvent : clip.events) {
            const auto eventStart = sourceEvent.startSeconds;
            const auto eventEnd = eventStart + sourceEvent.durationSeconds;
            if (eventEnd <= leftDuration + minimumDuration) {
                leftEvents.push_back(sourceEvent);
                continue;
            }
            if (eventStart >= leftDuration - minimumDuration) {
                auto rightEvent = sourceEvent;
                rightEvent.startSeconds = std::max(
                    0.0, eventStart - leftDuration);
                rightEvents.push_back(std::move(rightEvent));
                continue;
            }
            const auto eventSplit = leftDuration - eventStart;
            AudioClip mapping;
            mapping.durationSeconds = sourceEvent.durationSeconds;
            mapping.sourceOffsetSeconds = sourceEvent.sourceOffsetSeconds;
            mapping.sourceDurationSeconds
                = sourceEvent.sourceDurationSeconds;
            mapping.stretchMarkers = sourceEvent.stretchMarkers;
            const auto splitSource = sourceTimeForClipTimeline(
                mapping, eventSplit);
            auto leftEvent = sourceEvent;
            leftEvent.durationSeconds = eventSplit;
            leftEvent.sourceDurationSeconds = splitSource
                                              - sourceEvent
                                                    .sourceOffsetSeconds;
            leftEvent.fadeOutSeconds = 0.0;
            leftEvent.stretchMarkers = stretchMarkersForSlice(
                mapping, 0.0, eventSplit);
            leftEvents.push_back(std::move(leftEvent));
            auto rightEvent = sourceEvent;
            rightEvent.id = nextAudioEventId_++;
            rightEvent.startSeconds = 0.0;
            rightEvent.sourceOffsetSeconds = splitSource;
            rightEvent.durationSeconds = eventEnd - leftDuration;
            rightEvent.sourceDurationSeconds
                = sourceEvent.sourceOffsetSeconds
                  + sourceEvent.sourceDurationSeconds - splitSource;
            rightEvent.fadeInSeconds = 0.0;
            rightEvent.stretchMarkers = stretchMarkersForSlice(
                mapping, eventSplit, rightEvent.durationSeconds);
            rightEvents.push_back(std::move(rightEvent));
        }
        if (leftEvents.empty() || rightEvents.empty())
            return 0;
        auto previous = track->audioClips;
        found->durationSeconds = leftDuration;
        found->events = std::move(leftEvents);
        const auto rightId = nextAudioClipId_++;
        auto right = clip;
        right.id = rightId;
        right.name = clip.name + " B";
        right.startSeconds = timelineSeconds;
        right.durationSeconds = rightDuration;
        right.events = std::move(rightEvents);
        track->audioClips.push_back(std::move(right));
        if (rebuildArrangementPlan(*track).failed()) {
            track->audioClips = std::move(previous);
            (void) rebuildArrangementPlan(*track);
            return 0;
        }
        recordArrangementEdit(*track, std::move(previous));
        return rightId;
    }
    auto previous = track->audioClips;
    const auto splitSource = sourceTimeForClipTimeline(clip, leftDuration);
    const auto leftSourceDuration = splitSource
                                    - clip.sourceOffsetSeconds;
    const auto rightSourceDuration = std::max(
        1.0e-9, clip.sourceDurationSeconds - leftSourceDuration);
    found->durationSeconds = leftDuration;
    found->sourceDurationSeconds = leftSourceDuration;
    found->fadeInSeconds = std::min(clip.fadeInSeconds, leftDuration);
    found->fadeOutSeconds = 0.0;
    found->stretchMarkers = stretchMarkersForSlice(
        clip, 0.0, leftDuration);
    const auto rightId = nextAudioClipId_++;
    auto right = clip;
    right.id = rightId;
    right.name = clip.name + " B";
    right.startSeconds = timelineSeconds;
    right.sourceOffsetSeconds = clip.sourceOffsetSeconds
                                + leftSourceDuration;
    right.durationSeconds = rightDuration;
    right.sourceDurationSeconds = rightSourceDuration;
    right.fadeInSeconds = 0.0;
    right.fadeOutSeconds = std::min(clip.fadeOutSeconds,
                                    rightDuration);
    right.stretchMarkers = stretchMarkersForSlice(
        clip, leftDuration, rightDuration);
    track->audioClips.push_back(std::move(right));
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        return 0;
    }
    recordArrangementEdit(*track, std::move(previous));
    return rightId;
}

AudioTrackController::ArrangementRangeEditResult
AudioTrackController::editAudioClipRange(
    const std::size_t index,
    const std::vector<std::uint64_t>& clipIds,
    const double rangeStartSeconds,
    const double rangeEndSeconds,
    const ArrangementRangeOperation operation)
{
    ArrangementRangeEditResult result;
    auto* track = findTrack(index);
    if (track == nullptr || clipIds.empty())
        return result;

    constexpr double minimumDuration = 0.001;
    constexpr double comparisonTolerance = 0.0000001;
    const auto rangeStart = std::max(
        0.0, std::min(rangeStartSeconds, rangeEndSeconds));
    const auto rangeEnd = std::max(
        rangeStart, std::max(rangeStartSeconds, rangeEndSeconds));
    if (rangeEnd - rangeStart < minimumDuration)
        return result;

    std::vector<std::uint64_t> uniqueIds;
    uniqueIds.reserve(clipIds.size());
    for (const auto id : clipIds)
        if (id != 0
            && std::find(uniqueIds.begin(), uniqueIds.end(), id)
                   == uniqueIds.end())
            uniqueIds.push_back(id);
    if (uniqueIds.empty()
        || std::any_of(uniqueIds.begin(), uniqueIds.end(), [track](const auto id) {
               return std::none_of(
                   track->audioClips.begin(), track->audioClips.end(),
                   [id](const auto& clip) { return clip.id == id; });
           }))
        return result;

    const auto previous = track->audioClips;
    std::vector<AudioClip> edited;
    edited.reserve(previous.size() + uniqueIds.size() * 2);
    auto candidateNextClipId = nextAudioClipId_;
    auto changed = false;

    const auto isTarget = [&uniqueIds](const auto id) {
        return std::find(uniqueIds.begin(), uniqueIds.end(), id)
               != uniqueIds.end();
    };
    const auto overlapsRange = [rangeStart, rangeEnd](
                                   const double start,
                                   const double end) {
        return std::min(end, rangeEnd) - std::max(start, rangeStart)
               > comparisonTolerance;
    };

    for (const auto& clip : previous) {
        const auto clipStart = clip.startSeconds;
        const auto clipEnd = clip.startSeconds + clip.durationSeconds;
        const auto overlapStart = std::max(clipStart, rangeStart);
        const auto overlapEnd = std::min(clipEnd, rangeEnd);
        const auto overlapDuration = overlapEnd - overlapStart;
        if (!isTarget(clip.id) || overlapDuration < minimumDuration) {
            edited.push_back(clip);
            continue;
        }

        if (operation == ArrangementRangeOperation::splitAtBoundaries) {
            std::vector<double> boundaries { clipStart };
            for (const auto boundary : { rangeStart, rangeEnd }) {
                if (boundary - boundaries.back() >= minimumDuration
                    && clipEnd - boundary >= minimumDuration)
                    boundaries.push_back(boundary);
            }
            boundaries.push_back(clipEnd);
            changed = boundaries.size() > 2 || changed;
            for (std::size_t segmentIndex = 0;
                 segmentIndex + 1 < boundaries.size(); ++segmentIndex) {
                auto segment = clip;
                const auto segmentStart = boundaries[segmentIndex];
                const auto segmentEnd = boundaries[segmentIndex + 1];
                segment.id = segmentIndex == 0
                                 ? clip.id
                                 : candidateNextClipId++;
                if (segmentIndex != 0)
                    segment.name = clip.name + " "
                                   + juce::String(
                                       static_cast<int>(segmentIndex + 1));
                segment.startSeconds = segmentStart;
                const auto relativeStart = segmentStart - clipStart;
                const auto relativeEnd = segmentEnd - clipStart;
                segment.sourceOffsetSeconds = sourceTimeForClipTimeline(
                    clip, relativeStart);
                segment.durationSeconds = segmentEnd - segmentStart;
                segment.sourceDurationSeconds = sourceTimeForClipTimeline(
                    clip, relativeEnd) - segment.sourceOffsetSeconds;
                segment.stretchMarkers = stretchMarkersForSlice(
                    clip, relativeStart, segment.durationSeconds);
                segment.fadeInSeconds = segmentIndex == 0
                                            ? std::min(
                                                  clip.fadeInSeconds,
                                                  segment.durationSeconds)
                                            : 0.0;
                segment.fadeOutSeconds = segmentIndex + 2 == boundaries.size()
                                             ? std::min(
                                                   clip.fadeOutSeconds,
                                                   segment.durationSeconds)
                                             : 0.0;
                if (overlapsRange(segmentStart, segmentEnd))
                    result.affectedClipIds.push_back(segment.id);
                edited.push_back(std::move(segment));
            }
            continue;
        }

        if (operation == ArrangementRangeOperation::cropToRange) {
            auto cropped = clip;
            cropped.startSeconds = overlapStart;
            const auto cropRelativeStart = overlapStart - clipStart;
            const auto cropRelativeEnd = overlapEnd - clipStart;
            cropped.sourceOffsetSeconds = sourceTimeForClipTimeline(
                clip, cropRelativeStart);
            cropped.durationSeconds = overlapDuration;
            cropped.sourceDurationSeconds = sourceTimeForClipTimeline(
                clip, cropRelativeEnd) - cropped.sourceOffsetSeconds;
            cropped.stretchMarkers = stretchMarkersForSlice(
                clip, cropRelativeStart, overlapDuration);
            cropped.fadeInSeconds = std::abs(overlapStart - clipStart)
                                            <= comparisonTolerance
                                        ? std::min(clip.fadeInSeconds,
                                                   overlapDuration)
                                        : 0.0;
            cropped.fadeOutSeconds = std::abs(overlapEnd - clipEnd)
                                             <= comparisonTolerance
                                         ? std::min(clip.fadeOutSeconds,
                                                    overlapDuration)
                                         : 0.0;
            changed = changed
                      || std::abs(cropped.startSeconds - clip.startSeconds)
                             > comparisonTolerance
                      || std::abs(cropped.sourceOffsetSeconds
                                  - clip.sourceOffsetSeconds)
                             > comparisonTolerance
                      || std::abs(cropped.durationSeconds
                                  - clip.durationSeconds)
                             > comparisonTolerance;
            result.affectedClipIds.push_back(cropped.id);
            edited.push_back(std::move(cropped));
            continue;
        }

        const auto leftDuration = rangeStart - clipStart;
        const auto rightDuration = clipEnd - rangeEnd;
        const auto keepLeft = leftDuration >= minimumDuration;
        const auto keepRight = rightDuration >= minimumDuration;
        if (keepLeft) {
            auto left = clip;
            left.durationSeconds = leftDuration;
            left.sourceDurationSeconds = sourceTimeForClipTimeline(
                clip, leftDuration) - clip.sourceOffsetSeconds;
            left.stretchMarkers = stretchMarkersForSlice(
                clip, 0.0, leftDuration);
            left.fadeInSeconds = std::min(clip.fadeInSeconds,
                                          left.durationSeconds);
            left.fadeOutSeconds = 0.0;
            edited.push_back(std::move(left));
        }
        if (keepRight) {
            auto right = clip;
            right.id = keepLeft ? candidateNextClipId++ : clip.id;
            if (keepLeft)
                right.name = clip.name + " 2";
            right.startSeconds = rangeEnd;
            const auto rightRelativeStart = rangeEnd - clipStart;
            right.sourceOffsetSeconds = sourceTimeForClipTimeline(
                clip, rightRelativeStart);
            right.durationSeconds = rightDuration;
            right.sourceDurationSeconds = sourceTimeForClipTimeline(
                clip, clip.durationSeconds) - right.sourceOffsetSeconds;
            right.stretchMarkers = stretchMarkersForSlice(
                clip, rightRelativeStart, rightDuration);
            right.fadeInSeconds = 0.0;
            right.fadeOutSeconds = std::min(clip.fadeOutSeconds,
                                            right.durationSeconds);
            edited.push_back(std::move(right));
        }
        changed = true;
    }

    result.succeeded = true;
    result.changed = changed;
    if (!changed)
        return result;
    if (edited.size() > maximumAudioClipsPerTrack) {
        result = {};
        return result;
    }

    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    track->audioClips = std::move(edited);
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = previous;
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        result = {};
        return result;
    }
    nextAudioClipId_ = candidateNextClipId;

    const auto currentPosition = track->transportSource.getCurrentPosition();
    const auto trackLength = track->clipSource->arrangementLengthSeconds();
    const auto sessionLength = transportLength();
    track->transportSource.setPosition(
        juce::jlimit(0.0, sessionLength, currentPosition));
    if (wasPlaying && currentPosition < sessionLength)
        track->transportSource.start();
    for (auto& point : track->automation)
        point.timeSeconds = std::min(point.timeSeconds, trackLength);
    for (auto& lane : track->pluginAutomation)
        for (auto& point : lane.points)
            point.timeSeconds = std::min(point.timeSeconds, trackLength);
    publishTrackAutomation(*track);
    publishTrackPluginAutomation(*track);

    const auto clipStillExists = [track](const auto id) {
        return std::any_of(
            track->audioClips.begin(), track->audioClips.end(),
            [id](const auto& clip) { return clip.id == id; });
    };
    const auto removeMissingClipHistory = [&clipStillExists](auto& states) {
        std::erase_if(states, [&clipStillExists](const auto& state) {
            return !clipStillExists(state.clip.id);
        });
    };
    removeMissingClipHistory(track->clipAudioEditUndoStates);
    removeMissingClipHistory(track->clipAudioEditRedoStates);
    recordArrangementEdit(*track, previous);
    return result;
}

bool AudioTrackController::autoCrossfadeAudioClip(
    const std::size_t index,
    const std::uint64_t clipId)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto selected = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (selected == track->audioClips.end())
        return false;
    auto previous = track->audioClips;

    auto neighbour = track->audioClips.end();
    auto largestOverlap = 0.0;
    const auto selectedEnd = selected->startSeconds + selected->durationSeconds;
    for (auto candidate = track->audioClips.begin();
         candidate != track->audioClips.end(); ++candidate) {
        if (candidate == selected)
            continue;
        const auto overlap = std::min(
                                 selectedEnd,
                                 candidate->startSeconds
                                     + candidate->durationSeconds)
                             - std::max(selected->startSeconds,
                                        candidate->startSeconds);
        if (overlap > largestOverlap) {
            largestOverlap = overlap;
            neighbour = candidate;
        }
    }
    if (neighbour == track->audioClips.end() || largestOverlap < 0.001)
        return false;

    auto* outgoing = &*selected;
    auto* incoming = &*neighbour;
    if (incoming->startSeconds < outgoing->startSeconds)
        std::swap(outgoing, incoming);
    outgoing->fadeOutSeconds = std::min(largestOverlap,
                                        outgoing->durationSeconds);
    incoming->fadeInSeconds = std::min(largestOverlap,
                                       incoming->durationSeconds);

    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    if (wasPlaying)
        track->transportSource.start();
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

bool AudioTrackController::removeAudioClip(const std::size_t index,
                                           const std::uint64_t clipId)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return false;
    auto previous = track->audioClips;
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    track->audioClips.erase(found);
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    const auto currentPosition = track->transportSource.getCurrentPosition();
    const auto trackLength = track->clipSource->arrangementLengthSeconds();
    const auto sessionLength = transportLength();
    track->transportSource.setPosition(
        juce::jlimit(0.0, sessionLength, currentPosition));
    if (wasPlaying && currentPosition < sessionLength)
        track->transportSource.start();
    for (auto& point : track->automation)
        point.timeSeconds = std::min(point.timeSeconds, trackLength);
    for (auto& lane : track->pluginAutomation)
        for (auto& point : lane.points)
            point.timeSeconds = std::min(point.timeSeconds, trackLength);
    publishTrackAutomation(*track);
    publishTrackPluginAutomation(*track);
    const auto removeClipHistory = [clipId](auto& states) {
        states.erase(std::remove_if(
                         states.begin(), states.end(),
                         [clipId](const auto& state) {
                             return state.clip.id == clipId;
                         }),
                     states.end());
    };
    removeClipHistory(track->clipAudioEditUndoStates);
    removeClipHistory(track->clipAudioEditRedoStates);
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

bool AudioTrackController::removeAudioClips(
    const std::size_t index,
    const std::vector<std::uint64_t>& clipIds)
{
    auto* track = findTrack(index);
    if (track == nullptr || clipIds.empty())
        return false;
    std::vector<std::uint64_t> uniqueIds;
    uniqueIds.reserve(clipIds.size());
    for (const auto id : clipIds)
        if (id != 0
            && std::find(uniqueIds.begin(), uniqueIds.end(), id)
                   == uniqueIds.end())
            uniqueIds.push_back(id);
    if (uniqueIds.empty()
        || std::any_of(uniqueIds.begin(), uniqueIds.end(), [track](const auto id) {
               return std::none_of(
                   track->audioClips.begin(), track->audioClips.end(),
                   [id](const auto& clip) { return clip.id == id; });
           }))
        return false;

    auto previous = track->audioClips;
    const auto wasPlaying = track->transportSource.isPlaying();
    if (wasPlaying)
        track->transportSource.stop();
    std::erase_if(track->audioClips, [&uniqueIds](const auto& clip) {
        return std::find(uniqueIds.begin(), uniqueIds.end(), clip.id)
               != uniqueIds.end();
    });
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = std::move(previous);
        (void) rebuildArrangementPlan(*track);
        if (wasPlaying)
            track->transportSource.start();
        return false;
    }
    const auto currentPosition = track->transportSource.getCurrentPosition();
    const auto trackLength = track->clipSource->arrangementLengthSeconds();
    const auto sessionLength = transportLength();
    track->transportSource.setPosition(
        juce::jlimit(0.0, sessionLength, currentPosition));
    if (wasPlaying && currentPosition < sessionLength)
        track->transportSource.start();
    for (auto& point : track->automation)
        point.timeSeconds = std::min(point.timeSeconds, trackLength);
    for (auto& lane : track->pluginAutomation)
        for (auto& point : lane.points)
            point.timeSeconds = std::min(point.timeSeconds, trackLength);
    publishTrackAutomation(*track);
    publishTrackPluginAutomation(*track);
    const auto removeClipHistory = [&uniqueIds](auto& states) {
        states.erase(std::remove_if(
                         states.begin(), states.end(),
                         [&uniqueIds](const auto& state) {
                             return std::find(uniqueIds.begin(), uniqueIds.end(),
                                              state.clip.id)
                                    != uniqueIds.end();
                         }),
                     states.end());
    };
    removeClipHistory(track->clipAudioEditUndoStates);
    removeClipHistory(track->clipAudioEditRedoStates);
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

bool AudioTrackController::undoArrangementEdit(const std::size_t index)
{
    arrangementEditBatch_.reset();
    arrangementSessionUndoStates_.clear();
    arrangementSessionRedoStates_.clear();
    auto* track = findTrack(index);
    if (track == nullptr || track->arrangementEditUndoStates.empty())
        return false;
    auto current = track->audioClips;
    const auto& previous = track->arrangementEditUndoStates.back();
    if (!restoreArrangementEditState(*track, previous))
        return false;
    track->arrangementEditUndoStates.pop_back();
    track->arrangementEditRedoStates.push_back(std::move(current));
    if (track->arrangementEditRedoStates.size() > 32)
        track->arrangementEditRedoStates.erase(
            track->arrangementEditRedoStates.begin());
    return true;
}

bool AudioTrackController::redoArrangementEdit(const std::size_t index)
{
    arrangementEditBatch_.reset();
    arrangementSessionUndoStates_.clear();
    arrangementSessionRedoStates_.clear();
    auto* track = findTrack(index);
    if (track == nullptr || track->arrangementEditRedoStates.empty())
        return false;
    auto current = track->audioClips;
    const auto& next = track->arrangementEditRedoStates.back();
    if (!restoreArrangementEditState(*track, next))
        return false;
    track->arrangementEditRedoStates.pop_back();
    track->arrangementEditUndoStates.push_back(std::move(current));
    if (track->arrangementEditUndoStates.size() > 32)
        track->arrangementEditUndoStates.erase(
            track->arrangementEditUndoStates.begin());
    return true;
}

bool AudioTrackController::undoArrangementEdit()
{
    if (arrangementEditBatch_.has_value()
        || arrangementSessionUndoStates_.empty())
        return false;
    const auto& target = arrangementSessionUndoStates_.back();
    ArrangementSessionState inverse;
    if (!restoreArrangementSessionState(target, inverse))
        return false;
    static constexpr std::size_t maximumHistorySize = 32;
    for (std::size_t index = 0; index < target.tracks.size(); ++index) {
        auto* track = findTrack(target.tracks[index].trackIndex);
        if (track == nullptr)
            continue;
        if (!track->arrangementEditUndoStates.empty()
            && sameArrangement(track->arrangementEditUndoStates.back(),
                               target.tracks[index].clips))
            track->arrangementEditUndoStates.pop_back();
        else
            track->arrangementEditUndoStates.clear();
        track->arrangementEditRedoStates.push_back(
            inverse.tracks[index].clips);
        if (track->arrangementEditRedoStates.size() > maximumHistorySize)
            track->arrangementEditRedoStates.erase(
                track->arrangementEditRedoStates.begin());
    }
    arrangementSessionUndoStates_.pop_back();
    arrangementSessionRedoStates_.push_back(std::move(inverse));
    if (arrangementSessionRedoStates_.size() > maximumHistorySize)
        arrangementSessionRedoStates_.erase(
            arrangementSessionRedoStates_.begin());
    return true;
}

bool AudioTrackController::redoArrangementEdit()
{
    if (arrangementEditBatch_.has_value()
        || arrangementSessionRedoStates_.empty())
        return false;
    const auto& target = arrangementSessionRedoStates_.back();
    ArrangementSessionState inverse;
    if (!restoreArrangementSessionState(target, inverse))
        return false;
    static constexpr std::size_t maximumHistorySize = 32;
    for (std::size_t index = 0; index < target.tracks.size(); ++index) {
        auto* track = findTrack(target.tracks[index].trackIndex);
        if (track == nullptr)
            continue;
        if (!track->arrangementEditRedoStates.empty()
            && sameArrangement(track->arrangementEditRedoStates.back(),
                               target.tracks[index].clips))
            track->arrangementEditRedoStates.pop_back();
        else
            track->arrangementEditRedoStates.clear();
        track->arrangementEditUndoStates.push_back(
            inverse.tracks[index].clips);
        if (track->arrangementEditUndoStates.size() > maximumHistorySize)
            track->arrangementEditUndoStates.erase(
                track->arrangementEditUndoStates.begin());
    }
    arrangementSessionRedoStates_.pop_back();
    arrangementSessionUndoStates_.push_back(std::move(inverse));
    if (arrangementSessionUndoStates_.size() > maximumHistorySize)
        arrangementSessionUndoStates_.erase(
            arrangementSessionUndoStates_.begin());
    return true;
}

bool AudioTrackController::canUndoArrangementEdit() const noexcept
{
    return !arrangementEditBatch_.has_value()
           && !arrangementSessionUndoStates_.empty();
}

bool AudioTrackController::canRedoArrangementEdit() const noexcept
{
    return !arrangementEditBatch_.has_value()
           && !arrangementSessionRedoStates_.empty();
}

void AudioTrackController::beginArrangementEditBatch()
{
    if (!arrangementEditBatch_.has_value())
        arrangementEditBatch_.emplace();
}

bool AudioTrackController::endArrangementEditBatch()
{
    if (!arrangementEditBatch_.has_value())
        return false;
    auto state = std::move(*arrangementEditBatch_);
    arrangementEditBatch_.reset();
    if (state.tracks.empty())
        return false;
    static constexpr std::size_t maximumHistorySize = 32;
    arrangementSessionUndoStates_.push_back(std::move(state));
    if (arrangementSessionUndoStates_.size() > maximumHistorySize)
        arrangementSessionUndoStates_.erase(
            arrangementSessionUndoStates_.begin());
    arrangementSessionRedoStates_.clear();
    return true;
}

bool AudioTrackController::cancelArrangementEditBatch()
{
    if (!arrangementEditBatch_.has_value())
        return false;
    auto state = std::move(*arrangementEditBatch_);
    arrangementEditBatch_.reset();
    if (state.tracks.empty())
        return true;
    ArrangementSessionState inverse;
    if (!restoreArrangementSessionState(state, inverse))
        return false;
    for (const auto& target : state.tracks) {
        if (auto* track = findTrack(target.trackIndex)) {
            if (!track->arrangementEditUndoStates.empty()
                && sameArrangement(
                    track->arrangementEditUndoStates.back(),
                    target.clips))
                track->arrangementEditUndoStates.pop_back();
        }
    }
    return true;
}

bool AudioTrackController::canUndoArrangementEdit(
    const std::size_t index) const noexcept
{
    const auto* track = findTrack(index);
    return track != nullptr && !track->arrangementEditUndoStates.empty();
}

bool AudioTrackController::canRedoArrangementEdit(
    const std::size_t index) const noexcept
{
    const auto* track = findTrack(index);
    return track != nullptr && !track->arrangementEditRedoStates.empty();
}

void AudioTrackController::clearArrangementEditHistory(
    const std::size_t index) noexcept
{
    arrangementEditBatch_.reset();
    arrangementSessionUndoStates_.clear();
    arrangementSessionRedoStates_.clear();
    if (auto* track = findTrack(index)) {
        track->arrangementEditUndoStates.clear();
        track->arrangementEditRedoStates.clear();
    }
}

void AudioTrackController::clearArrangementEditHistory() noexcept
{
    arrangementEditBatch_.reset();
    arrangementSessionUndoStates_.clear();
    arrangementSessionRedoStates_.clear();
    for (auto& track : tracks_) {
        track->arrangementEditUndoStates.clear();
        track->arrangementEditRedoStates.clear();
    }
}

void AudioTrackController::setTrackEffects(const std::size_t index,
                                            const float driveDb,
                                            const float filterCutoffHz)
{
    if (auto* track = findTrack(index)) {
        track->driveDb = juce::jlimit(0.0f, 36.0f, driveDb);
        track->filterCutoffHz = juce::jlimit(20.0f, 20'000.0f, filterCutoffHz);
        track->gainPanSource.setDrive(track->driveDb);
        track->gainPanSource.setFilterCutoff(track->filterCutoffHz);
    }
}

juce::Result AudioTrackController::setNativeDspGraph(
    const cydaw::engine::NativeDspGraph& graph)
{
    for (auto& track : tracks_) {
        const auto result = track->gainPanSource.setNativeDspGraph(graph);
        if (result.failed())
            return result;
        track->nativeDspGraph = graph;
    }
    defaultNativeDspGraph_ = graph;
    return juce::Result::ok();
}

juce::Result AudioTrackController::setTrackNativeDspGraph(
    const std::size_t index,
    const cydaw::engine::NativeDspGraph& graph)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail("The selected audio track does not exist");

    const auto result = track->gainPanSource.setNativeDspGraph(graph);
    if (result.wasOk())
        track->nativeDspGraph = graph;
    return result;
}

juce::Result AudioTrackController::updateTrackNativeDspParameters(
    const std::size_t index,
    const cydaw::engine::NativeDspGraph& graph)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail("The selected audio track does not exist");

    const auto result = track->gainPanSource.updateNativeDspParameters(graph);
    if (result.wasOk())
        track->nativeDspGraph = graph;
    return result;
}

cydaw::engine::NativeDspGraph AudioTrackController::nativeDspGraph(
    const std::size_t trackIndex) const
{
    const auto* track = findTrack(trackIndex);
    return track != nullptr ? track->nativeDspGraph
                            : cydaw::engine::NativeDspGraph {};
}

std::size_t AudioTrackController::nativeDspNodeCount(
    const std::size_t trackIndex) const noexcept
{
    const auto* track = findTrack(trackIndex);
    return track != nullptr ? track->gainPanSource.nativeDspNodeCount() : 0;
}

std::vector<std::pair<std::uint64_t, float>>
AudioTrackController::trackControlValues(const std::size_t trackIndex) const
{
    if (const auto* track = findTrack(trackIndex))
        return track->gainPanSource.controlValues();
    return {};
}

void AudioTrackController::setTrackNodeTaps(
    const std::size_t trackIndex,
    const std::vector<NativeDspRuntime::TapRequest>& taps)
{
    if (auto* track = findTrack(trackIndex))
        track->gainPanSource.setNodeTaps(taps);
}

void AudioTrackController::setTrackNativeAutomation(const std::size_t trackIndex,
                                                    std::vector<NativeDspRuntime::AutomationLane> lanes)
{
    if (auto* track = findTrack(trackIndex))
        track->gainPanSource.setNativeAutomation(std::move(lanes));
}

bool AudioTrackController::readTrackNodeTap(
    const std::size_t trackIndex, const std::uint64_t nodeId, const bool input,
    cydaw::engine::AudioAnalysisFrame& frame) const
{
    if (const auto* track = findTrack(trackIndex))
        return track->gainPanSource.readNodeTap(nodeId, input, frame);
    return false;
}

bool AudioTrackController::popTrackAnalysisFrame(
    const std::size_t trackIndex,
    cydaw::engine::AudioAnalysisFrame& frame) noexcept
{
    if (auto* track = findTrack(trackIndex))
        return track->gainPanSource.tryPopAnalysisFrame(frame);
    return false;
}

bool AudioTrackController::popMidiTrackAnalysisFrame(
    const std::uint64_t trackId,
    cydaw::engine::AudioAnalysisFrame& frame) noexcept
{
    if (auto* track = findMidiTrack(trackId))
        return track->gainPanSource.tryPopAnalysisFrame(frame);
    return false;
}

bool AudioTrackController::popMasterAnalysisFrame(
    cydaw::engine::AudioAnalysisFrame& frame) noexcept
{
    return masterOutput_.tryPopAnalysisFrame(frame);
}

juce::Result AudioTrackController::normaliseTrackRange(
    const std::size_t index,
    const double sourceStartSeconds,
    const double durationSeconds,
    const float targetPeakDb,
    float* const appliedGainDb)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.normalizeNoTrack"));
    const auto source = track->file;
    const auto pan = track->pan;
    const auto muted = track->muted;
    auto gainDb = 0.0f;
    const auto result = analyseAudioRangeGain(
        source, sourceStartSeconds, durationSeconds, targetPeakDb, gainDb);
    if (result.failed())
        return result;
    track = findTrack(index);
    if (track == nullptr || track->file != source)
        return juce::Result::fail(tr("error.normalizeNoTrack"));
    setTrackMixer(index, gainDb, pan, muted);
    if (appliedGainDb != nullptr)
        *appliedGainDb = gainDb;
    return juce::Result::ok();
}

juce::Result AudioTrackController::exportTrackRange(
    const std::size_t index,
    const double sourceStartSeconds,
    const double durationSeconds,
    const juce::File& destination)
{
    const auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    return exportAudioRange(track->file, sourceStartSeconds, durationSeconds,
                            destination);
}

juce::Result AudioTrackController::analyseAudioRangeGain(
    const juce::File& source,
    const double sourceStartSeconds,
    const double durationSeconds,
    const float targetPeakDb,
    float& gainDb,
    const std::atomic<bool>* const cancellation)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(source));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
        return juce::Result::fail(tr("error.openAudio", "file", source.getFileName()));

    const auto startSample = juce::jlimit<juce::int64>(
        0, reader->lengthInSamples,
        static_cast<juce::int64>(std::llround(std::max(0.0, sourceStartSeconds)
                                              * reader->sampleRate)));
    const auto requestedSamples = static_cast<juce::int64>(std::llround(
        std::max(0.0, durationSeconds) * reader->sampleRate));
    const auto sampleCount = std::min(requestedSamples,
                                      reader->lengthInSamples - startSample);
    if (sampleCount <= 0)
        return juce::Result::fail(tr("error.normalizeEmpty"));

    constexpr int blockSize = 65'536;
    const auto channelCount = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(channelCount, blockSize);
    auto peak = 0.0f;
    juce::int64 readPosition = startSample;
    juce::int64 remaining = sampleCount;
    while (remaining > 0) {
        if (cancellation != nullptr
            && cancellation->load(std::memory_order_acquire))
            return juce::Result::fail(tr("error.audioEditCancelled"));
        const auto currentBlock = static_cast<int>(std::min<juce::int64>(
            remaining, blockSize));
        buffer.clear();
        if (!reader->read(&buffer, 0, currentBlock, readPosition, true, true))
            return juce::Result::fail(tr("error.audioEditRead"));
        for (int channel = 0; channel < channelCount; ++channel)
            peak = std::max(peak, buffer.getMagnitude(channel, 0, currentBlock));
        readPosition += currentBlock;
        remaining -= currentBlock;
    }
    if (peak <= 1.0e-7f)
        return juce::Result::fail(tr("error.normalizeSilent"));

    const auto measuredDb = juce::Decibels::gainToDecibels(peak, -120.0f);
    gainDb = juce::jlimit(-60.0f, 12.0f,
                          juce::jlimit(-12.0f, 0.0f, targetPeakDb)
                              - measuredDb);
    return juce::Result::ok();
}

std::optional<AudioTrackController::TempoDetectionResult>
AudioTrackController::detectAudioTempo(
    const juce::File& source,
    const std::atomic<bool>* const cancellation)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(source));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->numChannels == 0 || reader->lengthInSamples <= 0)
        return std::nullopt;

    constexpr double envelopeRate = 200.0;
    constexpr double maximumAnalysisSeconds = 240.0;
    const auto hopSamples = std::max(
        1, static_cast<int>(std::llround(reader->sampleRate / envelopeRate)));
    const auto analysisSamples = std::min<juce::int64>(
        reader->lengthInSamples,
        static_cast<juce::int64>(std::llround(
            maximumAnalysisSeconds * reader->sampleRate)));
    if (analysisSamples < static_cast<juce::int64>(reader->sampleRate * 2.0))
        return std::nullopt;

    std::vector<float> envelope;
    envelope.reserve(static_cast<std::size_t>(analysisSamples / hopSamples + 1));
    constexpr int blockSize = 32'768;
    juce::AudioBuffer<float> block(
        static_cast<int>(reader->numChannels), blockSize);
    auto accumulated = 0.0;
    auto accumulatedSamples = 0;
    auto readPosition = juce::int64 {};
    while (readPosition < analysisSamples) {
        if (cancellation != nullptr
            && cancellation->load(std::memory_order_acquire))
            return std::nullopt;
        const auto samples = static_cast<int>(std::min<juce::int64>(
            blockSize, analysisSamples - readPosition));
        block.clear();
        if (!reader->read(&block, 0, samples, readPosition, true, true))
            return std::nullopt;
        for (int sample = 0; sample < samples; ++sample) {
            auto mono = 0.0f;
            for (int channel = 0; channel < block.getNumChannels(); ++channel)
                mono += std::abs(block.getSample(channel, sample));
            mono /= static_cast<float>(block.getNumChannels());
            accumulated += static_cast<double>(mono);
            ++accumulatedSamples;
            if (accumulatedSamples >= hopSamples) {
                envelope.push_back(static_cast<float>(
                    accumulated / static_cast<double>(accumulatedSamples)));
                accumulated = 0.0;
                accumulatedSamples = 0;
            }
        }
        readPosition += samples;
    }
    if (envelope.size() < 400)
        return std::nullopt;

    std::vector<float> onset(envelope.size());
    auto previousCompressed = 0.0f;
    auto onsetEnergy = 0.0;
    for (std::size_t index = 0; index < envelope.size(); ++index) {
        const auto compressed = std::sqrt(std::max(0.0f, envelope[index]));
        const auto flux = std::max(0.0f, compressed - previousCompressed);
        onset[index] = flux;
        onsetEnergy += static_cast<double>(flux) * flux;
        previousCompressed = compressed;
    }
    if (onsetEnergy <= 1.0e-10)
        return std::nullopt;

    constexpr double minimumBpm = 55.0;
    constexpr double maximumBpm = 210.0;
    const auto minimumLag = std::max(
        2, static_cast<int>(std::floor(envelopeRate * 60.0 / maximumBpm)));
    const auto maximumLag = std::min(
        static_cast<int>(onset.size() / 3),
        static_cast<int>(std::ceil(envelopeRate * 60.0 / minimumBpm)));
    if (maximumLag <= minimumLag)
        return std::nullopt;

    const auto correlationAt = [&onset](const int lag) {
        if (lag <= 0 || static_cast<std::size_t>(lag) >= onset.size())
            return 0.0;
        auto product = 0.0;
        auto firstEnergy = 0.0;
        auto secondEnergy = 0.0;
        for (std::size_t index = static_cast<std::size_t>(lag);
             index < onset.size(); ++index) {
            const auto first = static_cast<double>(onset[index]);
            const auto second = static_cast<double>(
                onset[index - static_cast<std::size_t>(lag)]);
            product += first * second;
            firstEnergy += first * first;
            secondEnergy += second * second;
        }
        const auto normaliser = std::sqrt(firstEnergy * secondEnergy);
        return normaliser > 1.0e-12 ? product / normaliser : 0.0;
    };

    std::vector<double> scores(static_cast<std::size_t>(maximumLag + 1));
    auto bestLag = minimumLag;
    auto bestScore = -1.0;
    for (int lag = minimumLag; lag <= maximumLag; ++lag) {
        const auto bpm = envelopeRate * 60.0 / static_cast<double>(lag);
        auto score = correlationAt(lag);
        if (lag * 2 <= maximumLag)
            score += 0.18 * correlationAt(lag * 2);
        if (lag / 2 >= minimumLag)
            score += 0.08 * correlationAt(lag / 2);
        if (bpm < 70.0 || bpm > 190.0)
            score *= 0.96;
        scores[static_cast<std::size_t>(lag)] = score;
        if (score > bestScore) {
            bestScore = score;
            bestLag = lag;
        }
    }
    if (bestScore <= 0.01)
        return std::nullopt;

    auto refinedLag = static_cast<double>(bestLag);
    if (bestLag > minimumLag && bestLag < maximumLag) {
        const auto left = scores[static_cast<std::size_t>(bestLag - 1)];
        const auto centre = scores[static_cast<std::size_t>(bestLag)];
        const auto right = scores[static_cast<std::size_t>(bestLag + 1)];
        const auto denominator = left - 2.0 * centre + right;
        if (std::abs(denominator) > 1.0e-9)
            refinedLag += juce::jlimit(-0.5, 0.5,
                                       0.5 * (left - right) / denominator);
    }
    auto bpm = envelopeRate * 60.0 / refinedLag;
    if (bpm < 72.0 && bpm * 2.0 <= maximumBpm)
        bpm *= 2.0;
    if (bpm > 195.0 && bpm * 0.5 >= minimumBpm)
        bpm *= 0.5;
    bpm = std::round(bpm * 10.0) / 10.0;
    return TempoDetectionResult {
        bpm,
        juce::jlimit(0.0, 1.0, bestScore)
    };
}

juce::Result AudioTrackController::exportAudioRange(
    const juce::File& source,
    const double sourceStartSeconds,
    const double durationSeconds,
    const juce::File& destination,
    const std::atomic<bool>* const cancellation)
{
    if (source == juce::File {} || destination == juce::File {}
        || destination == source)
        return juce::Result::fail(tr("error.exportSource"));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(source));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
        return juce::Result::fail(tr("error.openAudio", "file", source.getFileName()));

    const auto startSample = juce::jlimit<juce::int64>(
        0, reader->lengthInSamples,
        static_cast<juce::int64>(std::llround(std::max(0.0, sourceStartSeconds)
                                              * reader->sampleRate)));
    const auto requestedSamples = static_cast<juce::int64>(std::llround(
        std::max(0.0, durationSeconds) * reader->sampleRate));
    const auto sampleCount = std::min(requestedSamples,
                                      reader->lengthInSamples - startSample);
    if (sampleCount <= 0)
        return juce::Result::fail(tr("error.exportEmpty"));
    if (!destination.getParentDirectory().createDirectory())
        return juce::Result::fail(tr("error.exportCreate", "file",
                                     destination.getFullPathName()));

    auto fileOutput = destination.createOutputStream();
    if (fileOutput == nullptr || !fileOutput->setPosition(0)
        || fileOutput->truncate().failed())
        return juce::Result::fail(tr("error.exportCreate", "file",
                                     destination.getFullPathName()));
    std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(
        output,
        juce::AudioFormatWriterOptions()
            .withSampleRate(reader->sampleRate)
            .withNumChannels(static_cast<int>(reader->numChannels))
            .withBitsPerSample(24));
    if (writer == nullptr) {
        destination.deleteFile();
        return juce::Result::fail(tr("error.exportCreate", "file",
                                     destination.getFullPathName()));
    }

    constexpr int blockSize = 8'192;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), blockSize);
    juce::int64 readPosition = startSample;
    juce::int64 remaining = sampleCount;
    while (remaining > 0) {
        if (cancellation != nullptr
            && cancellation->load(std::memory_order_acquire)) {
            writer.reset();
            destination.deleteFile();
            return juce::Result::fail(tr("error.audioEditCancelled"));
        }
        const auto currentBlock = static_cast<int>(std::min<juce::int64>(remaining,
                                                                         blockSize));
        buffer.clear();
        if (!reader->read(&buffer, 0, currentBlock, readPosition, true, true)
            || !writer->writeFromAudioSampleBuffer(buffer, 0, currentBlock)) {
            writer.reset();
            destination.deleteFile();
            return juce::Result::fail(tr("error.exportWrite"));
        }
        readPosition += currentBlock;
        remaining -= currentBlock;
    }
    return juce::Result::ok();
}

juce::Result AudioTrackController::renderAudioEdit(
    const juce::File& source,
    const double sourceStartSeconds,
    const double durationSeconds,
    const AudioEditOperation operation,
    const juce::File& destination,
    const std::atomic<bool>* const cancellation)
{
    const auto defaultLevel = operation == AudioEditOperation::normalisePeak
                                  ? -1.0f
                                  : 0.0f;
    return renderAudioEdit(source, sourceStartSeconds, durationSeconds,
                           AudioEditRequest { operation, defaultLevel }, destination,
                           cancellation);
}

juce::Result AudioTrackController::renderAudioEdit(
    const juce::File& source,
    const double sourceStartSeconds,
    const double durationSeconds,
    const AudioEditRequest request,
    const juce::File& destination,
    const std::atomic<bool>* const cancellation)
{
    if (source == juce::File {} || destination == juce::File {}
        || source == destination)
        return juce::Result::fail(tr("error.audioEditDestination"));
    if (changesLength(request.operation))
        return renderAudioSplice(source, sourceStartSeconds, durationSeconds, request, destination, cancellation);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(source));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
        return juce::Result::fail(tr("error.openAudio", "file",
                                     source.getFileName()));

    const auto selectionStart = juce::jlimit<juce::int64>(
        0, reader->lengthInSamples,
        static_cast<juce::int64>(std::llround(
            std::max(0.0, sourceStartSeconds) * reader->sampleRate)));
    const auto requestedSamples = static_cast<juce::int64>(std::llround(
        std::max(0.0, durationSeconds) * reader->sampleRate));
    const auto selectionLength = std::min(
        requestedSamples, reader->lengthInSamples - selectionStart);
    if (selectionLength <= 0)
        return juce::Result::fail(tr("error.audioEditEmpty"));
    const auto selectionEnd = selectionStart + selectionLength;
    const auto channelCount = static_cast<int>(reader->numChannels);

    if (request.operation == AudioEditOperation::swapStereo
        && channelCount < 2)
        return juce::Result::fail(tr("error.audioEditStereoRequired"));

    std::vector<double> dcOffsets(static_cast<std::size_t>(channelCount), 0.0);
    auto renderedGain = 1.0f;
    if (request.operation == AudioEditOperation::amplify) {
        renderedGain = juce::Decibels::decibelsToGain(
            juce::jlimit(-60.0f, 24.0f, request.levelDb), -60.0f);
    } else if (request.operation == AudioEditOperation::normalisePeak
               || request.operation == AudioEditOperation::removeDcOffset) {
        constexpr int analysisBlockSize = 65'536;
        juce::AudioBuffer<float> analysisBuffer(channelCount,
                                                analysisBlockSize);
        auto peak = 0.0f;
        juce::int64 readPosition = selectionStart;
        juce::int64 remaining = selectionLength;
        while (remaining > 0) {
            if (cancellation != nullptr
                && cancellation->load(std::memory_order_acquire))
                return juce::Result::fail(tr("error.audioEditCancelled"));
            const auto currentBlock = static_cast<int>(std::min<juce::int64>(
                remaining, analysisBlockSize));
            analysisBuffer.clear();
            if (!reader->read(&analysisBuffer, 0, currentBlock, readPosition,
                              true, true))
                return juce::Result::fail(tr("error.audioEditRead"));
            for (int channel = 0; channel < channelCount; ++channel) {
                if (request.operation == AudioEditOperation::normalisePeak) {
                    peak = std::max(
                        peak,
                        analysisBuffer.getMagnitude(channel, 0, currentBlock));
                } else {
                    auto sum = 0.0;
                    const auto* samples = analysisBuffer.getReadPointer(channel);
                    for (int sample = 0; sample < currentBlock; ++sample)
                        sum += samples[sample];
                    dcOffsets[static_cast<std::size_t>(channel)] += sum;
                }
            }
            readPosition += currentBlock;
            remaining -= currentBlock;
        }
        if (request.operation == AudioEditOperation::normalisePeak) {
            if (peak <= 1.0e-7f)
                return juce::Result::fail(tr("error.normalizeSilent"));
            const auto targetDb = juce::jlimit(-24.0f, 0.0f,
                                                request.levelDb);
            const auto measuredDb = juce::Decibels::gainToDecibels(
                peak, -120.0f);
            renderedGain = juce::Decibels::decibelsToGain(
                juce::jlimit(-60.0f, 24.0f, targetDb - measuredDb), -60.0f);
        } else {
            for (auto& offset : dcOffsets)
                offset /= static_cast<double>(selectionLength);
        }
    }

    if (!destination.getParentDirectory().createDirectory())
        return juce::Result::fail(tr("error.exportCreate", "file",
                                     destination.getFullPathName()));
    auto fileOutput = destination.createOutputStream();
    if (fileOutput == nullptr || !fileOutput->setPosition(0)
        || fileOutput->truncate().failed())
        return juce::Result::fail(tr("error.exportCreate", "file",
                                     destination.getFullPathName()));
    std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(
        output,
        juce::AudioFormatWriterOptions()
            .withSampleRate(reader->sampleRate)
            .withNumChannels(static_cast<int>(reader->numChannels))
            .withBitsPerSample(24));
    if (writer == nullptr) {
        destination.deleteFile();
        return juce::Result::fail(tr("error.exportCreate", "file",
                                     destination.getFullPathName()));
    }

    constexpr int blockSize = 8'192;
    juce::AudioBuffer<float> buffer(channelCount, blockSize);
    juce::AudioBuffer<float> reverseBuffer(channelCount, blockSize);
    const auto fadeDenominator = static_cast<double>(
        std::max<juce::int64>(1, selectionLength - 1));

    for (juce::int64 blockStart = 0; blockStart < reader->lengthInSamples;
         blockStart += blockSize) {
        if (cancellation != nullptr
            && cancellation->load(std::memory_order_acquire)) {
            writer.reset();
            destination.deleteFile();
            return juce::Result::fail(tr("error.audioEditCancelled"));
        }

        const auto currentBlock = static_cast<int>(std::min<juce::int64>(
            blockSize, reader->lengthInSamples - blockStart));
        buffer.clear();
        if (!reader->read(&buffer, 0, currentBlock, blockStart, true, true)) {
            writer.reset();
            destination.deleteFile();
            return juce::Result::fail(tr("error.audioEditRead"));
        }

        const auto overlapStart = std::max(blockStart, selectionStart);
        const auto overlapEnd = std::min(blockStart + currentBlock, selectionEnd);
        if (overlapEnd > overlapStart) {
            const auto destinationOffset = static_cast<int>(overlapStart - blockStart);
            const auto overlapLength = static_cast<int>(overlapEnd - overlapStart);
            if (request.operation == AudioEditOperation::silence) {
                for (int channel = 0; channel < channelCount; ++channel)
                    buffer.clear(channel, destinationOffset, overlapLength);
            } else if (request.operation == AudioEditOperation::reverse) {
                const auto reverseReadStart = selectionStart
                                              + (selectionEnd - overlapEnd);
                reverseBuffer.clear();
                if (!reader->read(&reverseBuffer, 0, overlapLength,
                                  reverseReadStart, true, true)) {
                    writer.reset();
                    destination.deleteFile();
                    return juce::Result::fail(tr("error.audioEditRead"));
                }
                for (int channel = 0; channel < channelCount; ++channel)
                    for (int sample = 0; sample < overlapLength; ++sample)
                        buffer.setSample(
                            channel, destinationOffset + sample,
                            reverseBuffer.getSample(channel,
                                                    overlapLength - sample - 1));
            } else if (request.operation == AudioEditOperation::fadeIn
                       || request.operation == AudioEditOperation::fadeOut) {
                for (int sample = 0; sample < overlapLength; ++sample) {
                    const auto selectionPosition = static_cast<double>(
                        overlapStart + sample - selectionStart);
                    const auto amount = selectionPosition / fadeDenominator;
                    const auto rising = request.operation == AudioEditOperation::fadeIn ? amount : 1.0 - amount;
                    // The shape: straight, equal power, slow start, fast start.
                    const auto shaped = request.curve == FadeCurve::equalPower ? std::sin(rising * juce::MathConstants<double>::halfPi)
                                        : request.curve == FadeCurve::slowStart ? rising * rising
                                        : request.curve == FadeCurve::fastStart ? 1.0 - (1.0 - rising) * (1.0 - rising)
                                                                                : rising;
                    const auto gain = static_cast<float>(shaped);
                    for (int channel = 0; channel < channelCount; ++channel)
                        buffer.setSample(
                            channel, destinationOffset + sample,
                            buffer.getSample(channel, destinationOffset + sample)
                                * gain);
                }
            } else if (request.operation == AudioEditOperation::swapStereo) {
                for (int sample = 0; sample < overlapLength; ++sample) {
                    const auto position = destinationOffset + sample;
                    const auto left = buffer.getSample(0, position);
                    buffer.setSample(0, position,
                                     buffer.getSample(1, position));
                    buffer.setSample(1, position, left);
                }
            } else {
                for (int channel = 0; channel < channelCount; ++channel) {
                    const auto offset = static_cast<float>(
                        dcOffsets[static_cast<std::size_t>(channel)]);
                    auto* samples = buffer.getWritePointer(channel,
                                                           destinationOffset);
                    for (int sample = 0; sample < overlapLength; ++sample) {
                        auto value = samples[sample];
                        if (request.operation
                            == AudioEditOperation::invertPolarity)
                            value = -value;
                        else if (request.operation
                                 == AudioEditOperation::removeDcOffset)
                            value -= offset;
                        else
                            value *= renderedGain;
                        samples[sample] = juce::jlimit(-1.0f, 1.0f, value);
                    }
                }
            }
        }

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, currentBlock)) {
            writer.reset();
            destination.deleteFile();
            return juce::Result::fail(tr("error.exportWrite"));
        }
    }

    return juce::Result::ok();
}

// ---- Destructive edits that change the length, takes, comping, versions ------------------------

juce::Result AudioTrackController::renderAudioSplice(
    const juce::File& source,
    const double sourceStartSeconds,
    const double durationSeconds,
    const AudioEditRequest& request,
    const juce::File& destination,
    const std::atomic<bool>* const cancellation)
{
    if (source == juce::File {} || destination == juce::File {} || source == destination)
        return juce::Result::fail(tr("error.audioEditDestination"));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
        return juce::Result::fail(tr("error.openAudio", "file", source.getFileName()));
    const auto rate = reader->sampleRate;
    const auto length = reader->lengthInSamples;
    const auto channels = static_cast<int>(reader->numChannels);
    const auto a = juce::jlimit<juce::int64>(0, length, static_cast<juce::int64>(std::llround(std::max(0.0, sourceStartSeconds) * rate)));
    auto selection = static_cast<juce::int64>(std::llround(std::max(0.0, durationSeconds) * rate));
    if (request.operation != AudioEditOperation::insertSilence && request.operation != AudioEditOperation::paste)
        selection = std::min(selection, length - a);
    if (selection <= 0 && request.operation != AudioEditOperation::paste)
        return juce::Result::fail(tr("error.audioEditEmpty"));
    const auto b = std::min(length, a + selection);

    // What is pasted: read whole (a clipboard is short), its channels spread
    // onto the file's.
    juce::AudioBuffer<float> pasted;
    if (request.operation == AudioEditOperation::paste) {
        const std::unique_ptr<juce::AudioFormatReader> from(formats.createReaderFor(request.pasteFile));
        if (from == nullptr || from->sampleRate <= 0.0)
            return juce::Result::fail(tr("error.openAudio", "file", request.pasteFile.getFileName()));
        if (std::abs(from->sampleRate - rate) > 0.5)
            return juce::Result::fail("The pasted sound is at another sample rate (" + juce::String(from->sampleRate, 0) + " Hz)");
        const auto start = juce::jlimit<juce::int64>(0, from->lengthInSamples,
                                                     static_cast<juce::int64>(std::llround(request.pasteStartSeconds * rate)));
        const auto count = static_cast<int>(juce::jlimit<juce::int64>(
            0, std::min<juce::int64>(from->lengthInSamples - start, static_cast<juce::int64>(rate * 600.0)),
            static_cast<juce::int64>(std::llround(request.pasteDurationSeconds * rate))));
        if (count <= 0)
            return juce::Result::fail(tr("error.audioEditEmpty"));
        juce::AudioBuffer<float> read(static_cast<int>(from->numChannels), count);
        (void) from->read(&read, 0, count, start, true, true);
        pasted.setSize(channels, count);
        for (int channel = 0; channel < channels; ++channel)
            pasted.copyFrom(channel, 0, read, std::min(channel, read.getNumChannels() - 1), 0, count);
    }

    if (!destination.getParentDirectory().createDirectory())
        return juce::Result::fail(tr("error.exportCreate", "file", destination.getFullPathName()));
    auto fileOutput = destination.createOutputStream();
    if (fileOutput == nullptr || !fileOutput->setPosition(0) || fileOutput->truncate().failed())
        return juce::Result::fail(tr("error.exportCreate", "file", destination.getFullPathName()));
    std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(
        output, juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(channels).withBitsPerSample(24));
    if (writer == nullptr) {
        destination.deleteFile();
        return juce::Result::fail(tr("error.exportCreate", "file", destination.getFullPathName()));
    }
    // The pieces written in turn, each with a short fade where it meets
    // another (no click at a join).
    const auto fade = static_cast<int>(std::max(8.0, rate * 0.0015));
    constexpr int blockSize = 8'192;
    juce::AudioBuffer<float> buffer(channels, blockSize);
    const auto copy = [&](const juce::int64 from, const juce::int64 to, const bool fadeInAtStart, const bool fadeOutAtEnd) {
        for (auto start = from; start < to; start += blockSize) {
            if (cancellation != nullptr && cancellation->load(std::memory_order_acquire))
                return false;
            const auto count = static_cast<int>(std::min<juce::int64>(blockSize, to - start));
            buffer.clear();
            if (!reader->read(&buffer, 0, count, start, true, true))
                return false;
            for (int i = 0; i < count; ++i) {
                const auto at = start + i;
                auto gain = 1.0f;
                if (fadeInAtStart && at - from < fade)
                    gain *= std::sin(static_cast<float>(at - from) / static_cast<float>(fade) * juce::MathConstants<float>::halfPi);
                if (fadeOutAtEnd && to - at <= fade)
                    gain *= std::sin(static_cast<float>(to - at - 1) / static_cast<float>(fade) * juce::MathConstants<float>::halfPi);
                if (gain < 1.0f)
                    for (int channel = 0; channel < channels; ++channel)
                        buffer.setSample(channel, i, buffer.getSample(channel, i) * gain);
            }
            if (!writer->writeFromAudioSampleBuffer(buffer, 0, count))
                return false;
        }
        return true;
    };
    const auto silence = [&](juce::int64 count) {
        buffer.clear();
        while (count > 0) {
            const auto block = static_cast<int>(std::min<juce::int64>(blockSize, count));
            if (!writer->writeFromAudioSampleBuffer(buffer, 0, block))
                return false;
            count -= block;
        }
        return true;
    };
    auto written = true;
    switch (request.operation) {
    case AudioEditOperation::cut:
        written = copy(0, a, false, a > 0 && b < length) && copy(b, length, a > 0 && b < length, false);
        break;
    case AudioEditOperation::crop:
        written = copy(a, b, a > 0, b < length);
        break;
    case AudioEditOperation::insertSilence:
        written = copy(0, a, false, a > 0) && silence(selection) && copy(a, length, a < length, false);
        break;
    case AudioEditOperation::paste: {
        written = copy(0, a, false, a > 0);
        if (written) {
            // The pasted sound faded at both ends.
            const auto count = pasted.getNumSamples();
            for (int i = 0; i < std::min(fade, count); ++i) {
                const auto gain = std::sin(static_cast<float>(i) / static_cast<float>(fade) * juce::MathConstants<float>::halfPi);
                for (int channel = 0; channel < channels; ++channel) {
                    pasted.setSample(channel, i, pasted.getSample(channel, i) * gain);
                    pasted.setSample(channel, count - 1 - i, pasted.getSample(channel, count - 1 - i) * gain);
                }
            }
            written = writer->writeFromAudioSampleBuffer(pasted, 0, count) && copy(a, length, a < length, false);
        }
        break;
    }
    default:
        written = false;
    }
    writer.reset();
    if (!written) {
        destination.deleteFile();
        return juce::Result::fail(cancellation != nullptr && cancellation->load() ? tr("error.audioEditCancelled")
                                                                                   : tr("error.exportWrite"));
    }
    return juce::Result::ok();
}

double AudioTrackController::clipSourceTime(const AudioClip& clip, const double timelineSeconds)
{
    return sourceTimeForClipTimeline(clip, timelineSeconds);
}

double AudioTrackController::eventSourceTime(const AudioEvent& event, const double timelineSeconds)
{
    AudioClip mapping;
    mapping.durationSeconds = event.durationSeconds;
    mapping.sourceOffsetSeconds = event.sourceOffsetSeconds;
    mapping.sourceDurationSeconds = event.sourceDurationSeconds;
    mapping.stretchMarkers = event.stretchMarkers;
    return sourceTimeForClipTimeline(mapping, timelineSeconds);
}

juce::Result AudioTrackController::renderAudioClipComp(const AudioClip& clip, const juce::File& destination,
                                                       const std::atomic<bool>* const cancellation)
{
    if (clip.takes.size() < 2 || clip.comp.empty())
        return juce::Result::fail("The clip plays one take: there is nothing to join");
    if (destination == juce::File {})
        return juce::Result::fail(tr("error.audioEditDestination"));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    // Every take's file, at one rate; the widest gives the file its channels.
    std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;
    auto rate = 0.0;
    auto channels = 1;
    for (const auto& take : clip.takes) {
        const auto file = take.sourceFile != juce::File {} ? take.sourceFile : clip.sourceFile;
        if (file == destination)
            return juce::Result::fail(tr("error.audioEditDestination"));
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
            return juce::Result::fail(tr("error.openAudio", "file", file.getFileName()));
        if (rate <= 0.0)
            rate = reader->sampleRate;
        else if (std::abs(reader->sampleRate - rate) > 0.5)
            return juce::Result::fail("The takes are at different sample rates (" + juce::String(rate, 0) + " and "
                                      + juce::String(reader->sampleRate, 0) + " Hz): join them in one rate first");
        channels = std::max(channels, static_cast<int>(reader->numChannels));
        readers.push_back(std::move(reader));
    }
    channels = std::min(channels, 8);
    // On the clip's own source time, as long as its source, or as far as it
    // plays.
    const auto lengthSeconds = std::max(clip.sourceLengthSeconds, clip.sourceOffsetSeconds + clip.sourceDurationSeconds);
    const auto length = static_cast<juce::int64>(std::llround(lengthSeconds * rate));
    if (length <= 0)
        return juce::Result::fail(tr("error.audioEditEmpty"));

    // The passages in source time, each heard between its ends; where two
    // meet they cross over 10 ms of the clip's time, as when it plays. The
    // first reaches back to the file's start when it starts the clip, the
    // last on to its end when it ends it.
    struct Span {
        double from {};
        double to {};
        bool open[2] {};
        std::size_t take {};
        juce::int64 shift {};
    };
    const auto speed = clip.sourceDurationSeconds / std::max(1.0e-6, clip.durationSeconds);
    const auto crossfade = 0.01 * speed * rate;
    std::vector<CompSegment> comp = clip.comp;
    std::sort(comp.begin(), comp.end(), [](const auto& left, const auto& right) { return left.startSeconds < right.startSeconds; });
    std::vector<Span> spans;
    for (std::size_t index = 0; index < comp.size(); ++index) {
        const auto& part = comp[index];
        if (part.endSeconds - part.startSeconds < 0.0005)
            continue;
        Span span;
        span.take = std::min(part.take, clip.takes.size() - 1);
        span.from = clipSourceTime(clip, part.startSeconds) * rate - crossfade * 0.5;
        span.to = clipSourceTime(clip, part.endSeconds) * rate + crossfade * 0.5;
        span.open[0] = index == 0 && part.startSeconds <= 1.0e-6;
        span.open[1] = index + 1 == comp.size() && part.endSeconds >= clip.durationSeconds - 1.0e-6;
        span.shift = static_cast<juce::int64>(std::llround(clip.takes[span.take].shiftSeconds * rate));
        spans.push_back(span);
    }
    const auto weight = [crossfade](const Span& span, const double at) {
        auto gain = 1.0;
        if (!span.open[0])
            gain *= std::clamp((at - span.from) / std::max(1.0, crossfade), 0.0, 1.0);
        if (!span.open[1])
            gain *= std::clamp((span.to - at) / std::max(1.0, crossfade), 0.0, 1.0);
        return gain;
    };

    if (!destination.getParentDirectory().createDirectory())
        return juce::Result::fail(tr("error.exportCreate", "file", destination.getFullPathName()));
    auto fileOutput = destination.createOutputStream();
    if (fileOutput == nullptr || !fileOutput->setPosition(0) || fileOutput->truncate().failed())
        return juce::Result::fail(tr("error.exportCreate", "file", destination.getFullPathName()));
    std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(
        output, juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(channels).withBitsPerSample(24));
    if (writer == nullptr) {
        destination.deleteFile();
        return juce::Result::fail(tr("error.exportCreate", "file", destination.getFullPathName()));
    }
    constexpr int blockSize = 8'192;
    juce::AudioBuffer<float> mix(channels, blockSize);
    juce::AudioBuffer<float> read(8, blockSize);
    auto written = true;
    for (juce::int64 start = 0; start < length && written; start += blockSize) {
        if (cancellation != nullptr && cancellation->load(std::memory_order_acquire)) {
            written = false;
            break;
        }
        const auto count = static_cast<int>(std::min<juce::int64>(blockSize, length - start));
        mix.clear();
        for (const auto& span : spans) {
            if (!span.open[1] && static_cast<double>(start) >= span.to)
                continue;
            if (!span.open[0] && static_cast<double>(start + count) <= span.from)
                continue;
            auto& reader = *readers[span.take];
            const auto width = static_cast<int>(std::min<unsigned int>(8, reader.numChannels));
            read.clear();
            if (!reader.read(&read, 0, count, start + span.shift, true, true)) {
                written = false;
                break;
            }
            for (int channel = 0; channel < channels; ++channel) {
                const auto* from = read.getReadPointer(std::min(channel, width - 1));
                auto* to = mix.getWritePointer(channel);
                for (int i = 0; i < count; ++i)
                    to[i] += static_cast<float>(weight(span, static_cast<double>(start + i))) * from[i];
            }
        }
        written = written && writer->writeFromAudioSampleBuffer(mix, 0, count);
    }
    writer.reset();
    if (!written) {
        destination.deleteFile();
        return juce::Result::fail(cancellation != nullptr && cancellation->load() ? tr("error.audioEditCancelled")
                                                                                   : tr("error.exportWrite"));
    }
    return juce::Result::ok();
}

juce::Result AudioTrackController::commitAudioClipCompFlatten(const std::size_t index, const std::uint64_t clipId,
                                                              const juce::File& renderedFile, const juce::String& label)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    const auto found = std::find_if(track->audioClips.begin(), track->audioClips.end(),
                                    [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return juce::Result::fail(tr("error.audioTakeClip"));
    const std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(renderedFile));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return juce::Result::fail(tr("error.openAudio", "file", renderedFile.getFileName()));
    const auto newLength = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    auto previous = track->audioClips;
    const auto clipBefore = *found;
    auto& clip = *found;
    if (clip.versions.empty())
        clip.versions.push_back({ "Original", clip.sourceFile, clip.sourceOffsetSeconds, clip.durationSeconds,
                                  clip.sourceDurationSeconds, clip.sourceLengthSeconds });
    clip.versions.resize(std::min(clip.versions.size(), clip.version + 1));
    // One take, the joined one, on the same time as the clip's source: the
    // clip plays the same moments of it.
    clip.sourceFile = renderedFile;
    clip.sourceLengthSeconds = newLength;
    clip.takes = { AudioTake { label, renderedFile, newLength, 0.0 } };
    clip.activeTakeIndex = 0;
    clip.comp.clear();
    clip.versions.push_back({ label, renderedFile, clip.sourceOffsetSeconds, clip.durationSeconds, clip.sourceDurationSeconds, newLength });
    clip.version = clip.versions.size() - 1;
    if (const auto result = rebuildArrangementPlan(*track); result.failed()) {
        clip = clipBefore;
        (void) rebuildArrangementPlan(*track);
        return result;
    }
    recordArrangementEdit(*track, std::move(previous));
    return juce::Result::ok();
}

juce::String AudioTrackController::audioClipEditProblem(const std::size_t index, const std::uint64_t clipId,
                                                        const AudioEditOperation operation) const
{
    const auto* track = findTrack(index);
    if (track == nullptr)
        return tr("error.exportNoTrack");
    const auto found = std::find_if(track->audioClips.begin(), track->audioClips.end(),
                                    [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return tr("error.audioTakeClip");
    if (!found->events.empty() || found->usesEventTimeline)
        return "This clip is made of events: edit each event";
    if (changesLength(operation)) {
        if (!found->stretchMarkers.empty())
            return "Remove the clip's warp markers first: cutting would move them";
        if (!found->comp.empty())
            return "The clip plays several takes: choose one take first";
    }
    return {};
}

juce::Result AudioTrackController::commitAudioClipSourceEdit(
    const std::size_t index,
    const std::uint64_t clipId,
    const juce::File& renderedFile,
    const juce::String& label,
    const AudioEditOperation operation,
    const double selectionStartSeconds,
    const double selectionSeconds)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    if (const auto problem = audioClipEditProblem(index, clipId, operation); problem.isNotEmpty())
        return juce::Result::fail(problem);
    const auto found = std::find_if(track->audioClips.begin(), track->audioClips.end(),
                                    [clipId](const auto& clip) { return clip.id == clipId; });
    const std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(renderedFile));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return juce::Result::fail(tr("error.openAudio", "file", renderedFile.getFileName()));
    const auto newLength = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;

    auto previous = track->audioClips;
    const auto clipBefore = *found;
    auto& clip = *found;
    // Its history: the original first, the versions after the one played
    // dropped (a new branch), this one added and played.
    if (clip.versions.empty())
        clip.versions.push_back({ "Original", clip.sourceFile, clip.sourceOffsetSeconds, clip.durationSeconds,
                                  clip.sourceDurationSeconds, clip.sourceLengthSeconds });
    clip.versions.resize(std::min(clip.versions.size(), clip.version + 1));
    // The clip keeps playing what it played around the edit.
    const auto speed = clip.sourceDurationSeconds / std::max(1.0e-6, clip.durationSeconds);
    auto start = clip.sourceOffsetSeconds;
    auto end = clip.sourceOffsetSeconds + clip.sourceDurationSeconds;
    const auto a = std::max(0.0, selectionStartSeconds);
    const auto length = std::max(0.0, selectionSeconds);
    switch (operation) {
    case AudioEditOperation::cut: {
        const auto map = [a, length](const double x) { return x < a ? x : (x < a + length ? a : x - length); };
        start = map(start);
        end = map(end);
        break;
    }
    case AudioEditOperation::crop:
        start = juce::jlimit(0.0, length, start - a);
        end = juce::jlimit(0.0, length, end - a);
        break;
    case AudioEditOperation::insertSilence:
    case AudioEditOperation::paste: {
        const auto inserted = newLength - clip.sourceLengthSeconds;
        if (a <= start && !(a >= start && a <= end))
            start += inserted;
        if (a <= end)
            end += inserted;
        break;
    }
    default:
        break;
    }
    if (end - start < 0.001) {
        clip = clipBefore;
        return juce::Result::fail("Nothing of the clip would be left");
    }
    clip.sourceFile = renderedFile;
    if (!clip.takes.empty())
        clip.takes[std::min(clip.activeTakeIndex, clip.takes.size() - 1)].sourceFile = renderedFile;
    clip.sourceOffsetSeconds = start;
    clip.sourceDurationSeconds = end - start;
    clip.durationSeconds = (end - start) / std::max(1.0e-6, speed);
    clip.sourceLengthSeconds = newLength;
    if (!clip.takes.empty())
        clip.takes[std::min(clip.activeTakeIndex, clip.takes.size() - 1)].sourceLengthSeconds = newLength;
    clip.fadeInSeconds = std::min(clip.fadeInSeconds, clip.durationSeconds);
    clip.fadeOutSeconds = std::min(clip.fadeOutSeconds, clip.durationSeconds);
    clip.versions.push_back({ label, renderedFile, clip.sourceOffsetSeconds, clip.durationSeconds, clip.sourceDurationSeconds, newLength });
    clip.version = clip.versions.size() - 1;
    if (const auto result = rebuildArrangementPlan(*track); result.failed()) {
        clip = clipBefore;
        (void) rebuildArrangementPlan(*track);
        return result;
    }
    recordArrangementEdit(*track, std::move(previous));
    return juce::Result::ok();
}

bool AudioTrackController::setAudioClipVersion(const std::size_t index, const std::uint64_t clipId, const std::size_t version)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(track->audioClips.begin(), track->audioClips.end(),
                                    [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end() || version >= found->versions.size())
        return false;
    if (found->version == version)
        return true;
    auto previous = track->audioClips;
    const auto clipBefore = *found;
    const auto& chosen = found->versions[version];
    found->sourceFile = chosen.file;
    if (!found->takes.empty()) {
        auto& take = found->takes[std::min(found->activeTakeIndex, found->takes.size() - 1)];
        take.sourceFile = chosen.file;
        take.sourceLengthSeconds = chosen.sourceLengthSeconds;
    }
    found->sourceOffsetSeconds = chosen.sourceOffsetSeconds;
    found->durationSeconds = chosen.durationSeconds;
    found->sourceDurationSeconds = chosen.sourceDurationSeconds;
    found->sourceLengthSeconds = chosen.sourceLengthSeconds;
    found->fadeInSeconds = std::min(found->fadeInSeconds, found->durationSeconds);
    found->fadeOutSeconds = std::min(found->fadeOutSeconds, found->durationSeconds);
    found->version = version;
    if (rebuildArrangementPlan(*track).failed()) {
        *found = clipBefore;
        (void) rebuildArrangementPlan(*track);
        return false;
    }
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

void AudioTrackController::restoreAudioClipHistory(const std::size_t index, const std::uint64_t clipId,
                                                   std::vector<SourceVersion> versions, const std::size_t version)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return;
    const auto found = std::find_if(track->audioClips.begin(), track->audioClips.end(),
                                    [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return;
    found->versions = std::move(versions);
    found->version = found->versions.empty() ? 0 : std::min(version, found->versions.size() - 1);
}

bool AudioTrackController::setAudioClipPitch(const std::size_t index, const std::uint64_t clipId, const double semitones)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(track->audioClips.begin(), track->audioClips.end(),
                                    [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return false;
    const auto pitch = juce::jlimit(-48.0, 48.0, semitones);
    if (std::abs(found->pitchSemitones - pitch) < 1.0e-9)
        return true;
    auto previous = track->audioClips;
    found->pitchSemitones = pitch;
    for (auto& event : found->events)
        event.pitchSemitones = pitch;
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = previous;
        (void) rebuildArrangementPlan(*track);
        return false;
    }
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

bool AudioTrackController::setAudioClipComp(const std::size_t index, const std::uint64_t clipId, std::vector<CompSegment> comp)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(track->audioClips.begin(), track->audioClips.end(),
                                    [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return false;
    // In order, inside the clip, each passage from a take it has; neighbours
    // from the same take joined.
    std::sort(comp.begin(), comp.end(), [](const auto& left, const auto& right) { return left.startSeconds < right.startSeconds; });
    std::vector<CompSegment> clean;
    for (auto segment : comp) {
        segment.startSeconds = juce::jlimit(0.0, found->durationSeconds, segment.startSeconds);
        segment.endSeconds = juce::jlimit(segment.startSeconds, found->durationSeconds, segment.endSeconds);
        if (segment.endSeconds - segment.startSeconds < 0.001 || segment.take >= std::max<std::size_t>(1, found->takes.size()))
            continue;
        if (!clean.empty() && segment.startSeconds < clean.back().endSeconds)
            segment.startSeconds = clean.back().endSeconds;
        if (segment.endSeconds - segment.startSeconds < 0.001)
            continue;
        if (!clean.empty() && clean.back().take == segment.take && std::abs(clean.back().endSeconds - segment.startSeconds) < 1.0e-6)
            clean.back().endSeconds = segment.endSeconds;
        else
            clean.push_back(segment);
    }
    // One passage of one take over the whole clip: that take, active.
    if (clean.size() == 1 && clean.front().startSeconds <= 1.0e-6 && clean.front().endSeconds >= found->durationSeconds - 1.0e-6) {
        const auto take = clean.front().take;
        clean.clear();
        if (take != found->activeTakeIndex && take < found->takes.size()) {
            found->activeTakeIndex = take;
            found->sourceFile = found->takes[take].sourceFile;
            found->sourceLengthSeconds = found->takes[take].sourceLengthSeconds;
        }
    }
    if (clean == found->comp)
        return true;
    auto previous = track->audioClips;
    found->comp = std::move(clean);
    if (rebuildArrangementPlan(*track).failed()) {
        track->audioClips = previous;
        (void) rebuildArrangementPlan(*track);
        return false;
    }
    recordArrangementEdit(*track, std::move(previous));
    return true;
}

juce::Result AudioTrackController::addAudioTakes(const std::size_t index, const std::uint64_t clipId, std::vector<AudioTake> takes,
                                                 const bool activateLast)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.recordedTakeTrack"));
    const auto found = std::find_if(track->audioClips.begin(), track->audioClips.end(),
                                    [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end())
        return juce::Result::fail(tr("error.audioTakeClip"));
    if (takes.empty())
        return juce::Result::ok();
    for (auto& take : takes) {
        const std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(take.sourceFile));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return juce::Result::fail(tr("error.openAudio", "file", take.sourceFile.getFileName()));
        take.sourceLengthSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        if (take.name.isEmpty())
            take.name = take.sourceFile.getFileNameWithoutExtension();
    }
    auto previous = track->audioClips;
    const auto clipBefore = *found;
    // The clip's own source as its first take.
    if (found->takes.empty())
        found->takes.push_back({ found->name.isNotEmpty() ? found->name : found->sourceFile.getFileNameWithoutExtension(),
                                 found->sourceFile, found->sourceLengthSeconds, 0.0 });
    for (auto& take : takes) {
        if (found->takes.size() >= maximumAudioTakesPerClip)
            break;
        found->takes.push_back(std::move(take));
    }
    if (activateLast) {
        found->activeTakeIndex = found->takes.size() - 1;
        found->comp.clear();
    }
    if (const auto result = rebuildArrangementPlan(*track); result.failed()) {
        *found = clipBefore;
        (void) rebuildArrangementPlan(*track);
        return result;
    }
    recordArrangementEdit(*track, std::move(previous));
    return juce::Result::ok();
}

juce::Result AudioTrackController::commitTrackAudioEdit(
    const std::size_t index,
    const juce::File& renderedFile)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    if (!renderedFile.existsAsFile() || renderedFile == track->file)
        return juce::Result::fail(tr("error.audioEditDestination"));
    auto firstReader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(renderedFile));
    auto secondReader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(renderedFile));
    if (firstReader == nullptr || secondReader == nullptr
        || firstReader->sampleRate <= 0.0 || firstReader->numChannels == 0)
        return juce::Result::fail(tr("error.openAudio", "file",
                                     renderedFile.getFileName()));

    static constexpr std::size_t maximumHistorySize = 32;
    track->arrangementEditUndoStates.clear();
    track->arrangementEditRedoStates.clear();
    track->clipAudioEditUndoStates.clear();
    track->clipAudioEditRedoStates.clear();
    const auto previousSource = track->file;
    track->audioEditUndoSources.push_back(previousSource);
    if (track->audioEditUndoSources.size() > maximumHistorySize)
        track->audioEditUndoSources.erase(track->audioEditUndoSources.begin());
    track->audioEditRedoSources.clear();
    track->replaceSource(std::move(firstReader), std::move(secondReader),
                         renderedFile, readAheadThread_);
    for (auto& clip : track->audioClips) {
        for (auto& take : clip.takes)
            if (take.sourceFile == previousSource)
                take.sourceFile = renderedFile;
        if (clip.sourceFile == previousSource)
            clip.sourceFile = renderedFile;
    }
    for (auto& clip : track->launcherClips)
        if (clip.sourceFile == previousSource)
            clip.sourceFile = renderedFile;
    const auto arrangementResult = rebuildArrangementPlan(*track);
    return arrangementResult.failed() ? arrangementResult
                                      : rebuildLauncherPlans(*track);
}

juce::Result AudioTrackController::commitAudioClipEdit(
    const std::size_t index,
    const std::uint64_t clipId,
    const juce::File& renderedFile)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    const auto found = std::find_if(
        track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& clip) { return clip.id == clipId; });
    if (found == track->audioClips.end() || !renderedFile.existsAsFile()
        || renderedFile == found->sourceFile)
        return juce::Result::fail(tr("error.audioEditDestination"));
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(renderedFile));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0)
        return juce::Result::fail(tr("error.openAudio", "file",
                                     renderedFile.getFileName()));

    const auto previous = *found;
    found->sourceFile = renderedFile;
    if (!found->takes.empty())
        found->takes[std::min(found->activeTakeIndex,
                              found->takes.size() - 1)].sourceFile
            = renderedFile;
    if (const auto result = rebuildArrangementPlan(*track); result.failed()) {
        *found = previous;
        (void) rebuildArrangementPlan(*track);
        return result;
    }
    static constexpr std::size_t maximumHistorySize = 32;
    track->arrangementEditUndoStates.clear();
    track->arrangementEditRedoStates.clear();
    track->audioEditUndoSources.clear();
    track->audioEditRedoSources.clear();
    track->clipAudioEditUndoStates.push_back({ previous });
    if (track->clipAudioEditUndoStates.size() > maximumHistorySize)
        track->clipAudioEditUndoStates.erase(
            track->clipAudioEditUndoStates.begin());
    track->clipAudioEditRedoStates.clear();
    return juce::Result::ok();
}

bool AudioTrackController::undoTrackAudioEdit(const std::size_t index)
{
    auto* track = findTrack(index);
    if (track != nullptr && !track->clipAudioEditUndoStates.empty()) {
        const auto state = track->clipAudioEditUndoStates.back();
        const auto found = std::find_if(
            track->audioClips.begin(), track->audioClips.end(),
            [&state](const auto& clip) { return clip.id == state.clip.id; });
        if (found == track->audioClips.end())
            return false;
        const auto current = *found;
        *found = state.clip;
        if (rebuildArrangementPlan(*track).failed()) {
            *found = current;
            (void) rebuildArrangementPlan(*track);
            return false;
        }
        track->clipAudioEditUndoStates.pop_back();
        track->clipAudioEditRedoStates.push_back({ current });
        return true;
    }
    if (track == nullptr || track->audioEditUndoSources.empty())
        return false;
    const auto previous = track->audioEditUndoSources.back();
    auto firstReader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(previous));
    auto secondReader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(previous));
    if (firstReader == nullptr || secondReader == nullptr)
        return false;
    const auto current = track->file;
    track->audioEditUndoSources.pop_back();
    track->audioEditRedoSources.push_back(current);
    track->replaceSource(std::move(firstReader), std::move(secondReader),
                         previous, readAheadThread_);
    for (auto& clip : track->audioClips) {
        for (auto& take : clip.takes)
            if (take.sourceFile == current)
                take.sourceFile = previous;
        if (clip.sourceFile == current)
            clip.sourceFile = previous;
    }
    for (auto& clip : track->launcherClips)
        if (clip.sourceFile == current)
            clip.sourceFile = previous;
    return rebuildArrangementPlan(*track).wasOk()
           && rebuildLauncherPlans(*track).wasOk();
}

bool AudioTrackController::redoTrackAudioEdit(const std::size_t index)
{
    auto* track = findTrack(index);
    if (track != nullptr && !track->clipAudioEditRedoStates.empty()) {
        const auto state = track->clipAudioEditRedoStates.back();
        const auto found = std::find_if(
            track->audioClips.begin(), track->audioClips.end(),
            [&state](const auto& clip) { return clip.id == state.clip.id; });
        if (found == track->audioClips.end())
            return false;
        const auto current = *found;
        *found = state.clip;
        if (rebuildArrangementPlan(*track).failed()) {
            *found = current;
            (void) rebuildArrangementPlan(*track);
            return false;
        }
        track->clipAudioEditRedoStates.pop_back();
        track->clipAudioEditUndoStates.push_back({ current });
        return true;
    }
    if (track == nullptr || track->audioEditRedoSources.empty())
        return false;
    const auto next = track->audioEditRedoSources.back();
    auto firstReader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(next));
    auto secondReader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(next));
    if (firstReader == nullptr || secondReader == nullptr)
        return false;
    const auto current = track->file;
    track->audioEditRedoSources.pop_back();
    track->audioEditUndoSources.push_back(current);
    track->replaceSource(std::move(firstReader), std::move(secondReader),
                         next, readAheadThread_);
    for (auto& clip : track->audioClips) {
        for (auto& take : clip.takes)
            if (take.sourceFile == current)
                take.sourceFile = next;
        if (clip.sourceFile == current)
            clip.sourceFile = next;
    }
    for (auto& clip : track->launcherClips)
        if (clip.sourceFile == current)
            clip.sourceFile = next;
    return rebuildArrangementPlan(*track).wasOk()
           && rebuildLauncherPlans(*track).wasOk();
}

bool AudioTrackController::canUndoTrackAudioEdit(const std::size_t index) const noexcept
{
    const auto* track = findTrack(index);
    return track != nullptr
           && (!track->clipAudioEditUndoStates.empty()
               || !track->audioEditUndoSources.empty());
}

bool AudioTrackController::canRedoTrackAudioEdit(const std::size_t index) const noexcept
{
    const auto* track = findTrack(index);
    return track != nullptr
           && (!track->clipAudioEditRedoStates.empty()
               || !track->audioEditRedoSources.empty());
}

juce::Result AudioTrackController::setTrackPlugin(const std::size_t index,
                                                   const juce::File& pluginFile,
                                                   std::size_t* const pluginIndex)
{
    auto* track = findTrack(index);
    if (track == nullptr)
        return juce::Result::fail("Unknown audio track");
    auto* device = deviceManager_.getCurrentAudioDevice();
    const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : 48'000.0;
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    const auto channels = track->channelCount;
    const auto result = track->gainPanSource.addPlugin(pluginFile, sampleRate,
                                                       juce::jlimit(1, 2'048, blockSize), channels,
                                                       pluginIndex);
    refreshLatencyCompensation();
    return result;
}

juce::Result AudioTrackController::reloadTrackPlugin(std::size_t index,std::size_t plugin) {
    auto* track=findTrack(index);if(!track)return juce::Result::fail("Unknown audio track");
    auto* device=deviceManager_.getCurrentAudioDevice();auto result=track->gainPanSource.reloadPlugin(plugin,device?device->getCurrentSampleRate():48000,device?device->getCurrentBufferSizeSamples():512,track->channelCount);
    refreshLatencyCompensation();return result;
}

void AudioTrackController::clearTrackPlugin(const std::size_t index,
                                            const std::size_t pluginIndex)
{
    if (auto* track = findTrack(index)) {
        track->gainPanSource.removePlugin(pluginIndex);
        std::erase_if(track->pluginAutomation, [pluginIndex](const auto& lane) {
            return lane.pluginIndex == pluginIndex;
        });
        for (auto& lane : track->pluginAutomation)
            if (lane.pluginIndex > pluginIndex)
                --lane.pluginIndex;
        publishTrackPluginAutomation(*track);
        refreshLatencyCompensation();
    }
}

void AudioTrackController::clearTrackPlugins(const std::size_t index)
{
    if (auto* track = findTrack(index)) {
        track->gainPanSource.clearPlugins();
        track->pluginAutomation.clear();
        publishTrackPluginAutomation(*track);
        refreshLatencyCompensation();
    }
}

bool AudioTrackController::moveTrackPlugin(const std::size_t index,
                                           const std::size_t pluginIndex,
                                           const int direction)
{
    if (auto* track = findTrack(index)) {
        const auto moved = track->gainPanSource.movePlugin(pluginIndex, direction);
        if (moved) {
            const auto destination = static_cast<std::size_t>(
                static_cast<std::ptrdiff_t>(pluginIndex) + (direction < 0 ? -1 : 1));
            for (auto& lane : track->pluginAutomation) {
                if (lane.pluginIndex == pluginIndex)
                    lane.pluginIndex = destination;
                else if (lane.pluginIndex == destination)
                    lane.pluginIndex = pluginIndex;
            }
            publishTrackPluginAutomation(*track);
            refreshLatencyCompensation();
        }
        return moved;
    }
    return false;
}

void AudioTrackController::pollPluginWorkers()
{
    for (auto& track : tracks_)
        track->gainPanSource.pollPluginHealth();
    refreshLatencyCompensation();
}

juce::Result AudioTrackController::getTrackPluginState(const std::size_t index,
                                                        const std::size_t pluginIndex,
                                                        juce::MemoryBlock& destination)
{
    if (auto* track = findTrack(index))
        return track->gainPanSource.getPluginState(pluginIndex, destination);
    return juce::Result::fail("Unknown audio track");
}

juce::Result AudioTrackController::getCachedTrackPluginState(
    const std::size_t index, const std::size_t pluginIndex,
    juce::MemoryBlock& destination) const
{
    if (const auto* track = findTrack(index))
        return track->gainPanSource.getCachedPluginState(pluginIndex,
                                                         destination);
    return juce::Result::fail("Unknown audio track");
}

juce::Result AudioTrackController::setTrackPluginState(const std::size_t index,
                                                        const std::size_t pluginIndex,
                                                        const juce::MemoryBlock& state)
{
    if (auto* track = findTrack(index))
        return track->gainPanSource.setPluginState(pluginIndex, state);
    return juce::Result::fail("Unknown audio track");
}

juce::Result AudioTrackController::showTrackPluginEditor(const std::size_t index,
                                                         const std::size_t pluginIndex)
{
    if (auto* track = findTrack(index))
        return track->gainPanSource.showPluginEditor(pluginIndex);
    return juce::Result::fail("Unknown audio track");
}

juce::Result AudioTrackController::refreshTrackPluginParameters(
    const std::size_t index,
    const std::size_t pluginIndex,
    std::vector<IsolatedPluginHost::ParameterSnapshot>& destination)
{
    if (auto* track = findTrack(index))
        return track->gainPanSource.refreshPluginParameters(pluginIndex, destination);
    return juce::Result::fail("Unknown audio track");
}

juce::Result AudioTrackController::setTrackPluginParameter(const std::size_t index,
                                                            const std::size_t pluginIndex,
                                                            const std::uint32_t parameterIndex,
                                                            const float normalisedValue)
{
    if (auto* track = findTrack(index))
        return track->gainPanSource.setPluginParameter(pluginIndex, parameterIndex,
                                                       normalisedValue);
    return juce::Result::fail("Unknown audio track");
}

juce::Result AudioTrackController::getTrackPluginPrograms(const std::size_t index, const std::size_t pluginIndex,
                                                          juce::StringArray& names, int& current)
{
    names.clear();
    current = -1;
    if (auto* track = findTrack(index))
        if (auto* host = track->gainPanSource.pluginHost(pluginIndex))
            return host->getPrograms(names, current);
    return juce::Result::fail("Unknown plugin slot");
}

juce::Result AudioTrackController::setTrackPluginProgram(const std::size_t index, const std::size_t pluginIndex,
                                                         const int program)
{
    if (auto* track = findTrack(index))
        if (auto* host = track->gainPanSource.pluginHost(pluginIndex))
            return host->setProgram(program);
    return juce::Result::fail("Unknown plugin slot");
}

std::uint64_t AudioTrackController::addMidiNote(const int noteNumber,
                                                const double startBeats,
                                                const double durationBeats,
                                                const float velocity)
{
    if (midiTracks_.empty())
        (void) addMidiTrack("Instrument 1");
    auto& track = *midiTracks_.front();
    if (track.clips.empty())
        (void) addMidiClip(track.id, "Clip 1");
    return addMidiNote(track.id, track.clips.front().id, noteNumber,
                       startBeats, durationBeats, velocity);
}

bool AudioTrackController::updateMidiNote(const std::uint64_t id,
                                          const int noteNumber,
                                          const double startBeats,
                                          const double durationBeats,
                                          const float velocity)
{
    for (auto& track : midiTracks_)
        for (auto& clip : track->clips)
            if (updateMidiNote(track->id, clip.id, id, noteNumber, startBeats,
                               durationBeats, velocity))
                return true;
    return false;
}

bool AudioTrackController::removeMidiNote(const std::uint64_t id)
{
    for (auto& track : midiTracks_)
        for (auto& clip : track->clips)
            if (removeMidiNote(track->id, clip.id, id))
                return true;
    return false;
}

void AudioTrackController::clearMidiNotes()
{
    if (!midiTracks_.empty() && !midiTracks_.front()->clips.empty())
        clearMidiNotes(midiTracks_.front()->id,
                       midiTracks_.front()->clips.front().id);
}

std::vector<AudioTrackController::MidiNote> AudioTrackController::midiNotes() const
{
    if (!midiTracks_.empty() && !midiTracks_.front()->clips.empty())
        return midiTracks_.front()->clips.front().notes;
    return {};
}

std::uint64_t AudioTrackController::addMidiTrack(juce::String name)
{
    const auto id = nextMidiTrackId_++;
    if (name.isEmpty())
        name = "Instrument " + juce::String(static_cast<int>(midiTracks_.size() + 1));
    auto track = std::make_unique<MidiPlaybackTrack>(
        *this, id, std::move(name), midiCollector_, midiKeyboardState_);
    track->clips.push_back({ nextMidiClipId_++, "Clip 1", 0.0, 16.0,
                             false, 0xff4fd3ae, {} });
    mixerSource_.addInputSource(&track->latencySource, false);
    midiTracks_.push_back(std::move(track));
    if (liveMidiTrackId_.load(std::memory_order_acquire) == 0)
        (void) setLiveMidiTrack(id);
    publishMidiSequence(*midiTracks_.back());
    refreshTrackGains();
    refreshLatencyCompensation();
    return id;
}

bool AudioTrackController::moveMidiTrack(const std::size_t fromIndex,
                                         const std::size_t toIndex)
{
    if (fromIndex >= midiTracks_.size() || toIndex >= midiTracks_.size())
        return false;
    if (fromIndex == toIndex)
        return true;
    auto moving = std::move(midiTracks_[fromIndex]);
    midiTracks_.erase(midiTracks_.begin()
                      + static_cast<std::ptrdiff_t>(fromIndex));
    midiTracks_.insert(midiTracks_.begin()
                           + static_cast<std::ptrdiff_t>(toIndex),
                       std::move(moving));
    refreshTrackGains();
    refreshLatencyCompensation();
    clearMidiEditHistory();
    return true;
}

bool AudioTrackController::removeMidiTrack(const std::uint64_t trackId)
{
    if (midiRecordingTrackId_ == trackId)
        stopMidiRecording(false);
    const auto found = std::find_if(midiTracks_.begin(), midiTracks_.end(),
                                    [trackId](const auto& track) {
                                        return track->id == trackId;
                                    });
    if (found == midiTracks_.end())
        return false;
    (*found)->synth.setPlaying(false);
    mixerSource_.removeInputSource(&(*found)->latencySource);
    const auto sentOut = (*found)->midiOutput.isNotEmpty();
    midiTracks_.erase(found);
    if (sentOut)
        publishMidiOutputs();
    if (liveMidiTrackId_.load(std::memory_order_acquire) == trackId) {
        liveMidiTrackId_.store(0, std::memory_order_release);
        if (!midiTracks_.empty())
            (void) setLiveMidiTrack(midiTracks_.front()->id);
    }
    refreshTransportRanges();
    refreshTrackGains();
    refreshLatencyCompensation();
    clearMidiEditHistory();
    return true;
}

void AudioTrackController::clearMidiTracks()
{
    stopMidiRecording(false);
    for (auto& track : midiTracks_) {
        track->synth.setPlaying(false);
        mixerSource_.removeInputSource(&track->latencySource);
    }
    midiTracks_.clear();
    liveMidiTrackId_.store(0, std::memory_order_release);
    clearMidiEditHistory();
    refreshTransportRanges();
    refreshLatencyCompensation();
}

std::vector<AudioTrackController::MidiTrackInfo> AudioTrackController::midiTracks() const
{
    std::vector<MidiTrackInfo> result;
    result.reserve(midiTracks_.size());
    for (const auto& track : midiTracks_)
        result.push_back({ track->id, track->name, track->gainDb, track->pan,
                           track->muted, track->solo, track->armed, track->clips });
    return result;
}

std::uint64_t AudioTrackController::addMidiClip(
    const std::uint64_t trackId,
    juce::String name,
    const double startBeats,
    const double lengthBeats,
    const bool looping,
    const std::uint32_t colour,
    const int launcherScene)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr || launcherScene >= maximumLauncherScenes)
        return 0;
    const auto id = nextMidiClipId_++;
    if (name.isEmpty())
        name = "Clip " + juce::String(static_cast<int>(track->clips.size() + 1));
    MidiClip clip;
    clip.id = id;
    clip.name = std::move(name);
    clip.startBeats = launcherScene >= 0 ? 0.0 : std::max(0.0, startBeats);
    clip.lengthBeats = std::max(0.25, lengthBeats);
    clip.looping = looping;
    clip.colour = colour;
    clip.launcherScene = std::max(-1, launcherScene);
    track->clips.push_back(std::move(clip));
    publishMidiSequence(*track);
    return id;
}

bool AudioTrackController::setMidiClipLauncherScene(const std::uint64_t trackId, const std::uint64_t clipId,
                                                    const int launcherScene)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr || launcherScene >= maximumLauncherScenes)
        return false;
    clip->launcherScene = std::max(-1, launcherScene);
    if (clip->launcherScene >= 0)
        clip->startBeats = 0.0;
    publishMidiSequence(*track);
    return true;
}

bool AudioTrackController::setMidiTrackName(const std::uint64_t trackId,
                                            juce::String name)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr || name.trim().isEmpty())
        return false;
    track->name = name.trim();
    return true;
}

bool AudioTrackController::removeMidiClip(const std::uint64_t trackId,
                                          const std::uint64_t clipId)
{
    if (midiRecordingTrackId_ == trackId
        && midiRecordingClipId_ == clipId)
        stopMidiRecording(false);
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return false;
    const auto removingEditorPreview
        = track->activeEditorClipId.load(std::memory_order_acquire)
          == clipId;
    if (removingEditorPreview)
        track->synth.setPlaying(false);
    const auto previousSize = track->clips.size();
    std::erase_if(track->clips, [clipId](const auto& clip) {
        return clip.id == clipId;
    });
    if (track->clips.size() == previousSize)
        return false;
    if (removingEditorPreview) {
        track->activeEditorClipId.store(0, std::memory_order_release);
        publishMidiTrackAutomation(*track);
    }
    clearMidiEditHistory();
    publishMidiSequence(*track);
    return true;
}

bool AudioTrackController::setMidiClipLooping(
    const std::uint64_t trackId, const std::uint64_t clipId,
    const bool looping)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return false;
    clip->looping = looping;
    if (track->activeLauncherScene.load(std::memory_order_acquire) >= 0
        || track->activeEditorClipId.load(std::memory_order_acquire)
               == clipId)
        track->synth.setLooping(looping);
    return true;
}

bool AudioTrackController::setMidiClipTimelineBounds(
    const std::uint64_t trackId, const std::uint64_t clipId,
    const double lengthBeats, const double playStartBeats,
    const double playEndBeats, const double loopStartBeats,
    const double loopEndBeats, const bool looping)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return false;
    const auto length = juce::jlimit(0.25, 86'400.0, lengthBeats);
    const auto start = juce::jlimit(0.0, std::max(0.0, length - 0.03125),
                                    playStartBeats);
    const auto end = juce::jlimit(start + 0.03125, length,
                                  playEndBeats > start ? playEndBeats
                                                       : length);
    const auto loopStart = juce::jlimit(
        start, std::max(start, end - 0.03125), loopStartBeats);
    const auto loopEnd = juce::jlimit(
        loopStart + 0.03125, end,
        loopEndBeats > loopStart ? loopEndBeats : end);
    clip->lengthBeats = length;
    clip->playStartBeats = start;
    clip->playEndBeats = end;
    clip->loopStartBeats = loopStart;
    clip->loopEndBeats = loopEnd;
    clip->looping = looping;
    publishMidiSequence(*track);
    if (track->activeLauncherScene.load(std::memory_order_acquire) >= 0) {
        publishMidiClipPreviewSequence(*track, *clip);
        track->synth.setLooping(looping);
    }
    return true;
}

std::uint64_t AudioTrackController::addMidiNote(
    const std::uint64_t trackId,
    const std::uint64_t clipId,
    const int noteNumber,
    const double startBeats,
    const double durationBeats,
    const float velocity)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (track == nullptr || clip == nullptr)
        return 0;
    prepareMidiMutation(trackId, clipId);
    const auto id = nextMidiNoteId_++;
    const auto requestedEnd = std::max(0.0, startBeats)
                              + std::max(0.03125, durationBeats);
    if (requestedEnd > clip->lengthBeats)
        clip->lengthBeats = std::ceil(requestedEnd / 4.0) * 4.0;
    const auto safeStart = juce::jlimit(0.0,
                                        std::max(0.0, clip->lengthBeats - 0.03125),
                                        std::max(0.0, startBeats));
    clip->notes.push_back({ id, juce::jlimit(0, 127, noteNumber), safeStart,
                            juce::jlimit(0.03125,
                                         std::max(0.03125, clip->lengthBeats - safeStart),
                                         durationBeats),
                            juce::jlimit(0.01f, 1.0f, velocity) });
    publishMidiSequence(*track);
    return id;
}

bool AudioTrackController::updateMidiNote(
    const std::uint64_t trackId,
    const std::uint64_t clipId,
    const std::uint64_t noteId,
    const int noteNumber,
    const double startBeats,
    const double durationBeats,
    const float velocity)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (track == nullptr || clip == nullptr)
        return false;
    const auto found = std::find_if(clip->notes.begin(), clip->notes.end(),
                                    [noteId](const auto& note) {
                                        return note.id == noteId;
                                    });
    if (found == clip->notes.end())
        return false;
    prepareMidiMutation(trackId, clipId);
    const auto requestedEnd = std::max(0.0, startBeats)
                              + std::max(0.03125, durationBeats);
    if (requestedEnd > clip->lengthBeats)
        clip->lengthBeats = std::ceil(requestedEnd / 4.0) * 4.0;
    found->noteNumber = juce::jlimit(0, 127, noteNumber);
    found->startBeats = juce::jlimit(0.0,
                                     std::max(0.0, clip->lengthBeats - 0.03125),
                                     std::max(0.0, startBeats));
    found->durationBeats = juce::jlimit(
        0.03125, std::max(0.03125, clip->lengthBeats - found->startBeats),
        durationBeats);
    found->velocity = juce::jlimit(0.01f, 1.0f, velocity);
    publishMidiSequence(*track);
    return true;
}

bool AudioTrackController::removeMidiNote(const std::uint64_t trackId,
                                          const std::uint64_t clipId,
                                          const std::uint64_t noteId)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (track == nullptr || clip == nullptr)
        return false;
    const auto previousSize = clip->notes.size();
    if (previousSize == 0
        || std::none_of(clip->notes.begin(), clip->notes.end(),
                        [noteId](const auto& note) {
                            return note.id == noteId;
                        }))
        return false;
    prepareMidiMutation(trackId, clipId);
    std::erase_if(clip->notes, [noteId](const auto& note) {
        return note.id == noteId;
    });
    publishMidiSequence(*track);
    return true;
}

void AudioTrackController::clearMidiNotes(const std::uint64_t trackId,
                                          const std::uint64_t clipId)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (track == nullptr || clip == nullptr)
        return;
    if (clip->notes.empty())
        return;
    prepareMidiMutation(trackId, clipId);
    clip->notes.clear();
    publishMidiSequence(*track);
}

std::uint64_t AudioTrackController::copyMidiNote(const std::uint64_t trackId, const std::uint64_t clipId, MidiNote note,
                                                const double startBeats)
{
    const auto id = addMidiNote(trackId, clipId, note.noteNumber, startBeats, note.durationBeats, note.velocity);
    if (id != 0 && (!note.pitch.empty() || !note.gain.empty()))
        (void) setMidiNoteExpression(trackId, clipId, id, std::move(note.pitch), std::move(note.gain));
    if (id != 0) {
        for (const auto curve : { NoteCurve::timbre, NoteCurve::pressure, NoteCurve::pan })
            if (!noteCurveOf(note, curve).empty())
                (void) setMidiNoteCurve(trackId, clipId, id, curve, noteCurveOf(note, curve));
        if (note.chance < 1.0f)
            (void) setMidiNoteChance(trackId, clipId, id, note.chance);
    }
    return id;
}

bool AudioTrackController::setMidiNoteCurve(const std::uint64_t trackId, const std::uint64_t clipId, const std::uint64_t noteId,
                                            const NoteCurve curve, std::vector<NoteExpressionPoint> points)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return false;
    const auto note = std::find_if(clip->notes.begin(), clip->notes.end(), [noteId](const auto& item) { return item.id == noteId; });
    if (note == clip->notes.end())
        return false;
    // In order along the note, inside it and its range.
    const auto [low, high] = noteCurveRange(curve);
    for (auto& point : points) {
        point.position = juce::jlimit(0.0f, 1.0f, point.position);
        point.value = juce::jlimit(low, high, point.value);
        point.bend = std::isfinite(point.bend) ? juce::jlimit(-1.0f, 1.0f, point.bend) : 0.0f;
    }
    std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
    if (points.size() > 256)
        points.resize(256);
    auto& held = noteCurveOf(*note, curve);
    if (held == points)
        return true;
    prepareMidiMutation(trackId, clipId);
    held = std::move(points);
    publishMidiSequence(*track);
    return true;
}

bool AudioTrackController::setMidiNoteChance(const std::uint64_t trackId, const std::uint64_t clipId, const std::uint64_t noteId,
                                             const float chance)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return false;
    const auto note = std::find_if(clip->notes.begin(), clip->notes.end(), [noteId](const auto& item) { return item.id == noteId; });
    if (note == clip->notes.end())
        return false;
    const auto value = std::isfinite(chance) ? juce::jlimit(0.0f, 1.0f, chance) : 1.0f;
    if (std::abs(note->chance - value) < 1.0e-6f)
        return true;
    prepareMidiMutation(trackId, clipId);
    note->chance = value;
    publishMidiSequence(*track);
    return true;
}

bool AudioTrackController::setMidiNoteExpression(const std::uint64_t trackId, const std::uint64_t clipId,
                                                 const std::uint64_t noteId, std::vector<NoteExpressionPoint> pitch,
                                                 std::vector<NoteExpressionPoint> gain)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return false;
    const auto note = std::find_if(clip->notes.begin(), clip->notes.end(), [noteId](const auto& item) { return item.id == noteId; });
    if (note == clip->notes.end())
        return false;
    // In order along the note, inside it, within what the voices take.
    const auto tidy = [](std::vector<NoteExpressionPoint>& points, const float low, const float high) {
        for (auto& point : points) {
            point.position = juce::jlimit(0.0f, 1.0f, point.position);
            point.value = juce::jlimit(low, high, point.value);
            point.bend = std::isfinite(point.bend) ? juce::jlimit(-1.0f, 1.0f, point.bend) : 0.0f;
        }
        std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
        if (points.size() > 256)
            points.resize(256);
    };
    tidy(pitch, -24.0f, 24.0f);
    tidy(gain, -48.0f, 12.0f);
    if (note->pitch == pitch && note->gain == gain)
        return true;
    prepareMidiMutation(trackId, clipId);
    note->pitch = std::move(pitch);
    note->gain = std::move(gain);
    publishMidiSequence(*track);
    return true;
}

bool AudioTrackController::replaceMidiNotes(
    const std::uint64_t trackId,
    const std::uint64_t clipId,
    std::vector<MidiNote> notes)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (track == nullptr || clip == nullptr)
        return false;

    std::unordered_set<std::uint64_t> existingIds;
    existingIds.reserve(clip->notes.size());
    for (const auto& note : clip->notes)
        existingIds.insert(note.id);

    std::unordered_set<std::uint64_t> usedIds;
    usedIds.reserve(notes.size());
    auto requiredEnd = 0.0;
    for (auto& note : notes) {
        if (note.id == 0 || !existingIds.contains(note.id)
            || !usedIds.insert(note.id).second) {
            do {
                note.id = nextMidiNoteId_++;
            } while (existingIds.contains(note.id)
                     || usedIds.contains(note.id));
            usedIds.insert(note.id);
        }
        note.noteNumber = juce::jlimit(0, 127, note.noteNumber);
        note.startBeats = std::max(0.0, note.startBeats);
        note.durationBeats = std::max(0.03125, note.durationBeats);
        note.velocity = juce::jlimit(0.01f, 1.0f, note.velocity);
        requiredEnd = std::max(requiredEnd,
                               note.startBeats + note.durationBeats);
    }

    auto targetLength = clip->lengthBeats;
    if (requiredEnd > targetLength)
        targetLength = std::ceil(requiredEnd / 4.0) * 4.0;
    targetLength = std::max(0.25, targetLength);
    for (auto& note : notes) {
        note.startBeats = juce::jlimit(
            0.0, std::max(0.0, targetLength - 0.03125), note.startBeats);
        note.durationBeats = juce::jlimit(
            0.03125,
            std::max(0.03125, targetLength - note.startBeats),
            note.durationBeats);
    }

    if (std::abs(targetLength - clip->lengthBeats) <= 0.0000001
        && sameMidiNotes(notes, clip->notes))
        return false;

    prepareMidiMutation(trackId, clipId);
    clip->lengthBeats = targetLength;
    clip->notes = std::move(notes);
    publishMidiSequence(*track);
    return true;
}

juce::Result AudioTrackController::importMidiFile(
    const juce::File& file,
    MidiFileImportSummary& summary,
    const bool adoptFileTempo)
{
    summary = {};
    summary.tempoBpm = tempoBpm_;
    if (midiRecordingActive_.load(std::memory_order_acquire))
        return juce::Result::fail(tr("error.midiImportRecording"));
    if (!file.existsAsFile())
        return juce::Result::fail(tr("error.midiImportMissing"));
    auto input = file.createInputStream();
    if (input == nullptr)
        return juce::Result::fail(tr("error.midiImportRead"));

    juce::MidiFile midiFile;
    int midiFileType = -1;
    if (!midiFile.readFrom(*input, true, &midiFileType)
        || midiFile.getNumTracks() <= 0)
        return juce::Result::fail(tr("error.midiImportInvalid"));
    juce::ignoreUnused(midiFileType);

    auto importedTempo = tempoBpm_;
    auto earliestTempoTime = std::numeric_limits<double>::max();
    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks();
         ++trackIndex) {
        const auto* sequence = midiFile.getTrack(trackIndex);
        if (sequence == nullptr)
            continue;
        for (int eventIndex = 0; eventIndex < sequence->getNumEvents();
             ++eventIndex) {
            const auto* event = sequence->getEventPointer(eventIndex);
            if (event == nullptr || !event->message.isTempoMetaEvent()
                || event->message.getTimeStamp() >= earliestTempoTime)
                continue;
            const auto secondsPerQuarter =
                event->message.getTempoSecondsPerQuarterNote();
            if (secondsPerQuarter > 0.0
                && std::isfinite(secondsPerQuarter)) {
                importedTempo = juce::jlimit(
                    20.0, 400.0, 60.0 / secondsPerQuarter);
                earliestTempoTime = event->message.getTimeStamp();
            }
        }
    }
    summary.tempoBpm = importedTempo;

    const auto timeFormat = midiFile.getTimeFormat();
    if (timeFormat == 0)
        return juce::Result::fail(tr("error.midiImportInvalid"));
    const auto timestampToBeats = [timeFormat, importedTempo](
                                      const double timestamp) {
        if (!std::isfinite(timestamp) || timestamp <= 0.0)
            return 0.0;
        if (timeFormat > 0)
            return timestamp / static_cast<double>(timeFormat);
        const auto encoded = static_cast<unsigned short>(timeFormat);
        const auto frameCode = -static_cast<int>(
            static_cast<std::int8_t>((encoded >> 8) & 0xff));
        const auto ticksPerFrame = static_cast<int>(encoded & 0xff);
        if (frameCode <= 0 || ticksPerFrame <= 0)
            return 0.0;
        const auto framesPerSecond = frameCode == 29 ? 29.97
                                                      : static_cast<double>(
                                                            frameCode);
        const auto seconds = timestamp
                             / (framesPerSecond
                                * static_cast<double>(ticksPerFrame));
        return seconds * importedTempo / 60.0;
    };

    struct ImportedGroup {
        int sourceTrack {};
        int channel { 1 };
        juce::String name;
        std::vector<MidiNote> notes;
        double requiredEndBeats {};
    };
    constexpr std::size_t maximumImportedNotes = 1'000'000;
    constexpr std::size_t maximumImportedTracks = 256;
    constexpr double maximumImportedBeat = 1'000'000.0;
    std::vector<ImportedGroup> groups;
    const auto fileBaseName = file.getFileNameWithoutExtension();
    auto totalNotes = std::size_t {};

    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks();
         ++trackIndex) {
        const auto* sequence = midiFile.getTrack(trackIndex);
        if (sequence == nullptr)
            continue;
        auto sourceName = juce::String {};
        for (int eventIndex = 0; eventIndex < sequence->getNumEvents();
             ++eventIndex) {
            const auto* event = sequence->getEventPointer(eventIndex);
            if (event != nullptr && event->message.isTrackNameEvent()) {
                sourceName = event->message.getTextFromTextMetaEvent().trim();
                if (sourceName.isNotEmpty())
                    break;
            }
        }
        if (sourceName.isEmpty()) {
            sourceName = fileBaseName;
            if (midiFile.getNumTracks() > 1)
                sourceName += " " + juce::String(trackIndex + 1);
        }

        std::array<int, 16> channelGroupIndices;
        channelGroupIndices.fill(-1);
        for (int eventIndex = 0; eventIndex < sequence->getNumEvents();
             ++eventIndex) {
            const auto* event = sequence->getEventPointer(eventIndex);
            if (event == nullptr || !event->message.isNoteOn())
                continue;
            if (++totalNotes > maximumImportedNotes)
                return juce::Result::fail(tr("error.midiImportTooLarge"));
            const auto channel = juce::jlimit(
                1, 16, event->message.getChannel());
            auto& groupIndex = channelGroupIndices[
                static_cast<std::size_t>(channel - 1)];
            if (groupIndex < 0) {
                if (groups.size() >= maximumImportedTracks)
                    return juce::Result::fail(
                        tr("error.midiImportTooLarge"));
                groupIndex = static_cast<int>(groups.size());
                groups.push_back(
                    { trackIndex, channel, sourceName, {}, 0.0 });
            }
            auto startBeats = timestampToBeats(
                event->message.getTimeStamp());
            auto endBeats = startBeats + 0.25;
            if (event->noteOffObject != nullptr)
                endBeats = timestampToBeats(
                    event->noteOffObject->message.getTimeStamp());
            startBeats = std::max(0.0, startBeats);
            const auto durationBeats = std::max(
                0.03125, endBeats - startBeats);
            if (startBeats > maximumImportedBeat
                || durationBeats > maximumImportedBeat
                || startBeats + durationBeats > maximumImportedBeat)
                return juce::Result::fail(tr("error.midiImportTooLarge"));
            auto& group = groups[static_cast<std::size_t>(groupIndex)];
            group.notes.push_back(
                { 0, event->message.getNoteNumber(), startBeats,
                  durationBeats, juce::jlimit(
                                     0.01f, 1.0f,
                                     event->message.getFloatVelocity()) });
            group.requiredEndBeats = std::max(
                group.requiredEndBeats, startBeats + durationBeats);
        }
    }
    if (groups.empty())
        return juce::Result::fail(tr("error.midiImportEmpty"));

    static constexpr std::array<std::uint32_t, 8> colours {
        0xff4fd3ae, 0xffff8a00, 0xff8e9dff, 0xffff657a,
        0xff55b8ff, 0xffffd166, 0xffc77dff, 0xff66d9e8
    };
    summary.trackIds.reserve(groups.size());
    for (auto& group : groups) {
        const auto sameSourceCount = std::count_if(
            groups.begin(), groups.end(), [&group](const auto& candidate) {
                return candidate.sourceTrack == group.sourceTrack;
            });
        if (sameSourceCount > 1)
            group.name += " - Ch " + juce::String(group.channel);
        std::stable_sort(group.notes.begin(), group.notes.end(),
                         [](const auto& first, const auto& second) {
                             if (first.startBeats != second.startBeats)
                                 return first.startBeats < second.startBeats;
                             return first.noteNumber < second.noteNumber;
                         });
        for (auto& note : group.notes)
            note.id = nextMidiNoteId_++;
        const auto trackId = nextMidiTrackId_++;
        const auto clipId = nextMidiClipId_++;
        const auto clipLength = std::max(
            4.0, std::ceil(group.requiredEndBeats / 4.0) * 4.0);
        auto track = std::make_unique<MidiPlaybackTrack>(
            *this, trackId, group.name, midiCollector_, midiKeyboardState_);
        track->clips.push_back(
            { clipId, fileBaseName, 0.0, clipLength, false,
              colours[summary.trackIds.size() % colours.size()],
              std::move(group.notes) });
        mixerSource_.addInputSource(&track->latencySource, false);
        midiTracks_.push_back(std::move(track));
        summary.trackIds.push_back(trackId);
    }
    summary.noteCount = totalNotes;
    if (adoptFileTempo)
        tempoBpm_ = importedTempo;
        NativeDspRuntime::setSessionTempo(tempoBpm_);
    (void) setLiveMidiTrack(summary.trackIds.front());
    clearMidiEditHistory();
    publishAllMidiSequences();
    refreshTrackGains();
    refreshLatencyCompensation();
    return juce::Result::ok();
}

juce::Result AudioTrackController::exportMidiClipToFile(
    const std::uint64_t trackId,
    const std::uint64_t clipId,
    const juce::File& file) const
{
    const auto* track = findMidiTrack(trackId);
    const auto* clip = track != nullptr ? findMidiClip(*track, clipId)
                                        : nullptr;
    if (track == nullptr || clip == nullptr)
        return juce::Result::fail(tr("error.midiExportNoClip"));
    if (clip->notes.empty())
        return juce::Result::fail(tr("error.midiExportEmpty"));
    if (file == juce::File {})
        return juce::Result::fail(tr("error.midiExportCreate"));

    constexpr int ticksPerQuarterNote = 960;
    juce::MidiMessageSequence sequence;
    auto trackName = juce::MidiMessage::textMetaEvent(
        3, track->name + " - " + clip->name);
    trackName.setTimeStamp(0.0);
    sequence.addEvent(std::move(trackName));
    const auto microsecondsPerQuarter = static_cast<int>(std::lround(
        60'000'000.0 / juce::jlimit(20.0, 400.0, tempoBpm_)));
    auto tempo = juce::MidiMessage::tempoMetaEvent(
        microsecondsPerQuarter);
    tempo.setTimeStamp(0.0);
    sequence.addEvent(std::move(tempo));
    auto timeSignature = juce::MidiMessage::timeSignatureMetaEvent(timeSignatureNumerator_, timeSignatureDenominator_);
    timeSignature.setTimeStamp(0.0);
    sequence.addEvent(std::move(timeSignature));

    auto latestEndBeats = 0.0;
    for (const auto& note : clip->notes) {
        const auto startBeats = std::max(0.0, note.startBeats);
        const auto endBeats = std::max(
            startBeats + 0.03125,
            note.startBeats + note.durationBeats);
        auto noteOn = juce::MidiMessage::noteOn(
            1, juce::jlimit(0, 127, note.noteNumber),
            juce::jlimit(0.01f, 1.0f, note.velocity));
        noteOn.setTimeStamp(startBeats * ticksPerQuarterNote);
        sequence.addEvent(std::move(noteOn));
        auto noteOff = juce::MidiMessage::noteOff(
            1, juce::jlimit(0, 127, note.noteNumber));
        noteOff.setTimeStamp(endBeats * ticksPerQuarterNote);
        sequence.addEvent(std::move(noteOff));
        latestEndBeats = std::max(latestEndBeats, endBeats);
    }
    auto endOfTrack = juce::MidiMessage::endOfTrack();
    endOfTrack.setTimeStamp(
        std::max(latestEndBeats, clip->lengthBeats)
        * ticksPerQuarterNote);
    sequence.addEvent(std::move(endOfTrack));
    sequence.updateMatchedPairs();
    sequence.sort();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ticksPerQuarterNote);
    midiFile.addTrack(sequence);
    auto output = file.createOutputStream();
    if (output == nullptr || !output->setPosition(0)
        || output->truncate().failed())
        return juce::Result::fail(tr("error.midiExportCreate"));
    if (!midiFile.writeTo(*output, 0)) {
        output.reset();
        (void) file.deleteFile();
        return juce::Result::fail(tr("error.midiExportWrite"));
    }
    output->flush();
    return juce::Result::ok();
}

std::vector<AudioTrackController::MidiNote> AudioTrackController::midiNotes(
    const std::uint64_t trackId,
    const std::uint64_t clipId) const
{
    const auto* track = findMidiTrack(trackId);
    const auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    return clip != nullptr ? clip->notes : std::vector<MidiNote> {};
}

bool AudioTrackController::beginMidiEdit(const std::uint64_t trackId,
                                         const std::uint64_t clipId)
{
    if (midiEditBatch_.has_value() || midiRecordingActive_.load(
            std::memory_order_acquire))
        return false;
    const auto state = captureMidiEditState(trackId, clipId);
    if (state.trackId == 0 || state.clipId == 0)
        return false;
    midiEditBatch_ = state;
    return true;
}

bool AudioTrackController::endMidiEdit()
{
    if (!midiEditBatch_.has_value())
        return false;
    const auto previous = std::move(*midiEditBatch_);
    midiEditBatch_.reset();
    const auto current = captureMidiEditState(previous.trackId,
                                              previous.clipId);
    const auto sameBounds = std::abs(current.playStartBeats - previous.playStartBeats) <= 0.0000001
                            && std::abs(current.playEndBeats - previous.playEndBeats) <= 0.0000001
                            && std::abs(current.loopStartBeats - previous.loopStartBeats) <= 0.0000001
                            && std::abs(current.loopEndBeats - previous.loopEndBeats) <= 0.0000001
                            && current.looping == previous.looping;
    if (current.trackId == 0
        || (std::abs(current.clipLengthBeats - previous.clipLengthBeats)
                <= 0.0000001
            && sameMidiNotes(current.notes, previous.notes) && sameBounds))
        return false;
    static constexpr std::size_t maximumHistorySize = 32;
    midiEditUndoStates_.push_back(previous);
    if (midiEditUndoStates_.size() > maximumHistorySize)
        midiEditUndoStates_.erase(midiEditUndoStates_.begin());
    midiEditRedoStates_.clear();
    return true;
}

void AudioTrackController::cancelMidiEdit()
{
    if (!midiEditBatch_.has_value())
        return;
    const auto previous = std::move(*midiEditBatch_);
    midiEditBatch_.reset();
    (void) restoreMidiEditState(previous);
}

bool AudioTrackController::undoMidiEdit()
{
    if (midiRecordingActive_.load(std::memory_order_acquire)
        || midiEditBatch_.has_value() || midiEditUndoStates_.empty())
        return false;
    const auto previous = midiEditUndoStates_.back();
    const auto current = captureMidiEditState(previous.trackId,
                                              previous.clipId);
    if (current.trackId == 0 || !restoreMidiEditState(previous))
        return false;
    midiEditUndoStates_.pop_back();
    midiEditRedoStates_.push_back(current);
    if (midiEditRedoStates_.size() > 32)
        midiEditRedoStates_.erase(midiEditRedoStates_.begin());
    return true;
}

bool AudioTrackController::redoMidiEdit()
{
    if (midiRecordingActive_.load(std::memory_order_acquire)
        || midiEditBatch_.has_value() || midiEditRedoStates_.empty())
        return false;
    const auto next = midiEditRedoStates_.back();
    const auto current = captureMidiEditState(next.trackId, next.clipId);
    if (current.trackId == 0 || !restoreMidiEditState(next))
        return false;
    midiEditRedoStates_.pop_back();
    midiEditUndoStates_.push_back(current);
    if (midiEditUndoStates_.size() > 32)
        midiEditUndoStates_.erase(midiEditUndoStates_.begin());
    return true;
}

bool AudioTrackController::canUndoMidiEdit() const noexcept
{
    return !midiEditUndoStates_.empty()
           && !midiRecordingActive_.load(std::memory_order_acquire)
           && !midiEditBatch_.has_value();
}

bool AudioTrackController::canRedoMidiEdit() const noexcept
{
    return !midiEditRedoStates_.empty()
           && !midiRecordingActive_.load(std::memory_order_acquire)
           && !midiEditBatch_.has_value();
}

void AudioTrackController::clearMidiEditHistory() noexcept
{
    midiEditUndoStates_.clear();
    midiEditRedoStates_.clear();
    midiEditBatch_.reset();
}

juce::Result AudioTrackController::startMidiRecording(
    const std::uint64_t trackId,
    const std::uint64_t clipId,
    const bool overdub,
    const MidiRecordQuantization quantization)
{
    if (midiRecordingActive_.load(std::memory_order_acquire))
        return juce::Result::fail(tr("error.midiRecordActive"));
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (track == nullptr || clip == nullptr)
        return juce::Result::fail(tr("error.midiRecordNoClip"));
    if (midiEditBatch_.has_value())
        return juce::Result::fail(tr("error.midiRecordEditActive"));
    if (!setLiveMidiTrack(trackId))
        return juce::Result::fail(tr("error.midiRecordNoClip"));

    MidiRecordingEvent stale;
    while (midiRecordingEvents_.tryPop(stale)) {}
    activeMidiRecordingNotes_.fill({});
    droppedMidiRecordingEvents_.store(0, std::memory_order_release);
    midiRecordingTrackId_ = trackId;
    midiRecordingClipId_ = clipId;
    midiRecordQuantization_ = quantization;
    midiRecordingNoteCount_ = 0;
    midiEditBatch_ = captureMidiEditState(trackId, clipId);
    if (!overdub && !clip->notes.empty()) {
        clip->notes.clear();
        publishMidiSequence(*track);
    }
    midiRecordingActive_.store(true, std::memory_order_release);
    // The keyboard plays the track recorded while it records.
    refreshMidiInputs();
    refreshTransportRanges();
    return juce::Result::ok();
}

bool AudioTrackController::enqueueMidiRecordingMessage(
    const juce::MidiMessage& message,
    const double timelineSeconds) noexcept
{
    if (!midiRecordingActive_.load(std::memory_order_acquire)
        || !message.isNoteOnOrOff())
        return false;
    const MidiRecordingEvent event {
        std::max(0.0, timelineSeconds),
        juce::jlimit(0.01f, 1.0f, message.getFloatVelocity()),
        static_cast<std::uint8_t>(juce::jlimit(0, 127,
                                               message.getNoteNumber())),
        static_cast<std::uint8_t>(juce::jlimit(1, 16,
                                               message.getChannel())),
        message.isNoteOn()
    };
    if (midiRecordingEvents_.tryPush(event))
        return true;
    droppedMidiRecordingEvents_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool AudioTrackController::pollMidiRecording()
{
    if (midiRecordingTrackId_ == 0 || midiRecordingClipId_ == 0)
        return false;
    auto* track = findMidiTrack(midiRecordingTrackId_);
    auto* clip = track != nullptr
                     ? findMidiClip(*track, midiRecordingClipId_)
                     : nullptr;
    if (track == nullptr || clip == nullptr)
        return false;

    auto changed = false;
    MidiRecordingEvent event;
    while (midiRecordingEvents_.tryPop(event)) {
        const auto channel = juce::jlimit(1, 16,
                                          static_cast<int>(event.channel));
        const auto noteNumber = juce::jlimit(
            0, 127, static_cast<int>(event.noteNumber));
        const auto activeIndex = static_cast<std::size_t>(
            (channel - 1) * 128 + noteNumber);
        auto& active = activeMidiRecordingNotes_[activeIndex];
        if (event.noteOn) {
            if (active.active)
                changed = closeRecordedMidiNote(channel, noteNumber,
                                                event.timelineSeconds)
                          || changed;
            const auto localBeat = event.timelineSeconds * tempoBpm_ / 60.0
                                   - clip->startBeats;
            if (localBeat < -0.000001)
                continue;
            const auto startBeat = quantiseRecordedBeat(
                std::max(0.0, localBeat));
            const auto noteId = addMidiNote(
                midiRecordingTrackId_, midiRecordingClipId_, noteNumber,
                startBeat, 0.03125, event.velocity);
            if (noteId == 0)
                continue;
            active = { noteId, event.timelineSeconds, startBeat,
                       event.velocity, true };
            ++midiRecordingNoteCount_;
            changed = true;
        } else if (active.active) {
            changed = closeRecordedMidiNote(channel, noteNumber,
                                            event.timelineSeconds)
                      || changed;
        }
    }

    if (midiRecordingActive_.load(std::memory_order_acquire)
        && isPlaying()) {
        const auto now = position();
        for (std::size_t index = 0;
             index < activeMidiRecordingNotes_.size(); ++index) {
            auto& active = activeMidiRecordingNotes_[index];
            if (!active.active)
                continue;
            const auto channel = static_cast<int>(index / 128) + 1;
            const auto noteNumber = static_cast<int>(index % 128);
            const auto adjustedNow = recordingLoopAdjustedTime(
                now, active.startTimelineSeconds);
            const auto endBeat = adjustedNow * tempoBpm_ / 60.0
                                 - clip->startBeats;
            const auto quantisedEnd = quantiseRecordedBeat(
                std::max(active.startBeats, endBeat));
            const auto duration = std::max(
                std::max(0.03125, midiRecordQuantizationStep()),
                quantisedEnd - active.startBeats);
            const auto found = std::find_if(
                clip->notes.begin(), clip->notes.end(),
                [&active](const auto& note) {
                    return note.id == active.noteId;
                });
            if (found != clip->notes.end()
                && std::abs(found->durationBeats - duration) > 0.0001) {
                changed = updateMidiNote(
                              midiRecordingTrackId_, midiRecordingClipId_,
                              active.noteId, noteNumber, active.startBeats,
                              duration, active.velocity)
                          || changed;
            }
            juce::ignoreUnused(channel);
        }
    }
    return changed;
}

void AudioTrackController::stopMidiRecording(const bool commit)
{
    if (midiRecordingTrackId_ == 0 || midiRecordingClipId_ == 0)
        return;
    midiRecordingActive_.store(false, std::memory_order_release);
    (void) pollMidiRecording();
    const auto stopTime = position();
    for (std::size_t index = 0;
         index < activeMidiRecordingNotes_.size(); ++index) {
        if (!activeMidiRecordingNotes_[index].active)
            continue;
        (void) closeRecordedMidiNote(static_cast<int>(index / 128) + 1,
                                     static_cast<int>(index % 128),
                                     stopTime);
    }
    activeMidiRecordingNotes_.fill({});
    if (commit)
        (void) endMidiEdit();
    else
        cancelMidiEdit();
    midiRecordingTrackId_ = 0;
    refreshMidiInputs();
    midiRecordingClipId_ = 0;
    refreshTransportRanges();
}

bool AudioTrackController::isMidiRecording() const noexcept
{
    return midiRecordingActive_.load(std::memory_order_acquire);
}

int AudioTrackController::droppedMidiRecordingEvents() const noexcept
{
    return droppedMidiRecordingEvents_.load(std::memory_order_acquire);
}

std::size_t AudioTrackController::midiRecordingNoteCount() const noexcept
{
    return midiRecordingNoteCount_;
}

void AudioTrackController::setMidiTrackMixer(const std::uint64_t trackId,
                                             const float gainDb,
                                             const float pan,
                                             const bool muted,
                                             const bool solo)
{
    if (auto* track = findMidiTrack(trackId)) {
        track->gainDb = juce::jlimit(-60.0f, 12.0f, gainDb);
        track->pan = juce::jlimit(-1.0f, 1.0f, pan);
        track->muted = muted;
        track->solo = solo;
        track->gainPanSource.setPan(track->pan);
        refreshTrackGains();
    }
}

std::uint64_t AudioTrackController::addMidiAutomationPoint(
    const std::uint64_t trackId, const double timeSeconds,
    const float valueDb)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return 0;
    const auto id = track->nextAutomationId++;
    const auto duration = std::max(
        0.001, std::max(track->synth.length(), length()));
    track->automation.push_back(
        { id, juce::jlimit(0.0, duration, timeSeconds),
          juce::jlimit(-60.0f, 12.0f, valueDb) });
    publishMidiTrackAutomation(*track);
    return id;
}

bool AudioTrackController::updateMidiAutomationPoint(
    const std::uint64_t trackId, const std::uint64_t id,
    const double timeSeconds, const float valueDb)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(
        track->automation.begin(), track->automation.end(),
        [id](const auto& point) { return point.id == id; });
    if (found == track->automation.end())
        return false;
    const auto duration = std::max(
        0.001, std::max(track->synth.length(), length()));
    found->timeSeconds = juce::jlimit(0.0, duration, timeSeconds);
    found->valueDb = juce::jlimit(-60.0f, 12.0f, valueDb);
    publishMidiTrackAutomation(*track);
    return true;
}

bool AudioTrackController::setMidiAutomationPointCurve(
    const std::uint64_t trackId, const std::uint64_t id,
    const AutomationCurve curve, const float curveAmount)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(
        track->automation.begin(), track->automation.end(),
        [id](const auto& point) { return point.id == id; });
    if (found == track->automation.end())
        return false;
    found->curve = curve;
    found->curveAmount = juce::jlimit(-1.0f, 1.0f, curveAmount);
    publishMidiTrackAutomation(*track);
    return true;
}

bool AudioTrackController::removeMidiAutomationPoint(
    const std::uint64_t trackId, const std::uint64_t id)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return false;
    const auto previousSize = track->automation.size();
    std::erase_if(track->automation, [id](const auto& point) {
        return point.id == id;
    });
    if (previousSize == track->automation.size())
        return false;
    publishMidiTrackAutomation(*track);
    return true;
}

std::vector<AudioTrackController::AutomationPoint>
AudioTrackController::midiAutomationPoints(
    const std::uint64_t trackId) const
{
    if (const auto* track = findMidiTrack(trackId))
        return track->automation;
    return {};
}

namespace {

// A lane's points made ready to keep: ids unique (0 or taken: a fresh one),
// times and values in range, in time order.
template <typename Point, typename Value>
void tidyLane(std::vector<Point>& points, std::uint64_t& nextId, const double length, Value&& value)
{
    std::unordered_set<std::uint64_t> seen;
    for (auto& point : points) {
        if (point.id == 0 || seen.contains(point.id))
            point.id = nextId++;
        seen.insert(point.id);
        nextId = std::max(nextId, point.id + 1);
        point.timeSeconds = juce::jlimit(0.0, std::max(0.001, length), point.timeSeconds);
        value(point);
        point.curveAmount = juce::jlimit(-1.0f, 1.0f, point.curveAmount);
    }
    std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
}

} // namespace

void AudioTrackController::replaceMidiAutomationPoints(const std::uint64_t trackId, std::vector<AutomationPoint> points)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return;
    tidyLane(points, track->nextAutomationId, std::max(track->synth.length(), length()),
             [](AutomationPoint& point) { point.valueDb = juce::jlimit(-60.0f, 12.0f, point.valueDb); });
    track->automation = std::move(points);
    publishMidiTrackAutomation(*track);
}

std::uint64_t AudioTrackController::addMidiClipAutomationPoint(
    const std::uint64_t trackId, const std::uint64_t clipId,
    const double timeSeconds, const float valueDb)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return 0;
    const auto duration = std::max(
        0.001, clip->lengthBeats * 60.0 / std::max(20.0, tempoBpm_));
    const auto id = nextClipAutomationPointId_++;
    clip->automation.push_back(
        { id, juce::jlimit(0.0, duration, timeSeconds),
          juce::jlimit(-60.0f, 12.0f, valueDb) });
    std::sort(clip->automation.begin(), clip->automation.end(),
              [](const auto& left, const auto& right) {
                  return left.timeSeconds < right.timeSeconds;
              });
    if (track->activeEditorClipId.load(std::memory_order_acquire)
        == clipId)
        publishMidiClipAutomation(*track, *clip);
    return id;
}

bool AudioTrackController::updateMidiClipAutomationPoint(
    const std::uint64_t trackId, const std::uint64_t clipId,
    const std::uint64_t id, const double timeSeconds, const float valueDb)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return false;
    const auto found = std::find_if(
        clip->automation.begin(), clip->automation.end(),
        [id](const auto& point) { return point.id == id; });
    if (found == clip->automation.end())
        return false;
    const auto duration = std::max(
        0.001, clip->lengthBeats * 60.0 / std::max(20.0, tempoBpm_));
    found->timeSeconds = juce::jlimit(0.0, duration, timeSeconds);
    found->valueDb = juce::jlimit(-60.0f, 12.0f, valueDb);
    std::sort(clip->automation.begin(), clip->automation.end(),
              [](const auto& left, const auto& right) {
                  return left.timeSeconds < right.timeSeconds;
              });
    if (track->activeEditorClipId.load(std::memory_order_acquire)
        == clipId)
        publishMidiClipAutomation(*track, *clip);
    return true;
}

bool AudioTrackController::setMidiClipAutomationPointCurve(
    const std::uint64_t trackId, const std::uint64_t clipId,
    const std::uint64_t id, const AutomationCurve curve,
    const float curveAmount)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return false;
    const auto found = std::find_if(
        clip->automation.begin(), clip->automation.end(),
        [id](const auto& point) { return point.id == id; });
    if (found == clip->automation.end())
        return false;
    found->curve = curve;
    found->curveAmount = juce::jlimit(-1.0f, 1.0f, curveAmount);
    if (track->activeEditorClipId.load(std::memory_order_acquire)
        == clipId)
        publishMidiClipAutomation(*track, *clip);
    return true;
}

bool AudioTrackController::removeMidiClipAutomationPoint(
    const std::uint64_t trackId, const std::uint64_t clipId,
    const std::uint64_t id)
{
    auto* track = findMidiTrack(trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return false;
    const auto previousSize = clip->automation.size();
    std::erase_if(clip->automation, [id](const auto& point) {
        return point.id == id;
    });
    const auto changed = clip->automation.size() != previousSize;
    if (changed
        && track->activeEditorClipId.load(std::memory_order_acquire)
               == clipId)
        publishMidiClipAutomation(*track, *clip);
    return changed;
}

std::vector<AudioTrackController::AutomationPoint>
AudioTrackController::midiClipAutomationPoints(
    const std::uint64_t trackId, const std::uint64_t clipId) const
{
    const auto* track = findMidiTrack(trackId);
    const auto* clip = track != nullptr ? findMidiClip(*track, clipId)
                                        : nullptr;
    return clip != nullptr ? clip->automation
                           : std::vector<AutomationPoint> {};
}

bool AudioTrackController::setMidiTrackArmed(
    const std::uint64_t trackId, const bool armed) noexcept
{
    if (midiRecordingActive_.load(std::memory_order_acquire)
        && trackId != midiRecordingTrackId_)
        return false;
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return false;
    if (!armFollowsMidiInput_) {
        // Each track armed on its own, several at once.
        track->armed = armed;
        refreshMidiInputs();
        return true;
    }
    if (armed)
        return setLiveMidiTrack(trackId);
    track->armed = false;
    track->synth.setLiveInput(false);
    if (liveMidiTrackId_.load(std::memory_order_acquire) == trackId)
        liveMidiTrackId_.store(0, std::memory_order_release);
    return true;
}

juce::Result AudioTrackController::setMidiTrackInstrumentGraph(
    const std::uint64_t trackId,
    const cydaw::engine::PolySynthGraphPlan& graph)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return juce::Result::fail("MIDI instrument track not found.");
    track->synth.setInstrumentGraph(graph);
    return juce::Result::ok();
}

juce::Result AudioTrackController::setMidiTrackEffectGraph(
    const std::uint64_t trackId, const cydaw::engine::NativeDspGraph& graph)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return juce::Result::fail("MIDI instrument track not found.");
    const auto result = track->synth.setEffectGraph(graph);
    if (result.wasOk())
        track->effectGraph = graph;
    return result;
}

juce::Result AudioTrackController::updateMidiTrackEffectParameters(
    const std::uint64_t trackId, const cydaw::engine::NativeDspGraph& graph)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return juce::Result::fail("MIDI instrument track not found.");
    const auto result = track->synth.updateEffectParameters(graph);
    if (result.wasOk())
        track->effectGraph = graph;
    return result;
}

std::vector<std::pair<std::uint64_t, float>>
AudioTrackController::midiTrackControlValues(const std::uint64_t trackId) const
{
    if (const auto* track = findMidiTrack(trackId))
        return track->synth.effects.controlValues();
    return {};
}

juce::Result AudioTrackController::setMidiTrackRackPad(const std::uint64_t trackId, const std::size_t pad,
                                                      const cydaw::engine::PolySynthGraphPlan& plan,
                                                      const cydaw::engine::NativeDspGraph& effects)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return juce::Result::fail("MIDI instrument track not found.");
    auto* slot = track->synth.padAt(pad);
    if (slot == nullptr)
        return juce::Result::fail("No such pad.");
    auto voices = plan;
    voices.voiceCount = 12;
    voices.oscillatorLevel = juce::jlimit(0.0f, cydaw::engine::playsRecordingLevel(voices.waveform) ? 1.0f : 0.5f, voices.oscillatorLevel);
    voices.attackSeconds = juce::jlimit(0.0001f, 5.0f, voices.attackSeconds);
    voices.decaySeconds = juce::jlimit(0.0001f, 5.0f, voices.decaySeconds);
    voices.sustainLevel = juce::jlimit(0.0f, 1.0f, voices.sustainLevel);
    voices.releaseSeconds = juce::jlimit(0.0001f, 10.0f, voices.releaseSeconds);
    voices.filterCutoffHz = juce::jlimit(20.0f, 20'000.0f, voices.filterCutoffHz);
    // A pad's notes reach it already through the track's MIDI effects.
    voices.midiEffects.reset();
    slot->plan.store(std::make_shared<const cydaw::engine::PolySynthGraphPlan>(voices));
    if (slot->effects.updateParameters(effects).wasOk())
        return juce::Result::ok();
    slot->effects.prepare(track->synth.sampleRateHz.load(std::memory_order_acquire), track->synth.blockSize.load(std::memory_order_acquire), 2);
    return slot->effects.setGraph(effects);
}

void AudioTrackController::setMidiTrackRackPadMix(const std::uint64_t trackId, const std::size_t pad, const PadMix& mix)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return;
    auto* slot = mix.note >= 0 ? track->synth.padAt(pad)
                               : (pad < MidiSynthEngine::maximumPads ? track->synth.padStore[pad].get() : nullptr);
    if (slot == nullptr)
        return;
    slot->playAs.store(mix.playAs, std::memory_order_relaxed);
    slot->gain.store(juce::Decibels::decibelsToGain(mix.gainDb, -96.0f), std::memory_order_relaxed);
    slot->pan.store(juce::jlimit(-1.0f, 1.0f, mix.pan), std::memory_order_relaxed);
    slot->muted.store(mix.muted, std::memory_order_relaxed);
    slot->solo.store(mix.solo, std::memory_order_relaxed);
    slot->choke.store(mix.choke, std::memory_order_relaxed);
    slot->keyLow.store(juce::jlimit(0, 127, mix.keyLow), std::memory_order_relaxed);
    slot->keyHigh.store(juce::jlimit(0, 127, mix.keyHigh), std::memory_order_relaxed);
    slot->velocityLow.store(juce::jlimit(1, 127, mix.velocityLow), std::memory_order_relaxed);
    slot->velocityHigh.store(juce::jlimit(1, 127, mix.velocityHigh), std::memory_order_relaxed);
    slot->note.store(mix.note >= 0 && mix.note < 128 ? mix.note : -1, std::memory_order_release);
}

int AudioTrackController::midiTrackSelectedLayer(const std::uint64_t trackId) const
{
    const auto* track = findMidiTrack(trackId);
    return track != nullptr ? track->synth.selectedLayer.load(std::memory_order_relaxed) : -1;
}

float AudioTrackController::midiTrackRackPadPeak(const std::uint64_t trackId, const std::size_t pad) const
{
    if (const auto* track = findMidiTrack(trackId))
        if (pad < MidiSynthEngine::maximumPads && track->synth.padStore[pad] != nullptr)
            return track->synth.padStore[pad]->peak.load(std::memory_order_relaxed);
    return 0.0f;
}

void AudioTrackController::setMidiTrackVoiceModulations(
    const std::uint64_t trackId,
    std::vector<cydaw::engine::PolyVoiceModulation> modulations)
{
    if (auto* track = findMidiTrack(trackId)) {
        const auto current = track->synth.voiceModulations.current();
        if (current != nullptr && *current == modulations)
            return;
        track->synth.voiceModulations.store(
            std::make_shared<const std::vector<cydaw::engine::PolyVoiceModulation>>(std::move(modulations)));
    }
}

void AudioTrackController::setMidiTrackNodeTaps(
    const std::uint64_t trackId,
    const std::vector<NativeDspRuntime::TapRequest>& taps)
{
    if (auto* track = findMidiTrack(trackId))
        track->synth.effects.setTaps(taps);
}

void AudioTrackController::setMidiTrackNativeAutomation(const std::uint64_t trackId,
                                                        std::vector<NativeDspRuntime::AutomationLane> lanes)
{
    if (auto* track = findMidiTrack(trackId))
        track->synth.effects.setAutomation(std::move(lanes));
}

void AudioTrackController::setMidiTrackVoiceAutomation(const std::uint64_t trackId, std::vector<VoiceAutomationLane> lanes)
{
    if (auto* track = findMidiTrack(trackId)) {
        std::erase_if(lanes, [](const auto& lane) { return lane.points.empty(); });
        track->synth.voiceAutomation.store(
            lanes.empty() ? nullptr : std::make_shared<const std::vector<VoiceAutomationLane>>(std::move(lanes)));
    }
}

bool AudioTrackController::readMidiTrackNodeTap(
    const std::uint64_t trackId, const std::uint64_t nodeId, const bool input,
    cydaw::engine::AudioAnalysisFrame& frame) const
{
    if (const auto* track = findMidiTrack(trackId))
        return track->synth.effects.readTap(nodeId, input, frame);
    return false;
}

cydaw::engine::NativeDspGraph AudioTrackController::midiTrackEffectGraph(
    const std::uint64_t trackId) const
{
    if (const auto* track = findMidiTrack(trackId))
        return track->effectGraph;
    return {};
}

cydaw::engine::PolySynthGraphPlan AudioTrackController::midiTrackInstrumentGraph(
    const std::uint64_t trackId) const
{
    if (const auto* track = findMidiTrack(trackId))
        return track->synth.instrumentPlan();
    return {};
}

bool AudioTrackController::setLiveMidiTrack(const std::uint64_t trackId) noexcept
{
    if (midiRecordingActive_.load(std::memory_order_acquire)
        && trackId != midiRecordingTrackId_)
        return false;
    if (findMidiTrack(trackId) == nullptr)
        return false;
    liveMidiTrackId_.store(trackId, std::memory_order_release);
    if (!armFollowsMidiInput_) {
        // Picking a track arms nothing: the arms are the person's.
        refreshMidiInputs();
        return true;
    }
    for (auto& track : midiTracks_) {
        track->armed = track->id == trackId;
        track->synth.setLiveInput(track->armed && !simulationTakesMidi());
    }
    return true;
}

std::uint64_t AudioTrackController::liveMidiTrackId() const noexcept
{
    return liveMidiTrackId_.load(std::memory_order_acquire);
}

bool AudioTrackController::startMidiClipPreview(
    const std::uint64_t trackId, const std::uint64_t clipId,
    const int sceneIndex)
{
    auto* track = findMidiTrack(trackId);
    const auto* clip = track != nullptr
                           ? findMidiClip(*track, clipId) : nullptr;
    if (track == nullptr || clip == nullptr || sceneIndex < 0
        || sceneIndex >= maximumLauncherScenes)
        return false;

    setArrangementPlaybackEnabled(false);
    const auto secondsPerBeat = 60.0 / std::max(20.0, tempoBpm_);
    const auto playStart = juce::jlimit(
        0.0, std::max(0.0, clip->lengthBeats - 0.03125),
        clip->playStartBeats);
    const auto playEnd = juce::jlimit(
        playStart + 0.03125, clip->lengthBeats,
        clip->playEndBeats > playStart ? clip->playEndBeats
                                       : clip->lengthBeats);
    const auto loopStart = juce::jlimit(
        playStart, std::max(playStart, playEnd - 0.03125),
        clip->loopStartBeats);
    const auto loopEnd = juce::jlimit(
        loopStart + 0.03125, playEnd,
        clip->loopEndBeats > loopStart ? clip->loopEndBeats : playEnd);
    track->synth.setPlaying(false);
    track->activeEditorClipId.store(0, std::memory_order_release);
    publishMidiClipPreviewSequence(*track, *clip);
    publishMidiClipAutomation(*track, *clip);
    track->synth.setTransportRange(playEnd * secondsPerBeat,
                                   loopStart * secondsPerBeat,
                                   loopEnd * secondsPerBeat);
    track->synth.setLooping(clip->looping);
    track->synth.setPosition(playStart * secondsPerBeat);
    track->launcherStartSeconds.store(playStart * secondsPerBeat,
                                      std::memory_order_release);
    track->launcherEndSeconds.store(playEnd * secondsPerBeat,
                                    std::memory_order_release);
    track->activeLauncherScene.store(sceneIndex,
                                     std::memory_order_release);
    track->synth.setPlaying(true);
    resetLatencyCompensation();
    return true;
}

bool AudioTrackController::stopMidiClipPreview(
    const std::uint64_t trackId)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr)
        return false;
    track->synth.setPlaying(false);
    track->activeEditorClipId.store(0, std::memory_order_release);
    track->activeLauncherScene.store(-1, std::memory_order_release);
    publishMidiSequence(*track);
    publishMidiTrackAutomation(*track);
    resetLatencyCompensation();
    return true;
}

bool AudioTrackController::startMidiClipEditorPreview(
    const std::uint64_t trackId, const std::uint64_t clipId,
    const double localStartSeconds)
{
    auto* track = findMidiTrack(trackId);
    const auto* clip = track != nullptr ? findMidiClip(*track, clipId)
                                        : nullptr;
    if (track == nullptr || clip == nullptr)
        return false;

    const auto secondsPerBeat = 60.0 / std::max(20.0, tempoBpm_);
    const auto playStart = juce::jlimit(
        0.0, std::max(0.0, clip->lengthBeats - 0.03125),
        clip->playStartBeats);
    const auto playEnd = juce::jlimit(
        playStart + 0.03125, clip->lengthBeats,
        clip->playEndBeats > playStart ? clip->playEndBeats
                                       : clip->lengthBeats);
    const auto loopStart = juce::jlimit(
        playStart, std::max(playStart, playEnd - 0.03125),
        clip->loopStartBeats);
    const auto loopEnd = juce::jlimit(
        loopStart + 0.03125, playEnd,
        clip->loopEndBeats > loopStart ? clip->loopEndBeats : playEnd);
    const auto playStartSeconds = playStart * secondsPerBeat;
    const auto playEndSeconds = playEnd * secondsPerBeat;
    const auto loopStartSeconds = loopStart * secondsPerBeat;
    const auto loopEndSeconds = loopEnd * secondsPerBeat;
    auto position = juce::jlimit(playStartSeconds, playEndSeconds,
                                 std::max(0.0, localStartSeconds));
    if (clip->looping && position >= loopEndSeconds)
        position = loopStartSeconds;

    setArrangementPlaybackEnabled(false);
    track->synth.setPlaying(false);
    track->activeLauncherScene.store(-1, std::memory_order_release);
    track->activeEditorClipId.store(clipId, std::memory_order_release);
    publishMidiClipPreviewSequence(*track, *clip);
    publishMidiClipAutomation(*track, *clip);
    track->synth.setTransportRange(playEndSeconds, loopStartSeconds,
                                   loopEndSeconds);
    track->synth.setLooping(clip->looping);
    track->synth.setPosition(position);
    track->synth.setPlaying(position < playEndSeconds);
    resetLatencyCompensation();
    return position < playEndSeconds;
}

bool AudioTrackController::seekMidiClipEditorPreview(
    const std::uint64_t trackId, const std::uint64_t clipId,
    const double localSeconds)
{
    auto* track = findMidiTrack(trackId);
    const auto* clip = track != nullptr ? findMidiClip(*track, clipId)
                                        : nullptr;
    if (track == nullptr || clip == nullptr
        || track->activeEditorClipId.load(std::memory_order_acquire)
               != clipId)
        return false;
    const auto secondsPerBeat = 60.0 / std::max(20.0, tempoBpm_);
    const auto start = clip->playStartBeats * secondsPerBeat;
    const auto endBeat = clip->playEndBeats > clip->playStartBeats
                             ? clip->playEndBeats : clip->lengthBeats;
    const auto end = endBeat * secondsPerBeat;
    track->synth.setPosition(juce::jlimit(start, end,
                                          std::max(0.0, localSeconds)));
    return true;
}

bool AudioTrackController::stopMidiClipEditorPreview(
    const std::uint64_t trackId)
{
    auto* track = findMidiTrack(trackId);
    if (track == nullptr
        || track->activeEditorClipId.load(std::memory_order_acquire) == 0)
        return false;
    return stopMidiClipPreview(trackId);
}

bool AudioTrackController::isMidiClipEditorPreviewing(
    const std::uint64_t trackId, const std::uint64_t clipId) const noexcept
{
    const auto* track = findMidiTrack(trackId);
    return track != nullptr && clipId != 0 && track->synth.isPlaying()
           && track->activeEditorClipId.load(std::memory_order_acquire)
                  == clipId;
}

double AudioTrackController::midiClipEditorPreviewPositionSeconds(
    const std::uint64_t trackId) const noexcept
{
    const auto* track = findMidiTrack(trackId);
    return track != nullptr
               && track->activeEditorClipId.load(std::memory_order_acquire)
                      != 0
               ? std::max(0.0, track->synth.position()) : -1.0;
}

int AudioTrackController::activeMidiLauncherScene(
    const std::uint64_t trackId) const noexcept
{
    const auto* track = findMidiTrack(trackId);
    return track != nullptr && track->synth.isPlaying()
               ? track->activeLauncherScene.load(std::memory_order_acquire)
               : -1;
}

double AudioTrackController::midiLauncherClipPositionSeconds(
    const std::uint64_t trackId) const noexcept
{
    const auto* track = findMidiTrack(trackId);
    if (track == nullptr
        || track->activeLauncherScene.load(std::memory_order_acquire) < 0)
        return 0.0;
    return std::max(
        0.0, track->synth.position()
                 - track->launcherStartSeconds.load(
                       std::memory_order_acquire));
}

bool AudioTrackController::midiTrackVoiceLive(const std::uint64_t trackId, cydaw::engine::VoiceLive& out) const noexcept
{
    const auto* track = findMidiTrack(trackId);
    return track != nullptr && track->synth.live.read(out);
}

float AudioTrackController::midiLauncherClipProgress(
    const std::uint64_t trackId) const noexcept
{
    const auto* track = findMidiTrack(trackId);
    if (track == nullptr
        || track->activeLauncherScene.load(std::memory_order_acquire) < 0)
        return 0.0f;
    const auto start = track->launcherStartSeconds.load(
        std::memory_order_acquire);
    const auto end = track->launcherEndSeconds.load(
        std::memory_order_acquire);
    return juce::jlimit(
        0.0f, 1.0f,
        static_cast<float>((track->synth.position() - start)
                           / std::max(0.000001, end - start)));
}

juce::Result AudioTrackController::selfTestMidiLauncherClip()
{
    AudioTrackController controller;
    controller.clearMidiTracks();
    const auto trackId = controller.addMidiTrack("Launcher MIDI");
    auto tracks = controller.midiTracks();
    if (tracks.empty())
        return juce::Result::fail("Cannot create a MIDI launcher track");
    for (const auto& clip : tracks.front().clips)
        (void) controller.removeMidiClip(trackId, clip.id);

    const auto clipId = controller.addMidiClip(
        trackId, "Scene 4", 48.0, 4.0, false, 0xff4fd3ae);
    const auto laterClipId = controller.addMidiClip(
        trackId, "Scene 5", 64.0, 4.0, false, 0xff66b5ff);
    if (clipId == 0 || laterClipId == 0
        || controller.addMidiNote(trackId, clipId, 60, 0.0, 1.0,
                                  0.8f) == 0
        || controller.addMidiNote(trackId, laterClipId, 67, 0.0, 1.0,
                                  0.8f) == 0)
        return juce::Result::fail("Cannot populate MIDI launcher clips");

    auto* track = controller.findMidiTrack(trackId);
    if (track == nullptr)
        return juce::Result::fail("MIDI launcher track disappeared");
    auto graph = track->synth.instrumentPlan();
    graph.enabled = true;
    graph.attackSeconds = 0.0001f;
    graph.decaySeconds = 0.0001f;
    graph.sustainLevel = 1.0f;
    graph.releaseSeconds = 0.01f;
    graph.oscillatorLevel = 0.15f;
    track->synth.setInstrumentGraph(graph);
    track->synth.prepareToPlay(256, 48'000.0);

    if (!controller.startMidiClipPreview(trackId, clipId, 3)) {
        track->synth.releaseResources();
        return juce::Result::fail("MIDI launcher clip did not start");
    }
    const auto expectedDuration = 4.0 * 60.0 / controller.tempo();
    if (std::abs(track->synth.length() - expectedDuration) > 0.000001
        || track->synth.position() > 0.000001) {
        track->synth.releaseResources();
        return juce::Result::fail(
            "MIDI launcher preview did not use clip-local time");
    }

    juce::AudioBuffer<float> rendered(2, 2'048);
    rendered.clear();
    track->synth.getNextAudioBlock(
        { &rendered, 0, rendered.getNumSamples() });
    const auto audible = rendered.getRMSLevel(
        0, 0, rendered.getNumSamples());
    const auto progressed = controller.midiLauncherClipProgress(trackId);
    (void) controller.stopMidiClipPreview(trackId);

    if (!controller.setMidiClipTimelineBounds(
            trackId, clipId, 4.0, 0.5, 3.5, 1.0, 3.0, true)
        || controller.addMidiNote(trackId, clipId, 64, 1.0, 1.0,
                                  0.8f) == 0
        || controller.addMidiClipAutomationPoint(
               trackId, clipId, 0.0, -6.0f) == 0
        || !controller.startMidiClipEditorPreview(
               trackId, clipId, 0.75)) {
        track->synth.releaseResources();
        return juce::Result::fail(
            "MIDI clip editor preview did not start");
    }
    const auto editorStartedLocally
        = controller.isMidiClipEditorPreviewing(trackId, clipId)
          && std::abs(controller.midiClipEditorPreviewPositionSeconds(trackId)
                      - 0.75) < 0.000001;
    const auto editorSequenceUsesClipTimeline
        = std::abs(track->synth.length() - 1.75) < 0.000001;
    const auto editorSeeked = controller.seekMidiClipEditorPreview(
        trackId, clipId, 1.25);
    rendered.clear();
    track->synth.getNextAudioBlock(
        { &rendered, 0, rendered.getNumSamples() });
    const auto editorProgressed
        = controller.midiClipEditorPreviewPositionSeconds(trackId) > 1.25;
    (void) controller.stopMidiClipEditorPreview(trackId);
    const auto arrangementRestored = track->synth.length()
                                     > expectedDuration * 2.0;
    track->synth.releaseResources();
    if (audible <= 0.001f || progressed <= 0.0f
        || !editorStartedLocally || !editorSequenceUsesClipTimeline
        || !editorSeeked || !editorProgressed || !arrangementRestored)
        return juce::Result::fail(
            "MIDI launcher/editor playback was silent, unsynchronised, or did not restore arrangement");
    return juce::Result::ok();
}

void AudioTrackController::setTimeSignature(const int numerator, const int denominator) noexcept
{
    timeSignatureNumerator_ = juce::jlimit(1, 32, numerator);
    timeSignatureDenominator_ = denominator == 1 || denominator == 2 || denominator == 8 || denominator == 16
                                        || denominator == 32
                                    ? denominator
                                    : 4;
}

void AudioTrackController::setTempo(const double beatsPerMinute)
{
    const auto before = tempoBpm_;
    tempoBpm_ = juce::jlimit(20.0, 400.0, beatsPerMinute);
    NativeDspRuntime::setSessionTempo(tempoBpm_);
    // A loop auditioned at the tempo follows it.
    if (tempoPreview_.file != juce::File() && before != tempoBpm_)
        (void) startTempoPreview(tempoPreview_.file, samplePreviewPositionSeconds(), tempoPreview_.tempo);
    publishAllMidiSequences();
    if (simulation_ != nullptr && !simulationPattern_.empty())
        publishSimulationPattern();
}

double AudioTrackController::tempo() const noexcept { return tempoBpm_; }
double AudioTrackController::midiLengthSeconds() const noexcept
{
    double lengthSeconds = 0.0;
    for (const auto& track : midiTracks_)
        lengthSeconds = std::max(lengthSeconds, track->synth.length());
    return lengthSeconds;
}

std::uint64_t AudioTrackController::addAutomationPoint(const std::size_t trackIndex,
                                                       const double timeSeconds,
                                                       const float valueDb)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return 0;
    const auto id = track->nextAutomationId++;
    track->automation.push_back({ id,
                                  juce::jlimit(0.0, track->clipSource->arrangementLengthSeconds(), timeSeconds),
                                  juce::jlimit(-60.0f, 12.0f, valueDb) });
    publishTrackAutomation(*track);
    return id;
}

bool AudioTrackController::updateAutomationPoint(const std::size_t trackIndex,
                                                 const std::uint64_t id,
                                                 const double timeSeconds,
                                                 const float valueDb)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(track->automation.begin(), track->automation.end(), [id](const auto& point) {
        return point.id == id;
    });
    if (found == track->automation.end())
        return false;
    found->timeSeconds = juce::jlimit(0.0, track->clipSource->arrangementLengthSeconds(), timeSeconds);
    found->valueDb = juce::jlimit(-60.0f, 12.0f, valueDb);
    publishTrackAutomation(*track);
    return true;
}

bool AudioTrackController::setAutomationPointCurve(
    const std::size_t trackIndex, const std::uint64_t id,
    const AutomationCurve curve, const float curveAmount)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    const auto found = std::find_if(
        track->automation.begin(), track->automation.end(),
        [id](const auto& point) { return point.id == id; });
    if (found == track->automation.end())
        return false;
    found->curve = curve;
    found->curveAmount = juce::jlimit(-1.0f, 1.0f, curveAmount);
    publishTrackAutomation(*track);
    return true;
}

bool AudioTrackController::removeAutomationPoint(const std::size_t trackIndex, const std::uint64_t id)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    const auto oldSize = track->automation.size();
    std::erase_if(track->automation, [id](const auto& point) { return point.id == id; });
    if (track->automation.size() == oldSize)
        return false;
    publishTrackAutomation(*track);
    return true;
}

void AudioTrackController::clearAutomation(const std::size_t trackIndex)
{
    if (auto* track = findTrack(trackIndex)) {
        track->automation.clear();
        publishTrackAutomation(*track);
    }
}

std::array<float, 4> AudioTrackController::trackInputLevels(const std::size_t trackIndex) const noexcept
{
    std::array<float, 4> levels {};
    if (const auto* track = findTrack(trackIndex)) {
        for (std::size_t bus = 0; bus < levels.size(); ++bus)
            levels[bus] = track->gainPanSource.inputLevel(bus);
        // The main input: what enters the track, its clips with it.
        levels[0] = std::max(levels[0], track->gainPanSource.soundInLevel());
    }
    return levels;
}

std::vector<AudioTrackController::AutomationPoint>
AudioTrackController::automationPoints(const std::size_t trackIndex) const
{
    if (const auto* track = findTrack(trackIndex))
        return track->automation;
    return {};
}

void AudioTrackController::replaceAutomationPoints(const std::size_t trackIndex, std::vector<AutomationPoint> points)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return;
    tidyLane(points, track->nextAutomationId, track->clipSource->arrangementLengthSeconds(),
             [](AutomationPoint& point) { point.valueDb = juce::jlimit(-60.0f, 12.0f, point.valueDb); });
    track->automation = std::move(points);
    publishTrackAutomation(*track);
}

void AudioTrackController::replacePluginAutomationPoints(const std::size_t trackIndex, const std::size_t pluginIndex,
                                                         const std::uint32_t parameterIndex, juce::String parameterName,
                                                         std::vector<PluginAutomationPoint> points)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr || pluginIndex >= track->gainPanSource.pluginCount())
        return;
    auto lane = std::find_if(track->pluginAutomation.begin(), track->pluginAutomation.end(),
                             [pluginIndex, parameterIndex](const auto& candidate) {
                                 return candidate.pluginIndex == pluginIndex && candidate.parameterIndex == parameterIndex;
                             });
    if (points.empty()) {
        if (lane != track->pluginAutomation.end())
            track->pluginAutomation.erase(lane);
        publishTrackPluginAutomation(*track);
        return;
    }
    if (lane == track->pluginAutomation.end()) {
        track->pluginAutomation.push_back({ pluginIndex, parameterIndex, std::move(parameterName), {} });
        lane = std::prev(track->pluginAutomation.end());
    } else if (parameterName.isNotEmpty()) {
        lane->parameterName = std::move(parameterName);
    }
    tidyLane(points, track->nextPluginAutomationId, track->clipSource->arrangementLengthSeconds(),
             [](PluginAutomationPoint& point) { point.normalisedValue = juce::jlimit(0.0f, 1.0f, point.normalisedValue); });
    lane->points = std::move(points);
    publishTrackPluginAutomation(*track);
}

std::uint64_t AudioTrackController::addPluginAutomationPoint(
    const std::size_t trackIndex,
    const std::size_t pluginIndex,
    const std::uint32_t parameterIndex,
    juce::String parameterName,
    const double timeSeconds,
    const float normalisedValue)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr || pluginIndex >= track->gainPanSource.pluginCount())
        return 0;

    auto lane = std::find_if(track->pluginAutomation.begin(),
                             track->pluginAutomation.end(),
                             [pluginIndex, parameterIndex](const auto& candidate) {
                                 return candidate.pluginIndex == pluginIndex
                                        && candidate.parameterIndex == parameterIndex;
                             });
    if (lane == track->pluginAutomation.end()) {
        track->pluginAutomation.push_back({ pluginIndex, parameterIndex,
                                            std::move(parameterName), {} });
        lane = std::prev(track->pluginAutomation.end());
    } else if (parameterName.isNotEmpty()) {
        lane->parameterName = std::move(parameterName);
    }

    const auto id = track->nextPluginAutomationId++;
    lane->points.push_back({ id,
                             juce::jlimit(0.0, track->clipSource->arrangementLengthSeconds(),
                                          timeSeconds),
                             juce::jlimit(0.0f, 1.0f, normalisedValue) });
    publishTrackPluginAutomation(*track);
    return id;
}

bool AudioTrackController::updatePluginAutomationPoint(
    const std::size_t trackIndex,
    const std::size_t pluginIndex,
    const std::uint32_t parameterIndex,
    const std::uint64_t id,
    const double timeSeconds,
    const float normalisedValue)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    const auto lane = std::find_if(track->pluginAutomation.begin(),
                                   track->pluginAutomation.end(),
                                   [pluginIndex, parameterIndex](const auto& candidate) {
                                       return candidate.pluginIndex == pluginIndex
                                              && candidate.parameterIndex == parameterIndex;
                                   });
    if (lane == track->pluginAutomation.end())
        return false;
    const auto point = std::find_if(lane->points.begin(), lane->points.end(), [id](const auto& candidate) {
        return candidate.id == id;
    });
    if (point == lane->points.end())
        return false;
    point->timeSeconds = juce::jlimit(0.0, track->clipSource->arrangementLengthSeconds(),
                                      timeSeconds);
    point->normalisedValue = juce::jlimit(0.0f, 1.0f, normalisedValue);
    publishTrackPluginAutomation(*track);
    return true;
}

bool AudioTrackController::setPluginAutomationPointCurve(
    const std::size_t trackIndex, const std::size_t pluginIndex,
    const std::uint32_t parameterIndex, const std::uint64_t id,
    const AutomationCurve curve, const float curveAmount)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    const auto lane = std::find_if(
        track->pluginAutomation.begin(), track->pluginAutomation.end(),
        [pluginIndex, parameterIndex](const auto& candidate) {
            return candidate.pluginIndex == pluginIndex
                   && candidate.parameterIndex == parameterIndex;
        });
    if (lane == track->pluginAutomation.end())
        return false;
    const auto point = std::find_if(
        lane->points.begin(), lane->points.end(),
        [id](const auto& candidate) { return candidate.id == id; });
    if (point == lane->points.end())
        return false;
    point->curve = curve;
    point->curveAmount = juce::jlimit(-1.0f, 1.0f, curveAmount);
    publishTrackPluginAutomation(*track);
    return true;
}

bool AudioTrackController::removePluginAutomationPoint(
    const std::size_t trackIndex,
    const std::size_t pluginIndex,
    const std::uint32_t parameterIndex,
    const std::uint64_t id)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    const auto lane = std::find_if(track->pluginAutomation.begin(),
                                   track->pluginAutomation.end(),
                                   [pluginIndex, parameterIndex](const auto& candidate) {
                                       return candidate.pluginIndex == pluginIndex
                                              && candidate.parameterIndex == parameterIndex;
                                   });
    if (lane == track->pluginAutomation.end())
        return false;
    const auto previousSize = lane->points.size();
    std::erase_if(lane->points, [id](const auto& point) { return point.id == id; });
    if (lane->points.size() == previousSize)
        return false;
    if (lane->points.empty())
        track->pluginAutomation.erase(lane);
    publishTrackPluginAutomation(*track);
    return true;
}

void AudioTrackController::clearPluginAutomation(const std::size_t trackIndex,
                                                  const std::size_t pluginIndex,
                                                  const std::uint32_t parameterIndex)
{
    if (auto* track = findTrack(trackIndex)) {
        std::erase_if(track->pluginAutomation, [pluginIndex, parameterIndex](const auto& lane) {
            return lane.pluginIndex == pluginIndex && lane.parameterIndex == parameterIndex;
        });
        publishTrackPluginAutomation(*track);
    }
}

std::vector<AudioTrackController::PluginAutomationPoint>
AudioTrackController::pluginAutomationPoints(const std::size_t trackIndex,
                                              const std::size_t pluginIndex,
                                              const std::uint32_t parameterIndex) const
{
    const auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return {};
    const auto lane = std::find_if(track->pluginAutomation.begin(),
                                   track->pluginAutomation.end(),
                                   [pluginIndex, parameterIndex](const auto& candidate) {
                                       return candidate.pluginIndex == pluginIndex
                                              && candidate.parameterIndex == parameterIndex;
                                   });
    return lane != track->pluginAutomation.end() ? lane->points
                                                  : std::vector<PluginAutomationPoint> {};
}

std::vector<AudioTrackController::PluginAutomationLane>
AudioTrackController::pluginAutomationLanes(const std::size_t trackIndex) const
{
    if (const auto* track = findTrack(trackIndex))
        return track->pluginAutomation;
    return {};
}

bool AudioTrackController::setLauncherClip(const std::size_t trackIndex,
                                           const int sceneIndex,
                                           juce::String name,
                                           const double sourceOffsetSeconds,
                                           const double durationSeconds,
                                           const bool looping,
                                           const std::uint32_t colour,
                                           const juce::File& sourceFile)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr || sceneIndex < 0 || sceneIndex >= maximumLauncherScenes)
        return false;
    const auto resolvedSource = sourceFile.existsAsFile()
                                    ? sourceFile
                                    : track->file;
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(resolvedSource));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0)
        return false;
    const auto sourceLength = static_cast<double>(reader->lengthInSamples)
                              / reader->sampleRate;
    const auto minimumDuration = 1.0 / reader->sampleRate;
    const auto offset = juce::jlimit(
        0.0, std::max(0.0, sourceLength - minimumDuration),
        sourceOffsetSeconds);
    const auto duration = juce::jlimit(
        minimumDuration,
        std::max(minimumDuration, sourceLength - offset), durationSeconds);
    auto clip = std::find_if(track->launcherClips.begin(), track->launcherClips.end(),
                             [sceneIndex](const auto& candidate) {
                                 return candidate.sceneIndex == sceneIndex;
                             });
    const auto previous = track->launcherClips;
    if (clip != track->launcherClips.end()
        && clip->sourceFile == resolvedSource && clip->usesEventTimeline) {
        clip->name = name.isNotEmpty() ? std::move(name) : clip->name;
        clip->looping = looping;
        clip->colour = colour;
        normaliseLauncherClipTimeline(*clip);
    } else {
        LauncherClip value;
        value.sceneIndex = sceneIndex;
        value.name = name.isNotEmpty()
                         ? std::move(name)
                         : "Scene " + juce::String(sceneIndex + 1);
        value.sourceFile = resolvedSource;
        value.sourceOffsetSeconds = offset;
        value.durationSeconds = duration;
        value.looping = looping;
        value.colour = colour;
        value.playEndSeconds = duration;
        value.loopEndSeconds = duration;
        value.usesEventTimeline = true;
        value.events.push_back({
            nextAudioEventId_++, value.name, 0.0, offset,
            duration, duration, colour, resolvedSource, sourceLength });
        if (clip == track->launcherClips.end())
            track->launcherClips.push_back(std::move(value));
        else
            *clip = std::move(value);
    }
    std::sort(track->launcherClips.begin(), track->launcherClips.end(),
              [](const auto& first, const auto& second) {
                  return first.sceneIndex < second.sceneIndex;
              });
    if (const auto result = rebuildLauncherPlans(*track); result.failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

bool AudioTrackController::createEmptyLauncherClip(
    const std::size_t trackIndex, const int sceneIndex, juce::String name,
    const double durationSeconds, const std::uint32_t colour)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr || sceneIndex < 0
        || sceneIndex >= maximumLauncherScenes)
        return false;
    auto clip = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [sceneIndex](const auto& value) {
                                 return value.sceneIndex == sceneIndex;
                             });
    if (clip != track->launcherClips.end())
        return false;
    const auto previous = track->launcherClips;
    LauncherClip value;
    value.sceneIndex = sceneIndex;
    value.name = name.isNotEmpty() ? std::move(name)
                                   : "Clip " + juce::String(sceneIndex + 1);
    value.durationSeconds = juce::jlimit(0.001, 86'400.0,
                                         durationSeconds);
    value.playEndSeconds = value.durationSeconds;
    value.loopEndSeconds = value.durationSeconds;
    value.looping = true;
    value.colour = colour;
    value.usesEventTimeline = true;
    track->launcherClips.push_back(std::move(value));
    std::sort(track->launcherClips.begin(), track->launcherClips.end(),
              [](const auto& first, const auto& second) {
                  return first.sceneIndex < second.sceneIndex;
              });
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

std::optional<AudioTrackController::LauncherClip>
AudioTrackController::launcherClip(const std::size_t trackIndex,
                                   const int sceneIndex) const
{
    const auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return std::nullopt;
    const auto clip = std::find_if(track->launcherClips.begin(),
                                   track->launcherClips.end(),
                                   [sceneIndex](const auto& value) {
                                       return value.sceneIndex == sceneIndex;
                                   });
    if (clip == track->launcherClips.end())
        return std::nullopt;
    auto result = *clip;
    normaliseLauncherClipTimeline(result);
    return result;
}

bool AudioTrackController::setLauncherClipTimelineBounds(
    const std::size_t trackIndex, const int sceneIndex,
    const double durationSeconds, const double playStartSeconds,
    const double playEndSeconds, const double loopStartSeconds,
    const double loopEndSeconds, const bool looping)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [sceneIndex](const auto& value) {
                                 return value.sceneIndex == sceneIndex;
                             });
    if (clip == track->launcherClips.end())
        return false;
    const auto previous = track->launcherClips;
    clip->durationSeconds = durationSeconds;
    clip->playStartSeconds = playStartSeconds;
    clip->playEndSeconds = playEndSeconds;
    clip->loopStartSeconds = loopStartSeconds;
    clip->loopEndSeconds = loopEndSeconds;
    clip->looping = looping;
    normaliseLauncherClipTimeline(*clip);
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

juce::Result AudioTrackController::addLauncherClipEventFromFile(
    const std::size_t trackIndex, const int sceneIndex,
    const juce::File& sourceFile, const double localStartSeconds,
    std::uint64_t* const newEventId)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    auto clip = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [sceneIndex](const auto& value) {
                                 return value.sceneIndex == sceneIndex;
                             });
    if (clip == track->launcherClips.end())
        return juce::Result::fail("Launcher clip not found");
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(sourceFile));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0)
        return juce::Result::fail(
            tr("error.openAudio", "file", sourceFile.getFileName()));
    const auto sourceLength = static_cast<double>(reader->lengthInSamples)
                              / reader->sampleRate;
    const auto previous = track->launcherClips;
    materialiseLegacyLauncherEvent(*clip);
    for (auto& event : clip->events)
        if (event.id == 0)
            event.id = nextAudioEventId_++;
    const auto eventId = nextAudioEventId_++;
    const auto start = std::max(0.0, localStartSeconds);
    clip->events.push_back({ eventId,
                             sourceFile.getFileNameWithoutExtension(),
                             start, 0.0, sourceLength, sourceLength,
                             0xff53d0c2, sourceFile, sourceLength });
    const auto oldDuration = clip->durationSeconds;
    clip->durationSeconds = std::max(oldDuration, start + sourceLength);
    if (clip->playEndSeconds >= oldDuration - 0.001)
        clip->playEndSeconds = clip->durationSeconds;
    if (clip->loopEndSeconds >= oldDuration - 0.001)
        clip->loopEndSeconds = clip->durationSeconds;
    normaliseLauncherClipTimeline(*clip);
    if (const auto result = rebuildLauncherPlans(*track); result.failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return result;
    }
    if (newEventId != nullptr)
        *newEventId = eventId;
    return juce::Result::ok();
}

juce::Result AudioTrackController::replaceLauncherClipEvents(
    const std::size_t trackIndex, const int sceneIndex,
    std::vector<AudioEvent> events)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return juce::Result::fail(tr("error.exportNoTrack"));
    auto clip = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [sceneIndex](const auto& value) {
                                 return value.sceneIndex == sceneIndex;
                             });
    if (clip == track->launcherClips.end())
        return juce::Result::fail("Launcher clip not found");
    for (auto& event : events) {
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager_.createReaderFor(event.sourceFile));
        if (reader == nullptr || reader->sampleRate <= 0.0
            || reader->lengthInSamples <= 0)
            return juce::Result::fail(
                tr("error.openAudio", "file",
                   event.sourceFile.getFileName()));
        event.sourceLengthSeconds
            = static_cast<double>(reader->lengthInSamples)
              / reader->sampleRate;
        if (event.id == 0)
            event.id = nextAudioEventId_++;
        else
            nextAudioEventId_ = std::max(nextAudioEventId_, event.id + 1);
        for (auto& marker : event.stretchMarkers) {
            if (marker.id == 0)
                marker.id = nextStretchMarkerId_++;
            else
                nextStretchMarkerId_ = std::max(nextStretchMarkerId_,
                                                marker.id + 1);
        }
    }
    const auto previous = track->launcherClips;
    clip->events = std::move(events);
    clip->usesEventTimeline = true;
    normaliseLauncherClipTimeline(*clip);
    if (const auto result = rebuildLauncherPlans(*track); result.failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return result;
    }
    return juce::Result::ok();
}

bool AudioTrackController::updateLauncherClipEvent(
    const std::size_t trackIndex, const int sceneIndex,
    const std::uint64_t eventId, const double localStartSeconds,
    const double sourceOffsetSeconds, const double durationSeconds,
    const double sourceDurationSeconds, const double fadeInSeconds,
    const double fadeOutSeconds)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [sceneIndex](const auto& value) {
                                 return value.sceneIndex == sceneIndex;
                             });
    if (clip == track->launcherClips.end())
        return false;
    const auto previous = track->launcherClips;
    materialiseLegacyLauncherEvent(*clip);
    for (auto& value : clip->events)
        if (value.id == 0)
            value.id = eventId == std::numeric_limits<std::uint64_t>::max()
                           ? eventId
                           : nextAudioEventId_++;
    auto event = std::find_if(clip->events.begin(), clip->events.end(),
                              [eventId](const auto& value) {
                                  return value.id == eventId;
                              });
    if (event == clip->events.end() && clip->events.size() == 1
        && clip->events.front().id == 0) {
        clip->events.front().id = eventId;
        event = clip->events.begin();
    }
    if (event == clip->events.end())
        return false;
    event->startSeconds = std::max(0.0, localStartSeconds);
    event->sourceOffsetSeconds = std::max(0.0, sourceOffsetSeconds);
    event->durationSeconds = juce::jlimit(0.001, 86'400.0,
                                         durationSeconds);
    event->sourceDurationSeconds = juce::jlimit(
        0.001, 86'400.0, sourceDurationSeconds);
    if (fadeInSeconds >= 0.0)
        event->fadeInSeconds = juce::jlimit(
            0.0, event->durationSeconds, fadeInSeconds);
    if (fadeOutSeconds >= 0.0)
        event->fadeOutSeconds = juce::jlimit(
            0.0, event->durationSeconds, fadeOutSeconds);
    std::erase_if(event->stretchMarkers, [event](const auto& marker) {
        return marker.sourceSeconds <= event->sourceOffsetSeconds + 0.0005
               || marker.sourceSeconds >= event->sourceOffsetSeconds
                                              + event->sourceDurationSeconds
                                              - 0.0005
               || marker.timelineSeconds <= 0.0005
               || marker.timelineSeconds
                      >= event->durationSeconds - 0.0005;
    });
    const auto oldDuration = clip->durationSeconds;
    clip->durationSeconds = std::max(
        oldDuration, event->startSeconds + event->durationSeconds);
    if (clip->playEndSeconds >= oldDuration - 0.001)
        clip->playEndSeconds = clip->durationSeconds;
    if (clip->loopEndSeconds >= oldDuration - 0.001)
        clip->loopEndSeconds = clip->durationSeconds;
    normaliseLauncherClipTimeline(*clip);
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

bool AudioTrackController::setLauncherClipEventProperties(
    const std::size_t trackIndex, const int sceneIndex,
    const std::uint64_t eventId, const float gainDb,
    const double sourceTempoBpm, const StretchMode mode)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [sceneIndex](const auto& value) {
                                 return value.sceneIndex == sceneIndex;
                             });
    if (clip == track->launcherClips.end())
        return false;
    const auto previous = track->launcherClips;
    materialiseLegacyLauncherEvent(*clip);
    auto event = std::find_if(clip->events.begin(), clip->events.end(),
                              [eventId](const auto& value) {
                                  return value.id == eventId;
                              });
    if (event == clip->events.end() && clip->events.size() == 1
        && clip->events.front().id == 0) {
        clip->events.front().id = eventId;
        event = clip->events.begin();
    }
    if (event == clip->events.end())
        return false;
    event->gainDb = juce::jlimit(-60.0f, 24.0f, gainDb);
    event->sourceTempoBpm = juce::jlimit(0.0, 400.0, sourceTempoBpm);
    event->stretchMode = mode;
    if (mode == StretchMode::raw) {
        // Never stretched: as long as its part of the file, no stretch
        // point left.
        event->durationSeconds = std::max(0.001, event->sourceDurationSeconds);
        event->stretchMarkers.clear();
        event->fadeInSeconds = std::min(event->fadeInSeconds, event->durationSeconds * 0.5);
        event->fadeOutSeconds = std::min(event->fadeOutSeconds, event->durationSeconds * 0.5);
    }
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

std::uint64_t AudioTrackController::addLauncherClipAutomationPoint(
    const std::size_t trackIndex, const int sceneIndex,
    const double timeSeconds, const float valueDb)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return 0;
    const auto clip = std::find_if(
        track->launcherClips.begin(), track->launcherClips.end(),
        [sceneIndex](const auto& value) {
            return value.sceneIndex == sceneIndex;
        });
    if (clip == track->launcherClips.end())
        return 0;
    const auto previous = track->launcherClips;
    const auto id = nextClipAutomationPointId_++;
    clip->automation.push_back(
        { id, juce::jlimit(0.0, clip->durationSeconds, timeSeconds),
          juce::jlimit(-60.0f, 12.0f, valueDb) });
    std::sort(clip->automation.begin(), clip->automation.end(),
              [](const auto& left, const auto& right) {
                  return left.timeSeconds < right.timeSeconds;
              });
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return 0;
    }
    return id;
}

bool AudioTrackController::updateLauncherClipAutomationPoint(
    const std::size_t trackIndex, const int sceneIndex,
    const std::uint64_t id, const double timeSeconds, const float valueDb)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    const auto clip = std::find_if(
        track->launcherClips.begin(), track->launcherClips.end(),
        [sceneIndex](const auto& value) {
            return value.sceneIndex == sceneIndex;
        });
    if (clip == track->launcherClips.end())
        return false;
    const auto point = std::find_if(
        clip->automation.begin(), clip->automation.end(),
        [id](const auto& value) { return value.id == id; });
    if (point == clip->automation.end())
        return false;
    const auto previous = track->launcherClips;
    point->timeSeconds = juce::jlimit(0.0, clip->durationSeconds,
                                      timeSeconds);
    point->valueDb = juce::jlimit(-60.0f, 12.0f, valueDb);
    std::sort(clip->automation.begin(), clip->automation.end(),
              [](const auto& left, const auto& right) {
                  return left.timeSeconds < right.timeSeconds;
              });
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

bool AudioTrackController::setLauncherClipAutomationPointCurve(
    const std::size_t trackIndex, const int sceneIndex,
    const std::uint64_t id, const AutomationCurve curve,
    const float curveAmount)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    const auto clip = std::find_if(
        track->launcherClips.begin(), track->launcherClips.end(),
        [sceneIndex](const auto& value) {
            return value.sceneIndex == sceneIndex;
        });
    if (clip == track->launcherClips.end())
        return false;
    const auto point = std::find_if(
        clip->automation.begin(), clip->automation.end(),
        [id](const auto& value) { return value.id == id; });
    if (point == clip->automation.end())
        return false;
    const auto previous = track->launcherClips;
    point->curve = curve;
    point->curveAmount = juce::jlimit(-1.0f, 1.0f, curveAmount);
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

bool AudioTrackController::removeLauncherClipAutomationPoint(
    const std::size_t trackIndex, const int sceneIndex,
    const std::uint64_t id)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    const auto clip = std::find_if(
        track->launcherClips.begin(), track->launcherClips.end(),
        [sceneIndex](const auto& value) {
            return value.sceneIndex == sceneIndex;
        });
    if (clip == track->launcherClips.end())
        return false;
    const auto previous = track->launcherClips;
    const auto previousSize = clip->automation.size();
    std::erase_if(clip->automation, [id](const auto& point) {
        return point.id == id;
    });
    if (clip->automation.size() == previousSize)
        return false;
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

std::vector<AudioTrackController::AutomationPoint>
AudioTrackController::launcherClipAutomationPoints(
    const std::size_t trackIndex, const int sceneIndex) const
{
    const auto clip = launcherClip(trackIndex, sceneIndex);
    return clip.has_value() ? clip->automation
                            : std::vector<AutomationPoint> {};
}

std::uint64_t AudioTrackController::splitLauncherClipEvent(
    const std::size_t trackIndex, const int sceneIndex,
    const std::uint64_t eventId, const double localTimelineSeconds)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return 0;
    auto clip = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [sceneIndex](const auto& value) {
                                 return value.sceneIndex == sceneIndex;
                             });
    if (clip == track->launcherClips.end())
        return 0;
    const auto previous = track->launcherClips;
    materialiseLegacyLauncherEvent(*clip);
    for (auto& value : clip->events)
        if (value.id == 0)
            value.id = nextAudioEventId_++;
    auto event = std::find_if(clip->events.begin(), clip->events.end(),
                              [eventId](const auto& value) {
                                  return value.id == eventId;
                              });
    if (event == clip->events.end())
        return 0;
    const auto split = localTimelineSeconds - event->startSeconds;
    if (split <= 0.001 || split >= event->durationSeconds - 0.001)
        return 0;
    AudioClip mapping;
    mapping.durationSeconds = event->durationSeconds;
    mapping.sourceOffsetSeconds = event->sourceOffsetSeconds;
    mapping.sourceDurationSeconds = event->sourceDurationSeconds;
    mapping.stretchMarkers = event->stretchMarkers;
    const auto splitSource = sourceTimeForClipTimeline(mapping, split);
    auto right = *event;
    right.id = nextAudioEventId_++;
    const auto rightId = right.id;
    right.startSeconds = event->startSeconds + split;
    right.sourceOffsetSeconds = splitSource;
    right.durationSeconds = event->durationSeconds - split;
    right.sourceDurationSeconds = event->sourceOffsetSeconds
                                  + event->sourceDurationSeconds
                                  - splitSource;
    right.fadeInSeconds = 0.0;
    right.stretchMarkers = stretchMarkersForSlice(
        mapping, split, right.durationSeconds);
    event->durationSeconds = split;
    event->sourceDurationSeconds = splitSource
                                   - event->sourceOffsetSeconds;
    event->fadeOutSeconds = 0.0;
    event->stretchMarkers = stretchMarkersForSlice(mapping, 0.0, split);
    clip->events.push_back(std::move(right));
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return 0;
    }
    return rightId;
}

bool AudioTrackController::removeLauncherClipEvent(
    const std::size_t trackIndex, const int sceneIndex,
    const std::uint64_t eventId)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [sceneIndex](const auto& value) {
                                 return value.sceneIndex == sceneIndex;
                             });
    if (clip == track->launcherClips.end())
        return false;
    const auto previous = track->launcherClips;
    materialiseLegacyLauncherEvent(*clip);
    for (auto& value : clip->events)
        if (value.id == 0)
            value.id = nextAudioEventId_++;
    const auto oldSize = clip->events.size();
    std::erase_if(clip->events, [eventId](const auto& value) {
        return value.id == eventId;
    });
    if (clip->events.size() == oldSize)
        return false;
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

std::uint64_t AudioTrackController::addLauncherClipEventStretchMarker(
    const std::size_t trackIndex, const int sceneIndex,
    const std::uint64_t eventId, const double sourceSeconds,
    const double localTimelineSeconds)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return 0;
    auto clip = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [sceneIndex](const auto& value) {
                                 return value.sceneIndex == sceneIndex;
                             });
    if (clip == track->launcherClips.end())
        return 0;
    const auto previous = track->launcherClips;
    materialiseLegacyLauncherEvent(*clip);
    for (auto& value : clip->events)
        if (value.id == 0)
            value.id = nextAudioEventId_++;
    auto event = std::find_if(clip->events.begin(), clip->events.end(),
                              [eventId](const auto& value) {
                                  return value.id == eventId;
                              });
    if (event == clip->events.end()
        || event->durationSeconds <= 0.002
        || event->sourceDurationSeconds <= 0.002)
        return 0;
    const auto markerId = nextStretchMarkerId_++;
    event->stretchMarkers.push_back({
        markerId,
        juce::jlimit(event->sourceOffsetSeconds + 0.001,
                     event->sourceOffsetSeconds
                         + event->sourceDurationSeconds - 0.001,
                     sourceSeconds),
        juce::jlimit(0.001, event->durationSeconds - 0.001,
                     localTimelineSeconds) });
    std::sort(event->stretchMarkers.begin(), event->stretchMarkers.end(),
              [](const auto& left, const auto& right) {
                  return left.sourceSeconds < right.sourceSeconds;
              });
    auto previousTimeline = 0.0;
    for (const auto& marker : event->stretchMarkers) {
        if (marker.timelineSeconds <= previousTimeline + 0.0005) {
            track->launcherClips = previous;
            return 0;
        }
        previousTimeline = marker.timelineSeconds;
    }
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return 0;
    }
    return markerId;
}

bool AudioTrackController::updateLauncherClipEventStretchMarker(
    const std::size_t trackIndex, const int sceneIndex,
    const std::uint64_t eventId, const std::uint64_t markerId,
    const double localTimelineSeconds)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [sceneIndex](const auto& value) {
                                 return value.sceneIndex == sceneIndex;
                             });
    if (clip == track->launcherClips.end())
        return false;
    const auto previous = track->launcherClips;
    materialiseLegacyLauncherEvent(*clip);
    auto event = std::find_if(clip->events.begin(), clip->events.end(),
                              [eventId](const auto& value) {
                                  return value.id == eventId;
                              });
    if (event == clip->events.end())
        return false;
    auto marker = std::find_if(event->stretchMarkers.begin(),
                               event->stretchMarkers.end(),
                               [markerId](const auto& value) {
                                   return value.id == markerId;
                               });
    if (marker == event->stretchMarkers.end())
        return false;
    const auto index = static_cast<std::size_t>(
        std::distance(event->stretchMarkers.begin(), marker));
    const auto minimum = index == 0
                             ? 0.001
                             : event->stretchMarkers[index - 1]
                                       .timelineSeconds + 0.001;
    const auto maximum = index + 1 == event->stretchMarkers.size()
                             ? event->durationSeconds - 0.001
                             : event->stretchMarkers[index + 1]
                                       .timelineSeconds - 0.001;
    if (maximum <= minimum)
        return false;
    marker->timelineSeconds = juce::jlimit(
        minimum, maximum, localTimelineSeconds);
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

bool AudioTrackController::removeLauncherClipEventStretchMarker(
    const std::size_t trackIndex, const int sceneIndex,
    const std::uint64_t eventId, const std::uint64_t markerId)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [sceneIndex](const auto& value) {
                                 return value.sceneIndex == sceneIndex;
                             });
    if (clip == track->launcherClips.end())
        return false;
    const auto previous = track->launcherClips;
    materialiseLegacyLauncherEvent(*clip);
    auto event = std::find_if(clip->events.begin(), clip->events.end(),
                              [eventId](const auto& value) {
                                  return value.id == eventId;
                              });
    if (event == clip->events.end())
        return false;
    const auto oldSize = event->stretchMarkers.size();
    std::erase_if(event->stretchMarkers, [markerId](const auto& value) {
        return value.id == markerId;
    });
    if (event->stretchMarkers.size() == oldSize)
        return false;
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

bool AudioTrackController::clearLauncherClip(const std::size_t trackIndex,
                                             const int sceneIndex)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    const auto previousSize = track->launcherClips.size();
    std::erase_if(track->launcherClips, [sceneIndex](const auto& clip) {
        return clip.sceneIndex == sceneIndex;
    });
    if (track->launcherClips.size() == previousSize)
        return false;
    if (track->clipSource->activeLauncherScene() == sceneIndex
        || track->clipSource->pendingLauncherScene() == sceneIndex) {
        track->transportSource.stop();
        track->clipSource->clearLauncherClip();
    }
    track->clipSource->clearLauncherSceneSource(sceneIndex);
    return true;
}

bool AudioTrackController::setLauncherClipLanes(
    const std::size_t trackIndex, const int sceneIndex,
    std::map<juce::String, std::vector<AutomationPoint>> lanes)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    auto clip = std::find_if(track->launcherClips.begin(), track->launcherClips.end(),
                             [sceneIndex](const auto& value) { return value.sceneIndex == sceneIndex; });
    if (clip == track->launcherClips.end())
        return false;
    for (auto& [name, points] : lanes)
        std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
    std::erase_if(lanes, [](const auto& lane) { return lane.second.empty(); });
    clip->lanes = std::move(lanes);
    return true;
}

bool AudioTrackController::restoreLauncherClip(const std::size_t trackIndex,
                                               const LauncherClip& clip)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr || clip.sceneIndex < 0
        || clip.sceneIndex >= maximumLauncherScenes)
        return false;
    const auto previous = track->launcherClips;
    auto slot = std::find_if(track->launcherClips.begin(),
                             track->launcherClips.end(),
                             [&clip](const auto& candidate) {
                                 return candidate.sceneIndex == clip.sceneIndex;
                             });
    if (slot == track->launcherClips.end())
        slot = track->launcherClips.insert(track->launcherClips.end(), clip);
    else
        *slot = clip;
    for (const auto& event : slot->events) {
        nextAudioEventId_ = std::max(nextAudioEventId_, event.id + 1);
        for (const auto& marker : event.stretchMarkers)
            nextStretchMarkerId_ = std::max(nextStretchMarkerId_, marker.id + 1);
    }
    normaliseLauncherClipTimeline(*slot);
    if (rebuildLauncherPlans(*track).failed()) {
        track->launcherClips = previous;
        (void) rebuildLauncherPlans(*track);
        return false;
    }
    return true;
}

bool AudioTrackController::transferLauncherClip(
    const std::size_t sourceTrackIndex, const int sourceSceneIndex,
    const std::size_t targetTrackIndex, const int targetSceneIndex,
    const bool copy)
{
    auto* sourceTrack = findTrack(sourceTrackIndex);
    auto* targetTrack = findTrack(targetTrackIndex);
    if (sourceSceneIndex < 0 || sourceSceneIndex >= maximumLauncherScenes
        || targetSceneIndex < 0 || targetSceneIndex >= maximumLauncherScenes
        || sourceTrack == nullptr || targetTrack == nullptr
        || (sourceTrackIndex == targetTrackIndex
            && sourceSceneIndex == targetSceneIndex))
        return false;
    const auto source = std::find_if(
        sourceTrack->launcherClips.begin(), sourceTrack->launcherClips.end(),
        [sourceSceneIndex](const auto& clip) {
            return clip.sceneIndex == sourceSceneIndex;
        });
    if (source == sourceTrack->launcherClips.end())
        return false;
    auto value = *source;
    value.sceneIndex = targetSceneIndex;
    if (copy)
        for (auto& event : value.events) {
            event.id = nextAudioEventId_++;
            for (auto& marker : event.stretchMarkers)
                marker.id = nextStretchMarkerId_++;
        }
    const auto sourcePrevious = sourceTrack->launcherClips;
    const auto targetPrevious = targetTrack->launcherClips;
    std::erase_if(targetTrack->launcherClips,
                  [targetSceneIndex](const auto& clip) {
                      return clip.sceneIndex == targetSceneIndex;
                  });
    targetTrack->launcherClips.push_back(std::move(value));
    if (!copy)
        std::erase_if(sourceTrack->launcherClips,
                      [sourceSceneIndex](const auto& clip) {
                          return clip.sceneIndex == sourceSceneIndex;
                      });
    const auto sortClips = [](auto& clips) {
        std::sort(clips.begin(), clips.end(),
                  [](const auto& first, const auto& second) {
                      return first.sceneIndex < second.sceneIndex;
                  });
    };
    sortClips(targetTrack->launcherClips);
    if (sourceTrack != targetTrack)
        sortClips(sourceTrack->launcherClips);
    const auto targetResult = rebuildLauncherPlans(*targetTrack);
    const auto sourceResult = sourceTrack == targetTrack
                                  ? juce::Result::ok()
                                  : rebuildLauncherPlans(*sourceTrack);
    if (targetResult.failed() || sourceResult.failed()) {
        sourceTrack->launcherClips = sourcePrevious;
        targetTrack->launcherClips = targetPrevious;
        (void) rebuildLauncherPlans(*sourceTrack);
        if (sourceTrack != targetTrack)
            (void) rebuildLauncherPlans(*targetTrack);
        return false;
    }
    return true;
}

std::vector<AudioTrackController::LauncherClip>
AudioTrackController::launcherClips(const std::size_t trackIndex) const
{
    if (const auto* track = findTrack(trackIndex)) {
        auto clips = track->launcherClips;
        for (auto& clip : clips)
            normaliseLauncherClipTimeline(clip);
        return clips;
    }
    return {};
}

bool AudioTrackController::launchClip(const std::size_t trackIndex,
                                       const int sceneIndex)
{
    const auto* track = findTrack(trackIndex);
    if (track == nullptr
        || std::none_of(track->launcherClips.begin(),
                        track->launcherClips.end(),
                        [sceneIndex](const auto& clip) {
                            return clip.sceneIndex == sceneIndex;
                        }))
        return false;

    setArrangementPlaybackEnabled(false);
    if (!isPlaying())
        play();
    // The arrangement's MIDI stops; a MIDI clip launched on another track
    // goes on.
    for (auto& midiTrack : midiTracks_)
        if (midiTrack->activeLauncherScene.load(std::memory_order_acquire) < 0)
            midiTrack->synth.setPlaying(false);
    return scheduleLauncherClip(trackIndex, sceneIndex, launchDelaySeconds());
}

bool AudioTrackController::startLauncherClipPreview(
    const std::size_t trackIndex, const int sceneIndex,
    const double localStartSeconds)
{
    const auto clip = launcherClip(trackIndex, sceneIndex);
    if (!clip.has_value())
        return false;
    setArrangementPlaybackEnabled(false);
    if (!isPlaying())
        play();
    // Audition this clip through its real launcher track. Replacing the clip
    // on the target track must not stop clips that are already running on
    // other audio or MIDI tracks: the editor is part of the live launcher
    // mix, not a solo preview bus.
    return scheduleLauncherClip(trackIndex, sceneIndex, 0.0,
                                juce::jlimit(clip->playStartSeconds,
                                             clip->playEndSeconds,
                                             localStartSeconds));
}

bool AudioTrackController::seekLauncherClip(
    const std::size_t trackIndex, const int sceneIndex,
    const double localSeconds)
{
    auto* track = findTrack(trackIndex);
    const auto clip = launcherClip(trackIndex, sceneIndex);
    if (track == nullptr || !clip.has_value())
        return false;
    const auto position = juce::jlimit(
        clip->playStartSeconds, clip->playEndSeconds, localSeconds);
    if (track->clipSource->activeLauncherScene() != sceneIndex) {
        if (!startLauncherClipPreview(trackIndex, sceneIndex))
            return false;
    }
    track->transportSource.setPosition(position);
    return true;
}

bool AudioTrackController::launchScene(const int sceneIndex)
{
    if (sceneIndex < 0 || sceneIndex >= maximumLauncherScenes)
        return false;

    const auto hasClip = std::any_of(
        tracks_.begin(), tracks_.end(), [sceneIndex](const auto& track) {
            return std::any_of(
                track->launcherClips.begin(), track->launcherClips.end(),
                [sceneIndex](const auto& clip) {
                    return clip.sceneIndex == sceneIndex;
                });
        });
    if (!hasClip)
        return false;
    setArrangementPlaybackEnabled(false);
    if (!isPlaying())
        play();
    for (auto& midiTrack : midiTracks_)
        if (midiTrack->activeLauncherScene.load(std::memory_order_acquire) < 0)
            midiTrack->synth.setPlaying(false);

    const auto delaySeconds = launchDelaySeconds();
    auto launched = false;
    for (std::size_t trackIndex = 0; trackIndex < tracks_.size(); ++trackIndex) {
        const auto hasTrackClip = std::any_of(
            tracks_[trackIndex]->launcherClips.begin(),
            tracks_[trackIndex]->launcherClips.end(),
            [sceneIndex](const auto& clip) {
                return clip.sceneIndex == sceneIndex;
            });
        launched = (hasTrackClip
                        ? scheduleLauncherClip(trackIndex, sceneIndex,
                                               delaySeconds)
                        : scheduleLauncherStop(trackIndex, delaySeconds))
                   || launched;
    }
    return launched;
}

bool AudioTrackController::restartLauncherClips(const std::vector<ClipToRestart>& clips)
{
    if (clips.empty())
        return false;
    setArrangementPlaybackEnabled(false);
    if (!isPlaying())
        play();
    // The arrangement's MIDI stays silent: only the clips play.
    for (auto& midiTrack : midiTracks_) {
        const auto restarts = std::any_of(clips.begin(), clips.end(), [&midiTrack](const auto& clip) {
            return clip.midi && clip.midiTrackId == midiTrack->id;
        });
        if (!restarts && midiTrack->activeLauncherScene.load(std::memory_order_acquire) < 0)
            midiTrack->synth.setPlaying(false);
    }
    auto restarted = false;
    for (const auto& clip : clips) {
        if (clip.midi) {
            restarted = startMidiClipPreview(clip.midiTrackId, clip.midiClipId, clip.sceneIndex) || restarted;
        } else if (const auto launched = launcherClip(clip.trackIndex, clip.sceneIndex)) {
            restarted = scheduleLauncherClip(clip.trackIndex, clip.sceneIndex, 0.0,
                                             launched->playStartSeconds)
                        || restarted;
        }
    }
    return restarted;
}

bool AudioTrackController::stopLauncherTrack(
    const std::size_t trackIndex, const bool quantized)
{
    const auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    if (!isPlaying())
        play();
    return scheduleLauncherStop(
        trackIndex, quantized ? launchDelaySeconds() : 0.0);
}

void AudioTrackController::beginArrangementBatch() noexcept
{
    ++arrangementBatchDepth_;
}

juce::Result AudioTrackController::endArrangementBatch()
{
    if (arrangementBatchDepth_ == 0 || --arrangementBatchDepth_ > 0)
        return juce::Result::ok();
    auto result = juce::Result::ok();
    for (auto& track : tracks_) {
        if (!track->arrangementPlanPending)
            continue;
        track->arrangementPlanPending = false;
        const auto rebuilt = rebuildArrangementPlan(*track);
        if (result.wasOk() && rebuilt.failed())
            result = rebuilt;
    }
    refreshTransportRanges();
    return result;
}

void AudioTrackController::stopTransportsTogether(
    const std::function<bool(PlaybackTrack&)>& shouldStop)
{
    // AudioTransportSource::stop() sleeps until the audio callback has run
    // once more and seen the flag: a block's worth, per transport, on the
    // message thread. Eight tracks stopped one after the other was the
    // eighty milliseconds every change between arrangement and launcher
    // cost. Stopped together, they wait for the same block. The flags are
    // atomic and the change message is posted, so a stop may run off the
    // message thread.
    std::vector<juce::AudioTransportSource*> transports;
    for (auto& track : tracks_)
        if (track->transportSource.isPlaying() && shouldStop(*track))
            transports.push_back(&track->transportSource);
    if (transports.size() <= 1) {
        for (auto* transport : transports)
            transport->stop();
        return;
    }
    std::vector<std::thread> stops;
    stops.reserve(transports.size());
    for (auto* transport : transports)
        stops.emplace_back([transport] { transport->stop(); });
    for (auto& stop : stops)
        stop.join();
}

void AudioTrackController::stopLauncher()
{
    stopTransportsTogether([](PlaybackTrack& track) {
        return track.clipSource->activeLauncherScene() >= 0
               || track.clipSource->pendingLauncherScene() >= 0;
    });
    for (auto& track : tracks_)
        track->clipSource->clearLauncherClip();
    for (auto& track : midiTracks_) {
        const auto wasLauncher = track->activeLauncherScene.load(
            std::memory_order_acquire) >= 0;
        track->synth.setPlaying(false);
        track->activeLauncherScene.store(-1,
                                         std::memory_order_release);
        if (wasLauncher)
            publishMidiSequence(*track);
    }
    resetLatencyCompensation();
}

int AudioTrackController::activeLauncherScene(const std::size_t trackIndex) const noexcept
{
    if (const auto* track = findTrack(trackIndex))
        return track->clipSource->activeLauncherScene();
    return -1;
}

int AudioTrackController::pendingLauncherScene(const std::size_t trackIndex) const noexcept
{
    if (const auto* track = findTrack(trackIndex))
        return track->clipSource->pendingLauncherScene();
    return -1;
}

float AudioTrackController::launcherClipProgress(
    const std::size_t trackIndex) const noexcept
{
    if (const auto* track = findTrack(trackIndex))
        return track->clipSource->launcherProgress();
    return 0.0f;
}

double AudioTrackController::launcherClipPositionSeconds(
    const std::size_t trackIndex) const noexcept
{
    if (const auto* track = findTrack(trackIndex))
        return track->clipSource->launcherPositionSeconds();
    return 0.0;
}

bool AudioTrackController::launcherUsesDoubleDeckReadAhead(
    const std::size_t trackIndex) const noexcept
{
    if (const auto* track = findTrack(trackIndex))
        return track->clipSource->usesDoubleDeckReadAhead();
    return false;
}

void AudioTrackController::setLaunchQuantization(
    const LaunchQuantization quantization) noexcept
{
    launchQuantization_ = static_cast<LaunchQuantization>(juce::jlimit(
        static_cast<int>(LaunchQuantization::none),
        static_cast<int>(LaunchQuantization::oneBar),
        static_cast<int>(quantization)));
}

AudioTrackController::LaunchQuantization
AudioTrackController::launchQuantization() const noexcept
{
    return launchQuantization_;
}

double AudioTrackController::launcherBoundaryDelaySeconds() const noexcept
{
    return launchDelaySeconds();
}

double AudioTrackController::quantizedLaunchDelaySeconds(
    const double positionSeconds,
    const double tempoBpm,
    const LaunchQuantization quantization) noexcept
{
    double intervalBeats {};
    switch (quantization) {
        case LaunchQuantization::quarterBeat: intervalBeats = 0.25; break;
        case LaunchQuantization::halfBeat: intervalBeats = 0.5; break;
        case LaunchQuantization::oneBeat: intervalBeats = 1.0; break;
        case LaunchQuantization::oneBar: intervalBeats = 4.0; break;
        case LaunchQuantization::none: return 0.0;
    }
    const auto intervalSeconds = intervalBeats * 60.0 / juce::jlimit(20.0, 999.0, tempoBpm);
    const auto position = std::max(0.0, positionSeconds);
    auto phase = std::fmod(position, intervalSeconds);
    if (phase < 0.0)
        phase += intervalSeconds;
    const auto tolerance = std::max(1.0e-7, intervalSeconds * 1.0e-7);
    return phase <= tolerance || intervalSeconds - phase <= tolerance
               ? 0.0
               : intervalSeconds - phase;
}

double AudioTrackController::launchDelaySeconds() const noexcept
{
    return isPlaying()
               ? quantizedLaunchDelaySeconds(position(), tempoBpm_, launchQuantization_)
               : 0.0;
}

bool AudioTrackController::scheduleLauncherClip(const std::size_t trackIndex,
                                                const int sceneIndex,
                                                const double delaySeconds,
                                                const double localStartSeconds)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    const auto clip = std::find_if(track->launcherClips.begin(), track->launcherClips.end(),
                                   [sceneIndex](const auto& candidate) {
                                       return candidate.sceneIndex == sceneIndex;
                                   });
    if (clip == track->launcherClips.end())
        return false;
    track->clipSource->scheduleLauncherClip(sceneIndex,
                                            clip->sourceOffsetSeconds,
                                            clip->durationSeconds,
                                            clip->looping,
                                            delaySeconds,
                                            localStartSeconds);
    if (!track->transportSource.isPlaying()) {
        track->transportSource.setPosition(0.0);
        track->transportSource.start();
    }
    resetLatencyCompensation();
    return true;
}

bool AudioTrackController::scheduleLauncherStop(
    const std::size_t trackIndex, const double delaySeconds)
{
    auto* track = findTrack(trackIndex);
    if (track == nullptr)
        return false;
    track->clipSource->scheduleLauncherStop(delaySeconds);
    if (!track->transportSource.isPlaying()) {
        track->transportSource.setPosition(position());
        track->transportSource.start();
    }
    return true;
}

void AudioTrackController::play()
{
    for (auto& track : midiTracks_) {
        if (track->activeLauncherScene.load(
                std::memory_order_acquire) >= 0
            || track->activeEditorClipId.load(
                   std::memory_order_acquire) != 0) {
            track->synth.setPlaying(false);
            track->activeLauncherScene.store(-1,
                                             std::memory_order_release);
            track->activeEditorClipId.store(0,
                                            std::memory_order_release);
            publishMidiSequence(*track);
            publishMidiTrackAutomation(*track);
        }
    }
    refreshTransportRanges();
    const auto sessionLength = std::max(transportLength(),
                                        transportHorizonSeconds);
    auto sessionPosition = position();
    if (looping_ && sessionPosition >= loopEndSeconds_) {
        setPosition(loopStartSeconds_);
        sessionPosition = loopStartSeconds_;
    } else if (sessionPosition >= sessionLength) {
        const auto restart = looping_ ? loopStartSeconds_ : 0.0;
        setPosition(restart);
        sessionPosition = restart;
    }

    for (auto& track : tracks_) {
        track->clipSource->clearLauncherClip();
        track->transportSource.setPosition(
            juce::jlimit(0.0, sessionLength, sessionPosition));
        if (sessionPosition < sessionLength)
            track->transportSource.start();
    }
    for (auto& track : midiTracks_) {
        track->activeLauncherScene.store(-1,
                                         std::memory_order_release);
        track->activeEditorClipId.store(0,
                                        std::memory_order_release);
        track->synth.setPosition(sessionPosition);
        track->synth.setPlaying(true);
    }
    masterOutput_.setClock(sessionPosition);
    masterOutput_.setClockRunning(true);
    NativeDspRuntime::setSessionPlaying(true);
    resetLatencyCompensation();
}

void AudioTrackController::stop()
{
    masterOutput_.setClockRunning(false);
    NativeDspRuntime::setSessionPlaying(false);
    stopTransportsTogether([](PlaybackTrack&) { return true; });
    for (auto& track : tracks_)
        track->clipSource->clearLauncherClip();
    for (auto& track : midiTracks_) {
        const auto wasPreview = track->activeLauncherScene.load(
                                    std::memory_order_acquire) >= 0
                                || track->activeEditorClipId.load(
                                       std::memory_order_acquire) != 0;
        track->synth.setPlaying(false);
        track->activeLauncherScene.store(-1,
                                         std::memory_order_release);
        track->activeEditorClipId.store(0,
                                        std::memory_order_release);
        if (wasPreview) {
            publishMidiSequence(*track);
            publishMidiTrackAutomation(*track);
        }
    }
    resetLatencyCompensation();
}

void AudioTrackController::setPosition(const double seconds)
{
    const auto sessionLength = std::max(transportLength(),
                                        transportHorizonSeconds);
    const auto safePosition = juce::jlimit(
        0.0, std::max(0.0, sessionLength), seconds);
    masterOutput_.setClock(safePosition);
    stopTransportsTogether([](PlaybackTrack& track) {
        return track.clipSource->activeLauncherScene() >= 0
               || track.clipSource->pendingLauncherScene() >= 0;
    });
    for (auto& track : tracks_) {
        track->clipSource->clearLauncherClip();
        track->transportSource.setPosition(safePosition);
    }
    for (auto& track : midiTracks_) {
        const auto wasPreview = track->activeLauncherScene.load(
                                    std::memory_order_acquire) >= 0
                                || track->activeEditorClipId.load(
                                       std::memory_order_acquire) != 0;
        track->activeLauncherScene.store(-1,
                                         std::memory_order_release);
        track->activeEditorClipId.store(0,
                                        std::memory_order_release);
        if (wasPreview) {
            publishMidiSequence(*track);
            publishMidiTrackAutomation(*track);
        }
        track->synth.setPosition(safePosition);
    }
    resetLatencyCompensation();
}

void AudioTrackController::setArrangementPlaybackEnabled(
    const bool enabled) noexcept
{
    if (enabled)
        stopLauncher();
    arrangementPlaybackEnabled_.store(enabled, std::memory_order_release);
    for (auto& track : tracks_)
        track->clipSource->setArrangementPlaybackEnabled(enabled);
    if (!enabled)
        for (auto& track : midiTracks_) {
            if (track->activeLauncherScene.load(
                    std::memory_order_acquire) >= 0)
                continue;
            track->synth.setPlaying(false);
        }
}

bool AudioTrackController::arrangementPlaybackEnabled() const noexcept
{
    return arrangementPlaybackEnabled_.load(std::memory_order_acquire);
}

void AudioTrackController::setLooping(const bool shouldLoop)
{
    looping_ = shouldLoop;
    refreshTransportRanges();
    for (auto& track : tracks_)
        track->clipSource->setLooping(shouldLoop);
    for (auto& track : midiTracks_)
        if (track->activeLauncherScene.load(std::memory_order_acquire) < 0
            && track->activeEditorClipId.load(std::memory_order_acquire)
                   == 0)
            track->synth.setLooping(shouldLoop);
}

void AudioTrackController::setLoopRange(const double startSeconds,
                                        const double endSeconds)
{
    loopRangeExplicit_ = true;
    loopStartSeconds_ = std::max(0.0, startSeconds);
    loopEndSeconds_ = std::max(loopStartSeconds_ + 0.000001,
                               endSeconds);
    refreshTransportRanges();
}

void AudioTrackController::resetLoopRange()
{
    loopRangeExplicit_ = false;
    loopStartSeconds_ = 0.0;
    loopEndSeconds_ = 0.0;
    refreshTransportRanges();
}

double AudioTrackController::loopStart() const noexcept
{
    return loopStartSeconds_;
}

double AudioTrackController::loopEnd() const noexcept
{
    return loopEndSeconds_;
}

bool AudioTrackController::hasExplicitLoopRange() const noexcept
{
    return loopRangeExplicit_;
}

juce::Result AudioTrackController::startRecording(
    const juce::File& destination, const double startDelaySeconds)
{
    auto busId = virtualBusRouter_.defaultInputBusId();
    auto afterEffects = true;
    if (const auto armed = armedAudioTrack(); armed.has_value()) {
        busId = tracks_[*armed]->inputBusId;
        afterEffects = tracks_[*armed]->inputAfterEffects;
    }
    recorder_.setAfterEffects(afterEffects);
    auto bus = virtualBusRouter_.bus(busId);
    if (bus.has_value() && bus->direction == VirtualBusDirection::input)
        recorder_.setInputBus(std::move(bus),
                              virtualBusRouter_.graph(busId));
    else
        recorder_.setInputBus(std::nullopt);
    return recorder_.start(destination, startDelaySeconds);
}

void AudioTrackController::scheduleRecordingStop(const double delaySeconds)
{
    recorder_.scheduleStop(delaySeconds);
}

void AudioTrackController::stopRecording() { recorder_.stop(); }
bool AudioTrackController::isRecording() const noexcept { return recorder_.isRecording(); }
bool AudioTrackController::hasRecordingSession() const noexcept
{
    return recorder_.hasSession();
}
bool AudioTrackController::recordingStarted() const noexcept
{
    return recorder_.hasStarted();
}
bool AudioTrackController::recordingStopPending() const noexcept
{
    return recorder_.stopPending();
}
bool AudioTrackController::recordingComplete() const noexcept
{
    return recorder_.recordingComplete();
}

juce::Result AudioTrackController::commitRecordedTake(
    const juce::File& recordedFile,
    const std::optional<std::size_t> targetTrack,
    const double startSeconds,
    RecordedTakeResult& result,
    const std::optional<std::uint64_t> targetClipId)
{
    result = {};
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager_.createReaderFor(recordedFile));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0)
        return juce::Result::fail(
            tr("error.recordedTakeInvalid", "file",
               recordedFile.getFileName()));

    const auto safeStart = std::max(0.0, startSeconds);
    const auto duration = static_cast<double>(reader->lengthInSamples)
                          / reader->sampleRate;
    if (targetClipId.has_value()) {
        if (!targetTrack.has_value() || *targetTrack >= tracks_.size())
            return juce::Result::fail(tr("error.recordedTakeTrack"));
        const auto clip = audioClip(*targetTrack, *targetClipId);
        if (!clip.has_value())
            return juce::Result::fail(tr("error.audioTakeClip"));
        const auto addResult = addAudioTake(
            *targetTrack, *targetClipId, recordedFile, true, true);
        if (addResult.failed())
            return addResult;
        result = { *targetTrack, *targetClipId, clip->startSeconds,
                   duration, false };
        return juce::Result::ok();
    }
    if (targetTrack.has_value()) {
        if (*targetTrack >= tracks_.size())
            return juce::Result::fail(tr("error.recordedTakeTrack"));
        std::uint64_t clipId {};
        const auto addResult = addAudioClipFromFile(
            *targetTrack, recordedFile, safeStart, &clipId);
        if (addResult.failed())
            return addResult;
        result = { *targetTrack, clipId, safeStart, duration, false };
        return juce::Result::ok();
    }

    std::size_t newTrackIndex {};
    const auto addResult = addAudioTrack(
        recordedFile, &newTrackIndex, safeStart);
    if (addResult.failed())
        return addResult;
    const auto clips = audioClips(newTrackIndex);
    if (clips.empty())
        return juce::Result::fail(tr("error.recordedTakeTrack"));
    setTrackArmed(newTrackIndex, true);
    result = { newTrackIndex, clips.front().id, safeStart, duration, true };
    return juce::Result::ok();
}

juce::Result AudioTrackController::startMasterExport(
    const juce::File& destination)
{
    if (isRecording())
        return juce::Result::fail(tr("error.masterExportRecording"));
    if (contentLength() <= 0.0)
        return juce::Result::fail(tr("error.masterExportEmpty"));
    for (const auto& track : tracks_)
        if (track->file == destination)
            return juce::Result::fail(tr("error.masterExportSource"));

    const juce::ScopedLock audioLock(deviceManager_.getAudioCallbackLock());
    stop();
    setPosition(0.0);
    masterExportPreviousLooping_ = looping_;
    setLooping(false);
    constexpr double effectTailSeconds = 2.0;
    const auto result = masterOutput_.startCapture(
        destination, contentLength() + effectTailSeconds,
        mixLatencySamples() + masterOutput_.chainLatencySamples());
    if (result.failed())
    {
        setLooping(masterExportPreviousLooping_);
        return result;
    }
    masterExportDestination_ = destination;
    play();
    return juce::Result::ok();
}

juce::Result AudioTrackController::finishMasterExport()
{
    if (!masterOutput_.isCapturing())
        return juce::Result::fail(tr("error.masterExportInactive"));
    const auto destination = masterExportDestination_;
    const auto dropped = masterOutput_.droppedBlockCount();
    {
        const juce::ScopedLock audioLock(deviceManager_.getAudioCallbackLock());
        masterOutput_.stopCapture();
        stop();
        setPosition(0.0);
        setLooping(masterExportPreviousLooping_);
    }
    masterExportDestination_ = {};
    if (!destination.existsAsFile())
        return juce::Result::fail(tr("error.masterExportCreate", "file",
                                     destination.getFullPathName()));
    if (dropped > 0)
        return juce::Result::fail(tr("error.masterExportDropped", "count",
                                     juce::String(dropped)));
    return juce::Result::ok();
}

void AudioTrackController::cancelMasterExport()
{
    const auto destination = masterExportDestination_;
    {
        const juce::ScopedLock audioLock(deviceManager_.getAudioCallbackLock());
        masterOutput_.stopCapture();
        stop();
        setPosition(0.0);
        setLooping(masterExportPreviousLooping_);
    }
    masterExportDestination_ = {};
    if (destination.existsAsFile())
        (void) destination.deleteFile();
}

bool AudioTrackController::isMasterExporting() const noexcept
{
    return masterOutput_.isCapturing();
}

bool AudioTrackController::masterExportComplete() const noexcept
{
    return masterOutput_.captureComplete();
}

double AudioTrackController::masterExportProgress() const noexcept
{
    return masterOutput_.captureProgress();
}

int AudioTrackController::droppedMasterExportBlocks() const noexcept
{
    return masterOutput_.droppedBlockCount();
}

bool AudioTrackController::hasAudioFile() const noexcept { return !tracks_.empty(); }

bool AudioTrackController::isPlaying() const noexcept
{
    return std::any_of(midiTracks_.begin(), midiTracks_.end(), [](const auto& track) {
               return track->synth.isPlaying();
           })
           || std::any_of(tracks_.begin(), tracks_.end(), [](const auto& track) {
                  return track->transportSource.isPlaying();
              });
}

double AudioTrackController::position() const noexcept
{
    double current = 0.0;
    for (const auto& track : tracks_)
        current = std::max(current, track->transportSource.getCurrentPosition());
    for (const auto& track : midiTracks_)
        current = std::max(current, track->synth.position());
    return current;
}

double AudioTrackController::length() const noexcept
{
    return transportLength();
}

double AudioTrackController::contentLength() const noexcept
{
    double longest = 0.0;
    for (const auto& track : tracks_)
        longest = std::max(longest, track->clipSource->arrangementLengthSeconds());
    for (const auto& track : midiTracks_)
        longest = std::max(longest, track->synth.length());
    if (midiRecordingTrackId_ != 0 && midiRecordingClipId_ != 0) {
        const auto* track = findMidiTrack(midiRecordingTrackId_);
        const auto* clip = track != nullptr
                               ? findMidiClip(*track, midiRecordingClipId_)
                               : nullptr;
        if (clip != nullptr)
            longest = std::max(
                longest,
                (clip->startBeats + clip->lengthBeats) * 60.0 / tempoBpm_);
    }
    return longest;
}

double AudioTrackController::transportLength() const noexcept
{
    return std::max(contentLength(),
                    loopRangeExplicit_ ? loopEndSeconds_ : 0.0);
}

void AudioTrackController::refreshTransportRanges() noexcept
{
    const auto contentEnd = contentLength();
    if (!loopRangeExplicit_) {
        loopStartSeconds_ = 0.0;
        loopEndSeconds_ = std::max(0.000001, contentEnd);
    } else {
        loopStartSeconds_ = std::max(0.0, loopStartSeconds_);
        loopEndSeconds_ = std::max(loopStartSeconds_ + 0.000001,
                                   loopEndSeconds_);
    }
    const auto sessionEnd = std::max(
        { transportHorizonSeconds, contentEnd, loopEndSeconds_ });
    for (auto& track : tracks_)
        track->clipSource->setTransportRange(
            sessionEnd, loopStartSeconds_, loopEndSeconds_);
    for (auto& track : midiTracks_)
        if (track->activeLauncherScene.load(std::memory_order_acquire) < 0
            && track->activeEditorClipId.load(std::memory_order_acquire)
                   == 0)
            track->synth.setTransportRange(
                sessionEnd, loopStartSeconds_, loopEndSeconds_);
}

int AudioTrackController::droppedRecordingBlocks() const noexcept { return recorder_.droppedBlockCount(); }
int AudioTrackController::mixLatencySamples() const noexcept
{
    return mixLatencySamples_.load(std::memory_order_acquire);
}

int AudioTrackController::trackLatencySamples(const std::size_t index) const noexcept
{
    const auto* track = findTrack(index);
    return track != nullptr ? track->gainPanSource.totalLatencySamples() : 0;
}

int AudioTrackController::midiTrackLatencySamples(const std::uint64_t trackId) const noexcept
{
    const auto* track = findMidiTrack(trackId);
    return track != nullptr ? track->gainPanSource.totalLatencySamples() + track->synth.effectLatencySamples() : 0;
}

int AudioTrackController::trackCompensationSamples(const std::size_t index) const noexcept
{
    const auto* track = findTrack(index);
    return track != nullptr ? track->latencySource.delaySamples() : 0;
}

int AudioTrackController::midiTrackCompensationSamples(const std::uint64_t trackId) const noexcept
{
    const auto* track = findMidiTrack(trackId);
    return track != nullptr ? track->latencySource.delaySamples() : 0;
}

juce::String AudioTrackController::audioDeviceDescription()
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return deviceManager_.getCurrentAudioDeviceType() + "  |  " + device->getName()
               + "  |  " + juce::String(device->getCurrentSampleRate() / 1'000.0, 1)
               + " kHz / " + juce::String(device->getCurrentBufferSizeSamples())
               + "  |  "
               + juce::String(device->getActiveInputChannels()
                                  .countNumberOfSetBits())
               + " IN / "
               + juce::String(device->getActiveOutputChannels()
                                  .countNumberOfSetBits())
               + " OUT"
               + "  |  I/O " + juce::String(device->getInputLatencyInSamples())
               + "/" + juce::String(device->getOutputLatencyInSamples()) + " smp";
    return tr("status.offline");
}

juce::AudioDeviceManager& AudioTrackController::deviceManager() noexcept { return deviceManager_; }
juce::AudioFormatManager& AudioTrackController::formatManager() noexcept { return formatManager_; }
juce::MidiKeyboardState& AudioTrackController::keyboardState() noexcept { return midiKeyboardState_; }

AudioTrackController::PlaybackTrack* AudioTrackController::findTrack(const std::size_t index) noexcept
{
    return index < tracks_.size() ? tracks_[index].get() : nullptr;
}

const AudioTrackController::PlaybackTrack* AudioTrackController::findTrack(const std::size_t index) const noexcept
{
    return index < tracks_.size() ? tracks_[index].get() : nullptr;
}

AudioTrackController::MidiPlaybackTrack* AudioTrackController::findMidiTrack(
    const std::uint64_t id) noexcept
{
    const auto found = std::find_if(midiTracks_.begin(), midiTracks_.end(),
                                    [id](const auto& track) {
                                        return track->id == id;
                                    });
    return found != midiTracks_.end() ? found->get() : nullptr;
}

const AudioTrackController::MidiPlaybackTrack* AudioTrackController::findMidiTrack(
    const std::uint64_t id) const noexcept
{
    const auto found = std::find_if(midiTracks_.begin(), midiTracks_.end(),
                                    [id](const auto& track) {
                                        return track->id == id;
                                    });
    return found != midiTracks_.end() ? found->get() : nullptr;
}

AudioTrackController::MidiClip* AudioTrackController::findMidiClip(
    MidiPlaybackTrack& track, const std::uint64_t clipId) noexcept
{
    const auto found = std::find_if(track.clips.begin(), track.clips.end(),
                                    [clipId](const auto& clip) {
                                        return clip.id == clipId;
                                    });
    return found != track.clips.end() ? &*found : nullptr;
}

const AudioTrackController::MidiClip* AudioTrackController::findMidiClip(
    const MidiPlaybackTrack& track, const std::uint64_t clipId) const noexcept
{
    const auto found = std::find_if(track.clips.begin(), track.clips.end(),
                                    [clipId](const auto& clip) {
                                        return clip.id == clipId;
                                    });
    return found != track.clips.end() ? &*found : nullptr;
}

void AudioTrackController::refreshTrackGains() noexcept
{
    const auto anySolo = std::any_of(tracks_.begin(), tracks_.end(), [](const auto& track) {
                             return track->solo;
                         })
                         || std::any_of(midiTracks_.begin(), midiTracks_.end(), [](const auto& track) {
                                return track->solo;
                            });

    // What a soloed track is routed into, at any distance, is heard too.
    std::vector<const juce::AudioSource*> reached;
    if (anySolo) {
        std::vector<const juce::AudioSource*> pending;
        for (const auto& track : tracks_)
            if (track->solo)
                pending.push_back(&track->latencySource);
        for (const auto& track : midiTracks_)
            if (track->solo)
                pending.push_back(&track->latencySource);
        while (!pending.empty()) {
            const auto* source = pending.back();
            pending.pop_back();
            for (const auto& [from, destinations] : routingEdges_)
                if (from == source)
                    for (const auto* destination : destinations)
                        if (std::find(reached.begin(), reached.end(), destination) == reached.end()) {
                            reached.push_back(destination);
                            pending.push_back(destination);
                        }
        }
    }
    const auto soloReached = [&reached](const juce::AudioSource* source) {
        return std::find(reached.begin(), reached.end(), source) != reached.end();
    };
    for (auto& track : tracks_) {
        const auto audible = !track->muted && (!anySolo || track->solo || soloReached(&track->latencySource));
        const auto gain = audible ? juce::Decibels::decibelsToGain(track->gainDb, -60.0f) : 0.0f;
        track->gainPanSource.setGain(gain);
        track->gainPanSource.setSendsAudible(audible);
    }
    for (auto& track : midiTracks_) {
        const auto audible = !track->muted && (!anySolo || track->solo || soloReached(&track->latencySource));
        const auto gain = audible ? juce::Decibels::decibelsToGain(track->gainDb, -60.0f) : 0.0f;
        track->gainPanSource.setGain(gain);
        track->gainPanSource.setSendsAudible(audible);
        track->gainPanSource.setPan(track->pan);
    }
}

void AudioTrackController::refreshLatencyCompensation() noexcept
{
    // Polled with the plugins: the plan is made again only when a latency,
    // or the set of tracks, changed.
    try {
        std::vector<std::pair<RoutingKey, int>> latencies;
        latencies.reserve(tracks_.size() + midiTracks_.size());
        for (const auto& track : tracks_)
            latencies.emplace_back(RoutingKey { track.get(), 0 }, track->gainPanSource.totalLatencySamples());
        for (const auto& track : midiTracks_)
            latencies.emplace_back(RoutingKey { nullptr, track->id },
                                   track->gainPanSource.totalLatencySamples() + track->synth.effectLatencySamples());
        if (latencies == plannedLatencies_)
            return;
        if (rebuildRouting().failed()) {
            routing_.clear();
            (void) rebuildRouting();
        }
    } catch (...) {
    }
}

void AudioTrackController::resetLatencyCompensation() noexcept
{
    for (auto& track : tracks_) {
        track->latencySource.requestReset();
        track->gainPanSource.resetInputDelay();
    }
    for (auto& track : midiTracks_) {
        track->latencySource.requestReset();
        track->synth.resetInputDelay();
    }
    for (auto& edge : edgeDelays_)
        edge.line->requestReset();
}

AudioTrackController::MidiEditState
AudioTrackController::captureMidiEditState(
    const std::uint64_t trackId,
    const std::uint64_t clipId) const
{
    const auto* track = findMidiTrack(trackId);
    const auto* clip = track != nullptr ? findMidiClip(*track, clipId) : nullptr;
    if (clip == nullptr)
        return {};
    return { trackId, clipId, clip->lengthBeats, clip->notes, clip->playStartBeats, clip->playEndBeats,
             clip->loopStartBeats, clip->loopEndBeats, clip->looping };
}

bool AudioTrackController::restoreMidiEditState(const MidiEditState& state)
{
    auto* track = findMidiTrack(state.trackId);
    auto* clip = track != nullptr ? findMidiClip(*track, state.clipId) : nullptr;
    if (track == nullptr || clip == nullptr)
        return false;
    clip->lengthBeats = std::max(0.25, state.clipLengthBeats);
    clip->notes = state.notes;
    clip->playStartBeats = state.playStartBeats;
    clip->playEndBeats = state.playEndBeats;
    clip->loopStartBeats = state.loopStartBeats;
    clip->loopEndBeats = state.loopEndBeats;
    clip->looping = state.looping;
    for (const auto& note : clip->notes)
        nextMidiNoteId_ = std::max(nextMidiNoteId_, note.id + 1);
    publishMidiSequence(*track);
    if (track->activeLauncherScene.load(std::memory_order_acquire) >= 0) {
        publishMidiClipPreviewSequence(*track, *clip);
        track->synth.setLooping(clip->looping);
    }
    return true;
}

void AudioTrackController::prepareMidiMutation(
    const std::uint64_t trackId,
    const std::uint64_t clipId)
{
    if (midiEditBatch_.has_value()) {
        if (midiEditBatch_->trackId == trackId
            && midiEditBatch_->clipId == clipId)
            return;
        (void) endMidiEdit();
    }
    const auto previous = captureMidiEditState(trackId, clipId);
    if (previous.trackId == 0)
        return;
    midiEditUndoStates_.push_back(previous);
    if (midiEditUndoStates_.size() > 32)
        midiEditUndoStates_.erase(midiEditUndoStates_.begin());
    midiEditRedoStates_.clear();
}

bool AudioTrackController::closeRecordedMidiNote(
    const int channel,
    const int noteNumber,
    const double timelineSeconds)
{
    const auto safeChannel = juce::jlimit(1, 16, channel);
    const auto safeNote = juce::jlimit(0, 127, noteNumber);
    auto& active = activeMidiRecordingNotes_[static_cast<std::size_t>(
        (safeChannel - 1) * 128 + safeNote)];
    if (!active.active)
        return false;
    auto* track = findMidiTrack(midiRecordingTrackId_);
    auto* clip = track != nullptr
                     ? findMidiClip(*track, midiRecordingClipId_)
                     : nullptr;
    if (clip == nullptr) {
        active = {};
        return false;
    }
    const auto adjustedEnd = recordingLoopAdjustedTime(
        timelineSeconds, active.startTimelineSeconds);
    const auto localEndBeat = adjustedEnd * tempoBpm_ / 60.0
                              - clip->startBeats;
    const auto endBeat = quantiseRecordedBeat(
        std::max(active.startBeats, localEndBeat));
    const auto duration = std::max(
        std::max(0.03125, midiRecordQuantizationStep()),
        endBeat - active.startBeats);
    const auto updated = updateMidiNote(
        midiRecordingTrackId_, midiRecordingClipId_, active.noteId,
        safeNote, active.startBeats, duration, active.velocity);
    active = {};
    return updated;
}

double AudioTrackController::quantiseRecordedBeat(
    const double beat) const noexcept
{
    const auto step = midiRecordQuantizationStep();
    if (step <= 0.0)
        return std::max(0.0, beat);
    return std::max(0.0, std::round(beat / step) * step);
}

double AudioTrackController::midiRecordQuantizationStep() const noexcept
{
    switch (midiRecordQuantization_) {
        case MidiRecordQuantization::none:
            return 0.0;
        case MidiRecordQuantization::quarterBeat:
            return 1.0;
        case MidiRecordQuantization::eighthBeat:
            return 0.5;
        case MidiRecordQuantization::sixteenthBeat:
            return 0.25;
        case MidiRecordQuantization::thirtySecondBeat:
            return 0.125;
    }
    return 0.0;
}

double AudioTrackController::recordingLoopAdjustedTime(
    const double timelineSeconds,
    const double noteStartSeconds) const noexcept
{
    auto adjusted = std::max(0.0, timelineSeconds);
    if (!looping_ || loopEndSeconds_ <= loopStartSeconds_)
        return std::max(adjusted, noteStartSeconds);
    const auto loopLength = loopEndSeconds_ - loopStartSeconds_;
    while (adjusted + 0.0000001 < noteStartSeconds)
        adjusted += loopLength;
    return adjusted;
}

namespace {

// A note's curves, shared with the audio thread (null: none).
std::shared_ptr<const AudioTrackController::MidiSynthEngineCurves> curvesOf(const AudioTrackController::MidiNote& note)
{
    if (note.pitch.empty() && note.gain.empty() && note.timbre.empty() && note.pressure.empty() && note.pan.empty())
        return nullptr;
    auto curves = std::make_shared<AudioTrackController::MidiSynthEngineCurves>();
    curves->pitch = note.pitch;
    curves->gain = note.gain;
    curves->timbre = note.timbre;
    curves->pressure = note.pressure;
    curves->pan = note.pan;
    return curves;
}

} // namespace

// The notes with an expression get a channel each (2..16), the one free
// longest; two at once never share one unless sixteen sound together.
template <typename Notes>
void assignExpressionChannels(Notes& notes)
{
    std::array<double, 17> busyUntil {};
    busyUntil.fill(-1.0e9);
    for (auto& note : notes) {
        if (note.curves == nullptr)
            continue;
        auto chosen = 2;
        for (int channel = 2; channel <= 16; ++channel)
            if (busyUntil[static_cast<std::size_t>(channel)] < busyUntil[static_cast<std::size_t>(chosen)])
                chosen = channel;
        note.channel = chosen;
        busyUntil[static_cast<std::size_t>(chosen)] = note.endSeconds + 0.5;
    }
}

void AudioTrackController::publishMidiSequence(MidiPlaybackTrack& track)
{
    const auto editorClipId = track.activeEditorClipId.load(
        std::memory_order_acquire);
    if (editorClipId != 0) {
        if (const auto* clip = findMidiClip(track, editorClipId)) {
            const auto currentPosition = track.synth.position();
            publishMidiClipPreviewSequence(track, *clip);
            const auto secondsPerBeat = 60.0 / std::max(20.0, tempoBpm_);
            const auto playStart = juce::jlimit(
                0.0, std::max(0.0, clip->lengthBeats - 0.03125),
                clip->playStartBeats);
            const auto playEnd = juce::jlimit(
                playStart + 0.03125, clip->lengthBeats,
                clip->playEndBeats > playStart ? clip->playEndBeats
                                               : clip->lengthBeats);
            const auto loopStart = juce::jlimit(
                playStart, std::max(playStart, playEnd - 0.03125),
                clip->loopStartBeats);
            const auto loopEnd = juce::jlimit(
                loopStart + 0.03125, playEnd,
                clip->loopEndBeats > loopStart ? clip->loopEndBeats
                                               : playEnd);
            track.synth.setTransportRange(playEnd * secondsPerBeat,
                                          loopStart * secondsPerBeat,
                                          loopEnd * secondsPerBeat);
            track.synth.setLooping(clip->looping);
            track.synth.setPosition(juce::jlimit(
                playStart * secondsPerBeat, playEnd * secondsPerBeat,
                currentPosition));
            return;
        }
        track.activeEditorClipId.store(0, std::memory_order_release);
    }
    // The arrangement plays its own clips; the launcher's wait to be
    // launched.
    auto scheduled = std::make_shared<std::vector<MidiSynthEngine::ScheduledNote>>();
    auto noteCount = std::size_t {};
    for (const auto& clip : track.clips)
        if (clip.launcherScene < 0)
            noteCount += clip.notes.size();
    scheduled->reserve(noteCount);
    const auto secondsPerBeat = 60.0 / tempoBpm_;
    double lengthSeconds = 0.0;
    for (const auto& clip : track.clips) {
        if (clip.launcherScene >= 0)
            continue;
        for (const auto& note : clip.notes) {
            const auto clippedStartBeats = juce::jlimit(
                0.0, clip.lengthBeats, note.startBeats);
            const auto clippedEndBeats = juce::jlimit(
                clippedStartBeats,
                clip.lengthBeats,
                note.startBeats + note.durationBeats);
            const auto start = (clip.startBeats + clippedStartBeats) * secondsPerBeat;
            const auto end = (clip.startBeats + clippedEndBeats) * secondsPerBeat;
            scheduled->push_back({ note.noteNumber, start, end, note.velocity, 1, curvesOf(note), std::clamp(note.chance, 0.0f, 1.0f) });
            lengthSeconds = std::max(lengthSeconds, end);
        }
    }
    std::sort(scheduled->begin(), scheduled->end(), [](const auto& first, const auto& second) {
        return first.startSeconds < second.startSeconds;
    });
    assignExpressionChannels(*scheduled);
    track.synth.setSequence(std::move(scheduled), lengthSeconds);
    refreshTransportRanges();
}

void AudioTrackController::publishMidiTrackAutomation(
    MidiPlaybackTrack& track)
{
    std::sort(track.automation.begin(), track.automation.end(),
              [](const auto& first, const auto& second) {
                  return first.timeSeconds < second.timeSeconds;
              });
    auto published = std::make_shared<
        std::vector<GainAutomationPoint>>();
    published->reserve(track.automation.size());
    for (const auto& point : track.automation)
        published->push_back({ point.timeSeconds, point.valueDb,
                               static_cast<int>(point.curve),
                               point.curveAmount, point.handles });
    track.gainPanSource.setAutomation(std::move(published));
}

void AudioTrackController::publishMidiClipAutomation(
    MidiPlaybackTrack& track, const MidiClip& clip)
{
    auto published = std::make_shared<
        std::vector<GainAutomationPoint>>();
    published->reserve(clip.automation.size());
    for (const auto& point : clip.automation)
        published->push_back({ point.timeSeconds, point.valueDb,
                               static_cast<int>(point.curve),
                               point.curveAmount, point.handles });
    std::sort(published->begin(), published->end(),
              [](const auto& first, const auto& second) {
                  return first.timeSeconds < second.timeSeconds;
              });
    track.gainPanSource.setAutomation(std::move(published));
}

void AudioTrackController::publishMidiClipPreviewSequence(
    MidiPlaybackTrack& track, const MidiClip& clip)
{
    auto scheduled = std::make_shared<
        std::vector<MidiSynthEngine::ScheduledNote>>();
    scheduled->reserve(clip.notes.size());
    const auto secondsPerBeat = 60.0 / std::max(20.0, tempoBpm_);
    const auto playStart = juce::jlimit(
        0.0, std::max(0.0, clip.lengthBeats - 0.03125),
        clip.playStartBeats);
    const auto playEnd = juce::jlimit(
        playStart + 0.03125, clip.lengthBeats,
        clip.playEndBeats > playStart ? clip.playEndBeats
                                      : clip.lengthBeats);
    const auto lengthSeconds = playEnd * secondsPerBeat;
    for (const auto& note : clip.notes) {
        const auto clippedStartBeats = juce::jlimit(
            playStart, playEnd, note.startBeats);
        const auto clippedEndBeats = juce::jlimit(
            clippedStartBeats, playEnd,
            note.startBeats + note.durationBeats);
        if (clippedEndBeats <= clippedStartBeats + 0.000001)
            continue;
        scheduled->push_back({
            note.noteNumber,
            clippedStartBeats * secondsPerBeat,
            clippedEndBeats * secondsPerBeat,
            note.velocity,
            1,
            curvesOf(note),
            std::clamp(note.chance, 0.0f, 1.0f)
        });
    }
    std::sort(scheduled->begin(), scheduled->end(),
              [](const auto& first, const auto& second) {
                  return first.startSeconds < second.startSeconds;
              });
    assignExpressionChannels(*scheduled);
    track.synth.setSequence(std::move(scheduled), lengthSeconds);
}

void AudioTrackController::publishAllMidiSequences()
{
    for (auto& track : midiTracks_)
        publishMidiSequence(*track);
}

void AudioTrackController::publishTrackAutomation(PlaybackTrack& track)
{
    std::sort(track.automation.begin(), track.automation.end(), [](const auto& first, const auto& second) {
        return first.timeSeconds < second.timeSeconds;
    });
    // Its clips' volume lanes, when they drive it, in place of its own.
    const auto& points = track.clipVolume ? *track.clipVolume : track.automation;
    auto published = std::make_shared<std::vector<GainAutomationPoint>>();
    published->reserve(points.size());
    for (const auto& point : points)
        published->push_back({ point.timeSeconds, point.valueDb,
                               static_cast<int>(point.curve),
                               point.curveAmount, point.handles });
    track.gainPanSource.setAutomation(std::move(published));
}

void AudioTrackController::publishTrackPluginAutomation(PlaybackTrack& track)
{
    auto published = std::make_shared<std::vector<PluginParameterAutomationLane>>();
    published->reserve(track.pluginAutomation.size());
    for (auto& lane : track.pluginAutomation) {
        std::sort(lane.points.begin(), lane.points.end(), [](const auto& first, const auto& second) {
            return first.timeSeconds < second.timeSeconds;
        });
        PluginParameterAutomationLane publishedLane;
        publishedLane.pluginIndex = lane.pluginIndex;
        publishedLane.parameterIndex = lane.parameterIndex;
        publishedLane.points.reserve(lane.points.size());
        for (const auto& point : lane.points)
            publishedLane.points.push_back({ point.timeSeconds,
                                              point.normalisedValue,
                                              static_cast<int>(point.curve),
                                              point.curveAmount, point.handles });
        published->push_back(std::move(publishedLane));
    }
    track.gainPanSource.setPluginAutomation(std::move(published));
}

juce::Result AudioTrackController::setTrackRouting(const std::vector<TrackRouting>& routing)
{
    // Kept by identity: a track that moves keeps its routing, one removed
    // leaves it, and the plan can be made again for a new latency.
    const auto keyOf = [this](const TrackRoute& route) -> std::optional<RoutingKey> {
        if (route.midi) {
            if (findMidiTrack(route.id) == nullptr)
                return std::nullopt;
            return RoutingKey { nullptr, route.id };
        }
        if (route.index < tracks_.size())
            return RoutingKey { tracks_[route.index].get(), 0 };
        return std::nullopt;
    };
    std::vector<KeptRouting> kept;
    for (const auto& entry : routing) {
        const auto track = keyOf(entry.track);
        if (!track)
            continue;
        KeptRouting keptEntry { *track, {} };
        for (const auto& out : entry.outputs) {
            KeptOutput output;
            output.gainDb = out.gainDb;
            output.inputBus = std::min<std::uint32_t>(out.inputBus, RoutedInputReceiver::maximumBuses - 1);
            output.preFader = out.preFader;
            output.hardwareBus = out.hardwareBus;
            output.hardwareAfterEffects = out.hardwareAfterEffects;
            if (out.hardwareBus != 0) {
                const auto bus = virtualBusRouter_.bus(out.hardwareBus);
                if (!bus || bus->direction != VirtualBusDirection::output)
                    return juce::Result::fail("a track is routed to an output of the sound card that does not exist");
            } else if (out.destination) {
                const auto destination = keyOf(*out.destination);
                if (!destination || *destination == *track)
                    return juce::Result::fail("a track is routed into itself or into a track that does not exist");
                output.destination = destination;
            }
            keptEntry.outputs.push_back(output);
        }
        kept.push_back(std::move(keptEntry));
    }
    auto previous = std::exchange(routing_, std::move(kept));
    if (auto built = rebuildRouting(); built.failed()) {
        routing_ = std::move(previous);
        return built;
    }
    return juce::Result::ok();
}

juce::Result AudioTrackController::rebuildRouting()
{
    constexpr auto maximumDelay = LatencyCompensatingAudioSource::maximumDelaySamples;
    struct NodeOutput {
        std::optional<std::size_t> to; // none: the master (or a hardware output)
        float gain {};
        std::uint32_t bus {};
        bool preFader {};
        std::uint64_t hardware {};     // an output bus of the sound card
        bool hardwareAfterEffects {};  // joining it after its effects
    };
    struct Node {
        RoutingKey key;
        PlaybackTrack* audio {};
        MidiPlaybackTrack* midi {};
        juce::AudioSource* source {};
        RoutedInputReceiver* receiver {};
        GainPanAudioSource* fader {};
        LatencyCompensatingAudioSource* output {};
        int latency {};
        std::vector<NodeOutput> outputs;
    };
    // Every track, routed or not yet (that one goes to the master).
    std::vector<Node> nodes;
    std::vector<std::pair<RoutingKey, int>> latencies;
    for (auto& track : tracks_) {
        Node node;
        node.key = { track.get(), 0 };
        node.audio = track.get();
        node.source = &track->latencySource;
        node.receiver = &track->gainPanSource;
        node.fader = &track->gainPanSource;
        node.output = &track->latencySource;
        node.latency = track->gainPanSource.totalLatencySamples();
        latencies.emplace_back(node.key, node.latency);
        nodes.push_back(std::move(node));
    }
    for (auto& track : midiTracks_) {
        Node node;
        node.key = { nullptr, track->id };
        node.midi = track.get();
        node.source = &track->latencySource;
        node.receiver = &track->synth;
        node.fader = &track->gainPanSource;
        node.output = &track->latencySource;
        node.latency = track->gainPanSource.totalLatencySamples() + track->synth.effectLatencySamples();
        latencies.emplace_back(node.key, node.latency);
        nodes.push_back(std::move(node));
    }
    const auto nodeOf = [&nodes](const RoutingKey& key) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < nodes.size(); ++index)
            if (nodes[index].key == key)
                return index;
        return std::nullopt;
    };
    for (const auto& entry : routing_) {
        const auto from = nodeOf(entry.track);
        if (!from)
            continue;
        for (const auto& out : entry.outputs) {
            const auto gain = juce::Decibels::decibelsToGain(juce::jlimit(-60.0f, 12.0f, out.gainDb), -60.0f);
            if (out.hardwareBus != 0) {
                // A bus removed since: that output went with it.
                if (const auto bus = virtualBusRouter_.bus(out.hardwareBus); bus && bus->direction == VirtualBusDirection::output)
                    nodes[*from].outputs.push_back({ std::nullopt, gain, 0, out.preFader, out.hardwareBus,
                                                     out.hardwareAfterEffects });
                continue;
            }
            if (!out.destination) {
                nodes[*from].outputs.push_back({ std::nullopt, gain, 0, out.preFader });
                continue;
            }
            // A destination removed since: that output went with it.
            if (const auto to = nodeOf(*out.destination); to && *to != *from)
                nodes[*from].outputs.push_back({ *to, gain, out.inputBus, out.preFader });
        }
    }
    // Senders before what they are routed into; a loop has no such order.
    std::vector<int> incoming(nodes.size(), 0);
    for (const auto& node : nodes)
        for (const auto& out : node.outputs)
            if (out.to)
                ++incoming[*out.to];
    std::vector<std::size_t> order;
    std::vector<std::size_t> ready;
    for (std::size_t index = 0; index < nodes.size(); ++index)
        if (incoming[index] == 0)
            ready.push_back(index);
    while (!ready.empty()) {
        const auto index = ready.front();
        ready.erase(ready.begin());
        order.push_back(index);
        for (const auto& out : nodes[index].outputs)
            if (out.to && --incoming[*out.to] == 0)
                ready.push_back(*out.to);
    }
    if (order.size() != nodes.size())
        return juce::Result::fail("the routing makes a loop");

    // The latency each track's sound arrives with (the latest of what is
    // routed into it; its own sound waits for that) and leaves with (plus
    // its plugins'), and the latest path into the master.
    std::vector<int> arrival(nodes.size(), 0);
    std::vector<int> leaving(nodes.size(), 0);
    auto masterArrival = 0;
    for (const auto index : order) {
        leaving[index] = std::min(maximumDelay, arrival[index] + std::max(0, nodes[index].latency));
        if (nodes[index].outputs.empty())
            masterArrival = std::max(masterArrival, leaving[index]);
        for (const auto& out : nodes[index].outputs) {
            if (out.to)
                arrival[*out.to] = std::max(arrival[*out.to], leaving[index]);
            else
                masterArrival = std::max(masterArrival, leaving[index]);
        }
    }

    // Each cable waits for the latest of what reaches its destination. What
    // all of a track's cables after its fader share is its own delay; the
    // rest, and a cable taken before the fader (ahead of that delay), get a
    // delay line of their own, kept while its length holds.
    std::vector<EdgeDelay> delays;
    const auto delayFor = [&](const juce::AudioSource* from, const juce::AudioSource* to, const std::uint32_t bus,
                              const bool preFader, const int samples, const int channels) -> std::shared_ptr<AudioDelayLine> {
        if (samples <= 0)
            return {};
        const auto taken = [&delays](const std::shared_ptr<AudioDelayLine>& line) {
            return std::any_of(delays.begin(), delays.end(), [&line](const EdgeDelay& edge) { return edge.line == line; });
        };
        for (const auto& existing : edgeDelays_)
            if (existing.from == from && existing.to == to && existing.bus == bus && existing.preFader == preFader
                && existing.line->delaySamples() == samples && existing.line->channelCount() >= channels
                && !taken(existing.line)) {
                delays.push_back(existing);
                return existing.line;
            }
        auto line = std::make_shared<AudioDelayLine>();
        line->prepare(samples, channels);
        line->setDelaySamples(samples);
        delays.push_back({ from, to, bus, preFader, line });
        return line;
    };
    const auto formatOf = [](const Node& node) { return node.audio != nullptr ? node.audio->channelCount : 2; };
    auto plan = std::make_shared<TrackMixer::Plan>();
    // The output buses tracks go to directly, a slot each.
    const auto hardwareSlot = [this, &plan](const std::uint64_t busId, const bool afterEffects, int& channels) {
        channels = 2;
        if (const auto bus = virtualBusRouter_.bus(busId))
            channels = juce::jlimit(1, VirtualAudioBus::maximumChannels, bus->channels);
        for (std::size_t slot = 0; slot < plan->hardware.size(); ++slot)
            if (plan->hardware[slot].busId == busId && plan->hardware[slot].beforeEffects == !afterEffects)
                return static_cast<int>(slot);
        if (plan->hardware.size() >= TrackMixer::maximumDirectOutputs)
            return -1;
        plan->hardware.push_back({ busId, channels, !afterEffects });
        return static_cast<int>(plan->hardware.size() - 1);
    };
    routingEdges_.clear();
    for (const auto index : order) {
        auto& node = nodes[index];
        const auto cableDelay = [&](const NodeOutput& out) {
            return std::max(0, (out.to ? arrival[*out.to] : masterArrival) - leaving[index]);
        };
        auto shared = node.outputs.empty() ? masterArrival - leaving[index] : -1;
        for (const auto& out : node.outputs)
            if (!out.preFader)
                shared = shared < 0 ? cableDelay(out) : std::min(shared, cableDelay(out));
        shared = std::max(0, shared);
        node.output->setDelaySamples(shared);
        if (node.audio != nullptr)
            node.audio->gainPanSource.setInputDelaySamples(arrival[index]);
        if (node.midi != nullptr)
            node.midi->synth.setInputDelaySamples(arrival[index]);

        TrackMixer::Entry entry;
        entry.source = node.source;
        entry.receiver = node.receiver;
        entry.fader = node.fader;
        entry.channels = formatOf(node);
        std::vector<const juce::AudioSource*> destinations;
        auto preFader = false;
        for (const auto& out : node.outputs) {
            auto* to = out.to ? nodes[*out.to].source : nullptr;
            const auto own = cableDelay(out) - (out.preFader ? 0 : shared);
            auto channels = out.to ? formatOf(nodes[*out.to]) : 2;
            auto hardware = -1;
            if (out.hardware != 0) {
                hardware = hardwareSlot(out.hardware, out.hardwareAfterEffects, channels);
                if (hardware < 0)
                    continue;
            }
            entry.outputs.push_back({ to, out.gain, out.bus, out.preFader,
                                      delayFor(node.source, to, out.bus, out.preFader, own, entry.channels), channels,
                                      hardware });
            preFader = preFader || out.preFader;
            if (to != nullptr)
                destinations.push_back(to);
        }
        if (entry.fader != nullptr)
            entry.fader->setKeepPreFader(preFader);
        plan->entries.push_back(std::move(entry));
        routingEdges_.emplace_back(node.source, std::move(destinations));
    }
    edgeDelays_ = std::move(delays);
    plannedLatencies_ = std::move(latencies);
    mixLatencySamples_.store(masterArrival, std::memory_order_release);
    mixerSource_.setPlan(std::move(plan));
    refreshTrackGains();
    return juce::Result::ok();
}

} // namespace cydaw::app
