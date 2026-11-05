#pragma once
#include "ManualContent.h"
#include <cyui/modules/DocumentViews.h>
#include <cyui/modules/Images.h>
#include <map>
#include <optional>

namespace cydaw::v2 {
class Studio; class ImGuiHost;
class ManualView {
public:
    ManualView(Studio&, ImGuiHost&);
    ~ManualView();
    void open(const juce::String& topic = {});
    void draw();
    bool focused() const { return visible_ && focused_; }
    juce::var command(const juce::var&);
private:
    struct Place { juce::String topic, section; float scroll {}; };
    struct Texture { juce::String key; unsigned long long id {}; int width {}, height {}, frame {}; };
    Studio& studio_; ImGuiHost& host_;
    ManualContent content_;
    bool loaded_ {}, visible_ {}, focusRequested_ {}, focused_ {}, showFigures_ {true}, enlarge_ {};
    juce::String language_ {"en"}, topic_ {"welcome"}, section_ {"overview"}, documentKey_;
    float textScale_ {1}, scroll_ {};
    char query_[256] {};
    juce::String indexedQuery_;
    std::vector<ManualHit> hits_;
    std::vector<Place> history_;
    int historyAt_ {-1}, figureIndex_ {};
    std::optional<float> restoreScroll_;
    cyui::Document document_;
    cyui::DocumentState documentState_;
    cyui::ImageState imageState_;
    std::vector<Texture> textures_;
    juce::Rectangle<float> bounds_, searchBounds_, imageBounds_, languageBounds_, zoomBounds_;
    std::vector<juce::Rectangle<float>> figureBounds_;
    const char* text(const char* fr, const char* en) const { return language_ == "fr" ? fr : en; }
    void load();
    void remember();
    bool navigate(const juce::String& topic, const juce::String& section = "overview", bool record = true);
    void history(int direction);
    void setLanguage(const juce::String&);
    void drawDocument();
    void drawFigures(bool rail);
    void drawEnlarged();
    void follow(const juce::String&);
    std::vector<ManualFigure> figures() const;
    Texture image(const juce::File&);
};
} // namespace cydaw::v2
