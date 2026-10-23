// The EEL2 headers include windows.h: its min/max macros kept out.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include "ProcessingCode.h"

#include "Locations.h"
#include "PluginFiles.h"

#include <ysfx.h>

#include "WDL/eel2/ns-eel.h"
#include "WDL/eel2/ns-eel-addfuncs.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <numbers>

namespace cydaw::v2::processing {
namespace {

// ---- Note scripts ----

// "param1:20<0,100,1>Timing (ms)": its index, default, range and name.
std::optional<Setting> parseParam(const juce::String& line)
{
    const auto text = line.trim();
    if (!text.startsWithIgnoreCase("param"))
        return std::nullopt;
    const auto colon = text.indexOfChar(':');
    const auto open = text.indexOfChar('<');
    const auto close = text.indexOfChar('>');
    if (colon < 0 || open < colon || close < open)
        return std::nullopt;
    const auto number = text.substring(5, colon).trim().getIntValue();
    if (number < 1 || number > 16)
        return std::nullopt;
    Setting setting;
    setting.index = number - 1;
    setting.defaultValue = text.substring(colon + 1, open).trim().getDoubleValue();
    const auto range = juce::StringArray::fromTokens(text.substring(open + 1, close), ",", "");
    setting.minimum = range.size() > 0 ? range[0].trim().getDoubleValue() : 0.0;
    setting.maximum = range.size() > 1 ? range[1].trim().getDoubleValue() : 1.0;
    setting.step = range.size() > 2 ? range[2].trim().getDoubleValue() : 0.0;
    if (setting.maximum < setting.minimum)
        std::swap(setting.minimum, setting.maximum);
    setting.name = text.substring(close + 1).trim();
    if (setting.name.isEmpty())
        setting.name = "param" + juce::String(number);
    return setting;
}

struct Sections {
    juce::String header, init, note, end;
    int initLine {}, noteLine {}, endLine {};
};

Sections split(const juce::String& source)
{
    Sections sections;
    juce::String* current = &sections.header;
    juce::String ignored;
    auto lineNumber = 0;
    for (const auto& line : juce::StringArray::fromLines(source)) {
        const auto trimmed = line.trim();
        if (trimmed.startsWith("@")) {
            const auto name = trimmed.substring(1).upToFirstOccurrenceOf(" ", false, false).toLowerCase();
            current = name == "init" ? &sections.init : name == "note" ? &sections.note : name == "end" ? &sections.end : nullptr;
            if (current == &sections.init)
                sections.initLine = lineNumber + 1;
            else if (current == &sections.note)
                sections.noteLine = lineNumber + 1;
            else if (current == &sections.end)
                sections.endLine = lineNumber + 1;
            if (current == nullptr) {
                ignored.clear();
                current = &ignored;
            }
        } else {
            *current << line << "\n";
        }
        ++lineNumber;
    }
    return sections;
}

struct Runner {
    std::vector<ScriptNote> original;
    std::vector<ScriptNote> added;
    std::uint64_t state { 0x9e3779b97f4a7c15ull };
    double tempo { 120.0 };
    double nextUniform()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<double>(state >> 11) * (1.0 / 9007199254740992.0);
    }
};

Runner* runnerOf(void* opaque) { return static_cast<Runner*>(opaque); }

EEL_F NSEEL_CGEN_CALL eelRandom(void* opaque, INT_PTR, EEL_F**) { return runnerOf(opaque)->nextUniform(); }

EEL_F NSEEL_CGEN_CALL eelGauss(void* opaque, INT_PTR, EEL_F**)
{
    auto* runner = runnerOf(opaque);
    const auto u = std::max(1.0e-12, runner->nextUniform());
    const auto v = runner->nextUniform();
    return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * std::numbers::pi * v);
}

EEL_F NSEEL_CGEN_CALL eelMsToBeats(void* opaque, INT_PTR count, EEL_F** parms)
{
    return count > 0 ? *parms[0] / 1'000.0 * runnerOf(opaque)->tempo / 60.0 : 0.0;
}

const ScriptNote* noteAt(void* opaque, INT_PTR count, EEL_F** parms)
{
    auto* runner = runnerOf(opaque);
    if (count < 1)
        return nullptr;
    const auto index = static_cast<long long>(std::floor(*parms[0] + 1.0e-6));
    return index >= 0 && index < static_cast<long long>(runner->original.size()) ? &runner->original[static_cast<std::size_t>(index)]
                                                                                   : nullptr;
}

EEL_F NSEEL_CGEN_CALL eelNoteStart(void* o, INT_PTR c, EEL_F** p) { const auto* n = noteAt(o, c, p); return n ? n->start : -1.0; }
EEL_F NSEEL_CGEN_CALL eelNotePitch(void* o, INT_PTR c, EEL_F** p) { const auto* n = noteAt(o, c, p); return n ? n->pitch : -1.0; }
EEL_F NSEEL_CGEN_CALL eelNoteLength(void* o, INT_PTR c, EEL_F** p) { const auto* n = noteAt(o, c, p); return n ? n->length : -1.0; }
EEL_F NSEEL_CGEN_CALL eelNoteVelocity(void* o, INT_PTR c, EEL_F** p) { const auto* n = noteAt(o, c, p); return n ? n->velocity : -1.0; }

EEL_F NSEEL_CGEN_CALL eelAddNote(void* opaque, INT_PTR count, EEL_F** parms)
{
    auto* runner = runnerOf(opaque);
    if (count < 4 || runner->added.size() >= 100'000)
        return 0.0;
    ScriptNote note;
    note.pitch = static_cast<int>(std::lround(*parms[0]));
    note.start = *parms[1];
    note.length = *parms[2];
    note.velocity = static_cast<float>(*parms[3]);
    runner->added.push_back(note);
    return 1.0;
}

// The note scripts' own functions (and none of the JSFX host's: a script
// calling those would crash).
// EEL2 keeps global state (its list of global variables, counted by the
// machines that use it) that ysfx does not lock: machines are made, compiled
// and freed one at a time, here and in the JSFX renders. They run unlocked.
std::recursive_mutex& eelLock()
{
    static std::recursive_mutex lock;
    return lock;
}

eel_function_table& functions()
{
    static eel_function_table table {};
    static std::once_flag once;
    std::call_once(once, [] {
        NSEEL_addfunc_varparm_ex("random", 0, 0, NSEEL_PProc_THIS, &eelRandom, &table);
        NSEEL_addfunc_varparm_ex("gauss", 0, 0, NSEEL_PProc_THIS, &eelGauss, &table);
        NSEEL_addfunc_varparm_ex("ms_to_beats", 1, 1, NSEEL_PProc_THIS, &eelMsToBeats, &table);
        NSEEL_addfunc_varparm_ex("note_start", 1, 1, NSEEL_PProc_THIS, &eelNoteStart, &table);
        NSEEL_addfunc_varparm_ex("note_pitch", 1, 1, NSEEL_PProc_THIS, &eelNotePitch, &table);
        NSEEL_addfunc_varparm_ex("note_length", 1, 1, NSEEL_PProc_THIS, &eelNoteLength, &table);
        NSEEL_addfunc_varparm_ex("note_velocity", 1, 1, NSEEL_PProc_THIS, &eelNoteVelocity, &table);
        NSEEL_addfunc_varparm_ex("add_note", 4, 1, NSEEL_PProc_THIS, &eelAddNote, &table);
    });
    return table;
}

struct Machine {
    NSEEL_VMCTX vm {};
    std::vector<NSEEL_CODEHANDLE> code;
    Machine()
    {
        const std::scoped_lock lock(eelLock());
        vm = NSEEL_VM_alloc();
        NSEEL_VM_SetFunctionTable(vm, &functions());
    }
    ~Machine()
    {
        const std::scoped_lock lock(eelLock());
        for (auto* handle : code)
            if (handle != nullptr)
                NSEEL_code_free(handle);
        NSEEL_VM_freeRAM(vm);
        NSEEL_VM_free(vm);
    }
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    // Null and `error` when it does not compile (an empty part: no code).
    NSEEL_CODEHANDLE compile(const juce::String& text, const int line, const juce::String& section, juce::String& error)
    {
        if (text.trim().isEmpty())
            return nullptr;
        const std::scoped_lock lock(eelLock());
        auto* handle = NSEEL_code_compile_ex(vm, text.toRawUTF8(), line, 0);
        if (handle == nullptr) {
            const auto* message = NSEEL_code_getcodeerror(vm);
            error = "@" + section + ": " + (message != nullptr ? juce::String::fromUTF8(message) : juce::String("does not compile"));
        }
        code.push_back(handle);
        return handle;
    }
};

const char* const variableNames[] = { "pitch", "start", "length", "velocity", "chance", "index", "keep", "count", "tempo",
                                      "clip_length", "prev_start", "next_start", "prev_pitch", "next_pitch" };
enum Variable { pitchVar, startVar, lengthVar, velocityVar, chanceVar, indexVar, keepVar, countVar, tempoVar, clipLengthVar,
                prevStartVar, nextStartVar, prevPitchVar, nextPitchVar, variableCount };

} // namespace

std::vector<Setting> noteScriptSettings(const juce::String& source)
{
    std::vector<Setting> settings;
    for (const auto& line : juce::StringArray::fromLines(split(source).header))
        if (auto setting = parseParam(line))
            if (std::none_of(settings.begin(), settings.end(), [&](const Setting& other) { return other.index == setting->index; }))
                settings.push_back(*setting);
    std::sort(settings.begin(), settings.end(), [](const Setting& a, const Setting& b) { return a.index < b.index; });
    return settings;
}

juce::String checkNoteScript(const juce::String& source)
{
    const auto sections = split(source);
    if (sections.note.trim().isEmpty() && sections.init.trim().isEmpty() && sections.end.trim().isEmpty())
        return "no @init, @note or @end section";
    Machine machine;
    for (const auto* name : variableNames)
        (void) NSEEL_VM_regvar(machine.vm, name);
    juce::String error;
    (void) machine.compile(sections.init, sections.initLine, "init", error);
    if (error.isEmpty())
        (void) machine.compile(sections.note, sections.noteLine, "note", error);
    if (error.isEmpty())
        (void) machine.compile(sections.end, sections.endLine, "end", error);
    return error;
}

NoteScriptResult runNoteScript(const juce::String& source, std::vector<ScriptNote> notes, const std::map<int, double>& values,
                               const double tempo, const double clipLength, const std::uint32_t seed)
{
    NoteScriptResult result;
    const auto sections = split(source);
    std::stable_sort(notes.begin(), notes.end(), [](const ScriptNote& a, const ScriptNote& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (notes[i].source < 0)
            notes[i].source = static_cast<int>(i);
    Runner runner;
    runner.original = notes;
    runner.tempo = tempo > 0.0 ? tempo : 120.0;
    runner.state ^= static_cast<std::uint64_t>(seed) * 0xbf58476d1ce4e5b9ull + 1;
    for (int warm = 0; warm < 8; ++warm)
        (void) runner.nextUniform();
    Machine machine;
    NSEEL_VM_SetCustomFuncThis(machine.vm, &runner);
    EEL_F* variables[variableCount] {};
    for (int v = 0; v < variableCount; ++v)
        variables[v] = NSEEL_VM_regvar(machine.vm, variableNames[v]);
    EEL_F* params[16] {};
    for (int p = 0; p < 16; ++p) {
        params[p] = NSEEL_VM_regvar(machine.vm, ("param" + std::to_string(p + 1)).c_str());
        if (params[p] != nullptr)
            *params[p] = 0.0;
    }
    for (const auto& setting : noteScriptSettings(source))
        if (params[setting.index] != nullptr)
            *params[setting.index] = setting.defaultValue;
    for (const auto& [index, value] : values)
        if (index >= 0 && index < 16 && params[index] != nullptr)
            *params[index] = value;
    *variables[countVar] = static_cast<double>(notes.size());
    *variables[tempoVar] = runner.tempo;
    *variables[clipLengthVar] = clipLength;
    auto* init = machine.compile(sections.init, sections.initLine, "init", result.error);
    auto* perNote = result.error.isEmpty() ? machine.compile(sections.note, sections.noteLine, "note", result.error) : nullptr;
    auto* end = result.error.isEmpty() ? machine.compile(sections.end, sections.endLine, "end", result.error) : nullptr;
    if (result.error.isNotEmpty())
        return result;
    if (init != nullptr)
        NSEEL_code_execute(init);
    std::vector<ScriptNote> kept;
    kept.reserve(notes.size());
    for (std::size_t i = 0; i < notes.size(); ++i) {
        auto note = notes[i];
        // The starts (and pitches) around this one, other than its own.
        auto previous = -1.0;
        auto previousPitch = -1.0;
        for (auto j = static_cast<long long>(i) - 1; j >= 0; --j)
            if (notes[static_cast<std::size_t>(j)].start < note.start) {
                previous = notes[static_cast<std::size_t>(j)].start;
                previousPitch = notes[static_cast<std::size_t>(j)].pitch;
                break;
            }
        auto next = -1.0;
        auto nextPitch = -1.0;
        for (auto j = i + 1; j < notes.size(); ++j)
            if (notes[j].start > note.start) {
                next = notes[j].start;
                nextPitch = notes[j].pitch;
                break;
            }
        *variables[pitchVar] = note.pitch;
        *variables[startVar] = note.start;
        *variables[lengthVar] = note.length;
        *variables[velocityVar] = note.velocity;
        *variables[chanceVar] = note.chance;
        *variables[indexVar] = static_cast<double>(i);
        *variables[keepVar] = 1.0;
        *variables[prevStartVar] = previous;
        *variables[nextStartVar] = next;
        *variables[prevPitchVar] = previousPitch;
        *variables[nextPitchVar] = nextPitch;
        if (perNote != nullptr)
            NSEEL_code_execute(perNote);
        if (*variables[keepVar] < 0.5)
            continue;
        note.pitch = static_cast<int>(std::lround(std::clamp(*variables[pitchVar], 0.0, 127.0)));
        note.start = *variables[startVar];
        note.length = *variables[lengthVar];
        note.velocity = static_cast<float>(*variables[velocityVar]);
        note.chance = static_cast<float>(*variables[chanceVar]);
        kept.push_back(note);
    }
    if (end != nullptr)
        NSEEL_code_execute(end);
    kept.insert(kept.end(), runner.added.begin(), runner.added.end());
    for (auto& note : kept) {
        note.pitch = std::clamp(note.pitch, 0, 127);
        note.start = std::max(0.0, std::isfinite(note.start) ? note.start : 0.0);
        note.length = std::clamp(std::isfinite(note.length) ? note.length : 0.25, 1.0 / 64.0, 4'096.0);
        note.velocity = std::clamp(std::isfinite(note.velocity) ? note.velocity : 0.8f, 0.01f, 1.0f);
        note.chance = std::clamp(std::isfinite(note.chance) ? note.chance : 1.0f, 0.0f, 1.0f);
    }
    result.ok = true;
    result.notes = std::move(kept);
    return result;
}

// ---- JSFX ----

namespace {

// The source in a file of its own (ysfx reads files), in the cache.
juce::File scriptFile(const juce::String& source)
{
    const auto text = source.replace("\r\n", "\n");
    const auto folder = locations::folder(Location::cache).getChildFile("processing");
    const auto file = folder.getChildFile(juce::String::toHexString(text.hashCode64()) + ".jsfx");
    static std::mutex lock;
    static std::set<juce::String> written;
    const std::scoped_lock held(lock);
    if (written.count(file.getFullPathName()) > 0 && file.existsAsFile())
        return file;
    (void) folder.createDirectory();
    if (!file.existsAsFile() || file.loadFileAsString() != text)
        (void) file.replaceWithText(text, false, false, "\n");
    written.insert(file.getFullPathName());
    return file;
}

struct Loaded {
    ysfx_config_u config { ysfx_config_new() };
    ysfx_u fx;
    juce::String log;
    Loaded() = default;
    Loaded(const Loaded&) = delete;
    Loaded& operator=(const Loaded&) = delete;
    ~Loaded()
    {
        const std::scoped_lock lock(eelLock());
        fx.reset();
        config.reset();
    }
    bool load(const juce::String& source, juce::String& error)
    {
        const std::scoped_lock lock(eelLock());
        ysfx_set_user_data(config.get(), reinterpret_cast<intptr_t>(this));
        ysfx_set_log_reporter(config.get(), [](intptr_t data, ysfx_log_level level, const char* message) {
            auto* self = reinterpret_cast<Loaded*>(data);
            if (self->log.length() < 16'384)
                self->log << ysfx_log_level_string(level) << ": " << juce::String::fromUTF8(message) << "\n";
        });
        const auto file = scriptFile(source);
        ysfx_guess_file_roots(config.get(), file.getFullPathName().toRawUTF8());
        ysfx_set_import_root(config.get(), plugin_files::scripts().getFullPathName().toRawUTF8());
        ysfx_register_builtin_audio_formats(config.get());
        fx.reset(ysfx_new(config.get()));
        if (!fx || !ysfx_load_file(fx.get(), file.getFullPathName().toRawUTF8(), 0) || !ysfx_compile(fx.get(), 0)) {
            error = log.isNotEmpty() ? log.trim() : juce::String("the JSFX does not compile");
            return false;
        }
        return true;
    }
};

} // namespace

JsfxInfo inspectJsfx(const juce::String& source)
{
    static std::mutex lock;
    static std::map<juce::String, JsfxInfo> seen;
    {
        const std::scoped_lock held(lock);
        if (const auto found = seen.find(source); found != seen.end())
            return found->second;
    }
    JsfxInfo info;
    const auto remember = [&] {
        const std::scoped_lock held(lock);
        if (seen.size() > 64)
            seen.clear();
        seen[source] = info;
    };
    Loaded loaded;
    if (!loaded.load(source, info.error)) {
        remember();
        return info;
    }
    info.ok = true;
    info.name = juce::String::fromUTF8(ysfx_get_name(loaded.fx.get()));
    for (int i = 0; i < ysfx_max_sliders; ++i)
        if (ysfx_slider_exists(loaded.fx.get(), static_cast<uint32_t>(i))) {
            ysfx_slider_range_t range {};
            ysfx_slider_get_range(loaded.fx.get(), static_cast<uint32_t>(i), &range);
            Setting setting;
            setting.index = i;
            setting.name = juce::String::fromUTF8(ysfx_slider_get_name(loaded.fx.get(), static_cast<uint32_t>(i)));
            setting.minimum = range.min;
            setting.maximum = range.max;
            setting.step = range.inc;
            setting.defaultValue = range.def;
            if (ysfx_slider_is_enum(loaded.fx.get(), static_cast<uint32_t>(i)))
                for (uint32_t j = 0; j < ysfx_slider_get_enum_size(loaded.fx.get(), static_cast<uint32_t>(i)); ++j)
                    setting.choices.add(juce::String::fromUTF8(ysfx_slider_get_enum_name(loaded.fx.get(), static_cast<uint32_t>(i), j)));
            info.sliders.push_back(setting);
        }
    remember();
    return info;
}

juce::Result renderJsfx(const juce::String& source, const std::map<int, double>& values, engine::repair::Channels& channels,
                        const double rate, const std::int64_t frames, const std::atomic<bool>* cancel)
{
    Loaded loaded;
    juce::String error;
    if (!loaded.load(source, error))
        return juce::Result::fail(error);
    auto* fx = loaded.fx.get();
    constexpr uint32_t block = 512;
    ysfx_set_sample_rate(fx, rate);
    ysfx_set_block_size(fx, block);
    ysfx_set_midi_capacity(fx, 1'024, false);
    ysfx_init(fx);
    for (const auto& [index, value] : values)
        if (ysfx_slider_exists(fx, static_cast<uint32_t>(index)))
            ysfx_slider_set_value(fx, static_cast<uint32_t>(index), value);
    const auto inputs = std::clamp<uint32_t>(ysfx_get_num_inputs(fx), 0, 2);
    const auto outputs = std::clamp<uint32_t>(ysfx_get_num_outputs(fx), 1, 2);
    const auto length = static_cast<std::int64_t>(channels.empty() ? 0 : channels.front().size());
    std::vector<float> in[2] = { std::vector<float>(block), std::vector<float>(block) };
    std::vector<float> out[2] = { std::vector<float>(block), std::vector<float>(block) };
    const float* inPointers[2] = { in[0].data(), in[1].data() };
    float* outPointers[2] = { out[0].data(), out[1].data() };
    engine::repair::Channels rendered(2, std::vector<float>(static_cast<std::size_t>(frames), 0.0f));
    std::int64_t latency = -1;
    for (std::int64_t at = 0;; at += block) {
        if (cancel != nullptr && cancel->load())
            return juce::Result::fail("cancelled");
        if (latency < 0)
            latency = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::lround(ysfx_get_pdc_delay(fx))));
        if (at >= frames + latency)
            break;
        for (uint32_t c = 0; c < 2; ++c)
            for (uint32_t i = 0; i < block; ++i) {
                const auto frame = at + i;
                const auto& source = channels[std::min<std::size_t>(c, channels.size() - 1)];
                in[c][i] = frame < length ? source[static_cast<std::size_t>(frame)] : 0.0f;
            }
        ysfx_process_float(fx, inPointers, outPointers, std::max<uint32_t>(inputs, 1), outputs, block);
        for (uint32_t c = 0; c < 2; ++c)
            for (uint32_t i = 0; i < block; ++i) {
                const auto frame = at + i - latency;
                if (frame >= 0 && frame < frames)
                    rendered[c][static_cast<std::size_t>(frame)] = out[std::min(c, outputs - 1)][i];
            }
    }
    channels = std::move(rendered);
    return juce::Result::ok();
}

} // namespace cydaw::v2::processing
