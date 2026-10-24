#include "ControllersImpl.h"

#include "ImGuiHost.h"
#include "Studio.h"
#include "UiState.h"
#include "Widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace cydaw::v2 {
namespace {

constexpr std::size_t monitorLength = 400;
constexpr const char* cydawPrefix = "/cydaw/";
using RawKind = Controllers::Impl::RawKind;

// " · " between two names, as a juce::String (inline text is UTF-8).
juce::String middleDot()
{
    return juce::String::fromUTF8(" \xc2\xb7 ");
}

double secondsNow()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

juce::String text(const juce::var& value, const char* key, const juce::String& fallback = {})
{
    return value.hasProperty(key) ? value.getProperty(key, {}).toString() : fallback;
}

double number(const juce::var& value, const char* key, const double fallback)
{
    return value.hasProperty(key) ? static_cast<double>(value.getProperty(key, fallback)) : fallback;
}

juce::var object()
{
    return juce::var(new juce::DynamicObject());
}

void set(juce::var& target, const char* key, const juce::var& value)
{
    if (auto* item = target.getDynamicObject())
        item->setProperty(key, value);
}

juce::var modifierVar(const control::Modifier& modifier)
{
    auto item = object();
    set(item, "kind", control::modifierName(modifier.kind));
    set(item, "a", modifier.a);
    set(item, "b", modifier.b);
    return item;
}

std::optional<control::Modifier> modifierFrom(const juce::var& value)
{
    const auto kind = control::modifierKindNamed(text(value, "kind").toStdString());
    if (!kind)
        return std::nullopt;
    auto modifier = control::defaultModifier(*kind);
    modifier.a = number(value, "a", modifier.a);
    modifier.b = number(value, "b", modifier.b);
    return modifier;
}

std::vector<control::Modifier> chainFrom(const juce::var& value)
{
    std::vector<control::Modifier> chain;
    if (const auto* list = value.getArray())
        for (const auto& item : *list)
            if (const auto modifier = modifierFrom(item))
                chain.push_back(*modifier);
    return chain;
}

juce::var chainVar(const std::vector<control::Modifier>& chain)
{
    juce::Array<juce::var> list;
    for (const auto& modifier : chain)
        list.add(modifierVar(modifier));
    return list;
}

juce::var assignmentVar(const control::Assignment& assignment)
{
    auto item = object();
    set(item, "device", juce::String(assignment.device));
    set(item, "control", juce::String(assignment.control));
    set(item, "target", juce::String(assignment.target));
    set(item, "minimum", assignment.minimum);
    set(item, "maximum", assignment.maximum);
    set(item, "takeover", assignment.takeover);
    return item;
}

std::optional<control::Assignment> assignmentFrom(const juce::var& value, const bool project)
{
    control::Assignment assignment;
    assignment.device = text(value, "device").toStdString();
    assignment.control = text(value, "control").toStdString();
    assignment.target = text(value, "target").toStdString();
    if (assignment.device.empty() || assignment.control.empty() || assignment.target.empty())
        return std::nullopt;
    assignment.minimum = std::clamp(number(value, "minimum", 0.0), 0.0, 1.0);
    assignment.maximum = std::clamp(number(value, "maximum", 1.0), 0.0, 1.0);
    assignment.takeover = static_cast<bool>(value.getProperty("takeover", false));
    assignment.project = project;
    return assignment;
}

const char* rawKindName(const RawKind kind)
{
    switch (kind) {
    case RawKind::midiIn: return "midi-in";
    case RawKind::midiOut: return "midi-out";
    case RawKind::osc: return "osc";
    case RawKind::plugin: return "plugin";
    case RawKind::keyboard: return "keyboard";
    }
    return "plugin";
}

RawKind rawKindNamed(const juce::String& name)
{
    return name == "midi-in" ? RawKind::midiIn : name == "midi-out" ? RawKind::midiOut : name == "osc" ? RawKind::osc
           : name == "keyboard"                                                                          ? RawKind::keyboard
                                                                                                         : RawKind::plugin;
}

// The device most of its controls listen to (or answer), for settings made
// before a device had its own hardware.
std::string mostUsed(const control::VirtualDevice& device, const bool feedback)
{
    std::map<std::string, int> counts;
    for (const auto& control : device.controls)
        if (const auto& id = feedback ? control.feedbackDevice : control.sourceDevice; !id.empty())
            ++counts[id];
    std::string best;
    auto most = 0;
    for (const auto& [id, count] : counts)
        if (count > most)
            best = id, most = count;
    return best;
}

// A new device's name until it is given one ("New device 2").
bool defaultDeviceName(const std::string& name)
{
    const auto text = juce::String::fromUTF8(name.c_str());
    return text.startsWith(juce::String::fromUTF8("Nouveau p\xc3\xa9riph\xc3\xa9rique")) || text.startsWith("New device");
}

} // namespace

// A raw address as a person reads it: "CC 74 · ch. 1", "Note C3 · ch. 10".
juce::String readableAddress(const std::string& address)
{
    const auto parts = juce::StringArray::fromTokens(juce::String(address), "/", "");
    if (parts.size() >= 3 && parts[0] == "cc")
        return "CC " + parts[2] + middleDot() + "ch. " + parts[1];
    if (parts.size() >= 3 && parts[0] == "note")
        return ui::Ls("Note ", "Note ") + juce::MidiMessage::getMidiNoteName(parts[2].getIntValue(), true, true, 3) + middleDot() + "ch. "
               + parts[1];
    if (parts.size() >= 2 && parts[0] == "pb")
        return ui::Ls("Pitch bend · ch. ", "Pitch bend · ch. ") + parts[1];
    if (parts.size() >= 2 && parts[0] == "at")
        return ui::Ls("Pression · ch. ", "Pressure · ch. ") + parts[1];
    if (parts.size() >= 3 && parts[0] == "pc")
        return ui::Ls("Programme ", "Program ") + parts[2] + middleDot() + "ch. " + parts[1];
    if (parts.size() >= 2 && parts[0] == "key")
        return ui::Ls("Touche ", "Key ") + address.substr(4);
    if (parts.size() >= 2 && parts[0] == "button")
        return ui::Ls("Bouton ", "Button ") + parts[1].toUpperCase();
    if (parts.size() >= 2 && parts[0] == "trigger")
        return parts[1] == "left" ? ui::Ls("Gâchette gauche", "Left trigger") : ui::Ls("Gâchette droite", "Right trigger");
    if (parts.size() >= 2 && parts[0] == "axis") {
        const auto& name = parts[1];
        const auto side = name.startsWith("left") ? ui::Ls("Stick gauche ", "Left stick ") : ui::Ls("Stick droit ", "Right stick ");
        return side + name.getLastCharacters(1);
    }
    return juce::String(address);
}

namespace {

// A raw device's id that can go into another id.
std::string slug(const std::string& id)
{
    std::string out;
    for (const auto c : id)
        out += std::isalnum(static_cast<unsigned char>(c)) != 0 ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '-';
    return out;
}

// "MIDIIN2 (box)" and "MIDIOUT2 (box)": the same box.
juce::String withoutDirection(const juce::String& name)
{
    return name.replace("MIDIIN", "MIDI").replace("MIDIOUT", "MIDI").replace(" In", "").replace(" Out", "")
        .replace(" IN", "").replace(" OUT", "").trim();
}

} // namespace

// ---- OSC -----------------------------------------------------------------------------------

struct Controllers::Impl::OscPort final : juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback> {
    OscPort(Impl& owner, std::string device) : owner_(owner), device_(std::move(device)) {}
    ~OscPort() override
    {
        // The receiver's thread stopped first: it may be calling the listeners.
        receiver.disconnect();
        receiver.removeListener(this);
        sender.disconnect();
    }
    void oscMessageReceived(const juce::OSCMessage& message) override { take(message); }
    void oscBundleReceived(const juce::OSCBundle& bundle) override { takeBundle(bundle, 0); }
    // Bundles in bundles, to a depth (a packet can nest thousands).
    void takeBundle(const juce::OSCBundle& bundle, const int depth)
    {
        if (depth > 16)
            return;
        for (const auto& element : bundle) {
            if (element.isMessage())
                take(element.getMessage());
            else if (element.isBundle())
                takeBundle(element.getBundle(), depth + 1);
        }
    }
    // Each number its own address; none: a press and its let go.
    void take(const juce::OSCMessage& message)
    {
        const auto address = message.getAddressPattern().toString().toStdString();
        auto index = 0;
        for (const auto& argument : message) {
            // A controller sends a few numbers per address, not thousands.
            if (index >= 64)
                break;
            std::optional<double> value;
            if (argument.isFloat32())
                value = argument.getFloat32();
            else if (argument.isInt32())
                value = argument.getInt32();
            if (!value)
                continue;
            owner_.post({ Incoming::Kind::message, device_, { index == 0 ? address : address + "#" + std::to_string(index), *value } });
            ++index;
        }
        if (message.isEmpty()) {
            owner_.post({ Incoming::Kind::message, device_, { address, 1.0 } });
            owner_.post({ Incoming::Kind::message, device_, { address, 0.0 } });
        }
    }

    juce::OSCReceiver receiver;
    juce::OSCSender sender;
    bool listening {};
    bool sending {};

private:
    Impl& owner_;
    std::string device_;
};

// ---- Application plug-ins: their thread --------------------------------------------------------

struct Controllers::Impl::PluginThread final : juce::Thread {
    explicit PluginThread(Impl& owner) : juce::Thread("cydaw-control-plugins"), owner_(owner) {}
    void run() override
    {
        while (!threadShouldExit()) {
            std::vector<std::pair<int, std::pair<std::string, control::RawMessage>>> sends;
            {
                const std::scoped_lock hold(lock);
                sends.swap(outgoing);
            }
            for (const auto& [index, send] : sends)
                if (index >= 0 && index < static_cast<int>(owner_.plugins.size())) {
                    const auto& plugin = *owner_.plugins[static_cast<std::size_t>(index)];
                    if (plugin.started && plugin.api->send != nullptr)
                        (void) plugin.api->send(send.first.c_str(), send.second.address.c_str(), send.second.value);
                }
            for (auto& plugin : owner_.plugins)
                if (plugin->started && plugin->api->poll != nullptr)
                    plugin->api->poll();
            wait(10);
        }
    }
    std::mutex lock;
    std::vector<std::pair<int, std::pair<std::string, control::RawMessage>>> outgoing;

private:
    Impl& owner_;
};

namespace {

struct PluginContext {
    Controllers::Impl* impl {};
    int index {};
};
std::vector<std::unique_ptr<PluginContext>> pluginContexts;

std::string pluginDeviceId(const Controllers::Impl& impl, const int index, const char* device)
{
    const auto& plugin = *impl.plugins[static_cast<std::size_t>(index)];
    return "plugin:" + slug(plugin.name.toStdString()) + "/" + std::string(device != nullptr ? device : "");
}

} // namespace

// ---- The whole --------------------------------------------------------------------------------

Controllers::Impl::Impl(Studio& studioIn, UiState& state, ImGuiHost& hostIn, const bool keep)
    : targets(studioIn, state), studio(studioIn), uiState(state), host(hostIn), keepSettings(keep)
{
    restoreGlobal(juce::JSON::parse(studio.settings().getValue("controllers")));
    // The computer's keyboard: always there.
    if (rawDevice(computerKeyboardId) == nullptr) {
        raw.push_back(std::make_unique<RawDevice>());
        raw.back()->id = computerKeyboardId;
        raw.back()->kind = RawKind::keyboard;
    }
    if (auto* keyboard = rawDevice(computerKeyboardId)) {
        keyboard->name = "Computer keyboard";
        keyboard->notes = false;
        keyboard->present = true;
    }
    scanMidi();
    loadPlugins();
    restoreProject(studio.controllerAssignments());
    projectRevision = studio.controllerAssignmentsRevision();
    studio.engine().setMidiInputGate([this](const juce::String& identifier, const juce::MidiMessage& message) {
        return gatePasses(identifier, message);
    });
    refreshGate();
    startTimerHz(30);
}

Controllers::Impl::~Impl()
{
    stopTimer();
    cancelPendingUpdate();
    studio.engine().setMidiInputGate(nullptr);
    if (globalDirty)
        saveNow();
    auto& manager = studio.engine().deviceManager();
    for (const auto& device : raw)
        if (device->kind == RawKind::midiIn && device->identifier.isNotEmpty())
            manager.removeMidiInputDeviceCallback(device->identifier, this);
    unloadPlugins();
    for (auto& device : raw)
        closeRaw(*device);
}

Controllers::Impl::RawDevice* Controllers::Impl::rawDevice(const std::string_view id)
{
    for (auto& device : raw)
        if (device->id == id)
            return device.get();
    return nullptr;
}

const Controllers::Impl::RawDevice* Controllers::Impl::rawDevice(const std::string_view id) const
{
    for (const auto& device : raw)
        if (device->id == id)
            return device.get();
    return nullptr;
}

// ---- MIDI -------------------------------------------------------------------------------------

void Controllers::Impl::scanMidi()
{
    auto& manager = studio.engine().deviceManager();
    for (auto& device : raw)
        if (device->kind == RawKind::midiIn || device->kind == RawKind::midiOut)
            device->present = false;
    const auto list = [&](const juce::Array<juce::MidiDeviceInfo>& devices, const RawKind kind) {
        std::map<juce::String, int> seen;
        for (const auto& info : devices) {
            auto name = info.name;
            if (const auto count = ++seen[info.name]; count > 1)
                name << " #" << count;
            const auto id = (kind == RawKind::midiIn ? "midi-in:" : "midi-out:") + name.toStdString();
            auto* device = rawDevice(id);
            if (device == nullptr) {
                raw.push_back(std::make_unique<RawDevice>());
                device = raw.back().get();
                device->id = id;
                device->kind = kind;
                device->name = name;
            }
            device->present = true;
            if (device->identifier != info.identifier) {
                if (kind == RawKind::midiIn && device->identifier.isNotEmpty())
                    manager.removeMidiInputDeviceCallback(device->identifier, this);
                device->identifier = info.identifier;
                if (kind == RawKind::midiIn)
                    manager.addMidiInputDeviceCallback(device->identifier, this);
            }
            // An input is open while the device manager has it on (the
            // MIDI inputs' tab, the transport sync share it).
            if (kind == RawKind::midiIn)
                device->enabled = manager.isMidiInputDeviceEnabled(device->identifier);
        }
    };
    list(juce::MidiInput::getAvailableDevices(), RawKind::midiIn);
    list(juce::MidiOutput::getAvailableDevices(), RawKind::midiOut);
    {
        const std::scoped_lock hold(gateLock);
        midiNames.clear();
        for (const auto& device : raw)
            if (device->kind == RawKind::midiIn && device->present)
                midiNames[device->identifier] = device->id;
    }
    for (auto& device : raw)
        if (device->kind == RawKind::midiOut && !device->present)
            device->output.reset();
}

void Controllers::Impl::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message)
{
    if (source == nullptr)
        return;
    std::string id;
    {
        const std::scoped_lock hold(gateLock);
        const auto found = midiNames.find(source->getIdentifier());
        if (found == midiNames.end())
            return;
        id = found->second;
    }
    if (const auto read = control::fromMidi(message.getRawData(), static_cast<std::size_t>(message.getRawDataSize())))
        post({ Incoming::Kind::message, id, *read });
}

void Controllers::Impl::refreshGate()
{
    std::set<std::string> silent;
    for (const auto& device : raw)
        if (device->kind == RawKind::midiIn && !device->notes)
            silent.insert(device->id);
    std::set<std::pair<std::string, std::string>> taken;
    for (const auto& assignment : router.assignments)
        if (const auto* control = router.findControl(assignment.device, assignment.control))
            if (control->sourceDevice.rfind("midi-in:", 0) == 0)
                taken.insert({ control->sourceDevice, control->sourceAddress });
    const std::scoped_lock hold(gateLock);
    silentDevices = std::move(silent);
    takenAddresses = std::move(taken);
}

bool Controllers::Impl::gatePasses(const juce::String& identifier, const juce::MidiMessage& message)
{
    const std::scoped_lock hold(gateLock);
    const auto found = midiNames.find(identifier);
    if (found == midiNames.end())
        return true;
    if (silentDevices.contains(found->second))
        return false;
    if (takenAddresses.empty())
        return true;
    const auto read = control::fromMidi(message.getRawData(), static_cast<std::size_t>(message.getRawDataSize()));
    return !read || !takenAddresses.contains({ found->second, read->address });
}

// ---- Opening, closing, sending -------------------------------------------------------------------

void Controllers::Impl::applyRaw(RawDevice& device)
{
    device.error.clear();
    if (device.kind == RawKind::midiIn) {
        if (device.present)
            studio.engine().deviceManager().setMidiInputDeviceEnabled(device.identifier, device.enabled);
    } else if (device.kind == RawKind::midiOut) {
        if (!device.enabled)
            device.output.reset();
    } else if (device.kind == RawKind::osc) {
        device.osc.reset();
        device.present = false;
        if (!device.enabled)
            return;
        auto port = std::make_unique<OscPort>(*this, device.id);
        if (device.listenPort > 0) {
            port->listening = port->receiver.connect(device.listenPort);
            if (port->listening)
                port->receiver.addListener(port.get());
            else
                device.error = ui::Ls("Le port ", "Port ") + juce::String(device.listenPort)
                               + ui::Ls(" est déjà pris.", " is already taken.");
        }
        if (device.sendPort > 0)
            port->sending = port->sender.connect(device.sendHost, device.sendPort);
        device.present = port->listening || port->sending;
        device.osc = std::move(port);
    }
}

void Controllers::Impl::closeRaw(RawDevice& device)
{
    device.output.reset();
    device.osc.reset();
}

Controllers::Impl::RawDevice& Controllers::Impl::addOsc(const juce::String& name, const int listenPort, const juce::String& sendHost,
                                                        const int sendPort)
{
    std::vector<std::string> taken;
    for (const auto& device : raw)
        taken.push_back(device->id);
    raw.push_back(std::make_unique<RawDevice>());
    auto& device = *raw.back();
    device.id = control::Router::freshId("osc", taken);
    device.kind = RawKind::osc;
    device.name = name.isNotEmpty() ? name : juce::String("OSC");
    device.listenPort = listenPort;
    device.sendHost = sendHost.isNotEmpty() ? sendHost : juce::String("127.0.0.1");
    device.sendPort = sendPort;
    applyRaw(device);
    changed(true, false);
    return device;
}

void Controllers::Impl::removeRaw(const std::string_view id)
{
    for (auto it = raw.begin(); it != raw.end(); ++it)
        if ((*it)->id == id) {
            // Only what CyDAW made can go: a port or a plug-in's device is
            // there as long as it is plugged in.
            if ((*it)->kind != RawKind::osc && (*it)->present)
                return;
            closeRaw(**it);
            raw.erase(it);
            changed(true, false);
            return;
        }
}

juce::String Controllers::Impl::nameOf(const std::string_view id) const
{
    if (id == computerKeyboardId)
        return ui::Ls("Clavier de l'ordinateur", "Computer keyboard");
    if (const auto* device = rawDevice(id))
        return device->name;
    return juce::String(std::string(id));
}

juce::String Controllers::Impl::familyOf(const RawDevice& device) const
{
    switch (device.kind) {
    case RawKind::midiIn:
    case RawKind::midiOut: return "MIDI";
    case RawKind::osc: return ui::Ls("OSC (r\xc3\xa9seau)", "OSC (network)");
    case RawKind::keyboard: return ui::Ls("Cet ordinateur", "This computer");
    case RawKind::plugin: break;
    }
    if (device.plugin >= 0 && device.plugin < static_cast<int>(plugins.size()))
        return plugins[static_cast<std::size_t>(device.plugin)]->name;
    return ui::Ls("Plug-ins d'application", "Application plug-ins");
}

std::string Controllers::Impl::feedbackFor(const std::string_view inputDevice) const
{
    const auto* input = rawDevice(inputDevice);
    if (input == nullptr)
        return {};
    if (input->kind == RawKind::osc)
        return input->sendPort > 0 ? input->id : std::string();
    if (input->kind == RawKind::plugin)
        return input->id;
    if (input->kind == RawKind::midiIn) {
        if (const auto* same = rawDevice("midi-out:" + input->name.toStdString()))
            return same->id;
        for (const auto& device : raw)
            if (device->kind == RawKind::midiOut && withoutDirection(device->name) == withoutDirection(input->name))
                return device->id;
    }
    return {};
}

void Controllers::Impl::send(const std::string_view id, const control::RawMessage& message)
{
    auto* device = rawDevice(id);
    if (device == nullptr || !device->enabled)
        return;
    if (device->kind == RawKind::midiOut) {
        if (!device->present)
            return;
        if (device->output == nullptr && device->error.isEmpty()) {
            device->output = juce::MidiOutput::openDevice(device->identifier);
            if (device->output == nullptr)
                device->error = ui::Ls("Port occupé par un autre logiciel.", "Port busy in another program.");
        }
        const auto bytes = control::toMidi(message);
        if (device->output == nullptr || bytes.empty())
            return;
        device->output->sendMessageNow(juce::MidiMessage(bytes.data(), static_cast<int>(bytes.size())));
    } else if (device->kind == RawKind::osc) {
        if (device->osc == nullptr || !device->osc->sending)
            return;
        // A second number ("#1") is not said back on its own.
        if (message.address.empty() || message.address[0] != '/' || message.address.find('#') != std::string::npos)
            return;
        try {
            (void) device->osc->sender.send(juce::OSCAddressPattern(juce::String(message.address)), static_cast<float>(message.value));
        } catch (const juce::OSCFormatError&) {
            return;
        }
    } else if (device->kind == RawKind::plugin) {
        if (pluginThread == nullptr || device->plugin < 0)
            return;
        const std::scoped_lock hold(pluginThread->lock);
        pluginThread->outgoing.push_back({ device->plugin, { device->pluginDevice, message } });
    } else {
        return;
    }
    device->activity = secondsNow();
    see(true, device->id, message);
}

// ---- Application plug-ins ------------------------------------------------------------------------

juce::Array<juce::File> Controllers::Impl::pluginFolders()
{
    return { juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("ControlPlugins"),
             juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("CyDAW").getChildFile("Control Plugins") };
}

void Controllers::Impl::loadPlugins()
{
    pluginHost.device_added = [](void* context, const char* id, const char* name) {
        const auto* c = static_cast<PluginContext*>(context);
        Incoming incoming;
        incoming.kind = Incoming::Kind::added;
        incoming.device = id != nullptr ? id : "";
        incoming.name = juce::String::fromUTF8(name != nullptr ? name : id);
        incoming.plugin = c->index;
        c->impl->post(std::move(incoming));
    };
    pluginHost.device_removed = [](void* context, const char* id) {
        const auto* c = static_cast<PluginContext*>(context);
        Incoming incoming;
        incoming.kind = Incoming::Kind::removed;
        incoming.device = id != nullptr ? id : "";
        incoming.plugin = c->index;
        c->impl->post(std::move(incoming));
    };
    pluginHost.message = [](void* context, const char* device, const char* address, const double value) {
        const auto* c = static_cast<PluginContext*>(context);
        if (device == nullptr || address == nullptr || !std::isfinite(value))
            return;
        c->impl->post({ Incoming::Kind::message, pluginDeviceId(*c->impl, c->index, device), { address, value } });
    };
    pluginHost.log = [](void* context, const char* line) {
        const auto* c = static_cast<PluginContext*>(context);
        if (line != nullptr)
            c->impl->post({ Incoming::Kind::message, "log", { std::string("log: ") + line, 0.0 }, {}, c->index });
    };
    for (const auto& folder : pluginFolders())
        for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.cyctl")) {
            auto plugin = std::make_unique<Plugin>();
            plugin->file = file;
            plugin->name = file.getFileNameWithoutExtension();
            plugin->library = std::make_unique<juce::DynamicLibrary>();
            if (!plugin->library->open(file.getFullPathName())) {
                plugin->error = ui::Ls("Bibliothèque illisible", "Library cannot be loaded");
            } else if (auto* entry = reinterpret_cast<CyControlPluginEntry>(plugin->library->getFunction(CYDAW_CONTROL_PLUGIN_ENTRY));
                       entry == nullptr) {
                plugin->error = ui::Ls("Pas un plug-in CyDAW", "Not a CyDAW plug-in");
            } else if (plugin->api = entry(); plugin->api == nullptr || plugin->api->abi_version != CYDAW_CONTROL_ABI_VERSION
                                              || plugin->api->start == nullptr) {
                plugin->error = ui::Ls("Version d'interface inconnue", "Unknown interface version");
                plugin->api = nullptr;
            } else if (plugin->api->name != nullptr) {
                plugin->name = juce::String::fromUTF8(plugin->api->name);
            }
            plugins.push_back(std::move(plugin));
        }
    for (std::size_t index = 0; index < plugins.size(); ++index) {
        auto& plugin = *plugins[index];
        if (plugin.api == nullptr)
            continue;
        pluginContexts.push_back(std::make_unique<PluginContext>(PluginContext { this, static_cast<int>(index) }));
        auto host = pluginHost;
        host.context = pluginContexts.back().get();
        // The host table must outlive the call: the plug-in may keep it.
        static std::vector<std::unique_ptr<CyControlHost>> hosts;
        hosts.push_back(std::make_unique<CyControlHost>(host));
        plugin.started = plugin.api->start(hosts.back().get()) != 0;
        if (!plugin.started)
            plugin.error = ui::Ls("N'a pas démarré", "Did not start");
    }
    if (std::any_of(plugins.begin(), plugins.end(), [](const auto& p) { return p->started; })) {
        pluginThread = std::make_unique<PluginThread>(*this);
        pluginThread->startThread();
    }
}

void Controllers::Impl::unloadPlugins()
{
    if (pluginThread != nullptr)
        pluginThread->stopThread(2'000);
    pluginThread.reset();
    for (auto& plugin : plugins)
        if (plugin->started && plugin->api->stop != nullptr)
            plugin->api->stop();
    plugins.clear();
    pluginContexts.clear();
}

// ---- What the devices heard ----------------------------------------------------------------------

void Controllers::Impl::post(Incoming incoming)
{
    {
        const std::scoped_lock hold(queueLock);
        // Flooded: the oldest half goes at once (not one by one, each time).
        if (queue.size() > 4'096)
            queue.erase(queue.begin(), queue.begin() + 2'048);
        queue.push_back(std::move(incoming));
    }
    triggerAsyncUpdate();
}

void Controllers::Impl::handleAsyncUpdate()
{
    std::vector<Incoming> taken;
    {
        const std::scoped_lock hold(queueLock);
        taken.swap(queue);
    }
    for (const auto& incoming : taken)
        process(incoming);
    if (!taken.empty())
        host.wake(4);
}

void Controllers::Impl::process(const Incoming& incoming)
{
    if (incoming.kind == Incoming::Kind::added || incoming.kind == Incoming::Kind::removed) {
        const auto id = pluginDeviceId(*this, incoming.plugin, incoming.device.c_str());
        auto* device = rawDevice(id);
        if (device == nullptr && incoming.kind == Incoming::Kind::added) {
            raw.push_back(std::make_unique<RawDevice>());
            device = raw.back().get();
            device->id = id;
            device->kind = RawKind::plugin;
        }
        if (device != nullptr) {
            if (incoming.kind == Incoming::Kind::added)
                device->name = incoming.name;
            device->plugin = incoming.plugin;
            device->pluginDevice = incoming.device;
            device->present = incoming.kind == Incoming::Kind::added;
        }
        return;
    }
    if (incoming.device == "log") {
        see(false, "log", incoming.message);
        return;
    }
    auto* device = rawDevice(incoming.device);
    if (device == nullptr || !device->enabled)
        return;
    device->activity = secondsNow();
    see(false, device->id, incoming.message);
    const auto& message = incoming.message;
    // OSC's own addresses: /cydaw/<target> moves it, without a virtual control.
    if (device->kind == RawKind::osc && device->cydawAddresses && message.address.rfind(cydawPrefix, 0) == 0) {
        const auto key = message.address.substr(std::string(cydawPrefix).size());
        if (!targets.describe(key))
            return;
        static std::map<std::string, double> previous;
        const auto before = previous.contains(key) ? previous[key] : 0.0;
        previous[key] = message.value;
        device->touched.insert(key);
        if (targets.apply(key, message.value, before < 0.5 && message.value >= 0.5, before >= 0.5 && message.value < 0.5, false))
            ordered[key] = { std::clamp(message.value, 0.0, 1.0), secondsNow() };
        return;
    }
    if (learnDevice(*device, message))
        return;
    if (learnSource || (learnMode && !learnTarget.empty())) {
        learnFrom(device->id, message);
        return;
    }
    const auto moved = router.receive(device->id, message);
    if (moved.empty())
        return;
    applyOrders(router.orders(moved, [this](const std::string& target) { return targets.value(target); }));
}

void Controllers::Impl::applyOrders(const std::vector<control::Router::Order>& orders)
{
    for (const auto& order : orders)
        if (targets.apply(order.target, order.value, order.pressed, order.released, order.momentary))
            if (const auto now = targets.value(order.target))
                ordered[order.target] = { *now, secondsNow() };
}

void Controllers::Impl::see(const bool out, const std::string& device, const control::RawMessage& message)
{
    if (monitorPaused)
        return;
    monitor.push_back({ secondsNow(), out, device, message });
    while (monitor.size() > monitorLength)
        monitor.pop_front();
}

// ---- Feedback --------------------------------------------------------------------------------------

void Controllers::Impl::timerCallback()
{
    // A project opened or made: its assignments.
    if (studio.controllerAssignmentsRevision() != projectRevision) {
        projectRevision = studio.controllerAssignmentsRevision();
        restoreProject(studio.controllerAssignments());
        told.clear();
    }
    static int ticks = 0;
    if (++ticks % 90 == 0)
        scanMidi();
    if (globalDirty && ticks % 30 == 0)
        saveNow();
    feedback(false);
}

void Controllers::Impl::feedback(const bool force)
{
    std::set<std::string> keys;
    for (const auto& assignment : router.assignments)
        keys.insert(assignment.target);
    for (const auto& device : raw)
        if (device->kind == RawKind::osc)
            keys.insert(device->touched.begin(), device->touched.end());
    const auto now = secondsNow();
    for (const auto& key : keys) {
        const auto value = targets.value(key);
        if (!value)
            continue;
        const auto last = told.find(key);
        if (!force && last != told.end() && std::abs(last->second - *value) < 1.0e-4)
            continue;
        told[key] = *value;
        const auto mine = ordered.find(key);
        const auto byThis = mine != ordered.end() && std::abs(mine->second.first - *value) < 0.01 && now - mine->second.second < 1.0;
        // Seen elsewhere: what this router ordered is no echo to wait for any
        // more (back at that value later, it was moved by something else).
        if (mine != ordered.end() && !byThis)
            ordered.erase(mine);
        const auto answers = router.targetMoved(key, *value, byThis);
        if (byThis && !force)
            continue;
        for (const auto& answer : answers)
            send(answer.device, answer.message);
        for (const auto& device : raw)
            if (device->kind == RawKind::osc && device->touched.contains(key))
                send(device->id, { cydawPrefix + key, *value });
    }
}

// ---- Virtual controllers and assignments --------------------------------------------------------------

std::string Controllers::Impl::addVirtualDevice(const juce::String& name, const std::string& input, const std::string& output)
{
    std::vector<std::string> taken;
    std::set<std::string> names;
    for (const auto& device : router.devices) {
        taken.push_back(device.id);
        names.insert(device.name);
    }
    control::VirtualDevice device;
    device.id = control::Router::freshId("virtual", taken);
    if (name.isNotEmpty()) {
        device.name = name.toStdString();
    } else {
        // Named after its hardware, or "New device"; a name of its own ("… 2").
        const auto base = input.empty() ? ui::Ls("Nouveau p\xc3\xa9riph\xc3\xa9rique", "New device") : nameOf(input);
        auto unique = base;
        for (auto number = 2; names.contains(unique.toStdString()); ++number)
            unique = base + " " + juce::String(number);
        device.name = unique.toStdString();
    }
    device.input = input;
    device.output = output.empty() && !input.empty() ? feedbackFor(input) : output;
    router.devices.push_back(device);
    changed(true, false);
    return device.id;
}

std::string Controllers::Impl::duplicateVirtualDevice(const std::string& id)
{
    const auto* original = router.findDevice(id);
    if (original == nullptr)
        return {};
    std::vector<std::string> taken;
    for (const auto& device : router.devices)
        taken.push_back(device.id);
    // The copy waits for hardware of its own (another box of the same kind):
    // until then its controls hear nothing.
    auto copy = *original;
    copy.id = control::Router::freshId("virtual", taken);
    copy.name = (juce::String::fromUTF8(original->name.c_str()) + ui::Ls(" (copie)", " (copy)")).toStdString();
    for (auto& control : copy.controls) {
        if (control.sourceDevice == copy.input)
            control.sourceDevice.clear();
        if (control.feedbackDevice == copy.output)
            control.feedbackDevice.clear();
    }
    copy.input.clear();
    copy.output.clear();
    std::vector<control::Assignment> assignments;
    for (const auto& assignment : router.assignments)
        if (assignment.device == id) {
            auto again = assignment;
            again.device = copy.id;
            assignments.push_back(again);
        }
    const auto project = std::any_of(assignments.begin(), assignments.end(), [](const auto& a) { return a.project; });
    router.devices.push_back(std::move(copy));
    router.assignments.insert(router.assignments.end(), assignments.begin(), assignments.end());
    changed(true, project);
    return router.devices.back().id;
}

void Controllers::Impl::setDeviceInput(control::VirtualDevice& device, const std::string& input)
{
    const auto before = device.input;
    for (auto& control : device.controls)
        if (control.sourceDevice == before || control.sourceDevice.empty())
            control.sourceDevice = input;
    device.input = input;
    // The output that answered the old input answers the new one.
    if (device.output.empty() || device.output == feedbackFor(before))
        setDeviceOutput(device, feedbackFor(input));
    changed(true, false);
}

void Controllers::Impl::setDeviceOutput(control::VirtualDevice& device, const std::string& output)
{
    for (auto& control : device.controls)
        if (control.feedbackDevice == device.output || control.feedbackDevice.empty()) {
            control.feedbackDevice = output;
            if (output.empty())
                control.feedbackAddress.clear();
            else if (control.feedbackAddress.empty())
                control.feedbackAddress = control.sourceAddress;
        }
    device.output = output;
    changed(true, false);
}

std::string Controllers::Impl::addControl(const std::string& deviceId, const juce::String& name, const control::ControlKind kind,
                                          const std::string& sourceDevice, const std::string& sourceAddress)
{
    auto* device = router.findDevice(deviceId);
    if (device == nullptr)
        return {};
    std::vector<std::string> taken;
    for (const auto& control : device->controls)
        taken.push_back(control.id);
    // A control listens to its device's hardware; a device without any
    // takes the first control's source as its own.
    const auto source = sourceDevice.empty() ? device->input : sourceDevice;
    if (device->input.empty() && !source.empty()) {
        device->input = source;
        if (device->output.empty())
            device->output = feedbackFor(source);
    }
    control::VirtualControl control;
    control.id = control::Router::freshId(control::controlKindName(kind), taken);
    control.kind = kind;
    control.chain = control::defaultChain(kind);
    control.sourceDevice = source;
    control.sourceAddress = sourceAddress;
    control.feedbackDevice = source == device->input ? device->output : feedbackFor(source);
    if (!control.feedbackDevice.empty())
        control.feedbackAddress = sourceAddress;
    control.name = (name.isNotEmpty() ? name
                    : sourceAddress.empty() ? juce::String(control.id)
                                            : readableAddress(sourceAddress)).toStdString();
    device->controls.push_back(control);
    changed(true, false);
    return control.id;
}

void Controllers::Impl::assign(const std::string& device, const std::string& control, const std::string& target, const bool replace)
{
    auto& list = router.assignments;
    const auto hadProject = std::any_of(list.begin(), list.end(), [&](const auto& a) { return a.target == target && a.project; });
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const auto& a) {
                                  return a.target == target && (replace || (a.device == device && a.control == control));
                              }),
               list.end());
    control::Assignment assignment;
    assignment.device = device;
    assignment.control = control;
    assignment.target = target;
    const auto info = targets.describe(target);
    assignment.project = info && info->project;
    list.push_back(assignment);
    told.erase(target);
    changed(!assignment.project || !hadProject, assignment.project || hadProject);
}

void Controllers::Impl::unassign(const std::size_t index)
{
    if (index >= router.assignments.size())
        return;
    const auto project = router.assignments[index].project;
    router.assignments.erase(router.assignments.begin() + static_cast<std::ptrdiff_t>(index));
    changed(!project, project);
}

void Controllers::Impl::removeVirtualDevice(const std::string& id)
{
    if (learnHardware == id || learnControls == id)
        stopLearning();
    auto& list = router.assignments;
    const auto project = std::any_of(list.begin(), list.end(), [&](const auto& a) { return a.device == id && a.project; });
    list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& a) { return a.device == id; }), list.end());
    auto& devices = router.devices;
    devices.erase(std::remove_if(devices.begin(), devices.end(), [&](const auto& d) { return d.id == id; }), devices.end());
    changed(true, project);
}

void Controllers::Impl::removeControl(const std::string& deviceId, const std::string& controlId)
{
    auto& list = router.assignments;
    const auto project = std::any_of(list.begin(), list.end(),
                                     [&](const auto& a) { return a.device == deviceId && a.control == controlId && a.project; });
    list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& a) { return a.device == deviceId && a.control == controlId; }),
               list.end());
    if (auto* device = router.findDevice(deviceId))
        device->controls.erase(std::remove_if(device->controls.begin(), device->controls.end(),
                                              [&](const auto& c) { return c.id == controlId; }),
                               device->controls.end());
    changed(true, project);
}

std::string Controllers::Impl::virtualDeviceFor(const RawDevice& device)
{
    for (const auto& made : router.devices)
        if (made.input == device.id)
            return made.id;
    return addVirtualDevice({}, device.id, {});
}

// ---- Learning ----------------------------------------------------------------------------------------

void Controllers::Impl::learnFrom(const std::string& rawId, const control::RawMessage& message)
{
    // A key let go teaches nothing (its press did).
    if (control::isPressAddress(message.address) && message.value <= 0.0)
        return;
    const auto* device = rawDevice(rawId);
    if (device == nullptr)
        return;
    if (learnSource) {
        if (auto* control = router.findControl(learnSource->first, learnSource->second)) {
            control->sourceDevice = rawId;
            control->sourceAddress = message.address;
            if (control->feedbackDevice.empty()) {
                control->feedbackDevice = feedbackFor(rawId);
                control->feedbackAddress = control->feedbackDevice.empty() ? std::string() : message.address;
            }
            learnStatus = juce::String::fromUTF8(control->name.c_str()) + juce::String::fromUTF8(" \xe2\x86\x90 ") + nameOf(rawId)
                          + middleDot() + readableAddress(message.address);
            changed(true, false);
        }
        learnSource.reset();
        return;
    }
    // The virtual control that raw address feeds, or a new one for it.
    std::string deviceId;
    std::string controlId;
    if (const auto fed = router.controlFedBy(rawId, message.address)) {
        deviceId = router.devices[fed->first].id;
        controlId = router.devices[fed->first].controls[fed->second].id;
    } else {
        deviceId = virtualDeviceFor(*device);
        controlId = addControl(deviceId, {}, control::kindForAddress(message.address), rawId, message.address);
    }
    assign(deviceId, controlId, learnTarget, true);
    if (auto* control = router.findControl(deviceId, controlId))
        learnStatus = learnTargetLabel + juce::String::fromUTF8(" \xe2\x86\x90 ") + juce::String::fromUTF8(control->name.c_str());
    selectedDevice = deviceId;
    selectedControl = controlId;
    learnTarget.clear();
    learnTargetLabel.clear();
    refreshGate();
}

bool Controllers::Impl::learnDevice(const RawDevice& source, const control::RawMessage& message)
{
    const auto letGo = control::isPressAddress(message.address) && message.value <= 0.0;
    if (!learnHardware.empty()) {
        if (!isInput(source.kind))
            return false;
        if (letGo)
            return true;
        if (auto* device = router.findDevice(learnHardware)) {
            setDeviceInput(*device, source.id);
            // A new device is named after what it is.
            if (device->controls.empty() && defaultDeviceName(device->name)) {
                std::set<std::string> names;
                for (const auto& other : router.devices)
                    if (other.id != device->id)
                        names.insert(other.name);
                const auto base = nameOf(source.id);
                auto unique = base;
                for (auto number = 2; names.contains(unique.toStdString()); ++number)
                    unique = base + " " + juce::String(number);
                device->name = unique.toStdString();
            }
            learnStatus = juce::String::fromUTF8(device->name.c_str()) + juce::String::fromUTF8(" \xe2\x86\x90 ") + nameOf(source.id);
        }
        learnHardware.clear();
        return true;
    }
    if (!learnControls.empty()) {
        auto* device = router.findDevice(learnControls);
        if (device == nullptr) {
            learnControls.clear();
            return false;
        }
        if (device->input != source.id || letGo)
            return false;
        const auto known = std::any_of(device->controls.begin(), device->controls.end(), [&](const auto& control) {
            return control.sourceDevice == source.id && control.sourceAddress == message.address;
        });
        if (!known) {
            selectedDevice = device->id;
            selectedControl = addControl(device->id, {}, control::kindForAddress(message.address), source.id, message.address);
            learnStatus = juce::String::fromUTF8(device->name.c_str()) + ui::Ls(" : nouvelle r\xc3\xa8gle ", ": new rule ")
                          + readableAddress(message.address);
        }
        // And it moves what it already moves.
        return false;
    }
    return false;
}

void Controllers::Impl::stopLearning()
{
    learnHardware.clear();
    learnControls.clear();
    learnSource.reset();
    pickTarget.reset();
}

// ---- Files ---------------------------------------------------------------------------------------------

juce::var Controllers::Impl::globalState() const
{
    auto state = object();
    set(state, "version", 1);
    juce::Array<juce::var> devices;
    for (const auto& device : raw) {
        auto item = object();
        set(item, "id", juce::String(device->id));
        set(item, "name", device->name);
        set(item, "kind", rawKindName(device->kind));
        set(item, "enabled", device->enabled);
        set(item, "notes", device->notes);
        if (device->kind == RawKind::osc) {
            set(item, "listenPort", device->listenPort);
            set(item, "sendHost", device->sendHost);
            set(item, "sendPort", device->sendPort);
            set(item, "cydawAddresses", device->cydawAddresses);
        }
        devices.add(item);
    }
    set(state, "raw", devices);
    juce::Array<juce::var> virtualDevices;
    for (const auto& device : router.devices) {
        auto item = object();
        set(item, "id", juce::String(device.id));
        set(item, "name", juce::String(device.name));
        set(item, "input", juce::String(device.input));
        set(item, "output", juce::String(device.output));
        juce::Array<juce::var> controls;
        for (const auto& control : device.controls) {
            auto entry = object();
            set(entry, "id", juce::String(control.id));
            set(entry, "name", juce::String(control.name));
            set(entry, "kind", control::controlKindName(control.kind));
            set(entry, "sourceDevice", juce::String(control.sourceDevice));
            set(entry, "sourceAddress", juce::String(control.sourceAddress));
            set(entry, "feedbackDevice", juce::String(control.feedbackDevice));
            set(entry, "feedbackAddress", juce::String(control.feedbackAddress));
            set(entry, "chain", chainVar(control.chain));
            controls.add(entry);
        }
        set(item, "controls", controls);
        virtualDevices.add(item);
    }
    set(state, "virtual", virtualDevices);
    juce::Array<juce::var> assignments;
    for (const auto& assignment : router.assignments)
        if (!assignment.project)
            assignments.add(assignmentVar(assignment));
    set(state, "assignments", assignments);
    return state;
}

void Controllers::Impl::restoreGlobal(const juce::var& state)
{
    if (!state.isObject())
        return;
    if (const auto* list = state.getProperty("raw", {}).getArray())
        for (const auto& item : *list) {
            const auto id = text(item, "id").toStdString();
            const auto kind = text(item, "kind");
            if (id.empty() || rawDevice(id) != nullptr)
                continue;
            raw.push_back(std::make_unique<RawDevice>());
            auto& device = *raw.back();
            device.id = id;
            device.name = text(item, "name", juce::String(id));
            device.kind = rawKindNamed(kind);
            device.enabled = static_cast<bool>(item.getProperty("enabled", true));
            device.notes = static_cast<bool>(item.getProperty("notes", true));
            if (device.kind == RawKind::osc) {
                device.listenPort = static_cast<int>(number(item, "listenPort", 0));
                device.sendHost = text(item, "sendHost", "127.0.0.1");
                device.sendPort = static_cast<int>(number(item, "sendPort", 0));
                device.cydawAddresses = static_cast<bool>(item.getProperty("cydawAddresses", true));
                applyRaw(device);
            }
        }
    if (const auto* list = state.getProperty("virtual", {}).getArray())
        for (const auto& item : *list) {
            control::VirtualDevice device;
            device.id = text(item, "id").toStdString();
            device.name = text(item, "name").toStdString();
            device.input = text(item, "input").toStdString();
            device.output = text(item, "output").toStdString();
            if (device.id.empty() || router.findDevice(device.id) != nullptr)
                continue;
            if (const auto* controls = item.getProperty("controls", {}).getArray())
                for (const auto& entry : *controls) {
                    control::VirtualControl control;
                    control.id = text(entry, "id").toStdString();
                    if (control.id.empty())
                        continue;
                    control.name = text(entry, "name", juce::String(control.id)).toStdString();
                    control.kind = control::controlKindNamed(text(entry, "kind").toStdString()).value_or(control::ControlKind::knob);
                    control.sourceDevice = text(entry, "sourceDevice").toStdString();
                    control.sourceAddress = text(entry, "sourceAddress").toStdString();
                    control.feedbackDevice = text(entry, "feedbackDevice").toStdString();
                    control.feedbackAddress = text(entry, "feedbackAddress").toStdString();
                    control.chain = chainFrom(entry.getProperty("chain", {}));
                    device.controls.push_back(control);
                }
            // Settings made before a device had its own hardware.
            if (!item.hasProperty("input")) {
                device.input = mostUsed(device, false);
                device.output = mostUsed(device, true);
            }
            router.devices.push_back(device);
        }
    if (const auto* list = state.getProperty("assignments", {}).getArray())
        for (const auto& item : *list)
            if (const auto assignment = assignmentFrom(item, false))
                router.assignments.push_back(*assignment);
}

juce::var Controllers::Impl::projectAssignments() const
{
    juce::Array<juce::var> list;
    for (const auto& assignment : router.assignments)
        if (assignment.project)
            list.add(assignmentVar(assignment));
    return list;
}

void Controllers::Impl::restoreProject(const juce::var& assignments)
{
    auto& list = router.assignments;
    list.erase(std::remove_if(list.begin(), list.end(), [](const auto& a) { return a.project; }), list.end());
    if (const auto* saved = assignments.getArray())
        for (const auto& item : *saved)
            if (const auto assignment = assignmentFrom(item, true))
                list.push_back(*assignment);
    refreshGate();
}

void Controllers::Impl::changed(const bool global, const bool project)
{
    if (global)
        globalDirty = true;
    if (project)
        studio.setControllerAssignments(projectAssignments(), false);
    refreshGate();
    host.wake(3);
}

void Controllers::Impl::saveNow()
{
    globalDirty = false;
    if (!keepSettings)
        return;
    studio.settings().setValue("controllers", juce::JSON::toString(globalState(), true));
    studio.settings().saveIfNeeded();
}

// ---- The control port --------------------------------------------------------------------------------

juce::var Controllers::Impl::describe() const
{
    auto result = object();
    juce::Array<juce::var> devices;
    for (const auto& device : raw) {
        auto item = object();
        set(item, "id", juce::String(device->id));
        set(item, "name", device->name);
        set(item, "kind", rawKindName(device->kind));
        set(item, "family", familyOf(*device));
        set(item, "enabled", device->enabled);
        set(item, "present", device->present);
        set(item, "notes", device->notes);
        if (device->kind == RawKind::osc) {
            set(item, "listenPort", device->listenPort);
            set(item, "sendHost", device->sendHost);
            set(item, "sendPort", device->sendPort);
            set(item, "listening", device->osc != nullptr && device->osc->listening);
            set(item, "sending", device->osc != nullptr && device->osc->sending);
            set(item, "cydawAddresses", device->cydawAddresses);
        }
        if (device->error.isNotEmpty())
            set(item, "error", device->error);
        devices.add(item);
    }
    set(result, "raw", devices);
    juce::Array<juce::var> virtualDevices;
    for (const auto& device : router.devices) {
        auto item = object();
        set(item, "id", juce::String(device.id));
        set(item, "name", juce::String(device.name));
        set(item, "input", juce::String(device.input));
        set(item, "output", juce::String(device.output));
        juce::Array<juce::var> controls;
        for (const auto& control : device.controls) {
            auto entry = object();
            set(entry, "id", juce::String(control.id));
            set(entry, "name", juce::String(control.name));
            set(entry, "kind", control::controlKindName(control.kind));
            set(entry, "source", juce::String(control.sourceDevice) + " " + juce::String(control.sourceAddress));
            set(entry, "sourceDevice", juce::String(control.sourceDevice));
            set(entry, "sourceAddress", juce::String(control.sourceAddress));
            set(entry, "feedbackDevice", juce::String(control.feedbackDevice));
            set(entry, "feedbackAddress", juce::String(control.feedbackAddress));
            set(entry, "chain", chainVar(control.chain));
            set(entry, "value", control.value);
            controls.add(entry);
        }
        set(item, "controls", controls);
        virtualDevices.add(item);
    }
    set(result, "virtual", virtualDevices);
    juce::Array<juce::var> assignments;
    for (const auto& assignment : router.assignments) {
        auto item = assignmentVar(assignment);
        const auto info = targets.describe(assignment.target);
        set(item, "label", info ? info->label : juce::String(assignment.target));
        set(item, "scope", assignment.project ? "project" : "global");
        assignments.add(item);
    }
    set(result, "assignments", assignments);
    juce::Array<juce::var> loaded;
    for (const auto& plugin : plugins) {
        auto item = object();
        set(item, "name", plugin->name);
        set(item, "file", plugin->file.getFullPathName());
        set(item, "started", plugin->started);
        if (plugin->error.isNotEmpty())
            set(item, "error", plugin->error);
        loaded.add(item);
    }
    set(result, "plugins", loaded);
    set(result, "learning", learnMode);
    set(result, "learnTarget", juce::String(learnTarget));
    set(result, "learnHardware", juce::String(learnHardware));
    set(result, "learnControls", juce::String(learnControls));
    set(result, "learnStatus", learnStatus);
    return result;
}

juce::var Controllers::Impl::command(const juce::var& request)
{
    const auto action = text(request, "action", "status");
    const auto fail = [](const juce::String& message) { return juce::var(message); };
    const auto device = text(request, "device").toStdString();
    const auto controlId = text(request, "control").toStdString();
    if (action == "show") {
        // The window on a tab, a device, a rule.
        if (static_cast<bool>(request.getProperty("center", false)))
            centreWindow = true;
        const auto tabName = text(request, "tab");
        if (tabName.isNotEmpty())
            requestedTab = tabName == "hardware" ? 1 : tabName == "assignments" ? 2 : 0;
        if (router.findDevice(device) != nullptr) {
            selectedDevice = device;
            selectedControl = router.findControl(device, controlId) != nullptr ? controlId : std::string();
            requestedTab = 0;
        }
        return describe();
    }
    if (action == "status")
        return describe();
    if (action == "scan") {
        scanMidi();
        return describe();
    }
    if (action == "add_osc") {
        auto& made = addOsc(text(request, "name", "OSC"), static_cast<int>(number(request, "port", 0)), text(request, "host", "127.0.0.1"),
                            static_cast<int>(number(request, "sendPort", 0)));
        auto result = object();
        set(result, "device", juce::String(made.id));
        set(result, "listening", made.osc != nullptr && made.osc->listening);
        set(result, "sending", made.osc != nullptr && made.osc->sending);
        if (made.error.isNotEmpty())
            set(result, "error", made.error);
        return result;
    }
    if (action == "set_raw") {
        auto* found = rawDevice(device);
        if (found == nullptr)
            return fail("no raw device \"" + juce::String(device) + "\"");
        if (request.hasProperty("enabled"))
            found->enabled = static_cast<bool>(request.getProperty("enabled", true));
        if (request.hasProperty("notes"))
            found->notes = static_cast<bool>(request.getProperty("notes", true));
        if (request.hasProperty("name"))
            found->name = text(request, "name");
        if (found->kind == RawKind::osc) {
            found->listenPort = static_cast<int>(number(request, "port", found->listenPort));
            found->sendHost = text(request, "host", found->sendHost);
            found->sendPort = static_cast<int>(number(request, "sendPort", found->sendPort));
            if (request.hasProperty("cydawAddresses"))
                found->cydawAddresses = static_cast<bool>(request.getProperty("cydawAddresses", true));
        }
        applyRaw(*found);
        changed(true, false);
        return describe();
    }
    if (action == "remove_raw") {
        removeRaw(device);
        return describe();
    }
    if (action == "inject") {
        if (rawDevice(device) == nullptr)
            return fail("no raw device \"" + juce::String(device) + "\"");
        process({ Incoming::Kind::message, device, { text(request, "address").toStdString(), number(request, "value", 0.0) } });
        return describe();
    }
    if (action == "add_virtual") {
        const auto input = text(request, "input").toStdString();
        const auto output = text(request, "output").toStdString();
        if (!input.empty() && (rawDevice(input) == nullptr || !isInput(rawDevice(input)->kind)))
            return fail("no raw input \"" + juce::String(input) + "\"");
        if (!output.empty() && (rawDevice(output) == nullptr || !isOutput(rawDevice(output)->kind)))
            return fail("no raw output \"" + juce::String(output) + "\"");
        auto result = object();
        set(result, "device", juce::String(addVirtualDevice(text(request, "name"), input, output)));
        return result;
    }
    if (action == "set_virtual") {
        auto* found = router.findDevice(device);
        if (found == nullptr)
            return fail("no virtual device \"" + juce::String(device) + "\"");
        if (request.hasProperty("input")) {
            const auto input = text(request, "input").toStdString();
            if (!input.empty() && (rawDevice(input) == nullptr || !isInput(rawDevice(input)->kind)))
                return fail("no raw input \"" + juce::String(input) + "\"");
            setDeviceInput(*found, input);
        }
        if (request.hasProperty("output")) {
            const auto output = text(request, "output").toStdString();
            if (!output.empty() && (rawDevice(output) == nullptr || !isOutput(rawDevice(output)->kind)))
                return fail("no raw output \"" + juce::String(output) + "\"");
            setDeviceOutput(*found, output);
        }
        if (request.hasProperty("name") && text(request, "name").isNotEmpty())
            found->name = text(request, "name").toStdString();
        changed(true, false);
        return describe();
    }
    if (action == "duplicate_virtual") {
        const auto made = duplicateVirtualDevice(device);
        if (made.empty())
            return fail("no virtual device \"" + juce::String(device) + "\"");
        auto result = object();
        set(result, "device", juce::String(made));
        return result;
    }
    if (action == "learn_hardware" || action == "learn_controls") {
        const auto on = static_cast<bool>(request.getProperty("on", true));
        const auto* found = router.findDevice(device);
        if (on && found == nullptr)
            return fail("no virtual device \"" + juce::String(device) + "\"");
        if (on && action == "learn_controls" && found->input.empty())
            return fail("the device has no input yet: learn_hardware or set_virtual first");
        stopLearning();
        if (on)
            (action == "learn_hardware" ? learnHardware : learnControls) = device;
        return describe();
    }
    if (action == "remove_virtual") {
        removeVirtualDevice(device);
        return describe();
    }
    if (action == "add_control" || action == "set_control") {
        std::string id = controlId;
        if (action == "add_control") {
            const auto kind = control::controlKindNamed(text(request, "kind", "knob").toStdString());
            if (!kind)
                return fail("kind is knob, fader, encoder, button, toggle, pad or axis");
            id = addControl(device, text(request, "name"), *kind, text(request, "sourceDevice").toStdString(),
                            text(request, "sourceAddress").toStdString());
            if (id.empty())
                return fail("no virtual device \"" + juce::String(device) + "\"");
        }
        auto* control = router.findControl(device, id);
        if (control == nullptr)
            return fail("no control \"" + juce::String(id) + "\" on \"" + juce::String(device) + "\"");
        if (request.hasProperty("name"))
            control->name = text(request, "name").toStdString();
        if (action == "set_control" && request.hasProperty("sourceDevice"))
            control->sourceDevice = text(request, "sourceDevice").toStdString();
        if (action == "set_control" && request.hasProperty("sourceAddress"))
            control->sourceAddress = text(request, "sourceAddress").toStdString();
        if (request.hasProperty("feedbackDevice"))
            control->feedbackDevice = text(request, "feedbackDevice").toStdString();
        if (request.hasProperty("feedbackAddress"))
            control->feedbackAddress = text(request, "feedbackAddress").toStdString();
        if (request.hasProperty("chain"))
            control->chain = chainFrom(request.getProperty("chain", {}));
        changed(true, false);
        auto result = object();
        set(result, "device", juce::String(device));
        set(result, "control", juce::String(id));
        return result;
    }
    if (action == "remove_control") {
        removeControl(device, controlId);
        return describe();
    }
    if (action == "assign") {
        const auto target = text(request, "target").toStdString();
        if (router.findControl(device, controlId) == nullptr)
            return fail("no control \"" + juce::String(controlId) + "\" on \"" + juce::String(device) + "\"");
        if (!targets.describe(target))
            return fail("no target \"" + juce::String(target) + "\"; targets lists them");
        assign(device, controlId, target, static_cast<bool>(request.getProperty("replace", false)));
        auto& assignment = router.assignments.back();
        assignment.minimum = std::clamp(number(request, "minimum", 0.0), 0.0, 1.0);
        assignment.maximum = std::clamp(number(request, "maximum", 1.0), 0.0, 1.0);
        assignment.takeover = static_cast<bool>(request.getProperty("takeover", false));
        assignment.caught = !assignment.takeover;
        changed(!assignment.project, assignment.project);
        return describe();
    }
    if (action == "unassign") {
        const auto target = text(request, "target").toStdString();
        for (std::size_t index = router.assignments.size(); index-- > 0;)
            if (router.assignments[index].target == target)
                unassign(index);
        return describe();
    }
    if (action == "learn") {
        learnMode = static_cast<bool>(request.getProperty("on", true));
        ui::setLearnMode(learnMode);
        learnTarget = text(request, "target").toStdString();
        if (!learnTarget.empty() && !targets.describe(learnTarget))
            return fail("no target \"" + juce::String(learnTarget) + "\"");
        learnTargetLabel = learnTarget.empty() ? juce::String() : targets.describe(learnTarget)->label;
        if (!learnMode)
            learnTarget.clear();
        return describe();
    }
    if (action == "targets") {
        const auto filter = text(request, "filter");
        juce::Array<juce::var> list;
        for (const auto& key : targets.list()) {
            if (filter.isNotEmpty() && !juce::String(key).containsIgnoreCase(filter))
                continue;
            const auto info = targets.describe(key);
            if (!info)
                continue;
            auto item = object();
            set(item, "target", juce::String(key));
            set(item, "label", info->label);
            set(item, "scope", info->project ? "project" : "global");
            if (const auto value = targets.value(key))
                set(item, "value", *value);
            list.add(item);
        }
        auto result = object();
        set(result, "targets", list);
        return result;
    }
    if (action == "value" || action == "apply") {
        const auto target = text(request, "target").toStdString();
        if (!targets.describe(target))
            return fail("no target \"" + juce::String(target) + "\"");
        if (action == "apply") {
            const auto value = number(request, "value", 0.0);
            (void) targets.apply(target, value, value >= 0.5, false, false);
        }
        auto result = object();
        set(result, "target", juce::String(target));
        if (const auto value = targets.value(target))
            set(result, "value", *value);
        return result;
    }
    if (action == "feedback") {
        feedback(true);
        return describe();
    }
    if (action == "monitor") {
        const auto count = static_cast<int>(number(request, "count", 50));
        juce::Array<juce::var> list;
        const auto from = monitor.size() > static_cast<std::size_t>(std::max(0, count)) ? monitor.size() - static_cast<std::size_t>(count) : 0;
        for (auto index = from; index < monitor.size(); ++index) {
            const auto& seen = monitor[index];
            auto item = object();
            set(item, "out", seen.out);
            set(item, "device", juce::String(seen.device));
            set(item, "address", juce::String(seen.message.address));
            set(item, "value", seen.message.value);
            list.add(item);
        }
        auto result = object();
        set(result, "messages", list);
        return result;
    }
    return fail("action is status, scan, add_osc, set_raw, remove_raw, inject, add_virtual, set_virtual, duplicate_virtual, "
                "remove_virtual, learn_hardware, learn_controls, add_control, set_control, remove_control, assign, unassign, learn, "
                "targets, value, apply, feedback or monitor");
}

// ---- The public face ---------------------------------------------------------------------------------

Controllers::Controllers(Studio& studio, UiState& state, ImGuiHost& host, const bool keepSettings)
    : impl_(std::make_unique<Impl>(studio, state, host, keepSettings))
{
}

Controllers::~Controllers() = default;

void Controllers::draw(bool& visible)
{
    impl_->draw(visible);
}

void Controllers::drawButton(bool& visible)
{
    impl_->drawButton(visible);
}

void Controllers::drawLearnOverlay()
{
    impl_->drawOverlay();
}

void Controllers::pollKeys()
{
    impl_->pollKeys();
}

void Controllers::setLearning(const bool on)
{
    impl_->learnMode = on;
    ui::setLearnMode(on);
    if (!on) {
        impl_->learnTarget.clear();
        impl_->learnSource.reset();
        impl_->pickTarget.reset();
    }
}

bool Controllers::learning() const
{
    return impl_->learnMode;
}

juce::var Controllers::command(const juce::var& request)
{
    return impl_->command(request);
}

} // namespace cydaw::v2
