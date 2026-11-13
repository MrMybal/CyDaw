#pragma once

#include <cydaw/base/File.h>

namespace cydaw::document {

class ProjectRecovery final {
public:
    static constexpr int schemaVersion = 1;
    static constexpr int maximumProjectVersion = 36;

    [[nodiscard]] static base::File defaultRecoveryFile();
    [[nodiscard]] static base::Value makeRecoveryDocument(
        base::Value projectDocument,
        const base::File& originalProject,
        base::Time timestamp = base::Time::getCurrentTime());
    [[nodiscard]] static bool isRecoveryDocument(const base::Value& document);
    [[nodiscard]] static base::File originalProject(const base::Value& document);
    [[nodiscard]] static base::Time recoveryTimestamp(const base::Value& document);
    [[nodiscard]] static base::Result validateProjectDocument(
        const base::Value& document);
    // Streamed to the file rather than built as one string, and read back
    // for verification only when asked: on a thirteen-megabyte project the
    // string grew by reallocation and the re-parse allocated a second tree,
    // and the interface thread stalled on the heap behind the writer for a
    // tenth of a second at every autosave. A save the user asks for still
    // reads its file back.
    [[nodiscard]] static base::Result writeDocumentAtomically(
        const base::File& destination,
        const base::Value& document,
        bool verifyRoundTrip = true);
    [[nodiscard]] static base::Result readDocument(
        const base::File& source,
        base::Value& document);
    [[nodiscard]] static bool discard(const base::File& recoveryFile);
};

} // namespace cydaw::document
