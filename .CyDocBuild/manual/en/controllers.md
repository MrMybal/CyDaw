---
id: controllers
title: MIDI, OSC and hardware controllers
description: Learn hardware controls and configure feedback.
section: Manual / Build and connect
order: 28
revision: be7cf38d3e9d0314
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

## Direct MIDI and macros {#direct-midi}

In a rule, use **+ Direct MIDI destination**, choose CC, Note, Pitch bend or Pressure, then set channel and number. Messages enter CyDAW's live MIDI input; arm/select the instrument track and configure its live input as usual. This does not install a virtual MIDI port for other applications.

A macro is one rule with several destinations. For example, map a gamepad's left stick to pitch bend and CC 74, and also to a track parameter. Each destination has its own output range; transformations before the destinations are shared. Keyboard, MIDI, OSC and application-plugin sources use the same rules. Add **Drawn curve** to the transformation chain, drag its 17 knots, or double-click the graph to restore a straight line. The response is interpolated between knots. A non-monotonic curve has several possible inverses; feedback uses its first nearest segment.

On Windows, the desktop application captures computer keyboard and mouse through a dedicated Raw Input service while CyDAW has focus. Musical key presses and mapped MIDI destinations do not wait for an interface frame. Text fields and Ctrl/Alt/Windows shortcuts suppress computer-keyboard controls. Focus loss, device removal, remapping and shutdown release held notes. Computer keyboards and mice are currently grouped into one source per family. Other platforms retain the keyboard UI fallback; exported plugin instances do not register this desktop capture.

Mouse addresses are `button/left`, `button/right`, `button/middle`, `button/x1`, `button/x2`, normalized screen position `axis/x`, `axis/y`, and relative `wheel`. Use **Relative** with two's-complement encoding for the wheel. The XInput application plugin supplies each connected Xbox-compatible controller, including sticks, triggers and buttons. Other hardware can provide raw addresses through the public `.cyctl` controller-plugin ABI; arbitrary HID protocols are not decoded automatically.

MIDI conversion runs on the input/provider thread. Commands that modify the DAW are dispatched safely on the application thread and can wait if that thread is busy. The audio callback does not execute controller configuration or UI commands. Controller definitions, curves and global MIDI assignments are saved with the application settings; existing project-specific DAW assignments remain in the project.

## Scope and persistence {#scope}
Ctrl+M or the CTRL context menu activates interface learning: click a highlighted control, then move hardware. Escape cancels learning. Track/device targets belong to the project; transport, DJ, master and selected-track targets may be global. Changing the device's input/return carries its rules. Duplicate a device for similar hardware, then identify the new input before using it.


See [MIDI setup](doc:setup#midi), [modulators](doc:modulators) and [transport sync](doc:sync).
