// The workspace's self-tests: the smoke tests ctest drives through the GUI
// process (--docking-self-test, --project-graph-self-test, ...) and the
// performance bench. They are members of WorkspaceComponent because they
// exercise its internals, but they are test scaffolding, so they live apart
// from the component itself.
#include "WorkspaceComponent.h"
#include "NativeDspRuntime.h"
#include "JsonValue.h"
#include "LibraryDragPayload.h"
#include "ProjectRecovery.h"
#include "WorkspaceComponentInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <typeinfo>
#include <functional>
#include <limits>

namespace cydaw::app {
using detail::deleteRecursivelyWithRetry;
using detail::isNodeGraphState;

bool WorkspaceComponent::runDockingSelfTest()
{
    const auto original = captureLayoutState("Self-test restore");
    // User layouts are intentionally persistent and may start with any panel
    // hidden or floating. Exercise docking from a deterministic visible state
    // and restore the user's exact layout before returning.
    auto testLayout = original;
    testLayout.inspectorVisible = true;
    testLayout.browserVisible = true;
    testLayout.bottomVisible = true;
    testLayout.graphFloating = false;
    testLayout.inspectorFloating = false;
    testLayout.browserFloating = false;
    testLayout.editorFloating = false;
    testLayout.bottomFloating = false;
    applyLayoutState(testLayout, false);
    resized();
    const auto originalTimelineLength = arrangementTimelineLengthSeconds_;
    const auto originalViewStart = arrangementViewStartSeconds_;
    const auto originalZoom = arrangementZoom_;
    const std::array panels { DockPanel::inspector, DockPanel::browser,
                              DockPanel::editor, DockPanel::bottom };
    bool passed = (!commandSearchButton_.isVisible()
                   || (commandSearchButton_.getWidth() > 0
                       && commandSearchButton_.getTooltip().isNotEmpty()))
                   && exportIsReachable()
                   && snapSelector_.isVisible()
                   && snapSelector_.getWidth() > 0
                   && snapSelector_.getTooltip().isNotEmpty()
                   && snapTypeSelector_.isVisible()
                   && snapTypeSelector_.getWidth() > 0
                   && snapTypeSelector_.getTooltip().isNotEmpty()
                   && toolSelectionIsReachable()
                   && timelineRuler_.getTooltip().isNotEmpty()
                   && timelineZoomInButton_.getTooltip().isNotEmpty()
                   && timelineZoomOutButton_.getTooltip().isNotEmpty()
                   && timelineFitButton_.getTooltip().isNotEmpty();
    arrangementTimelineLengthSeconds_ = 16.0;
    fitArrangement();
    zoomArrangement(4.0, 8.0);
    passed = passed && std::abs(arrangementZoom_ - 4.0) < 0.000001
             && std::abs(arrangementViewStartSeconds_ - 6.0) < 0.000001
             && std::abs(timelineScrollBar_.getCurrentRangeSize() - 4.0)
                    < 0.000001;
    panArrangement(100.0);
    passed = passed
             && std::abs(arrangementViewStartSeconds_ - 12.0) < 0.000001;
    fitArrangement();
    passed = passed && std::abs(arrangementZoom_ - 1.0) < 0.000001
             && std::abs(arrangementViewStartSeconds_) < 0.000001;
    arrangementTimelineLengthSeconds_ = originalTimelineLength;
    arrangementViewStartSeconds_ = originalViewStart;
    arrangementZoom_ = originalZoom;
    refreshArrangementView();
    for (const auto panel : panels)
        setPanelFloating(panel, true);
    resized();
    passed = passed && inspectorWindow_ != nullptr && inspectorWindow_->isVisible()
             && inspectorPanelContainer_.getParentComponent() != &inspectorDockSlot_;
    passed = passed && browserWindow_ != nullptr && browserWindow_->isVisible()
             && browser_.getParentComponent() != &browserDockSlot_;
    passed = passed && editorWindow_ != nullptr && editorWindow_->isVisible()
             && editorArea_.getParentComponent() != &editorDockSlot_;
    passed = passed && bottomWindow_ != nullptr && bottomWindow_->isVisible()
             && nodeAreaContainer_.getParentComponent() != &bottomDockSlot_;

    const auto encodedState = serialiseLayoutState(
        captureLayoutState("Docking self-test"));
    const auto decodedState = parseLayoutState(encodedState);
    passed = passed && decodedState.has_value()
             && decodedState->inspectorFloating
             && decodedState->browserFloating
             && decodedState->editorFloating
             && decodedState->bottomFloating
             && decodedState->editorWindowBounds.getWidth() > 0
             && decodedState->bottomWindowBounds.getHeight() > 0;

    for (const auto panel : panels)
        setPanelFloating(panel, false);
    resized();
    passed = passed && inspectorPanelContainer_.getParentComponent() == &inspectorDockSlot_
             && browser_.getParentComponent() == &browserDockSlot_
             && editorArea_.getParentComponent() == &editorDockSlot_
             && nodeAreaContainer_.getParentComponent() == &bottomDockSlot_;

    setEditorMode(7);
    resized();
    passed = passed && currentEditorMode_ == 7
             && nodeGraph_.getParentComponent() == &editorArea_
             && nodeGraph_.isVisible()
             && graphEditViewButton_.getToggleState()
             && bottomPanelMode_ == 1;
    setEditorMode(1);
    resized();
    passed = passed && nodeGraph_.getParentComponent() == &nodeAreaContainer_
             && !graphEditViewButton_.getToggleState();

    auto hiddenSidebars = captureLayoutState("Hidden sidebars self-test");
    hiddenSidebars.inspectorVisible = false;
    hiddenSidebars.browserVisible = false;
    hiddenSidebars.bottomVisible = false;
    applyLayoutState(hiddenSidebars, false);
    passed = passed && !inspectorDockSlot_.isVisible()
             && !browserDockSlot_.isVisible()
             && !bottomDockSlot_.isVisible()
             && inspectorShowButton_.isVisible()
             && browserShowButton_.isVisible()
             && bottomShowButton_.isVisible()
             && centerArea_.getX() >= 30
             && centerArea_.getRight() <= getWidth() - 30;
    hiddenSidebars.inspectorVisible = true;
    hiddenSidebars.browserVisible = true;
    hiddenSidebars.bottomVisible = true;
    applyLayoutState(hiddenSidebars, false);
    passed = passed && inspectorDockSlot_.isVisible()
             && browserDockSlot_.isVisible()
             && bottomDockSlot_.isVisible()
             && inspectorHideButton_.isVisible()
             && browserHideButton_.isVisible()
             && bottomHideButton_.isVisible();
    if (decodedState.has_value()) {
        applyLayoutState(*decodedState, false);
        passed = passed && isPanelFloating(DockPanel::inspector)
                 && isPanelFloating(DockPanel::browser)
                 && isPanelFloating(DockPanel::editor)
                 && isPanelFloating(DockPanel::bottom);
        for (const auto panel : panels)
            setPanelFloating(panel, false);
    }
    applyLayoutState(original, false);
    return passed;
}

juce::String WorkspaceComponent::auditInteractiveControls()
{
    // An empty session leaves most controls unavailable, which is not what the
    // user is looking at. Give the audit a track of each kind first, so the
    // controls that act on a selection are the ones being examined.
    if (audio_.trackCount() == 0 && audio_.midiTracks().empty()) {
        addEmptyMidiTrack();
        const auto midiTracks = audio_.midiTracks();
        if (!midiTracks.empty())
            selectLauncherMidiTrack(midiTracks.front().id);
        resized();
    }
    juce::StringArray lines;
    auto total = 0;
    auto unreachable = 0;
    auto showingCount = 0;
    auto disabledCount = 0;
    std::function<void(juce::Component&, const juce::String&)> walk;
    walk = [&](juce::Component& parent, const juce::String& path) {
        for (auto* child : parent.getChildren()) {
            if (child == nullptr)
                continue;
            const auto name = child->getName().isNotEmpty()
                                  ? child->getName()
                                  : juce::String(typeid(*child).name());
            const auto here = path + "/" + name;
            if (auto* button = dynamic_cast<juce::Button*>(child)) {
                ++total;
                const auto bounds = button->getLocalBounds();
                const auto centre = button->localPointToGlobal(
                    bounds.getCentre());
                const auto local = getLocalPoint(nullptr, centre);
                auto* hit = getComponentAt(local);
                const auto reaches = hit == button
                                     || (hit != nullptr
                                         && button->isParentOf(hit));
                const auto showing = button->isShowing();
                if (showing) {
                    ++showingCount;
                    if (!button->isEnabled()) {
                        ++disabledCount;
                        lines.add("DISABLED " + here + " text='"
                                  + button->getButtonText() + "'");
                    }
                }
                // A button in a view that is not on screen is not a fault,
                // and a button greyed out because nothing is selected is not
                // one either. What matters is a button the user can see whose
                // click lands somewhere else, or that has no area at all.
                if (showing && (!reaches || bounds.isEmpty())) {
                    ++unreachable;
                    lines.add(here + " text='" + button->getButtonText()
                              + "' bounds=" + bounds.toString()
                              + (showing ? "" : " NOT-SHOWING")
                              + (button->isEnabled() ? "" : " DISABLED")
                              + (bounds.isEmpty() ? " EMPTY" : "")
                              + (reaches
                                     ? ""
                                     : " COVERED-BY="
                                           + (hit != nullptr
                                                  ? (hit->getName().isNotEmpty()
                                                         ? hit->getName()
                                                         : juce::String(
                                                             typeid(*hit).name()))
                                                  : juce::String("nothing"))));
                }
            }
            walk(*child, here);
        }
    };
    walk(*this, {});
    lines.insert(0, "buttons=" + juce::String(total)
                        + " visible=" + juce::String(showingCount)
                        + " visibleButDisabled=" + juce::String(disabledCount)
                        + " visibleButUnclickable=" + juce::String(unreachable));
    return lines.joinIntoString("\n");
}

juce::String WorkspaceComponent::runPerformanceSelfTest()
{
    const auto now = [] { return juce::Time::getHighResolutionTicks(); };
    const auto msSince = [](const juce::int64 start) {
        return juce::Time::highResolutionTicksToSeconds(
                   juce::Time::getHighResolutionTicks() - start) * 1000.0;
    };

    const auto tempRoot = juce::File::getSpecialLocation(
                              juce::File::tempDirectory)
                              .getNonexistentChildFile("cydaw-perf", {}, true);
    if (!tempRoot.createDirectory())
        return "performance self test could not create its fixture";

    // A tone long enough that the arrangement has something real to draw.
    const auto tone = tempRoot.getChildFile("tone.wav");
    {
        auto fileOutput = tone.createOutputStream();
        if (fileOutput == nullptr)
            return "performance self test could not write its fixture";
        std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(
            output, juce::AudioFormatWriterOptions()
                        .withSampleRate(48'000.0)
                        .withNumChannels(2)
                        .withBitsPerSample(24));
        if (writer == nullptr)
            return "performance self test could not open a writer";
        juce::AudioBuffer<float> buffer(2, 48'000 * 8);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            const auto value = 0.2f * std::sin(
                static_cast<float>(juce::MathConstants<double>::twoPi * 220.0
                                   * sample / 48'000.0));
            buffer.setSample(0, sample, value);
            buffer.setSample(1, sample, value);
        }
        (void) writer->writeFromAudioSampleBuffer(buffer, 0,
                                                  buffer.getNumSamples());
    }

    constexpr auto audioTrackCount = 8;
    constexpr auto automationPointCount = 2'000;
    constexpr auto midiNoteCount = 400;
    constexpr auto iterations = 40;

    for (auto track = 0; track < audioTrackCount; ++track)
        loadAudioFile(tone);
    for (std::size_t track = 0; track < audio_.trackCount(); ++track)
        for (auto point = 0; point < automationPointCount; ++point) {
            const auto position = static_cast<double>(point)
                                  / automationPointCount * 8.0;
            (void) audio_.addAutomationPoint(
                track, position,
                -12.0f + 12.0f * std::sin(static_cast<float>(position)));
        }

    setEditorMode(4);
    addEmptyMidiTrack();
    const auto midiTracks = audio_.midiTracks();
    if (!midiTracks.empty()) {
        const auto trackId = midiTracks.front().id;
        const auto clipId = audio_.addMidiClip(trackId, "Perf");
        for (auto note = 0; note < midiNoteCount; ++note)
            (void) audio_.addMidiNote(
                trackId, clipId, 36 + (note % 48),
                static_cast<double>(note) * 0.25, 0.2, 0.8f);
        selectedMidiTrackId_ = trackId;
        selectedMidiClipId_ = clipId;
    }
    setEditorMode(1);
    // The automation editor only shows the selected track's lane. Without this
    // the automation figures below were measuring an empty component.
    selectTrack(0);
    refreshAutomationEditor();
    resized();

    juce::Image surface(juce::Image::ARGB, std::max(1, getWidth()),
                        std::max(1, getHeight()), true);
    // A mean over a handful of passes is dominated by whatever else the machine
    // is doing: measured back to back, the same build reported anywhere between
    // 5 ms and 26 ms. The fastest pass is the one that ran without interference,
    // so it is the only number worth comparing between builds.
    const auto timeClipped = [&](juce::Component& component,
                                 const juce::Rectangle<int> clip) {
        if (component.getWidth() <= 0 || component.getHeight() <= 0
            || clip.isEmpty())
            return 0.0;
        for (auto warmup = 0; warmup < 3; ++warmup) {
            juce::Graphics graphics(surface);
            graphics.reduceClipRegion(clip);
            component.paintEntireComponent(graphics, true);
        }
        auto best = std::numeric_limits<double>::max();
        for (auto pass = 0; pass < iterations; ++pass) {
            const auto start = now();
            {
                juce::Graphics graphics(surface);
                graphics.reduceClipRegion(clip);
                component.paintEntireComponent(graphics, true);
            }
            best = std::min(best, msSince(start));
        }
        return best;
    };
    const auto timePaint = [&](juce::Component& component) {
        return timeClipped(component, component.getLocalBounds());
    };
    // What the harness itself costs per pass: constructing a software renderer
    // over the offscreen surface and setting a clip, painting nothing. Roughly
    // 0.45 ms here, and every figure below carries it — read them as
    // differences from this floor, never as absolute paint costs.
    auto harnessMs = std::numeric_limits<double>::max();
    for (auto pass = 0; pass < iterations; ++pass) {
        const auto start = now();
        {
            juce::Graphics graphics(surface);
            graphics.reduceClipRegion(juce::Rectangle<int> { 0, 0, 6, 40 });
        }
        harnessMs = std::min(harnessMs, msSince(start));
    }

    for (auto warmup = 0; warmup < 3; ++warmup)
        resized();
    auto layoutMs = std::numeric_limits<double>::max();
    for (auto pass = 0; pass < iterations; ++pass) {
        const auto start = now();
        resized();
        layoutMs = std::min(layoutMs, msSince(start));
    }

    // What a moving playhead now costs: the same surface, clipped to the two
    // pixel columns it actually invalidates.
    // The same measurement over a set of rectangles, which is what a drag
    // asks for: one region for the cards it moved, one for the minimap at the
    // far corner. Their bounding box would span everything between them.
    const auto timeRegions = [&](juce::Component& component,
                                 const juce::RectangleList<int>& regions) {
        if (regions.isEmpty() || component.getWidth() <= 0)
            return 0.0;
        for (auto warmup = 0; warmup < 3; ++warmup) {
            juce::Graphics graphics(surface);
            graphics.reduceClipRegion(regions);
            component.paintEntireComponent(graphics, true);
        }
        auto best = std::numeric_limits<double>::max();
        for (auto pass = 0; pass < iterations; ++pass) {
            const auto start = now();
            {
                juce::Graphics graphics(surface);
                graphics.reduceClipRegion(regions);
                component.paintEntireComponent(graphics, true);
            }
            best = std::min(best, msSince(start));
        }
        return best;
    };

    const auto timeStripPaint = [&](juce::Component& component) {
        auto strip = component.getLocalBounds();
        strip.setX(strip.getCentreX() - 3);
        strip.setWidth(6);
        return timeClipped(component, strip);
    };

    // What the transport timer actually does on every tick: advance the playhead
    // on every lane. This is the number that decides whether playback feels
    // smooth, and it no longer includes re-rendering the lanes.
    // Exactly the fan-out timerCallback performs thirty times a second.
    const auto advanceTransport = [this](const double position) {
        for (auto& lane : trackLanes_)
            lane->setPlayhead(position);
        timelineRuler_.setPlayhead(position);
        pianoRoll_.setPlayhead(position);
        automationEditor_.setPlayhead(position);
        bottomAutomationEditor_.setPlayhead(position);
        contextAutomationEditor_.setPlayhead(position);
    };
    for (auto warmup = 0; warmup < 3; ++warmup)
        advanceTransport(static_cast<double>(warmup) * 0.01);
    auto playheadMoveMs = std::numeric_limits<double>::max();
    for (auto pass = 0; pass < iterations; ++pass) {
        const auto position = 0.5 + static_cast<double>(pass) * 0.017;
        const auto start = now();
        advanceTransport(position);
        playheadMoveMs = std::min(playheadMoveMs, msSince(start));
    }

    const auto workspaceMs = timePaint(*this);
    const auto arrangementMs = timePaint(trackViewport_);
    const auto playheadMs = timeStripPaint(trackViewport_);
    const auto automationMs = timePaint(automationEditor_);
    // The lane on its own, without the six child controls that share the
    // component: the figure above is dominated by two combo boxes, two labels
    // and two text editors, which no longer repaint when the playhead moves.
    const auto automationLaneMs = timeClipped(
        automationEditor_, automationEditor_.graphBounds().toNearestInt());
    // The same lane with a one-second window on it: what the editor costs once
    // the user zooms in, where all but a handful of points are off screen.
    automationEditor_.setVisibleRange(3.0, 4.0);
    const auto automationZoomedMs = timeClipped(
        automationEditor_, automationEditor_.graphBounds().toNearestInt());
    automationEditor_.setVisibleRange(0.0, 8.0);
    // Same lane, six pixels wide. If the cost tracks the area rather than the
    // number of points, then this surface is fill-rate bound and no amount of
    // culling its contents will move it.
    const auto automationStripMs = timeStripPaint(automationEditor_);
    const auto devicesMs = timePaint(deviceChain_);

    // What a click costs. The harness measured every surface's paint and none
    // of the work a selection sets off, which is where the interface actually
    // stalls: selecting a track saves one graph, restores another, and rebuilds
    // every panel that shows a track.
    const auto timeCall = [&](auto&& action) {
        for (auto warmup = 0; warmup < 2; ++warmup)
            action();
        auto best = std::numeric_limits<double>::max();
        for (auto pass = 0; pass < 8; ++pass) {
            const auto start = now();
            action();
            best = std::min(best, msSince(start));
        }
        return best;
    };
    // Five times a second while anything is selected: the plugin list.
    const auto pluginChainRefreshMs = timeCall([this] {
        refreshDevicePluginChain();
    });
    auto alternate = 0;
    const auto selectTrackMs = timeCall([this, &alternate] {
        selectTrack(static_cast<std::size_t>(alternate++ % 2));
    });
    const auto selectTrackBreakdown = selectTrackProfile();
    const auto restoreBreakdown = nodeGraph_.restoreProfile();
    const auto exportBreakdown = nodeGraph_.exportProfile();
    const auto definitionsBreakdown = nodeGraph_.definitionsProfile();
    const auto modulationBreakdown = modulationGraph_.restoreProfile();
    // One notch on each surface, on the path the mouse takes. A notch
    // arrives thirty to sixty times a second, so this is where lag is made.
    auto notch = 0;
    const auto published = nodeGraph_.graphInterfaceState().parameters;
    const auto knobBinding = published.empty() ? juce::String()
                                               : published.front().key;
    const auto knobNotchMs = timeCall([this, &notch, &knobBinding] {
        (void) nodeGraph_.setGraphInterfaceParameter(
            knobBinding, 0.3 + (notch++ % 2) * 0.01);
        onGraphParametersChanged();
    });
    const auto mixerNotchMs = timeCall([this, &notch] {
        applyChannelMix(0, -6.0f + static_cast<float>(notch++ % 2) * 0.1f,
                        0.0f);
    });
    const auto automationPointId = audio_.automationPoints(0).empty()
        ? std::uint64_t {} : audio_.automationPoints(0).front().id;
    const auto automationNotchMs = timeCall([this, automationPointId, &notch] {
        (void) audio_.updateAutomationPoint(
            0, automationPointId, 0.5,
            -6.0f + static_cast<float>(notch++ % 2) * 0.1f);
        refreshAutomationEditor();
        refreshBottomAutomationEditor();
        markProjectDirty();
    });
    const auto noteEditMs = timeCall([this] {
        auto notes = audio_.midiNotes(selectedMidiTrackId_,
                                      selectedMidiClipId_);
        if (!notes.empty())
            notes.front().startBeats += 0.0;
        (void) audio_.replaceMidiNotes(selectedMidiTrackId_,
                                       selectedMidiClipId_, std::move(notes));
        refreshMidiEditor();
        refreshTimelineRanges();
    });
    const auto arrangementPanMs = timeCall([this, &notch] {
        panArrangement((notch++ % 2) ? 0.01 : -0.01);
    });
    const auto pianoRollPaintMs = timePaint(pianoRoll_);
    const auto mixerPaintMs = timePaint(mixer_);
    // The modulation tick runs thirty times a second across every track
    // whether or not anything is assigned.
    const auto modulationTickMs = timeCall([this] { applyTrackModulations(); });

    // The same notch on a MIDI track: an instrument and the chain behind it.
    // A different publish path — the voice plan is recompiled and the chain
    // republished — so it gets its own figure.
    auto midiKnobMs = -1.0;
    juce::String midiKnobProfile;
    if (selectedMidiTrackId_ != 0) {
        const auto trackBefore = selectedTrack_;
        selectedTrack_.reset();
        setEditorMode(2);
        restoreNodeGraphForMidiTrack(selectedMidiTrackId_);
        (void) nodeGraph_.insertFactoryDevice("table-synth");
        for (auto band = 0; band < 4; ++band)
            (void) nodeGraph_.insertNodeOnConnection(
                band == 0 ? "voice-filter" : "eq-band", "master", "eq-band");
        setMidiTrackNodeGraphState(selectedMidiTrackId_,
                                   nodeGraph_.serialise());
        const auto midiKnobs = nodeGraph_.graphInterfaceState().parameters;
        const auto midiKnob = midiKnobs.empty() ? juce::String()
                                                : midiKnobs.back().key;
        midiKnobMs = timeCall([this, &notch, &midiKnob] {
            (void) nodeGraph_.setGraphInterfaceParameter(
                midiKnob, 0.3 + (notch++ % 2) * 0.01);
            onGraphParametersChanged();
        });
        midiKnobProfile = graphNotchProfile_;
        setEditorMode(1);
        if (trackBefore.has_value())
            selectTrack(*trackBefore);
    }
    const auto reselectSameMs = timeCall([this] { selectTrack(0); });
    const auto refreshMixerMs = timeCall([this] { refreshMixer(); });
    const auto refreshSelectedMs = timeCall([this] { refreshSelectedTrack(); });
    const auto serialiseGraphMs = timeCall([this] {
        (void) nodeGraph_.serialise();
    });
    const auto saveGraphMs = timeCall([this] { saveCurrentNodeGraphState(); });
    const auto restoreGraphMs = timeCall([this] {
        restoreNodeGraphForTrack(0);
    });
    const auto automationTargetsMs = timeCall([this] {
        refreshAutomationTargets();
    });
    const auto timelineRangesMs = timeCall([this] { refreshTimelineRanges(); });
    auto harvestState = nodeGraph_.serialise();
    const auto updateSharedMs = timeCall([this, &harvestState] {
        updateSharedNodeModules(harvestState);
    });
    // What the thirty-second autosave builds on the message thread before
    // handing it to a background writer. Eight tracks, each merging the
    // shared class library into its own stored state.
    const auto autosaveDocumentMs = timeCall([this] {
        (void) createProjectDocument(projectRecoveryFile_, false, true);
    });
    // The autosave's share of it: the arrays of points and notes are left
    // to the writer thread, and this is what stays on the message thread.
    const auto autosaveDeferredMs = timeCall([this] {
        DeferredDocumentWork deferred;
        (void) createProjectDocument(projectRecoveryFile_, false, true,
                                     &deferred);
    });
    const auto mergeLibraryMs = timeCall([this] {
        (void) graphStateWithSharedModules(trackNodeGraphStates_[0]);
    });
    const auto mergedState = graphStateWithSharedModules(
        trackNodeGraphStates_[0]);
    const auto graphRestoreMs = timeCall([this, &mergedState] {
        nodeGraph_.restore(mergedState);
    });
    const auto compileMs = timeCall([this] {
        const auto graph = nodeGraph_.nativeDspGraph();
        (void) audio_.setTrackNativeDspGraph(0, graph);
    });
    const auto deviceSurfaceMs = timeCall([this] {
        refreshNodeDeviceSurface();
    });
    const auto saveModulationMs = timeCall([this] {
        saveCurrentModulationState();
    });
    const auto restoreModulationMs = timeCall([this] {
        restoreModulationStateForTrack(0);
    });
    const auto clearAnalysisMs = timeCall([this] { nodeGraph_.clearAnalysis(); });
    const auto audioClipsMs = timeCall([this] {
        const auto clips = audio_.audioClips(0);
        normaliseAudioClipSelection(clips);
    });
    const auto laneSelectionMs = timeCall([this] {
        for (std::size_t index = 0; index < trackLanes_.size(); ++index)
            trackLanes_[index]->setSelected(index == 0);
    });

    // Last, because it is the one surface this harness has to disturb to
    // measure. The node graph sits in a panel left folded here, so it had no
    // size and the figure silently read 0.000 rather than saying it had
    // measured nothing; lending it a surface any earlier moved every click
    // figure below it by half a millisecond.
    const auto graphBoundsBefore = nodeGraph_.getBounds();
    if (nodeGraph_.getWidth() <= 0 || nodeGraph_.getHeight() <= 0)
        nodeGraph_.setBounds(editorArea_.getBounds().isEmpty()
                                 ? getLocalBounds()
                                 : editorArea_.getBounds());
    const auto graphMs = timePaint(nodeGraph_);
    // The same surface carrying a patch someone might actually build. Two
    // cards say nothing about whether a full one stays smooth to drag.
    auto populated = 0;
    for (const auto& key : { "gain", "drive", "lowpass", "eq", "compressor",
                             "delay", "reverb", "stereo-width" })
        for (auto copy = 0; copy < 4; ++copy)
            if (nodeGraph_.insertNode(key).wasOk())
                ++populated;
    if (populated > 0)
        nodeGraph_.spreadNodesForTest();
    const auto graphLoadedMs = populated > 0 ? timePaint(nodeGraph_) : 0.0;
    // The painter says in its own comments that it skips cards lying outside
    // the canvas. A six-pixel strip leaves almost nothing visible without
    // magnifying anything: if the cost does not fall here, the painter is
    // doing work for cards it never draws.
    // One frame of a drag: the region the move asks for, painted. Next to
    // graphLoaded above, this is what narrowing the repaint bought.
    const auto dragRegions = nodeGraph_.dragNodeForTest(
        "compressor", { 9.0f, 7.0f });
    const auto graphDragMs = timeRegions(nodeGraph_, dragRegions);
    const auto graphDragProfile = nodeGraph_.paintProfile();


    const auto graphCorner = nodeGraph_.getLocalBounds()
                                 .removeFromBottom(48).removeFromLeft(48);
    const auto graphStripMs = populated > 0
                                  ? timeClipped(nodeGraph_, graphCorner)
                                  : 0.0;
    const auto graphStripProfile = nodeGraph_.paintProfile();
    // Last, because each insertion grows the graph: run any earlier and
    // every figure below would describe a scene twice the size.
    const auto insertNodeMs = timeCall([this] {
        (void) nodeGraph_.insertNode("gain");
    });
    const auto insertHandler = layoutHandlerProfile_;
    const auto connectMs = timeCall([this] {
        (void) nodeGraph_.connectNodePorts("gain", "audio-out",
                                            "drive", "audio-in", true);
    });
    const auto historyCost = nodeGraph_.historyCostForTest();
    const auto undoMs = timeCall([this] { nodeGraph_.undoForTest(); });
    const auto undoRestore = nodeGraph_.restoreProfile();
    const auto redoMs = timeCall([this] { nodeGraph_.redoForTest(); });
    // How the graph behaves as a patch grows. Every figure above runs on
    // thirty-four cards; what decides whether a real session stays usable is
    // the slope, not the value at thirty-four.
    juce::String scale;
    for (const auto target : { 75, 150, 300 }) {
        while (nodeGraph_.nodeCount() < target
               && nodeGraph_.insertNode("gain").wasOk()) {
        }
        const auto serialiseMs = timeCall([this] {
            (void) nodeGraph_.serialise();
        });
        const auto insertMs = timeCall([this] {
            (void) nodeGraph_.insertNode("gain");
        });
        const auto paintMs = timePaint(nodeGraph_);
        // A fader notch on the path the mouse takes. Alternating values stop
        // the slider short-circuiting an unchanged set. A gesture fires this
        // sixty times a second, so the notch is the number that matters.
        const auto driveNotchMs = timeCall([this, &notch] {
            deviceChain_.dragEffectSliderForTest(6.0 + (notch++ % 2) * 0.1);
        });
        const auto driveNotchProfile = nodeGraph_.effectParametersProfile();
        const auto gainNotchMs = timeCall([this, &notch] {
            deviceChain_.dragMixerSliderForTest(-6.0 + (notch++ % 2) * 0.1);
        });
        // The card knob: the one notch whose cost grows with the patch.
        const auto scaleKnobs = nodeGraph_.graphInterfaceState().parameters;
        const auto scaleKnob = scaleKnobs.empty() ? juce::String()
                                                  : scaleKnobs.front().key;
        const auto knobMs = timeCall([this, &notch, &scaleKnob] {
            (void) nodeGraph_.setGraphInterfaceParameter(
                scaleKnob, 0.3 + (notch++ % 2) * 0.01);
            onGraphParametersChanged();
        });
        const auto knobProfile = graphNotchProfile_ + " cache="
                                 + nodeGraph_.exportCacheProfile();
        // One analysis frame, as playback delivers thirty a second, and how
        // much of the canvas it asks to have repainted. Measured here, where
        // the graph has bounds: measured before it had any, the share was
        // zero and said nothing.
        cydaw::engine::AudioAnalysisFrame analysisFrame;
        analysisFrame.channelCount = 2;
        analysisFrame.waveformCount
            = cydaw::engine::AudioAnalysisFrame::waveformCapacity;
        for (std::size_t index = 0; index < analysisFrame.waveformCount;
             ++index)
            analysisFrame.waveform[index] = static_cast<float>(
                0.4 * std::sin(0.07 * static_cast<double>(index)));
        analysisFrame.peak = { 0.4f, 0.4f };
        analysisFrame.rms = { 0.25f, 0.25f };
        const auto analysisFrameMs = timeCall([this, &analysisFrame] {
            nodeGraph_.setAnalysisFrame(analysisFrame);
        });
        auto analysisArea = 0;
        for (const auto& rectangle : nodeGraph_.analysisRepaintRegionForTest())
            analysisArea += rectangle.getWidth() * rectangle.getHeight();
        const auto analysisShare = static_cast<double>(analysisArea)
            / static_cast<double>(std::max(
                1, nodeGraph_.getWidth() * nodeGraph_.getHeight()));
        scale << " n=" << nodeGraph_.nodeCount()
              << "[serialise=" << juce::String(serialiseMs, 2)
              << " insert=" << juce::String(insertMs, 2)
              << " paint=" << juce::String(paintMs, 2)
              << " history(" << nodeGraph_.historyFootprint() << ")"
              << " " << nodeGraph_.insertProfile()
              << " add(" << nodeGraph_.addProfile() << ")"
              << " fader(drive=" << juce::String(driveNotchMs, 3)
              << " gain=" << juce::String(gainNotchMs, 3)
              << " || " << driveNotchProfile << ")"
              << " knob(" << juce::String(knobMs, 3) << " || "
              << knobProfile << ")"
              << " analysis(frame=" << juce::String(analysisFrameMs, 3)
              << " repaintShare=" << juce::String(analysisShare, 2) << ")"
              << "]";
    }

    // Only now: with the graph back at its own size the canvas is unusable,
    // and the search for a free spot leaves by its degenerate path.
    nodeGraph_.setBounds(graphBoundsBefore);

    // Which renderer the window actually uses. The figures above come from a
    // juce::Image and are therefore always software; they compare two builds,
    // they do not say what a frame costs on screen.
    juce::String renderer = "none";
    if (auto* peer = getPeer()) {
        const auto engines = peer->getAvailableRenderingEngines();
        const auto current = peer->getCurrentRenderingEngine();
        renderer = juce::isPositiveAndBelow(current, engines.size())
                       ? engines[current]
                       : juce::String("unknown");
    }

    const auto report =
        juce::String("tracks=") + juce::String(static_cast<int>(audio_.trackCount()))
        + " automation=" + juce::String(automationPointCount)
        + " notes=" + juce::String(midiNoteCount)
        + " | harnessOverhead=" + juce::String(harnessMs, 4)
        + " layout=" + juce::String(layoutMs, 3)
        + " workspace=" + juce::String(workspaceMs, 3)
        + " arrangement=" + juce::String(arrangementMs, 3)
        + " playheadStrip=" + juce::String(playheadMs, 3)
        + " transportTick=" + juce::String(playheadMoveMs, 4)
        + " automation=" + juce::String(automationMs, 3)
        + " automationLane=" + juce::String(automationLaneMs, 3)
        + " automationLaneZoomed=" + juce::String(automationZoomedMs, 3)
        + " automationStrip=" + juce::String(automationStripMs, 4)
        + " graph=" + juce::String(graphMs, 3)
        + " graphNodes=" + juce::String(populated)
        + " graphLoaded=" + juce::String(graphLoadedMs, 3)
        + " graphDrag=" + juce::String(graphDragMs, 3)
        + " graphDragDrew[" + graphDragProfile
        + " regions=" + juce::String(dragRegions.getNumRectangles()) + "]"
        + " insertNode=" + juce::String(insertNodeMs, 3)
        + " connectPorts=" + juce::String(connectMs, 3)
        + " history[" + historyCost + "]"
        + " insert[" + nodeGraph_.insertProfile()
        + " " + nodeGraph_.placementProfile() + "]"
        + " add[" + nodeGraph_.addProfile() + "]"
        + " insertHandler[" + insertHandler + "]"
        + " afterEdit[" + nodeGraph_.layoutChangeProfile()
        + " || " + layoutHandlerProfile_ + "]"
        + " undo=" + juce::String(undoMs, 3)
        + " undoRestore[" + undoRestore + "]"
        + " redo=" + juce::String(redoMs, 3)
        + " graphCorner=" + juce::String(graphStripMs, 3)
        + " graphCornerDrew[" + graphStripProfile + "]"
        + " || notch[knob=" + juce::String(knobNotchMs, 3)
        + " mixer=" + juce::String(mixerNotchMs, 3)
        + " automation=" + juce::String(automationNotchMs, 3)
        + " noteEdit=" + juce::String(noteEditMs, 3)
        + " arrangementPan=" + juce::String(arrangementPanMs, 3)
        + " midiKnob=" + juce::String(midiKnobMs, 3) + "{" + midiKnobProfile
        + "} modulationTick=" + juce::String(modulationTickMs, 3)
        + "] paint[pianoRoll=" + juce::String(pianoRollPaintMs, 2)
        + " mixer=" + juce::String(mixerPaintMs, 2) + "]"
        + " renderer=" + renderer.replaceCharacter(' ', '-')
        + " || scale" + scale
        + " devices=" + juce::String(devicesMs, 3)
        + " pluginChainRefresh=" + juce::String(pluginChainRefreshMs, 3)
        + " | selectTrack=" + juce::String(selectTrackMs, 3)
        + " reselectSame=" + juce::String(reselectSameMs, 3)
        + " refreshMixer=" + juce::String(refreshMixerMs, 3)
        + " refreshSelectedTrack=" + juce::String(refreshSelectedMs, 3)
        + " serialiseGraph=" + juce::String(serialiseGraphMs, 3)
        + " saveGraphState=" + juce::String(saveGraphMs, 3)
        + " restoreGraphState=" + juce::String(restoreGraphMs, 3)
        + " automationTargets=" + juce::String(automationTargetsMs, 3)
        + " timelineRanges=" + juce::String(timelineRangesMs, 3)
        + " updateSharedModules=" + juce::String(updateSharedMs, 3)
        + " autosaveDocument=" + juce::String(autosaveDocumentMs, 3)
        + " autosaveDeferred=" + juce::String(autosaveDeferredMs, 3)
        + " documentProfile=[" + projectDocumentProfile_ + "]"
        + " mergeLibrary=" + juce::String(mergeLibraryMs, 3)
        + " graphRestore=" + juce::String(graphRestoreMs, 3)
        + " compileAndApply=" + juce::String(compileMs, 3)
        + " deviceSurface=" + juce::String(deviceSurfaceMs, 3)
        + " saveModulation=" + juce::String(saveModulationMs, 3)
        + " restoreModulation=" + juce::String(restoreModulationMs, 3)
        + " clearAnalysis=" + juce::String(clearAnalysisMs, 3)
        + " audioClips=" + juce::String(audioClipsMs, 3)
        + " laneSelection=" + juce::String(laneSelectionMs, 3)
        + " || select[" + selectTrackBreakdown + "]"
        + " || export[" + exportBreakdown + "]"
        + " || definitions[" + definitionsBreakdown + "]"
        + " || restore[" + restoreBreakdown + "]"
        + " || modulationRestore[" + modulationBreakdown + "]"
        + " (ms, fastest of " + juce::String(iterations) + ")";

    audio_.clearTracks();
    clearTrackLanes();
    currentProjectFile_ = {};
    (void) deleteRecursivelyWithRetry(tempRoot);
    return report;
}

bool WorkspaceComponent::runProjectGraphSelfTest()
{
    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getNonexistentChildFile("cydaw-track-graph-self-test", {}, true);
    if (!tempRoot.createDirectory())
        return false;

    const auto writeTone = [](const juce::File& file, const float level) {
        auto fileOutput = file.createOutputStream();
        if (fileOutput == nullptr || !fileOutput->setPosition(0)
            || fileOutput->truncate().failed())
            return false;
        std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(
            output,
            juce::AudioFormatWriterOptions()
                .withSampleRate(48'000.0)
                .withNumChannels(2)
                .withBitsPerSample(24));
        if (writer == nullptr)
            return false;
        juce::AudioBuffer<float> buffer(2, 4'096);
        buffer.clear();
        buffer.applyGain(level);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            const auto value = level * std::sin(
                static_cast<float>(juce::MathConstants<double>::twoPi
                                   * 220.0 * sample / 48'000.0));
            buffer.setSample(0, sample, value);
            buffer.setSample(1, sample, value);
        }
        return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    };

    const auto firstAudio = tempRoot.getChildFile("first.wav");
    const auto secondAudio = tempRoot.getChildFile("second.wav");
    const auto project = tempRoot.getChildFile("per-track-graphs.cydaw");
    auto savedVirtualBusId = std::uint64_t {};
    // Names the block currently under test. The assertions below are chained
    // into a single boolean, so without this a failure reports nothing beyond
    // a non-zero exit code.
    const char* stage = "audio and MIDI track setup";
    auto passed = writeTone(firstAudio, 0.12f) && writeTone(secondAudio, 0.18f);
    if (audio_.midiTracks().empty()) {
        setEditorMode(4);
        addEmptyMidiTrack();
        const auto createdMidiTracks = audio_.midiTracks();
        passed = passed && createdMidiTracks.size() == 1
                 && createdMidiTracks.front().clips.empty()
                 && selectedMidiTrackId_ == createdMidiTracks.front().id;
        if (passed)
            selectedMidiClipId_ = audio_.addMidiClip(
                selectedMidiTrackId_, tr("midi.clip") + " 1");
        passed = passed && selectedMidiClipId_ != 0;
        setEditorMode(1);
    }
    if (passed) {
        stage = "the class library copy is deep at every level";
        // Stamp every object in the copy, then insist not one of those stamps
        // reaches the original. A copy that shares even one nested object
        // gives the autosave writer a tree the message thread can still edit
        // underneath it.
        const std::function<void(juce::var&)> stamp =
            [&stamp](juce::var& value) {
                if (auto* array = value.getArray()) {
                    for (auto& element : *array)
                        stamp(element);
                    return;
                }
                if (auto* object = value.getDynamicObject()) {
                    const auto& properties = object->getProperties();
                    for (int index = 0; index < properties.size(); ++index) {
                        // The copy shares the object underneath, so stamping
                        // through it reaches the one being walked.
                        auto child = properties.getValueAt(index);
                        stamp(child);
                    }
                    object->setProperty("cydawCloneProbe", 1);
                }
            };
        const std::function<bool(const juce::var&)> stamped =
            [&stamped](const juce::var& value) -> bool {
                if (const auto* array = value.getArray()) {
                    for (const auto& element : *array)
                        if (stamped(element))
                            return true;
                    return false;
                }
                if (const auto* object = value.getDynamicObject()) {
                    if (object->hasProperty("cydawCloneProbe"))
                        return true;
                    for (const auto& property : object->getProperties())
                        if (stamped(property.value))
                            return true;
                }
                return false;
            };
        auto libraryCopy = sharedNodeModulesCopy();
        const auto* originalValues = sharedNodeModules_.getArray();
        const auto* copiedValues = libraryCopy.getArray();
        stamp(libraryCopy);
        passed = originalValues != nullptr && copiedValues != nullptr
                 && !originalValues->isEmpty()
                 && originalValues->size() == copiedValues->size()
                 && !stamped(sharedNodeModules_);
        // The copy is the kept clone, shared read-only by the documents
        // made after it; stamped here, it is dropped so no document
        // carries the stamps.
        sharedNodeModulesCloneOf_ = nullptr;
        sharedNodeModulesClone_ = juce::var {};
    }

    if (passed) {
        stage = "audio tracks and per-track Graph states";
        loadAudioFile(firstAudio);
        loadAudioFile(secondAudio);
        passed = audio_.trackCount() == 2 && trackNodeGraphStates_.size() == 2;
    }

    if (passed) {
        stage = "track and clip inspector selection";
        selectTrack(0);
        passed = inspectorSelection_ == InspectorSelection::audioTrack;
        const auto clips = audio_.audioClips(0);
        passed = passed && !clips.empty();
        if (passed) {
            selectAudioClip(0, clips.front().id);
            passed = inspectorSelection_ == InspectorSelection::audioClip;
            selectTrack(0);
            passed = passed
                     && inspectorSelection_ == InspectorSelection::audioTrack;
        }
    }

    if (passed) {
        stage = "arrangement marquee delete, undo, copy and paste";
        const auto firstTrackClips = audio_.audioClips(0);
        const auto secondTrackClips = audio_.audioClips(1);
        arrangementMarqueeSelectionActive_ = true;
        arrangementTimeSelectionStartSeconds_ = 0.0;
        arrangementTimeSelectionEndSeconds_ = 1.0;
        arrangementTimeSelectionFirstTrack_ = 0;
        arrangementTimeSelectionLastTrack_ = 1;
        arrangementMarqueeClipIdsByTrack_ = {
            { firstTrackClips.front().id },
            { secondTrackClips.front().id }
        };
        passed = deleteArrangementMarqueeSelection()
                 && audio_.audioClips(0).empty()
                 && audio_.audioClips(1).empty()
                 && audio_.canUndoArrangementEdit()
                 && audio_.undoArrangementEdit()
                 && audio_.audioClips(0).size() == 1
                 && audio_.audioClips(1).size() == 1;
        if (passed) {
            const auto restoredFirst = audio_.audioClips(0);
            const auto restoredSecond = audio_.audioClips(1);
            arrangementMarqueeSelectionActive_ = true;
            arrangementTimeSelectionStartSeconds_ = 0.0;
            arrangementTimeSelectionEndSeconds_ = 0.1;
            arrangementTimeSelectionFirstTrack_ = 0;
            arrangementTimeSelectionLastTrack_ = 1;
            arrangementMarqueeClipIdsByTrack_ = {
                { restoredFirst.front().id },
                { restoredSecond.front().id }
            };
            audio_.clearArrangementEditHistory();
            const auto copied = copyArrangementMarqueeSelection();
            const auto pasted = copied
                                && pasteArrangementMarqueeClipboard(0, 0.25);
            const auto pastedFirstId = pasted
                                           && !arrangementMarqueeClipIdsByTrack_[0]
                                                   .empty()
                                       ? arrangementMarqueeClipIdsByTrack_[0]
                                             .front()
                                       : 0;
            const auto pastedSecondId = pasted
                                            && !arrangementMarqueeClipIdsByTrack_[1]
                                                    .empty()
                                        ? arrangementMarqueeClipIdsByTrack_[1]
                                              .front()
                                        : 0;
            const auto pastedFirst = audio_.audioClip(0, pastedFirstId);
            const auto pastedSecond = audio_.audioClip(1, pastedSecondId);
            const auto pasteShapeValid = pasted
                                         && audio_.audioClips(0).size() == 2
                                         && audio_.audioClips(1).size() == 2
                                         && pastedFirst.has_value()
                                         && pastedSecond.has_value()
                                         && std::abs(
                                                pastedFirst->startSeconds
                                                - 0.25) < 0.001
                                         && std::abs(
                                                pastedSecond->startSeconds
                                                - 0.25) < 0.001;
            const auto undoAvailable = audio_.canUndoArrangementEdit();
            const auto undone = undoAvailable
                                && audio_.undoArrangementEdit();
            const auto undoShapeValid = undone
                                        && audio_.audioClips(0).size() == 1
                                        && audio_.audioClips(1).size() == 1;
            const auto redone = undoShapeValid
                                && audio_.redoArrangementEdit();
            const auto redoShapeValid = redone
                                        && audio_.audioClips(0).size() == 2
                                        && audio_.audioClips(1).size() == 2;
            const auto finalUndo = redoShapeValid
                                   && audio_.undoArrangementEdit();
            passed = pasteShapeValid && undoShapeValid && redoShapeValid
                     && finalUndo
                     && audio_.audioClips(0).size() == 1
                     && audio_.audioClips(1).size() == 1;
        }
        if (passed) {
            const auto restoredFirst = audio_.audioClips(0);
            const auto restoredSecond = audio_.audioClips(1);
            arrangementMarqueeSelectionActive_ = true;
            arrangementTimeSelectionStartSeconds_ = 0.02;
            arrangementTimeSelectionEndSeconds_ = 0.05;
            arrangementTimeSelectionFirstTrack_ = 0;
            arrangementTimeSelectionLastTrack_ = 1;
            arrangementMarqueeClipIdsByTrack_ = {
                { restoredFirst.front().id },
                { restoredSecond.front().id }
            };
            audio_.clearArrangementEditHistory();
            const auto silenced = editArrangementMarqueeRange(
                AudioTrackController::ArrangementRangeOperation::
                    silenceRange);
            passed = silenced
                     && audio_.audioClips(0).size() == 2
                     && audio_.audioClips(1).size() == 2
                     && arrangementMarqueeSelectionActive_
                     && arrangementMarqueeClipCount() == 0
                     && audio_.canUndoArrangementEdit()
                     && audio_.undoArrangementEdit()
                     && audio_.audioClips(0).size() == 1
                     && audio_.audioClips(1).size() == 1
                     && audio_.redoArrangementEdit()
                     && audio_.audioClips(0).size() == 2
                     && audio_.audioClips(1).size() == 2
                     && audio_.undoArrangementEdit()
                     && audio_.audioClips(0).size() == 1
                     && audio_.audioClips(1).size() == 1;
        }
        if (passed) {
            clearArrangementMarqueeSelection();
            const auto verticalSource = audio_.audioClips(0);
            const auto sourceCount = verticalSource.size();
            const auto destinationCount = audio_.audioClips(1).size();
            selectAudioClip(
                0, verticalSource.front().id,
                WaveformComponent::RegionSelectionMode::replace);
            audio_.clearArrangementEditHistory();
            const auto destinationY = trackLanes_[1]
                                          ->getScreenBounds()
                                          .getCentreY();
            const auto movedVertically = moveArrangementSelection(
                0, verticalSource.front().id, 0.01, destinationY);
            passed = movedVertically
                     && selectedTrack_ == 1
                     && selectedAudioClipId_ != 0
                     && audio_.audioClips(0).size() == sourceCount - 1
                     && audio_.audioClips(1).size() == destinationCount + 1
                     && audio_.canUndoArrangementEdit()
                     && audio_.undoArrangementEdit()
                     && audio_.audioClips(0).size() == sourceCount
                     && audio_.audioClips(1).size() == destinationCount
                     && audio_.redoArrangementEdit()
                     && audio_.audioClips(0).size() == sourceCount - 1
                     && audio_.audioClips(1).size() == destinationCount + 1
                     && audio_.undoArrangementEdit()
                     && audio_.audioClips(0).size() == sourceCount
                     && audio_.audioClips(1).size() == destinationCount;
        }
        clearArrangementMarqueeSelection();
        audio_.clearArrangementEditHistory();
        refreshTimelineRanges();
    }
    auto firstGraphState = graphStateWithSharedModules(factoryNodeGraphState_);
    if (passed) {
        stage = "clip fades, gain and time stretching";
        auto* graph = firstGraphState.getDynamicObject();
        auto* connections = graph != nullptr
                                ? graph->getProperty("connections").getArray()
                                : nullptr;
        auto* modules = graph != nullptr
                            ? graph->getProperty("modules").getArray()
                            : nullptr;
        passed = connections != nullptr && !connections->isEmpty()
                 && modules != nullptr;
        if (passed) {
            connections->remove(0);
            auto module = juce::DynamicObject::Ptr(new juce::DynamicObject());
            module->setProperty("key", "self-test-module");
            module->setProperty("name", "Shared self-test module");
            module->setProperty("nodes", juce::Array<juce::var> {});
            module->setProperty("connections", juce::Array<juce::var> {});
            modules->add(juce::var(module.get()));
            setTrackNodeGraphState(0, firstGraphState);
            setTrackNodeGraphState(1, factoryNodeGraphState_);
            const auto firstGraph = audio_.nativeDspGraph(0);
            const auto secondGraph = audio_.nativeDspGraph(1);
            // Reconstituted, not raw: a stored state keeps only its own
            // inline classes, and both readers put the shared library back.
            // This is what the editor restores and what the project file
            // receives, so it is where the class has to be found.
            const auto firstReconstituted = graphStateWithSharedModules(
                trackNodeGraphStates_[0]);
            const auto secondReconstituted = graphStateWithSharedModules(
                trackNodeGraphStates_[1]);
            const auto* firstModules = firstReconstituted
                                           .getDynamicObject()->getProperty("modules").getArray();
            const auto* secondModules = secondReconstituted
                                            .getDynamicObject()->getProperty("modules").getArray();
            const juce::DynamicObject* firstModuleObject = nullptr;
            if (firstModules != nullptr)
                for (const auto& moduleValue : *firstModules)
                    if (const auto* candidate = moduleValue.getDynamicObject();
                        candidate != nullptr
                        && candidate->getProperty("key").toString()
                               == "self-test-module") {
                        firstModuleObject = candidate;
                        break;
                    }
            const auto* firstModuleInterface = firstModuleObject != nullptr
                                                   ? firstModuleObject
                                                         ->getProperty("interface")
                                                         .getDynamicObject()
                                                   : nullptr;
            const auto* interfaceControls = firstModuleInterface != nullptr
                                                ? firstModuleInterface
                                                      ->getProperty("controls").getArray()
                                                : nullptr;
            passed = firstGraph.connections.size() + 1 == secondGraph.connections.size()
                     && firstModules != nullptr && firstModules->size() >= 4
                     && secondModules != nullptr && secondModules->size() >= 4
                     && interfaceControls != nullptr
                     && interfaceControls->size() >= 3;
        }
    }
    if (passed) {
        stage = "the editor keeps the selected track's graph when another's is set";
        // Setting a track's graph passes through the editor. With track 0
        // selected, setting track 1's must leave the editor showing track
        // 0's: a graph restored in passing is nobody's selection, and the
        // restore of the selection must not be skipped as already shown.
        selectTrack(0);
        const auto shownBefore = nodeGraph_.nativeDspGraph().connections.size();
        setTrackNodeGraphState(1, factoryNodeGraphState_);
        const auto shownAfter = nodeGraph_.nativeDspGraph().connections.size();
        passed = selectedTrack_ && *selectedTrack_ == 0
                 && shownAfter == shownBefore
                 && shownAfter == audio_.nativeDspGraph(0).connections.size();
    }
    if (passed) {
        stage = "a device knob and the chain's gain reach a MIDI track from the launcher";
        // The launcher shows a MIDI track's devices and its gain as the
        // clip editor does. A knob turned there used to publish the
        // instrument as the factory graph — the context test knew only the
        // clip and graph editors — and the chain's gain was disabled.
        const auto midiTracks = audio_.midiTracks();
        passed = !midiTracks.empty();
        if (passed) {
            const auto trackId = midiTracks.front().id;
            setEditorMode(4);
            selectLauncherMidiTrack(trackId);
            passed = nodeGraph_.insertFactoryDevice("synth-bass").wasOk();
            if (passed) {
                setMidiTrackNodeGraphState(trackId, nodeGraph_.serialise());
                refreshNodeDeviceSurface();
                const auto factoryBefore = cloneJsonValue(factoryNodeGraphState_);
                const auto planBefore = audio_.midiTrackInstrumentGraph(trackId);
                juce::String sustainKey;
                for (const auto& parameter :
                     nodeGraph_.graphInterfaceState().parameters)
                    if (parameter.key.containsIgnoreCase("sustain")) {
                        sustainKey = parameter.key;
                        break;
                    }
                passed = sustainKey.isNotEmpty() && planBefore.enabled
                         && isMidiNodeGraphContext();
                if (passed) {
                    deviceChain_.turnNodeKnobForTest(sustainKey, 0.05);
                    const auto planAfter = audio_.midiTrackInstrumentGraph(trackId);
                    deviceChain_.dragMixerSliderForTest(-9.0);
                    const auto tracksAfter = audio_.midiTracks();
                    passed = std::abs(planAfter.sustainLevel - 0.05f) < 0.01f
                             && planAfter.sustainLevel != planBefore.sustainLevel
                             && jsonValuesMatch(factoryBefore, factoryNodeGraphState_)
                             && !tracksAfter.empty()
                             && std::abs(tracksAfter.front().gainDb + 9.0f) < 0.01f;
                }
            }
        }
        setEditorMode(1);
        selectTrack(0);
    }
    if (passed) {
        stage = "a modulation graph keeps the classes it was saved with";
        // A project can carry value-graph classes this session has never
        // seen. Storing one and reading it back used to lose it: the
        // injection keeps only the classes a stored state marks inline, and
        // nothing harvested the rest, so the file carried a class the editor
        // ignored. It has to reach the shared library, be visible to the
        // editor, and be written back out by the document.
        auto carried = juce::DynamicObject::Ptr(new juce::DynamicObject());
        carried->setProperty("key", "self-test-value-class");
        carried->setProperty("name", "Carried value class");
        carried->setProperty("nodes", juce::Array<juce::var> {});
        carried->setProperty("connections", juce::Array<juce::var> {});
        auto fromFile = cloneJsonValue(factoryModulationGraphState_);
        auto* fromFileGraph = fromFile.getDynamicObject();
        auto modules = fromFileGraph != nullptr
                           ? fromFileGraph->getProperty("modules")
                           : juce::var();
        passed = fromFileGraph != nullptr && modules.getArray() != nullptr;
        if (passed) {
            modules.getArray()->add(juce::var(carried.get()));
            fromFileGraph->setProperty("modules", modules);

            // The function the loader itself calls, not a copy of what it
            // does: a test that reimplements the path it is guarding stops
            // guarding it the moment the path changes.
            adoptTrackModulationGraph(0, fromFile);
            const auto stored = trackModulationStates_[0].graph;

            const auto holds = [](const juce::var& state) {
                const auto* graph = state.getDynamicObject();
                const auto* values = graph != nullptr
                    ? graph->getProperty("modules").getArray() : nullptr;
                if (values == nullptr)
                    return false;
                for (const auto& value : *values)
                    if (const auto* object = value.getDynamicObject();
                        object != nullptr
                        && object->getProperty("key").toString()
                               == "self-test-value-class")
                        return true;
                return false;
            };
            passed = !holds(stored)
                     && holds(modulationStateForEditor(stored))
                     && holds(modulationStateWithSharedModules(stored));
        }
    }

    if (passed) {
        stage = "MIDI instrument Graph parameters";
        auto instrumentState = graphStateWithSharedModules(
            factoryMidiNodeGraphState_);
        auto* graph = instrumentState.getDynamicObject();
        auto* nodes = graph != nullptr
                          ? graph->getProperty("nodes").getArray()
                          : nullptr;
        auto* modules = graph != nullptr
                            ? graph->getProperty("modules").getArray()
                            : nullptr;
        const auto midiTracks = audio_.midiTracks();
        passed = nodes != nullptr && modules != nullptr && !midiTracks.empty();
        if (passed) {
            auto instrumentNode = juce::DynamicObject::Ptr(
                new juce::DynamicObject());
            instrumentNode->setProperty("id", static_cast<juce::int64>(3));
            instrumentNode->setProperty("type", "poly-sine");
            instrumentNode->setProperty("title", "Poly Sine");
            instrumentNode->setProperty("x", 0.38);
            instrumentNode->setProperty("y", 0.42);
            nodes->add(juce::var(instrumentNode.get()));
            const auto addConnection = [graph](const juce::int64 source,
                                                const juce::int64 destination,
                                                const int kind) {
                auto* connections = graph->getProperty("connections")
                                        .getArray();
                if (connections == nullptr)
                    return;
                auto connection = juce::DynamicObject::Ptr(
                    new juce::DynamicObject());
                connection->setProperty("source", source);
                connection->setProperty("destination", destination);
                connection->setProperty("kind", kind);
                connections->add(juce::var(connection.get()));
            };
            addConnection(1, 3, 1);
            addConnection(3, 2, 0);
            graph->setProperty("nextNodeId", static_cast<juce::int64>(4));
            for (auto& moduleValue : *modules) {
                auto* module = moduleValue.getDynamicObject();
                auto* children = module != nullptr
                                     && module->getProperty("key").toString()
                                            == "poly-sine"
                                 ? module->getProperty("nodes").getArray()
                                 : nullptr;
                if (children == nullptr)
                    continue;
                for (auto& nodeValue : *children) {
                    auto* node = nodeValue.getDynamicObject();
                    auto* parameters = node != nullptr
                                           ? node->getProperty("parameters")
                                                 .getDynamicObject()
                                           : nullptr;
                    if (parameters == nullptr)
                        continue;
                    if (node->getProperty("type").toString()
                        == "poly-oscillator")
                        parameters->setProperty("oscillatorWaveform", 3.0);
                    if (node->getProperty("type").toString()
                        == "voice-filter")
                        parameters->setProperty("cutoffHz", 1'350.0);
                }
            }
            setMidiTrackNodeGraphState(midiTracks.front().id,
                                       instrumentState);
            const auto plan = audio_.midiTrackInstrumentGraph(
                midiTracks.front().id);
            passed = plan.enabled
                     && plan.waveform
                         == cydaw::engine::PolyOscillatorWaveform::square
                     && std::abs(plan.filterCutoffHz - 1'350.0f) < 0.001f;
        }
    }
    if (passed) {
        stage = "clip duplication and MIDI clip editing";
        const auto firstClips = audio_.audioClips(0);
        std::uint64_t duplicateClipId {};
        std::uint64_t importedClipId {};
        passed = firstClips.size() == 1
                 && audio_.setAudioClipStretchSettings(
                        0, firstClips.front().id, 175.0,
                        AudioTrackController::StretchMode::tonal)
                 && audio_.stretchAudioClip(
                        0, firstClips.front().id, 0.160)
                 && audio_.addAudioClipStretchMarker(
                        0, firstClips.front().id,
                        firstClips.front().sourceOffsetSeconds
                            + firstClips.front().sourceDurationSeconds * 0.5,
                        0.080) != 0
                 && (duplicateClipId = audio_.duplicateAudioClip(
                         0, firstClips.front().id, 0.040)) != 0
                 && audio_.autoCrossfadeAudioClip(0, duplicateClipId)
                 && audio_.addAudioClipFromFile(
                        0, secondAudio, 0.250, &importedClipId).wasOk()
                 && importedClipId != 0
                 && audio_.setLauncherClip(
                        0, 2, "Independent source", 0.05, 0.20,
                        true, 0xff4fd3ae, secondAudio);
    }
    if (passed) {
        stage = "automation lanes and plugin automation";
        trackPresentations_[0] = {
            "Self-test track", juce::Colour(0xff53d0c2) };
        savedVirtualBusId = audio_.addVirtualBus(
            VirtualBusDirection::input);
        auto vocalBus = audio_.virtualBus(savedVirtualBusId);
        if (vocalBus.has_value()) {
            vocalBus->name = "Self-test vocal input";
            vocalBus->channels = 1;
            vocalBus->physicalChannelA = 2;
            passed = audio_.updateVirtualBus(*vocalBus)
                     && audio_.setTrackInputBus(0, savedVirtualBusId);
        } else {
            passed = false;
        }
        snapSelector_.setSelectedId(5, juce::dontSendNotification);
        snapTypeSelector_.setSelectedId(3, juce::dontSendNotification);
        arrangementToolSelector_.setSelectedId(4,
                                                juce::dontSendNotification);
        audio_.setLoopRange(0.125, 0.375);
        loopButton_.setToggleState(true, juce::dontSendNotification);
        audio_.setLooping(true);
        saveProject(project);
        const auto parsed = juce::JSON::parse(project);
        const auto* root = parsed.getDynamicObject();
        const auto* tracks = root != nullptr
                                 ? root->getProperty("tracks").getArray()
                                 : nullptr;
        const auto* midiTracks = root != nullptr
                                     ? root->getProperty("midiTracks").getArray()
                                     : nullptr;
        const auto* virtualBuses = root != nullptr
                                       ? root->getProperty("virtualBuses")
                                             .getArray()
                                       : nullptr;
        const auto savedBusHasGraph = virtualBuses != nullptr
            && std::any_of(
                virtualBuses->begin(), virtualBuses->end(),
                [savedVirtualBusId](const auto& value) {
                    const auto* bus = value.getDynamicObject();
                    return bus != nullptr
                           && static_cast<std::uint64_t>(
                                  static_cast<juce::int64>(
                                      bus->getProperty("id")))
                                  == savedVirtualBusId
                           && isNodeGraphState(
                               bus->getProperty("nodeGraph"));
                });
        passed = root != nullptr
                 && static_cast<int>(root->getProperty("version")) == 36
                  && static_cast<int>(root->getProperty(
                         "arrangementSnapMode")) == 5
                  && static_cast<int>(root->getProperty(
                         "arrangementSnapType")) == 3
                  && static_cast<int>(root->getProperty(
                         "arrangementTool")) == 4
                  && static_cast<bool>(root->getProperty(
                         "loopRangeExplicit"))
                  && std::abs(static_cast<double>(root->getProperty(
                                  "loopStart")) - 0.125) < 0.000001
                  && std::abs(static_cast<double>(root->getProperty(
                                  "loopEnd")) - 0.375) < 0.000001
                  && tracks != nullptr && tracks->size() == 2
                  && virtualBuses != nullptr && virtualBuses->size() >= 3
                  && savedBusHasGraph
                  && midiTracks != nullptr && !midiTracks->isEmpty()
                  && isNodeGraphState(
                         midiTracks->getReference(0)
                             .getProperty("nodeGraph", {}));
        if (passed) {
            const auto* first = tracks->getReference(0).getDynamicObject();
            const auto* second = tracks->getReference(1).getDynamicObject();
            const auto connectionsForTrack = [](const juce::DynamicObject* track) {
                const auto* graph = track != nullptr
                                        ? track->getProperty("nodeGraph").getDynamicObject()
                                        : nullptr;
                return graph != nullptr
                           ? graph->getProperty("connections").getArray()
                           : nullptr;
            };
            const auto* firstConnections = connectionsForTrack(first);
            const auto* secondConnections = connectionsForTrack(second);
            const auto* firstAudioClips = first != nullptr
                                              ? first->getProperty("audioClips").getArray()
                                              : nullptr;
            const auto* firstLauncherClips = first != nullptr
                                                 ? first->getProperty(
                                                       "launcherClips").getArray()
                                                 : nullptr;
            const auto* firstStretchMarkers
                = firstAudioClips != nullptr && !firstAudioClips->isEmpty()
                      ? firstAudioClips->getReference(0)
                            .getProperty("stretchMarkers", {})
                            .getArray()
                      : nullptr;
            const auto* firstModulationGraph = first != nullptr
                                                    ? first->getProperty(
                                                          "modulationGraph")
                                                          .getDynamicObject()
                                                    : nullptr;
            passed = firstConnections != nullptr && secondConnections != nullptr
                     && first->getProperty("name").toString()
                            == "Self-test track"
                     && static_cast<std::uint64_t>(
                            static_cast<juce::int64>(first->getProperty(
                                "inputBusId"))) == savedVirtualBusId
                     && static_cast<juce::uint32>(static_cast<juce::int64>(
                            first->getProperty("colour"))) == 0xff53d0c2
                     && firstConnections->size() + 1 == secondConnections->size()
                     && firstAudioClips != nullptr && firstAudioClips->size() == 3
                     && firstStretchMarkers != nullptr
                     && firstStretchMarkers->size() == 1
                     && firstLauncherClips != nullptr
                     && firstLauncherClips->size() == 1
                     && firstLauncherClips->getReference(0)
                            .getProperty("source", {}).toString().isNotEmpty()
                     && !juce::File::isAbsolutePath(
                            firstLauncherClips->getReference(0)
                                .getProperty("source", {}).toString())
                     && firstModulationGraph != nullptr
                     && static_cast<bool>(firstModulationGraph->getProperty(
                            "valueGraph"))
                     && static_cast<double>(
                            firstAudioClips->getReference(0)
                                .getDynamicObject()->getProperty("sourceDuration"))
                            < static_cast<double>(
                                  firstAudioClips->getReference(0)
                                      .getDynamicObject()->getProperty("duration"))
                     && std::abs(static_cast<double>(
                            firstAudioClips->getReference(0)
                                .getDynamicObject()->getProperty("sourceTempo"))
                                 - 175.0) < 0.001
                     && static_cast<int>(
                            firstAudioClips->getReference(0)
                                .getDynamicObject()->getProperty("stretchMode"))
                            == static_cast<int>(
                                AudioTrackController::StretchMode::tonal)
                     && static_cast<double>(
                            firstAudioClips->getReference(0)
                                .getDynamicObject()->getProperty("fadeOut")) > 0.0
                     && static_cast<double>(
                            firstAudioClips->getReference(1)
                                .getDynamicObject()->getProperty("fadeIn")) > 0.0;
        }
    }

    if (passed) {
        stage = "virtual bus routing and Graph assignment";
        loadProject(project);
        const auto restoredAudioClips = audio_.audioClips(0);
        const auto restoredLauncherClips = audio_.launcherClips(0);
        const auto restoredVirtualBus = audio_.virtualBus(savedVirtualBusId);
        passed = audio_.trackCount() == 2
                  && restoredVirtualBus.has_value()
                  && restoredVirtualBus->name == "Self-test vocal input"
                  && restoredVirtualBus->channels == 1
                  && restoredVirtualBus->physicalChannelA == 2
                  && audio_.virtualBusGraph(savedVirtualBusId).nodes.size()
                         == 2
                  && audio_.virtualBusGraph(savedVirtualBusId)
                         .connections.size() == 1
                  && audio_.trackInfo(0).inputBusId == savedVirtualBusId
                  && restoredAudioClips.size() == 3
                  && restoredLauncherClips.size() == 1
                  && restoredLauncherClips.front().sourceFile == secondAudio
                  && snapSelector_.getSelectedId() == 5
                  && snapTypeSelector_.getSelectedId() == 3
                  && arrangementToolSelector_.getSelectedId() == 4
                  && trackPresentations_.size() == 2
                  && trackPresentations_[0].name == "Self-test track"
                  && trackPresentations_[0].colour.getARGB() == 0xff53d0c2
                  && loopButton_.getToggleState()
                  && audio_.hasExplicitLoopRange()
                  && std::abs(audio_.loopStart() - 0.125) < 0.000001
                  && std::abs(audio_.loopEnd() - 0.375) < 0.000001
                  && !audio_.canUndoArrangementEdit(0)
                 && !audio_.canRedoArrangementEdit(0)
                 && std::any_of(
                        restoredAudioClips.begin(), restoredAudioClips.end(),
                        [&secondAudio](const auto& clip) {
                            return clip.sourceFile == secondAudio;
                        })
                 && std::count_if(
                        restoredAudioClips.begin(), restoredAudioClips.end(),
                        [](const auto& clip) {
                            return clip.fadeInSeconds > 0.0
                                   || clip.fadeOutSeconds > 0.0;
                        }) == 2
                 && std::any_of(
                        restoredAudioClips.begin(), restoredAudioClips.end(),
                        [](const auto& clip) {
                            return std::abs(clip.durationSeconds
                                            - clip.sourceDurationSeconds)
                                   > 0.001;
                        })
                 && std::any_of(
                        restoredAudioClips.begin(), restoredAudioClips.end(),
                        [](const auto& clip) {
                            return std::abs(clip.sourceTempoBpm - 175.0)
                                       < 0.001
                                   && clip.stretchMode
                                          == AudioTrackController::StretchMode::tonal;
                        })
                 && std::any_of(
                        restoredAudioClips.begin(), restoredAudioClips.end(),
                        [](const auto& clip) {
                            return !clip.stretchMarkers.empty()
                                   && std::abs(
                                          clip.stretchMarkers.front()
                                                  .timelineSeconds
                                              - 0.080) < 0.001;
                        })
                 && audio_.nativeDspGraph(0).connections.size() + 1
                        == audio_.nativeDspGraph(1).connections.size();
        if (passed) {
            const auto midiTracks = audio_.midiTracks();
            const auto instrument = midiTracks.empty()
                                        ? cydaw::engine::PolySynthGraphPlan {}
                                        : audio_.midiTrackInstrumentGraph(
                                              midiTracks.front().id);
            passed = !midiTracks.empty() && instrument.enabled
                     && instrument.waveform
                            == cydaw::engine::PolyOscillatorWaveform::square
                     && std::abs(instrument.filterCutoffHz - 1'350.0f)
                            < 0.001f;
        }
        if (passed) {
            selectTrack(0);
            const auto firstConnectionCount = nodeGraph_.nativeDspGraph().connections.size();
            selectTrack(1);
            passed = firstConnectionCount + 1
                     == nodeGraph_.nativeDspGraph().connections.size();
        }
    }

    if (passed) {
        stage = "project save with per-track Graph presets";
        selectTrack(0);
        const auto firstPresetLoaded = nodeGraph_.loadFactoryPreset(7).wasOk();
        selectTrack(1);
        const auto secondPresetLoaded = nodeGraph_.loadFactoryPreset(10).wasOk();
        passed = firstPresetLoaded && secondPresetLoaded;
        saveProject(project);
        const auto parsed = juce::JSON::parse(project);
        const auto* root = parsed.getDynamicObject();
        const auto* tracks = root != nullptr
                                 ? root->getProperty("tracks").getArray()
                                 : nullptr;
        if (tracks == nullptr || tracks->size() != 2) {
            passed = false;
        } else {
            const auto presetForTrack = [](const juce::var& trackValue) {
                const auto* track = trackValue.getDynamicObject();
                const auto* graph = track != nullptr
                                        ? track->getProperty("nodeGraph").getDynamicObject()
                                        : nullptr;
                return graph != nullptr
                           ? graph->getProperty("factoryPreset").toString()
                           : juce::String {};
            };
            passed = presetForTrack(tracks->getReference(0)) == "parallel-ambience"
                     && presetForTrack(tracks->getReference(1)) == "noise-atmosphere";
        }
    }

    if (passed) {
        stage = "project reload preserves per-track Graph presets";
        loadProject(project);
        const auto* firstGraph = trackNodeGraphStates_[0].getDynamicObject();
        const auto* secondGraph = trackNodeGraphStates_[1].getDynamicObject();
        // Both presets happen to compile to six nodes, so comparing sizes
        // proved nothing about the two tracks holding different graphs.
        // Compare what the nodes actually are.
        const auto dspSignature = [](const cydaw::engine::NativeDspGraph& graph) {
            juce::String signature;
            for (const auto& node : graph.nodes)
                signature << static_cast<int>(node.kind) << ',';
            return signature;
        };
        passed = firstGraph != nullptr && secondGraph != nullptr
                 && firstGraph->getProperty("factoryPreset").toString()
                        == "parallel-ambience"
                 && secondGraph->getProperty("factoryPreset").toString()
                        == "noise-atmosphere"
                 && dspSignature(audio_.nativeDspGraph(0))
                        != dspSignature(audio_.nativeDspGraph(1));
    }

    if (passed) {
        stage = "an audio track's chain runs a dropped LFO while playing";
        // The whole road in the running DAW: an LFO dropped on the gain of
        // an audio track's chain reaches the engine, runs while the
        // transport does, and comes back as the dot on the knob's ring.
        // Until the track ran the shared runtime its chain lost every
        // control node at compile time and had nothing to report.
        selectTrack(0);
        nodeGraph_.rebuildEmbeddedControlsForTest();
        passed = nodeGraph_.insertNodeBeforeOutput("gain").wasOk()
                 && nodeGraph_.insertNode("lfo-core").wasOk();
        nodeGraph_.rebuildEmbeddedControlsForTest();
        passed = passed
                 && nodeGraph_.modulateParameterForTest("lfo-core", "gain",
                                                        "gain.db");
        publishNativeDspGraph();
        const auto chain = audio_.nativeDspGraph(0);
        const auto controlNodes = std::count_if(
            chain.nodes.begin(), chain.nodes.end(), [](const auto& node) {
                return isControlOnlyNativeKind(node.kind);
            });
        const auto compiled = editorShowsSelection() && controlNodes == 2
                              && chain.modulations.size() == 2;
        auto playing = false;
        std::size_t reported {};
        std::optional<double> position;
        std::optional<double> later;
        juce::Range<double> ring;
        auto onRing = false;
        if (passed && compiled
            && audio_.deviceManager().getCurrentAudioDevice() != nullptr) {
            // The play button clicks on the next turn of the message loop,
            // which a sleeping self test never gives it: the transport
            // directly, and silently. Two readings a beat apart: the LFO
            // has to have moved between them, or nothing ran.
            audio_.silenceMasterOutput();
            audio_.play();
            playing = audio_.isPlaying();
            juce::Thread::sleep(120);
            const auto values = audio_.trackControlValues(0);
            reported = values.size();
            nodeGraph_.setControlValues(values);
            position = nodeGraph_.modulationPositionForTest("gain", "gain.db");
            ring = nodeGraph_.modulationRingForTest("gain", "gain.db");
            juce::Thread::sleep(120);
            nodeGraph_.setControlValues(audio_.trackControlValues(0));
            later = nodeGraph_.modulationPositionForTest("gain", "gain.db");
            onRing = position.has_value() && later.has_value()
                     && ring.expanded(0.01).contains(*position)
                     && ring.expanded(0.01).contains(*later)
                     && std::abs(*position - *later) > 0.01;
            audio_.stop();
            nodeGraph_.setControlValues({});
        }
        juce::Logger::writeToLog(
            juce::String("audio track LFO: refused='")
            + nodeGraph_.modulateFailureForTest() + "' controlNodes="
            + juce::String(controlNodes) + " modulations="
            + juce::String(static_cast<int>(chain.modulations.size()))
            + " playing=" + (playing ? "yes" : "no") + " reported="
            + juce::String(static_cast<int>(reported)) + " position="
            + (position.has_value() ? juce::String(*position, 2)
                                    : juce::String("none"))
            + " later="
            + (later.has_value() ? juce::String(*later, 2)
                                 : juce::String("none"))
            + " ring=" + juce::String(ring.getStart(), 2) + ".."
            + juce::String(ring.getEnd(), 2));
        passed = passed && compiled && playing && reported == 2 && onRing;
    }

    if (!passed)
        juce::Logger::writeToLog(
            juce::String("Project graph self test failed while checking: ")
            + stage);
    audio_.clearTracks();
    clearTrackLanes();
    currentProjectFile_ = {};
    const auto cleaned = deleteRecursivelyWithRetry(tempRoot);
    if (!cleaned)
        juce::Logger::writeToLog(
            "Project graph self test could not remove its temporary fixture");
    return passed && cleaned;
}

bool WorkspaceComponent::runMasterExportSelfTest()
{
    const auto tempRoot = juce::File::getSpecialLocation(
                              juce::File::tempDirectory)
                              .getNonexistentChildFile(
                                  "cydaw-master-export-self-test", {}, true);
    if (!tempRoot.createDirectory())
        return false;

    const auto source = tempRoot.getChildFile("source.wav");
    const auto destination = tempRoot.getChildFile("master.wav");
    const auto cancelled = tempRoot.getChildFile("cancelled.wav");
    const auto writeTone = [](const juce::File& file) {
        auto fileOutput = file.createOutputStream();
        if (fileOutput == nullptr || !fileOutput->setPosition(0)
            || fileOutput->truncate().failed())
            return false;
        std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(
            output, juce::AudioFormatWriterOptions()
                        .withSampleRate(48'000.0)
                        .withNumChannels(2)
                        .withBitsPerSample(24));
        if (writer == nullptr)
            return false;
        juce::AudioBuffer<float> buffer(2, 4'800);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            const auto value = static_cast<float>(
                0.18 * std::sin(juce::MathConstants<double>::twoPi
                                * 330.0 * sample / 48'000.0));
            buffer.setSample(0, sample, value);
            buffer.setSample(1, sample, -value);
        }
        return writer->writeFromAudioSampleBuffer(
            buffer, 0, buffer.getNumSamples());
    };

    auto passed = writeTone(source);
    if (passed) {
        loadAudioFile(source);
        passed = audio_.trackCount() == 1;
    }
    if (passed) {
        audio_.setTrackMixer(0, -3.0f, 0.15f, false);
        audio_.setTrackEffects(0, 5.0f, 6'000.0f);
        const auto midiTracks = audio_.midiTracks();
        if (!midiTracks.empty() && !midiTracks.front().clips.empty())
            (void) audio_.addMidiNote(midiTracks.front().id,
                                      midiTracks.front().clips.front().id,
                                      67, 0.0, 0.5, 0.7f);
        const auto expectedDuration = audio_.length() + 2.0;
        passed = audio_.startMasterExport(destination).wasOk();
        for (int attempt = 0;
             attempt < 600 && passed && !audio_.masterExportComplete(); ++attempt)
            juce::Thread::sleep(10);
        passed = passed && audio_.masterExportComplete()
                 && audio_.finishMasterExport().wasOk();

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formats.createReaderFor(destination));
        if (reader == nullptr) {
            passed = false;
        } else {
            const auto expectedSamples = static_cast<juce::int64>(
                std::llround(reader->sampleRate * expectedDuration));
            const auto sampleCount = static_cast<int>(std::min<juce::int64>(
                reader->lengthInSamples, 8'192));
            juce::AudioBuffer<float> rendered(2, std::max(1, sampleCount));
            rendered.clear();
            const auto readable = sampleCount > 0
                                  && reader->read(&rendered, 0, sampleCount, 0,
                                                  true, true);
            passed = passed && reader->numChannels == 2
                     && reader->bitsPerSample == 24
                     && std::abs(reader->lengthInSamples - expectedSamples) <= 1
                     && readable
                     && rendered.getMagnitude(0, sampleCount) > 0.01f;
        }
    }

    if (passed) {
        passed = audio_.startMasterExport(cancelled).wasOk();
        juce::Thread::sleep(80);
        audio_.cancelMasterExport();
        passed = passed && !cancelled.existsAsFile();
    }

    if (passed) {
        // Compiling the chain and playing it are different claims. Render the
        // same instrument on the same note twice — once bare, once with a
        // tight low-pass behind it. If the engine ignores what the graph puts
        // after the instrument, the two renders come back the same.
        audio_.setTrackMixer(0, -60.0f, 0.0f, true);
        setEditorMode(4);
        if (audio_.midiTracks().empty())
            addEmptyMidiTrack();
        const auto chainTracks = audio_.midiTracks();
        passed = !chainTracks.empty();
        const auto chainTrackId = passed ? chainTracks.front().id
                                         : std::uint64_t {};
        if (passed) {
            const auto clipId = chainTracks.front().clips.empty()
                ? audio_.addMidiClip(chainTrackId, "Chain")
                : chainTracks.front().clips.front().id;
            passed = clipId != 0
                     && audio_.addMidiNote(chainTrackId, clipId, 60, 0.0, 2.0,
                                           0.9f)
                            != 0;
            selectedMidiTrackId_ = chainTrackId;
            setEditorMode(2);
        }
        const auto renderMagnitude = [this, &tempRoot](
                                         const juce::String& name) {
            const auto file = tempRoot.getChildFile(name + ".wav");
            if (audio_.startMasterExport(file).failed())
                return -1.0f;
            for (int attempt = 0;
                 attempt < 900 && !audio_.masterExportComplete(); ++attempt)
                juce::Thread::sleep(10);
            if (!audio_.masterExportComplete()
                || audio_.finishMasterExport().failed())
                return -1.0f;
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const auto reader = std::unique_ptr<juce::AudioFormatReader>(
                formats.createReaderFor(file));
            if (reader == nullptr)
                return -1.0f;
            const auto count = static_cast<int>(
                std::min<juce::int64>(reader->lengthInSamples, 48'000));
            if (count <= 0)
                return -1.0f;
            juce::AudioBuffer<float> rendered(2, count);
            rendered.clear();
            if (!reader->read(&rendered, 0, count, 0, true, true))
                return -1.0f;
            return rendered.getMagnitude(0, count);
        };
        if (passed)
            passed = nodeGraph_.insertFactoryDevice("table-synth").wasOk();
        if (passed) {
            setMidiTrackNodeGraphState(chainTrackId, nodeGraph_.serialise());
            passed = audio_.midiTrackEffectGraph(chainTrackId).nodes.size() > 0;
        }
        const auto bare = passed ? renderMagnitude("chain-bare") : -1.0f;
        if (passed)
            passed = nodeGraph_
                         .insertNodeOnConnection("voice-filter", "master",
                                                 "lowpass")
                         .wasOk();
        if (passed) {
            // A low-pass defaults to twenty kilohertz, which would let
            // everything through and prove nothing. Close it.
            auto state = cloneJsonValue(nodeGraph_.serialise());
            auto closed = false;
            if (auto* root = state.getDynamicObject())
                if (auto* nodes = root->getProperty("nodes").getArray())
                    for (auto& node : *nodes)
                        if (auto* body = node.getDynamicObject();
                            body != nullptr
                            && body->getProperty("type").toString()
                                   == "lowpass")
                            if (auto* parameters
                                = body->getProperty("parameters")
                                      .getDynamicObject()) {
                                parameters->setProperty("cutoffHz", 180.0);
                                closed = true;
                            }
            passed = closed;
            nodeGraph_.restore(state);
            setMidiTrackNodeGraphState(chainTrackId, nodeGraph_.serialise());
        }
        const auto filtered = passed ? renderMagnitude("chain-filtered")
                                     : -1.0f;
        passed = passed && bare > 0.01f && filtered >= 0.0f
                 && filtered < bare * 0.7f;
        if (!passed)
            juce::Logger::writeToLog(
                "master export self test: a chain behind the instrument was "
                "not heard (bare=" + juce::String(bare, 4)
                + " filtered=" + juce::String(filtered, 4) + ")");
        {
            // A table that serialises and compiles can still be silent. Render
            // the same note twice: once on the built-in bank, once on a table
            // cut down to its fundamental. A pure sine and a sawtooth cannot
            // come back as the same signal.
            auto shape = nodeGraph_.nodeWavetable("wavetable");
            for (auto& frame : shape.harmonics) {
                frame = {};
                frame[0] = 1.0f;
            }
            passed = nodeGraph_.setNodeWavetable("wavetable", shape);
            if (passed) {
                // The editor defers building the bank to the message loop, and
                // this test never runs one.
                nodeGraph_.rebuildWavetableBanksForTest();
                setMidiTrackNodeGraphState(chainTrackId, nodeGraph_.serialise());
            }
            const auto sine = passed ? renderMagnitude("table-sine") : -1.0f;
            // Same note, same envelope, same filter: only the table differs, so
            // a difference in what comes out can only have come from it.
            passed = passed && sine > 0.005f && std::abs(sine - filtered) > 0.004f;
            if (!passed)
                juce::Logger::writeToLog(
                    "master export self test: a shaped wavetable was not heard "
                    "(filtered=" + juce::String(filtered, 4)
                    + " sine=" + juce::String(sine, 4) + ")");
        }
    }

    if (passed) {
        // The splitting is tested on samples in memory. What is left is the
        // joinery: open a file, take a channel, land the result on the card,
        // and undo it like any other edit. That only tests with a real file.
        const auto table = tempRoot.getChildFile("table.wav");
        constexpr int cycle = 2'048;
        constexpr int frames = 16;
        {
            auto fileOutput = table.createOutputStream();
            passed = fileOutput != nullptr;
            if (passed) {
                std::unique_ptr<juce::OutputStream> output {
                    std::move(fileOutput) };
                juce::WavAudioFormat format;
                auto writer = format.createWriterFor(
                    output, juce::AudioFormatWriterOptions()
                                .withSampleRate(48'000.0)
                                .withNumChannels(1)
                                .withBitsPerSample(24));
                passed = writer != nullptr;
                if (passed) {
                    juce::AudioBuffer<float> buffer(1, cycle * frames);
                    for (int frame = 0; frame < frames; ++frame)
                        for (int index = 0; index < cycle; ++index) {
                            const auto phase =
                                2.0 * juce::MathConstants<double>::pi
                                * index / cycle;
                            buffer.setSample(
                                0, frame * cycle + index,
                                static_cast<float>(std::sin(
                                    phase * (frame + 1))));
                        }
                    passed = writer->writeFromAudioSampleBuffer(
                        buffer, 0, buffer.getNumSamples());
                }
            }
        }
        if (passed) {
            // Shaped distinctively first. Undoing back to the built-in table
            // would prove nothing: every older state holds that one too.
            auto marked = nodeGraph_.nodeWavetable("wavetable");
            marked.harmonics[6] = {};
            marked.harmonics[6][11] = 1.0f;
            passed = nodeGraph_.drawWavetableForTest("wavetable", marked);
            const auto before = nodeGraph_.nodeWavetable("wavetable");
            passed = passed && before == marked
                     && nodeGraph_.importWavetableFileForTest("wavetable",
                                                              table)
                            .wasOk();
            const auto imported = nodeGraph_.nodeWavetable("wavetable");
            // Frame n of the file is the nth harmonic alone, so the first
            // frame read must be the fundamental and the last the sixteenth.
            const auto loudest = [](const auto& row) {
                return static_cast<int>(std::distance(
                    row.begin(),
                    std::max_element(row.begin(), row.end())));
            };
            passed = passed && !(imported == before)
                     && loudest(imported.harmonics.front()) == 0
                     && loudest(imported.harmonics.back()) == 15;
            // And it is one edit: undoing puts the built-in table back.
            nodeGraph_.undoForTest();
            passed = passed
                     && nodeGraph_.nodeWavetable("wavetable") == before;
            if (!passed)
                juce::Logger::writeToLog(
                    "master export self test: importing a wavetable file did "
                    "not land on the card");
        }
    }

    if (passed) {
        // The instrument has to survive the file. Everything above was built
        // in memory; the person opening a project only ever sees what comes
        // back from disk — and for a while what came back was the factory
        // passthrough, because the state save wrote the editor's stale
        // content over the loaded graph before the editor had caught up.
        const auto midiTracksBefore = audio_.midiTracks();
        const auto savedId = midiTracksBefore.empty()
            ? std::uint64_t {} : midiTracksBefore.front().id;
        const auto project = tempRoot.getChildFile("instrument.cydaw");
        saveProject(project);
        passed = project.existsAsFile() && savedId != 0;
        if (passed) {
            loadProject(project);
            const auto reloaded = audio_.midiTracks();
            passed = !reloaded.empty();
            if (passed) {
                const auto id = reloaded.front().id;
                const auto instrument = audio_.midiTrackInstrumentGraph(id);
                const auto chain = audio_.midiTrackEffectGraph(id);
                restoreNodeGraphForMidiTrack(id);
                passed = instrument.enabled && chain.nodes.size() > 2
                         && nodeGraph_.nodeCount() > 2;
                if (!passed)
                    juce::Logger::writeToLog(
                        "master export self test: the instrument did not "
                        "survive the project file (instrument="
                        + juce::String(instrument.enabled ? "on" : "off")
                        + " chainNodes=" + juce::String(
                            static_cast<int>(chain.nodes.size()))
                        + " editorNodes=" + juce::String(nodeGraph_.nodeCount())
                        + ")");
            }
        }
    }

    if (passed) {
        // The same round trip with no audio track at all. A project of MIDI
        // tracks alone takes a different path on load, and that path is the
        // one that used to put a passthrough over the instrument.
        audio_.clearTracks();
        clearTrackLanes();
        selectedTrack_.reset();
        trackNodeGraphStates_.clear();
        const auto midiOnly = audio_.midiTracks();
        passed = !midiOnly.empty();
        if (passed) {
            const auto project = tempRoot.getChildFile("midi-only.cydaw");
            saveProject(project);
            passed = project.existsAsFile();
            if (passed) {
                loadProject(project);
                const auto reloaded = audio_.midiTracks();
                passed = !reloaded.empty()
                         && audio_.midiTrackInstrumentGraph(reloaded.front().id)
                                .enabled
                         && audio_.midiTrackEffectGraph(reloaded.front().id)
                                    .nodes.size() > 2;
                if (!passed)
                    juce::Logger::writeToLog(
                        "master export self test: a MIDI-only project lost "
                        "its instrument on reload");
            }
        }
    }

    audio_.cancelMasterExport();
    audio_.clearTracks();
    clearTrackLanes();
    selectedTrack_.reset();
    trackNodeGraphStates_.clear();
    const auto cleaned = deleteRecursivelyWithRetry(tempRoot);
    return passed && cleaned;
}

bool WorkspaceComponent::runProjectRecoverySelfTest()
{
    const auto tempRoot = juce::File::getSpecialLocation(
                              juce::File::tempDirectory)
                              .getNonexistentChildFile(
                                  "cydaw-project-recovery-self-test", {}, true);
    auto passed = tempRoot.createDirectory().wasOk();
    const auto toneFile = tempRoot.getChildFile("recovery-tone.wav");
    if (passed) {
        auto output = toneFile.createOutputStream();
        std::unique_ptr<juce::OutputStream> ownedOutput { std::move(output) };
        juce::WavAudioFormat format;
        auto writer = ownedOutput == nullptr
                          ? nullptr
                          : format.createWriterFor(
                                ownedOutput,
                                juce::AudioFormatWriterOptions()
                                    .withSampleRate(48'000.0)
                                    .withNumChannels(2)
                                    .withBitsPerSample(24));
        if (writer == nullptr) {
            passed = false;
        } else {
            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            buffer.applyGain(0.1f);
            passed = writer->writeFromAudioSampleBuffer(
                buffer, 0, buffer.getNumSamples());
        }
    }

    const auto previousRecoveryFile = projectRecoveryFile_;
    const auto previousRecoveryEnabled = recoveryEnabled_;
    projectRecoveryFile_ = tempRoot.getChildFile(
        "last-session.cydaw-recovery");
    recoveryEnabled_ = true;
    recoveryPromptShown_ = true;
    currentProjectFile_ = {};
    projectDirty_ = false;
    audio_.clearTracks();
    clearTrackLanes();
    if (passed)
        loadAudioFile(toneFile);
    const auto midiTracks = audio_.midiTracks();
    if (passed && !midiTracks.empty() && !midiTracks.front().clips.empty())
        (void) audio_.addMidiNote(midiTracks.front().id,
                                  midiTracks.front().clips.front().id,
                                  64, 1.0, 0.5, 0.75f);
    else
        passed = false;
    // The autosave leaves its point and note arrays to the writer thread.
    // What that thread fills in has to be what a save would have written.
    if (passed) {
        DeferredDocumentWork deferred;
        auto lazy = createProjectDocument(projectRecoveryFile_, false, true,
                                          &deferred);
        deferred.apply();
        const auto eager = createProjectDocument(projectRecoveryFile_, false,
                                                 true);
        passed = juce::JSON::toString(lazy) == juce::JSON::toString(eager);
        if (!passed)
            juce::Logger::writeToLog(
                "Project recovery self test: the deferred document differs "
                "from the inline one");
    }
    markProjectDirty();
    if (passed) {
        scheduleProjectAutosave();
        for (int waited = 0;
             waited < 5'000 && !projectRecoveryFile_.existsAsFile();
             waited += 10)
            juce::Thread::sleep(10);
        passed = projectRecoveryFile_.existsAsFile();
        (void) projectAutosavePool_.removeAllJobs(true, 5'000);
        projectAutosaveInProgress_.store(false,
                                         std::memory_order_release);
    }

    juce::var recoveryDocument;
    if (passed)
        passed = ProjectRecovery::readDocument(
                     projectRecoveryFile_, recoveryDocument).wasOk()
                 && ProjectRecovery::isRecoveryDocument(recoveryDocument);
    if (passed) {
        const auto* recovery = recoveryDocument.getDynamicObject();
        const auto* tracks = recovery != nullptr
                                 ? recovery->getProperty("tracks").getArray()
                                 : nullptr;
        const auto* firstTrack = tracks != nullptr && !tracks->isEmpty()
                                     ? tracks->getFirst().getDynamicObject()
                                     : nullptr;
        const auto savedAudio = firstTrack != nullptr
                                    ? firstTrack->getProperty("audioFile")
                                          .toString()
                                    : juce::String {};
        passed = savedAudio.isNotEmpty()
                 && juce::File::isAbsolutePath(savedAudio)
                 && ProjectRecovery::originalProject(recoveryDocument)
                        == juce::File {};
    }

    if (passed)
        loadProject(projectRecoveryFile_);
    auto recoveredNoteCount = std::size_t {};
    for (const auto& track : audio_.midiTracks())
        for (const auto& clip : track.clips)
            recoveredNoteCount += clip.notes.size();
    passed = passed && audio_.trackCount() == 1
             && recoveredNoteCount == 1 && projectDirty_
             && currentProjectFile_ == juce::File {};

    const auto confirmedProject = tempRoot.getChildFile("confirmed.cydaw");
    if (passed)
        saveProject(confirmedProject);
    juce::var confirmedDocument;
    if (passed)
        passed = ProjectRecovery::readDocument(
                     confirmedProject, confirmedDocument).wasOk()
                 && !ProjectRecovery::isRecoveryDocument(confirmedDocument)
                 && !projectRecoveryFile_.existsAsFile()
                 && !projectDirty_ && currentProjectFile_ == confirmedProject;
    if (passed) {
        const auto* confirmed = confirmedDocument.getDynamicObject();
        const auto* tracks = confirmed != nullptr
                                 ? confirmed->getProperty("tracks").getArray()
                                 : nullptr;
        const auto* firstTrack = tracks != nullptr && !tracks->isEmpty()
                                     ? tracks->getFirst().getDynamicObject()
                                     : nullptr;
        const auto savedAudio = firstTrack != nullptr
                                    ? firstTrack->getProperty("audioFile")
                                          .toString()
                                    : juce::String {};
        passed = savedAudio.isNotEmpty()
                 && !juce::File::isAbsolutePath(savedAudio);
    }

    audio_.clearTracks();
    clearTrackLanes();
    currentProjectFile_ = {};
    projectDirty_ = false;
    recoveryEnabled_ = previousRecoveryEnabled;
    projectRecoveryFile_ = previousRecoveryFile;
    const auto cleaned = deleteRecursivelyWithRetry(tempRoot);
    return passed && cleaned;
}

bool WorkspaceComponent::runAudioRecordingSelfTest()
{
    const auto tempRoot = juce::File::getSpecialLocation(
                              juce::File::tempDirectory)
                              .getNonexistentChildFile(
                                  "cydaw-audio-recording-self-test", {}, true);
    auto passed = tempRoot.createDirectory().wasOk();
    const auto writeTake = [](const juce::File& file,
                              const float level) {
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::OutputStream> output { std::move(stream) };
        juce::WavAudioFormat format;
        auto writer = output == nullptr
                          ? nullptr
                          : format.createWriterFor(
                                output,
                                juce::AudioFormatWriterOptions()
                                    .withSampleRate(48'000.0)
                                    .withNumChannels(2)
                                    .withBitsPerSample(24));
        if (writer == nullptr)
            return false;
        juce::AudioBuffer<float> buffer(2, 24'000);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            const auto value = level * static_cast<float>(
                std::sin(juce::MathConstants<double>::twoPi * 220.0
                         * static_cast<double>(sample) / 48'000.0));
            buffer.setSample(0, sample, value);
            buffer.setSample(1, sample, value);
        }
        return writer->writeFromAudioSampleBuffer(
            buffer, 0, buffer.getNumSamples());
    };

    const auto baseFile = tempRoot.getChildFile("base.wav");
    const auto firstTakeFile = tempRoot.getChildFile("take-one.wav");
    const auto secondTakeFile = tempRoot.getChildFile("take-two.wav");
    passed = passed && writeTake(baseFile, 0.1f)
             && writeTake(firstTakeFile, 0.2f)
             && writeTake(secondTakeFile, 0.3f);

    audio_.clearTracks();
    clearTrackLanes();
    trackNodeGraphStates_.clear();
    selectedTrack_.reset();
    selectedAudioClipId_ = 0;
    selectedAudioClipIds_.clear();
    currentProjectFile_ = {};
    projectDirty_ = false;

    std::size_t targetTrack {};
    if (passed) {
        passed = audio_.addAudioTrack(baseFile, &targetTrack).wasOk();
        if (passed) {
            addTrackLane(targetTrack);
            audio_.setTrackArmed(targetTrack, true);
            refreshAudioTrackArmStates();
        }
    }

    AudioTrackController::RecordedTakeResult firstTake;
    if (passed) {
        passed = audio_.commitRecordedTake(
                     firstTakeFile, audio_.armedAudioTrack(), 0.375,
                     firstTake).wasOk()
                 && !firstTake.createdTrack
                 && firstTake.trackIndex == 0
                 && std::abs(firstTake.startSeconds - 0.375) < 0.000001
                 && audio_.audioClips(0).size() == 2
                 && audio_.canUndoArrangementEdit()
                 && audio_.undoArrangementEdit()
                 && audio_.audioClips(0).size() == 1
                 && audio_.redoArrangementEdit()
                 && audio_.audioClips(0).size() == 2;
    }

    AudioTrackController::RecordedTakeResult secondTake;
    if (passed) {
        audio_.setTrackArmed(0, false);
        passed = audio_.commitRecordedTake(
                     secondTakeFile, std::nullopt, 1.25,
                     secondTake).wasOk()
                 && secondTake.createdTrack
                 && secondTake.trackIndex == 1
                 && audio_.armedAudioTrack() == 1;
        if (passed)
            addTrackLane(secondTake.trackIndex);
    }

    AudioTrackController::RecordedTakeResult alternativeTake;
    if (passed) {
        passed = audio_.commitRecordedTake(
                     firstTakeFile, secondTake.trackIndex, 1.25,
                     alternativeTake, secondTake.clipId).wasOk()
                 && !alternativeTake.createdTrack
                 && alternativeTake.clipId == secondTake.clipId;
        const auto active = audio_.audioClip(
            secondTake.trackIndex, secondTake.clipId);
        passed = passed && active.has_value()
                 && active->takes.size() == 2
                 && active->activeTakeIndex == 1
                 && active->sourceFile == firstTakeFile
                 && audio_.undoArrangementEdit();
        const auto undone = audio_.audioClip(
            secondTake.trackIndex, secondTake.clipId);
        passed = passed && undone.has_value()
                 && undone->takes.size() == 1
                 && undone->sourceFile == secondTakeFile
                 && audio_.redoArrangementEdit();
        const auto redone = audio_.audioClip(
            secondTake.trackIndex, secondTake.clipId);
        passed = passed && redone.has_value()
                 && redone->takes.size() == 2
                 && redone->activeTakeIndex == 1
                 && redone->sourceFile == firstTakeFile;
    }

    const auto project = tempRoot.getChildFile("recording-session.cydaw");
    if (passed) {
        selectTrack(secondTake.trackIndex);
        selectAudioClip(secondTake.trackIndex, secondTake.clipId);
        markProjectDirty();
        saveProject(project);
        const auto parsed = juce::JSON::parse(project);
        const auto* root = parsed.getDynamicObject();
        const auto* tracks = root != nullptr
                                 ? root->getProperty("tracks").getArray()
                                 : nullptr;
        const auto* first = tracks != nullptr && tracks->size() == 2
                                ? tracks->getReference(0).getDynamicObject()
                                : nullptr;
        const auto* second = tracks != nullptr && tracks->size() == 2
                                 ? tracks->getReference(1).getDynamicObject()
                                 : nullptr;
        const auto* secondClips = second != nullptr
                                      ? second->getProperty("audioClips")
                                            .getArray()
                                      : nullptr;
        const auto* savedSecondClip = secondClips != nullptr
                                           && secondClips->size() == 1
                                       ? secondClips->getReference(0)
                                             .getDynamicObject()
                                       : nullptr;
        const auto* savedTakes = savedSecondClip != nullptr
                                     ? savedSecondClip->getProperty("takes")
                                           .getArray()
                                     : nullptr;
        passed = root != nullptr
                 && static_cast<int>(root->getProperty("version")) == 36
                 && tracks != nullptr && tracks->size() == 2
                 && first != nullptr
                 && !static_cast<bool>(first->getProperty("armed"))
                 && second != nullptr
                 && static_cast<bool>(second->getProperty("armed"))
                 && secondClips != nullptr && secondClips->size() == 1
                 && savedTakes != nullptr && savedTakes->size() == 2
                 && savedSecondClip != nullptr
                 && static_cast<int>(
                        savedSecondClip->getProperty("activeTake")) == 1
                 && std::abs(static_cast<double>(
                        secondClips->getReference(0)
                            .getProperty("start", 0.0)) - 1.25) < 0.000001;
    }

    if (passed) {
        loadProject(project);
        const auto loadedSecondClips = audio_.audioClips(1);
        passed = audio_.trackCount() == 2
                 && audio_.armedAudioTrack() == 1
                 && loadedSecondClips.size() == 1
                 && loadedSecondClips.front().takes.size() == 2
                 && loadedSecondClips.front().activeTakeIndex == 1
                 && std::abs(loadedSecondClips.front().startSeconds - 1.25)
                        < 0.000001
                 && loadedSecondClips.front().sourceFile == firstTakeFile;
    }

    audio_.clearTracks();
    clearTrackLanes();
    trackNodeGraphStates_.clear();
    selectedTrack_.reset();
    selectedAudioClipId_ = 0;
    selectedAudioClipIds_.clear();
    currentProjectFile_ = {};
    projectDirty_ = false;
    const auto cleaned = deleteRecursivelyWithRetry(tempRoot);
    return passed && cleaned;
}

juce::String WorkspaceComponent::buildPerformanceProject(
    const juce::File& file)
{
    // The bench measures surface by surface; the probe watches the whole
    // while it plays. It had only the demo — one MIDI track, fourteen cards
    // — to watch. This is the bench's session, written down.
    const auto folder = file.getParentDirectory();
    if (!folder.createDirectory().wasOk())
        return "failed: " + folder.getFullPathName();
    const auto tone = folder.getChildFile("perf-tone.wav");
    {
        auto fileOutput = tone.createOutputStream();
        if (fileOutput == nullptr || !fileOutput->setPosition(0)
            || fileOutput->truncate().failed())
            return "failed: the tone could not be written";
        std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(
            output, juce::AudioFormatWriterOptions()
                        .withSampleRate(48'000.0)
                        .withNumChannels(2)
                        .withBitsPerSample(24));
        if (writer == nullptr)
            return "failed: no writer for the tone";
        juce::AudioBuffer<float> buffer(2, 48'000 * 8);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            const auto value = 0.2f * std::sin(
                static_cast<float>(juce::MathConstants<double>::twoPi * 220.0
                                   * sample / 48'000.0));
            buffer.setSample(0, sample, value);
            buffer.setSample(1, sample, value);
        }
        if (!writer->writeFromAudioSampleBuffer(buffer, 0,
                                                buffer.getNumSamples()))
            return "failed: the tone was not written";
    }

    constexpr auto audioTrackCount = 8;
    constexpr auto automationPointCount = 2'000;
    constexpr auto midiNoteCount = 400;
    constexpr auto graphCards = 160;

    setEditorMode(1);
    for (auto track = 0; track < audioTrackCount; ++track)
        loadAudioFile(tone);
    if (audio_.trackCount() != static_cast<std::size_t>(audioTrackCount))
        return "failed: " + juce::String(static_cast<int>(audio_.trackCount()))
               + " audio tracks";
    auto automationPoints = 0;
    for (std::size_t track = 0; track < audio_.trackCount(); ++track) {
        constexpr auto perTrack = automationPointCount / audioTrackCount;
        for (auto point = 0; point < perTrack; ++point) {
            const auto position = static_cast<double>(point) / perTrack * 8.0;
            if (audio_.addAutomationPoint(
                    track, position,
                    -12.0f + 12.0f * std::sin(static_cast<float>(position)))
                != 0)
                ++automationPoints;
        }
        // Every ribbon open: the arrangement at its heaviest.
        if (track < trackAutomationVisible_.size()
            && track < trackLaneHeights_.size()) {
            trackAutomationVisible_[track] = 1;
            trackLaneHeights_[track] = std::max(172, trackLaneHeights_[track]);
        }
    }
    layoutTrackLanes();

    // The demo's instrument and chain, with the bench's four hundred notes.
    setEditorMode(4);
    if (audio_.midiTracks().empty())
        addEmptyMidiTrack();
    const auto midiTracks = audio_.midiTracks();
    if (midiTracks.empty())
        return "failed: no MIDI track";
    const auto trackId = midiTracks.front().id;
    selectedTrack_.reset();
    selectedMidiTrackId_ = trackId;
    restoreNodeGraphForMidiTrack(trackId);
    setEditorMode(2);
    if (const auto applied = nodeGraph_.insertFactoryDevice("table-synth");
        applied.failed())
        return "failed: table-synth: " + applied.getErrorMessage();
    for (auto band = 0; band < 4; ++band) {
        const juce::String source = band == 0 ? "voice-filter" : "eq-band";
        if (nodeGraph_.insertNodeOnConnection(source, "master", "eq-band")
                .failed())
            return "failed: band " + juce::String(band + 1);
    }
    if (nodeGraph_.insertNodeOnConnection("eq-band", "master", "drive").wasOk()
        && nodeGraph_.insertNode("lfo-core").wasOk())
        (void) nodeGraph_.modulateParameterForTest("lfo-core", "drive",
                                                   "drive.db");
    if (nodeGraph_.getWidth() <= 0 || nodeGraph_.getHeight() <= 0)
        nodeGraph_.setBounds(0, 0, 1'300, 780);
    nodeGraph_.arrangeNodes();
    nodeGraph_.frameGraph();
    setMidiTrackNodeGraphState(trackId, nodeGraph_.serialise());
    const auto chainCards = static_cast<int>(
        audio_.midiTrackEffectGraph(trackId).nodes.size());
    const auto clipId = audio_.addMidiClip(trackId, "Perf");
    if (clipId == 0)
        return "failed: no MIDI clip";
    auto notes = 0;
    for (auto note = 0; note < midiNoteCount; ++note)
        if (audio_.addMidiNote(trackId, clipId, 36 + (note % 48),
                               static_cast<double>(note) * 0.25, 0.2, 0.8f)
            != 0)
            ++notes;

    // A hundred and sixty cards on the first audio track: what the bench
    // measures at n=160, on a graph the engine actually runs.
    setEditorMode(1);
    selectTrack(0);
    for (const auto& key : { "gain", "drive", "lowpass", "eq", "compressor",
                             "delay", "reverb", "stereo-width" })
        for (auto copy = 0; copy < 4; ++copy)
            (void) nodeGraph_.insertNode(key);
    while (nodeGraph_.nodeCount() < graphCards
           && nodeGraph_.insertNode("gain").wasOk()) {
    }
    nodeGraph_.spreadNodesForTest();
    nodeGraph_.frameGraph();
    publishNativeDspGraph();
    const auto cards = nodeGraph_.nodeCount();

    saveProject(file);
    if (!file.existsAsFile())
        return "failed: the project was not written";
    return "ok tracks=" + juce::String(static_cast<int>(audio_.trackCount()))
           + " automation=" + juce::String(automationPoints)
           + " notes=" + juce::String(notes)
           + " chainCards=" + juce::String(chainCards)
           + " graphCards=" + juce::String(cards)
           + " file=" + file.getFullPathName();
}

juce::String WorkspaceComponent::buildInstrumentFilterDemo(
    const juce::File& file)
{
    // An instrument, then a chain behind it, on one MIDI track. The
    // polyphonic renderer plays the voice per note and the graph's own
    // low-pass is part of that voice; everything the graph puts after it is
    // compiled separately and runs on the summed output.
    const auto folder = file.getParentDirectory();
    if (!folder.createDirectory().wasOk())
        return "failed: " + folder.getFullPathName();
    // The audio track this demo used to need is gone.
    folder.getChildFile("eq-test-tone.wav").deleteFile();

    setEditorMode(4);
    // The application starts with one MIDI track already. Adding another and
    // filling that one left the file with an empty first track, which is the
    // one a project opens on: the person opening the demo saw an empty clip
    // and heard nothing, while everything sat on track two.
    if (audio_.midiTracks().empty())
        addEmptyMidiTrack();
    const auto tracks = audio_.midiTracks();
    if (tracks.empty())
        return "failed: no MIDI track";
    const auto trackId = tracks.front().id;
    selectedMidiTrackId_ = trackId;
    setEditorMode(2);

    if (const auto applied = nodeGraph_.insertFactoryDevice("table-synth");
        applied.failed())
        return "failed: table-synth: " + applied.getErrorMessage();
    // The splice looks for the last node of each type, so each band lands
    // after the one added before it and the chain grows in signal order.
    for (auto band = 0; band < 4; ++band) {
        const juce::String source = band == 0 ? "voice-filter" : "eq-band";
        if (const auto spliced = nodeGraph_.insertNodeOnConnection(
                source, "master", "eq-band");
            spliced.failed())
            return "failed: band " + juce::String(band + 1) + ": "
                   + spliced.getErrorMessage();
    }

    // The serialised tree shares its node objects with the graph's own cache,
    // so tuning the bands in place would edit that cache behind its back.
    struct Band { double frequency; double gain; double q; };
    static constexpr std::array<Band, 4> bands { {
        { 110.0, -3.5, 0.9 },
        { 420.0, -2.5, 1.6 },
        { 2'800.0, 3.0, 1.1 },
        { 9'500.0, 2.5, 0.8 } } };
    auto state = cloneJsonValue(nodeGraph_.serialise());
    auto tuned = std::size_t {};
    if (auto* root = state.getDynamicObject())
        if (auto* nodes = root->getProperty("nodes").getArray())
            for (auto& node : *nodes) {
                auto* body = node.getDynamicObject();
                if (body == nullptr || tuned >= bands.size()
                    || body->getProperty("type").toString() != "eq-band")
                    continue;
                auto* parameters
                    = body->getProperty("parameters").getDynamicObject();
                if (parameters == nullptr)
                    continue;
                const auto& band = bands[tuned++];
                parameters->setProperty("bandFrequencyHz", band.frequency);
                parameters->setProperty("bandGainDb", band.gain);
                parameters->setProperty("bandQ", band.q);
            }
    if (tuned != bands.size())
        return "failed: tuned " + juce::String(static_cast<int>(tuned))
               + " bands";
    nodeGraph_.restore(state);
    // Spliced onto one cable, the four bands landed on the same midpoint and
    // over the synth's cards. Laid out as the Arrange button would, then
    // framed, so the saved view opens on the cards and not beside them.
    // A drive after the bands, and an LFO sweeping its amount: the project
    // carries a live modulation on a knob, so the ring and its running dot
    // can be seen — and heard — on opening it. A band's frequency would be
    // modulated just as well, but it is a handle on a curve, not a knob.
    if (nodeGraph_.insertNodeOnConnection("eq-band", "master", "drive").wasOk()
        && nodeGraph_.insertNode("lfo-core").wasOk())
        (void) nodeGraph_.modulateParameterForTest("lfo-core", "drive",
                                                   "drive.db");
    // Built without a shown window, the graph has no size, and framing a
    // sizeless canvas frames nothing: the saved view looked beside the
    // cards. The bench gives it a size for the same reason.
    // Shaped like the editor's area in the DAW window, so the framed zoom
    // fits the card there too; the view itself is saved as a point of the
    // graph and lands on the card whatever the window.
    if (nodeGraph_.getWidth() <= 0 || nodeGraph_.getHeight() <= 0)
        nodeGraph_.setBounds(0, 0, 1'300, 780);
    nodeGraph_.arrangeNodes();
    nodeGraph_.frameGraph();
    // Then close in on the modulated drive: opening the project shows the
    // ring and its running dot before anything else.
    nodeGraph_.frameNode("drive");

    cydaw::engine::PolySynthGraphPlan plan;
    const auto compiled = nodeGraph_.compilePolySynthGraph(plan);
    setMidiTrackNodeGraphState(trackId, nodeGraph_.serialise());
    const auto chain = audio_.midiTrackEffectGraph(trackId);

    const auto clipId = audio_.addMidiClip(trackId, "Table Synth");
    if (clipId == 0)
        return "failed: no clip";
    // Four bars of held chords with a line above them. Long notes are the
    // point: they leave the sound running while the filter and the bands are
    // moved.
    static constexpr std::array<std::array<int, 4>, 4> chords { {
        { 45, 57, 60, 64 },
        { 41, 53, 57, 60 },
        { 48, 55, 59, 64 },
        { 43, 55, 59, 62 } } };
    static constexpr std::array<int, 8> line {
        76, 74, 72, 71, 72, 74, 76, 79 };
    auto notes = 0;
    for (std::size_t bar = 0; bar < chords.size(); ++bar)
        for (const auto note : chords[bar])
            if (audio_.addMidiNote(trackId, clipId, note,
                                   static_cast<double>(bar) * 4.0, 3.75, 0.62f)
                != 0)
                ++notes;
    for (std::size_t step = 0; step < line.size(); ++step)
        if (audio_.addMidiNote(trackId, clipId, line[step],
                               static_cast<double>(step) * 2.0, 1.75, 0.78f)
            != 0)
            ++notes;
    // The editor gave the clip-less track an empty "Clip 1" when it showed
    // it; the project keeps the clip with the notes.
    for (const auto& track : audio_.midiTracks())
        for (const auto& clip : track.clips)
            if (clip.notes.empty())
                (void) audio_.removeMidiClip(track.id, clip.id);
    selectedMidiClipId_ = clipId;

    saveProject(file);
    if (!file.existsAsFile())
        return "failed: the project was not written";
    // What comes back from the file is the only thing that matters to the
    // person who opens it. Everything above was true in memory.
    loadProject(file);
    const auto reloadedTracks = audio_.midiTracks();
    auto reloadedNotes = 0;
    auto reloadedChain = 0;
    auto reloadedInstrument = false;
    if (!reloadedTracks.empty()) {
        const auto& reloaded = reloadedTracks.front();
        for (const auto& clip : reloaded.clips)
            reloadedNotes += static_cast<int>(clip.notes.size());
        reloadedChain = static_cast<int>(
            audio_.midiTrackEffectGraph(reloaded.id).nodes.size());
        reloadedInstrument = audio_.midiTrackInstrumentGraph(reloaded.id).enabled;
        // What the file holds for that track, and what the editor shows once
        // the track's graph is put back in front of it.
        const auto document = juce::JSON::parse(file.loadFileAsString());
        auto fileNodes = -1;
        if (const auto* root = document.getDynamicObject())
            if (const auto* midi = root->getProperty("midiTracks").getArray())
                if (!midi->isEmpty())
                    if (const auto* track = midi->getFirst().getDynamicObject())
                        if (const auto* graph = track->getProperty("nodeGraph")
                                                    .getDynamicObject())
                            if (const auto* nodes
                                = graph->getProperty("nodes").getArray())
                                fileNodes = nodes->size();
        restoreNodeGraphForMidiTrack(reloaded.id);
        juce::Logger::writeToLog(
            "demo reload: fileNodes=" + juce::String(fileNodes)
            + " editorNodes=" + juce::String(nodeGraph_.nodeCount())
            + " storedIsGraph="
            + juce::String(isNodeGraphState(midiNodeGraphStates_[reloaded.id])
                               ? "yes" : "no"));
    }
    if (!reloadedInstrument || reloadedChain <= 2 || reloadedNotes != notes)
        return "failed: the project did not come back from the file as it was "
               "saved (tracks=" + juce::String(static_cast<int>(reloadedTracks.size()))
               + " notes=" + juce::String(reloadedNotes)
               + " chainNodes=" + juce::String(reloadedChain)
               + " instrument=" + (reloadedInstrument ? "on" : "OFF") + ")";
    return "ok notes=" + juce::String(notes)
           + " reload[tracks=" + juce::String(static_cast<int>(reloadedTracks.size()))
           + " notes=" + juce::String(reloadedNotes)
           + " chainNodes=" + juce::String(reloadedChain)
           + " instrument=" + (reloadedInstrument ? "on" : "OFF") + "]"
           + " bands=" + juce::String(static_cast<int>(tuned))
           + " chainNodes=" + juce::String(static_cast<int>(chain.nodes.size()))
           + " instrument=" + (compiled.wasOk()
                                   ? juce::String("compiled")
                                   : compiled.getErrorMessage())
           + " voices=" + juce::String(plan.voiceCount)
           + " cutoff=" + juce::String(plan.filterCutoffHz, 0)
           + " file=" + file.getFullPathName();
}

juce::String WorkspaceComponent::buildTemplateProject(const juce::String& name,
                                                      const juce::File& file)
{
    // Small projects ready to play, one idea each: a bass under pads, an
    // arpeggio through an echo, a slow ambience whose filter breathes, a
    // rhythm section on audio loops. Built through the same calls the
    // application makes, so the file is what the application would write.
    const auto folder = file.getParentDirectory();
    if (!folder.createDirectory().wasOk())
        return "failed: " + folder.getFullPathName();

    struct Note {
        int note;
        double startBeats;
        double lengthBeats;
        float velocity;
    };
    // A hit of a drum loop: where on the grid of sixteenths, how hard,
    // where in the stereo field, and — for a chirp — at what pitch.
    struct Hit {
        double step;
        float accent;
        float pan { 0.0f };
        double pitchHz { 0.0 };
    };
    // What comes after the template's counts in its report.
    juce::String extra;
    // The tracks the application started with, replaced by named ones: a
    // project opens on its first MIDI track, which must be the template's.
    std::vector<std::uint64_t> defaultTracks;
    for (const auto& track : audio_.midiTracks())
        defaultTracks.push_back(track.id);
    const auto addNamedMidiTrack = [this](const juce::String& trackName) {
        saveCurrentNodeGraphState();
        const auto id = audio_.addMidiTrack(trackName);
        for (const auto& track : audio_.midiTracks())
            if (track.id == id)
                for (const auto& clip : track.clips)
                    (void) audio_.removeMidiClip(id, clip.id);
        selectedMidiClipId_ = 0;
        selectedTrack_.reset();
        selectedMidiTrackId_ = id;
        (void) audio_.setLiveMidiTrack(id);
        restoreNodeGraphForMidiTrack(id);
        return id;
    };
    const auto instrument = [this](const std::uint64_t trackId,
                                   const std::vector<juce::String>& deviceKeys)
        -> juce::String {
        selectedTrack_.reset();
        selectedMidiTrackId_ = trackId;
        restoreNodeGraphForMidiTrack(trackId);
        setEditorMode(2);
        for (const auto& key : deviceKeys)
            if (const auto applied = nodeGraph_.insertFactoryDevice(key);
                applied.failed())
                return "failed: " + key + ": " + applied.getErrorMessage();
        return {};
    };
    // Built without a shown window, the graph has no size, and framing a
    // sizeless canvas frames nothing; the demo gives it one for the same
    // reason.
    const auto commit = [this](const std::uint64_t trackId) {
        if (nodeGraph_.getWidth() <= 0 || nodeGraph_.getHeight() <= 0)
            nodeGraph_.setBounds(0, 0, 1'300, 780);
        nodeGraph_.arrangeNodes();
        nodeGraph_.frameGraph();
        setMidiTrackNodeGraphState(trackId, nodeGraph_.serialise());
    };
    // Turns the first published knob whose key ends with the suffix.
    const auto tune = [this](const juce::String& suffix, const double value) {
        for (const auto& parameter : nodeGraph_.graphInterfaceState().parameters)
            if (parameter.key.endsWith(suffix))
                return nodeGraph_.setGraphInterfaceParameter(parameter.key, value);
        return false;
    };
    // Turns a knob of the last node of a type: a preset's own low-pass
    // shares its suffix with the one in the chain behind it.
    const auto tuneNode = [this](const juce::String& typeKey,
                                 const juce::String& suffix, const double value) {
        const auto state = nodeGraph_.serialise();
        const auto* graph = state.getDynamicObject();
        const auto* nodes = graph != nullptr
                                ? graph->getProperty("nodes").getArray() : nullptr;
        if (nodes == nullptr)
            return false;
        juce::String key;
        for (const auto& node : *nodes)
            if (node.getProperty("type", {}).toString() == typeKey)
                key = "node." + node.getProperty("id", {}).toString() + "." + suffix;
        return key.isNotEmpty() && nodeGraph_.setGraphInterfaceParameter(key, value);
    };
    const auto addClip = [this](const std::uint64_t trackId,
                                const juce::String& clipName,
                                const double lengthBeats,
                                const std::vector<Note>& notes) {
        const auto clipId = audio_.addMidiClip(trackId, clipName, 0.0, lengthBeats);
        auto added = 0;
        if (clipId != 0)
            for (const auto& note : notes)
                if (audio_.addMidiNote(trackId, clipId, note.note,
                                       note.startBeats, note.lengthBeats,
                                       note.velocity)
                    != 0)
                    ++added;
        return added;
    };
    const auto loopBars = [this](const int bars) {
        const auto seconds = static_cast<double>(bars) * 4.0 * 60.0
                             / std::max(20.0, audio_.tempo());
        audio_.setLoopRange(0.0, seconds);
        loopButton_.setToggleState(true, juce::dontSendNotification);
        audio_.setLooping(true);
    };
    // A voice rendered at each hit of a grid of sixteenths into a stereo
    // file: the rhythm section's loops are made here, not sampled.
    const auto writeLoop = [&folder](
        const juce::String& fileName, const double tempo, const int bars,
        const std::vector<Hit>& hits,
        const std::function<double(double, float, double)>& voice) -> juce::File {
        constexpr auto rate = 48'000.0;
        const auto stepSeconds = 60.0 / tempo / 4.0;
        const auto total = static_cast<int>(
            std::ceil(bars * 16 * stepSeconds * rate));
        juce::AudioBuffer<float> buffer(2, total);
        buffer.clear();
        for (const auto& hit : hits) {
            const auto start = static_cast<int>(
                std::llround(hit.step * stepSeconds * rate));
            const auto end = std::min(total, start + static_cast<int>(rate * 0.9));
            // Constant-power panning: a hit at the side keeps its weight.
            const auto angle = (juce::jlimit(-1.0f, 1.0f, hit.pan) + 1.0f)
                               * juce::MathConstants<float>::pi * 0.25f;
            const auto left = std::cos(angle);
            const auto right = std::sin(angle);
            for (auto index = start; index < end; ++index) {
                const auto value = static_cast<float>(
                    voice(static_cast<double>(index - start) / rate, hit.accent,
                          hit.pitchHz));
                buffer.addSample(0, index, value * left);
                buffer.addSample(1, index, value * right);
            }
        }
        const auto audio = folder.getChildFile("audio").getChildFile(fileName);
        (void) audio.getParentDirectory().createDirectory();
        auto fileOutput = audio.createOutputStream();
        if (fileOutput == nullptr || !fileOutput->setPosition(0)
            || fileOutput->truncate().failed())
            return {};
        std::unique_ptr<juce::OutputStream> output { std::move(fileOutput) };
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(
            output, juce::AudioFormatWriterOptions()
                        .withSampleRate(rate)
                        .withNumChannels(2)
                        .withBitsPerSample(24));
        if (writer == nullptr)
            return {};
        const auto written = writer->writeFromAudioSampleBuffer(buffer, 0, total);
        writer.reset();
        return written ? audio : juce::File {};
    };
    // White noise as a hash of the sample index: the same every time the
    // template is built, so two builds write the same loops.
    const auto noise = [](const double seconds) {
        auto x = static_cast<std::uint32_t>(
                     std::llround(std::max(0.0, seconds) * 48'000.0))
                 * 2'654'435'761u;
        x ^= x >> 13;
        x *= 1'274'126'177u;
        x ^= x >> 16;
        return static_cast<double>(x) / 4'294'967'295.0 * 2.0 - 1.0;
    };
    const auto nameAudioTrack = [this](const std::size_t index,
                                       const juce::String& trackName,
                                       const std::uint32_t colour) {
        if (index >= trackPresentations_.size())
            return;
        auto& presentation = trackPresentations_[index];
        presentation.name = trackName;
        presentation.colour = juce::Colour(colour);
        if (index < trackLanes_.size())
            trackLanes_[index]->setPresentation(presentation.name,
                                                presentation.colour);
    };

    auto notes = 0;
    setEditorMode(4);
    if (name == "basse-et-nappes") {
        audio_.setTempo(96.0);
        const auto bass = addNamedMidiTrack("Basse");
        if (const auto error = instrument(bass, { "synth-bass" }); error.isNotEmpty())
            return error;
        commit(bass);
        static constexpr std::array<int, 8> roots { 33, 33, 29, 29, 36, 36, 31, 31 };
        std::vector<Note> bassLine;
        for (auto bar = 0; bar < 8; ++bar) {
            const auto root = roots[static_cast<std::size_t>(bar)];
            const auto at = static_cast<double>(bar) * 4.0;
            bassLine.push_back({ root, at, 0.75, 0.86f });
            bassLine.push_back({ root, at + 1.0, 0.45, 0.62f });
            bassLine.push_back({ root + 12, at + 1.5, 0.45, 0.58f });
            bassLine.push_back({ root, at + 2.0, 0.75, 0.82f });
            bassLine.push_back({ root + 7, at + 3.0, 0.45, 0.6f });
            bassLine.push_back({ root, at + 3.5, 0.45, 0.7f });
        }
        notes += addClip(bass, "Basse", 32.0, bassLine);

        const auto pads = addNamedMidiTrack("Nappes");
        if (const auto error = instrument(pads, { "synth-pad" }); error.isNotEmpty())
            return error;
        commit(pads);
        audio_.setMidiTrackMixer(pads, -7.0f, 0.0f, false, false);
        static constexpr std::array<std::array<int, 3>, 4> chords { {
            { 57, 60, 64 }, { 53, 57, 60 }, { 60, 64, 67 }, { 55, 59, 62 } } };
        std::vector<Note> padNotes;
        for (auto bar = 0; bar < 8; bar += 2)
            for (const auto note : chords[static_cast<std::size_t>(bar / 2)])
                padNotes.push_back({ note, static_cast<double>(bar) * 4.0, 7.8, 0.5f });
        notes += addClip(pads, "Nappes", 32.0, padNotes);
        loopBars(8);
    } else if (name == "arpege") {
        audio_.setTempo(118.0);
        const auto arp = addNamedMidiTrack("Arpege");
        if (const auto error = instrument(arp, { "synth-pluck" }); error.isNotEmpty())
            return error;
        commit(arp);
        static constexpr std::array<std::array<int, 4>, 4> chords { {
            { 52, 55, 59, 64 }, { 48, 52, 55, 60 },
            { 43, 47, 50, 55 }, { 50, 54, 57, 62 } } };
        std::vector<Note> arpNotes;
        for (auto bar = 0; bar < 8; ++bar) {
            const auto& chord = chords[static_cast<std::size_t>(bar / 2 % 4)];
            for (auto step = 0; step < 16; ++step) {
                // Up over the chord and its octave, then back down.
                const auto position = step % 8;
                const auto rising = (step / 8) % 2 == 0;
                const auto slot = rising ? position : 7 - position;
                const auto note = chord[static_cast<std::size_t>(slot % 4)]
                                  + (slot >= 4 ? 12 : 0);
                arpNotes.push_back({ note,
                                     static_cast<double>(bar) * 4.0
                                         + static_cast<double>(step) * 0.25,
                                     0.22, step % 4 == 0 ? 0.78f : 0.56f });
            }
        }
        notes += addClip(arp, "Arpege", 32.0, arpNotes);
        loopBars(8);
    } else if (name == "ambiance") {
        audio_.setTempo(72.0);
        const auto air = addNamedMidiTrack("Ambiance");
        // The table synth is a factory preset, the whole graph; a second
        // preset would replace it. The room is spliced between its voice
        // filter and the output, as the demo places its bands.
        if (const auto error = instrument(air, { "table-synth" });
            error.isNotEmpty())
            return error;
        if (const auto spliced = nodeGraph_.insertNodeOnConnection(
                "voice-filter", "master", "dark-room");
            spliced.failed())
            return "failed: dark-room: " + spliced.getErrorMessage();
        // A slow LFO on the voice filter: the sound breathes while the chords
        // hold, and the ring on the cutoff knob shows it moving.
        if (nodeGraph_.insertNode("lfo-core").wasOk()
            && nodeGraph_.modulateParameterForTest("lfo-core", "voice-filter",
                                                   "voiceFilter.cutoff")) {
            (void) tune(".lfo.rate", 0.09);
            (void) tune(".voiceFilter.cutoff", 2'400.0);
            (void) tune(".voiceFilter.resonance", 1.6);
        }
        commit(air);
        static constexpr std::array<std::array<int, 5>, 4> chords { {
            { 50, 57, 60, 64, 69 }, { 46, 53, 58, 62, 65 },
            { 41, 48, 53, 57, 60 }, { 48, 55, 59, 62, 64 } } };
        std::vector<Note> airNotes;
        for (auto bar = 0; bar < 8; bar += 2)
            for (const auto note : chords[static_cast<std::size_t>(bar / 2)])
                airNotes.push_back({ note, static_cast<double>(bar) * 4.0, 7.9, 0.46f });
        notes += addClip(air, "Ambiance", 32.0, airNotes);

        const auto bells = addNamedMidiTrack("Cloches");
        if (const auto error = instrument(bells, { "bell-tines" }); error.isNotEmpty())
            return error;
        commit(bells);
        audio_.setMidiTrackMixer(bells, -9.0f, 0.2f, false, false);
        static constexpr std::array<int, 11> bellLine {
            76, 81, 79, 84, 76, 83, 79, 88, 81, 76, 84 };
        std::vector<Note> bellNotes;
        for (std::size_t index = 0; index < bellLine.size(); ++index)
            bellNotes.push_back({ bellLine[index],
                                  static_cast<double>(index) * 3.0 + 1.0, 1.5,
                                  index % 3 == 0 ? 0.62f : 0.48f });
        notes += addClip(bells, "Cloches", 32.0, bellNotes);
        loopBars(8);
    } else if (name == "rythmique") {
        audio_.setTempo(100.0);
        constexpr auto bars = 4;
        std::vector<Hit> kicks;
        std::vector<Hit> snares;
        std::vector<Hit> hats;
        for (auto bar = 0; bar < bars; ++bar) {
            const auto at = static_cast<double>(bar * 16);
            kicks.push_back({ at, 1.0f });
            kicks.push_back({ at + 8, 0.9f });
            if (bar % 2 == 1)
                kicks.push_back({ at + 10, 0.7f });
            snares.push_back({ at + 4, 0.9f });
            snares.push_back({ at + 12, 1.0f });
            for (auto step = 0; step < 16; step += 2)
                hats.push_back({ at + step,
                                 step == 14 ? 0.95f : step % 4 == 0 ? 0.7f : 0.5f });
        }
        const auto kickVoice = [](const double t, const float accent, double) {
            constexpr auto tau = 0.028;
            const auto phase = juce::MathConstants<double>::twoPi
                * (46.0 * t + 120.0 * tau * (1.0 - std::exp(-t / tau)));
            const auto envelope = std::exp(-t / 0.28) * std::min(1.0, t / 0.003);
            return 0.85 * accent * envelope * std::sin(phase);
        };
        const auto snareVoice = [noise](const double t, const float accent, double) {
            const auto body = std::sin(juce::MathConstants<double>::twoPi * 190.0 * t)
                              * std::exp(-t / 0.07) * 0.5;
            const auto rattle = noise(t) * std::exp(-t / 0.14) * 0.42;
            return accent * (body + rattle);
        };
        const auto hatVoice = [noise](const double t, const float accent, double) {
            const auto bright = (noise(t) - noise(t - 1.0 / 48'000.0)) * 0.5;
            const auto decay = accent > 0.85f ? 0.22 : 0.045;
            return 0.5 * accent * bright * std::exp(-t / decay);
        };
        const auto kickFile = writeLoop("kick-loop.wav", 100.0, bars, kicks, kickVoice);
        const auto snareFile = writeLoop("snare-loop.wav", 100.0, bars, snares, snareVoice);
        const auto hatFile = writeLoop("hats-loop.wav", 100.0, bars, hats, hatVoice);
        if (kickFile == juce::File {} || snareFile == juce::File {}
            || hatFile == juce::File {})
            return "failed: the drum loops could not be written";
        setEditorMode(1);
        loadAudioFile(kickFile);
        loadAudioFile(snareFile);
        loadAudioFile(hatFile);
        if (audio_.trackCount() != 3)
            return "failed: " + juce::String(static_cast<int>(audio_.trackCount()))
                   + " audio tracks";
        nameAudioTrack(0, "Kick", 0xffff7373);
        nameAudioTrack(1, "Snare", 0xffffd15a);
        nameAudioTrack(2, "Hats", 0xff53d0c2);
        // Glue on the kick, a lift on the hats over the four bars.
        selectTrack(0);
        if (nodeGraph_.insertFactoryDevice("drum-bus-glue").wasOk()) {
            saveCurrentNodeGraphState();
            publishNativeDspGraph();
        }
        const auto barSeconds = 4.0 * 60.0 / 100.0;
        for (auto bar = 0; bar <= bars; ++bar)
            (void) audio_.addAutomationPoint(
                2, static_cast<double>(bar) * barSeconds,
                bar == 0 || bar == bars ? -9.0f : bar == 2 ? -2.0f : -5.0f);
        if (2 < trackAutomationVisible_.size())
            trackAutomationVisible_[2] = 1;
        setEditorMode(4);
        const auto bass = addNamedMidiTrack("Basse");
        if (const auto error = instrument(bass, { "synth-bass" }); error.isNotEmpty())
            return error;
        commit(bass);
        std::vector<Note> bassLine;
        static constexpr std::array<int, 4> roots { 33, 33, 36, 31 };
        for (auto bar = 0; bar < bars; ++bar) {
            const auto root = roots[static_cast<std::size_t>(bar)];
            const auto at = static_cast<double>(bar) * 4.0;
            bassLine.push_back({ root, at, 0.7, 0.85f });
            bassLine.push_back({ root, at + 2.0, 0.45, 0.8f });
            bassLine.push_back({ root + 12, at + 2.5, 0.25, 0.6f });
            bassLine.push_back({ root + 7, at + 3.5, 0.4, 0.66f });
        }
        notes += addClip(bass, "Basse", 16.0, bassLine);
        loopBars(bars);
    } else if (name == "tipper") {
        // Half-time glitch-hop near 98 BPM, the way Tipper builds a groove:
        // a sub kick with room around it, the snare on three, hats that
        // stutter into the bar line, chirps thrown left and right, a bass
        // whose filter wobbles in eighths under a squelching lead, and a
        // dark pad far behind.
        constexpr auto tempo = 98.0;
        constexpr auto bars = 8;
        audio_.setTempo(tempo);
        std::vector<Hit> kicks;
        std::vector<Hit> snares;
        std::vector<Hit> hats;
        std::vector<Hit> glitches;
        // One character per sixteenth: x a hit, o a softer one, . nothing.
        const auto pattern = [](std::vector<Hit>& hits, const int bar,
                                const char* steps, const float hard,
                                const float soft, const float pan) {
            for (auto step = 0; step < 16 && steps[step] != '\0'; ++step)
                if (steps[step] != '.')
                    hits.push_back({ static_cast<double>(bar * 16 + step),
                                     steps[step] == 'x' ? hard : soft, pan });
        };
        const auto hash = [](const int index) {
            auto x = static_cast<std::uint32_t>(index) * 2'654'435'761u;
            x ^= x >> 15;
            x *= 2'246'822'519u;
            x ^= x >> 13;
            return x;
        };
        for (auto bar = 0; bar < bars; ++bar) {
            const auto fill = bar == bars - 1;
            const auto at = static_cast<double>(bar * 16);
            pattern(kicks, bar,
                    fill ? "x...x.o...x.x.x."
                         : bar % 2 == 0 ? "x.....o...x....." : "x.....o.....x..o",
                    1.0f, 0.62f, 0.0f);
            pattern(snares, bar,
                    bar % 2 == 0 ? "........x......." : "...o....x....o..",
                    1.0f, 0.34f, 0.0f);
            if (fill) {
                static constexpr std::array<std::pair<double, float>, 6> roll { {
                    { 12.0, 0.5f }, { 13.0, 0.6f }, { 14.0, 0.7f },
                    { 14.5, 0.8f }, { 15.0, 0.9f }, { 15.5, 1.0f } } };
                for (const auto& [step, accent] : roll)
                    snares.push_back({ at + step, accent });
            }
            pattern(hats, bar,
                    bar % 2 == 0 ? "x.oxx.o.x.ox.o.x" : "x.o.x.oxx.o.x.o.",
                    0.72f, 0.42f, 0.2f);
            if (bar % 2 == 1)
                hats.push_back({ at + 14.0, 0.95f, 0.25f });
            if (bar % 4 == 3) {
                // A ratchet into the next bar: the hits close in on the line.
                static constexpr std::array<double, 6> ratchet {
                    14.0, 14.5, 15.0, 15.25, 15.5, 15.75 };
                for (std::size_t index = 0; index < ratchet.size(); ++index)
                    hats.push_back({ at + ratchet[index],
                                     0.45f + 0.07f * static_cast<float>(index),
                                     0.2f });
            }
            static constexpr std::array<double, 6> slots {
                3.5, 6.25, 9.5, 11.0, 13.75, 15.25 };
            for (std::size_t slot = 0; slot < slots.size(); ++slot) {
                const auto h = hash(bar * 8 + static_cast<int>(slot));
                if (h % 5 == 0)
                    continue;
                const auto pitch = 320.0
                    * std::pow(2.0, static_cast<double>(h % 23) / 6.0);
                glitches.push_back({ at + slots[slot],
                                     0.5f + static_cast<float>(h % 40) / 100.0f,
                                     slot % 2 == 0 ? -0.8f : 0.8f,
                                     (h & 64u) != 0 ? -pitch : pitch });
            }
        }
        const auto kickVoice = [noise](const double t, const float accent, double) {
            constexpr auto tau = 0.022;
            const auto phase = juce::MathConstants<double>::twoPi
                * (41.0 * t + 95.0 * tau * (1.0 - std::exp(-t / tau)));
            const auto envelope = std::exp(-t / 0.30) * std::min(1.0, t / 0.002);
            const auto click = noise(t) * std::exp(-t / 0.004) * 0.25;
            return accent * (0.9 * envelope * std::sin(phase) + click);
        };
        const auto snareVoice = [noise](const double t, const float accent, double) {
            const auto body = std::sin(juce::MathConstants<double>::twoPi * 185.0 * t)
                              * std::exp(-t / 0.055) * 0.45;
            const auto crack = noise(t) * std::exp(-t / 0.10) * 0.5;
            const auto room = noise(t + 1.0) * std::exp(-t / 0.32) * 0.06;
            return accent * (body + crack + room);
        };
        const auto hatVoice = [noise](const double t, const float accent, double) {
            const auto bright = (noise(t) - noise(t - 1.0 / 48'000.0)) * 0.5;
            const auto decay = accent > 0.85f ? 0.24 : 0.032;
            return 0.5 * accent * bright * std::exp(-t / decay);
        };
        // A chirp that falls onto its pitch; a negative pitch asks for the
        // crushed one, the sine held to five levels.
        const auto glitchVoice = [](const double t, const float accent,
                                    const double pitchHz) {
            const auto pitch = std::abs(pitchHz);
            constexpr auto tau = 0.018;
            const auto phase = juce::MathConstants<double>::twoPi
                * (pitch * t + 2.5 * pitch * tau * (1.0 - std::exp(-t / tau)));
            auto value = std::sin(phase);
            if (pitchHz < 0.0)
                value = std::round(value * 2.5) / 2.5;
            const auto envelope = std::exp(-t / 0.05) * std::min(1.0, t / 0.001);
            return 0.55 * accent * envelope * value;
        };
        const auto kickFile = writeLoop("tipper-kick.wav", tempo, bars, kicks, kickVoice);
        const auto snareFile = writeLoop("tipper-snare.wav", tempo, bars, snares, snareVoice);
        const auto hatFile = writeLoop("tipper-hats.wav", tempo, bars, hats, hatVoice);
        const auto glitchFile = writeLoop("tipper-glitch.wav", tempo, bars, glitches, glitchVoice);
        if (kickFile == juce::File {} || snareFile == juce::File {}
            || hatFile == juce::File {} || glitchFile == juce::File {})
            return "failed: the drum loops could not be written";
        setEditorMode(1);
        loadAudioFile(kickFile);
        loadAudioFile(snareFile);
        loadAudioFile(hatFile);
        loadAudioFile(glitchFile);
        if (audio_.trackCount() != 4)
            return "failed: " + juce::String(static_cast<int>(audio_.trackCount()))
                   + " audio tracks";
        nameAudioTrack(0, "Kick", 0xffff7373);
        nameAudioTrack(1, "Snare", 0xffffd15a);
        nameAudioTrack(2, "Hats", 0xff53d0c2);
        nameAudioTrack(3, "Glitch", 0xffc77dff);
        const auto trackDevice = [this](const std::size_t index,
                                        const juce::String& key) {
            selectTrack(index);
            if (nodeGraph_.insertFactoryDevice(key).wasOk()) {
                saveCurrentNodeGraphState();
                publishNativeDspGraph();
            }
        };
        trackDevice(0, "gated-punch");
        trackDevice(1, "drum-bus-glue");
        trackDevice(3, "lofi-crusher");
        audio_.setTrackMixer(1, -3.0f, 0.0f, false);
        audio_.setTrackMixer(2, -8.0f, 0.15f, false);
        audio_.setTrackMixer(3, -11.0f, 0.0f, false);

        setEditorMode(4);
        // The bass: the preset's low-pass pulled down and made to ring, an
        // LFO in eighths on its cutoff — the wobble — and more drive.
        const auto bass = addNamedMidiTrack("Basse wobble");
        if (const auto error = instrument(bass, { "synth-bass" }); error.isNotEmpty())
            return error;
        (void) tuneNode("lowpass", "lowpass.cutoff", 420.0);
        (void) tuneNode("lowpass", "lowpass.resonance", 3.0);
        (void) tuneNode("drive", "drive.db", 9.0);
        if (nodeGraph_.insertNode("lfo-core").wasOk()
            && nodeGraph_.modulateParameterForTest("lfo-core", "lowpass",
                                                   "lowpass.cutoff")) {
            // Eighths at this tempo, and a sweep that stays in the bass's
            // body: the cable's own range ran from 20 Hz, half a cycle of
            // near silence.
            (void) tune(".lfo.rate", tempo / 60.0 * 2.0);
            const auto swept = tuneNode("map-range", "map.outputMinimum", 90.0)
                               && tuneNode("map-range", "map.outputMaximum", 2'600.0);
            extra += swept ? " wobble=90..2600Hz@3.27Hz" : " wobble=unset";
        }
        commit(bass);
        audio_.setMidiTrackMixer(bass, -1.5f, 0.0f, false, false);
        std::vector<Note> bassLine;
        for (auto bar = 0; bar < bars; ++bar) {
            const auto at = static_cast<double>(bar) * 4.0;
            const auto shift = bar >= 6 ? 3 : 0;
            if (bar % 2 == 0) {
                bassLine.push_back({ 28 + shift, at, 0.7, 0.9f });
                bassLine.push_back({ 28 + shift, at + 0.75, 0.2, 0.7f });
                bassLine.push_back({ 40 + shift, at + 1.0, 0.2, 0.62f });
                bassLine.push_back({ 28 + shift, at + 1.5, 0.4, 0.8f });
                bassLine.push_back({ 31 + shift, at + 2.5, 0.45, 0.78f });
                bassLine.push_back({ 28 + shift, at + 3.0, 0.4, 0.86f });
                bassLine.push_back({ 33 + shift, at + 3.5, 0.45, 0.74f });
            } else {
                bassLine.push_back({ 28 + shift, at, 0.7, 0.9f });
                bassLine.push_back({ 35 + shift, at + 1.0, 0.2, 0.66f });
                bassLine.push_back({ 28 + shift, at + 1.25, 0.2, 0.7f });
                bassLine.push_back({ 28 + shift, at + 2.0, 0.45, 0.84f });
                bassLine.push_back({ 31 + shift, at + 2.75, 0.2, 0.7f });
                bassLine.push_back({ 33 + shift, at + 3.0, 0.6, 0.86f });
                bassLine.push_back({ 38 + shift, at + 3.75, 0.25, 0.64f });
            }
        }
        notes += addClip(bass, "Basse wobble", 32.0, bassLine);

        // The lead: the table synth through an envelope-following filter —
        // every stab opens it — then a wide echo.
        const auto lead = addNamedMidiTrack("Squelch");
        if (const auto error = instrument(lead, { "table-synth" }); error.isNotEmpty())
            return error;
        if (const auto spliced = nodeGraph_.insertNodeOnConnection(
                "voice-filter", "master", "dynamic-filter");
            spliced.failed())
            return "failed: dynamic-filter: " + spliced.getErrorMessage();
        if (const auto spliced = nodeGraph_.insertNodeOnConnection(
                "dynamic-filter", "master", "wide-echo");
            spliced.failed())
            return "failed: wide-echo: " + spliced.getErrorMessage();
        (void) tune(".voiceFilter.cutoff", 3'800.0);
        (void) tune(".voiceFilter.resonance", 2.2);
        commit(lead);
        audio_.setMidiTrackMixer(lead, -7.0f, -0.15f, false, false);
        std::vector<Note> stabs;
        for (auto bar = 0; bar < bars; ++bar) {
            const auto at = static_cast<double>(bar) * 4.0;
            const auto shift = bar >= 6 ? 3 : 0;
            if (bar % 2 == 0) {
                stabs.push_back({ 52 + shift, at + 0.5, 0.2, 0.72f });
                stabs.push_back({ 55 + shift, at + 1.0, 0.2, 0.6f });
                stabs.push_back({ 52 + shift, at + 1.75, 0.2, 0.8f });
                stabs.push_back({ 59 + shift, at + 2.5, 0.25, 0.7f });
                stabs.push_back({ 55 + shift, at + 3.0, 0.2, 0.62f });
                stabs.push_back({ 62 + shift, at + 3.75, 0.2, 0.84f });
            } else {
                stabs.push_back({ 52 + shift, at + 0.25, 0.2, 0.78f });
                stabs.push_back({ 59 + shift, at + 1.5, 0.2, 0.66f });
                stabs.push_back({ 55 + shift, at + 2.0, 0.2, 0.7f });
                stabs.push_back({ 52 + shift, at + 2.25, 0.15, 0.58f });
                stabs.push_back({ 62 + shift, at + 3.5, 0.25, 0.82f });
            }
        }
        notes += addClip(lead, "Squelch", 32.0, stabs);

        const auto pad = addNamedMidiTrack("Nappe");
        if (const auto error = instrument(pad, { "glass-atmosphere" }); error.isNotEmpty())
            return error;
        commit(pad);
        audio_.setMidiTrackMixer(pad, -13.0f, 0.1f, false, false);
        static constexpr std::array<std::array<int, 4>, 4> chords { {
            { 52, 55, 59, 62 }, { 48, 52, 55, 59 },
            { 55, 59, 62, 66 }, { 50, 54, 57, 62 } } };
        std::vector<Note> padNotes;
        for (auto bar = 0; bar < bars; bar += 2)
            for (const auto note : chords[static_cast<std::size_t>(bar / 2)])
                padNotes.push_back({ note, static_cast<double>(bar) * 4.0, 7.8, 0.42f });
        notes += addClip(pad, "Nappe", 32.0, padNotes);
        loopBars(bars);
    } else {
        return "failed: unknown template '" + name + "'";
    }

    for (const auto id : defaultTracks) {
        (void) audio_.removeMidiTrack(id);
        midiNodeGraphStates_.erase(id);
    }
    // The editor gives a clip-less track an empty "Clip 1" when it shows
    // it; each track keeps the one clip with its notes, and opens on it.
    for (const auto& track : audio_.midiTracks())
        for (const auto& clip : track.clips)
            if (clip.notes.empty())
                (void) audio_.removeMidiClip(track.id, clip.id);
    const auto tracks = audio_.midiTracks();
    if (!tracks.empty()) {
        selectedTrack_.reset();
        selectedMidiTrackId_ = tracks.front().id;
        selectedMidiClipId_ = tracks.front().clips.empty()
                                  ? std::uint64_t {} : tracks.front().clips.front().id;
        (void) audio_.setLiveMidiTrack(tracks.front().id);
        restoreNodeGraphForMidiTrack(tracks.front().id);
    }
    audio_.setPosition(0.0);
    // The arrangement shows audio lanes; a project of MIDI tracks opens on
    // the clip editor, as the demo does: play runs the arrangement from
    // there, and the graph editor shows the track's instrument. The
    // launcher would leave the arrangement stopped until a scene is
    // launched.
    setEditorMode(1);
    setEditorMode(audio_.trackCount() > 0 ? 1 : 2);
    refreshMidiEditor();
    refreshTimelineRanges();
    saveProject(file);
    if (!file.existsAsFile())
        return "failed: the project was not written";

    // What comes back from the file is the only thing that matters to the
    // person who opens it.
    loadProject(file);
    auto reloadedNotes = 0;
    auto instruments = 0;
    auto clips = 0;
    for (const auto& track : audio_.midiTracks()) {
        clips += static_cast<int>(track.clips.size());
        for (const auto& clip : track.clips)
            reloadedNotes += static_cast<int>(clip.notes.size());
        if (audio_.midiTrackInstrumentGraph(track.id).enabled)
            ++instruments;
    }
    const auto midiTrackCount = static_cast<int>(audio_.midiTracks().size());
    if (reloadedNotes != notes || instruments != midiTrackCount
        || clips != midiTrackCount)
        return "failed: the template did not come back as written (notes="
               + juce::String(reloadedNotes) + "/" + juce::String(notes)
               + " instruments=" + juce::String(instruments) + "/"
               + juce::String(midiTrackCount) + " clips=" + juce::String(clips)
               + "/" + juce::String(midiTrackCount) + ")";
    return "ok template=" + name + " audioTracks="
           + juce::String(static_cast<int>(audio_.trackCount()))
           + " midiTracks=" + juce::String(midiTrackCount)
           + " notes=" + juce::String(notes) + extra
           + " file=" + file.getFullPathName();
}

} // namespace cydaw::app
