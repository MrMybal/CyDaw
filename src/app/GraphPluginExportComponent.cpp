#include "GraphPluginExportComponent.h"

#include "Localization.h"

#include <algorithm>
#include <array>

namespace cydaw::app {
namespace {

cydaw::graph::SemanticVersion parseVersion(const juce::String& text)
{
    juce::StringArray parts;
    parts.addTokens(text.trim(), ".", {});
    const auto part = [&parts](const int index, const int fallback) {
        return index < parts.size()
                   ? juce::jlimit(0, 65'535, parts[index].getIntValue())
                   : fallback;
    };
    return {
        static_cast<std::uint16_t>(part(0, 1)),
        static_cast<std::uint16_t>(part(1, 0)),
        static_cast<std::uint16_t>(part(2, 0))
    };
}

juce::String directionLabel(const cydaw::graph::PortDirection direction)
{
    return tr(direction == cydaw::graph::PortDirection::input
                  ? "graph.export.input" : "graph.export.output");
}

} // namespace

GraphPluginExportComponent::GraphPluginExportComponent()
{
    setSize(920, 590);
    const std::array<juce::Component*, 19> components {
        &titleLabel_, &subtitleLabel_, &identityTitle_, &productLabel_,
        &productEditor_, &companyLabel_, &companyEditor_,
        &manufacturerCodeLabel_, &manufacturerCodeEditor_, &pluginCodeLabel_,
        &pluginCodeEditor_, &versionLabel_, &versionEditor_, &contractTitle_,
        &summaryEditor_, &validateButton_, &saveManifestButton_,
        &generateProjectButton_, &statusLabel_
    };
    for (auto* component : components)
        addAndMakeVisible(*component);

    titleLabel_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId,
                          juce::Colour(0xffffa13b));
    subtitleLabel_.setFont(juce::FontOptions(10.0f));
    subtitleLabel_.setColour(juce::Label::textColourId,
                             juce::Colour(0xffaeb5ba));
    subtitleLabel_.setJustificationType(juce::Justification::topLeft);
    identityTitle_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    contractTitle_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    for (auto* label : { &productLabel_, &companyLabel_,
                         &manufacturerCodeLabel_, &pluginCodeLabel_,
                         &versionLabel_ })
        label->setFont(juce::FontOptions(9.0f, juce::Font::bold));

    configureEditor(productEditor_, 128);
    configureEditor(companyEditor_, 128);
    configureEditor(manufacturerCodeEditor_, 4);
    configureEditor(pluginCodeEditor_, 4);
    configureEditor(versionEditor_, 18);
    summaryEditor_.setMultiLine(true, true);
    summaryEditor_.setReadOnly(true);
    summaryEditor_.setScrollbarsShown(true);
    summaryEditor_.setColour(juce::TextEditor::backgroundColourId,
                             juce::Colour(0xff111418));
    summaryEditor_.setColour(juce::TextEditor::outlineColourId,
                             juce::Colour(0xff454b51));
    summaryEditor_.setColour(juce::TextEditor::textColourId,
                             juce::Colour(0xffd7dadd));
    summaryEditor_.setFont(juce::FontOptions(10.5f));

    const auto commit = [this] {
        notifyProfileChanged();
        validateNow();
    };
    for (auto* editor : { &productEditor_, &companyEditor_,
                          &manufacturerCodeEditor_, &pluginCodeEditor_,
                          &versionEditor_ }) {
        editor->onReturnKey = commit;
        editor->onFocusLost = commit;
    }
    manufacturerCodeEditor_.onTextChange = [this] {
        const auto upper = manufacturerCodeEditor_.getText().toUpperCase();
        if (upper != manufacturerCodeEditor_.getText())
            manufacturerCodeEditor_.setText(upper, false);
    };
    pluginCodeEditor_.onTextChange = [this] {
        const auto upper = pluginCodeEditor_.getText().toUpperCase();
        if (upper != pluginCodeEditor_.getText())
            pluginCodeEditor_.setText(upper, false);
    };
    validateButton_.onClick = [this] {
        notifyProfileChanged();
        validateNow();
    };
    saveManifestButton_.onClick = [this] {
        notifyProfileChanged();
        validateNow();
        if (result_.plan.has_value() && result_.issues.empty()
            && saveManifestHandler_)
            saveManifestHandler_(profile(), *result_.plan);
    };
    generateProjectButton_.onClick = [this] {
        notifyProfileChanged();
        validateNow();
        if (result_.plan.has_value() && result_.issues.empty()
            && generateProjectHandler_)
            generateProjectHandler_(profile(), *result_.plan);
    };
    refreshTranslations();
}

void GraphPluginExportComponent::configureEditor(juce::TextEditor& editor,
                                                  const int maximumLength)
{
    editor.setInputRestrictions(maximumLength);
    editor.setColour(juce::TextEditor::backgroundColourId,
                     juce::Colour(0xff111418));
    editor.setColour(juce::TextEditor::outlineColourId,
                     juce::Colour(0xff454b51));
    editor.setColour(juce::TextEditor::focusedOutlineColourId,
                     juce::Colour(0xffff8a00));
}

void GraphPluginExportComponent::setProfile(
    const cydaw::graph::PluginIdentity& value)
{
    productEditor_.setText(juce::String(value.productName), false);
    companyEditor_.setText(juce::String(value.companyName), false);
    manufacturerCodeEditor_.setText(
        juce::String(value.manufacturerCode).toUpperCase(), false);
    pluginCodeEditor_.setText(
        juce::String(value.pluginCode).toUpperCase(), false);
    versionEditor_.setText(juce::String(value.version.toString()), false);
}

cydaw::graph::PluginIdentity GraphPluginExportComponent::profile() const
{
    return {
        productEditor_.getText().trim().toStdString(),
        companyEditor_.getText().trim().toStdString(),
        manufacturerCodeEditor_.getText().trim().toUpperCase().toStdString(),
        pluginCodeEditor_.getText().trim().toUpperCase().toStdString(),
        parseVersion(versionEditor_.getText())
    };
}

void GraphPluginExportComponent::setValidationHandler(ValidationHandler handler)
{
    validationHandler_ = std::move(handler);
}

void GraphPluginExportComponent::setProfileChangedHandler(
    ProfileChangedHandler handler)
{
    profileChangedHandler_ = std::move(handler);
}

void GraphPluginExportComponent::setSaveManifestHandler(
    SaveManifestHandler handler)
{
    saveManifestHandler_ = std::move(handler);
}

void GraphPluginExportComponent::setGenerateProjectHandler(
    GenerateProjectHandler handler)
{
    generateProjectHandler_ = std::move(handler);
}

void GraphPluginExportComponent::refreshTranslations()
{
    titleLabel_.setText(tr("graph.export.title"), juce::dontSendNotification);
    subtitleLabel_.setText(tr("graph.export.subtitle"),
                           juce::dontSendNotification);
    identityTitle_.setText(tr("graph.export.identity"),
                           juce::dontSendNotification);
    productLabel_.setText(tr("graph.export.product"),
                          juce::dontSendNotification);
    companyLabel_.setText(tr("graph.export.company"),
                          juce::dontSendNotification);
    manufacturerCodeLabel_.setText(tr("graph.export.manufacturerCode"),
                                   juce::dontSendNotification);
    pluginCodeLabel_.setText(tr("graph.export.pluginCode"),
                             juce::dontSendNotification);
    versionLabel_.setText(tr("graph.export.version"),
                          juce::dontSendNotification);
    contractTitle_.setText(tr("graph.export.contract"),
                           juce::dontSendNotification);
    validateButton_.setButtonText(tr("graph.export.validate"));
    saveManifestButton_.setButtonText(tr("graph.export.saveManifest"));
    generateProjectButton_.setButtonText(tr("graph.export.generateProject"));
    validateButton_.setTooltip(tr("graph.export.validateHelp"));
    saveManifestButton_.setTooltip(tr("graph.export.saveManifestHelp"));
    generateProjectButton_.setTooltip(
        tr("graph.export.generateProjectHelp"));
    productEditor_.setTooltip(tr("graph.export.productHelp"));
    companyEditor_.setTooltip(tr("graph.export.companyHelp"));
    manufacturerCodeEditor_.setTooltip(
        tr("graph.export.manufacturerCodeHelp"));
    pluginCodeEditor_.setTooltip(tr("graph.export.pluginCodeHelp"));
    versionEditor_.setTooltip(tr("graph.export.versionHelp"));
    summaryEditor_.setTooltip(tr("graph.export.contractHelp"));
    refreshSummary();
}

void GraphPluginExportComponent::validateNow()
{
    result_ = validationHandler_ ? validationHandler_(profile())
                                 : cydaw::graph::PluginExportResult {};
    refreshSummary();
}

void GraphPluginExportComponent::notifyProfileChanged()
{
    if (profileChangedHandler_)
        profileChangedHandler_(profile());
}

void GraphPluginExportComponent::refreshSummary()
{
    juce::String text;
    if (result_.plan.has_value() && result_.issues.empty()) {
        const auto& plan = *result_.plan;
        text << tr("graph.export.ready") << "\n\n";
        text << tr("graph.export.role") << ": "
             << juce::String(cydaw::graph::toString(plan.role)) << "\n";
        text << tr("graph.export.category") << ": "
             << juce::String(plan.vst3Category) << "\n";
        text << tr("graph.export.fingerprint") << ": "
             << juce::String::toHexString(
                    static_cast<juce::int64>(plan.sourceFingerprint))
                    .toUpperCase()
             << "\n\n";
        text << tr("graph.export.buses") << " ("
             << static_cast<int>(plan.buses.size()) << ")\n";
        for (const auto& bus : plan.buses) {
            text << "  " << directionLabel(bus.direction) << " · "
                 << juce::String(bus.displayName) << " · ";
            if (bus.mediaType == cydaw::graph::PluginBusMediaType::audio)
                text << juce::String(cydaw::graph::toString(bus.audioType))
                     << " / " << static_cast<int>(bus.channels) << " ch";
            else
                text << "MIDI";
            if (bus.main)
                text << " · " << tr("graph.export.mainBus");
            text << "\n";
        }
        text << "\n" << tr("graph.export.parameters") << " ("
             << static_cast<int>(plan.parameters.size()) << ")\n";
        if (plan.parameters.empty())
            text << "  " << tr("graph.export.noParameters") << "\n";
        for (const auto& parameter : plan.parameters)
            text << "  #" << static_cast<int>(parameter.pluginParameterId)
                 << " · " << juce::String(parameter.displayName)
                 << " [" << juce::String(parameter.key) << "]"
                 << (parameter.automatable ? " · AUTO" : "") << "\n";
        statusLabel_.setText(tr("graph.export.statusReady"),
                             juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId,
                               juce::Colour(0xff75d6ac));
        saveManifestButton_.setEnabled(true);
        generateProjectButton_.setEnabled(true);
    } else {
        text << tr("graph.export.notReady") << "\n\n";
        for (const auto& issue : result_.issues)
            text << "• " << juce::String(issue.message) << "\n";
        if (result_.issues.empty())
            text << tr("graph.export.validatePrompt") << "\n";
        statusLabel_.setText(tr("graph.export.statusInvalid"),
                             juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId,
                               juce::Colour(0xffff7373));
        saveManifestButton_.setEnabled(false);
        generateProjectButton_.setEnabled(false);
    }
    summaryEditor_.setText(text, false);
}

void GraphPluginExportComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff171a1e));
    auto area = getLocalBounds().toFloat().reduced(10.0f)
                    .withTrimmedTop(72.0f);
    const auto left = area.removeFromLeft(
        juce::jmin(360.0f, area.getWidth() * 0.42f));
    graphics.setColour(juce::Colour(0xff22262b));
    graphics.fillRoundedRectangle(left, 4.0f);
    graphics.fillRoundedRectangle(area.withTrimmedLeft(10.0f), 4.0f);
    graphics.setColour(juce::Colour(0xff454b51));
    graphics.drawRoundedRectangle(left, 4.0f, 1.0f);
    graphics.drawRoundedRectangle(area.withTrimmedLeft(10.0f), 4.0f, 1.0f);
}

void GraphPluginExportComponent::resized()
{
    auto area = getLocalBounds().reduced(16);
    titleLabel_.setBounds(area.removeFromTop(27));
    subtitleLabel_.setBounds(area.removeFromTop(38));
    area.removeFromTop(7);
    auto identity = area.removeFromLeft(
        juce::jmin(340, juce::roundToInt(area.getWidth() * 0.40f)));
    identity = identity.reduced(13, 10);
    identityTitle_.setBounds(identity.removeFromTop(23));
    identity.removeFromTop(5);
    const auto field = [&identity](juce::Label& label,
                                   juce::TextEditor& editor) {
        label.setBounds(identity.removeFromTop(18));
        editor.setBounds(identity.removeFromTop(27));
        identity.removeFromTop(7);
    };
    field(productLabel_, productEditor_);
    field(companyLabel_, companyEditor_);
    field(manufacturerCodeLabel_, manufacturerCodeEditor_);
    field(pluginCodeLabel_, pluginCodeEditor_);
    field(versionLabel_, versionEditor_);
    identity.removeFromTop(8);
    validateButton_.setBounds(identity.removeFromTop(30));
    identity.removeFromTop(7);
    saveManifestButton_.setBounds(identity.removeFromTop(30));
    identity.removeFromTop(8);
    generateProjectButton_.setBounds(identity.removeFromTop(30));
    identity.removeFromTop(8);
    statusLabel_.setBounds(identity.removeFromTop(44));
    statusLabel_.setJustificationType(juce::Justification::centred);

    area.removeFromLeft(22);
    auto contract = area.reduced(13, 10);
    contractTitle_.setBounds(contract.removeFromTop(23));
    contract.removeFromTop(5);
    summaryEditor_.setBounds(contract);
}

} // namespace cydaw::app
