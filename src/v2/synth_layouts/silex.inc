// Silex: the kick, the snare and the clap (each drawn as it sounds) on the
// first row; the hi-hats, the crash and the ride; the toms and the kit's own
// settings (its character, its tuning and length, the velocity, the drive
// and the tone, the voices); the four percussions.
Layout silex()
{
    Layout layout;
    layout.colour = IM_COL32(0xff, 0x6e, 0x4a, 0xff);
    layout.title = "SILEX";
    Page page { "BATTERIE", "DRUM KIT", {} };
    page.rows = {
        {
            { "kick", "GROSSE CAISSE", "KICK", 1, 8.3f, Picture::wave, "kick", nullptr,
              { { "kick.tune", "ACCORD", "TUNE" },
                { "kick.sweep", "CHUTE", "DROP" },
                { "kick.time", "TEMPS", "TIME" },
                { "kick.decay", "DÉCLIN", "DECAY" },
                { "kick.click", "CLIC", "CLICK" },
                { "kick.drive", "SATUR.", "DRIVE" },
                { "kick.level", "NIVEAU", "LEVEL" },
                { "kick.pan", "PANO.", "PAN" } } },
            { "snare", "CAISSE CLAIRE", "SNARE", 2, 7.3f, Picture::wave, "snare", nullptr,
              { { "snare.tune", "ACCORD", "TUNE" },
                { "snare.tone", "TON", "TONE" },
                { "snare.snappy", "TIMBRE", "SNAPPY" },
                { "snare.colour", "COULEUR", "COLOUR" },
                { "snare.decay", "DÉCLIN", "DECAY" },
                { "snare.level", "NIVEAU", "LEVEL" },
                { "snare.pan", "PANO.", "PAN" } } },
            { "clap", "CLAP", "CLAP", 3, 5.3f, Picture::wave, "clap", nullptr,
              { { "clap.spread", "ÉCART", "SPREAD" },
                { "clap.tone", "TON", "TONE" },
                { "clap.decay", "DÉCLIN", "DECAY" },
                { "clap.level", "NIVEAU", "LEVEL" },
                { "clap.pan", "PANO.", "PAN" } } },
        },
        {
            { "hats", "CHARLESTONS", "HI-HATS", 4, 7.3f, Picture::wave, "hat", nullptr,
              { { "hat.tune", "ACCORD", "TUNE" },
                { "hat.metal", "MÉTAL", "METAL" },
                { "hat.tone", "TON", "TONE" },
                { "hat.closed", "FERMÉ", "CLOSED" },
                { "hat.open", "OUVERT", "OPEN" },
                { "hat.level", "NIVEAU", "LEVEL" },
                { "hat.pan", "PANO.", "PAN" } } },
            { "crash", "CRASH", "CRASH", 6, 5.3f, Picture::wave, "crash", nullptr,
              { { "crash.tune", "ACCORD", "TUNE" },
                { "crash.tone", "TON", "TONE" },
                { "crash.decay", "DÉCLIN", "DECAY" },
                { "crash.level", "NIVEAU", "LEVEL" },
                { "crash.pan", "PANO.", "PAN" } } },
            { "ride", "RIDE", "RIDE", 6, 6.3f, Picture::wave, "ride", nullptr,
              { { "ride.tune", "ACCORD", "TUNE" },
                { "ride.tone", "TON", "TONE" },
                { "ride.bell", "CLOCHE", "BELL" },
                { "ride.decay", "DÉCLIN", "DECAY" },
                { "ride.level", "NIVEAU", "LEVEL" },
                { "ride.pan", "PANO.", "PAN" } } },
        },
        {
            { "toms", "TOMS", "TOMS", 5, 7.3f, Picture::wave, "tom", nullptr,
              { { "tom.tune", "ACCORD", "TUNE" },
                { "tom.drop", "CHUTE", "DROP" },
                { "tom.decay", "DÉCLIN", "DECAY" },
                { "tom.tone", "TON", "TONE" },
                { "tom.level", "NIVEAU", "LEVEL" },
                { "tom.pan", "PANO.", "PAN" },
                { "tom.spread", "ÉCART", "SPREAD" } } },
            { "kit", "GÉNÉRAL", "GLOBAL", 8, 10.5f, Picture::none, nullptr, nullptr,
              { { "kit.character" },
                { "kit.tune", "ACCORD", "TUNE" },
                { "kit.decay", "LONGUEUR", "LENGTH" },
                { "kit.velocity", "VÉLOCITÉ", "VELOCITY" },
                { "master.drive", "SATUR.", "DRIVE" },
                { "master.tone", "TON", "TONE" },
                { "voice.count", "VOIX", "VOICES" },
                { "voice.bend", "BEND", "BEND" },
                { "voice.master", "VOLUME", "VOLUME" } } },
        },
        {
            { "rim", "RIMSHOT", "RIMSHOT", 7, 1.0f, Picture::none, nullptr, nullptr,
              { { "rim.tune", "ACCORD", "TUNE" }, { "rim.decay", "DÉCLIN", "DECAY" }, { "rim.level", "NIVEAU", "LEVEL" }, { "rim.pan", "PANO.", "PAN" } } },
            { "cowbell", "CLOCHE", "COWBELL", 7, 1.0f, Picture::none, nullptr, nullptr,
              { { "cowbell.tune", "ACCORD", "TUNE" },
                { "cowbell.decay", "DÉCLIN", "DECAY" },
                { "cowbell.level", "NIVEAU", "LEVEL" },
                { "cowbell.pan", "PANO.", "PAN" } } },
            { "claves", "CLAVES", "CLAVES", 7, 1.0f, Picture::none, nullptr, nullptr,
              { { "clave.tune", "ACCORD", "TUNE" },
                { "clave.decay", "DÉCLIN", "DECAY" },
                { "clave.level", "NIVEAU", "LEVEL" },
                { "clave.pan", "PANO.", "PAN" } } },
            { "maracas", "MARACAS", "MARACAS", 7, 1.0f, Picture::none, nullptr, nullptr,
              { { "shaker.tone", "TON", "TONE" },
                { "shaker.decay", "DÉCLIN", "DECAY" },
                { "shaker.level", "NIVEAU", "LEVEL" },
                { "shaker.pan", "PANO.", "PAN" } } },
        },
    };
    layout.pages = { std::move(page) };
    // The card in Devices: the sound's main settings; the window has them all.
    layout.card = {
        { "kickCard", "GROSSE CAISSE", "KICK", 1, 1.0f, Picture::wave, "kick", nullptr,
          { { "kick.tune", "ACCORD", "TUNE" }, { "kick.decay", "DÉCLIN", "DECAY" }, { "kick.click", "CLIC", "CLICK" }, { "kick.drive", "SATUR.", "DRIVE" } } },
        { "snareCard", "CAISSE CLAIRE", "SNARE", 2, 1.0f, Picture::none, nullptr, nullptr,
          { { "snare.tune", "ACCORD", "TUNE" }, { "snare.tone", "TON", "TONE" }, { "snare.snappy", "TIMBRE", "SNAPPY" }, { "snare.decay", "DÉCLIN", "DECAY" } } },
        { "hatCard", "CHARLESTONS", "HI-HATS", 4, 1.0f, Picture::none, nullptr, nullptr,
          { { "hat.tone", "TON", "TONE" }, { "hat.closed", "FERMÉ", "CLOSED" }, { "hat.open", "OUVERT", "OPEN" }, { "hat.level", "NIVEAU", "LEVEL" } } },
        { "kitCard", "GÉNÉRAL", "GLOBAL", 8, 1.0f, Picture::none, nullptr, nullptr,
          { { "kit.character" }, { "kit.tune", "ACCORD", "TUNE" }, { "master.drive", "SATUR.", "DRIVE" }, { "voice.master", "VOLUME", "VOLUME" } } },
    };
    return layout;
}
