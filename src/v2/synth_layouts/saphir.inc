// Saphir: the voices, the algorithm (its routing named, its sound drawn) and
// the global settings; each operator's wave and its envelope, A and B, then C
// and D; on a second page the filter and its envelope, the pitch envelope and
// the LFO. Included inside SynthLayouts.cpp's namespace.

// One of Saphir's operators: its wave drawn at its ratio and level; its
// ratio (or its fixed frequency), its level and what moves it.
Panel saphirOperator(const int index)
{
    static const char* const keys[4][10] = {
        { "a.wave", "a.fixed", "a.coarse", "a.fine", "a.freq", "a.detune", "a.level", "a.velocity", "a.keyScale", "a.lfo" },
        { "b.wave", "b.fixed", "b.coarse", "b.fine", "b.freq", "b.detune", "b.level", "b.velocity", "b.keyScale", "b.lfo" },
        { "c.wave", "c.fixed", "c.coarse", "c.fine", "c.freq", "c.detune", "c.level", "c.velocity", "c.keyScale", "c.lfo" },
        { "d.wave", "d.fixed", "d.coarse", "d.fine", "d.freq", "d.detune", "d.level", "d.velocity", "d.keyScale", "d.lfo" }
    };
    static const char* const ids[4] = { "operatorA", "operatorB", "operatorC", "operatorD" };
    static const char* const frenchNames[4] = { "OPÉRATEUR A", "OPÉRATEUR B", "OPÉRATEUR C", "OPÉRATEUR D" };
    static const char* const englishNames[4] = { "OPERATOR A", "OPERATOR B", "OPERATOR C", "OPERATOR D" };
    static const char* const pictures[4] = { "a", "b", "c", "d" };
    const auto* k = keys[index];
    return { ids[index], frenchNames[index], englishNames[index], 2 + index, 1.3f, Picture::wave, pictures[index], nullptr,
             { { k[0] },
               { k[1], "Fixe", "Fixed" },
               { k[2], "RAPPORT", "RATIO", k[1], 0.0f, 0.0f },
               { k[3], "FIN ‰", "FINE ‰", k[1], 0.0f, 0.0f },
               { k[4], "FRÉQ.", "FREQ", k[1], 1.0f, 1.0f },
               { k[5], "DÉSACC.", "DETUNE" },
               { k[6], "NIVEAU", "LEVEL", nullptr, 0.0f, 0.0f, 0.0f, true },
               { k[7], "VÉLOCITÉ", "VELOCITY" },
               { k[8], "CLAVIER", "KEY" },
               { k[9], "LFO", "LFO" } } };
}

// One of Saphir's operator envelopes: drawn (its points dragged), and its loop.
Panel saphirEnvelope(const int index)
{
    static const char* const keys[4][5] = { { "a.attack", "a.decay", "a.sustain", "a.release", "a.loop" },
                                            { "b.attack", "b.decay", "b.sustain", "b.release", "b.loop" },
                                            { "c.attack", "c.decay", "c.sustain", "c.release", "c.loop" },
                                            { "d.attack", "d.decay", "d.sustain", "d.release", "d.loop" } };
    static const char* const ids[4] = { "envelopeA", "envelopeB", "envelopeC", "envelopeD" };
    static const char* const frenchNames[4] = { "ENV. A", "ENV. B", "ENV. C", "ENV. D" };
    static const char* const englishNames[4] = { "ENV A", "ENV B", "ENV C", "ENV D" };
    static const char* const pictures[4] = { "a", "b", "c", "d" };
    const auto* k = keys[index];
    return { ids[index], frenchNames[index], englishNames[index], 2 + index, 0.95f, Picture::envelope, pictures[index], nullptr,
             { { k[0], "ATTAQUE", "ATTACK" },
               { k[1], "DÉCLIN", "DECAY" },
               { k[2], "MAINTIEN", "SUSTAIN" },
               { k[3], "RELÂCHE", "RELEASE" },
               { k[4], nullptr, nullptr, nullptr, 0.0f, 0.0f, 0.0f, true } } };
}

Layout saphir()
{
    Layout layout;
    layout.colour = IM_COL32(0x5b, 0x8c, 0xff, 0xff);
    layout.title = "SAPHIR";
    Page operators { "OPÉRATEURS", "OPERATORS", {} };
    operators.rows = {
        {
            { "voices", "VOIX", "VOICES", 0, 1.0f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
            { "routing", "ALGORITHME", "ALGORITHM", 1, 1.4f, Picture::wave, "out", nullptr,
              { { "algorithm", nullptr, nullptr, nullptr, 0.0f, 0.0f, 140.0f },
                { "feedback", "RÉTROACT. D", "FEEDBACK D" },
                { "tone", "TIMBRE", "TONE" } } },
            { "global", "GLOBAL", "GLOBAL", 9, 1.0f, Picture::none, nullptr, nullptr,
              { { "spread", "LARGEUR", "SPREAD" },
                { "transpose", "TRANSPO.", "TRANSPOSE" },
                { "time", "DURÉES", "TIME" },
                { "time.key", "DURÉE CLAV.", "TIME KEY" } } },
        },
        { saphirOperator(0), saphirEnvelope(0), saphirOperator(1), saphirEnvelope(1) },
        { saphirOperator(2), saphirEnvelope(2), saphirOperator(3), saphirEnvelope(3) },
    };
    Page shaping { "FILTRE · LFO · HAUTEUR", "FILTER · LFO · PITCH", {} };
    shaping.rows = {
        {
            { "filter", "FILTRE", "FILTER", 6, 1.5f, Picture::filter, "f", nullptr,
              { { "f.type" },
                { "f.cutoff", "FRÉQUENCE", "CUTOFF" },
                { "f.resonance", "RÉSONANCE", "RESONANCE" },
                { "filter.drive", "SATURATION", "DRIVE" },
                { "filter.env", "ENVELOPPE", "ENVELOPE" },
                { "filter.key", "CLAVIER", "KEY" },
                { "filter.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "filterEnv", "ENV. FILTRE", "FILTER ENV", 6, 1.0f, Picture::envelope, "fenv", nullptr,
              { { "fenv.attack", "ATTAQUE", "ATTACK" },
                { "fenv.decay", "DÉCLIN", "DECAY" },
                { "fenv.sustain", "MAINTIEN", "SUSTAIN" },
                { "fenv.release", "RELÂCHE", "RELEASE" } } },
        },
        {
            { "pitchEnv", "ENV. HAUTEUR", "PITCH ENV", 7, 0.8f, Picture::none, nullptr, nullptr,
              { { "penv.amount", "QUANTITÉ", "AMOUNT" },
                { "penv.attack", "ATTAQUE", "ATTACK" },
                { "penv.decay", "DÉCLIN", "DECAY" } } },
            { "lfo", "LFO", "LFO", 8, 1.7f, Picture::lfo, "lfo1", nullptr,
              { { "lfo1.shape" },
                { "lfo1.rate", "VITESSE", "RATE", "lfo1.sync", 0.0f, 0.0f },
                { "lfo1.sync", "SYNCHRO", "SYNC" },
                { "lfo1.delay", "RETARD", "DELAY" },
                { "lfo1.amount", "PROFONDEUR", "DEPTH", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "lfo1.wheel", "MOLETTE", "WHEEL" },
                { "lfo1.pitch", "→ HAUTEUR", "→ PITCH" },
                { "lfo1.cutoff", "→ FILTRE", "→ CUTOFF" } } },
        },
    };
    layout.pages = { std::move(operators), std::move(shaping) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "algorithmCard", "ALGORITHME", "ALGORITHM", 1, 1.0f, Picture::wave, "out", nullptr,
          { { "algorithm" }, { "feedback", "RÉTRO.", "FEEDBACK" }, { "tone", "TIMBRE", "TONE" }, { "time", "DURÉES", "TIMES" } } },
        { "operatorsCard", "OPÉRATEURS", "OPERATORS", 2, 1.0f, Picture::none, nullptr, nullptr,
          { { "b.level", "NIV. B", "LEVEL B" }, { "c.level", "NIV. C", "LEVEL C" }, { "d.level", "NIV. D", "LEVEL D" }, { "b.coarse", "RAPP. B", "RATIO B" }, { "c.coarse", "RAPP. C", "RATIO C" }, { "d.coarse", "RAPP. D", "RATIO D" } } },
        { "filterCard", "FILTRE", "FILTER", 6, 1.0f, Picture::filter, "f", nullptr,
          { { "f.type" }, { "f.cutoff", "FRÉQ.", "CUTOFF" }, { "f.resonance", "RÉSO.", "RESO" }, { "filter.env", "ENV", "ENV" } } },
        { "outCard", "JEU", "PLAY", 9, 1.0f, Picture::none, nullptr, nullptr,
          { { "spread", "LARGEUR", "SPREAD" }, { "lfo1.rate", "LFO", "LFO" }, { "penv.amount", "ENV → HAUT.", "ENV → PITCH" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}
