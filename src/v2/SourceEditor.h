#pragma once
#include <cyui/modules/CodeEditor.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace cydaw::v2 {
// EEL2 is an application language extension of CyUI's shared lexer registry.
inline cyui::CodeLanguage eelLanguage()
{
    static const auto language = [] {
        cyui::CodeLanguageDefinition definition;
        definition.id = "cydaw-eel2"; definition.name = "JSFX / EEL2"; definition.extensions = {"jsfx", "jsfx-inc"};
        cyui::CodeSyntaxRules rules;
        rules.lineComment = "//"; rules.blockCommentStart = "/*"; rules.blockCommentEnd = "*/";
        rules.keywords = {"loop", "while", "function", "local", "instance", "global", "options", "import"};
        rules.functions = {"sin", "cos", "tan", "sqrt", "pow", "exp", "log", "abs", "min", "max", "floor", "ceil", "midirecv", "midisend", "memcpy", "memset"};
        definition.rules = rules;
        return cyui::registerCodeLanguage(definition);
    }();
    return language;
}
class SourceEditor {
public:
    bool draw(const char* id, std::vector<char>& buffer, ImVec2 size, cyui::CodeLanguage language)
    {
        const std::string source(buffer.empty() ? "" : buffer.data());
        const auto key = ImGui::GetID(id);
        if (!ready_ || key != key_ || source != model_) {
            editor_.setText(source); model_ = source; key_ = key; ready_ = true; dirty_ = false;
        }
        if (editor_.language() != language) editor_.setLanguage(language);
        auto* parent = ImGui::GetCurrentWindow();
        const auto changed = editor_.draw(id, size);
        if (changed) {
            model_ = editor_.text();
            buffer.assign(std::max(buffer.size(), model_.size() + 4096), 0);
            std::memcpy(buffer.data(), model_.data(), model_.size());
            dirty_ = true;
        }
        focused_ = false;
        for (auto* window = ImGui::GetCurrentContext()->NavWindow; window; window = window->ParentWindow)
            if (window->ParentWindow == parent && window->ChildId == key) focused_ = true;
        committed_ = dirty_ && !focused_;
        if (committed_) dirty_ = false;
        if (focused_) ImGui::SetNextFrameWantCaptureKeyboard(true);
        return changed;
    }
    bool focused() const { return focused_; }
    bool committed() const { return committed_; }
private:
    cyui::CodeEditor editor_;
    std::string model_;
    ImGuiID key_ {};
    bool ready_ {}, dirty_ {}, focused_ {}, committed_ {};
};
} // namespace cydaw::v2
