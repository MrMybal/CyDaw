#include <cydaw/base/FileDialog.h>
#include <cydaw/base/Text.h>
#pragma once

#include "Studio.h"
#include "UiState.h"


#include <memory>

// The semantic sound search's window (Options › Recherche sémantique, or the
// browser's bulb): optional, and said so. Three steps: the Python that runs
// the model (the companion's environment, PyTorch and transformers installed
// from here, their licences shown); the models (what each is, its licence
// with its page, its size and where it comes from; downloaded only once the
// person confirms, after ticking the acceptance when the licence asks for
// it); the index (how many sounds, indexing now or by itself, the categories
// CLAP gives the sounds nothing named).

namespace cydaw::v2 {

class SemanticSearchView final {
public:
    SemanticSearchView(Studio& studio, UiState& state);
    void draw();

private:
    void drawEngine();
    void drawModels();
    void drawModel(const AiModel& model);
    void drawIndex();
    void drawConfirm();
    void report(const cydaw::base::Text& text, bool error);

    Studio& studio_;
    UiState& state_;
    bool shown_ {};
    char python_[512] {};
    // The model the confirmation is about, and the licence ticked for it.
    cydaw::base::Text confirming_;
    cydaw::base::Text accepted_;
    // The engine's install, confirmed: "cpu" or "cuda".
    cydaw::base::Text engineInstall_;
    bool openConfirm_ {};
    // The NVIDIA build's licences (CUDA, cuDNN) accepted in the confirmation.
    bool engineLicencesAccepted_ {};
    std::unique_ptr<cydaw::base::FileDialog> chooser_;
};

} // namespace cydaw::v2
