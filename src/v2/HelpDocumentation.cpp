#include "HelpDocumentation.h"
#include <set>
namespace cydaw::v2 {
HelpDocumentation::HelpDocumentation(juce::File directory) : directory_(std::move(directory)) {}
juce::File HelpDocumentation::installedDirectory() {
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("help");
}
bool HelpDocumentation::load() {
    error_.clear();
    if (!directory_.getChildFile("index.html").existsAsFile()) { error_ = "CyDoc help/index.html is missing. Restore the complete help folder beside CyDAW."; return false; }
    if (catalog_.isObject()) return true;
    const auto file = directory_.getChildFile("routes.json");
    if (!file.existsAsFile() || file.getSize() > 8 * 1024 * 1024) { error_ = "CyDoc help route catalog is missing or too large"; return false; }
    const auto data = juce::JSON::parse(file);
    if (data.getProperty("format", {}).toString() != "CyDAW CyDoc Routes" || int(data.getProperty("schema", 0)) != 1
        || !data.getProperty("pages", {}).isArray()) { error_ = "Invalid CyDoc help route catalog"; return false; }
    catalog_ = data;
    if (!page("welcome")) { catalog_ = juce::var(); error_ = "CyDoc welcome page is missing"; return false; }
    return true;
}
const juce::var* HelpDocumentation::page(const juce::String& id) const {
    if (const auto* pages = catalog_.getProperty("pages", {}).getArray())
        for (const auto& value : *pages) if (value.getProperty("id", {}).toString() == id) return &value;
    return nullptr;
}
bool HelpDocumentation::canonical(juce::String& id, juce::String& anchor) const {
    std::set<juce::String> visited;
    const auto aliases = catalog_.getProperty("aliases", {});
    while (aliases.hasProperty(juce::Identifier(id))) {
        if (!visited.insert(id).second || visited.size() > 16) return false;
        const auto value = aliases.getProperty(juce::Identifier(id), {}).toString();
        id = value.upToFirstOccurrenceOf("#", false, false);
        if (anchor.isEmpty() && value.containsChar('#')) anchor = value.fromFirstOccurrenceOf("#", false, false);
    }
    return page(id) != nullptr;
}
juce::String HelpDocumentation::encode(const juce::String& value) { return juce::URL::addEscapeChars(value, true); }
juce::String HelpDocumentation::link(juce::String id, juce::String anchor, juce::String locale, const juce::StringPairArray& state) {
    if (!load()) return {};
    if (locale != "en" && locale != "fr") { error_ = "Help locale: en or fr"; return {}; }
    const auto base = juce::URL(directory_.getChildFile("index.html")).toString(true);
    if (id.isEmpty()) return base; // CyDoc restores the reader's last route/locale.
    if (id.length() > 256 || anchor.length() > 256 || !canonical(id, anchor)) { error_ = "Unknown help page"; return {}; }
    const auto* target = page(id);
    auto available = target->getProperty("anchors", {}).getProperty(juce::Identifier(locale), {});
    if (!available.isArray()) available = target->getProperty("anchors", {}).getProperty("en", {});
    if (anchor.isNotEmpty() && (!available.isArray() || !available.getArray()->contains(anchor))) { error_ = "Unknown help section"; return {}; }
    auto result = base + "#/" + encode(locale) + "/" + encode(id);
    if (anchor.isNotEmpty()) result += "/" + encode(anchor);
    if (state.size()) {
        const auto media = state["media"];
        auto ids = target->getProperty("media", {}).getProperty(juce::Identifier(locale), {});
        if (!ids.isArray()) ids = target->getProperty("media", {}).getProperty("en", {});
        if (media.isEmpty() || !ids.isArray() || !ids.getArray()->contains(media)) { error_ = "Unknown help media"; return {}; }
        auto keys = state.getAllKeys(); keys.sort(false);
        bool first = true;
        for (const auto& key : keys) {
            if (!key.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") || key.isEmpty()
                || key.length() > 32 || state[key].length() > 2000) { error_ = "Invalid help media state"; return {}; }
            result += (first ? "?" : "&") + encode(key) + "=" + encode(state[key]); first = false;
        }
    }
    return result;
}
juce::String HelpDocumentation::follow(const juce::String& target, juce::String locale) {
    if (target.length() > 16000) { error_ = "Help target too long"; return {}; }
    auto text = target;
    if (text.startsWith("manual:")) text = text.substring(7);
    else if (text.startsWith("doc:") && !text.startsWith("doc://")) text = text.substring(4);
    if (text.startsWith("//")) text = text.substring(2);
    if (text.startsWith("#/")) {
        const auto pieces = juce::StringArray::fromTokens(text.substring(2).upToFirstOccurrenceOf("?", false, false), "/", "");
        if (pieces.size() < 2 || pieces.size() > 3) { error_ = "Invalid help route"; return {}; }
        locale = juce::URL::removeEscapeChars(pieces[0]);
        text = juce::URL::removeEscapeChars(pieces[1]) + (pieces.size() == 3 ? "#" + juce::URL::removeEscapeChars(pieces[2]) : "")
             + (text.containsChar('?') ? "?" + text.fromFirstOccurrenceOf("?", false, false) : "");
    } else if (text.startsWithChar('#')) text = page_ + text;
    const auto query = text.containsChar('?') ? text.fromFirstOccurrenceOf("?", false, false) : juce::String();
    text = text.upToFirstOccurrenceOf("?", false, false);
    auto id = text.upToFirstOccurrenceOf("#", false, false);
    auto anchor = text.containsChar('#') ? text.fromFirstOccurrenceOf("#", false, false) : juce::String();
    juce::StringPairArray state;
    for (const auto& item : juce::StringArray::fromTokens(query, "&", "")) if (item.containsChar('='))
        state.set(juce::URL::removeEscapeChars(item.upToFirstOccurrenceOf("=", false, false)),juce::URL::removeEscapeChars(item.fromFirstOccurrenceOf("=", false, false)));
    return link(id, anchor, locale, state);
}
juce::var HelpDocumentation::command(const juce::var& request, bool allowBrowser) {
    const auto action = request.getProperty("action", "status").toString();
    const auto requestedLocale = request.getProperty("language", locale_).toString();
    if (requestedLocale != "en" && requestedLocale != "fr") return "Help language: en or fr";
    locale_ = requestedLocale;
    if (action == "open" || action == "navigate" || action == "follow") {
        const auto requestedPage = request.getProperty("topic", request.getProperty("page", action == "navigate" ? page_ : juce::String())).toString();
        const auto requestedAnchor = request.getProperty("section", request.getProperty("anchor", {})).toString();
        juce::StringPairArray state;
        if (const auto* object = request.getProperty("state", {}).getDynamicObject())
            for (const auto& value : object->getProperties()) state.set(value.name.toString(), value.value.toString());
        lastUrl_ = action == "follow" ? follow(request.getProperty("target", {}).toString(), locale_) : link(requestedPage, requestedAnchor, locale_, state);
        if (lastUrl_.isEmpty()) return error_;
        if (lastUrl_.contains("#/")) {
            const auto route = juce::StringArray::fromTokens(lastUrl_.fromFirstOccurrenceOf("#/", false, false).upToFirstOccurrenceOf("?", false, false), "/", "");
            if (route.size() >= 2) { locale_ = juce::URL::removeEscapeChars(route[0]); page_ = juce::URL::removeEscapeChars(route[1]); anchor_ = route.size() > 2 ? juce::URL::removeEscapeChars(route[2]) : juce::String(); }
        }
        ++requests_; launchSuppressed_ = !allowBrowser || !bool(request.getProperty("launch", true));
        launched_ = !launchSuppressed_ && juce::URL(lastUrl_).launchInDefaultBrowser();
        if (!launchSuppressed_ && !launched_) return "Cannot open the local CyDoc reader in the default browser";
    } else if (action != "status") return "CyDoc owns search/history/closing. Help actions: status, open, navigate, follow";
    if (!load()) return error_;
    auto* result = new juce::DynamicObject();
    result->setProperty("engine", "CyDoc"); result->setProperty("url", lastUrl_); result->setProperty("language", locale_);
    result->setProperty("topic", page_); result->setProperty("section", anchor_); result->setProperty("requests", requests_);
    result->setProperty("launchSuppressed", launchSuppressed_); result->setProperty("launched", launched_);
    result->setProperty("error", error_); result->setProperty("pages", catalog_.getProperty("pages", {}));
    return juce::var(result);
}
}
