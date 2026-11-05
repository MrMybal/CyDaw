#pragma once
#include <juce_core/juce_core.h>

namespace cydaw::v2 {
// Only routes/identities are native. CyDoc owns rendering, search and media.
class HelpDocumentation {
public:
    explicit HelpDocumentation(juce::File directory);
    static juce::File installedDirectory();
    bool load();
    juce::String link(juce::String page = {}, juce::String anchor = {}, juce::String locale = "en",
                      const juce::StringPairArray& state = {});
    juce::String follow(const juce::String& target, juce::String locale = "en");
    juce::var command(const juce::var&, bool allowBrowser);
    const juce::String& error() const { return error_; }
private:
    juce::File directory_;
    juce::var catalog_;
    juce::String error_, lastUrl_, locale_ {"en"}, page_ {"welcome"}, anchor_;
    int requests_ {};
    bool launchSuppressed_ {}, launched_ {};
    const juce::var* page(const juce::String& id) const;
    bool canonical(juce::String& page, juce::String& anchor) const;
    static juce::String encode(const juce::String&);
};
}
