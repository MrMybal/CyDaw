#pragma once

#include <cymcp/core.hpp>

namespace cydaw::v2 {

using AssistantDispatch = std::function<cymcp::Json(const std::string&, const cymcp::Json&, const cymcp::CallContext&)>;

// With no dispatcher, the native factory catalogue remains usable offline.
std::shared_ptr<cymcp::Registry> makeAssistantRegistry(AssistantDispatch = {},
                                                     std::shared_ptr<std::atomic_bool> allowEdits = {});

} // namespace cydaw::v2
