#include <cydaw/base/Url.h>
#include <cydaw/base/TextMap.h>
#include <cydaw/base/File.h>
#include "HelpDocumentation.h"
#include <set>
namespace cydaw::v2 {
HelpDocumentation::HelpDocumentation(cydaw::base::File directory) : directory_(std::move(directory)) {}
cydaw::base::File HelpDocumentation::installedDirectory() {
    return cydaw::base::File::getSpecialLocation(cydaw::base::File::currentExecutableFile).getSiblingFile("Documentation");
}
bool HelpDocumentation::load() {
    error_.clear();
    if (!directory_.getChildFile("Documentation.html").existsAsFile()) { error_ = "Documentation/Documentation.html is missing. Restore the complete Documentation folder beside CyDAW."; return false; }
    if (catalog_.isObject()) return true;
    const auto file = directory_.getChildFile("DocData/routes.json");
    if (!file.existsAsFile() || file.getSize() > 8 * 1024 * 1024) { error_ = "CyDoc help route catalog is missing or too large"; return false; }
    const auto data = cydaw::base::JSON::parse(file);
    if (data.getProperty("format", {}).toString() != "CyDAW CyDoc Routes" || int(data.getProperty("schema", 0)) != 1
        || !data.getProperty("pages", {}).isArray()) { error_ = "Invalid CyDoc help route catalog"; return false; }
    catalog_ = data;
    if (!page("welcome")) { catalog_ = cydaw::base::Value(); error_ = "CyDoc welcome page is missing"; return false; }
    return true;
}
const cydaw::base::Value* HelpDocumentation::page(const cydaw::base::Text& id) const {
    if (const auto* pages = catalog_.getProperty("pages", {}).getArray())
        for (const auto& value : *pages) if (value.getProperty("id", {}).toString() == id) return &value;
    return nullptr;
}
bool HelpDocumentation::canonical(cydaw::base::Text& id, cydaw::base::Text& anchor) const {
    std::set<cydaw::base::Text> visited;
    const auto aliases = catalog_.getProperty("aliases", {});
    while (aliases.hasProperty(cydaw::base::Identifier(id))) {
        if (!visited.insert(id).second || visited.size() > 16) return false;
        const auto value = aliases.getProperty(cydaw::base::Identifier(id), {}).toString();
        id = value.upToFirstOccurrenceOf("#", false, false);
        if (anchor.isEmpty() && value.containsChar('#')) anchor = value.fromFirstOccurrenceOf("#", false, false);
    }
    return page(id) != nullptr;
}
cydaw::base::Text HelpDocumentation::encode(const cydaw::base::Text& value) { return cydaw::base::Url::addEscapeChars(value, true); }
cydaw::base::Text HelpDocumentation::link(cydaw::base::Text id, cydaw::base::Text anchor, cydaw::base::Text locale, const cydaw::base::TextMap& state) {
    if (!load()) return {};
    if (locale != "en" && locale != "fr") { error_ = "Help locale: en or fr"; return {}; }
    const auto base = cydaw::base::Url(directory_.getChildFile("Documentation.html")).toString(true);
    if (id.isEmpty()) return base; // The reader chooses its home page and saved locale.
    if (id.length() > 256 || anchor.length() > 256 || !canonical(id, anchor)) { error_ = "Unknown help page"; return {}; }
    const auto* target = page(id);
    auto available = target->getProperty("anchors", {}).getProperty(cydaw::base::Identifier(locale), {});
    if (!available.isArray()) available = target->getProperty("anchors", {}).getProperty("en", {});
    if (anchor.isNotEmpty() && (!available.isArray() || !available.getArray()->contains(anchor))) { error_ = "Unknown help section"; return {}; }
    auto result = base + "#/" + encode(locale) + "/" + encode(id);
    if (anchor.isNotEmpty()) result += "/" + encode(anchor);
    if (state.size()) {
        const auto media = state["media"];
        auto ids = target->getProperty("media", {}).getProperty(cydaw::base::Identifier(locale), {});
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
cydaw::base::Text HelpDocumentation::follow(const cydaw::base::Text& target, cydaw::base::Text locale) {
    if (target.length() > 16000) { error_ = "Help target too long"; return {}; }
    auto text = target;
    if (text.startsWith("manual:")) text = text.substring(7);
    else if (text.startsWith("doc:") && !text.startsWith("doc://")) text = text.substring(4);
    if (text.startsWith("//")) text = text.substring(2);
    if (text.startsWith("#/")) {
        const auto pieces = cydaw::base::TextList::fromTokens(text.substring(2).upToFirstOccurrenceOf("?", false, false), "/", "");
        if (pieces.size() < 2 || pieces.size() > 3) { error_ = "Invalid help route"; return {}; }
        locale = cydaw::base::Url::removeEscapeChars(pieces[0]);
        text = cydaw::base::Url::removeEscapeChars(pieces[1]) + (pieces.size() == 3 ? "#" + cydaw::base::Url::removeEscapeChars(pieces[2]) : "")
             + (text.containsChar('?') ? "?" + text.fromFirstOccurrenceOf("?", false, false) : "");
    } else if (text.startsWithChar('#')) text = page_ + text;
    const auto query = text.containsChar('?') ? text.fromFirstOccurrenceOf("?", false, false) : cydaw::base::Text();
    text = text.upToFirstOccurrenceOf("?", false, false);
    auto id = text.upToFirstOccurrenceOf("#", false, false);
    auto anchor = text.containsChar('#') ? text.fromFirstOccurrenceOf("#", false, false) : cydaw::base::Text();
    cydaw::base::TextMap state;
    for (const auto& item : cydaw::base::TextList::fromTokens(query, "&", "")) if (item.containsChar('='))
        state.set(cydaw::base::Url::removeEscapeChars(item.upToFirstOccurrenceOf("=", false, false)),cydaw::base::Url::removeEscapeChars(item.fromFirstOccurrenceOf("=", false, false)));
    return link(id, anchor, locale, state);
}
cydaw::base::Value HelpDocumentation::command(const cydaw::base::Value& request, bool allowBrowser) {
    const auto action = request.getProperty("action", "status").toString();
    const auto requestedLocale = request.getProperty("language", locale_).toString();
    if (requestedLocale != "en" && requestedLocale != "fr") return "Help language: en or fr";
    locale_ = requestedLocale;
    if (action == "open" || action == "navigate" || action == "follow") {
        const auto requestedPage = request.getProperty("topic", request.getProperty("page", action == "navigate" ? page_ : cydaw::base::Text())).toString();
        const auto requestedAnchor = request.getProperty("section", request.getProperty("anchor", {})).toString();
        cydaw::base::TextMap state;
        if (const auto* object = request.getProperty("state", {}).getDynamicObject())
            for (const auto& value : object->getProperties()) state.set(value.name.toString(), value.value.toString());
        lastUrl_ = action == "follow" ? follow(request.getProperty("target", {}).toString(), locale_) : link(requestedPage, requestedAnchor, locale_, state);
        if (lastUrl_.isEmpty()) return error_;
        if (lastUrl_.contains("#/")) {
            const auto route = cydaw::base::TextList::fromTokens(lastUrl_.fromFirstOccurrenceOf("#/", false, false).upToFirstOccurrenceOf("?", false, false), "/", "");
            if (route.size() >= 2) { locale_ = cydaw::base::Url::removeEscapeChars(route[0]); page_ = cydaw::base::Url::removeEscapeChars(route[1]); anchor_ = route.size() > 2 ? cydaw::base::Text(cydaw::base::Url::removeEscapeChars(route[2])) : cydaw::base::Text(); }
        }
        ++requests_; launchSuppressed_ = !allowBrowser || !bool(request.getProperty("launch", true));
        launched_ = !launchSuppressed_ && cydaw::base::Url(lastUrl_).launchInDefaultBrowser();
        if (!launchSuppressed_ && !launched_) return "Cannot open the local CyDoc reader in the default browser";
    } else if (action != "status") return "CyDoc owns search/history/closing. Help actions: status, open, navigate, follow";
    if (!load()) return error_;
    auto* result = new cydaw::base::Object();
    result->setProperty("engine", "CyDoc"); result->setProperty("url", lastUrl_); result->setProperty("language", locale_);
    result->setProperty("topic", page_); result->setProperty("section", anchor_); result->setProperty("requests", requests_);
    result->setProperty("launchSuppressed", launchSuppressed_); result->setProperty("launched", launched_);
    result->setProperty("error", error_); result->setProperty("pages", catalog_.getProperty("pages", {}));
    return cydaw::base::Value(result);
}
}
