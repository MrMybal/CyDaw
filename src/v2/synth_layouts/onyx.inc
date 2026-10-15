// Onyx: its four oscillators (each its bent wave drawn) above their
// envelopes, then the phase-modulation matrix (a row for each oscillator
// that sends, a column for each that receives), the filter, the volume and
// the voices; on a second page what the four make together (drawn) with the
// pitch and the unison, the filter envelope and the LFO.
Layout onyx()
{
    static const char* const onyxKeys[4][14] = {
        { "osc1.mode", "osc1.amount", "osc1.level", "osc1.ratio", "osc1.fine", "osc1.fixed", "osc1.hz", "osc1.phase", "osc1.env.attack",
          "osc1.env.decay", "osc1.env.sustain", "osc1.env.release", "osc1.env.depth", "osc1.env.level" },
        { "osc2.mode", "osc2.amount", "osc2.level", "osc2.ratio", "osc2.fine", "osc2.fixed", "osc2.hz", "osc2.phase", "osc2.env.attack",
          "osc2.env.decay", "osc2.env.sustain", "osc2.env.release", "osc2.env.depth", "osc2.env.level" },
        { "osc3.mode", "osc3.amount", "osc3.level", "osc3.ratio", "osc3.fine", "osc3.fixed", "osc3.hz", "osc3.phase", "osc3.env.attack",
          "osc3.env.decay", "osc3.env.sustain", "osc3.env.release", "osc3.env.depth", "osc3.env.level" },
        { "osc4.mode", "osc4.amount", "osc4.level", "osc4.ratio", "osc4.fine", "osc4.fixed", "osc4.hz", "osc4.phase", "osc4.env.attack",
          "osc4.env.decay", "osc4.env.sustain", "osc4.env.release", "osc4.env.depth", "osc4.env.level" }
    };
    static const char* const onyxParts[4] = { "osc1", "osc2", "osc3", "osc4" };
    static const char* const onyxNames[4] = { "OSC 1", "OSC 2", "OSC 3", "OSC 4" };
    static const char* const onyxEnvelopes[4] = { "osc1.env", "osc2.env", "osc3.env", "osc4.env" };
    static const char* const onyxEnvelopeIds[4] = { "osc1Env", "osc2Env", "osc3Env", "osc4Env" };
    static const char* const onyxEnvelopeFrench[4] = { "ENV. 1 · DISTORSION", "ENV. 2 · DISTORSION", "ENV. 3 · DISTORSION",
                                                       "ENV. 4 · DISTORSION" };
    static const char* const onyxEnvelopeEnglish[4] = { "ENV 1 · DISTORTION", "ENV 2 · DISTORTION", "ENV 3 · DISTORTION",
                                                        "ENV 4 · DISTORTION" };
    static const char* const onyxMatrixKeys[16] = { "pm.11", "pm.12", "pm.13", "pm.14", "pm.21", "pm.22", "pm.23", "pm.24",
                                                    "pm.31", "pm.32", "pm.33", "pm.34", "pm.41", "pm.42", "pm.43", "pm.44" };
    static const char* const onyxMatrixFrench[16] = { "RÉTRO 1", "1 → 2", "1 → 3", "1 → 4", "2 → 1", "RÉTRO 2", "2 → 3", "2 → 4",
                                                      "3 → 1", "3 → 2", "RÉTRO 3", "3 → 4", "4 → 1", "4 → 2", "4 → 3", "RÉTRO 4" };
    static const char* const onyxMatrixEnglish[16] = { "FDBK 1", "1 → 2", "1 → 3", "1 → 4", "2 → 1", "FDBK 2", "2 → 3", "2 → 4",
                                                       "3 → 1", "3 → 2", "FDBK 3", "3 → 4", "4 → 1", "4 → 2", "4 → 3", "FDBK 4" };
    // An oscillator: how it bends, how far, how loud; its pitch (a ratio of
    // the note, or a fixed frequency) and its phase.
    const auto oscillator = [](const int index) {
        const auto* k = onyxKeys[index];
        return Panel { onyxParts[index], onyxNames[index], onyxNames[index], 1 + index, 1.0f, Picture::wave, onyxParts[index], nullptr,
                       { { k[0] },
                         { k[1], "QUANTITÉ", "AMOUNT" },
                         { k[2], "NIVEAU", "LEVEL" },
                         { k[5], "Fréq. fixe", "Fixed freq.", nullptr, 0.0f, 0.0f, 0.0f, true },
                         { k[3], "RAPPORT", "RATIO", k[5], 0.0f, 0.0f, 0.0f, false, true },
                         { k[4], "FIN", "FINE", k[5], 0.0f, 0.0f, 0.0f, false, true },
                         { k[6], "FRÉQ.", "FREQ", k[5], 1.0f, 1.0f, 0.0f, false, true },
                         { k[7], "PHASE", "PHASE" } } };
    };
    // Its envelope: the bend it moves, and its level if it shapes it too.
    const auto envelope = [](const int index) {
        const auto* k = onyxKeys[index];
        return Panel { onyxEnvelopeIds[index], onyxEnvelopeFrench[index], onyxEnvelopeEnglish[index], 1 + index, 1.0f, Picture::envelope,
                       onyxEnvelopes[index], nullptr,
                       { { k[8], "ATTAQUE", "ATTACK" },
                         { k[9], "DÉCLIN", "DECAY" },
                         { k[10], "MAINTIEN", "SUSTAIN" },
                         { k[11], "RELÂCHE", "RELEASE" },
                         { k[12], "→ DIST.", "→ DIST", nullptr, 0.0f, 0.0f, 0.0f, true },
                         { k[13], "→ NIVEAU", "→ LEVEL" } } };
    };
    std::vector<Control> matrix;
    for (int at = 0; at < 16; ++at)
        matrix.push_back({ onyxMatrixKeys[at], onyxMatrixFrench[at], onyxMatrixEnglish[at], nullptr, 0.0f, 0.0f, 0.0f, at % 4 == 0 && at > 0 });

    Layout layout;
    layout.colour = IM_COL32(0xd6, 0x8c, 0xff, 0xff);
    layout.title = "ONYX";
    Page synthPage { "SYNTHÉ", "SYNTH", {} };
    synthPage.rows = {
        { oscillator(0), oscillator(1), oscillator(2), oscillator(3) },
        { envelope(0), envelope(1), envelope(2), envelope(3) },
        {
            { "matrix", "MODULATION DE PHASE (DE → VERS)", "PHASE MODULATION (FROM → TO)", 6, 1.3f, Picture::none, nullptr, nullptr,
              std::move(matrix) },
            { "filter", "FILTRE", "FILTER", 7, 1.5f, Picture::filter, "f", nullptr,
              { { "f.type" },
                { "f.cutoff", "FRÉQUENCE", "CUTOFF" },
                { "f.resonance", "RÉSONANCE", "RESONANCE" },
                { "filter.env", "ENVELOPPE", "ENVELOPE" },
                { "filter.key", "CLAVIER", "KEY" },
                { "filter.drive", "SATURATION", "DRIVE" } } },
            { "ampEnv", "ENV. VOLUME", "AMP ENV", 8, 1.15f, Picture::envelope, "amp", nullptr,
              { { "amp.attack", "ATTAQUE", "ATTACK" },
                { "amp.decay", "DÉCLIN", "DECAY" },
                { "amp.sustain", "MAINTIEN", "SUSTAIN" },
                { "amp.release", "RELÂCHE", "RELEASE" },
                { "amp.velocity", "VÉLOCITÉ", "VELOCITY" },
                { "distortion.velocity", "VÉL. → DIST.", "VEL → DIST" } } },
            { "voices", "VOIX", "VOICES", 0, 1.0f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
        },
    };
    Page modulationPage { "HAUTEUR ET MODULATION", "PITCH & MODULATION", {} };
    modulationPage.rows = {
        {
            { "pitch", "HAUTEUR ET UNISSON", "PITCH & UNISON", 5, 1.3f, Picture::wave, "out", nullptr,
              { { "octave", "OCTAVE", "OCTAVE" },
                { "phase.random", "PHASE ALÉA.", "RND PHASE" },
                { "unison.voices", "UNISSON", "UNISON", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "unison.detune", "DÉSACC.", "DETUNE", "unison.voices", 2.0f, 4.0f },
                { "unison.spread", "LARGEUR", "SPREAD", "unison.voices", 2.0f, 4.0f } } },
            { "filterEnv", "ENV. FILTRE", "FILTER ENV", 8, 1.0f, Picture::envelope, "fenv", nullptr,
              { { "fenv.attack", "ATTAQUE", "ATTACK" },
                { "fenv.decay", "DÉCLIN", "DECAY" },
                { "fenv.sustain", "MAINTIEN", "SUSTAIN" },
                { "fenv.release", "RELÂCHE", "RELEASE" } } },
            { "lfo", "LFO", "LFO", 9, 1.4f, Picture::lfo, "lfo1", nullptr,
              { { "lfo1.shape" },
                { "lfo1.rate", "VITESSE", "RATE", "lfo1.sync", 0.0f, 0.0f },
                { "lfo1.sync", "SYNCHRO", "SYNC" },
                { "lfo1.delay", "RETARD", "DELAY" },
                { "lfo1.pitch", "→ HAUTEUR", "→ PITCH", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "lfo1.distortion", "→ DIST.", "→ DIST" },
                { "lfo1.cutoff", "→ FILTRE", "→ CUTOFF" },
                { "mod.wheel", "MOL. → DIST.", "WHEEL → DIST" } } },
        },
    };
    layout.pages = { std::move(synthPage), std::move(modulationPage) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "distortionCard", "DISTORSION", "DISTORTION", 1, 1.0f, Picture::wave, "out", nullptr,
          { { "osc1.mode" }, { "osc1.amount", "DIST. 1", "DIST 1" }, { "osc2.amount", "DIST. 2", "DIST 2" }, { "osc1.env.depth", "ENV 1", "ENV 1" }, { "osc2.env.depth", "ENV 2", "ENV 2" } } },
        { "filterCard", "FILTRE", "FILTER", 7, 1.0f, Picture::filter, "f", nullptr,
          { { "f.type" }, { "f.cutoff", "FRÉQ.", "CUTOFF" }, { "f.resonance", "RÉSO.", "RESO" }, { "filter.env", "ENV", "ENV" } } },
        { "ampCard", "ENVELOPPE", "ENVELOPE", 8, 1.0f, Picture::envelope, "amp", nullptr,
          { { "amp.attack", "ATTAQUE", "ATTACK" }, { "amp.decay", "DÉCLIN", "DECAY" }, { "amp.sustain", "MAINTIEN", "SUSTAIN" }, { "amp.release", "RELÂCHE", "RELEASE" } } },
        { "outCard", "VOIX", "VOICE", 0, 1.0f, Picture::none, nullptr, nullptr,
          { { "unison.voices", "UNISSON", "UNISON" }, { "unison.detune", "DÉSACC.", "DETUNE" }, { "lfo1.rate", "LFO", "LFO" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}
