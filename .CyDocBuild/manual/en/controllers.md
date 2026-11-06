---
id: controllers
title: MIDI, OSC and hardware controllers
description: Learn hardware controls and configure feedback.
section: Manual / Build and connect
order: 28
revision: 84e4e35ae2586777
---

## Overview {#overview}

Open CTRL or Options > MIDI/OSC controllers. Devices identify hardware; rules translate its controls into CyDAW targets. This mapping is separate from simply enabling MIDI notes on an instrument track. Learning can start from hardware or from a highlighted software control.


~~~image
{"id": "figure-automation", "src": "assets/automation-en.svg", "alt": "A hardware rule changes a parameter; automation and modulation can also address it.", "title": "A hardware rule changes a parameter; automation and modulation can also address it.", "controls": "minimal"}
~~~

*A hardware rule changes a parameter; automation and modulation can also address it.*


## Create and learn {#learn}
Add a device, move a control to identify its input or select the input manually. MIDI, computer keyboard, supported gamepad plugin and OSC endpoints are distinct families. For OSC create a UDP endpoint and set listening/return host/ports; it is controller traffic, not audio streaming.

Enable Learn in the device rules and move each control. Select a rule, name it and choose knob/fader/encoder/button/toggle/pad/axis behaviour. The Notes option controls whether its MIDI input also plays the selected instrument. Feedback chooses an output for LEDs, motor faders, tablet state or supported gamepad response.

## Transform and assign {#rules}
Input range, inversion, curve, relative-encoder decoding, toggle, threshold, dead zone, steps and output range act in order; feedback uses the inverse path where supported. Encoders need the correct relative format. Add one or several targets, by search or Click target in CyDAW. Set target range and pickup so a hardware fader can catch the current value without jumping.

MIDI addresses include cc/channel/number, note/channel/note, pb/channel, at/channel and pc/channel/program. Keyboard uses key/Name; OSC uses its received address. Check the monitor to separate missing hardware events from a wrong target binding.

## Scope and persistence {#scope}
Ctrl+M or the CTRL context menu activates interface learning: click a highlighted control, then move hardware. Escape cancels learning. Track/device targets belong to the project; transport, DJ, master and selected-track targets may be global. Changing the device's input/return carries its rules. Duplicate a device for similar hardware, then identify the new input before using it.


See [MIDI setup](doc:setup#midi), [modulators](doc:modulators) and [transport sync](doc:sync).
