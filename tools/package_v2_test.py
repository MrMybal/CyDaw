"""Gathers CyDAW 2's test build into dist/ and zips it.

    python tools/package_v2_test.py

From the Release build of build/agent-graph: the application, its plug-in
worker, fonts, translations and the templates (with their audio), the test
notice (docs/V2_TEST_BUILD.md as LISEZ-MOI.md), the interface notes and the
licences, and the factory instruments (content/factory, built by
tools/factory_content, as "Factory"). No debug symbols; the one script is the
sample classifier.
"""
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "agent-graph" / "CyDAW2_artefacts" / "Release"


def version():
    match = re.search(r"project\(CyDAW VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    return match.group(1) if match else "0.0.0"


def main():
    name = "CyDAW2-%s-Windows-x64-test" % version()
    target = ROOT / "dist" / name
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    for file in ("CyDAW 2.exe", "CyDAWPluginWorker.exe"):
        shutil.copy2(BUILD / file, target / file)
    # tools/samples: the sample classifier the browser runs when Python is there.
    # tools/companion: the Python companion (sound matching), without caches.
    for folder in ("fonts", "i18n", "tools"):
        shutil.copytree(BUILD / folder, target / folder, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    # The templates from the sources, so the package has the latest ones.
    shutil.copytree(ROOT / "templates", target / "templates")
    # The factory instruments: sampler maps, the sound models, the catalogue,
    # the banks' list and the credits. Not their samples: CyDAW downloads them
    # from each bank's repository (any FLAC left from older builds stays out).
    factory = ROOT / "content" / "factory"
    if (factory / "catalogue.json").exists():
        shutil.copytree(factory, target / "Factory", ignore=shutil.ignore_patterns("*.flac", "*.wav"))
    else:
        print("no factory content: run tools/factory_content first", file=sys.stderr)
    shutil.copy2(ROOT / "docs" / "V2_TEST_BUILD.md", target / "LISEZ-MOI.md")
    (target / "docs").mkdir()
    shutil.copy2(ROOT / "docs" / "V2_INTERFACE.md", target / "docs" / "V2_INTERFACE.md")
    shutil.copy2(ROOT / "docs" / "SOUND_MATCHING.md", target / "docs" / "SOUND_MATCHING.md")
    shutil.copy2(ROOT / "docs" / "PRISME.md", target / "docs" / "PRISME.md")
    shutil.copy2(ROOT / "docs" / "EFFETS_STUDIO.md", target / "docs" / "EFFETS_STUDIO.md")
    shutil.copy2(ROOT / "docs" / "KICK_ET_ELECTRO.md", target / "docs" / "KICK_ET_ELECTRO.md")
    shutil.copy2(ROOT / "docs" / "AMPLIS_ET_MICROS.md", target / "docs" / "AMPLIS_ET_MICROS.md")
    shutil.copy2(ROOT / "docs" / "EFFETS_MIXAGE.md", target / "docs" / "EFFETS_MIXAGE.md")
    for document in ("SAMPLER.md", "BANQUE_DE_SONS.md", "EDITION_AUDIO.md"):
        if (ROOT / "docs" / document).exists():
            shutil.copy2(ROOT / "docs" / document, target / "docs" / document)
    juce_licence = ROOT / "third_party" / "JUCE" / "LICENSE.md"
    previous = sorted((ROOT / "dist").glob("CyDAW-*-Windows-x64-ASIO"))
    for licence in ("JUCE_LICENSE.md", "ASIO_SDK_LICENSE.txt"):
        sources = [folder / licence for folder in previous if (folder / licence).exists()]
        if licence == "JUCE_LICENSE.md" and juce_licence.exists():
            sources.insert(0, juce_licence)
        if sources:
            shutil.copy2(sources[0], target / licence)
        else:
            print("no", licence, "found", file=sys.stderr)
    archive = shutil.make_archive(str(ROOT / "dist" / name), "zip", root_dir=ROOT / "dist", base_dir=name)
    size = sum(path.stat().st_size for path in target.rglob("*") if path.is_file())
    print(target)
    print(archive, "%.1f MB unpacked" % (size / 1e6))
    return 0


if __name__ == "__main__":
    sys.exit(main())
