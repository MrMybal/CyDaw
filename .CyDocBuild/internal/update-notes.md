# CyDoc update work record (internal)

CyDoc was fetched with Git and pinned at 6ddf9a582defb2f11fbb3f992afef6f98dcadc93.
The package remains version 0.1.0; the revision identifies this update precisely.

The 40 illustrated user chapters were preserved in English and French. The MCP
workflow and developer-contract overview are now authored in both languages.
Three real public API contracts use bilingual source-comment extraction plus short
public signature/example supplements. The 45 public topics produce 90 localised
pages. Historical technical originals remain repository notes; aliases redirect
old page addresses to current topics. Their discontinued individual implementation
line anchors are not maintained as source browsing has deliberately been removed.

The actual application remains C++/JUCE/CyUI/Dear ImGui. Ctrl+F1 is help;
bare F1 selects Arrangement and was intentionally preserved. There are no DSP or
transport changes. Source edits in API headers update documentation comments only.

Checks performed locally on Windows:
- Official CyDoc build/check and export integrity validation passed.
- Native Windows application and native help test compiled successfully.
- Six CTest checks passed, including a private silent process for actual help
  shortcuts, contextual routes, old manual aliases and encoded file URLs.
- Three authoring-boundary/translation tests and seven package tests passed.
- Registry reference checks covered 168 native types and 5766 bilingual numeric rows.
- 38 upstream CyDoc core/interaction/export/publication tests passed in a simulated
  DOM. These are unit tests, not a real browser rendering of the generated manual.
- A complete Windows ZIP was produced and inspected: all Documentation resources,
  no private authoring workspace or implementation source listings in the manual.
- Gitleaks staged scan found no secrets.

The browser automation policy rejected file:// navigation. No local web server,
CDP/browser workaround or alternate browser surface was used. Actual local-browser
layout, image zoom and touch checks could not be completed with that tool in this
session. The optional real-browser regression script was adapted and syntax checked,
but not executed. Linux/macOS native launches and physical touch remain untested.

Everything in this update stays local until the user requests a push. GitHub Pages
continues serving the previous deployment. The updated Pages workflow will generate
the same Documentation export, including a root index compatibility redirect, on a
future authorised push. Do not publish this work record in the reader.
