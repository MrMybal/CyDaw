#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#include "ExtraPluginFormats.h"
namespace cydaw::plugins
{
std::unique_ptr<juce::AudioPluginInstance> createJsfx(const cydaw::base::File &, cydaw::base::Text &);
std::unique_ptr<juce::AudioPluginInstance> createClap(const cydaw::base::File &, const cydaw::base::Text &,
                                                      cydaw::base::Text &);
void scanClap(const cydaw::base::File &, juce::OwnedArray<juce::PluginDescription> &, cydaw::base::Text &);
bool isExtraFormat(const cydaw::base::File &file)
{
    return file.hasFileExtension("clap;jsfx") || file.getFileExtension().isEmpty();
}
std::unique_ptr<juce::AudioPluginInstance> createExtraPlugin(const cydaw::base::File &file, double, int,
                                                             cydaw::base::Text &error, const cydaw::base::Text &id)
{
    return file.hasFileExtension("clap") ? createClap(file, id, error) : createJsfx(file, error);
}
void scanExtraPlugin(const cydaw::base::File &file, juce::OwnedArray<juce::PluginDescription> &result,
                     cydaw::base::Text &error)
{
    if (file.hasFileExtension("clap"))
    {
        scanClap(file, result, error);
        return;
    }
    auto plugin = createJsfx(file, error);
    if (plugin)
    {
        auto d = std::make_unique<juce::PluginDescription>();
        plugin->fillInPluginDescription(*d);
        result.add(std::move(d));
    }
}
class ExtraFormat final : public juce::AudioPluginFormat
{
    cydaw::base::Text name_;

  public:
    explicit ExtraFormat(cydaw::base::Text name) : name_(std::move(name)) {}
    juce::String getName() const override
    {
        return name_;
    }
    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription> &result,
                             const juce::String &path) override
    {
        cydaw::base::Text error;
        scanExtraPlugin(cydaw::base::File(path), result, error);
        if (error.isNotEmpty())
            juce::Logger::writeToLog(error);
    }
    bool fileMightContainThisPluginType(const juce::String &path) override
    {
        return isExtraFormat(cydaw::base::File(path));
    }
    juce::String getNameOfPluginFromIdentifier(const juce::String &path) override
    {
        return cydaw::base::File(path).getFileNameWithoutExtension();
    }
    bool pluginNeedsRescanning(const juce::PluginDescription &) override
    {
        return true;
    }
    bool doesPluginStillExist(const juce::PluginDescription &d) override
    {
        return cydaw::base::File(d.fileOrIdentifier).exists();
    }
    bool canScanForPlugins() const override
    {
        return true;
    }
    bool isTrivialToScan() const override
    {
        return false;
    }
    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath &paths, bool recursive, bool) override
    {
        cydaw::base::TextList result;
        for (int i = 0; i < paths.getNumPaths(); ++i)
            for (auto f : paths[i].findChildFiles(cydaw::base::File::findFiles, recursive, "*.clap;*.jsfx"))
                result.add(f.getFullPathName());
        return result;
    }
    juce::FileSearchPath getDefaultLocationsToSearch() override
    {
        return {};
    }
    bool requiresUnblockedMessageThreadDuringCreation(const juce::PluginDescription &) const override
    {
        return false;
    }
    void createPluginInstance(const juce::PluginDescription &d, double rate, int block,
                              PluginCreationCallback callback) override
    {
        cydaw::base::Text error;
        auto plugin = createExtraPlugin(cydaw::base::File(d.fileOrIdentifier), rate, block, error,
                                        d.pluginFormatName == "CLAP" ? cydaw::base::Text(d.descriptiveName) : cydaw::base::Text());
        callback(std::move(plugin), error);
    }
};
std::unique_ptr<juce::AudioPluginFormat> extraPluginFormat(const cydaw::base::File &f)
{
    return std::make_unique<ExtraFormat>(f.hasFileExtension("clap") ? "CLAP" : "JSFX");
}
} // namespace cydaw::plugins
