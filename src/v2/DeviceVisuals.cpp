#include "DeviceVisuals.h"

#include "StudioEffectsView.h"

#include <cydaw/engine/ProEffectsDesign.h>

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <cydaw/engine/BiquadDesign.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/WavetableShape.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <map>
#include <numbers>

namespace cydaw::v2::visual {

using ui::dp;
using ui::L;

namespace {

// The rate the curves are drawn for; close enough to any device's.
constexpr double kRate = 48'000.0;
constexpr double kPi = std::numbers::pi;
constexpr float kMinimumHz = 20.0f;
constexpr float kMaximumHz = 20'000.0f;

// ---- Values ------------------------------------------------------------------------------------

bool isLogarithmic(const Param& parameter)
{
    return parameter.minimum > 0.0
           && (parameter.unit == "Hz" || parameter.unit == "ms" || parameter.unit == "s" || parameter.unit == "cm");
}

double clampTo(const Param& parameter, const double value)
{
    return std::clamp(value, std::min(parameter.minimum, parameter.maximum), std::max(parameter.minimum, parameter.maximum));
}

double normalised(const Param& parameter, const double value)
{
    if (parameter.maximum <= parameter.minimum)
        return 0.0;
    if (isLogarithmic(parameter))
        return std::log(std::max(value, parameter.minimum) / parameter.minimum)
               / std::log(parameter.maximum / parameter.minimum);
    return (value - parameter.minimum) / (parameter.maximum - parameter.minimum);
}

double fromNormalised(const Param& parameter, const double t)
{
    const auto clamped = std::clamp(t, 0.0, 1.0);
    if (isLogarithmic(parameter))
        return parameter.minimum * std::pow(parameter.maximum / parameter.minimum, clamped);
    return parameter.minimum + (parameter.maximum - parameter.minimum) * clamped;
}

bool isStepped(const Param& parameter);

// Applies a value to the parameter and its graph; true when it moved.
bool apply(Param& parameter, const double value, const Setter& set)
{
    auto next = clampTo(parameter, value);
    if (isStepped(parameter))
        next = std::round(next);
    if (std::abs(next - parameter.value) < 1.0e-9)
        return false;
    parameter.value = next;
    if (set)
        set(parameter, next);
    return true;
}

Param* parameterAt(std::vector<Param>& parameters, const Pane& pane, const std::size_t slot)
{
    if (slot >= pane.parameters.size())
        return nullptr;
    const auto index = pane.parameters[slot];
    return index >= 0 && index < static_cast<int>(parameters.size()) ? &parameters[static_cast<std::size_t>(index)]
                                                                       : nullptr;
}

// ---- Geometry ----------------------------------------------------------------------------------

struct Plot {
    ImVec2 min;
    ImVec2 max;
    [[nodiscard]] float width() const { return max.x - min.x; }
    [[nodiscard]] float height() const { return max.y - min.y; }
    [[nodiscard]] bool contains(const ImVec2 point) const
    {
        return point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y;
    }
};

float xOfHz(const Plot& plot, const float hz)
{
    const auto t = std::log(std::clamp(hz, kMinimumHz, kMaximumHz) / kMinimumHz) / std::log(kMaximumHz / kMinimumHz);
    return plot.min.x + t * plot.width();
}

float hzOfX(const Plot& plot, const float x)
{
    const auto t = std::clamp((x - plot.min.x) / std::max(1.0f, plot.width()), 0.0f, 1.0f);
    return kMinimumHz * std::pow(kMaximumHz / kMinimumHz, t);
}

// A dB scale from `top` at the plot's top to `bottom` at its bottom.
float yOfDb(const Plot& plot, const float db, const float top, const float bottom)
{
    const auto t = (top - db) / (top - bottom);
    return plot.min.y + std::clamp(t, -0.05f, 1.05f) * plot.height();
}

float dbOfY(const Plot& plot, const float y, const float top, const float bottom)
{
    return top - (y - plot.min.y) / std::max(1.0f, plot.height()) * (top - bottom);
}

void smallText(ImDrawList* list, const ImVec2 position, const ImU32 colour, const char* text, const float scale = 0.72f)
{
    list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, position, colour, text);
}

ImVec2 smallTextSize(const char* text, const float scale = 0.72f)
{
    return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * scale, FLT_MAX, 0.0f, text);
}

void frequencyGrid(ImDrawList* list, const Plot& plot)
{
    const auto& p = theme::palette();
    static constexpr float lines[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1'000.0f, 2'000.0f, 5'000.0f, 10'000.0f };
    for (const auto hz : lines) {
        const auto x = xOfHz(plot, hz);
        const auto strong = hz == 100.0f || hz == 1'000.0f || hz == 10'000.0f;
        list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y), strong ? p.gridStrong : p.grid);
        if (strong)
            smallText(list, ImVec2(x + dp(2.0f), plot.max.y - ImGui::GetFontSize() * 0.75f), p.textFaint,
                      hz >= 1'000.0f ? (hz >= 10'000.0f ? "10k" : "1k") : "100", 0.66f);
    }
}

// The region under a curve, then the curve.
void curve(ImDrawList* list, const std::vector<ImVec2>& points, const ImU32 colour, const float thickness,
           const ImU32 fill, const float baseY)
{
    if (points.size() < 2)
        return;
    if ((fill & IM_COL32_A_MASK) != 0) {
        // Column by column down to the base. One polygon closed along the
        // base was the obvious shape, but ImGui's concave filler mistakes a
        // curve that touches or crosses its base for another polygon and
        // spills triangles over the display.
        const auto flags = list->Flags;
        list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        for (std::size_t index = 0; index + 1 < points.size(); ++index) {
            const auto& a = points[index];
            const auto& b = points[index + 1];
            list->AddQuadFilled(ImVec2(a.x, baseY), a, b, ImVec2(b.x, baseY), fill);
        }
        list->Flags = flags;
    }
    list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, thickness);
}

void drawSpectrum(ImDrawList* list, const Plot& plot, const Spectrum& spectrum, const ImU32 line, const ImU32 fill)
{
    if (!spectrum.visible())
        return;
    std::vector<ImVec2> points;
    points.reserve(Spectrum::binCount);
    const auto& levels = spectrum.levels();
    for (auto bin = 0; bin < Spectrum::binCount; ++bin) {
        const auto x = xOfHz(plot, Spectrum::frequencyOfBin(static_cast<float>(bin)));
        points.emplace_back(x, plot.max.y - levels[static_cast<std::size_t>(bin)] * plot.height());
    }
    curve(list, points, line, dp(1.0f), fill, plot.max.y);
}

void handle(ImDrawList* list, const ImVec2 centre, const ImU32 colour, const bool hot)
{
    const auto& p = theme::palette();
    const auto radius = hot ? dp(5.5f) : dp(4.5f);
    list->AddCircleFilled(centre, radius + dp(1.5f), theme::alpha(p.bg0, 0.85f), 16);
    list->AddCircleFilled(centre, radius, hot ? theme::brighter(colour, 0.25f) : colour, 16);
}

// The plot's own button: hover, drag, wheel.
struct Interaction {
    bool hovered {};
    bool active {};
    bool activated {};
    bool doubleClicked {};
    ImVec2 mouse {};
    ImVec2 delta {};
    float wheel {};
    // Ctrl held: slowly (fine); Shift: quickly. `speed` says how fast a
    // drag or the wheel moves what it moves (0.15, 1, 4).
    bool fine {};
    float speed { 1.0f };
};

Interaction interact(const char* id, const Plot& plot)
{
    ImGui::SetCursorScreenPos(plot.min);
    ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, plot.width()), std::max(1.0f, plot.height())));
    Interaction result;
    auto& io = ImGui::GetIO();
    result.hovered = ImGui::IsItemHovered();
    result.active = ImGui::IsItemActive();
    result.activated = ImGui::IsItemActivated();
    result.doubleClicked = result.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    result.mouse = io.MousePos;
    result.delta = io.MouseDelta;
    result.wheel = result.hovered ? io.MouseWheel : 0.0f;
    result.fine = io.KeyCtrl;
    result.speed = ui::valueSpeed();
    if (result.hovered && result.wheel != 0.0f) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        ui::takeWheel();
    }
    return result;
}

// ---- Responses ---------------------------------------------------------------------------------

std::complex<double> unitDelay(const double hz)
{
    const auto omega = 2.0 * kPi * std::min(hz, kRate * 0.4999) / kRate;
    return std::polar(1.0, -omega);
}

// The state-variable low-pass the engine runs, through its bilinear warp.
float lowpassDb(const float hz, const float cutoff, const float q)
{
    const auto wc = std::tan(kPi * std::min<double>(cutoff, kRate * 0.45) / kRate);
    const auto w = std::tan(kPi * std::min<double>(hz, kRate * 0.4999) / kRate);
    const auto x = w / std::max(wc, 1.0e-9);
    const auto damping = 1.0 / std::max(0.1, static_cast<double>(q));
    const auto denominator = (1.0 - x * x) * (1.0 - x * x) + (x * damping) * (x * damping);
    return static_cast<float>(-10.0 * std::log10(std::max(denominator, 1.0e-12)));
}

std::complex<double> onePoleLow(const double hz, const double cutoff)
{
    const auto c = 1.0 - std::exp(-2.0 * kPi * cutoff / kRate);
    return c / (1.0 - (1.0 - c) * unitDelay(hz));
}

float highpassDb(const float hz, const float cutoff)
{
    const auto response = 1.0 - onePoleLow(hz, std::min<double>(cutoff, kRate * 0.45));
    return static_cast<float>(20.0 * std::log10(std::max(std::abs(response), 1.0e-9)));
}

float peakingDb(const float hz, const float frequency, const float gainDb, const float q)
{
    const auto amplitude = std::pow(10.0, gainDb / 40.0);
    const auto omega = 2.0 * kPi * std::min<double>(frequency, kRate * 0.45) / kRate;
    const auto alpha = std::sin(omega) / (2.0 * std::max(0.1f, q));
    const auto cosine = std::cos(omega);
    const auto z = unitDelay(hz);
    const auto numerator = (1.0 + alpha * amplitude) - 2.0 * cosine * z + (1.0 - alpha * amplitude) * z * z;
    const auto denominator = (1.0 + alpha / amplitude) - 2.0 * cosine * z + (1.0 - alpha / amplitude) * z * z;
    return static_cast<float>(20.0 * std::log10(std::max(std::abs(numerator / denominator), 1.0e-9)));
}

// The tone control: the signal plus `amount` of what lies above 900 Hz.
float toneDb(const float hz, const float amount)
{
    const auto response = 1.0 + static_cast<double>(amount) * (1.0 - onePoleLow(hz, 900.0));
    return static_cast<float>(20.0 * std::log10(std::max(std::abs(response), 1.0e-9)));
}

// ---- Pane frames -------------------------------------------------------------------------------

void paneTitle(ImDrawList* list, const Plot& plot, const char* title)
{
    const auto& p = theme::palette();
    smallText(list, ImVec2(plot.min.x + dp(5.0f), plot.min.y + dp(3.0f)), theme::alpha(p.textDim, 0.85f), title, 0.68f);
}

const char* paneTitleOf(const Pane& pane)
{
    switch (pane.kind) {
    case PaneKind::spectrum: return L("SPECTRE", "SPECTRUM");
    case PaneKind::filter: return pane.variant == 1 ? L("PASSE-HAUT", "HIGH-PASS") : L("PASSE-BAS", "LOW-PASS");
    case PaneKind::equalizer: return L("ÉGALISEUR", "EQUALISER");
    case PaneKind::dynamics: return L("COMPRESSION", "COMPRESSION");
    case PaneKind::saturation: return pane.variant == 1 ? L("ÉCRÊTAGE", "CLIPPING") : L("SATURATION", "SATURATION");
    case PaneKind::envelope: return L("ENVELOPPE", "ENVELOPE");
    case PaneKind::oscillator: return L("OSCILLATEUR", "OSCILLATOR");
    case PaneKind::wavetable: return L("TABLE D'ONDES", "WAVETABLE");
    case PaneKind::delay: return L("ÉCHO", "ECHO");
    case PaneKind::reverb: return L("RÉVERBÉRATION", "REVERB");
    case PaneKind::width: return L("STÉRÉO", "STEREO");
    case PaneKind::level: return L("NIVEAU", "LEVEL");
    case PaneKind::lfo: return "LFO";
    case PaneKind::scope: return L("OSCILLOSCOPE", "OSCILLOSCOPE");
    case PaneKind::multiFilter: return L("FILTRE", "FILTER");
    case PaneKind::autoFilter: return L("FILTRE AUTO", "AUTO FILTER");
    case PaneKind::eqThree: return L("EQ 3 BANDES", "3-BAND EQ");
    case PaneKind::sweep: return pane.variant == 0 ? "CHORUS" : pane.variant == 1 ? "FLANGER" : "PHASER";
    case PaneKind::tremolo: return L("TRÉMOLO", "TREMOLO");
    case PaneKind::crusher: return "BITCRUSHER";
    case PaneKind::limiter: return L("LIMITEUR", "LIMITER");
    case PaneKind::gate: return "GATE";
    case PaneKind::transient: return L("TRANSITOIRES", "TRANSIENTS");
    case PaneKind::eqPro: return L("ÉGALISEUR PRO", "PRO EQ");
    case PaneKind::compPro: return L("COMPRESSEUR PRO", "PRO COMPRESSOR");
    case PaneKind::reverbPro: return L("RÉVERBE PRO", "PRO REVERB");
    case PaneKind::limiterPro: return L("LIMITEUR PRO", "PRO LIMITER");
    case PaneKind::filterPro: return L("FILTRE PRO", "PRO FILTER");
    case PaneKind::ampSim: return L("AMPLI", "AMP");
    case PaneKind::cabSim: return L("BAFFLE ET MICRO", "CABINET AND MIC");
    case PaneKind::micSim: return L("MICRO", "MICROPHONE");
    case PaneKind::pression: return "PRESSION";
    case PaneKind::fournaise: return "FOURNAISE";
    case PaneKind::echoBande: return L("ÉCHO BANDE", "ECHO BANDE");
    case PaneKind::multibandPro: return "MULTIBANDE PRO";
    case PaneKind::velours: return "VELOURS";
    case PaneKind::patine: return "PATINE";
    case PaneKind::sonometre: return L("SONOMÈTRE", "SONOMETRE");
    case PaneKind::tranche: return "TRANCHE";
    case PaneKind::finition: return "FINITION";
    case PaneKind::halo: return "HALO";
    case PaneKind::vocoder: return L("VOCODEUR", "VOCODER");
    case PaneKind::deEsser: return L("DÉ-ESSEUR", "DE-ESSER");
    case PaneKind::tuner: return L("ACCORDEUR", "TUNER");
    case PaneKind::harmonie: return L("HARMONIE", "HARMONIE");
    case PaneKind::justesse: return L("JUSTESSE", "JUSTESSE");
    case PaneKind::empreinte: return L("EMPREINTE", "EMPREINTE");
    case PaneKind::givre: return L("GIVRE", "GIVRE");
    case PaneKind::grains: return L("GRAINS", "GRAINS");
    case PaneKind::saccade: return L("SACCADE", "SACCADE");
    case PaneKind::boucleur: return L("BOUCLEUR", "LOOPER");
    case PaneKind::pompe: return L("POMPE", "POMPE");
    case PaneKind::decaleur: return L("DÉCALEUR", "DECALEUR");
    case PaneKind::anneau: return L("ANNEAU", "ANNEAU");
    case PaneKind::rotor: return L("ROTOR", "ROTOR");
    case PaneKind::resonateurs: return L("RÉSONATEURS", "RESONATORS");
    case PaneKind::corps: return L("CORPS", "CORPS");
    case PaneKind::peigne: return L("PEIGNE", "COMB");
    case PaneKind::utilitaire: return L("UTILITAIRE", "UTILITY");
    case PaneKind::stereoscope: return L("STÉRÉOSCOPE", "STEREOSCOPE");
    case PaneKind::arpege: return L("ARPÈGE", "ARPEGGIATOR");
    case PaneKind::accords: return L("ACCORDS", "CHORDS");
    case PaneKind::gamme: return L("GAMME", "SCALE");
    case PaneKind::echoNotes: return L("ÉCHO DE NOTES", "NOTE ECHO");
    case PaneKind::alea: return L("ALÉA", "RANDOM");
    case PaneKind::duree: return L("DURÉE", "NOTE LENGTH");
    case PaneKind::sequenceur: return L("SÉQUENCEUR", "STEP SEQUENCER");
    case PaneKind::roulement: return L("ROULEMENT", "NOTE REPEAT");
    case PaneKind::spectrometer: return L("SPECTROMÈTRE", "SPECTROMETER");
    }
    return "";
}

// ---- The panes ---------------------------------------------------------------------------------

// ---- The spectrometer --------------------------------------------------------------------------

// Each spectrometer's analysis, kept from frame to frame by its device (the
// binding of its first value), updated once a frame whichever view draws it.
struct SpectrometerView {
    Spectrometer analysis;
    int frame { -1 };
};

SpectrometerView& spectrometerView(const juce::String& key)
{
    static std::map<juce::String, SpectrometerView> views;
    return views[key];
}

// From silence (0) to full scale (1): night blue, violet, magenta, orange,
// pale yellow, as a waterfall's heat.
ImU32 heatColour(const float t)
{
    static constexpr std::array<std::array<float, 3>, 6> stops { {
        { 0.05f, 0.05f, 0.10f },
        { 0.16f, 0.07f, 0.36f },
        { 0.45f, 0.10f, 0.52f },
        { 0.78f, 0.22f, 0.42f },
        { 0.97f, 0.52f, 0.20f },
        { 0.99f, 0.94f, 0.62f },
    } };
    const auto x = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(stops.size() - 1);
    const auto index = std::min(static_cast<int>(x), static_cast<int>(stops.size()) - 2);
    const auto f = x - static_cast<float>(index);
    const auto& a = stops[static_cast<std::size_t>(index)];
    const auto& b = stops[static_cast<std::size_t>(index + 1)];
    return ImGui::GetColorU32(ImVec4(a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, 1.0f));
}

// "A#3" for a frequency.
juce::String noteName(const float hz)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto note = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(std::max(1.0f, hz) / 440.0)));
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1);
}

juce::String hertzText(const float hz)
{
    return hz >= 1'000.0f ? juce::String(hz / 1'000.0f, hz >= 10'000.0f ? 1 : 2) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz";
}

bool spectrometerPlot(const Plot& plot, std::vector<Param>& parameters, const Pane& pane, const ImU32 colour, const Live& live,
                      const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    Spectrometer::Settings settings;
    const auto read = [&](const std::size_t slot, float& value) {
        if (const auto* parameter = parameterAt(parameters, pane, slot))
            value = static_cast<float>(parameter->value);
    };
    read(0, settings.floorDb);
    read(1, settings.tiltDb);
    read(2, settings.smoothing);
    read(3, settings.holdSeconds);
    read(4, settings.historySeconds);
    const auto* first = parameterAt(parameters, pane, 0);
    auto& view = spectrometerView(first != nullptr ? first->binding : juce::String("spectrometer"));
    if (view.frame != ImGui::GetFrameCount()) {
        view.frame = ImGui::GetFrameCount();
        view.analysis.update(live.afterFrame, ImGui::GetTime(), settings);
    }
    const auto& analysis = view.analysis;
    constexpr float ceilingDb = 6.0f;
    const auto floorDb = std::min(settings.floorDb, ceilingDb - 24.0f);
    const auto fraction = large ? 0.6f : 0.56f;
    const Plot curvePlot { ImVec2(plot.min.x, plot.min.y + ImGui::GetFontSize() * 0.9f),
                           ImVec2(plot.max.x, plot.min.y + plot.height() * fraction) };
    const Plot fallPlot { ImVec2(plot.min.x, curvePlot.max.y + dp(3.0f)), plot.max };

    // The spectrum: a grid in hertz and decibels, the curve, the held peaks.
    frequencyGrid(list, curvePlot);
    for (auto db = 0.0f; db > floorDb; db -= 12.0f) {
        const auto y = yOfDb(curvePlot, db, ceilingDb, floorDb);
        list->AddLine(ImVec2(curvePlot.min.x, y), ImVec2(curvePlot.max.x, y), db == 0.0f ? p.gridStrong : p.grid);
        smallText(list, ImVec2(curvePlot.min.x + dp(2.0f), y - ImGui::GetFontSize() * 0.66f), p.textFaint,
                  juce::String(juce::roundToInt(db)).toRawUTF8(), 0.62f);
    }
    std::vector<ImVec2> levels;
    std::vector<ImVec2> peaks;
    levels.reserve(Spectrometer::binCount);
    peaks.reserve(Spectrometer::binCount);
    auto audible = false;
    for (auto bin = 0; bin < Spectrometer::binCount; ++bin) {
        const auto x = xOfHz(curvePlot, Spectrometer::frequencyOfBin(static_cast<float>(bin)));
        const auto level = analysis.levels()[static_cast<std::size_t>(bin)];
        const auto peak = analysis.peaks()[static_cast<std::size_t>(bin)];
        audible = audible || level > floorDb;
        levels.emplace_back(x, std::min(curvePlot.max.y, yOfDb(curvePlot, level, ceilingDb, floorDb)));
        peaks.emplace_back(x, std::min(curvePlot.max.y, yOfDb(curvePlot, peak, ceilingDb, floorDb)));
    }
    if (audible) {
        curve(list, levels, colour, dp(1.4f), theme::alpha(colour, 0.24f), curvePlot.max.y);
        if (settings.holdSeconds > 0.0f)
            list->AddPolyline(peaks.data(), static_cast<int>(peaks.size()), theme::alpha(theme::brighter(colour, 0.45f), 0.75f),
                              dp(1.0f));
    }

    // The waterfall: the spectrum over the last seconds, the newest on top,
    // drawn at the size of the pixels it has (cells of one colour merged).
    list->AddRectFilled(fallPlot.min, fallPlot.max, heatColour(0.0f));
    const auto rows = std::clamp(static_cast<int>(fallPlot.height() / dp(2.0f)), 1, Spectrometer::rowCount);
    const auto columns = std::clamp(static_cast<int>(fallPlot.width() / dp(2.5f)), 1, Spectrometer::binCount);
    const auto cellWidth = fallPlot.width() / static_cast<float>(columns);
    const auto cellHeight = fallPlot.height() / static_cast<float>(rows);
    const auto range = std::max(1.0f, ceilingDb - floorDb);
    for (auto row = 0; row < rows; ++row) {
        const auto* values = analysis.row(row * Spectrometer::rowCount / rows);
        const auto y = fallPlot.min.y + cellHeight * static_cast<float>(row);
        auto runStart = 0;
        auto runLevel = -1;
        for (auto column = 0; column <= columns; ++column) {
            auto level = -1;
            if (column < columns) {
                const auto from = column * Spectrometer::binCount / columns;
                const auto to = std::max(from + 1, (column + 1) * Spectrometer::binCount / columns);
                auto db = -1'000.0f;
                for (auto bin = from; bin < to; ++bin)
                    db = std::max(db, values[bin]);
                level = std::clamp(static_cast<int>((db - floorDb) / range * 40.0f), 0, 40);
            }
            if (level != runLevel) {
                if (runLevel > 0)
                    list->AddRectFilled(ImVec2(fallPlot.min.x + cellWidth * static_cast<float>(runStart), y),
                                        ImVec2(fallPlot.min.x + cellWidth * static_cast<float>(column), y + cellHeight + 0.5f),
                                        heatColour(static_cast<float>(runLevel) / 40.0f));
                runStart = column;
                runLevel = level;
            }
        }
    }
    const auto seconds = juce::String(juce::roundToInt(settings.historySeconds)) + " s";
    smallText(list, ImVec2(fallPlot.max.x - smallTextSize(seconds.toRawUTF8(), 0.62f).x - dp(3.0f), fallPlot.max.y - ImGui::GetFontSize() * 0.72f),
              theme::alpha(p.text, 0.7f), seconds.toRawUTF8(), 0.62f);

    if (!analysis.active() && !audible)
        ui::centredText(list, curvePlot.min, curvePlot.max, p.textFaint, L("lecture arrêtée", "not playing"));

    // Under the mouse: the frequency, its note and the level there; a double
    // click lets the held peaks go.
    const auto mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
        && (curvePlot.contains(mouse) || fallPlot.contains(mouse))) {
        const auto hz = hzOfX(curvePlot, mouse.x);
        const auto bin = std::clamp(static_cast<int>(std::lround(std::log(hz / 20.0f) / std::log(1'000.0f)
                                                                 * static_cast<float>(Spectrometer::binCount - 1))),
                                    0, Spectrometer::binCount - 1);
        list->AddLine(ImVec2(mouse.x, curvePlot.min.y), ImVec2(mouse.x, fallPlot.max.y), theme::alpha(p.text, 0.35f));
        const auto text = hertzText(hz) + "  " + noteName(hz) + "  "
                          + juce::String(analysis.levels()[static_cast<std::size_t>(bin)], 1) + " dB";
        const auto size = smallTextSize(text.toRawUTF8(), 0.7f);
        auto x = mouse.x + dp(6.0f);
        if (x + size.x > curvePlot.max.x - dp(2.0f))
            x = mouse.x - dp(6.0f) - size.x;
        const ImVec2 at(x, curvePlot.min.y + dp(2.0f));
        list->AddRectFilled(ImVec2(at.x - dp(3.0f), at.y - dp(1.0f)), ImVec2(at.x + size.x + dp(3.0f), at.y + size.y + dp(1.0f)),
                            theme::alpha(p.bg0, 0.85f), dp(3.0f));
        smallText(list, at, p.text, text.toRawUTF8(), 0.7f);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            view.analysis.clearPeaks();
    }
    return false;
}

bool spectrumPlot(const Plot& plot, ImU32 colour, const Live& live)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    frequencyGrid(list, plot);
    if (live.before != nullptr)
        drawSpectrum(list, plot, *live.before, theme::alpha(p.textDim, 0.55f), theme::alpha(p.textDim, 0.16f));
    if (live.after != nullptr)
        drawSpectrum(list, plot, *live.after, colour, theme::alpha(colour, 0.22f));
    const auto quiet = (live.before == nullptr || !live.before->visible()) && (live.after == nullptr || !live.after->visible());
    if (quiet)
        ui::centredText(list, plot.min, plot.max, p.textFaint, L("lecture arrêtée", "not playing"));
    else if (live.before != nullptr) {
        // Which line is which.
        const auto y = plot.min.y + dp(4.0f);
        auto x = plot.max.x - smallTextSize(L("après", "after")).x - dp(6.0f);
        smallText(list, ImVec2(x, y), colour, L("après", "after"));
        x -= smallTextSize(L("avant", "before")).x + dp(8.0f);
        smallText(list, ImVec2(x, y), p.textDim, L("avant", "before"));
    }
    return false;
}

bool filterPlot(const Plot& plot, Param* cutoff, Param* resonance, const bool highpass, const ImU32 colour,
                const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    constexpr float top = 24.0f;
    constexpr float bottom = -36.0f;
    frequencyGrid(list, plot);
    list->AddLine(ImVec2(plot.min.x, yOfDb(plot, 0.0f, top, bottom)), ImVec2(plot.max.x, yOfDb(plot, 0.0f, top, bottom)),
                  p.gridStrong);
    if (live.before != nullptr)
        drawSpectrum(list, plot, *live.before, theme::alpha(p.textDim, 0.45f), theme::alpha(p.textDim, 0.13f));
    if (live.after != nullptr)
        drawSpectrum(list, plot, *live.after, theme::alpha(colour, 0.65f), theme::alpha(colour, 0.16f));
    if (cutoff == nullptr)
        return false;

    const auto interaction = interact("##filter", plot);
    auto changed = false;
    if (interaction.active && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        if (interaction.fine) {
            changed |= apply(*cutoff, cutoff->value * std::pow(2.0, interaction.delta.x / dp(400.0f)), set);
        } else {
            changed |= apply(*cutoff, hzOfX(plot, interaction.mouse.x), set);
        }
        if (resonance != nullptr && !highpass) {
            const auto db = dbOfY(plot, interaction.mouse.y, top, bottom);
            changed |= apply(*resonance, std::pow(10.0, std::max(-6.0f, db) / 20.0), set);
        }
    }
    if (interaction.wheel != 0.0f && resonance != nullptr)
        changed |= apply(*resonance, resonance->value * std::pow(1.12, interaction.wheel), set);
    if (interaction.doubleClicked) {
        changed |= apply(*cutoff, cutoff->defaultValue, set);
        if (resonance != nullptr)
            changed |= apply(*resonance, resonance->defaultValue, set);
    }

    const auto fc = static_cast<float>(cutoff->value);
    const auto q = resonance != nullptr ? static_cast<float>(resonance->value) : 0.707f;
    const auto steps = std::max(48, static_cast<int>(plot.width() / dp(2.0f)));
    const auto response = [&](const float frequency, const float quality) {
        std::vector<ImVec2> points;
        for (auto step = 0; step <= steps; ++step) {
            const auto x = plot.min.x + plot.width() * static_cast<float>(step) / static_cast<float>(steps);
            const auto hz = hzOfX(plot, x);
            const auto db = highpass ? highpassDb(hz, frequency) : lowpassDb(hz, frequency, quality);
            points.emplace_back(x, yOfDb(plot, db, top, bottom));
        }
        return points;
    };
    curve(list, response(fc, q), colour, dp(2.0f), theme::alpha(colour, 0.10f), plot.max.y);
    // Where a cable moves it now, over where it is set.
    if (live.modulated != nullptr) {
        const auto modulatedCutoff = live.modulated->find(cutoff->binding);
        const auto modulatedQ = resonance != nullptr ? live.modulated->find(resonance->binding) : live.modulated->end();
        if (modulatedCutoff != live.modulated->end() || modulatedQ != live.modulated->end()) {
            const auto mf = modulatedCutoff != live.modulated->end() ? std::clamp(modulatedCutoff->second, 20.0f, 20'000.0f) : fc;
            const auto mq = modulatedQ != live.modulated->end() ? std::clamp(modulatedQ->second, 0.1f, 20.0f) : q;
            const auto points = response(mf, mq);
            list->AddPolyline(points.data(), static_cast<int>(points.size()), theme::alpha(p.modulation, 0.9f), dp(1.6f));
            list->AddCircleFilled(ImVec2(xOfHz(plot, mf), yOfDb(plot, highpass ? highpassDb(mf, mf) : lowpassDb(mf, mf, mq), top, bottom)),
                                  dp(3.5f), p.modulation, 12);
        }
    }
    const ImVec2 point(xOfHz(plot, fc), yOfDb(plot, highpass ? highpassDb(fc, fc) : lowpassDb(fc, fc, q), top, bottom));
    handle(list, point, colour, interaction.hovered || interaction.active);
    if (interaction.hovered || interaction.active)
        ImGui::SetTooltip("%s%s", formatValue(*cutoff, cutoff->value).toRawUTF8(),
                          resonance != nullptr ? (juce::String::fromUTF8(" · Q ") + juce::String(resonance->value, 2)).toRawUTF8() : "");
    return changed;
}

bool equalizerPlot(const char* id, const Plot& plot, std::vector<Param>& parameters, const Pane& pane, const ImU32 colour,
                   const Live& live, const Setter& set, int& selectedBand)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    constexpr float top = 18.0f;
    constexpr float bottom = -18.0f;
    frequencyGrid(list, plot);
    for (const auto db : { -12.0f, -6.0f, 6.0f, 12.0f })
        list->AddLine(ImVec2(plot.min.x, yOfDb(plot, db, top, bottom)), ImVec2(plot.max.x, yOfDb(plot, db, top, bottom)), p.grid);
    list->AddLine(ImVec2(plot.min.x, yOfDb(plot, 0.0f, top, bottom)), ImVec2(plot.max.x, yOfDb(plot, 0.0f, top, bottom)),
                  p.gridStrong);
    if (live.before != nullptr)
        drawSpectrum(list, plot, *live.before, theme::alpha(p.textDim, 0.45f), theme::alpha(p.textDim, 0.13f));
    if (live.after != nullptr)
        drawSpectrum(list, plot, *live.after, theme::alpha(colour, 0.65f), theme::alpha(colour, 0.16f));

    auto* tone = parameterAt(parameters, pane, 0);
    struct Band {
        Param* frequency;
        Param* gain;
        Param* q;
    };
    std::vector<Band> bands;
    for (std::size_t slot = 1; slot + 2 < pane.parameters.size(); slot += 3) {
        Band band { parameterAt(parameters, pane, slot), parameterAt(parameters, pane, slot + 1),
                    parameterAt(parameters, pane, slot + 2) };
        if (band.frequency != nullptr && band.gain != nullptr && band.q != nullptr)
            bands.push_back(band);
    }
    const auto responseAt = [&](const float hz) {
        auto db = tone != nullptr ? toneDb(hz, static_cast<float>(tone->value)) : 0.0f;
        for (const auto& band : bands)
            db += peakingDb(hz, static_cast<float>(band.frequency->value), static_cast<float>(band.gain->value),
                            static_cast<float>(band.q->value));
        return db;
    };

    const auto interaction = interact(id, plot);
    auto changed = false;
    // The band nearest the pointer when the drag starts.
    if (interaction.activated && !bands.empty()) {
        auto best = FLT_MAX;
        for (std::size_t index = 0; index < bands.size(); ++index) {
            const auto dx = xOfHz(plot, static_cast<float>(bands[index].frequency->value)) - interaction.mouse.x;
            if (std::abs(dx) < best) {
                best = std::abs(dx);
                selectedBand = static_cast<int>(index);
            }
        }
    }
    selectedBand = std::clamp(selectedBand, 0, std::max(0, static_cast<int>(bands.size()) - 1));
    if (interaction.active && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        if (!bands.empty()) {
            auto& band = bands[static_cast<std::size_t>(selectedBand)];
            changed |= apply(*band.frequency, hzOfX(plot, interaction.mouse.x), set);
            changed |= apply(*band.gain, dbOfY(plot, interaction.mouse.y, top, bottom), set);
        } else if (tone != nullptr) {
            changed |= apply(*tone, tone->value - interaction.delta.y / std::max(1.0f, plot.height()) * 2.0, set);
        }
    }
    if (interaction.wheel != 0.0f && !bands.empty()) {
        auto& band = bands[static_cast<std::size_t>(selectedBand)];
        changed |= apply(*band.q, band.q->value * std::pow(1.15, interaction.wheel), set);
    }

    std::vector<ImVec2> points;
    const auto steps = std::max(48, static_cast<int>(plot.width() / dp(2.0f)));
    for (auto step = 0; step <= steps; ++step) {
        const auto x = plot.min.x + plot.width() * static_cast<float>(step) / static_cast<float>(steps);
        points.emplace_back(x, yOfDb(plot, responseAt(hzOfX(plot, x)), top, bottom));
    }
    curve(list, points, colour, dp(2.0f), 0, plot.max.y);
    for (std::size_t index = 0; index < bands.size(); ++index) {
        const auto hz = static_cast<float>(bands[index].frequency->value);
        const ImVec2 point(xOfHz(plot, hz), yOfDb(plot, static_cast<float>(bands[index].gain->value), top, bottom));
        handle(list, point, colour, static_cast<int>(index) == selectedBand && (interaction.hovered || interaction.active));
        char label[8];
        std::snprintf(label, sizeof(label), "%d", static_cast<int>(index) + 1);
        smallText(list, ImVec2(point.x - dp(3.0f), point.y - dp(17.0f)), p.text, label, 0.66f);
    }
    return changed;
}

bool dynamicsPlot(const Plot& plot, Param* threshold, Param* ratio, const ImU32 colour, const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (threshold == nullptr || ratio == nullptr)
        return false;
    // The transfer curve in a square on the left, what happens on the right.
    const auto side = std::min(plot.height(), plot.width() * 0.55f);
    const Plot transfer { plot.min, ImVec2(plot.min.x + side, plot.min.y + side) };
    const Plot history { ImVec2(transfer.max.x + dp(6.0f), plot.min.y), plot.max };
    constexpr float floorDb = -60.0f;
    const auto xOf = [&](const float db) { return transfer.min.x + (db - floorDb) / -floorDb * transfer.width(); };
    const auto yOf = [&](const float db) { return transfer.max.y - (db - floorDb) / -floorDb * transfer.height(); };
    for (const auto db : { -48.0f, -36.0f, -24.0f, -12.0f }) {
        list->AddLine(ImVec2(xOf(db), transfer.min.y), ImVec2(xOf(db), transfer.max.y), p.grid);
        list->AddLine(ImVec2(transfer.min.x, yOf(db)), ImVec2(transfer.max.x, yOf(db)), p.grid);
    }
    list->AddLine(ImVec2(xOf(floorDb), yOf(floorDb)), ImVec2(xOf(0.0f), yOf(0.0f)), p.gridStrong);

    const auto interaction = interact("##dynamics", transfer);
    auto changed = false;
    if (interaction.active && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        const auto db = floorDb + (interaction.mouse.x - transfer.min.x) / std::max(1.0f, transfer.width()) * -floorDb;
        changed |= apply(*threshold, db, set);
        if (interaction.delta.y != 0.0f)
            changed |= apply(*ratio, ratio->value * std::pow(1.012, -interaction.delta.y / ImGui::GetStyle().FontScaleDpi), set);
    }
    if (interaction.wheel != 0.0f)
        changed |= apply(*ratio, ratio->value * std::pow(1.1, interaction.wheel), set);
    if (interaction.doubleClicked) {
        changed |= apply(*threshold, threshold->defaultValue, set);
        changed |= apply(*ratio, ratio->defaultValue, set);
    }

    const auto t = static_cast<float>(threshold->value);
    const auto r = std::max(1.0f, static_cast<float>(ratio->value));
    const auto outputOf = [&](const float in) { return in <= t ? in : t + (in - t) / r; };
    std::vector<ImVec2> points;
    for (auto step = 0; step <= 60; ++step) {
        const auto in = floorDb + static_cast<float>(step);
        points.emplace_back(xOf(in), yOf(outputOf(in)));
    }
    curve(list, points, colour, dp(2.0f), theme::alpha(colour, 0.10f), transfer.max.y);
    // The threshold, across.
    for (auto x = transfer.min.x; x < transfer.max.x; x += dp(6.0f))
        list->AddLine(ImVec2(x, yOf(t)), ImVec2(std::min(transfer.max.x, x + dp(3.0f)), yOf(t)), theme::alpha(colour, 0.6f));
    handle(list, ImVec2(xOf(t), yOf(t)), colour, interaction.hovered || interaction.active);
    if (live.running && live.inputDb > floorDb) {
        const auto in = std::min(0.0f, live.inputDb);
        list->AddCircleFilled(ImVec2(xOf(in), yOf(outputOf(in))), dp(3.5f), p.text, 12);
    }
    if (interaction.hovered || interaction.active)
        ImGui::SetTooltip("%s · %s", formatValue(*threshold, threshold->value).toRawUTF8(),
                          formatValue(*ratio, ratio->value).toRawUTF8());

    // Input level over time, and the gain taken off it.
    if (history.width() > dp(40.0f)) {
        list->AddRectFilled(history.min, history.max, theme::alpha(p.bg0, 0.6f), dp(3.0f));
        const auto yLevel = [&](const float db) {
            return history.max.y - std::clamp((db - floorDb) / -floorDb, 0.0f, 1.0f) * history.height();
        };
        list->AddLine(ImVec2(history.min.x, yLevel(t)), ImVec2(history.max.x, yLevel(t)), theme::alpha(colour, 0.45f));
        if (live.inputHistory != nullptr && live.historyCount > 1) {
            const auto step = history.width() / static_cast<float>(live.historyCount - 1);
            std::vector<ImVec2> levels;
            std::vector<ImVec2> reductions;
            for (auto index = 0; index < live.historyCount; ++index) {
                const auto x = history.min.x + step * static_cast<float>(index);
                levels.emplace_back(x, yLevel(live.inputHistory[index]));
                const auto reduction = live.reductionHistory != nullptr ? live.reductionHistory[index] : 0.0f;
                reductions.emplace_back(x, history.min.y + std::clamp(-reduction / 24.0f, 0.0f, 1.0f) * history.height());
            }
            curve(list, levels, theme::alpha(p.textDim, 0.7f), dp(1.0f), theme::alpha(p.textDim, 0.18f), history.max.y);
            curve(list, reductions, p.record, dp(1.5f), theme::alpha(p.record, 0.18f), history.min.y);
        }
        const auto reduction = live.running ? std::min(0.0f, live.outputDb - live.inputDb) : 0.0f;
        char text[32];
        std::snprintf(text, sizeof(text), "GR %.1f dB", static_cast<double>(reduction));
        const auto size = smallTextSize(text);
        smallText(list, ImVec2(history.max.x - size.x - dp(4.0f), history.min.y + dp(3.0f)),
                  reduction < -0.5f ? p.record : p.textFaint, text);
    }
    return changed;
}

// The last samples as a waveform: the highest and lowest value under each
// column, filled.
void scope(ImDrawList* list, const Plot& plot, const engine::AudioAnalysisFrame* frame, const ImU32 colour,
           const float scale, const std::uint32_t samples)
{
    if (frame == nullptr || frame->waveformCount < 16)
        return;
    const auto count = std::min(samples, frame->waveformCount);
    const auto start = frame->waveformCount - count;
    const auto columns = std::max(16, static_cast<int>(plot.width() / dp(1.0f)));
    const auto centre = (plot.min.y + plot.max.y) * 0.5f;
    const auto half = plot.height() * 0.46f;
    for (auto column = 0; column < columns; ++column) {
        const auto first = start + static_cast<std::uint32_t>(static_cast<float>(column) / static_cast<float>(columns) * count);
        const auto last = start + static_cast<std::uint32_t>(static_cast<float>(column + 1) / static_cast<float>(columns) * count);
        auto low = 1.0f;
        auto high = -1.0f;
        for (auto index = first; index < std::max(first + 1, last) && index < frame->waveformCount; ++index) {
            const auto value = std::clamp(frame->waveform[index] * scale, -1.0f, 1.0f);
            low = std::min(low, value);
            high = std::max(high, value);
        }
        const auto x = plot.min.x + plot.width() * static_cast<float>(column) / static_cast<float>(columns);
        list->AddLine(ImVec2(x, centre - high * half), ImVec2(x, centre - low * half + 1.0f), colour, dp(1.0f));
    }
}

// The oscilloscope: the sound's last samples across, its zero line, its
// full scale.
bool scopePlot(const Plot& plot, const ImU32 colour, const Live& live)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto centre = (plot.min.y + plot.max.y) * 0.5f;
    const auto half = plot.height() * 0.46f;
    for (const auto level : { -1.0f, -0.5f, 0.5f, 1.0f })
        list->AddLine(ImVec2(plot.min.x, centre - level * half), ImVec2(plot.max.x, centre - level * half), p.grid);
    list->AddLine(ImVec2(plot.min.x, centre), ImVec2(plot.max.x, centre), theme::alpha(p.line, 0.9f));
    const auto* frame = live.afterFrame != nullptr ? live.afterFrame : live.beforeFrame;
    if (!live.running || frame == nullptr || frame->waveformCount < 16) {
        ui::centredText(list, plot.min, plot.max, p.textFaint, L("lecture arrêtée", "not playing"));
        return false;
    }
    scope(list, plot, frame, colour, 1.0f, 1'024);
    return false;
}

bool saturationPlot(const Plot& plot, Param* amount, const bool clip, const ImU32 colour, const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (amount == nullptr)
        return false;
    const auto side = std::min(plot.height(), plot.width() * 0.5f);
    const Plot transfer { plot.min, ImVec2(plot.min.x + side, plot.min.y + side) };
    const Plot wave { ImVec2(transfer.max.x + dp(6.0f), plot.min.y), plot.max };
    const ImVec2 centre((transfer.min.x + transfer.max.x) * 0.5f, (transfer.min.y + transfer.max.y) * 0.5f);
    list->AddLine(ImVec2(transfer.min.x, centre.y), ImVec2(transfer.max.x, centre.y), p.grid);
    list->AddLine(ImVec2(centre.x, transfer.min.y), ImVec2(centre.x, transfer.max.y), p.grid);
    list->AddLine(ImVec2(transfer.min.x, transfer.max.y), ImVec2(transfer.max.x, transfer.min.y), p.gridStrong);

    const auto interaction = interact("##saturation", plot);
    auto changed = false;
    if (interaction.active && interaction.delta.y != 0.0f) {
        const auto t = normalised(*amount, amount->value)
                       + (clip ? 1.0 : -1.0) * interaction.delta.y / std::max(1.0f, plot.height()) * interaction.speed;
        changed |= apply(*amount, fromNormalised(*amount, t), set);
    }
    if (interaction.wheel != 0.0f)
        changed |= apply(*amount, fromNormalised(*amount, normalised(*amount, amount->value) + interaction.wheel * 0.03), set);
    if (interaction.doubleClicked)
        changed |= apply(*amount, amount->defaultValue, set);

    const auto value = static_cast<float>(amount->value);
    const auto gain = std::pow(10.0f, value / 20.0f);
    const auto shape = [&](const float x) {
        if (clip)
            return std::clamp(x, -value, value);
        return value > 0.001f ? std::tanh(x * gain) / std::tanh(gain) : x;
    };
    std::vector<ImVec2> points;
    for (auto step = 0; step <= 64; ++step) {
        const auto x = -1.0f + 2.0f * static_cast<float>(step) / 64.0f;
        points.emplace_back(centre.x + x * transfer.width() * 0.5f, centre.y - shape(x) * transfer.height() * 0.5f);
    }
    list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(2.0f));
    if (interaction.hovered || interaction.active)
        ImGui::SetTooltip("%s", formatValue(*amount, amount->value).toRawUTF8());

    // Before and after, a few milliseconds of it.
    if (wave.width() > dp(40.0f)) {
        list->AddRectFilled(wave.min, wave.max, theme::alpha(p.bg0, 0.6f), dp(3.0f));
        const auto middle = (wave.min.y + wave.max.y) * 0.5f;
        list->AddLine(ImVec2(wave.min.x, middle), ImVec2(wave.max.x, middle), p.grid);
        auto peak = 0.05f;
        for (const auto* frame : { live.beforeFrame, live.afterFrame })
            if (frame != nullptr)
                for (std::uint32_t index = 0; index < frame->waveformCount; ++index)
                    peak = std::max(peak, std::abs(frame->waveform[index]));
        if (live.running) {
            // What leaves in front of what came in.
            scope(list, wave, live.beforeFrame, theme::alpha(p.textDim, 0.55f), 1.0f / peak, 2'048);
            scope(list, wave, live.afterFrame, theme::alpha(colour, 0.85f), 1.0f / peak, 2'048);
            smallText(list, ImVec2(wave.min.x + dp(4.0f), wave.max.y - ImGui::GetFontSize() * 0.8f), p.textDim,
                      L("avant", "before"), 0.66f);
            smallText(list, ImVec2(wave.min.x + dp(40.0f), wave.max.y - ImGui::GetFontSize() * 0.8f), colour,
                      L("après", "after"), 0.66f);
        } else {
            ui::centredText(list, wave.min, wave.max, p.textFaint, L("à l'arrêt", "stopped"));
        }
    }
    return changed;
}

bool envelopePlot(const Plot& plot, Param* attack, Param* decay, Param* sustain, Param* release, const ImU32 colour,
                  const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (attack == nullptr || decay == nullptr || sustain == nullptr || release == nullptr)
        return false;
    const auto inner = Plot { ImVec2(plot.min.x + dp(8.0f), plot.min.y + dp(20.0f)), ImVec2(plot.max.x - dp(8.0f), plot.max.y - dp(6.0f)) };
    const auto section = inner.width() / 4.0f;
    // A segment's length grows with the log of its time, so 2 ms and 2 s
    // both read.
    const auto lengthOf = [&](const Param& parameter) {
        const auto top = std::max(parameter.maximum, 1.0);
        return section * static_cast<float>(std::log10(1.0 + std::max(0.0, parameter.value)) / std::log10(1.0 + top));
    };
    const auto level = static_cast<float>(std::clamp(sustain->value, 0.0, 1.0));
    const ImVec2 start(inner.min.x, inner.max.y);
    const ImVec2 peak(start.x + std::max(dp(2.0f), lengthOf(*attack)), inner.min.y);
    const ImVec2 hold(peak.x + std::max(dp(2.0f), lengthOf(*decay)), inner.max.y - level * inner.height());
    const ImVec2 releaseStart(hold.x + section, hold.y);
    const ImVec2 end(releaseStart.x + std::max(dp(2.0f), lengthOf(*release)), inner.max.y);

    std::vector<ImVec2> points { start, peak };
    for (auto step = 1; step <= 16; ++step) {
        const auto t = static_cast<float>(step) / 16.0f;
        const auto shaped = 1.0f - std::exp(-4.0f * t);
        points.emplace_back(peak.x + (hold.x - peak.x) * t, peak.y + (hold.y - peak.y) * shaped / (1.0f - std::exp(-4.0f)));
    }
    points.push_back(releaseStart);
    for (auto step = 1; step <= 16; ++step) {
        const auto t = static_cast<float>(step) / 16.0f;
        const auto shaped = (1.0f - std::exp(-4.0f * t)) / (1.0f - std::exp(-4.0f));
        points.emplace_back(releaseStart.x + (end.x - releaseStart.x) * t, releaseStart.y + (end.y - releaseStart.y) * shaped);
    }
    list->AddLine(ImVec2(hold.x, inner.min.y), ImVec2(hold.x, inner.max.y), p.grid);
    list->AddLine(ImVec2(releaseStart.x, inner.min.y), ImVec2(releaseStart.x, inner.max.y), p.grid);
    curve(list, points, colour, dp(2.0f), theme::alpha(colour, 0.16f), inner.max.y);

    // Three corners to drag.
    auto* storage = ImGui::GetStateStorage();
    const auto grabbedId = ImGui::GetID("##envelopeGrab");
    const auto interaction = interact("##envelope", plot);
    const ImVec2 corners[] = { peak, hold, end };
    auto nearest = -1;
    auto best = dp(16.0f) * dp(16.0f);
    for (auto index = 0; index < 3; ++index) {
        const auto dx = corners[index].x - interaction.mouse.x;
        const auto dy = corners[index].y - interaction.mouse.y;
        if (dx * dx + dy * dy < best) {
            best = dx * dx + dy * dy;
            nearest = index;
        }
    }
    if (interaction.activated)
        storage->SetInt(grabbedId, nearest);
    const auto grabbed = interaction.active ? storage->GetInt(grabbedId, -1) : -1;
    auto changed = false;
    const auto timeAt = [&](const Param& parameter, const float length) {
        const auto top = std::max(parameter.maximum, 1.0);
        return std::pow(10.0, std::clamp(length / section, 0.0f, 1.0f) * std::log10(1.0 + top)) - 1.0;
    };
    if (grabbed >= 0 && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        if (grabbed == 0)
            changed |= apply(*attack, timeAt(*attack, interaction.mouse.x - start.x), set);
        else if (grabbed == 1) {
            changed |= apply(*decay, timeAt(*decay, interaction.mouse.x - peak.x), set);
            changed |= apply(*sustain, (inner.max.y - interaction.mouse.y) / std::max(1.0f, inner.height()), set);
        } else {
            changed |= apply(*release, timeAt(*release, interaction.mouse.x - releaseStart.x), set);
        }
    }
    for (auto index = 0; index < 3; ++index)
        handle(list, corners[index], colour, index == (grabbed >= 0 ? grabbed : (interaction.hovered ? nearest : -1)));
    if (interaction.hovered || interaction.active) {
        const auto shown = grabbed >= 0 ? grabbed : nearest;
        if (shown == 0)
            ImGui::SetTooltip("%s %s", L("Attaque", "Attack"), formatValue(*attack, attack->value).toRawUTF8());
        else if (shown == 1)
            ImGui::SetTooltip("%s %s · %s %s", L("Déclin", "Decay"), formatValue(*decay, decay->value).toRawUTF8(),
                              L("Maintien", "Sustain"), formatValue(*sustain, sustain->value).toRawUTF8());
        else if (shown == 2)
            ImGui::SetTooltip("%s %s", L("Relâchement", "Release"), formatValue(*release, release->value).toRawUTF8());
    }
    return changed;
}

float waveformValue(const int shape, const float phase)
{
    const auto angle = phase * 2.0f * std::numbers::pi_v<float>;
    switch (shape) {
    case 1: return 2.0f / std::numbers::pi_v<float> * std::asin(std::sin(angle));
    case 2: return phase * 2.0f - 1.0f;
    case 3: return phase < 0.5f ? 1.0f : -1.0f;
    default: return std::sin(angle);
    }
}

bool oscillatorPlot(const Plot& plot, Param* waveform, Param* level, const ImU32 colour, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (waveform == nullptr)
        return false;
    const auto interaction = interact("##oscillator", plot);
    auto changed = false;
    const auto shapes = static_cast<int>(std::round(waveform->maximum - waveform->minimum)) + 1;
    if (interaction.hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        && std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).x) < 3.0f) {
        const auto next = (static_cast<int>(std::round(waveform->value - waveform->minimum)) + 1) % std::max(1, shapes);
        changed |= apply(*waveform, waveform->minimum + next, set);
    }
    if (interaction.wheel != 0.0f)
        changed |= apply(*waveform, std::round(waveform->value + (interaction.wheel > 0.0f ? 1.0 : -1.0)), set);
    const auto shape = static_cast<int>(std::round(waveform->value));
    const auto amplitude = level != nullptr ? std::clamp(static_cast<float>(normalised(*level, level->value)), 0.15f, 1.0f) : 0.85f;
    const auto middle = (plot.min.y + plot.max.y) * 0.5f + dp(5.0f);
    const auto height = (plot.height() - dp(18.0f)) * 0.5f;
    list->AddLine(ImVec2(plot.min.x, middle), ImVec2(plot.max.x, middle), p.grid);
    std::vector<ImVec2> points;
    const auto columns = std::max(64, static_cast<int>(plot.width() / dp(1.0f)));
    for (auto column = 0; column <= columns; ++column) {
        const auto t = static_cast<float>(column) / static_cast<float>(columns);
        const auto phase = std::fmod(t * 2.0f, 1.0f);
        points.emplace_back(plot.min.x + plot.width() * t, middle - waveformValue(shape, phase) * height * amplitude);
    }
    curve(list, points, colour, dp(2.0f), theme::alpha(colour, 0.12f), middle);
    static const char* namesFr[] { "Sinus", "Triangle", "Scie", "Carré" };
    static const char* namesEn[] { "Sine", "Triangle", "Saw", "Square" };
    const auto name = ui::french() ? namesFr[std::clamp(shape, 0, 3)] : namesEn[std::clamp(shape, 0, 3)];
    const auto size = smallTextSize(name, 0.8f);
    smallText(list, ImVec2(plot.max.x - size.x - dp(5.0f), plot.min.y + dp(3.0f)), p.text, name, 0.8f);
    if (interaction.hovered)
        ImGui::SetTooltip("%s", L("Clic : forme suivante", "Click: next shape"));
    return changed;
}

// The built-in bank, drawn once.
const std::array<std::array<float, 128>, engine::WavetableShape::frameCount>& wavetableFrames()
{
    static const auto frames = [] {
        std::array<std::array<float, 128>, engine::WavetableShape::frameCount> result {};
        const auto shape = engine::defaultWavetableShape();
        for (std::size_t frame = 0; frame < result.size(); ++frame)
            engine::renderWavetableFrame(shape.harmonics[frame], result[frame].data(), result[frame].size());
        return result;
    }();
    return frames;
}

bool wavetablePlot(const Plot& plot, Param* position, Param* level, const ImU32 colour, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (position == nullptr)
        return false;
    const auto interaction = interact("##wavetable", plot);
    auto changed = false;
    if (interaction.active && interaction.delta.x != 0.0f)
        changed |= apply(*position, position->value + interaction.delta.x / std::max(1.0f, plot.width() * 0.7f)
                                                           * (position->maximum - position->minimum),
                         set);
    if (interaction.wheel != 0.0f)
        changed |= apply(*position, position->value + interaction.wheel * 0.02 * (position->maximum - position->minimum), set);
    const auto& frames = wavetableFrames();
    const auto count = static_cast<int>(frames.size());
    const auto t = static_cast<float>(normalised(*position, position->value));
    const auto amplitude = level != nullptr ? std::clamp(static_cast<float>(normalised(*level, level->value)), 0.2f, 1.0f) : 0.9f;
    // The frames stacked in depth, the one playing in front.
    const auto depthX = plot.width() * 0.22f;
    const auto depthY = plot.height() * 0.30f;
    const auto width = plot.width() - depthX - dp(8.0f);
    const auto height = (plot.height() - depthY - dp(16.0f)) * 0.5f;
    const auto drawFrame = [&](const float index, const ImU32 frameColour, const float thickness) {
        const auto lower = std::clamp(static_cast<int>(index), 0, count - 1);
        const auto upper = std::min(lower + 1, count - 1);
        const auto fraction = index - static_cast<float>(lower);
        const auto depth = 1.0f - index / static_cast<float>(count - 1);
        const ImVec2 origin(plot.min.x + dp(4.0f) + depthX * depth, plot.min.y + dp(14.0f) + height + depthY * (1.0f - depth));
        std::vector<ImVec2> points;
        for (std::size_t sample = 0; sample < 128; ++sample) {
            const auto value = frames[static_cast<std::size_t>(lower)][sample] * (1.0f - fraction)
                               + frames[static_cast<std::size_t>(upper)][sample] * fraction;
            points.emplace_back(origin.x + width * static_cast<float>(sample) / 127.0f, origin.y - value * height * amplitude);
        }
        list->AddPolyline(points.data(), static_cast<int>(points.size()), frameColour, thickness);
    };
    for (auto frame = count - 1; frame >= 0; --frame)
        drawFrame(static_cast<float>(frame), theme::alpha(p.textDim, 0.25f), dp(1.0f));
    drawFrame(t * static_cast<float>(count - 1), colour, dp(2.0f));
    if (interaction.hovered || interaction.active)
        ImGui::SetTooltip("%s %s", L("Position", "Position"), formatValue(*position, position->value).toRawUTF8());
    return changed;
}

juce::String noteValue(const double milliseconds, const double tempo)
{
    const auto beat = 60'000.0 / std::max(20.0, tempo);
    struct Division {
        double beats;
        const char* name;
    };
    static constexpr Division divisions[] = {
        { 4.0, "1/1" },   { 2.0, "1/2" },   { 1.0, "1/4" },    { 0.5, "1/8" },    { 0.25, "1/16" },  { 0.125, "1/32" },
        { 3.0, "1/2." },  { 1.5, "1/4." },  { 0.75, "1/8." },  { 0.375, "1/16." }, { 4.0 / 3.0, "1/2T" },
        { 2.0 / 3.0, "1/4T" }, { 1.0 / 3.0, "1/8T" }, { 1.0 / 6.0, "1/16T" },
    };
    for (const auto& division : divisions)
        if (std::abs(milliseconds - division.beats * beat) <= division.beats * beat * 0.03)
            return division.name;
    return {};
}

bool delayPlot(const Plot& plot, Param* time, Param* feedback, Param* mix, const ImU32 colour, const Live& live,
               const Setter& set, const bool pingPong = false, Param* sync = nullptr)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (time == nullptr)
        return false;
    // Synced, the echo's time is a note value's length at the tempo.
    const auto* division = sync != nullptr ? engine::noteDivisionFor(static_cast<float>(sync->value)) : nullptr;
    const auto effectiveTime = division != nullptr ? std::min(2'000.0, division->beats * 60'000.0 / std::max(20.0, live.tempo))
                                                   : time->value;
    const auto inner = Plot { ImVec2(plot.min.x + dp(6.0f), plot.min.y + dp(16.0f)), ImVec2(plot.max.x - dp(6.0f), plot.max.y - dp(4.0f)) };
    const auto span = std::max(400.0, std::min(4'000.0, effectiveTime * 7.0));
    const auto xOf = [&](const double ms) { return inner.min.x + static_cast<float>(ms / span) * inner.width(); };
    const auto interaction = interact("##delay", plot);
    auto changed = false;
    auto* storage = ImGui::GetStateStorage();
    const auto stepId = ImGui::GetID("##syncStep");
    if (interaction.activated)
        storage->SetFloat(stepId, 0.0f);
    if (interaction.active && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        if (interaction.delta.x != 0.0f && division != nullptr) {
            // One note value every few points of travel.
            auto travel = storage->GetFloat(stepId, 0.0f) + interaction.delta.x;
            const auto step = dp(18.0f);
            while (std::abs(travel) >= step) {
                const auto direction = travel > 0.0f ? 1.0 : -1.0;
                changed |= apply(*sync, std::clamp(std::round(sync->value) + direction, 1.0, sync->maximum), set);
                travel -= static_cast<float>(direction) * step;
            }
            storage->SetFloat(stepId, travel);
        } else if (interaction.delta.x != 0.0f)
            changed |= apply(*time, time->value * std::pow(2.0, interaction.delta.x * interaction.speed / dp(160.0f)), set);
        if (feedback != nullptr && interaction.delta.y != 0.0f)
            changed |= apply(*feedback, feedback->value - interaction.delta.y / std::max(1.0f, inner.height()), set);
    }
    if (interaction.doubleClicked) {
        changed |= apply(*time, time->defaultValue, set);
        if (feedback != nullptr)
            changed |= apply(*feedback, feedback->defaultValue, set);
    }
    // The beat grid behind.
    const auto beat = 60'000.0 / std::max(20.0, live.tempo);
    for (auto ms = beat / 4.0; ms < span; ms += beat / 4.0)
        list->AddLine(ImVec2(xOf(ms), inner.min.y), ImVec2(xOf(ms), inner.max.y),
                      std::fmod(ms + 1.0e-6, beat) < 1.0e-3 ? p.gridStrong : p.grid);
    const auto wet = mix != nullptr ? static_cast<float>(mix->value) : 0.5f;
    const auto fb = feedback != nullptr ? static_cast<float>(feedback->value) : 0.3f;
    if (pingPong) {
        // Left above the line, right below, one after the other.
        const auto centre = (inner.min.y + inner.max.y) * 0.5f;
        list->AddLine(ImVec2(inner.min.x, centre), ImVec2(inner.max.x, centre), p.gridStrong);
        list->AddLine(ImVec2(xOf(0.0), centre - (1.0f - wet * 0.5f) * inner.height() * 0.48f),
                      ImVec2(xOf(0.0), centre + (1.0f - wet * 0.5f) * inner.height() * 0.48f), p.textDim, dp(2.0f));
        auto amplitude = wet;
        for (auto echo = 1; echo < 64 && echo * effectiveTime < span && amplitude > 0.01f; ++echo) {
            const auto x = xOf(echo * effectiveTime);
            const auto direction = echo % 2 == 1 ? -1.0f : 1.0f;
            const auto end = centre + direction * amplitude * inner.height() * 0.48f;
            list->AddLine(ImVec2(x, centre), ImVec2(x, end), colour, dp(2.0f));
            list->AddCircleFilled(ImVec2(x, end), dp(2.5f), colour, 10);
            amplitude *= fb;
        }
        smallText(list, ImVec2(inner.min.x + dp(4.0f), inner.min.y), p.textDim, L("G", "L"), 0.62f);
        smallText(list, ImVec2(inner.min.x + dp(4.0f), inner.max.y - ImGui::GetFontSize() * 0.75f), p.textDim, L("D", "R"), 0.62f);
    } else {
        // The dry hit, then the echoes.
        list->AddLine(ImVec2(xOf(0.0), inner.max.y), ImVec2(xOf(0.0), inner.max.y - (1.0f - wet * 0.5f) * inner.height()),
                      p.textDim, dp(2.0f));
        auto amplitude = wet;
        for (auto echo = 1; echo < 64 && echo * effectiveTime < span && amplitude > 0.01f; ++echo) {
            const auto x = xOf(echo * effectiveTime);
            const auto top = inner.max.y - amplitude * inner.height();
            list->AddLine(ImVec2(x, inner.max.y), ImVec2(x, top), colour, dp(2.0f));
            list->AddCircleFilled(ImVec2(x, top), dp(2.5f), colour, 10);
            amplitude *= fb;
        }
    }
    auto label = formatValue(*time, effectiveTime);
    if (division != nullptr)
        label << "  " << juce::String(std::string(division->name));
    else if (const auto note = noteValue(effectiveTime, live.tempo); note.isNotEmpty())
        label << "  " << note;
    const auto size = smallTextSize(label.toRawUTF8(), 0.8f);
    smallText(list, ImVec2(plot.max.x - size.x - dp(5.0f), plot.min.y + dp(3.0f)), p.text, label.toRawUTF8(), 0.8f);
    if (interaction.hovered && !interaction.active)
        ImGui::SetTooltip("%s", L("Glisser : temps (horizontal), réinjection (vertical)",
                                  "Drag: time (sideways), feedback (up and down)"));
    return changed;
}

bool reverbPlot(const Plot& plot, Param* room, Param* mix, const ImU32 colour, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (room == nullptr)
        return false;
    const auto interaction = interact("##reverb", plot);
    auto changed = false;
    if (interaction.active && interaction.delta.x != 0.0f)
        changed |= apply(*room, room->value + interaction.delta.x / std::max(1.0f, plot.width()) * (room->maximum - room->minimum), set);
    if (interaction.active && mix != nullptr && interaction.delta.y != 0.0f)
        changed |= apply(*mix, mix->value - interaction.delta.y / std::max(1.0f, plot.height()), set);
    const auto inner = Plot { ImVec2(plot.min.x + dp(6.0f), plot.min.y + dp(16.0f)), ImVec2(plot.max.x - dp(6.0f), plot.max.y - dp(4.0f)) };
    // Seconds for the tail to fall 60 dB, as the room size makes it.
    const auto decay = 0.25f + static_cast<float>(room->value) * 4.0f;
    const auto span = 4.5f;
    const auto wet = mix != nullptr ? static_cast<float>(mix->value) : 0.4f;
    const auto columns = std::max(32, static_cast<int>(inner.width() / dp(2.0f)));
    std::uint32_t seed = 12'345u;
    for (auto column = 0; column < columns; ++column) {
        const auto t = static_cast<float>(column) / static_cast<float>(columns) * span;
        seed = seed * 1'664'525u + 1'013'904'223u;
        const auto noise = 0.55f + 0.45f * static_cast<float>(seed >> 8) / static_cast<float>(1u << 24);
        const auto envelope = std::pow(10.0f, -3.0f * t / decay);
        const auto x = inner.min.x + inner.width() * static_cast<float>(column) / static_cast<float>(columns);
        const auto height = envelope * noise * wet * inner.height();
        list->AddLine(ImVec2(x, inner.max.y), ImVec2(x, inner.max.y - height), theme::alpha(colour, 0.75f), dp(1.2f));
    }
    // The early reflections.
    for (const auto ms : { 9.0f, 17.0f, 26.0f, 38.0f, 51.0f })
        list->AddLine(ImVec2(inner.min.x + ms / 1'000.0f / span * inner.width(), inner.max.y),
                      ImVec2(inner.min.x + ms / 1'000.0f / span * inner.width(), inner.max.y - wet * inner.height() * 0.9f),
                      p.text, dp(1.0f));
    char label[32];
    std::snprintf(label, sizeof(label), "%.1f s", static_cast<double>(decay));
    const auto size = smallTextSize(label, 0.8f);
    smallText(list, ImVec2(plot.max.x - size.x - dp(5.0f), plot.min.y + dp(3.0f)), p.text, label, 0.8f);
    return changed;
}

bool widthPlot(const Plot& plot, Param* width, const ImU32 colour, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (width == nullptr)
        return false;
    const auto interaction = interact("##width", plot);
    auto changed = false;
    if (interaction.active && interaction.delta.x != 0.0f)
        changed |= apply(*width, width->value + interaction.delta.x / std::max(1.0f, plot.width() * 0.5f), set);
    if (interaction.doubleClicked)
        changed |= apply(*width, width->defaultValue, set);
    const ImVec2 base((plot.min.x + plot.max.x) * 0.5f, plot.max.y - dp(6.0f));
    const auto radius = std::min(plot.width() * 0.45f, plot.height() - dp(22.0f));
    list->PathArcTo(base, radius, std::numbers::pi_v<float>, 2.0f * std::numbers::pi_v<float>, 32);
    list->PathStroke(p.gridStrong, dp(1.0f));
    const auto spread = std::clamp(static_cast<float>(width->value), 0.0f, 2.0f) * 0.5f * std::numbers::pi_v<float> * 0.45f;
    const auto up = -0.5f * std::numbers::pi_v<float>;
    const ImVec2 left(base.x + std::cos(up - spread) * radius, base.y + std::sin(up - spread) * radius);
    const ImVec2 right(base.x + std::cos(up + spread) * radius, base.y + std::sin(up + spread) * radius);
    list->PathLineTo(base);
    list->PathArcTo(base, radius, up - spread, up + spread, 24);
    list->PathFillConvex(theme::alpha(colour, 0.22f));
    list->AddLine(base, left, colour, dp(2.0f));
    list->AddLine(base, right, colour, dp(2.0f));
    smallText(list, ImVec2(left.x - dp(10.0f), left.y - dp(12.0f)), p.textDim, "L");
    smallText(list, ImVec2(right.x + dp(4.0f), right.y - dp(12.0f)), p.textDim, "R");
    return changed;
}

// An LFO as CyDAW evaluates it: offset plus depth times the shape.
float lfoShapeAt(const int shape, const float phase);
float lfoShape(const int shape, const float phase)
{
    return lfoShapeAt(shape, phase);
}

float lfoShapeAt(const int shape, const float phase)
{
    switch (shape) {
    case 1: return 1.0f - 4.0f * std::abs(phase - 0.5f);
    case 2: return phase * 2.0f - 1.0f;
    case 3: return phase < 0.5f ? 1.0f : -1.0f;
    default: return std::sin(2.0f * std::numbers::pi_v<float> * phase);
    }
}

bool lfoPlot(const Plot& plot, Param* rate, Param* depth, Param* offset, Param* shape, const ImU32 colour, const Live& live,
             const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (rate == nullptr)
        return false;
    const auto interaction = interact("##lfo", plot);
    auto changed = false;
    if (interaction.active && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        changed |= apply(*rate, rate->value * std::pow(2.0, interaction.delta.x * interaction.speed / dp(120.0f)), set);
        if (depth != nullptr)
            changed |= apply(*depth, depth->value - interaction.delta.y / std::max(1.0f, plot.height() * 0.5f), set);
    }
    if (shape != nullptr && interaction.hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        && std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).x) < 3.0f
        && std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y) < 3.0f) {
        const auto count = static_cast<int>(std::round(shape->maximum - shape->minimum)) + 1;
        changed |= apply(*shape, shape->minimum + (static_cast<int>(std::round(shape->value - shape->minimum)) + 1) % std::max(1, count),
                         set);
    }
    if (interaction.wheel != 0.0f && offset != nullptr)
        changed |= apply(*offset, offset->value + interaction.wheel * 0.05, set);
    const auto inner = Plot { ImVec2(plot.min.x + dp(6.0f), plot.min.y + dp(18.0f)), ImVec2(plot.max.x - dp(6.0f), plot.max.y - dp(6.0f)) };
    const auto middle = (inner.min.y + inner.max.y) * 0.5f;
    const auto yOf = [&](const float value) { return middle - std::clamp(value, -1.0f, 1.0f) * inner.height() * 0.5f; };
    list->AddLine(ImVec2(inner.min.x, middle), ImVec2(inner.max.x, middle), p.gridStrong);
    list->AddLine(ImVec2(inner.min.x, yOf(1.0f)), ImVec2(inner.max.x, yOf(1.0f)), p.grid);
    list->AddLine(ImVec2(inner.min.x, yOf(-1.0f)), ImVec2(inner.max.x, yOf(-1.0f)), p.grid);
    const auto form = shape != nullptr ? static_cast<int>(std::round(shape->value)) : 0;
    const auto amount = depth != nullptr ? static_cast<float>(depth->value) : 1.0f;
    const auto centre = offset != nullptr ? static_cast<float>(offset->value) : 0.0f;
    std::vector<ImVec2> points;
    const auto columns = std::max(64, static_cast<int>(inner.width() / dp(1.0f)));
    for (auto column = 0; column <= columns; ++column) {
        const auto t = static_cast<float>(column) / static_cast<float>(columns);
        points.emplace_back(inner.min.x + inner.width() * t, yOf(centre + lfoShape(form, std::fmod(t * 2.0f, 1.0f)) * amount));
    }
    curve(list, points, colour, dp(2.0f), theme::alpha(colour, 0.12f), middle);
    // Where it stands now.
    if (live.hasControl && live.running) {
        const auto y = yOf(live.control);
        list->AddLine(ImVec2(inner.min.x, y), ImVec2(inner.max.x, y), theme::alpha(p.modulation, 0.7f), dp(1.0f));
        list->AddCircleFilled(ImVec2(inner.max.x - dp(4.0f), y), dp(4.0f), p.modulation, 12);
    }
    auto label = formatValue(*rate, rate->value);
    if (shape != nullptr)
        label = formatValue(*shape, shape->value) + "  " + label;
    const auto size = smallTextSize(label.toRawUTF8(), 0.8f);
    smallText(list, ImVec2(plot.max.x - size.x - dp(5.0f), plot.min.y + dp(3.0f)), p.text, label.toRawUTF8(), 0.8f);
    if (interaction.hovered && !interaction.active)
        ImGui::SetTooltip("%s", L("Glisser : vitesse (horizontal), profondeur (vertical). Clic : forme suivante.",
                                  "Drag: rate (sideways), depth (up and down). Click: next shape."));
    return changed;
}

// ---- The effect catalogue's displays --------------------------------------------------------------

// What the dynamics panes share on their right: the input level over time
// and the gain taken off it, against a threshold line.
void historyPanel(ImDrawList* list, const Plot& history, const float thresholdDb, const ImU32 colour, const Live& live,
                  const float gainDb = 0.0f)
{
    const auto& p = theme::palette();
    if (history.width() <= dp(40.0f))
        return;
    constexpr float floorDb = -60.0f;
    list->AddRectFilled(history.min, history.max, theme::alpha(p.bg0, 0.6f), dp(3.0f));
    const auto yLevel = [&](const float db) {
        return history.max.y - std::clamp((db - floorDb) / -floorDb, 0.0f, 1.0f) * history.height();
    };
    list->AddLine(ImVec2(history.min.x, yLevel(thresholdDb)), ImVec2(history.max.x, yLevel(thresholdDb)),
                  theme::alpha(colour, 0.45f));
    if (live.inputHistory != nullptr && live.historyCount > 1) {
        const auto step = history.width() / static_cast<float>(live.historyCount - 1);
        std::vector<ImVec2> levels;
        std::vector<ImVec2> reductions;
        for (auto index = 0; index < live.historyCount; ++index) {
            const auto x = history.min.x + step * static_cast<float>(index);
            levels.emplace_back(x, yLevel(live.inputHistory[index]));
            const auto reduction = live.reductionHistory != nullptr ? live.reductionHistory[index] - gainDb : 0.0f;
            reductions.emplace_back(x, history.min.y + std::clamp(-reduction / 24.0f, 0.0f, 1.0f) * history.height());
        }
        curve(list, levels, theme::alpha(p.textDim, 0.7f), dp(1.0f), theme::alpha(p.textDim, 0.18f), history.max.y);
        curve(list, reductions, p.record, dp(1.5f), theme::alpha(p.record, 0.18f), history.min.y);
    }
    const auto reduction = live.running ? std::min(0.0f, live.outputDb - live.inputDb - gainDb) : 0.0f;
    char text[32];
    std::snprintf(text, sizeof(text), "GR %.1f dB", static_cast<double>(reduction));
    const auto size = smallTextSize(text);
    smallText(list, ImVec2(history.max.x - size.x - dp(4.0f), history.min.y + dp(3.0f)),
              reduction < -0.5f ? p.record : p.textFaint, text);
}

// A transfer curve in dB on the left of a dynamics pane, the history on the
// right. `outputOf` maps an input level to an output level.
template <typename Transfer>
Plot transferSquare(ImDrawList* list, const Plot& plot, const ImU32 colour, Transfer outputOf, const float markerDb,
                    const Live& live, Plot& history)
{
    const auto& p = theme::palette();
    const auto side = std::min(plot.height(), plot.width() * 0.55f);
    const Plot transfer { plot.min, ImVec2(plot.min.x + side, plot.min.y + side) };
    history = Plot { ImVec2(transfer.max.x + dp(6.0f), plot.min.y), plot.max };
    constexpr float floorDb = -60.0f;
    const auto xOf = [&](const float db) { return transfer.min.x + (db - floorDb) / -floorDb * transfer.width(); };
    const auto yOf = [&](const float db) {
        return transfer.max.y - std::clamp((db - floorDb) / -floorDb, 0.0f, 1.0f) * transfer.height();
    };
    for (const auto db : { -48.0f, -36.0f, -24.0f, -12.0f }) {
        list->AddLine(ImVec2(xOf(db), transfer.min.y), ImVec2(xOf(db), transfer.max.y), p.grid);
        list->AddLine(ImVec2(transfer.min.x, yOf(db)), ImVec2(transfer.max.x, yOf(db)), p.grid);
    }
    list->AddLine(ImVec2(xOf(floorDb), yOf(floorDb)), ImVec2(xOf(0.0f), yOf(0.0f)), p.gridStrong);
    std::vector<ImVec2> points;
    for (auto step = 0; step <= 120; ++step) {
        const auto in = floorDb + static_cast<float>(step) * 0.5f;
        points.emplace_back(xOf(in), yOf(outputOf(in)));
    }
    curve(list, points, colour, dp(2.0f), theme::alpha(colour, 0.10f), transfer.max.y);
    for (auto x = transfer.min.x; x < transfer.max.x; x += dp(6.0f))
        list->AddLine(ImVec2(x, yOf(markerDb)), ImVec2(std::min(transfer.max.x, x + dp(3.0f)), yOf(markerDb)),
                      theme::alpha(colour, 0.6f));
    if (live.running && live.inputDb > floorDb) {
        const auto in = std::min(0.0f, live.inputDb);
        list->AddCircleFilled(ImVec2(xOf(in), yOf(outputOf(in))), dp(3.5f), p.text, 12);
    }
    return transfer;
}

bool limiterPlot(const Plot& plot, Param* gain, Param* ceiling, const ImU32 colour, const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    if (gain == nullptr || ceiling == nullptr)
        return false;
    const auto g = static_cast<float>(gain->value);
    const auto c = static_cast<float>(ceiling->value);
    Plot history;
    const auto transfer = transferSquare(list, plot, colour, [&](const float in) { return std::min(in + g, c); }, c, live, history);
    const auto interaction = interact("##limiter", transfer);
    auto changed = false;
    if (interaction.active && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        changed |= apply(*ceiling, ceiling->value - interaction.delta.y / std::max(1.0f, transfer.height()) * 60.0, set);
        changed |= apply(*gain, gain->value + interaction.delta.x / std::max(1.0f, transfer.width()) * 60.0, set);
    }
    if (interaction.doubleClicked) {
        changed |= apply(*gain, gain->defaultValue, set);
        changed |= apply(*ceiling, ceiling->defaultValue, set);
    }
    if (interaction.hovered || interaction.active)
        ImGui::SetTooltip("%s · %s", formatValue(*gain, gain->value).toRawUTF8(), formatValue(*ceiling, ceiling->value).toRawUTF8());
    historyPanel(list, history, c, colour, live, g);
    return changed;
}

bool gatePlot(const Plot& plot, Param* threshold, Param* range, const ImU32 colour, const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    if (threshold == nullptr)
        return false;
    const auto t = static_cast<float>(threshold->value);
    const auto floor = range != nullptr ? static_cast<float>(range->value) : -80.0f;
    Plot history;
    const auto transfer = transferSquare(list, plot, colour, [&](const float in) { return in >= t ? in : in + floor; }, t, live,
                                         history);
    const auto interaction = interact("##gate", transfer);
    auto changed = false;
    if (interaction.active && interaction.delta.x != 0.0f)
        changed |= apply(*threshold, -60.0 + (interaction.mouse.x - transfer.min.x) / std::max(1.0f, transfer.width()) * 60.0, set);
    if (interaction.active && range != nullptr && interaction.delta.y != 0.0f)
        changed |= apply(*range, range->value - interaction.delta.y / std::max(1.0f, transfer.height()) * 80.0, set);
    if (interaction.hovered || interaction.active)
        ImGui::SetTooltip("%s%s", formatValue(*threshold, threshold->value).toRawUTF8(),
                          range != nullptr ? (juce::String::fromUTF8(" · ") + formatValue(*range, range->value)).toRawUTF8() : "");
    historyPanel(list, history, t, colour, live);
    return changed;
}

// The state-variable filter's four responses, through its bilinear warp.
float stateVariableDb(const float hz, const float cutoff, const float q, const int mode)
{
    const auto wc = std::tan(kPi * std::min<double>(cutoff, kRate * 0.45) / kRate);
    const auto w = std::tan(kPi * std::min<double>(hz, kRate * 0.4999) / kRate);
    const auto x = w / std::max(wc, 1.0e-9);
    const auto k = 1.0 / std::max(0.05, static_cast<double>(q));
    const auto denominator = std::sqrt((1.0 - x * x) * (1.0 - x * x) + (x * k) * (x * k));
    double magnitude = 1.0 / denominator;
    if (mode == 1)
        magnitude = x * x / denominator;
    else if (mode == 2)
        magnitude = x * k / denominator;
    else if (mode == 3)
        magnitude = std::abs(1.0 - x * x) / denominator;
    return static_cast<float>(20.0 * std::log10(std::max(magnitude, 1.0e-6)));
}

// A filter with a mode: the curve, the cutoff handle, the mode named and
// changed with a click on its name.
bool multiFilterPlot(const Plot& plot, Param* mode, Param* cutoff, Param* resonance, const ImU32 colour, const Live& live,
                     const Setter& set, const float sweepOctaves = 0.0f)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    constexpr float top = 24.0f;
    constexpr float bottom = -36.0f;
    frequencyGrid(list, plot);
    list->AddLine(ImVec2(plot.min.x, yOfDb(plot, 0.0f, top, bottom)), ImVec2(plot.max.x, yOfDb(plot, 0.0f, top, bottom)), p.gridStrong);
    if (live.before != nullptr)
        drawSpectrum(list, plot, *live.before, theme::alpha(p.textDim, 0.45f), theme::alpha(p.textDim, 0.13f));
    if (live.after != nullptr)
        drawSpectrum(list, plot, *live.after, theme::alpha(colour, 0.65f), theme::alpha(colour, 0.16f));
    if (cutoff == nullptr)
        return false;
    const auto fc = static_cast<float>(cutoff->value);
    const auto q = resonance != nullptr ? static_cast<float>(resonance->value) : 0.707f;
    const auto modeIndex = mode != nullptr ? std::clamp(static_cast<int>(std::round(mode->value)), 0, 3) : 0;
    // The range an LFO sweeps the cutoff over, shaded.
    if (sweepOctaves > 0.0f) {
        const auto low = xOfHz(plot, fc * std::exp2(-sweepOctaves));
        const auto high = xOfHz(plot, fc * std::exp2(sweepOctaves));
        list->AddRectFilled(ImVec2(low, plot.min.y), ImVec2(high, plot.max.y), theme::alpha(colour, 0.08f));
        list->AddLine(ImVec2(low, plot.min.y), ImVec2(low, plot.max.y), theme::alpha(colour, 0.4f));
        list->AddLine(ImVec2(high, plot.min.y), ImVec2(high, plot.max.y), theme::alpha(colour, 0.4f));
    }
    // The mode's name, top right, a click changes it.
    static const char* modesFr[] { "Passe-bas", "Passe-haut", "Passe-bande", "Réjecteur" };
    static const char* modesEn[] { "Low-pass", "High-pass", "Band-pass", "Notch" };
    auto changed = false;
    if (mode != nullptr) {
        const auto* name = ui::french() ? modesFr[modeIndex] : modesEn[modeIndex];
        const auto size = smallTextSize(name, 0.8f);
        const ImVec2 at(plot.max.x - size.x - dp(6.0f), plot.min.y + dp(3.0f));
        const Plot label { ImVec2(at.x - dp(3.0f), at.y - dp(1.0f)), ImVec2(plot.max.x - dp(2.0f), at.y + size.y + dp(2.0f)) };
        const auto modeClick = interact("##mode", label);
        list->AddRectFilled(label.min, label.max, modeClick.hovered ? p.bg4 : p.bg3, dp(3.0f));
        smallText(list, at, p.text, name, 0.8f);
        if (modeClick.hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            changed |= apply(*mode, static_cast<double>((modeIndex + 1) % 4), set);
        if (modeClick.hovered)
            ImGui::SetTooltip("%s", L("Clic : mode suivant", "Click: next mode"));
    }
    const auto interaction = interact("##multiFilter", Plot { ImVec2(plot.min.x, plot.min.y + dp(18.0f)), plot.max });
    if (interaction.active && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        changed |= apply(*cutoff, interaction.fine ? cutoff->value * std::pow(2.0, interaction.delta.x / dp(400.0f))
                                                   : static_cast<double>(hzOfX(plot, interaction.mouse.x)),
                         set);
        if (resonance != nullptr)
            changed |= apply(*resonance, resonance->value * std::pow(1.02, -interaction.delta.y / ImGui::GetStyle().FontScaleDpi),
                             set);
    }
    if (interaction.wheel != 0.0f && resonance != nullptr)
        changed |= apply(*resonance, resonance->value * std::pow(1.12, interaction.wheel), set);
    std::vector<ImVec2> points;
    const auto steps = std::max(48, static_cast<int>(plot.width() / dp(2.0f)));
    for (auto step = 0; step <= steps; ++step) {
        const auto x = plot.min.x + plot.width() * static_cast<float>(step) / static_cast<float>(steps);
        points.emplace_back(x, yOfDb(plot, stateVariableDb(hzOfX(plot, x), fc, q, modeIndex), top, bottom));
    }
    curve(list, points, colour, dp(2.0f), theme::alpha(colour, 0.10f), plot.max.y);
    if (live.modulated != nullptr)
        if (const auto found = live.modulated->find(cutoff->binding); found != live.modulated->end()) {
            const auto mf = std::clamp(found->second, 20.0f, 20'000.0f);
            std::vector<ImVec2> moved;
            for (auto step = 0; step <= steps; ++step) {
                const auto x = plot.min.x + plot.width() * static_cast<float>(step) / static_cast<float>(steps);
                moved.emplace_back(x, yOfDb(plot, stateVariableDb(hzOfX(plot, x), mf, q, modeIndex), top, bottom));
            }
            list->AddPolyline(moved.data(), static_cast<int>(moved.size()), theme::alpha(p.modulation, 0.9f), dp(1.6f));
        }
    handle(list, ImVec2(xOfHz(plot, fc), yOfDb(plot, std::max(-30.0f, stateVariableDb(fc, fc, q, modeIndex)), top, bottom)), colour,
           interaction.hovered || interaction.active);
    if (interaction.hovered || interaction.active)
        ImGui::SetTooltip("%s%s", formatValue(*cutoff, cutoff->value).toRawUTF8(),
                          resonance != nullptr ? (juce::String::fromUTF8(" · Q ") + juce::String(resonance->value, 2)).toRawUTF8() : "");
    return changed;
}

float shelfDb(const float hz, const float frequency, const float gainDb, const bool high)
{
    const auto a = std::pow(10.0, gainDb / 40.0);
    const auto w0 = 2.0 * kPi * std::min<double>(frequency, kRate * 0.45) / kRate;
    const auto cosine = std::cos(w0);
    const auto alpha = std::sin(w0) / 2.0 * std::sqrt(2.0);
    const auto root = 2.0 * std::sqrt(a) * alpha;
    double b0, b1, b2, a0, a1, a2;
    if (!high) {
        b0 = a * ((a + 1.0) - (a - 1.0) * cosine + root);
        b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosine);
        b2 = a * ((a + 1.0) - (a - 1.0) * cosine - root);
        a0 = (a + 1.0) + (a - 1.0) * cosine + root;
        a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosine);
        a2 = (a + 1.0) + (a - 1.0) * cosine - root;
    } else {
        b0 = a * ((a + 1.0) + (a - 1.0) * cosine + root);
        b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosine);
        b2 = a * ((a + 1.0) + (a - 1.0) * cosine - root);
        a0 = (a + 1.0) - (a - 1.0) * cosine + root;
        a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosine);
        a2 = (a + 1.0) - (a - 1.0) * cosine - root;
    }
    const auto z = unitDelay(hz);
    const auto response = (b0 + b1 * z + b2 * z * z) / (a0 + a1 * z + a2 * z * z);
    return static_cast<float>(20.0 * std::log10(std::max(std::abs(response), 1.0e-9)));
}

bool eqThreePlot(const Plot& plot, std::vector<Param>& parameters, const Pane& pane, const ImU32 colour, const Live& live,
                 const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto* low = parameterAt(parameters, pane, 0);
    auto* mid = parameterAt(parameters, pane, 1);
    auto* high = parameterAt(parameters, pane, 2);
    auto* lowFrequency = parameterAt(parameters, pane, 3);
    auto* highFrequency = parameterAt(parameters, pane, 4);
    if (low == nullptr || mid == nullptr || high == nullptr || lowFrequency == nullptr || highFrequency == nullptr)
        return false;
    constexpr float top = 15.0f;
    constexpr float bottom = -27.0f;
    frequencyGrid(list, plot);
    for (const auto db : { -24.0f, -12.0f, 12.0f })
        list->AddLine(ImVec2(plot.min.x, yOfDb(plot, db, top, bottom)), ImVec2(plot.max.x, yOfDb(plot, db, top, bottom)), p.grid);
    list->AddLine(ImVec2(plot.min.x, yOfDb(plot, 0.0f, top, bottom)), ImVec2(plot.max.x, yOfDb(plot, 0.0f, top, bottom)), p.gridStrong);
    if (live.before != nullptr)
        drawSpectrum(list, plot, *live.before, theme::alpha(p.textDim, 0.45f), theme::alpha(p.textDim, 0.13f));
    if (live.after != nullptr)
        drawSpectrum(list, plot, *live.after, theme::alpha(colour, 0.65f), theme::alpha(colour, 0.16f));
    const auto fl = static_cast<float>(lowFrequency->value);
    const auto fh = std::max(static_cast<float>(highFrequency->value), fl * 1.5f);
    const auto fm = std::sqrt(fl * fh);
    const auto responseAt = [&](const float hz) {
        return shelfDb(hz, fl, static_cast<float>(low->value), false) + peakingDb(hz, fm, static_cast<float>(mid->value), 0.7f)
               + shelfDb(hz, fh, static_cast<float>(high->value), true);
    };
    const ImVec2 handles[] { ImVec2(xOfHz(plot, fl), yOfDb(plot, static_cast<float>(low->value), top, bottom)),
                             ImVec2(xOfHz(plot, fm), yOfDb(plot, static_cast<float>(mid->value), top, bottom)),
                             ImVec2(xOfHz(plot, fh), yOfDb(plot, static_cast<float>(high->value), top, bottom)) };
    auto* storage = ImGui::GetStateStorage();
    const auto grabbedId = ImGui::GetID("##eq3Grab");
    const auto interaction = interact("##eq3", plot);
    auto nearest = 0;
    auto best = FLT_MAX;
    for (auto index = 0; index < 3; ++index)
        if (const auto distance = std::abs(handles[index].x - interaction.mouse.x); distance < best) {
            best = distance;
            nearest = index;
        }
    if (interaction.activated)
        storage->SetInt(grabbedId, nearest);
    const auto grabbed = interaction.active ? storage->GetInt(grabbedId, 0) : -1;
    auto changed = false;
    if (grabbed >= 0 && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        const auto db = dbOfY(plot, interaction.mouse.y, top, bottom);
        if (grabbed == 0) {
            changed |= apply(*low, db, set);
            changed |= apply(*lowFrequency, hzOfX(plot, interaction.mouse.x), set);
        } else if (grabbed == 1) {
            changed |= apply(*mid, db, set);
        } else {
            changed |= apply(*high, db, set);
            changed |= apply(*highFrequency, hzOfX(plot, interaction.mouse.x), set);
        }
    }
    std::vector<ImVec2> points;
    const auto steps = std::max(48, static_cast<int>(plot.width() / dp(2.0f)));
    for (auto step = 0; step <= steps; ++step) {
        const auto x = plot.min.x + plot.width() * static_cast<float>(step) / static_cast<float>(steps);
        points.emplace_back(x, yOfDb(plot, responseAt(hzOfX(plot, x)), top, bottom));
    }
    curve(list, points, colour, dp(2.0f), 0, plot.max.y);
    static const char* labelsFr[] { "Graves", "Médiums", "Aigus" };
    static const char* labelsEn[] { "Low", "Mid", "High" };
    for (auto index = 0; index < 3; ++index) {
        handle(list, handles[index], colour, index == (grabbed >= 0 ? grabbed : (interaction.hovered ? nearest : -1)));
        smallText(list, ImVec2(handles[index].x - dp(10.0f), handles[index].y - dp(17.0f)), p.textDim,
                  ui::french() ? labelsFr[index] : labelsEn[index], 0.62f);
    }
    return changed;
}

// What a chorus, a flanger or a phaser sweeps over time: the delay, or the
// frequency, of each side, two cycles of it.
bool sweepPlot(const Plot& plot, Param* rate, Param* depth, Param* third, Param* fourth, const int variant, const ImU32 colour,
               const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (rate == nullptr || depth == nullptr)
        return false;
    const auto interaction = interact("##sweep", plot);
    auto changed = false;
    if (interaction.active && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        changed |= apply(*rate, rate->value * std::pow(2.0, interaction.delta.x * interaction.speed / dp(120.0f)), set);
        changed |= apply(*depth, depth->value - interaction.delta.y / std::max(1.0f, plot.height() * 0.5f), set);
    }
    if (interaction.doubleClicked) {
        changed |= apply(*rate, rate->defaultValue, set);
        changed |= apply(*depth, depth->defaultValue, set);
    }
    const auto inner = Plot { ImVec2(plot.min.x + dp(6.0f), plot.min.y + dp(18.0f)), ImVec2(plot.max.x - dp(6.0f), plot.max.y - dp(6.0f)) };
    const auto middle = (inner.min.y + inner.max.y) * 0.5f;
    list->AddLine(ImVec2(inner.min.x, middle), ImVec2(inner.max.x, middle), p.gridStrong);
    const auto amount = static_cast<float>(depth->value);
    // The right side's offset: a chorus by its width, the others a quarter.
    const auto offset = variant == 0 ? (fourth != nullptr ? 0.25f * static_cast<float>(fourth->value) : 0.2f) : 0.25f;
    const auto amplitude = variant == 0 ? 0.5f * amount : amount;
    for (auto side = 1; side >= 0; --side) {
        std::vector<ImVec2> points;
        const auto columns = std::max(64, static_cast<int>(inner.width() / dp(1.5f)));
        for (auto column = 0; column <= columns; ++column) {
            const auto t = static_cast<float>(column) / static_cast<float>(columns);
            const auto phase = t * 2.0f + (side == 1 ? offset : 0.0f);
            const auto value = std::sin(2.0f * std::numbers::pi_v<float> * phase) * amplitude;
            points.emplace_back(inner.min.x + inner.width() * t, middle - value * inner.height() * 0.48f);
        }
        list->AddPolyline(points.data(), static_cast<int>(points.size()), side == 0 ? colour : theme::alpha(colour, 0.4f),
                          side == 0 ? dp(2.0f) : dp(1.2f));
    }
    smallText(list, ImVec2(inner.min.x, inner.max.y - ImGui::GetFontSize() * 0.75f), colour, L("G", "L"), 0.62f);
    smallText(list, ImVec2(inner.min.x + dp(12.0f), inner.max.y - ImGui::GetFontSize() * 0.75f), theme::alpha(colour, 0.5f), L("D", "R"), 0.62f);
    auto label = formatValue(*rate, rate->value);
    if (third != nullptr)
        label << "  " << formatValue(*third, third->value);
    const auto size = smallTextSize(label.toRawUTF8(), 0.8f);
    smallText(list, ImVec2(plot.max.x - size.x - dp(5.0f), plot.min.y + dp(3.0f)), p.text, label.toRawUTF8(), 0.8f);
    if (interaction.hovered && !interaction.active)
        ImGui::SetTooltip("%s", L("Glisser : vitesse (horizontal), profondeur (vertical)", "Drag: rate (sideways), depth (up and down)"));
    return changed;
}

float lfoShapeAt(const int shape, const float phase);

bool tremoloPlot(const Plot& plot, Param* rate, Param* depth, Param* shape, Param* spread, const ImU32 colour, const Setter& set,
                 Param* sync = nullptr)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (rate == nullptr || depth == nullptr)
        return false;
    const auto interaction = interact("##tremolo", plot);
    auto changed = false;
    if (interaction.active && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        changed |= apply(*rate, rate->value * std::pow(2.0, interaction.delta.x * interaction.speed / dp(120.0f)), set);
        changed |= apply(*depth, depth->value - interaction.delta.y / std::max(1.0f, plot.height()), set);
    }
    if (shape != nullptr && interaction.hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        && std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).x) < 3.0f
        && std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y) < 3.0f)
        changed |= apply(*shape, static_cast<double>((static_cast<int>(std::round(shape->value)) + 1) % 4), set);
    const auto inner = Plot { ImVec2(plot.min.x + dp(6.0f), plot.min.y + dp(18.0f)), ImVec2(plot.max.x - dp(6.0f), plot.max.y - dp(6.0f)) };
    list->AddLine(ImVec2(inner.min.x, inner.min.y), ImVec2(inner.max.x, inner.min.y), p.grid);
    const auto form = shape != nullptr ? static_cast<int>(std::round(shape->value)) : 0;
    const auto amount = static_cast<float>(depth->value);
    const auto apart = spread != nullptr ? static_cast<float>(spread->value) : 0.0f;
    for (auto side = 1; side >= 0; --side) {
        std::vector<ImVec2> points;
        const auto columns = std::max(64, static_cast<int>(inner.width() / dp(1.5f)));
        for (auto column = 0; column <= columns; ++column) {
            const auto t = static_cast<float>(column) / static_cast<float>(columns);
            auto phase = t * 2.0f + (side == 1 ? 0.5f * apart : 0.0f);
            phase -= std::floor(phase);
            const auto gain = 1.0f - amount * (0.5f - 0.5f * lfoShapeAt(form, phase));
            points.emplace_back(inner.min.x + inner.width() * t, inner.max.y - gain * inner.height());
        }
        if (side == 0)
            curve(list, points, colour, dp(2.0f), theme::alpha(colour, 0.12f), inner.max.y);
        else if (apart > 0.01f)
            list->AddPolyline(points.data(), static_cast<int>(points.size()), theme::alpha(colour, 0.45f), dp(1.2f));
    }
    auto label = sync != nullptr && engine::noteDivisionFor(static_cast<float>(sync->value)) != nullptr
                     ? formatValue(*sync, sync->value)
                     : formatValue(*rate, rate->value);
    if (shape != nullptr)
        label = formatValue(*shape, shape->value) + "  " + label;
    const auto size = smallTextSize(label.toRawUTF8(), 0.8f);
    smallText(list, ImVec2(plot.max.x - size.x - dp(5.0f), plot.min.y + dp(3.0f)), p.text, label.toRawUTF8(), 0.8f);
    if (interaction.hovered && !interaction.active)
        ImGui::SetTooltip("%s", L("Glisser : vitesse et profondeur. Clic : forme suivante.", "Drag: rate and depth. Click: next shape."));
    return changed;
}

bool crusherPlot(const Plot& plot, Param* bits, Param* downsample, const ImU32 colour, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (bits == nullptr || downsample == nullptr)
        return false;
    const auto interaction = interact("##crusher", plot);
    auto changed = false;
    if (interaction.active && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        changed |= apply(*bits, bits->value - interaction.delta.y / std::max(1.0f, plot.height()) * 15.0, set);
        changed |= apply(*downsample, downsample->value + interaction.delta.x / std::max(1.0f, plot.width()) * 31.0, set);
    }
    const auto inner = Plot { ImVec2(plot.min.x + dp(6.0f), plot.min.y + dp(18.0f)), ImVec2(plot.max.x - dp(6.0f), plot.max.y - dp(6.0f)) };
    const auto middle = (inner.min.y + inner.max.y) * 0.5f;
    list->AddLine(ImVec2(inner.min.x, middle), ImVec2(inner.max.x, middle), p.gridStrong);
    // A sine, then the same through the crusher at 96 samples a cycle.
    const auto levels = std::exp2(std::max(0.0f, static_cast<float>(bits->value) - 1.0f));
    const auto hold = std::max(1.0f, static_cast<float>(downsample->value));
    std::vector<ImVec2> clean;
    std::vector<ImVec2> crushed;
    auto held = 0.0f;
    auto counter = 1.0e9f;
    constexpr auto samples = 192;
    for (auto index = 0; index <= samples; ++index) {
        const auto t = static_cast<float>(index) / static_cast<float>(samples);
        const auto value = std::sin(2.0f * std::numbers::pi_v<float> * 2.0f * t) * 0.9f;
        counter += 1.0f;
        if (counter >= hold) {
            counter = std::fmod(counter, hold);
            held = std::round(value * levels) / levels;
        }
        const auto x = inner.min.x + inner.width() * t;
        clean.emplace_back(x, middle - value * inner.height() * 0.48f);
        if (!crushed.empty())
            crushed.emplace_back(x, crushed.back().y);
        crushed.emplace_back(x, middle - held * inner.height() * 0.48f);
    }
    list->AddPolyline(clean.data(), static_cast<int>(clean.size()), theme::alpha(p.textDim, 0.5f), dp(1.0f));
    list->AddPolyline(crushed.data(), static_cast<int>(crushed.size()), colour, dp(2.0f));
    auto label = formatValue(*bits, bits->value) + "  " + formatValue(*downsample, downsample->value);
    const auto size = smallTextSize(label.toRawUTF8(), 0.8f);
    smallText(list, ImVec2(plot.max.x - size.x - dp(5.0f), plot.min.y + dp(3.0f)), p.text, label.toRawUTF8(), 0.8f);
    if (interaction.hovered && !interaction.active)
        ImGui::SetTooltip("%s", L("Glisser : bits (vertical), sous-échantillonnage (horizontal)",
                                  "Drag: bits (up and down), downsampling (sideways)"));
    return changed;
}

// A drum hit as it comes in (grey) and as the shaper leaves it.
bool transientPlot(const Plot& plot, Param* attack, Param* sustain, const ImU32 colour, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (attack == nullptr || sustain == nullptr)
        return false;
    const auto interaction = interact("##transient", plot);
    auto changed = false;
    const auto inner = Plot { ImVec2(plot.min.x + dp(6.0f), plot.min.y + dp(18.0f)), ImVec2(plot.max.x - dp(6.0f), plot.max.y - dp(6.0f)) };
    const auto split = inner.min.x + inner.width() * 0.22f;
    if (interaction.active && interaction.delta.y != 0.0f) {
        auto& target = interaction.mouse.x < split ? *attack : *sustain;
        changed |= apply(target, target.value - interaction.delta.y / std::max(1.0f, inner.height()) * 2.0, set);
    }
    if (interaction.doubleClicked) {
        changed |= apply(*attack, 0.0, set);
        changed |= apply(*sustain, 0.0, set);
    }
    list->AddLine(ImVec2(split, inner.min.y), ImVec2(split, inner.max.y), p.grid);
    const auto a = static_cast<float>(attack->value);
    const auto s = static_cast<float>(sustain->value);
    std::vector<ImVec2> original;
    std::vector<ImVec2> shaped;
    const auto columns = std::max(64, static_cast<int>(inner.width() / dp(1.5f)));
    for (auto column = 0; column <= columns; ++column) {
        const auto t = static_cast<float>(column) / static_cast<float>(columns);
        const auto envelope = std::min(1.0f, t * 60.0f) * std::exp(-t * 5.0f);
        // Where the hit starts the fast follower leads; then it trails.
        const auto onset = std::clamp(1.0f - t * 6.0f, 0.0f, 1.0f);
        const auto gainDb = a * 12.0f * onset + s * 12.0f * (1.0f - onset);
        const auto value = std::min(1.2f, envelope * std::pow(10.0f, gainDb / 20.0f));
        const auto x = inner.min.x + inner.width() * t;
        original.emplace_back(x, inner.max.y - envelope * inner.height() * 0.8f);
        shaped.emplace_back(x, inner.max.y - value * inner.height() * 0.8f);
    }
    list->AddPolyline(original.data(), static_cast<int>(original.size()), theme::alpha(p.textDim, 0.55f), dp(1.2f));
    curve(list, shaped, colour, dp(2.0f), theme::alpha(colour, 0.14f), inner.max.y);
    smallText(list, ImVec2(inner.min.x + dp(2.0f), inner.max.y - ImGui::GetFontSize() * 0.75f), p.textDim, L("attaque", "attack"), 0.62f);
    smallText(list, ImVec2(split + dp(4.0f), inner.max.y - ImGui::GetFontSize() * 0.75f), p.textDim, L("maintien", "sustain"), 0.62f);
    if (interaction.hovered && !interaction.active)
        ImGui::SetTooltip("%s", L("Glisser verticalement : à gauche l'attaque, à droite le maintien",
                                  "Drag up or down: attack on the left, sustain on the right"));
    return changed;
}

bool levelPlot(const Plot& plot, const ImU32 colour, const Live& live)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto barWidth = std::min(dp(14.0f), plot.width() * 0.2f);
    const auto drawBar = [&](const float x, const float db, const ImU32 barColour, const char* label) {
        const ImVec2 min(x, plot.min.y + dp(18.0f));
        const ImVec2 max(x + barWidth, plot.max.y - dp(14.0f));
        list->AddRectFilled(min, max, p.bg0, dp(2.0f));
        const auto level = live.running ? ui::meterPosition(db) : 0.0f;
        list->AddRectFilled(ImVec2(min.x, max.y - level * (max.y - min.y)), max, barColour, dp(2.0f));
        smallText(list, ImVec2(x, plot.max.y - dp(12.0f)), p.textFaint, label, 0.66f);
    };
    const auto centre = (plot.min.x + plot.max.x) * 0.5f;
    drawBar(centre - barWidth - dp(6.0f), live.inputDb, theme::alpha(p.textDim, 0.8f), L("ent.", "in"));
    drawBar(centre + dp(6.0f), live.outputDb, colour, L("sort.", "out"));
    return false;
}

} // namespace

// ---- The parametric equaliser ------------------------------------------------------------------

namespace {

// A colour per band, the same in every view of it.
constexpr ImU32 kBandColours[engine::eqProBands] {
    IM_COL32(255, 112, 112, 255), IM_COL32(255, 168, 82, 255), IM_COL32(246, 212, 74, 255),  IM_COL32(112, 216, 126, 255),
    IM_COL32(64, 212, 180, 255),  IM_COL32(88, 172, 250, 255), IM_COL32(158, 128, 250, 255), IM_COL32(244, 134, 182, 255),
};
constexpr ImU32 kHandleText = IM_COL32(18, 20, 24, 255);

const char* shapeName(const engine::EqBandType type)
{
    switch (type) {
    case engine::EqBandType::bell: return L("Cloche", "Bell");
    case engine::EqBandType::lowShelf: return L("Plateau bas", "Low shelf");
    case engine::EqBandType::highShelf: return L("Plateau haut", "High shelf");
    case engine::EqBandType::lowCut: return L("Coupe-bas", "Low cut");
    case engine::EqBandType::highCut: return L("Coupe-haut", "High cut");
    case engine::EqBandType::notch: return L("Réjecteur", "Notch");
    case engine::EqBandType::bandPass: return L("Passe-bande", "Band pass");
    case engine::EqBandType::tilt: return L("Inclinaison", "Tilt");
    }
    return "";
}

const char* slopeName(const int slope)
{
    static const char* names[] { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
    return names[std::clamp(slope, 0, engine::eqSlopeCount - 1)];
}

// The note nearest a frequency: "A4", "C#2 +12 ct".
juce::String noteOf(const double hz)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto midi = 69.0 + 12.0 * std::log2(std::max(hz, 1.0) / 440.0);
    const auto nearest = static_cast<int>(std::lround(midi));
    const auto cents = static_cast<int>(std::lround((midi - nearest) * 100.0));
    auto text = juce::String(names[((nearest % 12) + 12) % 12])
                + juce::String(static_cast<int>(std::floor(nearest / 12.0)) - 1);
    if (cents != 0)
        text << (cents > 0 ? " +" : " ") << cents << " ct";
    return text;
}

// Which of a band's values a binding is ("node.4.eqpro.b3Gain" → gain),
// or -1.
int eqProValueOf(const juce::String& binding)
{
    const auto at = binding.lastIndexOf("eqpro.b");
    if (at < 0)
        return -1;
    const auto name = binding.substring(at + 7).trimCharactersAtStart("0123456789");
    static const char* names[] { "Enabled", "Type", "Frequency", "Gain", "Q", "Slope" };
    for (auto index = 0; index < static_cast<int>(std::size(names)); ++index)
        if (name == names[index])
            return index;
    return -1;
}

// Which of a band's dynamics a binding is ("node.4.eqpro.b3Range" →
// range), or -1.
int eqProDynamicOf(const juce::String& binding)
{
    const auto at = binding.lastIndexOf("eqpro.b");
    if (at < 0)
        return -1;
    const auto name = binding.substring(at + 7).trimCharactersAtStart("0123456789");
    static const char* names[] { "Dynamic", "Range", "Threshold", "Attack", "Release", "Detector" };
    for (auto index = 0; index < static_cast<int>(std::size(names)); ++index)
        if (name == names[index])
            return index;
    return -1;
}

// Values that only take whole steps: a band's shape, slope and switches,
// what its dynamics listen to.
bool isStepped(const Param& parameter)
{
    const auto value = eqProValueOf(parameter.binding);
    const auto dynamic = eqProDynamicOf(parameter.binding);
    return value == static_cast<int>(engine::EqProValue::enabled) || value == static_cast<int>(engine::EqProValue::type)
           || value == static_cast<int>(engine::EqProValue::slope) || dynamic == static_cast<int>(engine::EqProDynamic::on)
           || dynamic == static_cast<int>(engine::EqProDynamic::detector) || studioStepped(parameter);
}

struct EqBand {
    Param* enabled {};
    Param* type {};
    Param* frequency {};
    Param* gain {};
    Param* q {};
    Param* slope {};
    // Its dynamics, in EqProDynamic order.
    Param* dynamic {};
    Param* range {};
    Param* threshold {};
    Param* attack {};
    Param* release {};
    Param* detector {};

    [[nodiscard]] bool complete() const
    {
        return enabled != nullptr && type != nullptr && frequency != nullptr && gain != nullptr && q != nullptr
               && slope != nullptr;
    }
    [[nodiscard]] bool on() const { return enabled->value >= 0.5; }
    [[nodiscard]] engine::EqBandType shape() const
    {
        return static_cast<engine::EqBandType>(
            std::clamp(static_cast<int>(std::lround(type->value)), 0, engine::eqBandTypeCount - 1));
    }
    [[nodiscard]] int slopeIndex() const
    {
        return std::clamp(static_cast<int>(std::lround(slope->value)), 0, engine::eqSlopeCount - 1);
    }
    [[nodiscard]] bool hasGain() const
    {
        const auto s = shape();
        return s == engine::EqBandType::bell || s == engine::EqBandType::lowShelf || s == engine::EqBandType::highShelf
               || s == engine::EqBandType::tilt;
    }
    [[nodiscard]] bool isCut() const
    {
        return shape() == engine::EqBandType::lowCut || shape() == engine::EqBandType::highCut;
    }
    [[nodiscard]] bool hasDynamics() const
    {
        return dynamic != nullptr && range != nullptr && threshold != nullptr && attack != nullptr && release != nullptr
               && detector != nullptr;
    }
    // Its gain moves with the sound: only a shape with a gain can.
    [[nodiscard]] bool isDynamic() const { return hasDynamics() && dynamic->value >= 0.5 && hasGain(); }
};

struct Eq {
    std::array<EqBand, engine::eqProBands> bands {};
    Param* output {};
    // The band heard alone (0 none, n band n), while a listen is held.
    Param* listen {};
    // The start the device's bindings share, which names its view.
    juce::String key;

    [[nodiscard]] bool valid() const
    {
        return key.isNotEmpty() && std::all_of(bands.begin(), bands.end(), [](const EqBand& band) { return band.complete(); });
    }
};

Eq eqOf(std::vector<Param>& parameters, const Pane& pane)
{
    Eq eq;
    for (auto band = 0; band < engine::eqProBands; ++band) {
        const auto at = [&](const engine::EqProValue value) {
            return parameterAt(parameters, pane, static_cast<std::size_t>(engine::eqProSlot(band, value)));
        };
        auto& item = eq.bands[static_cast<std::size_t>(band)];
        item.enabled = at(engine::EqProValue::enabled);
        item.type = at(engine::EqProValue::type);
        item.frequency = at(engine::EqProValue::frequency);
        item.gain = at(engine::EqProValue::gain);
        item.q = at(engine::EqProValue::q);
        item.slope = at(engine::EqProValue::slope);
        const auto dynamicAt = [&](const engine::EqProDynamic value) {
            return parameterAt(parameters, pane, static_cast<std::size_t>(engine::eqProDynamicIndex(band, value)));
        };
        item.dynamic = dynamicAt(engine::EqProDynamic::on);
        item.range = dynamicAt(engine::EqProDynamic::range);
        item.threshold = dynamicAt(engine::EqProDynamic::threshold);
        item.attack = dynamicAt(engine::EqProDynamic::attack);
        item.release = dynamicAt(engine::EqProDynamic::release);
        item.detector = dynamicAt(engine::EqProDynamic::detector);
    }
    eq.output = parameterAt(parameters, pane, static_cast<std::size_t>(engine::eqProBands * engine::eqProValuesPerBand));
    eq.listen = parameterAt(parameters, pane, static_cast<std::size_t>(engine::eqProBands * engine::eqProValuesPerBand + 1));
    if (eq.bands[0].frequency != nullptr)
        eq.key = eq.bands[0].frequency->binding.upToLastOccurrenceOf("eqpro.", true, false);
    return eq;
}

// What an equaliser's views share between frames, the device panel's and
// the editor window's alike: the band in hand, the scale, where a dragged
// handle is, what a right click was on.
struct EqView {
    int selected { -1 };
    float range { 12.0f };
    ImVec2 anchor {};
    int menuBand { -1 };
    float menuHz { 1'000.0f };
    float menuDb {};
};

EqView& eqView(const juce::String& key)
{
    static std::map<juce::String, EqView> views;
    return views[key];
}

struct BandCurve {
    int stages {};
    std::array<engine::BiquadCoefficients, engine::eqMaximumStages> sections {};

    [[nodiscard]] float at(const float hz) const
    {
        auto db = 0.0;
        for (auto stage = 0; stage < stages; ++stage)
            db += engine::biquadMagnitudeDb(sections[static_cast<std::size_t>(stage)], hz, kRate);
        return static_cast<float>(db);
    }
};

BandCurve curveOf(const engine::EqBandType type, const double frequency, const double gainDb, const double q, const int slope)
{
    BandCurve curve;
    curve.stages = engine::eqBandStages(type, slope);
    for (auto stage = 0; stage < curve.stages; ++stage)
        curve.sections[static_cast<std::size_t>(stage)] = engine::designEqBand(type, frequency, gainDb, q, slope, stage, kRate);
    return curve;
}

// A parameter where a cable or the track's modulation puts it now.
double liveValueOf(const Param& parameter, const Live& live)
{
    if (live.modulated != nullptr)
        if (const auto found = live.modulated->find(parameter.binding); found != live.modulated->end())
            return found->second;
    return parameter.value;
}

bool isModulated(const Param* parameter, const Live& live)
{
    return parameter != nullptr && live.modulated != nullptr && live.modulated->count(parameter->binding) > 0;
}

// How far a band's dynamics move its gain now (dB), and the level its
// detector hears (dB), as the processor shows them; still while the
// transport stands.
float movedDbOf(const Live& live, const int band)
{
    if (!live.running || live.readings == nullptr || live.readingCount < engine::eqProReadingCount)
        return 0.0f;
    const auto value = live.readings[band];
    return std::isfinite(value) ? std::clamp(value, -30.0f, 30.0f) : 0.0f;
}

float detectorDbOf(const Live& live, const int band)
{
    if (!live.running || live.readings == nullptr || live.readingCount < engine::eqProReadingCount)
        return -120.0f;
    const auto value = live.readings[engine::eqProBands + band];
    return std::isfinite(value) ? std::max(value, -120.0f) : -120.0f;
}

// A band's dynamics switched on or off; on without a range, a cut of 6 dB
// to start from.
bool setDynamic(EqBand& band, const bool on, const Setter& set)
{
    if (!band.hasDynamics())
        return false;
    auto changed = apply(*band.dynamic, on ? 1.0 : 0.0, set);
    if (on && std::abs(band.range->value) < 0.05)
        changed |= apply(*band.range, -6.0, set);
    return changed;
}

// The region between two curves over the same frequencies.
void fillBetween(ImDrawList* list, const std::vector<ImVec2>& upper, const std::vector<ImVec2>& lower, const ImU32 colour)
{
    if (upper.size() < 2 || upper.size() != lower.size() || (colour & IM_COL32_A_MASK) == 0)
        return;
    const auto flags = list->Flags;
    list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (std::size_t index = 0; index + 1 < upper.size(); ++index)
        list->AddQuadFilled(upper[index], upper[index + 1], lower[index + 1], lower[index], colour);
    list->Flags = flags;
}

// The region between a curve and a level line it may cross.
void fillToLine(ImDrawList* list, const std::vector<ImVec2>& points, const float lineY, const ImU32 colour)
{
    if (points.size() < 2 || (colour & IM_COL32_A_MASK) == 0)
        return;
    const auto flags = list->Flags;
    list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (std::size_t index = 0; index + 1 < points.size(); ++index) {
        const auto& a = points[index];
        const auto& b = points[index + 1];
        list->AddQuadFilled(ImVec2(a.x, lineY), a, b, ImVec2(b.x, lineY), colour);
    }
    list->Flags = flags;
}

// The frequency and dB grid; the large one has every line of each decade
// and its labels.
void eqGrid(ImDrawList* list, const Plot& plot, const float range, const bool large)
{
    const auto& p = theme::palette();
    if (!large)
        frequencyGrid(list, plot);
    else {
        for (auto decade = 10.0f; decade <= 10'000.0f; decade *= 10.0f)
            for (auto step = 1; step <= 9; ++step) {
                const auto hz = decade * static_cast<float>(step);
                if (hz < kMinimumHz || hz > kMaximumHz)
                    continue;
                const auto x = xOfHz(plot, hz);
                list->AddLine(ImVec2(x, plot.min.y), ImVec2(x, plot.max.y), step == 1 ? p.gridStrong : theme::alpha(p.grid, 0.55f));
            }
        static constexpr std::pair<float, const char*> labels[] { { 20.0f, "20" },    { 50.0f, "50" },    { 100.0f, "100" },
                                                                   { 200.0f, "200" },  { 500.0f, "500" },  { 1'000.0f, "1k" },
                                                                   { 2'000.0f, "2k" }, { 5'000.0f, "5k" }, { 10'000.0f, "10k" },
                                                                   { 20'000.0f, "20k" } };
        for (const auto& [hz, text] : labels) {
            const auto size = smallTextSize(text, 0.7f);
            const auto x = std::clamp(xOfHz(plot, hz) - size.x * 0.5f, plot.min.x + dp(2.0f), plot.max.x - size.x - dp(2.0f));
            smallText(list, ImVec2(x, plot.max.y - size.y - dp(2.0f)), theme::alpha(p.textDim, 0.85f), text, 0.7f);
        }
    }
    const auto step = large ? (range <= 3.0f ? 1.0f : range <= 6.0f ? 2.0f : range <= 12.0f ? 3.0f : 10.0f) : range * 0.5f;
    for (auto db = -range; db <= range + 0.01f; db += step) {
        const auto y = yOfDb(plot, db, range, -range);
        const auto zero = std::abs(db) < 0.01f;
        list->AddLine(ImVec2(plot.min.x, y), ImVec2(plot.max.x, y), zero ? p.gridStrong : p.grid);
        if (std::abs(std::abs(db) - range) < 0.01f && !large)
            continue;
        char text[16];
        std::snprintf(text, sizeof(text), zero ? "0" : "%+.0f", static_cast<double>(db));
        const auto size = smallTextSize(text, 0.66f);
        const auto labelY = std::clamp(y - size.y * 0.5f, plot.min.y + dp(1.0f), plot.max.y - size.y - dp(1.0f));
        smallText(list, ImVec2(plot.max.x - size.x - dp(4.0f), labelY), theme::alpha(p.textDim, large ? 0.85f : 0.6f), text, 0.66f);
    }
}

// The Q a shape starts with: flat for the cuts and shelves, an octave and
// a third for the others.
double defaultQ(const engine::EqBandType type)
{
    switch (type) {
    case engine::EqBandType::lowCut:
    case engine::EqBandType::highCut:
    case engine::EqBandType::lowShelf:
    case engine::EqBandType::highShelf:
    case engine::EqBandType::tilt: return 0.71;
    default: return 1.0;
    }
}

// A band given another shape; a Q still at the old shape's default takes
// the new one's, so a bell turned into a cut does not resonate.
bool setShape(EqBand& band, const engine::EqBandType type, const Setter& set)
{
    const auto before = band.shape();
    auto changed = apply(*band.type, static_cast<double>(type), set);
    if (changed && std::abs(band.q->value - defaultQ(before)) < 1.0e-3)
        changed |= apply(*band.q, defaultQ(type), set);
    return changed;
}

// The first band off, switched on as `type` at a point; its index, or -1
// when all eight are in use.
int addBand(Eq& eq, const engine::EqBandType type, const float hz, const float db, const Setter& set)
{
    for (auto index = 0; index < engine::eqProBands; ++index) {
        auto& band = eq.bands[static_cast<std::size_t>(index)];
        if (band.on())
            continue;
        const auto gain = type == engine::EqBandType::bell || type == engine::EqBandType::lowShelf
                                  || type == engine::EqBandType::highShelf || type == engine::EqBandType::tilt
                              ? db
                              : 0.0f;
        (void) apply(*band.type, static_cast<double>(type), set);
        (void) apply(*band.frequency, hz, set);
        (void) apply(*band.gain, std::abs(gain) < 0.5f ? 0.0f : gain, set);
        (void) apply(*band.q, defaultQ(type), set);
        (void) apply(*band.slope, 1.0, set);
        (void) apply(*band.enabled, 1.0, set);
        return index;
    }
    return -1;
}

// A shape drawn small, for its button.
void shapeGlyph(ImDrawList* list, const ImVec2 min, const ImVec2 max, const engine::EqBandType type, const ImU32 colour)
{
    auto frequency = 1'000.0;
    auto gain = 0.0;
    auto q = 1.0;
    switch (type) {
    case engine::EqBandType::bell: gain = 12.0; break;
    case engine::EqBandType::lowShelf: frequency = 400.0; gain = 12.0; q = 0.71; break;
    case engine::EqBandType::highShelf: frequency = 2'500.0; gain = 12.0; q = 0.71; break;
    case engine::EqBandType::lowCut: frequency = 600.0; q = 0.71; break;
    case engine::EqBandType::highCut: frequency = 1'600.0; q = 0.71; break;
    case engine::EqBandType::notch: q = 1.5; break;
    case engine::EqBandType::bandPass: q = 0.9; break;
    case engine::EqBandType::tilt: gain = 12.0; break;
    }
    const auto left = min.x + dp(4.0f);
    const auto right = max.x - dp(4.0f);
    const auto middle = (min.y + max.y) * 0.5f + dp(2.0f);
    const auto scale = (max.y - min.y) * 0.5f / 16.0f;
    std::vector<ImVec2> points;
    for (auto step = 0; step <= 20; ++step) {
        const auto t = static_cast<float>(step) / 20.0f;
        const auto hz = kMinimumHz * std::pow(kMaximumHz / kMinimumHz, t);
        const auto db = std::clamp(static_cast<float>(engine::eqBandMagnitudeDb(type, frequency, gain, q, 1, hz, kRate)), -18.0f, 14.0f);
        points.emplace_back(left + (right - left) * t, middle - db * scale);
    }
    list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(1.4f));
}

// Right click on a handle: its shape, slope, removal; on the ground: a band
// of a shape added there.
bool eqMenu(Eq& eq, EqView& view, const Setter& set)
{
    if (!ImGui::BeginPopup("##eqMenu"))
        return false;
    auto changed = false;
    if (view.menuBand >= 0 && view.menuBand < engine::eqProBands) {
        auto& band = eq.bands[static_cast<std::size_t>(view.menuBand)];
        ImGui::TextDisabled("%s %d", L("Bande", "Band"), view.menuBand + 1);
        for (auto type = 0; type < engine::eqBandTypeCount; ++type)
            if (ImGui::MenuItem(shapeName(static_cast<engine::EqBandType>(type)), nullptr, static_cast<int>(band.shape()) == type))
                changed |= setShape(band, static_cast<engine::EqBandType>(type), set);
        if (band.isCut()) {
            ImGui::Separator();
            for (auto slope = 0; slope < engine::eqSlopeCount; ++slope)
                if (ImGui::MenuItem(slopeName(slope), nullptr, band.slopeIndex() == slope))
                    changed |= apply(*band.slope, slope, set);
        }
        if (band.hasGain() && band.hasDynamics()) {
            ImGui::Separator();
            if (ImGui::MenuItem(L("Bande dynamique", "Dynamic band"), nullptr, band.isDynamic()))
                changed |= setDynamic(band, !band.isDynamic(), set);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Retirer la bande", "Remove the band"))) {
            changed |= apply(*band.enabled, 0.0, set);
            view.selected = -1;
        }
    } else {
        const auto free = std::any_of(eq.bands.begin(), eq.bands.end(), [](const EqBand& band) { return !band.on(); });
        ImGui::TextDisabled("%s", free ? L("Ajouter une bande ici", "Add a band here") : L("Les 8 bandes sont prises", "All 8 bands are in use"));
        for (auto type = 0; type < engine::eqBandTypeCount; ++type)
            if (ImGui::MenuItem(shapeName(static_cast<engine::EqBandType>(type)), nullptr, false, free))
                if (const auto added = addBand(eq, static_cast<engine::EqBandType>(type), view.menuHz, view.menuDb, set); added >= 0) {
                    view.selected = added;
                    changed = true;
                }
    }
    ImGui::EndPopup();
    return changed;
}

// The parametric equaliser over the spectrum before and after it: each
// band's own curve, their sum, a numbered handle per band. A handle drags
// its frequency and gain (a cut's or a notch's height is its Q), the wheel
// turns its Q, a double click on the ground adds a band and on a handle
// removes it, a right click chooses a shape. `large`: the editor window's,
// with every grid line and a readout of the mouse.
bool eqProPlot(const Plot& plot, std::vector<Param>& parameters, const Pane& pane, const ImU32 colour, const Live& live,
               const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto eq = eqOf(parameters, pane);
    if (!eq.valid())
        return false;
    auto& view = eqView(eq.key);
    if (view.selected < 0)
        for (auto index = 0; index < engine::eqProBands && view.selected < 0; ++index)
            if (eq.bands[static_cast<std::size_t>(index)].on())
                view.selected = index;
    const auto range = view.range;
    const auto yAt = [&](const float db) { return yOfDb(plot, db, range, -range); };
    const auto dbAt = [&](const float y) { return std::clamp(dbOfY(plot, y, range, -range), -30.0f, 30.0f); };

    eqGrid(list, plot, range, large);
    if (live.before != nullptr)
        drawSpectrum(list, plot, *live.before, theme::alpha(p.textDim, 0.38f), theme::alpha(p.textDim, 0.11f));
    if (live.after != nullptr)
        drawSpectrum(list, plot, *live.after, theme::alpha(colour, 0.5f), theme::alpha(colour, 0.13f));

    // The handles, where the bands are set, and each dynamic band's
    // diamond, where its range would take its gain (kept clear of its
    // handle).
    const auto radius = large ? dp(8.5f) : dp(6.0f);
    const auto diamond = radius * 0.8f;
    std::array<ImVec2, engine::eqProBands> handles {};
    std::array<ImVec2, engine::eqProBands> diamonds {};
    const auto place = [&] {
        for (auto index = 0; index < engine::eqProBands; ++index) {
            const auto& band = eq.bands[static_cast<std::size_t>(index)];
            const auto y = band.hasGain() ? yAt(static_cast<float>(band.gain->value)) : yAt(0.0f);
            const auto x = xOfHz(plot, static_cast<float>(band.frequency->value));
            auto& handle = handles[static_cast<std::size_t>(index)];
            handle = ImVec2(x, std::clamp(y, plot.min.y + radius, plot.max.y - radius));
            if (!band.isDynamic())
                continue;
            auto end = yAt(static_cast<float>(band.gain->value + band.range->value));
            const auto clear = radius + diamond + dp(3.0f);
            if (std::abs(end - handle.y) < clear)
                end = handle.y + (band.range->value > 0.0 ? -clear : clear);
            diamonds[static_cast<std::size_t>(index)] = ImVec2(x, std::clamp(end, plot.min.y + diamond, plot.max.y - diamond));
        }
    };
    place();

    // The band in hand's listen: a headphone over its handle, held.
    const auto listenable = view.selected >= 0 && eq.listen != nullptr && eq.bands[static_cast<std::size_t>(view.selected)].on();
    const auto listenSize = large ? dp(9.0f) : dp(7.0f);
    const auto listenAt = listenable ? ImVec2(handles[static_cast<std::size_t>(view.selected)].x,
                                              std::max(plot.min.y + listenSize, handles[static_cast<std::size_t>(view.selected)].y
                                                                                     - radius - listenSize - dp(5.0f)))
                                     : ImVec2();
    const auto onListen = [&](const ImVec2 point) {
        return listenable && std::abs(point.x - listenAt.x) <= listenSize && std::abs(point.y - listenAt.y) <= listenSize;
    };

    const auto interaction = interact("##eqpro", plot);
    auto hit = -1;
    auto diamondHit = -1;
    if (interaction.hovered) {
        auto best = (radius + dp(4.0f)) * (radius + dp(4.0f));
        for (auto index = 0; index < engine::eqProBands; ++index) {
            if (!eq.bands[static_cast<std::size_t>(index)].on())
                continue;
            const auto dx = handles[static_cast<std::size_t>(index)].x - interaction.mouse.x;
            const auto dy = handles[static_cast<std::size_t>(index)].y - interaction.mouse.y;
            if (const auto distance = dx * dx + dy * dy; distance < best) {
                best = distance;
                hit = index;
            }
        }
        if (hit < 0) {
            auto nearest = (diamond + dp(4.0f)) * (diamond + dp(4.0f));
            for (auto index = 0; index < engine::eqProBands; ++index) {
                if (!eq.bands[static_cast<std::size_t>(index)].isDynamic())
                    continue;
                const auto dx = diamonds[static_cast<std::size_t>(index)].x - interaction.mouse.x;
                const auto dy = diamonds[static_cast<std::size_t>(index)].y - interaction.mouse.y;
                if (const auto distance = dx * dx + dy * dy; distance < nearest) {
                    nearest = distance;
                    diamondHit = index;
                }
            }
        }
    }
    auto* storage = ImGui::GetStateStorage();
    const auto grabId = ImGui::GetID("##eqGrab");
    const auto rangeGrabId = ImGui::GetID("##eqRangeGrab");
    const auto listenId = ImGui::GetID("##eqListen");
    auto changed = false;
    if (interaction.activated) {
        // Pressed on the headphone, or Alt on a handle: that band alone,
        // as long as the button is held.
        const auto alone = onListen(interaction.mouse) ? view.selected : ImGui::GetIO().KeyAlt ? hit : -1;
        if (alone >= 0 && eq.listen != nullptr) {
            storage->SetInt(listenId, alone);
            view.selected = alone;
            view.anchor = handles[static_cast<std::size_t>(alone)];
            changed |= apply(*eq.listen, static_cast<double>(alone + 1), set);
            hit = -1;
            diamondHit = -1;
        }
        storage->SetInt(grabId, hit);
        storage->SetInt(rangeGrabId, diamondHit);
        if (hit >= 0) {
            view.selected = hit;
            view.anchor = handles[static_cast<std::size_t>(hit)];
        } else if (diamondHit >= 0) {
            // Where the range truly ends, the diamond kept clear or not.
            const auto& band = eq.bands[static_cast<std::size_t>(diamondHit)];
            view.selected = diamondHit;
            view.anchor = ImVec2(diamonds[static_cast<std::size_t>(diamondHit)].x,
                                 yAt(static_cast<float>(band.gain->value + band.range->value)));
        }
    }
    const auto heardAlone = storage->GetInt(listenId, -1);
    if (heardAlone >= 0 && !interaction.active) {
        storage->SetInt(listenId, -1);
        if (eq.listen != nullptr)
            changed |= apply(*eq.listen, 0.0, set);
    }
    const auto grabbed = interaction.active ? storage->GetInt(grabId, -1) : -1;
    const auto rangeGrabbed = interaction.active ? storage->GetInt(rangeGrabId, -1) : -1;
    // A band heard alone follows the mouse along the frequencies, so it can
    // be searched by ear; the wheel narrows or widens it.
    const auto listening = interaction.active ? storage->GetInt(listenId, -1) : -1;
    if (listening >= 0 && eq.bands[static_cast<std::size_t>(listening)].on() && interaction.delta.x != 0.0f) {
        view.anchor.x += interaction.delta.x * interaction.speed;
        changed |= apply(*eq.bands[static_cast<std::size_t>(listening)].frequency, hzOfX(plot, view.anchor.x), set);
    }
    if (grabbed >= 0 && eq.bands[static_cast<std::size_t>(grabbed)].on()
        && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        const auto scale = interaction.speed;
        view.anchor.x += interaction.delta.x * scale;
        view.anchor.y += interaction.delta.y * scale;
        auto& band = eq.bands[static_cast<std::size_t>(grabbed)];
        changed |= apply(*band.frequency, hzOfX(plot, view.anchor.x), set);
        if (band.hasGain())
            changed |= apply(*band.gain, dbAt(view.anchor.y), set);
        else if (interaction.delta.y != 0.0f)
            changed |= apply(*band.q, band.q->value * std::pow(2.0, -interaction.delta.y * scale / dp(70.0f)), set);
    }
    // A diamond dragged up or down: the range, from the band's gain.
    if (rangeGrabbed >= 0 && eq.bands[static_cast<std::size_t>(rangeGrabbed)].isDynamic() && interaction.delta.y != 0.0f) {
        view.anchor.y += interaction.delta.y * interaction.speed;
        auto& band = eq.bands[static_cast<std::size_t>(rangeGrabbed)];
        changed |= apply(*band.range, std::clamp(static_cast<double>(dbOfY(plot, view.anchor.y, range, -range)) - band.gain->value, -30.0, 30.0),
                         set);
    }
    if (const auto wheelBand = listening >= 0 ? listening : grabbed >= 0 ? grabbed : hit; interaction.wheel != 0.0f && wheelBand >= 0) {
        auto& band = eq.bands[static_cast<std::size_t>(wheelBand)];
        changed |= apply(*band.q, band.q->value * std::pow(1.15, interaction.wheel * interaction.speed), set);
        view.selected = wheelBand;
    }
    if (interaction.doubleClicked && !onListen(interaction.mouse)) {
        if (hit >= 0) {
            changed |= apply(*eq.bands[static_cast<std::size_t>(hit)].enabled, 0.0, set);
            if (view.selected == hit)
                view.selected = -1;
        } else if (diamondHit >= 0) {
            // On a diamond: the band static again.
            changed |= setDynamic(eq.bands[static_cast<std::size_t>(diamondHit)], false, set);
        } else {
            const auto hz = hzOfX(plot, interaction.mouse.x);
            const auto type = hz < 30.0f        ? engine::EqBandType::lowCut
                              : hz > 15'000.0f ? engine::EqBandType::highCut
                                               : engine::EqBandType::bell;
            if (const auto added = addBand(eq, type, hz, dbAt(interaction.mouse.y), set); added >= 0) {
                view.selected = added;
                changed = true;
            }
        }
    }
    if (interaction.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        view.menuBand = hit >= 0 ? hit : diamondHit;
        view.menuHz = hzOfX(plot, interaction.mouse.x);
        view.menuDb = dbAt(interaction.mouse.y);
        if (view.menuBand >= 0)
            view.selected = view.menuBand;
        ImGui::OpenPopup("##eqMenu");
    }
    changed |= eqMenu(eq, view, set);
    // Handles move with what was just set.
    place();

    // Each band's curve where its gain is now (a dynamic band's moved by
    // what it hears, its range shaded from its gain), then their sum with
    // the output.
    const auto steps = std::max(64, static_cast<int>(plot.width() / dp(1.5f)));
    std::vector<float> xs(static_cast<std::size_t>(steps) + 1);
    std::vector<float> hzs(xs.size());
    for (std::size_t step = 0; step < xs.size(); ++step) {
        xs[step] = plot.min.x + plot.width() * static_cast<float>(step) / static_cast<float>(steps);
        hzs[step] = hzOfX(plot, xs[step]);
    }
    const auto zeroY = yAt(0.0f);
    const auto emphasised = grabbed >= 0 ? grabbed : hit >= 0 ? hit : rangeGrabbed >= 0 ? rangeGrabbed : diamondHit >= 0 ? diamondHit : view.selected;
    const auto outputDb = eq.output != nullptr ? static_cast<float>(eq.output->value) : 0.0f;
    std::vector<float> total(xs.size(), outputDb);
    std::vector<ImVec2> points(xs.size());
    std::vector<ImVec2> setPoints(xs.size());
    std::vector<ImVec2> rangePoints(xs.size());
    std::array<float, engine::eqProBands> moved {};
    auto anyOn = false;
    auto anyModulated = isModulated(eq.output, live);
    for (auto index = 0; index < engine::eqProBands; ++index) {
        const auto& band = eq.bands[static_cast<std::size_t>(index)];
        if (!band.on())
            continue;
        anyOn = true;
        anyModulated = anyModulated || isModulated(band.frequency, live) || isModulated(band.gain, live) || isModulated(band.q, live);
        const auto dynamic = band.isDynamic();
        moved[static_cast<std::size_t>(index)] = dynamic ? movedDbOf(live, index) : 0.0f;
        const auto bandColour = kBandColours[index];
        const auto strong = index == emphasised;
        if (dynamic) {
            const auto setCurve = curveOf(band.shape(), band.frequency->value, band.gain->value, band.q->value, band.slopeIndex());
            const auto rangeCurve = curveOf(band.shape(), band.frequency->value, band.gain->value + band.range->value, band.q->value,
                                            band.slopeIndex());
            for (std::size_t step = 0; step < xs.size(); ++step) {
                setPoints[step] = ImVec2(xs[step], yAt(setCurve.at(hzs[step])));
                rangePoints[step] = ImVec2(xs[step], yAt(rangeCurve.at(hzs[step])));
            }
            fillBetween(list, setPoints, rangePoints, theme::alpha(bandColour, strong ? 0.16f : 0.07f));
            list->AddPolyline(rangePoints.data(), static_cast<int>(rangePoints.size()), theme::alpha(bandColour, strong ? 0.55f : 0.25f),
                              dp(1.0f));
        }
        const auto bandCurve = curveOf(band.shape(), band.frequency->value, band.gain->value + moved[static_cast<std::size_t>(index)],
                                       band.q->value, band.slopeIndex());
        for (std::size_t step = 0; step < xs.size(); ++step) {
            const auto db = bandCurve.at(hzs[step]);
            total[step] += db;
            points[step] = ImVec2(xs[step], yAt(db));
        }
        fillToLine(list, points, zeroY, theme::alpha(bandColour, strong ? 0.2f : 0.06f));
        list->AddPolyline(points.data(), static_cast<int>(points.size()), theme::alpha(bandColour, strong ? 0.85f : 0.35f),
                          strong ? dp(1.4f) : dp(1.0f));
    }
    for (std::size_t step = 0; step < xs.size(); ++step)
        points[step] = ImVec2(xs[step], yAt(total[step]));
    fillToLine(list, points, zeroY, theme::alpha(colour, 0.07f));
    list->AddPolyline(points.data(), static_cast<int>(points.size()), theme::mix(colour, p.text, 0.62f), large ? dp(2.4f) : dp(2.0f));
    // Where the modulation puts the sum now.
    if (anyModulated) {
        std::vector<float> modulated(xs.size(), eq.output != nullptr ? static_cast<float>(liveValueOf(*eq.output, live)) : 0.0f);
        for (auto index = 0; index < engine::eqProBands; ++index) {
            const auto& band = eq.bands[static_cast<std::size_t>(index)];
            if (!band.on())
                continue;
            const auto bandCurve = curveOf(band.shape(), std::clamp(liveValueOf(*band.frequency, live), 10.0, 22'000.0),
                                           liveValueOf(*band.gain, live) + moved[static_cast<std::size_t>(index)],
                                           std::max(0.1, liveValueOf(*band.q, live)), band.slopeIndex());
            for (std::size_t step = 0; step < xs.size(); ++step)
                modulated[step] += bandCurve.at(hzs[step]);
        }
        for (std::size_t step = 0; step < xs.size(); ++step)
            points[step] = ImVec2(xs[step], yAt(modulated[step]));
        list->AddPolyline(points.data(), static_cast<int>(points.size()), theme::alpha(p.modulation, 0.9f), dp(1.6f));
    }

    // A dynamic band's line from its handle to its diamond, where its gain
    // is now on it, the diamond.
    for (auto index = 0; index < engine::eqProBands; ++index) {
        const auto& band = eq.bands[static_cast<std::size_t>(index)];
        if (!band.isDynamic())
            continue;
        const auto bandColour = kBandColours[index];
        const auto centre = handles[static_cast<std::size_t>(index)];
        const auto end = diamonds[static_cast<std::size_t>(index)];
        list->AddLine(centre, end, theme::alpha(bandColour, 0.6f), dp(1.4f));
        if (const auto now = moved[static_cast<std::size_t>(index)]; std::abs(now) > 0.05f) {
            const ImVec2 at(centre.x, std::clamp(yAt(static_cast<float>(band.gain->value) + now), plot.min.y + dp(3.0f), plot.max.y - dp(3.0f)));
            list->AddLine(centre, at, bandColour, dp(3.0f));
            list->AddCircleFilled(at, radius * 0.42f, bandColour, 16);
        }
        const auto hot = index == diamondHit || index == rangeGrabbed;
        const auto d = hot ? diamond * 1.15f : diamond;
        const ImVec2 top(end.x, end.y - d);
        const ImVec2 right(end.x + d, end.y);
        const ImVec2 bottom(end.x, end.y + d);
        const ImVec2 left(end.x - d, end.y);
        list->AddQuadFilled(top, right, bottom, left, hot ? theme::alpha(bandColour, 0.9f) : theme::alpha(p.bg0, 0.92f));
        list->AddQuad(top, right, bottom, left, bandColour, dp(1.6f));
    }

    // The handles over it all, numbered.
    for (auto index = 0; index < engine::eqProBands; ++index) {
        if (!eq.bands[static_cast<std::size_t>(index)].on())
            continue;
        const auto centre = handles[static_cast<std::size_t>(index)];
        const auto hot = index == grabbed || index == hit;
        const auto size = hot ? radius * 1.12f : radius;
        if (index == view.selected)
            list->AddCircle(centre, size + dp(2.5f), p.text, 24, dp(1.5f));
        list->AddCircleFilled(centre, size + dp(1.0f), theme::alpha(p.bg0, 0.9f), 24);
        list->AddCircleFilled(centre, size, hot ? theme::brighter(kBandColours[index], 0.2f) : kBandColours[index], 24);
        char number[4];
        std::snprintf(number, sizeof(number), "%d", index + 1);
        const auto scale = large ? 0.78f : 0.6f;
        const auto textSize = smallTextSize(number, scale);
        smallText(list, ImVec2(centre.x - textSize.x * 0.5f, centre.y - textSize.y * 0.5f), kHandleText, number, scale);
    }
    if (listenable) {
        const auto held = storage->GetInt(listenId, -1) >= 0;
        const auto over = interaction.hovered && onListen(interaction.mouse);
        list->AddCircleFilled(listenAt, listenSize, held ? kBandColours[view.selected] : theme::alpha(p.bg0, over ? 0.95f : 0.75f), 20);
        list->AddCircle(listenAt, listenSize, theme::alpha(kBandColours[view.selected], over || held ? 1.0f : 0.6f), 20, dp(1.2f));
        ImGui::PushFont(nullptr, listenSize * 1.25f);
        ui::centredText(list, ImVec2(listenAt.x - listenSize, listenAt.y - listenSize), ImVec2(listenAt.x + listenSize, listenAt.y + listenSize),
                        held ? kHandleText : p.text, icon::headphone);
        ImGui::PopFont();
        if (over && !held)
            ImGui::SetTooltip("%s", L("Maintenir pour écouter cette bande seule (ou Alt + clic maintenu sur une bande) : en écoutant, "
                                      "glisser la déplace, la molette la resserre ou l'élargit",
                                      "Hold to hear this band alone (or Alt + click held on a band): while listening, dragging moves "
                                      "it, the wheel narrows or widens it"));
    }
    if (!anyOn)
        ui::centredText(list, plot.min, plot.max, p.textFaint,
                        L("Double-clic pour ajouter une bande", "Double-click to add a band"));

    // What is under the mouse; the band heard alone, while it is.
    if (listening >= 0) {
        const auto& band = eq.bands[static_cast<std::size_t>(listening)];
        auto text = ui::Ls("Bande ", "Band ") + juce::String(listening + 1) + ui::Ls(" seule", " alone") + "\n"
                    + formatValue(*band.frequency, band.frequency->value) + "   " + noteOf(band.frequency->value) + "\nQ "
                    + juce::String(band.q->value, 2) + "\n"
                    + ui::Ls("Glisser : la fréquence \xc2\xb7 molette : la largeur", "Drag: the frequency \xc2\xb7 wheel: the width");
        ImGui::SetTooltip("%s", text.toRawUTF8());
    } else if (const auto shown = grabbed >= 0 ? grabbed : hit; shown >= 0 && !ImGui::IsPopupOpen("##eqMenu")) {
        const auto& band = eq.bands[static_cast<std::size_t>(shown)];
        auto text = ui::Ls("Bande ", "Band ") + juce::String(shown + 1) + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String::fromUTF8(shapeName(band.shape()));
        text << "\n" << formatValue(*band.frequency, band.frequency->value) << "   " << noteOf(band.frequency->value);
        if (band.hasGain())
            text << "\n" << formatValue(*band.gain, band.gain->value);
        text << "\nQ " << juce::String(band.q->value, 2);
        if (band.isCut())
            text << "   " << slopeName(band.slopeIndex());
        if (band.isDynamic()) {
            char dynamics[96];
            std::snprintf(dynamics, sizeof(dynamics), "%s %+.1f dB \xc2\xb7 %s %.1f dB", L("Dynamique : plage", "Dynamic: range"),
                          band.range->value, L("seuil", "threshold"), band.threshold->value);
            text << "\n" << juce::String::fromUTF8(dynamics);
        }
        ImGui::SetTooltip("%s", text.toRawUTF8());
    } else if (const auto ranged = rangeGrabbed >= 0 ? rangeGrabbed : diamondHit; ranged >= 0 && !ImGui::IsPopupOpen("##eqMenu")) {
        const auto& band = eq.bands[static_cast<std::size_t>(ranged)];
        char text[192];
        std::snprintf(text, sizeof(text), "%s %d \xc2\xb7 %s %+.1f dB\n%s", ui::french() ? "Bande" : "Band", ranged + 1,
                      L("plage dynamique", "dynamic range"), band.range->value,
                      L("Glisser : la plage \xc2\xb7 double-clic : bande statique", "Drag: the range \xc2\xb7 double-click: static band"));
        ImGui::SetTooltip("%s", text);
    } else if (interaction.hovered && !large && anyOn && !onListen(interaction.mouse)) {
        const auto* hint = L("Double-clic : ajouter une bande", "Double-click: add a band");
        const auto size = smallTextSize(hint, 0.66f);
        smallText(list, ImVec2(plot.max.x - size.x - dp(6.0f), plot.min.y + dp(4.0f)), p.textDim, hint, 0.66f);
    } else if (interaction.hovered && large) {
        const auto hz = hzOfX(plot, interaction.mouse.x);
        char db[24];
        std::snprintf(db, sizeof(db), "%+.1f dB", static_cast<double>(dbAt(interaction.mouse.y)));
        const auto readout = formatValue(*eq.bands[0].frequency, hz) + "  " + noteOf(hz) + "  " + juce::String(db);
        const auto size = smallTextSize(readout.toRawUTF8(), 0.72f);
        smallText(list, ImVec2(plot.max.x - size.x - dp(8.0f), plot.min.y + dp(5.0f)), p.textDim, readout.toRawUTF8(), 0.72f);
    }
    return changed;
}

// The band in hand's dynamics, a row under the large equaliser's: on or
// off, what its detector hears, its range, threshold, attack and release,
// the level its detector hears against its threshold, and a threshold
// taken from what the band has heard lately.
bool eqDynamicsRow(const ImVec2 origin, const float width, Eq& eq, const int index, const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& band = eq.bands[static_cast<std::size_t>(index)];
    if (!band.hasDynamics() || !band.on())
        return false;
    auto changed = false;
    auto x = origin.x;
    const auto bandColour = kBandColours[index];
    list->AddLine(ImVec2(origin.x, origin.y - dp(7.0f)), ImVec2(origin.x + width, origin.y - dp(7.0f)), theme::alpha(p.line, 0.7f));
    char title[64];
    std::snprintf(title, sizeof(title), "%s %d", L("DYNAMIQUE \xc2\xb7 BANDE", "DYNAMICS \xc2\xb7 BAND"), index + 1);
    smallText(list, ImVec2(x, origin.y), bandColour, title, 0.7f);
    const auto top = origin.y + dp(18.0f);
    if (!band.hasGain()) {
        smallText(list, ImVec2(x, top + dp(6.0f)), p.textDim,
                  L("Une coupe, un réjecteur ou un passe-bande n'a pas de gain : pas de dynamique.",
                    "A cut, a notch or a band-pass has no gain: no dynamics."),
                  0.8f);
        return false;
    }
    const auto dynamic = band.isDynamic();
    ImGui::PushID(2'000 + index);

    // On or off, then what the detector hears.
    ImGui::SetCursorScreenPos(ImVec2(x, top + dp(2.0f)));
    auto on = dynamic;
    if (ui::toggle(L("Dynamique", "Dynamic"), &on, bandColour, ImVec2(dp(118.0f), dp(26.0f)),
                   L("Le gain de la bande bouge avec le son : au-dessus du seuil, il va vers sa plage",
                     "The band's gain moves with the sound: over the threshold, it goes toward its range")))
        changed |= setDynamic(band, on, set);
    const ImVec2 sourceSize(dp(58.0f), dp(22.0f));
    for (auto source = 0; source < 2; ++source) {
        const ImVec2 min(x + static_cast<float>(source) * (sourceSize.x + dp(2.0f)), top + dp(34.0f));
        const ImVec2 max(min.x + sourceSize.x, min.y + sourceSize.y);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(source);
        ImGui::BeginDisabled(!dynamic);
        if (ImGui::InvisibleButton("##detector", sourceSize))
            changed |= apply(*band.detector, static_cast<double>(source), set);
        const auto hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::EndDisabled();
        if (hovered)
            ImGui::SetTooltip("%s", source == 0 ? L("Le détecteur écoute cette bande du son lui-même",
                                                    "The detector hears this band of the sound itself")
                                                : L("Le détecteur écoute cette bande du sidechain (choisi dans l'en-tête de la carte) ; "
                                                    "sans sidechain, la bande elle-même",
                                                    "The detector hears this band of the sidechain (chosen in the card's header); "
                                                    "without one, the band itself"));
        ImGui::PopID();
        const auto current = static_cast<int>(std::lround(band.detector->value)) == source;
        list->AddRectFilled(min, max, current && dynamic ? theme::alpha(bandColour, 0.3f) : hovered && dynamic ? p.bg4 : p.bg3, dp(3.0f));
        if (current && dynamic)
            list->AddRect(min, max, bandColour, dp(3.0f), dp(1.2f));
        ui::centredText(list, min, max, !dynamic ? p.textFaint : current ? p.text : p.textDim,
                        source == 0 ? L("Sa bande", "Its band") : "Sidechain");
    }
    smallText(list, ImVec2(x, top + dp(59.0f)), p.textFaint, L("ce que le détecteur écoute", "what the detector hears"), 0.66f);
    x += dp(136.0f);

    // Range, threshold, attack, release.
    const auto knobAt = [&](Param& parameter, const char* label, const char* format, const char* unit, const int flags) {
        ImGui::SetCursorScreenPos(ImVec2(x, top));
        auto value = static_cast<float>(parameter.value);
        ui::KnobStyle style;
        style.diameter = 46.0f;
        style.label = label;
        style.format = format;
        style.unit = unit;
        style.colour = bandColour;
        style.flags = flags;
        ImGui::PushID(label);
        ImGui::BeginDisabled(!dynamic);
        if (ui::knob("##knob", &value, static_cast<float>(parameter.minimum), static_cast<float>(parameter.maximum),
                     static_cast<float>(parameter.defaultValue), style))
            changed |= apply(parameter, value, set);
        ImGui::EndDisabled();
        ImGui::PopID();
        const auto text = dynamic ? formatValue(parameter, parameter.value) : juce::String::fromUTF8("\xe2\x80\x94");
        const auto size = smallTextSize(text.toRawUTF8(), 0.78f);
        smallText(list, ImVec2(x + (dp(58.0f) - size.x) * 0.5f, top + dp(46.0f) + ImGui::GetFontSize() * 0.95f),
                  dynamic ? p.text : p.textFaint, text.toRawUTF8(), 0.78f);
        x += dp(76.0f);
    };
    knobAt(*band.range, L("Plage", "Range"), "%+.1f", "dB", ui::knobBipolar);
    knobAt(*band.threshold, L("Seuil", "Threshold"), "%.1f", "dB", 0);
    knobAt(*band.attack, L("Attaque", "Attack"), "%.1f", "ms", ui::knobLogarithmic);
    knobAt(*band.release, L("Relâchement", "Release"), "%.0f", "ms", ui::knobLogarithmic);

    // What the detector hears (-60..0 dB), the threshold across it.
    const auto level = detectorDbOf(live, index);
    const auto threshold = static_cast<float>(band.threshold->value);
    const ImVec2 meterMin(x + dp(6.0f), top + dp(2.0f));
    const ImVec2 meterMax(meterMin.x + dp(12.0f), top + dp(58.0f));
    const auto yOfLevel = [&](const float db) {
        return meterMax.y - (std::clamp(db, -60.0f, 0.0f) + 60.0f) / 60.0f * (meterMax.y - meterMin.y);
    };
    list->AddRectFilled(meterMin, meterMax, p.bg3, dp(2.0f));
    if (dynamic && level > -60.0f)
        list->AddRectFilled(ImVec2(meterMin.x, yOfLevel(level)), meterMax, level > threshold ? bandColour : theme::alpha(bandColour, 0.45f),
                            dp(2.0f));
    if (dynamic) {
        const auto thresholdY = yOfLevel(threshold);
        list->AddLine(ImVec2(meterMin.x - dp(3.0f), thresholdY), ImVec2(meterMax.x + dp(3.0f), thresholdY), p.text, dp(1.5f));
    }
    ImGui::SetCursorScreenPos(meterMin);
    ImGui::InvisibleButton("##heard", ImVec2(meterMax.x - meterMin.x, meterMax.y - meterMin.y));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", L("Ce que le détecteur de la bande entend (le trait : son seuil)",
                                  "What the band's detector hears (the line: its threshold)"));
    char heardText[24];
    if (dynamic && level > -100.0f)
        std::snprintf(heardText, sizeof(heardText), "%.0f dB", static_cast<double>(level));
    else
        std::snprintf(heardText, sizeof(heardText), "\xe2\x80\x94");
    const auto heardSize = smallTextSize(heardText, 0.66f);
    smallText(list, ImVec2(meterMin.x + dp(6.0f) - heardSize.x * 0.5f, meterMax.y + dp(4.0f)), p.textDim, heardText, 0.66f);
    x += dp(40.0f);

    // A threshold from what the band heard lately: 6 dB under its loudest
    // of the last few seconds, while the transport runs.
    struct Heard {
        float peak { -120.0f };
        double when {};
    };
    static std::map<juce::String, Heard> heard;
    auto& recent = heard[eq.key + juce::String(index)];
    const auto now = ImGui::GetTime();
    if (level > recent.peak || now - recent.when > 4.0) {
        recent.peak = level;
        recent.when = now;
    }
    ImGui::SetCursorScreenPos(ImVec2(x, top + dp(2.0f)));
    const auto learnable = dynamic && live.running && recent.peak > -60.0f;
    ImGui::BeginDisabled(!learnable);
    if (ImGui::Button(L("Seuil auto", "Auto threshold"), ImVec2(dp(112.0f), dp(26.0f))))
        changed |= apply(*band.threshold, std::clamp(recent.peak - 6.0f, -60.0f, 0.0f), set);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", L("Pendant la lecture : le seuil se place 6 dB sous les crêtes que la bande a eues ces dernières secondes",
                                  "While playing: the threshold goes 6 dB under the peaks the band had in the last seconds"));
    smallText(list, ImVec2(x, top + dp(32.0f)), p.textFaint, L("6 dB sous ses crêtes", "6 dB under its peaks"), 0.66f);
    smallText(list, ImVec2(x, top + dp(32.0f) + ImGui::GetFontSize() * 0.8f), p.textFaint,
              L("(pendant la lecture)", "(while playing)"), 0.66f);
    ImGui::PopID();
    return changed;
}


// Under the large equaliser: the bands as numbered chips, then the band in
// hand (its shape, slope, frequency, gain and Q), then the output and the
// scale; on a second row, the band in hand's dynamics.
bool eqBandPanel(const ImVec2 origin, const float width, Eq& eq, EqView& view, const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    auto x = origin.x;
    const auto y = origin.y;

    // The eight chips: a click picks one, a double click switches it.
    const auto chip = dp(26.0f);
    const auto chipGap = dp(4.0f);
    for (auto index = 0; index < engine::eqProBands; ++index) {
        auto& band = eq.bands[static_cast<std::size_t>(index)];
        const ImVec2 min(x + static_cast<float>(index) * (chip + chipGap), y);
        const ImVec2 max(min.x + chip, min.y + chip);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(index);
        ImGui::InvisibleButton("##chip", ImVec2(chip, chip));
        const auto hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
            view.selected = index;
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            changed |= apply(*band.enabled, band.on() ? 0.0 : 1.0, set);
            view.selected = index;
        }
        if (hovered) {
            auto tip = ui::Ls("Bande ", "Band ") + juce::String(index + 1);
            if (band.on())
                tip << juce::String::fromUTF8(" \xc2\xb7 ") << juce::String::fromUTF8(shapeName(band.shape())) << juce::String::fromUTF8(" \xc2\xb7 ")
                    << formatValue(*band.frequency, band.frequency->value);
            else
                tip << ui::Ls(" (coupée) : double-clic pour l'activer", " (off): double-click to switch it on");
            ImGui::SetTooltip("%s", tip.toRawUTF8());
        }
        ImGui::PopID();
        const auto bandColour = kBandColours[index];
        if (band.on())
            list->AddRectFilled(min, max, hovered ? theme::brighter(bandColour, 0.15f) : bandColour, dp(4.0f));
        else
            list->AddRect(min, max, theme::alpha(bandColour, hovered ? 0.9f : 0.45f), dp(4.0f), dp(1.2f));
        if (index == view.selected)
            list->AddRect(ImVec2(min.x - dp(2.0f), min.y - dp(2.0f)), ImVec2(max.x + dp(2.0f), max.y + dp(2.0f)), p.text, dp(5.0f), dp(1.5f));
        char number[4];
        std::snprintf(number, sizeof(number), "%d", index + 1);
        ui::centredText(list, min, max, band.on() ? kHandleText : theme::alpha(bandColour, 0.8f), number);
    }
    const auto hints = { L("Double-clic sur le fond : ajouter une bande", "Double-click the ground: add a band"),
                         L("Glisser : fréquence et gain · Ctrl : fin · Maj : rapide", "Drag: frequency and gain · Ctrl: fine · Shift: fast"),
                         L("Molette : Q · Clic droit : forme et pente", "Wheel: Q · Right click: shape and slope"),
                         L("Losange : plage d'une bande dynamique", "Diamond: a dynamic band's range") };
    auto hintY = y + chip + dp(9.0f);
    for (const auto* hint : hints) {
        smallText(list, ImVec2(x, hintY), theme::alpha(p.textDim, 0.8f), hint, 0.72f);
        hintY += ImGui::GetFontSize() * 0.86f;
    }
    x += static_cast<float>(engine::eqProBands) * (chip + chipGap) + dp(22.0f);

    // A knob's value (line 0) and what follows it (1), centred under it.
    const auto valueUnder = [&](const float left, const float knobWidth, const juce::String& text, const int line) {
        const auto top = y + dp(46.0f) + ImGui::GetFontSize() * (0.95f + 0.85f * static_cast<float>(line));
        const auto size = smallTextSize(text.toRawUTF8(), 0.78f);
        smallText(list, ImVec2(left + (knobWidth - size.x) * 0.5f, top), line == 0 ? p.text : p.textDim, text.toRawUTF8(), 0.78f);
    };

    // The band in hand.
    if (view.selected >= 0 && view.selected < engine::eqProBands) {
        auto& band = eq.bands[static_cast<std::size_t>(view.selected)];
        const auto bandColour = kBandColours[view.selected];
        ImGui::PushID(1000 + view.selected);
        const ImVec2 shapeSize(dp(38.0f), dp(24.0f));
        const auto gap = dp(3.0f);
        for (auto type = 0; type < engine::eqBandTypeCount; ++type) {
            const ImVec2 min(x + static_cast<float>(type % 4) * (shapeSize.x + gap), y + static_cast<float>(type / 4) * (shapeSize.y + gap));
            const ImVec2 max(min.x + shapeSize.x, min.y + shapeSize.y);
            ImGui::SetCursorScreenPos(min);
            ImGui::PushID(type);
            if (ImGui::InvisibleButton("##shape", shapeSize))
                changed |= setShape(band, static_cast<engine::EqBandType>(type), set);
            const auto hovered = ImGui::IsItemHovered();
            if (hovered)
                ImGui::SetTooltip("%s", shapeName(static_cast<engine::EqBandType>(type)));
            ImGui::PopID();
            const auto current = static_cast<int>(band.shape()) == type;
            list->AddRectFilled(min, max, current ? theme::alpha(bandColour, 0.3f) : hovered ? p.bg4 : p.bg3, dp(3.0f));
            if (current)
                list->AddRect(min, max, bandColour, dp(3.0f), dp(1.2f));
            shapeGlyph(list, min, max, static_cast<engine::EqBandType>(type), current ? p.text : p.textDim);
        }
        // The cuts' slopes.
        const auto slopeY = y + 2.0f * (shapeSize.y + gap) + dp(2.0f);
        static const char* slopes[] { "12", "24", "36", "48" };
        for (auto slope = 0; slope < engine::eqSlopeCount; ++slope) {
            const ImVec2 min(x + static_cast<float>(slope) * (shapeSize.x + gap), slopeY);
            const ImVec2 max(min.x + shapeSize.x, min.y + dp(20.0f));
            ImGui::SetCursorScreenPos(min);
            ImGui::PushID(100 + slope);
            ImGui::BeginDisabled(!band.isCut());
            if (ImGui::InvisibleButton("##slope", ImVec2(shapeSize.x, dp(20.0f))))
                changed |= apply(*band.slope, slope, set);
            const auto hovered = ImGui::IsItemHovered();
            ImGui::EndDisabled();
            if (hovered)
                ImGui::SetTooltip("%s", slopeName(slope));
            ImGui::PopID();
            const auto current = band.isCut() && band.slopeIndex() == slope;
            list->AddRectFilled(min, max, current ? theme::alpha(bandColour, 0.3f) : hovered ? p.bg4 : p.bg3, dp(3.0f));
            ui::centredText(list, min, max, !band.isCut() ? p.textFaint : current ? p.text : p.textDim, slopes[slope]);
        }
        smallText(list, ImVec2(x, slopeY + dp(23.0f)), p.textFaint, L("pente (dB/oct)", "slope (dB/oct)"), 0.66f);
        x += 4.0f * (shapeSize.x + gap) + dp(18.0f);

        // Frequency, gain, Q.
        const auto knobAt = [&](Param& parameter, const char* label, const char* format, const char* unit, const int flags,
                                const bool enabled) {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            auto value = static_cast<float>(parameter.value);
            ui::KnobStyle style;
            style.diameter = 46.0f;
            style.label = label;
            style.format = format;
            style.unit = unit;
            style.colour = bandColour;
            style.flags = flags;
            ImGui::PushID(label);
            ImGui::BeginDisabled(!enabled);
            if (ui::knob("##knob", &value, static_cast<float>(parameter.minimum), static_cast<float>(parameter.maximum),
                         static_cast<float>(parameter.defaultValue), style))
                changed |= apply(parameter, value, set);
            ImGui::EndDisabled();
            ImGui::PopID();
            // The value under the name, always; a click on it types it in.
            valueUnder(x, dp(58.0f), enabled ? formatValue(parameter, parameter.value) : juce::String::fromUTF8("\xe2\x80\x94"), 0);
            if (enabled) {
                ImGui::PushID(label);
                changed |= typedValue("##typed", parameter, ImVec2(x, y + dp(46.0f) + ImGui::GetFontSize() * 0.95f),
                                      ImVec2(dp(58.0f), ImGui::GetFontSize() * 0.85f), set);
                ImGui::PopID();
            }
            x += dp(76.0f);
        };
        knobAt(*band.frequency, L("Fréquence", "Frequency"), "%.0f", "Hz", ui::knobLogarithmic, true);
        valueUnder(x - dp(76.0f), dp(58.0f), noteOf(band.frequency->value), 1);
        knobAt(*band.gain, "Gain", "%.1f", "dB", ui::knobBipolar, band.hasGain());
        knobAt(*band.q, "Q", "%.2f", "", ui::knobLogarithmic, true);

        // On, off, away.
        ImGui::SetCursorScreenPos(ImVec2(x, y + dp(2.0f)));
        auto on = band.on();
        if (ui::toggle(L("Active", "On"), &on, bandColour, ImVec2(dp(78.0f), dp(26.0f))))
            changed |= apply(*band.enabled, on ? 1.0 : 0.0, set);
        ImGui::SetCursorScreenPos(ImVec2(x, y + dp(34.0f)));
        if (ui::iconButton(icon::trash, L("Retirer la bande", "Remove the band"), false, 0, ImVec2(dp(78.0f), dp(26.0f)))) {
            changed |= apply(*band.enabled, 0.0, set);
            view.selected = -1;
        }
        // Held: this band alone.
        if (eq.listen != nullptr && band.on()) {
            ImGui::SetCursorScreenPos(ImVec2(x + dp(86.0f), y + dp(2.0f)));
            const auto alone = static_cast<int>(std::lround(eq.listen->value)) == view.selected + 1;
            (void) ui::iconButton(icon::headphone,
                                  L("Maintenir pour écouter cette bande seule : en écoutant, glisser à gauche ou à droite la déplace, "
                                    "la molette la resserre ou l'élargit",
                                    "Hold to hear this band alone: while listening, dragging left or right moves it, the wheel "
                                    "narrows or widens it"),
                                  alone, bandColour, ImVec2(dp(40.0f), dp(58.0f)));
            if (ImGui::IsItemActivated())
                changed |= apply(*eq.listen, static_cast<double>(view.selected + 1), set);
            if (ImGui::IsItemActive()) {
                // Held: the band follows the mouse (an octave every 60
                // points), the wheel turns its width.
                auto& io = ImGui::GetIO();
                if (io.MouseDelta.x != 0.0f)
                    changed |= apply(*band.frequency,
                                     band.frequency->value * std::pow(2.0, io.MouseDelta.x * ui::valueSpeed() / dp(60.0f)), set);
                if (io.MouseWheel != 0.0f) {
                    changed |= apply(*band.q, band.q->value * std::pow(1.15, io.MouseWheel * ui::valueSpeed()), set);
                    ui::takeWheel();
                }
            }
            if (ImGui::IsItemDeactivated())
                changed |= apply(*eq.listen, 0.0, set);
            x += dp(50.0f);
        }
        ImGui::PopID();
        x += dp(96.0f);
    } else {
        smallText(list, ImVec2(x, y + dp(6.0f)), p.textDim,
                  L("Choisissez une bande, ou double-cliquez", "Pick a band, or double-click"), 0.8f);
        smallText(list, ImVec2(x, y + dp(6.0f) + ImGui::GetFontSize()), p.textDim,
                  L("sur la courbe pour en ajouter une.", "the curve to add one."), 0.8f);
        x += dp(300.0f);
    }

    // The output and the scale, at the right end.
    const auto right = std::max(x + dp(150.0f), origin.x + width);
    if (eq.output != nullptr) {
        ImGui::SetCursorScreenPos(ImVec2(right - dp(170.0f), y));
        auto value = static_cast<float>(eq.output->value);
        ui::KnobStyle style;
        style.diameter = 46.0f;
        style.label = L("Sortie", "Output");
        style.format = "%.1f";
        style.unit = "dB";
        style.flags = ui::knobBipolar;
        if (ui::knob("##output", &value, static_cast<float>(eq.output->minimum), static_cast<float>(eq.output->maximum),
                     static_cast<float>(eq.output->defaultValue), style))
            changed |= apply(*eq.output, value, set);
        valueUnder(right - dp(170.0f), dp(58.0f), formatValue(*eq.output, eq.output->value), 0);
    }
    static constexpr float ranges[] { 3.0f, 6.0f, 12.0f, 30.0f };
    const ImVec2 rangeSize(dp(36.0f), dp(22.0f));
    for (auto index = 0; index < 4; ++index) {
        const ImVec2 min(right - dp(80.0f) + static_cast<float>(index % 2) * (rangeSize.x + dp(3.0f)),
                         y + static_cast<float>(index / 2) * (rangeSize.y + dp(3.0f)));
        const ImVec2 max(min.x + rangeSize.x, min.y + rangeSize.y);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(200 + index);
        if (ImGui::InvisibleButton("##range", rangeSize))
            view.range = ranges[index];
        const auto hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetTooltip("%s", L("Échelle de l'affichage", "Display scale"));
        ImGui::PopID();
        const auto current = std::abs(view.range - ranges[index]) < 0.01f;
        list->AddRectFilled(min, max, current ? p.accentDim : hovered ? p.bg4 : p.bg3, dp(3.0f));
        char text[8];
        std::snprintf(text, sizeof(text), "\xc2\xb1%.0f", static_cast<double>(ranges[index]));
        ui::centredText(list, min, max, current ? p.text : p.textDim, text);
    }
    smallText(list, ImVec2(right - dp(80.0f), y + 2.0f * (rangeSize.y + dp(3.0f)) + dp(2.0f)), p.textFaint,
              L("échelle (dB)", "scale (dB)"), 0.66f);

    // The band in hand's dynamics.
    if (view.selected >= 0 && view.selected < engine::eqProBands)
        changed |= eqDynamicsRow(ImVec2(origin.x, y + dp(110.0f)), width, eq, view.selected, live, set);
    return changed;
}

} // namespace

// ---- The studio compressor ----------------------------------------------------------------------

namespace {

struct Compressor {
    Param* threshold {};
    Param* ratio {};
    Param* knee {};
    Param* attack {};
    Param* release {};
    Param* makeup {};
    Param* mix {};
    Param* sidechain {};

    [[nodiscard]] bool valid() const { return threshold != nullptr && ratio != nullptr && knee != nullptr; }
    [[nodiscard]] float makeupDb() const { return makeup != nullptr ? static_cast<float>(makeup->value) : 0.0f; }
};

Compressor compressorOf(std::vector<Param>& parameters, const Pane& pane)
{
    Compressor c;
    Param** slots[] { &c.threshold, &c.ratio, &c.knee, &c.attack, &c.release, &c.makeup, &c.mix, &c.sidechain };
    for (std::size_t slot = 0; slot < std::size(slots); ++slot)
        *slots[slot] = parameterAt(parameters, pane, slot);
    return c;
}

// The studio compressor: the levels in (grey) and out (colour) scrolling by
// with the gain taken off them (red, from the top), the threshold across
// them to drag up and down; a gain reduction meter; the transfer curve with
// its knee, to drag (threshold sideways, ratio up and down, the wheel for
// the knee). `large`: the editor window's, with its scales written.
bool compProPlot(const Plot& plot, std::vector<Param>& parameters, const Pane& pane, const ImU32 colour, const Live& live,
                 const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto c = compressorOf(parameters, pane);
    if (!c.valid())
        return false;
    auto changed = false;
    const auto side = std::min(plot.height() - (large ? dp(8.0f) : 0.0f), plot.width() * (large ? 0.3f : 0.36f));
    const auto top = plot.min.y + (plot.height() - side) * 0.5f;
    const Plot transfer { ImVec2(plot.max.x - side, top), ImVec2(plot.max.x, top + side) };
    const auto meterWidth = large ? dp(16.0f) : dp(8.0f);
    const Plot meter { ImVec2(transfer.min.x - dp(8.0f) - meterWidth, plot.min.y + dp(large ? 18.0f : 14.0f)),
                       ImVec2(transfer.min.x - dp(8.0f), plot.max.y - dp(4.0f)) };
    const Plot history { ImVec2(plot.min.x, plot.min.y + dp(large ? 18.0f : 14.0f)), ImVec2(meter.min.x - dp(8.0f), plot.max.y - dp(4.0f)) };
    constexpr float floorDb = -60.0f;
    // The history draws the reduction from the top at the levels' scale, so
    // it stays clear of them; the meter beside it zooms on 24 dB.
    constexpr float reductionRange = 24.0f;
    const auto makeup = c.makeupDb();

    // ---- The history.
    const auto yLevel = [&](const float db) {
        return history.max.y - std::clamp((db - floorDb) / -floorDb, 0.0f, 1.0f) * history.height();
    };
    const auto yReduction = [&](const float db) {
        return history.min.y + std::clamp(db / floorDb, 0.0f, 1.0f) * history.height();
    };
    list->AddRectFilled(history.min, history.max, theme::alpha(p.bg0, 0.55f), dp(3.0f));
    for (const auto db : { -6.0f, -12.0f, -24.0f, -36.0f, -48.0f }) {
        list->AddLine(ImVec2(history.min.x, yLevel(db)), ImVec2(history.max.x, yLevel(db)), p.grid);
        if (large) {
            char text[8];
            std::snprintf(text, sizeof(text), "%.0f", static_cast<double>(db));
            smallText(list, ImVec2(history.min.x + dp(3.0f), yLevel(db) - ImGui::GetFontSize() * 0.36f), p.textFaint, text, 0.62f);
        }
    }
    const auto t = static_cast<float>(c.threshold->value);
    const auto r = std::max(1.0f, static_cast<float>(c.ratio->value));
    const auto w = std::max(0.0f, static_cast<float>(c.knee->value));
    // The knee, a band about the threshold.
    if (w > 0.1f)
        list->AddRectFilled(ImVec2(history.min.x, yLevel(t + w * 0.5f)), ImVec2(history.max.x, yLevel(t - w * 0.5f)),
                            theme::alpha(colour, 0.07f));
    if (live.inputHistory != nullptr && live.historyCount > 1) {
        const auto step = history.width() / static_cast<float>(live.historyCount - 1);
        std::vector<ImVec2> inputs;
        std::vector<ImVec2> outputs;
        std::vector<ImVec2> reductions;
        // Through a silence the reduction is held where it was: there is
        // nothing to measure it on.
        auto held = 0.0f;
        for (auto index = 0; index < live.historyCount; ++index) {
            const auto x = history.min.x + step * static_cast<float>(index);
            inputs.emplace_back(x, yLevel(live.inputHistory[index]));
            if (live.outputHistory != nullptr)
                outputs.emplace_back(x, yLevel(live.outputHistory[index]));
            const auto difference = live.reductionHistory != nullptr ? live.reductionHistory[index] : 0.0f;
            if (live.inputHistory[index] > -100.0f)
                held = std::min(0.0f, difference - makeup);
            reductions.emplace_back(x, yReduction(held));
        }
        // Filled column by column: these curves sit on their base line where
        // nothing plays, which the polygon fill does not take well.
        fillToLine(list, inputs, history.max.y, theme::alpha(p.textDim, 0.13f));
        list->AddPolyline(inputs.data(), static_cast<int>(inputs.size()), theme::alpha(p.textDim, 0.5f), dp(1.0f));
        if (!outputs.empty()) {
            fillToLine(list, outputs, history.max.y, theme::alpha(colour, 0.2f));
            list->AddPolyline(outputs.data(), static_cast<int>(outputs.size()), theme::alpha(colour, 0.85f), dp(1.2f));
        }
        fillToLine(list, reductions, history.min.y, theme::alpha(p.record, 0.09f));
        list->AddPolyline(reductions.data(), static_cast<int>(reductions.size()), p.record, dp(1.6f));
    }
    if (!live.running)
        ui::centredText(list, history.min, history.max, p.textFaint, L("lecture arrêtée", "not playing"));
    // The threshold, dragged up and down; the wheel turns the ratio.
    {
        const auto y = yLevel(t);
        for (auto x = history.min.x; x < history.max.x; x += dp(7.0f))
            list->AddLine(ImVec2(x, y), ImVec2(std::min(history.max.x, x + dp(4.0f)), y), colour, dp(1.4f));
        const auto interaction = interact("##compHistory", history);
        if (interaction.active && interaction.delta.y != 0.0f)
            changed |= apply(*c.threshold,
                             c.threshold->value
                                 - interaction.delta.y * interaction.speed * -floorDb / std::max(1.0f, history.height()),
                             set);
        if (interaction.wheel != 0.0f)
            changed |= apply(*c.ratio, c.ratio->value * std::pow(1.1, interaction.wheel), set);
        if (interaction.doubleClicked)
            changed |= apply(*c.threshold, c.threshold->defaultValue, set);
        if (interaction.hovered || interaction.active) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            const auto text = ui::Ls("Seuil ", "Threshold ") + formatValue(*c.threshold, c.threshold->value);
            smallText(list, ImVec2(history.min.x + dp(large ? 26.0f : 4.0f), y - ImGui::GetFontSize() * 0.95f), colour,
                      text.toRawUTF8(), 0.72f);
        }
    }

    // ---- The gain reduction meter.
    const auto current = live.running ? std::min(0.0f, live.outputDb - live.inputDb - makeup) : 0.0f;
    list->AddRectFilled(meter.min, meter.max, theme::alpha(p.bg0, 0.7f), dp(2.0f));
    list->AddRectFilled(meter.min, ImVec2(meter.max.x, meter.min.y + std::clamp(-current / reductionRange, 0.0f, 1.0f) * meter.height()),
                        p.record, dp(2.0f));
    if (large)
        for (const auto db : { -3.0f, -6.0f, -12.0f, -18.0f }) {
            const auto y = meter.min.y + -db / reductionRange * meter.height();
            list->AddLine(ImVec2(meter.min.x, y), ImVec2(meter.max.x, y), theme::alpha(p.text, 0.25f));
            char text[8];
            std::snprintf(text, sizeof(text), "%.0f", static_cast<double>(db));
            const auto size = smallTextSize(text, 0.6f);
            smallText(list, ImVec2(meter.min.x - size.x - dp(2.0f), y - size.y * 0.5f), p.textFaint, text, 0.6f);
        }
    char reductionText[32];
    std::snprintf(reductionText, sizeof(reductionText), "GR %.1f dB", static_cast<double>(current));
    const auto reductionSize = smallTextSize(reductionText, large ? 0.8f : 0.66f);
    smallText(list, ImVec2(meter.max.x - reductionSize.x, plot.min.y + dp(3.0f)), current < -0.5f ? p.record : p.textFaint,
              reductionText, large ? 0.8f : 0.66f);

    // ---- The transfer curve.
    const auto xOf = [&](const float db) { return transfer.min.x + (db - floorDb) / -floorDb * transfer.width(); };
    const auto yOf = [&](const float db) { return transfer.max.y - std::clamp((db - floorDb) / -floorDb, 0.0f, 1.0f) * transfer.height(); };
    list->AddRectFilled(transfer.min, transfer.max, theme::alpha(p.bg0, 0.55f), dp(3.0f));
    for (const auto db : { -48.0f, -36.0f, -24.0f, -12.0f }) {
        list->AddLine(ImVec2(xOf(db), transfer.min.y), ImVec2(xOf(db), transfer.max.y), p.grid);
        list->AddLine(ImVec2(transfer.min.x, yOf(db)), ImVec2(transfer.max.x, yOf(db)), p.grid);
    }
    list->AddLine(ImVec2(xOf(floorDb), yOf(floorDb)), ImVec2(xOf(0.0f), yOf(0.0f)), p.gridStrong);
    if (w > 0.1f)
        list->AddRectFilled(ImVec2(xOf(t - w * 0.5f), transfer.min.y), ImVec2(xOf(t + w * 0.5f), transfer.max.y),
                            theme::alpha(colour, 0.08f));
    const auto interaction = interact("##compTransfer", transfer);
    if (interaction.active && (interaction.delta.x != 0.0f || interaction.delta.y != 0.0f)) {
        if (interaction.delta.x != 0.0f)
            changed |= apply(*c.threshold,
                             c.threshold->value
                                 + interaction.delta.x * interaction.speed * -floorDb / std::max(1.0f, transfer.width()),
                             set);
        if (interaction.delta.y != 0.0f)
            changed |= apply(*c.ratio, c.ratio->value * std::pow(1.015, -interaction.delta.y * interaction.speed / ImGui::GetStyle().FontScaleDpi), set);
    }
    if (interaction.wheel != 0.0f)
        changed |= apply(*c.knee, c.knee->value + interaction.wheel * interaction.speed, set);
    if (interaction.doubleClicked) {
        changed |= apply(*c.threshold, c.threshold->defaultValue, set);
        changed |= apply(*c.ratio, c.ratio->defaultValue, set);
        changed |= apply(*c.knee, c.knee->defaultValue, set);
    }
    const auto threshold = static_cast<float>(c.threshold->value);
    const auto ratio = std::max(1.0f, static_cast<float>(c.ratio->value));
    // The style softens the knee; the range bounds the reduction.
    const auto* styleValue = parameterAt(parameters, pane, 8);
    const auto* rangeValue = parameterAt(parameters, pane, 9);
    const auto style = engine::pro::compressorStyles[static_cast<std::size_t>(
        std::clamp(styleValue != nullptr ? static_cast<int>(std::lround(styleValue->value)) : 0, 0, engine::pro::compressorStyleCount - 1))];
    const auto knee = std::max(0.0f, static_cast<float>(c.knee->value)) + static_cast<float>(style.kneeExtra);
    const auto range = rangeValue != nullptr ? rangeValue->value : 60.0;
    const auto outOf = [&](const float in) {
        return in + static_cast<float>(engine::pro::compressorGainDb(in, threshold, ratio, knee, range));
    };
    std::vector<ImVec2> points;
    const auto steps = std::max(60, static_cast<int>(transfer.width() / dp(2.0f)));
    for (auto step = 0; step <= steps; ++step) {
        const auto in = floorDb + -floorDb * static_cast<float>(step) / static_cast<float>(steps);
        points.emplace_back(xOf(in), yOf(outOf(in)));
    }
    curve(list, points, colour, dp(2.0f), theme::alpha(colour, 0.12f), transfer.max.y);
    handle(list, ImVec2(xOf(threshold), yOf(outOf(threshold))), colour, interaction.hovered || interaction.active);
    if (live.running && live.inputDb > floorDb) {
        const auto in = std::min(0.0f, live.inputDb);
        list->AddCircleFilled(ImVec2(xOf(in), yOf(outOf(in))), dp(3.5f), p.text, 12);
    }
    if (large) {
        char text[40];
        std::snprintf(text, sizeof(text), "%.1f:1", static_cast<double>(ratio));
        smallText(list, ImVec2(transfer.min.x + dp(4.0f), transfer.min.y + dp(3.0f)), p.textDim, text, 0.8f);
    }
    if (interaction.hovered || interaction.active)
        ImGui::SetTooltip("%s %s\n%s %s\n%s %s\n%s", L("Seuil", "Threshold"), formatValue(*c.threshold, threshold).toRawUTF8(),
                          L("Ratio", "Ratio"), formatValue(*c.ratio, ratio).toRawUTF8(), L("Genou", "Knee"),
                          formatValue(*c.knee, knee).toRawUTF8(),
                          L("Glisser : seuil (côté), ratio (haut/bas) ; molette : genou",
                            "Drag: threshold (sideways), ratio (up/down); wheel: knee"));
    return changed;
}

// Under the large compressor: every value as a knob with its value written.
} // namespace

// ---- Formatting and fields ---------------------------------------------------------------------

double positionOf(const Param& parameter, const double value)
{
    return normalised(parameter, value);
}

double valueAtPosition(const Param& parameter, const double position)
{
    return fromNormalised(parameter, position);
}

namespace {

ParameterMenu& parameterMenu()
{
    static ParameterMenu menu;
    return menu;
}

} // namespace

void setParameterMenu(ParameterMenu menu)
{
    parameterMenu() = std::move(menu);
}

void openParameterMenu(const Param& parameter)
{
    if (parameterMenu())
        parameterMenu()(parameter);
}

namespace {

// A number with that many decimals; juce::String(value, 0) would print
// them all.
juce::String fixed(const double value, const int decimals)
{
    return decimals <= 0 ? juce::String(juce::roundToInt(value)) : juce::String(value, decimals);
}

} // namespace

bool typedValue(const char* id, Param& parameter, const ImVec2 min, const ImVec2 size, const Setter& set)
{
    const auto percent = parameter.unit.isEmpty() && parameter.minimum >= 0.0 && parameter.maximum <= 1.0 + 1.0e-9
                         && parameter.maximum > 0.0;
    auto typed = parameter.value;
    ImGui::SetCursorScreenPos(min);
    if (ui::valueEntryButton(id, ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y)), typed, parameter.minimum, parameter.maximum,
                             percent ? 100.0 : 1.0))
        return apply(parameter, typed, set);
    if (ImGui::IsItemHovered())
        ui::tooltip(L("Clic : taper la valeur", "Click: type the value"));
    return false;
}

juce::String formatValue(const Param& parameter, const double value)
{
    if (auto studio = studioFormat(parameter, value); studio.isNotEmpty())
        return studio;
    if (parameter.binding.endsWith(".sync")) {
        if (const auto* division = engine::noteDivisionFor(static_cast<float>(value)))
            return juce::String(std::string(division->name));
        return ui::Ls("Libre", "Free");
    }
    // A voice allocator's playing: three words, as the card says them.
    if (parameter.binding.endsWith("voice.mode")) {
        static const char* modes[] { "Poly", "Mono", "Legato" };
        return modes[std::clamp(static_cast<int>(std::round(value)), 0, 2)];
    }
    if (parameter.binding.endsWith("filter.mode")) {
        static const char* modesFr[] { "Passe-bas", "Passe-haut", "Passe-bande", "Réjecteur" };
        static const char* modesEn[] { "Low-pass", "High-pass", "Band-pass", "Notch" };
        const auto mode = std::clamp(static_cast<int>(std::round(value)), 0, 3);
        return juce::String::fromUTF8(ui::french() ? modesFr[mode] : modesEn[mode]);
    }
    switch (eqProValueOf(parameter.binding)) {
    case static_cast<int>(engine::EqProValue::enabled): return (value >= 0.5 ? ui::Ls("Active", "On") : ui::Ls("Coupée", "Off"));
    case static_cast<int>(engine::EqProValue::type):
        return juce::String::fromUTF8(shapeName(static_cast<engine::EqBandType>(
            std::clamp(static_cast<int>(std::lround(value)), 0, engine::eqBandTypeCount - 1))));
    case static_cast<int>(engine::EqProValue::slope): return slopeName(static_cast<int>(std::lround(value)));
    case static_cast<int>(engine::EqProValue::q): return "Q " + juce::String(value, 2);
    default: break;
    }
    switch (eqProDynamicOf(parameter.binding)) {
    case static_cast<int>(engine::EqProDynamic::on): return (value >= 0.5 ? ui::Ls("Active", "On") : ui::Ls("Coupée", "Off"));
    case static_cast<int>(engine::EqProDynamic::detector): return value >= 0.5 ? juce::String("Sidechain") : ui::Ls("Sa bande", "Its band");
    default: break;
    }
    if (parameter.binding.endsWith("crusher.bits"))
        return juce::String(value, 1) + " bits";
    if (parameter.binding.endsWith("crusher.downsample"))
        return juce::String::fromUTF8("\xc3\x97") + juce::String(value, 1);
    if (parameter.binding.endsWith("polyOscillator.waveform") || parameter.binding.endsWith("lfo.shape")
        || parameter.binding.endsWith("tremolo.shape")) {
        static const char* namesFr[] { "Sinus", "Triangle", "Scie", "Carré" };
        static const char* namesEn[] { "Sine", "Triangle", "Saw", "Square" };
        const auto shape = std::clamp(static_cast<int>(std::round(value)), 0, 3);
        return juce::String::fromUTF8(ui::french() ? namesFr[shape] : namesEn[shape]);
    }
    const auto& unit = parameter.unit;
    if (unit == "Hz")
        return value >= 1'000.0 ? juce::String(value / 1'000.0, value >= 10'000.0 ? 1 : 2) + " kHz"
                                : fixed(value, value >= 100.0 ? 0 : 1) + " Hz";
    if (unit == "ms")
        return value >= 1'000.0 ? juce::String(value / 1'000.0, 2) + " s" : fixed(value, value >= 100.0 ? 0 : 1) + " ms";
    if (unit == "s")
        return value < 1.0 ? fixed(value * 1'000.0, 0) + " ms" : juce::String(value, 2) + " s";
    if (unit == "dB")
        return juce::String(value, 1) + " dB";
    if (unit == ":1")
        return juce::String(value, 1) + ":1";
    if (unit == "Q")
        return "Q " + juce::String(value, 2);
    if (unit.isEmpty() && parameter.minimum >= 0.0 && parameter.maximum <= 1.0 + 1.0e-9 && parameter.maximum > 0.0)
        return juce::String(juce::roundToInt(value * 100.0)) + " %";
    if (unit.isNotEmpty())
        return juce::String(value, 2) + " " + unit;
    return juce::String(value, 2);
}

bool valueField(const char* id, Param& parameter, const float width, const ImU32 colour, const Setter& set)
{
    const auto& p = theme::palette();
    auto& io = ImGui::GetIO();
    const auto height = ImGui::GetFontSize() * 1.75f;
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImGui::InvisibleButton("##field", ImVec2(std::max(1.0f, width), height));
    const auto hovered = ImGui::IsItemHovered();
    const auto active = ImGui::IsItemActive();
    auto changed = false;
    auto t = normalised(parameter, parameter.value);
    const auto stepped = isStepped(parameter);
    auto* storage = ImGui::GetStateStorage();
    const auto fieldId = ImGui::GetItemID();
    const auto gatheredId = ImGui::GetID("##gathered");
    const auto movedId = ImGui::GetID("##moved");
    const auto movement = active ? ui::heldMovement(ImGui::GetID("##anchor")) : ImVec2();
    if (ImGui::IsItemActivated()) {
        storage->SetFloat(gatheredId, static_cast<float>(t));
        storage->SetBool(movedId, false);
    }
    if (active && (movement.x != 0.0f || movement.y != 0.0f)) {
        storage->SetBool(movedId, true);
        const auto speed = 0.0045 * ui::valueSpeed() / std::max(0.5f, ImGui::GetStyle().FontScaleDpi);
        const auto step = (-movement.y + movement.x * 0.5f) * speed;
        if (stepped) {
            // Whole steps: the drag gathers until it reaches the next one.
            const auto gathered = std::clamp(storage->GetFloat(gatheredId, static_cast<float>(t)) + static_cast<float>(step) * 1.6f, 0.0f, 1.0f);
            storage->SetFloat(gatheredId, gathered);
            changed |= apply(parameter, fromNormalised(parameter, gathered), set);
        } else
            changed |= apply(parameter, fromNormalised(parameter, t + step), set);
    }
    if (hovered && io.MouseWheel != 0.0f) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        ui::takeWheel();
        if (stepped)
            changed |= apply(parameter, parameter.value + (io.MouseWheel > 0.0f ? 1.0 : -1.0), set);
        else
            changed |= apply(parameter, fromNormalised(parameter, t + io.MouseWheel * 0.02 * ui::valueSpeed()), set);
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        changed |= apply(parameter, parameter.defaultValue, set);
    // Clicked without a drag: its number typed in (a fraction in percent).
    if (!stepped) {
        const auto percent = parameter.unit.isEmpty() && parameter.minimum >= 0.0 && parameter.maximum <= 1.0 + 1.0e-9
                             && parameter.maximum > 0.0;
        auto typed = parameter.value;
        const auto open = ImGui::IsItemDeactivated() && !storage->GetBool(movedId, false)
                          && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (ui::valueEntry(fieldId, origin, ImVec2(origin.x + width, origin.y + height), typed, parameter.minimum,
                           parameter.maximum, open, percent ? 100.0 : 1.0))
            changed |= apply(parameter, typed, set);
    }
    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && parameterMenu())
        parameterMenu()(parameter);
    auto* list = ImGui::GetWindowDrawList();
    const ImVec2 max(origin.x + width, origin.y + height);
    list->AddRectFilled(origin, max, active ? p.bg4 : hovered ? p.bg3 : p.bg2, dp(3.0f));
    t = normalised(parameter, parameter.value);
    list->AddRectFilled(ImVec2(origin.x + dp(2.0f), max.y - dp(2.5f)),
                        ImVec2(origin.x + dp(2.0f) + static_cast<float>(t) * (width - dp(4.0f)), max.y - dp(1.0f)),
                        theme::alpha(colour, 0.9f));
    list->PushClipRect(origin, max, true);
    smallText(list, ImVec2(origin.x + dp(4.0f), origin.y + dp(1.0f)), p.textFaint, parameter.label.toRawUTF8(), 0.66f);
    const auto text = formatValue(parameter, parameter.value);
    smallText(list, ImVec2(origin.x + dp(4.0f), origin.y + height * 0.44f), p.text, text.toRawUTF8(), 0.8f);
    list->PopClipRect();
    ImGui::PopID();
    return changed;
}

// ---- Choosing the displays ---------------------------------------------------------------------

Surface inferSurface(const std::vector<Param>& parameters, const bool hasSignal, const bool instrument, const juce::String& typeKey)
{
    Surface surface;
    // node.2.node.19004.envelope.attack → prefix "node.2.node.19004.",
    // suffix "envelope.attack".
    struct Group {
        juce::String prefix;
        std::map<juce::String, int> bySuffix;
    };
    std::vector<Group> groups;
    for (auto index = 0; index < static_cast<int>(parameters.size()); ++index) {
        const auto& binding = parameters[static_cast<std::size_t>(index)].binding;
        const auto last = binding.lastIndexOfChar('.');
        const auto previous = last > 0 ? binding.substring(0, last).lastIndexOfChar('.') : -1;
        const auto prefix = previous >= 0 ? binding.substring(0, previous + 1) : juce::String();
        const auto suffix = previous >= 0 ? binding.substring(previous + 1) : binding;
        auto group = std::find_if(groups.begin(), groups.end(), [&prefix](const Group& g) { return g.prefix == prefix; });
        if (group == groups.end()) {
            groups.push_back({ prefix, {} });
            group = std::prev(groups.end());
        }
        group->bySuffix[suffix] = index;
    }
    std::vector<bool> consumed(parameters.size(), false);
    const auto take = [&consumed](const int index) {
        if (index >= 0)
            consumed[static_cast<std::size_t>(index)] = true;
        return index;
    };
    const auto find = [](const Group& group, const char* suffix) {
        const auto found = group.bySuffix.find(suffix);
        return found != group.bySuffix.end() ? found->second : -1;
    };

    Pane equalizer { PaneKind::equalizer, { -1 }, 0 };
    auto equalizerAt = -1;
    for (const auto& group : groups) {
        const auto attack = find(group, "envelope.attack");
        const auto decay = find(group, "envelope.decay");
        const auto sustain = find(group, "envelope.sustain");
        const auto release = find(group, "envelope.release");
        if (attack >= 0 && decay >= 0 && sustain >= 0 && release >= 0)
            surface.panes.push_back({ PaneKind::envelope, { take(attack), take(decay), take(sustain), take(release) }, 0 });
        if (const auto waveform = find(group, "polyOscillator.waveform"); waveform >= 0)
            surface.panes.push_back({ PaneKind::oscillator, { take(waveform), take(find(group, "polyOscillator.level")) }, 0 });
        if (const auto position = find(group, "wavetable.position"); position >= 0)
            surface.panes.push_back({ PaneKind::wavetable, { take(position), take(find(group, "wavetable.level")) }, 0 });
        for (const char* filter : { "lowpass", "voiceFilter" }) {
            const auto cutoff = find(group, (juce::String(filter) + ".cutoff").toRawUTF8());
            if (cutoff >= 0)
                surface.panes.push_back(
                    { PaneKind::filter, { take(cutoff), take(find(group, (juce::String(filter) + ".resonance").toRawUTF8())) }, 0 });
        }
        if (const auto cutoff = find(group, "highpass.cutoff"); cutoff >= 0)
            surface.panes.push_back({ PaneKind::filter, { take(cutoff), -1 }, 1 });
        const auto frequency = find(group, "eqBand.frequency");
        const auto gain = find(group, "eqBand.gain");
        const auto q = find(group, "eqBand.q");
        const auto tone = find(group, "eq.tone");
        if ((frequency >= 0 && gain >= 0 && q >= 0) || tone >= 0) {
            if (equalizerAt < 0) {
                equalizerAt = static_cast<int>(surface.panes.size());
                surface.panes.push_back(equalizer);
            }
            auto& pane = surface.panes[static_cast<std::size_t>(equalizerAt)];
            if (tone >= 0 && pane.parameters[0] < 0)
                pane.parameters[0] = take(tone);
            if (frequency >= 0 && gain >= 0 && q >= 0) {
                pane.parameters.push_back(take(frequency));
                pane.parameters.push_back(take(gain));
                pane.parameters.push_back(take(q));
            }
        }
        const auto threshold = find(group, "compressor.threshold");
        const auto ratio = find(group, "compressor.ratio");
        if (threshold >= 0 && ratio >= 0)
            surface.panes.push_back({ PaneKind::dynamics, { take(threshold), take(ratio) }, 0 });
        if (const auto drive = find(group, "drive.db"); drive >= 0)
            surface.panes.push_back({ PaneKind::saturation, { take(drive) }, 0 });
        if (const auto clip = find(group, "clip.threshold"); clip >= 0)
            surface.panes.push_back({ PaneKind::saturation, { take(clip) }, 1 });
        const auto time = find(group, "delay.time");
        if (time >= 0)
            surface.panes.push_back({ PaneKind::delay, { take(time), take(find(group, "delay.feedback")), take(find(group, "delay.mix")) }, 0 });
        if (const auto room = find(group, "reverb.roomSize"); room >= 0)
            surface.panes.push_back({ PaneKind::reverb, { take(room), take(find(group, "reverb.mix")) }, 0 });
        if (const auto width = find(group, "stereo.width"); width >= 0)
            surface.panes.push_back({ PaneKind::width, { take(width) }, 0 });
        if (const auto cutoff = find(group, "filter.cutoff"); cutoff >= 0)
            surface.panes.push_back({ PaneKind::multiFilter,
                                      { take(cutoff), take(find(group, "filter.resonance")), take(find(group, "filter.mode")) }, 0 });
        if (const auto cutoff = find(group, "autofilter.cutoff"); cutoff >= 0)
            surface.panes.push_back({ PaneKind::autoFilter,
                                      { take(cutoff), take(find(group, "autofilter.resonance")), take(find(group, "autofilter.rate")),
                                        take(find(group, "autofilter.depth")), take(find(group, "autofilter.sync")) },
                                      0 });
        if (const auto low = find(group, "eq3.low"); low >= 0)
            surface.panes.push_back({ PaneKind::eqThree,
                                      { take(low), take(find(group, "eq3.mid")), take(find(group, "eq3.high")),
                                        take(find(group, "eq3.lowFrequency")), take(find(group, "eq3.highFrequency")) },
                                      0 });
        static const char* sweeps[] { "chorus", "flanger", "phaser" };
        for (auto variant = 0; variant < 3; ++variant) {
            const juce::String name(sweeps[variant]);
            if (const auto rate = find(group, (name + ".rate").toRawUTF8()); rate >= 0) {
                const auto third = find(group, (name + (variant == 2 ? ".frequency" : ".delay")).toRawUTF8());
                const auto fourth = find(group, (name + (variant == 0 ? ".width" : ".feedback")).toRawUTF8());
                surface.panes.push_back({ PaneKind::sweep,
                                          { take(rate), take(find(group, (name + ".depth").toRawUTF8())), take(third), take(fourth),
                                            take(find(group, (name + ".mix").toRawUTF8())) },
                                          variant });
            }
        }
        if (const auto rate = find(group, "tremolo.rate"); rate >= 0)
            surface.panes.push_back({ PaneKind::tremolo,
                                      { take(rate), take(find(group, "tremolo.depth")), take(find(group, "tremolo.shape")),
                                        take(find(group, "tremolo.spread")), take(find(group, "tremolo.sync")) },
                                      0 });
        if (const auto bits = find(group, "crusher.bits"); bits >= 0)
            surface.panes.push_back({ PaneKind::crusher, { take(bits), take(find(group, "crusher.downsample")) }, 0 });
        if (const auto ceiling = find(group, "limiter.ceiling"); ceiling >= 0)
            surface.panes.push_back({ PaneKind::limiter,
                                      { take(find(group, "limiter.gain")), take(ceiling), take(find(group, "limiter.release")) }, 0 });
        if (const auto threshold = find(group, "gate.threshold"); threshold >= 0)
            surface.panes.push_back({ PaneKind::gate,
                                      { take(threshold), take(find(group, "gate.attack")), take(find(group, "gate.release")),
                                        take(find(group, "gate.range")) },
                                      0 });
        if (const auto attack = find(group, "transient.attack"); attack >= 0)
            surface.panes.push_back({ PaneKind::transient,
                                      { take(attack), take(find(group, "transient.sustain")), take(find(group, "transient.output")) }, 0 });
        if (const auto time = find(group, "pingpong.time"); time >= 0)
            surface.panes.push_back({ PaneKind::delay,
                                      { take(time), take(find(group, "pingpong.feedback")), take(find(group, "pingpong.mix")),
                                        take(find(group, "pingpong.width")), take(find(group, "pingpong.sync")) },
                                      1 });
        if (const auto threshold = find(group, "comppro.threshold"); threshold >= 0) {
            // The mix and the detector filter stay knobs beside it; the large
            // editor has them too, with the styles and options.
            Pane pane { PaneKind::compPro,
                        { take(threshold), take(find(group, "comppro.ratio")), take(find(group, "comppro.knee")),
                          take(find(group, "comppro.attack")), take(find(group, "comppro.release")), take(find(group, "comppro.makeup")),
                          find(group, "comppro.mix"), find(group, "comppro.sidechain") },
                        0 };
            for (const auto* suffix : { "comppro.style", "comppro.range", "comppro.hold", "comppro.lookahead", "comppro.autoRelease",
                                        "comppro.autoGain", "comppro.sidechainLow", "comppro.link", "comppro.channels",
                                        "comppro.audition" })
                pane.parameters.push_back(take(find(group, suffix)));
            surface.panes.push_back(std::move(pane));
        }
        // The studio effects take all of theirs, in their catalogue's order.
        const auto studio = [&](const char* first, const PaneKind kind, const char* typeKey) {
            const auto* effect = engine::effectForType(typeKey);
            if (find(group, first) < 0 || effect == nullptr)
                return;
            Pane pane { kind, {}, 0 };
            for (const auto& item : effect->parameters)
                pane.parameters.push_back(item.suffix.empty() ? -1 : take(find(group, std::string(item.suffix).c_str())));
            surface.panes.push_back(std::move(pane));
        };
        studio("revpro.space", PaneKind::reverbPro, "reverb-pro");
        studio("limpro.gain", PaneKind::limiterPro, "limiter-pro");
        studio("filtpro.f1Cutoff", PaneKind::filterPro, "filter-pro");
        studio("amp.gain", PaneKind::ampSim, "amp-sim");
        studio("cab.positionA", PaneKind::cabSim, "cab-sim");
        studio("micsim.distance", PaneKind::micSim, "mic-sim");
        studio("pression.depth", PaneKind::pression, "pression");
        studio("fournaise.drive", PaneKind::fournaise, "fournaise");
        studio("echoband.time", PaneKind::echoBande, "echo-bande");
        studio("multiband.threshold1", PaneKind::multibandPro, "multiband-pro");
        studio("velours.depth", PaneKind::velours, "velours");
        studio("patine.noise", PaneKind::patine, "patine");
        studio("sonometre.target", PaneKind::sonometre, "sonometre");
        studio("tranche.gateThreshold", PaneKind::tranche, "tranche");
        studio("finition.gain", PaneKind::finition, "finition");
        studio("halo.mix", PaneKind::halo, "halo");
        studio("vocoder.mix", PaneKind::vocoder, "vocoder");
        studio("deesser.threshold", PaneKind::deEsser, "de-esser");
        studio("tuner.reference", PaneKind::tuner, "tuner");
        studio("harmonie.shiftA", PaneKind::harmonie, "harmonie");
        studio("justesse.speed", PaneKind::justesse, "justesse");
        studio("empreinte.mix", PaneKind::empreinte, "empreinte");
        studio("givre.blur", PaneKind::givre, "givre");
        studio("grains.time", PaneKind::grains, "grains");
        studio("saccade.chance", PaneKind::saccade, "saccade");
        studio("boucleur.feedback", PaneKind::boucleur, "boucleur");
        studio("pompe.depth", PaneKind::pompe, "pompe");
        studio("decaleur.shift", PaneKind::decaleur, "decaleur");
        studio("anneau.frequency", PaneKind::anneau, "anneau");
        studio("rotor.slow", PaneKind::rotor, "rotor");
        studio("resonateurs.note", PaneKind::resonateurs, "resonateurs");
        studio("corps.tune", PaneKind::corps, "corps");
        studio("peigne.frequency", PaneKind::peigne, "peigne");
        studio("utilitaire.gain", PaneKind::utilitaire, "utilitaire");
        studio("stereoscope.zoom", PaneKind::stereoscope, "stereoscope");
        studio("arpege.gate", PaneKind::arpege, "arpege");
        studio("accords.velocity", PaneKind::accords, "accords");
        studio("gamme.transpose", PaneKind::gamme, "gamme");
        studio("echonotes.time", PaneKind::echoNotes, "echo-notes");
        studio("alea.timing", PaneKind::alea, "alea");
        studio("duree.length", PaneKind::duree, "duree");
        studio("sequenceur.swing", PaneKind::sequenceur, "sequenceur");
        studio("roulement.rate", PaneKind::roulement, "roulement");
        if (find(group, "eqpro.b1Frequency") >= 0) {
            Pane pane { PaneKind::eqPro, {}, 0 };
            static const char* values[] { "Enabled", "Type", "Frequency", "Gain", "Q", "Slope" };
            for (auto band = 1; band <= engine::eqProBands; ++band)
                for (const auto* value : values)
                    pane.parameters.push_back(take(find(group, ("eqpro.b" + juce::String(band) + value).toRawUTF8())));
            pane.parameters.push_back(take(find(group, "eqpro.output")));
            pane.parameters.push_back(take(find(group, "eqpro.listen")));
            static const char* dynamics[] { "Dynamic", "Range", "Threshold", "Attack", "Release", "Detector" };
            for (auto band = 1; band <= engine::eqProBands; ++band)
                for (const auto* value : dynamics)
                    pane.parameters.push_back(take(find(group, ("eqpro.b" + juce::String(band) + value).toRawUTF8())));
            surface.panes.push_back(std::move(pane));
        }
        if (const auto floor = find(group, "spectrometer.floor"); floor >= 0)
            surface.panes.push_back({ PaneKind::spectrometer,
                                      { take(floor), take(find(group, "spectrometer.tilt")),
                                        take(find(group, "spectrometer.smoothing")), take(find(group, "spectrometer.hold")),
                                        take(find(group, "spectrometer.history")) },
                                      0 });
        if (const auto rate = find(group, "lfo.rate"); rate >= 0)
            surface.panes.push_back({ PaneKind::lfo,
                                      { take(rate), take(find(group, "lfo.depth")), take(find(group, "lfo.offset")),
                                        take(find(group, "lfo.shape")) },
                                      0 });
    }
    // A plain gain shows what goes in and out of it.
    if (surface.panes.empty() && hasSignal && !instrument && parameters.size() == 1
        && parameters.front().binding.endsWith("gain.db"))
        surface.panes.push_back({ PaneKind::level, { take(0) }, 0 });
    // The analysers: what each is for.
    if (typeKey == "oscilloscope")
        surface.panes.push_back({ PaneKind::scope, {}, 0 });
    if (typeKey == "level-meter")
        surface.panes.push_back({ PaneKind::level, {}, 0 });
    // Every device that sounds shows its sound, if nothing above does.
    const auto showsSignal = std::any_of(surface.panes.begin(), surface.panes.end(), [](const Pane& pane) {
        return pane.kind == PaneKind::filter || pane.kind == PaneKind::equalizer || pane.kind == PaneKind::dynamics
               || pane.kind == PaneKind::saturation || pane.kind == PaneKind::level || pane.kind == PaneKind::spectrum
               || pane.kind == PaneKind::scope
               || pane.kind == PaneKind::multiFilter || pane.kind == PaneKind::autoFilter || pane.kind == PaneKind::eqThree
               || pane.kind == PaneKind::limiter || pane.kind == PaneKind::gate || pane.kind == PaneKind::eqPro
               || pane.kind == PaneKind::compPro || pane.kind == PaneKind::spectrometer || isStudioPane(pane.kind);
    });
    if (hasSignal && !showsSignal)
        surface.panes.push_back({ PaneKind::spectrum, {}, 0 });
    for (auto index = 0; index < static_cast<int>(parameters.size()); ++index)
        if (!consumed[static_cast<std::size_t>(index)])
            surface.knobs.push_back(index);
    return surface;
}

float paneWidth(const Pane& pane, const float height)
{
    auto factor = 1.3f;
    switch (pane.kind) {
    case PaneKind::spectrum: factor = 1.5f; break;
    case PaneKind::filter: factor = 1.45f; break;
    case PaneKind::equalizer: factor = 1.7f; break;
    case PaneKind::dynamics: factor = 1.75f; break;
    case PaneKind::saturation: factor = 1.6f; break;
    case PaneKind::envelope: factor = 1.4f; break;
    case PaneKind::oscillator: factor = 1.05f; break;
    case PaneKind::wavetable: factor = 1.2f; break;
    case PaneKind::delay: factor = 1.4f; break;
    case PaneKind::reverb: factor = 1.25f; break;
    case PaneKind::width: factor = 0.9f; break;
    case PaneKind::level: factor = 0.55f; break;
    case PaneKind::scope: factor = 1.6f; break;
    case PaneKind::lfo: factor = 1.3f; break;
    case PaneKind::multiFilter: factor = 1.45f; break;
    case PaneKind::autoFilter: factor = 1.45f; break;
    case PaneKind::eqThree: factor = 1.6f; break;
    case PaneKind::sweep: factor = 1.3f; break;
    case PaneKind::tremolo: factor = 1.3f; break;
    case PaneKind::crusher: factor = 1.3f; break;
    case PaneKind::limiter: factor = 1.75f; break;
    case PaneKind::gate: factor = 1.75f; break;
    case PaneKind::transient: factor = 1.3f; break;
    case PaneKind::eqPro: factor = 2.5f; break;
    case PaneKind::compPro: factor = 2.5f; break;
    case PaneKind::spectrometer: factor = 2.9f; break;
    case PaneKind::reverbPro: factor = 2.4f; break;
    case PaneKind::limiterPro: factor = 2.4f; break;
    case PaneKind::filterPro: factor = 2.6f; break;
    case PaneKind::ampSim: factor = 2.6f; break;
    case PaneKind::cabSim: factor = 2.6f; break;
    case PaneKind::micSim: factor = 2.4f; break;
    case PaneKind::pression: factor = 2.4f; break;
    case PaneKind::fournaise: factor = 2.4f; break;
    case PaneKind::echoBande: factor = 2.4f; break;
    case PaneKind::multibandPro: factor = 2.5f; break;
    case PaneKind::velours: factor = 2.4f; break;
    case PaneKind::patine: factor = 2.2f; break;
    case PaneKind::sonometre: factor = 2.2f; break;
    case PaneKind::tranche: factor = 2.5f; break;
    case PaneKind::finition: factor = 2.5f; break;
    case PaneKind::halo: factor = 2.3f; break;
    default: factor = addedPaneFactor(pane.kind); break;
    }
    const auto wide = pane.kind == PaneKind::eqPro || pane.kind == PaneKind::compPro || isStudioPane(pane.kind);
    return std::clamp(height * factor, dp(110.0f), dp(pane.kind == PaneKind::spectrometer ? 640.0f : wide ? 460.0f : 380.0f));
}

namespace {

// What a pane draws inside its frame.
bool paneBody(const Pane& pane, std::vector<Param>& parameters, const Plot& plot, const ImU32 colour, const Live& live,
              const Setter& set, int& selectedBand, const bool large)
{
    auto changed = false;
    switch (pane.kind) {
    case PaneKind::spectrum:
        changed = spectrumPlot(plot, colour, live);
        break;
    case PaneKind::filter:
        changed = filterPlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1), pane.variant == 1, colour,
                             live, set);
        break;
    case PaneKind::equalizer:
        changed = equalizerPlot("##equalizer", plot, parameters, pane, colour, live, set, selectedBand);
        break;
    case PaneKind::dynamics:
        changed = dynamicsPlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1), colour, live, set);
        break;
    case PaneKind::saturation:
        changed = saturationPlot(plot, parameterAt(parameters, pane, 0), pane.variant == 1, colour, live, set);
        break;
    case PaneKind::envelope:
        changed = envelopePlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1),
                               parameterAt(parameters, pane, 2), parameterAt(parameters, pane, 3), colour, set);
        break;
    case PaneKind::oscillator:
        changed = oscillatorPlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1), colour, set);
        break;
    case PaneKind::wavetable:
        changed = wavetablePlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1), colour, set);
        break;
    case PaneKind::delay:
        changed = delayPlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1),
                            parameterAt(parameters, pane, 2), colour, live, set, pane.variant == 1, parameterAt(parameters, pane, 4));
        break;
    case PaneKind::multiFilter:
        changed = multiFilterPlot(plot, parameterAt(parameters, pane, 2), parameterAt(parameters, pane, 0),
                                  parameterAt(parameters, pane, 1), colour, live, set);
        break;
    case PaneKind::autoFilter: {
        const auto* depth = parameterAt(parameters, pane, 3);
        changed = multiFilterPlot(plot, nullptr, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1), colour, live,
                                  set, depth != nullptr ? 4.0f * static_cast<float>(depth->value) : 0.0f);
        break;
    }
    case PaneKind::eqThree:
        changed = eqThreePlot(plot, parameters, pane, colour, live, set);
        break;
    case PaneKind::sweep:
        changed = sweepPlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1), parameterAt(parameters, pane, 2),
                            parameterAt(parameters, pane, 3), pane.variant, colour, set);
        break;
    case PaneKind::tremolo:
        changed = tremoloPlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1),
                              parameterAt(parameters, pane, 2), parameterAt(parameters, pane, 3), colour, set,
                              parameterAt(parameters, pane, 4));
        break;
    case PaneKind::crusher:
        changed = crusherPlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1), colour, set);
        break;
    case PaneKind::limiter:
        changed = limiterPlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1), colour, live, set);
        break;
    case PaneKind::gate:
        changed = gatePlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 3), colour, live, set);
        break;
    case PaneKind::transient:
        changed = transientPlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1), colour, set);
        break;
    case PaneKind::reverb:
        changed = reverbPlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1), colour, set);
        break;
    case PaneKind::width:
        changed = widthPlot(plot, parameterAt(parameters, pane, 0), colour, set);
        break;
    case PaneKind::level:
        changed = levelPlot(plot, colour, live);
        break;
    case PaneKind::scope:
        changed = scopePlot(plot, colour, live);
        break;
    case PaneKind::eqPro:
        changed = eqProPlot(plot, parameters, pane, colour, live, set, large);
        break;
    case PaneKind::compPro:
        changed = compProPlot(plot, parameters, pane, colour, live, set, large);
        break;
    case PaneKind::lfo:
        changed = lfoPlot(plot, parameterAt(parameters, pane, 0), parameterAt(parameters, pane, 1),
                          parameterAt(parameters, pane, 2), parameterAt(parameters, pane, 3), colour, live, set);
        break;
    case PaneKind::spectrometer:
        changed = spectrometerPlot(plot, parameters, pane, colour, live, large);
        break;
    case PaneKind::reverbPro:
    case PaneKind::limiterPro:
    case PaneKind::filterPro:
    case PaneKind::ampSim:
    case PaneKind::cabSim:
    case PaneKind::micSim:
    case PaneKind::pression:
    case PaneKind::fournaise:
    case PaneKind::echoBande:
    case PaneKind::multibandPro:
    case PaneKind::velours:
    case PaneKind::patine:
    case PaneKind::sonometre:
    case PaneKind::tranche:
    case PaneKind::finition:
    case PaneKind::halo:
    case PaneKind::vocoder:
    case PaneKind::deEsser:
    case PaneKind::tuner:
    case PaneKind::harmonie:
    case PaneKind::justesse:
    case PaneKind::empreinte:
    case PaneKind::givre:
    case PaneKind::grains:
    case PaneKind::saccade:
    case PaneKind::boucleur:
    case PaneKind::pompe:
    case PaneKind::decaleur:
    case PaneKind::anneau:
    case PaneKind::rotor:
    case PaneKind::resonateurs:
    case PaneKind::corps:
    case PaneKind::peigne:
    case PaneKind::utilitaire:
    case PaneKind::stereoscope:
    case PaneKind::arpege:
    case PaneKind::accords:
    case PaneKind::gamme:
    case PaneKind::echoNotes:
    case PaneKind::alea:
    case PaneKind::duree:
    case PaneKind::sequenceur:
    case PaneKind::roulement:
        changed = studioPane(pane, parameters, plot.min, plot.max, colour, live, set);
        break;
    }
    return changed;
}

// The button after a display's title that opens it large; true when
// clicked.
ImVec2 expandButtonAt(const Plot& plot, const Pane& pane)
{
    return ImVec2(plot.min.x + dp(7.0f) + smallTextSize(paneTitleOf(pane), 0.68f).x, plot.min.y + dp(1.0f));
}

bool expandButton(const Plot& plot, const Pane& pane, bool& hovered)
{
    const auto size = dp(17.0f);
    ImGui::SetCursorScreenPos(expandButtonAt(plot, pane));
    const auto clicked = ImGui::InvisibleButton("##expand", ImVec2(size, size));
    hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetTooltip("%s", L("Ouvrir en grand, dans une fenêtre détachable", "Open large, in a window of its own"));
    return clicked;
}

void drawExpandButton(ImDrawList* list, const Plot& plot, const Pane& pane, const bool hovered, const bool paneHovered)
{
    const auto& p = theme::palette();
    const auto size = dp(17.0f);
    const auto min = expandButtonAt(plot, pane);
    const ImVec2 max(min.x + size, min.y + size);
    if (hovered)
        list->AddRectFilled(min, max, theme::alpha(p.bg4, 0.9f), dp(3.0f));
    const auto fontSize = ImGui::GetFontSize() * 0.72f;
    const auto glyph = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, icon::fullScreen);
    list->AddText(ImGui::GetFont(), fontSize, ImVec2(min.x + (size - glyph.x) * 0.5f, min.y + (size - glyph.y) * 0.5f),
                  hovered ? p.text : theme::alpha(p.textDim, paneHovered ? 0.9f : 0.45f), icon::fullScreen);
}

} // namespace

const char* paneTitle(const Pane& pane)
{
    return paneTitleOf(pane);
}

namespace {

// What a pane edits under it (its slots): all its parameters; for the
// equaliser its tone and the band in hand; for the parametric one the band
// chosen (`eqBand` set to it) and its output; for the studio compressor its
// six main ones; for a studio effect its main ones.
std::vector<int> paneFields(const Pane& pane, std::vector<Param>& parameters, const int selectedBand, int* eqBand)
{
    std::vector<int> fields;
    for (std::size_t slot = 0; slot < pane.parameters.size(); ++slot)
        if (parameterAt(parameters, pane, slot) != nullptr)
            fields.push_back(static_cast<int>(slot));
    if (pane.kind == PaneKind::equalizer) {
        std::vector<int> shown;
        if (parameterAt(parameters, pane, 0) != nullptr)
            shown.push_back(0);
        for (auto offset = 0; offset < 3; ++offset)
            if (parameterAt(parameters, pane, static_cast<std::size_t>(1 + selectedBand * 3 + offset)) != nullptr)
                shown.push_back(1 + selectedBand * 3 + offset);
        fields = shown;
    }
    if (pane.kind == PaneKind::eqPro) {
        std::vector<int> shown;
        if (auto eq = eqOf(parameters, pane); eq.valid()) {
            const auto& view = eqView(eq.key);
            if (view.selected >= 0 && view.selected < engine::eqProBands && eq.bands[static_cast<std::size_t>(view.selected)].on()) {
                if (eqBand != nullptr)
                    *eqBand = view.selected;
                const auto& band = eq.bands[static_cast<std::size_t>(view.selected)];
                const auto slotOf = [&](const engine::EqProValue value) { return engine::eqProSlot(view.selected, value); };
                shown.push_back(slotOf(engine::EqProValue::type));
                shown.push_back(slotOf(engine::EqProValue::frequency));
                if (band.hasGain())
                    shown.push_back(slotOf(engine::EqProValue::gain));
                else if (band.isCut())
                    shown.push_back(slotOf(engine::EqProValue::slope));
                shown.push_back(slotOf(engine::EqProValue::q));
                if (band.isDynamic()) {
                    shown.push_back(engine::eqProDynamicIndex(view.selected, engine::EqProDynamic::range));
                    shown.push_back(engine::eqProDynamicIndex(view.selected, engine::EqProDynamic::threshold));
                }
            }
            if (eq.output != nullptr)
                shown.push_back(engine::eqProBands * engine::eqProValuesPerBand);
        }
        fields = shown;
    }
    if (pane.kind == PaneKind::compPro)
        std::erase_if(fields, [](const int slot) { return slot >= 6; });
    if (isStudioPane(pane.kind)) {
        fields.clear();
        for (const auto slot : studioCardFields(pane, parameters))
            if (parameterAt(parameters, pane, static_cast<std::size_t>(slot)) != nullptr)
                fields.push_back(slot);
    }
    return fields;
}

} // namespace

std::vector<int> cardSlots(const char* id, const Pane& pane, std::vector<Param>& parameters)
{
    // The band in hand, as drawPane keeps it under the same id.
    ImGui::PushID(id);
    const auto selectedBand = ImGui::GetStateStorage()->GetInt(ImGui::GetID("##band"), 0);
    ImGui::PopID();
    return paneFields(pane, parameters, selectedBand, nullptr);
}

bool drawPane(const char* id, const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size,
              const ImU32 colour, const Live& live, const Setter& set, const bool withFields, bool* expand)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    ImGui::PushID(id);
    // The fields this pane edits, under it.
    auto* storage = ImGui::GetStateStorage();
    const auto bandId = ImGui::GetID("##band");
    auto selectedBand = storage->GetInt(bandId, 0);
    auto eqBand = -1;
    auto fields = paneFields(pane, parameters, selectedBand, &eqBand);
    if (!withFields)
        fields.clear();
    const auto fieldHeight = fields.empty() ? 0.0f : ImGui::GetFontSize() * 1.75f + dp(4.0f);
    const Plot frame { position, ImVec2(position.x + size.x, position.y + size.y - fieldHeight) };
    list->AddRectFilled(frame.min, frame.max, theme::mix(p.bg0, p.bg1, 0.35f), dp(4.0f));
    list->AddRect(frame.min, frame.max, theme::alpha(p.line, 0.8f), dp(4.0f));
    const Plot plot { ImVec2(frame.min.x + dp(1.0f), frame.min.y + dp(1.0f)), ImVec2(frame.max.x - dp(1.0f), frame.max.y - dp(1.0f)) };
    // The expand button goes first, so the display under it leaves it the
    // mouse.
    auto expandHovered = false;
    if (expand != nullptr && expandButton(plot, pane, expandHovered))
        *expand = true;
    list->PushClipRect(plot.min, plot.max, true);
    auto changed = paneBody(pane, parameters, plot, colour, live, set, selectedBand, false);
    if (pane.kind == PaneKind::equalizer)
        storage->SetInt(bandId, selectedBand);
    paneTitle(list, plot, paneTitleOf(pane));
    if (expand != nullptr)
        drawExpandButton(list, plot, pane, expandHovered, ImGui::IsMouseHoveringRect(plot.min, plot.max));
    list->PopClipRect();

    if (!fields.empty()) {
        const auto gap = dp(3.0f);
        const auto width = (size.x - gap * static_cast<float>(fields.size() - 1)) / static_cast<float>(fields.size());
        auto x = position.x;
        for (const auto slot : fields) {
            ImGui::SetCursorScreenPos(ImVec2(x, frame.max.y + dp(4.0f)));
            auto* parameter = parameterAt(parameters, pane, static_cast<std::size_t>(slot));
            ImGui::PushID(slot);
            if (eqBand >= 0 && slot != engine::eqProBands * engine::eqProValuesPerBand) {
                // A band's field: its own colour, "Gain" for "Bande 3 · Gain".
                auto shown = *parameter;
                shown.label = parameter->label.fromLastOccurrenceOf(juce::String::fromUTF8("\xc2\xb7 "), false, false);
                if (valueField("##value", shown, width, kBandColours[eqBand], set)) {
                    parameter->value = shown.value;
                    changed = true;
                }
            } else
                changed |= valueField("##value", *parameter, width, colour, set);
            ImGui::PopID();
            x += width + gap;
        }
    }
    ImGui::PopID();
    return changed;
}


namespace {

// The sound's levels beside a display in its window: what goes in and what
// comes out, on a scale.
void levelColumn(const Plot& plot, const ImU32 colour, const Live& live)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(plot.min, plot.max, theme::mix(p.bg0, p.bg1, 0.35f), dp(4.0f));
    list->AddRect(plot.min, plot.max, theme::alpha(p.line, 0.8f), dp(4.0f));
    smallText(list, ImVec2(plot.min.x + dp(6.0f), plot.min.y + dp(4.0f)), theme::alpha(p.textDim, 0.85f), L("NIVEAUX", "LEVELS"), 0.64f);
    const auto top = plot.min.y + dp(26.0f);
    const auto bottom = plot.max.y - dp(40.0f);
    if (bottom - top < dp(30.0f))
        return;
    const auto barWidth = std::min(dp(16.0f), plot.width() * 0.18f);
    const auto inX = plot.min.x + plot.width() * 0.52f - barWidth - dp(4.0f);
    const auto outX = plot.min.x + plot.width() * 0.52f + dp(4.0f);
    for (const auto db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f }) {
        const auto y = bottom - ui::meterPosition(db) * (bottom - top);
        list->AddLine(ImVec2(inX - dp(4.0f), y), ImVec2(outX + barWidth + dp(4.0f), y), p.grid);
        char label[8];
        std::snprintf(label, sizeof(label), "%.0f", db);
        smallText(list, ImVec2(plot.min.x + dp(4.0f), y - ImGui::GetFontSize() * 0.32f), p.textFaint, label, 0.56f);
    }
    const auto bar = [&](const float x, const float db, const ImU32 barColour, const char* name) {
        list->AddRectFilled(ImVec2(x, top), ImVec2(x + barWidth, bottom), p.bg0, dp(2.0f));
        const auto level = live.running ? ui::meterPosition(db) : 0.0f;
        list->AddRectFilled(ImVec2(x, bottom - level * (bottom - top)), ImVec2(x + barWidth, bottom), barColour, dp(2.0f));
        const auto nameSize = smallTextSize(name, 0.6f);
        smallText(list, ImVec2(x + (barWidth - nameSize.x) * 0.5f, bottom + dp(4.0f)), p.textDim, name, 0.6f);
        char value[16];
        if (live.running && db > -100.0f)
            std::snprintf(value, sizeof(value), "%.1f", db);
        else
            std::snprintf(value, sizeof(value), "%s", "\xe2\x80\x94");
        const auto valueSize = smallTextSize(value, 0.62f);
        smallText(list, ImVec2(x + (barWidth - valueSize.x) * 0.5f, bottom + dp(20.0f)), p.text, value, 0.62f);
    };
    bar(inX, live.inputDb, theme::alpha(p.textDim, 0.8f), L("ENT.", "IN"));
    bar(outX, live.outputDb, colour, L("SORT.", "OUT"));
}

// A display large in its own window, for a device without a window of its
// own: its name and its values in large over it, the sound's spectrum
// before and after under it and its levels beside it (when the sound is
// tapped), its controls large under all of it.
bool nativeLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                 const Live& live, const Setter& set, const std::vector<int>* extras)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    std::vector<int> main;
    for (std::size_t slot = 0; slot < pane.parameters.size(); ++slot)
        if (parameterAt(parameters, pane, slot) != nullptr)
            main.push_back(pane.parameters[slot]);
    std::vector<int> others;
    if (extras != nullptr)
        for (const auto index : *extras)
            if (index >= 0 && index < static_cast<int>(parameters.size()) && std::find(main.begin(), main.end(), index) == main.end())
                others.push_back(index);
    const auto controlsHeight = nativeControls(parameters, main, others, paneTitleOf(pane), position, size.x, colour, live, set, false,
                                               nullptr);
    const auto displayHeight = std::max(dp(220.0f), size.y - controlsHeight - dp(10.0f));
    const auto tapped = live.before != nullptr || live.after != nullptr;
    const auto metersWidth = tapped ? dp(96.0f) : 0.0f;
    const Plot display { position, ImVec2(position.x + size.x - (tapped ? metersWidth + dp(8.0f) : 0.0f), position.y + displayHeight) };
    list->AddRectFilled(display.min, display.max, theme::mix(p.bg0, p.bg1, 0.35f), dp(4.0f));
    list->AddRect(display.min, display.max, theme::alpha(p.line, 0.8f), dp(4.0f));

    // The header: its name, then its values in large.
    const auto headerHeight = dp(40.0f);
    ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 1.05f);
    list->AddText(ImVec2(display.min.x + dp(10.0f), display.min.y + dp(9.0f)), colour, paneTitleOf(pane));
    const auto titleWidth = ImGui::CalcTextSize(paneTitleOf(pane)).x;
    ImGui::PopFont();
    {
        auto x = display.max.x - dp(10.0f);
        const auto shown = std::min<std::size_t>(main.size(), 6);
        for (std::size_t at = shown; at-- > 0;) {
            const auto& parameter = parameters[static_cast<std::size_t>(main[at])];
            const auto value = formatValue(parameter, parameter.value);
            const auto label = parameter.label.fromLastOccurrenceOf(juce::String::fromUTF8("\xc2\xb7 "), false, false);
            const auto valueSize = smallTextSize(value.toRawUTF8(), 1.0f);
            const auto labelSize = smallTextSize(label.toRawUTF8(), 0.6f);
            const auto width = std::max(valueSize.x, labelSize.x);
            if (x - width < display.min.x + titleWidth + dp(30.0f))
                break;
            smallText(list, ImVec2(x - labelSize.x, display.min.y + dp(4.0f)), p.textFaint, label.toRawUTF8(), 0.6f);
            smallText(list, ImVec2(x - valueSize.x, display.min.y + dp(4.0f) + labelSize.y), p.text, value.toRawUTF8(), 1.0f);
            x -= width + dp(18.0f);
        }
    }
    list->AddLine(ImVec2(display.min.x + dp(6.0f), display.min.y + headerHeight), ImVec2(display.max.x - dp(6.0f), display.min.y + headerHeight),
                  theme::alpha(p.line, 0.6f));

    // The display, and the spectrum under it when the sound is tapped.
    const auto ownSpectrum = pane.kind == PaneKind::spectrum || pane.kind == PaneKind::level || pane.kind == PaneKind::spectrometer;
    const auto spectrumHeight = tapped && !ownSpectrum ? std::max(dp(70.0f), (displayHeight - headerHeight) * 0.28f) : 0.0f;
    const Plot plot { ImVec2(display.min.x + dp(1.0f), display.min.y + headerHeight + dp(1.0f)),
                      ImVec2(display.max.x - dp(1.0f), display.max.y - dp(1.0f) - spectrumHeight) };
    auto* storage = ImGui::GetStateStorage();
    const auto bandId = ImGui::GetID("##largeBand");
    auto selectedBand = storage->GetInt(bandId, 0);
    list->PushClipRect(plot.min, plot.max, true);
    changed |= paneBody(pane, parameters, plot, colour, live, set, selectedBand, true);
    list->PopClipRect();
    if (pane.kind == PaneKind::equalizer)
        storage->SetInt(bandId, selectedBand);
    if (spectrumHeight > 0.0f) {
        const Plot strip { ImVec2(plot.min.x, plot.max.y), ImVec2(plot.max.x, display.max.y - dp(1.0f)) };
        list->AddLine(ImVec2(strip.min.x, strip.min.y), ImVec2(strip.max.x, strip.min.y), theme::alpha(p.line, 0.6f));
        list->PushClipRect(strip.min, strip.max, true);
        (void) spectrumPlot(strip, colour, live);
        smallText(list, ImVec2(strip.min.x + dp(5.0f), strip.min.y + dp(3.0f)), theme::alpha(p.textDim, 0.85f),
                  L("SPECTRE  avant · après", "SPECTRUM  before · after"), 0.62f);
        list->PopClipRect();
    }
    if (tapped)
        levelColumn(Plot { ImVec2(display.max.x + dp(8.0f), display.min.y), ImVec2(position.x + size.x, display.max.y) }, colour, live);

    // The controls, large, under it.
    bool moved = false;
    (void) nativeControls(parameters, main, others, paneTitleOf(pane), ImVec2(position.x, display.max.y + dp(10.0f)), size.x, colour,
                          live, set, true, &moved);
    return changed || moved;
}

} // namespace

bool drawOverview(const char* id, const std::vector<Pane>& panes, std::vector<Param>& parameters, const ImVec2 position,
                  const ImVec2 size, const ImU32 colour, const Live& live, const Setter& set, const std::vector<int>& extras,
                  int* expand)
{
    ImGui::PushID(id);
    auto changed = false;
    // Every setting large under them all: each display's, under its name,
    // then the ones no display takes.
    std::vector<ControlGroup> groups;
    std::vector<int> taken;
    const auto add = [&](const char* title, const std::vector<int>& indices) {
        ControlGroup group { juce::String::fromUTF8(title), {} };
        for (const auto index : indices)
            if (index >= 0 && index < static_cast<int>(parameters.size()) && std::find(taken.begin(), taken.end(), index) == taken.end()) {
                group.indices.push_back(index);
                taken.push_back(index);
            }
        groups.push_back(std::move(group));
    };
    for (const auto& pane : panes) {
        // The parametric equaliser: its band chosen (its curve has them all).
        std::vector<int> indices;
        if (pane.kind == PaneKind::eqPro) {
            for (const auto slot : paneFields(pane, parameters, 0, nullptr))
                indices.push_back(pane.parameters[static_cast<std::size_t>(slot)]);
        } else {
            indices = pane.parameters;
        }
        add(paneTitleOf(pane), indices);
    }
    add(L("AUTRES RÉGLAGES", "OTHER SETTINGS"), extras);
    const auto controlsHeight = controlGroups(parameters, groups, position, size.x, colour, live, set, false, nullptr);
    const auto tapped = live.before != nullptr || live.after != nullptr;
    const auto metersWidth = tapped ? dp(96.0f) : 0.0f;
    const auto gap = dp(8.0f);
    const auto width = size.x - (tapped ? metersWidth + gap : 0.0f);
    const auto height = std::max(dp(160.0f), size.y - controlsHeight - (controlsHeight > 0.0f ? dp(10.0f) : 0.0f));

    // The panes as large as they go: on as few rows as fit their widths at
    // that height, each row's panes widened to fill it.
    auto rows = 1;
    auto paneHeight = height;
    for (; rows <= 4; ++rows) {
        paneHeight = (height - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);
        auto row = 1;
        auto x = 0.0f;
        for (const auto& pane : panes) {
            const auto natural = paneWidth(pane, paneHeight);
            if (x > 0.0f && x + gap + natural > width) {
                ++row;
                x = 0.0f;
            }
            x += (x > 0.0f ? gap : 0.0f) + natural;
        }
        if (row <= rows)
            break;
    }
    rows = std::min(rows, 4);
    std::vector<std::vector<std::size_t>> lines(1);
    {
        auto x = 0.0f;
        for (std::size_t index = 0; index < panes.size(); ++index) {
            const auto natural = paneWidth(panes[index], paneHeight);
            if (!lines.back().empty() && x + gap + natural > width && static_cast<int>(lines.size()) < rows) {
                lines.emplace_back();
                x = 0.0f;
            }
            x += (lines.back().empty() ? 0.0f : gap) + natural;
            lines.back().push_back(index);
        }
    }
    auto y = position.y;
    for (const auto& line : lines) {
        auto natural = 0.0f;
        for (const auto index : line)
            natural += paneWidth(panes[index], paneHeight);
        const auto room = width - gap * static_cast<float>(line.size() - 1);
        const auto scale = natural > 0.0f ? room / natural : 1.0f;
        auto x = position.x;
        for (const auto index : line) {
            const auto paneSize = ImVec2(paneWidth(panes[index], paneHeight) * scale, paneHeight);
            ImGui::PushID(static_cast<int>(index));
            auto open = false;
            changed |= drawPane("##overviewPane", panes[index], parameters, ImVec2(x, y), paneSize, colour, live, set, false, &open);
            if (open && expand != nullptr)
                *expand = static_cast<int>(index);
            ImGui::PopID();
            x += paneSize.x + gap;
        }
        y += paneHeight + gap;
    }
    if (tapped)
        levelColumn(Plot { ImVec2(position.x + width + gap, position.y), ImVec2(position.x + size.x, position.y + height) }, colour, live);
    if (controlsHeight > 0.0f) {
        bool moved = false;
        (void) controlGroups(parameters, groups, ImVec2(position.x, position.y + height + dp(10.0f)), size.x, colour, live, set, true,
                             &moved);
        changed |= moved;
    }
    ImGui::PopID();
    return changed;
}

bool drawLargePane(const char* id, const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size,
                   const ImU32 colour, const Live& live, const Setter& set, const std::vector<int>* extras)
{
    if (isStudioPane(pane.kind)) {
        ImGui::PushID(id);
        const auto changed = studioLargePane(pane, parameters, position, size, colour, live, set);
        ImGui::PopID();
        return changed;
    }
    if (pane.kind == PaneKind::compPro) {
        const auto& p = theme::palette();
        auto* list = ImGui::GetWindowDrawList();
        ImGui::PushID(id);
        const auto panelHeight = compressorStudioPanelHeight() + dp(16.0f);
        const Plot frame { position, ImVec2(position.x + size.x, position.y + std::max(dp(160.0f), size.y - panelHeight - dp(10.0f))) };
        list->AddRectFilled(frame.min, frame.max, theme::mix(p.bg0, p.bg1, 0.35f), dp(4.0f));
        list->AddRect(frame.min, frame.max, theme::alpha(p.line, 0.8f), dp(4.0f));
        const Plot plot { ImVec2(frame.min.x + dp(6.0f), frame.min.y + dp(4.0f)), ImVec2(frame.max.x - dp(6.0f), frame.max.y - dp(4.0f)) };
        list->PushClipRect(frame.min, frame.max, true);
        auto changed = compProPlot(plot, parameters, pane, colour, live, set, true);
        paneTitle(list, plot, paneTitleOf(pane));
        list->PopClipRect();
        changed |= compressorStudioPanel(pane, parameters, ImVec2(position.x + dp(4.0f), frame.max.y + dp(16.0f)), size.x - dp(8.0f), colour,
                                         live, set);
        ImGui::PopID();
        return changed;
    }
    if (pane.kind != PaneKind::eqPro) {
        ImGui::PushID(id);
        const auto changed = nativeLarge(pane, parameters, position, size, colour, live, set, extras);
        ImGui::PopID();
        return changed;
    }
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    ImGui::PushID(id);
    const auto panelHeight = dp(204.0f);
    const Plot frame { position, ImVec2(position.x + size.x, position.y + std::max(dp(140.0f), size.y - panelHeight - dp(10.0f))) };
    list->AddRectFilled(frame.min, frame.max, theme::mix(p.bg0, p.bg1, 0.35f), dp(4.0f));
    list->AddRect(frame.min, frame.max, theme::alpha(p.line, 0.8f), dp(4.0f));
    const Plot plot { ImVec2(frame.min.x + dp(1.0f), frame.min.y + dp(1.0f)), ImVec2(frame.max.x - dp(1.0f), frame.max.y - dp(1.0f)) };
    list->PushClipRect(plot.min, plot.max, true);
    auto changed = eqProPlot(plot, parameters, pane, colour, live, set, true);
    list->PopClipRect();
    if (auto eq = eqOf(parameters, pane); eq.valid())
        changed |= eqBandPanel(ImVec2(position.x + dp(4.0f), frame.max.y + dp(12.0f)), size.x - dp(8.0f), eq, eqView(eq.key), live, set);
    ImGui::PopID();
    return changed;
}

} // namespace cydaw::v2::visual
