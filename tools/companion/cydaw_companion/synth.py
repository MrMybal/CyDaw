"""The reference player of a sound model: what CyDAW's "sound-model" voice does.

The C++ voice (src/engine/SoundModel.*) follows this, step by step, so a
model can be checked here against its source and there in real time.

A note at `note` (MIDI, in tune: 69 = 440 Hz) and `velocity` (0..1):

* zones: the recorded pitches either side of the note, and at each the velocity layers either
  side of the velocity, weighted bilinearly (up to four zones; one where the note or the velocity
  is beyond the recorded ones);
* attacks: a zone's frames start (onset - onset_ref) / frameSeconds in (later), the reference
  being the zone whose head plays (else the heaviest): the zones' attacks meet;
* time: a frame position p advances speed / frameSeconds per second; while the
  key is down and the zone loops, p ping-pongs between loop.start and loop.end;
  at key up it jumps to `release` when the zone has one (its recorded release
  plays); without one, the voice fades out over 0.3 s (to -60 dB);
  `release_seconds` sets that fade, for both;
* head: when the heaviest zone has one, its recording plays from the note-on (resampled by the
  pitch ratio as attack_follows_pitch says), at the zones' summed level; the model is silent
  under it, its frames following the head's time, and comes in over the head's last samples
  (linearly, the head going out the same way), its partials set at the block where that begins
  to the phases the recording had there;
* harmonics: partial k at k f sqrt(1 + B' k^2) 2^(cents(p) * pitch_curve / 1200),
  f = 440 * 2^((note - 69) / 12), B' = B * inharmonicity; a zone with free ratios (a bar, a
  bell) has partial k at (k + (ratio_k - k) * inharmonicity) f instead (inharmonicity 0: back
  to harmonics), and when one zone of those played has them, every zone's multiplier of f
  (ratio or k sqrt(1 + B' k^2)) is blended by weight among the zones that have that partial;
  its amplitude is the
  zone's at p, read by harmonic number (formant 0) or at the same frequency in
  the recorded spectrum (formant 1), tilted by `brightness` dB per octave of k;
  phases start at 2 pi frac(k * 0.6180339887); partials above 0.45 fs fade out; in stereo each
  channel's share by the partial's balance, the right's phase moved by the zones' (circular)
  blend of their phase offsets;
* noise: white noise (one continuous stream, and a second for the right) shaped every hop by the
  zone's band PSD at p (sqrt-Hann windows, 50 % overlap, FFT of 2^round(log2(0.0213 fs))); in
  stereo each channel's PSD by the band's balance, the right c times the left's stream and
  sqrt(1 - c^2) times its own; band edges move with the pitch by `noise_follows_pitch` (0..1);
* attack (a zone without a head): its transient, played once from the start, resampled by the
  pitch ratio (attack_follows_pitch);
* string: when the heaviest zone has one (string_model.py), it plays instead of the partials, at
  the zones' summed level: tuned to the note (no pitch curve), its decay `speed` times faster,
  its stiffness scaled by `inharmonicity`, its excitation tilted as the partials would be; it
  starts where the partials take over from the head (the same block, the recording's phases
  there), from the note-on without a head; in stereo its two channels by the partials' balance
  at its start, weighted by their level;
* velocity: gain 10^(velocity_db (v - v_zone) / 20), brightness
  + velocity_brightness (v - v_zone) dB/oct.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .model import Zone, channel_gains, noise_gains
from .pitch import midi_to_hz

GOLDEN = 0.6180339887498949
BLOCK = 32


@dataclass
class PlayParameters:
    gain: float = 1.0
    harmonics_mix: float = 1.0
    noise_mix: float = 1.0
    attack_mix: float = 1.0
    brightness: float = 0.0           # dB per octave of harmonic number
    formant: float = 0.0              # 0 harmonics follow the note, 1 the spectrum stays in place
    inharmonicity: float = 1.0
    pitch_curve: float = 1.0
    speed: float = 1.0
    # Key up: None (automatic) plays the recorded release when the zone has
    # one, else fades over 0.3 s; a value fades over that many seconds too.
    release_seconds: float | None = None
    noise_follows_pitch: float = 0.0
    attack_follows_pitch: float = 1.0
    velocity_db: float = 24.0         # dB for the whole velocity range
    velocity_brightness: float = 3.0  # dB/oct for the whole velocity range
    max_harmonics: int = 96


def noise_fft_size(rate: float) -> int:
    return int(2 ** round(np.log2(0.0213 * rate)))


def band_gains(edges: np.ndarray, psd: np.ndarray, frequencies: np.ndarray) -> np.ndarray:
    """The PSD read at each bin: constant inside a band, zero outside them."""
    index = np.searchsorted(edges, frequencies, side="right") - 1
    inside = (index >= 0) & (index < len(psd))
    values = np.zeros_like(frequencies)
    values[inside] = psd[index[inside]]
    return values


def interpolate_frames(curves: np.ndarray, positions: np.ndarray) -> np.ndarray:
    """Rows of `curves` (frames, ...) read at fractional positions."""
    lower = np.clip(np.floor(positions).astype(int), 0, len(curves) - 1)
    upper = np.minimum(lower + 1, len(curves) - 1)
    weight = (positions - lower).reshape((-1,) + (1,) * (curves.ndim - 1))
    return curves[lower] * (1.0 - weight) + curves[upper] * weight


def choose_zones(model, note: float, velocity: float) -> list[tuple[int, float]]:
    """The zones a note plays and their weights: the recorded pitches either side of the note,
    and at each the velocity layers either side of the velocity, weighted bilinearly."""
    zones = [(index, zone) for index, zone in enumerate(model.zones) if zone.frames > 0]
    if not zones:
        return []
    roots = sorted({zone.root_note for _, zone in zones})
    below = [root for root in roots if root <= note]
    above = [root for root in roots if root >= note]
    if below and above:
        low, high = below[-1], above[0]
        pitches = [(low, 1.0)] if low == high else [(low, (high - note) / (high - low)),
                                                    (high, (note - low) / (high - low))]
    else:
        pitches = [(below[-1] if below else above[0], 1.0)]
    chosen = []
    for root, pitch_weight in pitches:
        layers = sorted((zone.velocity, index) for index, zone in zones if zone.root_note == root)
        under = [layer for layer in layers if layer[0] <= velocity]
        over = [layer for layer in layers if layer[0] >= velocity]
        if under and over and under[-1][1] != over[0][1] and over[0][0] > under[-1][0]:
            (v1, i1), (v2, i2) = under[-1], over[0]
            share = (velocity - v1) / (v2 - v1)
            chosen += [(i1, pitch_weight * (1.0 - share)), (i2, pitch_weight * share)]
        else:
            chosen.append(((under[-1] if under else over[0])[1], pitch_weight))
    return [(index, weight) for index, weight in chosen if weight > 1e-6]


@dataclass
class _Lane:
    zone: Zone
    weight: float
    positions: np.ndarray
    fade: np.ndarray
    ratio: float
    level: float
    tilt: float


def _lane_timeline(zone: Zone, rate: int, samples: int, held: int, params: PlayParameters,
                   head_time: np.ndarray | None, offset: float = 0.0) -> tuple[np.ndarray, np.ndarray]:
    """The frame position for each sample, and the release fade multiplier. While a head plays
    (and the key is down), the position is the head's time; after, it steps on from there."""
    step = params.speed / (zone.frame_seconds * rate)
    last = zone.frames - 1
    positions = np.empty(samples)
    fade = np.ones(samples)
    p = 0.0
    direction = 1.0
    released = False
    natural = zone.release is not None and zone.loop is not None
    fade_seconds = params.release_seconds if params.release_seconds is not None else (None if natural else 0.3)
    release_rate = np.exp(np.log(1e-3) / max(1e-3, fade_seconds * rate)) if fade_seconds else 1.0
    level = 1.0
    p = offset
    for index in range(samples):
        if index == held and not released:
            released = True
            if natural:
                p = float(zone.release)
                direction = 1.0
        if released:
            level *= release_rate
        if head_time is not None and not released and index < len(head_time):
            p = head_time[index] / zone.frame_seconds + offset
        positions[index] = min(p, last)
        fade[index] = level
        p += step * direction
        if not released and zone.loop is not None:
            start, end = zone.loop
            if direction > 0 and p >= end:
                p = end - (p - end)
                direction = -1.0
            elif direction < 0 and p <= start:
                p = start + (start - p)
                direction = 1.0
    return positions, fade


def _render_string(zone: Zone, lanes: list, heaviest: Zone, frequency: float, velocity: float, rate: int, samples: int,
                   params: PlayParameters, head_zone: Zone | None, snap, model_in: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    from .string_model import PluckedString, excitation, tune  # noqa: PLC0415
    string = zone.string
    lane = max(lanes, key=lambda item: item.weight)
    count = len(string.excitation_db)
    k = np.arange(1, count + 1, dtype=np.float64)
    amplitudes = excitation(string, frequency, rate, lane.tilt, count)
    tuning = tune(frequency, rate, string.gain, string.pole, string.dispersion, string.stages, params.speed, params.inharmonicity,
                  string.order, string.zero, string.dips)
    start = 0
    phases = 2.0 * np.pi * ((k * GOLDEN) % 1.0)
    if head_zone is not None:
        if snap is None:
            return np.zeros(samples), np.zeros(samples)
        start, since = snap
        if head_zone.head_phase is not None and len(head_zone.head_phase):
            own = min(count, len(head_zone.head_phase))
            position = lane.positions[start]
            recorded_cents = float(interpolate_frames(head_zone.pitch_cents, np.array([position]))[0])
            recorded = (k[:own] * np.sqrt(1.0 + head_zone.inharmonicity * k[:own] ** 2) * head_zone.f0
                        * 2.0 ** (recorded_cents / 1200.0))
            phases[:own] = head_zone.head_phase[:own] + 2.0 * np.pi * recorded * since / head_zone.head_rate
    voice = PluckedString(tuning, frequency, rate, amplitudes, phases)
    out = np.zeros(samples)
    out[start:] = voice.render(samples - start)
    total = sum(item.level for item in lanes)
    out *= params.harmonics_mix * total * lane.fade * model_in
    gain_left, gain_right = 1.0, 1.0
    if zone.stereo:
        frame = int(min(zone.frames - 1, max(0, string.start)))
        left, right = channel_gains(zone.balance[frame, :count])
        power = 10.0 ** (np.minimum(string.excitation_db[:len(left)], 0.0) / 10.0)
        gain_left = float(np.sum(left * power) / max(np.sum(power), 1e-30))
        gain_right = float(np.sum(right * power) / max(np.sum(power), 1e-30))
    return out * gain_left, out * gain_right


def partial_multipliers(zones: list[tuple[Zone, float]], count: int, inharmonicity: float) -> np.ndarray:
    """Each partial's frequency as a multiple of the note's: per zone its free ratio (moved
    towards k by `inharmonicity` 0..1, past it beyond) or k sqrt(1 + B' k^2), blended by weight
    among the zones that have that partial."""
    k = np.arange(1, count + 1, dtype=np.float64)
    total = np.zeros(count)
    weights = np.zeros(count)
    for zone, weight in zones:
        if zone.ratios is not None:
            own = min(count, len(zone.ratios))
            values = np.zeros(count)
            values[:own] = k[:own] + (zone.ratios[:own] - k[:own]) * inharmonicity
            has = np.arange(count) < own
        else:
            values = k * np.sqrt(1.0 + zone.inharmonicity * inharmonicity * k * k)
            has = np.ones(count, dtype=bool)
        total += np.where(has, weight * values, 0.0)
        weights += np.where(has, weight, 0.0)
    return np.where(weights > 0, total / np.maximum(weights, 1e-12), k)


def render_model(model, note: float, velocity: float, rate: int, seconds: float, held_seconds: float | None = None,
                 params: PlayParameters | None = None, seed: int = 1, channels: int = 1) -> np.ndarray:
    """A note of a whole model: its zones chosen and blended."""
    return render_zones([(model.zones[index], weight) for index, weight in choose_zones(model, note, velocity)],
                        note, velocity, rate, seconds, held_seconds, params, seed, channels)


def render(zone: Zone, note: float, velocity: float, rate: int, seconds: float, held_seconds: float | None = None,
           params: PlayParameters | None = None, seed: int = 1, channels: int = 1) -> np.ndarray:
    return render_zones([(zone, 1.0)], note, velocity, rate, seconds, held_seconds, params, seed, channels)


def render_zones(zones: list[tuple[Zone, float]], note: float, velocity: float, rate: int, seconds: float,
                 held_seconds: float | None = None, params: PlayParameters | None = None, seed: int = 1,
                 channels: int = 1) -> np.ndarray:
    """The zones played together as one voice: one bank of partials whose
    amplitudes, pitch curve and inharmonicity are the zones' weighted blend
    (each zone read at its own frame, with its own loop and release), their
    noise PSDs and their transients blended the same way. `channels` 1: the
    two channels' mean, 2: (samples, 2)."""
    params = params or PlayParameters()
    samples = int(round(seconds * rate))
    left = np.zeros(samples)
    right = np.zeros(samples)
    if not zones or samples == 0:
        return np.zeros((samples, 2)) if channels == 2 else left
    held = samples if held_seconds is None else int(round(held_seconds * rate))
    frequency = midi_to_hz(note)
    controls = np.arange(0, samples + BLOCK, BLOCK)
    control_index = np.minimum(controls, samples - 1)
    sample_index = np.arange(samples)
    count = min(params.max_harmonics, max(zone.harmonics.shape[1] for zone, _ in zones))
    k = np.arange(1, count + 1, dtype=np.float64)

    # The head: the heaviest zone's, at the zones' summed level.
    heaviest = max(zones, key=lambda item: item[1])[0]
    head_zone = heaviest if heaviest.head is not None and len(heaviest.head) > 2 * max(1, heaviest.head_fade) else None
    head_time = None
    model_in = np.ones(samples)
    snap = None
    if head_zone is not None:
        ratio = frequency / head_zone.f0 if head_zone.f0 > 0 else 1.0
        head_step = head_zone.head_rate / rate * ratio ** params.attack_follows_pitch
        head_position = sample_index * head_step
        length = len(head_zone.head)
        fade_start = length - head_zone.head_fade
        head_time = head_position[head_position < length] / head_zone.head_rate
        model_in = np.clip((head_position - fade_start) / max(1, head_zone.head_fade), 0.0, 1.0)
        # The block whose end reaches the fade's start: there the partials take the recording's phases.
        ends = (controls[:-1] + BLOCK) * head_step
        reaching = np.nonzero(ends >= fade_start)[0]
        if len(reaching) and controls[reaching[0]] < samples:
            snap = (int(controls[reaching[0]]), float(controls[reaching[0]] * head_step - fade_start))

    # Each zone on its own timeline (its attack where the reference's is): positions, fades, level.
    reference = head_zone if head_zone is not None else heaviest
    lanes = []
    for zone, weight in zones:
        offset = (zone.onset - reference.onset) / zone.frame_seconds
        positions, fade = _lane_timeline(zone, rate, samples, held, params,
                                         head_time if head_zone is not None else None, offset)
        ratio = frequency / zone.f0 if zone.f0 > 0 else 1.0
        dv = velocity - zone.velocity
        level = weight * params.gain * zone.gain * 10.0 ** (params.velocity_db * dv / 20.0)
        tilt = params.brightness + params.velocity_brightness * dv
        lanes.append(_Lane(zone, weight, positions, fade, ratio, level, tilt))

    # A plucked string in place of the partials.
    string_zone = heaviest if getattr(heaviest, "string", None) is not None else None
    if string_zone is not None and params.harmonics_mix > 0:
        out_left, out_right = _render_string(string_zone, lanes, heaviest, frequency, velocity, rate, samples, params,
                                             head_zone, snap, model_in)
        left += out_left
        right += out_right
    # Harmonics, at a control rate of 32 samples, amplitudes ramped per sample.
    elif params.harmonics_mix > 0 and count > 0:
        amplitudes_left = np.zeros((len(controls), count))
        amplitudes_right = np.zeros((len(controls), count))
        rotation = np.zeros(count, dtype=np.complex128)
        cents = np.zeros(len(controls))
        stretch_b = 0.0
        for lane in lanes:
            zone = lane.zone
            control_positions = np.interp(controls, sample_index, lane.positions)
            cents += lane.weight * interpolate_frames(zone.pitch_cents, control_positions) * params.pitch_curve
            stretch_b += lane.weight * zone.inharmonicity * params.inharmonicity
            own = zone.harmonics.shape[1]
            rows = interpolate_frames(zone.harmonics, control_positions)
            by_number = np.zeros((len(controls), count))
            by_number[:, :min(own, count)] = rows[:, :count]
            if params.formant > 0 and zone.ratios is None:
                # The same frequency in the recorded spectrum: harmonic k * ratio.
                source = k * lane.ratio
                lower = np.floor(source).astype(int)
                fraction = source - lower
                padded = np.concatenate([np.zeros((len(rows), 1)), rows, np.zeros((len(rows), 2))], axis=1)
                lower = np.clip(lower, 0, own + 1)
                in_place = padded[:, lower] * (1.0 - fraction) + padded[:, np.minimum(lower + 1, own + 2)] * fraction
                zone_amplitudes = by_number * (1.0 - params.formant) + in_place * params.formant
            else:
                zone_amplitudes = by_number
            zone_amplitudes = zone_amplitudes * (10.0 ** (lane.tilt * np.log2(k) / 20.0))[None, :]
            scaled = zone_amplitudes * (lane.level * lane.fade[control_index] * model_in[control_index])[:, None]
            if zone.stereo:
                balance = np.zeros((len(controls), count))
                balance[:, :min(own, count)] = interpolate_frames(zone.balance, control_positions)[:, :count]
                gain_left, gain_right = channel_gains(balance)
                amplitudes_left += scaled * gain_left
                amplitudes_right += scaled * gain_right
                offsets = np.zeros(count)
                offsets[:min(own, count)] = zone.phase[:count]
                rotation += lane.weight * np.exp(1j * offsets)
            else:
                amplitudes_left += scaled
                amplitudes_right += scaled
                rotation += lane.weight
        stretch = np.sqrt(1.0 + stretch_b * k * k)
        multiplier = k * stretch
        if any(lane.zone.ratios is not None for lane in lanes):
            multiplier = partial_multipliers([(lane.zone, lane.weight) for lane in lanes], count, params.inharmonicity)
        partials = frequency * 2.0 ** (cents / 1200.0)[:, None] * multiplier[None, :]
        audible = np.clip((0.45 * rate - partials) / (0.05 * rate), 0.0, 1.0)
        amplitudes_left *= audible
        amplitudes_right *= audible
        offset = np.angle(rotation)
        increment = np.empty((samples, count))
        for column in range(count):
            increment[:, column] = np.interp(sample_index, controls, partials[:, column])
        phases = 2.0 * np.pi * ((k * GOLDEN) % 1.0)[None, :] + 2.0 * np.pi * np.cumsum(increment, axis=0) / rate
        # The first sample starts at the initial phase.
        phases -= 2.0 * np.pi * increment[0:1, :] / rate
        if snap is not None and head_zone.head_phase is not None and len(head_zone.head_phase):
            # Where the head gives way: the recording's phases at the block's first sample (its
            # partials at the recorded frequencies over the head's time since the fade's start).
            at, since = snap
            own = min(count, len(head_zone.head_phase))
            position = max(lanes, key=lambda lane: lane.weight).positions[at]
            recorded_cents = float(interpolate_frames(head_zone.pitch_cents, np.array([position]))[0])
            recorded = ((head_zone.ratios[:own] if head_zone.ratios is not None
                         else k[:own] * np.sqrt(1.0 + head_zone.inharmonicity * k[:own] ** 2))
                        * head_zone.f0 * 2.0 ** (recorded_cents / 1200.0))
            target = head_zone.head_phase[:own] + 2.0 * np.pi * recorded * since / head_zone.head_rate
            later = np.cumsum(increment[at:, :own], axis=0) - increment[at:at + 1, :own]
            phases[at:, :own] = target[None, :] + 2.0 * np.pi * later / rate
        attack_in = np.clip(sample_index / (0.002 * rate), 0.0, 1.0) if head_zone is None else np.ones(samples)
        amplitude_left = np.empty((samples, count))
        amplitude_right = np.empty((samples, count))
        for column in range(count):
            amplitude_left[:, column] = np.interp(sample_index, controls, amplitudes_left[:, column])
            amplitude_right[:, column] = np.interp(sample_index, controls, amplitudes_right[:, column])
        left += params.harmonics_mix * attack_in * np.sum(amplitude_left * np.sin(phases), axis=1)
        right += params.harmonics_mix * attack_in * np.sum(amplitude_right * np.sin(phases + offset[None, :]), axis=1)

    # Noise: two white streams, shaped every hop by the zones' PSDs.
    if params.noise_mix > 0 and any(zone.noise.shape[1] > 0 for zone, _ in zones):
        size = noise_fft_size(rate)
        hop = size // 2
        window = np.sqrt(np.hanning(size + 1)[:-1])
        rng = np.random.default_rng(seed)
        stream = rng.uniform(-np.sqrt(3.0), np.sqrt(3.0), samples + size)
        other = np.random.default_rng(seed + 7919).uniform(-np.sqrt(3.0), np.sqrt(3.0), samples + size)
        shaped_left = np.zeros(samples + size)
        shaped_right = np.zeros(samples + size)
        bins = np.fft.rfftfreq(size, 1.0 / rate)
        for start in range(0, samples, hop):
            # This frame covers output samples start - hop .. start + hop.
            centre = min(samples - 1, start)
            power_left = np.zeros_like(bins)
            power_right = np.zeros_like(bins)
            coherence = np.zeros_like(bins)
            weights = np.zeros_like(bins)
            for lane in lanes:
                zone = lane.zone
                if zone.noise.shape[1] == 0:
                    continue
                edges = zone.noise_edges * (lane.ratio ** params.noise_follows_pitch)
                psd = interpolate_frames(zone.noise, np.array([lane.positions[centre]]))[0]
                gain = (lane.level * lane.fade[centre] * model_in[centre]) ** 2
                if zone.stereo:
                    balance = interpolate_frames(zone.noise_balance, np.array([lane.positions[centre]]))[0]
                    gain_left, gain_right = noise_gains(balance)
                    power_left += gain * band_gains(edges, psd * gain_left, bins)
                    power_right += gain * band_gains(edges, psd * gain_right, bins)
                    coherence += lane.weight * band_gains(edges, zone.noise_coherence, bins)
                else:
                    power_left += gain * band_gains(edges, psd, bins)
                    power_right += gain * band_gains(edges, psd, bins)
                    coherence += lane.weight * band_gains(edges, np.ones(len(psd)), bins)
                weights += lane.weight * band_gains(edges, np.ones(len(psd)), bins)
            coherence = np.where(weights > 0, coherence / np.maximum(weights, 1e-12), 1.0)
            base = np.fft.rfft(stream[start:start + size] * window)
            own = np.fft.rfft(other[start:start + size] * window)
            spectrum_left = base * np.sqrt(power_left * rate / 2.0)
            spectrum_right = (coherence * base + np.sqrt(np.maximum(0.0, 1.0 - coherence ** 2)) * own) \
                * np.sqrt(power_right * rate / 2.0)
            shaped_left[start:start + size] += np.fft.irfft(spectrum_left, size) * window
            shaped_right[start:start + size] += np.fft.irfft(spectrum_right, size) * window
        left += params.noise_mix * shaped_left[hop:hop + samples]
        right += params.noise_mix * shaped_right[hop:hop + samples]

    # The head, at the zones' summed level, going out as the model comes in.
    if head_zone is not None and params.attack_mix > 0:
        head = np.asarray(head_zone.head, dtype=np.float64)
        if head.ndim == 1:
            head = head[:, None]
        ratio = frequency / head_zone.f0 if head_zone.f0 > 0 else 1.0
        head_step = head_zone.head_rate / rate * ratio ** params.attack_follows_pitch
        head_position = sample_index * head_step
        inside = head_position < len(head) - 1
        total = sum(lane.level for lane in lanes)
        heaviest_lane = max(lanes, key=lambda lane: lane.weight)
        scale = params.attack_mix * total * heaviest_lane.fade * (1.0 - model_in)
        grid = np.arange(len(head))
        head_left = np.zeros(samples)
        head_right = np.zeros(samples)
        head_left[inside] = np.interp(head_position[inside], grid, head[:, 0])
        head_right[inside] = np.interp(head_position[inside], grid, head[:, -1])
        left += scale * head_left
        right += scale * head_right

    # The transients of the zones without a head, once from the start.
    if params.attack_mix > 0:
        for lane in lanes:
            zone = lane.zone
            if zone.attack is None or not len(zone.attack) or head_zone is not None:
                continue
            step = (zone.attack_rate / rate) * (lane.ratio ** params.attack_follows_pitch)
            positions_attack = sample_index * step
            inside = positions_attack < len(zone.attack) - 1
            attack = np.zeros(samples)
            attack[inside] = np.interp(positions_attack[inside], np.arange(len(zone.attack)), zone.attack)
            value = params.attack_mix * zone.attack_gain * lane.level * lane.fade * attack
            left += value
            right += value

    if channels == 2:
        return np.stack([left, right], axis=1)
    return 0.5 * (left + right)
