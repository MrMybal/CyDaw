# CyDAW documentation workspace

This private authoring workspace uses CyDoc 0.1.0 at revision
6ddf9a582defb2f11fbb3f992afef6f98dcadc93, imported through authenticated Git and
vendored as a verified local npm archive. It is not a reader page.

## Generate and validate

Requirements: Python 3.11 and Node.js 22/npm. From the application root:

~~~sh
python .CyDocBuild/tools/build.py
python .CyDocBuild/tools/validate.py --root Documentation
python .CyDocBuild/tools/build.py --offline
~~~

The first run installs pinned public build dependencies in .CyDocBuild/cache.
No Three.js is installed: only the image reader is needed for PNG screenshots and
SVG diagrams. CyDoc's official check command compiles the whole reader. The host
adds only native routes, notices, integrity metadata and a legacy index redirect;
it does not replace or patch the official reader. Documentation.html is the actual
entry. DocData contains runtime resources. DocAI.md is an AI-only sidecar.

## Authoring

Edit manual/en and manual/fr together. Keep IDs, explicit anchors and media IDs
shared; use parent IDs and section paths for navigation. Provide useful translated
descriptions and media captions. English is original/default; French is complete.
After reviewing a paired change, update revision/sourceRevision to the SHA256
prefix (16 characters) of the English body after frontmatter. The validator rejects
stale English fingerprints and mismatched translation revisions.

Public API pages are extracted with the public cydoc/extract API from explicit
bilingual comments in three allowlisted headers. api/en and api/fr supply concise
public declarations and usage examples. No implementation listings, source trees,
AI instructions or internal reports enter the reader. Keep work notes in internal.
Original technical repository documents remain outside reader inputs; relevant
user workflows are covered by the bilingual manual. Old technical/source-page
addresses have aliases to the appropriate current topic or public contract.

The registry snapshot in internal/registry.json supports the native parameter
reference. capture/check_reference.py checks every native type, numeric parameter
and typed port against the authored reference. Capture tools are private authoring
aids; they never ship with the application. Existing screenshots and bilingual SVG
figures were preserved. Native module names and engine keys retain their exact
application spelling in both languages.

## Integration and distribution

CMake generates a validated Documentation directory and installs it beside the
executable. Help > User manual (Ctrl+F1), contextual help (Ctrl+Shift+F1), and ? use
Documentation/Documentation.html; bare F1 continues to select Studio Arrangement.
Never ship .CyDocBuild. The package contract tests verify the complete generated
tree and reject missing reader resources or figures.

The Pages workflow is prepared to generate the same reader on future authorised
pushes. Its compatibility index keeps older root URLs with their fragments.
This update is local until the user requests a push. For UI checks run
node tests/cydoc_browser_check.mjs --help and tests/v2_help_check.py --help.
