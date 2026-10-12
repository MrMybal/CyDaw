#pragma once

#include <cydaw/graph/GraphPluginExport.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace cydaw::app {

class GraphPluginExportComponent final : public juce::Component {
public:
    using ValidationHandler = std::function<cydaw::graph::PluginExportResult(
        const cydaw::graph::PluginIdentity&)>;
    using ProfileChangedHandler = std::function<void(
        const cydaw::graph::PluginIdentity&)>;
    using SaveManifestHandler = std::function<void(
        const cydaw::graph::PluginIdentity&,
        const cydaw::graph::PluginExportPlan&)>;
    using GenerateProjectHandler = SaveManifestHandler;

    GraphPluginExportComponent();

    void setProfile(const cydaw::graph::PluginIdentity& profile);
    [[nodiscard]] cydaw::graph::PluginIdentity profile() const;
    void setValidationHandler(ValidationHandler handler);
    void setProfileChangedHandler(ProfileChangedHandler handler);
    void setSaveManifestHandler(SaveManifestHandler handler);
    void setGenerateProjectHandler(GenerateProjectHandler handler);
    void refreshTranslations();
    void validateNow();

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void configureEditor(juce::TextEditor& editor, int maximumLength);
    void notifyProfileChanged();
    void refreshSummary();

    cydaw::graph::PluginExportResult result_;
    ValidationHandler validationHandler_;
    ProfileChangedHandler profileChangedHandler_;
    SaveManifestHandler saveManifestHandler_;
    GenerateProjectHandler generateProjectHandler_;

    juce::Label titleLabel_;
    juce::Label subtitleLabel_;
    juce::Label identityTitle_;
    juce::Label productLabel_;
    juce::TextEditor productEditor_;
    juce::Label companyLabel_;
    juce::TextEditor companyEditor_;
    juce::Label manufacturerCodeLabel_;
    juce::TextEditor manufacturerCodeEditor_;
    juce::Label pluginCodeLabel_;
    juce::TextEditor pluginCodeEditor_;
    juce::Label versionLabel_;
    juce::TextEditor versionEditor_;
    juce::Label contractTitle_;
    juce::TextEditor summaryEditor_;
    juce::TextButton validateButton_;
    juce::TextButton saveManifestButton_;
    juce::TextButton generateProjectButton_;
    juce::Label statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphPluginExportComponent)
};

} // namespace cydaw::app
