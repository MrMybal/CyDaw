#include "ManualContent.h"
#include <algorithm>

namespace cydaw::v2 {
namespace {
int index(const juce::String& language) { return language == "fr" ? 1 : 0; }
bool identity(const juce::String& s) {
    return s.isNotEmpty() && s.length() <= 96 && s.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789-");
}
juce::String plain(juce::String s) {
    s = s.replaceCharacters("#*`|[]_", "       ");
    return juce::StringArray::fromTokens(s, " \t\r\n", "").joinIntoString(" ").trim();
}
}
juce::String ManualContent::normalise(juce::String s) {
    return s.toLowerCase().replaceCharacters(juce::String::fromUTF8("àâäáãåçéèêëíìîïñóòôöõúùûüýÿœæ"),
                                            "aaaaaaceeeeiiiinooooouuuuyyoa");
}
const std::vector<ManualTopic>& ManualContent::topics(const juce::String& language) const { return topics_[index(language)]; }
const ManualTopic* ManualContent::topic(const juce::String& language, const juce::String& id) const {
    for (const auto& t : topics(language)) if (t.id == id) return &t;
    return nullptr;
}
const ManualSection* ManualContent::section(const juce::String& language, const juce::String& id, const juce::String& part) const {
    if (const auto* t = topic(language, id)) for (const auto& s : t->sections) if (s.id == part) return &s;
    return nullptr;
}
bool ManualContent::load(const juce::File& root) {
    root_ = root; error_.clear(); for (auto& list : topics_) list.clear();
    const auto manifest = root.getChildFile("manifest.json");
    if (!manifest.existsAsFile() || manifest.getSize() > 1024 * 1024) { error_ = "Manual manifest is missing or too large"; return false; }
    const auto data = juce::JSON::parse(manifest);
    const auto* entries = data.getProperty("topics", {}).getArray();
    if (data.getProperty("format", {}).toString() != "CyDAW Manual" || int(data.getProperty("schema", 0)) != 1 || !entries) {
        error_ = "Invalid manual manifest"; return false;
    }
    for (const auto& entry : *entries) {
        const auto id = entry.getProperty("id", {}).toString();
        if (!identity(id) || topic("en", id)) { error_ = "Invalid or duplicate topic identity"; break; }
        for (const auto* language : {"en", "fr"}) {
            const auto file = root.getChildFile(language).getChildFile(id + ".md");
            if (!file.existsAsFile() || file.getSize() > 4 * 1024 * 1024) { error_ = "Missing manual chapter: " + id; break; }
            ManualTopic t; t.id = id;
            t.title = entry.getProperty("title", {}).getProperty(language, id).toString();
            t.group = entry.getProperty("group", {}).getProperty(language, "Manual").toString();
            t.sections.push_back({"overview", t.title, {}, {}});
            auto* part = &t.sections.back(); bool fenced = false;
            for (auto line : juce::StringArray::fromLines(file.loadFileAsString())) {
                if (line.startsWith("```") || line.startsWith("~~~")) fenced = !fenced;
                if (!fenced && line.startsWith("# ")) continue;
                if (!fenced && line.startsWith("## ")) {
                    const auto mark = line.lastIndexOf(" {#");
                    const auto key = mark >= 0 && line.endsWithChar('}') ? line.substring(mark + 3).dropLastCharacters(1) : juce::String();
                    if (!identity(key) || std::any_of(t.sections.begin(), t.sections.end(), [&](const auto& s){ return s.id == key; })) {
                        error_ = "Invalid section identity in " + id; break;
                    }
                    t.sections.push_back({key, line.substring(3, mark).trim(), {}, {}}); part = &t.sections.back(); continue;
                }
                if (!fenced && line.startsWith("![") && line.contains("](") && line.endsWithChar(')')) {
                    const auto split = line.indexOf("](");
                    const auto path = line.substring(split + 2).dropLastCharacters(1);
                    // Authors use ../assets/<name>. Only that bounded directory is read.
                    if (!path.startsWith("../assets/") || path.substring(10).containsAnyOf("/\\:") || path.substring(3).contains("..")) {
                        error_ = "Invalid manual figure path"; break;
                    }
                    const auto image = root.getChildFile("assets").getChildFile(path.substring(10));
                    if (!image.existsAsFile() || image.getSize() > 16 * 1024 * 1024) { error_ = "Missing manual figure: " + path; break; }
                    part->figures.push_back({line.substring(2, split), image}); continue;
                }
                part->markdown += line + "\n";
            }
            if (error_.isNotEmpty()) break;
            topics_[index(language)].push_back(std::move(t));
        }
        if (error_.isNotEmpty()) break;
    }
    if (topics_[0].empty() && error_.isEmpty()) error_ = "The manual has no topics";
    if (error_.isNotEmpty()) { for (auto& list : topics_) list.clear(); return false; }
    return true;
}
std::vector<ManualHit> ManualContent::search(const juce::String& language, const juce::String& query) const {
    auto words = juce::StringArray::fromTokens(normalise(query), " \t\r\n", ""); words.removeEmptyStrings();
    std::vector<ManualHit> hits; if (words.isEmpty()) return hits;
    for (const auto& t : topics(language)) for (const auto& s : t.sections) {
        const auto body = normalise(s.markdown), title = normalise(t.title + " " + s.title);
        int score = 0; bool match = true;
        for (const auto& word : words) {
            if (title.contains(word)) score += 20;
            else if (body.contains(word)) score += 2;
            else { match = false; break; }
        }
        if (!match) continue;
        const auto text = plain(s.markdown); auto at = normalise(text).indexOf(words[0]);
        at = std::max(0, at - 45);
        hits.push_back({t.id, s.id, t.title + " / " + s.title, text.substring(at, at + 160), score});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b){ return a.score > b.score; });
    if (hits.size() > 100) hits.resize(100);
    return hits;
}
bool ManualContent::resolve(const juce::String& target, const juce::String& current, juce::String& id, juce::String& part) const {
    auto path = target;
    if (path.startsWith("manual:")) path = path.substring(7);
    if (path.startsWith("//")) path = path.substring(2);
    if (path.startsWithChar('#')) path = current + path;
    id = path.upToFirstOccurrenceOf("#", false, false);
    if (id.endsWith(".md")) id = id.dropLastCharacters(3);
    part = path.containsChar('#') ? path.fromFirstOccurrenceOf("#", false, false) : "overview";
    return identity(id) && identity(part) && section("en", id, part) != nullptr;
}
} // namespace cydaw::v2
