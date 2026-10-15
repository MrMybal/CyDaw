// Jade: the voices, the mechanism and the hammer on the first row; the
// vibrating part (its sound drawn as the pickup turns it into a voltage),
// the pickup (its curve drawn) and the key noises; the tone stack and the
// preamp, the tremolo (its shape drawn) and the chorus.
Layout jade()
{
    Layout layout;
    layout.colour = IM_COL32(0x5f, 0xc8, 0x9a, 0xff);
    layout.title = "JADE";
    Page page { "PIANO", "PIANO", {} };
    page.rows = {
        {
            { "voices", "VOIX", "VOICES", 0, 0.85f, Picture::none, nullptr, nullptr,
              { { "voice.mode" },
                { "voice.glideAlways", "Toujours", "Always" },
                { "voice.count", "VOIX", "VOICES", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "voice.glide", "GLISSÉ", "GLIDE" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
            { "mechanism", "MÉCANIQUE", "MECHANISM", 1, 0.9f, Picture::none, nullptr, nullptr,
              { { "model", nullptr, nullptr, nullptr, 0.0f, 0.0f, 132.0f },
                { "tune.stretch", "ÉTIREMENT", "STRETCH", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "tune.detune", "DÉSACCORD", "DETUNE" } } },
            { "hammer", "MARTEAU", "HAMMER", 2, 1.3f, Picture::none, nullptr, nullptr,
              { { "hammer.hardness", "DURETÉ", "HARDNESS" },
                { "hammer.velocity", "VÉL. → DURETÉ", "VEL → HARD" },
                { "hammer.position", "FRAPPE", "STRIKE" },
                { "velocity.curve", "COURBE VÉL.", "VEL CURVE", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "amp.velocity", "VÉL. → VOL.", "VEL → VOL" } } },
        },
        {
            { "vibrator", "LAME / ANCHE / CORDE", "TINE / REED / STRING", 3, 1.3f, Picture::wave, "pickup", nullptr,
              { { "tine.decay", "DÉCLIN", "DECAY" },
                { "tine.tracking", "SUIVI", "TRACKING" },
                { "tine.damper", "ÉTOUFFOIR", "DAMPER" },
                { "tine.bar", "BARRE", "BAR", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "tine.harmonics", "HARMONIQUES", "HARMONICS" } } },
            { "pickup", "MICRO", "PICKUP", 4, 1.2f, Picture::wave, "curve", nullptr,
              { { "pickup.proximity", "PROXIMITÉ", "PROXIMITY" },
                { "pickup.symmetry", "SYMÉTRIE", "SYMMETRY" },
                { "pickup.position", "POSITION", "POSITION", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "pickup.tone", "RÉSONANCE", "RESONANCE" } } },
            { "noises", "BRUITS", "NOISES", 5, 0.6f, Picture::none, nullptr, nullptr,
              { { "noise.thump", "CHOC", "THUMP" },
                { "noise.release", "RELÂCHE", "RELEASE", nullptr, 0.0f, 0.0f, 0.0f, true } } },
        },
        {
            { "tone", "TONALITÉ · PRÉAMPLI", "TONE · PREAMP", 6, 1.4f, Picture::none, nullptr, nullptr,
              { { "tone.bass", "GRAVES", "BASS" },
                { "tone.body", "CORPS", "BODY" },
                { "tone.treble", "AIGUS", "TREBLE" },
                { "drive.amount", "SATURATION", "DRIVE", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "drive.output", "SORTIE", "OUTPUT" },
                { "stereo.spread", "LARGEUR", "SPREAD" } } },
            { "tremolo", "TRÉMOLO", "TREMOLO", 7, 1.3f, Picture::lfo, "tremolo", nullptr,
              { { "tremolo.shape" },
                { "tremolo.rate", "VITESSE", "RATE", "tremolo.sync", 0.0f, 0.0f, 0.0f, true },
                { "tremolo.sync", "SYNCHRO", "SYNC" },
                { "tremolo.depth", "PROFONDEUR", "DEPTH", nullptr, 0.0f, 0.0f, 0.0f, true },
                { "tremolo.stereo", "STÉRÉO", "STEREO" } } },
            { "chorus", "CHORUS", "CHORUS", 8, 0.9f, Picture::none, nullptr, nullptr,
              { { "chorus.mix", "MÉLANGE", "MIX" },
                { "chorus.rate", "VITESSE", "RATE", "chorus.mix", 0.01f, 1.0f, 0.0f, true },
                { "chorus.depth", "PROFONDEUR", "DEPTH", "chorus.mix", 0.01f, 1.0f } } },
        },
    };
    layout.pages = { std::move(page) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "mechanismCard", "MÉCANIQUE", "MECHANISM", 1, 1.0f, Picture::wave, "pickup", nullptr,
          { { "model" }, { "hammer.hardness", "DURETÉ", "HARDNESS" }, { "tine.decay", "DÉCLIN", "DECAY" }, { "tine.bar", "BARRE", "BAR" } } },
        { "pickupCard", "MICRO", "PICKUP", 4, 1.0f, Picture::wave, "curve", nullptr,
          { { "pickup.proximity", "ABOIEMENT", "BARK" }, { "pickup.symmetry", "SYMÉTRIE", "SYMMETRY" }, { "tine.harmonics", "HARMON.", "HARMONICS" } } },
        { "toneCard", "TONALITÉ", "TONE", 6, 1.0f, Picture::none, nullptr, nullptr,
          { { "tone.bass", "GRAVES", "BASS" }, { "tone.treble", "AIGUS", "TREBLE" }, { "drive.amount", "SATUR.", "DRIVE" } } },
        { "effectsCard", "MOUVEMENT", "MOTION", 7, 1.0f, Picture::none, nullptr, nullptr,
          { { "tremolo.depth", "TRÉMOLO", "TREMOLO" }, { "tremolo.rate", "VITESSE", "RATE" }, { "chorus.mix", "CHORUS", "CHORUS" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}
