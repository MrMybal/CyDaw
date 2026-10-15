// Bronze: its exciters (the mallet; the noise, its envelope drawn), its two
// resonators (each its object drawn ringing), the structure and the LFO, on
// one page.
Layout bronze()
{
    Layout layout;
    layout.colour = IM_COL32(0xd0, 0x8c, 0x4c, 0xff);
    layout.title = "BRONZE";
    static const char* const keys[2][11] = {
        { "r1.type", "r1.quality", "r1.tune", "r1.fine", "r1.decay", "r1.material", "r1.bright", "r1.inharm", "r1.hit", "r1.left",
          "r1.right" },
        { "r2.type", "r2.quality", "r2.tune", "r2.fine", "r2.decay", "r2.material", "r2.bright", "r2.inharm", "r2.hit", "r2.left",
          "r2.right" }
    };
    // A resonator: its object (its number of modes only for the four modal
    // ones), its tuning and decay, its material, then where it is struck and
    // listened to.
    const auto resonator = [](const int index) {
        const auto* k = keys[index];
        return Panel { index == 0 ? "resonator1" : "resonator2",
                       index == 0 ? "RÉSONATEUR 1" : "RÉSONATEUR 2",
                       index == 0 ? "RESONATOR 1" : "RESONATOR 2",
                       3 + index,
                       1.0f,
                       Picture::wave,
                       index == 0 ? "r1" : "r2",
                       nullptr,
                       { { k[0] },
                         { k[1], "MODES", "MODES", k[0], 0.0f, 3.0f },
                         { k[2], "ACCORD", "TUNE", nullptr, 0.0f, 0.0f, 0.0f, true },
                         { k[3], "FIN", "FINE" },
                         { k[4], "DÉCLIN", "DECAY" },
                         { k[5], "MATIÈRE", "MATERIAL" },
                         { k[6], "BRILLANCE", "BRIGHT", nullptr, 0.0f, 0.0f, 0.0f, true },
                         { k[7], "INHARM.", "INHARM" },
                         { k[8], "FRAPPE", "HIT" },
                         { k[9], "ÉCOUTE G", "LISTEN L" },
                         { k[10], "ÉCOUTE D", "LISTEN R" } } };
    };
    Page page { "INSTRUMENT", "INSTRUMENT", {} };
    page.rows = {
        {
            { "voices", "VOIX", "VOICES", 0, 1.0f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
            { "mallet", "MAILLET", "MALLET", 1, 0.9f, Picture::none, nullptr, nullptr,
              { { "mallet.volume", "VOLUME", "VOLUME" },
                { "mallet.stiffness", "DURETÉ", "STIFFNESS" },
                { "mallet.noise", "BRUIT", "NOISE", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "mallet.colour", "COULEUR", "COLOUR" } } },
            { "noise", "BRUIT", "NOISE", 2, 1.7f, Picture::envelope, "noise", nullptr,
              { { "noise.filter" },
                { "noise.volume", "VOLUME", "VOLUME", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "noise.cutoff", "FRÉQUENCE", "CUTOFF" },
                { "noise.resonance", "RÉSONANCE", "RESONANCE" },
                { "noise.key", "CLAVIER", "KEY" },
                { "noise.attack", "ATTAQUE", "ATTACK", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "noise.decay", "DÉCLIN", "DECAY" },
                { "noise.sustain", "MAINTIEN", "SUSTAIN" },
                { "noise.release", "RELÂCHE", "RELEASE" } } },
        },
        { resonator(0), resonator(1) },
        {
            { "structure", "STRUCTURE", "STRUCTURE", 5, 1.2f, Picture::none, nullptr, nullptr,
              { { "structure" },
                { "output.limiter", "Limiteur", "Limiter" },
                { "balance", "BALANCE", "BALANCE", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "release", "RELÂCHE", "RELEASE" },
                { "output.gain", "SORTIE", "OUTPUT" },
                { "velocity.volume", "VÉL. → VOL.", "VEL → VOL", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "velocity.stiffness", "VÉL. → DURETÉ", "VEL → STIFF" } } },
            { "lfo1", "LFO", "LFO", 6, 1.3f, Picture::lfo, "lfo1", nullptr,
              { { "lfo1.shape" },
                { "lfo1.rate", "VITESSE", "RATE", "lfo1.sync", 0.0f, 0.0f, 0.0f, true },
                { "lfo1.sync", "SYNCHRO", "SYNC" },
                { "lfo1.tune", "→ ACCORD", "→ TUNE", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "lfo1.decay", "→ DÉCLIN", "→ DECAY" },
                { "lfo1.hit", "→ FRAPPE", "→ HIT" },
                { "lfo1.volume", "→ VOLUME", "→ VOLUME" } } },
        },
    };
    layout.pages = { std::move(page) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "malletCard", "MAILLET", "MALLET", 1, 1.0f, Picture::none, nullptr, nullptr,
          { { "mallet.volume", "FRAPPE", "STRIKE" }, { "mallet.stiffness", "DURETÉ", "STIFFNESS" }, { "mallet.noise", "BRUIT", "NOISE" }, { "noise.volume", "SOUFFLE", "BREATH" } } },
        { "r1Card", "RÉSONATEUR 1", "RESONATOR 1", 3, 1.0f, Picture::wave, "r1", nullptr,
          { { "r1.type" }, { "r1.decay", "DÉCLIN", "DECAY" }, { "r1.material", "MATIÈRE", "MATERIAL" }, { "r1.bright", "BRILL.", "BRIGHT" } } },
        { "r2Card", "RÉSONATEUR 2", "RESONATOR 2", 4, 1.0f, Picture::wave, "r2", nullptr,
          { { "r2.type" }, { "r2.decay", "DÉCLIN", "DECAY" }, { "r2.tune", "ACCORD", "TUNE" } } },
        { "outCard", "STRUCTURE", "STRUCTURE", 5, 1.0f, Picture::none, nullptr, nullptr,
          { { "structure" }, { "balance", "BALANCE", "BALANCE" }, { "release", "RELÂCHE", "RELEASE" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}
