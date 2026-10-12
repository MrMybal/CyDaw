#include "ProjectRecovery.h"

namespace cydaw::app {

juce::File ProjectRecovery::defaultRecoveryFile()
{
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("CyDAW")
        .getChildFile("Recovery")
        .getChildFile("last-session.cydaw-recovery");
}

juce::var ProjectRecovery::makeRecoveryDocument(
    juce::var projectDocument, const juce::File& originalProject,
    const juce::Time timestamp)
{
    if (auto* object = projectDocument.getDynamicObject()) {
        object->setProperty("recoverySchema", schemaVersion);
        object->setProperty("recoveryTimestamp",
                            timestamp.toMilliseconds());
        object->setProperty("recoveryOriginalProject",
                            originalProject == juce::File {}
                                ? juce::String {}
                                : originalProject.getFullPathName());
    }
    return projectDocument;
}

bool ProjectRecovery::isRecoveryDocument(const juce::var& document)
{
    const auto* object = document.getDynamicObject();
    return object != nullptr
           && static_cast<int>(object->getProperty("recoverySchema"))
                  == schemaVersion;
}

juce::File ProjectRecovery::originalProject(const juce::var& document)
{
    const auto* object = document.getDynamicObject();
    if (object == nullptr)
        return {};
    const auto path = object->getProperty("recoveryOriginalProject")
                          .toString();
    return path.isEmpty() ? juce::File {} : juce::File(path);
}

juce::Time ProjectRecovery::recoveryTimestamp(const juce::var& document)
{
    const auto* object = document.getDynamicObject();
    if (object == nullptr)
        return {};
    return juce::Time(static_cast<juce::int64>(
        object->getProperty("recoveryTimestamp")));
}

juce::Result ProjectRecovery::validateProjectDocument(
    const juce::var& document)
{
    const auto* object = document.getDynamicObject();
    if (object == nullptr
        || object->getProperty("format").toString() != "CyDAW Project")
        return juce::Result::fail("Invalid CyDAW project document");
    const auto version = static_cast<int>(object->getProperty("version"));
    if (version <= 0 || version > maximumProjectVersion)
        return juce::Result::fail("Unsupported CyDAW project version");
    if (object->hasProperty("recoverySchema")
        && static_cast<int>(object->getProperty("recoverySchema"))
               != schemaVersion)
        return juce::Result::fail("Unsupported recovery schema");
    return juce::Result::ok();
}

juce::Result ProjectRecovery::writeDocumentAtomically(
    const juce::File& destination, const juce::var& document,
    const bool verifyRoundTrip)
{
    if (const auto validation = validateProjectDocument(document);
        validation.failed())
        return validation;
    if (destination == juce::File {})
        return juce::Result::fail("Missing project destination");
    if (!destination.getParentDirectory().createDirectory())
        return juce::Result::fail("Could not create project directory");

    juce::TemporaryFile temporary(destination);
    {
        juce::FileOutputStream output(temporary.getFile());
        if (output.failedToOpen())
            return juce::Result::fail("Could not write temporary project file");
        juce::JSON::writeToStream(output, document, true);
        output.flush();
        if (output.getStatus().failed())
            return juce::Result::fail("Could not write temporary project file");
    }
    if (verifyRoundTrip) {
        const auto verification = juce::JSON::parse(temporary.getFile());
        if (const auto validation = validateProjectDocument(verification);
            validation.failed())
            return validation;
    }
    if (!temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Could not replace project file atomically");
    return juce::Result::ok();
}

juce::Result ProjectRecovery::readDocument(const juce::File& source,
                                            juce::var& document)
{
    document = juce::var {};
    if (!source.existsAsFile())
        return juce::Result::fail("Recovery file does not exist");
    document = juce::JSON::parse(source);
    return validateProjectDocument(document);
}

bool ProjectRecovery::discard(const juce::File& recoveryFile)
{
    return !recoveryFile.exists() || recoveryFile.deleteFile();
}

} // namespace cydaw::app
