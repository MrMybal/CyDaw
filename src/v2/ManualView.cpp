#include "ManualView.h"
#include "Studio.h"
#include "ImGuiHost.h"
#include "Theme.h"
#include "Widgets.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cfloat>

namespace cydaw::v2 {
using ui::dp;
namespace {
bool wrappedChoice(const char* id, const juce::String& label, bool selected) {
    const auto width = std::max(1.f, ImGui::GetContentRegionAvail().x);
    const auto* value = label.toRawUTF8();
    const auto height = ImGui::CalcTextSize(value, nullptr, false, width).y;
    const auto pos = ImGui::GetCursorScreenPos();
    const auto pressed = ImGui::Selectable(id, selected, ImGuiSelectableFlags_None, {width, height});
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos,
        ImGui::GetColorU32(ImGuiCol_Text), value, nullptr, width);
    return pressed;
}
}

ManualView::ManualView(Studio& studio, ImGuiHost& host) : studio_(studio), host_(host) {
    language_ = studio_.settings().getValue("manualLanguage", "en");
    if (language_ != "fr") language_ = "en";
    topic_ = studio_.settings().getValue("manualTopic", "welcome");
    section_ = studio_.settings().getValue("manualSection", "overview");
    textScale_ = std::clamp(float(studio_.settings().getDoubleValue("manualTextScale", 1)), .85f, 1.6f);
}
ManualView::~ManualView() { for (const auto& t : textures_) host_.releaseTexture(t.key); }
void ManualView::load() {
    if (loaded_) return; loaded_ = true;
    auto root = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("docs").getChildFile("manual");
    if (!root.getChildFile("manifest.json").existsAsFile()) root = juce::File::getCurrentWorkingDirectory().getChildFile("docs/manual");
    content_.load(root);
    if (!content_.section(language_, topic_, section_)) { topic_ = "welcome"; section_ = "overview"; }
    navigate(topic_, section_);
}
void ManualView::remember() {
    if (!studio_.keepsSettings()) return;
    auto& settings = studio_.settings();
    settings.setValue("manualLanguage", language_); settings.setValue("manualTopic", topic_);
    settings.setValue("manualSection", section_); settings.setValue("manualTextScale", textScale_);
}
void ManualView::open(const juce::String& topic) {
    load(); visible_ = focusRequested_ = true;
    if (topic.isNotEmpty()) navigate(topic);
    host_.wake();
}
bool ManualView::navigate(const juce::String& topic, const juce::String& section, bool record) {
    if (!content_.section(language_, topic, section)) return false;
    if (record) {
        if (historyAt_ >= 0 && historyAt_ < int(history_.size())) history_[size_t(historyAt_)].scroll = scroll_;
        if (historyAt_ < 0 || topic_ != topic || section_ != section) {
            history_.resize(size_t(historyAt_ + 1)); history_.push_back({topic, section, 0});
            if (history_.size() > 100) history_.erase(history_.begin());
            historyAt_ = int(history_.size()) - 1;
        }
    }
    topic_ = topic; section_ = section; restoreScroll_ = 0; documentKey_.clear(); figureIndex_ = 0;
    remember(); return true;
}
void ManualView::history(int direction) {
    const auto next = historyAt_ + direction; if (next < 0 || next >= int(history_.size())) return;
    history_[size_t(historyAt_)].scroll = scroll_; const auto target = history_[size_t(next)]; historyAt_ = next;
    navigate(target.topic, target.section, false); restoreScroll_ = target.scroll;
}
void ManualView::setLanguage(const juce::String& value) {
    if (value != "en" && value != "fr") return;
    language_ = value; documentKey_.clear(); indexedQuery_.clear(); enlarge_ = false;
    restoreScroll_ = 0; remember();
}
std::vector<ManualFigure> ManualView::figures() const {
    if (const auto* s = content_.section(language_, topic_, section_); s && !s->figures.empty()) return s->figures;
    if (const auto* s = content_.section(language_, topic_, "overview")) return s->figures;
    return {};
}
ManualView::Texture ManualView::image(const juce::File& file) {
    const auto key = "manual:" + file.getFullPathName();
    for (auto& cached : textures_) if (cached.key == key) { cached.frame = ImGui::GetFrameCount(); return cached; }
    juce::Image pixels;
    if (file.hasFileExtension("svg")) {
        if (const auto xml = juce::XmlDocument::parse(file)) if (auto drawing = juce::Drawable::createFromSVG(*xml)) {
            const auto original = drawing->getDrawableBounds();
            const auto width = 1400, height = std::clamp(int(1400 * original.getHeight() / std::max(1.f, original.getWidth())), 100, 1400);
            pixels = juce::Image(juce::Image::ARGB, width, height, true); juce::Graphics g(pixels);
            drawing->drawWithin(g, {0, 0, float(width), float(height)}, juce::RectanglePlacement::centred, 1);
        }
    } else pixels = juce::ImageFileFormat::loadFrom(file);
    if (!pixels.isValid()) return {};
    if (textures_.size() >= 10) {
        const auto oldest = std::min_element(textures_.begin(), textures_.end(), [](const auto& a, const auto& b){ return a.frame < b.frame; });
        host_.releaseTexture(oldest->key); textures_.erase(oldest);
    }
    Texture result {key, host_.texture(key, pixels), pixels.getWidth(), pixels.getHeight(), ImGui::GetFrameCount()};
    textures_.push_back(result); return result;
}
void ManualView::follow(const juce::String& link) {
    juce::String topic, section;
    if (content_.resolve(link, topic_, topic, section)) navigate(topic, section);
    else if (link.startsWith("https://")) (void)juce::URL(link).launchInDefaultBrowser();
}
void ManualView::drawDocument() {
    const auto* topic = content_.topic(language_, topic_); const auto* section = content_.section(language_, topic_, section_);
    if (!topic || !section) { ImGui::TextWrapped("%s", content_.error().toRawUTF8()); return; }
    const auto key = language_ + ":" + topic_ + ":" + section_;
    if (documentKey_ != key) { document_.setMarkdown(section->markdown.toStdString()); documentState_ = {}; documentKey_ = key; }
    ImGui::TextDisabled("%s", topic->title.toRawUTF8());
    ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 1.25f);
    ImGui::TextWrapped("%s", section->title.toRawUTF8()); ImGui::PopFont();
    ImGui::Separator();
    auto* parent = ImGui::GetCurrentWindow();
    ImGui::PushID("##manualText"); const auto child = ImGui::GetID("##document"); ImGui::PopID();
    ImGui::PushFont(theme::uiFont(), ui::currentFontBase() * textScale_);
    const auto result = cyui::documentView("##manualText", documentState_, document_);
    ImGui::PopFont();
    for (auto* window : ImGui::GetCurrentContext()->Windows) if (window->ParentWindow == parent && window->ChildId == child) {
        if (restoreScroll_) { ImGui::SetScrollY(window, *restoreScroll_); restoreScroll_.reset(); }
        scroll_ = window->Scroll.y; break;
    }
    if (!result.link.empty()) follow(juce::String::fromUTF8(result.link.c_str()));
}
void ManualView::drawFigures(bool rail) {
    const auto items = figures(); if (items.empty()) return;
    figureIndex_ = std::clamp(figureIndex_, 0, int(items.size()) - 1);
    if (!rail && items.size() > 1) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##manualFigureChoice", items[size_t(figureIndex_)].caption.toRawUTF8())) {
            for (size_t i = 0; i < items.size(); ++i)
                if (ImGui::Selectable(items[i].caption.toRawUTF8(), figureIndex_ == int(i))) figureIndex_ = int(i);
            ImGui::EndCombo();
        }
    }
    ImGui::TextDisabled("%s", text("Illustrations — cliquer pour agrandir", "Figures — click to enlarge"));
    const auto width = ImGui::GetContentRegionAvail().x;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!rail && int(i) != figureIndex_) continue;
        const auto texture = image(items[i].file); if (!texture.id) { ImGui::TextWrapped("%s", text("Image indisponible", "Image unavailable")); continue; }
        const auto height = std::min(dp(rail ? 240.f : 150.f), width * float(texture.height) / float(texture.width));
        const auto size = ImVec2(height * float(texture.width) / float(texture.height), height);
        ImGui::Image(ImTextureRef(ImTextureID(texture.id)), size);
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        figureBounds_.push_back({min.x, min.y, max.x-min.x, max.y-min.y});
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { enlarge_ = true; figureIndex_ = int(i); imageState_.fitPending = true; }
        ImGui::TextWrapped("%s", items[i].caption.toRawUTF8());
        if (!rail) break;
        ImGui::Spacing();
    }
}
void ManualView::drawEnlarged() {
    if (!enlarge_) return; const auto items = figures(); if (items.empty()) { enlarge_ = false; return; }
    figureIndex_ = std::clamp(figureIndex_, 0, int(items.size()) - 1);
    const auto& figure = items[size_t(figureIndex_)];
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(dp(980), viewport->WorkSize.x * .94f),
        std::min(dp(680), viewport->WorkSize.y * .94f)), ImGuiCond_FirstUseEver);
    if (ImGui::Begin((juce::String(text("Illustration", "Figure")) + "###manualFigure").toRawUTF8(), &enlarge_)) {
        focused_ = focused_ || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::TextWrapped("%s", figure.caption.toRawUTF8());
        ImGui::TextDisabled("%s", text("Molette : zoom · glisser au milieu ou Espace+glisser : déplacer · double-clic : cadrer", "Wheel: zoom · middle-drag or Space+drag: pan · double-click: fit"));
        const auto texture = image(figure.file);
        cyui::ImageData data; data.texture = ImTextureID(texture.id); data.width = texture.width; data.height = texture.height;
        const auto top = ImGui::GetCursorScreenPos(), available = ImGui::GetContentRegionAvail();
        imageBounds_ = {top.x, top.y, available.x, available.y};
        cyui::imageViewport("##manualFigureImage", imageState_, data, available);
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) enlarge_ = false;
    }
    ImGui::End();
}
void ManualView::draw() {
    focused_ = false; figureBounds_.clear(); if (!visible_) return; load();
    const auto oldLocale = cyui::locale(); cyui::setLocale(language_.toRawUTF8());
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(dp(1320), viewport->WorkSize.x * .94f), std::min(dp(850), viewport->WorkSize.y * .94f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(dp(620), dp(380)), ImVec2(FLT_MAX, FLT_MAX));
    if (focusRequested_) { ImGui::SetNextWindowFocus(); focusRequested_ = false; }
    if (ImGui::Begin((juce::String(text("Manuel de CyDAW", "CyDAW User Manual")) + "###manual").toRawUTF8(), &visible_)) {
        const auto pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize(); bounds_ = {pos.x,pos.y,size.x,size.y};
        focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::BeginDisabled(historyAt_ <= 0); if (ImGui::Button("<##back")) history(-1); ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::BeginDisabled(historyAt_ + 1 >= int(history_.size())); if (ImGui::Button(">##forward")) history(1); ImGui::EndDisabled();
        ImGui::SameLine(); if (ImGui::Button(text("Accueil", "Home"))) navigate("welcome");
        ImGui::SameLine(); ImGui::SetNextItemWidth(dp(130));
        int language = language_ == "fr" ? 1 : 0;
        if (ImGui::Combo("##manualLanguage", &language, "English\0Français\0")) setLanguage(language ? "fr" : "en");
        { const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); languageBounds_ = {a.x,a.y,b.x-a.x,b.y-a.y}; }
        ImGui::SameLine(); ImGui::SetNextItemWidth(dp(105));
        if (ImGui::SliderFloat("##manualZoom", &textScale_, .85f, 1.6f, text("Texte %.2fx", "Text %.2fx"))) remember();
        { const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); zoomBounds_ = {a.x,a.y,b.x-a.x,b.y-a.y}; }
        ImGui::SameLine(); ImGui::Checkbox(text("Figures", "Figures"), &showFigures_);
        if (focused_ && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) ImGui::SetKeyboardFocusHere();
        const bool hadQueryBeforeInput = query_[0] != 0;
        ImGui::SetNextItemWidth(-dp(56)); ImGui::InputTextWithHint("##manualSearch", text("Rechercher dans tout le manuel…", "Search the entire manual…"), query_, sizeof(query_));
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); searchBounds_ = {a.x,a.y,b.x-a.x,b.y-a.y};
        ImGui::SameLine(); if (ImGui::Button(text("Effacer", "Clear"))) query_[0] = 0;
        const auto query = juce::String::fromUTF8(query_).trim();
        if (indexedQuery_ != language_ + ":" + query) { hits_ = content_.search(language_, query); indexedQuery_ = language_ + ":" + query; }
        if (focused_ && !enlarge_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) { if (hadQueryBeforeInput || query_[0]) query_[0] = 0; else visible_ = false; }
        ImGui::Separator();
        const auto rail = ImGui::GetContentRegionAvail().x > dp(1100);
        const auto tocWidth = std::clamp(ImGui::GetContentRegionAvail().x * .24f, dp(175), dp(270));
        ImGui::BeginChild("##manualContents", ImVec2(tocWidth, 0), ImGuiChildFlags_Borders);
        if (query.isNotEmpty()) {
            ImGui::Text("%d %s", int(hits_.size()), text("résultat(s)", "result(s)"));
            if (hits_.empty()) ImGui::TextWrapped("%s", text("Aucun résultat. Essayez moins de mots ou un nom de module.", "No results. Try fewer words or a module name."));
            int n = 0; for (const auto& hit : hits_) {
                ImGui::PushID(n++);
                if (wrappedChoice("##result", hit.title, topic_ == hit.topic && section_ == hit.section)) navigate(hit.topic, hit.section);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hit.excerpt.toRawUTF8()); ImGui::PopID();
            }
        } else {
            juce::String group;
            for (const auto& topic : content_.topics(language_)) {
                if (topic.group != group) { group = topic.group; ImGui::Spacing(); ImGui::TextDisabled("%s", group.toRawUTF8()); }
                ImGui::PushID(topic.id.toRawUTF8());
                if (wrappedChoice("##topic", topic.title, topic_ == topic.id)) navigate(topic.id);
                if (topic_ == topic.id) { ImGui::Indent(dp(10)); for (const auto& section : topic.sections)
                    if (wrappedChoice(("##" + section.id).toRawUTF8(), section.title, section_ == section.id)) navigate(topic.id, section.id);
                    ImGui::Unindent(dp(10)); }
                ImGui::PopID();
            }
        }
        ImGui::EndChild(); ImGui::SameLine();
        const auto figureWidth = showFigures_ && rail ? dp(300) : 0;
        ImGui::BeginChild("##manualReading", ImVec2(ImGui::GetContentRegionAvail().x - (figureWidth ? figureWidth + ImGui::GetStyle().ItemSpacing.x : 0), 0));
        if (showFigures_ && !rail && ImGui::CollapsingHeader(text("Illustration", "Illustration"), ImGuiTreeNodeFlags_DefaultOpen)) drawFigures(false);
        if (content_.error().isNotEmpty()) {
            ImGui::TextWrapped("%s", content_.error().toRawUTF8());
            if (ImGui::Button(text("Recharger", "Reload"))) loaded_ = false;
            ImGui::TextWrapped("%s", text("Réinstallez le dossier docs/manual à côté de l'exécutable.", "Restore the docs/manual folder beside the executable."));
        } else drawDocument();
        ImGui::EndChild();
        if (figureWidth) { ImGui::SameLine(); ImGui::BeginChild("##manualFigures", ImVec2(figureWidth, 0), ImGuiChildFlags_Borders); drawFigures(true); ImGui::EndChild(); }
    }
    ImGui::End(); drawEnlarged(); cyui::setLocale(oldLocale.c_str());
}
juce::var ManualView::command(const juce::var& request) {
    load();
    const auto action = request.getProperty("action", "status").toString();
    if (request.hasProperty("language")) {
        const auto value = request.getProperty("language", {}).toString();
        if (value != "en" && value != "fr") return "language: en or fr";
        setLanguage(value);
    }
    if (action == "open") open();
    else if (action == "close") visible_ = enlarge_ = false;
    else if (action == "back") history(-1);
    else if (action == "forward") history(1);
    else if (action == "follow") {
        juce::String topic, section;
        if (!content_.resolve(request.getProperty("target", {}).toString(), topic_, topic, section)) return "unknown manual link";
        navigate(topic, section);
    } else if (action != "status" && action != "navigate" && action != "search") return "action: status, open, close, navigate, search, follow, back, forward";
    if (request.hasProperty("topic") && !navigate(request.getProperty("topic", {}).toString(), request.getProperty("section", "overview").toString())) return "unknown manual section";
    if (request.hasProperty("query")) request.getProperty("query", {}).toString().copyToUTF8(query_, sizeof(query_));
    hits_ = content_.search(language_, juce::String::fromUTF8(query_));
    auto* result = new juce::DynamicObject(); result->setProperty("visible", visible_); result->setProperty("language", language_);
    result->setProperty("topic", topic_); result->setProperty("section", section_); result->setProperty("error", content_.error());
    result->setProperty("scroll", scroll_); result->setProperty("figureEnlarged", enlarge_);
    result->setProperty("imageZoom", imageState_.canvas.view.zoom);
    result->setProperty("imageOriginX", imageState_.canvas.view.origin.x);
    result->setProperty("imageOriginY", imageState_.canvas.view.origin.y);
    result->setProperty("imguiErrorCount", GImGui->ErrorCountCurrentFrame);
    result->setProperty("imguiErrorLog", juce::String(GImGui->DebugLogBuf.c_str())); result->setProperty("focused", focused_);
    result->setProperty("textScale", textScale_);
    if (const auto* section = content_.section(language_, topic_, section_)) {
        result->setProperty("sectionTitle", section->title); result->setProperty("textLength", section->markdown.length());
    }
    result->setProperty("query", query_); result->setProperty("historyIndex", historyAt_); result->setProperty("textureCount", int(textures_.size()));
    const auto rect = [](juce::Rectangle<float> r) { auto* value = new juce::DynamicObject(); value->setProperty("x", r.getX()); value->setProperty("y",r.getY()); value->setProperty("width",r.getWidth()); value->setProperty("height",r.getHeight()); return juce::var(value); };
    result->setProperty("bounds", rect(bounds_)); result->setProperty("searchBounds", rect(searchBounds_)); result->setProperty("imageBounds", rect(imageBounds_));
    result->setProperty("languageBounds", rect(languageBounds_)); result->setProperty("zoomBounds", rect(zoomBounds_));
    juce::Array<juce::var> chapters, matches, images;
    for (const auto& topic : content_.topics(language_)) {
        auto* t = new juce::DynamicObject(); t->setProperty("id",topic.id); t->setProperty("title",topic.title);
        juce::Array<juce::var> sections;
        for (const auto& section : topic.sections) { auto* s = new juce::DynamicObject(); s->setProperty("id",section.id); s->setProperty("title",section.title); sections.add(juce::var(s)); }
        t->setProperty("sections",sections); chapters.add(juce::var(t));
    }
    for (const auto& hit : hits_) { auto* h = new juce::DynamicObject(); h->setProperty("topic",hit.topic); h->setProperty("section",hit.section); h->setProperty("title",hit.title); matches.add(juce::var(h)); }
    for (const auto& r : figureBounds_) images.add(rect(r));
    result->setProperty("topics",chapters); result->setProperty("results",matches); result->setProperty("figureBounds",images);
    return juce::var(result);
}
} // namespace cydaw::v2
