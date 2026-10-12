"""Download whole sound banks (CC0, public domain, Unlicense) from GitHub, to make instruments and models.

    python tools/factory_content/fetch_banks.py [--dest DIR] [--only id,id] [--jobs N] [--list]

Every bank is a GitHub repository listed in BANKS: its tree is read once through the GitHub API, then
each file worth keeping (audio, SFZ mappings, licence, read-me, manual) comes from
raw.githubusercontent.com (media.githubusercontent.com for a file kept in Git LFS). A file already
present with the right size is skipped, so an interrupted run resumes. Each bank gets
<dest>/<id>/bank.json: where it comes from (repository, branch, tree), its licence as declared, and its
files with their sizes; <dest>/banks.json lists them all.

The destination is --dest, else CYDAW_BANKS_DIR, else the banks folder chosen in CyDAW (Options ›
Emplacements), else %LOCALAPPDATA%/CyDAW/Banks. Banks whose licence
is not confirmed by their authors are fetched for evaluation but marked so ("licenceConfirmed": false):
CyDAW offers none of them for download until it is.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import sys
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path

# id, repository, licence (SPDX), confirmed by the authors (or the repository's licence file)
BANKS = [
    ("vcsl", "sgossner/VCSL", "CC0-1.0", True),
    ("vsco", "sgossner/VSCO-2-CE", "CC0-1.0", True),
    ("virtuosity-drums", "sfzinstruments/virtuosity_drums", "CC0-1.0", True),
    ("sm-drums", "sfzinstruments/SMDrums", "public-domain", False),
    ("osiris-piano", "sfzinstruments/Osiris_Piano", "CC0-1.0", True),
    ("steel-drum", "sfzinstruments/jlearman.SteelDrum", "Unlicense", True),
    ("erhu", "sfzinstruments/aliexpress-erhu", "CC0-1.0", True),
    ("body-percussion", "sfzinstruments/body_percussion", "CC0-1.0", True),
    ("cithara-barbarica", "sfzinstruments/cithara-barbarica", "CC0-1.0", True),
    ("ganjo", "sfzinstruments/ganjo", "CC0-1.0", True),
    ("hungarian-zither", "sfzinstruments/hungarian_zither", "CC0-1.0", True),
    ("legato-vocal", "sfzinstruments/legato_vocal_tutorial", "CC0-1.0", True),
    ("stickmusterstuch", "sfzinstruments/zamenhof_stickmusterstuch_error", "CC0-1.0", True),
    ("smolken-double-bass", "sfzinstruments/dsmolken.double-bass", "CC0-1.0", True),
    ("bigcat-cello", "sfzinstruments/karoryfer-bigcat.cello", "CC0-1.0", True),
    ("horse-pulse", "sfzinstruments/Karoryfer.HorsePulse", "CC0-1.0", True),
    ("hat-with-the-phat", "sfzinstruments/Karoryfer.TheHatWithThePhat", "CC0-1.0", True),
    ("merry-orks", "sfzinstruments/karoryfer.272-merry-orks", "CC0-1.0", True),
    ("bear-sax", "sfzinstruments/karoryfer.bear-sax", "CC0-1.0", True),
    ("big-little-bass", "sfzinstruments/karoryfer.big-little-bass", "CC0-1.0", True),
    ("big-rusty-drums", "sfzinstruments/karoryfer.big-rusty-drums", "CC0-1.0", True),
    ("black-and-blue-basses", "sfzinstruments/karoryfer.black-and-blue-basses", "CC0-1.0", True),
    ("black-and-green-guitars", "sfzinstruments/karoryfer.black-and-green-guitars", "CC0-1.0", True),
    ("caveman-cosmonaut", "sfzinstruments/karoryfer.caveman-cosmonaut", "CC0-1.0", True),
    ("cowsynth", "sfzinstruments/karoryfer.cowsynth", "CC0-1.0", True),
    ("emilyguitar", "sfzinstruments/karoryfer.emilyguitar", "CC0-1.0", True),
    ("ergo", "sfzinstruments/karoryfer.ergo", "CC0-1.0", True),
    ("fashionbass", "sfzinstruments/karoryfer.fashionbass", "CC0-1.0", True),
    ("frankensnare", "sfzinstruments/karoryfer.frankensnare", "CC0-1.0", True),
    ("gogodze-phu-1", "sfzinstruments/karoryfer.gogodze-phu-vol-i", "CC0-1.0", True),
    ("gogodze-phu-2", "sfzinstruments/karoryfer.gogodze-phu-vol-ii", "CC0-1.0", True),
    ("growlybass", "sfzinstruments/karoryfer.growlybass", "CC0-1.0", True),
    ("meatbass", "sfzinstruments/karoryfer.meatbass", "CC0-1.0", True),
    ("pastabass", "sfzinstruments/karoryfer.pastabass", "CC0-1.0", True),
    ("scarypiano", "sfzinstruments/karoryfer.scarypiano", "CC0-1.0", True),
    ("shinyguitar", "sfzinstruments/karoryfer.shinyguitar", "CC0-1.0", True),
    ("sneakybass", "sfzinstruments/karoryfer.sneakybass", "CC0-1.0", True),
    ("squidpipes", "sfzinstruments/karoryfer.squidpipes", "CC0-1.0", True),
    ("string-cyborgs", "sfzinstruments/karoryfer.string-cyborgs", "CC0-1.0", True),
    ("swagbass", "sfzinstruments/karoryfer.swagbass", "CC0-1.0", True),
    ("swirly-drums", "sfzinstruments/karoryfer.swirly-drums", "CC0-1.0", True),
    ("unruly-drums", "sfzinstruments/karoryfer.unruly-drums", "CC0-1.0", True),
    ("war-tuba", "sfzinstruments/karoryfer.war-tuba", "CC0-1.0", True),
    ("weresax", "sfzinstruments/karoryfer.weresax", "CC0-1.0", True),
]

# Their default branches, so that only the trees cost a call to the GitHub API (60 an hour without a
# token): a tree already read today is read from its file.
MAIN = {"virtuosity-drums": "master", "sm-drums": "main", "osiris-piano": "main", "steel-drum": "main", "erhu": "main",
        "body-percussion": "main", "cithara-barbarica": "main", "ganjo": "main", "hungarian-zither": "main",
        "legato-vocal": "main", "stickmusterstuch": "main", "horse-pulse": "main", "big-little-bass": "main",
        "big-rusty-drums": "main", "black-and-blue-basses": "main", "black-and-green-guitars": "main",
        "frankensnare": "main", "sneakybass": "main", "swirly-drums": "main", "unruly-drums": "main"}

# The two Versilian banks keep the folder names fetch_sources.py gave them.
FOLDER = {"vcsl": "vcsl", "vsco": "vsco"}
KEPT = (".wav", ".flac", ".ogg", ".aif", ".aiff", ".sfz", ".sfzh", ".txt", ".md", ".pdf")
SKIPPED_DIRECTORIES = ("GUI/", "Manual, Track Templates")
LFS = b"version https://git-lfs"


def default_destination() -> Path:
    """Where CyDAW keeps the banks: CYDAW_BANKS_DIR, else the folder chosen in CyDAW (Options ›
    Emplacements, %APPDATA%/CyDAW/Locations.json), else %LOCALAPPDATA%/CyDAW/Banks."""
    if os.environ.get("CYDAW_BANKS_DIR"):
        return Path(os.environ["CYDAW_BANKS_DIR"])
    settings = Path(os.environ.get("APPDATA", Path.home())) / "CyDAW" / "Locations.json"
    try:
        chosen = json.loads(settings.read_text(encoding="utf-8")).get("folders", {}).get("banks")
        if chosen:
            return Path(chosen)
    except (OSError, ValueError):
        pass
    return Path(os.environ.get("LOCALAPPDATA", Path.home())) / "CyDAW" / "Banks"


def api(url: str) -> dict:
    request = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json", "User-Agent": "CyDAW-banks"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read().decode("utf-8"))


def kept(path: str, size: int) -> bool:
    name = path.rsplit("/", 1)[-1].lower()
    if any(path.startswith(folder) or f"/{folder}" in path for folder in SKIPPED_DIRECTORIES):
        return False
    if name.startswith(("license", "licence", "readme", "copying")):
        return True
    if name.endswith(".pdf"):
        return size < 30_000_000
    return name.endswith(KEPT)


def fetch(repository: str, branch: str, path: str, target: Path, size: int) -> int:
    """The file, unless it is there already; the bytes written."""
    if target.exists() and target.stat().st_size == size:
        return 0
    quoted = urllib.parse.quote(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".partial")
    for attempt in range(4):
        try:
            url = f"https://raw.githubusercontent.com/{repository}/{branch}/{quoted}"
            with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "CyDAW-banks"}), timeout=180) as response:
                data = response.read()
            if data.startswith(LFS):
                url = f"https://media.githubusercontent.com/media/{repository}/{branch}/{quoted}"
                with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "CyDAW-banks"}), timeout=600) as response:
                    data = response.read()
            partial.write_bytes(data)
            partial.replace(target)
            return len(data)
        except Exception as error:  # noqa: BLE001 - retried, then reported
            if attempt == 3:
                raise RuntimeError(f"{repository}/{path}: {error}") from error
            time.sleep(2.0 * (attempt + 1))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dest", type=Path, default=default_destination())
    parser.add_argument("--only", default="")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    only = {key for key in args.only.split(",") if key}
    dest: Path = args.dest
    dest.mkdir(parents=True, exist_ok=True)
    summary_path = dest / "banks.json"
    summary = json.loads(summary_path.read_text(encoding="utf-8")) if summary_path.exists() else {"banks": {}}
    lock = threading.Lock()
    for bank_id, repository, licence, confirmed in BANKS:
        if only and bank_id not in only:
            continue
        folder = dest / FOLDER.get(bank_id, bank_id)
        folder.mkdir(parents=True, exist_ok=True)
        branch = MAIN.get(bank_id, "master")
        tree_path = folder / "tree.json" if bank_id not in FOLDER else dest / f"{bank_id}-tree.json"
        if tree_path.exists() and time.time() - tree_path.stat().st_mtime < 86_400:
            tree = json.loads(tree_path.read_text(encoding="utf-8"))
        else:
            tree = api(f"https://api.github.com/repos/{repository}/git/trees/{branch}?recursive=1")
            tree_path.write_text(json.dumps(tree), encoding="utf-8")
        files = [item for item in tree.get("tree", []) if item["type"] == "blob" and kept(item["path"], item["size"])]
        total = sum(item["size"] for item in files)
        print(json.dumps({"bank": bank_id, "files": len(files), "megabytes": round(total / 1e6, 1),
                          "truncated": tree.get("truncated", False)}), flush=True)
        if args.list:
            continue
        done = [0, 0]
        started = time.time()

        def one(item: dict) -> None:
            written = fetch(repository, branch, item["path"], folder / item["path"], item["size"])
            with lock:
                done[0] += 1
                done[1] += written
                if done[0] % 100 == 0 or done[0] == len(files):
                    rate = done[1] / max(1.0, time.time() - started) / 1e6
                    print(json.dumps({"bank": bank_id, "done": done[0], "of": len(files), "mb": round(done[1] / 1e6, 1),
                                      "mbPerSecond": round(rate, 2)}), flush=True)

        failures = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for future in concurrent.futures.as_completed([pool.submit(one, item) for item in files]):
                if future.exception() is not None:
                    failures.append(str(future.exception()))
        licence_files = [item["path"] for item in files if item["path"].rsplit("/", 1)[-1].lower().startswith(("license", "licence"))]
        record = {
            "id": bank_id, "repository": f"https://github.com/{repository}", "branch": branch, "tree": tree.get("sha"),
            "title": repository.split("/")[-1], "licence": licence, "licenceConfirmed": confirmed,
            "licenceFiles": licence_files, "fetched": time.strftime("%Y-%m-%d"),
            "files": [{"path": item["path"], "size": item["size"]} for item in files],
            "failures": failures,
        }
        (folder / "bank.json").write_text(json.dumps(record, indent=1, ensure_ascii=False), encoding="utf-8")
        summary["banks"][bank_id] = {key: record[key] for key in record if key not in ("files",)}
        summary["banks"][bank_id]["folder"] = folder.name
        summary["banks"][bank_id]["megabytes"] = round(total / 1e6, 1)
        summary_path.write_text(json.dumps(summary, indent=1, ensure_ascii=False), encoding="utf-8")
        print(json.dumps({"bank": bank_id, "finished": True, "failures": len(failures)}), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
