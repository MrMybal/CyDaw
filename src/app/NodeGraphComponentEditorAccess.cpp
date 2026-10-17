// The graph as plain data for CyDAW 2, whose editor is drawn with Dear ImGui
// and never shows this component. Every edit below goes through the same
// paths the canvas uses, so history, validation and the change handlers that
// compile the graph follow.

#include "NodeGraphComponent.h"

#include "NativeDspRuntime.h"
#include "NodeGraphComponentInternal.h"

#include "Localization.h"

#include <cydaw/engine/NativeDspParameters.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace cydaw::app {
namespace {

juce::Point<float> clampedPosition(const juce::Point<float> value) noexcept
{
    const auto clamp = [](const float coordinate) {
        return std::isfinite(coordinate) ? juce::jlimit(-16.0f, 17.0f, coordinate) : 0.5f;
    };
    return { clamp(value.x), clamp(value.y) };
}

juce::String translated(const juce::String& key)
{
    if (key.isEmpty())
        return {};
    const auto text = tr(key);
    return text == key ? juce::String() : text;
}

juce::String localText(const char* french, const char* english)
{
    return Localization::instance().languageCode() == "fr" ? juce::String::fromUTF8(french)
                                                           : juce::String(english);
}

bool hiddenTypeKey(const juce::String& key)
{
    return key.startsWith("graph-") || key == "master" || key == "midi-input" || key == "audio-clips"
           || key == "midi-output";
}

} // namespace

std::vector<NodeGraphComponent::EditorNode> NodeGraphComponent::editorNodes() const
{
    std::vector<EditorNode> out;
    out.reserve(nodes_.size());
    for (const auto& node : nodes_) {
        EditorNode entry;
        entry.id = node.id;
        entry.typeKey = node.typeKey;
        entry.title = displayTitle(node);
        entry.typeTitle = node.title;
        entry.alias = node.alias;
        entry.colour = node.colour;
        entry.position = node.position;
        entry.collapsed = node.collapsed;
        entry.bypassed = node.bypassed;
        entry.protectedNode = node.protectedNode;
        entry.composite = node.composite;
        if (node.composite) {
            const auto& controls = presentationForNode(node).controls;
            entry.hasInterface = std::any_of(controls.begin(), controls.end(), [](const auto& control) {
                return control.kind != ModuleControlKind::inputPort && control.kind != ModuleControlKind::outputPort;
            });
        }
        entry.boundary = isBoundaryNode(node);
        entry.invalid = invalidGraphNodes_.contains(node.id);
        entry.description = node.description;
        if (const auto* type = findType(node.typeKey)) {
            // A card named as its type is named in the language now: the
            // catalogue's titles may have been made in another one.
            if (node.alias.trim().isEmpty() && type->translationKey.isNotEmpty()
                && (node.title.trim().isEmpty() || node.title == type->title))
                if (const auto now = translated(type->translationKey); now.isNotEmpty()) {
                    entry.title = now;
                    entry.typeTitle = now;
                }
            entry.category = translated(type->categoryKey);
            if (entry.description.isEmpty())
                entry.description = translated(type->descriptionKey);
            // Track inputs and outputs carry no title of their own in a
            // stored graph: the type's name stands in.
            if (entry.title.trim().isEmpty())
                entry.title = type->title.isNotEmpty() ? type->title : translated(type->translationKey);
            if (entry.typeTitle.trim().isEmpty())
                entry.typeTitle = entry.title;
        }
        if (entry.title.trim().isEmpty())
            entry.title = node.typeKey;
        for (const auto& port : node.ports) {
            EditorPort editorPort;
            editorPort.id = port.id;
            editorPort.key = port.key;
            editorPort.name = port.name.isNotEmpty() ? port.name : port.key;
            editorPort.kind = static_cast<int>(enginePortKind(port.type));
            editorPort.type = static_cast<int>(port.type);
            editorPort.input = port.input;
            editorPort.channels = port.channels;
            editorPort.multiple = port.acceptsMultipleConnections;
            editorPort.defaultValue = port.defaultValue;
            editorPort.exposure = port.exposure;
            editorPort.category = port.category;
            entry.ports.push_back(std::move(editorPort));
        }
        // Older cards declare one socket per side and no port list.
        if (node.ports.empty()) {
            if (node.hasInput)
                entry.ports.push_back({ 1, "in", localText("Entrée", "Input"), static_cast<int>(node.inputKind), 0, true,
                                        juce::jmax(1, static_cast<int>(node.inputChannels)), true, 0.0 });
            if (node.hasOutput)
                entry.ports.push_back({ 2, "out", localText("Sortie", "Output"), static_cast<int>(node.outputKind), 0, false,
                                        juce::jmax(1, static_cast<int>(node.outputChannels)), true, 0.0 });
        }
        for (const auto& parameter : node.interfaceState.parameters) {
            EditorParameter editorParameter;
            editorParameter.binding = parameter.key;
            editorParameter.label = node.typeKey=="value-constant" && node.alias.isNotEmpty() ? node.alias
                : parameter.label.isNotEmpty() ? parameter.label : parameter.key;
            editorParameter.unit = parameter.unit;
            editorParameter.minimum = parameter.minimum;
            editorParameter.maximum = parameter.maximum;
            editorParameter.value = parameter.value;
            editorParameter.defaultValue = std::isnan(parameter.defaultValue) ? parameter.value
                                                                              : parameter.defaultValue;
            editorParameter.showInDevice = parameter.showInDevice;
            editorParameter.publishToHost = parameter.publishToHost;
            editorParameter.description = parameter.description;
            const auto primitiveKey = parameter.key.fromLastOccurrenceOf(".",false,false);
            juce::String catalogueKey;
            if(const auto* effect=cydaw::engine::effectForType(node.typeKey.toStdString()))
                for(const auto& setting:effect->parameters)
                    if(parameter.key.endsWith("."+juce::String(std::string(setting.suffix)))) {catalogueKey=juce::String(std::string(setting.key));break;}
            for (const auto& port : node.ports)
                if (port.input && ((catalogueKey.isNotEmpty() && port.key==catalogueKey) || port.key == primitiveKey || port.key == "knob." + parameter.key
                    || parameter.key.endsWith("." + port.key) || (port.key=="gain" && parameter.key.endsWith("gain.db"))
                    || (port.key=="drive" && parameter.key.endsWith("drive.db")))) {
                    editorParameter.inputPortId = port.id; break;
                }
            entry.parameters.push_back(std::move(editorParameter));
        }
        out.push_back(std::move(entry));
    }
    return out;
}

bool NodeGraphComponent::editorSetPortExposure(const std::uint64_t id, const std::uint64_t portId, const bool exposed)
{
    auto* node = findNode(id);
    if (!node || editingLocked_) return false;
    auto port = std::ranges::find(node->ports, portId, &NodePort::id);
    if (port == node->ports.end()) return false;
    // Connected sockets remain visible. Hiding one never deletes its cable.
    port->exposure = exposed ? 1 : 0;
    node->serialised = juce::var();
    notifyLayoutChanged("v2-port-exposure");
    return true;
}

bool NodeGraphComponent::editorSetPortDetails(const std::uint64_t id, const std::uint64_t portId,
    const juce::String& name, const juce::String& category)
{
    auto* node = findNode(id);
    if (!node || editingLocked_) return false;
    auto port = std::ranges::find(node->ports, portId, &NodePort::id);
    if (port == node->ports.end()) return false;
    port->name = name.trim().substring(0, 96); port->category = category.trim().substring(0, 128);
    node->serialised = juce::var(); notifyLayoutChanged("v2-port-details"); return true;
}

bool NodeGraphComponent::editorRemoveBoundary(const std::uint64_t id)
{
    auto* node = findNode(id);
    if (!node || !isBoundaryNode(*node) || editingLocked_) return false;
    std::erase_if(connections_, [id](const auto& c) { return c.source == id || c.destination == id; });
    std::erase_if(nodes_, [id](const auto& n) { return n.id == id; });
    selectedNodes_.erase(id); synchroniseGraphInterface();
    notifyLayoutChanged("v2-remove-boundary"); return true;
}

juce::Result NodeGraphComponent::editorPromoteValue(const std::uint64_t id, std::uint64_t portId,
    const juce::String& binding, const bool graphInput, std::uint64_t& created)
{
    created = 0;
    if (editingLocked_) return juce::Result::fail(localText("Ce graphe est verrouillé", "This graph is locked"));
    const auto all = editorNodes();
    const auto target = std::ranges::find(all, id, &EditorNode::id);
    if (target == all.end()) return juce::Result::fail("Unknown node");
    const auto parameter = std::ranges::find(target->parameters, binding, &EditorParameter::binding);
    if (binding.isNotEmpty()) {
        if (parameter == target->parameters.end()) return juce::Result::fail("Unknown parameter");
        portId = parameter->inputPortId;
    }
    const auto socket = std::ranges::find(target->ports, portId, &EditorPort::id);
    if (socket == target->ports.end() || !socket->input)
        return juce::Result::fail(localText("Cette valeur n'a pas encore d'entrée pilotable dans le moteur.",
                                          "This value does not yet have a controllable input in the engine."));
    if (!graphInput && socket->kind != 2)
        return juce::Result::fail(localText("Une variable locale doit être une valeur.", "A local variable must be a value."));
    if (std::ranges::any_of(connections_, [&](const auto& c) {return c.destination==id && c.destinationPort==portId;}))
        return juce::Result::fail(localText("Cette entrée est déjà reliée. Débranchez-la avant de la promouvoir.",
                                          "This input is already connected. Disconnect it before promoting it."));
    const auto key = !graphInput ? juce::String("value-constant")
        : socket->kind==0 ? juce::String("graph-audio-input-")+juce::String(socket->channels)
        : juce::String(socket->kind==1 ? "graph-midi-input" : socket->kind==2 ? "graph-value-input" : "graph-transport-input");
    const auto* type = findType(key);
    if (!type) return juce::Result::fail("Input type is unavailable");
    const auto beforeInterface = graphInterfaceState_;
    const auto beforeSelection = selectedNodes_;
    const auto beforeNext = nextNodeId_;
    auto at = target->position.translated(-.27f, .04f);
    addNodeFromType(*type, clampedPosition(at));
    const auto sourceId = nodes_.back().id;
    auto* source = findNode(sourceId);
    auto label = parameter != target->parameters.end() ? parameter->label : socket->name;
    source->alias = label;
    const auto value = parameter != target->parameters.end() ? parameter->value : socket->defaultValue;
    auto output = std::ranges::find_if(source->ports, [](const auto& p){return !p.input;});
    const auto outputId = output != source->ports.end() ? output->id : 0;
    if (output != source->ports.end()) {
        output->name = label; output->category = socket->category; output->defaultValue = value;
    }
    if (!graphInput) {
        auto& p = source->interfaceState.parameters.front();
        p.label = label; p.value = value; p.defaultValue = value;
        for(auto* controls:{&source->interfaceState.controls,&source->interfaceState.deviceControls})
            for(auto& c:*controls)if(c.parameterBinding==p.key)c.label=label;
        if (parameter != target->parameters.end()) {
            p.minimum = parameter->minimum; p.maximum = parameter->maximum; p.unit = parameter->unit;
        }
        source->parameters.valueOperand=static_cast<float>(value);
    }
    if (!outputId || !connectReplacingInput(sourceId,id,outputId,portId)) {
        std::erase_if(nodes_, [sourceId](const auto& n){return n.id==sourceId;});
        graphInterfaceState_ = beforeInterface; selectedNodes_=beforeSelection; nextNodeId_=beforeNext;
        return juce::Result::fail("The promoted value could not be connected");
    }
    // A promoted value sets its destination exactly; it is not an LFO.
    for (auto& c : connections_) if(c.source==sourceId && c.destination==id && c.destinationPort==portId) c.mode=0;
    if (auto* destination=findNode(id)) {
        if(auto p=std::ranges::find(destination->ports,portId,&NodePort::id);p!=destination->ports.end()) p->exposure=1;
        destination->serialised=juce::var();
    }
    source->serialised=juce::var();
    synchroniseGraphInterface(); rebuildEmbeddedSliders();
    notifyLayoutChanged(graphInput ? "v2-create-input" : "v2-create-variable");
    created=sourceId;
    return juce::Result::ok();
}

ModuleInterfaceState NodeGraphComponent::editorNodeInterface(const std::uint64_t nodeId) const
{
    const auto* node = findNode(nodeId);
    if (node == nullptr)
        return {};
    auto state = presentationForNode(*node);
    // The values the graph holds now (a knob turned anywhere).
    for (auto& parameter : state.parameters)
        for (const auto& live : graphInterfaceState_.parameters)
            if (live.key == parameter.key) {
                parameter.value = live.value;
                break;
            }
    return state;
}

std::vector<NodeGraphComponent::EditorConnection> NodeGraphComponent::editorConnections() const
{
    std::vector<EditorConnection> out;
    out.reserve(connections_.size());
    for (const auto& connection : connections_) {
        EditorConnection entry { connection.source, connection.destination, connection.sourcePort,
                                 connection.destinationPort, static_cast<int>(connection.kind),
                                 static_cast<int>(connection.audioChannels) };
        entry.mode = connection.mode;
        entry.amount = connection.amount;
        entry.drivesParameter = connection.kind == PortKind::control && controlCableTarget(connection).has_value();
        out.push_back(entry);
    }
    return out;
}

bool NodeGraphComponent::editorSetConnectionMode(const std::uint64_t source, const std::uint64_t sourcePort,
                                                 const std::uint64_t destination, const std::uint64_t destinationPort,
                                                 const int mode, const float amount)
{
    if (editingLocked_)
        return false;
    const auto found = std::ranges::find_if(connections_, [&](const Connection& connection) {
        return connection.source == source && connection.destination == destination
               && connection.sourcePort == sourcePort && connection.destinationPort == destinationPort;
    });
    if (found == connections_.end() || found->kind != PortKind::control)
        return false;
    const auto nextMode = static_cast<std::uint8_t>(juce::jlimit(0, 1, mode));
    const auto nextAmount = std::isfinite(amount) ? juce::jlimit(-1.0f, 1.0f, amount) : 0.5f;
    if (found->mode == nextMode && std::abs(found->amount - nextAmount) < 1.0e-6f)
        return true;
    found->mode = nextMode;
    found->amount = nextAmount;
    for (auto& node : nodes_)
        if (node.id == source || node.id == destination)
            node.serialised = juce::var();
    notifyLayoutChanged("v2-connection-mode");
    return true;
}

std::vector<NodeGraphComponent::EditorComment> NodeGraphComponent::editorComments() const
{
    std::vector<EditorComment> out;
    for (const auto& comment : comments_)
        out.push_back({ comment.id, comment.title, comment.description, comment.position, comment.size,
                        comment.colour });
    return out;
}

std::vector<NodeGraphComponent::EditorNodeType> NodeGraphComponent::editorNodeTypes() const
{
    std::vector<EditorNodeType> out;
    out.reserve(catalog_.size());
    for (const auto& type : catalog_) {
        EditorNodeType entry;
        entry.key = type.key;
        entry.title = type.title.isNotEmpty() ? type.title : tr(type.translationKey);
        entry.category = translated(type.categoryKey);
        entry.categoryKey = type.categoryKey;
        entry.description = translated(type.descriptionKey);
        entry.colour = type.colour;
        entry.module = findModule(type.key) != nullptr;
        if (const auto* module = findModule(type.key); module != nullptr && module->category.isNotEmpty())
            entry.moduleCategory = tr(module->category);
        entry.hidden = hiddenTypeKey(type.key);
        entry.inputKind = type.hasInput ? static_cast<int>(type.inputKind) : -1;
        entry.outputKind = type.hasOutput ? static_cast<int>(type.outputKind) : -1;
        const auto addKind = [](std::vector<int>& kinds, const int kind) {
            if (kind >= 0 && std::find(kinds.begin(), kinds.end(), kind) == kinds.end())
                kinds.push_back(kind);
        };
        addKind(entry.inputKinds, entry.inputKind);
        addKind(entry.outputKinds, entry.outputKind);
        for (const auto& port : type.ports)
            addKind(port.input ? entry.inputKinds : entry.outputKinds, static_cast<int>(enginePortKind(port.type)));
        out.push_back(std::move(entry));
    }
    return out;
}

std::vector<NodeGraphComponent::EditorParameter> NodeGraphComponent::editorGraphParameters() const
{
    std::vector<EditorParameter> out;
    for (const auto& parameter : graphInterfaceState_.parameters) {
        EditorParameter entry;
        entry.binding = parameter.key;
        entry.label = parameter.label.isNotEmpty() ? parameter.label : parameter.key;
        entry.unit = parameter.unit;
        entry.minimum = parameter.minimum;
        entry.maximum = parameter.maximum;
        entry.value = parameter.value;
        entry.defaultValue = std::isnan(parameter.defaultValue) ? parameter.value : parameter.defaultValue;
        entry.showInDevice = parameter.showInDevice;
        entry.publishToHost = parameter.publishToHost;
        entry.description = parameter.description;
        out.push_back(std::move(entry));
    }
    return out;
}

std::optional<float> NodeGraphComponent::editorControlValue(const std::uint64_t nodeId) const
{
    const auto found = controlValues_.find(nodeId);
    if (found == controlValues_.end())
        return std::nullopt;
    return found->second;
}

juce::String NodeGraphComponent::editorCanConnect(const std::uint64_t source, const std::uint64_t sourcePort,
                                                  const std::uint64_t destination,
                                                  const std::uint64_t destinationPort) const
{
    if (source == destination)
        return localText("Ces deux ports ne se relient pas", "These two ports do not connect");
    if (findNode(source) == nullptr || findNode(destination) == nullptr)
        return localText("Ces deux ports ne se relient pas", "These two ports do not connect");
    const auto result = validateConnection(source, destination, sourcePort, destinationPort);
    if (result)
        return {};
    return connectionProblemText(result);
}

bool NodeGraphComponent::spliceBeforeOutput(const std::uint64_t instanceId)
{
    const auto instance = static_cast<NodeId>(instanceId);
    const auto* card = findNode(instance);
    if (card == nullptr || !card->hasInput || !card->hasOutput || card->inputKind != PortKind::audio
        || card->outputKind != PortKind::audio)
        return false;
    const auto output = std::find_if(nodes_.begin(), nodes_.end(), [](const Node& node) {
        bool input = false;
        return node.typeKey == "master" || (isGraphAudioBoundaryKey(node.typeKey, &input) && !input);
    });
    if (output == nodes_.end())
        return false;
    const auto target = output->id;
    const auto feeding = std::find_if(connections_.begin(), connections_.end(), [target](const Connection& connection) {
        return connection.destination == target && connection.kind == PortKind::audio;
    });
    if (feeding == connections_.end())
        return false;
    const auto original = *feeding;
    cydaw::graph::PortId in {};
    cydaw::graph::PortId out {};
    for (const auto& port : card->ports)
        if (port.type == cydaw::graph::PortType::audio) {
            if (port.input && in == 0)
                in = port.id;
            else if (!port.input && out == 0)
                out = port.id;
        }
    connections_.erase(feeding);
    // What fed the output feeds the device, which feeds the output; widths
    // that differ (a mono device on a stereo track) meet through converters.
    const auto join = [this](const NodeId source, const cydaw::graph::PortId sourcePort, const NodeId destination,
                             const cydaw::graph::PortId destinationPort) {
        return connectReplacingInput(source, destination, sourcePort, destinationPort)
               || editorConnectThroughConverter(source, sourcePort, destination, destinationPort) != 0;
    };
    if (join(original.source, original.sourcePort, instance, in) && join(instance, out, target, original.destinationPort))
        return true;
    std::erase_if(connections_, [instance](const Connection& connection) {
        return connection.source == instance || connection.destination == instance;
    });
    connections_.push_back(original);
    return false;
}

int NodeGraphComponent::editorAudioChannels(const std::uint64_t nodeId, const std::uint64_t portId, const bool input) const
{
    const auto* node = findNode(static_cast<NodeId>(nodeId));
    if (node == nullptr)
        return 0;
    if (const auto* socket = findPort(*node, static_cast<cydaw::graph::PortId>(portId));
        socket != nullptr && socket->input == input && socket->type == cydaw::graph::PortType::audio)
        return socket->channels;
    return input ? audioInputChannels(*node) : audioOutputChannels(*node);
}

std::uint64_t NodeGraphComponent::editorConnectThroughConverter(const std::uint64_t source, const std::uint64_t sourcePort,
                                                                const std::uint64_t destination,
                                                                const std::uint64_t destinationPort)
{
    if (editingLocked_)
        return 0;
    const auto from = editorAudioChannels(source, sourcePort, false);
    const auto to = editorAudioChannels(destination, destinationPort, true);
    const juce::String key = from == 1 && to == 2 ? "mono-to-stereo" : from == 2 && to == 1 ? "stereo-to-mono" : juce::String();
    const auto* a = findNode(static_cast<NodeId>(source));
    const auto* b = findNode(static_cast<NodeId>(destination));
    if (key.isEmpty() || a == nullptr || b == nullptr)
        return 0;
    // Between the two cards, a little below the cable.
    const juce::Point<float> middle((a->position.x + b->position.x) * 0.5f, (a->position.y + b->position.y) * 0.5f + 0.08f);
    const auto converter = addNodeForRemote(key, clampedPosition(middle));
    if (converter == 0)
        return 0;
    const auto id = static_cast<NodeId>(converter);
    if (connectReplacingInput(static_cast<NodeId>(source), id, static_cast<cydaw::graph::PortId>(sourcePort), 1)
        && connectReplacingInput(id, static_cast<NodeId>(destination), 2, static_cast<cydaw::graph::PortId>(destinationPort))) {
        for (auto& node : nodes_)
            if (node.id == source || node.id == destination || node.id == id)
                node.serialised = juce::var();
        synchroniseGraphInterface();
        rebuildEmbeddedSliders();
        notifyLayoutChanged("v2-connect-converter");
        return converter;
    }
    (void) editorRemoveNodes({ converter });
    return 0;
}

juce::String NodeGraphComponent::connectionProblemText(const cydaw::engine::GraphResult& result) const
{
    using Error = cydaw::engine::GraphError;
    switch (result.error) {
    case Error::incompatiblePortKinds:
        return juce::String(result.message).containsIgnoreCase("channel")
                   ? localText("Pas le même nombre de canaux (mono, stéréo)", "Not the same number of channels (mono, stereo)")
                   : localText("Ces prises ne portent pas le même signal", "These sockets do not carry the same signal");
    case Error::cycleDetected: return localText("Ce câble ferait une boucle", "This cable would make a loop");
    case Error::duplicateConnection: return localText("Cette entrée est déjà reliée", "This input is already connected");
    case Error::invalidPort:
    case Error::nodeNotFound:
    case Error::invalidNodeId:
    case Error::nodeAlreadyExists:
    case Error::connectionNotFound:
    case Error::none: break;
    }
    return localText("Ces deux prises ne se relient pas", "These two sockets do not connect");
}

bool NodeGraphComponent::editorCanUndo() const noexcept
{
    return !undoHistory_.empty();
}

bool NodeGraphComponent::editorCanRedo() const noexcept
{
    return !redoHistory_.empty();
}

std::uint64_t NodeGraphComponent::editorAddNode(const juce::String& typeKey, const juce::Point<float> position)
{
    return addNodeForRemote(typeKey, clampedPosition(position));
}

bool NodeGraphComponent::editorRemoveNodes(const std::vector<std::uint64_t>& ids)
{
    if (editingLocked_)
        return false;
    selectedNodes_.clear();
    for (const auto id : ids) {
        auto* node = findNode(id);
        if (node == nullptr || isBoundaryNode(*node))
            continue;
        node->protectedNode = false;
        selectedNodes_.insert(id);
    }
    if (selectedNodes_.empty())
        return false;
    selectedCommentId_ = 0;
    selectedConnection_ = -1;
    selectedPortId_ = 0;
    const auto before = nodes_.size();
    removeSelectedNodes();
    return nodes_.size() < before;
}

bool NodeGraphComponent::editorRemoveAudioInput(const std::uint64_t id)
{
    if (editingLocked_)
        return false;
    const auto others = audioInputBuses();
    if (std::none_of(others.begin(), others.end(), [id](const auto& input) { return input.first == id; }))
        return false;
    auto* node = findNode(id);
    if (node == nullptr)
        return false;
    node->protectedNode = false;
    selectedNodes_ = { id };
    selectedCommentId_ = 0;
    selectedConnection_ = -1;
    selectedPortId_ = 0;
    const auto before = nodes_.size();
    removeSelectedNodes();
    return nodes_.size() < before;
}

void NodeGraphComponent::editorMoveNodes(const std::vector<std::pair<std::uint64_t, juce::Point<float>>>& positions,
                                         const bool commit)
{
    if (editingLocked_)
        return;
    for (const auto& [id, position] : positions)
        if (auto* node = findNode(id)) {
            node->position = clampedPosition(position);
            node->serialised = juce::var();
        }
    if (commit)
        notifyLayoutChanged("v2-move");
}

juce::String NodeGraphComponent::editorConnect(const std::uint64_t source, const std::uint64_t sourcePort,
                                               const std::uint64_t destination,
                                               const std::uint64_t destinationPort)
{
    if (editingLocked_)
        return localText("Ce graphe est verrouillé", "This graph is locked");
    if (source == destination || findNode(source) == nullptr || findNode(destination) == nullptr)
        return localText("Ces deux prises ne se relient pas", "These two sockets do not connect");
    // A cable onto an input that takes one replaces the cable it had, and a
    // free card takes the other side's channel count (connectReplacingInput,
    // connectNodes). Only what is left is refused, and said why.
    if (!connectReplacingInput(source, destination, sourcePort, destinationPort)) {
        std::vector<Connection> occupying;
        for (const auto& connection : connections_)
            if (connection.destination == destination && connection.destinationPort == destinationPort)
                occupying.push_back(connection);
        std::erase_if(connections_, [&](const Connection& connection) {
            return connection.destination == destination && connection.destinationPort == destinationPort;
        });
        const auto result = validateConnection(source, destination, sourcePort, destinationPort);
        connections_.insert(connections_.end(), occupying.begin(), occupying.end());
        // Mono to stereo or back: through a converter rather than refused.
        if (!result && result.error == cydaw::engine::GraphError::incompatiblePortKinds
            && editorConnectThroughConverter(source, sourcePort, destination, destinationPort) != 0)
            return {};
        return result ? localText("Ces deux prises ne se relient pas", "These two sockets do not connect") : connectionProblemText(result);
    }
    for (auto& node : nodes_)
        if (node.id == source || node.id == destination)
            node.serialised = juce::var();
    applyDefaultCableMode(source, destination, destinationPort);
    synchroniseGraphInterface();
    rebuildEmbeddedSliders();
    notifyLayoutChanged("v2-connect");
    return {};
}

bool NodeGraphComponent::editorDisconnect(const std::uint64_t source, const std::uint64_t sourcePort,
                                          const std::uint64_t destination, const std::uint64_t destinationPort)
{
    if (editingLocked_)
        return false;
    const auto before = connections_.size();
    std::erase_if(connections_, [&](const Connection& connection) {
        return connection.source == source && connection.destination == destination
               && connection.sourcePort == sourcePort && connection.destinationPort == destinationPort;
    });
    if (connections_.size() == before)
        return false;
    for (auto& node : nodes_)
        if (node.id == source || node.id == destination)
            node.serialised = juce::var();
    selectedConnection_ = -1;
    synchroniseGraphInterface();
    rebuildEmbeddedSliders();
    notifyLayoutChanged("v2-disconnect");
    return true;
}

std::map<std::uint64_t, NodeGraphComponent::EditorNativeSpan> NodeGraphComponent::editorNativeSpans() const
{
    return nativeSpansOf(exportedNativeGraph());
}


// ---- Devices made of a graph (modules) -------------------------------------------------------------

std::optional<NodeGraphComponent::EditorModuleInfo> NodeGraphComponent::editorModuleOf(const std::uint64_t nodeId) const
{
    const auto* node = findNode(nodeId);
    const auto* module = node != nullptr ? findModule(node->typeKey) : nullptr;
    if (module == nullptr)
        return std::nullopt;
    return EditorModuleInfo { module->key, module->name, inferModuleRole(*module), module->factoryModule,
                              module->inlineModule };
}

juce::var NodeGraphComponent::editorModuleGraphState(const juce::String& key) const
{
    const auto* module = findModule(key);
    return module != nullptr ? moduleGraphState(*module) : juce::var();
}

void NodeGraphComponent::editorApplyModuleGraphState(const juce::String& key, const juce::var& state, const bool structural)
{
    if (editingLocked_ || findModule(key) == nullptr)
        return;
    applyModuleGraphState(key, state, structural);
}

juce::String NodeGraphComponent::editorMakeModuleOwn(const std::uint64_t nodeId)
{
    const auto* node = findNode(nodeId);
    const auto* module = node != nullptr ? findModule(node->typeKey) : nullptr;
    if (module == nullptr)
        return {};
    if (module->inlineModule && !module->factoryModule)
        return module->key;
    const auto sourceKey = module->key;
    const auto name = module->name;
    const auto previousSelection = selectedNodes_;
    selectedNodes_ = { static_cast<NodeId>(nodeId) };
    const auto created = createGraphVariant(sourceKey, name, true, false);
    selectedNodes_ = previousSelection;
    if (created.failed())
        return {};
    const auto* instance = findNode(nodeId);
    return instance != nullptr ? instance->typeKey : juce::String();
}

std::uint64_t NodeGraphComponent::editorCreateModuleFromSelection(const std::vector<std::uint64_t>& ids, const juce::String& name,
                                                                  juce::String& error)
{
    if (editingLocked_) {
        error = localText("Ce graphe est verrouillé", "This graph is locked");
        return 0;
    }
    std::set<NodeId> chosen;
    for (const auto id : ids)
        if (const auto* node = findNode(id); node != nullptr && !isBoundaryNode(*node) && node->typeKey != "master"
                                             && node->typeKey != "midi-input" && node->typeKey != "audio-clips")
            chosen.insert(static_cast<NodeId>(id));
    if (chosen.empty()) {
        error = localText("Choisissez les périphériques à réunir", "Pick the devices to put together");
        return 0;
    }
    // What crosses the edge of the selection: sound in, sound out, and MIDI
    // in or out — an instrument's voice section is a device as the factory
    // keys are (notes in, sound out). A control cable crossing it would be
    // cut.
    std::vector<Connection> into;
    std::vector<Connection> out;
    std::vector<Connection> midiInto;
    std::vector<Connection> midiOut;
    for (const auto& connection : connections_) {
        const auto fromInside = chosen.contains(connection.source);
        const auto toInside = chosen.contains(connection.destination);
        if (fromInside == toInside)
            continue;
        if (connection.kind == PortKind::midi) {
            (toInside ? midiInto : midiOut).push_back(connection);
            continue;
        }
        if (connection.kind != PortKind::audio) {
            error = localText("Un câble de modulation traverse la sélection : prenez aussi sa source",
                              "A modulation cable crosses the selection: take its source too");
            return 0;
        }
        (toInside ? into : out).push_back(connection);
    }
    if ((into.empty() && midiInto.empty()) || (out.empty() && midiOut.empty())) {
        error = localText("La sélection doit recevoir du son ou des notes, et en rendre",
                          "The selection must take sound or notes in, and give some out");
        return 0;
    }

    SavedModule module;
    module.key = "inline-graph-" + juce::Uuid().toString().removeCharacters("{}-").toLowerCase();
    module.inlineModule = true;
    module.factoryModule = false;
    module.name = name.trim().substring(0, 128);
    if (module.name.isEmpty())
        module.name = localText("Périphérique ", "Device ") + juce::String(nextModuleIndex_);
    ++nextModuleIndex_;
    module.author = graphAuthor_;
    juce::Point<float> least { 1.0e9f, 1.0e9f };
    juce::Point<float> most { -1.0e9f, -1.0e9f };
    juce::Point<float> sum;
    NodeId highest {};
    std::uint8_t channels {};
    for (const auto id : chosen) {
        const auto* node = findNode(id);
        ModuleNode child { node->id, node->typeKey, node->title, node->position, node->parameters,
                           node->inputChannels, node->outputChannels, node->alias, node->description };
        child.interfaceState = node->interfaceState;
        child.ports = node->ports;
        child.collapsed = node->collapsed;
        child.wavetableShape = node->wavetableShape;
        child.wavetableBank = node->wavetableBank;
        child.soundModel = node->soundModel;
        child.soundModelJson = node->soundModelJson;
        child.samplerMap = node->samplerMap;
        child.samplerJson = node->samplerJson;
        child.prismTables = node->prismTables;
        child.prismTablesJson = node->prismTablesJson;
        child.impulse = node->impulse;
        child.impulseJson = node->impulseJson;
        module.nodes.push_back(std::move(child));
        least = { std::min(least.x, node->position.x), std::min(least.y, node->position.y) };
        most = { std::max(most.x, node->position.x), std::max(most.y, node->position.y) };
        sum += node->position;
        highest = std::max(highest, node->id);
    }
    for (const auto& connection : into)
        if (const auto* node = findNode(connection.destination))
            channels = std::max(channels, audioInputChannels(*node));
    channels = channels == 0 ? std::uint8_t { 2 } : channels;
    for (const auto& connection : connections_)
        if (chosen.contains(connection.source) && chosen.contains(connection.destination))
            module.connections.push_back(connection);
    // Its own inputs and outputs, where the chain came in and went out.
    const auto inputId = highest + 1;
    const auto outputId = highest + 2;
    const auto midiInputId = highest + 3;
    const auto midiOutputId = highest + 4;
    const auto boundary = [this, &module](const NodeId id, const juce::String& key, const juce::Point<float> position) {
        if (const auto* type = findType(key)) {
            module.nodes.push_back({ id, key, type->title, position, defaultParametersForType(key), type->inputChannels,
                                     type->outputChannels, {} });
            module.nodes.back().ports = type->ports;
        }
    };
    const auto middle = (least.y + most.y) * 0.5f;
    if (!into.empty())
        boundary(inputId, graphAudioBoundaryKey(true, channels), { std::max(0.02f, least.x - 0.14f), middle });
    if (!out.empty())
        boundary(outputId, graphAudioBoundaryKey(false, channels), { std::min(0.95f, most.x + 0.16f), middle });
    if (!midiInto.empty())
        boundary(midiInputId, "graph-midi-input", { std::max(0.02f, least.x - 0.14f), std::max(0.04f, middle - 0.2f) });
    if (!midiOut.empty())
        boundary(midiOutputId, "graph-midi-output", { std::min(0.95f, most.x + 0.16f), std::max(0.04f, middle - 0.2f) });
    std::set<std::pair<NodeId, cydaw::graph::PortId>> entries;
    for (const auto& connection : into)
        if (entries.insert({ connection.destination, connection.destinationPort }).second)
            module.connections.push_back({ inputId, connection.destination, PortKind::audio, connection.audioChannels, 0,
                                           connection.destinationPort });
    std::set<std::pair<NodeId, cydaw::graph::PortId>> exits;
    for (const auto& connection : out)
        if (exits.insert({ connection.source, connection.sourcePort }).second)
            module.connections.push_back({ connection.source, outputId, PortKind::audio, connection.audioChannels,
                                           connection.sourcePort, 0 });
    std::set<std::pair<NodeId, cydaw::graph::PortId>> noteEntries;
    for (const auto& connection : midiInto)
        if (noteEntries.insert({ connection.destination, connection.destinationPort }).second)
            module.connections.push_back({ midiInputId, connection.destination, PortKind::midi, 0, 0,
                                           connection.destinationPort });
    std::set<std::pair<NodeId, cydaw::graph::PortId>> noteExits;
    for (const auto& connection : midiOut)
        if (noteExits.insert({ connection.source, connection.sourcePort }).second)
            module.connections.push_back({ connection.source, midiOutputId, PortKind::midi, 0, connection.sourcePort, 0 });
    inferModulePorts(module);
    module.role = inferModuleRole(module);
    module.interfaceState = createDefaultModuleInterface(module);
    const auto key = module.key;
    savedModules_.push_back(std::move(module));
    registerModuleType(savedModules_.back());
    applyLibraryFilter();
    const auto* type = findType(key);
    if (type == nullptr) {
        error = localText("Le périphérique n'a pas pu être créé", "The device could not be made");
        return 0;
    }

    // The device in their place, wired as they were.
    const auto centre = sum / static_cast<float>(chosen.size());
    std::erase_if(connections_, [&chosen](const Connection& connection) {
        return chosen.contains(connection.source) || chosen.contains(connection.destination);
    });
    std::erase_if(nodes_, [&chosen](const Node& node) { return chosen.contains(node.id); });
    for (const auto id : chosen)
        selectedNodes_.erase(id);
    addNodeFromType(*type, centre);
    const auto instance = nodes_.back().id;
    // The device's own socket of a kind: notes and sound each have theirs.
    const auto socket = [this, instance](const bool input, const PortKind kind) -> cydaw::graph::PortId {
        if (const auto* node = findNode(instance))
            for (const auto& port : node->ports)
                if (port.input == input && enginePortKind(port.type) == kind)
                    return port.id;
        return 0;
    };
    std::set<std::pair<NodeId, cydaw::graph::PortId>> sources;
    for (const auto& connection : into)
        if (sources.insert({ connection.source, connection.sourcePort }).second)
            (void) connectNodes(connection.source, instance, connection.sourcePort, socket(true, PortKind::audio));
    std::set<std::pair<NodeId, cydaw::graph::PortId>> destinations;
    for (const auto& connection : out)
        if (destinations.insert({ connection.destination, connection.destinationPort }).second)
            (void) connectReplacingInput(instance, connection.destination, socket(false, PortKind::audio), connection.destinationPort);
    std::set<std::pair<NodeId, cydaw::graph::PortId>> noteSources;
    for (const auto& connection : midiInto)
        if (noteSources.insert({ connection.source, connection.sourcePort }).second)
            (void) connectNodes(connection.source, instance, connection.sourcePort, socket(true, PortKind::midi));
    std::set<std::pair<NodeId, cydaw::graph::PortId>> noteDestinations;
    for (const auto& connection : midiOut)
        if (noteDestinations.insert({ connection.destination, connection.destinationPort }).second)
            (void) connectNodes(instance, connection.destination, socket(false, PortKind::midi), connection.destinationPort);
    for (auto& node : nodes_)
        node.serialised = juce::var();
    synchroniseGraphInterface();
    rebuildEmbeddedSliders();
    notifyLayoutChanged("v2-module-from-selection");
    return instance;
}

std::uint64_t NodeGraphComponent::editorCreateEmptyDevice(const cydaw::graph::GraphRole role)
{
    if (editingLocked_)
        return 0;
    const auto before = nextNodeId_;
    if (createEditableGraphModule(role, false, 2).failed())
        return 0;
    for (auto index = nodes_.size(); index-- > 0;)
        if (nodes_[index].id >= before && findModule(nodes_[index].typeKey) != nullptr)
            return nodes_[index].id;
    return 0;
}

std::uint64_t NodeGraphComponent::editorInsertModuleFromState(const juce::var& state, const cydaw::graph::GraphRole role,
                                                              const juce::String& name)
{
    const auto* root = state.getDynamicObject();
    const auto* nodes = root != nullptr ? root->getProperty("nodes").getArray() : nullptr;
    if (editingLocked_ || nodes == nullptr || nodes->isEmpty())
        return 0;
    // An empty class first, which the state then fills: its ports come
    // from the state's own boundaries before any instance is made.
    SavedModule module;
    module.key = "inline-graph-" + juce::Uuid().toString().removeCharacters("{}-").toLowerCase();
    module.inlineModule = true;
    module.factoryModule = false;
    module.role = role;
    module.name = name.isNotEmpty() ? name : localText("Périphérique", "Device");
    module.author = graphAuthor_;
    if (const auto* type = findType(graphAudioBoundaryKey(false, 2))) {
        module.nodes.push_back({ 1, type->key, type->title, { 0.7f, 0.4f }, defaultParametersForType(type->key),
                                 type->inputChannels, type->outputChannels, {} });
        module.nodes.back().ports = type->ports;
    }
    const auto key = module.key;
    savedModules_.push_back(std::move(module));
    registerModuleType(savedModules_.back());
    applyModuleGraphState(key, state, true);
    auto* filled = findModule(key);
    if (filled == nullptr)
        return 0;
    if (name.isNotEmpty())
        filled->name = name;
    inferModulePorts(*filled);
    filled->role = inferModuleRole(*filled);
    registerModuleType(*filled);
    applyLibraryFilter();
    const auto* type = findType(key);
    if (type == nullptr)
        return 0;
    addNodeFromType(*type);
    const auto instance = nodes_.back().id;
    if (!spliceBeforeOutput(instance))
        (void) insertModuleIntoCurrentSignalFlow(instance);
    synchroniseGraphInterface();
    rebuildEmbeddedSliders();
    notifyLayoutChanged("v2-insert-library-device");
    return instance;
}

juce::Result NodeGraphComponent::editorSetInterface(const ModuleInterfaceState& state, const bool commit)
{
    if (editingLocked_)
        return juce::Result::fail(localText("Ce graphe est verrouillé", "This graph is locked"));
    auto next = graphInterfaceState_;
    next.controls = state.controls;
    next.deviceControls = state.deviceControls;
    next.deviceDesigned = state.deviceDesigned;
    next.deviceWidth = state.deviceWidth;
    next.deviceHeight = state.deviceHeight;
    next.snapToGrid = state.snapToGrid;
    next.gridColumns = state.gridColumns;
    next.gridRows = state.gridRows;
    next.windowDesign=state.windowDesign.clone();
    next.deviceDesign=state.deviceDesign.clone();
    next.windowWidth = state.windowWidth;
    next.windowHeight = state.windowHeight;
    // The person's own layout from now on.
    next.designed = state.designed;
    next.nextControlId = std::max(next.nextControlId, state.nextControlId);
    for (const auto* controls : { &next.controls, &next.deviceControls })
        for (const auto& control : *controls) next.nextControlId = std::max(next.nextControlId, control.id + 1);
    if (const auto valid = validateModuleInterface(next); valid.failed())
        return valid;
    graphInterfaceState_ = std::move(next);
    if (commit)
        notifyLayoutChanged("v2-interface");
    return juce::Result::ok();
}

NodeGraphComponent::EditorGraphInfo NodeGraphComponent::editorGraphInfo() const
{
    return { graphName_, graphDescription_, graphAuthor_, graphCategory_ };
}

void NodeGraphComponent::editorSetGraphInfo(const EditorGraphInfo& info)
{
    if (editingLocked_)
        return;
    auto name = info.name.trim().substring(0, 128);
    if (name.isEmpty())
        name = graphName_;
    const auto description = info.description.substring(0, 2'048);
    const auto author = info.author.trim().substring(0, 128);
    const auto category = info.category.trim().substring(0, 192);
    if (name == graphName_ && description == graphDescription_ && author == graphAuthor_ && category == graphCategory_)
        return;
    graphName_ = name;
    graphDescription_ = description;
    graphAuthor_ = author;
    graphCategory_ = category;
    notifyLayoutChanged("v2-graph-info");
}

void NodeGraphComponent::editorSetPrograms(std::vector<EditorProgram> programs)
{
    if (editingLocked_)
        return;
    std::vector<EditorProgram> cleaned;
    for (auto& program : programs) {
        program.name = program.name.trim().substring(0, 128);
        if (program.name.isNotEmpty() && cleaned.size() < 512
            && std::none_of(cleaned.begin(), cleaned.end(), [&](const auto& other) { return other.name == program.name; }))
            cleaned.push_back(std::move(program));
    }
    const auto same = cleaned.size() == programs_.size()
                      && std::equal(cleaned.begin(), cleaned.end(), programs_.begin(), [](const auto& a, const auto& b) {
                             return a.name == b.name && a.values == b.values;
                         });
    if (same)
        return;
    programs_ = std::move(cleaned);
    notifyLayoutChanged("v2-programs");
}

NodeGraphComponent::EditorProgram NodeGraphComponent::editorCaptureProgram(const juce::String& name, const bool defaults) const
{
    EditorProgram program;
    program.name = name.trim();
    for (const auto& parameter : graphInterfaceState_.parameters)
        program.values.emplace_back(parameter.key, defaults && std::isfinite(parameter.defaultValue) ? parameter.defaultValue : parameter.value);
    return program;
}

bool NodeGraphComponent::editorApplyProgram(const EditorProgram& program)
{
    auto changed = false;
    for (const auto& [key, value] : program.values)
        for (const auto& parameter : graphInterfaceState_.parameters)
            if (parameter.key == key) {
                const auto clamped = juce::jlimit(parameter.minimum, parameter.maximum, value);
                if (std::abs(parameter.value - clamped) > 1.0e-9)
                    changed = editorSetParameter(key, clamped) || changed;
                break;
            }
    return changed;
}

void NodeGraphComponent::editorSetGraphName(const juce::String& name)
{
    const auto cleaned = name.trim().substring(0, 128);
    if (editingLocked_ || cleaned.isEmpty() || cleaned == graphName_)
        return;
    graphName_ = cleaned;
    notifyLayoutChanged("v2-graph-name");
}

bool NodeGraphComponent::editorIsVoiceNode(const std::uint64_t id) const
{
    const auto boundary = instrumentVoiceBoundary();
    return boundary.outputNodeId != 0 && boundary.voiceNodes.contains(id);
}

std::optional<cydaw::engine::PolyVoiceTarget> NodeGraphComponent::editorVoiceTargetFor(const juce::String& binding) const
{
    if (!binding.startsWith("node."))
        return std::nullopt;
    const auto separator = binding.indexOfChar(5, '.');
    if (separator < 0)
        return std::nullopt;
    const auto id = static_cast<std::uint64_t>(binding.substring(5, separator).getLargeIntValue());
    if (!editorIsVoiceNode(id))
        return std::nullopt;
    const auto suffix = binding.substring(separator + 1);
    using T = cydaw::engine::PolyVoiceTarget;
    if (suffix == "voiceFilter.cutoff" || suffix == "lowpass.cutoff")
        return T::filterCutoff;
    if (suffix == "voiceFilter.resonance" || suffix == "lowpass.resonance")
        return T::filterResonance;
    if (suffix == "wavetable.position")
        return T::wavetablePosition;
    if (suffix == "wavetable.level")
        return T::oscillatorLevel;
    if (suffix == "wavetable.detune")
        return T::wavetableDetune;
    if (suffix == "string.damping")
        return T::stringDamping;
    return std::nullopt;
}

std::optional<cydaw::engine::PolyVoiceParameter> NodeGraphComponent::editorVoiceParameterFor(const juce::String& binding) const
{
    using T = cydaw::engine::PolyVoiceTarget;
    if (const auto target = editorVoiceTargetFor(binding))
        return cydaw::engine::PolyVoiceParameter { *target, 0 };
    if (!binding.startsWith("node."))
        return std::nullopt;
    const auto separator = binding.indexOfChar(5, '.');
    if (separator < 0 || !editorIsVoiceNode(static_cast<std::uint64_t>(binding.substring(5, separator).getLargeIntValue())))
        return std::nullopt;
    const auto suffix = binding.substring(separator + 1);
    if (suffix == "prism.level" || suffix == "kick.level" || suffix == "model.level" || suffix == "sampler.level")
        return cydaw::engine::PolyVoiceParameter { T::oscillatorLevel, 0 };
    const auto indexed = [](const T target, const int index) -> std::optional<cydaw::engine::PolyVoiceParameter> {
        if (index < 0)
            return std::nullopt;
        return cydaw::engine::PolyVoiceParameter { target, static_cast<std::uint16_t>(index) };
    };
    if (suffix.startsWith("prism."))
        return indexed(T::prismValue, cydaw::engine::prism::parameterIndex(suffix.substring(6).toStdString()));
    if (suffix.startsWith("kick."))
        return indexed(T::kickValue, cydaw::engine::kick::parameterIndex(suffix.substring(5).toStdString()));
    if (const auto model = cydaw::engine::synth::modelForPrefix(suffix.upToFirstOccurrenceOf(".", false, false).toStdString());
        model != cydaw::engine::synth::Model::none) {
        const auto key = suffix.fromFirstOccurrenceOf(".", false, false);
        if (key == "level")
            return cydaw::engine::PolyVoiceParameter { T::oscillatorLevel, 0 };
        return indexed(T::synthValue, cydaw::engine::synth::parameterIndex(model, key.toStdString()));
    }
    if (suffix.startsWith("sampler.cc")) {
        const auto digits = suffix.substring(10);
        if (digits.isNotEmpty() && digits.containsOnly("0123456789") && digits.getIntValue() < 128)
            return indexed(T::samplerController, digits.getIntValue());
    }
    if (suffix.startsWith("sampler."))
        return indexed(T::samplerValue, cydaw::engine::samplerSettingIndex(suffix.toStdString()));
    if (suffix.startsWith("batterie."))
        return indexed(T::drumValue, cydaw::engine::drum::parameterIndex(suffix.substring(9).toStdString()));
    if (suffix.startsWith("model."))
        return indexed(T::soundModelValue, cydaw::engine::soundModelSettingIndex(suffix.toStdString()));
    return std::nullopt;
}

std::vector<cydaw::engine::PolyVoiceModulation> NodeGraphComponent::editorVoiceModulations() const
{
    std::vector<cydaw::engine::PolyVoiceModulation> modulations;
    const auto boundary = instrumentVoiceBoundary();
    if (boundary.outputNodeId == 0)
        return modulations;
    for (const auto& connection : connections_) {
        if (connection.kind != PortKind::control || !boundary.voiceNodes.contains(connection.destination))
            continue;
        const auto* source = findNode(connection.source);
        const auto* destination = findNode(connection.destination);
        if (source == nullptr || destination == nullptr)
            continue;
        const auto sourceKind = cydaw::engine::nativeDspControlKindForProcessor(
            detail::primitiveProcessorKeyForType(source->typeKey).toStdString());
        if (!sourceKind.has_value() || !isControlOnlyNativeKind(*sourceKind))
            continue;
        const auto* port = findPort(*destination, connection.destinationPort);
        if (port == nullptr)
            continue;
        const auto processor = detail::primitiveProcessorKeyForType(destination->typeKey);
        std::optional<cydaw::engine::PolyVoiceTarget> target;
        using T = cydaw::engine::PolyVoiceTarget;
        if (processor == "core.lowpass" && port->key == "cutoff")
            target = T::filterCutoff;
        else if (processor == "core.lowpass" && port->key == "resonance")
            target = T::filterResonance;
        else if (processor == "core.wavetable" && port->key == "position")
            target = T::wavetablePosition;
        else if (processor == "core.wavetable" && port->key == "level")
            target = T::oscillatorLevel;
        else if (processor == "core.wavetable" && port->key == "detune")
            target = T::wavetableDetune;
        else if (processor == "core.string" && port->key == "damping")
            target = T::stringDamping;
        if (target) {
            // A modulating cable of no depth still modulates: by nothing.
            const auto depth = connection.mode == 1 ? (connection.amount != 0.0f ? connection.amount : 1.0e-6f) : 0.0f;
            modulations.push_back({ connection.source, *target, depth });
        }
    }
    return modulations;
}

std::map<juce::String, float> NodeGraphComponent::editorModulatedParameters() const
{
    std::map<juce::String, float> values;
    if (controlValues_.empty())
        return values;
    for (const auto& connection : connections_) {
        if (connection.kind != PortKind::control)
            continue;
        const auto value = controlValues_.find(connection.source);
        if (value == controlValues_.end())
            continue;
        const auto target = controlCableTarget(connection);
        if (!target || target->binding.isEmpty())
            continue;
        if (connection.mode != 1) {
            values[target->binding] = value->second;
            continue;
        }
        // Around the knob's own value, as the engine does.
        const auto* destination = findNode(connection.destination);
        if (destination == nullptr)
            continue;
        const auto& parameters = presentationForNode(*destination).parameters;
        const auto knob = std::ranges::find_if(parameters, [&target](const auto& parameter) {
            return parameter.key == target->binding;
        });
        if (knob == parameters.end())
            continue;
        const auto low = std::min(target->minimum, target->maximum);
        const auto high = std::max(target->minimum, target->maximum);
        const auto control = static_cast<double>(value->second) * static_cast<double>(connection.amount);
        auto moved = knob->value;
        if (low > 0.0 && high / low >= 100.0 && moved > 0.0)
            moved = moved * std::pow(high / low, control);
        else
            moved = moved + control * (high - low);
        values[target->binding] = static_cast<float>(juce::jlimit(low, high, moved));
    }
    return values;
}

bool NodeGraphComponent::editorSetParameter(const juce::String& binding, const double value)
{
    if (!setGraphInterfaceParameter(binding, value))
        return false;
    parameterTouches_[binding] = ++parameterTouchCount_;
    // Heard at once; the undo step is taken once the knob rests, as the
    // timer does for CyDAW 1's sliders.
    parameterHistoryPending_ = true;
    lastParameterUiChangeTimeMs_ = juce::Time::currentTimeMillis();
    dispatchParameterNotification();
    if (!isTimerRunning())
        startTimerHz(30);
    return true;
}

std::uint64_t NodeGraphComponent::editorParameterTouch(const juce::String& binding) const
{
    const auto found = parameterTouches_.find(binding);
    return found != parameterTouches_.end() ? found->second : 0;
}

std::optional<cydaw::engine::prism::PrismValues> NodeGraphComponent::editorPrismValues(const std::uint64_t nodeId) const
{
    const auto* node = findNode(static_cast<NodeId>(nodeId));
    if (node == nullptr || node->typeKey != "prism-synth")
        return std::nullopt;
    return node->parameters.prism;
}

std::optional<cydaw::engine::kick::KickValues> NodeGraphComponent::editorKickValues(const std::uint64_t nodeId) const
{
    const auto* node = findNode(static_cast<NodeId>(nodeId));
    if (node == nullptr || node->typeKey != "kick-synth")
        return std::nullopt;
    return node->parameters.kick;
}

std::optional<std::pair<cydaw::engine::synth::Model, cydaw::engine::synth::Values>>
NodeGraphComponent::editorSynthValues(const std::uint64_t nodeId) const
{
    const auto* node = findNode(static_cast<NodeId>(nodeId));
    if (node == nullptr)
        return std::nullopt;
    const auto model = cydaw::engine::synth::modelForType(node->typeKey.toStdString());
    if (model == cydaw::engine::synth::Model::none)
        return std::nullopt;
    return std::make_pair(model, node->parameters.synth);
}

std::optional<cydaw::engine::drum::Values> NodeGraphComponent::editorDrumValues(const std::uint64_t nodeId) const
{
    const auto* node = findNode(static_cast<NodeId>(nodeId));
    if (node == nullptr || node->typeKey != "batterie")
        return std::nullopt;
    return node->parameters.drum;
}

std::optional<NodeGraphComponent::SamplerCardControls> NodeGraphComponent::editorSamplerControls(const std::uint64_t nodeId) const
{
    const auto* node = findNode(static_cast<NodeId>(nodeId));
    if (node == nullptr || (node->typeKey != "sampler" && node->typeKey != "batterie"))
        return std::nullopt;
    return SamplerCardControls { node->parameters.samplerControllers,
                                 cydaw::engine::samplerValue(node->parameters.sampler, cydaw::engine::SamplerSetting::articulation),
                                 cydaw::engine::samplerValue(node->parameters.sampler, cydaw::engine::SamplerSetting::simple) >= 0.5f };
}

bool NodeGraphComponent::editorSetAlias(const std::uint64_t id, const juce::String& alias)
{
    auto* node = findNode(id);
    if (node == nullptr || editingLocked_)
        return false;
    node->alias = alias.trim().substring(0, 96);
    node->serialised = juce::var();
    notifyLayoutChanged("v2-alias");
    return true;
}

bool NodeGraphComponent::editorSetDescription(const std::uint64_t id, const juce::String& description)
{
    auto* node = findNode(id);
    if (node == nullptr || editingLocked_)
        return false;
    node->description = description.substring(0, 2'000);
    node->serialised = juce::var();
    notifyLayoutChanged("v2-description");
    return true;
}

bool NodeGraphComponent::editorSetBypassed(const std::uint64_t id, const bool bypassed)
{
    auto* node = findNode(id);
    if (node == nullptr || isBoundaryNode(*node) || node->bypassed == bypassed)
        return node != nullptr && node->bypassed == bypassed;
    node->bypassed = bypassed;
    node->serialised = juce::var();
    // Published as a knob is (the shape stays), its undo step taken as one.
    parameterHistoryPending_ = true;
    lastParameterUiChangeTimeMs_ = juce::Time::currentTimeMillis();
    dispatchParameterNotification();
    if (!isTimerRunning())
        startTimerHz(30);
    return true;
}

bool NodeGraphComponent::editorSetCollapsed(const std::uint64_t id, const bool collapsed)
{
    auto* node = findNode(id);
    if (node == nullptr)
        return false;
    node->collapsed = collapsed;
    node->serialised = juce::var();
    notifyLayoutChanged("v2-collapse");
    return true;
}

std::vector<std::uint64_t> NodeGraphComponent::editorDuplicate(const std::vector<std::uint64_t>& ids)
{
    if (editingLocked_)
        return {};
    selectedNodes_.clear();
    for (const auto id : ids)
        if (const auto* node = findNode(id); node != nullptr && !isBoundaryNode(*node))
            selectedNodes_.insert(id);
    if (selectedNodes_.empty())
        return {};
    duplicateSelectedNodes();
    return { selectedNodes_.begin(), selectedNodes_.end() };
}

bool NodeGraphComponent::editorUndo()
{
    if (undoHistory_.empty())
        return false;
    undoGraphEdit();
    return true;
}

bool NodeGraphComponent::editorRedo()
{
    if (redoHistory_.empty())
        return false;
    redoGraphEdit();
    return true;
}

std::uint64_t NodeGraphComponent::editorAddComment(const juce::String& title, const juce::Point<float> position,
                                                   const juce::Point<float> size)
{
    return addCommentForRemote(title, {}, clampedPosition(position), size);
}

bool NodeGraphComponent::editorSetComment(const std::uint64_t id, const juce::String& title,
                                          const juce::Point<float> position, const juce::Point<float> size,
                                          const bool commit)
{
    for (auto& comment : comments_)
        if (comment.id == id) {
            comment.title = title;
            comment.position = clampedPosition(position);
            comment.size = { juce::jmax(0.05f, size.x), juce::jmax(0.05f, size.y) };
            if (commit)
                notifyLayoutChanged("v2-comment");
            return true;
        }
    return false;
}

bool NodeGraphComponent::editorRemoveComment(const std::uint64_t id)
{
    return removeCommentForRemote(id);
}

void NodeGraphComponent::editorArrange()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        setBounds(0, 0, 1'300, 780);
    resetNodeLayout();
}

} // namespace cydaw::app
