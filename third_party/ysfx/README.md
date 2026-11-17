# ysfx

JSFX hosting library — CyDaw vendored subset

This copy retains the compiler/runtime library from [jpcima/ysfx](https://github.com/jpcima/ysfx). The optional upstream JUCE plugin wrapper, its build configuration and its CI workflow have been removed. CyDaw hosts JSFX through CyAudioCore; it does not build the upstream plugin. See LICENSE and the retained third-party notices.

## Description

This package provides support for audio and MIDI effects developed with the JSFX
language. These effects exist in source code form, and they are compiled and ran
natively by hosting software.

This contains a hosting library, providing a JSFX compiler and runtime.


## Remarks

Despite what the name might suggest, JSFX is not JavaScript.
These technologies are unrelated to each other.

This project is not the work of Cockos, Inc; however, it is based on several
free and open source components from the WDL. Originally, this project was based
on jsusfx by Pascal Gauthier, but then it became an entire rewrite made from
scratch.

Some time after the realization of this project, Cockos announced the release of
JSFX as open source under the LGPL. This does not affect the development of this
project, which remains a custom implementation based on the liberally licensed
bits of the WDL.

# Usage notes

The effects are source code files which end with the extension `.jsfx`, or with
no extension at all.

Note that, unlike REAPER, ysfx will let you install JSFX wherever you want.
If you use effects from a custom folder, ysfx will usually figure things out, but
not always.

The ideal hierarchy is one where there exists at least a pair of folders named
"Effects" and "Data" side-by-side, which receive the code files and resource files
respectively.

Example:
- `My JSFX/Effects/guitar/amp-model`
- `My JSFX/Data/amp_models/Marshall JCM800 - Marshall Stock 70.wav`

## Building

CyDaw uses the runtime supplied by CyAudioCore. This vendored library subset
keeps the library CMake entry point; it has no plugin export target.

## JSFX resources

Great free and open-source effects:

- Saike: https://github.com/JoepVanlier/JSFX
- Geraint Luff: https://github.com/geraintluff/jsfx
- Justin Johnson: https://github.com/Justin-Johnson/ReJJ
