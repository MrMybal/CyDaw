#include "ControlTargets.h"

#include "Studio.h"
#include "UiState.h"
#include "Widgets.h"

#include <cmath>

namespace cydaw::v2 {
namespace {

using Kind = ControlTargets::Kind;

// " · " between two names, as a juce::String (inline text is UTF-8).
juce::String middleDot()
{
    return juce::String::fromUTF8(" \xc2\xb7 ");
}

// "track/12/volume" into its parts (a binding keeps its dots).
std::vector<std::string> parts(const std::string& key)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const auto slash = key.find('/', start);
        // A card's binding is the rest, whatever it holds.
        if (out.size() == 2 && out[0] == "param") {
            out.push_back(key.substr(start));
            break;
        }
        out.push_back(key.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return out;
}

double clamp01(const double value)
{
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

// The decks' and the mix's courses, 0..1 each way.
constexpr double volumeLow = -60.0, volumeHigh = 12.0;
constexpr double tempoLow = 20.0, tempoHigh = 300.0;
constexpr double trimLow = -24.0, trimHigh = 12.0;
constexpr double djMasterLow = -24.0, djMasterHigh = 6.0;

double toUnit(const double value, const double low, const double high) { return clamp01((value - low) / (high - low)); }
double fromUnit(const double unit, const double low, const double high) { return low + clamp01(unit) * (high - low); }

int deckIndex(const std::string& letter)
{
    if (letter.size() != 1)
        return -1;
    const auto index = letter[0] >= 'a' ? letter[0] - 'a' : letter[0] - 'A';
    return index >= 0 && index < app::DjEngine::deckCount ? index : -1;
}

struct FoundParameter {
    app::NodeGraphComponent::EditorParameter parameter;
    juce::String card;
};

std::optional<FoundParameter> findParameter(const GraphSlot& slot, const std::string& binding)
{
    for (const auto& node : slot.graph().editorNodes())
        if (!node.boundary)
            for (const auto& parameter : node.parameters)
                if (parameter.binding.toStdString() == binding)
                    return FoundParameter { parameter, node.title };
    for (const auto& parameter : slot.graph().editorGraphParameters())
        if (parameter.binding.toStdString() == binding)
            return FoundParameter { parameter, {} };
    return std::nullopt;
}

const char* deckLetter(const int deck)
{
    static constexpr const char* letters[] { "A", "B", "C", "D" };
    return deck >= 0 && deck < 4 ? letters[deck] : "?";
}

// Whether a toggle target goes on now: a button flips it at its press,
// anything else puts it where it is.
std::optional<bool> toggled(const bool now, const double value, const bool pressed, const bool momentary)
{
    if (momentary)
        return pressed ? std::optional<bool> { !now } : std::nullopt;
    const auto wanted = value >= 0.5;
    return wanted != now ? std::optional<bool> { wanted } : std::nullopt;
}

} // namespace

namespace control_targets {

std::string forTrack(const std::uint64_t trackKey, const char* what)
{
    return "track/" + std::to_string(trackKey) + "/" + what;
}

std::string forMaster(const char* what)
{
    return std::string("master/") + what;
}

std::string forParameter(const std::uint64_t trackKey, const juce::String& binding)
{
    return "param/" + (trackKey == 0 ? std::string("master") : std::to_string(trackKey)) + "/" + binding.toStdString();
}

std::string forDeck(const int deck, const char* what)
{
    return std::string("dj/") + deckLetter(deck) + "/" + what;
}

std::string forTransport(const char* what)
{
    return std::string("transport/") + what;
}

std::string forDj(const char* what)
{
    return std::string("dj/") + what;
}

std::pair<std::string, std::string> forTrackLearn(const Studio& studio, const TrackRef& track, const char* what, const juce::String& label)
{
    if (track.bus != 0 || track.pad >= 0)
        return {};
    if (track.master)
        return { forMaster(what), ("Master" + middleDot() + label).toStdString() };
    if (!studio.isValid(track))
        return {};
    return { forTrack(studio.routingKey(track), what), (studio.trackName(track) + middleDot() + label).toStdString() };
}

} // namespace control_targets

ControlTargets::ControlTargets(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

std::optional<TrackRef> ControlTargets::trackOf(const std::string& part) const
{
    if (part == "selected") {
        if (state_.selectedTrack && (state_.selectedTrack->master || studio_.isValid(*state_.selectedTrack)))
            return state_.selectedTrack;
        return std::nullopt;
    }
    if (part == "master") {
        TrackRef master;
        master.master = true;
        return master;
    }
    char* end = nullptr;
    const auto key = std::strtoull(part.c_str(), &end, 10);
    if (end == part.c_str() || *end != '\0')
        return std::nullopt;
    return studio_.trackForKey(key);
}

std::optional<ControlTargets::Info> ControlTargets::describe(const std::string& key) const
{
    const auto p = parts(key);
    if (p.empty())
        return std::nullopt;
    Info info;
    if (p[0] == "transport" && p.size() == 2) {
        const auto& what = p[1];
        if (what == "play") info = { ui::Ls("Lecture / pause", "Play / pause"), Kind::trigger };
        else if (what == "stop") info = { ui::Ls("Stop", "Stop"), Kind::trigger };
        else if (what == "record") info = { ui::Ls("Enregistrer", "Record"), Kind::trigger };
        else if (what == "loop") info = { ui::Ls("Boucle", "Loop"), Kind::toggle };
        else if (what == "tempo") info = { ui::Ls("Tempo", "Tempo"), Kind::continuous };
        else return std::nullopt;
        info.label = ui::Ls("Transport · ", "Transport · ") + info.label;
        return info;
    }
    if ((p[0] == "track" && p.size() == 3) || (p[0] == "master" && p.size() == 2)) {
        const auto& what = p.back();
        const auto master = p[0] == "master";
        juce::String name = master ? juce::String("Master")
                            : p[1] == "selected" ? ui::Ls("Piste sélectionnée", "Selected track")
                                                 : juce::String();
        if (!master && p[1] != "selected") {
            const auto track = trackOf(p[1]);
            if (!track)
                return std::nullopt;
            name = studio_.trackName(*track);
            info.project = true;
        }
        if (what == "volume") info.label = ui::Ls("Volume", "Volume");
        else if (what == "pan") info.label = ui::Ls("Panoramique", "Pan");
        else if (!master && what == "mute") { info.label = ui::Ls("Muet", "Mute"); info.kind = Kind::toggle; }
        else if (!master && what == "solo") { info.label = "Solo"; info.kind = Kind::toggle; }
        else if (!master && what == "arm") { info.label = ui::Ls("Armer", "Arm"); info.kind = Kind::toggle; }
        else return std::nullopt;
        info.label = name + middleDot() + info.label;
        return info;
    }
    if (p[0] == "param" && p.size() == 3) {
        const auto track = trackOf(p[1]);
        if (!track)
            return std::nullopt;
        const auto* slot = studio_.graphFor(*track);
        const auto found = slot != nullptr ? findParameter(*slot, p[2]) : std::nullopt;
        if (!found)
            return std::nullopt;
        info.label = studio_.trackName(*track) + middleDot() + (found->card.isNotEmpty() ? found->card + middleDot() : juce::String())
                     + found->parameter.label;
        info.project = !track->master;
        return info;
    }
    if (p[0] == "dj") {
        if (p.size() == 2) {
            if (p[1] == "crossfader") info.label = "DJ" + middleDot() + "Crossfader";
            else if (p[1] == "master") info.label = "DJ" + middleDot() + "Master";
            else return std::nullopt;
            return info;
        }
        if (p.size() != 3 || deckIndex(p[1]) < 0)
            return std::nullopt;
        const auto& what = p[2];
        juce::String label;
        if (what == "play") { label = ui::Ls("Lecture", "Play"); info.kind = Kind::toggle; }
        else if (what == "cue") { label = "CUE"; info.kind = Kind::hold; }
        else if (what == "sync") { label = "SYNC"; info.kind = Kind::toggle; }
        else if (what == "gain") label = "GAIN";
        else if (what == "high") label = "HI";
        else if (what == "mid") label = "MID";
        else if (what == "low") label = "LOW";
        else if (what == "filter") label = "FILTER";
        else if (what == "fader") label = "Fader";
        else if (what == "killHigh") { label = "Kill HI"; info.kind = Kind::toggle; }
        else if (what == "killMid") { label = "Kill MID"; info.kind = Kind::toggle; }
        else if (what == "killLow") { label = "Kill LOW"; info.kind = Kind::toggle; }
        else if (what == "pfl") { label = ui::Ls("Pré-écoute", "Cue (headphones)"); info.kind = Kind::toggle; }
        else return std::nullopt;
        info.label = "DJ " + juce::String(deckLetter(deckIndex(p[1]))) + middleDot() + label;
        return info;
    }
    return std::nullopt;
}

std::optional<double> ControlTargets::value(const std::string& key) const
{
    const auto p = parts(key);
    if (p.empty())
        return std::nullopt;
    auto& engine = studio_.engine();
    if (p[0] == "transport" && p.size() == 2) {
        if (p[1] == "play") return engine.isPlaying() ? 1.0 : 0.0;
        if (p[1] == "stop") return engine.isPlaying() ? 0.0 : 1.0;
        if (p[1] == "record") return studio_.isRecording() ? 1.0 : 0.0;
        if (p[1] == "loop") return studio_.loopEnabled() ? 1.0 : 0.0;
        if (p[1] == "tempo") return toUnit(engine.tempo(), tempoLow, tempoHigh);
        return std::nullopt;
    }
    if ((p[0] == "track" && p.size() == 3) || (p[0] == "master" && p.size() == 2)) {
        const auto track = p[0] == "master" ? trackOf("master") : trackOf(p[1]);
        if (!track)
            return std::nullopt;
        const auto mix = studio_.trackMix(*track);
        const auto& what = p.back();
        if (what == "volume") return toUnit(mix.gainDb, volumeLow, volumeHigh);
        if (what == "pan") return clamp01((mix.pan + 1.0) * 0.5);
        if (what == "mute") return mix.muted ? 1.0 : 0.0;
        if (what == "solo") return mix.solo ? 1.0 : 0.0;
        if (what == "arm") return mix.armed ? 1.0 : 0.0;
        return std::nullopt;
    }
    if (p[0] == "param" && p.size() == 3) {
        const auto track = trackOf(p[1]);
        const auto* slot = track ? studio_.graphFor(*track) : nullptr;
        const auto found = slot != nullptr ? findParameter(*slot, p[2]) : std::nullopt;
        if (!found)
            return std::nullopt;
        const auto& parameter = found->parameter;
        const auto low = parameter.minimum, high = parameter.maximum;
        if (high == low)
            return 0.0;
        if (low > 0.0 && high > low && studio_.parameterIsLogarithmic(*track, parameter.binding))
            return clamp01(std::log(std::max(low, parameter.value) / low) / std::log(high / low));
        return toUnit(parameter.value, low, high);
    }
    if (p[0] == "dj") {
        auto& dj = engine.dj();
        if (p.size() == 2) {
            if (p[1] == "crossfader") return clamp01(dj.crossfader());
            if (p[1] == "master") return toUnit(dj.masterGainDb(), djMasterLow, djMasterHigh);
            return std::nullopt;
        }
        const auto deck = p.size() == 3 ? deckIndex(p[1]) : -1;
        if (deck < 0)
            return std::nullopt;
        const auto channel = dj.channel(deck);
        const auto state = dj.deck(deck);
        const auto& what = p[2];
        if (what == "play") return state.playing ? 1.0 : 0.0;
        if (what == "cue") return 0.0;
        if (what == "sync") return state.sync ? 1.0 : 0.0;
        if (what == "gain") return toUnit(channel.trimDb, trimLow, trimHigh);
        if (what == "high") return clamp01(channel.high);
        if (what == "mid") return clamp01(channel.mid);
        if (what == "low") return clamp01(channel.low);
        if (what == "filter") return clamp01((channel.filter + 1.0) * 0.5);
        if (what == "fader") return clamp01(channel.fader);
        if (what == "killHigh") return channel.killHigh ? 1.0 : 0.0;
        if (what == "killMid") return channel.killMid ? 1.0 : 0.0;
        if (what == "killLow") return channel.killLow ? 1.0 : 0.0;
        if (what == "pfl") return channel.cue ? 1.0 : 0.0;
    }
    return std::nullopt;
}

bool ControlTargets::apply(const std::string& key, const double unit, const bool pressed, const bool released, const bool momentary)
{
    const auto p = parts(key);
    if (p.empty())
        return false;
    auto& engine = studio_.engine();
    const auto value = clamp01(unit);
    if (p[0] == "transport" && p.size() == 2) {
        if (p[1] == "tempo") {
            engine.setTempo(fromUnit(value, tempoLow, tempoHigh));
            studio_.markDirty();
            return true;
        }
        if (p[1] == "loop") {
            if (const auto on = toggled(studio_.loopEnabled(), value, pressed, momentary))
                studio_.setLoopEnabled(*on);
            return true;
        }
        if (!pressed)
            return true;
        if (p[1] == "play")
            studio_.togglePlay();
        else if (p[1] == "stop")
            studio_.stop();
        else if (p[1] == "record") {
            if (studio_.isRecording())
                (void) studio_.stopRecording(true);
            else
                (void) studio_.startRecording(state_.selectedTrack, 0);
        } else
            return false;
        return true;
    }
    if ((p[0] == "track" && p.size() == 3) || (p[0] == "master" && p.size() == 2)) {
        const auto track = p[0] == "master" ? trackOf("master") : trackOf(p[1]);
        if (!track)
            return false;
        auto mix = studio_.trackMix(*track);
        const auto& what = p.back();
        if (what == "volume")
            mix.gainDb = static_cast<float>(fromUnit(value, volumeLow, volumeHigh));
        else if (what == "pan")
            mix.pan = static_cast<float>(value * 2.0 - 1.0);
        else if (what == "mute" || what == "solo" || what == "arm") {
            auto& flag = what == "mute" ? mix.muted : what == "solo" ? mix.solo : mix.armed;
            const auto on = toggled(flag, value, pressed, momentary);
            if (!on)
                return true;
            flag = *on;
        } else
            return false;
        studio_.setTrackMix(*track, mix);
        return true;
    }
    if (p[0] == "param" && p.size() == 3) {
        const auto track = trackOf(p[1]);
        auto* slot = track ? studio_.graphFor(*track) : nullptr;
        const auto found = slot != nullptr ? findParameter(*slot, p[2]) : std::nullopt;
        if (!found)
            return false;
        const auto& parameter = found->parameter;
        const auto low = parameter.minimum, high = parameter.maximum;
        auto next = fromUnit(value, low, high);
        if (low > 0.0 && high > low && studio_.parameterIsLogarithmic(*track, parameter.binding))
            next = low * std::pow(high / low, value);
        return slot->graph().editorSetParameter(parameter.binding, next);
    }
    if (p[0] == "dj") {
        auto& dj = engine.dj();
        if (p.size() == 2) {
            if (p[1] == "crossfader")
                dj.setCrossfader(static_cast<float>(value));
            else if (p[1] == "master")
                dj.setMasterGainDb(static_cast<float>(fromUnit(value, djMasterLow, djMasterHigh)));
            else
                return false;
            return true;
        }
        const auto deck = p.size() == 3 ? deckIndex(p[1]) : -1;
        if (deck < 0)
            return false;
        const auto& what = p[2];
        const auto state = dj.deck(deck);
        if (what == "play") {
            if (const auto on = toggled(state.playing, value, pressed, momentary))
                dj.setPlaying(deck, *on);
            return true;
        }
        if (what == "cue") {
            if (pressed || released)
                dj.cue(deck, pressed);
            return true;
        }
        if (what == "sync") {
            if (const auto on = toggled(state.sync, value, pressed, momentary))
                dj.setSync(deck, *on);
            return true;
        }
        auto channel = dj.channel(deck);
        if (what == "gain") channel.trimDb = static_cast<float>(fromUnit(value, trimLow, trimHigh));
        else if (what == "high") channel.high = static_cast<float>(value);
        else if (what == "mid") channel.mid = static_cast<float>(value);
        else if (what == "low") channel.low = static_cast<float>(value);
        else if (what == "filter") channel.filter = static_cast<float>(value * 2.0 - 1.0);
        else if (what == "fader") channel.fader = static_cast<float>(value);
        else if (what == "killHigh" || what == "killMid" || what == "killLow" || what == "pfl") {
            auto& flag = what == "killHigh" ? channel.killHigh : what == "killMid" ? channel.killMid
                       : what == "killLow"  ? channel.killLow  : channel.cue;
            const auto on = toggled(flag, value, pressed, momentary);
            if (!on)
                return true;
            flag = *on;
        } else
            return false;
        dj.setChannel(deck, channel);
        return true;
    }
    return false;
}

std::vector<std::string> ControlTargets::list() const
{
    using namespace control_targets;
    std::vector<std::string> out;
    for (const auto* what : { "play", "stop", "record", "loop", "tempo" })
        out.push_back(forTransport(what));
    out.push_back(forMaster("volume"));
    out.push_back(forMaster("pan"));
    for (const auto* what : { "volume", "pan", "mute", "solo", "arm" })
        out.push_back(std::string("track/selected/") + what);
    for (const auto& track : studio_.tracks()) {
        const auto key = studio_.routingKey(track);
        for (const auto* what : { "volume", "pan", "mute", "solo", "arm" })
            out.push_back(forTrack(key, what));
        if (const auto* slot = studio_.graphFor(track))
            for (const auto& node : slot->graph().editorNodes())
                if (!node.boundary)
                    for (const auto& parameter : node.parameters)
                        // The card a modulator belongs to is no setting.
                        if (!parameter.binding.endsWith(".ownerLow") && !parameter.binding.endsWith(".ownerHigh"))
                            out.push_back(forParameter(key, parameter.binding));
    }
    for (int deck = 0; deck < app::DjEngine::deckCount; ++deck)
        for (const auto* what : { "play", "cue", "sync", "gain", "high", "mid", "low", "filter", "fader", "killHigh", "killMid",
                                  "killLow", "pfl" })
            out.push_back(forDeck(deck, what));
    out.push_back(forDj("crossfader"));
    out.push_back(forDj("master"));
    return out;
}

} // namespace cydaw::v2
