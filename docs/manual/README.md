# Maintaining the bilingual manual

The manifest defines ordered chapters and translated titles/groups. English is
the default. Each chapter exists in en and fr with identical stable section IDs:
`## Human title {#stable-id}`. The introduction is section `overview`.
Local links use `chapter.md#stable-id`. Keep them relative and valid in both languages.

Standalone `![Translated caption](../assets/name-en.png)` lines become figure entries.
Use bilingual PNG screenshots and SVG diagrams. The app retains chapter/section
when switching language. It does not load arbitrary remote pictures or execute HTML.

## Regeneration

Run from the repository root with Python 3.11+:

~~~sh
python tools/manual/diagrams.py
python tools/manual/build_reference.py
python tools/manual/build_reference.py --check
python tools/manual/validate.py
~~~

To refresh screenshots and the native registry from a Windows build:

~~~sh
python tools/manual/capture.py --exe "<build>/CyDAW 2.exe" --images docs/manual/assets --catalogue docs/manual/reference.json --ffmpeg "<ffmpeg.exe>"
~~~

This launches a separate hidden silent process, uses temporary locations, generates
original test audio and synthetic video, then closes it. It does not use the user's
projects or commercial tracks. A new registry snapshot must be followed by reference
regeneration and review; raw numeric bounds/defaults do not encode UI units or choices.
The manifest and generation check are versioned with the content.

## Verification and ownership

~~~sh
ctest --test-dir <build> -C Release -R "^cydaw_manual_" --output-on-failure
python tests/v2_package_check.py
~~~

The model test verifies load/search/identity/path rules; the content validator checks
both languages, local links, figures, translation parity and complete registry coverage.
The Windows reader test renders all sections and exercises keyboard, language,
scroll/history, zoom, five themes and narrow layout in an isolated instance. Review
its PNGs in `<build>/manual-check` as well as the prose.

CMake's `cydaw_manual_resources` target refreshes installed resources even if the
executable does not relink. Runtime uses docs/manual beside the executable, falling
back to the working directory for source-tree development. Missing resources display
a reload/restoration message. Windows release packaging requires a valid manual.

Screenshots and diagrams are source-controlled documentation assets. Their synthetic
media fixtures and test logs stay outside this folder. Do not add customer recordings,
commercial examples, user paths, downloads or build outputs here. Maintain equivalent
instructions in both languages; preserve explicit limitations instead of documenting
unimplemented capabilities as available.
