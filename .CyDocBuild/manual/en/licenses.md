---
id: licenses
title: Versions, licences and documentation
description: Find application, plugin and sound-bank licence information.
section: Manual / Reference
order: 36
revision: 2a59486cab791db0
---

## Overview {#overview}

This manual accompanies the Windows CyDAW V2 interface and records the current Alpha capabilities rather than promising every feature of every plugin standard. Text, diagrams and captures are supplied locally with the application. No browser connection is required to read them.


~~~image
{"id": "figure-graph-export", "src": "assets/graph-export-en.svg", "alt": "The product and its export targets retain their own dependency boundaries.", "title": "The product and its export targets retain their own dependency boundaries.", "controls": "minimal"}
~~~

*The product and its export targets retain their own dependency boundaries.*


## Application and dependencies {#licences}
CyDAW's supplied LICENSE is GNU AGPL version 3. JUCE has its supplied terms; Dear ImGui and CyUI supply MIT licences. The package includes CyUI's licence and TextEditor/Lexilla/Scintilla notices under Licenses/CyUI, alongside other dependency notices. FFmpeg is the pinned shared LGPL build with configuration and corresponding source supplied in its licence folder. Refer to these exact delivered texts for the version in use.

External format SDKs, custom components, plugin dependencies, sound banks and sample sources retain their own terms. The format chosen for export does not change the included source's licence. A factory instrument's media terms are separate from the executable's.

## Source links and offline files {#sources}
[CyDAW source](https://github.com/MrMybal/CyDaw) and [CyUI source](https://github.com/MrMybal/CyUI) identify the projects. [CyUI's pinned MIT licence](https://github.com/MrMybal/CyUI/blob/8367401fe45fc22bfed5e227cb3de343177f676b/LICENSE) matches this integration. Access to a private source repository can require permissions; the installed notice texts remain readable offline.

The installed manual opens through `Documentation/Documentation.html`; its resources and dependency notices live in `Documentation/DocData`. Keep the directory together when moving the application.

## Report an issue {#maintenance}
If a gesture differs from this guide or an image is outdated, report the application version and the page/section name. You can copy a precise page or image link with the reader’s link controls. Personal bookmarks and notes stay in the browser’s local storage; use My space to export them when moving machines.

Start again at [Welcome](doc:welcome), consult [troubleshooting](doc:troubleshooting) or [plugin limitations](doc:plugin-formats#limits).

The CyDoc reader is MIT licensed; its licence and generation notices are shipped in Documentation/DocData/Licenses. See the [CyDoc repository](https://github.com/MrMybal/CyDoc).
