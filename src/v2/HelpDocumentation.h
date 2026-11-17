#include <cydaw/base/TextMap.h>
#include <cydaw/base/File.h>
#pragma once


namespace cydaw::v2 {
// Only routes/identities are native. CyDoc owns rendering, search and media.
class HelpDocumentation {
public:
    explicit HelpDocumentation(cydaw::base::File directory);
    static cydaw::base::File installedDirectory();
    bool load();
    cydaw::base::Text link(cydaw::base::Text page = {}, cydaw::base::Text anchor = {}, cydaw::base::Text locale = "en",
                      const cydaw::base::TextMap& state = {});
    cydaw::base::Text follow(const cydaw::base::Text& target, cydaw::base::Text locale = "en");
    cydaw::base::Value command(const cydaw::base::Value&, bool allowBrowser);
    const cydaw::base::Text& error() const { return error_; }
private:
    cydaw::base::File directory_;
    cydaw::base::Value catalog_;
    cydaw::base::Text error_, lastUrl_, locale_ {"en"}, page_ {"welcome"}, anchor_;
    int requests_ {};
    bool launchSuppressed_ {}, launched_ {};
    const cydaw::base::Value* page(const cydaw::base::Text& id) const;
    bool canonical(cydaw::base::Text& page, cydaw::base::Text& anchor) const;
    static cydaw::base::Text encode(const cydaw::base::Text&);
};
}
