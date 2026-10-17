# Transport dependencies

These dependencies are used only by CyDAW 2, not by exported native VST3 plug-ins.

* Ableton Link 3.1.2, commit `41d9aa111f702e78b6fbaee9d3e06dda1db6420d`, https://github.com/ableton/link . Unmodified upstream headers and build configuration. GPL-2.0-or-later; see ableton-link/LICENSE.md and GNU-GPL-v2.0.md.
* Standalone Asio 1.28.0, commit `c465349fa5cd91a64bb369f5131ceacab2c0c1c3`, https://github.com/chriskohlhoff/asio . Headers in ableton-link/modules/asio-standalone/asio/include. Boost Software License 1.0, retained alongside the headers. This is the networking library, not the Steinberg ASIO audio driver SDK.
* JACK2 1.9.22 headers, commit `4f58969432339a250ce87fe855fb962c67d00ddb`, https://github.com/jackaudio/jack2 . Unmodified jack.h, transport.h, types.h, systemdeps.h and weakmacros.h. LGPL-2.1-or-later, see jack-transport/COPYING and header notices. The JACK library is dynamically loaded at runtime and is not redistributed here.

No build-time download is required. Link support uses only its transport API. JACK transport support is independent of JUCE_JACK and opens no audio or MIDI ports.
