// The factory's processings (Factory/Processings): written by writeFactory
// (the `processing` command's "writeFactory"), kept with the content. Their
// names in English, as every factory preset's.

#include "ModeProcessing.h"
#include "Processing.h"

namespace cydaw::v2::processing {
namespace {

Step builtin(const StepType type, std::initializer_list<std::pair<const char*, double>> settings)
{
    auto step = newStep(type);
    for (const auto& [key, value] : settings)
        step.settings[key] = value;
    return step;
}

Step code(const char* source)
{
    Step step;
    step.type = StepType::code;
    step.code = source;
    for (const auto& slider : inspectJsfx(step.code).sliders)
        step.settings["slider" + juce::String(slider.index + 1)] = slider.defaultValue;
    return step;
}

Processing audio(const char* name, const char* description, std::vector<Step> steps, const bool tail = false, const float maxSeconds = 10.0f)
{
    Processing processing;
    processing.kind = Kind::audio;
    processing.name = name;
    processing.description = description;
    processing.steps = std::move(steps);
    processing.tail.allow = tail;
    processing.tail.maxSeconds = maxSeconds;
    return processing;
}

Processing midi(const char* name, const char* description, const char* script)
{
    Processing processing;
    processing.kind = Kind::midi;
    processing.name = name;
    processing.description = description;
    processing.script = script;
    return processing;
}

const char* const tapeEcho = R"(desc:Tape Echo
slider1:350<20,2000,1>Delay (ms)
slider2:45<0,95,1>Feedback (%)
slider3:35<0,100,1>Mix (%)
slider4:4000<500,16000,10>Tone (Hz)

@init
size = floor(srate * 2.1);
buf = 0;
pos = 0;
lp0 = lp1 = 0;

@slider
d = max(1, floor(slider1 * 0.001 * srate));
fb = slider2 / 100;
mix = slider3 / 100;
k = exp(-2 * $pi * slider4 / srate);

@sample
rd = pos - d;
rd < 0 ? rd += size;
e0 = buf[rd * 2];
e1 = buf[rd * 2 + 1];
lp0 = e0 + (lp0 - e0) * k;
lp1 = e1 + (lp1 - e1) * k;
// Ping-pong: each side's echo comes back on the other.
buf[pos * 2] = spl0 + lp1 * fb;
buf[pos * 2 + 1] = spl1 + lp0 * fb;
spl0 = spl0 * (1 - mix * 0.5) + lp0 * mix;
spl1 = spl1 * (1 - mix * 0.5) + lp1 * mix;
pos += 1;
pos >= size ? pos = 0;
)";

const char* const smallRoom = R"(desc:Small Room
slider1:1.2<0.2,8,0.1>Decay (s)
slider2:30<0,100,1>Mix (%)
slider3:6000<1000,16000,10>Damping (Hz)

@init
// Eight combs (four a side) and four all-passes (two a side), as Freeverb.
// EEL2's names ignore case: each table has a name of its own.
s = srate / 44100;
combLen = 0; combPos = 8; combLp = 16; combBuf = 24; combGain = 32;
apLen = 40; apBuf = 44; apPos = 48;
next = 1000;
i = 0;
loop(8,
  base = (i % 4) == 0 ? 1116 : (i % 4) == 1 ? 1188 : (i % 4) == 2 ? 1277 : 1356;
  combLen[i] = floor((base + (i >= 4 ? 23 : 0)) * s);
  combBuf[i] = next; next += combLen[i] + 4;
  combPos[i] = 0; combLp[i] = 0;
  i += 1;
);
j = 0;
loop(4,
  apLen[j] = floor(((j % 2) == 0 ? 556 : 441) * s + (j >= 2 ? 23 * s : 0));
  apBuf[j] = next; next += apLen[j] + 4;
  apPos[j] = 0;
  j += 1;
);


@slider
mix = slider2 / 100;
damp = exp(-2 * $pi * slider3 / srate);
i = 0;
loop(8,
  combGain[i] = 10 ^ (-3 * combLen[i] / (slider1 * srate));
  i += 1;
);

@sample
in = (spl0 + spl1) * 0.015;
outL = 0;
outR = 0;
i = 0;
loop(8,
  buf = combBuf[i];
  at = combPos[i];
  y = buf[at];
  combLp[i] = y + (combLp[i] - y) * damp;
  buf[at] = in + combLp[i] * combGain[i];
  at += 1;
  at >= combLen[i] ? at = 0;
  combPos[i] = at;
  i < 4 ? outL += y : outR += y;
  i += 1;
);
j = 0;
loop(4,
  buf = apBuf[j];
  at = apPos[j];
  x = j < 2 ? outL : outR;
  held = buf[at];
  buf[at] = x + held * 0.5;
  y = held - x;
  j < 2 ? outL = y : outR = y;
  at += 1;
  at >= apLen[j] ? at = 0;
  apPos[j] = at;
  j += 1;
);
spl0 = spl0 * (1 - mix * 0.5) + outL * mix * 3;
spl1 = spl1 * (1 - mix * 0.5) + outR * mix * 3;
)";

const char* const humanize = R"(param1:12<0,60,1>Timing (ms)
param2:10<0,50,1>Velocity (%)
param3:5<0,50,1>Length (%)

@note
start += gauss() * ms_to_beats(param1) / 2;
velocity *= 1 + gauss() * param2 / 200;
length *= 1 + gauss() * param3 / 200;
)";

const char* const swing = R"(param1:50<0,100,1>Amount (%)
param2:0<0,1,1>Grid (0 = 1/8, 1 = 1/16)

@note
grid = param2 < 0.5 ? 0.5 : 0.25;
pos = start / grid;
step = floor(pos + 0.001);
// The notes on an off-beat of the grid, later: 100 % is a triplet feel.
(step % 2) == 1 && abs(pos - step) < 0.02 ? start += grid / 3 * param1 / 100;
)";

const char* const velocityVariation = R"(param1:12<0,50,1>Variation (%)
param2:10<0,40,1>Accent on beats (%)

@note
velocity *= 1 + (random() * 2 - 1) * param1 / 100;
// On a beat: a little louder.
abs(start - floor(start + 0.001)) < 0.01 ? velocity *= 1 + param2 / 100;
)";

const char* const strum = R"(param1:25<0,120,1>Spread (ms)
param2:0<0,1,1>Direction (0 = up, 1 = down)

@init
last = -1;
k = 0;
size = 1;

@note
// The notes struck together (the same start, lowest first), one after another.
here = start;
here != last ? (
  k = 0;
  last = here;
  size = 0;
  t = index;
  go = 1;
  loop(count - index,
    go ? (note_start(t) == here ? (size += 1; t += 1;) : go = 0;);
  );
) : (
  k += 1;
);
order = param2 < 0.5 ? k : size - 1 - k;
start = here + order * ms_to_beats(param1);
)";

const char* const legato = R"(param1:0<0,0.5,0.01>Overlap (beats)

@note
// Each note held until the next one starts (the last as it was).
next_start > 0 ? length = next_start - start + param1;
)";

const char* const staccato = R"(param1:40<5,100,1>Length (%)

@note
length *= param1 / 100;
)";

const char* const randomOctaves = R"(param1:20<0,100,1>Chance (%)
param2:0<0,1,1>Direction (0 = both, 1 = up only)

@note
random() * 100 < param1 ? (
  up = param2 > 0.5 || random() < 0.5;
  pitch += up ? 12 : -12;
);
)";

const char* const noteEcho = R"(param1:3<1,8,1>Echoes
param2:0.5<0.0625,2,0.0625>Delay (beats)
param3:60<10,95,1>Decay (%)

@note
j = 1;
loop(param1,
  add_note(pitch, start + param2 * j, length, velocity * (param3 / 100) ^ j);
  j += 1;
);
)";

// CyDAW's own reverb on a track of its own, its decay and mix shown.
Processing roomReverb()
{
    auto step = newStep(StepType::chain);
    ModeProcessing mixer;
    mixer.restore(step.chain);
    mixer.ensureInput(1, "Sound");
    auto* request = new juce::DynamicObject();
    request->setProperty("op", "add_effect");
    request->setProperty("inputId", 1);
    request->setProperty("key", "reverb-pro");
    (void) mixer.command(juce::var(request));
    step.chain = mixer.serialise();
    for (const auto& knob : mixer.parameters(1)) {
        const auto label = juce::String(knob.label).toLowerCase();
        if (knob.binding.empty())
            continue;
        if (label.contains("decay") || label.contains("mix") || label.contains("size") || label.contains("dry/wet")) {
            step.published.push_back({ juce::String(knob.binding), juce::String(knob.label), knob.minimum, knob.maximum });
            step.settings[juce::String(knob.binding)] = knob.value;
        }
    }
    return audio("Room Reverb", "CyDAW's studio reverb on the sound, its tail let ring.", { step }, true, 12.0f);
}

} // namespace

juce::Result writeFactory(const juce::File& folder)
{
    std::vector<Processing> all {
        audio("Remove Noise", "Background noise (hiss, hum of fans) measured in the quietest moments and taken out.",
              { builtin(StepType::denoise, { { "reduction", 18.0 }, { "sensitivity", 1.5 }, { "smoothing", 0.5 } }) }),
        audio("Remove Hum", "Mains hum at 50 or 60 Hz (found) and its harmonics notched out.",
              { builtin(StepType::hum, { { "fundamental", 0.0 }, { "harmonics", 6.0 }, { "width", 30.0 } }) }),
        audio("Remove Clicks", "Clicks, pops and dropouts redrawn from the sound around them.",
              { builtin(StepType::declick, { { "sensitivity", 0.5 }, { "maxClick", 2.0 } }) }),
        audio("Voice Clean-up", "A spoken or sung recording: rumble cut, hum and clicks taken out, noise lowered, level set.",
              { builtin(StepType::highpass, { { "cutoff", 80.0 }, { "slope", 0.0 } }),
                builtin(StepType::hum, { { "fundamental", 0.0 }, { "harmonics", 6.0 }, { "width", 35.0 } }),
                builtin(StepType::declick, { { "sensitivity", 0.4 }, { "maxClick", 1.5 } }),
                builtin(StepType::denoise, { { "reduction", 12.0 }, { "sensitivity", 1.3 }, { "smoothing", 0.6 } }),
                builtin(StepType::normalise, { { "peak", -1.0 } }) }),
        audio("Vinyl Restoration", "A record: crackle and pops mended, hum and surface noise lowered, the harshest highs tamed.",
              { builtin(StepType::declick, { { "sensitivity", 0.7 }, { "maxClick", 4.0 } }),
                builtin(StepType::hum, { { "fundamental", 0.0 }, { "harmonics", 4.0 }, { "width", 30.0 } }),
                builtin(StepType::denoise, { { "reduction", 10.0 }, { "sensitivity", 1.2 }, { "smoothing", 0.6 } }),
                builtin(StepType::lowpass, { { "cutoff", 15'000.0 }, { "slope", 0.0 } }) }),
        audio("Normalize", "The loudest peak brought to -1 dBFS.", { builtin(StepType::normalise, { { "peak", -1.0 } }) }),
        audio("Trim Silence", "Silence at the start and the end cut off.",
              { builtin(StepType::trim, { { "threshold", -60.0 }, { "keep", 20.0 } }) }),
        audio("Tape Echo", "A ping-pong echo written in JSFX, its repeats let ring past the end.", { code(tapeEcho) }, true, 10.0f),
        audio("Small Room", "A small room written in JSFX (Freeverb's design), its tail let ring.", { code(smallRoom) }, true, 10.0f),
        roomReverb(),
        midi("Humanize", "Small random shifts of timing, velocity and length, as a player's.", humanize),
        midi("Swing", "The off-beats of the grid later (100 %: a triplet feel).", swing),
        midi("Velocity Variation", "Velocities varied at random, the beats a little louder.", velocityVariation),
        midi("Strum", "Notes struck together played one after another, as a strummed guitar.", strum),
        midi("Legato", "Each note held until the next one.", legato),
        midi("Staccato", "Every note shortened.", staccato),
        midi("Random Octaves", "Some notes moved an octave, at random.", randomOctaves),
        midi("Note Echo", "Each note repeated after it, softer each time.", noteEcho),
    };
    (void) folder.createDirectory();
    for (const auto& processing : all)
        if (const auto saved = save(processing, folder.getChildFile(juce::File::createLegalFileName(processing.name) + fileExtension));
            saved.failed())
            return saved;
    return juce::Result::ok();
}

} // namespace cydaw::v2::processing
