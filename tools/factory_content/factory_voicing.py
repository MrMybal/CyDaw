"""How each factory instrument is played: the ones that sound one note at a
time (a saxophone, a flute, a trumpet, a bowed solo string, a voice) are set
to legato with a short glide, so a line slides from note to note instead of
stacking into chords. Written into the catalogue's entries as
"voicing": {"mode": "legato", "glide": <ms>}; a track made from the
instrument sets its voice allocator from it (Jeu / Glide on its card).

    python factory_voicing.py <factory folder>     updates <folder>/catalogue.json

build_sfz_instruments.py applies it too, when it writes the catalogue.
"""
import json
import sys
from pathlib import Path

# Glide in milliseconds: quick for the reeds and flutes, longer for the
# trombone's slide and the erhu's.
VOICING = {
    "flute": 30, "oboe": 30, "clarinet": 30, "bassoon": 35,
    "recorder-alto": 25, "recorder-tenor": 25, "ocarina": 25,
    "alto-sax": 35, "tenor-sax": 35, "baritone-sax": 40,
    "horn": 40, "trumpet": 30, "trombone": 60, "tuba": 45, "tuba-war": 45,
    "violin-arco": 35, "cello-solo": 40, "contrabass-arco": 45,
    "erhu": 60, "voice-a": 50,
}


def apply(catalogue):
    """Sets each listed instrument's voicing; the others are left polyphonic."""
    for entry in catalogue.get("instruments", []):
        glide = VOICING.get(entry.get("key"))
        if glide is None:
            entry.pop("voicing", None)
        else:
            entry["voicing"] = {"mode": "legato", "glide": glide}
    return catalogue


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    path = Path(sys.argv[1]) / "catalogue.json"
    catalogue = json.loads(path.read_text(encoding="utf-8"))
    apply(catalogue)
    path.write_text(json.dumps(catalogue, indent=1, ensure_ascii=False), encoding="utf-8")
    count = sum(1 for entry in catalogue["instruments"] if "voicing" in entry)
    print("%d instruments play legato" % count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
