# CyDoc migration record

CyDAW V2 is a C++20/JUCE audio application with Dear ImGui/CyUI views, CMake builds
for Windows, Linux and macOS, Python tooling and native exported plugin UIs.
Before migration, Help used a custom CyUI DocumentViews/Images reader backed by
40 bilingual Markdown chapters, plus a separate native shortcut popup. Technical
guides and API/control-port documentation also lived outside the user reader.

## Implementation and ownership

CyDoc 0.1.0 is pinned to Git revision 774f2a7eabf7dd181a98ffc5add115b580b67f42.
The four ai guides and installed dist declarations were checked. Git authentication
is inherited; credentials are not written into source, URLs or packages.
The imported checkout is unchanged. Its built MIT package is vendored with a SHA-256
pin, so future application/CI builds do not require access to the private repository.

tools/docs/prepare.py converts application-owned content and inventories original
files. Existing page/section IDs remain stable. Fenced code is retained.
Screenshots and SVG drawings become minimal image blocks with stable media IDs.
The official CyDoc check command compiles/validates and exports the manual. The
application adapter uses mountViewer, createDocumentation and createLink through
public exports, retaining official local module scripts/resource catalogs. It
adds CyDAW branding, application storage namespace and generic-open route resume.
No generated manifest/data or upstream source is patched to repair content.

The native HelpDocumentation component reads a small generated route catalog,
uses JUCE's platform file-URL conversion, validates page/anchor/media identities,
and asks the default browser to open the local file. It does not create a server
or change audio processing. Menus, Ctrl+F1, mode context help, ? and shortcut help
use this path. The manual remote command remains as a route-opening compatibility
entry; help is an alias. Native search/history/window-closing commands are replaced
by CyDoc/browser controls, rather than pretending to control an external tab.

The old ManualView/ManualContent implementation, native shortcut popup and their
reader tests are removed. CyUI DocumentViews is no longer selected by CyDAW.
Images remains selected for Cinema. Native exported plugins retain their existing
small UI dependency closure and do not carry CyDoc.

## Content preservation and gaps

The 40 English/French user chapters remain current bilingual authoring inputs.
The 168-type/2,883-parameter registry references, examples, images, diagrams,
technical Markdown, licence texts and MCP guide are included. Each retained
technical input has provenance in migration-report.json. The source allowlist is
explicit in docs/cydoc.inventory.json; it contains public extension/processor
contracts and the distributed Python MCP server, not the whole repository.

PRODUCT_SPEC is design intent. V2_TEST_BUILD contains historical build/testing
instructions. GRAPH_EDITOR_VST_ARCHITECTURE mixes architecture milestones with
later implementation notes. They are preserved with historical-scope notices.
Other technical originals carry a version-scope warning; pending English/French
translations are stated explicitly. They are not rewritten into invented features.
The current user chapters keep Alpha limitations such as plugin-hosting scope,
deferred macOS export and the known JSFX tempo check.

CyDoc's translation report records copied originals and extracted API pages as
untracked when no source revision is declared. The main manual has a separately
reviewed source-revision ledger and rejects stale translations. Full technical
translations and semantic C++ API indexing remain future authoring work.
CyDoc's index is lexical and .h files use its current C-header classification;
Doxygen/TypeDoc are not invoked or claimed.

## Selected modules and offline resources

Only **image** is enabled. Real screenshots and static SVG figures need zoom/pan,
keyboard/touch controls and in-window enlargement. No existing demonstration video,
controllable GIF, 3D asset, gallery or interactive node graph justifies another reader.
No Three.js or mesh bundle is in the tool consumer or installed export. Upstream
CyDoc's own development build includes its declared development/test dependencies;
they are not application runtime dependencies.

CMake generates help, verifies it and copies a complete managed tree beside the
executable. Windows packaging requires that valid tree. Install/package includes
all classic scripts, local images, styles, notices, integrity/routes indexes and
translation/migration reports. The export works with file origins and does not
fetch JSON or load external fonts/scripts. Browser language, theme, notes, bookmarks
and reading state stay local, subject to browser storage policy.

## Regeneration and validation

See [USER_MANUAL](USER_MANUAL.md) for commands. The first dependency preparation installs pinned public npm dependencies from the
vendored CyDoc archive. Generation then works from cache with --offline.
No network access is required to read the installed documentation.

To update the library itself, use Git authentication to check out the new reviewed
revision, read its ai guides/declarations, run npm ci, npm run build and npm pack.
Replace the vendored archive and update third_party/cydoc.json's revision, archive
checksum and build dependency versions from that checkout's lockfile. Never place
a credential in that manifest. Ordinary documentation regeneration does not do this.

Validation covers the compiled identities/anchors, retained content, manual
translation revisions, source allowlist, script/module selection, integrity and
package resources. Native tests cover URL escaping and help entry points in an
isolated silent instance. Browser tests must run against file:// with network
blocked and inspect language, links, images, keyboard, touch and reading state.

Available target: Windows x64 with Chromium. Linux/macOS and a physical touch device
are not available for native integration validation in this workspace. Those checks
remain necessary before claiming equivalent platform UI/storage behavior.


## Verified in this workspace

The Windows Release build, native route/model checks, actual Help menu and keyboard
entry points, content-preservation checks and Windows packaging contract tests pass.
All 182 localized pages were rendered in installed Chromium through file:// with
network disabled. Checks cover French/English, explicit anchors, precise image state,
PNG/SVG, minimal settings, in-window enlargement, search, browser history and saved
positions, themes and a simulated touch viewport. Native Windows file URLs were also
opened by Chromium without file-access bypass flags. No page network requests or
browser errors were observed. The native test records URLs rather than disturbing
the user's default browser session.

CyDoc currently loads all pages together; segmentation is not available. The source
allowlist limits that payload. EEL2/JSFX examples remain intact as plain code where
the installed highlighter has no matching grammar. This is not an execution engine.
