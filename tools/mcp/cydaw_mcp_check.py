#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Verifie le serveur MCP de bout en bout, sans agent : lance le serveur sur
stdio, fait l'initialisation MCP, puis enchaine les outils sur le DAW (qu'il
lance au besoin). Ne demarre pas la lecture."""
import argparse
import json
import os
import zipfile
import wave
import tempfile
import struct
import shutil
import math
import gzip
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SERVER = os.path.join(HERE, "cydaw_mcp_server.py")
OUT = os.path.join(ROOT, "artifacts", "mcp")


class Client:
    def __init__(self):
        self.process = subprocess.Popen(
            [sys.executable, SERVER], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=ROOT)
        self.next_id = 1

    def request(self, method, params=None):
        message = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            message["params"] = params
        self.next_id += 1
        self.process.stdin.write((json.dumps(message) + "\n").encode("utf-8"))
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("le serveur MCP s'est arrete : "
                               + self.process.stderr.read().decode("utf-8", "replace"))
        reply = json.loads(line.decode("utf-8"))
        if "error" in reply:
            raise RuntimeError("%s -> %s" % (method, reply["error"]))
        return reply["result"]

    def notify(self, method):
        self.process.stdin.write(
            (json.dumps({"jsonrpc": "2.0", "method": method}) + "\n").encode("utf-8"))
        self.process.stdin.flush()

    def tool(self, tool_name, **arguments):
        result = self.request("tools/call",
                              {"name": tool_name, "arguments": arguments})
        text = result["content"][0]["text"]
        if result.get("isError"):
            raise RuntimeError("%s -> %s" % (tool_name, text))
        return json.loads(text)

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=10)


def write_tone(path, seconds=1.0, rate=44100):
    """A one-second sine, enough for a clip to point at."""
    frames = bytearray()
    for i in range(int(seconds * rate)):
        value = int(12000 * math.sin(2 * math.pi * 220.0 * i / rate))
        frames += struct.pack("<h", value)
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(frames))


DAWPROJECT_XML = """<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0">
  <Application name="Check" version="1"/>
  <Transport><Tempo unit="bpm" value="100"/><TimeSignature numerator="4" denominator="4"/></Transport>
  <Structure>
    <Track contentType="notes" id="t1" name="Keys" color="#ff8800">
      <Channel id="c1" role="regular" solo="false">
        <Volume unit="linear" value="0.5"/><Pan unit="normalized" value="0.25"/><Mute value="false"/>
        <Devices><Vst3Plugin deviceRole="instrument" deviceName="Synth"/></Devices>
      </Channel>
    </Track>
    <Track contentType="audio" id="t2" name="Drums">
      <Channel id="c2" role="regular"><Volume unit="linear" value="1"/></Channel>
    </Track>
    <Track id="master"><Channel role="master"><Volume unit="linear" value="0.7"/></Channel></Track>
  </Structure>
  <Arrangement>
    <Lanes timeUnit="beats">
      <Lanes track="t1"><Clips>
        <Clip time="4" duration="8" name="Riff" playStart="0">
          <Notes><Note time="0" duration="1" key="60" vel="0.9"/><Note time="2" duration="0.5" key="64" vel="0.5"/><Note time="9" duration="1" key="67" vel="0.5"/></Notes>
        </Clip>
      </Clips></Lanes>
      <Lanes track="t2"><Clips>
        <Clip time="0" duration="4" name="Loop">
          <Warps contentTimeUnit="seconds" timeUnit="beats">
            <Audio algorithm="raw" channels="1" duration="1" sampleRate="44100"><File path="audio/tone.wav"/></Audio>
            <Warp time="0" contentTime="0"/><Warp time="4" contentTime="1"/>
          </Warps>
        </Clip>
      </Clips></Lanes>
    </Lanes>
  </Arrangement>
  <Scenes><Scene name="S1"><Lanes><ClipSlot track="t1">
    <Clip time="0" duration="4" name="Launch"><Notes><Note time="0" duration="1" key="48" vel="1"/></Notes></Clip>
  </ClipSlot></Lanes></Scene></Scenes>
</Project>
"""

ABLETON_XML = """<?xml version="1.0" encoding="UTF-8"?>
<Ableton MajorVersion="5" MinorVersion="11.0_433" Creator="Ableton Live 11.3.13">
  <LiveSet>
    <MasterTrack><DeviceChain><Mixer><Tempo><Manual Value="90"/></Tempo><Volume><Manual Value="1"/></Volume></Mixer></DeviceChain></MasterTrack>
    <Tracks>
      <MidiTrack Id="1">
        <Name><EffectiveName Value="Bass"/></Name><Color Value="14"/>
        <DeviceChain>
          <Mixer><Volume><Manual Value="0.5"/></Volume><Pan><Manual Value="0.3"/></Pan><Speaker><Manual Value="true"/></Speaker></Mixer>
          <DeviceChain><Devices><OriginalSimpler/></Devices></DeviceChain>
          <MainSequencer>
            <ClipTimeable><ArrangerAutomation><Events>
              <MidiClip Time="8"><CurrentStart Value="0"/><CurrentEnd Value="4"/><Name Value="Line"/>
                <Loop><LoopStart Value="0"/><LoopEnd Value="2"/><LoopOn Value="true"/></Loop>
                <Notes><KeyTracks><KeyTrack><Notes><MidiNoteEvent Time="0" Duration="0.5" Velocity="100" IsEnabled="true"/><MidiNoteEvent Time="1" Duration="0.5" Velocity="64"/></Notes><MidiKey Value="36"/></KeyTrack></KeyTracks></Notes>
              </MidiClip>
            </Events></ArrangerAutomation></ClipTimeable>
            <ClipSlotList><ClipSlot><ClipSlot><Value>
              <MidiClip Time="0"><CurrentStart Value="0"/><CurrentEnd Value="4"/><Name Value="Slot"/>
                <Notes><KeyTracks><KeyTrack><Notes><MidiNoteEvent Time="0" Duration="1" Velocity="127"/></Notes><MidiKey Value="40"/></KeyTrack></KeyTracks></Notes>
              </MidiClip>
            </Value></ClipSlot></ClipSlot></ClipSlotList>
          </MainSequencer>
        </DeviceChain>
      </MidiTrack>
      <AudioTrack Id="2">
        <Name><EffectiveName Value="Vox"/></Name>
        <DeviceChain>
          <Mixer><Volume><Manual Value="1"/></Volume></Mixer>
          <MainSequencer><ClipTimeable><ArrangerAutomation><Events>
            <AudioClip Time="2"><CurrentStart Value="0"/><CurrentEnd Value="3"/><Name Value="Take"/><IsWarped Value="false"/>
              <SampleRef><FileRef><Name Value="tone.wav"/><RelativePath Value="Samples/tone.wav"/><Path Value="C:/nowhere/tone.wav"/></FileRef></SampleRef>
            </AudioClip>
          </Events></ArrangerAutomation></ClipTimeable></MainSequencer>
        </DeviceChain>
      </AudioTrack>
    </Tracks>
  </LiveSet>
</Ableton>
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=0,
                        help="port de controle (defaut : celui du serveur)")
    parser.add_argument("--exe", default="", help="CyDAW 2.exe a lancer")
    parser.add_argument("--quit", action="store_true",
                        help="ferme le DAW a la fin")
    options = parser.parse_args()
    if options.port:
        os.environ["CYDAW_CONTROL_PORT"] = str(options.port)
    if options.exe:
        os.environ["CYDAW_EXE"] = options.exe
    # The checked session is thrown away: CyDAW 2 keeps no copy to recover.
    os.environ["CYDAW_NO_RECOVERY"] = "1"
    os.makedirs(OUT, exist_ok=True)
    # The sample library's settings, this check's own: the person's folders
    # stay theirs, and the banks' sounds stay out of it.
    settings = os.path.join(OUT, "samples.settings")
    with open(settings, "w", encoding="utf-8") as out:
        out.write('<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n'
                  '  <VALUE name="sampleIncludeBanks" val="0"/>\n</PROPERTIES>\n')
    os.environ["CYDAW_SAMPLES_SETTINGS"] = settings
    # Its own cache too, empty at each run (the sample index it edits): the
    # person's other folders as they chose them.
    cache = os.path.join(OUT, "cache")
    shutil.rmtree(cache, ignore_errors=True)
    shutil.rmtree(os.path.join(OUT, "sample-cache"), ignore_errors=True)
    chosen = {}
    try:
        with open(os.path.join(os.environ.get("APPDATA", ""), "CyDAW", "Locations.json"), encoding="utf-8") as source:
            chosen = json.load(source).get("folders", {})
    except (OSError, ValueError):
        pass
    chosen["cache"] = cache
    locations_file = os.path.join(OUT, "Locations.json")
    with open(locations_file, "w", encoding="utf-8") as out:
        json.dump({"format": "CyDAW Locations", "version": 1, "folders": chosen}, out)
    os.environ["CYDAW_LOCATIONS_FILE"] = locations_file
    client = Client()
    failures = []
    try:
        init = client.request("initialize", {
            "protocolVersion": "2025-06-18", "capabilities": {},
            "clientInfo": {"name": "cydaw-mcp-check", "version": "1"}})
        client.notify("notifications/initialized")
        print("initialize:", init["serverInfo"], init["protocolVersion"])
        tools = client.request("tools/list")["tools"]
        print("tools:", len(tools), ", ".join(t["name"] for t in tools))

        print("launch:", client.tool("launch"))
        templates = client.tool("list_templates")["templates"]
        print("templates:", [t["name"] for t in templates])
        # The sound banks CyDAW knows and where it keeps things (read only).
        banks = client.tool("banks")
        places = client.tool("locations")["locations"]
        print("banks:", len(banks.get("banks", [])), "in", banks.get("folder"), "; locations:", [p["key"] for p in places])
        if not banks.get("banks"):
            failures.append("banks lists no bank")
        # Banks, downloads, cache, companion, AI models, sound models, devices,
        # presets, plug-in exports, DJ mixes, the Foundry, the processings.
        if len(places) != 12:
            failures.append("locations gives %d folders, expected 12" % len(places))
        status = client.tool("load_template", name="basse-et-nappes")
        print("loaded:", os.path.basename(status["project"]), "tempo", status["tempo"],
              "midi tracks", [t["name"] for t in status["midiTracks"]])
        # Nothing audible from an automated run: the master stays at -60 dB
        # until the end.
        client.tool("set_master_gain", gainDb=-60)

        added = client.tool("add_midi_track", name="Test MCP")
        print("add_midi_track:", added)
        inserted = client.tool("insert_device", track="Test MCP", key="synth-pluck")
        print("insert_device:", inserted)
        notes = client.tool("add_notes", track="Test MCP", clip="Motif",
                            notes=[{"note": 64 + (i % 4) * 3, "start": i * 0.5,
                                    "length": 0.4, "velocity": 0.7} for i in range(8)])
        print("add_notes:", notes)
        listed = client.tool("list_notes", track="Test MCP")
        count = sum(len(c["notes"]) for c in listed["clips"])
        if count != 8:
            failures.append("list_notes gives %d notes, expected 8" % count)

        parameters = client.tool("list_parameters", track="Basse")["parameters"]
        cutoff = next((p for p in parameters if p["key"].endswith(".lowpass.cutoff")), None)
        print("parameters:", len(parameters), "cutoff:", cutoff and cutoff["value"])
        if cutoff is None:
            failures.append("no lowpass cutoff on Basse")
        else:
            after = client.tool("set_parameter", track="Basse", key=cutoff["key"],
                                value=900.0)["parameters"]
            value = next(p["value"] for p in after if p["key"] == cutoff["key"])
            if abs(value - 900.0) > 0.5:
                failures.append("set_parameter left cutoff at %s" % value)
            # The same knob on a lane of the arrangement: written, read back,
            # cleared.
            automated = client.tool("add_automation", track="Basse", key=cutoff["key"], clear=True,
                                    points=[{"time": 0.0, "value": 300.0}, {"time": 2.0, "value": 4000.0}])
            print("device automation:", automated.get("list"))
            if automated.get("points") != 2:
                failures.append("add_automation on a device parameter: %s" % automated)
            listed_lane = client.tool("list_automation", track="Basse", key=cutoff["key"])
            if [round(point["value"]) for point in listed_lane.get("list", [])] != [300, 4000]:
                failures.append("list_automation on a device parameter: %s" % listed_lane)
            cleared = client.tool("clear_automation", track="Basse", key=cutoff["key"])
            if cleared.get("points") != 0:
                failures.append("clear_automation on a device parameter: %s" % cleared)

        mixed = client.tool("set_track", track="Nappes", gainDb=-9.0, pan=0.25)
        nappes = next(t for t in mixed["midiTracks"] if t["name"] == "Nappes")
        if abs(nappes["gainDb"] + 9.0) > 0.01 or abs(nappes["pan"] - 0.25) > 0.01:
            failures.append("set_track did not apply: %s" % nappes)

        tempo = client.tool("set_tempo", bpm=104)["tempo"]
        if abs(tempo - 104) > 0.01:
            failures.append("set_tempo gives %s" % tempo)
        loop = client.tool("set_loop", start=0, end=10, enabled=True)
        if not loop["enabled"] or abs(loop["end"] - 10) > 0.01:
            failures.append("set_loop gives %s" % loop)

        print("set_view:", client.tool("set_view", mode=4))
        snapshot = client.tool("snapshot", path=os.path.join(OUT, "mcp-check-launcher.png"))
        print("snapshot:", snapshot)
        if not os.path.isfile(snapshot["path"]):
            failures.append("no snapshot file")
        print("set_view:", client.tool("set_view", mode=2))
        devices = client.tool("list_devices")["devices"]
        kinds = {}
        for device in devices:
            kinds[device["kind"]] = kinds.get(device["kind"], 0) + 1
        print("devices:", kinds)
        # --- the second half: editing, arrangement, modulation, launcher
        listed = client.tool("list_notes", track="Test MCP")
        first = listed["clips"][0]["notes"][0]
        updated = client.tool("update_notes", track="Test MCP",
                              notes=[{"id": first["id"], "note": 72, "velocity": 0.5}])
        if updated["updated"] != 1:
            failures.append("update_notes: %s" % updated)
        removed = client.tool("remove_notes", track="Test MCP",
                              **{"from": 3.0, "to": 4.0})
        if removed["removed"] != 2:
            failures.append("remove_notes by range removed %s" % removed["removed"])
        clip = client.tool("set_midi_clip", track="Test MCP", lengthBeats=8, looping=True)
        if abs(clip["lengthBeats"] - 8) > 0.01 or not clip["looping"]:
            failures.append("set_midi_clip: %s" % clip)
        graph = client.tool("get_track_graph", track="Test MCP")
        print("graph:", [n["type"] for n in graph["nodes"]])
        if not any(n["type"] == "poly-pluck" for n in graph["nodes"]):
            failures.append("get_track_graph misses the pluck")
        client.tool("insert_device", track="Test MCP", key="lowpass")
        modulated = client.tool("modulate", track="Test MCP", target="lowpass",
                                parameter="lowpass.cutoff", rate=2.0,
                                minimum=200, maximum=3000)
        print("modulate:", modulated)
        if not modulated.get("modulated"):
            failures.append("modulate failed: %s" % modulated)
        client.tool("remove_modulation", track="Test MCP", target="lowpass",
                    parameter="lowpass.cutoff")
        removed_device = client.tool("remove_device", track="Test MCP", key="lowpass")
        if any(n["type"] == "lowpass" for n in removed_device["nodes"]):
            failures.append("remove_device left the lowpass")
        auto = client.tool("add_automation", track="Nappes", clear=True,
                           points=[{"time": 0, "valueDb": -12}, {"time": 4, "valueDb": -3}])
        if auto["points"] != 2:
            failures.append("add_automation: %s" % auto)
        client.tool("clear_automation", track="Nappes")
        if client.tool("list_automation", track="Nappes")["points"]:
            failures.append("clear_automation left points")
        renamed = client.tool("set_track", track="Test MCP", name="Pluck MCP")
        if not any(t["name"] == "Pluck MCP" for t in renamed["midiTracks"]):
            failures.append("rename failed")
        gone = client.tool("remove_track", track="Pluck MCP")
        if any(t["name"] == "Pluck MCP" for t in gone["midiTracks"]):
            failures.append("remove_track left the track")

        # an audio track from a shipped loop, and its clips
        loop = os.path.join(ROOT, "templates", "audio", "kick-loop.wav")
        if os.path.isfile(loop):
            audio = client.tool("add_audio_track", file=loop)
            clips = client.tool("list_audio_clips", track=audio["index"])["clips"]
            if len(clips) != 1:
                failures.append("add_audio_track gives %d clips" % len(clips))
            else:
                copy = client.tool("duplicate_audio_clip", track=audio["index"],
                                   clipId=clips[0]["id"], start=12.0)
                moved = client.tool("move_audio_clip", track=audio["index"],
                                    clipId=copy["clipId"], start=14.0)
                if abs(moved["start"] - 14.0) > 0.01:
                    failures.append("move_audio_clip: %s" % moved)
                client.tool("remove_clip", track=audio["index"], clipId=copy["clipId"])
                if len(client.tool("list_audio_clips", track=audio["index"])["clips"]) != 1:
                    failures.append("remove_clip left the copy")
        else:
            print("no kick-loop.wav: audio clip steps skipped")

        # --- the third part: graph cabling, buses, plugins, audio editing
        graph = client.tool("get_track_graph", track="Basse")
        lowpass = next(n for n in graph["nodes"] if n["type"] == "lowpass")
        master = next(n for n in graph["nodes"] if n["type"] == "master")
        node = client.tool("add_node", track="Basse", key="gain", x=0.8, y=0.7)
        print("add_node:", node["id"], node["type"], [p["name"] for p in node["ports"]])
        client.tool("disconnect", track="Basse", source=lowpass["id"], destination=master["id"])
        client.tool("connect", track="Basse", source=lowpass["id"], destination=node["id"])
        wired = client.tool("connect", track="Basse", source=node["id"], destination=master["id"])
        links = {(c["source"], c["destination"]) for c in wired["connections"]}
        if (lowpass["id"], node["id"]) not in links or (node["id"], master["id"]) not in links:
            failures.append("connect did not wire the gain in: %s" % sorted(links))
        set_node = client.tool("set_node", track="Basse", nodeId=node["id"], title="Trim MCP")
        if set_node["title"] != "Trim MCP":
            failures.append("set_node title: %s" % set_node)
        param = client.tool("set_node_parameter", track="Basse", nodeId=node["id"],
                            parameter="gain.db", value=-4.5)
        print("set_node_parameter:", param)
        described = client.tool("describe_node_type", key="lowpass")
        if not any(p["key"].endswith("cutoff") for p in described["parameters"]):
            failures.append("describe_node_type lacks the cutoff: %s" % described)
        comment = client.tool("add_comment", track="Basse", title="Note MCP", text="ajoute par le test",
                              x=0.05, y=0.8)
        client.tool("remove_comment", track="Basse", commentId=comment["commentId"])
        client.tool("remove_device", track="Basse", nodeId=node["id"])
        after = client.tool("get_track_graph", track="Basse")
        if any(n["type"] == "gain" and n["title"] == "Trim MCP" for n in after["nodes"]):
            failures.append("remove_device by id left the gain")

        buses = client.tool("list_buses")["buses"]
        print("buses:", [(b["name"], b["direction"]) for b in buses])
        master_graph = client.tool("insert_device", track="master", key="stereo-widener")
        print("master devices:", master_graph["devices"])
        if "stereo-width" not in master_graph["devices"]:
            failures.append("insert_device on master: %s" % master_graph["devices"])
        master_params = client.tool("list_parameters", track="master")["parameters"]
        width = next((p for p in master_params if p["key"].endswith("stereo.width")), None)
        if width is None:
            failures.append("master parameters lack the width: %s" % [p["key"] for p in master_params])
        else:
            client.tool("set_parameter", track="master", key=width["key"], value=1.1)
        added_bus = client.tool("add_bus", direction="output", name="Retour MCP")
        client.tool("set_bus", bus="Retour MCP", gainDb=-3.0, channels=2)
        removed_bus = client.tool("remove_bus", bus="Retour MCP")
        if any(b["name"] == "Retour MCP" for b in removed_bus["buses"]):
            failures.append("remove_bus left the bus")

        audio_settings = client.tool("audio_settings")
        print("audio:", audio_settings["type"], audio_settings["output"],
              audio_settings["sampleRate"], audio_settings["bufferSize"])
        print("launch quantization:", client.tool("set_launch_quantization", value="beat"))

        exported = client.tool("export_midi", track="Nappes", clip="Nappes",
                               file=os.path.join(OUT, "motif-mcp.mid"))
        print("export_midi:", exported)
        imported = client.tool("import_midi", file=exported["path"])
        if imported["tracksAdded"] < 1:
            failures.append("import_midi added no track")
        moved_clip = client.tool("set_midi_clip", track="Nappes", clip="Nappes",
                                 startBeats=16, name="Nappes 2", colour="#ff8844")
        if moved_clip["startBeats"] != 16 or moved_clip["scene"] != -1 or moved_clip["name"] != "Nappes 2":
            failures.append("set_midi_clip start/name: %s" % moved_clip)
        new_clip = client.tool("add_midi_clip", track="Nappes", name="Vide", scene=2, lengthBeats=8)
        if new_clip["scene"] != 2:
            failures.append("add_midi_clip scene: %s" % new_clip)
        slotted = client.tool("set_midi_clip", track="Nappes", clip="Nappes 2", scene=3)
        if slotted["scene"] != 3:
            failures.append("set_midi_clip scene: %s" % slotted)
        back = client.tool("set_midi_clip", track="Nappes", clip="Nappes 2", scene=-1, startBeats=16)
        if back["scene"] != -1 or back["startBeats"] != 16:
            failures.append("set_midi_clip back to the arrangement: %s" % back)
        # In the order shown, every kind together.
        moved = client.tool("move_track", track="Nappes", to=0)
        if moved["order"][0]["name"] != "Nappes":
            failures.append("move_track: %s" % [t["name"] for t in moved["order"]])

        test_plugin = os.path.join(ROOT, "build", "agent-graph", "CyDAWTestGain_artefacts",
                                   "Release", "VST3", "CyDAW Test Gain.vst3")
        loop = os.path.join(ROOT, "templates", "audio", "kick-loop.wav")
        if os.path.isfile(loop):
            audio = client.tool("add_audio_track", file=loop)
            index = audio["index"]
            client.tool("set_track", track=index, colour="#53d0c2", name="Kick MCP")
            clips = client.tool("list_audio_clips", track=index)["clips"]
            piece = client.tool("split_audio_clip", track=index, clipId=clips[0]["id"], at=2.0)
            if len(piece["clips"]) != 2:
                failures.append("split_audio_clip: %d clips" % len(piece["clips"]))
            client.tool("set_audio_clip", track=index, clipId=piece["newClipId"], fadeIn=0.05,
                        gainDb=-2.0, stretchMode="beats")
            client.tool("stretch_audio_clip", track=index, clipId=piece["newClipId"], duration=3.0)
            ranged = client.tool("range_edit", track=index, start=0.5, end=1.0, operation="split")
            print("range_edit clips:", len(ranged["clips"]))
            client.tool("set_audio_clip_bounds", track=index, clipId=piece["newClipId"],
                        looping=False)
            if os.path.isdir(test_plugin) or os.path.isfile(test_plugin):
                plugin = client.tool("add_plugin", track=index, file=test_plugin)
                params = client.tool("list_plugin_parameters", track=index,
                                     plugin=plugin["pluginIndex"])["parameters"]
                print("plugin parameters:", [p["name"] for p in params][:6])
                if params:
                    client.tool("set_plugin_parameter", track=index, plugin=plugin["pluginIndex"],
                                parameter=params[0]["index"], value=0.25)
                    auto = client.tool("add_automation", track=index, plugin=plugin["pluginIndex"],
                                       parameter=params[0]["index"], clear=True,
                                       points=[{"time": 0, "value": 0.2}, {"time": 2, "value": 0.8}])
                    if auto["points"] != 2:
                        failures.append("plugin automation: %s" % auto)
                client.tool("remove_plugin", track=index, plugin=plugin["pluginIndex"])
            else:
                print("no test plugin built: plugin steps skipped")
            gone = client.tool("remove_track", track="Kick MCP")
            if any(t["name"] == "Kick MCP" for t in gone["audioTracks"]):
                failures.append("remove_track left the audio track")
        else:
            print("no kick-loop.wav: audio editing steps skipped")

        # --- the sample explorer, on the shipped loops
        loops = os.path.join(ROOT, "templates", "audio")
        if os.path.isdir(loops):
            cache = os.path.join(OUT, "sample-cache")
            before = client.tool("samples_status")
            # The loops alone: the banks' sounds left out of this check's library.
            client.tool("sample_folders", add=loops, autoRescanMinutes=0, includeBanks=False)
            cached = client.tool("sample_cache", folder=cache)
            if os.path.normcase(cached["cacheFolder"]) != os.path.normcase(cache):
                failures.append("sample_cache folder: %s" % cached["cacheFolder"])
            client.tool("scan_samples", full=True)
            deadline = time.time() + 60
            while time.time() < deadline:
                state = client.tool("samples_status")
                if not state["scanning"] and state["samples"] >= 3:
                    break
                time.sleep(0.3)
            found = client.tool("search_samples", query="kick")["samples"]
            print("samples: kick ->", [(s["name"], s["bpm"], s["bpmSource"],
                                        round(s["bpmConfidence"], 2)) for s in found],
                  "categories:", state["categories"])
            # The shipped loops run at 100 and 98 BPM; the detector may answer
            # the tempo, its half or its double.
            for sample in found:
                expected = 100.0 if sample["name"] == "kick-loop" else 98.0
                tempo = sample["bpm"]
                if not any(abs(tempo - expected * factor) <= 2.0 * factor
                           for factor in (0.5, 1.0, 2.0)):
                    failures.append("tempo of %s: %s, expected about %s" % (
                        sample["name"], tempo, expected))
            if not any(s["category"] == "kick" for s in found):
                failures.append("search_samples: no kick categorised: %s" % found)
            if found:
                edited = client.tool("set_sample", path=found[0]["path"], favourite=True,
                                     rating=4, tags=["test", "punchy"], bpm=124)
                if not edited["favourite"] or edited["rating"] != 4 or edited["bpm"] != 124:
                    failures.append("set_sample: %s" % edited)
                if not os.path.isfile(os.path.join(cache, "sample-library.json")):
                    failures.append("no sample-library.json in the chosen cache")
            if found:
                selected = client.tool("select_sample", path=found[0]["path"])
                if selected["name"] != found[0]["name"]:
                    failures.append("select_sample: %s" % selected)
                previewed = client.tool("preview_sample", path=found[0]["path"], start=0.2)
                if not previewed["previewing"]:
                    failures.append("preview_sample did not start")
                time.sleep(0.3)
                client.tool("preview_sample", stop=True)
            # A tonal file of a known key, written here, must come back keyed.
            import math
            import struct
            import wave
            tonal = os.path.join(OUT, "tonal")
            os.makedirs(tonal, exist_ok=True)
            melody = os.path.join(tonal, "melody_am_check.wav")
            if not os.path.isfile(melody):
                rate = 44100
                frames = bytearray()
                for _ in range(3):
                    for midi in (57, 60, 64, 57, 59, 60, 62, 64, 67, 57):
                        hz = 440.0 * 2 ** ((midi - 69) / 12.0)
                        for k in range(int(0.3 * rate)):
                            t = k / rate
                            v = sum(a * math.sin(2 * math.pi * hz * h * t)
                                    for h, a in ((1, 0.6), (2, 0.25), (3, 0.12)))
                            v *= math.exp(-t / 0.5)
                            sample = int(max(-1.0, min(1.0, v)) * 32767)
                            frames += struct.pack("<hh", sample, sample)
                with wave.open(melody, "wb") as handle:
                    handle.setnchannels(2)
                    handle.setsampwidth(2)
                    handle.setframerate(rate)
                    handle.writeframes(bytes(frames))
            client.tool("sample_folders", add=tonal)
            deadline = time.time() + 90
            keyed = []
            while time.time() < deadline:
                keyed = client.tool("search_samples", query="melody_am_check")["samples"]
                if keyed and keyed[0]["keySource"]:
                    break
                time.sleep(0.3)
            print("key:", [(s["name"], s["key"], s["keySource"], round(s["keyConfidence"], 2))
                          for s in keyed])
            if not keyed or keyed[0]["key"] != "Am":
                failures.append("key detection: %s" % keyed)
            client.tool("sample_folders", remove=tonal)
            client.tool("set_view", mode=10)
            client.tool("snapshot", path=os.path.join(OUT, "mcp-check-samples.png"))
            client.tool("sample_folders", remove=loops,
                        autoRescanMinutes=before["autoRescanMinutes"])
            # The person's cache folder stays theirs.
            client.tool("sample_cache", folder=before["cacheFolder"])
        else:
            print("no templates/audio: sample explorer steps skipped")

        armed = client.tool("arm", track="Basse", armed=True)
        if not armed["armed"]:
            failures.append("arm failed")
        client.tool("arm", track="Basse", armed=False)
        scene = client.tool("launch_scene", scene=0)
        print("launch_scene:", scene)
        client.tool("stop_launcher")
        client.tool("undo", scope="midi")
        client.tool("redo", scope="midi")
        # Importers: a DAWproject zip and an Ableton set written here, then
        # read back through the DAW. The master stays silent throughout.
        import_root = tempfile.mkdtemp(prefix="cydaw-import-")
        try:
            wav = os.path.join(import_root, "tone.wav")
            write_tone(wav)
            dawproject = os.path.join(import_root, "fixture.dawproject")
            with zipfile.ZipFile(dawproject, "w") as archive:
                archive.writestr("project.xml", DAWPROJECT_XML)
                archive.write(wav, "audio/tone.wav")
            report = client.tool("import_project", file=dawproject)
            client.tool("set_master_gain", gainDb=-60)
            print("import dawproject:", report["imported"], "warnings", len(report["warnings"]))
            kinds = [(t["name"], t["kind"]) for t in report["tracks"]]
            if report["tempo"] != 100 or kinds != [("Keys", "midi"), ("Drums", "audio")]:
                failures.append("import_project dawproject: %s %s" % (report["tempo"], kinds))
            if report["imported"]["notes"] != 3 or report["imported"]["clips"] != 3:
                failures.append("import_project dawproject counts: %s" % report["imported"])
            status = client.tool("status")
            if [t["name"] for t in status["midiTracks"]] != ["Keys"] \
                    or [t["name"] for t in status["audioTracks"]] != ["Drums"]:
                failures.append("import_project dawproject tracks: %s / %s" % (
                    status["midiTracks"], status["audioTracks"]))
            graph = client.tool("get_track_graph", track="Keys")
            if not graph.get("nodes"):
                failures.append("import_project: no instrument on Keys")
            os.makedirs(os.path.join(import_root, "Samples"), exist_ok=True)
            shutil.copy(wav, os.path.join(import_root, "Samples", "tone.wav"))
            als = os.path.join(import_root, "fixture.als")
            with gzip.open(als, "wb") as archive:
                archive.write(ABLETON_XML.encode("utf-8"))
            report = client.tool("import_project", file=als)
            client.tool("set_master_gain", gainDb=-60)
            print("import ableton:", report["imported"], "warnings", len(report["warnings"]))
            if report["tempo"] != 90 or report["imported"]["notes"] != 5 \
                    or report["imported"]["audioTracks"] != 1 or report["imported"]["midiTracks"] != 1:
                failures.append("import_project ableton: %s %s" % (report["tempo"], report["imported"]))
            clips = client.tool("list_audio_clips", track="Vox")["clips"]
            if len(clips) != 1 or abs(clips[0]["start"] - 2 * 60.0 / 90.0) > 0.01:
                failures.append("import_project ableton audio clip: %s" % clips)
            unknown = os.path.join(import_root, "fixture.txt")
            open(unknown, "w").write("x")
            try:
                client.tool("import_project", file=unknown)
                failures.append("import_project accepted a .txt")
            except RuntimeError:
                pass
        finally:
            shutil.rmtree(import_root, ignore_errors=True)
        fresh = client.tool("new_project")
        if len(fresh["audioTracks"]) != 0 or len(fresh["midiTracks"]) != 1:
            failures.append("new_project: %s audio, %s midi" % (
                len(fresh["audioTracks"]), len(fresh["midiTracks"])))
        # The device workshop: an effect built apart, put on the track and
        # linked, checked for a VST3; nothing saved, nothing written.
        if "workshop" in {t["name"] for t in tools}:
            made = client.tool("workshop", action="new", name="Check effect")
            nodes = made["graph"]["nodes"]
            inputs = [n["id"] for n in nodes if "input" in n["type"]]
            outputs = [n["id"] for n in nodes if "output" in n["type"]]
            gain = client.tool("add_node", track="workshop", key="gain")
            gain_id = int(gain["id"])
            if inputs and outputs:
                client.tool("disconnect", track="workshop", source=inputs[0], destination=outputs[0])
                client.tool("connect", track="workshop", source=inputs[0], destination=gain_id)
                client.tool("connect", track="workshop", source=gain_id, destination=outputs[0])
            else:
                failures.append("workshop new: no input or output %s" % nodes)
            linked = client.tool("workshop", action="link", track=0)
            if not linked.get("track"):
                failures.append("workshop link: %s" % linked)
            on_track = client.tool("get_track_graph", track=0)
            if not any(n.get("graphDevice") for n in on_track["nodes"]):
                failures.append("workshop link put no device on the track: %s" % on_track["nodes"])
            check = client.tool("workshop", action="vst3_check")
            if not check.get("ready"):
                failures.append("workshop vst3_check: %s" % check)
            print("workshop:", linked.get("name"), "on", linked.get("track"), "; vst3 ready:", check.get("ready"))
            client.tool("workshop", action="close")
            fresh = client.tool("new_project")
        else:
            failures.append("the MCP server has no workshop tool")
        if len(fresh["audioTracks"]) != 0 or len(fresh["midiTracks"]) != 1:
            failures.append("new_project: %s audio, %s midi" % (
                len(fresh["audioTracks"]), len(fresh["midiTracks"])))
        client.tool("set_master_gain", gainDb=0)
        final = client.tool("status")
        print("status: tracks", len(final["audioTracks"]), "audio,",
              len(final["midiTracks"]), "midi; playing", final["playing"])
        resources = client.request("resources/list")["resources"]
        devices_resource = client.request("resources/read", {"uri": "cydaw://devices"})
        print("resources:", [r["uri"] for r in resources],
              "devices resource bytes:", len(devices_resource["contents"][0]["text"]))
        prompt = client.request("prompts/get", {"name": "compose",
                                                "arguments": {"brief": "test"}})
        if not prompt["messages"]:
            failures.append("prompt empty")
        if options.quit:
            print("quit:", client.tool("quit"))
    finally:
        client.close()
    if failures:
        print("FAILED:")
        for failure in failures:
            print("  -", failure)
        sys.exit(1)
    print("ok: MCP server and control port answer as expected")


if __name__ == "__main__":
    main()
