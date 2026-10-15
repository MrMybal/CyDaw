// Opale: the voices and the two oscillators side by side (each drawn as it
// sounds, its sub and its sync with it); the mixer and the filter (its
// curve); the amp and filter envelopes, the LFO and the mod wheel.
Layout opale()
{
    // One oscillator: its pitch, its shape, its sub and its sync, its unison.
    const auto opaleOscillator = [](const char* id, const char* name, const int index) -> Panel {
        static const char* const keys[2][12] = {
            { "osc1.octave", "osc1.retrigger", "osc1.pitch", "osc1.fine", "osc1.level", "osc1.shape", "osc1.hardsync", "osc1.sub",
              "osc1.subshape", "osc1.unison", "osc1.detune", "osc1.spread" },
            { "osc2.octave", "osc2.retrigger", "osc2.pitch", "osc2.fine", "osc2.level", "osc2.shape", "osc2.hardsync", "osc2.sub",
              "osc2.subshape", "osc2.unison", "osc2.detune", "osc2.spread" }
        };
        const auto* k = keys[index];
        return { id, name, name, 1, 1.5f, Picture::wave, index == 0 ? "osc1" : "osc2", nullptr,
                 { { k[0], nullptr, nullptr, nullptr, 0.0f, 0.0f, 64.0f },
                   { k[1], "R", "R" },
                   { k[2], "HAUTEUR", "PITCH" },
                   { k[3], "FIN", "FINE" },
                   { k[4], "NIVEAU", "LEVEL" },
                   { k[5], "FORME", "SHAPE", nullptr, 0.0f, 0.0f, 0.0f, true },
                   { k[6], "SYNCHRO", "SYNC" },
                   { k[7], "SUB", "SUB" },
                   { k[8], "FORME SUB", "SUB SHAPE", k[7], 0.001f, 1.0f },
                   { k[9], "UNISSON", "UNISON", nullptr, 0.0f, 0.0f, 0.0f, true },
                   { k[10], "DÉSACC.", "DETUNE", k[9], 2.0f, 7.0f },
                   { k[11], "LARGEUR", "SPREAD", k[9], 2.0f, 7.0f } } };
    };
    Layout layout;
    layout.colour = IM_COL32(0xf2, 0xa6, 0xe8, 0xff);
    layout.title = "OPALE";
    Page page { "SYNTHÉ", "SYNTH", {} };
    page.rows = {
        {
            { "voices", "VOIX", "VOICES", 0, 0.9f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
            opaleOscillator("osc1", "OSC 1", 0),
            opaleOscillator("osc2", "OSC 2", 1),
        },
        {
            { "mixer", "MÉLANGEUR", "MIXER", 2, 1.3f, Picture::none, nullptr, nullptr,
              { { "mix.mode" },
                { "mix.balance", "BALANCE", "BALANCE" },
                { "mix.drive", "SATURATION", "DRIVE" },
                { "mix.feedback", "RÉINJECT.", "FEEDBACK" },
                { "noise.level", "BRUIT", "NOISE", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "noise.colour", "COULEUR", "COLOUR", "noise.level", 0.001f, 1.0f },
                { "drift", "DÉRIVE", "DRIFT" },
                { "pitchenv.target", nullptr, nullptr, nullptr, 0.0f, 0.0f, 0.0f, true },
                { "pitchenv.amount", "ENV → HAUT.", "ENV → PITCH" } } },
            { "filter", "FILTRE", "FILTER", 3, 1.6f, Picture::filter, "f", nullptr,
              { { "f.type" },
                { "f.cutoff", "FRÉQUENCE", "CUTOFF" },
                { "f.resonance", "RÉSONANCE", "RESONANCE" },
                { "filter.env", "ENVELOPPE", "ENVELOPE" },
                { "filter.drive", "SATURATION", "DRIVE" },
                { "filter.key", "CLAVIER", "KEY" },
                { "filter.velocity", "VÉLOCITÉ", "VELOCITY" } } },
        },
        {
            { "ampEnv", "ENV. VOLUME", "AMP ENV", 4, 1.0f, Picture::envelope, "amp", nullptr,
              { { "amp.attack", "ATTAQUE", "ATTACK" },
                { "amp.decay", "DÉCLIN", "DECAY" },
                { "amp.sustain", "MAINTIEN", "SUSTAIN" },
                { "amp.release", "RELÂCHE", "RELEASE" },
                { "amp.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "filterEnv", "ENV. FILTRE", "FILTER ENV", 4, 0.85f, Picture::envelope, "fenv", nullptr,
              { { "fenv.attack", "ATTAQUE", "ATTACK" },
                { "fenv.decay", "DÉCLIN", "DECAY" },
                { "fenv.sustain", "MAINTIEN", "SUSTAIN" },
                { "fenv.release", "RELÂCHE", "RELEASE" } } },
            { "lfo", "LFO", "LFO", 5, 1.3f, Picture::lfo, "lfo1", nullptr,
              { { "lfo1.shape" },
                { "lfo1.rate", "VITESSE", "RATE", "lfo1.sync", 0.0f, 0.0f },
                { "lfo1.sync", "SYNCHRO", "SYNC" },
                { "lfo1.delay", "RETARD", "DELAY" },
                { "lfo1.pitch", "→ HAUTEUR", "→ PITCH", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "lfo1.morph", "→ FORMES", "→ SHAPES" },
                { "lfo1.balance", "→ BALANCE", "→ BALANCE" },
                { "lfo1.cutoff", "→ FILTRE", "→ CUTOFF" },
                { "lfo1.amp", "→ VOLUME", "→ VOLUME" } } },
            { "wheel", "MOLETTE", "MOD WHEEL", 5, 0.5f, Picture::none, nullptr, nullptr,
              { { "mod.vibrato", "VIBRATO", "VIBRATO" },
                { "mod.cutoff", "→ FILTRE", "→ CUTOFF" } } },
        },
    };
    layout.pages = { std::move(page) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "oscCard", "OSCILLATEURS", "OSCILLATORS", 1, 1.0f, Picture::wave, "osc1", nullptr,
          { { "osc1.shape", "FORME 1", "SHAPE 1" }, { "osc2.shape", "FORME 2", "SHAPE 2" }, { "mix.balance", "BALANCE", "BALANCE" }, { "osc2.hardsync", "SYNCHRO", "SYNC" }, { "osc1.sub", "SUB", "SUB" }, { "osc1.unison", "UNISSON", "UNISON" } } },
        { "filterCard", "FILTRE", "FILTER", 3, 1.0f, Picture::filter, "f", nullptr,
          { { "f.type" }, { "f.cutoff", "FRÉQ.", "CUTOFF" }, { "f.resonance", "RÉSO.", "RESO" }, { "filter.env", "ENV", "ENV" }, { "filter.drive", "SATUR.", "DRIVE" } } },
        { "ampCard", "ENVELOPPE", "ENVELOPE", 4, 1.0f, Picture::envelope, "amp", nullptr,
          { { "amp.attack", "ATTAQUE", "ATTACK" }, { "amp.decay", "DÉCLIN", "DECAY" }, { "amp.sustain", "MAINTIEN", "SUSTAIN" }, { "amp.release", "RELÂCHE", "RELEASE" } } },
        { "outCard", "LFO · VOIX", "LFO · VOICE", 5, 1.0f, Picture::lfo, "lfo1", nullptr,
          { { "lfo1.rate", "VITESSE", "RATE" }, { "lfo1.cutoff", "→ FILTRE", "→ CUTOFF" }, { "voice.glide", "GLISSÉ", "GLIDE" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}
