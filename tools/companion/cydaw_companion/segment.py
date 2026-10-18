"""The notes in a longer recording: where each starts and ends, its pitch and its level.

A phrase played on an instrument (or a few notes one after another) becomes notes the analysis
takes one by one, as if each had been recorded alone:

1. onsets: the spectral flux (the rise of the log spectrum, summed over the bins, every 10 ms)
   above its running median by a margin, and every rise out of silence;
2. the pitch over time: YIN (pitch.py), or CREPE when torchcrepe is installed and asked for ("auto":
   CREPE when it is there, YIN otherwise);
   a note also ends where the pitch moves by more than 0.7 semitone and stays there (legato);
3. each note: from its onset to the next one (or until it has died away), its pitch the median
   of its voiced frames, its velocity from its peak level against the loudest note's (1.0; 36 dB
   under it, the softest);
4. the zones: per pitch, up to three velocity layers, each the longest clean take (one that
   rang out before the next note came) of its level.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .pitch import hz_to_midi, yin

FRAME_SECONDS = 0.01


@dataclass
class Note:
    start: int           # samples
    end: int             # samples
    midi: float          # the pitch (MIDI, fractional)
    velocity: float      # 0..1, as analysis.py reads a note's level
    peak_db: float       # its peak, dBFS
    clean: bool          # it rang out (fell 30 dB) before the next note
    voiced: float        # the share of its frames with a pitch

    def seconds(self, rate: int) -> float:
        return (self.end - self.start) / rate


def _frames(signal: np.ndarray, size: int, hop: int) -> np.ndarray:
    padded = np.concatenate([np.zeros(size // 2), signal, np.zeros(size)])
    count = max(1, int(np.ceil(len(signal) / hop)))
    return np.lib.stride_tricks.sliding_window_view(padded, size)[np.arange(count) * hop]


def _crepe_pitch(signal: np.ndarray, rate: int, hop: int, count: int) -> tuple[np.ndarray, np.ndarray] | None:
    """CREPE's pitch and periodicity per frame, when torchcrepe is there; None otherwise."""
    try:
        import torch  # noqa: PLC0415
        import torchcrepe  # noqa: PLC0415
    except Exception:  # noqa: BLE001 - optional
        return None
    from .audio_io import resample  # noqa: PLC0415
    audio = resample(signal, rate, 16000) if rate != 16000 else signal
    device = "cuda" if torch.cuda.is_available() else "cpu"
    tensor = torch.tensor(audio, dtype=torch.float32).unsqueeze(0)
    pitch, periodicity = torchcrepe.predict(tensor, 16000, hop_length=160, fmin=30.0, fmax=2000.0, model="full",
                                            return_periodicity=True, batch_size=512, device=device)
    pitch = pitch.squeeze(0).cpu().numpy()
    periodicity = periodicity.squeeze(0).cpu().numpy()
    # CREPE's 10 ms frames onto ours.
    times = np.arange(count) * hop / rate
    source = np.arange(len(pitch)) * 0.01
    return np.interp(times, source, pitch), np.interp(times, source, periodicity)


def detect_notes(signal: np.ndarray, rate: int, pitch: str = "auto") -> tuple[list[Note], str]:
    """The notes of a mono recording, in time order, and the pitch tracker used."""
    signal = np.asarray(signal, dtype=np.float64)
    hop = int(round(FRAME_SECONDS * rate))
    size = 2048 if rate <= 48000 else 4096
    frames = _frames(signal, size, hop)
    count = len(frames)
    window = np.hanning(size + 1)[:-1]
    spectrum = np.abs(np.fft.rfft(frames * window, axis=1))
    log_spectrum = np.log(spectrum + 1e-6)
    level = 20.0 * np.log10(np.sqrt(np.mean(frames ** 2, axis=1)) + 1e-12)
    loudest = float(level.max()) if count else -120.0
    silence = loudest - 50.0

    # 1. Onsets: the flux above its running median, and rises out of silence.
    flux = np.concatenate([[0.0], np.sum(np.maximum(np.diff(log_spectrum, axis=0), 0.0), axis=1)])
    flux[level < silence] = 0.0
    span = 25
    padded = np.pad(flux, span, mode="edge")
    median = np.median(np.lib.stride_tricks.sliding_window_view(padded, 2 * span + 1), axis=1)[:count]
    spread = np.median(np.abs(flux - median)) + 1e-9
    peaks = [index for index in range(1, count - 1)
             if flux[index] > median[index] + 6.0 * spread and flux[index] >= flux[index - 1] and flux[index] >= flux[index + 1]]
    # Out of silence: 50 ms of it before, and a rise of 6 dB (a note's tail that grazes the
    # floor and comes back is none).
    rises = [index for index in range(5, count)
             if level[index] >= silence and np.all(level[index - 5:index] < silence)
             and level[index:index + 4].max() - level[index - 5:index].min() >= 6.0]
    if count and level[0] >= silence:
        rises.insert(0, 0)
    onsets: list[int] = []
    for index in sorted(set(peaks) | set(rises)):
        # 60 ms at least between two notes; a strong flux peak on a quiet frame is no note, nor
        # one the level does not rise at (3 dB within 30 ms: the ringing of a note going on).
        if level[index:index + 5].max() < silence:
            continue
        if index not in rises and level[index:index + 4].max() - level[max(0, index - 3):index + 1].min() < 3.0:
            continue
        if onsets and index - onsets[-1] < 6:
            continue
        onsets.append(index)

    # 2. The pitch over time.
    used = "yin"
    track = None
    if pitch in ("crepe", "auto"):
        crepe = _crepe_pitch(signal, rate, hop, count)
        if crepe is not None:
            f0, periodicity = crepe
            track = np.where(periodicity > 0.5, f0, 0.0)
            used = "crepe"
    if track is None:
        found = yin(signal, rate, hop)
        track = np.zeros(count)
        track[:min(count, len(found.f0))] = found.f0[:count]
    midi = np.where(track > 0, hz_to_midi(np.maximum(track, 1e-6)), np.nan)

    # Legato: a new note where the pitch moves 0.7 semitone and stays 80 ms.
    smoothed = midi.copy()
    for index in range(2, count - 2):
        window_values = midi[index - 2:index + 3]
        if np.all(np.isfinite(window_values)):
            smoothed[index] = np.median(window_values)
    boundaries = list(onsets)
    for first, last in zip(onsets, onsets[1:] + [count]):
        current = None
        index = first
        while index < last:
            value = smoothed[index]
            if np.isfinite(value):
                if current is None:
                    current = value
                elif abs(value - current) > 0.7:
                    ahead = smoothed[index:min(last, index + 8)]
                    if len(ahead) >= 8 and np.all(np.isfinite(ahead)) and np.all(np.abs(ahead - value) < 0.5):
                        boundaries.append(index)
                        current = value
                        index += 8
                        continue
                else:
                    current = 0.9 * current + 0.1 * value
            index += 1
    boundaries = sorted(set(boundaries))

    # 3. The notes.
    notes: list[Note] = []
    for number, first in enumerate(boundaries):
        last = boundaries[number + 1] if number + 1 < len(boundaries) else count
        own = level[first:last]
        if not len(own):
            continue
        peak_frame = first + int(np.argmax(own))
        peak = float(level[peak_frame])
        if peak < silence:
            continue
        # Where it has died away: 50 dB under its peak (or the next note).
        quiet = np.nonzero(level[peak_frame:last] < peak - 50.0)[0]
        end_frame = peak_frame + int(quiet[0]) if len(quiet) else last
        tail = level[peak_frame:last]
        clean = bool(len(tail) and tail.min() < peak - 30.0)
        voiced_values = midi[first:end_frame]
        voiced_values = voiced_values[np.isfinite(voiced_values)]
        voiced = len(voiced_values) / max(1, end_frame - first)
        pitch_value = float(np.median(voiced_values)) if len(voiced_values) else float("nan")
        start = max(0, first * hop - int(0.01 * rate))
        # Never into the next note.
        end = min(len(signal), end_frame * hop + int(0.02 * rate), last * hop if last < count else len(signal))
        if end - start < int(0.12 * rate):
            continue
        sample_peak = 20.0 * np.log10(max(np.max(np.abs(signal[start:end])), 1e-9))
        notes.append(Note(start=start, end=end, midi=pitch_value, velocity=1.0, peak_db=float(sample_peak), clean=clean,
                          voiced=float(voiced)))
    # Velocities: against the loudest note of the recording.
    if notes:
        loudest_note = max(note.peak_db for note in notes)
        for note in notes:
            note.velocity = float(np.clip(1.0 + (note.peak_db - loudest_note) / 36.0, 0.1, 1.0))
    return notes, used


def choose_notes(notes: list[Note], rate: int, layers: int = 3) -> list[Note]:
    """The zones a model keeps: per pitch, up to `layers` velocity layers, each the longest clean
    take of its level (pitched notes only)."""
    # A zone is a take of a quarter of a second at least.
    pitched = [note for note in notes if np.isfinite(note.midi) and note.voiced >= 0.3 and note.seconds(rate) >= 0.25]
    by_pitch: dict[int, list[Note]] = {}
    for note in pitched:
        by_pitch.setdefault(int(round(note.midi)), []).append(note)
    chosen: list[Note] = []
    for pitch in sorted(by_pitch):
        takes = sorted(by_pitch[pitch], key=lambda note: note.velocity)
        # Layers: takes within 0.08 of velocity stand for one level.
        groups: list[list[Note]] = []
        for note in takes:
            if groups and note.velocity - groups[-1][0].velocity < 0.08:
                groups[-1].append(note)
            else:
                groups.append([note])
        if len(groups) > layers:
            # The softest, the loudest and the ones between, evenly.
            picks = np.unique(np.round(np.linspace(0, len(groups) - 1, layers)).astype(int))
            groups = [groups[index] for index in picks]
        for group in groups:
            best = max(group, key=lambda note: (note.clean, note.seconds(rate)))
            chosen.append(best)
    return chosen
