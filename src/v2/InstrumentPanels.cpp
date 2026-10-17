#include "InstrumentPanels.h"
#include "Theme.h"
#include "Widgets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cydaw/engine/KickSynth.h>
#include <cydaw/engine/PrismSynth.h>
#include <map>
#include <numbers>

namespace cydaw::v2::instrument_ui {
namespace {
using namespace cydaw::engine;
using Picture = synth_layout::Picture;
constexpr double kPi = std::numbers::pi;
std::complex<double> colosseStage(const int type, const double cutoff,
                                  const double resonance, const double hz) {
  using complex = std::complex<double>;
  const complex s(0.0, hz / std::max(1.0, cutoff));
  const auto damping = std::max(0.02, 2.0 - 1.98 * resonance);
  switch (type) {
  case 0: {
    const auto k = 4.2 * resonance;
    const auto pole = 1.0 + s;
    return (1.0 + 0.6 * k) / (pole * pole * pole * pole + k);
  }
  case 1:
    return 1.0 / (s * s + damping * s + 1.0);
  case 2:
    return damping * s / (s * s + damping * s + 1.0);
  case 3:
    return s * s / (s * s + damping * s + 1.0);
  case 4: {
    const auto feedback = 0.97 * resonance;
    const auto delay = 1.0 / std::max(40.0, cutoff);
    const auto z = std::exp(complex(0.0, -2.0 * kPi * hz * delay));
    return (1.0 - 0.6 * feedback) / (1.0 - feedback * z);
  }
  case 5: {
    static constexpr std::array<std::array<double, 3>, 5> vowels{
        {{800.0, 1'150.0, 2'900.0},
         {400.0, 1'900.0, 2'700.0},
         {280.0, 2'200.0, 2'950.0},
         {450.0, 800.0, 2'830.0},
         {325.0, 700.0, 2'530.0}}};
    static constexpr std::array<double, 3> gains{1.0, 0.7, 0.35};
    const auto t =
        std::clamp(std::log(std::max(20.0, cutoff) / 20.0) / std::log(1'000.0),
                   0.0, 1.0) *
        4.0;
    const auto lower = std::min(static_cast<int>(t), 3);
    const auto mix = t - static_cast<double>(lower);
    const auto width = std::max(0.04, 0.5 - 0.44 * resonance);
    complex sum;
    for (std::size_t at = 0; at < 3; ++at) {
      const auto centre = vowels[static_cast<std::size_t>(lower)][at] +
                          (vowels[static_cast<std::size_t>(lower + 1)][at] -
                           vowels[static_cast<std::size_t>(lower)][at]) *
                              mix;
      const complex f(0.0, hz / centre);
      sum += gains[at] * width * f / (f * f + width * f + 1.0);
    }
    return sum * 1.6;
  }
  default: {
    const auto screaming =
        std::max(0.02, 2.0 - 1.98 * std::min(0.98, 0.55 + 0.45 * resonance));
    return (0.5 + resonance) * s / (s * s + screaming * s + 1.0);
  }
  }
}

double nativeFamilyFilter(synth::Model synthModel, const synth::Values &values,
                          double hz) {
  const auto indexOf = [&](const char *key) {
    return synth::parameterIndex(synthModel, key);
  };
  const auto valueOf = [&](const char *key) {
    const auto i = indexOf(key);
    return i >= 0 ? values[static_cast<std::size_t>(i)] : 0.0f;
  };

  using complex = std::complex<double>;
  complex response;
  // A model that draws its own curve.
  if (const auto own = synth::filterDb(synthModel, values, hz);
      std::isfinite(own))
    return own;
  if (indexOf("f.type") >= 0) {
    // One filter of the family's kinds.
    response = colosseStage(
        static_cast<int>(std::lround(valueOf("f.type"))),
        static_cast<double>(valueOf("f.cutoff")),
        std::clamp(static_cast<double>(valueOf("f.resonance")), 0.0, 1.0), hz);
    return 20.0 * std::log10(std::max(1.0e-9, std::abs(response)));
  }
  if (indexOf("f1.type") >= 0) {
    // Two filters, in series or side by side, blended.
    const auto stage = [&, hz](const char *type, const char *cutoff,
                               const char *resonance) {
      return colosseStage(
          static_cast<int>(std::lround(valueOf(type))),
          static_cast<double>(valueOf(cutoff)),
          std::clamp(static_cast<double>(valueOf(resonance)), 0.0, 1.0), hz);
    };
    const auto one = stage("f1.type", "f1.cutoff", "f1.resonance");
    const auto two = stage("f2.type", "f2.cutoff", "f2.resonance");
    const auto parallel = valueOf("filter.routing") >= 0.5f;
    if (indexOf("filter.blend") >= 0) {
      // The second dosed by its blend.
      const auto blend =
          std::clamp(static_cast<double>(valueOf("filter.blend")), 0.0, 1.0);
      response = parallel ? one + (two - one) * blend
                          : one + (one * two - one) * blend;
    } else {
      // Side by side, each as much as the engines send it; in series, both.
      const auto levelA = static_cast<double>(valueOf("a.level"));
      const auto levelB = static_cast<double>(valueOf("b.level"));
      const auto sent =
          (levelA * valueOf("a.send") + levelB * valueOf("b.send")) /
          std::max(1.0e-6, levelA + levelB);
      response = parallel ? one + (two - one) * sent : one * two;
    }
    return 20.0 * std::log10(std::max(1.0e-9, std::abs(response)));
  }
  const auto cutoff =
      std::max(1.0, static_cast<double>(valueOf("filter.cutoff")));
  const auto resonance =
      std::clamp(static_cast<double>(valueOf("filter.resonance")), 0.0, 1.0);
  const auto model =
      indexOf("filter.model") >= 0
          ? static_cast<int>(std::lround(valueOf("filter.model")))
          : 2;
  const auto mode = indexOf("filter.mode") >= 0
                        ? static_cast<int>(std::lround(valueOf("filter.mode")))
                        : 0;
  const complex s(0.0, hz / cutoff);
  if (model == 0 || model == 1) {
    // Four poles in a loop: low-pass, the lows given back as it resonates.
    const auto k = (model == 0 ? 4.2 : 4.6) * resonance;
    const auto makeUp = 1.0 + (model == 0 ? 0.6 : 0.35) * k;
    const auto pole = 1.0 + s;
    response = makeUp / (pole * pole * pole * pole + k);
  } else {
    // Two poles, state-variable: low, band or high.
    const auto damping = std::max(0.02, 2.0 - 2.0 * resonance * 0.99);
    const auto denominator = s * s + damping * s + 1.0;
    response = mode == 1   ? s / denominator
               : mode == 2 ? s * s / denominator
                           : 1.0 / denominator;
  }
  if (const auto hp =
          indexOf("hpf") >= 0 ? static_cast<double>(valueOf("hpf")) : 0.0;
      hp > 20.5) {
    const complex h(0.0, hz / hp);
    response *= h / (1.0 + h);
  }
  return 20.0 * std::log10(std::max(1.0e-9, std::abs(response)));
}
double nativePrismFilter(const prism::PrismValues &values, double f) {
  const auto value = [&](int i) { return values[static_cast<std::size_t>(i)]; };

  const auto type = static_cast<prism::FilterType>(
      std::lround(value(prism::filterBase + prism::filterType)));
  const auto cutoff =
      static_cast<double>(value(prism::filterBase + prism::filterCutoff));
  const auto resonance =
      static_cast<double>(value(prism::filterBase + prism::filterResonance));
  const auto variation =
      static_cast<double>(value(prism::filterBase + prism::filterVariation));
  const auto mix =
      static_cast<double>(value(prism::filterBase + prism::filterMix));
  const auto q = 0.5 + resonance * resonance * 24.0;
  using C = std::complex<double>;
  const auto svf = [](const C s, const double quality, C &low, C &band,
                      C &high) {
    const auto d = s * s + s / quality + 1.0;
    low = 1.0 / d;
    band = (s / quality) / d;
    high = (s * s) / d;
  };
  const C s(0.0, f / std::max(cutoff, 1.0));
  C h(1.0, 0.0);
  C low;
  C band;
  C high;
  switch (type) {
  case prism::FilterType::lowpass12:
    svf(s, q, low, band, high);
    h = low;
    break;
  case prism::FilterType::highpass12:
    svf(s, q, low, band, high);
    h = high;
    break;
  case prism::FilterType::bandpass12:
    svf(s, q, low, band, high);
    h = band;
    break;
  case prism::FilterType::notch:
    svf(s, q, low, band, high);
    h = low + high;
    break;
  case prism::FilterType::peak: {
    svf(s, q, low, band, high);
    const auto gain = std::pow(10.0, (variation - 0.5) * 36.0 / 20.0);
    h = 1.0 + (gain - 1.0) * band;
    break;
  }
  case prism::FilterType::lowpass24:
  case prism::FilterType::highpass24:
  case prism::FilterType::bandpass24: {
    C l2;
    C b2;
    C h2;
    svf(s, 0.54, low, band, high);
    svf(s, 1.31 * (1.0 + resonance * resonance * 9.0), l2, b2, h2);
    h = type == prism::FilterType::lowpass24    ? low * l2
        : type == prism::FilterType::highpass24 ? high * h2
                                                : band * b2;
    break;
  }
  case prism::FilterType::ladder: {
    const auto k = resonance * 3.9;
    const auto pole = 1.0 + s;
    h = (1.0 + k * 0.35) / (pole * pole * pole * pole + k);
    break;
  }
  case prism::FilterType::combPlus:
  case prism::FilterType::combMinus: {
    const auto feedback =
        (type == prism::FilterType::combMinus ? -1.0 : 1.0) * resonance * 0.97;
    const auto phase = 2.0 * std::numbers::pi * f / std::max(cutoff, 1.0);
    h = 0.5 / (1.0 - feedback * std::polar(1.0, -phase));
    break;
  }
  case prism::FilterType::formant: {
    const double vowels[5][3] = {{800, 1150, 2900},
                                 {400, 1600, 2700},
                                 {270, 2290, 3010},
                                 {450, 800, 2830},
                                 {325, 700, 2530}};
    const auto at = variation * 4.0;
    const auto from = std::min(3, static_cast<int>(at));
    const auto t = at - from;
    const auto shift = std::clamp(cutoff / 1'000.0, 0.25, 4.0);
    const double gains[3] = {1.0, 0.7, 0.4};
    h = 0.0;
    for (int formant = 0; formant < 3; ++formant) {
      const auto centre =
          (vowels[from][formant] +
           (vowels[from + 1][formant] - vowels[from][formant]) * t) *
          shift;
      svf(C(0.0, f / centre), 4.0 + resonance * 16.0, low, band, high);
      h += band * gains[formant] * 1.4;
    }
    break;
  }
  case prism::FilterType::count:
    break;
  }
  h = 1.0 + (h - 1.0) * mix;
  const auto db =
      std::clamp(20.0 * std::log10(std::max(std::abs(h), 1.0e-6)), -36.0, 18.0);
  return db;
}

std::vector<Panel> build(const std::string &type, bool compact) {
  std::vector<Panel> result;
  const auto model = synth::modelForType(type);
  if (model != synth::Model::none) {
    const auto &info = synth::info(model);
    const auto &layout = synth_layout::layoutFor(model);
    const auto append = [&](const synth_layout::Panel &source,
                            const std::string &page, int row) {
      Panel p;
      p.id = source.id;
      p.title = source.english;
      p.page = page;
      p.row = row;
      p.weight = source.weight;
      p.picture = source.picture;
      p.pictureKey = source.pictureKey ? source.pictureKey : "";
      p.secondKey = source.secondKey ? source.secondKey : "";
      p.colour =
          source.section >= 0 &&
                  source.section < static_cast<int>(info.sections.size())
              ? theme::fromArgb(
                    info.sections[static_cast<std::size_t>(source.section)]
                        .colour)
              : layout.colour;
      for (const auto &c : source.controls) {
        if (!c.key)
          continue;
        const auto index = synth::parameterIndex(model, c.key);
        if (index < 0)
          continue;
        const auto &v = synth::parameter(model, index);
        Setting s;
        s.key = v.key;
        s.label = c.english ? c.english : v.english;
        s.unit = v.unit;
        s.minimum = v.minimum;
        s.maximum = v.maximum;
        s.initial = v.defaultValue;
        s.kind = static_cast<int>(v.kind);
        s.logarithmic = v.logarithmic;
        s.condition = c.onlyWhen ? c.onlyWhen : "";
        s.from = c.from;
        s.to = c.to;
        s.hide = c.hide;
        s.newLine = c.newLine;
        for (const auto &choice : v.choices)
          s.choices.push_back(choice.second);
        p.settings.push_back(std::move(s));
      }
      result.push_back(std::move(p));
    };
    if (compact)
      for (const auto &p : layout.card)
        append(p, "", 0);
    else
      for (const auto &page : layout.pages)
        for (std::size_t row = 0; row < page.rows.size(); ++row)
          for (const auto &p : page.rows[row])
            append(p, page.english, static_cast<int>(row));
  } else if (type == "kick-synth") {
    const char *titles[]{"BODY / PITCH", "AMPLITUDE", "CLICK", "SUB",
                         "DRIVE",        "TONE",      "GLOBAL"};
    for (int i = 0; i < kick::parameterCount; ++i) {
      const auto &v = kick::parameter(i);
      const auto group = static_cast<int>(v.group);
      if (compact && group != 0 && group != 1 && group != 6)
        continue;
      const auto id = "kick-" + std::to_string(group);
      auto found = std::find_if(result.begin(), result.end(),
                                [&](const auto &p) { return p.id == id; });
      if (found == result.end()) {
        Panel p;
        p.id = id;
        p.title = titles[group];
        p.page = compact ? "" : "KICK";
        p.row = compact ? 0 : group < 2 ? 0 : group < 5 ? 1 : 2;
        p.colour = group == 1 ? IM_COL32(95, 201, 247, 255)
                              : IM_COL32(245, 179, 66, 255);
        if (group == 0)
          p.picture = Picture::wave;
        else if (group == 1)
          p.picture = Picture::envelope;
        result.push_back(p);
        found = std::prev(result.end());
      }
      Setting field;
      field.key = v.key;
      field.label = v.english;
      field.unit = v.unit;
      field.minimum = v.minimum;
      field.maximum = v.maximum;
      field.initial = v.defaultValue;
      field.logarithmic = v.logarithmic;
      field.kind = v.choices.empty() ? 0 : 3;
      for (const auto &choice : v.choices)
        field.choices.push_back(choice.second);
      found->settings.push_back(std::move(field));
    }
  } else if (type == "prism-synth") {
    // Prisme's own groups: oscillators, filter, envelopes/LFOs, macros,
    // matrix and voicing. Their keys/choices come from the engine table.
    for (int i = 0; i < prism::parameterCount; ++i) {
      const auto &v = prism::parameter(i);
      if (v.group == prism::Group::lfo &&
          v.key.find(".draw") != std::string::npos)
        continue;
      if (compact && v.group != prism::Group::macro &&
          v.group != prism::Group::filter && v.group != prism::Group::voicing)
        continue;
      const auto id = std::to_string(static_cast<int>(v.group)) + "-" +
                      std::to_string(v.instance);
      auto found = std::find_if(result.begin(), result.end(),
                                [&](const auto &p) { return p.id == id; });
      if (found == result.end()) {
        Panel p;
        p.id = id;
        p.page = "OSC";
        p.colour = IM_COL32(245, 151, 62, 255);
        switch (v.group) {
        case prism::Group::oscA:
        case prism::Group::oscB:
        case prism::Group::oscC:
          p.title = "OSC " + std::string(v.group == prism::Group::oscA   ? "A"
                                         : v.group == prism::Group::oscB ? "B"
                                                                         : "C");
          p.picture = Picture::wave;
          p.pictureKey = v.group == prism::Group::oscA   ? "a"
                         : v.group == prism::Group::oscB ? "b"
                                                         : "c";
          p.weight = 1.5f;
          break;
        case prism::Group::sub:
          p.title = "SUB";
          p.row = 1;
          break;
        case prism::Group::noise:
          p.title = "NOISE";
          p.row = 1;
          break;
        case prism::Group::filter:
          p.title = "FILTER";
          p.row = 1;
          p.picture = Picture::filter;
          p.pictureKey = "filter";
          p.weight = 2;
          break;
        case prism::Group::env:
          p.title = "ENV " + std::to_string(v.instance + 1);
          p.page = "ENVELOPES";
          p.picture = Picture::envelope;
          p.pictureKey = "env" + std::to_string(v.instance + 1);
          break;
        case prism::Group::lfo:
          p.title = "LFO " + std::to_string(v.instance + 1);
          p.page = "LFO";
          p.picture = Picture::lfo;
          p.pictureKey = "lfo" + std::to_string(v.instance + 1);
          break;
        case prism::Group::macro:
          p.title = "MACRO " + std::to_string(v.instance + 1);
          p.page = "GLOBAL";
          p.row = 1;
          break;
        case prism::Group::voicing:
          p.title = "VOICING";
          p.page = "GLOBAL";
          break;
        case prism::Group::matrix:
          p.title = "MOD " + std::to_string(v.instance + 1);
          p.page = "MATRIX";
          p.row = v.instance / 4;
          break;
        }
        if (compact) {
          p.page = "";
          p.row = 0;
        }
        result.push_back(std::move(p));
        found = std::prev(result.end());
      }
      Setting s;
      s.key = v.key;
      s.label = v.english;
      s.unit = v.unit;
      s.minimum = v.minimum;
      s.maximum = v.maximum;
      s.initial = v.defaultValue;
      s.kind = static_cast<int>(v.kind);
      s.logarithmic = v.logarithmic;
      for (const auto &choice : v.choices)
        s.choices.push_back(choice.second);
      found->settings.push_back(std::move(s));
    }
  }
  return result;
}
} // namespace
double filterResponse(engine::synth::Model model,
                      const engine::synth::Values &values, double hz) {
  return nativeFamilyFilter(model, values, hz);
}
double filterResponse(const engine::prism::PrismValues &values, double hz) {
  return nativePrismFilter(values, hz);
}

const std::vector<Panel> &panels(const std::string &type, bool compact) {
  static std::map<std::pair<std::string, bool>, std::vector<Panel>> cache;
  const auto key = std::make_pair(type, compact);
  auto found = cache.find(key);
  if (found == cache.end())
    found = cache.emplace(key, build(type, compact)).first;
  return found->second;
}
const Panel *panel(const std::string &type, const std::string &id,
                   bool compact) {
  const auto &list = panels(type, compact);
  const auto found = std::find_if(list.begin(), list.end(),
                                  [&](const auto &p) { return p.id == id; });
  return found == list.end() ? nullptr : &*found;
}

void draw(const char *id, const std::string &type, const Panel &spec,
          const std::vector<std::string> &keys,
          std::vector<visual::Param> &parameters, ImVec2 at, ImVec2 size,
          ImU32 colour, const visual::Setter &set, bool interactive,
          float scale, const char *title) {
  scale = std::clamp(scale, .1f, 4.0f);
  auto &uiStyle = ImGui::GetStyle();
  const auto dpi = uiStyle.FontScaleDpi;
  uiStyle.FontScaleDpi *= scale;
  ImGui::PushFont(nullptr, ui::currentFontBase());
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      ImVec2(uiStyle.WindowPadding.x * scale, uiStyle.WindowPadding.y * scale));
  ImGui::PushStyleVar(
      ImGuiStyleVar_FramePadding,
      ImVec2(uiStyle.FramePadding.x * scale, uiStyle.FramePadding.y * scale));
  ImGui::PushStyleVar(
      ImGuiStyleVar_ItemSpacing,
      ImVec2(uiStyle.ItemSpacing.x * scale, uiStyle.ItemSpacing.y * scale));
  using ui::dp;
  const auto indexOf = [&](const std::string &key) {
    const auto found = std::find(keys.begin(), keys.end(), key);
    return found == keys.end() ? -1 : static_cast<int>(found - keys.begin());
  };
  const auto param = [&](const std::string &key) -> visual::Param * {
    const auto i = indexOf(key);
    return i >= 0 && i < static_cast<int>(parameters.size()) &&
                   parameters[static_cast<std::size_t>(i)].binding.isNotEmpty()
               ? &parameters[static_cast<std::size_t>(i)]
               : nullptr;
  };
  const auto value = [&](const std::string &key, double fallback = 0.0) {
    if (const auto *p = param(key))
      return p->value;
    if (type == "prism-synth") {
      const auto i = prism::parameterIndex(key);
      if (i >= 0)
        return static_cast<double>(prism::parameter(i).defaultValue);
    }
    return fallback;
  };
  const auto apply = [&](const std::string &key, double v) {
    if (auto *p = param(key); p && interactive) {
      v = std::clamp(v, p->minimum, p->maximum);
      p->value = v;
      set(*p, v);
    }
  };
  const auto kept = ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos(at);
  ImGui::PushStyleColor(ImGuiCol_Border, theme::alpha(colour, .65f));
  ImGui::BeginChild(id, size, ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoSavedSettings |
                        (!interactive ? ImGuiWindowFlags_NoInputs : 0));
  ImGui::TextColored(theme::vec(colour), "%s",
                     title && *title ? title : spec.title.c_str());
  auto origin = ImGui::GetCursorScreenPos();
  const auto width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
  auto *list = ImGui::GetWindowDrawList();
  if (spec.picture != Picture::none) {
    const ImVec2 pictureSize(width,
                             dp(spec.picture == Picture::filter ? 90 : 70));
    const auto prefix = spec.pictureKey;
    const auto plot = ImVec2(origin.x + dp(4), origin.y + dp(4));
    const auto pw = std::max(1.0f, pictureSize.x - dp(8)),
               ph = pictureSize.y - dp(8);
    list->AddRectFilled(origin,
                        ImVec2(origin.x + width, origin.y + pictureSize.y),
                        theme::palette().bg0, dp(3));
    if (type == "kick-synth") {
      auto values = kick::defaultValues();
      for (std::size_t i = 0; i < keys.size() && i < parameters.size(); ++i)
        if (parameters[i].binding.isNotEmpty()) {
          const auto field = kick::parameterIndex(keys[i]);
          if (field >= 0)
            values[static_cast<std::size_t>(field)] =
                static_cast<float>(parameters[i].value);
        }
      const auto length = std::max(.005, kick::lengthSeconds(values));
      std::vector<ImVec2> line;
      for (int i = 0; i < 160; ++i) {
        const auto time = length * i / 159.0;
        const auto y =
            spec.picture == Picture::envelope
                ? kick::amplitudeAt(values, time)
                : std::clamp(
                      std::log(std::max(20.0, kick::pitchAt(values, time)) /
                               20) /
                          std::log(1000),
                      0.0, 1.0);
        line.emplace_back(plot.x + pw * i / 159.0f,
                          plot.y + ph * (1 - static_cast<float>(y)));
      }
      list->AddPolyline(line.data(), static_cast<int>(line.size()), colour,
                        dp(1.7f));
    } else if (spec.picture == Picture::filter) {
      const auto model = synth::modelForType(type);
      auto values = synth::defaultValues(model);
      auto prismValues = prism::defaultValues();
      for (std::size_t i = 0; i < keys.size() && i < parameters.size(); ++i)
        if (parameters[i].binding.isNotEmpty()) {
          const auto field = model != synth::Model::none
                                 ? synth::parameterIndex(model, keys[i])
                                 : prism::parameterIndex(keys[i]);
          if (field >= 0) {
            if (model != synth::Model::none)
              values[static_cast<std::size_t>(field)] =
                  static_cast<float>(parameters[i].value);
            else
              prismValues[static_cast<std::size_t>(field)] =
                  static_cast<float>(parameters[i].value);
          }
        }
      std::vector<ImVec2> curve;
      for (int i = 0; i < 120; ++i) {
        const auto hz = 20.0 * std::pow(1000.0, i / 119.0);
        const auto db = type == "prism-synth"
                            ? nativePrismFilter(prismValues, hz)
                            : nativeFamilyFilter(model, values, hz);
        curve.emplace_back(
            plot.x + pw * i / 119.0f,
            plot.y + ph * static_cast<float>(
                              (18 - std::clamp(db, -36.0, 18.0)) / 54.0));
      }
      list->AddPolyline(curve.data(), static_cast<int>(curve.size()), colour,
                        dp(1.7f));
      for (const auto hz : {100.0, 1000.0, 10000.0}) {
        const auto x = plot.x + pw * static_cast<float>(std::log(hz / 20) /
                                                        std::log(1000));
        list->AddLine(ImVec2(x, plot.y), ImVec2(x, plot.y + ph),
                      theme::palette().grid);
      }
      const auto cutoffKey = prefix + ".cutoff",
                 resonanceKey = prefix + ".resonance";
      if (param(cutoffKey)) {
        const auto hz = std::max(20.0, value(cutoffKey, 1000));
        const auto x =
            plot.x + pw * static_cast<float>(std::clamp(
                              std::log(hz / 20) / std::log(1000), 0.0, 1.0));
        list->AddCircleFilled(ImVec2(x, plot.y + ph * .5f), dp(4), colour);
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##filter", pictureSize);
        if (interactive && ImGui::IsItemActive()) {
          const auto mouse = ImGui::GetIO().MousePos;
          apply(cutoffKey,
                20 * std::pow(1000,
                              std::clamp((mouse.x - plot.x) / pw, 0.0f, 1.0f)));
          if (const auto *p = param(resonanceKey))
            apply(resonanceKey,
                  p->minimum +
                      (1 - std::clamp((mouse.y - plot.y) / ph, 0.0f, 1.0f)) *
                          (p->maximum - p->minimum));
        }
      }
    } else if (spec.picture == Picture::envelope) {
      std::vector<int> slots;
      if (spec.picture == Picture::filter)
        slots = {indexOf(prefix + ".cutoff"), indexOf(prefix + ".resonance")};
      else
        for (const auto *stage : {"attack", "decay", "sustain", "release"})
          slots.push_back(indexOf(prefix + "." + stage));
      visual::Pane pane{spec.picture == Picture::filter
                            ? visual::PaneKind::filter
                            : visual::PaneKind::envelope,
                        slots, 0};
      visual::Live live;
      visual::drawPane(
          "##picture", pane, parameters, origin, pictureSize, colour, live,
          [&](const auto &p, double v) {
            if (interactive)
              set(p, v);
          },
          false);
    } else {
      std::vector<float> samples;
      const auto model = synth::modelForType(type);
      if (spec.picture == Picture::wave && model != synth::Model::none) {
        auto values = synth::defaultValues(model);
        for (std::size_t i = 0; i < keys.size() && i < parameters.size(); ++i)
          if (parameters[i].binding.isNotEmpty()) {
            const auto field = synth::parameterIndex(model, keys[i]);
            if (field >= 0)
              values[static_cast<std::size_t>(field)] =
                  static_cast<float>(parameters[i].value);
          }
        samples =
            synth::picture(model, values, prefix.empty() ? "mix" : prefix, 160);
        // Ambre's sum is defined directly by its three analogue oscillators.
        if (samples.empty() && model == synth::Model::ambre)
          for (int i = 0; i < 160; ++i) {
            double sum = 0;
            for (int osc = 1; osc <= 3; ++osc) {
              const auto key = "osc" + std::to_string(osc);
              const auto phase = std::fmod(
                  i / 80.0 * std::pow(2.0, value(key + ".octave")), 1.0);
              const auto wave = static_cast<int>(value(key + ".wave"));
              const auto y =
                  wave == 0   ? 2 * phase - 1
                  : wave == 1 ? (phase < value(key + ".pw", .5) ? 1 : -1)
                  : wave == 2 ? (phase < .5 ? 4 * phase - 1 : 3 - 4 * phase)
                              : std::sin(2 * std::numbers::pi * phase);
              sum += y * value(key + ".level");
            }
            samples.push_back(static_cast<float>(sum / 3));
          }
      } else if (spec.picture == Picture::wave && type == "prism-synth") {
        const auto table =
            prism::factoryTable(static_cast<int>(value(prefix + ".table")));
        if (table && table->frameCount > 0) {
          const auto frame = std::clamp(value(prefix + ".position"), 0.0, 1.0) *
                             (table->frameCount - 1);
          const auto a = static_cast<int>(frame),
                     b = std::min(a + 1, table->frameCount - 1);
          for (int i = 0; i < 160; ++i) {
            const auto pos = (i * prism::Wavetable::frameLength / 80) %
                             prism::Wavetable::frameLength;
            samples.push_back(
                static_cast<float>(table->frame(a, 0)[pos] * (1 - (frame - a)) +
                                   table->frame(b, 0)[pos] * (frame - a)));
          }
        }
      } else if (spec.picture == Picture::lfo) {
        const auto shape = static_cast<int>(value(prefix + ".shape"));
        static constexpr double held[]{.35, -.7, .85, -.15, .55, -.9, .1, .7};
        for (int i = 0; i < 160; ++i) {
          const auto phase = i / 159.0;
          double v = std::sin(2 * std::numbers::pi * phase);
          if (type == "prism-synth") {
            if (shape == 1)
              v = 1 - 4 * std::abs(phase - .5);
            else if (shape == 2)
              v = 2 * phase - 1;
            else if (shape == 3)
              v = 1 - 2 * phase;
            else if (shape == 4)
              v = phase < .5 ? 1 : -1;
            else if (shape == 5)
              v = held[std::min(7, static_cast<int>(phase * 4))];
            else if (shape == 6) {
              const auto t = phase * 4;
              const auto from = static_cast<int>(t) % 8;
              v = held[from] + (held[(from + 1) % 8] - held[from]) *
                                   (.5 - .5 * std::cos(std::numbers::pi *
                                                       (t - std::floor(t))));
            } else if (shape == 7)
              v = 2 * std::exp(-5 * phase) - 1;
            else if (shape == 8) {
              const auto pos = phase * (prism::drawPoints - 1);
              const auto point = static_cast<int>(pos);
              const auto a = value(prefix + ".draw" +
                                   std::to_string(point + 1)),
                         b = value(prefix + ".draw" +
                                   std::to_string(
                                       std::min(point + 2, prism::drawPoints)));
              v = a + (b - a) * (pos - point);
            }
          } else {
            if (shape == 1)
              v = phase < .5 ? 4 * phase - 1 : 3 - 4 * phase;
            else if (shape == 2)
              v = 1 - 2 * phase;
            else if (shape == 3)
              v = phase < .5 ? 1 : -1;
            else if (shape >= 4)
              v = held[std::min(7, static_cast<int>(phase * 4))];
            if (type == "derive-synth" && shape == 5)
              v = 2 * phase - 1;
            else if (type == "derive-synth" && shape == 6) {
              const auto t = phase * 4;
              const auto from = static_cast<int>(t) % 8;
              v = held[from] + (held[(from + 1) % 8] - held[from]) *
                                   (.5 - .5 * std::cos(std::numbers::pi *
                                                       (t - std::floor(t))));
            }
          }
          samples.push_back(static_cast<float>(v));
        }
      } else if (spec.picture == Picture::steps) {
        const auto stem = param(prefix + ".step1") ? ".step" : ".point";
        const auto count = std::clamp(
            static_cast<int>(value(
                prefix + (stem == std::string(".step") ? ".steps" : ".points"),
                16)),
            1, 16);
        for (int i = 1; i <= count; ++i) {
          const auto key = prefix + stem + std::to_string(i);
          const auto *p = param(key);
          if (!p)
            continue;
          const auto normalized = (p->value - p->minimum) /
                                  std::max(.0001, p->maximum - p->minimum);
          const auto x = plot.x + pw * (i - 1) / count;
          list->AddRectFilled(
              ImVec2(x, plot.y + ph * (1 - static_cast<float>(std::clamp(
                                               normalized, 0.0, 1.0)))),
              ImVec2(x + pw / count - 1, plot.y + ph), colour);
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##steps", pictureSize);
        if (interactive && ImGui::IsItemActive()) {
          const auto mouse = ImGui::GetIO().MousePos;
          const auto slot = std::clamp(
              static_cast<int>((mouse.x - plot.x) / pw * count), 0, count - 1);
          const auto key = prefix + stem + std::to_string(slot + 1);
          if (const auto *p = param(key))
            apply(key, p->minimum + (1 - std::clamp((mouse.y - plot.y) / ph,
                                                    0.0f, 1.0f)) *
                                        (p->maximum - p->minimum));
        }
      } else if (spec.picture == Picture::orb) {
        const auto centre = ImVec2(plot.x + pw * .5f, plot.y + ph * .5f);
        const auto radius = std::min(pw, ph) * .4f;
        list->AddCircle(centre, radius, theme::alpha(colour, .5f));
        list->AddCircleFilled(
            ImVec2(centre.x + radius * static_cast<float>(value(prefix + ".x")),
                   centre.y -
                       radius * static_cast<float>(value(prefix + ".y"))),
            dp(4), colour);
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##orb", pictureSize);
        if (interactive && ImGui::IsItemActive()) {
          const auto mouse = ImGui::GetIO().MousePos;
          apply(prefix + ".x", (mouse.x - centre.x) / std::max(1.0f, radius));
          apply(prefix + ".y", (centre.y - mouse.y) / std::max(1.0f, radius));
        }
      }
      if (samples.size() > 1) {
        std::vector<ImVec2> line;
        for (std::size_t i = 0; i < samples.size(); ++i)
          line.emplace_back(
              plot.x + pw * static_cast<float>(i) /
                           static_cast<float>(samples.size() - 1),
              plot.y + ph * (.5f - .45f * std::clamp(samples[i], -1.0f, 1.0f)));
        list->AddPolyline(line.data(), static_cast<int>(line.size()), colour,
                          dp(1.7f));
      }
    }
    ImGui::SetCursorScreenPos(
        ImVec2(origin.x, origin.y + pictureSize.y + dp(5)));
    ImGui::Dummy(ImVec2(0, 0));
  }
  float used = 0, rowHeight = 0;
  origin = ImGui::GetCursorScreenPos();
  for (const auto &setting : spec.settings) {
    const auto *p = param(setting.key);
    const auto v = value(setting.key, setting.initial);
    const auto condition = value(setting.condition);
    const auto enabled =
        setting.condition.empty() ||
        (condition >= setting.from - 1e-4 && condition <= setting.to + 1e-4);
    if (!enabled && setting.hide)
      continue;
    const bool choice = setting.kind == 3, toggle = setting.kind == 2;
    const auto cell = choice   ? std::min(width, dp(110))
                      : toggle ? std::min(width, dp(90))
                               : std::min(width, dp(76));
    const auto height =
        choice || toggle ? ImGui::GetFrameHeight() + dp(20) : dp(56);
    if (used > 0 && (setting.newLine || used + cell > width)) {
      origin.y += rowHeight + dp(4);
      used = 0;
      rowHeight = 0;
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x + used, origin.y));
    ImGui::PushID(setting.key.c_str());
    ImGui::BeginDisabled(!p || !enabled);
    if (choice) {
      const auto selected = static_cast<int>(std::lround(v));
      const auto text =
          selected >= 0 && selected < static_cast<int>(setting.choices.size())
              ? setting.choices[static_cast<std::size_t>(selected)]
              : std::to_string(selected);
      ImGui::TextUnformatted(setting.label.c_str());
      ImGui::SetNextItemWidth(cell - dp(4));
      if (ImGui::BeginCombo("##choice", text.c_str())) {
        for (std::size_t i = 0; i < setting.choices.size(); ++i)
          if (ImGui::Selectable(setting.choices[i].c_str(),
                                selected == static_cast<int>(i)))
            apply(setting.key, static_cast<double>(i));
        ImGui::EndCombo();
      }
    } else if (toggle) {
      auto on = v > .5;
      if (ImGui::Checkbox(setting.label.c_str(), &on))
        apply(setting.key, on ? 1 : 0);
    } else {
      auto next = static_cast<float>(v);
      ui::KnobStyle style;
      style.diameter = 36;
      style.label = setting.label.c_str();
      style.labelWidth = cell;
      style.unit = p ? p->unit.toRawUTF8() : setting.unit.c_str();
      style.colour = colour;
      if (setting.logarithmic && p && p->minimum > 0)
        style.flags |= ui::knobLogarithmic;
      if (setting.kind == 1)
        style.flags |= ui::knobInteger;
      const auto lo = p ? p->minimum : setting.minimum,
                 hi = p ? p->maximum : setting.maximum;
      if (ui::knob(
              "##value", &next, static_cast<float>(lo), static_cast<float>(hi),
              static_cast<float>(p ? p->defaultValue : setting.initial), style))
        apply(setting.key, next);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", (!p         ? "Bind this setting in the designer"
                               : !enabled ? "Inactive for the current mode"
                                          : setting.key.c_str()));
    ImGui::PopID();
    used += cell;
    rowHeight = std::max(rowHeight, height);
  }
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowHeight));
  ImGui::Dummy(ImVec2(0, 0));
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::SetCursorScreenPos(kept);
  ImGui::Dummy(ImVec2(0, 0));
  ImGui::PopStyleVar(3);
  ImGui::PopFont();
  uiStyle.FontScaleDpi = dpi;
}
} // namespace cydaw::v2::instrument_ui
