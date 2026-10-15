#include "StudioTypes.h"

namespace cydaw::v2 {

// ---- GraphSlot ------------------------------------------------------------------

GraphSlot::GraphSlot(const Owner owner, std::function<void(GraphSlot&, bool)> changed)
    : owner_(owner), graph_(std::make_unique<app::NodeGraphComponent>())
{
    // Never shown; the size is the one the canvas lays cards out in.
    graph_->setBounds(0, 0, 1'300, 780);
    graph_->setInstrumentIsMainInput(owner == Owner::midiTrack);
    graph_->setLayoutChangedHandler([this, changed] {
        ++revision_;
        if (!restoring_ && changed)
            changed(*this, true);
    });
    graph_->setParametersChangedHandler([this, changed] {
        ++revision_;
        if (!restoring_ && changed)
            changed(*this, false);
    });
}

GraphSlot::~GraphSlot()
{
    graph_->setLayoutChangedHandler({});
    graph_->setParametersChangedHandler({});
}

void GraphSlot::restore(const juce::var& stateWithLibrary)
{
    const juce::ScopedValueSetter<bool> guard(restoring_, true);
    graph_->restore(stateWithLibrary);
    ++revision_;
}

juce::var GraphSlot::serialise() const
{
    return graph_->serialise();
}

} // namespace cydaw::v2
