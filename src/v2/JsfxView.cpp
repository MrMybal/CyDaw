#include "JsfxView.h"
#include "PluginFiles.h"
#include "Theme.h"
#include "Widgets.h"
#include "WindowsPluginBridge.h"
#include <imgui.h>
#include <imgui_internal.h>
namespace cydaw::v2
{
static std::vector<JsfxView *> &scriptEditors()
{
    static std::vector<JsfxView *> editors;
    return editors;
}

class JsfxView::Compiler final : public juce::Thread
{
  public:
    juce::File source, result;
    juce::String message, compiled;
    bool success{};
    std::atomic<bool> done{};
    explicit Compiler(juce::File f)
        : juce::Thread("CyDAW JSFX compile"), source(std::move(f)),
          result(source.withFileExtension("diagnostics.json"))
    {
    }
    ~Compiler() override
    {
        signalThreadShouldExit();
        stopThread(3000);
        source.deleteFile();
        result.deleteFile();
    }
    void run() override
    {
        auto launch = app::WindowsPluginBridge::workerLaunchFor(source);
        auto args = launch.command;
        args.addArray({"--probe-plugin", source.getFullPathName(), "--output", result.getFullPathName()});
        juce::ChildProcess child;
        if (!launch.isValid() || !child.start(args, 0))
        {
            message = "Cannot start JSFX compiler: " + launch.error;
            done = true;
            return;
        }
        double until = juce::Time::getMillisecondCounterHiRes() + 30000;
        while (child.isRunning() && !threadShouldExit() && juce::Time::getMillisecondCounterHiRes() < until)
            wait(20);
        if (child.isRunning())
        {
            child.kill();
            message = "JSFX compilation timed out";
            done = true;
            return;
        }
        auto data = juce::JSON::parse(result);
        if (auto *list = data.getArray(); list && !list->isEmpty())
        {
            const auto &item = list->getFirst();
            if (item.hasProperty("error"))
                message = item.getProperty("error", {}).toString();
            else
            {
                success = true;
                message = item.getProperty("name", {}).toString() + " — " +
                          juce::String(int(item.getProperty("inputs", 0))) + " → " +
                          juce::String(int(item.getProperty("outputs", 0))) + " channels";
            }
        }
        else
            message = "The JSFX compiler returned no diagnostic";
        done.store(true, std::memory_order_release);
    }
};
JsfxView::JsfxView(Studio &s, UiState &state) : studio_(s), state_(state)
{
    fresh();
    scriptEditors().push_back(this);
}
JsfxView::~JsfxView() { std::erase(scriptEditors(), this); }
bool JsfxView::askBeforeQuit(const std::function<void()> &next)
{
    for (auto *editor : scriptEditors())
        if (editor->changed_)
        {
            editor->state_.showJsfx = true;
            editor->quitting_ = true;
            editor->pending_ = [editor, next]
            {
                editor->changed_ = false;
                editor->quitting_ = false;
                if (!askBeforeQuit(next))
                    next();
            };
            return true;
        }
    return false;
}
void JsfxView::drawQuitPrompts()
{
    for (auto *editor : scriptEditors())
        if (editor->quitting_)
            editor->draw();
}
void JsfxView::fresh()
{
    compiler_.reset();
    file_ = {};
    const char *text =
        "desc:CyDAW Gain\nauthor:CyDAW\n// Original template, MIT license.\nslider1:0<-60,12,0.1>Gain "
        "(dB)\nin_pin:Left\nin_pin:Right\nout_pin:Left\nout_pin:Right\n\n@slider\ngain = 10^(slider1 / "
        "20);\n\n@sample\nspl0 *= gain;\nspl1 *= gain;\n";
    code_.assign(text, text + std::strlen(text) + 1);
    code_.resize(std::max<size_t>(16384, code_.size()));
    changed_ = false;
    error_ = false;
    status_.clear();
}
void JsfxView::load(const juce::File &file)
{
    compiler_.reset();
    if (!file.existsAsFile() || file.getSize() > 8 * 1024 * 1024)
    {
        status_ = "Cannot open JSFX (8 MB maximum)";
        error_ = true;
        return;
    }
    file_ = file;
    auto text = file.loadFileAsString().toStdString();
    code_.assign(text.begin(), text.end());
    code_.resize(std::max<size_t>(16384, text.size() + 4096));
    changed_ = false;
    status_ = file.getFullPathName();
    error_ = false;
}
bool JsfxView::save()
{
    if (file_ == juce::File())
    {
        choose(true);
        return false;
    }
    if (!file_.replaceWithText(juce::String::fromUTF8(code_.data()), false, false, "\n"))
    {
        status_ = "Cannot write " + file_.getFullPathName();
        error_ = true;
        return false;
    }
    changed_ = false;
    status_ = "Saved: " + file_.getFullPathName();
    return true;
}
void JsfxView::choose(bool saving)
{
    (void)plugin_files::scripts().createDirectory();
    chooser_ = std::make_unique<juce::FileChooser>(
        saving ? "Save JSFX" : "Open JSFX",
        file_ == juce::File() ? plugin_files::scripts().getChildFile("CyDAW Gain.jsfx") : file_,
        saving ? "*.jsfx" : "*");
    chooser_->launchAsync(
        (saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                : juce::FileBrowserComponent::openMode) |
            juce::FileBrowserComponent::canSelectFiles,
        [this, saving](const juce::FileChooser &chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File())
                return;
            if (saving)
            {
                file_ = file.withFileExtension("jsfx");
                if (save() && pending_)
                {
                    auto next = std::move(pending_);
                    pending_ = {};
                    next();
                }
            }
            else
                load(file);
        });
}
void JsfxView::request(std::function<void()> action)
{
    if (changed_)
        pending_ = std::move(action);
    else
        action();
}
void JsfxView::compile(bool apply, bool insert)
{
    if (compiler_ && !compiler_->done.load())
        return;
    if ((apply || insert) && file_ == juce::File())
    {
        status_ = "Save this script before applying it to a track";
        error_ = true;
        choose(true);
        return;
    }
    auto folder = file_ == juce::File() ? plugin_files::scripts() : file_.getParentDirectory();
    if (!folder.createDirectory())
    {
        status_ = "Cannot create compiler workspace";
        error_ = true;
        return;
    }
    auto source = folder.getChildFile(".cydaw-jsfx-" + juce::Uuid().toString() + ".jsfx");
    if (!source.replaceWithText(juce::String::fromUTF8(code_.data())))
    {
        status_ = "Cannot write compiler source";
        error_ = true;
        return;
    }
    compiler_ = std::make_unique<Compiler>(source);
    compiler_->compiled = juce::String::fromUTF8(code_.data());
    applyAfterCompile_ = apply;
    insertAfterCompile_ = insert;
    status_ = "Compiling JSFX…";
    error_ = false;
    compiler_->startThread();
}
void JsfxView::poll()
{
    if (!compiler_ || !compiler_->done.load(std::memory_order_acquire))
        return;
    auto &job = *compiler_;
    status_ = job.message;
    error_ = !job.success;
    if (job.success && (applyAfterCompile_ || insertAfterCompile_))
    {
        if (job.compiled != juce::String::fromUTF8(code_.data()))
        {
            status_ = "The source changed during compilation; compile again before applying";
            error_ = true;
        }
        else if (!file_.replaceWithText(job.compiled, false, false, "\n"))
        {
            status_ = "Cannot save compiled source";
            error_ = true;
        }
        else
        {
            changed_ = false;
            auto track = state_.selectedTrack;
            if (!track || track->midi || track->master || track->isBus())
            {
                status_ = "Choose an audio track to insert or reload this JSFX";
                error_ = true;
            }
            else
            {
                bool found = false;
                auto plugins = studio_.engine().trackPlugins(track->audioIndex);
                for (size_t i = 0; i < plugins.size(); ++i)
                    if (plugins[i].file == file_)
                    {
                        found = true;
                        auto result = studio_.engine().reloadTrackPlugin(track->audioIndex, i);
                        if (result.failed())
                        {
                            status_ = result.getErrorMessage();
                            error_ = true;
                        }
                    }
                if (!found && insertAfterCompile_)
                {
                    auto result = studio_.engine().setTrackPlugin(track->audioIndex, file_);
                    if (result.failed())
                    {
                        status_ = result.getErrorMessage();
                        error_ = true;
                    }
                    else
                        found = true;
                }
                if (!found && !error_)
                    status_ = "Compiled and saved. Insert the script from the library or with Add to track.";
                if (found && !error_)
                {
                    status_ = "JSFX applied: " + file_.getFileName();
                    studio_.markDirty();
                }
            }
        }
    }
    compiler_.reset();
}
void JsfxView::draw()
{
    const auto frame = ImGui::GetFrameCount();
    if (drawnFrame_ == frame)
        return;
    drawnFrame_ = frame;
    if (state_.jsfxFileToOpen != juce::File())
    {
        auto file = state_.jsfxFileToOpen;
        state_.jsfxFileToOpen = {};
        request([this, file] { load(file); });
    }
    poll();
    if (!state_.showJsfx)
        return;
    ImGui::SetNextWindowSize(ImVec2(ui::dp(1000), ui::dp(720)), ImGuiCond_FirstUseEver);
    const auto title =
        ui::Ls("Éditeur JSFX", "JSFX editor") + "###jsfxEditor" + juce::String(state_.deviceWindowNamespace);
    if (ImGui::Begin(title.toRawUTF8(), &state_.showJsfx, ImGuiWindowFlags_NoDocking))
    {
        if (ImGui::Button(ui::L("Nouveau", "New")))
            request([this] { fresh(); });
        ImGui::SameLine();
        if (ImGui::Button(ui::L("Ouvrir…", "Open…")))
            request([this] { choose(false); });
        ImGui::SameLine();
        if (ImGui::Button(ui::L("Enregistrer", "Save")))
            save();
        ImGui::SameLine();
        if (ImGui::Button(ui::L("Enregistrer sous…", "Save as…")))
            choose(true);
        ImGui::SameLine();
        ImGui::BeginDisabled(compiler_ != nullptr);
        if (ImGui::Button(ui::L("Compiler", "Compile")))
            compile(false);
        ImGui::SameLine();
        if (ImGui::Button(ui::L("Compiler et appliquer", "Compile and apply")))
            compile(true);
        ImGui::SameLine();
        if (ImGui::Button(ui::L("Ajouter à la piste", "Add to track")))
            compile(true, true);
        ImGui::EndDisabled();
        ImGui::TextDisabled(
            "%s%s",
            (file_ == juce::File() ? juce::String("Unsaved JSFX") : file_.getFullPathName()).toRawUTF8(),
            changed_ ? " *" : "");
        if (pending_)
            ImGui::OpenPopup("jsfxUnsaved");
        if (ImGui::BeginPopupModal("jsfxUnsaved", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (!pending_)
            {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            else
            {
                ImGui::TextUnformatted(
                    ui::L("Conserver les modifications du script ?", "Keep script changes?"));
                if (ImGui::Button(ui::L("Enregistrer", "Save")) && save())
                {
                    auto next = std::move(pending_);
                    pending_ = {};
                    ImGui::CloseCurrentPopup();
                    next();
                }
                ImGui::SameLine();
                if (ImGui::Button(ui::L("Ignorer", "Discard")))
                {
                    auto next = std::move(pending_);
                    pending_ = {};
                    ImGui::CloseCurrentPopup();
                    next();
                }
                ImGui::SameLine();
                if (ImGui::Button(ui::L("Annuler", "Cancel")))
                {
                    pending_ = {};
                    quitting_ = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        const auto available = ImGui::GetContentRegionAvail();
        const auto height = std::max(ui::dp(140), available.y - ui::dp(150));
        ImGui::PushFont(theme::monoFont(), ui::dp(theme::baseFontSize()));
        if (ImGui::InputTextMultiline(
                "##jsfxSource", code_.data(), code_.size(), ImVec2(-1, height),
                ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize,
                [](ImGuiInputTextCallbackData *data)
                {
                    auto *code = static_cast<std::vector<char> *>(data->UserData);
                    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
                    {
                        code->resize(size_t(data->BufSize) + 4096);
                        data->Buf = code->data();
                    }
                    return 0;
                },
                &code_))
            changed_ = true;
        ImGui::PopFont();
        ImGui::TextColored(theme::vec(error_ ? theme::palette().record : theme::palette().textDim), "%s",
                           ui::L("Diagnostics", "Diagnostics"));
        ImGui::BeginChild("jsfxDiagnostics", ImVec2(0, ui::dp(80)), ImGuiChildFlags_Borders);
        ImGui::TextWrapped("%s", status_.toRawUTF8());
        ImGui::EndChild();
        ImGui::TextDisabled("%s", ui::L("JSFX / EEL2 : @init · @slider · @block · @sample · @serialize · "
                                        "@gfx. Les imports sont résolus depuis le dossier du script.",
                                        "JSFX / EEL2: @init · @slider · @block · @sample · @serialize · "
                                        "@gfx. Imports resolve relative to the script folder."));
    }
    ImGui::End();
}
juce::var JsfxView::command(const juce::var &request)
{
    poll();
    state_.showJsfx = true;
    auto action = request.getProperty("action", "info").toString();
    if (action == "open")
        load(juce::File(request.getProperty("file", {}).toString()));
    else if (action == "source")
    {
        auto text = request.getProperty("source", {}).toString().toStdString();
        code_.assign(text.begin(), text.end());
        code_.resize(text.size() + 4096);
        changed_ = true;
    }
    else if (action == "compile")
        compile(false);
    else if (action == "apply")
        compile(true, static_cast<bool>(request.getProperty("insert", false)));
    else if (action == "save")
        save();
    else if (action == "new")
        fresh();
    auto result = juce::DynamicObject::Ptr(new juce::DynamicObject());
    result->setProperty("file", file_.getFullPathName());
    result->setProperty("changed", changed_);
    result->setProperty("compiling", compiler_ != nullptr);
    result->setProperty("error", error_);
    result->setProperty("diagnostics", status_);
    return juce::var(result.get());
}
} // namespace cydaw::v2
