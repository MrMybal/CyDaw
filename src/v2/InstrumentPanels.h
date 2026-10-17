#pragma once
#include "DeviceVisuals.h"
#include "SynthLayouts.h"
#include <string>
#include <vector>

// Reusable instrument sections. They use the native model layouts and
// parameter tables, and depend only on parameter bindings, never a track.
namespace cydaw::v2::instrument_ui {
struct Setting {
  std::string key, label, unit, condition;
  double minimum{}, maximum{1}, initial{}, from{}, to{};
  int kind{};
  bool logarithmic{}, newLine{}, hide{};
  std::vector<std::string> choices;
};
struct Panel {
  std::string id, title, page, pictureKey, secondKey;
  int row{};
  float weight{1};
  ImU32 colour{};
  synth_layout::Picture picture{synth_layout::Picture::none};
  std::vector<Setting> settings;
};
[[nodiscard]] const std::vector<Panel> &panels(const std::string &type,
                                               bool compact);
[[nodiscard]] const Panel *panel(const std::string &type, const std::string &id,
                                 bool compact);
// These equations also draw the built-in SynthView and PrismView.
[[nodiscard]] double filterResponse(engine::synth::Model model,
                                    const engine::synth::Values &values,
                                    double hz);
[[nodiscard]] double filterResponse(const engine::prism::PrismValues &values,
                                    double hz);
// keys and parameters share their indices; a missing binding is an empty Param.
void draw(const char *id, const std::string &type, const Panel &panel,
          const std::vector<std::string> &keys,
          std::vector<visual::Param> &parameters, ImVec2 at, ImVec2 size,
          ImU32 colour, const visual::Setter &set, bool interactive,
          float scale = 1.0f, const char *title = nullptr);
} // namespace cydaw::v2::instrument_ui
