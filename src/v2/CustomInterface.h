#include <cydaw/base/Thread.h>
#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#pragma once
#include "CustomInterfaceABI.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace cydaw::v2
{
// Authoring-side compiler. The component ABI and user source contain no JUCE.
class CustomInterface final
{
  public:
    using Draw = void (*)(const CyInterfaceContext *);
    struct Job
    {
        std::atomic<int> status{}; // 0 unbuilt, 1 compiling, 2 ready, -1 failed
        std::mutex mutex;
        cydaw::base::Text log;
        cydaw::base::File file;
        juce::DynamicLibrary library;
        Draw draw{};
    };
    static cydaw::base::Text key(const cydaw::base::Text &source)
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (auto c : source.toStdString())
        {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }
        return cydaw::base::Text::toHexString(static_cast<juce::int64>(hash)) + "-" + cydaw::base::Text(IMGUI_VERSION_NUM) + "-" +
               cydaw::base::Text(sizeof(CyInterfaceContext)) + "-u8";
    }
    static std::shared_ptr<Job> job(const cydaw::base::Text &source)
    {
        static std::map<cydaw::base::Text, std::shared_ptr<Job>> jobs;
        auto &result = jobs[key(source)];
        if (!result)
        {
            result = std::make_shared<Job>();
            const auto cache = cydaw::base::SystemStats::getEnvironmentVariable("CYDAW_UI_COMPONENT_CACHE", {});
            const auto base = cache.isNotEmpty()
                                  ? cydaw::base::File(cache)
                                  : cydaw::base::File::getSpecialLocation(cydaw::base::File::userApplicationDataDirectory)
                                        .getChildFile("CyDAW/InterfaceComponents");
            result->file = base.getChildFile(key(source)).getChildFile("build/Release/CyInterfaceComponent.dll");
            if (result->file.existsAsFile())
                result->status = 2;
        }
        return result;
    }
    static cydaw::base::Text diagnostics(const std::shared_ptr<Job> &job)
    {
        const std::lock_guard lock(job->mutex);
        return job->log;
    }
    static void build(const cydaw::base::Text &source)
    {
        if (source.trim().isEmpty())
            return;
        auto work = job(source);
        if (work->status.load() == 2 || work->status.exchange(1) == 1)
            return;
        std::thread([work, source] {
            const auto finish = [&](cydaw::base::Text message) {
                {
                    const std::lock_guard lock(work->mutex);
                    work->log = std::move(message);
                }
                work->status = -1;
            };
            const auto root =
                cydaw::base::File(cydaw::base::Text(__FILE__)).getParentDirectory().getParentDirectory().getParentDirectory();
            const auto imgui = root.getChildFile("third_party/imgui");
            if (!imgui.getChildFile("imgui.cpp").existsAsFile())
            {
                finish("Sources Dear ImGui introuvables : " + imgui.getFullPathName());
                return;
            }
            auto cache = cydaw::base::SystemStats::getEnvironmentVariable("CYDAW_UI_COMPONENT_CACHE", {});
            const auto folder =
                (cache.isNotEmpty() ? cydaw::base::File(cache)
                                    : cydaw::base::File::getSpecialLocation(cydaw::base::File::userApplicationDataDirectory)
                                          .getChildFile("CyDAW/InterfaceComponents"))
                    .getChildFile(key(source));
            if (folder.createDirectory().failed())
            {
                finish("Impossible de créer le cache du composant.");
                return;
            }
            folder.getChildFile("Component.cpp").replaceWithText(source);
            cydaw::base::Text cmake = "cmake_minimum_required(VERSION 3.24)\nproject(CyInterfaceComponent LANGUAGES "
                                 "CXX)\nadd_library(CyInterfaceComponent SHARED Component.cpp\n";
            for (const auto *file : {"imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp"})
                cmake += "  \"" + imgui.getChildFile(file).getFullPathName().replaceCharacter('\\', '/') + "\"\n";
            cmake += ")\ntarget_compile_features(CyInterfaceComponent PRIVATE "
                     "cxx_std_20)\ntarget_compile_definitions(CyInterfaceComponent PRIVATE "
                     "NOMINMAX)\ntarget_include_directories(CyInterfaceComponent PRIVATE \"" +
                     imgui.getFullPathName().replaceCharacter('\\', '/') + "\" \"" +
                     root.getChildFile("src/v2").getFullPathName().replaceCharacter('\\', '/') + "\")\n";
            cmake += "target_compile_options(CyInterfaceComponent PRIVATE /utf-8)\n";
            folder.getChildFile("CMakeLists.txt").replaceWithText(cmake);
            cydaw::base::File tool;
            for (const auto *edition : {"Community", "Professional", "Enterprise", "BuildTools"})
            {
                auto f = cydaw::base::File("C:/Program Files/Microsoft Visual Studio/2022")
                             .getChildFile(edition)
                             .getChildFile("Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe");
                if (f.existsAsFile())
                {
                    tool = f;
                    break;
                }
            }
            const auto exe = tool.existsAsFile() ? cydaw::base::Text(tool.getFullPathName()) : cydaw::base::Text("cmake");
            cydaw::base::Text output;
            const auto run = [&](cydaw::base::TextList args) {
                // MSBuild rejects inherited PATH/Path duplicates. CMake's
                // environment utility normalises the child without changing CyDAW.
                cydaw::base::TextList clean{exe,
                                        "-E",
                                        "env",
                                        "--unset=PATH",
                                        "--unset=Path",
                                        "Path=" + cydaw::base::SystemStats::getEnvironmentVariable("PATH", {}),
                                        "--"};
                clean.addArray(args);
                juce::ChildProcess child;
                if (!child.start(clean))
                {
                    output += "Impossible de lancer CMake.\n";
                    return false;
                }
                char buffer[4096];
                const auto started = cydaw::base::Time::getMillisecondCounterHiRes();
                while (child.isRunning())
                {
                    const auto count = child.readProcessOutput(buffer, sizeof(buffer));
                    if (count > 0)
                        output += cydaw::base::Text::fromUTF8(buffer, count);
                    if (cydaw::base::Time::getMillisecondCounterHiRes() - started > 240000)
                    {
                        child.kill();
                        output += "Compilation expirée.\n";
                        return false;
                    }
                    cydaw::base::Thread::sleep(15);
                }
                for (int count; (count = child.readProcessOutput(buffer, sizeof(buffer))) > 0;)
                    output += cydaw::base::Text::fromUTF8(buffer, count);
                return child.getExitCode() == 0;
            };
            const auto build = folder.getChildFile("build").getFullPathName();
            if (!run({exe, "-S", folder.getFullPathName(), "-B", build, "-G", "Visual Studio 17 2022", "-A", "x64"}) ||
                !run({exe, "--build", build, "--config", "Release", "--parallel", "2"}))
            {
                finish(output);
                return;
            }
            work->file = cydaw::base::File(build).getChildFile("Release/CyInterfaceComponent.dll");
            {
                const std::lock_guard lock(work->mutex);
                work->log = output;
            }
            work->status = 2;
        }).detach();
    }
    static Draw renderer(const cydaw::base::Text &source)
    {
        auto item = job(source);
        if (item->status.load() != 2)
            return nullptr;
        if (!item->draw)
        {
            if (!item->library.open(item->file.getFullPathName()))
                return nullptr;
            item->draw = reinterpret_cast<Draw>(item->library.getFunction("cydawDrawInterfaceV1"));
        }
        return item->draw;
    }
    static cydaw::base::Text example()
    {
        return R"CPP(#include "CustomInterfaceABI.h"
extern "C" CY_INTERFACE_EXPORT void cydawDrawInterfaceV1(const CyInterfaceContext* ui)
{
    if (!ui || ui->abiVersion != 1 || ui->imguiVersion != IMGUI_VERSION_NUM) return;
    ImGui::SetCurrentContext(ui->imgui);
    ImGui::TextUnformatted("Mon composant");
    float value = static_cast<float>(ui->get(ui->user, ui->primary));
    if (ImGui::SliderFloat("Valeur", &value, static_cast<float>(ui->minimum), static_cast<float>(ui->maximum)))
        ui->set(ui->user, ui->primary, value);
}
)CPP";
    }
};
} // namespace cydaw::v2
