"""Graph preset authoring, defaults, nesting, persistence and native export."""
import argparse
import json
import socket
import uuid
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--port", type=int, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)

def request(**command):
    with socket.create_connection(("127.0.0.1", args.port), timeout=10) as sock:
        sock.settimeout(120)
        sock.sendall((json.dumps(command) + "\n").encode())
        return json.loads(sock.makefile("rb").readline())

def need(**command):
    response = request(**command)
    assert response.get("ok"), response
    return response["result"]

def w(action, **kw):
    return need(cmd="workshop", action=action, **kw)

def set_threshold(value):
    need(cmd="set_node_parameter", track="workshop", nodeId=node,
         parameter="comppro.threshold", value=value)

def threshold():
    return next(p["value"] for p in w("presentation")["parameters"] if p["key"] == key)

w("new", name="Graph preset regression", instrument=False)
node = need(cmd="add_node", track="workshop", key="comp-pro")["id"]
need(cmd="disconnect", track="workshop", source=1, destination=2)
need(cmd="connect", track="workshop", source=1, destination=node)
need(cmd="connect", track="workshop", source=node, destination=2)
scene = w("presentation")
p = next(p for p in scene["parameters"] if p["key"].endswith("comppro.threshold"))
key, default = p["key"], p["defaultValue"]
# This setting stays editable in the plugin, but is absent from host automation.
need(cmd="expose_parameter", track="workshop", key=key, host=False)
set_threshold(-34)
w("programs", op="add", name="Init", defaults=True)
assert threshold() == -34, "capturing defaults must not change the current sound"
assert w("programs")["programs"][0]["values"][key] == default
w("programs", op="add", name="Heavy")
set_threshold(-6)
w("programs", op="load", name="Heavy")
assert threshold() == -34
w("programs", op="load", name="Init")
assert threshold() == default
set_threshold(-40)
w("programs", op="add", name="Heavy")
assert len(w("programs")["programs"]) == 2
w("programs", op="rename", name="Heavy", newName="Deep")
assert not request(cmd="workshop", action="programs", op="rename", name="Deep", newName="Init")["ok"]
# A subgraph has its own bank and does not replace the parent's bank.
module = need(cmd="add_node", track="workshop", key="clean-channel")["id"]
w("enter", nodeId=module)
assert not w("programs")["programs"]
w("programs", op="add", name="Nested Init", defaults=True)
w("leave")
assert [p["name"] for p in w("programs")["programs"]] == ["Init", "Deep"]
w("enter", nodeId=module)
assert [p["name"] for p in w("programs")["programs"]] == ["Nested Init"]
w("programs", op="remove", name="Nested Init")
assert not w("programs")["programs"]
w("leave")
# Keep the nested bank through a real file save/reopen, independently of its parent.
w("enter", nodeId=module)
w("programs", op="add", name="Nested Saved", defaults=True)
w("leave")
state = w("port_exposure", nodeId=2, portId=1, exposed=True)
nested_file = (args.output / ("nested-presets-" + uuid.uuid4().hex + ".cydawdevice")).resolve()
nested_file.write_text(json.dumps(dict(format="CyDAW Device", schema=1, role="audio-effect",
                                       name="Nested preset regression", graph=state)), encoding="utf8")
opened = w("open", file=str(nested_file))
w("save")
w("close_tab", id=opened["activeTab"])
w("open", file=str(nested_file))
w("enter", nodeId=module)
assert [p["name"] for p in w("programs")["programs"]] == ["Nested Saved"]
w("leave")
# Drop the unconnected fixture module from the separate effect used for export.
state["nodes"] = [n for n in state["nodes"] if n["id"] != module]
file = (args.output / ("graph-presets-" + uuid.uuid4().hex + ".cydawdevice")).resolve()
file.write_text(json.dumps(dict(format="CyDAW Device", schema=1, role="audio-effect",
                               name="Graph preset regression", graph=state)), encoding="utf8")
opened = w("open", file=str(file))
w("save")
w("close_tab", id=opened["activeTab"])
w("open", file=str(file))
assert [p["name"] for p in w("programs")["programs"]] == ["Init", "Deep"]
w("programs", op="load", name="Deep")
assert threshold() == -40
runtime = w("runtime_state")
internal = next(p for p in runtime["allParameters"] if p["key"] == key)
assert not internal["exportedToHost"]
assert len(runtime["programs"][0]["allValues"]) == len(runtime["allParameters"])
assert len(runtime["programs"][0]["hostValues"]) == len(runtime["publishedParameters"])
export = w("vst3_export", folder=str((args.output / "NativePresets").resolve()),
           productName="CyGraphPresets", pluginCode="GPRS")
cpp = (Path(export["project"]) / "generated/Definition.cpp").read_text(encoding="utf8")
assert "p.allValues.push_back" in cpp and 'p.name="Deep"' in cpp
report = dict(defaults=True, recall=True, replace=True, rename=True, nested=True,
              persistence=True, internalParameterId=internal["id"], project=export["project"],
              hostParameters=len(runtime["publishedParameters"]), programs=2)
(args.output / "checks.json").write_text(json.dumps(report, indent=2), encoding="utf8")
print("Graph preset integration checks passed", flush=True)
