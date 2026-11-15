"""Replace a local piano comparison template with an existing Standard MIDI file.

Requires mido. Song files and generated projects remain in ignored local folders.
No transcription, quantisation, added notes or velocity normalisation is performed.
"""
from __future__ import annotations

import argparse
from collections import defaultdict, deque
import copy
import hashlib
import json
from pathlib import Path
import shutil

import mido


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_piano(path):
    midi = mido.MidiFile(path)
    if midi.type == 2 or midi.ticks_per_beat <= 0:
        raise ValueError('A synchronous MIDI file with PPQ timing is required')
    active = defaultdict(deque)
    pedals = {}
    spans = defaultdict(list)
    notes = []
    seconds = 0.0
    tempo = 500000
    tempos = []
    for message in mido.merge_tracks(midi.tracks):
        seconds += mido.tick2second(message.time, midi.ticks_per_beat, tempo)
        if message.type == 'set_tempo':
            tempo = message.tempo
            tempos.append(dict(seconds=seconds, microsecondsPerQuarter=tempo))
        elif message.type == 'control_change' and message.control == 64:
            channel = message.channel
            if message.value >= 64:
                pedals.setdefault(channel, seconds)
            elif channel in pedals:
                spans[channel].append((pedals.pop(channel), seconds))
        elif message.type == 'note_on' and message.velocity:
            active[message.channel, message.note].append((seconds, message.velocity))
        elif message.type == 'note_off' or message.type == 'note_on' and not message.velocity:
            key = message.channel, message.note
            if not active[key]:
                raise ValueError(f'Unmatched note-off: {message}')
            start, velocity = active[key].popleft()
            if seconds <= start:
                raise ValueError(f'Non-positive note duration: {message}')
            notes.append(dict(channel=message.channel, note=message.note, start=start,
                              keyEnd=seconds, end=seconds, velocity=velocity))
    if any(active.values()):
        raise ValueError('MIDI contains notes without a matching note-off')
    for channel, start in pedals.items():
        spans[channel].append((start, seconds))
    notes.sort(key=lambda n: (n['start'], n['channel'], n['note']))
    # CyDAW clips store notes, not a CC64 lane. Bake pedal-up into release times.
    # Pedal is shared across source tracks on the same MIDI channel (both hands).
    extended = 0
    for note in notes:
        for start, end in spans[note['channel']]:
            if start <= note['keyEnd'] < end:
                note['end'] = end
                extended += 1
                break
    # A retrigger terminates the preceding sustained instance of the same key.
    previous = {}
    for note in notes:
        key = note['channel'], note['note']
        if key in previous:
            previous[key]['end'] = min(previous[key]['end'], note['start'])
        previous[key] = note
    if not notes or any(n['end'] <= n['start'] for n in notes):
        raise ValueError('No playable notes, or overlapping simultaneous key starts')
    return notes, dict(sourceDurationSeconds=seconds, tempoChanges=tempos,
                       sourceTracks=len(midi.tracks), sourceNotes=len(notes),
                       pedalSpans=sum(map(len, spans.values())), sustainedNotes=extended)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--midi', type=Path, required=True)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--title', required=True)
    parser.add_argument('--credit', required=True)
    parser.add_argument('--source-url', required=True)
    parser.add_argument('--install', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    original_hash = sha(args.project)
    backup = args.output / 'previous.cydaw'
    shutil.copy2(args.project, backup)
    notes, summary = read_piano(args.midi)
    project = json.loads(backup.read_text(encoding='utf-8-sig'))
    if len(project.get('midiTracks', [])) != 2:
        raise ValueError('Expected a piano template with primary and muted alternative tracks')
    bpm = 60000000 / summary['tempoChanges'][0]['microsecondsPerQuarter'] if summary['tempoChanges'] else 120.0
    duration = max(summary['sourceDurationSeconds'], max(n['end'] for n in notes)) + 3.0
    # Preserve elapsed timing through every tempo change. CyDAW's current project
    # schema has a global tempo, so beats below are mapped from source seconds.
    converted = [dict(note=n['note'], startBeats=n['start'] * bpm / 60,
                      durationBeats=(n['end'] - n['start']) * bpm / 60,
                      velocity=n['velocity'] / 127) for n in notes]
    project.update(tracks=[], midiNotes=copy.deepcopy(converted), tempo=bpm,
                   timeSignatureNumerator=4, timeSignatureDenominator=4,
                   masterGainDb=-1.0, position=0.0, loop=False, loopRangeExplicit=True,
                   loopStart=0.0, loopEnd=duration, arrangementPlayback=True, editorMode=1,
                   launcherActionReplay=False, launcherActions=[])
    for i, track in enumerate(project['midiTracks']):
        track.update(name=('Steinway' if i == 0 else 'Kawai') + ' - ' + args.title,
                     gainDb=-4.0 if i == 0 else -8.0, mute=i != 0, solo=False, armed=False,
                     outputs=[dict(gainDb=0.0, master=True)], gainAutomation=[],
                     parameterAutomation=[], shownAutomation=[])
        clip = copy.deepcopy(track['clips'][0])
        clip.update(name=args.title + ' - MIDI', startBeats=0.0, lengthBeats=duration*bpm/60,
                    playStartBeats=0.0, playEndBeats=duration*bpm/60,
                    loopStartBeats=0.0, loopEndBeats=duration*bpm/60, loop=False,
                    notes=copy.deepcopy(converted), gainAutomation=[])
        track['clips'] = [clip]
        for node in track['nodeGraph']['nodes']:
            if node['type'] == 'envelope':
                node['parameters']['releaseMilliseconds'] = 300.0
            elif node['type'] == 'reverb':
                node['parameters']['mix'] = 0.08
    args.project.write_text(json.dumps(project, ensure_ascii=False) + '\n', encoding='utf-8')
    for destination in args.install:
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.project, destination / args.project.name)
    summary.update(sourceMidi=str(args.midi.resolve()), sourceSha256=sha(args.midi),
                   sourceUrl=args.source_url, credit=args.credit,
                   rights='Third-party transcription; no redistribution licence inferred.',
                   project=str(args.project.resolve()), previousSha256=original_hash,
                   projectSha256=sha(args.project), projectTempo=bpm, durationSeconds=duration,
                   timing='All tempo changes integrated to seconds; mapped to global project beats.',
                   sustain='CC64 baked into note lengths, capped at next same-key onset.',
                   ignoredControllers='CC2, CC7, CC10, CC91, CC93, CC121; piano graph controls its own sound.',
                   installed=[str((p/args.project.name).resolve()) for p in args.install])
    (args.output/'import.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    print(json.dumps(summary, ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
