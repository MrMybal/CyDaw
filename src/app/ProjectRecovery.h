#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace cydaw::app {

class ProjectRecovery final {
public:
    static constexpr int schemaVersion = 1;
    static constexpr int maximumProjectVersion = 36;

    [[nodiscard]] static juce::File defaultRecoveryFile();
    [[nodiscard]] static juce::var makeRecoveryDocument(
        juce::var projectDocument,
        const juce::File& originalProject,
        juce::Time timestamp = juce::Time::getCurrentTime());
    [[nodiscard]] static bool isRecoveryDocument(const juce::var& document);
    [[nodiscard]] static juce::File originalProject(const juce::var& document);
    [[nodiscard]] static juce::Time recoveryTimestamp(const juce::var& document);
    [[nodiscard]] static juce::Result validateProjectDocument(
        const juce::var& document);
    // Streamed to the file rather than built as one string, and read back
    // for verification only when asked: on a thirteen-megabyte project the
    // string grew by reallocation and the re-parse allocated a second tree,
    // and the interface thread stalled on the heap behind the writer for a
    // tenth of a second at every autosave. A save the user asks for still
    // reads its file back.
    [[nodiscard]] static juce::Result writeDocumentAtomically(
        const juce::File& destination,
        const juce::var& document,
        bool verifyRoundTrip = true);
    [[nodiscard]] static juce::Result readDocument(
        const juce::File& source,
        juce::var& document);
    [[nodiscard]] static bool discard(const juce::File& recoveryFile);
};

} // namespace cydaw::app
