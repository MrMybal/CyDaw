// Tirette: the voices, the nine drawbars (their sum drawn) and the organ's
// generator on the first row; the percussion, the scanner vibrato and the
// preamp; the rotating speaker and the gate.
Layout tirette()
{
    Layout layout;
    layout.colour = IM_COL32(0xe8, 0xc0, 0x8a, 0xff);
    layout.title = "TIRETTE";
    Page page { "ORGUE", "ORGAN", {} };
    page.rows = {
        {
            { "voices", "VOIX", "VOICES", 0, 0.85f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
            { "drawbars", "TIRETTES", "DRAWBARS", 1, 2.7f, Picture::wave, "bars", nullptr,
              { { "bar1", "16'", "16'" },
                { "bar2", "5⅓'", "5⅓'" },
                { "bar3", "8'", "8'" },
                { "bar4", "4'", "4'" },
                { "bar5", "2⅔'", "2⅔'" },
                { "bar6", "2'", "2'" },
                { "bar7", "1⅗'", "1⅗'" },
                { "bar8", "1⅓'", "1⅓'" },
                { "bar9", "1'", "1'" } } },
            { "organ", "ORGUE", "ORGAN", 4, 0.9f, Picture::none, nullptr, nullptr,
              { { "organ.type", nullptr, nullptr, nullptr, 0.0f, 0.0f, 132.0f },
                { "leakage", "FUITE", "LEAKAGE", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "click", "CLIC", "CLICK" } } },
        },
        {
            { "percussion", "PERCUSSION", "PERCUSSION", 5, 1.3f, Picture::none, nullptr, nullptr,
              { { "perc.on", "Active", "On" },
                { "perc.harmonic", nullptr, nullptr, "perc.on", 1.0f, 1.0f, 96.0f },
                { "perc.decay", nullptr, nullptr, "perc.on", 1.0f, 1.0f, 80.0f, true },
                { "perc.volume", nullptr, nullptr, "perc.on", 1.0f, 1.0f, 80.0f } } },
            { "vibrato", "VIBRATO / CHORUS", "VIBRATO / CHORUS", 6, 0.8f, Picture::none, nullptr, nullptr,
              { { "vibrato.mode", nullptr, nullptr, nullptr, 0.0f, 0.0f, 80.0f },
                { "vibrato.rate", "VITESSE", "RATE", "vibrato.mode", 1.0f, 6.0f, 0.0f, true } } },
            { "preamp", "PRÉAMPLI", "PREAMP", 7, 1.0f, Picture::none, nullptr, nullptr,
              { { "drive.amount", "SATURATION", "DRIVE" },
                { "tone", "TIMBRE", "TONE" },
                { "drive.output", "SORTIE", "OUTPUT" } } },
        },
        {
            { "rotary", "CABINE TOURNANTE", "ROTARY SPEAKER", 8, 2.0f, Picture::none, nullptr, nullptr,
              { { "rotary.mode", nullptr, nullptr, nullptr, 0.0f, 0.0f, 90.0f },
                { "rotary.wheel", "Molette → rapide", "Wheel → fast", "rotary.mode", 1.0f, 2.0f },
                { "rotary.accel", "ACCÉLÉR.", "ACCEL", "rotary.mode", 1.0f, 2.0f, 0.0f, true },
                { "rotary.balance", "PAV./TAMB.", "HORN/DRUM", "rotary.mode", 1.0f, 2.0f },
                { "rotary.depth", "PROFONDEUR", "DEPTH", "rotary.mode", 1.0f, 2.0f },
                { "rotary.width", "LARGEUR", "WIDTH", "rotary.mode", 1.0f, 2.0f },
                { "rotary.mix", "MÉLANGE", "MIX", "rotary.mode", 1.0f, 2.0f } } },
            { "gate", "ENVELOPPE", "ENVELOPE", 9, 1.0f, Picture::none, nullptr, nullptr,
              { { "amp.attack", "ATTAQUE", "ATTACK" },
                { "amp.release", "RELÂCHE", "RELEASE" },
                { "amp.velocity", "VÉLOCITÉ", "VELOCITY" } } },
        },
    };
    layout.pages = { std::move(page) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "barsCard", "TIRETTES", "DRAWBARS", 1, 1.0f, Picture::wave, "bars", nullptr,
          { { "bar1", "16'", "16'" }, { "bar2", "5⅓'", "5⅓'" }, { "bar3", "8'", "8'" }, { "bar4", "4'", "4'" }, { "bar5", "2⅔'", "2⅔'" }, { "bar6", "2'", "2'" }, { "bar7", "1⅗'", "1⅗'" }, { "bar8", "1⅓'", "1⅓'" }, { "bar9", "1'", "1'" } } },
        { "organCard", "ORGUE", "ORGAN", 4, 1.0f, Picture::none, nullptr, nullptr,
          { { "organ.type" }, { "vibrato.mode" }, { "perc.on", "Percussion", "Percussion" }, { "drive.amount", "SATUR.", "DRIVE" } } },
        { "rotaryCard", "CABINE", "ROTARY", 8, 1.0f, Picture::none, nullptr, nullptr,
          { { "rotary.mode" }, { "rotary.mix", "MÉLANGE", "MIX" }, { "amp.release", "RELÂCHE", "RELEASE" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}
