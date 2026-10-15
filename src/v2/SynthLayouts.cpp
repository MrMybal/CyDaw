#include "SynthLayouts.h"

#include <string_view>

namespace cydaw::v2::synth_layout {

namespace {

using engine::synth::Model;

// Ambre: voices and the three oscillators; the mix (its wave drawn) and the
// filter (its curve); the two envelopes, the two LFOs.
Layout ambre()
{
    Layout layout;
    layout.colour = IM_COL32(0xff, 0xb0, 0x4a, 0xff);
    layout.title = "AMBRE";
    Page page { "SYNTHÉ", "SYNTH", {} };
    page.rows = {
        {
            { "voices", "VOIX", "VOICES", 0, 1.3f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES" },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
            { "osc1", "OSC 1", "OSC 1", 1, 1.0f, Picture::none, nullptr, nullptr,
              { { "osc1.wave" },
                { "osc1.octave", "OCTAVE", "OCTAVE" },
                { "osc1.pw", "LARGEUR", "WIDTH", "osc1.wave", 1.0f, 1.0f },
                { "osc1.level", "NIVEAU", "LEVEL" } } },
            { "osc2", "OSC 2", "OSC 2", 1, 1.55f, Picture::none, nullptr, nullptr,
              { { "osc2.wave" },
                { "osc2.sync", "Synchro 1", "Sync 1" },
                { "osc2.octave", "OCTAVE", "OCTAVE" },
                { "osc2.semi", "DEMI-TON", "SEMI" },
                { "osc2.fine", "FIN", "FINE" },
                { "osc2.pw", "LARGEUR", "WIDTH", "osc2.wave", 1.0f, 1.0f },
                { "osc2.level", "NIVEAU", "LEVEL" } } },
            { "osc3", "OSC 3", "OSC 3", 1, 1.45f, Picture::none, nullptr, nullptr,
              { { "osc3.wave" },
                { "osc3.octave", "OCTAVE", "OCTAVE" },
                { "osc3.semi", "DEMI-TON", "SEMI" },
                { "osc3.fine", "FIN", "FINE" },
                { "osc3.level", "NIVEAU", "LEVEL" },
                { "fm.amount", "FM → 1", "FM → 1" } } },
        },
        {
            { "mix", "MÉLANGE", "MIX", 1, 1.0f, Picture::wave, nullptr, nullptr,
              { { "noise.level", "BRUIT", "NOISE" },
                { "drift", "DÉRIVE", "DRIFT" },
                { "unison.voices", "UNISSON", "UNISON" },
                { "unison.detune", "DÉSACC.", "DETUNE", "unison.voices", 2.0f, 4.0f },
                { "unison.spread", "LARGEUR", "SPREAD", "unison.voices", 2.0f, 4.0f } } },
            { "filter", "FILTRE", "FILTER", 2, 1.6f, Picture::filter, "filter", nullptr,
              { { "filter.model" },
                { "filter.mode", nullptr, nullptr, "filter.model", 2.0f, 3.0f },
                { "hpf", "PASSE-HAUT", "HIGH-PASS" },
                { "filter.cutoff", "FRÉQUENCE", "CUTOFF" },
                { "filter.resonance", "RÉSONANCE", "RESONANCE" },
                { "filter.drive", "SATURATION", "DRIVE" },
                { "filter.env", "ENVELOPPE", "ENVELOPE" },
                { "filter.key", "CLAVIER", "KEY" },
                { "filter.velocity", "VÉLOCITÉ", "VELOCITY" } } },
        },
        {
            { "ampEnv", "ENV. VOLUME", "AMP ENV", 3, 1.0f, Picture::envelope, "amp", nullptr,
              { { "amp.attack", "ATTAQUE", "ATTACK" },
                { "amp.decay", "DÉCLIN", "DECAY" },
                { "amp.sustain", "MAINTIEN", "SUSTAIN" },
                { "amp.release", "RELÂCHE", "RELEASE" },
                { "amp.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "filterEnv", "ENV. FILTRE", "FILTER ENV", 3, 0.85f, Picture::envelope, "fenv", nullptr,
              { { "fenv.attack", "ATTAQUE", "ATTACK" },
                { "fenv.decay", "DÉCLIN", "DECAY" },
                { "fenv.sustain", "MAINTIEN", "SUSTAIN" },
                { "fenv.release", "RELÂCHE", "RELEASE" } } },
            { "lfo1", "LFO 1", "LFO 1", 4, 1.15f, Picture::lfo, "lfo1", nullptr,
              { { "lfo1.shape" },
                { "lfo1.rate", "VITESSE", "RATE", "lfo1.sync", 0.0f, 0.0f },
                { "lfo1.sync", "SYNCHRO", "SYNC" },
                { "lfo1.delay", "RETARD", "DELAY" },
                { "lfo1.pitch", "→ HAUTEUR", "→ PITCH" },
                { "lfo1.pwm", "→ LARGEUR", "→ WIDTH" },
                { "mod.wheel", "MOLETTE", "WHEEL" } } },
            { "lfo2", "LFO 2", "LFO 2", 4, 1.0f, Picture::lfo, "lfo2", nullptr,
              { { "lfo2.shape" },
                { "lfo2.rate", "VITESSE", "RATE", "lfo2.sync", 0.0f, 0.0f },
                { "lfo2.sync", "SYNCHRO", "SYNC" },
                { "lfo2.cutoff", "→ FILTRE", "→ CUTOFF" },
                { "lfo2.amp", "→ VOLUME", "→ VOLUME" } } },
        },
    };
    layout.pages = { std::move(page) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "oscCard", "OSCILLATEURS", "OSCILLATORS", 1, 1.0f, Picture::wave, nullptr, nullptr,
          { { "osc1.level", "OSC 1", "OSC 1" }, { "osc2.level", "OSC 2", "OSC 2" }, { "osc3.level", "OSC 3", "OSC 3" }, { "noise.level", "BRUIT", "NOISE" }, { "unison.voices", "UNISSON", "UNISON" }, { "drift", "DÉRIVE", "DRIFT" } } },
        { "filterCard", "FILTRE", "FILTER", 2, 1.0f, Picture::filter, "filter", nullptr,
          { { "filter.model" }, { "filter.cutoff", "FRÉQ.", "CUTOFF" }, { "filter.resonance", "RÉSO.", "RESO" }, { "filter.env", "ENV", "ENV" }, { "filter.drive", "SATUR.", "DRIVE" } } },
        { "ampCard", "ENVELOPPE", "ENVELOPE", 3, 1.0f, Picture::envelope, "amp", nullptr,
          { { "amp.attack", "ATTAQUE", "ATTACK" }, { "amp.decay", "DÉCLIN", "DECAY" }, { "amp.sustain", "MAINTIEN", "SUSTAIN" }, { "amp.release", "RELÂCHE", "RELEASE" } } },
        { "outCard", "LFO · VOIX", "LFO · VOICE", 4, 1.0f, Picture::lfo, "lfo1", nullptr,
          { { "lfo1.rate", "VITESSE", "RATE" }, { "lfo1.pitch", "→ HAUTEUR", "→ PITCH" }, { "voice.glide", "GLISSÉ", "GLIDE" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}

// One of Colosse's oscillators: its table and its bending, its pitch, its level.
Panel colosseOscillator(const char* id, const char* name, const int index)
{
    const auto key = [index](const char* rest) {
        static const char* const keys[2][8] = {
            { "osc1.table", "osc1.mode", "osc1.position", "osc1.warp", "osc1.octave", "osc1.semi", "osc1.fine", "osc1.level" },
            { "osc2.table", "osc2.mode", "osc2.position", "osc2.warp", "osc2.octave", "osc2.semi", "osc2.fine", "osc2.level" }
        };
        static const char* const rests[8] = { "table", "mode", "position", "warp", "octave", "semi", "fine", "level" };
        for (int at = 0; at < 8; ++at)
            if (std::string_view(rests[at]) == rest)
                return keys[index - 1][at];
        return keys[index - 1][0];
    };
    return { id, name, name, 1, 1.45f, Picture::wave, index == 1 ? "osc1" : "osc2", nullptr,
             { { key("table") },
               { key("mode") },
               { key("position"), "POSITION", "POSITION", nullptr, 0.0f, 0.0f, 0.0f, true },
               { key("warp"), "DÉFORM.", "WARP", key("mode"), 1.0f, 7.0f },
               { key("octave"), "OCTAVE", "OCTAVE" },
               { key("semi"), "DEMI-TON", "SEMI" },
               { key("fine"), "FIN", "FINE" },
               { key("level"), "NIVEAU", "LEVEL" } } };
}

// Colosse: its oscillators, sources, filters, saturation and envelopes on a
// page; its sequence, LFOs and modulation slots on another.
Layout colosse()
{
    Layout layout;
    layout.colour = IM_COL32(0x4f, 0xd1, 0xff, 0xff);
    layout.title = "COLOSSE";
    Page synthPage { "SYNTHÉ", "SYNTH", {} };
    synthPage.rows = {
        {
            { "voices", "VOIX", "VOICES", 0, 1.0f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
            colosseOscillator("osc1", "OSC 1", 1),
            colosseOscillator("osc2", "OSC 2", 2),
            { "sources", "SOURCES", "SOURCES", 1, 1.25f, Picture::none, nullptr, nullptr,
              { { "pm.ratio", nullptr, nullptr, nullptr, 0.0f, 0.0f, 70.0f },
                { "pm.osc1", "PM → 1", "PM → 1" },
                { "pm.osc2", "PM → 2", "PM → 2" },
                { "sub.level", "SUB", "SUB" },
                { "noise.colour", nullptr, nullptr, nullptr, 0.0f, 0.0f, 0.0f, true },
                { "noise.level", "BRUIT", "NOISE" },
                { "unison.voices", "UNISSON", "UNISON", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "unison.detune", "DÉSACC.", "DETUNE", "unison.voices", 2.0f, 8.0f },
                { "unison.spread", "LARGEUR", "SPREAD", "unison.voices", 2.0f, 8.0f } } },
        },
        {
            { "filters", "FILTRES", "FILTERS", 2, 2.3f, Picture::filter, "f1", "f2",
              { { "f1.type" },
                { "f1.cutoff", "FRÉQ. 1", "CUTOFF 1" },
                { "f1.resonance", "RÉSO. 1", "RESO 1" },
                { "f2.type" },
                { "f2.cutoff", "FRÉQ. 2", "CUTOFF 2" },
                { "f2.resonance", "RÉSO. 2", "RESO 2" },
                { "filter.routing" },
                { "filter.blend", "MÉLANGE", "BLEND" },
                { "filter.key", "CLAVIER", "KEY" },
                { "filter.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "drive", "SATURATION", "DRIVE", 2, 0.75f, Picture::none, nullptr, nullptr,
              { { "drive.type" },
                { "drive.amount", "QUANTITÉ", "AMOUNT", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "drive.mix", "MÉLANGE", "MIX" } } },
        },
        {
            { "ampEnv", "ENV. VOLUME", "AMP ENV", 3, 1.0f, Picture::envelope, "amp", nullptr,
              { { "amp.attack", "ATTAQUE", "ATTACK" },
                { "amp.decay", "DÉCLIN", "DECAY" },
                { "amp.sustain", "MAINTIEN", "SUSTAIN" },
                { "amp.release", "RELÂCHE", "RELEASE" },
                { "amp.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "env2", "ENV. 2", "ENV 2", 3, 0.85f, Picture::envelope, "env2", nullptr,
              { { "env2.attack", "ATTAQUE", "ATTACK" },
                { "env2.decay", "DÉCLIN", "DECAY" },
                { "env2.sustain", "MAINTIEN", "SUSTAIN" },
                { "env2.release", "RELÂCHE", "RELEASE" } } },
            { "env3", "ENV. 3", "ENV 3", 3, 0.85f, Picture::envelope, "env3", nullptr,
              { { "env3.attack", "ATTAQUE", "ATTACK" },
                { "env3.decay", "DÉCLIN", "DECAY" },
                { "env3.sustain", "MAINTIEN", "SUSTAIN" },
                { "env3.release", "RELÂCHE", "RELEASE" } } },
        },
    };
    Page modulationPage { "MODULATION", "MODULATION", {} };
    std::vector<Control> slots;
    for (int slot = 0; slot < 6; ++slot) {
        static const char* const keys[6][3] = { { "mod1.source", "mod1.dest", "mod1.amount" }, { "mod2.source", "mod2.dest", "mod2.amount" },
                                                { "mod3.source", "mod3.dest", "mod3.amount" }, { "mod4.source", "mod4.dest", "mod4.amount" },
                                                { "mod5.source", "mod5.dest", "mod5.amount" }, { "mod6.source", "mod6.dest", "mod6.amount" } };
        static const char* const names[6] = { "MOD 1", "MOD 2", "MOD 3", "MOD 4", "MOD 5", "MOD 6" };
        slots.push_back({ keys[slot][0], nullptr, nullptr, nullptr, 0.0f, 0.0f, 112.0f, slot % 2 == 0 && slot > 0 });
        slots.push_back({ keys[slot][1], nullptr, nullptr, nullptr, 0.0f, 0.0f, 168.0f });
        slots.push_back({ keys[slot][2], names[slot], names[slot] });
    }
    modulationPage.rows = {
        {
            { "sequence", "SÉQUENCE", "SEQUENCE", 5, 2.0f, Picture::steps, "perf", nullptr,
              { { "perf.steps", "PAS", "STEPS" },
                { "perf.sync", "SYNCHRO", "SYNC" },
                { "perf.rate", "VITESSE", "RATE", "perf.sync", 0.0f, 0.0f },
                { "perf.smooth", "LISSAGE", "SMOOTH" } } },
            { "lfo1", "LFO 1", "LFO 1", 4, 1.0f, Picture::lfo, "lfo1", nullptr,
              { { "lfo1.shape" },
                { "lfo1.rate", "VITESSE", "RATE", "lfo1.sync", 0.0f, 0.0f },
                { "lfo1.sync", "SYNCHRO", "SYNC" } } },
            { "lfo2", "LFO 2", "LFO 2", 4, 1.0f, Picture::lfo, "lfo2", nullptr,
              { { "lfo2.shape" },
                { "lfo2.rate", "VITESSE", "RATE", "lfo2.sync", 0.0f, 0.0f },
                { "lfo2.sync", "SYNCHRO", "SYNC" } } },
        },
        {
            { "matrix", "MODULATIONS", "MODULATIONS", 6, 1.0f, Picture::none, nullptr, nullptr, std::move(slots) },
        },
    };
    layout.pages = { std::move(synthPage), std::move(modulationPage) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "osc1Card", "OSC 1", "OSC 1", 1, 1.0f, Picture::wave, "osc1", nullptr,
          { { "osc1.table" }, { "osc1.position", "POSITION", "POSITION" }, { "osc1.warp", "DÉFORM.", "WARP" }, { "osc1.level", "NIVEAU", "LEVEL" } } },
        { "osc2Card", "OSC 2", "OSC 2", 1, 1.0f, Picture::wave, "osc2", nullptr,
          { { "osc2.table" }, { "osc2.position", "POSITION", "POSITION" }, { "osc2.warp", "DÉFORM.", "WARP" }, { "osc2.level", "NIVEAU", "LEVEL" } } },
        { "filterCard", "FILTRES", "FILTERS", 2, 1.0f, Picture::filter, "f1", "f2",
          { { "f1.type" }, { "f1.cutoff", "FRÉQ. 1", "CUTOFF 1" }, { "f1.resonance", "RÉSO. 1", "RESO 1" }, { "f2.cutoff", "FRÉQ. 2", "CUTOFF 2" }, { "drive.amount", "SATUR.", "DRIVE" } } },
        { "ampCard", "ENVELOPPE", "ENVELOPE", 3, 1.0f, Picture::envelope, "amp", nullptr,
          { { "amp.attack", "ATTAQUE", "ATTACK" }, { "amp.decay", "DÉCLIN", "DECAY" }, { "amp.sustain", "MAINTIEN", "SUSTAIN" }, { "amp.release", "RELÂCHE", "RELEASE" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}

// One of Mosaïque's engines: its kind, its pitch, its level, pan and send;
// then only what its kind has.
Panel mosaiqueEngine(const char* id, const char* name, const int engine)
{
    static const char* const keys[2][23] = {
        { "a.type", "a.octave", "a.semi", "a.fine", "a.level", "a.pan", "a.send", "a.wave", "a.pw", "a.sync", "a.table", "a.mode",
          "a.position", "a.warp", "a.partials", "a.bright", "a.parity", "a.stretch", "a.material", "a.exciter", "a.decay", "a.tone",
          "a.strike" },
        { "b.type", "b.octave", "b.semi", "b.fine", "b.level", "b.pan", "b.send", "b.wave", "b.pw", "b.sync", "b.table", "b.mode",
          "b.position", "b.warp", "b.partials", "b.bright", "b.parity", "b.stretch", "b.material", "b.exciter", "b.decay", "b.tone",
          "b.strike" }
    };
    const auto* k = keys[engine];
    const auto only = [k](const char* key, const char* french, const char* english, const float kind, const bool newLine = false) {
        return Control { key, french, english, k[0], kind, kind, 0.0f, newLine, true };
    };
    return { id, name, name, 1 + engine, 1.0f, Picture::wave, engine == 0 ? "a" : "b", nullptr,
             { { k[0] },
               { k[1], "OCTAVE", "OCTAVE" },
               { k[2], "DEMI-TON", "SEMI" },
               { k[3], "FIN", "FINE" },
               { k[4], "NIVEAU", "LEVEL" },
               { k[5], "PANO.", "PAN" },
               { k[6], "→ FILTRE 2", "→ FILTER 2" },
               only(k[7], "FORME", "SHAPE", 0.0f, true),
               only(k[8], "LARGEUR", "WIDTH", 0.0f),
               only(k[9], "SYNCHRO", "SYNC", 0.0f),
               only(k[10], nullptr, nullptr, 1.0f, true),
               only(k[11], nullptr, nullptr, 1.0f),
               only(k[12], "POSITION", "POSITION", 1.0f),
               only(k[13], "DÉFORM.", "WARP", 1.0f),
               only(k[14], "PARTIELS", "PARTIALS", 2.0f, true),
               only(k[15], "BRILLANCE", "BRIGHT", 2.0f),
               only(k[16], "PAIRS", "ODD/EVEN", 2.0f),
               only(k[17], "ÉTIREMENT", "STRETCH", 2.0f),
               only(k[18], nullptr, nullptr, 3.0f, true),
               only(k[19], nullptr, nullptr, 3.0f),
               only(k[20], "RÉSONANCE", "RING", 3.0f),
               only(k[21], "TIMBRE", "TONE", 3.0f),
               only(k[22], "FRAPPE", "STRIKE", 3.0f) } };
}

// Mosaïque: its two engines, voices, sources, filters, volume and macros on
// a page; its envelopes, LFOs, function, randomness and slots on another.
Layout mosaique()
{
    Layout layout;
    layout.colour = IM_COL32(0x6e, 0xe7, 0xc7, 0xff);
    layout.title = "MOSAÏQUE";
    Page engines { "MOTEURS", "ENGINES", {} };
    engines.rows = {
        { mosaiqueEngine("engineA", "MOTEUR A", 0), mosaiqueEngine("engineB", "MOTEUR B", 1) },
        {
            { "voices", "VOIX", "VOICES", 0, 0.9f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
            { "sources", "SOURCES", "SOURCES", 3, 1.0f, Picture::none, nullptr, nullptr,
              { { "noise.colour" },
                { "noise.level", "BRUIT", "NOISE", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "noise.send", "→ FILTRE 2", "→ FILTER 2" },
                { "unison.voices", "UNISSON", "UNISON", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "unison.detune", "DÉSACC.", "DETUNE", "unison.voices", 2.0f, 4.0f },
                { "unison.spread", "LARGEUR", "SPREAD", "unison.voices", 2.0f, 4.0f } } },
            { "filters", "FILTRES", "FILTERS", 4, 2.1f, Picture::filter, "f1", "f2",
              { { "f1.type" },
                { "f1.cutoff", "FRÉQ. 1", "CUTOFF 1" },
                { "f1.resonance", "RÉSO. 1", "RESO 1" },
                { "f2.type" },
                { "f2.cutoff", "FRÉQ. 2", "CUTOFF 2" },
                { "f2.resonance", "RÉSO. 2", "RESO 2" },
                { "filter.routing" },
                { "filter.key", "CLAVIER", "KEY" },
                { "filter.velocity", "VÉLOCITÉ", "VELOCITY" },
                { "drive.type" },
                { "drive.amount", "SATUR.", "DRIVE" } } },
        },
        {
            { "ampEnv", "ENV. VOLUME", "AMP ENV", 5, 1.2f, Picture::envelope, "amp", nullptr,
              { { "amp.attack", "ATTAQUE", "ATTACK" },
                { "amp.decay", "DÉCLIN", "DECAY" },
                { "amp.sustain", "MAINTIEN", "SUSTAIN" },
                { "amp.release", "RELÂCHE", "RELEASE" },
                { "amp.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "macros", "MACROS", "MACROS", 9, 1.0f, Picture::none, nullptr, nullptr,
              { { "macro1", "MACRO 1", "MACRO 1" },
                { "macro2", "MACRO 2", "MACRO 2" },
                { "macro3", "MACRO 3", "MACRO 3" },
                { "macro4", "MACRO 4", "MACRO 4" } } },
        },
    };
    Page modulation { "MODULATION", "MODULATION", {} };
    std::vector<Control> slots;
    for (int slot = 0; slot < 8; ++slot) {
        static const char* const keys[8][3] = { { "mod1.source", "mod1.dest", "mod1.amount" }, { "mod2.source", "mod2.dest", "mod2.amount" },
                                                { "mod3.source", "mod3.dest", "mod3.amount" }, { "mod4.source", "mod4.dest", "mod4.amount" },
                                                { "mod5.source", "mod5.dest", "mod5.amount" }, { "mod6.source", "mod6.dest", "mod6.amount" },
                                                { "mod7.source", "mod7.dest", "mod7.amount" }, { "mod8.source", "mod8.dest", "mod8.amount" } };
        static const char* const names[8] = { "MOD 1", "MOD 2", "MOD 3", "MOD 4", "MOD 5", "MOD 6", "MOD 7", "MOD 8" };
        slots.push_back({ keys[slot][0], nullptr, nullptr, nullptr, 0.0f, 0.0f, 112.0f, slot % 2 == 0 && slot > 0 });
        slots.push_back({ keys[slot][1], nullptr, nullptr, nullptr, 0.0f, 0.0f, 168.0f });
        slots.push_back({ keys[slot][2], names[slot], names[slot] });
    }
    modulation.rows = {
        {
            { "env2", "ENV. 2", "ENV 2", 5, 1.0f, Picture::envelope, "env2", nullptr,
              { { "env2.attack", "ATTAQUE", "ATTACK" },
                { "env2.decay", "DÉCLIN", "DECAY" },
                { "env2.sustain", "MAINTIEN", "SUSTAIN" },
                { "env2.release", "RELÂCHE", "RELEASE" } } },
            { "env3", "ENV. 3", "ENV 3", 5, 1.0f, Picture::envelope, "env3", nullptr,
              { { "env3.attack", "ATTAQUE", "ATTACK" },
                { "env3.decay", "DÉCLIN", "DECAY" },
                { "env3.sustain", "MAINTIEN", "SUSTAIN" },
                { "env3.release", "RELÂCHE", "RELEASE" } } },
            { "lfo1", "LFO 1", "LFO 1", 6, 0.8f, Picture::lfo, "lfo1", nullptr,
              { { "lfo1.shape" },
                { "lfo1.rate", "VITESSE", "RATE", "lfo1.sync", 0.0f, 0.0f, 0.0f, true },
                { "lfo1.sync", "SYNCHRO", "SYNC" } } },
            { "lfo2", "LFO 2", "LFO 2", 6, 0.8f, Picture::lfo, "lfo2", nullptr,
              { { "lfo2.shape" },
                { "lfo2.rate", "VITESSE", "RATE", "lfo2.sync", 0.0f, 0.0f, 0.0f, true },
                { "lfo2.sync", "SYNCHRO", "SYNC" } } },
        },
        {
            { "function", "FONCTION", "FUNCTION", 7, 2.0f, Picture::steps, "fn", nullptr,
              { { "fn.loop", "En boucle", "Looping" },
                { "fn.points", "POINTS", "POINTS" },
                { "fn.sync", "SYNCHRO", "SYNC" },
                { "fn.rate", "VITESSE", "RATE", "fn.sync", 0.0f, 0.0f },
                { "fn.smooth", "LISSAGE", "SMOOTH" } } },
            { "turing", "ALÉATOIRE", "RANDOM", 8, 1.0f, Picture::none, nullptr, nullptr,
              { { "turing.sync", "SYNCHRO", "SYNC" },
                { "turing.rate", "VITESSE", "RATE", "turing.sync", 0.0f, 0.0f },
                { "turing.change", "CHANGEMENT", "CHANGE" },
                { "turing.length", "LONGUEUR", "LENGTH" } } },
        },
        {
            { "matrix", "MODULATIONS", "MODULATIONS", 10, 1.0f, Picture::none, nullptr, nullptr, std::move(slots) },
        },
    };
    layout.pages = { std::move(engines), std::move(modulation) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "enginesCard", "MOTEURS", "ENGINES", 1, 1.0f, Picture::wave, "a", nullptr,
          { { "a.type" }, { "b.type" }, { "a.level", "NIV. A", "LEVEL A" }, { "b.level", "NIV. B", "LEVEL B" } } },
        { "filterCard", "FILTRES", "FILTERS", 4, 1.0f, Picture::filter, "f1", "f2",
          { { "f1.type" }, { "f1.cutoff", "FRÉQ. 1", "CUTOFF 1" }, { "f1.resonance", "RÉSO. 1", "RESO 1" }, { "drive.amount", "SATUR.", "DRIVE" } } },
        { "macrosCard", "MACROS", "MACROS", 9, 1.0f, Picture::none, nullptr, nullptr,
          { { "macro1", "MACRO 1", "MACRO 1" }, { "macro2", "MACRO 2", "MACRO 2" }, { "macro3", "MACRO 3", "MACRO 3" }, { "macro4", "MACRO 4", "MACRO 4" } } },
        { "ampCard", "ENVELOPPE", "ENVELOPE", 5, 1.0f, Picture::envelope, "amp", nullptr,
          { { "amp.attack", "ATTAQUE", "ATTACK" }, { "amp.decay", "DÉCLIN", "DECAY" }, { "amp.sustain", "MAINTIEN", "SUSTAIN" }, { "amp.release", "RELÂCHE", "RELEASE" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}

// One of Nébuleuse's layers: its source, its pitch, its level and pan; its
// table or its grains, as its source is; its chord, its colour, its filter.
Panel nebuleuseLayer(const char* id, const char* name, const int layer)
{
    static const char* const keys[2][22] = {
        { "a.source", "a.level", "a.pan", "a.octave", "a.semi", "a.fine", "a.table", "a.position", "a.texture", "a.scan", "a.travel",
          "a.grain", "a.density", "a.spray", "a.scatter", "a.reverse", "a.harmonia", "a.harmoniaLevel", "a.timbre", "a.cutoff",
          "a.resonance", "a.drift" },
        { "b.source", "b.level", "b.pan", "b.octave", "b.semi", "b.fine", "b.table", "b.position", "b.texture", "b.scan", "b.travel",
          "b.grain", "b.density", "b.spray", "b.scatter", "b.reverse", "b.harmonia", "b.harmoniaLevel", "b.timbre", "b.cutoff",
          "b.resonance", "b.drift" }
    };
    const auto* k = keys[layer];
    const auto only = [k](const char* key, const char* french, const char* english, const float source, const bool newLine = false) {
        return Control { key, french, english, k[0], source, source, 0.0f, newLine, true };
    };
    return { id, name, name, 1 + layer, 1.0f, Picture::wave, layer == 0 ? "a" : "b", nullptr,
             { { k[0] },
               { k[3], "OCTAVE", "OCTAVE" },
               { k[4], "DEMI-TON", "SEMI" },
               { k[5], "FIN", "FINE" },
               { k[1], "NIVEAU", "LEVEL" },
               { k[2], "PANO.", "PAN" },
               only(k[6], nullptr, nullptr, 0.0f, true),
               only(k[7], "POSITION", "POSITION", 0.0f),
               only(k[8], nullptr, nullptr, 1.0f, true),
               only(k[9], "LECTURE", "SCAN", 1.0f),
               only(k[10], "DÉFILEMENT", "TRAVEL", 1.0f),
               only(k[11], "GRAIN", "GRAIN", 1.0f),
               only(k[12], "DENSITÉ", "DENSITY", 1.0f),
               only(k[13], "DISPERSION", "SPRAY", 1.0f),
               only(k[14], "ÉCART", "SCATTER", 1.0f),
               only(k[15], "ENVERS", "REVERSE", 1.0f),
               { k[16], nullptr, nullptr, nullptr, 0.0f, 0.0f, 0.0f, true },
               { k[17], "HARMONIA", "HARMONIA" },
               { k[18], "TIMBRE", "TIMBRE" },
               { k[19], "FILTRE", "FILTER" },
               { k[20], "RÉSO.", "RESO" },
               { k[21], "DÉRIVE", "DRIFT" } } };
}

// Nébuleuse: its two layers, voices, filter, orb and volume on a page; its
// second envelope, LFOs and slots on another.
Layout nebuleuse()
{
    Layout layout;
    layout.colour = IM_COL32(0x9a, 0xd5, 0xff, 0xff);
    layout.title = "NÉBULEUSE";
    Page layers { "COUCHES", "LAYERS", {} };
    layers.rows = {
        { nebuleuseLayer("layerA", "COUCHE A", 0), nebuleuseLayer("layerB", "COUCHE B", 1) },
        {
            { "voices", "VOIX", "VOICES", 0, 0.8f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
            { "filter", "FILTRE", "FILTER", 3, 1.2f, Picture::filter, "f", nullptr,
              { { "f.type" },
                { "f.cutoff", "FRÉQUENCE", "CUTOFF" },
                { "f.resonance", "RÉSONANCE", "RESONANCE" },
                { "filter.key", "CLAVIER", "KEY" },
                { "filter.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "orb", "ORBE", "ORB", 6, 0.8f, Picture::orb, "orb", nullptr,
              { { "orb.x", "X", "X" },
                { "orb.y", "Y", "Y" },
                { "orb.motion", "MOUVEMENT", "MOTION" },
                { "orb.rate", "VITESSE", "RATE" } } },
            { "ampEnv", "ENV. VOLUME", "AMP ENV", 4, 1.0f, Picture::envelope, "amp", nullptr,
              { { "amp.attack", "ATTAQUE", "ATTACK" },
                { "amp.decay", "DÉCLIN", "DECAY" },
                { "amp.sustain", "MAINTIEN", "SUSTAIN" },
                { "amp.release", "RELÂCHE", "RELEASE" },
                { "amp.velocity", "VÉLOCITÉ", "VELOCITY" } } },
        },
    };
    Page modulation { "MODULATION", "MODULATION", {} };
    std::vector<Control> slots;
    for (int slot = 0; slot < 8; ++slot) {
        static const char* const keys[8][3] = { { "mod1.source", "mod1.dest", "mod1.amount" }, { "mod2.source", "mod2.dest", "mod2.amount" },
                                                { "mod3.source", "mod3.dest", "mod3.amount" }, { "mod4.source", "mod4.dest", "mod4.amount" },
                                                { "mod5.source", "mod5.dest", "mod5.amount" }, { "mod6.source", "mod6.dest", "mod6.amount" },
                                                { "mod7.source", "mod7.dest", "mod7.amount" }, { "mod8.source", "mod8.dest", "mod8.amount" } };
        static const char* const names[8] = { "MOD 1", "MOD 2", "MOD 3", "MOD 4", "MOD 5", "MOD 6", "MOD 7", "MOD 8" };
        slots.push_back({ keys[slot][0], nullptr, nullptr, nullptr, 0.0f, 0.0f, 112.0f, slot % 2 == 0 && slot > 0 });
        slots.push_back({ keys[slot][1], nullptr, nullptr, nullptr, 0.0f, 0.0f, 168.0f });
        slots.push_back({ keys[slot][2], names[slot], names[slot] });
    }
    modulation.rows = {
        {
            { "env2", "ENV. 2", "ENV 2", 4, 1.0f, Picture::envelope, "env2", nullptr,
              { { "env2.attack", "ATTAQUE", "ATTACK" },
                { "env2.decay", "DÉCLIN", "DECAY" },
                { "env2.sustain", "MAINTIEN", "SUSTAIN" },
                { "env2.release", "RELÂCHE", "RELEASE" } } },
            { "lfo1", "LFO 1", "LFO 1", 5, 1.0f, Picture::lfo, "lfo1", nullptr,
              { { "lfo1.shape" },
                { "lfo1.rate", "VITESSE", "RATE", "lfo1.sync", 0.0f, 0.0f, 0.0f, true },
                { "lfo1.sync", "SYNCHRO", "SYNC" } } },
            { "lfo2", "LFO 2", "LFO 2", 5, 1.0f, Picture::lfo, "lfo2", nullptr,
              { { "lfo2.shape" },
                { "lfo2.rate", "VITESSE", "RATE", "lfo2.sync", 0.0f, 0.0f, 0.0f, true },
                { "lfo2.sync", "SYNCHRO", "SYNC" } } },
        },
        {
            { "matrix", "MODULATIONS", "MODULATIONS", 7, 1.0f, Picture::none, nullptr, nullptr, std::move(slots) },
        },
    };
    layout.pages = { std::move(layers), std::move(modulation) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "orbCard", "ORBE", "ORB", 6, 1.0f, Picture::orb, "orb", nullptr,
          { { "orb.motion", "MOUV.", "MOTION" }, { "orb.rate", "VITESSE", "RATE" } } },
        { "layersCard", "COUCHES", "LAYERS", 1, 1.0f, Picture::none, nullptr, nullptr,
          { { "a.texture" }, { "a.scan", "LECTURE", "SCAN" }, { "a.grain", "GRAIN", "GRAIN" }, { "a.level", "NIV. A", "LEVEL A" }, { "b.level", "NIV. B", "LEVEL B" } } },
        { "filterCard", "FILTRE", "FILTER", 3, 1.0f, Picture::filter, "f", nullptr,
          { { "f.type" }, { "f.cutoff", "FRÉQ.", "CUTOFF" }, { "f.resonance", "RÉSO.", "RESO" } } },
        { "ampCard", "ENVELOPPE", "ENVELOPE", 4, 1.0f, Picture::envelope, "amp", nullptr,
          { { "amp.attack", "ATTAQUE", "ATTACK" }, { "amp.decay", "DÉCLIN", "DECAY" }, { "amp.sustain", "MAINTIEN", "SUSTAIN" }, { "amp.release", "RELÂCHE", "RELEASE" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}

#include "synth_layouts/opale.inc"

#include "synth_layouts/onyx.inc"

#include "synth_layouts/tirette.inc"

#include "synth_layouts/alliage.inc"

#include "synth_layouts/quartz.inc"

#include "synth_layouts/argile.inc"

#include "synth_layouts/saphir.inc"

#include "synth_layouts/derive.inc"

#include "synth_layouts/bronze.inc"
#include "synth_layouts/silex.inc"
#include "synth_layouts/jade.inc"

} // namespace

const Layout& layoutFor(const Model model)
{
    static const Layout none {};
    static const Layout ambreLayout = ambre();
    static const Layout colosseLayout = colosse();
    static const Layout mosaiqueLayout = mosaique();
    static const Layout nebuleuseLayout = nebuleuse();
    static const Layout bronzeLayout = bronze();
    static const Layout silexLayout = silex();
    static const Layout jadeLayout = jade();
    static const Layout deriveLayout = derive();
    static const Layout saphirLayout = saphir();
    static const Layout argileLayout = argile();
    static const Layout quartzLayout = quartz();
    static const Layout alliageLayout = alliage();
    static const Layout tiretteLayout = tirette();
    static const Layout onyxLayout = onyx();
    static const Layout opaleLayout = opale();
    switch (model) {
    case Model::ambre: return ambreLayout;
    case Model::colosse: return colosseLayout;
    case Model::mosaique: return mosaiqueLayout;
    case Model::nebuleuse: return nebuleuseLayout;
    case Model::bronze: return bronzeLayout;
    case Model::silex: return silexLayout;
    case Model::jade: return jadeLayout;
    case Model::derive: return deriveLayout;
    case Model::saphir: return saphirLayout;
    case Model::argile: return argileLayout;
    case Model::quartz: return quartzLayout;
    case Model::alliage: return alliageLayout;
    case Model::tirette: return tiretteLayout;
    case Model::onyx: return onyxLayout;
    case Model::opale: return opaleLayout;
    default: return none;
    }
}

} // namespace cydaw::v2::synth_layout
