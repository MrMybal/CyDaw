#include "TransportSync.h"
#include "Localization.h"
#include "Theme.h"
#include "TransportTimecode.h"
#include "Widgets.h"
#include <ableton/Link.hpp>
#include <algorithm>
#include <atomic>
#include <deque>
#include <imgui.h>
#include <jack/jack.h>
#include <mutex>
#include <numeric>
#include <optional>
#include <vector>

namespace cydaw::v2 {
namespace {
    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
    juce::var obj() { return juce::var(new juce::DynamicObject()); }
    void put(juce::var& v, const char* k, const juce::var& x) { v.getDynamicObject()->setProperty(k, x); }
    const char* protocols[] = { "MIDI Clock", "MTC + MMC", "JACK Transport", "Ableton Link" };
    const char* protocolKeys[] = { "midi-clock", "mtc-mmc", "jack", "link" };
    const char* targetKeys[] = { "studio", "foundry", "deck-a", "deck-b", "deck-c", "deck-d", "cinema" };
    struct Packet {
        std::vector<juce::uint8> bytes;
        double time {};
    };
    struct Config {
        bool enabled {}, send {}, tempo { true }, play { true }, position { true }, stopOnLoss { true };
        int protocol {}, target {}, rate { 1 }, device { 127 };
        double offsetMs {}, quantum { 4.0 };
        juce::String input, output, jackLibrary, server;
    };
    juce::var json(const Config& c)
    {
        auto v = obj();
        put(v, "enabled", c.enabled);
        put(v, "protocol", protocolKeys[c.protocol]);
        put(v, "target", targetKeys[c.target]);
        put(v, "role", c.send ? "send" : "follow");
        put(v, "tempo", c.tempo);
        put(v, "play", c.play);
        put(v, "position", c.position);
        put(v, "stopOnLoss", c.stopOnLoss);
        put(v, "input", c.input);
        put(v, "output", c.output);
        put(v, "frameRate", c.rate);
        put(v, "deviceId", c.device);
        put(v, "offsetMs", c.offsetMs);
        put(v, "quantum", c.quantum);
        put(v, "jackLibrary", c.jackLibrary);
        put(v, "server", c.server);
        return v;
    }
    juce::String parse(const juce::var& v, Config& c)
    {
        if (!v.isObject())
            return "configuration must be an object";
        auto key = v.getProperty("protocol", protocolKeys[c.protocol]).toString();
        int protocol = -1;
        for (int i = 0; i < 4; ++i)
            if (key == protocolKeys[i])
                protocol = i;
        if (protocol < 0)
            return "protocol: midi-clock, mtc-mmc, jack, link";
        key = v.getProperty("target", targetKeys[c.target]).toString();
        int target = -1;
        for (int i = 0; i < 7; ++i)
            if (key == targetKeys[i])
                target = i;
        if (target < 0)
            return "target: studio, foundry, deck-a, deck-b, deck-c, deck-d, cinema";
        key = v.getProperty("role", c.send ? "send" : "follow").toString();
        if (key != "send" && key != "follow")
            return "role: send or follow (Link is always a peer)";
        c.protocol = protocol;
        c.target = target;
        c.send = key == "send";
        c.enabled = static_cast<bool>(v.getProperty("enabled", c.enabled));
        c.tempo = static_cast<bool>(v.getProperty("tempo", c.tempo));
        c.play = static_cast<bool>(v.getProperty("play", c.play));
        c.position = static_cast<bool>(v.getProperty("position", c.position));
        c.stopOnLoss = static_cast<bool>(v.getProperty("stopOnLoss", c.stopOnLoss));
        c.input = v.getProperty("input", c.input).toString();
        c.output = v.getProperty("output", c.output).toString();
        c.jackLibrary = v.getProperty("jackLibrary", c.jackLibrary).toString();
        c.server = v.getProperty("server", c.server).toString();
        c.rate = static_cast<int>(v.getProperty("frameRate", c.rate));
        c.device = static_cast<int>(v.getProperty("deviceId", c.device));
        c.offsetMs = static_cast<double>(v.getProperty("offsetMs", c.offsetMs));
        c.quantum = static_cast<double>(v.getProperty("quantum", c.quantum));
        if (c.rate < 0 || c.rate > 3 || c.device < 0 || c.device > 127)
            return "frameRate: 0=24, 1=25, 2=29.97 drop, 3=30; deviceId: 0..127";
        if (!std::isfinite(c.offsetMs) || std::abs(c.offsetMs) > 86400000 || !std::isfinite(c.quantum) || c.quantum < 1
            || c.quantum > 64)
            return "offsetMs: -86400000..86400000; quantum: 1..64 beats";
        return {};
    }
}

class TransportSync::Impl final : private juce::Timer,
                                  private juce::MidiInputCallback,
                                  private juce::HighResolutionTimer {
public:
    Impl(juce::PropertiesFile& properties, juce::AudioDeviceManager& manager, bool saves, Read read, Apply apply)
        : settings(properties)
        , midiManager(manager)
        , saving(saves)
        , read_(std::move(read))
        , apply_(std::move(apply))
    {
        auto saved = juce::JSON::parse(settings.getValue("transportSync"));
        if (saved.isObject()) {
            auto candidate = config;
            if (parse(saved, candidate).isEmpty())
                config = candidate;
        }
        config.enabled = false;
        draft = config;
        refreshDevices();
    }
    ~Impl() override { disconnect(); }
    Config config, draft;
    juce::String error, info;
    juce::Array<juce::MidiDeviceInfo> inputs, outputs;
    bool connected {};
    juce::String configure(Config next)
    {
        disconnect();
        config = next;
        error.clear();
        info.clear();
        previous = read_(config.target);
        previousAt = nowSeconds();
        incoming = previous;
        clockBeats = previous.seconds * previous.bpm / 60;
        lastSignal = lastClock = incomingAt = 0;
        lastTempoApplied = nowSeconds() - 1;
        clockIntervals.clear();
        qfExpected = -1;
        qfCount = 0;
        remoteRunning = false;
        clockArmed = false;
        rxCount = 0;
        txCount = 0;
        lastLinkPlaying = false;
        linkPending = false;
        if (config.enabled) {
            if (config.protocol < 2) {
                if (config.send) {
                    output = juce::MidiOutput::openDevice(config.output);
                    if (!output)
                        error = ui::Ls(
                            "Sortie MIDI indisponible. Choisissez un port.", "MIDI output unavailable. Choose a port.");
                } else {
                    bool found = false;
                    for (const auto& device : juce::MidiInput::getAvailableDevices())
                        if (device.identifier == config.input)
                            found = true;
                    if (!found)
                        error = ui::Ls(
                            "Entrée MIDI indisponible. Choisissez un port.", "MIDI input unavailable. Choose a port.");
                    else {
                        inputWasEnabled = midiManager.isMidiInputDeviceEnabled(config.input);
                        midiManager.setMidiInputDeviceEnabled(config.input, true);
                        if (!midiManager.isMidiInputDeviceEnabled(config.input))
                            error = ui::Ls("Impossible d'ouvrir le port MIDI.", "Could not open MIDI input.");
                        else {
                            registeredInput = config.input;
                            midiManager.addMidiInputDeviceCallback(registeredInput, this);
                        }
                    }
                }
                connected = error.isEmpty();
            } else if (config.protocol == 2)
                connectJack();
            else {
                try {
                    link = std::make_unique<ableton::Link>(previous.bpm);
                    link->enableStartStopSync(config.play);
                    link->enable(true);
                    auto state = link->captureAppSessionState();
                    lastLinkPlaying = state.isPlaying();
                    linkBeatOffset = state.beatAtTime(link->clock().micros(), config.quantum)
                        - previous.seconds * previous.bpm / 60;
                    connected = true;
                } catch (const std::exception& ex) {
                    error = juce::String("Link: ") + ex.what();
                }
            }
            if (!connected) {
                disconnect();
                config.enabled = false;
            }
        }
        if (config.enabled) {
            publishOutput(previous, true);
            juce::Timer::startTimer(10);
            if (config.protocol < 2 && config.send)
                juce::HighResolutionTimer::startTimer(1);
        }
        draft = config;
        if (saving) {
            auto saved = json(config);
            put(saved, "enabled", false);
            settings.setValue("transportSync", juce::JSON::toString(saved, false));
        }
        return error;
    }
    void refreshDevices()
    {
        inputs = juce::MidiInput::getAvailableDevices();
        outputs = juce::MidiOutput::getAvailableDevices();
    }
    juce::var status()
    {
        auto v = json(config);
        put(v, "connected", connected);
        put(v, "error", error);
        put(v, "info", info);
        put(v, "rxMessages", static_cast<juce::int64>(rxCount.load()));
        put(v, "txMessages", static_cast<juce::int64>(txCount.load()));
        put(v, "signal", lastSignal > 0 && nowSeconds() - lastSignal < 1);
        put(v, "peers", link ? static_cast<int>(link->numPeers()) : 0);
        put(v, "timing",
            "message-thread transport; dedicated 1 ms MIDI output scheduler; not sample-locked across audio devices");
        auto ports = [](const auto& devices) {
            juce::Array<juce::var> a;
            for (const auto& d : devices) {
                auto p = obj();
                put(p, "id", d.identifier);
                put(p, "name", d.name);
                a.add(p);
            }
            return juce::var(a);
        };
        put(v, "inputs", ports(inputs));
        put(v, "outputs", ports(outputs));
        auto s = read_(config.target);
        auto state = obj();
        put(state, "playing", s.playing);
        put(state, "position", s.seconds);
        put(state, "bpm", s.bpm);
        put(state, "available", s.available);
        put(v, "transport", state);
        return v;
    }
    juce::var command(const juce::var& request)
    {
        auto action = request.getProperty("action", "status").toString();
        if (action == "configure") {
            auto c = config;
            auto e = parse(request, c);
            if (e.isNotEmpty())
                return e;
            e = configure(c);
            if (e.isNotEmpty())
                return e;
        } else if (action == "disable") {
            auto c = config;
            c.enabled = false;
            configure(c);
        } else if (action == "refresh")
            refreshDevices();
        else if (action == "inject") {
            if (juce::SystemStats::getEnvironmentVariable("CYDAW_NO_RECOVERY", {}).isEmpty())
                return "diagnostic injection requires CYDAW_NO_RECOVERY";
            if (config.enabled)
                return "disable live synchronization before diagnostic injection";
            auto c = config;
            auto e = parse(request, c);
            if (e.isNotEmpty())
                return e;
            if (c.protocol > 1 || c.send)
                return "injection: MIDI receive only";
            config = c;
            config.enabled = false;
            const auto packets = request.getProperty("packets", {});
            const auto* a = packets.getArray();
            if (!a || a->size() > 4096)
                return "packets must be an array (max 4096)";
            for (const auto& p : *a) {
                const auto b = p.getProperty("bytes", {});
                const auto* bytes = b.getArray();
                if (!bytes || bytes->size() > 64)
                    return "bytes: at most 64 bytes";
                Packet packet;
                packet.time = static_cast<double>(p.getProperty("time", nowSeconds()));
                if (!std::isfinite(packet.time))
                    return "invalid time";
                for (const auto& value : *bytes) {
                    int n = static_cast<int>(value);
                    if (n < 0 || n > 255)
                        return "byte out of range";
                    packet.bytes.push_back(static_cast<juce::uint8>(n));
                }
                receive(packet);
            }
            applyIncoming(false);
            draft = config;
        } else if (action == "capture") {
            juce::Array<juce::var> messages;
            {
                std::lock_guard guard(ioMutex);
                for (const auto& bytes : sent) {
                    juce::Array<juce::var> a;
                    for (auto b : bytes)
                        a.add(static_cast<int>(b));
                    messages.add(juce::var(a));
                }
                sent.clear();
            }
            auto v = status();
            put(v, "messages", juce::var(messages));
            return v;
        } else if (action != "status")
            return "action: status, configure, disable, refresh, capture";
        return status();
    }
    void draw(bool& visible);
    // Its traffic and its Link peers, for the top bar.
    [[nodiscard]] std::uint64_t receivedMessages() const { return rxCount.load(); }
    [[nodiscard]] std::uint64_t sentMessages() const { return txCount.load(); }
    [[nodiscard]] int linkPeers() const { return link ? static_cast<int>(link->numPeers()) : 0; }

private:
    juce::PropertiesFile& settings;
    juce::AudioDeviceManager& midiManager;
    bool saving;
    Read read_;
    Apply apply_;
    juce::String registeredInput;
    bool inputWasEnabled {};
    std::unique_ptr<juce::MidiOutput> output;
    std::unique_ptr<ableton::Link> link;
    bool remoteRunning {}, clockArmed {}, lastLinkPlaying {}, linkPending {};
    double linkStartTime {}, linkBeatOffset {}, linkLocalStart {};
    State previous, incoming;
    double previousAt {}, lastSignal {}, lastClock {}, clockBeats {}, incomingAt {}, lastTempoApplied {};
    bool pendingPlay {}, pendingPosition {}, pendingTempo {};
    std::deque<double> clockIntervals;
    std::array<int, 8> qf {};
    int qfExpected { -1 }, qfCount {};
    std::atomic<std::uint64_t> rxCount {}, txCount {};
    std::mutex ioMutex;
    std::vector<Packet> received;
    std::deque<std::vector<juce::uint8>> sent;
    State outputState;
    double outputAt {};
    bool outputReset {};
    double nextPulse {};
    int quarterPiece {};
    timecode::Code outgoingCode;
    juce::DynamicLibrary jackLibrary;
    jack_client_t* client {};
    std::atomic<bool> jackDead {};
    std::atomic<double> jackBpm { 120 };
    std::atomic<int> jackNumerator { 4 }, jackDenominator { 4 };
    using Open = jack_client_t* (*)(const char*, jack_options_t, jack_status_t*, ...);
    Open jackOpen {};
    int (*jackClose)(jack_client_t*) {};
    int (*jackActivate)(jack_client_t*) {};
    jack_transport_state_t (*jackQuery)(const jack_client_t*, jack_position_t*) {};
    void (*jackStart)(jack_client_t*) {};
    void (*jackStop)(jack_client_t*) {};
    int (*jackLocate)(jack_client_t*, jack_nframes_t) {};
    int (*jackTimebase)(jack_client_t*, int, JackTimebaseCallback, void*) {};
    void (*jackShutdown)(jack_client_t*, JackShutdownCallback, void*) {};
    jack_nframes_t (*jackRate)(jack_client_t*) {};
    void disconnect()
    {
        juce::Timer::stopTimer();
        juce::HighResolutionTimer::stopTimer();
        if (registeredInput.isNotEmpty()) {
            midiManager.removeMidiInputDeviceCallback(registeredInput, this);
            if (!inputWasEnabled)
                midiManager.setMidiInputDeviceEnabled(registeredInput, false);
            registeredInput.clear();
        }
        if (output && config.send && config.play && outputState.playing) {
            if (config.protocol == 0)
                output->sendMessageNow(juce::MidiMessage::midiStop());
            else {
                const juce::uint8 bytes[] = { 0xf0, 0x7f, static_cast<juce::uint8>(config.device), 6, 1, 0xf7 };
                output->sendMessageNow(juce::MidiMessage(bytes, 6, 0));
            }
        }
        output.reset();
        if (link) {
            link->enable(false);
            link.reset();
        }
        if (client && jackClose)
            jackClose(client);
        client = nullptr;
        jackLibrary.close();
        std::lock_guard guard(ioMutex);
        received.clear();
        sent.clear();
        pendingPlay = pendingPosition = pendingTempo = false;
        nextPulse = 0;
        outputReset = false;
        connected = false;
    }
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override
    {
        const auto* bytes = message.getRawData();
        const auto size = message.getRawDataSize();
        if (size < 1 || size > 64)
            return;
        const auto arrived = nowSeconds();
        const auto stamp = message.getTimeStamp();
        const auto time = stamp > 0 && std::abs(arrived - stamp) < 1 ? stamp : arrived;
        std::lock_guard guard(ioMutex);
        if (received.size() < 4096)
            received.push_back({ std::vector<juce::uint8>(bytes, bytes + size), time });
    }
    void receive(const Packet&);
    bool acceptDevice(int id) const { return id == 127 || config.device == 127 || config.device == id; }
    void applyIncoming(bool extrapolate);
    void timerCallback() override;
    bool seeked(const State& s, double t) const
    {
        return std::abs(s.seconds - previous.seconds - (previous.playing ? t - previousAt : 0)) > 0.075;
    }
    void sendBytes(const std::vector<juce::uint8>& bytes)
    {
        if (output)
            output->sendMessageNow(juce::MidiMessage(bytes.data(), static_cast<int>(bytes.size()), 0));
        ++txCount;
        if (sent.size() >= 256)
            sent.pop_front();
        sent.push_back(bytes);
    }
    void sendCode(double);
    void publishOutput(const State&, bool);
    void hiResTimerCallback() override;
    template <class T> bool symbol(T& f, const char* name)
    {
        f = reinterpret_cast<T>(jackLibrary.getFunction(name));
        return f != nullptr;
    }
    void connectJack();
    static void timebase(jack_transport_state_t, jack_nframes_t, jack_position_t*, int, void*);
    void tickJack(const State&);
    void tickLink(const State&);
};

void TransportSync::Impl::receive(const Packet& packet)
{
    const auto& b = packet.bytes;
    const auto n = b.size();
    if (n == 0)
        return;
    ++rxCount;
    const auto t = packet.time;
    if (config.protocol == 0) {
        if (b[0] == 0xf8 && n == 1) {
            lastSignal = t;
            if (lastClock > 0) {
                const auto delta = t - lastClock;
                if (delta > 0.002 && delta < 0.25) {
                    clockIntervals.push_back(delta);
                    if (clockIntervals.size() > 24)
                        clockIntervals.pop_front();
                    if (clockIntervals.size() >= 3) {
                        const auto average = std::accumulate(clockIntervals.begin(), clockIntervals.end(), 0.0)
                            / clockIntervals.size();
                        incoming.bpm = std::clamp(60 / (24 * average), 20.0, 400.0);
                        pendingTempo = true;
                    }
                } else
                    clockIntervals.clear();
            }
            lastClock = t;
            if (clockArmed) {
                clockArmed = false;
                remoteRunning = true;
                incoming.playing = true;
                pendingPlay = true;
            } else if (remoteRunning) {
                clockBeats += 1.0 / 24;
                incoming.seconds += 60 / (24 * std::max(20.0, incoming.bpm));
            }
            if (remoteRunning) {
                incomingAt = t;
                pendingPosition = true;
            }
        } else if (n == 1 && (b[0] == 0xfa || b[0] == 0xfb || b[0] == 0xfc)) {
            lastSignal = t;
            if (b[0] == 0xfc) {
                clockArmed = false;
                incoming.playing = false;
                remoteRunning = false;
                pendingPlay = true;
            } else {
                clockArmed = true;
                remoteRunning = false;
            }
            if (b[0] == 0xfa) {
                clockBeats = 0;
                incoming.seconds = 0;
                incomingAt = t;
                pendingPosition = true;
            }
            if (b[0] == 0xfb) {
                incomingAt = t;
                pendingPosition = true;
            }
        } else if (n == 3 && b[0] == 0xf2 && b[1] < 128 && b[2] < 128) {
            lastSignal = t;
            clockBeats = (b[1] + (b[2] << 7)) / 4.0;
            incoming.seconds = clockBeats * 60 / incoming.bpm;
            incomingAt = t;
            pendingPosition = true;
        }
        return;
    }
    if (config.protocol != 1)
        return;
    if (n == 2 && b[0] == 0xf1 && b[1] < 128) {
        const int piece = (b[1] >> 4) & 7;
        if (piece != qfExpected) {
            qfCount = 0;
            qfExpected = 0;
        }
        if (piece == qfExpected) {
            qf[piece] = b[1] & 15;
            ++qfCount;
            qfExpected = (piece + 1) % 8;
        }
        if (qfCount == 8) {
            qfCount = 0;
            const auto code = timecode::decode(qf);
            const auto s = timecode::seconds(code);
            if (s >= 0) {
                lastSignal = t;
                incoming.seconds = std::fmod(
                    s + 2 / timecode::fps(code.rate), code.rate == 2 ? 2589408 / timecode::fps(2) : 86400.0);
                incomingAt = t;
                incoming.playing = true;
                remoteRunning = true;
                pendingPosition = pendingPlay = true;
            }
        }
    } else if (n == 10 && b[0] == 0xf0 && b[1] == 0x7f && acceptDevice(b[2]) && b[3] == 1 && b[4] == 1
        && b[9] == 0xf7) {
        const timecode::Code code { b[5] & 31, b[6], b[7], b[8], (b[5] >> 5) & 3 };
        const auto s = timecode::seconds(code);
        if (s >= 0) {
            lastSignal = t;
            incoming.seconds = s;
            incomingAt = t;
            pendingPosition = true;
            qfCount = 0;
            qfExpected = -1;
        }
    } else if (n >= 6 && b[0] == 0xf0 && b[1] == 0x7f && acceptDevice(b[2]) && b[3] == 6 && b.back() == 0xf7) {
        const int command = b[4];
        if (n == 6 && (command == 1 || command == 2 || command == 3 || command == 9)) {
            lastSignal = t;
            incoming.playing = command == 2 || command == 3;
            remoteRunning = incoming.playing;
            pendingPlay = true;
        } else if (n == 13 && command == 0x44 && b[5] == 6 && b[6] == 1) {
            const timecode::Code code { b[7] & 31, b[8], b[9], b[10], (b[7] >> 5) & 3 };
            const auto s = timecode::seconds(code);
            if (s >= 0 && b[11] < 100) {
                lastSignal = t;
                incoming.seconds = s + b[11] / (100 * timecode::fps(code.rate));
                incomingAt = t;
                pendingPosition = true;
            }
        }
    }
}
void TransportSync::Impl::applyIncoming(bool extrapolate)
{
    const auto local = read_(config.target);
    if (!local.available)
        return;
    Update change;
    change.state = incoming;
    if (config.protocol == 0 && !config.tempo)
        change.state.seconds = clockBeats * 60 / std::max(20.0, local.bpm);
    change.state.seconds = std::max(0.0,
        change.state.seconds + config.offsetMs / 1000
            + (extrapolate && remoteRunning && incomingAt > 0 ? std::clamp(nowSeconds() - incomingAt, 0.0, 0.1) : 0));
    change.play = config.play && pendingPlay && local.playing != incoming.playing;
    const bool tempoChanged = std::abs(local.bpm - incoming.bpm) > 0.05 || local.numerator != incoming.numerator
        || local.denominator != incoming.denominator;
    change.tempo = config.tempo && pendingTempo && tempoChanged && nowSeconds() - lastTempoApplied >= 0.1;
    change.position = config.position && pendingPosition
        && (change.play || std::abs(local.seconds - change.state.seconds) > (local.playing ? 0.045 : 0.00001));
    if (change.play || change.tempo || change.position)
        apply_(config.target, change);
    if (change.tempo)
        lastTempoApplied = nowSeconds();
    pendingPlay = pendingPosition = false;
    pendingTempo = pendingTempo && config.tempo && tempoChanged && !change.tempo;
}
void TransportSync::Impl::timerCallback()
{
    if (!config.enabled)
        return;
    auto local = read_(config.target);
    const auto t = nowSeconds();
    if (config.protocol < 2) {
        if (config.send)
            publishOutput(local, false);
        else {
            std::vector<Packet> packets;
            {
                std::lock_guard guard(ioMutex);
                packets.swap(received);
            }
            for (const auto& p : packets)
                receive(p);
            const auto timeout
                = config.protocol == 0 ? std::max(1.0, 5 * 60 / (24 * std::max(20.0, incoming.bpm))) : 0.5;
            if (config.stopOnLoss && remoteRunning && lastSignal > 0 && t - lastSignal > timeout) {
                incoming.playing = false;
                remoteRunning = false;
                pendingPlay = true;
                info = ui::Ls("Signal perdu : transport arrêté.", "Signal lost: transport stopped.");
            } else if (!packets.empty())
                info.clear();
            applyIncoming(true);
        }
    } else if (config.protocol == 2)
        tickJack(local);
    else
        tickLink(local);
    previous = read_(config.target);
    previousAt = t;
}
void TransportSync::Impl::sendCode(double seconds)
{
    const auto c = timecode::fromSeconds(seconds, config.rate);
    sendBytes({ 0xf0, 0x7f, static_cast<juce::uint8>(config.device), 1, 1,
        static_cast<juce::uint8>(c.hours | (c.rate << 5)), static_cast<juce::uint8>(c.minutes),
        static_cast<juce::uint8>(c.seconds), static_cast<juce::uint8>(c.frames), 0xf7 });
    sendBytes({ 0xf0, 0x7f, static_cast<juce::uint8>(config.device), 6, 0x44, 6, 1,
        static_cast<juce::uint8>(c.hours | (c.rate << 5)), static_cast<juce::uint8>(c.minutes),
        static_cast<juce::uint8>(c.seconds), static_cast<juce::uint8>(c.frames), 0, 0xf7 });
}
void TransportSync::Impl::publishOutput(const State& s, bool initial)
{
    if (config.protocol >= 2 || !config.send)
        return;
    std::lock_guard guard(ioMutex);
    const auto t = nowSeconds();
    const auto changed = s.playing != previous.playing;
    const auto seek = seeked(s, t);
    if (config.protocol == 0) {
        if (config.position && (initial || seek || (changed && s.playing))) {
            if (seek && s.playing && config.play)
                sendBytes({ 0xfc });
            const auto spp = std::clamp(
                static_cast<int>(std::max(0.0, s.seconds - config.offsetMs / 1000) * s.bpm / 60 * 4), 0, 16383);
            sendBytes({ 0xf2, static_cast<juce::uint8>(spp & 127), static_cast<juce::uint8>((spp >> 7) & 127) });
            if (seek && s.playing && config.play && !changed)
                sendBytes({ 0xfb });
        }
        if (config.play && (changed || (initial && s.playing)))
            sendBytes({ static_cast<juce::uint8>(s.playing ? (s.seconds < 0.001 ? 0xfa : 0xfb) : 0xfc) });
    } else {
        if (config.position && (initial || seek || (changed && s.playing)))
            sendCode(std::max(0.0, s.seconds - config.offsetMs / 1000));
        if (config.play && (changed || (initial && s.playing)))
            sendBytes({ 0xf0, 0x7f, static_cast<juce::uint8>(config.device), 6,
                static_cast<juce::uint8>(s.playing ? 2 : 1), 0xf7 });
    }
    outputState = s;
    outputAt = t;
    outputReset = outputReset || initial || seek || changed;
}
void TransportSync::Impl::hiResTimerCallback()
{
    std::lock_guard guard(ioMutex);
    if (!output || !outputState.available)
        return;
    const auto t = nowSeconds();
    if (outputReset) {
        outputReset = false;
        nextPulse = t;
        quarterPiece = 0;
    }
    const bool clock = config.protocol == 0;
    if (clock ? !config.tempo : (!config.position || !outputState.playing)) {
        nextPulse = 0;
        return;
    }
    const auto interval = clock ? 60 / (std::max(20.0, outputState.bpm) * 24) : 1 / (timecode::fps(config.rate) * 4);
    if (nextPulse <= 0)
        nextPulse = t;
    if (t - nextPulse > interval * 2)
        nextPulse = t; // Never burst stale clocks after an OS stall.
    if (t + 0.0001 < nextPulse)
        return;
    if (clock)
        sendBytes({ 0xf8 });
    else {
        if (quarterPiece == 0)
            outgoingCode = timecode::fromSeconds(
                std::max(0.0, outputState.seconds + (t - outputAt) - config.offsetMs / 1000), config.rate);
        const auto parts = timecode::nibbles(outgoingCode);
        sendBytes({ 0xf1, static_cast<juce::uint8>((quarterPiece << 4) | parts[quarterPiece]) });
        quarterPiece = (quarterPiece + 1) % 8;
    }
    nextPulse += interval;
}
void TransportSync::Impl::connectJack()
{
    juce::StringArray paths;
    if (config.jackLibrary.isNotEmpty())
        paths.add(config.jackLibrary);
#if JUCE_WINDOWS
    else {
        const auto pf = juce::SystemStats::getEnvironmentVariable("ProgramW6432", "C:/Program Files");
        paths.add(pf + "/JACK2/lib/libjack64.dll");
        paths.add(pf + "/JACK2/lib/libjack.dll");
        paths.add("libjack64.dll");
        paths.add("libjack.dll");
    }
#else
    else {
        paths.add("libjack.so.0");
        paths.add("libjack.0.dylib");
    }
#endif
    bool loaded = false;
    for (const auto& path : paths)
        if (jackLibrary.open(path)) {
            loaded = true;
            break;
        }
    if (!loaded) {
        error = ui::Ls("Bibliothèque JACK introuvable. Installez JACK2 ou indiquez sa bibliothèque.",
            "JACK library not found. Install JACK2 or select its library.");
        return;
    }
    if (!symbol(jackOpen, "jack_client_open") || !symbol(jackClose, "jack_client_close")
        || !symbol(jackActivate, "jack_activate") || !symbol(jackQuery, "jack_transport_query")
        || !symbol(jackStart, "jack_transport_start") || !symbol(jackStop, "jack_transport_stop")
        || !symbol(jackLocate, "jack_transport_locate") || !symbol(jackRate, "jack_get_sample_rate")
        || !symbol(jackShutdown, "jack_on_shutdown") || !symbol(jackTimebase, "jack_set_timebase_callback")) {
        error = "JACK: incompatible library";
        return;
    }
    jack_status_t status {};
    auto options = JackNoStartServer;
    if (config.server.isNotEmpty())
        options = static_cast<jack_options_t>(options | JackServerName);
    client = jackOpen("CyDAW Transport", options, &status, config.server.toRawUTF8());
    if (!client) {
        error = ui::Ls("Serveur JACK inaccessible. Démarrez-le puis reconnectez.",
            "JACK server unavailable. Start it, then reconnect.");
        return;
    }
    jackDead = false;
    jackShutdown(client, [](void* p) { static_cast<Impl*>(p)->jackDead = true; }, this);
    jackBpm = previous.bpm;
    jackNumerator = previous.numerator;
    jackDenominator = previous.denominator;
    if (config.send && config.tempo && jackTimebase(client, 1, &timebase, this) != 0) {
        error = ui::Ls("JACK a déjà un maître de tempo. Suivez-le, ou désactivez l'envoi du tempo.",
            "JACK already has a tempo master. Follow it, or disable tempo output.");
        return;
    }
    if (jackActivate(client) != 0) {
        error = "JACK: activation failed";
        return;
    }
    if (config.send) {
        if (config.position)
            jackLocate(client,
                static_cast<jack_nframes_t>(
                    std::clamp((previous.seconds - config.offsetMs / 1000) * jackRate(client), 0.0, 4294967295.0)));
        if (config.play && previous.playing)
            jackStart(client);
    }
    connected = true;
}
void TransportSync::Impl::timebase(jack_transport_state_t, jack_nframes_t, jack_position_t* p, int, void* opaque)
{
    auto& self = *static_cast<Impl*>(opaque);
    const auto bpm = self.jackBpm.load();
    const auto num = self.jackNumerator.load();
    const auto den = self.jackDenominator.load();
    const auto beat = p->frame_rate > 0 ? static_cast<double>(p->frame) / p->frame_rate * bpm / 60 * den / 4 : 0;
    const auto bar = std::floor(beat / std::max(1, num));
    const auto inBar = beat - bar * num;
    p->valid = JackPositionBBT;
    p->bar = static_cast<int>(bar) + 1;
    p->beat = static_cast<int>(inBar) + 1;
    p->tick = static_cast<int>((inBar - std::floor(inBar)) * 1920);
    p->bar_start_tick = bar * num * 1920;
    p->beats_per_bar = static_cast<float>(num);
    p->beat_type = static_cast<float>(den);
    p->ticks_per_beat = 1920;
    p->beats_per_minute = bpm * den / 4;
}
void TransportSync::Impl::tickJack(const State& local)
{
    if (jackDead) {
        connected = false;
        error = ui::Ls("Serveur JACK déconnecté.", "JACK server disconnected.");
        if (config.stopOnLoss && !config.send && config.play && local.playing) {
            Update u;
            u.state = local;
            u.state.playing = false;
            u.play = true;
            apply_(config.target, u);
        }
        return;
    }
    if (!client)
        return;
    if (config.send) {
        jackBpm = local.bpm;
        jackNumerator = local.numerator;
        jackDenominator = local.denominator;
        if (!local.available)
            return;
        const auto rate = jackRate(client);
        if (config.position && seeked(local, nowSeconds()))
            jackLocate(client,
                static_cast<jack_nframes_t>(
                    std::clamp((local.seconds - config.offsetMs / 1000) * rate, 0.0, 4294967295.0)));
        if (config.play && local.playing != previous.playing) {
            if (local.playing) {
                if (config.position)
                    jackLocate(client,
                        static_cast<jack_nframes_t>(
                            std::clamp((local.seconds - config.offsetMs / 1000) * rate, 0.0, 4294967295.0)));
                jackStart(client);
            } else
                jackStop(client);
        }
        return;
    }
    jack_position_t p {};
    const auto state = jackQuery(client, &p);
    if (p.frame_rate == 0)
        return;
    lastSignal = nowSeconds();
    incoming = local;
    incoming.seconds = static_cast<double>(p.frame) / p.frame_rate;
    incomingAt = lastSignal;
    incoming.playing = state == JackTransportRolling;
    remoteRunning = incoming.playing;
    pendingPlay = true;
    pendingPosition = true;
    if ((p.valid & JackPositionBBT) && p.beat_type > 0 && std::isfinite(p.beats_per_minute) && p.beats_per_minute > 0) {
        incoming.bpm = std::clamp(p.beats_per_minute * 4 / p.beat_type, 20.0, 400.0);
        incoming.numerator = static_cast<int>(p.beats_per_bar);
        incoming.denominator = static_cast<int>(p.beat_type);
        pendingTempo = true;
    }
    applyIncoming(false);
}
void TransportSync::Impl::tickLink(const State& local)
{
    if (!link || !local.available)
        return;
    const auto time = link->clock().micros();
    auto state = link->captureAppSessionState();
    const auto beat = state.beatAtTime(time, config.quantum);
    bool changed = false;
    Update u;
    u.state = local;
    // Compare with the last state AFTER applying remote changes, preventing feedback.
    if (config.tempo && std::abs(local.bpm - previous.bpm) > 0.01) {
        state.setTempo(local.bpm, time);
        changed = true;
    } else if (config.tempo && std::abs(state.tempo() - local.bpm) > 0.01) {
        u.state.bpm = state.tempo();
        u.tempo = true;
    }
    if (config.tempo && (state.tempo() < 20 || state.tempo() > 400)) {
        state.setTempo(std::clamp(state.tempo(), 20.0, 400.0), time);
        u.state.bpm = std::clamp(u.state.bpm, 20.0, 400.0);
        changed = true;
    }
    const bool remotePlay = state.isPlaying();
    if (config.play && local.playing != previous.playing) {
        state.setIsPlaying(local.playing, time);
        changed = true;
        if (local.playing) {
            linkPending = config.position;
            linkLocalStart = local.seconds;
            linkStartTime = std::ceil(beat / config.quantum) * config.quantum;
        } else
            linkPending = false;
    } else if (config.play && remotePlay != lastLinkPlaying) {
        if (remotePlay) {
            linkLocalStart = local.seconds;
            linkStartTime = std::ceil(beat / config.quantum) * config.quantum;
            linkPending = true;
        } else {
            linkPending = false;
            u.state.playing = false;
            u.play = local.playing;
        }
    }
    lastLinkPlaying = state.isPlaying();
    if (linkPending) {
        if (beat + 0.002 >= linkStartTime) {
            linkPending = false;
            u.state.playing = true;
            u.play = !local.playing;
            u.state.seconds = linkLocalStart + std::max(0.0, beat - linkStartTime) * 60 / u.state.bpm;
            u.position = config.position;
            linkBeatOffset = linkStartTime - linkLocalStart * u.state.bpm / 60;
        } else if (local.playing) {
            u.state.playing = false;
            u.play = true;
        }
    } else if (config.position && local.playing) {
        // Link shares phase, not an absolute song position. Preserve the local bar number.
        if (seeked(local, nowSeconds()))
            linkBeatOffset = std::floor((beat - local.seconds * u.state.bpm / 60) / config.quantum) * config.quantum;
        const auto expected = (beat - linkBeatOffset) * 60 / u.state.bpm;
        const auto period = config.quantum * 60 / u.state.bpm;
        const auto difference = std::remainder(expected - local.seconds, period);
        if (std::abs(difference) > 0.045) {
            u.state.seconds = std::max(0.0, local.seconds + difference);
            u.position = true;
        }
    }
    if (changed)
        link->commitAppSessionState(state);
    if (u.play || u.tempo || u.position)
        apply_(config.target, u);
    info = linkPending
        ? ui::Ls("En attente du prochain quantum…", "Waiting for next quantum…")
        : ui::Ls("Link : pairs égaux, position locale conservée.", "Link: equal peers, local song position retained.");
    if (link->numPeers() > 0)
        lastSignal = nowSeconds();
}

TransportSync::TransportSync(
    juce::PropertiesFile& p, juce::AudioDeviceManager& manager, bool save, Read read, Apply apply)
    : impl_(std::make_unique<Impl>(p, manager, save, std::move(read), std::move(apply)))
{
}
TransportSync::~TransportSync() = default;
bool TransportSync::active() const { return impl_->config.enabled && impl_->connected; }
std::uint64_t TransportSync::receivedCount() const { return impl_->receivedMessages(); }
std::uint64_t TransportSync::sentCount() const { return impl_->sentMessages(); }
juce::String TransportSync::statusText() const
{
    if (!active())
        return {};
    auto text = juce::String(protocols[impl_->config.protocol]);
    if (impl_->config.protocol == 3)
        text << " (" << impl_->linkPeers() << ui::Ls(" pair(s))", " peer(s))");
    else
        text << (impl_->config.send ? ui::Ls(" : CyDAW l'envoie", ": CyDAW sends it") : ui::Ls(" : CyDAW le suit", ": CyDAW follows it"));
    return text;
}
juce::var TransportSync::command(const juce::var& request) { return impl_->command(request); }
void TransportSync::drawButton(bool& visible)
{
    using ui::L;
    if (active())
        ImGui::PushStyleColor(ImGuiCol_Button, theme::vec(theme::palette().accent));
    const auto label
        = active() ? juce::String::fromUTF8("SYNC · ") + protocols[impl_->config.protocol] : juce::String("SYNC");
    if (ImGui::Button(label.toRawUTF8()))
        visible = !visible;
    if (active())
        ImGui::PopStyleColor();
    ui::tooltip(L("Synchronisation du transport : MIDI, JACK, Link", "Transport synchronization: MIDI, JACK, Link"));
}
void TransportSync::draw(bool& visible)
{
    if (visible)
        impl_->draw(visible);
}
void TransportSync::Impl::draw(bool& visible)
{
    using ui::dp;
    using ui::L;
    ImGui::SetNextWindowSize(ImVec2(dp(700), dp(590)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(
            L("Synchronisation du transport###transportSync", "Transport synchronization###transportSync"), &visible)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s",
        L("Partage du transport uniquement. Le périphérique audio reste celui choisi dans les réglages audio.",
            "Transport sharing only. The audio device remains the one chosen in audio settings."));
    ImGui::Separator();
    ImGui::SetNextItemWidth(dp(230));
    ImGui::Combo(L("Protocole", "Protocol"), &draft.protocol, protocols, 4);
    const char* targets[]
        = { "Studio", L("Fonderie (onglet actif)", "Foundry (active tab)"), "Deck A", "Deck B", "Deck C", "Deck D", ui::L("Cinéma", "Cinema") };
    ImGui::SetNextItemWidth(dp(230));
    ImGui::Combo(L("Transport à relier", "Transport to connect"), &draft.target, targets, 7);
    if (draft.protocol == 3)
        ImGui::TextDisabled("%s",
            L("Link : chaque pair peut proposer le tempo et la lecture.",
                "Link: every peer can propose tempo and playback."));
    else {
        int role = draft.send ? 1 : 0;
        const char* roles[] = { L("Suivre (recevoir)", "Follow (receive)"), L("Piloter (envoyer)", "Lead (send)") };
        ImGui::SetNextItemWidth(dp(230));
        if (ImGui::Combo(L("Direction", "Direction"), &role, roles, 2))
            draft.send = role == 1;
    }
    if (draft.protocol < 2) {
        auto& ports = draft.send ? outputs : inputs;
        auto& selected = draft.send ? draft.output : draft.input;
        juce::String name = ui::Ls("Choisir un port…", "Choose a port…");
        for (const auto& p : ports)
            if (p.identifier == selected)
                name = p.name;
        ImGui::SetNextItemWidth(dp(350));
        if (ImGui::BeginCombo(
                draft.send ? L("Sortie MIDI", "MIDI output") : L("Entrée MIDI", "MIDI input"), name.toRawUTF8())) {
            for (const auto& p : ports)
                if (ImGui::Selectable((p.name + "##" + p.identifier).toRawUTF8(), p.identifier == selected))
                    selected = p.identifier;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Actualiser", "Refresh")))
            refreshDevices();
        ImGui::TextWrapped("%s",
            L("Les ports MIDI virtuels et réseau installés dans Windows apparaissent dans cette liste.",
                "Virtual and network MIDI ports installed in Windows appear in this list."));
    }
    if (draft.protocol == 2) {
        auto edit = [](const char* name, juce::String& value) {
            char buffer[1024] {};
            value.copyToUTF8(buffer, sizeof(buffer));
            ImGui::SetNextItemWidth(ui::dp(360));
            if (ImGui::InputText(name, buffer, sizeof(buffer)))
                value = juce::String::fromUTF8(buffer);
        };
        edit(L("Bibliothèque JACK (facultatif)", "JACK library (optional)"), draft.jackLibrary);
        edit(L("Serveur JACK (vide : défaut)", "JACK server (empty: default)"), draft.server);
        ImGui::TextWrapped("%s",
            L("Démarrez JACK avant de connecter. CyDAW ouvre un client sans port audio. Envoi du tempo : maître "
              "temporel uniquement si la place est libre.",
                "Start JACK before connecting. CyDAW opens a client with no audio ports. Tempo output acquires "
                "timebase ownership only if free."));
    }
    ImGui::SeparatorText(L("Éléments partagés", "Shared elements"));
    ImGui::BeginDisabled(draft.protocol == 1);
    ImGui::Checkbox(L("Tempo", "Tempo"), &draft.tempo);
    ImGui::EndDisabled();
    if (draft.protocol == 1)
        ui::tooltip(L(
            "MTC transmet une position temporelle, pas un tempo musical.", "MTC carries timecode, not musical tempo."));
    ImGui::SameLine();
    ImGui::Checkbox(L("Lecture / arrêt", "Play / stop"), &draft.play);
    ImGui::SameLine();
    ImGui::Checkbox(
        draft.protocol == 3 ? L("Calage de phase", "Phase alignment") : L("Position", "Position"), &draft.position);
    if (draft.protocol == 3) {
        ImGui::SetNextItemWidth(dp(120));
        ImGui::InputDouble("Quantum", &draft.quantum, 1, 4, "%.0f");
        ImGui::TextWrapped("%s",
            L("Link partage les temps et la phase, pas l'emplacement absolu dans le morceau. Les départs sont calés au "
              "prochain quantum.",
                "Link shares beats and phase, not absolute song position. Starts align to the next quantum."));
    }
    if (draft.protocol == 1) {
        const char* rates[] = { "24 fps", "25 fps", "29.97 drop-frame", "30 fps" };
        ImGui::SetNextItemWidth(dp(180));
        ImGui::Combo(L("Cadence en sortie", "Output frame rate"), &draft.rate, rates, 4);
        ImGui::SetNextItemWidth(dp(100));
        ImGui::InputInt(L("ID MMC (127 = tous)", "MMC ID (127 = all)"), &draft.device);
    }
    ImGui::BeginDisabled(draft.protocol == 3);
    ImGui::SetNextItemWidth(dp(130));
    ImGui::InputDouble(L("Décalage (ms)", "Offset (ms)"), &draft.offsetMs, 1, 10, "%.1f");
    ImGui::EndDisabled();
    if (draft.protocol != 3 && !draft.send)
        ImGui::Checkbox(L("Arrêter à la perte du signal", "Stop on signal loss"), &draft.stopOnLoss);
    ImGui::Separator();
    if (ImGui::Button(config.enabled ? L("Appliquer / reconnecter", "Apply / reconnect") : L("Activer", "Enable"))) {
        auto c = draft;
        c.enabled = true;
        auto e = parse(json(c), c);
        if (e.isEmpty())
            configure(c);
        else
            error = e;
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Désactiver", "Disable"))) {
        auto c = config;
        c.enabled = false;
        configure(c);
    }
    ImGui::Text("%s : %s", L("État", "State"),
        config.enabled && connected ? L("Connecté", "Connected") : L("Désactivé", "Disabled"));
    if (link)
        ImGui::Text("%s : %d", L("Pairs Link", "Link peers"), static_cast<int>(link->numPeers()));
    else if (config.enabled && !config.send)
        ImGui::Text("%s : %s", L("Signal", "Signal"),
            lastSignal > 0 && nowSeconds() - lastSignal < 1 ? L("Reçu", "Received") : L("En attente", "Waiting"));
    ImGui::Text("MIDI RX %llu / TX %llu", static_cast<unsigned long long>(rxCount.load()),
        static_cast<unsigned long long>(txCount.load()));
    if (error.isNotEmpty())
        ImGui::TextWrapped("%s", error.toRawUTF8());
    if (info.isNotEmpty())
        ImGui::TextWrapped("%s", info.toRawUTF8());
    ImGui::TextDisabled("%s",
        L("Réglages mémorisés ; activation manuelle à chaque lancement.",
            "Settings retained; enable manually after each launch."));
    ImGui::TextDisabled("%s",
        L("Précision liée aux pilotes et au fil d'interface ; pas de verrouillage audio à l'échantillon.",
            "Timing depends on drivers and the UI thread; no sample-locked audio."));
    ImGui::End();
}
} // namespace cydaw::v2
