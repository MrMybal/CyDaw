"""The sound model CyDAW plays: a note as harmonics and noise over time.

File format "cydaw-sound-model", version 1 (JSON, `.cysound`):

    {
      "format": "cydaw-sound-model", "version": 1,
      "name": "...", "source": ["file.wav", ...],
      "zones": [ zone, ... ],          # one per analysed note / velocity
      "musical": { ... }               # readable parameters (ADSR, brightness...)
    }

A zone:

    {
      "rootNote": 57, "f0": 220.3, "velocity": 0.8, "gain": 1.0,
      "frameSeconds": 0.01, "frames": N,
      "inharmonicity": B,              # partial k at k f0 sqrt(1 + B k^2)
      "ratios": [K] | absent,          # partials that are no harmonics (a bar, a bell): partial
                                       # k at ratios[k-1] f0 instead (B unused)
      "onset": seconds | absent,       # where its attack is (the zones played together meet there)
      "string": {...} | absent,        # a plucked string played instead of the partials (see
                                       # string_model.py)
      "pitchCents": [N],               # deviation from f0 per frame (cents)
      "harmonics": {"count": K, "db10": rows},
                                       # partial amplitudes (sine peak; in stereo, the RMS of the
                                       # two channels'), dB x 10, -1200 = silent
      "noise": {"edges": [B + 1] Hz, "db10": rows},
                                       # one-sided noise PSD per band (amplitude^2/Hz; in stereo, the
                                       # mean of the two channels'), dB x 10
      "attack": {"rate": 48000, "gain": 1.0, "float32": "<base64>"} | null,
                                       # the transient the harmonics miss, played once at note-on
      "loop": {"start": f, "end": f} | null,
                                       # held between these frames while the key is down (ping-pong)
      "release": f | null,             # where the release begins (key up jumps there)
      "frameTimes": [M] | absent,      # compact: the frames the rows are at (see below)
      "stereo": {...} | absent,        # the two channels (see below)
      "head": {...} | absent           # the recording's first moments (see below)
    }

Every curve is sampled every frameSeconds; the player interpolates linearly, between key frames
(a compact zone) the linear values (amplitudes, powers).

Rows ("db10"): a list of lists, base64 of little-endian int16 rows, or "z:" + base64 of the zlib
stream of those int16 rows each written as its difference from the row before (modulo 2^16; the
first as it is): curves that change slowly shrink several times.

Compact zones. A note that rings for half a minute is thousands of frames, most of them on a
slow decay. Written, a zone that does not loop keeps every frame of its first 0.3 s, then one
frame in 2 to 1 s, one in 3 to 3 s and one in 5 after; a looped (sustained) one keeps one in 2
after 0.3 s, its vibrato with it. Each kept frame past 0.3 s holds the median of the frames it
stands for (the analysis' frame-to-frame jitter goes with it); the loop and release frames are
always kept. "frameTimes" lists the kept frames (ascending, 0 to frames - 1), the "pitchCents"
and "db10" rows are theirs, and a reader rebuilds the frames between by interpolating. "frames"
stays the full count.

Stereo. A recording in two channels keeps, besides the partials' RMS and the noise's mean:

    "stereo": {
      "step": S,                       # a row every S frames (0, S, 2S... to the last)
      "balance": rows,                 # per partial, dB x 10 of right / left amplitude
      "phase": [K],                    # per partial, the right's phase minus the left's (radians)
      "noiseBalance": rows,            # per band, dB x 10 of right / left PSD
      "noiseCoherence": [B]            # per band, 0..1: how alike the two noises are
    }

A partial of RMS amplitude a and balance b (r = 10^(b/20)) is a * sqrt(2 / (1 + r^2)) on the
left and r times that on the right, its phase there moved by "phase"; a band of mean PSD p and
balance n (q = 10^(n/10)) is 2p / (1 + q) on the left and q times that on the right, the right's
noise c times the left's and sqrt(1 - c^2) times a noise of its own.

Head. The first moments of the recording, played as they are (its strike, breath or pluck, which
the model would only approximate), the model coming in under it:

    "head": {
      "rate": 48000, "channels": 1 | 2, "length": L, "fade": F,
      "gain": g, "int16": "<base64>",  # L frames, interleaved, times g
      "phase": [K]                     # per partial, its phase (sine) on the first channel at
                                       # L - F samples
    }

The head plays from the note-on; over its last F samples it fades out (linearly) while the model
fades in, its partials started at those phases so the two meet in phase. Until then the model is
silent and its frames follow the head's time. A zone with a head has no "attack".
"""
from __future__ import annotations

import base64
import json
import zlib
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

FORMAT = "cydaw-sound-model"
VERSION = 1
SILENT_DB10 = -1200
NOISE_FLOOR_DB10 = -2000
# Compaction (see the docstring): every frame first, then one in COMPACT_STEPS[i] up to frame
# COMPACT_LIMITS[i]; a looped zone one in COMPACT_SUSTAINED_STEP after the first.
COMPACT_FIRST_FRAMES = 30
COMPACT_STEPS = (2, 3, 5)
COMPACT_LIMITS = (100, 300)
COMPACT_SUSTAINED_STEP = 2
# A stereo row every this many frames: the image moves slowly.
STEREO_STEP = 25
BALANCE_LIMIT_DB10 = 400


def key_frames(frames: int, sustained: bool, forced: list[int]) -> np.ndarray:
    """The frames a compact zone keeps."""
    must = sorted(set(int(f) for f in forced if 0 <= f < frames))
    kept = []
    frame = 0
    while frame < frames:
        kept.append(frame)
        if frame < COMPACT_FIRST_FRAMES:
            step = 1
        elif sustained:
            step = COMPACT_SUSTAINED_STEP
        elif frame < COMPACT_LIMITS[0]:
            step = COMPACT_STEPS[0]
        elif frame < COMPACT_LIMITS[1]:
            step = COMPACT_STEPS[1]
        else:
            step = COMPACT_STEPS[2]
        frame += step
    return np.asarray(sorted(set(kept) | set(must) | {frames - 1}), dtype=np.int64)


def median_rows(table: np.ndarray, kept: np.ndarray) -> np.ndarray:
    """Each kept frame's row: past the first frames, the median of the frames it stands for."""
    out = np.empty((len(kept),) + table.shape[1:], dtype=np.float64)
    for index, frame in enumerate(kept):
        if frame < COMPACT_FIRST_FRAMES:
            out[index] = table[frame]
            continue
        low = frame if index == 0 else min(frame, (kept[index - 1] + frame) // 2 + 1)
        high = frame if index == len(kept) - 1 else max(frame, (frame + kept[index + 1]) // 2)
        out[index] = np.median(table[low:high + 1], axis=0)
    return out


def encode_rows(rows: np.ndarray) -> str:
    """"z:" + base64 of the zlib stream of the int16 rows, each as its difference from the last."""
    values = np.asarray(np.round(rows), dtype=np.int64)
    if values.ndim == 1:
        values = values[:, None]
    deltas = values.copy()
    deltas[1:] = values[1:] - values[:-1]
    data = np.asarray(deltas & 0xFFFF, dtype="<u2").tobytes()
    return "z:" + base64.b64encode(zlib.compress(data, 9)).decode("ascii")


def decode_rows(value, rows: int, columns: int) -> np.ndarray:
    """Rows of dB x 10 integers as floats: a list of lists, plain base64 or "z:" (see encode_rows)."""
    if not columns:
        return np.zeros((rows, 0))
    if isinstance(value, str):
        if value.startswith("z:"):
            data = np.frombuffer(zlib.decompress(base64.b64decode(value[2:])), dtype="<u2").astype(np.int64)
            deltas = data.reshape(rows, columns)
            values = np.cumsum(deltas, axis=0) & 0xFFFF
            return np.where(values >= 0x8000, values - 0x10000, values).astype(np.float64)
        data = np.frombuffer(base64.b64decode(value), dtype="<i2").astype(np.float64)
        return data.reshape(rows, columns)
    return np.asarray(value, dtype=np.float64).reshape(rows, columns)


def expand_rows(rows: np.ndarray, times: np.ndarray, frames: int) -> np.ndarray:
    """Rows at key frames back to every frame, interpolated linearly per column."""
    grid = np.arange(frames, dtype=np.float64)
    rows = np.asarray(rows, dtype=np.float64)
    if rows.ndim == 1:
        return np.interp(grid, times, rows)
    out = np.empty((frames, rows.shape[1]))
    for column in range(rows.shape[1]):
        out[:, column] = np.interp(grid, times, rows[:, column])
    return out


def stereo_rows(frames: int, step: int) -> np.ndarray:
    """The frames a stereo row stands at: 0, step, 2 step... and the last."""
    return np.asarray(sorted(set(range(0, frames, step)) | {frames - 1}), dtype=np.int64)


def channel_gains(balance_db: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """A partial's left and right amplitude from its RMS, by its balance (dB of right / left)."""
    r = 10.0 ** (np.asarray(balance_db) / 20.0)
    left = np.sqrt(2.0 / (1.0 + r * r))
    return left, left * r


def noise_gains(balance_db: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """A band's left and right PSD from its mean, by its balance (dB of right / left power)."""
    q = 10.0 ** (np.asarray(balance_db) / 10.0)
    left = 2.0 / (1.0 + q)
    return left, left * q


@dataclass
class Zone:
    root_note: int
    f0: float
    velocity: float
    frame_seconds: float
    harmonics: np.ndarray           # (frames, K) linear amplitude (RMS of the channels)
    noise_edges: np.ndarray         # (B + 1,) Hz
    noise: np.ndarray               # (frames, B) PSD (mean of the channels)
    pitch_cents: np.ndarray         # (frames,)
    inharmonicity: float = 0.0
    gain: float = 1.0
    attack: np.ndarray | None = None
    attack_rate: int = 48000
    attack_gain: float = 1.0
    loop: tuple[int, int] | None = None
    release: int | None = None
    # Stereo: balances per frame (dB, right / left), the right's phase per partial, the noise's
    # coherence per band. None: mono.
    balance: np.ndarray | None = None          # (frames, K) dB
    phase: np.ndarray | None = None            # (K,) radians
    noise_balance: np.ndarray | None = None    # (frames, B) dB
    noise_coherence: np.ndarray | None = None  # (B,)
    # The recording's first moments: (length, channels) at head_rate, the model coming in over
    # its last head_fade samples, its partials there at head_phase (sine, first channel).
    head: np.ndarray | None = None
    head_rate: int = 48000
    head_fade: int = 0
    head_phase: np.ndarray | None = None
    # Partials that are no harmonics: each one's frequency as a ratio to f0. None: harmonics.
    ratios: np.ndarray | None = None
    # Where its attack is (seconds from its start): zones played together meet there.
    onset: float = 0.0
    # A plucked string played instead of its partials (string_model.StringModel). None: partials.
    string: object | None = None

    @property
    def frames(self) -> int:
        return int(self.harmonics.shape[0])

    @property
    def stereo(self) -> bool:
        return self.balance is not None

    def to_json(self, compact: bool = True) -> dict:
        harmonics_db = np.where(self.harmonics > 0, 20.0 * np.log10(np.maximum(self.harmonics, 1e-12)), -200.0)
        noise_db = np.where(self.noise > 0, 10.0 * np.log10(np.maximum(self.noise, 1e-30)), -300.0)
        harmonics_db10 = np.clip(np.round(harmonics_db * 10), SILENT_DB10, 0x7FFF).astype(int)
        noise_db10 = np.clip(np.round(noise_db * 10), NOISE_FLOOR_DB10, 0x7FFF).astype(int)
        cents = np.asarray(self.pitch_cents, dtype=np.float64)
        times = None
        if compact and self.frames > COMPACT_FIRST_FRAMES * 2:
            forced = [self.loop[0], self.loop[1]] if self.loop else []
            if self.release is not None:
                forced.append(int(self.release))
            times = key_frames(self.frames, self.loop is not None, forced)
        if times is not None:
            harmonics_rows = np.clip(np.round(median_rows(harmonics_db10, times)), SILENT_DB10, 0x7FFF)
            noise_rows = np.clip(np.round(median_rows(noise_db10, times)), NOISE_FLOOR_DB10, 0x7FFF)
            cents_rows = median_rows(cents.reshape(-1, 1), times)[:, 0] if self.loop is None else cents[times]
            harmonics_value = encode_rows(harmonics_rows)
            noise_value = encode_rows(noise_rows)
        else:
            cents_rows = cents
            harmonics_value = harmonics_db10.tolist()
            noise_value = noise_db10.tolist()
        value = {
            "rootNote": int(self.root_note),
            "f0": round(float(self.f0), 4),
            "velocity": round(float(self.velocity), 4),
            "gain": round(float(self.gain), 6),
            "frameSeconds": float(self.frame_seconds),
            "frames": self.frames,
            "inharmonicity": float(self.inharmonicity),
            "pitchCents": [round(float(c), 2) for c in cents_rows],
            "harmonics": {
                "count": int(self.harmonics.shape[1]),
                "db10": harmonics_value,
            },
            "noise": {
                "edges": [round(float(e), 2) for e in self.noise_edges],
                "db10": noise_value,
            },
            "attack": None,
            "loop": {"start": int(self.loop[0]), "end": int(self.loop[1])} if self.loop else None,
            "release": int(self.release) if self.release is not None else None,
        }
        if times is not None:
            value["frameTimes"] = [int(t) for t in times]
        if self.ratios is not None:
            value["ratios"] = [round(float(r), 6) for r in self.ratios]
        if self.onset > 0:
            value["onset"] = round(float(self.onset), 4)
        if self.string is not None:
            value["string"] = self.string.to_json()
        if self.attack is not None and len(self.attack):
            value["attack"] = {
                "rate": int(self.attack_rate),
                "gain": round(float(self.attack_gain), 6),
                "float32": base64.b64encode(np.asarray(self.attack, dtype="<f4").tobytes()).decode("ascii"),
            }
        if self.stereo:
            rows = stereo_rows(self.frames, STEREO_STEP)
            balance_rows = _block_means(self.balance, rows)
            noise_rows_db = _block_means(self.noise_balance, rows)
            value["stereo"] = {
                "step": STEREO_STEP,
                "balance": encode_rows(np.clip(np.round(balance_rows * 10), -BALANCE_LIMIT_DB10, BALANCE_LIMIT_DB10)),
                "phase": [round(float(p), 3) for p in self.phase],
                "noiseBalance": encode_rows(np.clip(np.round(noise_rows_db * 10), -BALANCE_LIMIT_DB10,
                                                    BALANCE_LIMIT_DB10)),
                "noiseCoherence": [round(float(c), 3) for c in self.noise_coherence],
            }
        if self.head is not None and len(self.head):
            head = np.asarray(self.head, dtype=np.float64)
            if head.ndim == 1:
                head = head[:, None]
            peak = float(np.max(np.abs(head))) or 1.0
            scale = peak / 32767.0
            data = np.asarray(np.round(head / scale), dtype="<i2").tobytes()
            value["head"] = {
                "rate": int(self.head_rate),
                "channels": int(head.shape[1]),
                "length": int(head.shape[0]),
                "fade": int(self.head_fade),
                "gain": float(scale),
                "int16": base64.b64encode(data).decode("ascii"),
                "phase": [round(float(p), 4) for p in self.head_phase],
            }
            value["attack"] = None
        return value

    @staticmethod
    def from_json(value: dict) -> "Zone":
        frames = int(value["frames"])
        times = value.get("frameTimes")
        stored = len(times) if times else frames
        # A compact zone says how many partials it has (none, for a note
        # heard as noise); an older one has them as lists of rows.
        if "count" in value["harmonics"]:
            count = int(value["harmonics"]["count"])
        else:
            rows = value["harmonics"]["db10"]
            count = len(rows[0]) if stored and isinstance(rows, list) and rows else 0
        harmonics_db10 = decode_rows(value["harmonics"]["db10"], stored, count)
        bands = max(0, len(value["noise"]["edges"]) - 1)
        noise_db10 = decode_rows(value["noise"]["db10"], stored, bands)
        harmonics = np.where(harmonics_db10 <= SILENT_DB10, 0.0, 10.0 ** (harmonics_db10 / 200.0))
        noise = np.where(noise_db10 <= NOISE_FLOOR_DB10, 0.0, 10.0 ** (noise_db10 / 100.0))
        cents = np.asarray(value.get("pitchCents", [0.0] * stored), dtype=np.float64)
        if times:
            # A compact zone: the frames between its key frames, interpolated (linear values).
            keys = np.asarray(times, dtype=np.float64)
            harmonics = expand_rows(harmonics, keys, frames)
            noise = expand_rows(noise, keys, frames) if bands else np.zeros((frames, 0))
            cents = expand_rows(cents, keys, frames)
        attack = None
        attack_value = value.get("attack")
        if attack_value:
            attack = np.frombuffer(base64.b64decode(attack_value["float32"]), dtype="<f4").astype(np.float64)
        loop = value.get("loop")
        zone = Zone(
            root_note=int(value["rootNote"]), f0=float(value["f0"]), velocity=float(value.get("velocity", 0.8)),
            frame_seconds=float(value["frameSeconds"]), harmonics=harmonics,
            noise_edges=np.asarray(value["noise"]["edges"], dtype=np.float64), noise=noise,
            pitch_cents=cents,
            inharmonicity=float(value.get("inharmonicity", 0.0)), gain=float(value.get("gain", 1.0)),
            attack=attack, attack_rate=int(attack_value["rate"]) if attack_value else 48000,
            attack_gain=float(attack_value.get("gain", 1.0)) if attack_value else 1.0,
            loop=(int(loop["start"]), int(loop["end"])) if loop else None,
            release=value.get("release"))
        if value.get("ratios"):
            zone.ratios = np.asarray(value["ratios"], dtype=np.float64)
        zone.onset = float(value.get("onset", 0.0))
        if value.get("string"):
            from .string_model import StringModel  # noqa: PLC0415
            zone.string = StringModel.from_json(value["string"])
        stereo = value.get("stereo")
        if stereo:
            rows = stereo_rows(frames, int(stereo["step"]))
            zone.balance = expand_rows(decode_rows(stereo["balance"], len(rows), count) / 10.0,
                                       rows.astype(np.float64), frames)
            zone.phase = np.asarray(stereo["phase"], dtype=np.float64)
            zone.noise_balance = (expand_rows(decode_rows(stereo["noiseBalance"], len(rows), bands) / 10.0,
                                              rows.astype(np.float64), frames) if bands else np.zeros((frames, 0)))
            zone.noise_coherence = np.asarray(stereo["noiseCoherence"], dtype=np.float64)
        head = value.get("head")
        if head:
            channels = int(head["channels"])
            data = np.frombuffer(base64.b64decode(head["int16"]), dtype="<i2").astype(np.float64)
            zone.head = data.reshape(-1, channels) * float(head["gain"])
            zone.head_rate = int(head["rate"])
            zone.head_fade = int(head["fade"])
            zone.head_phase = np.asarray(head["phase"], dtype=np.float64)
        return zone


def _block_means(table: np.ndarray, rows: np.ndarray) -> np.ndarray:
    """Each stereo row: the mean of the frames around it (half a step either side)."""
    out = np.empty((len(rows), table.shape[1]))
    for index, frame in enumerate(rows):
        low = 0 if index == 0 else (rows[index - 1] + frame) // 2 + 1
        high = table.shape[0] - 1 if index == len(rows) - 1 else (frame + rows[index + 1]) // 2
        out[index] = np.mean(table[low:high + 1], axis=0)
    return out


@dataclass
class SoundModel:
    name: str
    zones: list[Zone]
    source: list[str] = field(default_factory=list)
    musical: dict = field(default_factory=dict)
    analysis: dict = field(default_factory=dict)

    def to_json(self, compact: bool = True) -> dict:
        return {
            "format": FORMAT,
            "version": VERSION,
            "name": self.name,
            "source": self.source,
            "zones": [zone.to_json(compact) for zone in self.zones],
            "musical": self.musical,
            "analysis": self.analysis,
        }

    def save(self, path: str | Path, compact: bool = True) -> None:
        Path(path).write_text(json.dumps(self.to_json(compact), separators=(",", ":")), encoding="utf-8")

    @staticmethod
    def load(path: str | Path) -> "SoundModel":
        value = json.loads(Path(path).read_text(encoding="utf-8"))
        if value.get("format") != FORMAT:
            raise ValueError(f"{path}: not a {FORMAT} file")
        if int(value.get("version", 0)) > VERSION:
            raise ValueError(f"{path}: version {value.get('version')} is newer than this companion")
        return SoundModel(name=value.get("name", ""), zones=[Zone.from_json(z) for z in value["zones"]],
                          source=list(value.get("source", [])), musical=dict(value.get("musical", {})),
                          analysis=dict(value.get("analysis", {})))
