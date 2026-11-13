#include <cydaw/base/Text.h>
#pragma once

#include "Studio.h"
#include "UiState.h"

// The sound banks' window (Options › Banques de sons, or the instruments'
// browser): each bank CyDAW knows, its author, licence, size and what of it
// is here; downloaded whole or removed; where it comes from. The download
// running, with its progress, and where the banks are kept.

namespace cydaw::v2 {

class BanksView final {
public:
    BanksView(Studio& studio, UiState& state);
    // A window of its own while the state shows it.
    void draw();

private:
    void drawProgress();
    void drawDetails(const SoundBanks::Bank& bank);
    void report(const cydaw::base::Text& text, bool error);

    Studio& studio_;
    UiState& state_;
    bool shown_ {};
    cydaw::base::Text selected_;
    cydaw::base::Text removing_;
};

} // namespace cydaw::v2
