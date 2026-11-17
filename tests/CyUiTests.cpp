#include "Theme.h"
#include "Widgets.h"
#include "SourceEditor.h"
#include <cyui/modules/AudioViews.h>
#include <cyui/modules/Images.h>
#include <cyui/modules/Inspector.h>
#include <cyui/modules/Nodes.h>
#include <cstdio>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <limits>

using namespace cydaw::v2;
namespace {
int failures = 0;
float controlValue = .25f;
bool editedControl = false;
void check(bool value, const char* message) {
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
void initialize(ImGuiContext* context) {
    ImGui::SetCurrentContext(context);
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.DisplaySize = {1024, 768}; io.DeltaTime = 1.f / 60;
    theme::apply(1); theme::loadFonts();
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height); io.Fonts->SetTexID(ImTextureID(1));
}
void frame(SourceEditor& editor, std::vector<char>& source) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({1000, 740});
    ImGui::Begin("Migration tests", nullptr, ImGuiWindowFlags_NoSavedSettings);
    float value = .25f;
    ImGui::BeginDisabled(); ui::knob("disabled", &value, 0, 1, .5f); ImGui::EndDisabled();
    check(value == .25f, "disabled audio controls preserve the model");
    ui::meter("meter", {100, 25}, .7f, .6f, .4f, .3f, false);
    ImGui::SetCursorScreenPos({20,60});
    ui::knob("active", &controlValue, 0, 1, .5f);
    editedControl = ImGui::IsItemEdited();
    ImGui::SetCursorScreenPos({20,150});
    editor.draw("source", source, {500, 160}, eelLanguage());
    const std::vector<cyui::Point> points{{0,0},{.5,.7},{1,.2}};
    const cyui::PlotSeries series{1,"signal", points};
    cyui::PlotState view{{{0,1},false},{{-1,1},false}};
    cyui::PlotOptions options; options.navigation = false;
    cyui::oscilloscope("scope", view, {&series,1}, {500,100}, options);
    ImGui::Dummy({0,0}); ImGui::End(); cyui::renderNotifications(); ImGui::Render();
    check(ImGui::GetDrawData()->TotalVtxCount > 0, "migrated views generate drawing commands");
}

void audioViews() {
    cyui::SpectrogramHistory history;
    check(history.reset(2, 2, .1, {20,20000}, true), "spectrogram history accepts bounded log-frequency data");
    const float first[] {-80,-60}, second[] {-40,-20}, third[] {-10,0};
    history.append({first,2}); history.append({second,2}); history.append({third,2});
    const auto data = history.view();
    check(data.frames==2 && data.bins==2 && data.values[0]==-40 && data.values[3]==0,
          "spectrogram ring exposes chronological frames after wrapping");
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({1000,740});
    ImGui::Begin("Audio analysis views", nullptr, ImGuiWindowFlags_NoSavedSettings);
    cyui::PlotState spectrumView{{{20,20000},true},{{-90,6},false}};
    const cyui::Point spectrum[] {{20,-80},{1000,-10},{20000,-60}};
    cyui::PlotSeries series{1,"prepared spectrum", {spectrum,3}};
    series.style=cyui::PlotStyle::area; series.baseline=-90; series.gradient=true; series.fillAlpha=.3f;
    cyui::PlotOptions plotOptions; plotOptions.navigation=false;
    cyui::spectrum("filled spectrum", spectrumView, {&series,1}, {400,100}, plotOptions);
    cyui::PlotState timeView{{data.time,false},{data.frequency,true}};
    cyui::SpectrogramOptions waterfall; waterfall.orientation=cyui::SpectrogramOrientation::timeVertical;
    waterfall.navigation=false; waterfall.legend=true; waterfall.maxCells=32;
    check(cyui::spectrogram("waterfall",timeView,data,{400,150},waterfall).valid,
          "vertical spectrogram renders the host's chronological history");
    cyui::StereoDensityState density;
    cyui::StereoDensityOptions options; options.persistence.resolution=64;
    options.persistence.navigation=false; options.persistence.diffusion=0;
    const cyui::Point samples[] {{.2,.4},{-.3,-.5},{std::numeric_limits<double>::quiet_NaN(),0}};
    cyui::stereoDensity("density first",density,{samples,3},1,{160,160},options);
    const auto energy=std::accumulate(density.density.energy.begin(),density.density.energy.end(),0.0);
    cyui::stereoDensity("density repeat",density,{samples,3},1,{160,160},options);
    check(energy>0 && energy==std::accumulate(density.density.energy.begin(),density.density.energy.end(),0.0),
          "same audio revision is not injected twice into persistent density");
    check(std::all_of(density.density.energy.begin(),density.density.energy.end(),
                      [](float v){return std::isfinite(v);}), "nonfinite input cannot poison stereo persistence");
    cyui::stereoDensity("density fresh",density,{samples,3},2,{160,160},options);
    check(std::accumulate(density.density.energy.begin(),density.density.energy.end(),0.0)>energy,
          "a fresh revision adds energy even when the audio samples are identical");
    check(density.density.energy.size()==64*64, "stereo persistence storage respects the host's resolution budget");
    cyui::StereoDensityState independent;
    check(independent.density.energy.empty(), "each panel owns independent persistence");
    ImGui::End(); ImGui::Render();
    check(ImGui::GetDrawData()->TotalVtxCount>0,"new audio views generate drawing commands");
}

}
int main() {
    auto* first = ImGui::CreateContext(); initialize(first);
    auto custom = theme::palette(); custom.audio = cyui::rgb(0x123456); custom.bg1 = cyui::rgb(0x222333);
    custom.meterLow = cyui::rgb(0x228844);
    theme::setPalette(custom);
    check(cyui::palette().success == custom.meterLow, "legacy meter theme colors reach common audio controls");
    check(theme::paletteFromJson(theme::paletteToJson(custom), custom).audio == custom.audio,
          "existing CyDAW JSON themes retain application signal roles");
    auto* second = ImGui::CreateContext(); initialize(second);
    theme::apply(2);
    const auto other = theme::palette().bg1;
    ImGui::SetCurrentContext(first);
    check(theme::palette().bg1 == custom.bg1 && theme::palette().audio == custom.audio, "palettes are isolated by ImGui context");
    check(std::abs(theme::dpi()-1.f) < 1.e-5f, "DPI is isolated by ImGui context");
    ImGui::SetCurrentContext(second);
    check(theme::palette().bg1 == other && std::abs(theme::dpi()-2.f)<1.e-5f, "second context keeps its own DPI");
    ImGui::DestroyContext(second); ImGui::SetCurrentContext(first);
    {
        SourceEditor editor;
        const std::string text = "// UTF-8: éclair\r\n@sample\r\n\tspl0 *= .5;\r\n";
        std::vector<char> buffer(text.begin(), text.end()); buffer.push_back(0);
        for (const auto& entry : cyui::builtInThemes())
            for (auto density : {cyui::Density::compact, cyui::Density::comfortable}) {
                cyui::Config config; config.themeKey=entry.key.c_str(); config.density=density;
                cyui::apply(config); frame(editor, buffer); frame(editor, buffer);
                check(std::string(buffer.data()) == text, "source rendering preserves UTF-8, tabs, CRLF and trailing newline");
            }
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(42,70); frame(editor, buffer); frame(editor, buffer);
        io.AddMouseWheelEvent(0,1); frame(editor, buffer);
        check(controlValue > .25f && editedControl && ui::wheelTaken(), "audio wheel edit marks the item and consumes scrolling");
        io.AddMousePosEvent(80,170); io.AddMouseButtonEvent(0,true); frame(editor, buffer);
        io.AddMouseButtonEvent(0,false); frame(editor, buffer);
        check(editor.focused(), "source editor owns keyboard focus in its child window");
        io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_End, true); frame(editor, buffer);
        io.AddKeyEvent(ImGuiKey_End, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); frame(editor, buffer);
        io.AddInputCharactersUTF8("é"); frame(editor, buffer); frame(editor, buffer);
        check(std::string(buffer.data()) != text, "typing edits the application's source buffer");
        io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_Z, true); frame(editor, buffer);
        io.AddKeyEvent(ImGuiKey_Z, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); frame(editor, buffer);
        check(std::string(buffer.data()) == text, "source Undo restores the original UTF-8 and CRLF bytes");
        cyui::CodeEditor document;
        document.setText(text);
        check(document.text()==text, "native editor preserves exact source round-trip");
        const cyui::CanvasView canvas{{1000000000., 0}, 5};
        auto zoomed=canvas; const auto before=canvas.world({100,50}); zoomed.zoomAt({100,50},17);
        check(std::abs(before.x-zoomed.world({100,50}).x)<1.e-6, "double-precision navigation retains pointer anchor");
    }
    audioViews();
    ImGui::DestroyContext(first);
    std::printf("CyDAW/CyUI checks: %d failures\n", failures);
    return failures ? 1 : 0;
}
