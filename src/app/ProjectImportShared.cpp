#include "ProjectImport.h"
#include "ProjectImportShared.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <mutex>

namespace cydaw::app::project_import {
namespace {

std::mutex folderLock;
juce::File folder;

} // namespace

void setImportedAudioFolder(const juce::File& where)
{
    const std::scoped_lock lock(folderLock);
    folder = where;
}

juce::File importedAudioFolder()
{
    const std::scoped_lock lock(folderLock);
    return folder != juce::File() ? folder
                                  : juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("CyDAW Imported Audio");
}

namespace shared {

juce::File joinChannels(const juce::File& left, const juce::File& right, const juce::String& project, const juce::String& name,
                        ImportedProject& out)
{
    auto safe = juce::File::createLegalFileName(name.isNotEmpty() ? name : left.getFileNameWithoutExtension());
    // Beside the project it comes from (it moves with it), else in CyDAW's
    // own folder for imported audio.
    auto where = out.source.getParentDirectory().getChildFile("CyDAW Imported Audio");
    if (out.source == juce::File() || where.createDirectory().failed())
        where = importedAudioFolder().getChildFile(juce::File::createLegalFileName(project));
    const auto target = where.getChildFile(safe + ".wav");
    if (!out.warnings.contains("Stereo files made from the left and right recordings: " + where.getFullPathName()))
        out.warnings.add("Stereo files made from the left and right recordings: " + where.getFullPathName());
    // Made once: the same two files make the same one.
    if (target.existsAsFile() && target.getLastModificationTime() >= left.getLastModificationTime()
        && target.getLastModificationTime() >= right.getLastModificationTime())
        return target;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> leftReader(formats.createReaderFor(left));
    std::unique_ptr<juce::AudioFormatReader> rightReader(formats.createReaderFor(right));
    if (leftReader == nullptr || rightReader == nullptr) {
        out.warnings.add("\"" + name + "\": its two channels could not be read together; its left one plays.");
        return left;
    }
    if (target.getParentDirectory().createDirectory().failed()) {
        out.warnings.add("No folder for the imported audio at " + target.getParentDirectory().getFullPathName());
        return left;
    }
    const auto length = std::max(leftReader->lengthInSamples, rightReader->lengthInSamples);
    (void) target.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(target.createOutputStream());
    if (stream == nullptr)
        return left;
    const auto bits = std::max(16, std::min(24, static_cast<int>(leftReader->bitsPerSample)));
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), leftReader->sampleRate, 2, bits, {}, 0));
    if (writer == nullptr)
        return left;
    (void) stream.release();
    juce::AudioBuffer<float> block(2, 65'536);
    for (juce::int64 at = 0; at < length; at += block.getNumSamples()) {
        const auto count = static_cast<int>(std::min<juce::int64>(block.getNumSamples(), length - at));
        block.clear();
        auto* channels = block.getArrayOfWritePointers();
        (void) leftReader->read(&channels[0], 1, at, count);
        (void) rightReader->read(&channels[1], 1, at, count);
        (void) writer->writeFromAudioSampleBuffer(block, 0, count);
    }
    return target;
}

} // namespace shared
} // namespace cydaw::app::project_import
