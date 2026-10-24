"""The list of downloadable sound banks CyDAW ships (content/factory/banks.json and BankLists/<id>.json).

    python tools/factory_content/build_bank_registry.py [--banks DIR] [--out content/factory]

From the banks fetch_banks.py downloaded (each folder's bank.json), for every bank whose licence is
confirmed: where it comes from (the GitHub repository and the commit its files are read at, from
`git ls-remote`, so a later change upstream cannot change what CyDAW downloads), its licence, its
author, a word on what it holds, and each of its files with its size (as on disk: a file kept in Git
LFS is listed in the repository's tree by its pointer) and whether Git LFS keeps it. CyDAW downloads
the samples from there, into the banks folder chosen in Options › Emplacements.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fetch_banks import BANKS, MAIN, default_destination  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
LICENCE_URLS = {
    "CC0-1.0": "https://creativecommons.org/publicdomain/zero/1.0/",
    "Unlicense": "https://unlicense.org/",
}
KARORYFER = "Karoryfer Samples"

# Titles, authors and what each bank holds (fr, en). Authors as the banks themselves name them; empty
# when they do not.
INFO = {
    "vcsl": ("Versilian Community Sample Library", "Versilian Studios LLC",
             "Pianos, clavecins, harpes, orgues, percussions mélodiques et du monde, flûtes à bec…",
             "Pianos, harpsichords, harps, organs, tuned and world percussion, recorders…"),
    "vsco": ("Versilian Studios Chamber Orchestra 2, Community Edition", "Versilian Studios LLC",
             "Orchestre de chambre : cordes solo et en section, cuivres, bois, percussions, orgue, pianos droits.",
             "Chamber orchestra: solo and section strings, brass, woodwinds, percussion, organ, upright pianos."),
    "virtuosity-drums": ("Virtuosity Drums", "Versilian Studios, Karoryfer Samples",
                         "Batterie jazz complète, six micros, avec ses articulations.",
                         "A complete jazz drum kit, six microphones, with its articulations."),
    "osiris-piano": ("Osiris Piano", "", "Piano joué una corda, avec ou sans pédale de sustain, bruit réduit ou non.", "Piano played una corda, with or without the sustain pedal, noise-reduced or not."),
    "steel-drum": ("jSteelDrum", "J. Learman", "Steel drum en do, de C4 à B5, cinq couches de vélocité.",
                   "C steel drum, C4 to B5, five velocity layers."),
    "erhu": ("Erhu", "", "Erhu (vièle chinoise) : tenu, court, sul tasto.", "Erhu (Chinese fiddle): long, short, sul tasto."),
    "body-percussion": ("Body Percussion", "", "Percussions corporelles : frappes, claquements, round robins.",
                        "Body percussion: hits, claps, round robins."),
    "cithara-barbarica": ("Cithara Barbarica", "", "Lyre médiévale à dix cordes métalliques, pincées de plusieurs façons.",
                          "Medieval ten-string lyre with metal strings, plucked several ways."),
    "ganjo": ("Ganjo", "itsclipping", "Banjo à six cordes (guitare-banjo).", "Six-string banjo (guitar banjo)."),
    "hungarian-zither": ("Hungarian Zither", "", "Cithare hongroise en sol : grattée et mélodique.",
                         "Hungarian zither in G: strummed and melody."),
    "legato-vocal": ("Legato Vocal", KARORYFER, "Voix chantée « a » en legato.", "A sung \"a\" vowel, legato."),
    "stickmusterstuch": ("Zamenhof Stickmusterstuch Error", "", "Sons expérimentaux.", "Experimental sounds."),
    "smolken-double-bass": ("D. Smolken's Double Bass", "D. Smolken",
                            "Contrebasse Otto Rubner de 1958 : arco et pizzicato.", "1958 Otto Rubner double bass: arco and pizzicato."),
    "bigcat-cello": ("Karoryfer x bigcat Cello", KARORYFER, "Violoncelle solo, joué par Kamila Borowiak.",
                     "Solo cello, played by Kamila Borowiak."),
    "horse-pulse": ("Horse Pulse", KARORYFER, "Talharpa basse jouée en pizzicato.", "Bass tagelharpa played pizzicato."),
    "hat-with-the-phat": ("The Hat With The Phat", KARORYFER, "Charleston géant fait de deux cymbales ride.",
                          "An oversized hi-hat made of two ride cymbals."),
    "merry-orks": ("272 Merry Orks", KARORYFER, "Voix death metal féminines.", "Female death metal vocals."),
    "bear-sax": ("Bear Sax", KARORYFER, "Saxophone baryton Conn de 1926.", "1926 Conn baritone saxophone."),
    "big-little-bass": ("Big Little Bass", KARORYFER, "Très grosse basse acoustique jouée dans l'aigu.",
                        "A very big acoustic bass played high up."),
    "big-rusty-drums": ("Big Rusty Drums", KARORYFER, "Batterie.", "Drum kit."),
    "black-and-blue-basses": ("Black And Blue Basses", KARORYFER, "Basses électriques.", "Electric basses."),
    "black-and-green-guitars": ("Black And Green Guitars", KARORYFER, "Guitares Gretsch Anniversary verte et Hofner Club noire.", "A green Gretsch Anniversary and a black Hofner Club."),
    "caveman-cosmonaut": ("Caveman Cosmonaut", KARORYFER, "Orgue à transistors Unitra B-11 devenu synthétiseur analogique virtuel.", "A Unitra B-11 transistor organ turned into a virtual analog synthesizer."),
    "cowsynth": ("Cowsynth", KARORYFER, "Réplique de cornemuse médiévale, en oscillateurs de synthétiseur.", "A replica of medieval bagpipes, as synthesizer oscillators."),
    "emilyguitar": ("Emilyguitar", KARORYFER, "Guitare électrique Epiphone Emily the Strange.", "Epiphone Emily the Strange electric guitar."),
    "ergo": ("Ergo", KARORYFER, "Contrebasse électrique Ergo, jouée par D. Smolken.", "Ergo electric upright bass played by D. Smolken."),
    "fashionbass": ("Fashionbass", KARORYFER, "Basse électrique Killer KB-Simmony.", "Killer KB-Simmony bass guitar."),
    "frankensnare": ("Frankensnare", KARORYFER, "Caisses claires.", "Snare drums."),
    "gogodze-phu-1": ("Gogodze Phu Vol. I", KARORYFER, "Cinq tambours bobobo du Ghana et un cajón.", "Five bobobo drums from Ghana and a cajón."),
    "gogodze-phu-2": ("Gogodze Phu Vol. II", KARORYFER, "Batterie : grosse caisse, caisse claire, trois toms, charleston.", "Drum kit: kick, snare, three toms, hi-hat."),
    "growlybass": ("Growlybass", KARORYFER, "Basse Squier Jazz Bass.", "Squier Jazz Bass."),
    "meatbass": ("Meatbass", KARORYFER, "Contrebasse Otto Rubner de 1958, jouée par D. Smolken.", "1958 Otto Rubner double bass played by D. Smolken."),
    "pastabass": ("Pastabass", KARORYFER, "Basse six cordes Squier Bass VI.", "Squier Bass VI six-string bass."),
    "scarypiano": ("Scarypiano", KARORYFER, "Piano (échantillons de l'Université d'Iowa) déformé pour Halloween.", "A piano (University of Iowa samples) twisted for Halloween."),
    "shinyguitar": ("Shinyguitar", KARORYFER, "Guitare archtop.", "Archtop guitar."),
    "sneakybass": ("Sneakybass", KARORYFER, "La contrebasse Otto Rubner en pizzicato, très doucement.", "The Otto Rubner double bass played pizzicato, very quietly."),
    "squidpipes": ("Squidpipes", KARORYFER, "Cornemuse dont la poche est un manteau de calmar.", "Bagpipes with a squid's mantle as the bag."),
    "string-cyborgs": ("String Cyborgs", KARORYFER, "Cordes frottées, en oscillateurs d'un synthétiseur soustractif.", "Bowed strings as the oscillators of a subtractive synthesizer."),
    "swagbass": ("Swagbass", KARORYFER, "Basse électrique à finition Louis Vuitton.", "Bass guitar with a Louis Vuitton finish."),
    "swirly-drums": ("Swirly Drums", KARORYFER, "Batterie.", "Drum kit."),
    "unruly-drums": ("Unruly Drums", KARORYFER, "Batterie.", "Drum kit."),
    "war-tuba": ("War Tuba", KARORYFER, "Tuba joué par Jakub Lewicki.", "Tuba played by Jakub Lewicki."),
    "weresax": ("Weresax", KARORYFER, "Saxophone alto.", "Alto saxophone."),
}


def commit_of(repository: str, branch: str) -> str:
    """The commit the branch is at, by git (not the GitHub API, which allows 60 calls an hour)."""
    completed = subprocess.run(["git", "ls-remote", f"https://github.com/{repository}", f"refs/heads/{branch}"],
                               capture_output=True, text=True, timeout=60)
    line = completed.stdout.strip().split()
    if completed.returncode != 0 or not line:
        raise RuntimeError(f"{repository}: git ls-remote failed: {completed.stderr.strip()}")
    return line[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--banks", type=Path, default=default_destination())
    parser.add_argument("--out", type=Path, default=ROOT / "content" / "factory")
    args = parser.parse_args()
    lists = args.out / "BankLists"
    lists.mkdir(parents=True, exist_ok=True)
    registry = []
    for bank_id, repository, licence, confirmed in BANKS:
        record_path = args.banks / bank_id / "bank.json"
        if not confirmed or not record_path.exists():
            print(json.dumps({"bank": bank_id, "skipped": "licence not confirmed" if not confirmed else "not downloaded"}))
            continue
        record = json.loads(record_path.read_text(encoding="utf-8"))
        if record.get("failures"):
            print(json.dumps({"bank": bank_id, "skipped": f"{len(record['failures'])} failed files"}))
            continue
        folder = args.banks / bank_id
        files = []
        for item in record["files"]:
            path = folder / item["path"]
            if not path.exists():
                continue
            size = path.stat().st_size
            # Git LFS: the tree gives the pointer's size, the disk the file's.
            entry = [item["path"], size]
            if size != item["size"]:
                entry.append(1)
            files.append(entry)
        branch = record.get("branch") or MAIN.get(bank_id, "master")
        title, author, french, english = INFO.get(bank_id, (bank_id, "", "", ""))
        list_name = f"BankLists/{bank_id}.json"
        # One file a line: the lists stay readable and their changes small.
        body = ",\n".join(json.dumps(entry, ensure_ascii=False) for entry in files)
        (args.out / list_name).write_text('{"files": [\n' + body + "\n]}\n", encoding="utf-8")
        registry.append({
            "id": bank_id,
            "title": title,
            "author": author,
            "licence": licence,
            "licenceUrl": LICENCE_URLS.get(licence, ""),
            "homepage": f"https://github.com/{repository}",
            "repository": repository,
            # The commit the files were fetched at (an older download: the
            # branch's now, as before).
            "commit": record.get("commit") or commit_of(repository, branch),
            "description": {"fr": french, "en": english},
            "list": list_name,
            "count": len(files),
            "bytes": sum(entry[1] for entry in files),
        })
        print(json.dumps({"bank": bank_id, "files": len(files), "megabytes": round(registry[-1]["bytes"] / 1e6, 1),
                          "lfs": sum(1 for entry in files if len(entry) > 2)}), flush=True)
    # The banks CyDAW makes itself (build_electro_banks.py) are kept as they are.
    previous = args.out / "banks.json"
    if previous.exists():
        registry += [bank for bank in json.loads(previous.read_text(encoding="utf-8")).get("banks", [])
                     if bank.get("generator")]
    document = {"format": "cydaw-banks", "version": 1, "banks": registry}
    (args.out / "banks.json").write_text(json.dumps(document, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"banks": len(registry), "gigabytes": round(sum(b["bytes"] for b in registry) / 1e9, 2)}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
