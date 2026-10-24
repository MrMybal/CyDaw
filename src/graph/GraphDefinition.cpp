#include <cydaw/graph/GraphDefinition.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <ranges>
#include <set>
#include <sstream>
#include <unordered_set>

namespace cydaw::graph {
namespace {

template <typename T>
void hashValue(std::uint64_t& hash, const T& value)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
}

void hashString(std::uint64_t& hash, const std::string& value)
{
    for (const auto character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    hash ^= 0xffU;
    hash *= 1099511628211ULL;
}

void addIssue(ValidationResult& result, const ValidationCode code,
              std::string message, const std::optional<NodeId> nodeId = {},
              const std::optional<PortId> portId = {})
{
    result.issues.push_back({ ValidationSeverity::error, code,
                              std::move(message), nodeId, portId });
}

bool validKey(const std::string& value)
{
    if (value.empty())
        return false;
    return std::all_of(value.begin(), value.end(), [](const unsigned char c) {
        return std::isalnum(c) != 0 || c == '.' || c == '_' || c == '-';
    });
}

const GraphPort* findPort(const std::vector<GraphPort>& ports,
                          const PortId id)
{
    const auto iterator = std::find_if(ports.begin(), ports.end(),
                                       [id](const GraphPort& port) {
                                           return port.id == id;
                                       });
    return iterator == ports.end() ? nullptr : &*iterator;
}

const GraphNode* findNode(const GraphDefinition& definition, const NodeId id)
{
    const auto iterator = std::find_if(definition.nodes.begin(),
                                       definition.nodes.end(),
                                       [id](const GraphNode& node) {
                                           return node.id == id;
                                       });
    return iterator == definition.nodes.end() ? nullptr : &*iterator;
}

const PublishedParameter* findParameter(const GraphDefinition& definition,
                                        const ParameterId id)
{
    const auto iterator = std::find_if(definition.parameters.begin(),
                                       definition.parameters.end(),
                                       [id](const PublishedParameter& parameter) {
                                           return parameter.id == id;
                                       });
    return iterator == definition.parameters.end() ? nullptr : &*iterator;
}

bool portsAreCompatible(const GraphPort& source, const GraphPort& destination)
{
    if (source.type != destination.type || source.rate != destination.rate)
        return false;
    if (source.type != PortType::audio)
        return true;
    const auto sourceType = source.audioType != AudioPortType::unspecified
                                ? source.audioType
                                : audioPortTypeForChannels(source.channels);
    const auto destinationType = destination.audioType != AudioPortType::unspecified
                                     ? destination.audioType
                                     : audioPortTypeForChannels(destination.channels);
    return sourceType == destinationType;
}

std::uint64_t fingerprintDefinition(const GraphDefinition& definition,
                                    const bool includePresentation)
{
    std::uint64_t hash = 1469598103934665603ULL;
    const auto hashPort = [&hash, includePresentation](const GraphPort& port) {
        hashValue(hash, port.id);
        hashString(hash, port.key);
        if (includePresentation)
            hashString(hash, port.displayName);
        hashValue(hash, port.type);
        hashValue(hash, port.audioType);
        hashValue(hash, port.direction);
        hashValue(hash, port.rate);
        hashValue(hash, port.channels);
        hashValue(hash, port.optional);
        hashValue(hash, port.acceptsMultipleConnections);
        hashValue(hash, port.defaultValue);
    };
    hashString(hash, definition.reference.id);
    hashValue(hash, definition.reference.version.major);
    hashValue(hash, definition.reference.version.minor);
    hashValue(hash, definition.reference.version.patch);
    hashValue(hash, definition.schemaVersion);
    if (includePresentation) {
        hashString(hash, definition.displayName);
        hashString(hash, definition.description);
    }
    hashValue(hash, definition.role);
    if (includePresentation) {
        hashValue(hash, definition.scope);
        hashValue(hash, definition.tags.size());
        for (const auto& tag : definition.tags)
            hashString(hash, tag);
    }
    hashValue(hash, definition.ports.size());
    for (const auto& port : definition.ports) {
        hashPort(port);
    }
    hashValue(hash, definition.parameters.size());
    for (const auto& parameter : definition.parameters) {
        hashValue(hash, parameter.id);
        hashString(hash, parameter.key);
        if (includePresentation) {
            hashString(hash, parameter.displayName);
            hashString(hash, parameter.unit);
        }
        hashValue(hash, parameter.minimum);
        hashValue(hash, parameter.maximum);
        hashValue(hash, parameter.defaultValue);
        hashValue(hash, parameter.targetNodeId);
        hashString(hash, parameter.targetParameterKey);
        hashValue(hash, parameter.automatable);
        hashValue(hash, parameter.modulatable);
        if (includePresentation)
            hashValue(hash, parameter.visibleInDevice);
        hashValue(hash, parameter.exportedToHost);
    }
    hashValue(hash, definition.nodes.size());
    for (const auto& node : definition.nodes) {
        hashValue(hash, node.id);
        if (includePresentation) {
            hashString(hash, node.displayName);
            hashString(hash, node.description);
        }
        hashValue(hash, node.kind);
        hashString(hash, node.processorKey);
        if (node.graph.has_value()) {
            hashString(hash, node.graph->id);
            hashValue(hash, node.graph->version.major);
            hashValue(hash, node.graph->version.minor);
            hashValue(hash, node.graph->version.patch);
        }
        hashValue(hash, node.ports.size());
        for (const auto& port : node.ports)
            hashPort(port);
        hashValue(hash, node.parameters.size());
        for (const auto& parameter : node.parameters) {
            hashString(hash, parameter.key);
            hashValue(hash, parameter.minimum);
            hashValue(hash, parameter.maximum);
            hashValue(hash, parameter.defaultValue);
        }
        std::vector<std::pair<std::string, double>> overrides(
            node.parameterOverrides.begin(), node.parameterOverrides.end());
        std::ranges::sort(overrides, [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        hashValue(hash, overrides.size());
        for (const auto& [key, value] : overrides) {
            hashString(hash, key);
            hashValue(hash, value);
        }
        hashValue(hash, node.breaksFeedbackCycle);
    }
    hashValue(hash, definition.connections.size());
    for (const auto& connection : definition.connections) {
        hashValue(hash, connection.source.nodeId);
        hashValue(hash, connection.source.portId);
        hashValue(hash, connection.destination.nodeId);
        hashValue(hash, connection.destination.portId);
        if (connection.mode != 0) {
            hashValue(hash, connection.mode);
            hashValue(hash, connection.amount);
        }
    }
    if (includePresentation) {
        hashValue(hash, definition.interfaces.size());
        for (const auto& interface : definition.interfaces) {
        hashString(hash, interface.id);
        hashString(hash, interface.displayName);
        hashValue(hash, interface.mode);
        hashValue(hash, interface.width);
        hashValue(hash, interface.height);
        hashValue(hash, interface.controls.size());
        for (const auto& control : interface.controls) {
            hashValue(hash, control.id);
            hashValue(hash, control.kind);
            hashString(hash, control.label);
            hashValue(hash, control.parameterBinding.has_value());
            if (control.parameterBinding.has_value())
                hashValue(hash, *control.parameterBinding);
            hashValue(hash, control.secondaryParameterBinding.has_value());
            if (control.secondaryParameterBinding.has_value())
                hashValue(hash, *control.secondaryParameterBinding);
            hashValue(hash, control.tertiaryParameterBinding.has_value());
            if (control.tertiaryParameterBinding.has_value())
                hashValue(hash, *control.tertiaryParameterBinding);
            hashValue(hash, control.quaternaryParameterBinding.has_value());
            if (control.quaternaryParameterBinding.has_value())
                hashValue(hash, *control.quaternaryParameterBinding);
            hashValue(hash, control.portBinding.has_value());
            if (control.portBinding.has_value())
                hashValue(hash, *control.portBinding);
            hashValue(hash, control.x);
            hashValue(hash, control.y);
            hashValue(hash, control.width);
            hashValue(hash, control.height);
            hashValue(hash, control.zOrder);
        }
        }
    }
    return hash;
}

} // namespace

std::string SemanticVersion::toString() const
{
    return std::to_string(major) + "." + std::to_string(minor) + "."
        + std::to_string(patch);
}

std::string GraphReference::toString() const
{
    return id + "@" + version.toString();
}

bool ValidationResult::ok() const noexcept
{
    return std::none_of(issues.begin(), issues.end(),
                        [](const ValidationIssue& issue) {
                            return issue.severity == ValidationSeverity::error;
                        });
}

std::string GraphLibrary::keyFor(const GraphReference& reference) const
{
    return reference.toString();
}

ValidationResult GraphLibrary::install(GraphDefinition definition,
                                       const bool replaceExisting)
{
    const auto key = keyFor(definition.reference);
    const auto existing = definitions_.find(key);
    if (existing != definitions_.end() && replaceExisting
        && existing->second == definition)
        return compiled(key, existing->second).validation;
    auto result = validate(definition);
    if (existing != definitions_.end() && !replaceExisting)
        addIssue(result, ValidationCode::duplicateId,
                 "Graph version already installed: " + key);
    if (!result)
        return result;
    definitions_.insert_or_assign(key, std::move(definition));
    forget(key);
    return result;
}

bool GraphLibrary::holds(const GraphDefinition& definition) const
{
    const auto* installed = resolve(definition.reference);
    return installed != nullptr && *installed == definition;
}

void GraphLibrary::forget(const std::string& key)
{
    // The definition, and every definition that nests it, however deep:
    // their verdicts were reached against what is now gone.
    std::vector<std::string> pending { key };
    std::unordered_set<std::string> seen;
    while (!pending.empty()) {
        const auto current = std::move(pending.back());
        pending.pop_back();
        if (!seen.insert(current).second)
            continue;
        compiled_.erase(current);
        for (const auto& [other, definition] : definitions_) {
            if (seen.contains(other))
                continue;
            const auto nests = std::ranges::any_of(
                definition.nodes, [this, &current](const GraphNode& node) {
                    return node.graph.has_value()
                           && keyFor(*node.graph) == current;
                });
            if (nests)
                pending.push_back(other);
        }
    }
}

const GraphLibrary::Compiled& GraphLibrary::compiled(
    const std::string& key, const GraphDefinition& definition) const
{
    if (const auto found = compiled_.find(key); found != compiled_.end())
        return found->second;
    Compiled item;
    item.validation = validate(definition);
    item.fingerprint = fingerprintDefinition(definition, false);
    item.assetFingerprint = fingerprintDefinition(definition, true);

    std::unordered_map<NodeId, std::size_t> indegrees;
    std::unordered_map<NodeId, std::vector<NodeId>> edges;
    for (const auto& node : definition.nodes)
        indegrees[node.id] = 0;
    for (const auto& connection : definition.connections) {
        if (connection.source.nodeId == boundaryNodeId
            || connection.destination.nodeId == boundaryNodeId)
            continue;
        const auto* destination = findNode(definition,
                                           connection.destination.nodeId);
        if (destination != nullptr && destination->breaksFeedbackCycle)
            continue;
        edges[connection.source.nodeId].push_back(connection.destination.nodeId);
        ++indegrees[connection.destination.nodeId];
    }
    std::set<NodeId> ready;
    for (const auto& [node, degree] : indegrees)
        if (degree == 0)
            ready.insert(node);
    while (!ready.empty()) {
        const auto node = *ready.begin();
        ready.erase(ready.begin());
        item.processingOrder.push_back(node);
        for (const auto destination : edges[node])
            if (--indegrees[destination] == 0)
                ready.insert(destination);
    }
    return compiled_.emplace(key, std::move(item)).first->second;
}

ValidationResult GraphLibrary::installBatch(
    std::vector<GraphDefinition> definitions, const bool replaceExisting)
{
    GraphLibrary candidate = *this;
    ValidationResult result;
    std::unordered_set<std::string> batchKeys;
    for (auto& definition : definitions) {
        const auto key = keyFor(definition.reference);
        if (!batchKeys.insert(key).second
            || (candidate.definitions_.contains(key) && !replaceExisting)) {
            addIssue(result, ValidationCode::duplicateId,
                     "Graph version already installed: " + key);
            continue;
        }
        candidate.definitions_.insert_or_assign(key, std::move(definition));
        candidate.forget(key);
    }
    for (const auto& key : batchKeys) {
        const auto found = candidate.definitions_.find(key);
        if (found == candidate.definitions_.end())
            continue;
        const auto& validation = candidate.compiled(key, found->second)
                                     .validation;
        result.issues.insert(result.issues.end(), validation.issues.begin(),
                             validation.issues.end());
    }
    ValidationResult compileValidation;
    (void) candidate.compile(&compileValidation);
    for (const auto& issue : compileValidation.issues)
        if (issue.code == ValidationCode::dependencyCycle
            || issue.code == ValidationCode::missingDependency)
            result.issues.push_back(issue);
    if (result)
        *this = std::move(candidate);
    return result;
}

bool GraphLibrary::remove(const GraphReference& reference)
{
    const auto key = keyFor(reference);
    if (definitions_.erase(key) == 0)
        return false;
    forget(key);
    return true;
}

const GraphDefinition* GraphLibrary::resolve(
    const GraphReference& reference) const noexcept
{
    const auto iterator = definitions_.find(keyFor(reference));
    return iterator == definitions_.end() ? nullptr : &iterator->second;
}

std::vector<GraphReference> GraphLibrary::versions(const std::string& id) const
{
    std::vector<GraphReference> result;
    for (const auto& [key, definition] : definitions_) {
        (void) key;
        if (definition.reference.id == id)
            result.push_back(definition.reference);
    }
    std::ranges::sort(result, {}, &GraphReference::version);
    return result;
}

std::vector<GraphReference> GraphLibrary::all() const
{
    std::vector<GraphReference> result;
    result.reserve(definitions_.size());
    for (const auto& [key, definition] : definitions_) {
        (void) key;
        result.push_back(definition.reference);
    }
    std::ranges::sort(result, [](const GraphReference& left,
                                const GraphReference& right) {
        if (left.id != right.id)
            return left.id < right.id;
        return left.version < right.version;
    });
    return result;
}

ValidationResult GraphLibrary::validate(const GraphDefinition& definition) const
{
    ValidationResult result;
    if (!validKey(definition.reference.id) || definition.displayName.empty()
        || definition.schemaVersion == 0)
        addIssue(result, ValidationCode::invalidIdentity,
                 "A graph needs a stable id, a name and a schema version");

    std::unordered_set<PortId> portIds;
    std::unordered_set<std::string> portKeys;
    for (const auto& port : definition.ports) {
        if (port.id == 0 || !portIds.insert(port.id).second)
            addIssue(result, ValidationCode::duplicateId,
                     "Published port ids must be non-zero and unique", {}, port.id);
        if (!validKey(port.key) || !portKeys.insert(port.key).second)
            addIssue(result, ValidationCode::duplicateKey,
                     "Published port keys must be valid and unique", {}, port.id);
        if (port.channels == 0 || (port.type != PortType::audio && port.channels != 1))
            addIssue(result, ValidationCode::invalidPort,
                     "Only audio ports may expose more than one channel", {}, port.id);
        if (port.type == PortType::audio && port.rate != PortRate::audio)
            addIssue(result, ValidationCode::invalidPort,
                     "Audio ports must run at audio rate", {}, port.id);
        if (port.type == PortType::audio
            && port.audioType != AudioPortType::unspecified
            && channelCount(port.audioType) != port.channels)
            addIssue(result, ValidationCode::invalidPort,
                     "Audio port type and channel count must describe the same bus",
                     {}, port.id);
        if ((port.type == PortType::midi || port.type == PortType::trigger
             || port.type == PortType::transport)
            && port.rate != PortRate::event)
            addIssue(result, ValidationCode::invalidPort,
                     "MIDI, trigger and transport ports must run at event rate", {}, port.id);
    }

    std::unordered_set<NodeId> nodeIds;
    for (const auto& node : definition.nodes) {
        if (node.id == boundaryNodeId || !nodeIds.insert(node.id).second)
            addIssue(result, ValidationCode::duplicateId,
                     "Node ids must be non-zero and unique", node.id);
        if (node.kind == GraphNodeKind::primitive) {
            if (!validKey(node.processorKey) || node.graph.has_value())
                addIssue(result, ValidationCode::invalidIdentity,
                         "A primitive node needs a processor key and no graph reference",
                         node.id);
        } else if (!node.graph.has_value() || !node.processorKey.empty()) {
            addIssue(result, ValidationCode::invalidIdentity,
                     "A graph instance needs an exact graph reference", node.id);
        } else if (resolve(*node.graph) == nullptr) {
            addIssue(result, ValidationCode::missingDependency,
                     "Missing nested graph " + node.graph->toString(), node.id);
        }

        std::unordered_set<PortId> localPortIds;
        std::unordered_set<std::string> localPortKeys;
        for (const auto& port : node.ports) {
            if (port.id == 0 || !localPortIds.insert(port.id).second)
                addIssue(result, ValidationCode::duplicateId,
                         "Node port ids must be non-zero and unique", node.id, port.id);
            if (!validKey(port.key) || !localPortKeys.insert(port.key).second)
                addIssue(result, ValidationCode::duplicateKey,
                         "Node port keys must be valid and unique", node.id, port.id);
        }
        std::unordered_set<std::string> parameterKeys;
        for (const auto& parameter : node.parameters) {
            if (!validKey(parameter.key)
                || !parameterKeys.insert(parameter.key).second
                || !std::isfinite(parameter.minimum)
                || !std::isfinite(parameter.maximum)
                || parameter.minimum > parameter.maximum
                || parameter.defaultValue < parameter.minimum
                || parameter.defaultValue > parameter.maximum)
                addIssue(result, ValidationCode::invalidRange,
                         "Node parameters need unique keys and valid ranges", node.id);
        }
    }

    std::unordered_set<ParameterId> parameterIds;
    std::unordered_set<std::string> parameterKeys;
    for (const auto& parameter : definition.parameters) {
        if (parameter.id == 0 || !parameterIds.insert(parameter.id).second)
            addIssue(result, ValidationCode::duplicateId,
                     "Published parameter ids must be non-zero and unique");
        if (!validKey(parameter.key) || !parameterKeys.insert(parameter.key).second)
            addIssue(result, ValidationCode::duplicateKey,
                     "Published parameter keys must be valid and unique");
        if (!std::isfinite(parameter.minimum) || !std::isfinite(parameter.maximum)
            || parameter.minimum > parameter.maximum
            || parameter.defaultValue < parameter.minimum
            || parameter.defaultValue > parameter.maximum)
            addIssue(result, ValidationCode::invalidRange,
                     "Published parameter range is invalid (" + parameter.key + ")");
        const auto* node = findNode(definition, parameter.targetNodeId);
        if (node == nullptr) {
            addIssue(result, ValidationCode::invalidBinding,
                     "Published parameter target node does not exist");
            continue;
        }
        bool targetExists = false;
        if (node->kind == GraphNodeKind::primitive) {
            targetExists = std::ranges::any_of(node->parameters,
                [&parameter](const NodeParameter& candidate) {
                    return candidate.key == parameter.targetParameterKey;
                });
        } else if (node->graph.has_value()) {
            if (const auto* nested = resolve(*node->graph); nested != nullptr)
                targetExists = std::ranges::any_of(nested->parameters,
                    [&parameter](const PublishedParameter& candidate) {
                        return candidate.key == parameter.targetParameterKey;
                    });
        }
        if (!targetExists)
            addIssue(result, ValidationCode::invalidBinding,
                     "Published parameter target does not exist", node->id);
    }

    const auto endpointPort = [this, &definition](const GraphEndpoint& endpoint)
        -> const GraphPort* {
        if (endpoint.nodeId == boundaryNodeId)
            return findPort(definition.ports, endpoint.portId);
        const auto* node = findNode(definition, endpoint.nodeId);
        if (node == nullptr)
            return nullptr;
        if (node->kind == GraphNodeKind::primitive)
            return findPort(node->ports, endpoint.portId);
        if (node->graph.has_value()) {
            if (const auto* nested = resolve(*node->graph); nested != nullptr)
                return findPort(nested->ports, endpoint.portId);
        }
        return nullptr;
    };

    std::vector<GraphEndpoint> occupiedInputs;
    std::unordered_map<NodeId, std::vector<NodeId>> edges;
    for (const auto& connection : definition.connections) {
        const auto* source = endpointPort(connection.source);
        const auto* destination = endpointPort(connection.destination);
        if (source == nullptr || destination == nullptr) {
            addIssue(result, ValidationCode::invalidConnection,
                     "Connection endpoint does not exist");
            continue;
        }
        const auto sourceDirection = connection.source.nodeId == boundaryNodeId
            ? PortDirection::input : PortDirection::output;
        const auto destinationDirection = connection.destination.nodeId == boundaryNodeId
            ? PortDirection::output : PortDirection::input;
        if (source->direction != sourceDirection
            || destination->direction != destinationDirection) {
            addIssue(result, ValidationCode::invalidConnection,
                     "Connection direction is invalid");
            continue;
        }
        if (!portsAreCompatible(*source, *destination))
            addIssue(result, ValidationCode::incompatiblePorts,
                     "Connection port types, rates or channel counts differ: "
                         + std::to_string(connection.source.nodeId) + "."
                         + source->key + " -> "
                         + std::to_string(connection.destination.nodeId) + "."
                         + destination->key);
        if (!destination->acceptsMultipleConnections
            && std::ranges::find(occupiedInputs, connection.destination)
                   != occupiedInputs.end())
            addIssue(result, ValidationCode::multipleInputConnections,
                     "Input port accepts only one connection",
                     connection.destination.nodeId,
                     connection.destination.portId);
        occupiedInputs.push_back(connection.destination);
        if (connection.source.nodeId != boundaryNodeId
            && connection.destination.nodeId != boundaryNodeId) {
            const auto* destinationNode = findNode(definition,
                                                   connection.destination.nodeId);
            if (destinationNode == nullptr || !destinationNode->breaksFeedbackCycle)
                edges[connection.source.nodeId].push_back(connection.destination.nodeId);
        }
    }

    // A cycle, without recursion (a long chain of nodes overflowed the
    // stack): the nodes taken one by one once nothing feeds them; those left
    // over are on a cycle or after one.
    std::unordered_map<NodeId, std::size_t> feeding;
    for (const auto& node : definition.nodes)
        feeding.emplace(node.id, 0);
    for (const auto& [source, destinations] : edges)
        for (const auto destination : destinations)
            ++feeding[destination];
    std::vector<NodeId> ready;
    for (const auto& [node, count] : feeding)
        if (count == 0)
            ready.push_back(node);
    std::size_t taken {};
    while (!ready.empty()) {
        const auto node = ready.back();
        ready.pop_back();
        ++taken;
        if (const auto found = edges.find(node); found != edges.end())
            for (const auto destination : found->second)
                if (--feeding[destination] == 0)
                    ready.push_back(destination);
    }
    if (taken < feeding.size())
        for (const auto& node : definition.nodes)
            if (feeding[node.id] > 0) {
                addIssue(result, ValidationCode::processingCycle,
                         "Processing cycle needs an explicit delay node", node.id);
                break;
            }

    std::unordered_set<std::string> interfaceIds;
    for (const auto& interface : definition.interfaces) {
        if (!validKey(interface.id) || !interfaceIds.insert(interface.id).second
            || interface.width <= 0.0f || interface.height <= 0.0f)
            addIssue(result, ValidationCode::invalidInterface,
                     "Interface ids and dimensions must be valid");
        std::unordered_set<ControlId> controlIds;
        for (const auto& control : interface.controls) {
            if (control.id == 0 || !controlIds.insert(control.id).second
                || !std::isfinite(control.x) || !std::isfinite(control.y)
                || !std::isfinite(control.width) || !std::isfinite(control.height)
                || control.width <= 0.0f || control.height <= 0.0f)
                addIssue(result, ValidationCode::invalidInterface,
                         "Interface controls need unique ids and valid bounds");
            if (interface.mode == InterfaceLayoutMode::device
                && (control.x < 0.0f || control.y < 0.0f
                    || control.x + control.width > interface.width
                    || control.y + control.height > interface.height))
                addIssue(result, ValidationCode::invalidInterface,
                         "Device controls must stay inside the device surface");
            if (control.parameterBinding.has_value()
                && findParameter(definition, *control.parameterBinding) == nullptr)
                addIssue(result, ValidationCode::invalidBinding,
                         "Interface parameter binding does not exist");
            if (control.secondaryParameterBinding.has_value()
                && findParameter(definition,
                                 *control.secondaryParameterBinding) == nullptr)
                addIssue(result, ValidationCode::invalidBinding,
                         "Interface secondary parameter binding does not exist");
            if (control.tertiaryParameterBinding.has_value()
                && findParameter(definition,
                                 *control.tertiaryParameterBinding) == nullptr)
                addIssue(result, ValidationCode::invalidBinding,
                         "Interface tertiary parameter binding does not exist");
            if (control.quaternaryParameterBinding.has_value()
                && findParameter(definition,
                                 *control.quaternaryParameterBinding) == nullptr)
                addIssue(result, ValidationCode::invalidBinding,
                         "Interface quaternary parameter binding does not exist");
            if ((control.kind == InterfaceControlKind::xyPad
                 || control.kind == InterfaceControlKind::filterResponse
                 || control.kind == InterfaceControlKind::dynamicsResponse)
                && (!control.parameterBinding.has_value()
                    || !control.secondaryParameterBinding.has_value()
                    || control.parameterBinding
                           == control.secondaryParameterBinding))
                addIssue(result, ValidationCode::invalidBinding,
                         "Two-parameter interface controls need two distinct parameter bindings");
            if (control.kind == InterfaceControlKind::valueDisplay
                && !control.parameterBinding.has_value())
                addIssue(result, ValidationCode::invalidBinding,
                         "Value displays need one published parameter binding");
            if (control.kind == InterfaceControlKind::envelope) {
                std::unordered_set<ParameterId> bindings;
                for (const auto binding : {
                         control.parameterBinding,
                         control.secondaryParameterBinding,
                         control.tertiaryParameterBinding,
                         control.quaternaryParameterBinding })
                    if (binding.has_value())
                        bindings.insert(*binding);
                if (bindings.size() != 4)
                    addIssue(result, ValidationCode::invalidBinding,
                             "Envelope controls need four distinct parameter bindings");
            }
            if (control.portBinding.has_value()
                && findPort(definition.ports, *control.portBinding) == nullptr)
                addIssue(result, ValidationCode::invalidBinding,
                         "Interface telemetry port binding does not exist");
        }
    }

    const auto inferred = inferGraphRole(definition);
    if (definition.role != GraphRole::generic && inferred != GraphRole::generic
        && definition.role != inferred)
        addIssue(result, ValidationCode::invalidRole,
                 "Declared graph role does not match its published ports");
    return result;
}

ValidationResult GraphLibrary::validate(const GraphInstance& instance) const
{
    ValidationResult result;
    if (instance.id == 0)
        addIssue(result, ValidationCode::invalidIdentity,
                 "A graph instance needs a non-zero instance id");
    const GraphDefinition* definition = resolve(instance.definition);
    GraphLibrary embeddedLibrary = *this;
    if (instance.embeddedDefinition.has_value()) {
        if (instance.embeddedDefinition->reference != instance.definition) {
            addIssue(result, ValidationCode::invalidIdentity,
                     "Embedded graph reference differs from its instance");
        } else if (instance.embeddedDefinition->scope
                       != GraphScope::inlineTrack
                   && instance.embeddedDefinition->scope
                       != GraphScope::inlineBus
                   && instance.embeddedDefinition->scope
                       != GraphScope::project) {
            addIssue(result, ValidationCode::invalidIdentity,
                     "Only project or inline graphs may be embedded");
        } else {
            auto installResult = embeddedLibrary.install(
                *instance.embeddedDefinition, true);
            result.issues.insert(result.issues.end(),
                                 installResult.issues.begin(),
                                 installResult.issues.end());
            definition = embeddedLibrary.resolve(instance.definition);
        }
    }
    if (definition == nullptr) {
        addIssue(result, ValidationCode::missingDependency,
                 "Graph instance definition is unavailable: "
                     + instance.definition.toString());
        return result;
    }
    for (const auto& [parameterId, value] : instance.parameterOverrides) {
        const auto parameter = std::ranges::find(
            definition->parameters, parameterId, &PublishedParameter::id);
        if (parameter == definition->parameters.end() || !std::isfinite(value)
            || value < parameter->minimum || value > parameter->maximum)
            addIssue(result, ValidationCode::invalidRange,
                     "Graph instance parameter override is invalid");
    }
    return result;
}

std::optional<CompiledLibrary> GraphLibrary::compile(
    ValidationResult* validation) const
{
    ValidationResult combined;
    for (const auto& [key, definition] : definitions_) {
        const auto& result = compiled(key, definition).validation;
        combined.issues.insert(combined.issues.end(), result.issues.begin(),
                               result.issues.end());
    }

    enum class Visit : std::uint8_t { unseen, visiting, visited };
    std::unordered_map<std::string, Visit> visits;
    std::vector<GraphReference> loadOrder;
    // Graphs inside graphs, to a depth (a long chain of them would run the
    // recursion off the stack).
    auto depth = 0;
    std::function<bool(const GraphDefinition&)> visitDefinition
        = [&](const GraphDefinition& definition) {
        const auto key = keyFor(definition.reference);
        if (depth >= 64) {
            addIssue(combined, ValidationCode::dependencyCycle,
                     "Nested graphs go deeper than 64 levels at " + key);
            return false;
        }
        struct Level {
            int& depth;
            explicit Level(int& value) : depth(++value) {}
            ~Level() { --depth; }
        } level { depth };
        if (visits[key] == Visit::visiting) {
            addIssue(combined, ValidationCode::dependencyCycle,
                     "Nested graph dependency cycle at " + key);
            return false;
        }
        if (visits[key] == Visit::visited)
            return true;
        visits[key] = Visit::visiting;
        for (const auto& node : definition.nodes) {
            if (node.kind != GraphNodeKind::graphInstance || !node.graph.has_value())
                continue;
            const auto* dependency = resolve(*node.graph);
            if (dependency == nullptr) {
                addIssue(combined, ValidationCode::missingDependency,
                         "Missing nested graph " + node.graph->toString(), node.id);
                continue;
            }
            if (!visitDefinition(*dependency))
                return false;
        }
        visits[key] = Visit::visited;
        loadOrder.push_back(definition.reference);
        return true;
    };
    const auto orderedReferences = all();
    for (const auto& reference : orderedReferences) {
        if (!visitDefinition(*resolve(reference)))
            break;
    }

    if (validation != nullptr)
        *validation = combined;
    if (!combined)
        return std::nullopt;

    CompiledLibrary library;
    library.loadOrder = loadOrder;
    library.fingerprint = 1469598103934665603ULL;
    for (const auto& reference : loadOrder) {
        const auto key = keyFor(reference);
        const auto& known = compiled(key, *resolve(reference));
        CompiledDefinition item;
        item.reference = reference;
        item.fingerprint = known.fingerprint;
        item.assetFingerprint = known.assetFingerprint;
        item.processingOrder = known.processingOrder;
        hashValue(library.fingerprint, item.fingerprint);
        library.definitions.push_back(std::move(item));
    }
    return library;
}

std::size_t GraphLibrary::size() const noexcept
{
    return definitions_.size();
}

GraphRole inferGraphRole(const GraphDefinition& definition)
{
    const auto has = [&definition](const PortType type,
                                   const PortDirection direction) {
        return std::ranges::any_of(definition.ports,
            [type, direction](const GraphPort& port) {
                return port.type == type && port.direction == direction;
            });
    };
    const auto audioIn = has(PortType::audio, PortDirection::input);
    const auto audioOut = has(PortType::audio, PortDirection::output);
    const auto midiIn = has(PortType::midi, PortDirection::input);
    const auto midiOut = has(PortType::midi, PortDirection::output);
    const auto valueOut = has(PortType::value, PortDirection::output);
    const auto analysisOut = has(PortType::analysis, PortDirection::output);
    if (midiIn && audioOut && !audioIn)
        return GraphRole::instrument;
    if (audioIn && audioOut)
        return GraphRole::audioEffect;
    if (midiIn && midiOut && !audioOut)
        return GraphRole::midiEffect;
    if (valueOut && !audioOut && !midiOut)
        return GraphRole::modulator;
    if (audioIn && analysisOut && !audioOut)
        return GraphRole::analyzer;
    return GraphRole::generic;
}

const char* toString(const GraphRole role) noexcept
{
    switch (role) {
    case GraphRole::generic: return "generic";
    case GraphRole::audioEffect: return "audio-effect";
    case GraphRole::instrument: return "instrument";
    case GraphRole::midiEffect: return "midi-effect";
    case GraphRole::modulator: return "modulator";
    case GraphRole::analyzer: return "analyzer";
    case GraphRole::utility: return "utility";
    }
    return "generic";
}

const char* toString(const GraphScope scope) noexcept
{
    switch (scope) {
    case GraphScope::factory: return "factory";
    case GraphScope::user: return "user";
    case GraphScope::project: return "project";
    case GraphScope::inlineTrack: return "inline-track";
    case GraphScope::inlineBus: return "inline-bus";
    }
    return "project";
}

const char* toString(const PortType type) noexcept
{
    switch (type) {
    case PortType::audio: return "audio";
    case PortType::midi: return "midi";
    case PortType::value: return "value";
    case PortType::trigger: return "trigger";
    case PortType::transport: return "transport";
    case PortType::analysis: return "analysis";
    }
    return "value";
}

const char* toString(const AudioPortType type) noexcept
{
    switch (type) {
    case AudioPortType::unspecified: return "audio-unspecified";
    case AudioPortType::mono: return "audio-mono";
    case AudioPortType::stereo: return "audio-stereo";
    case AudioPortType::channels3: return "audio-3";
    case AudioPortType::quad: return "audio-quad";
    case AudioPortType::channels5: return "audio-5";
    case AudioPortType::channels6: return "audio-6";
    case AudioPortType::channels7: return "audio-7";
    case AudioPortType::channels8: return "audio-8";
    }
    return "audio-unspecified";
}

AudioPortType audioPortTypeForChannels(const std::uint16_t channels) noexcept
{
    switch (channels) {
    case 1: return AudioPortType::mono;
    case 2: return AudioPortType::stereo;
    case 3: return AudioPortType::channels3;
    case 4: return AudioPortType::quad;
    case 5: return AudioPortType::channels5;
    case 6: return AudioPortType::channels6;
    case 7: return AudioPortType::channels7;
    case 8: return AudioPortType::channels8;
    default: return AudioPortType::unspecified;
    }
}

std::uint16_t channelCount(const AudioPortType type) noexcept
{
    switch (type) {
    case AudioPortType::mono: return 1;
    case AudioPortType::stereo: return 2;
    case AudioPortType::channels3: return 3;
    case AudioPortType::quad: return 4;
    case AudioPortType::channels5: return 5;
    case AudioPortType::channels6: return 6;
    case AudioPortType::channels7: return 7;
    case AudioPortType::channels8: return 8;
    case AudioPortType::unspecified: return 0;
    }
    return 0;
}

} // namespace cydaw::graph
