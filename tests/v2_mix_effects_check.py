"""The mixing effects in the software (Pression, Fournaise, Écho Bande, Multibande Pro, Velours,
Patine, Sonomètre) and the suites (Tranche, Finition). Silent, offline.

    python tests/v2_mix_effects_check.py --exe "<CyDAW 2.exe>" [--port 47844]

The presets folder is the test's own (CYDAW_LOCATIONS_FILE). Each check renders the track's
effects as an exported plug-in gets them (effect_chain_check: the state written, restored,
compiled, a sine through it offline): nothing is played.

* Each goes on an audio track, with its factory presets.
* Pression: at no depth it leaves a sine as it is; at full depth it raises a quiet one.
* Fournaise: driven, it adds harmonics; its automatic level keeps the level near the sound's.
* Écho Bande: the echoes add to the sound and stay bounded, even run away.
* Multibande Pro: with no amount it leaves the sound; a band compressed holds the bass.
* Velours: a lone resonance is calmed. Patine plays, bounded. The Sonomètre leaves the sound.
* Tranche's compressor holds a loud sound; Finition raises the sound and keeps its ceiling.
* A saved project opened again keeps their settings.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

failures: list[str] = []
port = 47844
ROOT = Path(__file__).resolve().parents[1]


def request(command: dict) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as connection:
        connection.settimeout(120)
        connection.sendall((json.dumps(command) + "\n").encode("utf-8"))
        data = b""
        while not data.endswith(b"\n"):
            chunk = connection.recv(1 << 20)
            if not chunk:
                break
            data += chunk
    return json.loads(data.decode("utf-8"))


def need(command: dict, what: str) -> dict:
    answer = request(command)
    if not answer.get("ok"):
        raise RuntimeError(f"{what}: {answer.get('error')}")
    return answer.get("result", {})


def check(condition: bool, message: str) -> None:
    print(("ok   " if condition else "FAIL ") + message, flush=True)
    if not condition:
        failures.append(message)


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-mix-effects-check-"))
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "presets": str(scratch / "Presets"), "cache": str(scratch / "Cache")}}), encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([str(Path(args.exe).resolve()), f"--control-port={port}", "--silent",
                                "--window-size=1400x900"], env=environment, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(300):
            try:
                request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        if not need({"cmd": "status"}, "status").get("speakersSilenced"):
            raise RuntimeError("the session is not silent")
        need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
        loop = sorted((ROOT / "templates" / "audio").glob("*.wav"))[0]
        track = need({"cmd": "add_audio_track", "file": str(loop)}, "track")["name"]

        def chain(hz: float, amplitude: float, seconds: float = 0.8) -> dict:
            return need({"cmd": "effect_chain_check", "track": track, "hz": hz, "amplitude": amplitude, "seconds": seconds}, "chain")

        def node_of(answer: dict, kind: str) -> int:
            return [item["id"] for item in answer["nodes"] if item.get("type") == kind][-1]

        def load(node: int, name: str) -> None:
            need({"cmd": "presets", "track": track, "nodeId": node, "action": "load", "name": name}, f"preset {name}")

        def factory(node: int) -> list[str]:
            listed = need({"cmd": "presets", "track": track, "nodeId": node}, "presets")
            return [item["name"] for item in listed.get("presets", []) if item.get("factory")]

        def keys() -> dict:
            return {p["key"].split(".", 2)[-1]: p["key"] for p in need({"cmd": "list_parameters", "track": track}, "keys")["parameters"]}

        def set_value(key: str, value: float) -> None:
            need({"cmd": "set_parameter", "track": track, "key": keys()[key], "value": value}, key)

        # ---- Pression ----
        pression = node_of(need({"cmd": "insert_device", "track": track, "key": "pression"}, "pression"), "pression")
        check(len(factory(pression)) >= 8, f"Pression's factory presets ({len(factory(pression))})")
        set_value("pression.depth", 0.0)
        untouched = chain(1_000.0, 0.2)
        check(abs(untouched["outputDb"] - untouched["inputDb"]) < 0.5,
              f"at no depth Pression leaves the sound ({untouched['inputDb']:.1f} -> {untouched['outputDb']:.1f} dB)")
        load(pression, "Pression totale")
        raised = chain(1_000.0, 0.005, 1.5)
        check(raised["outputDb"] > raised["inputDb"] + 10.0,
              f"Pression raises a quiet sound ({raised['inputDb']:.1f} -> {raised['outputDb']:.1f} dB)")
        need({"cmd": "remove_device", "track": track, "nodeId": pression}, "remove Pression")

        # ---- Fournaise ----
        fournaise = node_of(need({"cmd": "insert_device", "track": track, "key": "fournaise"}, "fournaise"), "fournaise")
        names = factory(fournaise)
        check(len(names) >= 8 and "Lampe mordante" in names, f"Fournaise's factory presets ({len(names)})")
        set_value("fournaise.drive", 0.0)
        cool = chain(200.0, 0.3)
        load(fournaise, "Lampe mordante")
        hot = chain(200.0, 0.3, 1.5)
        check(hot["secondDb"] > cool["secondDb"] + 15.0,
              f"Fournaise driven adds harmonics (2nd {cool['secondDb']:.1f} -> {hot['secondDb']:.1f} dB)")
        check(abs(hot["outputDb"] - hot["inputDb"]) < 6.0 and hot["peakDb"] < 3.0,
              f"its automatic level keeps the level ({hot['inputDb']:.1f} -> {hot['outputDb']:.1f} dB)")
        need({"cmd": "remove_device", "track": track, "nodeId": fournaise}, "remove Fournaise")

        # ---- Multibande Pro ----
        multiband = node_of(need({"cmd": "insert_device", "track": track, "key": "multiband-pro"}, "multiband"), "multiband-pro")
        check(len(factory(multiband)) >= 7, "Multibande Pro's factory presets")
        set_value("multiband.amount", 0.0)
        flat = chain(1_000.0, 0.2)
        check(abs(flat["outputDb"] - flat["inputDb"]) < 0.5,
              f"Multibande Pro with no amount leaves the sound ({flat['inputDb']:.1f} -> {flat['outputDb']:.1f} dB)")
        load(multiband, "Basse tenue")
        bass = chain(80.0, 0.5, 1.0)
        check(bass["outputDb"] < bass["inputDb"] - 6.0, f"a band compressed holds the bass ({bass['inputDb']:.1f} -> {bass['outputDb']:.1f} dB)")
        need({"cmd": "remove_device", "track": track, "nodeId": multiband}, "remove Multibande Pro")

        # ---- Velours ----
        velours = node_of(need({"cmd": "insert_device", "track": track, "key": "velours"}, "velours"), "velours")
        check(len(factory(velours)) >= 6, "Velours's factory presets")
        ringing = chain(2_000.0, 0.2, 1.0)
        check(ringing["outputDb"] < ringing["inputDb"] - 3.0,
              f"Velours calms a lone resonance ({ringing['inputDb']:.1f} -> {ringing['outputDb']:.1f} dB)")
        need({"cmd": "remove_device", "track": track, "nodeId": velours}, "remove Velours")

        # ---- Patine ----
        patine = node_of(need({"cmd": "insert_device", "track": track, "key": "patine"}, "patine"), "patine")
        check(len(factory(patine)) >= 8, "Patine's factory presets")
        load(patine, "Vieux disque")
        worn = chain(440.0, 0.3, 1.0)
        check(-40.0 < worn["outputDb"] < worn["inputDb"] + 3.0 and worn["peakDb"] < 3.0, f"Patine plays, bounded ({worn['outputDb']:.1f} dB)")
        need({"cmd": "remove_device", "track": track, "nodeId": patine}, "remove Patine")

        # ---- Sonomètre ----
        meter = node_of(need({"cmd": "insert_device", "track": track, "key": "sonometre"}, "sonometre"), "sonometre")
        check(len(factory(meter)) >= 4, "the Sonometre's factory presets")
        measured = chain(1_000.0, 0.3)
        check(abs(measured["outputDb"] - measured["inputDb"]) < 0.01, "the Sonometre leaves the sound as it is")
        need({"cmd": "remove_device", "track": track, "nodeId": meter}, "remove the Sonometre")

        # ---- Tranche ----
        tranche = node_of(need({"cmd": "insert_device", "track": track, "key": "tranche"}, "tranche"), "tranche")
        check(len(factory(tranche)) >= 8, "Tranche's factory presets")
        load(tranche, "Piano")
        pressed = chain(1_000.0, 0.5, 1.0)
        check(pressed["outputDb"] < pressed["inputDb"] - 2.0,
              f"Tranche's compressor holds a loud sound ({pressed['inputDb']:.1f} -> {pressed['outputDb']:.1f} dB)")
        need({"cmd": "remove_device", "track": track, "nodeId": tranche}, "remove Tranche")

        # ---- Finition ----
        finition = node_of(need({"cmd": "insert_device", "track": track, "key": "finition"}, "finition"), "finition")
        check(len(factory(finition)) >= 8, "Finition's factory presets")
        load(finition, "Club fort")
        loud = chain(440.0, 0.3, 1.0)
        check(loud["peakDb"] <= -0.25 and loud["outputDb"] > loud["inputDb"] + 3.0,
              f"Finition raises the sound under its ceiling (peak {loud['peakDb']:.2f} dB, {loud['inputDb']:.1f} -> {loud['outputDb']:.1f} dB)")
        need({"cmd": "remove_device", "track": track, "nodeId": finition}, "remove Finition")

        # ---- Halo ----
        halo = node_of(need({"cmd": "insert_device", "track": track, "key": "halo"}, "halo"), "halo")
        check(len(factory(halo)) >= 10, "Halo's factory presets")
        load(halo, "Grande salle")
        hall = chain(440.0, 0.3, 1.5)
        check(hall["peakDb"] < 3.0 and abs(hall["outputDb"] - hall["inputDb"]) < 6.0,
              f"Halo plays, its level near the sound's ({hall['inputDb']:.1f} -> {hall['outputDb']:.1f} dB)")
        need({"cmd": "remove_device", "track": track, "nodeId": halo}, "remove Halo")

        # ---- Écho Bande ----
        echo = node_of(need({"cmd": "insert_device", "track": track, "key": "echo-bande"}, "echo"), "echo-bande")
        check(len(factory(echo)) >= 8, "Écho Bande's factory presets")
        load(echo, "Slapback")
        slap = chain(440.0, 0.2)
        check(slap["outputDb"] > -40.0 and slap["peakDb"] < 3.0, f"the slapback plays ({slap['outputDb']:.1f} dB)")
        load(echo, "Emballement")
        wild = chain(440.0, 0.3, 3.0)
        check(wild["peakDb"] < 12.0, f"a runaway echo stays bounded (peak {wild['peakDb']:.1f} dB)")

        # ---- A project keeps them ----
        load(echo, "Écho dub")
        project = scratch / "Mixage.cydaw"
        need({"cmd": "save_project", "path": str(project)}, "save")
        need({"cmd": "new_project"}, "new")
        need({"cmd": "open_project", "path": str(project)}, "open")
        kept = {}
        for _ in range(150):
            answer = request({"cmd": "list_parameters", "track": track})
            if answer.get("ok"):
                kept = {p["key"].split(".", 2)[-1]: p["value"] for p in answer["result"]["parameters"]}
                break
            time.sleep(0.2)
        check(kept.get("echoband.mode") == 3.0 and kept.get("echoband.time") == 300.0, "a saved project keeps Écho Bande")
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=30)
        except Exception:
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    if failures:
        print("FAILED: " + "; ".join(failures))
        return 1
    print("all mix effects checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
