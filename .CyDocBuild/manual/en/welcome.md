---
id: welcome
title: Welcome and first project
description: Make your first sound and choose a workspace.
section: Manual / Start here
order: 0
revision: c929e4beac79abaf
---

## Overview {#overview}

CyDAW combines a music studio, DJ mixer, multitrack sound editor, sound-to-picture workspace, processing editor and plugin workshop. This offline manual describes the shipped V2 interface. English is the default manual language; choose Français above to read the same topics while keeping your place. This does not change the application's language.


~~~image
{"id": "figure-startup", "src": "assets/startup-en.svg", "alt": "From audio setup to a saved and rendered project.", "title": "From audio setup to a saved and rendered project.", "controls": "minimal"}
~~~

*From audio setup to a saved and rendered project.*


## Read, search and navigate {#reading}
Open Help > User manual or press Ctrl+F1. CyDAW opens the installed CyDoc reader as a local file in your default browser. Keep the complete Documentation folder beside the executable; reading needs no server, CDN or Internet connection.

Use the topic tree, page outline and global search at the top (Ctrl+K). Search covers prose, code and symbols; use the content/language filters and optional typo tolerance. French accents are optional. The language selector retains the page and common section IDs. The complete manual is available in English and French.

Browser Back/Forward and CyDoc's reading tools keep navigation and reading positions. My space stores bookmarks, recent pages and private notes locally; Resume reading returns to a saved position. Export personal data to back it up. Browser storage policy can limit persistence. Contextual help opens the requested page or section; use Resume reading in My space to return to saved reading.

Images use minimal controls: focus or hover to show them, enlarge inside the same documentation window, zoom with the wheel or buttons, drag to pan and double-click to reset. Escape reduces an enlarged image. Touch controls remain visible and support pan/pinch. Use browser text zoom and the theme menu for comfortable reading. The browser owns these keys; the DAW's project and transport are separate.

## Make your first sound {#first-project}
1. In Options > Audio settings choose a real output device and the channels connected to your speakers. Start with a moderate buffer and lower the master before listening.
2. In Studio create a MIDI instrument track. Opale is a useful first synthesizer.
3. Double-click empty space on that MIDI track to make a clip. Open its note editor, draw a short phrase and press Space.
4. Select the track and use the plus at the end of Devices to add an effect. Save the editable project with Ctrl+S.
5. Export a mix when you need an audio file. Saving a project and rendering audio are different operations.

## Choose your workspace {#modes}
| Mode | Purpose |
| --- | --- |
| Studio | Arrange tracks or launch clips and scenes. |
| DJ mix | Blend complete tracks on two or four decks. |
| Foundry | Cut, repair and assemble sound files. |
| Cinema | Place sound against frames, with optional video. |
| Processings | Design and audition reusable audio or note operations. |
| Plugin workshop | Build graphs and designed VST3, CLAP or LV2 interfaces. |

Each mode owns an editing context. Check the active mode before Save, Undo or transport keys. Native module panels keep English labels even in the French interface; both manual languages describe the same controls.

Continue with [audio setup](doc:setup), [the workspace](doc:workspace) or [Studio](doc:studio).
