"""Factory instruments made from the SFZ banks (Karoryfer, Virtuosity Drums, jSteelDrum...).

    python tools/factory_content/build_sfz_instruments.py --exe "<CyDAW 2.exe>"
           [--banks DIR] [--out content/factory] [--only key,key] [--port 47837]

Each instrument below is one SFZ program of a downloaded bank, read by CyDAW itself (a silent
session, its control port's sampler_file) into a sampler map whose files are "bank:<id>/<path>":
what its controllers do kept as rules played as they move (their starting values, names and
curves with the map), its keyswitches, its release and legato regions. The map is written as <out>/Instruments/<folder>/<folder>.cysampler
and listed in <out>/catalogue.json (after the instruments build_factory_content.py makes), with
the card settings and envelope it plays with; LICENCE.txt credits the banks.
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

from build_factory_content import CATEGORIES, note_name, write_licence  # noqa: E402
from fetch_banks import default_destination  # noqa: E402
import factory_voicing  # noqa: E402

MELODIC = {"velocity-db": 18}
DRUMS = {"velocity-db": 10, "one-shot": 1}


def spec(key, bank, sfz, folder, fr, en, category, release=300, settings=None, melodic=True, controllers=None):
    """controllers: the SFZ's controllers at other starting values ({"100": 127})."""
    return {"key": key, "bank": bank, "sfz": sfz, "folder": folder, "names": {"fr": fr, "en": en},
            "category": category, "envelope": {"attack": 1, "release": release},
            "settings": settings if settings is not None else (MELODIC if melodic else DRUMS), "melodic": melodic,
            "controllers": controllers or {}}


SPECS = [
    # Guitars.
    spec("guitar-epiphone", "emilyguitar", "emily_basic.sfz", "Epiphone Guitar",
         "Guitare électrique Epiphone", "Epiphone electric guitar", "guitars", 400),
    spec("guitar-archtop", "shinyguitar", "Programs/main.sfz", "Archtop Guitar",
         "Guitare archtop (électrique)", "Archtop guitar (electric)", "guitars", 400),
    # Its "Blend" up (126: the acoustic regions stop below 127): the microphone, not the pickup.
    spec("guitar-archtop-acoustic", "shinyguitar", "Programs/main.sfz", "Archtop Guitar Acoustic",
         "Guitare archtop (acoustique)", "Archtop guitar (acoustic)", "guitars", 400, controllers={"100": 126}),
    spec("guitar-gretsch", "black-and-green-guitars", "Programs/04-green_twang.sfz", "Gretsch Guitar",
         "Guitare Gretsch Anniversary", "Gretsch Anniversary guitar", "guitars", 400),
    spec("guitar-hofner", "black-and-green-guitars", "Programs/07-black_twang.sfz", "Hofner Guitar",
         "Guitare Hofner Club", "Hofner Club guitar", "guitars", 400),
    spec("banjo", "ganjo", "ganjo.sfz", "Six-String Banjo", "Banjo six cordes", "Six-string banjo", "guitars", 500),
    # Basses.
    spec("bass-jazz", "growlybass", "growlybass_clean.sfz", "Jazz Bass", "Basse Jazz Bass", "Jazz bass", "basses", 250),
    spec("bass-dark-black", "black-and-blue-basses", "Programs/05-darkblack_pluck.sfz", "Dark Black Bass",
         "Basse électrique « Dark Black »", "\"Dark Black\" electric bass", "basses", 250),
    spec("bass-baby-blue", "black-and-blue-basses", "Programs/04-babyblue_warm.sfz", "Baby Blue Bass",
         "Basse électrique « Baby Blue »", "\"Baby Blue\" electric bass", "basses", 250),
    spec("bass-fashion", "fashionbass", "fashionbass_clean.sfz", "Fashionbass",
         "Basse Killer KB-Simmony", "Killer KB-Simmony bass", "basses", 250),
    spec("bass-swag", "swagbass", "swagbass_clean.sfz", "Swagbass", "Basse Swagbass", "Swagbass", "basses", 250),
    spec("bass-six", "pastabass", "fetuccine.sfz", "Bass VI", "Basse six cordes (Bass VI)", "Bass VI", "basses", 250),
    spec("bass-big-little", "big-little-bass", "Programs/01-big_little_pluck.sfz", "Big Little Bass",
         "Basse acoustique Big Little", "Big Little acoustic bass", "basses", 300),
    spec("upright-bass-jazz", "meatbass", "Programs/04_pizz.sfz", "Jazz Double Bass",
         "Contrebasse jazz (pizzicato)", "Jazz double bass (pizzicato)", "basses", 350),
    spec("upright-bass-hushed", "sneakybass", "Programs/02-sneakybass_pluck.sfz", "Hushed Double Bass",
         "Contrebasse feutrée", "Hushed double bass", "basses", 350),
    spec("upright-bass-electric", "ergo", "ergo_pizz.sfz", "Ergo Electric Upright Bass",
         "Contrebasse électrique Ergo", "Ergo electric upright bass", "basses", 300),
    # Bowed and plucked strings.
    spec("cello-solo", "bigcat-cello", "Programs/01- Bowed (velocity layer).sfz", "Solo Cello",
         "Violoncelle solo (archet)", "Solo cello (bowed)", "strings", 400),
    spec("cello-solo-pizz", "bigcat-cello", "Programs/03- Plucked.sfz", "Solo Cello Pizzicato",
         "Violoncelle solo pizzicato", "Solo cello pizzicato", "strings", 450),
    spec("double-bass-rubner", "meatbass", "Programs/03_arco_5vel.sfz", "Rubner Double Bass",
         "Contrebasse Otto Rubner (archet)", "Otto Rubner double bass (bowed)", "strings", 400),
    # Winds.
    spec("baritone-sax", "bear-sax", "Programs/2-solo-poly.sfz", "Baritone Saxophone",
         "Saxophone baryton", "Baritone saxophone", "woodwinds", 250),
    spec("alto-sax", "weresax", "Programs/Sax.sfz", "Alto Saxophone", "Saxophone alto", "Alto saxophone", "woodwinds", 250),
    spec("tuba-war", "war-tuba", "Programs/2-solo-poly.sfz", "War Tuba", "Tuba (War Tuba)", "Tuba (War Tuba)", "brass", 300),
    # Keys.
    spec("piano-osiris", "osiris-piano", "Programs/01-natural.sfz", "Osiris Piano",
         "Piano Osiris (una corda)", "Osiris piano (una corda)", "pianos", 400),
    spec("piano-scary", "scarypiano", "Programs/Scarypiano.sfz", "Scarypiano",
         "Piano hanté (Scarypiano)", "Haunted piano (Scarypiano)", "pianos", 500),
    # Folk.
    spec("erhu", "erhu", "Programs/02-erhu_long.sfz", "Erhu", "Erhu (vièle chinoise)", "Erhu (Chinese fiddle)", "folk", 300),
    spec("cithara", "cithara-barbarica", "Programs/02-cithara_barbarica_finger.sfz", "Cithara Barbarica",
         "Cithara barbarica (lyre médiévale)", "Cithara barbarica (medieval lyre)", "folk", 1500),
    spec("zither-hungarian", "hungarian-zither", "Programs/hungarian_zither_mel_strings_poly.sfz", "Hungarian Zither",
         "Cithare hongroise", "Hungarian zither", "folk", 1500),
    spec("tagelharpa", "horse-pulse", "Programs/03-pluck.sfz", "Bass Tagelharpa",
         "Talharpa basse (pincée)", "Bass tagelharpa (plucked)", "folk", 800),
    spec("steel-drum", "steel-drum", "jSteelDrum-noxfade.sfz", "Steel Drum", "Steel drum", "Steel drum", "mallets", 1200,
         settings={"velocity-db": 12}),
    # Drums and percussion.
    spec("drums-jazz", "virtuosity-drums", "Programs/01-basic-kit.sfz", "Jazz Kit",
         "Batterie jazz (Virtuosity)", "Jazz kit (Virtuosity)", "drums", melodic=False),
    spec("drums-rusty", "big-rusty-drums", "Programs/02-basic.sfz", "Big Rusty Kit",
         "Batterie Big Rusty", "Big Rusty kit", "drums", melodic=False),
    spec("drums-swirly", "swirly-drums", "Programs/Basic_kit.sfz", "Swirly Kit",
         "Batterie Swirly", "Swirly kit", "drums", melodic=False),
    spec("drums-unruly-sticks", "unruly-drums", "Programs/01-kit-sticks.sfz", "Unruly Kit Sticks",
         "Batterie Unruly (baguettes)", "Unruly kit (sticks)", "drums", melodic=False),
    spec("drums-unruly-brushes", "unruly-drums", "Programs/02-kit-brushes.sfz", "Unruly Kit Brushes",
         "Batterie Unruly (balais)", "Unruly kit (brushes)", "drums", melodic=False),
    spec("drums-seventies", "gogodze-phu-2", "Programs/Kit_b.sfz", "Seventies Kit",
         "Batterie seventies (Gogodze Phu)", "Seventies kit (Gogodze Phu)", "drums", melodic=False),
    spec("bobobo", "gogodze-phu-1", "Programs/Bobobo.sfz", "Bobobo Drums",
         "Tambours bobobo (Ghana)", "Bobobo drums (Ghana)", "drums", melodic=False),
    spec("body-percussion", "body-percussion", "Programs/body.sfz", "Body Percussion",
         "Percussions corporelles", "Body percussion", "drums", melodic=False),
    # Sampled synths and voices.
    spec("caveman-cosmonaut", "caveman-cosmonaut", "Programs/main.sfz", "Caveman Cosmonaut",
         "Caveman Cosmonaut (orgue-synthé)", "Caveman Cosmonaut (organ synth)", "synths", 300),
    spec("cowsynth", "cowsynth", "cowsynth_lonely_pad.sfz", "Cowsynth",
         "Cowsynth (cornemuse-synthé)", "Cowsynth (bagpipe synth)", "synths", 600),
    spec("string-cyborgs", "string-cyborgs", "Programs/zinc_one_voice.sfz", "String Cyborgs",
         "String Cyborgs (cordes-synthé)", "String Cyborgs (string synth)", "synths", 500),
    spec("voice-a", "legato-vocal", "Programs/01-no_legato_polyphonic_only_no_vibrato_a.sfz", "Voice A",
         "Voix chantée « a »", "Sung voice \"a\"", "voices", 300),
]


class Session:
    """A silent CyDAW 2, its banks folder the one given."""

    def __init__(self, exe: Path, port: int, banks: Path):
        self.port = port
        self.scratch = Path(tempfile.mkdtemp(prefix="cydaw-sfz-"))
        locations = self.scratch / "Locations.json"
        locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {"banks": str(banks)}}),
                             encoding="utf-8")
        environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
        self.process = subprocess.Popen([str(exe), f"--control-port={port}", "--silent", "--window-size=1200x800"],
                                        cwd=str(exe.parent), env=environment, stdin=subprocess.DEVNULL,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(200):
            try:
                self.request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        self.request({"cmd": "set_master_gain", "gainDb": -60, "muted": True})

    def request(self, command: dict) -> dict:
        with socket.create_connection(("127.0.0.1", self.port), timeout=5.0) as connection:
            connection.settimeout(300)
            connection.sendall((json.dumps(command) + "\n").encode("utf-8"))
            data = b""
            while not data.endswith(b"\n"):
                chunk = connection.recv(1 << 22)
                if not chunk:
                    break
                data += chunk
        return json.loads(data.decode("utf-8"))

    def close(self) -> None:
        try:
            self.request({"cmd": "quit"})
            self.process.wait(timeout=20)
        except (OSError, subprocess.TimeoutExpired):
            self.process.kill()


def map_text(sampler: dict) -> str:
    """The map as JSON, a zone per line (thousands of zones stay small and readable)."""
    compact = {"separators": (",", ":"), "ensure_ascii": False}
    head = {key: value for key, value in sampler.items() if key != "zones"}
    lines = [json.dumps(zone, **compact) for zone in sampler["zones"]]
    body = json.dumps(head, **compact)[:-1]
    return body + ',"zones":[\n' + ",\n".join(lines) + "\n]}\n"


def build(session: Session, item: dict, banks: Path, out: Path) -> dict:
    answer = session.request({"cmd": "sampler_file", "file": str(banks / item["bank"] / item["sfz"]),
                              "controllers": item["controllers"]})
    if not answer.get("ok"):
        raise RuntimeError(f"{item['key']}: {answer.get('error')}")
    mapping = answer["result"]["map"]
    zones = mapping["zones"]
    prefix = f"bank:{item['bank']}/"
    strays = [zone["file"] for zone in zones if not zone["file"].startswith(prefix)]
    if strays:
        raise RuntimeError(f"{item['key']}: files outside the bank: {strays[:3]}")
    absent = sorted({zone["file"] for zone in zones if not (banks / zone["file"][len("bank:"):]).exists()})
    if absent:
        raise RuntimeError(f"{item['key']}: {len(absent)} files not in the bank: {absent[:3]}")
    settings = dict(mapping.get("settings") or {})
    settings.update(item["settings"])
    sampler = {"format": "cydaw-sampler", "version": 1, "name": item["names"]["en"], "zones": zones,
               "settings": settings}
    # What moves it: its controllers (where they start, their names), curves, keyswitches.
    for extra in ("controllers", "controls", "curves", "keyswitches"):
        if extra in mapping:
            sampler[extra] = mapping[extra]
    folder = out / "Instruments" / item["folder"]
    folder.mkdir(parents=True, exist_ok=True)
    (folder / f"{item['folder']}.cysampler").write_text(map_text(sampler), encoding="utf-8")
    if item["melodic"]:
        # The notes played: not an effect mapped above them (a cello's harmonics), which has fewer zones.
        counts: dict[int, int] = {}
        for zone in zones:
            counts[zone["root"]] = counts.get(zone["root"], 0) + 1
        typical = sorted(counts.values())[len(counts) // 2]
        roots = sorted(root for root, count in counts.items() if count * 2 >= typical)
    else:
        roots = sorted({key for zone in zones for key in zone.get("keys", [zone["root"], zone["root"]])})
    files = {zone["file"] for zone in zones}
    size = sum((banks / f[len("bank:"):]).stat().st_size for f in files if (banks / f[len("bank:"):]).exists())
    return {
        "key": item["key"],
        "names": item["names"],
        "category": item["category"],
        "map": f"Instruments/{item['folder']}/{item['folder']}.cysampler",
        "settings": settings,
        "envelope": item["envelope"],
        "source": item["bank"],
        "bank": item["bank"],
        "sfz": f"{item['bank']}/{item['sfz']}",
        "melodic": item["melodic"],
        "licence": "Unlicense" if item["bank"] == "steel-drum" else "CC0-1.0",
        "range": [note_name(roots[0]), note_name(roots[-1])],
        "report": {"key": item["key"], "zones": len(zones), "files": len(files), "megabytes": round(size / 1e6, 1),
                   "description": answer["result"]["description"]},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path,
                        default=ROOT / "build" / "agent-graph" / "CyDAW2_artefacts" / "Release" / "CyDAW 2.exe")
    parser.add_argument("--banks", type=Path, default=default_destination())
    parser.add_argument("--out", type=Path, default=ROOT / "content" / "factory")
    parser.add_argument("--only", default="")
    parser.add_argument("--port", type=int, default=47837)
    args = parser.parse_args()
    only = {key for key in args.only.split(",") if key}
    catalogue_path = args.out / "catalogue.json"
    catalogue = json.loads(catalogue_path.read_text(encoding="utf-8"))
    entries = {entry["key"]: entry for entry in catalogue["instruments"]}
    session = Session(args.exe.resolve(), args.port, args.banks)
    try:
        for item in SPECS:
            if only and item["key"] not in only:
                continue
            entry = build(session, item, args.banks, args.out)
            # Its sound model, made by build_factory_models.py, stays listed.
            for kept in ("model", "modelSettings"):
                if kept in entries.get(item["key"], {}):
                    entry[kept] = entries[item["key"]][kept]
            entries[item["key"]] = entry
            print(json.dumps(entry["report"], ensure_ascii=False), flush=True)
    finally:
        session.close()
    # Those of build_factory_content.py first, in their order, then these in theirs.
    ours = {item["key"] for item in SPECS}
    ordered = [entries[entry["key"]] for entry in catalogue["instruments"] if entry["key"] not in ours]
    ordered += [entries[item["key"]] for item in SPECS if item["key"] in entries]
    catalogue = {"format": "cydaw-factory-catalogue", "version": 1, "categories": CATEGORIES, "instruments": ordered}
    # One note at a time for the winds, brass, bowed solo strings, the voice.
    factory_voicing.apply(catalogue)
    catalogue_path.write_text(json.dumps(catalogue, indent=1, ensure_ascii=False), encoding="utf-8")
    write_licence(args.out, catalogue)
    return 0


if __name__ == "__main__":
    sys.exit(main())
