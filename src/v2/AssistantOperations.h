#pragma once

#include <cymcp/http.hpp>

namespace cydaw::v2 {

using AssistantDispatch = std::function<cymcp::Json(const std::string&, const cymcp::Json&, const cymcp::CallContext&)>;

bool assistantReadOnly(const std::string& tool, const cymcp::Json& arguments);

// Offline discovery and the chat use the controlled default. HTTP may opt into a different profile.
std::shared_ptr<cymcp::Registry> makeAssistantRegistry(AssistantDispatch = {},
                                                     std::shared_ptr<std::atomic_bool> editGate = {},
                                                     cymcp::LocalAccessProfile = cymcp::LocalAccessProfile::protected_access);

} // namespace cydaw::v2
