"""Exercise CyDaw's native assistant with an isolated profile and synthetic local API."""
import argparse
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import threading
import time
import wave
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


class Fixture(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        reply = json.dumps({"data": [{"id": "cydaw-fixture"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)

    def event(self, delta, finish=None):
        message = {"choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}
        self.wfile.write(("data: " + json.dumps(message) + "\n\n").encode())
        self.wfile.flush()

    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        messages = payload["messages"]
        if messages[-1]["content"] == "Exercise provider failure.":
            body = json.dumps({"error": {"message": "Fixture service unavailable", "code": "fixture_unavailable"}}).encode()
            self.send_response(503)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if messages[-1]["role"] == "tool":
            track = json.loads(messages[-2]["content"])
            workshop = json.loads(messages[-1]["content"])
            if track.get("name") != "Assistant QA" or not any(
                    tab["name"] == "Assistant device" for tab in workshop.get("tabs", [])):
                self.send_error(422, "CyDaw did not apply the tool")
                return
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        try:
            if messages[-1]["role"] == "tool":
                self.event({"content": "Done."}, "stop")
            elif messages[-1]["content"] == "Exercise cancellation.":
                self.event({"content": "Waiting"})
                # A completed turn would be a failure. Leave the call active long enough to cancel.
                for _ in range(30):
                    time.sleep(0.1)
                    self.wfile.write(b": idle\n\n")
                    self.wfile.flush()
                self.event({"content": " too long."}, "stop")
            else:
                tools = {tool["function"]["name"] for tool in payload["tools"]}
                assert {"cy_tools_search", "cy_tools_describe", "cy_tools_call"} <= tools, tools
                self.event({"content": "Creating the track."})
                time.sleep(0.55)
                edits = [("add_midi_track", {"name": "Assistant QA"}), ("workshop", {
                    "action": "new", "name": "Assistant device"})]
                calls = [{"index": index, "id": f"edit-{index}", "type": "function",
                          "function": {"name": "cy_tools_call", "arguments": json.dumps({
                              "name": tool, "arguments": params})}}
                         for index, (tool, params) in enumerate(edits)]
                self.event({"tool_calls": calls}, "tool_calls")
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for(condition, seconds, description):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        try:
            value = condition()
            if value:
                return value
        except (URLError, TimeoutError, FileNotFoundError, json.JSONDecodeError):
            pass
        time.sleep(0.08)
    raise AssertionError("Timed out: " + description)


class ExternalMcp:
    def __init__(self, port, token=None):
        self.url = f"http://127.0.0.1:{port}/mcp"
        self.headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
                        "MCP-Protocol-Version": "2025-11-25"}
        if token is not None:
            self.headers["Authorization"] = f"Bearer {token}"
        self.next_id = 0

    def request(self, method, params=None, notification=False):
        message = {"jsonrpc": "2.0", "method": method, "params": params or {}}
        if not notification:
            self.next_id += 1
            message["id"] = self.next_id
        request = Request(self.url, json.dumps(message).encode(), self.headers)
        with urlopen(request, timeout=3) as response:
            if method == "initialize":
                self.headers["Mcp-Session-Id"] = response.headers["Mcp-Session-Id"]
            if not notification:
                reply = json.load(response)
                assert reply.get("id") == self.next_id and "result" in reply, reply
                return reply["result"]
        return None

    def connect(self):
        result = self.request("initialize", {"protocolVersion": "2025-11-25",
                                              "capabilities": {},
                                              "clientInfo": {"name": "CyDaw QA", "version": "1"}})
        assert result["protocolVersion"] == "2025-11-25", result
        self.request("notifications/initialized", notification=True)
        return True

    def tool(self, name, arguments=None):
        return self.request("tools/call", {"name": name, "arguments": arguments or {}})


def cli(exe, *arguments, env=None, data=None, expected=0):
    result = subprocess.run([exe, *arguments], env=env, input=data, capture_output=True,
                            text=True, encoding="utf-8", timeout=15, check=False)
    if result.returncode != expected:
        raise AssertionError((arguments, result.returncode, result.stdout, result.stderr))
    if expected:
        assert not result.stdout, result
        return json.loads(result.stderr)["error"]
    assert not result.stderr, result
    if arguments == ("--help",):
        return result.stdout
    return json.loads(result.stdout)


def apply(peer, tool, params=None):
    response = peer.tool(tool, params)
    assert not response["isError"], response
    return response["structuredContent"]


def error_code(response):
    assert response["isError"], response
    return json.loads(response["content"][0]["text"])["code"]


def exercise_modes(args, peer, port, env, folder, track):
    media = folder / "Media"
    audio = media / "fixture.wav"
    with wave.open(str(audio), "wb") as sound:
        sound.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        sound.writeframes(b"\x00\x08\xff\xf7" * 24000)
    outside = folder / "outside.wav"
    outside.write_bytes(audio.read_bytes())
    refused = peer.tool("dj", {"action": "load", "deck": "A", "file": str(outside)})
    assert error_code(refused) == "FileAccessDenied"
    apply(peer, "dj", {"action": "load", "deck": "A", "file": str(audio)})
    dj = cli(args.cli, f"--port={port}", "call", "dj", "-", env=env,
             data='{"action":"mixer","crossfader":0.4,"decks":4}')
    assert abs(dj["crossfader"] - 0.4) < 0.001 and dj["deckCount"] == 4, dj
    wait_for(lambda: apply(peer, "dj", {"action": "status"})["decks"][0]["file"], 8, "DJ media load")
    collection = apply(peer, "dj", {"action": "tracks", "query": "fixture", "limit": 1})
    assert len(collection["tracks"]) <= 1 and "nextOffset" in collection, collection

    cli(args.cli, f"--port={port}", "call", "foundry", "-", env=env,
        data='{"action":"new","name":"QA Foundry"}')
    imported = apply(peer, "foundry", {"action": "import", "file": str(audio), "track": 0})
    assert imported["tabs"][imported["current"]]["tracks"][0]["clips"], imported
    foundry_file = media / "qa.cyfoundry"
    apply(peer, "foundry", {"action": "save_session", "path": str(foundry_file)})
    assert foundry_file.is_file()

    apply(peer, "cinema", {"action": "new", "name": "QA Cinema"})
    grouped = apply(peer, "cinema", {"action": "group", "op": "add", "name": "Sound design"})
    group = next(item["id"] for item in grouped["tabs"][grouped["current"]]["tracks"]
                 if item["name"] == "Sound design")
    cinema = apply(peer, "cinema", {"action": "add_subtrack", "groupId": group,
                                  "type": "midi", "name": "Cues"})
    child = next(item for item in cinema["tabs"][cinema["current"]]["tracks"] if item["name"] == "Cues")
    assert child["parentId"] == group and child["midi"], cinema
    apply(peer, "cinema", {"action": "container", "op": "focus", "id": child["id"]})
    if args.ffmpeg:
        movie = media / "fixture.mp4"
        subprocess.run([args.ffmpeg, "-loglevel", "error", "-f", "lavfi", "-i",
                        "testsrc2=size=160x96:rate=25:duration=1", "-c:v", "mpeg4", "-g", "1",
                        "-pix_fmt", "yuv420p", movie], check=True)
        apply(peer, "cinema", {"action": "video", "file": str(movie)})

        def video_ready():
            view = apply(peer, "cinema", {"action": "status"})
            return view["tabs"][view["current"]].get("decoder", {}).get("width") == 160

        wait_for(video_ready, 10, "Cinema video decoding")

    graph = apply(peer, "get_track_graph", {"track": f"track-id:{track}"})
    assert "nodes" in graph, graph
    added = apply(peer, "add_node", {"track": "workshop", "key": "chorus"})
    node = int(added["id"])
    apply(peer, "set_node", {"track": "workshop", "nodeId": node, "title": "QA Chorus"})
    changed = apply(peer, "get_track_graph", {"track": "workshop"})
    assert any(item["id"] == node and item["name"] == "QA Chorus" for item in changed["nodes"]), changed
    apply(peer, "workshop", {"action": "programs", "op": "add", "name": "QA Program"})
    programs = apply(peer, "workshop", {"action": "programs", "op": "list"})["programs"]
    assert any(item["name"] == "QA Program" for item in programs), programs
    export = media / "Assistant device.cydawdevice"
    apply(peer, "workshop", {"action": "device_export", "file": str(export)})
    assert export.is_file()
    assert error_code(peer.tool("workshop", {"action": "device_export", "file": str(export)})) == "DestinationExists"
    target = media / "Plugin projects"
    batch = apply(peer, "workshop", {"action": "plugin_export_batch", "folder": str(target),
                                    "formats": ["VST3", "CLAP", "LV2"], "platforms": ["Windows"],
                                    "build": False})
    assert {item["format"] for item in batch["targets"]} == {"VST3", "CLAP", "LV2"}, batch
    assert all((Path(item["project"]) / "CMakeLists.txt").is_file() for item in batch["targets"]), batch


@contextmanager
def instance(exe, environment, *arguments):
    legacy_port = free_port()
    while f"--mcp-port={legacy_port}" in arguments:
        legacy_port = free_port()
    legacy_token = secrets.token_hex(32)
    env = dict(environment, CYDAW_CONTROL_TOKEN=legacy_token)
    process = subprocess.Popen([exe, "--silent", f"--control-port={legacy_port}", *arguments],
                               env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    def legacy(command):
        with socket.create_connection(("127.0.0.1", legacy_port), timeout=3) as client:
            request = json.dumps({"cmd": command, "token": legacy_token}) + "\n"
            client.sendall(request.encode("utf-8"))
            with client.makefile("rb") as reply:
                return json.loads(reply.readline())

    try:
        yield process, legacy
        assert legacy("quit")["ok"]
        assert process.wait(timeout=8) == 0
    finally:
        if process.poll() is None:
            process.terminate()
        _, stderr = process.communicate(timeout=8)
        if process.returncode != 0 and stderr:
            print(stderr.decode("utf-8", errors="replace"))


def run(args, folder, provider):
    assert "describe NAME" in cli(args.cli, "--help")
    version = cli(args.cli, "--version")
    assert version["sdkVersion"] and version["sdkRevision"]
    assert cli(args.cli, "describe", "set_tempo")["inputSchema"]["required"] == ["bpm"]
    page = cli(args.cli, "call", "list_factory_effects", "-", data='{"limit":2}')
    assert len(page["effects"]) == 2 and page["nextOffset"] == 2, page
    assert cli(args.cli, "call", "session_status", expected=1)["code"] == "InstanceRequired"

    token = secrets.token_hex(32)
    (folder / "Media").mkdir()
    env = dict(os.environ, APPDATA=str(folder / "Roaming"), LOCALAPPDATA=str(folder / "Local"),
               CYDAW_NO_RECOVERY="1", CYDAW_TEST_VIRTUAL_AUDIO="1",
               CYDAW_MCP_FILE_ROOT=str(folder / "Media"))
    env.pop("CYDAW_MCP_TOKEN", None)
    authenticated = dict(env, CYDAW_MCP_TOKEN=token)
    settings = folder / "Roaming/CyDAW/Assistant/mcp-settings.json"

    # Both a fresh configuration and an existing one without a profile use Free.
    for previous in (None, {"previousSetting": True}, {"profile": None}):
        if previous is not None:
            settings.parent.mkdir(parents=True, exist_ok=True)
            settings.write_text(json.dumps(previous), encoding="utf-8")
        port = free_port()
        with instance(args.exe, env, f"--mcp-port={port}") as (process, legacy):
            peer = ExternalMcp(port)
            wait_for(peer.connect, 18, "default Free instance")
            assert process.poll() is None and legacy("status")["ok"]
            assert apply(peer, "session_status")["tempo"] > 0
            assert apply(peer, "set_tempo", {"bpm": 127})["tempo"] == 127
            assert apply(peer, "session_status")["tempo"] == 127
            assert peer.tool("set_tempo", {"bpm": "invalid"})["isError"]
            assert apply(peer, "session_status")["tempo"] == 127
        if previous is not None:
            assert json.loads(settings.read_text("utf-8")) == previous

    def require_token(port):
        for candidate in (None, secrets.token_hex(32)):
            try:
                ExternalMcp(port, candidate).connect()
            except HTTPError as error:
                assert error.code == 401
            else:
                raise AssertionError("An absent or wrong bearer token was accepted")

    # A stored selection survives restarts and an explicit launch-only override.
    settings.write_text(json.dumps({"profile": "protected"}), encoding="utf-8")
    port = free_port()
    with instance(args.exe, authenticated, f"--mcp-port={port}"):
        peer = ExternalMcp(port, token)
        wait_for(peer.connect, 18, "stored Protected instance")
        require_token(port)
        assert apply(peer, "session_status")["tempo"] > 0
        denied = peer.tool("set_tempo", {"bpm": 127})
        assert error_code(denied) == "AccessDenied", denied
        failure = cli(args.cli, f"--port={port}", "call", "set_tempo", "-", data='{"bpm":127}',
                      env=authenticated, expected=1)
        assert failure["code"] == "AccessDenied", failure
        cli(args.cli, f"--port={port}", "call", "session_status", env=env, expected=1)
        for tool in ("dj", "cinema", "foundry", "workshop"):
            assert not peer.tool(tool, {"action": "status"})["isError"]
        assert error_code(peer.tool("dj", {"action": "status", "show": True})) == "AccessDenied"
        assert error_code(peer.tool("foundry", {"action": "status", "tab": 0})) == "AccessDenied"
        assert error_code(peer.tool("workshop", {"action": "vst3_check", "productName": "Changed"})) == "AccessDenied"

    port = free_port()
    with instance(args.exe, env, f"--mcp-port={port}", "--mcp-profile=local_controlled"):
        peer = ExternalMcp(port)
        wait_for(peer.connect, 18, "Local controlled instance")
        assert apply(peer, "session_status")["tempo"] > 0
        assert error_code(peer.tool("set_tempo", {"bpm": 127})) == "AccessDenied"
    assert json.loads(settings.read_text("utf-8")) == {"profile": "protected"}

    settings.write_text(json.dumps({"profile": "token_only"}), encoding="utf-8")
    port = free_port()
    with instance(args.exe, authenticated, f"--mcp-port={port}"):
        peer = ExternalMcp(port, token)
        wait_for(peer.connect, 18, "stored Token only instance")
        require_token(port)
        assert apply(peer, "set_tempo", {"bpm": 127})["tempo"] == 127

    port = free_port()
    report = folder / "Roaming/CyDAW/Assistant/check.json"
    with instance(args.exe, env, f"--mcp-port={port}", "--mcp-profile=free",
                  "--mcp-allow-edits", "--mcp-metrics", "--mcp-cache-seconds=0",
                  f"--assistant-check-port={provider.server_port}"):
        peer = ExternalMcp(port)
        wait_for(peer.connect, 18, "Free instance with chat enabled")
        assert any(tool["name"] == "list_tracks" for tool in peer.request("tools/list")["tools"])
        assert cli(args.cli, f"--port={port}", "describe", "set_tempo", env=env)["name"] == "set_tempo"
        tempo = cli(args.cli, f"--port={port}", "call", "set_tempo", "-", data='{"bpm":132}', env=env)
        assert tempo["tempo"] == 132, tempo
        assert peer.tool("session_status")["structuredContent"]["tempo"] == 132
        result = wait_for(lambda: json.loads(report.read_text("utf-8")), 40, "stream, tool action, cancel and restore")
        assert result == {"streamed": True, "cancelled": True, "restored": True, "providerError": True}, result
        tracks = peer.tool("list_tracks", {"limit": 1})["structuredContent"]["tracks"]
        assert tracks[0]["name"] == "Assistant QA", tracks
        track = tracks[0]["trackId"]
        mixed = peer.tool("set_track_mix", {"trackId": track, "gainDb": -8})["structuredContent"]
        assert mixed["gainDb"] == -8, mixed
        clip = peer.tool("add_midi_clip", {"trackId": track, "name": "Test clip", "startBeats": 0,
                                           "lengthBeats": 4})["structuredContent"]
        assert clip["clipId"], clip
        assert peer.tool("add_bus_track", {"name": "QA bus"})["structuredContent"]["name"] == "QA bus"
        first = peer.tool("list_tracks", {"limit": 1})["structuredContent"]
        assert first["total"] == 2 and first["nextOffset"] == 1, first
        last = peer.tool("list_tracks", {"offset": first["nextOffset"]})["structuredContent"]
        assert last["tracks"][0]["kind"] == "bus", last
        exercise_modes(args, peer, port, env, folder, track)
        assert peer.tool("set_tempo", {"bpm": "invalid"})["isError"]
        metrics = report.with_name("metrics.jsonl")
        log = wait_for(lambda: metrics.read_text("utf-8"), 5, "performance records")
        assert "set_tempo" in log and token not in log and "Assistant QA" not in log and "Test clip" not in log
    print("CyDaw Free, Local controlled, Token only, Protected, CLI and widget chat passed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--ffmpeg", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cydaw-assistant-") as temporary, \
            ThreadingHTTPServer(("127.0.0.1", 0), Fixture) as provider:
        worker = threading.Thread(target=provider.serve_forever, daemon=True)
        worker.start()
        try:
            run(args, Path(temporary), provider)
        finally:
            provider.shutdown()
            worker.join()


if __name__ == "__main__":
    main()
