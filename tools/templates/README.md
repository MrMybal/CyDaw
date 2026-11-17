# Local project templates

## Foundry, Cinema and DJ Mix

`build_mode_examples.py` synthesises original audio and a short reference video,
then builds two templates for each mode. Pass `--control-port` for an isolated
development instance to attach the native processing graphs and cinema bus sends.
`--install <executable-folder>/templates` stages the mode folders and their shared
media next to the app. See [usage and verification](../../docs/ModeTemplates.fr.md).

These examples use no personal banks or external recordings. Generated sessions
and media stay in the ignored `templates/` and `output/` directories.

## Piano MIDI replacement

`replace_piano_midi.py` replaces the notes of an existing two-piano comparison
template with a Standard MIDI file. It requires the Python `mido` package.

Example (PowerShell, using a MIDI file already downloaded locally):

```powershell
python tools/templates/replace_piano_midi.py `
  --midi output/medellin-midi/medellin-hmmm.mid `
  --project templates/19-medellin-comparaison-midi.cydaw `
  --output output/medellin-midi/new-import `
  --title "Medellin - Sofiane Pamart" `
  --credit "Hmmm - unofficial transcription, album version" `
  --source-url "https://www.youtube.com/watch?v=7rGOcFEa30k" `
  --install build/cyaudio-032/CyDAW2_artefacts/Release/templates
```

The output directory must be new. It receives the previous project and an import
report with the source hash, credit and conversion details. The primary Steinway
is active; the alternative Kawai is muted. The old reference-audio track is removed.
The script does not download, transcribe, quantise or generate musical content.

All source tempo changes are integrated into elapsed time before converting to
CyDAW's global-tempo beat coordinates. This preserves playback timing, but does
not create a tempo/signature map in the editor. CC64 sustain extends note lengths
until pedal-up or the next same-key onset, since these clips contain notes rather
than a pedal-controller lane. Other controllers are not imported. Notes and
velocities remain editable in the normal piano roll.

The song MIDI, project templates, bank samples, backups and rendered audio stay
local in ignored folders. A public download does not establish a redistribution
licence; the import report records provenance without assigning CyDAW's licence
to third-party musical content.
