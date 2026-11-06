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
