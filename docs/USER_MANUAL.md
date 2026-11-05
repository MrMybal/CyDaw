# CyDAW offline user manual

Open **Help > User manual** (French: **Aide > Manuel utilisateur**) or **Ctrl+F1**.
The manual starts in English on a fresh installation. Its language selector switches
between English and French independently of the application language and retains the
current chapter and section.

The 40 chapters in each language cover setup, Studio/Launcher, MIDI/audio editing,
automation, mixing/routing, instruments and effects, controllers/modulators, containers,
DJ, Foundry, Cinema, processings, graphs, interface design, native plugin export,
JSFX/CLAP/LV2/VST3 hosting, sync, media/library management and troubleshooting.
Three generated reference chapters enumerate 168 native types and 2,883 numeric
schema parameters from the Alpha 0.78 registry. These tables document raw schema
values, not inferred UI units or enum labels.

There are 22 real application captures (11 scenes in both languages) and 24 editable
SVG diagrams. Capture fixtures use original generated audio and synthetic video;
commercial example projects and sample packs are not documentation resources.

## Reader

- Search covers the entire selected language, with all entered words required and
  French accent folding. Results link directly to stable section IDs.
- Back/Forward restore reading position. Home, Ctrl+F, Escape, adjustable text size
  and a resizable window support navigation.
- Wide layouts show a figure rail. Narrow layouts show a collapsible illustration
  above the text. Click to enlarge; wheel zoom, middle-button pan and double-click fit.
- Help for this mode opens the relevant chapter.
- Reading/searching does not edit the project or own the transport. HTTPS source
  links open only after a deliberate click; prose, pictures and diagrams work offline.

The reader uses the pinned CyUI **DocumentViews** module for Markdown and **Images**
for figure navigation. Stable identities, bilingual search/history, figure loading and
texture lifetime belong to CyDAW. The pinned renderer does not expose inline image
or anchor hooks, so sections and figure rails are composed by the application.
It is not a general HTML/browser renderer.

## Build, package and checks

CMake copies `docs/manual` beside the executable on every CyDAW2 build and installs
it under `docs/manual`. The Windows packager validates the complete two-language
manual before writing its ZIP. A missing chapter, image or broken local link fails
packaging. Copy the entire manual directory when moving the application.

Run `ctest -C Release -R "^cydaw_manual_" --output-on-failure` in the build directory.
The Windows reader test launches its own hidden, silent instance with temporary
locations. It renders every section, tests real keyboard/mouse interactions, language,
search/history, figures, themes and narrow layout, and writes QA screenshots.
The content/model checks are independent of the audio engine.

See [maintenance instructions](manual/README.md) for regeneration commands and
[English contents](manual/en/welcome.md) / [French contents](manual/fr/welcome.md).
Older developer and technical documents elsewhere in docs remain mostly French;
they are separate from this bilingual user manual. Current unsupported features
and known Alpha limitations are explicitly identified in the relevant chapters.
