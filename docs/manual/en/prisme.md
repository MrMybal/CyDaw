# Prisme: wavetable synthesis

Prisme is the dedicated wavetable instrument. A table contains a succession of wave shapes; table position scans through them. Oscillator pitch, table choice, position and modulation are distinct settings. Use its expanded editor to see the source wave, filter, envelopes and modulation destinations.

![A voice architecture useful when designing a wavetable patch.](../assets/midi-voice-en.svg)

## Establish the tone {#tone}
Create a Prisme instrument track, initialise or load a factory preset, then reduce the patch to one audible source. Choose a table, set oscillator octave/fine tuning and sweep position. Increase unison gradually and compensate output level. Mix additional sources only after each has a purpose. Check low notes and chords: a rich table plus high resonance can produce a very different balance across the keyboard.

## Animate the patch {#modulation}
Set amp Attack/Decay/Sustain/Release for the intended articulation. Add filter motion with an envelope or LFO, then adjust depth and polarity rather than only source speed. Drag modulation sources onto supported destinations in the editor; inspect their existing assignments before adding more. Choose free versus tempo-synchronised rates and retrigger behaviour deliberately. Macros are useful for performance controls that coordinate several destinations.

## Save and transfer {#presets}

Audition the preset through velocity, pitch bend and modulation wheel, then save it with a name describing its role. A track preset, Prisme preset and complete device graph carry different scopes. When exporting a graph, check its published controls and designed interface before compiling. Use the generated parameter reference for exact engine keys and current ranges rather than guessing a value from knob appearance.

Continue with [synth basics](synths.md), [graph exposure](graphs.md#exposure) and [plugin export](plugin-formats.md).
