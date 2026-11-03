#include "Theme.h"
#include "Widgets.h"
#include "SourceEditor.h"
#include <cyui/modules/AudioViews.h>
#include <cyui/modules/Images.h>
#include <cyui/modules/Inspector.h>
#include <cyui/modules/Nodes.h>
#include <cstdio>
#include <cmath>

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
    ImGui::DestroyContext(first);
    std::printf("CyDAW/CyUI checks: %d failures\n", failures);
    return failures ? 1 : 0;
}
