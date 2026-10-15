// Quartz (included inside SynthLayouts.cpp's unnamed namespace): its four
// operators, each beside its envelope, and the matrix where they modulate one
// another on a page; voices, filter, envelopes, LFO and wheel on another.

// One of Quartz's operators, drawn as it sounds: its ratio (or, fixed, its
// frequency in the same place), its tuning, its share in the output, its
// depth.
Panel quartzOperator(const int op)
{
    static const char* const keys[4][6] = { { "op1.ratio", "op1.hz", "op1.fine", "op1.level", "op1.depth", "op1.fixed" },
                                             { "op2.ratio", "op2.hz", "op2.fine", "op2.level", "op2.depth", "op2.fixed" },
                                             { "op3.ratio", "op3.hz", "op3.fine", "op3.level", "op3.depth", "op3.fixed" },
                                             { "op4.ratio", "op4.hz", "op4.fine", "op4.level", "op4.depth", "op4.fixed" } };
    static const char* const ids[4] = { "op1", "op2", "op3", "op4" };
    static const char* const names[4] = { "OP 1", "OP 2", "OP 3", "OP 4" };
    const auto* k = keys[op];
    return { ids[op], names[op], names[op], 1 + op, 1.0f, Picture::wave, ids[op], nullptr,
             { { k[0], "RAPPORT", "RATIO", k[5], 0.0f, 0.0f, 0.0f, false, true },
               { k[1], "FRÉQ.", "FREQ", k[5], 1.0f, 1.0f, 0.0f, false, true },
               { k[2], "FIN", "FINE" },
               { k[3], "NIVEAU", "LEVEL" },
               { k[4], "PROFOND.", "DEPTH" },
               { k[5], "Fixe", "Fixed" } } };
}

// One of Quartz's operators' envelopes (its points dragged).
Panel quartzEnvelope(const int op)
{
    static const char* const keys[4][4] = { { "op1.attack", "op1.decay", "op1.sustain", "op1.release" },
                                             { "op2.attack", "op2.decay", "op2.sustain", "op2.release" },
                                             { "op3.attack", "op3.decay", "op3.sustain", "op3.release" },
                                             { "op4.attack", "op4.decay", "op4.sustain", "op4.release" } };
    static const char* const ids[4] = { "op1Env", "op2Env", "op3Env", "op4Env" };
    static const char* const prefixes[4] = { "op1", "op2", "op3", "op4" };
    static const char* const french[4] = { "ENV. OP 1", "ENV. OP 2", "ENV. OP 3", "ENV. OP 4" };
    static const char* const english[4] = { "OP 1 ENV", "OP 2 ENV", "OP 3 ENV", "OP 4 ENV" };
    const auto* k = keys[op];
    return { ids[op], french[op], english[op], 1 + op, 1.0f, Picture::envelope, prefixes[op], nullptr,
             { { k[0], "ATTAQUE", "ATTACK" }, { k[1], "DÉCLIN", "DECAY" }, { k[2], "MAINTIEN", "SUSTAIN" }, { k[3], "RELÂCHE", "RELEASE" } } };
}

// Quartz's matrix: a line for each operator modulated, from operators 1 to 4
// (its own: its feedback) and the noise; each knob in its source's colour.
Panel quartzMatrix()
{
    static const char* const keys[4][5] = { { "m.11", "m.21", "m.31", "m.41", "m.n1" },
                                             { "m.12", "m.22", "m.32", "m.42", "m.n2" },
                                             { "m.13", "m.23", "m.33", "m.43", "m.n3" },
                                             { "m.14", "m.24", "m.34", "m.44", "m.n4" } };
    static const char* const french[4][5] = { { "1→1", "2→1", "3→1", "4→1", "B→1" },
                                               { "1→2", "2→2", "3→2", "4→2", "B→2" },
                                               { "1→3", "2→3", "3→3", "4→3", "B→3" },
                                               { "1→4", "2→4", "3→4", "4→4", "B→4" } };
    static const char* const english[4][5] = { { "1→1", "2→1", "3→1", "4→1", "N→1" },
                                                { "1→2", "2→2", "3→2", "4→2", "N→2" },
                                                { "1→3", "2→3", "3→3", "4→3", "N→3" },
                                                { "1→4", "2→4", "3→4", "4→4", "N→4" } };
    std::vector<Control> controls;
    for (int to = 0; to < 4; ++to)
        for (int from = 0; from < 5; ++from)
            controls.push_back({ keys[to][from], french[to][from], english[to][from], nullptr, 0.0f, 0.0f, 0.0f, from == 0 && to > 0 });
    return { "matrix", "MATRICE", "MATRIX", 5, 1.0f, Picture::none, nullptr, nullptr, std::move(controls) };
}

Layout quartz()
{
    Layout layout;
    layout.colour = IM_COL32(0xf5, 0x9e, 0xd0, 0xff);
    layout.title = "QUARTZ";
    Page operators { "OPÉRATEURS", "OPERATORS", {} };
    operators.rows = {
        { quartzOperator(0), quartzEnvelope(0), quartzOperator(1), quartzEnvelope(1) },
        { quartzOperator(2), quartzEnvelope(2), quartzOperator(3), quartzEnvelope(3) },
        {
            quartzMatrix(),
            { "fm", "FM", "FM", 5, 1.0f, Picture::wave, "out", nullptr,
              { { "ratio.snap", "Rapports entiers", "Whole ratios" },
                { "velocity.depth", "VÉLOCITÉ", "VELOCITY" },
                { "key.depth", "CLAVIER", "KEY" },
                { "key.time", "CLAV. TEMPS", "KEY TIME" },
                { "noise.tone", "BRUIT", "NOISE" } } },
        },
    };
    Page shaping { "FILTRE · MODULATION", "FILTER · MODULATION", {} };
    shaping.rows = {
        {
            { "voices", "VOIX", "VOICES", 0, 0.9f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
            { "filter", "FILTRE", "FILTER", 6, 1.6f, Picture::filter, "f", nullptr,
              { { "f.type" },
                { "f.cutoff", "FRÉQUENCE", "CUTOFF" },
                { "f.resonance", "RÉSONANCE", "RESONANCE" },
                { "filter.drive", "SATURATION", "DRIVE" },
                { "filter.env", "ENVELOPPE", "ENVELOPE" },
                { "filter.key", "CLAVIER", "KEY" },
                { "filter.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "filterEnv", "ENV. FILTRE", "FILTER ENV", 7, 1.0f, Picture::envelope, "fenv", nullptr,
              { { "fenv.attack", "ATTAQUE", "ATTACK" },
                { "fenv.decay", "DÉCLIN", "DECAY" },
                { "fenv.sustain", "MAINTIEN", "SUSTAIN" },
                { "fenv.release", "RELÂCHE", "RELEASE" } } },
        },
        {
            { "ampEnv", "ENV. VOLUME", "AMP ENV", 7, 1.0f, Picture::envelope, "amp", nullptr,
              { { "amp.attack", "ATTAQUE", "ATTACK" },
                { "amp.decay", "DÉCLIN", "DECAY" },
                { "amp.sustain", "MAINTIEN", "SUSTAIN" },
                { "amp.release", "RELÂCHE", "RELEASE" },
                { "amp.velocity", "VÉLOCITÉ", "VELOCITY" } } },
            { "lfo1", "LFO", "LFO", 8, 1.4f, Picture::lfo, "lfo1", nullptr,
              { { "lfo1.shape" },
                { "lfo1.rate", "VITESSE", "RATE", "lfo1.sync", 0.0f, 0.0f },
                { "lfo1.sync", "SYNCHRO", "SYNC" },
                { "lfo1.delay", "RETARD", "DELAY" },
                { "lfo1.pitch", "→ HAUTEUR", "→ PITCH", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "lfo1.depth", "→ PROFOND.", "→ DEPTH" },
                { "lfo1.cutoff", "→ FILTRE", "→ CUTOFF" },
                { "lfo1.amp", "→ VOLUME", "→ VOLUME" } } },
            { "wheel", "MOLETTE", "WHEEL", 8, 0.6f, Picture::none, nullptr, nullptr,
              { { "wheel.vibrato", "VIBRATO", "VIBRATO" }, { "wheel.depth", "→ PROFOND.", "→ DEPTH" } } },
        },
    };
    layout.pages = { std::move(operators), std::move(shaping) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "fmCard", "FM", "FM", 5, 1.0f, Picture::wave, "out", nullptr,
          { { "op2.ratio", "RAPP. 2", "RATIO 2" }, { "m.21", "2→1", "2→1" }, { "op3.ratio", "RAPP. 3", "RATIO 3" }, { "m.31", "3→1", "3→1" }, { "op4.ratio", "RAPP. 4", "RATIO 4" }, { "m.43", "4→3", "4→3" } } },
        { "filterCard", "FILTRE", "FILTER", 6, 1.0f, Picture::filter, "f", nullptr,
          { { "f.type" }, { "f.cutoff", "FRÉQ.", "CUTOFF" }, { "f.resonance", "RÉSO.", "RESO" }, { "filter.env", "ENV", "ENV" } } },
        { "ampCard", "ENVELOPPE", "ENVELOPE", 7, 1.0f, Picture::envelope, "amp", nullptr,
          { { "amp.attack", "ATTAQUE", "ATTACK" }, { "amp.decay", "DÉCLIN", "DECAY" }, { "amp.sustain", "MAINTIEN", "SUSTAIN" }, { "amp.release", "RELÂCHE", "RELEASE" } } },
        { "outCard", "JEU", "PLAY", 8, 1.0f, Picture::none, nullptr, nullptr,
          { { "velocity.depth", "VÉL. → FM", "VEL → FM" }, { "key.depth", "CLAV. → FM", "KEY → FM" }, { "wheel.vibrato", "VIBRATO", "VIBRATO" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}
