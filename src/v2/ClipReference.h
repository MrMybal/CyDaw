#pragma once

#include <functional>

struct ImDrawList;
struct ImVec2;

namespace cydaw::v2 {
class Studio;
class WaveformCache;
class ImGuiHost;
struct TrackRef;
struct UiState;

namespace clip_reference {
void button(Studio& studio, UiState& state, const TrackRef& edited);
void draw(Studio& studio, const UiState& state, const TrackRef& edited, WaveformCache& waveforms,
          ImGuiHost& host, ImDrawList& list, ImVec2 minimum, ImVec2 maximum,
          const std::function<float(double)>& projectToX,
          const std::function<float(int)>& noteToY = {}, float noteHeight = 0.0f);
}
}
