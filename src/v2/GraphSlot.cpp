#include <cydaw/base/Thread.h>
#include <cydaw/base/File.h>
#include "StudioTypes.h"

namespace cydaw::v2 {
namespace { std::vector<GraphSlot*> liveGraphSlots; }
void GraphSlot::processPending() { const auto slots=liveGraphSlots; for(auto* slot : slots) if(std::ranges::find(liveGraphSlots,slot)!=liveGraphSlots.end()) slot->graph_->processPendingChanges(); }

// ---- GraphSlot ------------------------------------------------------------------

GraphSlot::GraphSlot(const Owner owner, std::function<void(GraphSlot&, bool)> changed)
    : owner_(owner), graph_(std::make_unique<app::GraphDocument>())
{
    liveGraphSlots.push_back(this);
    // The reference space used by the graph layout algorithm.
    graph_->setLayoutSpace({0, 0, 1'300, 780});
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
    std::erase(liveGraphSlots,this);
    graph_->setLayoutChangedHandler({});
    graph_->setParametersChangedHandler({});
}

void GraphSlot::restore(const cydaw::base::Value& stateWithLibrary)
{
    const cydaw::base::ScopedValueSetter<bool> guard(restoring_, true);
    graph_->restore(stateWithLibrary);
    ++revision_;
}

cydaw::base::Value GraphSlot::serialise() const
{
    return graph_->serialise();
}

} // namespace cydaw::v2
