---
id: workshop
title: Plugin workshop: from graph to device
description: Turn a processing graph into a reusable plugin.
section: Manual / Build and connect
order: 25
revision: 45afcb3ac767b0d7
---

## Overview {#overview}

Open Plugin workshop from the top switch, View menu or F7. It is an authoring workspace separate from Studio. Create a new effect/instrument or open an existing graph device with its pencil. A track-linked device can be auditioned while editing; a library file is not automatically the same instance as every track that uses it.


~~~image
{"id": "figure-workshop", "src": "assets/workshop-en.png", "alt": "A compressor graph in Plugin workshop.", "title": "A compressor graph in Plugin workshop.", "controls": "minimal"}
~~~

*A compressor graph in Plugin workshop.*


~~~image
{"id": "figure-graph-export", "src": "assets/graph-export-en.svg", "alt": "Publication, interface and presets precede native compilation.", "title": "Publication, interface and presets precede native compilation.", "controls": "minimal"}
~~~

*Publication, interface and presets precede native compilation.*


## Start and audition {#start}
Choose effect for audio input-to-output processing; choose instrument for MIDI-to-audio sound. Build the smallest working path first. In the simulator, use a test source or note pattern and the on-screen keyboard. Test on a track links changes to a chosen compatible track; None detaches the audition link and leaves separate copies. Keep track/master volume moderate when first testing feedback or resonance.

## Define the product {#product}
Set name, author/vendor, category, description and stable plugin identity. Choose typed E/S and only the parameters the user/host should see. Make initial values musically useful. Create programs for meaningful variations and test loading them. Arrange both full-window and module-card surfaces; a bare graph with published values is not yet a designed instrument interface.

## Save and verify {#save}
Save the device in My devices for reuse. Export CyDAW writes an editable .cydawdevice snapshot including used module definitions, interfaces, custom source and preset banks. It lists external audio instead of copying every sample bank. Opening the file on another CyDAW needs compatible schema and media; compiling C++ there needs that machine's compiler.

Run export verification before building. Resolve listed unsupported nodes, boundaries or parameter adapters. Native compilation uses the chosen source tree/toolchains and cached builds; deleting the cache forces preparation again. Build selected targets sequentially and inspect each result, then validate it in an actual host.

See [graph design](doc:graphs), [interface design](doc:interface-design) and [format/platform export](doc:plugin-formats).
