// Alliage: voices, the oscillator module (its wave drawn, its kind, its pitch,
// its two knobs named as the kind reads them) and the mix; the filter module
// (its curve) and the LFO; the volume, filter and pitch envelopes.
Layout alliage()
{
    Layout layout;
    layout.colour = IM_COL32(0xe8, 0xc2, 0x5a, 0xff);
    layout.title = "ALLIAGE";
    // A knob shown only for one kind of oscillator (0 Swarm, 1 Wavetable,
    // 2 Pulse, 3 Sine FM, 4 Supersquare, 5 Sync, 6 Additive, 7 Noise Band),
    // named as that kind reads it.
    const auto only = [](const char* key, const char* french, const char* english, const float kind) {
        return Control { key, french, english, "osc.type", kind, kind, 0.0f, false, true };
    };
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
            { "oscillator", "OSCILLATEUR", "OSCILLATOR", 1, 2.1f, Picture::wave, "osc", nullptr,
              { { "osc.type", nullptr, nullptr, nullptr, 0.0f, 0.0f, 124.0f },
                { "osc.table", nullptr, nullptr, "osc.type", 1.0f, 1.0f, 0.0f, false, true },
                { "osc.mode", nullptr, nullptr, "osc.type", 1.0f, 1.0f, 0.0f, false, true },
                { "osc.ratio", nullptr, nullptr, "osc.type", 3.0f, 3.0f, 72.0f, false, true },
                { "osc.octave", "OCTAVE", "OCTAVE", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "osc.semi", "DEMI-TON", "SEMI" },
                { "osc.fine", "FIN", "FINE" },
                only("osc.timbre", "DÉSACC.", "DETUNE", 0.0f),
                only("osc.timbre", "POSITION", "POSITION", 1.0f),
                only("osc.timbre", "LARG. IMP.", "WIDTH", 2.0f),
                only("osc.timbre", "INDEX", "INDEX", 3.0f),
                only("osc.timbre", "DÉSACC.", "DETUNE", 4.0f),
                only("osc.timbre", "SYNCHRO", "SYNC", 5.0f),
                only("osc.timbre", "PENTE", "TILT", 6.0f),
                only("osc.timbre", "BANDE", "BANDWIDTH", 7.0f),
                only("osc.shape", "MÉLANGE", "MIX", 0.0f),
                only("osc.shape", "DÉFORM.", "WARP", 1.0f),
                only("osc.shape", "PWM", "PWM", 2.0f),
                only("osc.shape", "RÉTRO.", "FEEDBACK", 3.0f),
                only("osc.shape", "LARG. IMP.", "WIDTH", 4.0f),
                only("osc.shape", "FENÊTRE", "WINDOW", 5.0f),
                only("osc.shape", "REGISTRE", "REGISTER", 6.0f),
                only("osc.shape", "HARMON.", "OVERTONES", 7.0f),
                { "osc.env", "ENV. → TIMB.", "ENV → TIMB." },
                { "osc.spread", "LARGEUR", "SPREAD" } } },
            { "mixer", "MÉLANGE", "MIXER", 2, 0.95f, Picture::none, nullptr, nullptr,
              { { "osc.level", "OSC", "OSC" },
                { "sub.level", "SUB", "SUB" },
                { "noise.level", "BRUIT", "NOISE" },
                { "sub.wave", nullptr, nullptr, nullptr, 0.0f, 0.0f, 88.0f, true },
                { "noise.colour", "COULEUR", "COLOUR" } } },
        },
        {
            { "filter", "FILTRE", "FILTER", 3, 1.5f, Picture::filter, "f", nullptr,
              { { "f.type" },
                { "f.cutoff", "FRÉQUENCE", "CUTOFF" },
                { "f.resonance", "RÉSONANCE", "RESONANCE" },
                { "f.drive", "SATURATION", "DRIVE" },
                { "f.env", "ENVELOPPE", "ENVELOPE" },
                { "f.key", "CLAVIER", "KEY" },
                { "f.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "lfo1", "LFO", "LFO", 5, 1.3f, Picture::lfo, "lfo1", nullptr,
              { { "lfo1.shape" },
                { "lfo1.rate", "VITESSE", "RATE", "lfo1.sync", 0.0f, 0.0f },
                { "lfo1.sync", "SYNCHRO", "SYNC" },
                { "lfo1.delay", "RETARD", "DELAY" },
                { "lfo1.pitch", "→ HAUTEUR", "→ PITCH", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "lfo1.timbre", "→ TIMBRE", "→ TIMBRE" },
                { "lfo1.cutoff", "→ FILTRE", "→ CUTOFF" },
                { "lfo1.amp", "→ VOLUME", "→ VOLUME" },
                { "mod.wheel", "MOLETTE", "WHEEL" } } },
        },
        {
            { "ampEnv", "ENV. VOLUME", "AMP ENV", 4, 1.0f, Picture::envelope, "amp", nullptr,
              { { "amp.attack", "ATTAQUE", "ATTACK" },
                { "amp.decay", "DÉCLIN", "DECAY" },
                { "amp.sustain", "MAINTIEN", "SUSTAIN" },
                { "amp.release", "RELÂCHE", "RELEASE" },
                { "amp.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "filterEnv", "ENV. FILTRE", "FILTER ENV", 4, 0.9f, Picture::envelope, "fenv", nullptr,
              { { "fenv.attack", "ATTAQUE", "ATTACK" },
                { "fenv.decay", "DÉCLIN", "DECAY" },
                { "fenv.sustain", "MAINTIEN", "SUSTAIN" },
                { "fenv.release", "RELÂCHE", "RELEASE" } } },
            { "pitchEnv", "ENV. HAUTEUR", "PITCH ENV", 4, 0.55f, Picture::none, nullptr, nullptr,
              { { "pitch.env", "QUANTITÉ", "AMOUNT" },
                { "pitch.decay", "DÉCLIN", "DECAY" } } },
        },
    };
    layout.pages = { std::move(page) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "oscCard", "OSCILLATEUR", "OSCILLATOR", 1, 1.0f, Picture::wave, "osc", nullptr,
          { { "osc.type" }, { "osc.timbre", "TIMBRE", "TIMBRE" }, { "osc.shape", "FORME", "SHAPE" }, { "sub.level", "SUB", "SUB" }, { "noise.level", "BRUIT", "NOISE" } } },
        { "filterCard", "FILTRE", "FILTER", 3, 1.0f, Picture::filter, "f", nullptr,
          { { "f.type" }, { "f.cutoff", "FRÉQ.", "CUTOFF" }, { "f.resonance", "RÉSO.", "RESO" }, { "f.env", "ENV", "ENV" }, { "f.drive", "SATUR.", "DRIVE" } } },
        { "ampCard", "ENVELOPPE", "ENVELOPE", 4, 1.0f, Picture::envelope, "amp", nullptr,
          { { "amp.attack", "ATTAQUE", "ATTACK" }, { "amp.decay", "DÉCLIN", "DECAY" }, { "amp.sustain", "MAINTIEN", "SUSTAIN" }, { "amp.release", "RELÂCHE", "RELEASE" } } },
        { "outCard", "LFO · VOIX", "LFO · VOICE", 5, 1.0f, Picture::lfo, "lfo1", nullptr,
          { { "lfo1.rate", "VITESSE", "RATE" }, { "lfo1.timbre", "→ TIMBRE", "→ TIMBRE" }, { "lfo1.cutoff", "→ FILTRE", "→ CUTOFF" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}
