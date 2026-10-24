// Bronze: a physical-modelling instrument of the synth family, objects that
// are struck, rubbed or blown, and the way they ring. Two exciters: a mallet
// (its hardness, a touch of coloured noise where it lands) and a noise with
// its own filter and envelope, to bow, blow or brush. Two resonators, each one
// of seven objects: a beam, a tuned marimba bar, a membrane and a plate (banks
// of resonant modes whose ratios and shapes come from the object's physics),
// a string, a pipe closed at one end and a tube open at both (waveguides: a
// delay line, its losses, its stiffness, its ends' reflections). Each is
// tuned, damped, struck at a place and listened to at two others (left and
// right); resonator 1 rings into resonator 2, or both ring side by side. An
// LFO moves their pitch, their decay, where they are struck and the volume; a
// gentle limiter keeps their resonances in hand.

#include "SynthEngines.h"
#include "SynthParts.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

// One resonator's settings, from its first.
enum ResonatorField : int { rType, rQuality, rTune, rFine, rDecay, rMaterial, rBright, rInharm, rHit, rLeft, rRight, resonatorFieldCount };

// Bronze's settings after the shared voicing, in its table's order.
enum Bronze : int {
    malletVolume = voicingFieldCount,
    malletStiffness,
    malletNoise,
    malletColour,
    noiseVolume,
    noiseFilter,
    noiseCutoff,
    noiseResonance,
    noiseKey,
    noiseAttack,
    noiseDecay,
    noiseSustain,
    noiseRelease,
    resonator1,
    resonator2 = resonator1 + resonatorFieldCount,
    structureChoice = resonator2 + resonatorFieldCount,
    structureBalance,
    releaseTime,
    velocityToVolume,
    velocityToStiffness,
    outputGain,
    outputLimiter,
    lfoShape,
    lfoRate,
    lfoSync,
    lfoToTune,
    lfoToDecay,
    lfoToHit,
    lfoToVolume,
    bronzeCount
};
static_assert(bronzeCount == 55);

// The objects: four banks of modes, then three waveguides.
enum ObjectType : int { typeBeam, typeMarimba, typeMembrane, typePlate, typeString, typePipe, typeTube, typeCount };
constexpr int modalTypes = 4;

constexpr int modeLeast = 4;
constexpr int modeMost = 32;
constexpr std::size_t modeSlots = 32;
// A modal object's shapes, sampled from its centre (0) to its edge (1).
constexpr int placePoints = 33;
// The slow things (envelope, LFO, glide, coefficients) once every this many samples.
constexpr std::size_t stepFrames = 16;
constexpr int controlStep = static_cast<int>(stepFrames);
// A fall of 60 dB: ln(1000).
constexpr double kT60 = 6.907755278982137;
// The lowest note a waveguide holds (its delay line sized for it).
constexpr double lowestGuideHz = 27.5;

// How loud each part is: every object about as loud when struck alike (a
// bar moves less than a skin away from its ends, a closed pipe's combs add
// up at the bottom), the noise as present as a mallet, resonator 1 heard by 2.
constexpr std::array<double, 7> kObjectLevels { 2.0, 2.0, 1.0, 1.2, 0.8, 0.55, 0.8 };
constexpr double modalLevel = 0.9;
constexpr double guideLevel = 0.5 * 0.9;
constexpr double noiseDrive = 1.0;
constexpr double serialGain = 0.15;
constexpr double burstLevel = 0.8;
// The limiter: untouched below, bent softly up to 1 above.
constexpr double limitKnee = 0.6;

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices kObjects { { "Poutre", "Beam" },  { "Marimba", "Marimba" }, { "Membrane", "Membrane" }, { "Plaque", "Plate" },
                         { "Corde", "String" }, { "Tuyau", "Pipe" },      { "Tube", "Tube" } };
const Choices kNoiseFilters { { "Passe-bas", "Low-pass" }, { "Passe-bande", "Band-pass" }, { "Passe-haut", "High-pass" } };
const Choices kStructures { { "1 > 2", "1 > 2" }, { "1 + 2", "1 + 2" } };
const Choices kLfoShapes { { "Sinus", "Sine" }, { "Triangle", "Triangle" }, { "Dent de scie", "Saw" }, { "Carré", "Square" },
                           { "Aléatoire", "Random" } };

// The modal objects' modes: each one's frequency over the lowest, and its
// shape (how far it moves at a place, from the centre to the edge, -1..1).
struct ModalTables {
    std::array<std::array<double, modeSlots>, modalTypes> ratios {};
    std::array<std::array<std::array<float, placePoints>, modeSlots>, modalTypes> shapes {};
};

// A mode of a bar free at both ends, at x along it (0..1), beta its root of
// cos(beta) cosh(beta) = 1; its growing parts written so that nothing
// overflows nor cancels.
double barShape(const double beta, const double x) noexcept
{
    const auto sigma = (std::cosh(beta) - std::cos(beta)) / (std::sinh(beta) - std::sin(beta));
    const auto rising = (std::cos(beta) - std::sin(beta) - std::exp(-beta)) / (2.0 * (std::sinh(beta) - std::sin(beta)));
    return rising * std::exp(beta * x) + 0.5 * (1.0 + sigma) * std::exp(-beta * x) + std::cos(beta * x) - sigma * std::sin(beta * x);
}

// Bessel's function of the first kind, by its series (enough for x < 20).
double besselJ(const int order, const double x) noexcept
{
    const auto half = 0.5 * x;
    auto term = 1.0;
    for (int i = 1; i <= order; ++i)
        term *= half / static_cast<double>(i);
    auto sum = term;
    for (int s = 1; s < 200; ++s) {
        term *= -half * half / (static_cast<double>(s) * static_cast<double>(s + order));
        sum += term;
        if (static_cast<double>(s) > half && std::abs(term) < 1.0e-17)
            break;
    }
    return sum;
}

// A round membrane's lowest modes: their order (nodal diameters) and the
// root of the Bessel function they ring at, lowest first.
struct MembraneMode {
    int order;
    double root;
};
constexpr std::array<MembraneMode, modeSlots> kMembraneModes { {
    { 0, 2.404826 },  { 1, 3.831706 },  { 2, 5.135622 },  { 0, 5.520078 },  { 3, 6.380162 },  { 1, 7.015587 },  { 4, 7.588342 },
    { 2, 8.417244 },  { 0, 8.653728 },  { 5, 8.771484 },  { 3, 9.761023 },  { 6, 9.936110 },  { 1, 10.173468 }, { 4, 11.064709 },
    { 7, 11.086370 }, { 2, 11.619841 }, { 0, 11.791534 }, { 8, 12.225092 }, { 5, 12.338604 }, { 3, 13.015201 }, { 1, 13.323692 },
    { 9, 13.354300 }, { 6, 13.589290 }, { 4, 14.372537 }, { 10, 14.475501 }, { 2, 14.795952 }, { 7, 14.821269 }, { 0, 14.930918 },
    { 11, 15.589848 }, { 5, 15.700174 }, { 8, 16.037774 }, { 3, 16.223466 },
} };

// A tuned marimba bar (cut under its middle): its partials at two octaves and
// about three octaves and a third above the note.
constexpr std::array<double, 8> kMarimbaRatios { 1.0, 3.99, 9.92, 17.6, 27.3, 38.8, 52.3, 67.7 };

// Made once, off the audio thread (an engine's constructor asks first).
const ModalTables& modalTables()
{
    static const ModalTables built = [] {
        ModalTables tables;
        const auto fill = [&tables](const int type, const std::size_t mode, const auto& displacement) {
            auto& shape = tables.shapes[static_cast<std::size_t>(type)][mode];
            for (int point = 0; point < placePoints; ++point)
                shape[static_cast<std::size_t>(point)] = static_cast<float>(displacement(static_cast<double>(point) / (placePoints - 1.0)));
        };
        // A beam: a bar free at both ends (struck from its middle, 0, to near an end, 1).
        constexpr std::array<double, 5> firstRoots { 4.730041, 7.853205, 10.995608, 14.137165, 17.278760 };
        for (std::size_t mode = 0; mode < modeSlots; ++mode) {
            const auto beta = mode < firstRoots.size() ? firstRoots[mode] : (static_cast<double>(mode) + 1.5) * kPi;
            tables.ratios[typeBeam][mode] = (beta / firstRoots[0]) * (beta / firstRoots[0]);
            tables.ratios[typeMarimba][mode] = mode < kMarimbaRatios.size()
                                                   ? kMarimbaRatios[mode]
                                                   : kMarimbaRatios.back() * ((static_cast<double>(mode) + 1.5) / 8.5) * ((static_cast<double>(mode) + 1.5) / 8.5);
            auto most = 1.0e-9;
            for (int at = 0; at <= 256; ++at)
                most = std::max(most, std::abs(barShape(beta, static_cast<double>(at) / 256.0)));
            const auto bar = [beta, most](const double place) { return barShape(beta, 0.5 - 0.49 * place) / most; };
            fill(typeBeam, mode, bar);
            fill(typeMarimba, mode, bar);
        }
        // A membrane held at its rim (struck from its centre, 0, to near the rim, 1).
        for (std::size_t mode = 0; mode < modeSlots; ++mode) {
            const auto order = kMembraneModes[mode].order;
            const auto root = kMembraneModes[mode].root;
            tables.ratios[typeMembrane][mode] = root / kMembraneModes[0].root;
            auto most = 1.0e-9;
            for (int at = 0; at <= 256; ++at)
                most = std::max(most, std::abs(besselJ(order, root * static_cast<double>(at) / 256.0)));
            fill(typeMembrane, mode, [order, root, most](const double place) { return besselJ(order, root * 0.96 * place) / most; });
        }
        // A plate held at its edges, its sides as the golden ratio (from its
        // centre, 0, towards a corner, 1).
        struct PlateMode {
            int across;
            int along;
            double value;
        };
        std::array<PlateMode, 144> plate {};
        for (int across = 1; across <= 12; ++across)
            for (int along = 1; along <= 12; ++along)
                plate[static_cast<std::size_t>((across - 1) * 12 + along - 1)]
                    = { across, along, static_cast<double>(across * across) + static_cast<double>(along * along) / (0.618 * 0.618) };
        std::sort(plate.begin(), plate.end(), [](const PlateMode& a, const PlateMode& b) { return a.value < b.value; });
        for (std::size_t mode = 0; mode < modeSlots; ++mode) {
            const auto across = plate[mode].across;
            const auto along = plate[mode].along;
            tables.ratios[typePlate][mode] = plate[mode].value / plate[0].value;
            fill(typePlate, mode, [across, along](const double place) {
                return std::sin(static_cast<double>(across) * kPi * (0.5 - 0.46 * place))
                       * std::sin(static_cast<double>(along) * kPi * (0.5 - 0.31 * place));
            });
        }
        return tables;
    }();
    return built;
}

// A modal object's shape at a place (0..1).
double shapeAt(const std::array<float, placePoints>& shape, const double place) noexcept
{
    const auto at = std::clamp(place, 0.0, 1.0) * (placePoints - 1.0);
    const auto lower = std::min(static_cast<int>(at), placePoints - 2);
    const auto fraction = at - static_cast<double>(lower);
    const auto a = static_cast<double>(shape[static_cast<std::size_t>(lower)]);
    const auto b = static_cast<double>(shape[static_cast<std::size_t>(lower + 1)]);
    return a + (b - a) * fraction;
}

// Where along a waveguide a place lies (0: its middle, 1: near its end), as
// a share of its period.
double guidePlace(const double place) noexcept
{
    return 0.5 - 0.48 * std::clamp(place, 0.0, 1.0);
}

} // namespace

std::vector<Section> bronzeSections()
{
    return { { "VOIX", "VOICES", 0xffb0b8c8u },           { "MAILLET", "MALLET", 0xffe8a35cu },
             { "BRUIT", "NOISE", 0xff9fd3c7u },           { "RÉSONATEUR 1", "RESONATOR 1", 0xffd9884au },
             { "RÉSONATEUR 2", "RESONATOR 2", 0xff6cc4b0u }, { "STRUCTURE", "STRUCTURE", 0xfff2d16bu },
             { "LFO", "LFO", 0xffa58cffu } };
}

std::vector<Parameter> bronzeParameters()
{
    Table table;
    addVoicing(table, 12, 0.7f);
    table.section = 1;
    table.add("mallet.volume", "Maillet · volume", "Mallet · volume", {}, 0.0f, 1.0f, 1.0f);
    table.add("mallet.stiffness", "Maillet · dureté", "Mallet · stiffness", {}, 0.0f, 1.0f, 0.45f);
    table.add("mallet.noise", "Maillet · bruit", "Mallet · noise", {}, 0.0f, 1.0f, 0.1f);
    table.add("mallet.colour", "Maillet · couleur du bruit", "Mallet · noise colour", {}, 0.0f, 1.0f, 0.5f);
    table.section = 2;
    table.add("noise.volume", "Bruit · volume", "Noise · volume", {}, 0.0f, 1.0f, 0.0f);
    table.choice("noise.filter", "Bruit · filtre", "Noise · filter", kNoiseFilters, 0);
    table.add("noise.cutoff", "Bruit · fréquence", "Noise · cutoff", "Hz", 20.0f, 20'000.0f, 3'000.0f, Kind::continuous, true);
    table.add("noise.resonance", "Bruit · résonance", "Noise · resonance", {}, 0.0f, 1.0f, 0.2f);
    table.add("noise.key", "Bruit · suivi du clavier", "Noise · key tracking", {}, 0.0f, 1.0f, 0.0f);
    table.add("noise.attack", "Bruit · attaque", "Noise · attack", "s", 0.001f, 10.0f, 0.002f, Kind::continuous, true);
    table.add("noise.decay", "Bruit · déclin", "Noise · decay", "s", 0.005f, 10.0f, 0.15f, Kind::continuous, true);
    table.add("noise.sustain", "Bruit · maintien", "Noise · sustain", {}, 0.0f, 1.0f, 0.0f);
    table.add("noise.release", "Bruit · relâchement", "Noise · release", "s", 0.005f, 10.0f, 0.1f, Kind::continuous, true);
    for (int index = 1; index <= 2; ++index) {
        table.section = 2 + index;
        const auto first = index == 1;
        const auto prefix = "r" + std::to_string(index) + ".";
        const auto french = "Résonateur " + std::to_string(index) + " · ";
        const auto english = "Resonator " + std::to_string(index) + " · ";
        table.choice(prefix + "type", french + "objet", english + "object", kObjects, first ? typeMarimba : typeTube);
        table.whole(prefix + "quality", french + "qualité (modes)", english + "quality (modes)", {}, modeLeast, modeMost, 16);
        table.whole(prefix + "tune", french + "accord", english + "tune", "st", -48, 48, 0);
        table.add(prefix + "fine", french + "accord fin", english + "fine tune", "ct", -50.0f, 50.0f, 0.0f);
        table.add(prefix + "decay", french + "déclin", english + "decay", "s", 0.02f, 20.0f, first ? 1.2f : 0.6f, Kind::continuous, true);
        table.add(prefix + "material", french + "matière", english + "material", {}, -1.0f, 1.0f, first ? 0.4f : 0.3f);
        table.add(prefix + "bright", french + "rayon / brillance", english + "radius / brightness", {}, 0.0f, 1.0f, first ? 0.55f : 0.45f);
        table.add(prefix + "inharm", french + "inharmonicité", english + "inharmonicity", {}, -1.0f, 1.0f, 0.0f);
        table.add(prefix + "hit", french + "point de frappe", english + "hit position", {}, 0.0f, 1.0f, first ? 0.2f : 0.15f);
        table.add(prefix + "left", french + "écoute gauche", english + "left pickup", {}, 0.0f, 1.0f, first ? 0.15f : 0.2f);
        table.add(prefix + "right", french + "écoute droite", english + "right pickup", {}, 0.0f, 1.0f, first ? 0.45f : 0.55f);
    }
    table.section = 5;
    table.choice("structure", "Structure", "Structure", kStructures, 0);
    table.add("balance", "Balance 1 ↔ 2", "Balance 1 ↔ 2", {}, 0.0f, 1.0f, 0.35f);
    table.add("release", "Relâchement (étouffé)", "Release (damping)", "s", 0.01f, 10.0f, 1.0f, Kind::continuous, true);
    table.add("velocity.volume", "Vélocité → volume", "Velocity → volume", {}, 0.0f, 1.0f, 0.6f);
    table.add("velocity.stiffness", "Vélocité → dureté", "Velocity → stiffness", {}, 0.0f, 1.0f, 0.5f);
    table.add("output.gain", "Sortie", "Output", "dB", -24.0f, 12.0f, 0.0f);
    table.toggle("output.limiter", "Limiteur", "Limiter", true);
    table.section = 6;
    table.choice("lfo1.shape", "LFO · forme", "LFO · shape", kLfoShapes, 0);
    table.add("lfo1.rate", "LFO · vitesse", "LFO · rate", "Hz", 0.05f, 30.0f, 4.0f, Kind::continuous, true);
    table.whole("lfo1.sync", "LFO · synchro", "LFO · sync", {}, 0, 12, 0);
    table.add("lfo1.tune", "LFO → accord", "LFO → tune", "st", -12.0f, 12.0f, 0.0f);
    table.add("lfo1.decay", "LFO → déclin", "LFO → decay", {}, -1.0f, 1.0f, 0.0f);
    table.add("lfo1.hit", "LFO → point de frappe", "LFO → hit position", {}, -1.0f, 1.0f, 0.0f);
    table.add("lfo1.volume", "LFO → volume", "LFO → volume", {}, 0.0f, 1.0f, 0.0f);
    return table.parameters;
}

namespace {

// What a resonator plays for a step: its object and how it is set, as its
// knobs and the modulations put them.
struct Setting {
    int type { typeMarimba };
    int quality { 16 };
    double hz { 220.0 };
    double decay { 1.0 };     // seconds to fall 60 dB (its lowest mode)
    double release { 1.0e9 }; // the same once the key is up
    double material {};
    double bright { 0.5 };
    double inharm {};
    double hit { 0.3 };
    double left { 0.2 };
    double right { 0.5 };
};

// One resonator of a voice: a bank of up to 32 modes (each a decaying
// rotation, so that its pitch may move while it rings) or a waveguide (a
// delay line read between its samples, a loss filter, allpasses for the
// stiffness), struck by a blow, driven by a lasting push.
class Resonator {
public:
    // Off the audio thread: the delay line for the lowest note at this rate.
    void prepare(const double rate)
    {
        std::size_t size = 64;
        const auto needed = static_cast<std::size_t>(std::ceil(std::max(1.0, rate) / lowestGuideHz)) + 16;
        while (size < needed)
            size *= 2;
        line_.assign(size, 0.0f);
        mask_ = static_cast<int>(size) - 1;
        write_ = 0;
        type_ = -1;
    }

    // Starts again from silence, its coefficients made anew when next set.
    void restart() noexcept { type_ = -1; }

    void set(const Setting& s, const double rate, const ModalTables& tables) noexcept
    {
        const auto type = std::clamp(s.type, 0, typeCount - 1);
        auto force = false;
        if (type != type_ || rate != rate_) {
            type_ = type;
            rate_ = rate;
            clear();
            force = true;
        }
        if (type_ < modalTypes)
            setModal(s, tables, force);
        else
            setGuide(s, force);
    }

    // Adds what it rings into outLeft and outRight; `excited`: strike or
    // drive is not all silence.
    void process(const double* strike, const double* drive, double* outLeft, double* outRight, const int count, const bool excited) noexcept
    {
        if (type_ < 0)
            return;
        if (type_ < modalTypes)
            processModal(strike, drive, outLeft, outRight, count, excited);
        else if (ready_)
            processGuide(strike, drive, outLeft, outRight, count);
    }

    // How much louder than what drives it it plays (a broad push in, its
    // trims out): what a resonator ringing into it is scaled by.
    [[nodiscard]] double loudness() const noexcept
    {
        const auto object = static_cast<std::size_t>(std::clamp(type_, 0, typeCount - 1));
        return (type_ < modalTypes ? modalLevel : 2.0 * guideLevel) * kObjectLevels[object];
    }

    // Every state at rest (the coefficients kept).
    void clear() noexcept
    {
        re_.fill(0.0);
        im_.fill(0.0);
        std::fill(line_.begin(), line_.end(), 0.0f);
        lossState_ = 0.0;
        allpassIn_.fill(0.0);
        allpassOut_.fill(0.0);
        brightState_ = 0.0;
    }

private:
    static bool moved(const double now, const double before, const double tolerance) noexcept { return std::abs(now - before) > tolerance; }

    void setModal(const Setting& s, const ModalTables& tables, const bool force) noexcept
    {
        const auto quality = std::clamp(s.quality, modeLeast, modeMost);
        const auto material = std::clamp(s.material, -1.0, 1.0);
        const auto bright = std::clamp(s.bright, 0.0, 1.0);
        const auto inharm = std::clamp(s.inharm, -1.0, 1.0);
        const auto hit = std::clamp(s.hit, 0.0, 1.0);
        const auto left = std::clamp(s.left, 0.0, 1.0);
        const auto right = std::clamp(s.right, 0.0, 1.0);
        const auto shapeMoved = force || quality != quality_ || moved(material, material_, 1.0e-5) || moved(bright, bright_, 1.0e-5)
                                || moved(inharm, inharm_, 1.0e-5);
        const auto placeMoved = force || moved(hit, hit_, 1.0e-5) || moved(left, left_, 1.0e-5) || moved(right, right_, 1.0e-5);
        const auto pitchMoved = shapeMoved || moved(s.hz, hz_, hz_ * 1.0e-5);
        const auto decayMoved = shapeMoved || moved(s.decay, decay_, decay_ * 1.0e-4) || moved(s.release, release_, release_ * 1.0e-4);
        if (!placeMoved && !pitchMoved && !decayMoved)
            return;
        const auto object = static_cast<std::size_t>(type_);
        if (shapeMoved) {
            // Its modes: how many, their ratios stretched or squeezed, how much
            // faster the high ones die, how far up they reach (as loud whatever
            // their number).
            quality_ = quality;
            material_ = material;
            bright_ = bright;
            inharm_ = inharm;
            used_ = static_cast<std::size_t>((quality + 1) & ~1);
            const auto stretch = 1.0 + 0.3 * inharm;
            const auto reach = 1.5 + 48.0 * bright * bright;
            auto power = 0.0;
            for (std::size_t k = 0; k < modeSlots; ++k) {
                if (k >= static_cast<std::size_t>(quality)) {
                    weight_[k] = 0.0;
                    ratio_[k] = 1.0;
                    scale_[k] = 1.0;
                    audible_[k] = false;
                    re_[k] = 0.0;
                    im_[k] = 0.0;
                    continue;
                }
                ratio_[k] = std::pow(tables.ratios[object][k], stretch);
                scale_[k] = std::pow(ratio_[k], 1.5 * material);
                const auto over = ratio_[k] / reach;
                weight_[k] = 1.0 / (1.0 + over * over);
                power += weight_[k] * weight_[k];
            }
            const auto norm = 1.0 / std::sqrt(std::max(power, 1.0e-12));
            for (auto& weight : weight_)
                weight *= norm;
        }
        if (placeMoved) {
            hit_ = hit;
            left_ = left;
            right_ = right;
            const auto& shapes = tables.shapes[object];
            for (std::size_t k = 0; k < modeSlots; ++k) {
                hitAt_[k] = shapeAt(shapes[k], hit);
                leftAt_[k] = shapeAt(shapes[k], left);
                rightAt_[k] = shapeAt(shapes[k], right);
            }
        }
        if (pitchMoved) {
            hz_ = s.hz;
            for (std::size_t k = 0; k < used_; ++k) {
                const auto hz = hz_ * ratio_[k];
                audible_[k] = k < static_cast<std::size_t>(quality_) && hz > 10.0 && hz < rate_ * 0.45;
                if (!audible_[k])
                    continue;
                const auto w = 2.0 * kPi * hz / rate_;
                cos_[k] = std::cos(w);
                sin_[k] = std::sin(w);
            }
        }
        if (decayMoved) {
            decay_ = s.decay;
            release_ = s.release;
            for (std::size_t k = 0; k < used_; ++k) {
                const auto seconds = std::clamp(std::min(decay_ / scale_[k], release_), 0.002, 120.0);
                radius_[k] = std::exp(-kT60 / (seconds * rate_));
            }
        }
        for (std::size_t k = 0; k < used_; ++k) {
            if (!audible_[k]) {
                rotateRe_[k] = 0.0;
                rotateIm_[k] = 0.0;
                strikeIn_[k] = 0.0;
                driveIn_[k] = 0.0;
                outLeft_[k] = 0.0;
                outRight_[k] = 0.0;
                re_[k] = 0.0;
                im_[k] = 0.0;
                continue;
            }
            const auto r = radius_[k];
            rotateRe_[k] = r * cos_[k];
            rotateIm_[k] = r * sin_[k];
            // A blow rings each mode as much as the object moves where it
            // lands; a lasting push, as loud whatever the mode's decay.
            const auto into = weight_[k] * hitAt_[k];
            strikeIn_[k] = into;
            driveIn_[k] = into * std::sqrt(std::max(0.0, 1.0 - r * r));
            outLeft_[k] = leftAt_[k] * modalLevel * kObjectLevels[object];
            outRight_[k] = rightAt_[k] * modalLevel * kObjectLevels[object];
        }
    }

    void setGuide(const Setting& s, const bool force) noexcept
    {
        const auto material = std::clamp(s.material, -1.0, 1.0);
        const auto bright = std::clamp(s.bright, 0.0, 1.0);
        const auto inharm = std::clamp(s.inharm, -1.0, 1.0);
        const auto hit = std::clamp(s.hit, 0.0, 1.0);
        const auto left = std::clamp(s.left, 0.0, 1.0);
        const auto right = std::clamp(s.right, 0.0, 1.0);
        const auto changed = force || moved(s.hz, hz_, hz_ * 1.0e-5) || moved(s.decay, decay_, decay_ * 1.0e-4)
                             || moved(s.release, release_, release_ * 1.0e-4) || moved(material, material_, 1.0e-5)
                             || moved(bright, bright_, 1.0e-5) || moved(inharm, inharm_, 1.0e-5) || moved(hit, hit_, 1.0e-5)
                             || moved(left, left_, 1.0e-5) || moved(right, right_, 1.0e-5);
        if (!changed)
            return;
        hz_ = s.hz;
        decay_ = s.decay;
        release_ = s.release;
        material_ = material;
        bright_ = bright;
        inharm_ = inharm;
        hit_ = hit;
        left_ = left;
        right_ = right;
        ready_ = line_.size() >= 64;
        if (!ready_)
            return;
        // A pipe closed at one end: its reflections of opposite signs, its
        // loop half its period (odd harmonics only); a string and a tube:
        // the same sign at both ends, the loop its whole period.
        const auto pipe = type_ == typePipe;
        const auto trips = pipe ? 2.0 : 1.0;
        sign_ = pipe ? -1.0 : 1.0;
        combSign_ = pipe ? 1.0 : -1.0;
        const auto most = static_cast<double>(line_.size()) - 8.0;
        period_ = std::clamp(rate_ / (trips * std::max(1.0, hz_)), 4.0, most);
        const auto fundamental = rate_ / (trips * period_);
        const auto w = 2.0 * kPi * fundamental / rate_;
        // The losses: the decay at the lowest frequencies, a shorter (or
        // longer) one at the top as the material says, one pole between.
        const auto low = std::clamp(std::min(decay_, release_), 0.002, 120.0);
        const auto top = std::max(1.0, 0.5 * rate_ / fundamental);
        const auto high = std::clamp(low / std::pow(top, 1.5 * material), 0.001, 120.0);
        const auto lowGain = std::pow(10.0, -3.0 * period_ / (rate_ * low));
        const auto highGain = std::pow(10.0, -3.0 * period_ / (rate_ * high));
        auto pole = std::clamp((highGain / lowGain - 1.0) / (highGain / lowGain + 1.0), -0.9, 0.9);
        // The stiffness: allpasses whose delay falls (stretched partials) or
        // rises (squeezed ones) with the frequency.
        const auto c = -0.7 * inharm;
        auto stages = std::abs(c) > 1.0e-3 ? 4 : 0;
        // The loop tuned: what the filters delay at the note taken from the line.
        const auto lossDelay = [w](const double a) { return -std::atan2(a * std::sin(w), 1.0 + a * std::cos(w)) / w; };
        const auto allpassDelay = 1.0 + 2.0 * std::atan2(-c * std::sin(w), 1.0 + c * std::cos(w)) / w;
        auto line = period_ - lossDelay(pole) - static_cast<double>(stages) * allpassDelay;
        while (line < 3.0 && stages > 0) {
            stages /= 2;
            line = period_ - lossDelay(pole) - static_cast<double>(stages) * allpassDelay;
        }
        for (int tries = 0; line < 2.5 && tries < 12; ++tries) {
            pole *= 0.5;
            line = period_ - lossDelay(pole);
        }
        delayTarget_ = std::clamp(line, 2.0, most);
        if (force)
            delay_ = delayTarget_;
        lossPole_ = pole;
        lossGain_ = lowGain * (1.0 + pole);
        allpass_ = c;
        stages_ = stages;
        // A lasting push as loud whatever the decay; a blow as loud as on the
        // modes, spread over the harmonics it reaches.
        driveGain_ = std::sqrt(std::max(0.0, 1.0 - lowGain * lowGain));
        const auto cycle = rate_ / fundamental;
        const auto reach = std::min(1.5 + 48.0 * bright * bright, 0.5 * cycle);
        strikeGain_ = cycle / (2.0 * std::sqrt(1.57 * reach));
        brightCoefficient_ = 1.0 - std::exp(-2.0 * kPi * std::min(fundamental * reach, 0.45 * rate_) / rate_);
        // Where it is struck and listened to: combs across its period.
        hitTap_ = guidePlace(hit) * period_;
        leftTap_ = guidePlace(left) * period_;
        rightTap_ = guidePlace(right) * period_;
        hitLeftTap_ = hitTap_ + leftTap_;
        hitRightTap_ = hitTap_ + rightTap_;
        level_ = guideLevel * kObjectLevels[static_cast<std::size_t>(type_)];
    }

    void processModal(const double* strike, const double* drive, double* outLeft, double* outRight, const int count,
                      const bool excited) noexcept
    {
        // Two modes at a time: two rotations side by side.
        for (std::size_t k = 0; k < used_; k += 2) {
            const auto j = k + 1;
            if (!audible_[k] && !audible_[j])
                continue;
            auto ar = re_[k];
            auto ai = im_[k];
            auto br = re_[j];
            auto bi = im_[j];
            const auto ac = rotateRe_[k];
            const auto as = rotateIm_[k];
            const auto bc = rotateRe_[j];
            const auto bs = rotateIm_[j];
            const auto al = outLeft_[k];
            const auto aR = outRight_[k];
            const auto bl = outLeft_[j];
            const auto bR = outRight_[j];
            if (excited) {
                const auto am = strikeIn_[k];
                const auto ad = driveIn_[k];
                const auto bm = strikeIn_[j];
                const auto bd = driveIn_[j];
                for (int i = 0; i < count; ++i) {
                    const auto nextA = ac * ar - as * ai + strike[i] * am + drive[i] * ad;
                    ai = ac * ai + as * ar;
                    ar = nextA;
                    const auto nextB = bc * br - bs * bi + strike[i] * bm + drive[i] * bd;
                    bi = bc * bi + bs * br;
                    br = nextB;
                    outLeft[i] += ai * al + bi * bl;
                    outRight[i] += ai * aR + bi * bR;
                }
            } else {
                for (int i = 0; i < count; ++i) {
                    const auto nextA = ac * ar - as * ai;
                    ai = ac * ai + as * ar;
                    ar = nextA;
                    const auto nextB = bc * br - bs * bi;
                    bi = bc * bi + bs * br;
                    br = nextB;
                    outLeft[i] += ai * al + bi * bl;
                    outRight[i] += ai * aR + bi * bR;
                }
            }
            // Kept clear of the denormals.
            if (std::abs(ar) + std::abs(ai) < 1.0e-15)
                ar = ai = 0.0;
            if (std::abs(br) + std::abs(bi) < 1.0e-15)
                br = bi = 0.0;
            re_[k] = ar;
            im_[k] = ai;
            re_[j] = br;
            im_[j] = bi;
        }
    }

    // The loop's sample `delay` ago (2 or more), read between its samples
    // (four points, Lagrange: it never adds energy in a loop).
    [[nodiscard]] double readCubic(const double delay) const noexcept
    {
        const auto whole = static_cast<int>(delay);
        const auto f = delay - static_cast<double>(whole);
        const auto at = write_ - whole;
        const auto* line = line_.data();
        const auto before = static_cast<double>(line[(at + 1) & mask_]);
        const auto x0 = static_cast<double>(line[at & mask_]);
        const auto x1 = static_cast<double>(line[(at - 1) & mask_]);
        const auto x2 = static_cast<double>(line[(at - 2) & mask_]);
        const auto c0 = -f * (f - 1.0) * (f - 2.0) / 6.0;
        const auto c1 = (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0;
        const auto c2 = -(f + 1.0) * f * (f - 2.0) / 2.0;
        const auto c3 = (f + 1.0) * f * (f - 1.0) / 6.0;
        return c0 * before + c1 * x0 + c2 * x1 + c3 * x2;
    }

    // What the line held `delay` ago (0: what was just written), between two samples.
    [[nodiscard]] double tap(const double delay) const noexcept
    {
        const auto whole = static_cast<int>(delay);
        const auto f = delay - static_cast<double>(whole);
        const auto a = static_cast<double>(line_[static_cast<std::size_t>((write_ - whole) & mask_)]);
        const auto b = static_cast<double>(line_[static_cast<std::size_t>((write_ - whole - 1) & mask_)]);
        return a + (b - a) * f;
    }

    void processGuide(const double* strike, const double* drive, double* outLeft, double* outRight, const int count) noexcept
    {
        // The delay eased to its new length across the block: a pitch that
        // moves slides, it does not click.
        const auto slide = (delayTarget_ - delay_) / static_cast<double>(std::max(1, count));
        auto* line = line_.data();
        for (int i = 0; i < count; ++i) {
            delay_ += slide;
            // What comes back round: delayed, its losses, its stiffness, its ends' reflections.
            auto back = readCubic(delay_);
            lossState_ = lossGain_ * back - lossPole_ * lossState_;
            back = lossState_;
            for (std::size_t stage = 0; stage < static_cast<std::size_t>(stages_); ++stage) {
                const auto out = allpass_ * back + allpassIn_[stage] - allpass_ * allpassOut_[stage];
                allpassIn_[stage] = back;
                allpassOut_[stage] = out;
                back = out;
            }
            // The excitation, as far up as the brightness lets it (and a
            // breath of offset that keeps the loop clear of the denormals).
            const auto in = strike[i] * strikeGain_ + drive[i] * driveGain_;
            brightState_ += brightCoefficient_ * (in - brightState_);
            const auto y = sign_ * back + brightState_ + 1.0e-18;
            line[write_] = static_cast<float>(y);
            // Struck at one place, heard at two: their combs.
            const auto struck = tap(hitTap_);
            outLeft[i] += level_ * (y + combSign_ * (struck + tap(leftTap_)) + tap(hitLeftTap_));
            outRight[i] += level_ * (y + combSign_ * (struck + tap(rightTap_)) + tap(hitRightTap_));
            write_ = (write_ + 1) & mask_;
        }
        // What fades out of the filters kept clear of the denormals.
        const auto flush = [](double& state) {
            if (std::abs(state) < 1.0e-20)
                state = 0.0;
        };
        flush(brightState_);
        flush(lossState_);
        for (std::size_t stage = 0; stage < allpassIn_.size(); ++stage) {
            flush(allpassIn_[stage]);
            flush(allpassOut_[stage]);
        }
    }

    // As last set (what the coefficients were made for).
    int type_ { -1 };
    double rate_ {};
    int quality_ {};
    double hz_ {};
    double decay_ {};
    double release_ {};
    double material_ {};
    double bright_ {};
    double inharm_ {};
    double hit_ {};
    double left_ {};
    double right_ {};
    // The modes.
    std::size_t used_ {};
    std::array<bool, modeSlots> audible_ {};
    std::array<double, modeSlots> ratio_ {};
    std::array<double, modeSlots> scale_ {};
    std::array<double, modeSlots> weight_ {};
    std::array<double, modeSlots> hitAt_ {};
    std::array<double, modeSlots> leftAt_ {};
    std::array<double, modeSlots> rightAt_ {};
    std::array<double, modeSlots> cos_ {};
    std::array<double, modeSlots> sin_ {};
    std::array<double, modeSlots> radius_ {};
    std::array<double, modeSlots> rotateRe_ {};
    std::array<double, modeSlots> rotateIm_ {};
    std::array<double, modeSlots> strikeIn_ {};
    std::array<double, modeSlots> driveIn_ {};
    std::array<double, modeSlots> outLeft_ {};
    std::array<double, modeSlots> outRight_ {};
    std::array<double, modeSlots> re_ {};
    std::array<double, modeSlots> im_ {};
    // The waveguide.
    std::vector<float> line_;
    int mask_ {};
    int write_ {};
    bool ready_ {};
    double period_ { 8.0 };
    double delay_ { 8.0 };
    double delayTarget_ { 8.0 };
    double sign_ { 1.0 };
    double combSign_ { -1.0 };
    double lossGain_ {};
    double lossPole_ {};
    double lossState_ {};
    double allpass_ {};
    int stages_ {};
    std::array<double, 4> allpassIn_ {};
    std::array<double, 4> allpassOut_ {};
    double strikeGain_ {};
    double driveGain_ {};
    double brightCoefficient_ { 1.0 };
    double brightState_ {};
    double hitTap_ {};
    double leftTap_ {};
    double rightTap_ {};
    double hitLeftTap_ {};
    double hitRightTap_ {};
    double level_ {};
};

class BronzeEngine final : public Engine {
public:
    // The modal tables made here, off the audio thread, before any note.
    BronzeEngine() : tables_(&modalTables()) {}

    void prepare(const double sampleRate) override
    {
        rate_ = std::max(1.0, sampleRate);
        for (auto& resonator : resonators_)
            resonator.prepare(rate_);
    }

    void start(const Values& values, const int note, const float velocity, const double glideFrom, const double sampleRate) noexcept override
    {
        rate_ = std::max(1.0, sampleRate);
        target_ = static_cast<double>(note);
        pitch_ = glideFrom >= 0.0 && values[voicingGlide] > 0.0f ? glideFrom : target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        stage_ = Stage::held;
        quiet_ = false;
        silentSteps_ = 0;
        noise_.stage = Envelope::Stage::idle;
        noise_.level = 0.0;
        noise_.start();
        previousNoise_ = 0.0;
        noiseFilter_.reset();
        for (auto& resonator : resonators_)
            resonator.restart();
        running_ = { false, false };
        lfoPhase_ = 0.0;
        lfoHeld_ = random();
        previousLevel_ = -1.0;
        dcIn_ = {};
        dcOut_ = {};
        limiter_ = 0.0;
        strike(values);
    }

    void retarget(const Values& values, const int note, const float velocity) noexcept override
    {
        target_ = static_cast<double>(note);
        if (values[voicingGlide] <= 0.0f)
            pitch_ = target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        // Legato: the objects slide to the note still ringing; struck again
        // only once let go of (or rung out).
        if (stage_ == Stage::released || quiet_) {
            stage_ = Stage::held;
            noise_.start();
            strike(values);
            quiet_ = false;
            silentSteps_ = 0;
        }
    }

    void release() noexcept override
    {
        if (stage_ == Stage::held) {
            stage_ = Stage::released;
            noise_.release();
        }
    }

    void kill() noexcept override
    {
        stage_ = Stage::idle;
        noise_.stage = Envelope::Stage::idle;
        noise_.level = 0.0;
    }

    [[nodiscard]] bool active() const noexcept override { return stage_ != Stage::idle; }
    [[nodiscard]] bool releasing() const noexcept override { return stage_ == Stage::released; }
    [[nodiscard]] double currentPitch() const noexcept override { return active() ? pitch_ : -1.0; }

    bool render(const Values& values, const prism::Context& context, float* left, float* right, const int count, const double sampleRate,
                const double expressionPitch, const double expressionGain) noexcept override
    {
        if (!active())
            return false;
        rate_ = std::max(1.0, sampleRate);
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto choice = [&values](const int index, const int options) {
            return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(index)])), 0, options - 1);
        };
        // What holds for the whole block.
        const auto loud = 1.0 - std::clamp(v(velocityToVolume), 0.0, 1.0) * (1.0 - velocity_);
        const auto noiseAmount = std::clamp(v(noiseVolume), 0.0, 1.0) * loud * noiseDrive;
        const auto noiseMode = choice(noiseFilter, 3);
        const auto noiseQ = std::clamp(v(noiseResonance), 0.0, 1.0);
        const auto noiseDamping = std::max(0.02, 2.0 - 2.0 * noiseQ * 0.99);
        const auto noiseHz = std::clamp(std::max(20.0, v(noiseCutoff)) * std::pow(2.0, v(noiseKey) * (target_ - 60.0) / 12.0), 20.0,
                                        std::min(20'000.0, rate_ * 0.45));
        const auto noiseG = std::tan(kPi * noiseHz / rate_);
        const auto coefficient = [this](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(controlStep) / (std::max(1.0e-4, seconds * fraction) * rate_));
        };
        const auto attackStep = coefficient(v(noiseAttack), 1.0 / 1.466);
        const auto decayStep = coefficient(v(noiseDecay), 1.0 / 4.6);
        const auto releaseStep = coefficient(v(noiseRelease), 1.0 / 4.6);
        const auto sustain = std::clamp(v(noiseSustain), 0.0, 1.0);
        const auto serial = choice(structureChoice, 2) == 0;
        const auto balance = std::clamp(v(structureBalance), 0.0, 1.0);
        const auto gainOne = std::min(1.0, 2.0 * (1.0 - balance));
        const auto gainTwo = std::min(1.0, 2.0 * balance);
        // A resonator not heard is not played (the first still is when it rings into the second).
        const std::array<bool, 2> running { serial || gainOne > 0.0, gainTwo > 0.0 };
        const auto outGain = std::pow(10.0, std::clamp(v(outputGain), -24.0, 12.0) / 20.0);
        const auto limiting = v(outputLimiter) >= 0.5;
        const auto limiterFall = std::exp(-1.0 / (0.08 * rate_));
        const auto dcPole = 1.0 - 2.0 * kPi * 8.0 / rate_;
        const auto master = v(voicingMaster);
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        const auto lfoHz = [&] {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(lfoSync)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(lfoRate);
        }();
        const auto shape = choice(lfoShape, 5);
        const auto released = stage_ == Stage::released;
        const auto releaseSeconds = std::max(0.005, v(releaseTime));
        const auto toTune = v(lfoToTune);
        const auto toDecay = std::clamp(v(lfoToDecay), -1.0, 1.0);
        const auto toHit = v(lfoToHit);
        const auto toVolume = std::clamp(v(lfoToVolume), 0.0, 1.0);

        for (int done = 0; done < count; done += controlStep) {
            const auto length = std::min(controlStep, count - done);
            const auto part = static_cast<double>(length) / static_cast<double>(controlStep);
            // The slow things, once a step: the noise's envelope, the LFO, the glide, the resonators' coefficients.
            const auto noiseLevel = noise_.step(attackStep, decayStep, sustain, releaseStep, part);
            lfoPhase_ += lfoHz * static_cast<double>(length) / rate_;
            if (lfoPhase_ >= 1.0) {
                lfoPhase_ -= std::floor(lfoPhase_);
                lfoHeld_ = random();
            }
            const auto lfo = lfoWave(shape, lfoPhase_, lfoHeld_);
            // The glide: the same time whatever the distance, at an even pace.
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            const auto pitch = pitch_ + expressionPitch + bend + lfo * toTune;
            const auto decayFactor = std::pow(2.0, 3.0 * lfo * toDecay);
            for (std::size_t index = 0; index < 2; ++index) {
                const auto base = index == 0 ? static_cast<int>(resonator1) : static_cast<int>(resonator2);
                Setting setting;
                setting.type = choice(base + rType, typeCount);
                setting.quality = std::clamp(static_cast<int>(std::lround(v(base + rQuality))), modeLeast, modeMost);
                setting.hz = 440.0 * std::pow(2.0, (pitch + std::round(v(base + rTune)) + v(base + rFine) / 100.0 - 69.0) / 12.0);
                setting.decay = std::max(0.005, v(base + rDecay)) * decayFactor;
                setting.release = released ? releaseSeconds : 1.0e9;
                setting.material = v(base + rMaterial);
                setting.bright = v(base + rBright);
                setting.inharm = v(base + rInharm);
                setting.hit = std::clamp(v(base + rHit) + lfo * toHit, 0.0, 1.0);
                setting.left = v(base + rLeft);
                setting.right = v(base + rRight);
                if (running[index])
                    resonators_[index].set(setting, rate_, *tables_);
                else if (running_[index])
                    resonators_[index].clear();
                running_[index] = running[index];
                show(base + rHit, setting.hit, v(base + rHit));
                show(base + rDecay, setting.decay, v(base + rDecay));
            }
            show(noiseCutoff, noiseHz, v(noiseCutoff));
            showLfo(0, lfo, lfoPhase_);
            showEnvelope(1, noiseLevel);

            // The exciters: the mallet's blow (and its noise), the noise's push.
            std::array<double, stepFrames> strikes {};
            std::array<double, stepFrames> drives {};
            auto excited = false;
            if (pulseAt_ < pulseLength_ || burstAt_ < burstLength_) {
                for (std::size_t i = 0; i < static_cast<std::size_t>(length); ++i)
                    strikes[i] = malletSample();
                excited = true;
            }
            const auto noiseFrom = previousNoise_;
            previousNoise_ = noiseLevel;
            if (noiseAmount > 0.0 && (noiseLevel > 1.0e-6 || noiseFrom > 1.0e-6)) {
                for (std::size_t i = 0; i < static_cast<std::size_t>(length); ++i) {
                    const auto envelope = noiseFrom + (noiseLevel - noiseFrom) * static_cast<double>(i + 1) / static_cast<double>(length);
                    auto x = noiseFilter_.variable(random(), noiseG, noiseQ, noiseMode, false);
                    if (noiseMode == 1)
                        x *= noiseDamping;
                    drives[i] = x * envelope * noiseAmount;
                }
                excited = true;
            }
            if (excited)
                quiet_ = false;

            // The resonators: side by side, or the first ringing into the second.
            std::array<double, stepFrames> oneLeft {};
            std::array<double, stepFrames> oneRight {};
            std::array<double, stepFrames> twoLeft {};
            std::array<double, stepFrames> twoRight {};
            if (!quiet_) {
                if (running[0])
                    resonators_[0].process(strikes.data(), drives.data(), oneLeft.data(), oneRight.data(), length, excited);
                if (running[1]) {
                    if (serial) {
                        std::array<double, stepFrames> fed {};
                        const std::array<double, stepFrames> none {};
                        const auto feed = 0.5 * serialGain / resonators_[1].loudness();
                        for (std::size_t i = 0; i < static_cast<std::size_t>(length); ++i)
                            fed[i] = (oneLeft[i] + oneRight[i]) * feed;
                        resonators_[1].process(none.data(), fed.data(), twoLeft.data(), twoRight.data(), length, true);
                    } else {
                        resonators_[1].process(strikes.data(), drives.data(), twoLeft.data(), twoRight.data(), length, excited);
                    }
                }
            }

            // The mix: its balance, its gain, no offset, the limiter, the volume eased across the step.
            const auto tremolo = 1.0 - toVolume * 0.5 * (1.0 - lfo);
            const auto level = master * expressionGain * tremolo;
            const auto from = previousLevel_ < 0.0 ? level : previousLevel_;
            previousLevel_ = level;
            auto peak = 0.0;
            // At rest (held, rung out): nothing to add, nothing to keep.
            const auto mixed = quiet_ ? 0 : length;
            if (quiet_) {
                dcIn_ = {};
                dcOut_ = {};
                limiter_ = 0.0;
            }
            for (std::size_t i = 0; i < static_cast<std::size_t>(mixed); ++i) {
                const auto gain = from + (level - from) * static_cast<double>(i + 1) / static_cast<double>(length);
                std::array<double, 2> channel { (gainOne * oneLeft[i] + gainTwo * twoLeft[i]) * outGain,
                                                (gainOne * oneRight[i] + gainTwo * twoRight[i]) * outGain };
                for (std::size_t side = 0; side < 2; ++side) {
                    const auto blocked = channel[side] - dcIn_[side] + dcPole * dcOut_[side];
                    dcIn_[side] = channel[side];
                    dcOut_[side] = blocked;
                    channel[side] = blocked;
                }
                if (limiting) {
                    const auto loudest = std::max(std::abs(channel[0]), std::abs(channel[1]));
                    limiter_ = loudest > limiter_ ? loudest : limiter_ * limiterFall;
                    if (limiter_ > limitKnee) {
                        const auto ceiling = limitKnee + (1.0 - limitKnee) * std::tanh((limiter_ - limitKnee) / (1.0 - limitKnee));
                        const auto squeeze = ceiling / limiter_;
                        channel[0] *= squeeze;
                        channel[1] *= squeeze;
                    }
                }
                peak = std::max({ peak, std::abs(channel[0]), std::abs(channel[1]) });
                const auto at = static_cast<std::size_t>(done) + i;
                left[at] += static_cast<float>(channel[0] * gain);
                right[at] += static_cast<float>(channel[1] * gain);
            }
            for (std::size_t side = 0; side < 2; ++side)
                if (std::abs(dcOut_[side]) < 1.0e-20)
                    dcOut_[side] = 0.0;
            if (limiter_ < 1.0e-12)
                limiter_ = 0.0;
            if (!std::isfinite(peak)) {
                // Never heard, but never kept either.
                for (auto& resonator : resonators_)
                    resonator.clear();
                dcIn_ = {};
                dcOut_ = {};
                limiter_ = 0.0;
                peak = 0.0;
            }
            showEnvelope(0, std::min(1.0, peak));
            // Rung out: once let go of, the voice ends; held, it rests until struck again.
            const auto exciting = excited || (noiseAmount > 0.0 && noise_.stage == Envelope::Stage::attack);
            silentSteps_ = !exciting && peak < 2.0e-6 ? silentSteps_ + 1 : 0;
            if (silentSteps_ >= 8) {
                if (released) {
                    stage_ = Stage::idle;
                    return false;
                }
                if (!quiet_) {
                    quiet_ = true;
                    for (auto& resonator : resonators_)
                        resonator.clear();
                }
            }
        }
        return active();
    }

private:
    enum class Stage : std::uint8_t { idle, held, released };

    // The mallet: its blow (half a sine, as long as it is soft, shorter up
    // the keyboard, as loud whatever its length) and the noise where it lands.
    void strike(const Values& values) noexcept
    {
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto stiffness
            = std::clamp(v(malletStiffness) - 0.7 * std::clamp(v(velocityToStiffness), 0.0, 1.0) * (1.0 - velocity_), 0.0, 1.0);
        const auto seconds = 0.004 * std::pow(0.02, stiffness) * std::pow(2.0, -(target_ - 60.0) / 24.0);
        pulseLength_ = std::clamp(static_cast<int>(seconds * rate_), 2, static_cast<int>(0.012 * rate_) + 2);
        pulseAt_ = 0;
        const auto loud = 1.0 - std::clamp(v(velocityToVolume), 0.0, 1.0) * (1.0 - velocity_);
        const auto amount = std::clamp(v(malletVolume), 0.0, 1.0) * loud;
        pulsePeak_ = amount * kPi / (2.0 * static_cast<double>(pulseLength_));
        burstLength_ = pulseLength_ + static_cast<int>(0.002 * rate_);
        burstAt_ = 0;
        burstPeak_ = pulsePeak_ * std::clamp(v(malletNoise), 0.0, 1.0) * burstLevel;
        // Its colour: dark (a thud) to bright (a click).
        const auto colour = std::clamp(v(malletColour), 0.0, 1.0);
        burstLowCoefficient_ = 1.0 - std::exp(-2.0 * kPi * std::min(400.0 * std::pow(2.0, 5.6 * colour), 0.45 * rate_) / rate_);
        burstHighCoefficient_ = 1.0 - std::exp(-2.0 * kPi * 30.0 * std::pow(2.0, 7.0 * colour) / rate_);
        burstLow_ = 0.0;
        burstHigh_ = 0.0;
        if (amount <= 0.0) {
            pulseAt_ = pulseLength_;
            burstAt_ = burstLength_;
        }
    }

    double malletSample() noexcept
    {
        auto x = 0.0;
        if (pulseAt_ < pulseLength_) {
            x = pulsePeak_ * std::sin(kPi * (static_cast<double>(pulseAt_) + 0.5) / static_cast<double>(pulseLength_));
            ++pulseAt_;
        }
        if (burstAt_ < burstLength_) {
            const auto fade = 1.0 - static_cast<double>(burstAt_) / static_cast<double>(burstLength_);
            const auto white = random() * burstPeak_ * fade;
            burstLow_ += burstLowCoefficient_ * (white - burstLow_);
            burstHigh_ += burstHighCoefficient_ * (burstLow_ - burstHigh_);
            x += burstLow_ - burstHigh_;
            ++burstAt_;
        }
        return x;
    }

    // The pace a glide from here to the note takes to last its time.
    void aimGlide(const Values& values) noexcept
    {
        const auto seconds = static_cast<double>(values[voicingGlide]);
        glidePerSample_ = seconds > 0.0 ? std::abs(target_ - pitch_) / (seconds * rate_) : 0.0;
    }

    double random() noexcept
    {
        seed_ = seed_ * 1'664'525u + 1'013'904'223u;
        return static_cast<double>(seed_ >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }

    const ModalTables* tables_ {};
    double rate_ { 48'000.0 };
    double target_ {};
    double pitch_ {};
    double velocity_ { 1.0 };
    double glidePerSample_ {};
    Stage stage_ { Stage::idle };
    bool quiet_ {};
    int silentSteps_ {};
    Envelope noise_;
    double previousNoise_ {};
    Filter noiseFilter_;
    int pulseLength_ {};
    int pulseAt_ {};
    int burstLength_ {};
    int burstAt_ {};
    double pulsePeak_ {};
    double burstPeak_ {};
    double burstLowCoefficient_ {};
    double burstHighCoefficient_ {};
    double burstLow_ {};
    double burstHigh_ {};
    std::array<Resonator, 2> resonators_ {};
    std::array<bool, 2> running_ {};
    double lfoPhase_ {};
    double lfoHeld_ {};
    double previousLevel_ { -1.0 };
    std::array<double, 2> dcIn_ {};
    std::array<double, 2> dcOut_ {};
    double limiter_ {};
    // Each voice its own draw: their noises apart.
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeBronze()
{
    return std::make_unique<BronzeEngine>();
}

std::vector<float> bronzePicture(const Values& values, const std::string_view part, const int points)
{
    // One resonator struck once, its first eighty milliseconds at 110 Hz
    // (about nine periods): its partials as its object, decay, material,
    // brightness, inharmonicity and places make them.
    const auto index = part == "r1" ? 0 : part == "r2" ? 1 : -1;
    if (index < 0 || points < 2)
        return {};
    const auto base = index == 0 ? static_cast<int>(resonator1) : static_cast<int>(resonator2);
    const auto v = [&values](const int at) { return static_cast<double>(values[static_cast<std::size_t>(at)]); };
    const auto type = std::clamp(static_cast<int>(std::lround(v(base + rType))), 0, typeCount - 1);
    const auto quality = std::clamp(static_cast<int>(std::lround(v(base + rQuality))), modeLeast, modeMost);
    const auto decay = std::max(0.005, v(base + rDecay));
    const auto material = std::clamp(v(base + rMaterial), -1.0, 1.0);
    const auto bright = std::clamp(v(base + rBright), 0.0, 1.0);
    const auto inharm = std::clamp(v(base + rInharm), -1.0, 1.0);
    const auto hit = std::clamp(v(base + rHit), 0.0, 1.0);
    const auto listen = std::clamp(v(base + rLeft), 0.0, 1.0);
    const auto reach = 1.5 + 48.0 * bright * bright;
    struct Partial {
        double ratio;
        double amplitude;
        double seconds;
    };
    std::array<Partial, 48> partials {};
    std::size_t count = 0;
    if (type < modalTypes) {
        const auto& tables = modalTables();
        const auto object = static_cast<std::size_t>(type);
        for (std::size_t k = 0; k < static_cast<std::size_t>(quality); ++k) {
            const auto ratio = std::pow(tables.ratios[object][k], 1.0 + 0.3 * inharm);
            const auto over = ratio / reach;
            partials[count++] = { ratio, shapeAt(tables.shapes[object][k], hit) * shapeAt(tables.shapes[object][k], listen) / (1.0 + over * over),
                                  decay / std::pow(ratio, 1.5 * material) };
        }
    } else {
        // A waveguide's harmonics (a closed pipe's odd ones), its combs where
        // it is struck and heard, stretched as its stiffness says.
        const auto pipe = type == typePipe;
        const auto struck = guidePlace(hit);
        const auto heard = guidePlace(listen);
        for (int n = 1; count < partials.size(); ++n) {
            const auto harmonic = static_cast<double>(pipe ? 2 * n - 1 : n);
            const auto combs = pipe ? std::cos(kPi * harmonic * struck * 0.5) * std::cos(kPi * harmonic * heard * 0.5)
                                    : std::sin(kPi * harmonic * struck) * std::sin(kPi * harmonic * heard);
            const auto over = harmonic / reach;
            partials[count++] = { harmonic * std::sqrt(std::max(0.2, 1.0 + 0.0008 * inharm * harmonic * harmonic)),
                                  combs / (1.0 + over * over) / std::sqrt(harmonic), decay / std::pow(harmonic, 0.9 * material) };
        }
    }
    constexpr double hz = 110.0;
    constexpr double window = 0.08;
    const auto dt = window / static_cast<double>(points - 1);
    std::vector<double> wave(static_cast<std::size_t>(points), 0.0);
    for (std::size_t at = 0; at < count; ++at) {
        const auto& partial = partials[at];
        const auto turn = 2.0 * kPi * hz * partial.ratio * dt;
        // Past what the picture can draw: left out.
        if (turn > 0.8 * kPi || std::abs(partial.amplitude) < 1.0e-6)
            continue;
        const auto fall = std::exp(-kT60 * dt / std::clamp(partial.seconds, 0.002, 120.0));
        const auto turnCos = std::cos(turn);
        const auto turnSin = std::sin(turn);
        auto c = 1.0;
        auto s = 0.0;
        auto amplitude = partial.amplitude;
        for (auto& sample : wave) {
            sample += amplitude * s;
            const auto next = c * turnCos - s * turnSin;
            s = s * turnCos + c * turnSin;
            c = next;
            amplitude *= fall;
        }
    }
    auto peak = 1.0e-9;
    for (const auto sample : wave)
        peak = std::max(peak, std::abs(sample));
    std::vector<float> result(static_cast<std::size_t>(points));
    for (std::size_t at = 0; at < result.size(); ++at)
        result[at] = static_cast<float>(wave[at] * 0.9 / peak);
    return result;
}

} // namespace cydaw::engine::synth::detail
