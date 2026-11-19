#include "AssistantOperations.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>

namespace cydaw::v2 {
namespace {

using cymcp::Json;

const Json& legacyCatalogue()
{
    static const auto catalogue = [] {
        static constexpr std::string_view chunks[] = {
#include "AssistantLegacyCatalogue.inc"
        };
        std::string data;
        for (const auto chunk : chunks)
            data += chunk;
        return cymcp::parse_json(data);
    }();
    return catalogue;
}

Json object(Json properties = Json::object(), Json required = Json::array())
{
    return {{"type", "object"}, {"properties", std::move(properties)},
            {"required", std::move(required)}, {"additionalProperties", false}};
}

Json type(const char* value) { return {{"type", value}}; }

Json number(const char* kind, double from, double to)
{
    return {{"type", kind}, {"minimum", from}, {"maximum", to}};
}

Json choices(std::initializer_list<const char*> values)
{
    return {{"type", "string"}, {"enum", values}};
}

Json arrayOf(Json item) { return {{"type", "array"}, {"items", std::move(item)}}; }

const Json nameField = {{"type", "string"}, {"minLength", 1}, {"maxLength", 120}};
const Json trackIdField = {{"type", "string"}, {"minLength", 1}, {"maxLength", 20}};
const Json trackField = number("integer", 0, 1'000'000);
const Json timeField = number("number", 0, 86400);
const Json pageProperties = {{"offset", trackField}, {"limit", number("integer", 1, 50)}};

Json pageOf(const char* field, Json item)
{
    return object({{field, arrayOf(std::move(item))}, {"total", type("integer")},
                   {"nextOffset", {{"type", {"integer", "null"}}}}}, {field, "total", "nextOffset"});
}

Json factoryEffects(const Json& args)
{
    auto items = Json::array();
    const auto offset = args.value("offset", std::size_t{});
    const auto limit = args.value("limit", std::size_t{20});
    const auto query = args.value("typeKeyContains", std::string{});
    std::size_t total = 0;
    for (const auto& effect : engine::effectCatalogue) {
        if (effect.typeKey.find(query) == std::string_view::npos)
            continue;
        if (total++ < offset || items.size() == limit)
            continue;
        items.push_back({{"key", effect.typeKey}, {"name", effect.displayName}, {"category", effect.categoryKey},
                         {"midi", effect.midi}, {"modulator", effect.control}});
    }
    const auto end = offset + items.size();
    return {{"effects", std::move(items)}, {"total", total}, {"nextOffset", end < total ? Json(end) : Json(nullptr)}};
}

} // namespace

bool assistantReadOnly(const std::string& tool, const Json& arguments)
{
    if (!arguments.is_object())
        return false;
    if (tool == "list_factory_effects" || tool == "session_status" || tool == "list_tracks")
        return true;
    const auto& catalogue = legacyCatalogue();
    const auto spec = catalogue.find(tool);
    if (spec == catalogue.end())
        return false;
    if (spec->at("readOnly").get<bool>())
        return true;
    const auto field = arguments.find("action");
    if (field != arguments.end() && !field->is_string())
        return false;
    const auto& action = field == arguments.end() ? spec->at("defaultAction") : *field;
    if (tool == "workshop" && action == "programs" && arguments.contains("op") && arguments.at("op") != "list")
        return false;
    const auto& reads = spec->at("reads");
    const auto allowed = reads.find(action.get_ref<const std::string&>());
    if (allowed == reads.end())
        return false;
    for (auto entry = arguments.begin(); entry != arguments.end(); ++entry)
        if (entry.key() != "action" && !(entry.key() == "show" && entry.value() == false)
            && std::find(allowed->begin(), allowed->end(), Json(entry.key())) == allowed->end())
            return false;
    return true;
}

std::shared_ptr<cymcp::Registry> makeAssistantRegistry(AssistantDispatch dispatch,
                                                     std::shared_ptr<std::atomic_bool> allowEdits)
{
    auto registry = std::make_shared<cymcp::Registry>(cymcp::Policy(
        [allowEdits](const cymcp::CallContext&, const std::string& name, const Json& arguments) {
            return assistantReadOnly(name, arguments) || (allowEdits && allowEdits->load())
                ? cymcp::Decision::allow : cymcp::Decision::deny;
        }));
    const auto add = [&](std::string name, const char* description, Json input, Json output,
                         bool readOnly = true, bool offline = false) {
        registry->add({name, description, std::move(input), std::move(output), readOnly,
                       [dispatch, allowEdits, name, offline](const Json& arguments, const cymcp::CallContext& context) {
                           context.cancellation.check();
                           if (offline)
                               return factoryEffects(arguments);
                           if (!dispatch)
                               throw cymcp::Error("InstanceRequired", "Connect to a running CyDaw session");
                           if (!assistantReadOnly(name, arguments) && (!allowEdits || !allowEdits->load()))
                               throw cymcp::Error("PermissionDenied", "Session edits are disabled in CyDaw");
                           // The dispatcher also checks this permission on the message thread.
                           return dispatch(name, arguments, context);
                       }});
    };

    auto effects = pageProperties;
    effects["typeKeyContains"] = {{"type", "string"}, {"maxLength", 80}};
    add("list_factory_effects", "Browse the native effect catalogue before choosing an effect. Filter by the stable type key or page with nextOffset.",
        object(effects), pageOf("effects", object({{"key", type("string")}, {"name", type("string")},
            {"category", type("string")}, {"midi", type("boolean")}, {"modulator", type("boolean")}},
            {"key", "name", "category", "midi", "modulator"})), true, true);
    add("session_status", "Read the active project name, transport, tempo, loop and unsaved state. Use list_tracks to browse its tracks.",
        object(), object({{"project", type("string")}, {"dirty", type("boolean")}, {"opening", type("boolean")},
            {"tempo", type("number")}, {"playing", type("boolean")}, {"position", type("number")},
            {"tracks", type("integer")}, {"loopEnabled", type("boolean")}},
            {"project", "dirty", "opening", "tempo", "playing", "position", "tracks", "loopEnabled"}));
    add("list_tracks", "List arrangement tracks in their current order and read their mixer settings. Use the returned trackId with editing tools.",
        object(pageProperties), pageOf("tracks", object({{"index", type("integer")}, {"trackId", trackIdField}, {"name", type("string")},
            {"kind", choices({"audio", "midi", "bus"})}, {"gainDb", type("number")}, {"pan", type("number")},
            {"muted", type("boolean")}, {"solo", type("boolean")}},
            {"index", "trackId", "name", "kind", "gainDb", "pan", "muted", "solo"})));
    add("set_tempo", "Set the session tempo and optionally its meter. This changes the unsaved project.",
        object({{"bpm", number("number", 20, 300)}, {"numerator", number("integer", 1, 32)},
                {"denominator", {{"type", "integer"}, {"enum", {1, 2, 4, 8, 16, 32}}}}}, {"bpm"}),
        object({{"tempo", type("number")}, {"numerator", type("integer")}, {"denominator", type("integer")}},
               {"tempo", "numerator", "denominator"}), false);
    add("transport", "Play, stop, rewind or seek in the active session. Position is in seconds. Transport control is unavailable during recording.",
        object({{"action", choices({"play", "stop", "rewind", "seek"})}, {"position", timeField}}, {"action"}),
        object({{"playing", type("boolean")}, {"position", type("number")}}, {"playing", "position"}), false);
    add("set_loop", "Set the loop range in seconds and enable or disable looping in the active session.",
        object({{"start", timeField}, {"end", timeField}, {"enabled", type("boolean")}}, {"start", "end", "enabled"}),
        object({{"start", type("number")}, {"end", type("number")}, {"enabled", type("boolean")}},
               {"start", "end", "enabled"}), false);
    add("add_midi_track", "Add an empty MIDI track or a track with the specified factory instrument. Changes stay in the open project until the user saves.",
        object({{"name", nameField}, {"instrument", choices({"prism-instrument", "ambre-instrument", "kick-instrument"})}}, {"name"}),
        object({{"trackId", trackIdField}, {"name", type("string")}}, {"trackId", "name"}), false);
    add("add_bus_track", "Create a named bus in the open project for effects and routing.", object({{"name", nameField}}, {"name"}),
        object({{"trackId", trackIdField}, {"name", type("string")}}, {"trackId", "name"}), false);
    add("set_track_mix", "Set volume, pan, mute and solo for a track by its trackId from list_tracks. Only the supplied settings change.",
        object({{"trackId", trackIdField}, {"gainDb", number("number", -60, 12)}, {"pan", number("number", -1, 1)},
                {"muted", type("boolean")}, {"solo", type("boolean")}}, {"trackId"}),
        object({{"gainDb", type("number")}, {"pan", type("number")}, {"muted", type("boolean")}, {"solo", type("boolean")}},
               {"gainDb", "pan", "muted", "solo"}), false);
    add("add_midi_clip", "Create an empty MIDI clip on an existing MIDI track in the arrangement. Coordinates are in beats.",
        object({{"trackId", trackIdField}, {"name", nameField}, {"startBeats", number("number", 0, 100000)},
                {"lengthBeats", number("number", 0.25, 10000)}}, {"trackId", "name", "startBeats", "lengthBeats"}),
        object({{"clipId", type("string")}}, {"clipId"}), false);
    const auto undoInput = object({{"scope", choices({"arrangement", "midi", "graph"})}}, {"scope"});
    const auto undoOutput = object({{"done", type("boolean")}}, {"done"});
    add("undo", "Undo the last action in the selected CyDaw edit history, which may include user edits.", undoInput, undoOutput, false);
    add("redo", "Redo the last undone action in the selected CyDaw edit history.", undoInput, undoOutput, false);
    for (const auto& [name, spec] : legacyCatalogue().items()) {
        const auto& description = spec.at("description").get_ref<const std::string&>();
        add(name, description.c_str(), spec.at("input"), spec.at("output"), spec.at("readOnly").get<bool>());
    }
    return registry;
}

} // namespace cydaw::v2
