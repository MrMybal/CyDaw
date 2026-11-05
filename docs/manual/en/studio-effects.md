# EQ, dynamics, reverb and filtering

Select a track and add an effect from the normal Devices library. Compact cards expose common settings; expand for the full visual editor and detailed controls. Use bypass and compare at similar loudness. The parameter reference records current engine keys, ranges and defaults.

![Follow processing order and check the level entering every stage.](../assets/signal-flow-en.svg)

## EQ and Filter Pro {#eq}
Égaliseur Pro shapes frequency bands, including dynamic behaviour where enabled. Start with broad changes and listen to the whole mix; a narrow resonant boost can look small and sound large. Dynamic bands react to the signal and need threshold/range settings as well as frequency and Q. Filtre Pro provides routing and filter character options: choose type/slope, cutoff and resonance before drive or modulation. Drag visual points for coarse changes, then check numeric values.

## Compressor Pro and Limiter Pro {#dynamics}

Compression reduces level above a threshold according to ratio/knee and time constants. Set threshold to obtain the desired reduction, attack for transient retention and release for recovery; Mix blends parallel compression. Range limits gain reduction. Detector filters and sidechain determine what drives it, not what the output EQ sounds like. Styles alter response.

Limiter Pro handles final peaks with input gain, ceiling, lookahead, attack/release, oversampling/true-peak and style controls as offered. Watch gain reduction and output loudness; pushing more gain increases limiting, not guaranteed clarity. Reset loudness measurements before a full pass. Measurements and approximations displayed by CyDAW should not be confused with a certified delivery report.

## Reverb Pro {#reverb}
Set decay and room size, then predelay to separate the source from reflections. Drag low/mid/high decay handles to shape frequency-dependent tails; damping is different from simply lowering output. Width, distance, diffusion, early reflections and modulation change space. Ducking reduces reverb while the source is active; Freeze can sustain it indefinitely. Keep a suitable tail when exporting. On a send return, use a wet treatment and set balance with the send.

See [sidechains](mixing.md#sidechain), [mix effects](mixing-effects.md) and [effect reference](reference-effects.md).
