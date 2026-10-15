// Dérive: one page. The voices (one, two across the stereo or four in
// unison), the two oscillators (each drawn as it sounds), the mix and the
// drift; the filter (its curve), the amp envelope, the second envelope (its
// stages, or its cycle when it cycles); the LFO and the three modulation
// slots.
Layout derive()
{
    Layout layout;
    layout.colour = IM_COL32(0xff, 0x8f, 0x6b, 0xff);
    layout.title = "DÉRIVE";
    // Oscillator 1's shape knob, named for what it does to each wave.
    const auto shapeFor = [](const char* french, const char* english, const float wave) {
        return Control { "osc1.shape", french, english, "osc1.wave", wave, wave, 0.0f, false, true };
    };
    // The second envelope's stages, or its cycle: only the ones it plays.
    const auto stage = [](const char* key, const char* french, const char* english, const bool cycling) {
        const auto when = cycling ? 1.0f : 0.0f;
        return Control { key, french, english, "env2.cycle", when, when, 0.0f, false, true };
    };
    static const char* const slotKeys[3][3] = { { "mod1.source", "mod1.dest", "mod1.amount" },
                                                { "mod2.source", "mod2.dest", "mod2.amount" },
                                                { "mod3.source", "mod3.dest", "mod3.amount" } };
    static const char* const slotNames[3] = { "MOD 1", "MOD 2", "MOD 3" };
    std::vector<Control> slots;
    for (int slot = 0; slot < 3; ++slot) {
        slots.push_back({ slotKeys[slot][0], nullptr, nullptr, nullptr, 0.0f, 0.0f, 112.0f, slot > 0 });
        slots.push_back({ slotKeys[slot][1], nullptr, nullptr, nullptr, 0.0f, 0.0f, 136.0f });
        slots.push_back({ slotKeys[slot][2], slotNames[slot], slotNames[slot] });
    }
    Page page { "SYNTHÉ", "SYNTH", {} };
    page.rows = {
        {
            { "voices", "VOIX", "VOICES", 0, 1.1f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" },
                { "unison.mode", nullptr, nullptr, nullptr, 0.0f, 0.0f, 0.0f, true },
                { "unison.thickness", "ÉPAISSEUR", "THICKNESS", "unison.mode", 1.0f, 2.0f } } },
            { "osc1", "OSC 1", "OSC 1", 1, 1.0f, Picture::wave, "osc1", nullptr,
              { { "osc1.wave", nullptr, nullptr, nullptr, 0.0f, 0.0f, 120.0f },
                shapeFor("MORDANT", "BITE", 0.0f),
                shapeFor("PENTE", "SKEW", 1.0f),
                shapeFor("PENTE", "SKEW", 2.0f),
                shapeFor("SATUR.", "DRIVE", 3.0f),
                shapeFor("DÉCALAGE", "OFFSET", 4.0f),
                shapeFor("LARGEUR", "WIDTH", 5.0f),
                shapeFor("CREUX", "GAPS", 6.0f),
                { "osc1.octave", "OCTAVE", "OCTAVE" } } },
            { "osc2", "OSC 2", "OSC 2", 1, 1.2f, Picture::wave, "osc2", nullptr,
              { { "osc2.wave", nullptr, nullptr, nullptr, 0.0f, 0.0f, 120.0f },
                { "osc2.octave", "OCTAVE", "OCTAVE" },
                { "osc2.semi", "DEMI-TON", "SEMI" },
                { "osc2.detune", "DÉSACC.", "DETUNE" } } },
            { "mix", "MÉLANGE", "MIX", 2, 1.2f, Picture::none, nullptr, nullptr,
              { { "osc1.level", "OSC 1", "OSC 1" },
                { "osc2.level", "OSC 2", "OSC 2" },
                { "noise.level", "BRUIT", "NOISE" },
                { "drift", "DÉRIVE", "DRIFT" },
                { "osc1.filter", "Osc 1 → filtre", "Osc 1 → filter", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "osc2.filter", "Osc 2 → filtre", "Osc 2 → filter" } } },
        },
        {
            { "filter", "FILTRE", "FILTER", 3, 1.6f, Picture::filter, "f", nullptr,
              { { "f.type", nullptr, nullptr, nullptr, 0.0f, 0.0f, 140.0f },
                { "f.cutoff", "FRÉQUENCE", "CUTOFF" },
                { "f.resonance", "RÉSONANCE", "RESONANCE" },
                { "filter.hpf", "PASSE-HAUT", "HIGH-PASS" },
                { "filter.env", "ENV 2", "ENV 2" },
                { "filter.lfo", "LFO", "LFO" },
                { "filter.key", "CLAVIER", "KEY" } } },
            { "ampEnv", "ENV. VOLUME", "AMP ENV", 4, 1.0f, Picture::envelope, "amp", nullptr,
              { { "amp.attack", "ATTAQUE", "ATTACK" },
                { "amp.decay", "DÉCLIN", "DECAY" },
                { "amp.sustain", "MAINTIEN", "SUSTAIN" },
                { "amp.release", "RELÂCHE", "RELEASE" },
                { "amp.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "env2", "ENV. 2", "ENV 2", 4, 1.0f, Picture::envelope, "env2", nullptr,
              { { "env2.cycle", "Cyclique", "Cycling" },
                stage("env2.attack", "ATTAQUE", "ATTACK", false),
                stage("env2.decay", "DÉCLIN", "DECAY", false),
                stage("env2.sustain", "MAINTIEN", "SUSTAIN", false),
                stage("env2.release", "RELÂCHE", "RELEASE", false),
                stage("env2.rate", "VITESSE", "RATE", true),
                stage("env2.sync", "SYNCHRO", "SYNC", true),
                stage("env2.tilt", "PENTE", "TILT", true),
                stage("env2.hold", "TENUE", "HOLD", true) } },
        },
        {
            { "lfo1", "LFO", "LFO", 5, 1.0f, Picture::lfo, "lfo1", nullptr,
              { { "lfo1.shape", nullptr, nullptr, nullptr, 0.0f, 0.0f, 128.0f },
                { "lfo1.retrigger", "Redémarre", "Retrigger" },
                { "lfo1.rate", "VITESSE", "RATE", "lfo1.sync", 0.0f, 0.0f, 0.0f, true },
                { "lfo1.sync", "SYNCHRO", "SYNC" },
                { "lfo1.amount", "QUANTITÉ", "AMOUNT" } } },
            { "matrix", "MODULATIONS", "MODULATIONS", 6, 1.7f, Picture::none, nullptr, nullptr, std::move(slots) },
        },
    };
    layout.pages = { std::move(page) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "oscCard", "OSCILLATEURS", "OSCILLATORS", 1, 1.0f, Picture::wave, "osc1", nullptr,
          { { "osc1.wave" }, { "osc1.shape", "FORME", "SHAPE" }, { "osc2.detune", "DÉSACC.", "DETUNE" }, { "osc2.level", "NIV. 2", "LEVEL 2" }, { "noise.level", "BRUIT", "NOISE" }, { "drift", "DÉRIVE", "DRIFT" } } },
        { "filterCard", "FILTRE", "FILTER", 3, 1.0f, Picture::filter, "f", nullptr,
          { { "f.type" }, { "f.cutoff", "FRÉQ.", "CUTOFF" }, { "f.resonance", "RÉSO.", "RESO" }, { "filter.env", "ENV 2", "ENV 2" } } },
        { "ampCard", "ENVELOPPE", "ENVELOPE", 4, 1.0f, Picture::envelope, "amp", nullptr,
          { { "amp.attack", "ATTAQUE", "ATTACK" }, { "amp.decay", "DÉCLIN", "DECAY" }, { "amp.sustain", "MAINTIEN", "SUSTAIN" }, { "amp.release", "RELÂCHE", "RELEASE" } } },
        { "outCard", "LFO · VOIX", "LFO · VOICE", 5, 1.0f, Picture::lfo, "lfo1", nullptr,
          { { "lfo1.rate", "VITESSE", "RATE" }, { "lfo1.amount", "QUANTITÉ", "AMOUNT" }, { "voice.glide", "GLISSÉ", "GLIDE" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}
