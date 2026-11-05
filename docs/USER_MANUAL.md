# CyDAW local documentation — CyDoc

Open **Help > User manual** (Ctrl+F1) to open the installed **help/index.html** in the
default browser with a correctly encoded file URL. Help for this mode targets the
current workspace; Keyboard shortcuts and the existing ? binding target the shortcut
reference. No HTTP server, CDN or runtime Node.js is used. CyDAW's audio application
does not gain a WebView dependency.

The manual preserves the 40 English/French user chapters, 168 native types and
2,883 numeric schema parameters, the real screenshots/diagrams, original technical
documents, licences, code examples and MCP guide. Historical design/test-build
documents carry scope warnings. Technical originals without complete translations
are retained with explicit notices; they are not presented as completed translations.

Only CyDoc's **image** reader is enabled: screenshots and static SVG diagrams need
zoom/pan/in-window enlargement. There is no justified video/GIF/mesh/gallery/interactive
graph content in this manual, so those readers and Three.js are not distributed.
An SVG drawing of a graph is still a static illustration.

## Authoring and generation

Requirements: Python 3.11+, Node.js 22+ and npm. CyDoc was retrieved with authenticated
Git and is supplied as a revision-pinned, SHA-256-verified package archive. Ordinary
application builds need no access to its private repository and store no tokens. Reading the completed export requires none of these tools.

~~~sh
python tools/docs/build.py --out build/help
python tools/docs/validate.py --root build/help
# After the pinned build tools are cached:
python tools/docs/build.py --offline --out build/help
~~~

CMake's cydaw_documentation target generates/validates the export and
cydaw_help_resources copies it beside CyDAW2. The installation and Windows ZIP
packager distribute the whole managed tree, including local scripts, data, images,
notices, route catalog, integrity index and translation/migration reports.
First generation installs the pinned npm build dependencies; cached generation and all reading are offline.
The cache is application-owned under output/cydoc-cache. No compiled CyDoc library
or generated documentation is hand-edited in the source repository.

Read [authoring instructions](manual/README.md) and the detailed
[migration record](CYDOC_MIGRATION.md).

## Language and source contracts

English is the content default. Page/section/media identities are shared with French.
The native filename/manual aliases remain accepted; CyDoc validates its canonical
routes. A missing content locale falls back to English. The versioned translation
ledger records the English revision acknowledged by each manual translation.
An English change without a reviewed French update fails the application validator.
CyDoc also writes translation-report.json and its reader page; copied technical
originals and source-extracted API pages retain an honest untracked status.

Only the six files in docs/cydoc.inventory.json's source allowlist enter the lexical
source index. Controller/custom-ImGui/audio-processor APIs are extracted from real
bilingual C/C++ annotations. The Python MCP server is indexed as distributed tool
source. Third-party sources, whole build trees, private settings and user projects
are excluded. CyDoc does not infer C++ overloads/macros or replace Doxygen.

## Verification

~~~sh
ctest --test-dir build/cyui-migration -C Release -R "cydaw_help|cydaw_manual_reference" --output-on-failure
python tests/v2_package_check.py
node tests/cydoc_browser_check.mjs --help
~~~

The route model tests validate local URLs, escaping, aliases, anchors and media.
The native input test uses a hidden silent instance and records requested URLs
without opening browser tabs. The separate browser test uses the installed Windows
Chromium executable and a fresh profile, blocks network traffic and reads file://.

Windows is the available native test platform. Linux/macOS distributions share the
same generated assets and native platform URL API, but their browser/storage/UI
integration must still be checked on those systems. Mobile/touch browser simulation
is not validation on a physical tablet. Browser storage/privacy settings determine
personal-data persistence; browser defaults determine whether a native help open
reuses a tab. Runtime video codecs and WebGL are not requirements for this image-only
export.
