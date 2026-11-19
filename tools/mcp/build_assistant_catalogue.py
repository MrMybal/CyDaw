"""Embed selected legacy MCP schemas in the assistant and console client."""
import argparse
import json
from pathlib import Path
import runpy


READ = ("get_track_graph", "describe_node", "describe_node_type", "list_devices",
        "list_parameters", "list_my_devices", "voice_live", "instrument_choices")
EDIT = ("add_node", "connect", "disconnect", "set_node", "set_node_parameter",
        "expose_parameter", "add_comment", "remove_comment", "insert_device",
        "remove_device", "move_device", "create_device", "insert_my_device",
        "modulate", "remove_modulation", "device_window", "value_list", "sidechain", "cab_impulse")

FOUNDRY = ("status open new close import add_track remove_track track clip select cursor edit process "
           "undo redo save save_session export play stop marker stats send_to_studio midi_clip notes "
           "automation reverse_clip").split()
ACTIONS = {
    "dj": ("status load eject play cue seek tempo bend sync keylock master hotcue loop loop_move "
           "loop_resize beatjump grid channel mixer record folders analyse tracks").split(),
    "foundry": FOUNDRY,
    "cinema": FOUNDRY + "video video_audio group add_subtrack assign view container processing".split(),
    "workshop": ("status new open open_track link unlink channels save close enter leave info "
                 "vst3_check vst3_export clap_export lv2_export plugin_export plugin_export_batch device_export "
                 "vst3_status vst3_cancel tab close_tab simulate note programs runtime_state presentation "
                 "instrument_interface port_exposure promote_value remove_boundary").split(),
    "container": "insert add_device move add_branch remove_branch remove rename show instrument list".split(),
    "modulator": "add set modulate unmodulate remove arm column list".split(),
    "rack": "list add instrument mix sample remove open close".split(),
}

# Optional parameters on read actions are listed explicitly. For example, a
# legacy status call can also switch workspace (show) or select a tab.
READ_ARGS = {
    "app_mode": {"": []},
    "dj": {"status": [], "tracks": ["query", "offset", "limit"]},
    "foundry": {"status": [], "stats": []},
    "cinema": {"status": [], "stats": []},
    "workshop": {"status": [], "vst3_check": [], "vst3_status": ["logLines"],
                 "runtime_state": [], "presentation": [], "programs": ["op", "runtime"]},
    "container": {"list": ["track"]},
    "modulator": {"list": ["track", "nodeId"]},
    "rack": {"list": ["track"]},
    "routing": {"": ["track"]},
    "track_modulation": {"": ["track"]},
    "presets": {"list": ["track", "nodeId", "plugin", "pad"]},
}
DEFAULT_ACTION = {"dj": "status", "foundry": "status", "cinema": "status",
                  "workshop": "status", "rack": "list"}


def fields(kind, names):
    return {name: {"type": kind} for name in names.split()}


def extend(name, props):
    if name in ("foundry", "cinema"):
        props.update(fields("number", "pan fadeIn fadeOut length offset"))
        props.update(fields("boolean", "delete mute solo"))
        props.update(fields("string", "type binding key"))
        props.update(fields("integer", "inputId"))
        props.setdefault("notes", {"type": "array", "items": {"type": "object"}})
        props.setdefault("lanes", {"type": "array", "items": {"type": "object"}})
        props.setdefault("value", {"type": "number"})
        props["track"] = {"type": ["integer", "string"], "description":
                          "Local track index in this session. Some import operations use -1 to create a track."}
    if name == "cinema":
        props.update(fields("number", "zoom"))
        props.update(fields("boolean", "filmstrip fit resetLayout"))
        props["seconds"] = {"type": ["boolean", "number"], "description":
                            "True for time values in seconds, or the visible duration for action view."}
        # Cinema's processing action hosts an existing effects rack. Expose
        # the fields for its routing and device controls, not arbitrary code.
        props.update(fields("string", "select testFile"))
        props.update(fields("boolean", "reload"))
        props["nodeId"] = {"type": "integer"}
    if name == "dj":
        props.update(fields("number", "trimDb high mid low filter resonance fader curve masterDb cueMix"))
        props.update(fields("boolean", "killHigh killMid killLow cue split quantize autoGain limiter scan"))
        props["side"] = {"type": ["string", "integer"]}
        props["decks"] = {"type": "integer", "enum": [2, 4]}
        props["effect"] = {"type": "object", "properties": {
            **fields("string", "kind"), **fields("boolean", "on"),
            **fields("number", "mix first second")}, "additionalProperties": False}
    if name in ("dj", "list_devices", "list_my_devices", "instrument_choices"):
        props.update({"offset": {"type": "integer", "minimum": 0, "maximum": 1000000},
                      "limit": {"type": "integer", "minimum": 1, "maximum": 200}})
    if name == "workshop":
        props["logLines"] = {"type": "integer", "minimum": 1, "maximum": 400}
        props.update(fields("string", "op newName type binding"))
        props.update(fields("boolean", "runtime defaults compact exposed"))
        props["panel"]["type"] = ["boolean", "string"]
        props["portId"] = {"type": "integer"}
        props["mode"] = {"type": "string", "enum": ["input", "variable"]}
        props.pop("sourceRoot", None)


def output_schema(name):
    props = {}
    if name in ("cinema", "foundry"):
        props = {**fields("integer", "current"), **fields("number", "position"),
                 **fields("boolean", "playing"), "tabs": {"type": "array", "items": {"type": "object"}}}
    elif name in ("dj", "workshop", "get_track_graph"):
        array = {"dj": "decks", "workshop": "tabs", "get_track_graph": "nodes"}[name]
        props[array] = {"type": "array", "items": {"type": "object"}}
    return {"type": "object", "properties": props}


def catalogue(legacy):
    selected = dict.fromkeys((*READ, *EDIT, *READ_ARGS))
    legacy = {entry["name"]: entry for entry in legacy}
    result = {}
    for name in selected:
        tool = legacy[name]
        schema = tool["inputSchema"]
        props = schema.setdefault("properties", {})
        if name == "cinema":
            # Cinema delegates editing, marker and processing actions to Foundry.
            for key, value in legacy["foundry"]["inputSchema"]["properties"].items():
                props.setdefault(key, value)
        extend(name, props)
        if name in ACTIONS:
            props["action"]["enum"] = ACTIONS[name]
        # Keep documented legacy parameters and fail closed on unregistered fields.
        schema["additionalProperties"] = False
        if name not in ("foundry", "cinema") and "track" in props and isinstance(props["track"].get("type"), list):
            description = props["track"].get("description", "")
            props["track"] = dict(props["track"], description=description +
                                  ' Use "track-id:<trackId>" for the stable identifier from list_tracks.')
        if "graph" in props:
            props["graph"]["enum"] = ["modulation"]
        result[name] = {
            "description": tool["description"], "input": schema, "output": output_schema(name),
            "readOnly": name in READ, "reads": READ_ARGS.get(name, {}),
            "defaultAction": DEFAULT_ACTION.get(name, ""),
        }
        if name == "workshop":
            result[name]["description"] += (
                " Programs: action programs with op list, add, load, remove or rename. "
                "Presentation reads the interface; instrument_interface adds native controls. "
                "Use port_exposure, promote_value or remove_boundary for public graph ports.")
    return result


def main():
    args = argparse.ArgumentParser()
    args.add_argument("--legacy", type=Path, required=True)
    args.add_argument("--output", type=Path, required=True)
    options = args.parse_args()
    data = json.dumps(catalogue(runpy.run_path(str(options.legacy))["TOOLS"]),
                      ensure_ascii=True, separators=(",", ":"))
    # The only closing token of this delimiter cannot occur in the JSON.
    assert ')CYTOOLS"' not in data
    options.output.parent.mkdir(parents=True, exist_ok=True)
    chunks = (data[i:i + 4096] for i in range(0, len(data), 4096))
    options.output.write_text("".join('R"CYTOOLS(' + chunk + ')CYTOOLS",\n' for chunk in chunks), encoding="ascii")


if __name__ == "__main__":
    main()
