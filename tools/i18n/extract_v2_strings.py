"""The interface's texts, for a new language.

CyDAW 2 writes its interface texts inline, in French and English:
L("Lecture", "Play"). Another language translates the English. This script
gathers every pair from src/v2 and writes resources/i18n/v2/template.json:

    { "Play": "", ... }

To add a language (code xx):
  1. copy resources/i18n/v2/template.json to i18n/v2/xx.json beside
     "CyDAW 2.exe" and fill in the translations (an empty one keeps the
     English);
  2. copy resources/i18n/en.json (the engine's texts: node names, messages)
     to i18n/xx.json, translate its values, and set "language.name" to the
     language's own name ("Español");
  3. restart CyDAW 2: the language is in Options > Language.

Run again after the interface changes: an existing translation file is
merged (kept translations, new texts added empty) when given as an argument:

    python tools/i18n/extract_v2_strings.py [i18n/v2/xx.json ...]
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = ROOT / "src" / "v2"
TEMPLATE = ROOT / "resources" / "i18n" / "v2" / "template.json"

LITERAL = r'"(?:[^"\\]|\\.)*"'
SEQUENCE = r"(?:%s\s*)+" % LITERAL
PAIR = re.compile(r"\bLs?\(\s*(%s),\s*(%s)\)" % (SEQUENCE, SEQUENCE))


def unescape(sequence):
    """Adjacent C++ literals joined, their escapes read."""
    text = ""
    for literal in re.findall(LITERAL, sequence):
        body = literal[1:-1]
        # \xNN escapes are UTF-8 bytes; the rest are the usual ones.
        raw = bytearray()
        index = 0
        while index < len(body):
            char = body[index]
            if char == "\\" and index + 1 < len(body):
                following = body[index + 1]
                if following == "x":
                    digits = re.match(r"[0-9a-fA-F]{1,2}", body[index + 2:])
                    raw += bytes([int(digits.group(0), 16)])
                    index += 2 + len(digits.group(0))
                    continue
                raw += {"n": b"\n", "t": b"\t", '"': b'"', "\\": b"\\", "'": b"'"}.get(following, following.encode("utf-8"))
                index += 2
                continue
            raw += char.encode("utf-8")
            index += 1
        text += raw.decode("utf-8", errors="replace")
    return text


def english_texts():
    texts = set()
    for path in sorted(SOURCES.glob("*.cpp")) + sorted(SOURCES.glob("*.h")):
        source = path.read_text(encoding="utf-8")
        for french, english in PAIR.findall(source):
            text = unescape(english)
            if text.strip():
                texts.add(text)
    return sorted(texts, key=str.casefold)


def main():
    texts = english_texts()
    TEMPLATE.parent.mkdir(parents=True, exist_ok=True)
    TEMPLATE.write_text(json.dumps({text: "" for text in texts}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("%d texts -> %s" % (len(texts), TEMPLATE))
    for argument in sys.argv[1:]:
        target = pathlib.Path(argument)
        existing = json.loads(target.read_text(encoding="utf-8")) if target.exists() else {}
        merged = {text: existing.get(text, "") for text in texts}
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(merged, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        missing = sum(1 for value in merged.values() if not value)
        print("%s: %d texts, %d to translate" % (target, len(merged), missing))


if __name__ == "__main__":
    main()
