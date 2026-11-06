# CyDAW presentation for GitHub Pages

Static HTML, CSS and JavaScript, in English (default) and French. No framework,
external font, account service or runtime API is required. Images are existing
application captures and diagrams. Native format names are kept as identifiers.

From the repository root:

```sh
python .CyDocBuild/tools/build.py
python tools/build_website.py
python -m http.server 48761 --bind 127.0.0.1 --directory build/github-pages
```

The Pages workflow generates a fresh CyDoc export, validates it, then assembles
build/github-pages. The presentation is index.html; the unchanged full reader is
manual/Documentation.html. The former Documentation.html address redirects with
its original query and fragment. Former root #/locale/page links are forwarded to
the manual by the presentation script. Ordinary #workspaces/#sound anchors remain
presentation navigation. The standalone/offline application documentation is
unchanged and never receives website assets.

Only the allowlisted images, presentation files and validated documentation are
copied. Authoring inputs, caches, application projects and sample packs stay out.
The existing GitHub Pages deployment is reused; no other hosting service is needed.
A local preview does not publish changes. Push only when the user requests it.

## Showcase content and interactions

The presentation now includes dedicated sections for arrangement, clip launching,
MIDI, native instruments and effect cards, mixing and routing, DJ mix, Foundry,
Cinema, Plugin workshop, track graphs, external formats and JSFX, transport sync,
reusable processing, documentation and release questions. A sticky chapter bar
helps navigate the longer page. The six workspace tabs support arrow keys and
Home/End. Image buttons open a contained native dialog; Escape closes it and
returns focus. Reduced-motion preferences disable smooth scrolling/transitions.
English is the initial language; `?lang=fr` selects French. Text, image descriptions,
documentation targets and modal labels follow the selected language.

`website/assets/` preserves three actual CyDAW captures supplied by the project
owner in this conversation: the wavetable graph/workshop, the native rack with
Chords/Opale/Chorus/Pro Reverb, and the Chorus/Flanger cards. These supplement
existing application/manual captures. Captures may show an earlier interface
revision (including the former workshop name); the presentation uses the current
workspace names. No third-party music, video frames or user project files are
included. The compact-device close-up is a CSS view of the Studio capture; its
image dialog opens the original complete image.
