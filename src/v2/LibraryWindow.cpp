#include "PluginFiles.h"
#include "WindowsPluginBridge.h"
#include "LibraryWindow.h"

#include "Icons.h"
#include "ImGuiHost.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <map>
#include <set>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {


// Who the semantic search answers here.
constexpr auto semanticOwner = 2;

double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

const char* facetTitle(const int facet)
{
    switch (facet) {
    case SampleCatalog::category: return L("Catégorie", "Category");
    case SampleCatalog::group: return L("Instrument / banque", "Instrument / bank");
    case SampleCatalog::source: return L("Source", "Source");
    case SampleCatalog::type: return L("Type", "Type");
    case SampleCatalog::key: return L("Tonalité", "Key");
    case SampleCatalog::tempo: return L("Tempo", "Tempo");
    case SampleCatalog::length: return L("Durée", "Length");
    default: return "";
    }
}

// A sound's shape in a box: its peaks, one bar per column.
void drawShape(ImDrawList* list, const std::vector<std::uint8_t>& overview, const ImVec2 min, const ImVec2 max, const ImU32 colour,
               const float played = -1.0f)
{
    if (overview.empty())
        return;
    const auto width = max.x - min.x;
    const auto middle = (min.y + max.y) * 0.5f;
    // Each sound at its own scale: a quiet one is seen as well as a loud one.
    const auto loudest = std::max(1, static_cast<int>(*std::max_element(overview.begin(), overview.end())));
    const auto half = (max.y - min.y) * 0.5f * 255.0f / static_cast<float>(loudest);
    const auto columns = std::max(1, static_cast<int>(width / 2.0f));
    for (auto column = 0; column < columns; ++column) {
        const auto from = static_cast<std::size_t>(column) * overview.size() / static_cast<std::size_t>(columns);
        const auto to = std::max(from + 1, static_cast<std::size_t>(column + 1) * overview.size() / static_cast<std::size_t>(columns));
        auto peak = 0;
        for (auto index = from; index < std::min(to, overview.size()); ++index)
            peak = std::max(peak, static_cast<int>(overview[index]));
        const auto x = min.x + static_cast<float>(column) * 2.0f;
        const auto h = std::min((max.y - min.y) * 0.5f, std::max(0.5f, half * static_cast<float>(peak) / 255.0f));
        const auto done = played >= 0.0f && static_cast<float>(column) / static_cast<float>(columns) <= played;
        list->AddRectFilled(ImVec2(x, middle - h), ImVec2(x + 1.0f, middle + h), done ? theme::palette().accent : colour);
    }
}

juce::String plainText(const char* fr, const char* en)
{
    return ui::Ls(fr, en);
}

} // namespace

// ---- The small facets (instruments, effects) -------------------------------------------------------

struct LibraryWindow::Facets {
    std::vector<juce::String> titles;
    std::vector<std::vector<juce::String>> values; // per facet, its values
    std::vector<std::vector<int>> order;           // per facet, the values in label order
    std::vector<std::vector<char>> selected;
    std::vector<std::vector<int>> rowValues;       // per row, its value per facet
    std::vector<std::string> haystacks;
    std::vector<std::map<juce::String, int>> index;

    explicit Facets(std::vector<juce::String> names) : titles(std::move(names))
    {
        values.resize(titles.size());
        order.resize(titles.size());
        selected.resize(titles.size());
        index.resize(titles.size());
    }

    // Starts again, keeping what was chosen (by label).
    void begin()
    {
        std::vector<juce::StringArray> chosen(titles.size());
        for (std::size_t facet = 0; facet < titles.size(); ++facet)
            for (std::size_t value = 0; value < selected[facet].size() && value < values[facet].size(); ++value)
                if (selected[facet][value] != 0)
                    chosen[facet].add(values[facet][value]);
        kept = std::move(chosen);
        for (auto& list : values)
            list.clear();
        for (auto& map : index)
            map.clear();
        rowValues.clear();
        haystacks.clear();
    }

    int value(const std::size_t facet, const juce::String& label)
    {
        if (const auto found = index[facet].find(label); found != index[facet].end())
            return found->second;
        const auto id = static_cast<int>(values[facet].size());
        values[facet].push_back(label);
        index[facet].emplace(label, id);
        return id;
    }

    void addRow(const std::vector<juce::String>& labels, const juce::String& words)
    {
        std::vector<int> row;
        for (std::size_t facet = 0; facet < titles.size(); ++facet)
            row.push_back(value(facet, facet < labels.size() ? labels[facet] : juce::String()));
        rowValues.push_back(std::move(row));
        haystacks.push_back(SampleCatalog::fold(words));
    }

    void end()
    {
        for (std::size_t facet = 0; facet < titles.size(); ++facet) {
            auto& ids = order[facet];
            ids.resize(values[facet].size());
            for (std::size_t id = 0; id < ids.size(); ++id)
                ids[id] = static_cast<int>(id);
            std::sort(ids.begin(), ids.end(), [this, facet](const int a, const int b) {
                return values[facet][static_cast<std::size_t>(a)].compareNatural(values[facet][static_cast<std::size_t>(b)]) < 0;
            });
            selected[facet].assign(values[facet].size(), 0);
            for (const auto& label : kept[facet])
                if (const auto found = index[facet].find(label); found != index[facet].end())
                    selected[facet][static_cast<std::size_t>(found->second)] = 1;
        }
    }

    // The rows kept by the words and the columns, and each value's count as
    // the other columns leave it.
    std::vector<int> filter(const juce::String& text, std::vector<std::vector<int>>& counts) const
    {
        std::vector<std::string> words;
        for (const auto& word : juce::StringArray::fromTokens(text, " ", ""))
            if (word.isNotEmpty())
                words.push_back(SampleCatalog::fold(word));
        counts.assign(titles.size(), {});
        for (std::size_t facet = 0; facet < titles.size(); ++facet)
            counts[facet].assign(values[facet].size(), 0);
        std::vector<bool> active(titles.size());
        for (std::size_t facet = 0; facet < titles.size(); ++facet)
            active[facet] = std::find(selected[facet].begin(), selected[facet].end(), 1) != selected[facet].end();
        std::vector<int> rows;
        for (std::size_t row = 0; row < rowValues.size(); ++row) {
            auto all = true;
            for (const auto& word : words)
                if (haystacks[row].find(word) == std::string::npos)
                    all = false;
            if (!all)
                continue;
            auto failed = -1;
            auto failures = 0;
            for (std::size_t facet = 0; facet < titles.size(); ++facet)
                if (active[facet] && selected[facet][static_cast<std::size_t>(rowValues[row][facet])] == 0) {
                    failed = static_cast<int>(facet);
                    ++failures;
                }
            if (failures > 1)
                continue;
            if (failures == 1) {
                ++counts[static_cast<std::size_t>(failed)][static_cast<std::size_t>(rowValues[row][static_cast<std::size_t>(failed)])];
                continue;
            }
            for (std::size_t facet = 0; facet < titles.size(); ++facet)
                ++counts[facet][static_cast<std::size_t>(rowValues[row][facet])];
            rows.push_back(static_cast<int>(row));
        }
        return rows;
    }

    std::vector<juce::StringArray> kept;
};

struct LibraryWindow::InstrumentRow {
    enum Kind { factory, native, mine };
    Kind kind { factory };
    juce::String key;
    juce::String name;
    juce::String category;
    juce::String genre;
    juce::String state;
    juce::String bank;
    juce::String range;
    juce::String licence;
    juce::String description;
    juce::File file;
    bool missing {};
    bool model {};
    juce::int64 missingBytes {};
    juce::StringArray banks;
};

struct LibraryWindow::EffectRow {
    enum Kind { node, module, preset, mine, vst3 };
    Kind kind { node };
    juce::String key;
    juce::String name;
    juce::String category;
    juce::String genre;
    juce::String role;
    juce::String maker;
    juce::String description;
    juce::File file;
    bool soundSource {};
};

// ---- The VST3 plug-ins on this computer, looked for once on a thread of its own ----------------------

class LibraryWindow::PluginScan final : private juce::Thread {
public:
    struct Plugin {
        juce::String name;
        juce::String vendor;
        juce::String kind; // "Fx", "Instrument"…
        juce::String subcategory;
        juce::String format;
        juce::File file;
    };

    PluginScan() : juce::Thread("CyDAW plug-in list") {}
    ~PluginScan() override { stopThread(5'000); }

    // The standard VST3 folders and these.
    void start(juce::StringArray folders)
    {
        if (isThreadRunning())
            return;
        {
            const std::scoped_lock lock(mutex_);
            extra_ = std::move(folders);
        }
        done_ = false;
        startThread(juce::Thread::Priority::low);
    }
    [[nodiscard]] bool done() const noexcept { return done_.load(); }
    [[nodiscard]] bool scanning() const { return isThreadRunning(); }
    [[nodiscard]] std::vector<Plugin> plugins() const
    {
        const std::scoped_lock lock(mutex_);
        return plugins_;
    }
    [[nodiscard]] int generation() const noexcept { return generation_.load(); }

private:
    void run() override
    {
        std::vector<Plugin> found;
        auto folders = plugin_files::folders();
        {
            const std::scoped_lock lock(mutex_);
            for (const auto& folder : extra_)
                if (juce::File::isAbsolutePath(folder))
                    folders.add(juce::File(folder));
        }
        juce::StringArray seen;
        for (const auto& folder : folders) {
            if (!folder.isDirectory())
                continue;
            for (const auto& entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFilesAndDirectories)) {
                if (threadShouldExit())
                    return;
                const auto file = entry.getFile();
                // A bundle's insides are not plug-ins of their own.
                if (!plugin_files::candidate(file) || seen.contains(file.getFullPathName()))
                    continue;
                seen.add(file.getFullPathName());
                Plugin plugin;
                plugin.name = file.getFileNameWithoutExtension();
                plugin.file = file;
                plugin.format = plugin_files::format(file);
                if (!file.hasFileExtension("vst3")) {
                    auto cache = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("CyDAW/plugin-catalogue");
                    (void) cache.createDirectory();
                    auto stamp = file.getLastModificationTime().toMilliseconds();
                    if (file.isDirectory()) for (const auto& child : juce::RangedDirectoryIterator(file, true, "*", juce::File::findFiles)) stamp = std::max(stamp, child.getFile().getLastModificationTime().toMilliseconds());
                    const auto key = juce::String::toHexString(file.getFullPathName().hashCode64());
                    const auto report = cache.getChildFile(key + ".json");
                    auto data = juce::JSON::parse(report);
                    if (static_cast<juce::int64>(data.getProperty("stamp", 0)) != stamp) {
                        const auto launch = app::WindowsPluginBridge::workerLaunchFor(file);
                        auto command = launch.command; const auto resultFile = cache.getChildFile(key + "-" + juce::Uuid().toString() + ".probe.json");
                        (void) resultFile.deleteFile();
                        command.addArray({"--probe-plugin", file.getFullPathName(), "--output", resultFile.getFullPathName()});
                        juce::ChildProcess process;
                        if (launch.isValid() && process.start(command, 0)) {
                            const auto deadline=juce::Time::getMillisecondCounterHiRes()+15000;
                            while(process.isRunning() && !threadShouldExit() && juce::Time::getMillisecondCounterHiRes()<deadline) wait(30);
                            if(process.isRunning())process.kill();
                            data=juce::JSON::parse(resultFile);
                            if (auto* entries=data.getArray()) {
                                auto root=juce::DynamicObject::Ptr(new juce::DynamicObject());root->setProperty("stamp", stamp);root->setProperty("plugins", *entries);data=juce::var(root.get());
                                (void) report.replaceWithText(juce::JSON::toString(data));
                            }
                        }
                    }
                    if (const auto* entries=data.getProperty("plugins", {}).getArray()) {
                        bool added=false;
                        for (int index=0; index<entries->size(); ++index) {
                            const auto& item=(*entries)[index];if(item.hasProperty("error"))continue;
                            auto foundPlugin=plugin;foundPlugin.name=item.getProperty("name", plugin.name).toString();foundPlugin.vendor=item.getProperty("manufacturer", {}).toString();
                            foundPlugin.kind=static_cast<bool>(item.getProperty("instrument", false))?"Instrument":"Fx";
                            foundPlugin.subcategory=item.getProperty("category", {}).toString();foundPlugin.format=item.getProperty("format", plugin.format).toString();
                            if(entries->size()>1) {
                                auto descriptor=juce::DynamicObject::Ptr(new juce::DynamicObject());descriptor->setProperty("file", file.getFullPathName());descriptor->setProperty("id", item.getProperty("pluginId", {}));
                                const auto descriptors=cache.getSiblingFile("PluginDescriptors");(void)descriptors.createDirectory();
                                descriptor->setProperty("name",foundPlugin.name);descriptor->setProperty("format",foundPlugin.format);
                                const auto entryId=juce::String::toHexString(item.getProperty("pluginId",{}).toString().hashCode64());
                                const auto path=descriptors.getChildFile(key+"-"+entryId+".cyplugin");
                                (void) path.replaceWithText(juce::JSON::toString(juce::var(descriptor.get())));foundPlugin.file=path;
                            }
                            found.push_back(std::move(foundPlugin));added=true;
                        }
                        if(added)continue;
                    }
                }
                // What the bundle says of itself (VST 3.7 and later).
                const auto info = juce::JSON::parse(file.getChildFile("Contents").getChildFile("Resources").getChildFile("moduleinfo.json"));
                plugin.vendor = info.getProperty("Factory Info", {}).getProperty("Vendor", {}).toString();
                if (const auto* classes = info.getProperty("Classes", {}).getArray())
                    for (const auto& item : *classes)
                        if (item.getProperty("Category", {}).toString() == "Audio Module Class") {
                            if (const auto name = item.getProperty("Name", {}).toString(); name.isNotEmpty())
                                plugin.name = name;
                            if (const auto* subs = item.getProperty("Sub Categories", {}).getArray(); subs != nullptr) {
                                juce::StringArray parts;
                                for (const auto& part : *subs)
                                    parts.add(part.toString());
                                plugin.kind = parts.contains("Instrument") ? "Instrument" : "Fx";
                                parts.removeString("Fx");
                                parts.removeString("Instrument");
                                plugin.subcategory = parts.joinIntoString(" ");
                            }
                            break;
                        }
                found.push_back(std::move(plugin));
            }
        }
        std::sort(found.begin(), found.end(), [](const Plugin& a, const Plugin& b) { return a.name.compareNatural(b.name) < 0; });
        {
            const std::scoped_lock lock(mutex_);
            plugins_ = std::move(found);
        }
        ++generation_;
        done_ = true;
    }

    mutable std::mutex mutex_;
    std::vector<Plugin> plugins_;
    juce::StringArray extra_;
    std::atomic<bool> done_ {};
    std::atomic<int> generation_ {};
};

// ---- The window ---------------------------------------------------------------------------------------

LibraryWindow::LibraryWindow(Studio& studio, UiState& state, ImGuiHost& host, SampleCatalog& catalog)
    : studio_(studio), state_(state), host_(host), catalog_(catalog),
      instrumentFacets_(std::make_unique<Facets>(std::vector<juce::String> { "category", "genre", "state", "bank" })),
      effectFacets_(std::make_unique<Facets>(std::vector<juce::String> { "category", "genre", "role", "maker" })),
      plugins_(std::make_unique<PluginScan>())
{
    windowId_ = "###library" + state_.deviceWindowNamespace;
    catalog_.addOnChange(this, [this] {
        dirty_ = true;
        host_.wake();
    });
    query_.sortColumn = SampleCatalog::colName;
}

LibraryWindow::~LibraryWindow()
{
    catalog_.removeOnChange(this);
}

void LibraryWindow::report(const juce::String& message, const bool error)
{
    state_.status = message;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetTime();
}

std::optional<TrackRef> LibraryWindow::target() const
{
    if (state_.libraryTarget)
        return state_.libraryTarget;
    return state_.selectedTrack;
}

void LibraryWindow::picked()
{
    // Opened by a "+": its job is done.
    if (state_.libraryPick && !keepOpen_) {
        state_.showLibrary = false;
        state_.libraryPick = false;
        state_.libraryTarget.reset();
        state_.deviceInsertBefore.reset();
    }
}

void LibraryWindow::draw()
{
    if (!state_.showLibrary)
        return;
    const auto started = nowMs();
    const auto* viewport = ImGui::GetMainViewport();
    if (std::exchange(state_.libraryDetach, false))
        ui::setDetached(windowId_.c_str(), true);
    ui::beforeDetachable(windowId_.c_str());
    const auto detached = ui::isDetached(windowId_.c_str());
    if (maximised_ && !detached) {
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
    } else {
        ImGui::SetNextWindowSize(ImVec2(std::min(dp(1'380.0f), viewport->WorkSize.x * 0.92f), std::min(dp(860.0f), viewport->WorkSize.y * 0.9f)),
                                 ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                                ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(dp(760.0f), dp(480.0f)), ImVec2(FLT_MAX, FLT_MAX));
    if (std::exchange(state_.libraryFocus, false))
        ImGui::SetNextWindowFocus();
    auto open = true;
    auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (detached)
        flags |= ImGuiWindowFlags_NoDocking;
    if (maximised_ && !detached)
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    const auto title = juce::String::fromUTF8(L("Bibliothèque", "Library")) + windowId_.c_str();
    if (ImGui::Begin(title.toRawUTF8(), &open, flags)) {
        drawHeader();
        switch (state_.libraryTab) {
        case 1: drawInstruments(); break;
        case 2: drawEffects(); break;
        default: drawSamples(); break;
        }
        // Escape closes it, when nothing is being typed.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput
            && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            open = false;
    }
    ImGui::End();
    if (!open) {
        state_.showLibrary = false;
        state_.libraryPick = false;
        state_.libraryTarget.reset();
        state_.deviceInsertBefore.reset();
    }
    frameMs_ = nowMs() - started;
}

void LibraryWindow::drawHeader()
{
    const auto& p = theme::palette();
    static const char* tabs[][2] = { { "Sons", "Sounds" }, { "Instruments", "Instruments" }, { "Effets et périphériques", "Effects and devices" } };
    for (auto index = 0; index < 3; ++index) {
        if (index > 0)
            ImGui::SameLine(0.0f, dp(4.0f));
        auto on = state_.libraryTab == index;
        ImGui::PushID(index);
        if (ui::toggle(L(tabs[index][0], tabs[index][1]), &on, p.accent, ImVec2(0.0f, dp(26.0f))))
            state_.libraryTab = index;
        ImGui::PopID();
    }
    // Where a pick goes.
    if (const auto track = target(); track && state_.libraryTab != 0) {
        ImGui::SameLine(0.0f, dp(18.0f));
        ImGui::AlignTextToFramePadding();
        const auto text = (state_.libraryTab == 1 ? ui::Ls("Double-clic : une nouvelle piste", "Double click: a new track")
                                                  : ui::Ls("Ajouter à : ", "Add to: ") + studio_.trackName(*track));
        ImGui::TextColored(theme::vec(state_.libraryPick ? p.accent : p.textDim), "%s", text.toRawUTF8());
    }
    if (state_.libraryPick) {
        ImGui::SameLine(0.0f, dp(12.0f));
        ImGui::Checkbox(L("Garder ouverte", "Keep open"), &keepOpen_);
        ui::tooltip(L("Sinon elle se ferme dès qu'un élément est ajouté.", "Else it closes as soon as something is added."));
    }
    // On the right: as large as the studio's window, out of it.
    const auto button = ImVec2(dp(26.0f), dp(24.0f));
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                  ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - button.x * 2.0f - dp(6.0f)));
    if (!ui::isDetached(windowId_.c_str())) {
        if (ui::iconButton(icon::fullScreen, maximised_ ? L("Taille normale", "Normal size") : L("Toute la fenêtre", "The whole window"),
                           maximised_, p.accent, button))
            maximised_ = !maximised_;
        ImGui::SameLine(0.0f, dp(4.0f));
    }
    (void) ui::detachButton(windowId_.c_str(), button);
    ImGui::Separator();
}

// ---- Sounds ---------------------------------------------------------------------------------------------

void LibraryWindow::setSampleQuery(const juce::String& text, const bool semantic,
                                   const std::vector<std::pair<int, juce::StringArray>>& chosen, const int sortColumn,
                                   const bool ascending)
{
    text.copyToUTF8(search_, sizeof(search_));
    lastSearch_ = search_;
    query_.text = semantic ? juce::String() : text;
    semanticMode_ = semantic;
    semanticHits_.clear();
    semanticSimilarTo_.clear();
    semanticError_.clear();
    semanticEnglish_.clear();
    semanticAsked_.clear();
    semanticPending_ = semantic && text.isNotEmpty();
    editedAt_ = -100.0;
    data_ = catalog_.data();
    for (auto& selection : query_.selected)
        selection.clear();
    for (const auto& [facet, keys] : chosen) {
        if (facet < 0 || facet >= SampleCatalog::facetCount)
            continue;
        auto& selection = query_.selected[static_cast<std::size_t>(facet)];
        selection.assign(data_->values[static_cast<std::size_t>(facet)].size(), 0);
        for (const auto& key : keys)
            if (const auto value = data_->valueOf(static_cast<SampleCatalog::Facet>(facet), key); value >= 0)
                selection[static_cast<std::size_t>(value)] = 1;
    }
    query_.sortColumn = sortColumn;
    query_.ascending = ascending;
    sortToTable_ = true;
    dirty_ = true;
    state_.libraryTab = 0;
}

void LibraryWindow::requery()
{
    // New data: what was chosen, kept by key.
    if (const auto fresh = catalog_.data(); fresh != data_) {
        std::array<juce::StringArray, SampleCatalog::facetCount> keys;
        if (data_ != nullptr)
            for (auto facet = 0; facet < SampleCatalog::facetCount; ++facet) {
                const auto& selection = query_.selected[static_cast<std::size_t>(facet)];
                for (std::size_t value = 0; value < selection.size() && value < data_->values[static_cast<std::size_t>(facet)].size(); ++value)
                    if (selection[value] != 0)
                        keys[static_cast<std::size_t>(facet)].add(data_->values[static_cast<std::size_t>(facet)][value].key);
            }
        data_ = fresh;
        for (auto facet = 0; facet < SampleCatalog::facetCount; ++facet) {
            auto& selection = query_.selected[static_cast<std::size_t>(facet)];
            selection.assign(data_->values[static_cast<std::size_t>(facet)].size(), 0);
            for (const auto& key : keys[static_cast<std::size_t>(facet)])
                if (const auto value = data_->valueOf(static_cast<SampleCatalog::Facet>(facet), key); value >= 0)
                    selection[static_cast<std::size_t>(value)] = 1;
        }
        dirty_ = true;
    }
    if (!dirty_ || data_ == nullptr)
        return;
    for (auto facet = 0; facet < SampleCatalog::facetCount; ++facet)
        query_.selected[static_cast<std::size_t>(facet)].resize(data_->values[static_cast<std::size_t>(facet)].size(), 0);
    query_.ranked = semanticMode_ && (!semanticHits_.empty() || semanticAsked_.isNotEmpty() || semanticSimilarTo_.isNotEmpty())
                        ? &semanticHits_
                        : nullptr;
    result_ = SampleCatalog::run(*data_, query_);
    dirty_ = false;
    // The chosen sound followed to its new row.
    selectedRow_ = -1;
    if (selectedPath_.isNotEmpty())
        if (const auto index = data_->find(selectedPath_); index >= 0)
            for (std::size_t row = 0; row < result_.rows.size(); ++row)
                if (result_.rows[row] == index) {
                    selectedRow_ = static_cast<int>(row);
                    break;
                }
}

void LibraryWindow::takeSemanticAnswer()
{
    auto answer = studio_.semantic().takeAnswer(semanticOwner);
    if (!answer)
        return;
    SemanticSearch::rerank(*answer, studio_.samples(), 600);
    semanticHits_.clear();
    semanticError_ = answer->failed ? answer->error : juce::String();
    semanticEnglish_ = answer->english;
    for (const auto& hit : answer->hits)
        semanticHits_.emplace_back(hit.path, hit.score);
    // Best first, unless another order is asked.
    if (query_.sortColumn == SampleCatalog::colName)
        query_.sortColumn = SampleCatalog::colScore;
    dirty_ = true;
}

void LibraryWindow::addSampleToTrack(const juce::String& path)
{
    auto& engine = studio_.engine();
    const juce::File file(path);
    auto placed = false;
    if (const auto track = state_.selectedTrack; track && !track->midi && !track->master) {
        std::uint64_t clipId {};
        placed = engine.addAudioClipFromFile(track->audioIndex, file, engine.position(), &clipId).wasOk();
    }
    if (!placed)
        if (const auto track = studio_.addAudioTrack(file, engine.position()))
            state_.selectedTrack = track;
    studio_.markDirty();
    report(file.getFileNameWithoutExtension());
}

bool LibraryWindow::drawFacetColumns(const char* id, std::vector<FacetColumn>& columns, const float height)
{
    const auto& p = theme::palette();
    static std::map<juce::String, int> lastClicked;
    auto changed = false;
    ImGui::PushID(id);
    auto total = 0.0f;
    for (const auto& column : columns)
        total += column.weight;
    const auto spacing = dp(4.0f);
    const auto available = ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(columns.size() - 1);
    for (std::size_t index = 0; index < columns.size(); ++index) {
        auto& column = columns[index];
        auto& selected = *column.selected;
        if (index > 0)
            ImGui::SameLine(0.0f, spacing);
        const auto width = std::max(dp(90.0f), available * column.weight / std::max(0.01f, total));
        ImGui::PushID(static_cast<int>(index));
        ImGui::BeginChild("##column", ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
        const auto active = std::find(selected.begin(), selected.end(), 1) != selected.end();
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.82f);
        ImGui::TextColored(theme::vec(active ? p.accent : p.textDim), "%s", column.title.toRawUTF8());
        ImGui::PopFont();
        if (active) {
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - dp(26.0f));
            if (ImGui::SmallButton(icon::close)) {
                std::fill(selected.begin(), selected.end(), char {});
                changed = true;
            }
            ui::tooltip(L("Tout montrer", "Show all"));
        }
        ImGui::BeginChild("##values");
        const auto listWidth = ImGui::GetContentRegionAvail().x;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(column.entries.size()));
        while (clipper.Step())
            for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& entry = column.entries[static_cast<std::size_t>(row)];
                ImGui::PushID(row);
                if (entry.value < 0) {
                    // A family's heading: a click takes all of its values.
                    ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(p.textFaint));
                    if (ImGui::Selectable(entry.label.toRawUTF8(), false)) {
                        std::vector<int> members;
                        for (std::size_t other = static_cast<std::size_t>(row) + 1;
                             other < column.entries.size() && column.entries[other].value >= 0; ++other)
                            members.push_back(column.entries[other].value);
                        const auto all = std::all_of(members.begin(), members.end(), [&selected](const int value) {
                            return selected[static_cast<std::size_t>(value)] != 0;
                        });
                        if (!ImGui::GetIO().KeyCtrl)
                            std::fill(selected.begin(), selected.end(), char {});
                        for (const auto value : members)
                            selected[static_cast<std::size_t>(value)] = all ? 0 : 1;
                        changed = true;
                    }
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                    continue;
                }
                const auto isSelected = selected[static_cast<std::size_t>(entry.value)] != 0;
                if (entry.count == 0 && !isSelected)
                    ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(p.textFaint));
                if (ImGui::Selectable(entry.label.toRawUTF8(), isSelected, ImGuiSelectableFlags_None, ImVec2(listWidth - dp(46.0f), 0.0f))) {
                    const auto& io = ImGui::GetIO();
                    auto& last = lastClicked[column.title];
                    if (io.KeyShift && last >= 0 && last < static_cast<int>(column.entries.size())) {
                        for (auto other = std::min(last, row); other <= std::max(last, row); ++other)
                            if (const auto value = column.entries[static_cast<std::size_t>(other)].value; value >= 0)
                                selected[static_cast<std::size_t>(value)] = 1;
                    } else if (io.KeyCtrl) {
                        selected[static_cast<std::size_t>(entry.value)] = isSelected ? 0 : 1;
                    } else {
                        const auto only = isSelected && std::count(selected.begin(), selected.end(), 1) == 1;
                        std::fill(selected.begin(), selected.end(), char {});
                        if (!only)
                            selected[static_cast<std::size_t>(entry.value)] = 1;
                    }
                    last = row;
                    changed = true;
                }
                if (entry.count == 0 && !isSelected)
                    ImGui::PopStyleColor();
                ImGui::SameLine(listWidth - dp(42.0f));
                ImGui::TextColored(theme::vec(p.textFaint), "%d", entry.count);
                ImGui::PopID();
            }
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

void LibraryWindow::drawSamples()
{
    const auto& p = theme::palette();
    auto& library = studio_.samples();
    auto& semantic = studio_.semantic();
    if (!library.loaded())
        studio_.ensureSamples();
    takeSemanticAnswer();

    // The search: words, or a description; favourites; everything again.
    const auto buttons = dp(250.0f);
    ImGui::SetNextItemWidth(-buttons);
    const auto hint = semanticMode_ ? L("Décrivez le son (kick sombre, nappe aérienne, vinyl crackle…)",
                                        "Describe the sound (dark kick, airy pad, vinyl crackle…)")
                                    : L("Rechercher dans les noms, catégories, instruments, banques, tags…",
                                        "Search names, categories, instruments, banks, tags…");
    const auto edited = ImGui::InputTextWithHint("##librarySearch", hint, search_, sizeof(search_));
    const auto entered = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
    if (edited) {
        editedAt_ = ImGui::GetTime();
        if (semanticMode_) {
            semanticPending_ = true;
        } else {
            query_.text = juce::String::fromUTF8(search_);
            dirty_ = true;
        }
    }
    ImGui::SameLine(0.0f, dp(4.0f));
    {
        const auto ready = semantic.available();
        if (ui::iconButton(icon::lightbulb,
                           ready ? L("Recherche par description (IA)", "Search by description (AI)")
                                 : L("Recherche par description (IA, optionnelle) : à installer", "Search by description (AI, optional): to install"),
                           semanticMode_, p.accent)) {
            if (!ready && !semanticMode_) {
                state_.showSemantic = true;
            } else {
                semanticMode_ = !semanticMode_;
                semanticHits_.clear();
                semanticSimilarTo_.clear();
                semanticError_.clear();
                semanticEnglish_.clear();
                semanticAsked_.clear();
                semanticPending_ = semanticMode_ && search_[0] != 0;
                editedAt_ = 0.0;
                query_.text = semanticMode_ ? juce::String() : juce::String::fromUTF8(search_);
                if (!semanticMode_ && query_.sortColumn == SampleCatalog::colScore)
                    query_.sortColumn = SampleCatalog::colName;
                dirty_ = true;
            }
        }
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(query_.favouritesOnly ? icon::starFill : icon::star, L("Favoris seulement", "Favourites only"),
                       query_.favouritesOnly)) {
        query_.favouritesOnly = !query_.favouritesOnly;
        dirty_ = true;
    }
    ImGui::SameLine(0.0f, dp(6.0f));
    if (ImGui::Button(L("Tout montrer", "Show all"))) {
        for (auto& selection : query_.selected)
            std::fill(selection.begin(), selection.end(), char {});
        search_[0] = 0;
        query_.text.clear();
        query_.favouritesOnly = false;
        semanticHits_.clear();
        semanticSimilarTo_.clear();
        semanticAsked_.clear();
        dirty_ = true;
    }
    ImGui::SameLine(0.0f, dp(6.0f));
    if (ImGui::Button(L("Dossiers…", "Folders…")))
        ImGui::OpenPopup("##libraryFolders");
    if (ImGui::BeginPopup("##libraryFolders")) {
        ImGui::TextDisabled("%s", L("Dossiers analysés", "Scanned folders"));
        std::optional<juce::File> remove;
        for (const auto& folder : library.folders()) {
            ImGui::PushID(folder.getFullPathName().toRawUTF8());
            if (ui::iconButton(icon::close, L("Retirer ce dossier", "Remove this folder"), false, 0, ImVec2(dp(20.0f), dp(20.0f))))
                remove = folder;
            ImGui::SameLine();
            ImGui::TextUnformatted(folder.getFullPathName().toRawUTF8());
            ImGui::PopID();
        }
        if (remove)
            library.removeFolder(*remove);
        if (ImGui::Selectable(L("Ajouter un dossier…", "Add a folder…"))) {
            chooser_ = std::make_unique<juce::FileChooser>(L("Dossier de samples", "Sample folder"), juce::File {});
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this](const juce::FileChooser& chooser) {
                                      if (const auto folder = chooser.getResult(); folder.isDirectory())
                                          studio_.samples().addFolder(folder);
                                  });
        }
        auto banks = library.includeBanks();
        if (ImGui::Checkbox(L("Inclure les banques de CyDAW", "Include CyDAW's banks"), &banks))
            library.setIncludeBanks(banks);
        if (ImGui::Selectable(L("Analyser à nouveau", "Rescan")))
            library.rescan(false);
        if (ImGui::Selectable(L("Recherche sémantique (IA)…", "Semantic search (AI)…")))
            state_.showSemantic = true;
        ImGui::EndPopup();
    }

    // A description asked once the typing pauses, or on Enter.
    if (semanticMode_ && (semanticPending_ || entered) && (entered || ImGui::GetTime() - editedAt_ > 0.6)) {
        semanticPending_ = false;
        const auto text = juce::String::fromUTF8(search_).trim();
        semanticSimilarTo_.clear();
        semanticHits_.clear();
        semanticError_.clear();
        semanticEnglish_.clear();
        semanticAsked_ = text;
        if (text.isNotEmpty())
            (void) semantic.searchText(text, 600, semanticOwner);
        dirty_ = true;
    } else if (semanticMode_ && semanticPending_) {
        host_.wake(2);
    }
    if (semanticMode_) {
        juce::String line;
        auto colour = p.textDim;
        if (semanticSimilarTo_.isNotEmpty())
            line = ui::Ls("Sons proches de « ", "Sounds like \"") + juce::File(semanticSimilarTo_).getFileNameWithoutExtension() + ui::Ls(" »", "\"");
        else if (semanticEnglish_.isNotEmpty())
            line = ui::Ls("Cherché : « ", "Searched: \"") + semanticEnglish_ + ui::Ls(" »", "\"");
        if (semantic.searching() || semantic.status().loadingModel) {
            line = semantic.status().loadingModel ? ui::Ls("Chargement du modèle…", "Loading the model…") : ui::Ls("Recherche…", "Searching…");
            colour = p.accent;
            host_.wake(2);
        } else if (semanticError_.isNotEmpty()) {
            line = semanticError_;
            colour = p.record;
        }
        ImGui::TextColored(theme::vec(colour), "%s", line.isNotEmpty() ? line.toRawUTF8() : " ");
    }

    requery();
    if (data_ == nullptr || data_->items.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(theme::vec(p.textDim), "%s",
                           !library.loaded() || catalog_.building() ? L("Lecture de la bibliothèque…", "Reading the library…")
                                                                    : L("Aucun son : ajoutez un dossier, ou téléchargez des banques "
                                                                        "(Options › Banques de sons).",
                                                                        "No sound: add a folder, or download banks (Options › Sound banks)."));
        host_.wake(6);
        return;
    }

    // The multi-column filter.
    {
        std::vector<FacetColumn> columns;
        static constexpr std::array<float, SampleCatalog::facetCount> weights { 1.4f, 1.6f, 1.2f, 0.8f, 0.8f, 0.9f, 0.9f };
        const auto& families = app::SampleLibrary::categoryFamilies();
        for (auto facet = 0; facet < SampleCatalog::facetCount; ++facet) {
            FacetColumn column;
            column.title = juce::String::fromUTF8(facetTitle(facet));
            column.selected = &query_.selected[static_cast<std::size_t>(facet)];
            column.weight = weights[static_cast<std::size_t>(facet)];
            const auto& values = data_->values[static_cast<std::size_t>(facet)];
            const auto& counts = result_.counts[static_cast<std::size_t>(facet)];
            std::vector<int> ids(values.size());
            for (std::size_t id = 0; id < ids.size(); ++id)
                ids[id] = static_cast<int>(id);
            const auto& ranks = data_->ranks[static_cast<std::size_t>(facet)];
            std::sort(ids.begin(), ids.end(),
                      [&ranks](const int a, const int b) { return ranks[static_cast<std::size_t>(a)] < ranks[static_cast<std::size_t>(b)]; });
            auto family = -2;
            for (const auto id : ids) {
                const auto count = id < static_cast<int>(counts.size()) ? counts[static_cast<std::size_t>(id)] : 0;
                const auto chosen = (*column.selected)[static_cast<std::size_t>(id)] != 0;
                // What the other columns leave out is not listed (what is chosen stays).
                if (count == 0 && !chosen)
                    continue;
                const auto& value = values[static_cast<std::size_t>(id)];
                if (facet == SampleCatalog::category && value.family != family && value.family >= 0
                    && value.family < static_cast<int>(families.size())) {
                    family = value.family;
                    column.entries.push_back({ -1, juce::String::fromUTF8(ui::french() ? families[static_cast<std::size_t>(family)].french
                                                                                       : families[static_cast<std::size_t>(family)].english),
                                               0, family });
                }
                column.entries.push_back({ id, value.label, count, value.family });
            }
            columns.push_back(std::move(column));
        }
        facetHeight_ = std::clamp(facetHeight_, dp(90.0f), std::max(dp(90.0f), ImGui::GetContentRegionAvail().y - dp(220.0f)));
        if (drawFacetColumns("##sampleFacets", columns, facetHeight_)) {
            dirty_ = true;
            requery();
        }
        ImGui::InvisibleButton("##facetSplitter", ImVec2(ImGui::GetContentRegionAvail().x, dp(6.0f)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive())
            facetHeight_ += ImGui::GetIO().MouseDelta.y;
    }

    // The table, and beside it the chosen sound.
    const auto previewWidth = dp(330.0f);
    const auto statusHeight = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 tableSize(ImGui::GetContentRegionAvail().x - previewWidth - dp(8.0f), ImGui::GetContentRegionAvail().y - statusHeight);
    const auto& rows = result_.rows;
    const auto semanticShown = query_.ranked != nullptr;
    const auto flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
                       | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable
                       | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    // The keyboard walks the rows.
    auto moved = 0;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
            moved = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
            moved = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
            moved = 20;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
            moved = -20;
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && selectedPath_.isNotEmpty())
            addSampleToTrack(selectedPath_);
    }
    if (moved != 0 && !rows.empty()) {
        selectedRow_ = std::clamp(selectedRow_ < 0 ? 0 : selectedRow_ + moved, 0, static_cast<int>(rows.size()) - 1);
        selectedPath_ = data_->items[static_cast<std::size_t>(rows[static_cast<std::size_t>(selectedRow_)])].path;
        scrollToSelected_ = true;
        if (autoPreview_)
            (void) studio_.auditionSample(selectedPath_, 0.0, state_.auditionAtTempo);
    }
    if (ImGui::BeginTable("##librarySamples", SampleCatalog::columnCount, flags, tableSize)) {
        using C = SampleCatalog::Column;
        ImGui::TableSetupScrollFreeze(2, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, dp(22.0f), C::colFavourite);
        ImGui::TableSetupColumn(L("Nom", "Name"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_DefaultSort,
                                dp(300.0f), C::colName);
        ImGui::TableSetupColumn(L("Forme", "Shape"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, dp(84.0f), C::colShape);
        ImGui::TableSetupColumn(L("Catégorie", "Category"), ImGuiTableColumnFlags_WidthFixed, dp(110.0f), C::colCategory);
        ImGui::TableSetupColumn(L("Instrument / banque", "Instrument / bank"), ImGuiTableColumnFlags_WidthFixed, dp(190.0f), C::colGroup);
        ImGui::TableSetupColumn(L("Source", "Source"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, dp(150.0f),
                                C::colSource);
        ImGui::TableSetupColumn(L("Type", "Type"), ImGuiTableColumnFlags_WidthFixed, dp(64.0f), C::colType);
        ImGui::TableSetupColumn(L("Durée", "Length"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending,
                                dp(58.0f), C::colLength);
        ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, dp(44.0f), C::colBpm);
        ImGui::TableSetupColumn(L("Tonalité", "Key"), ImGuiTableColumnFlags_WidthFixed, dp(56.0f), C::colKey);
        ImGui::TableSetupColumn(L("Canaux", "Channels"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, dp(54.0f),
                                C::colChannels);
        ImGui::TableSetupColumn("kHz", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, dp(44.0f), C::colRate);
        ImGui::TableSetupColumn(L("Score", "Score"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, dp(48.0f),
                                C::colScore);
        ImGui::TableSetColumnEnabled(C::colScore, semanticShown);
        // An order asked from outside (the control port): the header shows it,
        // once the table has read its own settings (its first frame).
        if (sortToTable_ && tableFrames_ > 1) {
            sortToTable_ = false;
            ImGui::TableSetColumnSortDirection(query_.sortColumn,
                                               query_.ascending ? ImGuiSortDirection_Ascending : ImGuiSortDirection_Descending, false);
        }
        ++tableFrames_;
        ImGui::TableHeadersRow();
        if (auto* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsDirty && !sortToTable_) {
            if (specs->SpecsCount > 0) {
                query_.sortColumn = static_cast<int>(specs->Specs[0].ColumnUserID);
                query_.ascending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
            }
            specs->SpecsDirty = false;
            dirty_ = true;
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        if (scrollToSelected_ && selectedRow_ >= 0)
            clipper.IncludeItemByIndex(selectedRow_);
        while (clipper.Step()) {
            for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto index = rows[static_cast<std::size_t>(row)];
                const auto& item = data_->items[static_cast<std::size_t>(index)];
                const auto* record = library.find(item.path);
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(C::colFavourite);
                const auto favourite = record != nullptr ? record->favourite : item.favourite;
                ImGui::PushStyleColor(ImGuiCol_Text, favourite ? p.accent : p.textFaint);
                ImGui::TextUnformatted(favourite ? icon::starFill : icon::star);
                ImGui::PopStyleColor();
                if (ImGui::IsItemClicked() && record != nullptr)
                    library.setFavourite(item.path, !favourite);
                ImGui::TableSetColumnIndex(C::colName);
                const auto selected = item.path == selectedPath_;
                if (selected && scrollToSelected_) {
                    ImGui::SetScrollHereY(0.5f);
                    scrollToSelected_ = false;
                }
                if (ImGui::Selectable(item.label.toRawUTF8(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap
                                          | ImGuiSelectableFlags_AllowDoubleClick)) {
                    selectedPath_ = item.path;
                    selectedRow_ = row;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        addSampleToTrack(item.path);
                    else if (autoPreview_)
                        (void) studio_.auditionSample(item.path, 0.0, state_.auditionAtTempo);
                }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    const auto path = item.path.toStdString();
                    ImGui::SetDragDropPayload(kFilePayload, path.c_str(), path.size() + 1);
                    ImGui::TextUnformatted(item.label.toRawUTF8());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginPopupContextItem("##libSampleMenu")) {
                    selectedPath_ = item.path;
                    if (ImGui::MenuItem(L("Ajouter à la piste", "Add to the track")))
                        addSampleToTrack(item.path);
                    if (ImGui::MenuItem(L("Sons similaires", "Similar sounds"), nullptr, false, semantic.available())) {
                        semanticMode_ = true;
                        semanticSimilarTo_ = item.path;
                        semanticAsked_.clear();
                        semanticHits_.clear();
                        (void) semantic.searchSimilar(item.path, 600, semanticOwner);
                        dirty_ = true;
                    }
                    if (ImGui::MenuItem(L("Favori", "Favourite"), nullptr, favourite))
                        library.setFavourite(item.path, !favourite);
                    if (ImGui::BeginMenu(L("Catégorie", "Category"))) {
                        for (const auto& family : app::SampleLibrary::categoryFamilies()) {
                            ImGui::TextDisabled("%s", ui::french() ? family.french : family.english);
                            for (const auto& category : app::SampleLibrary::categoryTaxonomy())
                                if (juce::String(category.family) == family.key
                                    && ImGui::MenuItem((juce::String("  ") + juce::String::fromUTF8(ui::french() ? category.french : category.english))
                                                           .toRawUTF8(),
                                                       nullptr, record != nullptr && record->category == category.key))
                                    library.setCategory(item.path, category.key);
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(L("Montrer dans l'explorateur", "Show in the file explorer")))
                        juce::File(item.path).revealToUser();
                    if (ImGui::MenuItem(L("Copier le chemin", "Copy the path")))
                        juce::SystemClipboard::copyTextToClipboard(item.path);
                    ImGui::EndPopup();
                }
                if (ImGui::TableSetColumnIndex(C::colShape) && record != nullptr) {
                    const auto min = ImGui::GetCursorScreenPos();
                    const auto width = ImGui::GetContentRegionAvail().x;
                    const auto height = ImGui::GetTextLineHeight();
                    drawShape(ImGui::GetWindowDrawList(), record->overview, min, ImVec2(min.x + width, min.y + height), p.audio);
                }
                if (ImGui::TableSetColumnIndex(C::colCategory))
                    ImGui::TextColored(theme::vec(item.aiCategory ? p.accent : p.textDim), "%s", item.categoryText.toRawUTF8());
                if (ImGui::TableSetColumnIndex(C::colGroup))
                    ImGui::TextColored(theme::vec(p.textDim), "%s", item.groupText.toRawUTF8());
                if (ImGui::TableSetColumnIndex(C::colSource))
                    ImGui::TextColored(theme::vec(p.textFaint), "%s", item.sourceText.toRawUTF8());
                if (ImGui::TableSetColumnIndex(C::colType))
                    ImGui::TextColored(theme::vec(p.textFaint), "%s", item.typeText.toRawUTF8());
                if (ImGui::TableSetColumnIndex(C::colLength))
                    ImGui::TextColored(theme::vec(p.textFaint), "%s", item.lengthText.toRawUTF8());
                if (ImGui::TableSetColumnIndex(C::colBpm))
                    ImGui::TextColored(theme::vec(p.textDim), "%s", item.bpmText.toRawUTF8());
                if (ImGui::TableSetColumnIndex(C::colKey))
                    ImGui::TextColored(theme::vec(p.textDim), "%s", item.keyText.toRawUTF8());
                if (ImGui::TableSetColumnIndex(C::colChannels))
                    ImGui::TextColored(theme::vec(p.textFaint), "%s", item.channels == 1 ? "mono" : item.channels == 2 ? "stereo" : "");
                if (ImGui::TableSetColumnIndex(C::colRate))
                    ImGui::TextColored(theme::vec(p.textFaint), "%s", item.rateText.toRawUTF8());
                if (semanticShown && ImGui::TableSetColumnIndex(C::colScore) && static_cast<std::size_t>(row) < result_.scores.size())
                    ImGui::TextColored(theme::vec(p.textDim), "%.0f", result_.scores[static_cast<std::size_t>(row)]);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::SameLine(0.0f, dp(8.0f));
    const SampleCatalog::Item* chosen = nullptr;
    if (selectedPath_.isNotEmpty())
        if (const auto index = data_->find(selectedPath_); index >= 0)
            chosen = &data_->items[static_cast<std::size_t>(index)];
    drawSamplePreview(chosen, previewWidth);

    // What is shown, of how many, and how fast.
    const auto status = juce::String(result_.matched) + ui::Ls(" sons sur ", " sounds of ") + juce::String(static_cast<int>(data_->items.size()))
                        + "   \xc2\xb7   " + ui::Ls("filtre ", "filter ") + juce::String(result_.ms, 1) + " ms";
    ImGui::TextColored(theme::vec(p.textFaint), "%s", status.toRawUTF8());
    if (library.isScanning()) {
        ImGui::SameLine();
        const auto progress = library.progress();
        const auto text = progress.toAnalyse > 0
                              ? ui::Ls("   analyse : ", "   analysing: ") + juce::String(progress.analysed) + " / " + juce::String(progress.toAnalyse)
                              : ui::Ls("   vérification des fichiers : ", "   checking the files: ") + juce::String(progress.found);
        ImGui::TextColored(theme::vec(p.accent), "%s", text.toRawUTF8());
        host_.wake(4);
    }
}

void LibraryWindow::tempoSwitch(const juce::String& path)
{
    const auto& p = theme::palette();
    if (ImGui::Checkbox(L("Au tempo", "At tempo"), &state_.auditionAtTempo) && path.isNotEmpty()
        && studio_.engine().isSamplePreviewing())
        (void) studio_.auditionSample(path, 0.0, state_.auditionAtTempo);
    ui::tooltip(L("Les boucles (celles dont le tempo est connu) jouées au tempo du projet, étirées sans changer de hauteur, "
                  "calées sur la mesure quand le transport tourne.",
                  "Loops (those whose tempo is known) played at the project's tempo, stretched without changing pitch, "
                  "in step with the bar while the transport runs."));
    if (const auto from = path.isNotEmpty() ? studio_.auditionTempoOf(path) : 0.0; from > 0.0) {
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(p.accent), "%.0f \xe2\x86\x92 %.0f BPM", from, studio_.engine().tempo());
    }
}

void LibraryWindow::drawSamplePreview(const SampleCatalog::Item* item, const float width)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    auto& library = studio_.samples();
    ImGui::BeginChild("##samplePreview", ImVec2(width, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing()),
                      ImGuiChildFlags_Borders);
    if (item == nullptr) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(p.textFaint), "%s",
                           L("Choisissez un son : il s'écoute, et ses détails s'affichent ici. Flèches : le suivant ; Entrée : "
                             "sur la piste ; glisser : où vous voulez.",
                             "Choose a sound: it plays, and its details show here. Arrows: the next one; Enter: onto the "
                             "track; drag: wherever you like."));
        ImGui::PopTextWrapPos();
        ImGui::Checkbox(L("Écoute au clic", "Preview on click"), &autoPreview_);
        ImGui::SameLine();
        tempoSwitch({});
        ImGui::EndChild();
        return;
    }
    const auto* record = library.find(item->path);
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(item->label.toRawUTF8());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    {
        auto where = item->categoryText.isNotEmpty() ? item->categoryText : ui::Ls("sans catégorie", "uncategorised");
        if (item->groupText.isNotEmpty())
            where << "  \xe2\x80\xba  " << item->groupText;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(item->aiCategory ? p.accent : p.textDim), "%s", where.toRawUTF8());
        ImGui::PopTextWrapPos();
    }
    // Its shape, large: a click plays from there.
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, dp(110.0f));
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), p.bg0, dp(4.0f));
    const auto playing = engine.isSamplePreviewing() && record != nullptr && record->length > 0.0;
    const auto played = playing ? static_cast<float>(engine.samplePreviewPositionSeconds() / record->length) : -1.0f;
    if (record != nullptr)
        drawShape(list, record->overview, ImVec2(origin.x + dp(4.0f), origin.y + dp(4.0f)),
                  ImVec2(origin.x + size.x - dp(4.0f), origin.y + size.y - dp(4.0f)), p.audio, played);
    ImGui::InvisibleButton("##previewShape", size);
    if (ImGui::IsItemClicked() && record != nullptr && record->length > 0.0) {
        const auto t = (ImGui::GetIO().MousePos.x - origin.x) / size.x * record->length;
        (void) studio_.auditionSample(item->path, std::max(0.0, t), state_.auditionAtTempo);
    }
    if (ImGui::BeginDragDropSource()) {
        const auto path = item->path.toStdString();
        ImGui::SetDragDropPayload(kFilePayload, path.c_str(), path.size() + 1);
        ImGui::TextUnformatted(item->label.toRawUTF8());
        ImGui::EndDragDropSource();
    }
    if (playing)
        host_.wake(2);
    if (ui::iconButton(engine.isSamplePreviewing() ? icon::stop : icon::play, L("Écouter / arrêter", "Preview / stop"),
                       engine.isSamplePreviewing(), p.play)) {
        if (engine.isSamplePreviewing())
            engine.stopSamplePreview();
        else
            (void) studio_.auditionSample(item->path, 0.0, state_.auditionAtTempo);
    }
    ImGui::SameLine();
    ImGui::Checkbox(L("Écoute au clic", "Preview on click"), &autoPreview_);
    ImGui::SameLine();
    tempoSwitch(item->path);
    // What it is.
    const auto row = [&p](const char* label, const juce::String& value) {
        if (value.isEmpty())
            return;
        ImGui::TextColored(theme::vec(p.textFaint), "%s", label);
        ImGui::SameLine(dp(92.0f));
        ImGui::TextUnformatted(value.toRawUTF8());
    };
    row(L("Durée", "Length"), item->lengthText);
    row(L("Type", "Type"), item->typeText);
    row("BPM", item->bpmText);
    row(L("Tonalité", "Key"), item->keyText);
    row(L("Format", "Format"), (item->channels == 1 ? juce::String("mono") : item->channels == 2 ? juce::String("stereo") : juce::String(item->channels))
                                   + "  " + item->rateText + " kHz");
    row(L("Source", "Source"), item->sourceText);
    ImGui::Spacing();
    if (ImGui::Button(L("Ajouter à la piste", "Add to the track"), ImVec2(-1.0f, 0.0f)))
        addSampleToTrack(item->path);
    ui::tooltip(L("Sur la piste audio choisie, à la tête de lecture ; sinon sur une nouvelle piste.",
                  "On the chosen audio track, at the playhead; else on a new track."));
    if (studio_.semantic().available() && ImGui::Button(L("Sons similaires", "Similar sounds"), ImVec2(-1.0f, 0.0f))) {
        semanticMode_ = true;
        semanticSimilarTo_ = item->path;
        semanticAsked_.clear();
        semanticHits_.clear();
        (void) studio_.semantic().searchSimilar(item->path, 600, semanticOwner);
        dirty_ = true;
    }
    if (ImGui::Button(L("Montrer dans l'explorateur", "Show in the file explorer"), ImVec2(-1.0f, 0.0f)))
        juce::File(item->path).revealToUser();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textFaint), "%s", item->path.toRawUTF8());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

// ---- Instruments ----------------------------------------------------------------------------------------

void LibraryWindow::drawInstruments()
{
    const auto& p = theme::palette();
    auto& facets = *instrumentFacets_;
    const auto download = studio_.banks().progress();
    // Read again now and then (a download ends, a device saved).
    if (ImGui::GetTime() - instrumentsTime_ > 5.0 || (downloading_ && !download.running)) {
        instrumentsTime_ = ImGui::GetTime();
        instruments_.clear();
        for (const auto& instrument : studio_.factoryInstruments()) {
            InstrumentRow row;
            row.kind = InstrumentRow::factory;
            row.key = instrument.key;
            row.name = instrument.name;
            row.category = instrument.categoryName;
            row.genre = plainText("Échantillonné", "Sampled");
            row.missing = instrument.missingFiles > 0;
            row.missingBytes = instrument.missingBytes;
            row.banks = instrument.banks;
            row.state = !row.missing ? plainText("Prêt", "Ready")
                        : studio_.banks().generatedOnly(instrument.banks) ? plainText("À générer", "To generate")
                                                                           : plainText("À télécharger", "To download");
            if (const auto* bank = studio_.banks().find(instrument.bank); bank != nullptr && bank->title.isNotEmpty())
                row.bank = bank->title;
            else
                row.bank = instrument.source.isNotEmpty() ? instrument.source : instrument.bank;
            row.range = instrument.range;
            row.licence = instrument.licence;
            row.model = instrument.model != juce::File();
            instruments_.push_back(std::move(row));
        }
        for (const auto& entry : studio_.deviceCatalogue()) {
            if (!entry.soundSource || entry.kind == "node")
                continue;
            InstrumentRow row;
            row.kind = InstrumentRow::native;
            row.key = entry.key;
            row.name = entry.name;
            row.category = entry.kind == "preset" ? plainText("Synthés (chaînes)", "Synths (chains)") : plainText("Synthés", "Synths");
            row.genre = plainText("Synthé de CyDAW", "CyDAW synth");
            row.state = plainText("Prêt", "Ready");
            row.bank = "CyDAW";
            row.description = entry.description;
            instruments_.push_back(std::move(row));
        }
        for (const auto& device : studio_.libraryDevices()) {
            if (!device.instrument)
                continue;
            InstrumentRow row;
            row.kind = InstrumentRow::mine;
            row.name = device.name;
            row.category = plainText("Mes instruments", "My instruments");
            row.genre = plainText("Mes périphériques", "My devices");
            row.state = plainText("Prêt", "Ready");
            row.bank = plainText("Mes périphériques", "My devices");
            row.description = device.description;
            row.file = device.file;
            instruments_.push_back(std::move(row));
        }
        facets.begin();
        for (const auto& row : instruments_)
            facets.addRow({ row.category, row.genre, row.state, row.bank },
                          row.name + " " + row.category + " " + row.genre + " " + row.bank + " " + row.description);
        facets.end();
    }
    downloading_ = download.running;

    ImGui::SetNextItemWidth(-dp(360.0f));
    ImGui::InputTextWithHint("##instrumentSearch", L("Rechercher un instrument…", "Search an instrument…"), instrumentSearch_,
                             sizeof(instrumentSearch_));
    ImGui::SameLine();
    if (ImGui::Button(L("Nouveau sampler…", "New sampler…"))) {
        chooser_ = std::make_unique<juce::FileChooser>(L("Les échantillons du sampler", "The sampler's samples"), juce::File {},
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this](const juce::FileChooser& chooser) {
                                  if (chooser.getResults().isEmpty())
                                      return;
                                  juce::String error;
                                  if (const auto track = studio_.newSamplerTrack(chooser.getResults(), error))
                                      state_.selectTrack(*track);
                                  else
                                      report(error, true);
                              });
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Banques de sons…", "Sound banks…")))
        state_.showBanks = true;

    std::vector<std::vector<int>> counts;
    const auto facetRows = facets.filter(juce::String::fromUTF8(instrumentSearch_), counts);
    {
        std::vector<FacetColumn> columns;
        const char* titles[][2] = { { "Catégorie", "Category" }, { "Genre", "Kind" }, { "État", "State" }, { "Source", "Source" } };
        for (std::size_t facet = 0; facet < facets.titles.size(); ++facet) {
            FacetColumn column;
            column.title = juce::String::fromUTF8(L(titles[facet][0], titles[facet][1]));
            column.selected = &facets.selected[facet];
            column.weight = facet == 0 ? 1.4f : 1.0f;
            for (const auto id : facets.order[facet]) {
                const auto count = counts[facet][static_cast<std::size_t>(id)];
                if (count == 0 && facets.selected[facet][static_cast<std::size_t>(id)] == 0)
                    continue;
                column.entries.push_back({ id, facets.values[facet][static_cast<std::size_t>(id)], count });
            }
            columns.push_back(std::move(column));
        }
        (void) drawFacetColumns("##instrumentFacets", columns, dp(170.0f));
    }
    const auto previewWidth = dp(330.0f);
    const ImVec2 tableSize(ImGui::GetContentRegionAvail().x - previewWidth - dp(8.0f), ImGui::GetContentRegionAvail().y);
    // The instrument track it was opened for (its "+"), or the one picked:
    // what is chosen takes the place of its instrument, its effects kept.
    // Without one, or asked, a new track.
    std::optional<TrackRef> onto;
    if (const auto track = target(); track && track->midi && !track->master && !track->isBus() && studio_.isValid(*track))
        onto = track;
    const auto add = [this](const InstrumentRow& row, const bool model, const std::optional<TrackRef> on) {
        juce::String error;
        std::optional<TrackRef> track;
        if (row.kind == InstrumentRow::factory) {
            if (row.missing && !studio_.banks().busy())
                (void) studio_.downloadFactoryInstrument(row.key, error);
            track = studio_.addFactoryInstrument(row.key, error, model, on);
        } else if (row.kind == InstrumentRow::native) {
            if (on) {
                if (const auto replaced = studio_.replaceInstrument(*on, row.key, row.name); replaced.wasOk())
                    track = on;
                else
                    error = replaced.getErrorMessage();
            } else {
                track = studio_.addMidiTrack({}, row.key);
            }
        } else if ((track = on ? on : studio_.addMidiTrack(row.name))) {
            if (const auto inserted = studio_.insertLibraryDevice(*track, row.file); inserted.failed())
                error = inserted.getErrorMessage();
        }
        if (track) {
            state_.selectTrack(*track);
            report(studio_.trackName(*track));
            picked();
        } else {
            report(error, true);
        }
    };
    auto rows = facetRows;
    if (ImGui::BeginTable("##libraryInstruments", 5,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV
                              | ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable,
                          tableSize)) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn(L("Nom", "Name"), ImGuiTableColumnFlags_WidthStretch, 2.5f);
        ImGui::TableSetupColumn(L("Catégorie", "Category"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 1.2f);
        ImGui::TableSetupColumn(L("Source", "Source"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(L("Étendue", "Range"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, dp(90.0f));
        ImGui::TableSetupColumn(L("État", "State"), ImGuiTableColumnFlags_WidthFixed, dp(120.0f));
        ImGui::TableHeadersRow();
        if (const auto* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
            const auto column = specs->Specs[0].ColumnIndex;
            const auto ascending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
            const auto field = [this, column](const int index) -> const juce::String& {
                const auto& row = instruments_[static_cast<std::size_t>(index)];
                return column == 1 ? row.category : column == 2 ? row.bank : column == 4 ? row.state : row.name;
            };
            std::stable_sort(rows.begin(), rows.end(), [&](const int a, const int b) {
                const auto order = field(a).compareNatural(field(b));
                const auto byName = instruments_[static_cast<std::size_t>(a)].name.compareNatural(instruments_[static_cast<std::size_t>(b)].name);
                const auto value = order != 0 ? order : byName;
                return ascending ? value < 0 : value > 0;
            });
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step())
            for (auto line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
                const auto index = rows[static_cast<std::size_t>(line)];
                const auto& row = instruments_[static_cast<std::size_t>(index)];
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(row.name.toRawUTF8(), selectedInstrument_ == index,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    selectedInstrument_ = index;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        add(row, false, onto);
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(theme::vec(p.textDim), "%s", row.category.toRawUTF8());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(theme::vec(p.textDim), "%s", row.bank.toRawUTF8());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(theme::vec(p.textFaint), "%s", row.range.toRawUTF8());
                ImGui::TableSetColumnIndex(4);
                const auto state = row.missing ? row.state + " " + ui::sizeText(row.missingBytes) : row.state;
                ImGui::TextColored(theme::vec(row.missing ? p.accent : p.textFaint), "%s", state.toRawUTF8());
                ImGui::PopID();
            }
        ImGui::EndTable();
    }
    ImGui::SameLine(0.0f, dp(8.0f));
    ImGui::BeginChild("##instrumentPreview", ImVec2(previewWidth, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders);
    if (selectedInstrument_ >= 0 && selectedInstrument_ < static_cast<int>(instruments_.size())) {
        const auto& row = instruments_[static_cast<std::size_t>(selectedInstrument_)];
        ImGui::PushFont(theme::boldFont(), 0.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(row.name.toRawUTF8());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::TextColored(theme::vec(p.textDim), "%s", (row.category + "  \xc2\xb7  " + row.genre).toRawUTF8());
        if (row.description.isNotEmpty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(row.description.toRawUTF8());
            ImGui::PopTextWrapPos();
        }
        if (row.range.isNotEmpty())
            ImGui::TextColored(theme::vec(p.textFaint), "%s", (ui::Ls("Étendue : ", "Range: ") + row.range).toRawUTF8());
        if (row.licence.isNotEmpty())
            ImGui::TextColored(theme::vec(p.textFaint), "%s", (ui::Ls("Licence : ", "Licence: ") + row.licence).toRawUTF8());
        if (row.missing)
            ImGui::TextColored(theme::vec(p.accent), "%s",
                               (row.state + " : " + ui::sizeText(row.missingBytes) + " (" + row.banks.joinIntoString(", ") + ")").toRawUTF8());
        ImGui::Spacing();
        if (onto) {
            // In place of the track's instrument (the double click's way).
            const auto label = ui::Ls("Remplacer l'instrument de « ", "Replace the instrument of \"") + studio_.trackName(*onto)
                               + ui::Ls(" »", "\"");
            if (ui::primaryButton(label.toRawUTF8(), ImVec2(-1.0f, 0.0f)))
                add(row, false, onto);
            ui::tooltip(L("Ses effets, leur automation et ses modulateurs restent", "Its effects, their automation and its modulators stay"));
            if (row.model && ImGui::Button(L("Remplacer (modèle de son)", "Replace (sound model)"), ImVec2(-1.0f, 0.0f)))
                add(row, true, onto);
        }
        if (ImGui::Button(L("Nouvelle piste", "New track"), ImVec2(-1.0f, 0.0f)))
            add(row, false, std::nullopt);
        if (row.model && ImGui::Button(L("Nouvelle piste (modèle de son)", "New track (sound model)"), ImVec2(-1.0f, 0.0f)))
            add(row, true, std::nullopt);
    } else {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(p.textFaint), "%s",
                           onto ? (ui::Ls("Double-clic : il remplace l'instrument de « ", "Double click: it replaces the instrument of \"")
                                   + studio_.trackName(*onto)
                                   + ui::Ls(" » (ses effets restent ; ses échantillons téléchargés ou générés s'il le faut).",
                                            "\" (its effects stay; its samples downloaded or generated if need be)."))
                                      .toRawUTF8()
                                : L("Double-clic : une nouvelle piste qui le joue (ses échantillons téléchargés ou générés s'il le faut).",
                                    "Double click: a new track playing it (its samples downloaded or generated if need be)."));
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

// ---- Effects and devices ------------------------------------------------------------------------------

void LibraryWindow::drawEffects()
{
    const auto& p = theme::palette();
    auto& facets = *effectFacets_;
    const auto track = target();
    const auto pluginFolders = juce::StringArray::fromLines(studio_.samples().setting("pluginFolders", juce::String()).toString());
    if (!plugins_->done() && !plugins_->scanning())
        plugins_->start(pluginFolders);
    static int pluginGeneration = -1;
    // Made again when the target changes, the plug-ins are found, now and then.
    const auto stale = effectsFor_ != track || ImGui::GetTime() - effectsTime_ > 10.0 || pluginGeneration != plugins_->generation();
    if (stale) {
        effectsFor_ = track;
        effectsTime_ = ImGui::GetTime();
        pluginGeneration = plugins_->generation();
        effects_.clear();
        const auto audioTarget = track && !track->midi;
        const auto& catalogue = studio_.deviceCatalogue();
        for (const auto& entry : catalogue) {
            if (audioTarget && entry.soundSource)
                continue;
            if (entry.key == "mono-to-stereo" || entry.key == "stereo-to-mono")
                continue;
            // A whole chain of effects would take every card's place: the
            // track's preset button (in Devices) has them.
            if (entry.kind == "preset" && !entry.soundSource)
                continue;
            EffectRow row;
            row.kind = entry.kind == "preset" ? EffectRow::preset : entry.kind == "module" ? EffectRow::module : EffectRow::node;
            row.key = entry.key;
            row.name = entry.name;
            row.description = entry.description;
            row.soundSource = entry.soundSource;
            row.category = entry.kind == "preset" ? plainText("Instruments", "Instruments")
                           : entry.category.isNotEmpty() ? entry.category
                                                         : plainText("Utilitaires", "Utilities");
            row.genre = row.kind == EffectRow::preset   ? plainText("Instrument de CyDAW", "CyDAW instrument")
                        : row.kind == EffectRow::module ? plainText("Périphérique de CyDAW", "CyDAW device")
                                                        : plainText("Nœud", "Node");
            row.role = entry.soundSource          ? plainText("Instrument", "Instrument")
                       : entry.input == "midi"    ? plainText("Effet MIDI", "MIDI effect")
                       : entry.input == "audio"   ? plainText("Effet audio", "Audio effect")
                                                  : plainText("Modulation, utilitaire", "Modulation, utility");
            row.maker = "CyDAW";
            effects_.push_back(std::move(row));
        }
        for (const auto& device : studio_.libraryDevices()) {
            if (audioTarget && device.instrument)
                continue;
            EffectRow row;
            row.kind = EffectRow::mine;
            row.name = device.name;
            row.description = device.description;
            row.file = device.file;
            row.soundSource = device.instrument;
            row.category = plainText("Mes périphériques", "My devices");
            row.genre = plainText("Mes périphériques", "My devices");
            row.role = device.instrument ? plainText("Instrument", "Instrument") : plainText("Effet audio", "Audio effect");
            row.maker = plainText("Moi", "Me");
            effects_.push_back(std::move(row));
        }
        for (const auto& plugin : plugins_->plugins()) {
            EffectRow row;
            row.kind = EffectRow::vst3;
            row.name = plugin.name;
            row.file = plugin.file;
            row.soundSource = plugin.kind == "Instrument";
            row.category = plugin.subcategory.isNotEmpty() ? plugin.subcategory : plainText("Plug-ins", "Plug-ins");
            row.genre = plugin.format;
            row.role = row.soundSource ? plainText("Instrument", "Instrument") : plainText("Effet audio", "Audio effect");
            row.maker = plugin.vendor.isNotEmpty() ? plugin.vendor : plainText("Inconnu", "Unknown");
            row.description = plugin.file.getFullPathName();
            effects_.push_back(std::move(row));
        }
        const auto place = [](const EffectRow& row) {
            switch (row.kind) {
            case EffectRow::module: return 0;
            case EffectRow::mine: return 1;
            case EffectRow::vst3: return 2;
            case EffectRow::preset: return 3;
            default: return 4;
            }
        };
        std::stable_sort(effects_.begin(), effects_.end(), [&place](const EffectRow& a, const EffectRow& b) {
            if (place(a) != place(b))
                return place(a) < place(b);
            return a.name.compareNatural(b.name) < 0;
        });
        facets.begin();
        for (const auto& row : effects_)
            facets.addRow({ row.category, row.genre, row.role, row.maker },
                          row.name + " " + row.category + " " + row.genre + " " + row.role + " " + row.maker + " " + row.key + " "
                              + row.description);
        facets.end();
        selectedEffect_ = -1;
    }

    ImGui::SetNextItemWidth(-dp(380.0f));
    ImGui::InputTextWithHint("##effectSearch", L("Rechercher un effet, un périphérique, un plug-in…", "Search an effect, a device, a plug-in…"),
                             effectSearch_, sizeof(effectSearch_));
    ImGui::SameLine();
    if (ImGui::Button(L("Atelier de périphériques…", "Device workshop…")))
        state_.bringToFront = Panel::workshop;
    ImGui::SameLine();
    if (ImGui::Button(L("Éditeur JSFX…", "JSFX editor…"))) state_.showJsfx=true;
    ImGui::SameLine();
    if (ImGui::Button(plugins_->scanning() ? L("Recherche des plug-ins…", "Finding plug-ins…") : L("Dossiers de plug-ins…", "Plug-in folders…")))
        ImGui::OpenPopup("##pluginFolders");
    if (ImGui::BeginPopup("##pluginFolders")) {
        ImGui::TextDisabled("%s", L("Les dossiers VST3, CLAP, LV2 et JSFX standard, et ceux-ci :", "The standard VST3, CLAP, LV2 and JSFX folders, and these:"));
        auto folders = pluginFolders;
        folders.removeEmptyStrings();
        auto changed = false;
        for (auto index = 0; index < folders.size(); ++index) {
            ImGui::PushID(index);
            if (ui::iconButton(icon::close, L("Retirer ce dossier", "Remove this folder"), false, 0, ImVec2(dp(20.0f), dp(20.0f)))) {
                folders.remove(index);
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(folders[index].toRawUTF8());
            ImGui::PopID();
        }
        if (ImGui::Selectable(L("Ajouter un dossier…", "Add a folder…"))) {
            chooser_ = std::make_unique<juce::FileChooser>(L("Un dossier de plug-ins VST3", "A VST3 plug-in folder"), juce::File {});
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this](const juce::FileChooser& chooser) {
                                      const auto folder = chooser.getResult();
                                      if (!folder.isDirectory())
                                          return;
                                      auto list = juce::StringArray::fromLines(studio_.samples().setting("pluginFolders", juce::String()).toString());
                                      list.removeEmptyStrings();
                                      list.addIfNotAlreadyThere(folder.getFullPathName());
                                      studio_.samples().setSetting("pluginFolders", list.joinIntoString("\n"));
                                      plugins_->start(list);
                                  });
        }
        if (ImGui::Selectable(L("Chercher à nouveau", "Look again")))
            plugins_->start(folders);
        if (changed) {
            studio_.samples().setSetting("pluginFolders", folders.joinIntoString("\n"));
            plugins_->start(folders);
        }
        ImGui::EndPopup();
    }
    if (plugins_->scanning())
        host_.wake(6);

    std::vector<std::vector<int>> counts;
    auto rows = facets.filter(juce::String::fromUTF8(effectSearch_), counts);
    {
        std::vector<FacetColumn> columns;
        const char* titles[][2] = { { "Catégorie", "Category" }, { "Genre", "Kind" }, { "Rôle", "Role" }, { "Fabricant", "Maker" } };
        for (std::size_t facet = 0; facet < facets.titles.size(); ++facet) {
            FacetColumn column;
            column.title = juce::String::fromUTF8(L(titles[facet][0], titles[facet][1]));
            column.selected = &facets.selected[facet];
            column.weight = facet == 0 ? 1.5f : 1.0f;
            for (const auto id : facets.order[facet]) {
                const auto count = counts[facet][static_cast<std::size_t>(id)];
                if (count == 0 && facets.selected[facet][static_cast<std::size_t>(id)] == 0)
                    continue;
                column.entries.push_back({ id, facets.values[facet][static_cast<std::size_t>(id)], count });
            }
            columns.push_back(std::move(column));
        }
        (void) drawFacetColumns("##effectFacets", columns, dp(170.0f));
    }
    const auto insert = [this, track](const EffectRow& row) {
        if (!track) {
            report(ui::Ls("Choisissez d'abord une piste", "Choose a track first"), true);
            return;
        }
        juce::Result result = juce::Result::ok();
        // Before the card whose "+" opened the window, else where it goes.
        const auto before = state_.deviceInsertBefore && state_.deviceInsertBefore->first == *track ? state_.deviceInsertBefore->second
                                                                                                      : std::uint64_t {};
        if (row.kind == EffectRow::vst3) {
            if (track->midi || track->master)
                result = juce::Result::fail(ui::Ls("Les plug-ins externes vont sur les pistes audio", "External plug-ins go on audio tracks"));
            else
                result = studio_.engine().setTrackPlugin(track->audioIndex, row.file);
        } else if (row.kind == EffectRow::mine) {
            result = studio_.insertLibraryDeviceBefore(*track, row.file, before);
        } else {
            result = studio_.insertDeviceBefore(*track, row.key, before);
        }
        if (result.failed()) {
            report(result.getErrorMessage(), true);
            return;
        }
        studio_.markDirty();
        report(row.name + ui::Ls(" ajouté à ", " added to ") + studio_.trackName(*track));
        picked();
    };
    const auto previewWidth = dp(330.0f);
    const ImVec2 tableSize(ImGui::GetContentRegionAvail().x - previewWidth - dp(8.0f), ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginTable("##libraryEffects", 4,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV
                              | ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable
                              | ImGuiTableFlags_SortTristate,
                          tableSize)) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn(L("Nom", "Name"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(L("Catégorie", "Category"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(L("Genre", "Kind"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(L("Fabricant", "Maker"), ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableHeadersRow();
        // Unsorted, the useful first: CyDAW's devices, mine, plug-ins, chains, nodes.
        if (const auto* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
            const auto column = specs->Specs[0].ColumnIndex;
            const auto ascending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
            const auto field = [this, column](const int index) -> const juce::String& {
                const auto& row = effects_[static_cast<std::size_t>(index)];
                return column == 1 ? row.category : column == 2 ? row.genre : column == 3 ? row.maker : row.name;
            };
            std::stable_sort(rows.begin(), rows.end(), [&](const int a, const int b) {
                const auto order = field(a).compareNatural(field(b));
                return ascending ? order < 0 : order > 0;
            });
        }
        // Enter puts the chosen one there.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput
            && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && selectedEffect_ >= 0 && selectedEffect_ < static_cast<int>(effects_.size()))
            insert(effects_[static_cast<std::size_t>(selectedEffect_)]);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step())
            for (auto line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
                const auto index = rows[static_cast<std::size_t>(line)];
                if (index >= static_cast<int>(effects_.size()))
                    continue;
                const auto& row = effects_[static_cast<std::size_t>(index)];
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(row.name.toRawUTF8(), selectedEffect_ == index,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    selectedEffect_ = index;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        insert(row);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && row.description.isNotEmpty())
                    ui::tooltip(row.description.toRawUTF8());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(theme::vec(p.textDim), "%s", row.category.toRawUTF8());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(theme::vec(p.textFaint), "%s", row.genre.toRawUTF8());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(theme::vec(p.textFaint), "%s", row.maker.toRawUTF8());
                ImGui::PopID();
            }
        ImGui::EndTable();
    }
    ImGui::SameLine(0.0f, dp(8.0f));
    ImGui::BeginChild("##effectPreview", ImVec2(previewWidth, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders);
    if (selectedEffect_ >= 0 && selectedEffect_ < static_cast<int>(effects_.size())) {
        const auto& row = effects_[static_cast<std::size_t>(selectedEffect_)];
        ImGui::PushFont(theme::boldFont(), 0.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(row.name.toRawUTF8());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::TextColored(theme::vec(p.textDim), "%s", (row.category + "  \xc2\xb7  " + row.role).toRawUTF8());
        if (row.description.isNotEmpty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(row.description.toRawUTF8());
            ImGui::PopTextWrapPos();
        }
        if (row.kind == EffectRow::preset)
            ImGui::TextColored(theme::vec(p.accent), "%s", L("Il remplace l'instrument de la piste ; ses effets restent.",
                                                             "It replaces the track's instrument; its effects stay."));
        ImGui::Spacing();
        const auto label = track ? ui::Ls("Ajouter à ", "Add to ") + studio_.trackName(*track) : ui::Ls("Choisissez une piste", "Choose a track");
        ImGui::BeginDisabled(!track);
        if (ImGui::Button(label.toRawUTF8(), ImVec2(-1.0f, 0.0f)))
            insert(row);
        ImGui::EndDisabled();
    } else {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(p.textFaint), "%s",
                           L("Double-clic ou Entrée : l'ajouter à la piste. Les colonnes du haut filtrent (Ctrl, Maj : plusieurs).",
                             "Double click or Enter: add it to the track. The columns above filter (Ctrl, Shift: several)."));
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

// ---- The control port --------------------------------------------------------------------------------

LibraryWindow::Probe LibraryWindow::probe()
{
    requery();
    Probe probe;
    probe.shown = state_.showLibrary;
    probe.tab = state_.libraryTab;
    probe.rows = result_.matched;
    probe.total = data_ != nullptr ? static_cast<int>(data_->items.size()) : 0;
    probe.queryMs = result_.ms;
    probe.buildMs = data_ != nullptr ? data_->buildMs : 0.0;
    probe.extractMs = data_ != nullptr ? data_->extractMs : 0.0;
    probe.frameMs = frameMs_;
    if (data_ != nullptr) {
        for (auto facet = 0; facet < SampleCatalog::facetCount; ++facet) {
            std::vector<std::pair<juce::String, int>> values;
            const auto& counts = result_.counts[static_cast<std::size_t>(facet)];
            for (std::size_t id = 0; id < counts.size(); ++id)
                if (counts[id] > 0)
                    values.emplace_back(data_->values[static_cast<std::size_t>(facet)][id].key, counts[id]);
            static const char* names[] = { "category", "group", "source", "type", "key", "tempo", "length" };
            probe.facets.emplace_back(names[facet], std::move(values));
        }
        for (std::size_t row = 0; row < result_.rows.size() && row < 20; ++row)
            probe.names.add(data_->items[static_cast<std::size_t>(result_.rows[row])].label);
    }
    return probe;
}

} // namespace cydaw::v2
