#pragma once
#include "Studio.h"
#include "UiState.h"
namespace cydaw::v2
{
class JsfxView final
{
  public:
    JsfxView(Studio &, UiState &);
    ~JsfxView();
    void draw();
    juce::var command(const juce::var &);
    static bool askBeforeQuit(const std::function<void()> &next);
    static void drawQuitPrompts();

  private:
    class Compiler;
    Studio &studio_;
    UiState &state_;
    std::unique_ptr<Compiler> compiler_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::File file_;
    std::vector<char> code_;
    bool changed_{}, applyAfterCompile_{}, insertAfterCompile_{};
    juce::String status_;
    bool error_{};
    std::function<void()> pending_;
    bool quitting_{};
    int drawnFrame_{-1};
    void load(const juce::File &);
    void fresh();
    bool save();
    void choose(bool saving);
    void compile(bool apply, bool insert = false);
    void poll();
    void request(std::function<void()>);
};
} // namespace cydaw::v2
