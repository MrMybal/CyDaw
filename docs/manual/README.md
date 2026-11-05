# Authoring the CyDAW CyDoc manual

Application-owned authoring inputs remain in docs/manual/en, fr and assets.
manifest.json records ordered stable chapter IDs and translated titles/groups.
Headings use explicit matching IDs: `## Human title {#stable-id}`.
The introduction becomes the stable overview section. Existing links such as
`mixing.md#sidechain` are converted to CyDoc routes during preparation.

Technical Markdown and licence files are inventoried in docs/cydoc.inventory.json.
The migration keeps original prose and fenced code, marks historical/design scope,
and identifies untranslated originals. Add new technical documents to the inventory.
The six explicitly approved source files are the only source-index inputs.

## Build and check

~~~sh
python tools/docs/build.py --out build/help
python tools/docs/validate.py --root build/help
python tools/docs/build.py --offline --out build/help
~~~

The first command prepares a fresh authoring project, calls the pinned CyDoc
official check/export CLI, bundles the application adapter through public API
exports and validates/copies the complete result. check writes an export.
Do not hand-edit generated HTML, data.js, manifest.json or module bundles.

For source screenshots and numeric registry references:

~~~sh
python tools/manual/diagrams.py
python tools/manual/capture.py --exe "<build>/CyDAW 2.exe" --images docs/manual/assets --catalogue docs/manual/reference.json --ffmpeg "<ffmpeg.exe>"
python tools/manual/build_reference.py
python tools/manual/build_reference.py --check
python tools/manual/validate.py
~~~

Captures use a private hidden silent process and original synthetic test media.
No commercial tracks, user projects or sample packs belong in documentation assets.
Schema reference numbers are raw engine values, not inferred UI units or choices.

## Translation tracking

translations.json is a reviewed ledger, not an automatically refreshed build output.
When English meaning changes, update French with the same page/section/media IDs,
review it, then acknowledge only those chapters:

~~~sh
python tools/docs/translations.py --ack welcome --ack shortcuts
~~~

The generator fingerprints English inputs consistently across line endings. A stale
ledger makes validation fail. Never acknowledge a translation merely to silence a
check. Technical originals copied into another locale retain explicit pending
translation notices and untracked status until real translations are authored.

## Media and dependencies

Screenshots/SVG files become stable image blocks with minimal controls.
Only image is currently selected. Static diagrams need no graph reader.
CyDoc's official export supplies local classic module scripts and resource catalogs;
the application adapter uses those providers, not file fetches or a web server.
Source-index extraction is lexical, not a compiler API parser.

The repository pins CyDoc's Git revision and a verified package archive in third_party/cydoc.json.
The library was imported through authenticated Git. Ordinary builds install pinned
public npm tools without accessing the private repository. The installed application
carries the complete help tree and MIT/dependency notices. Generation needs Node.js
22/npm and Python 3.11; reading requires only a browser.

See [integration and verification](../USER_MANUAL.md) and
[migration details](../CYDOC_MIGRATION.md).
