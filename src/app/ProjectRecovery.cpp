#include <cydaw/document/ProjectRecovery.h>

namespace cydaw::document {

base::File ProjectRecovery::defaultRecoveryFile()
{
    return base::File::getSpecialLocation(
               base::File::userApplicationDataDirectory)
        .getChildFile("CyDAW")
        .getChildFile("Recovery")
        .getChildFile("last-session.cydaw-recovery");
}

base::Value ProjectRecovery::makeRecoveryDocument(
    base::Value projectDocument, const base::File& originalProject,
    const base::Time timestamp)
{
    if (auto* object = projectDocument.getDynamicObject()) {
        object->setProperty("recoverySchema", schemaVersion);
        object->setProperty("recoveryTimestamp",
                            timestamp.toMilliseconds());
        object->setProperty("recoveryOriginalProject",
                            originalProject == base::File {}
                                ? base::Text {}
                                : originalProject.getFullPathName());
    }
    return projectDocument;
}

bool ProjectRecovery::isRecoveryDocument(const base::Value& document)
{
    const auto* object = document.getDynamicObject();
    return object != nullptr
           && static_cast<int>(object->getProperty("recoverySchema"))
                  == schemaVersion;
}

base::File ProjectRecovery::originalProject(const base::Value& document)
{
    const auto* object = document.getDynamicObject();
    if (object == nullptr)
        return {};
    const auto path = object->getProperty("recoveryOriginalProject")
                          .toString();
    return path.isEmpty() ? base::File {} : base::File(path);
}

base::Time ProjectRecovery::recoveryTimestamp(const base::Value& document)
{
    const auto* object = document.getDynamicObject();
    if (object == nullptr)
        return {};
    return base::Time(static_cast<base::int64>(
        object->getProperty("recoveryTimestamp")));
}

base::Result ProjectRecovery::validateProjectDocument(
    const base::Value& document)
{
    const auto* object = document.getDynamicObject();
    if (object == nullptr
        || object->getProperty("format").toString() != "CyDAW Project")
        return base::Result::fail("Invalid CyDAW project document");
    const auto version = static_cast<int>(object->getProperty("version"));
    if (version <= 0 || version > maximumProjectVersion)
        return base::Result::fail("Unsupported CyDAW project version");
    if (object->hasProperty("recoverySchema")
        && static_cast<int>(object->getProperty("recoverySchema"))
               != schemaVersion)
        return base::Result::fail("Unsupported recovery schema");
    return base::Result::ok();
}

base::Result ProjectRecovery::writeDocumentAtomically(
    const base::File& destination, const base::Value& document,
    const bool verifyRoundTrip)
{
    if (const auto validation = validateProjectDocument(document);
        validation.failed())
        return validation;
    if (destination == base::File {})
        return base::Result::fail("Missing project destination");
    if (!destination.getParentDirectory().createDirectory())
        return base::Result::fail("Could not create project directory");

    base::TemporaryFile temporary(destination);
    {
        base::FileOutputStream output(temporary.getFile());
        if (output.failedToOpen())
            return base::Result::fail("Could not write temporary project file");
        if (auto write = base::JSON::writeToStream(output, document, true); write.failed())
            return write;
        output.flush();
        if (output.getStatus().failed())
            return base::Result::fail("Could not write temporary project file");
    }
    if (verifyRoundTrip) {
        const auto verification = base::JSON::parse(temporary.getFile());
        if (const auto validation = validateProjectDocument(verification);
            validation.failed())
            return validation;
    }
    if (!temporary.overwriteTargetFileWithTemporary())
        return base::Result::fail("Could not replace project file atomically");
    return base::Result::ok();
}

base::Result ProjectRecovery::readDocument(const base::File& source,
                                            base::Value& document)
{
    document = base::Value {};
    if (!source.existsAsFile())
        return base::Result::fail("Recovery file does not exist");
    document = base::JSON::parse(source);
    return validateProjectDocument(document);
}

bool ProjectRecovery::discard(const base::File& recoveryFile)
{
    return !recoveryFile.exists() || recoveryFile.deleteFile();
}

} // namespace cydaw::document
