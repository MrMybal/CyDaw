"""From a recorded note to a sound model zone: harmonics + noise (+ head), in stereo.

Classic analysis, no training:

1. trim the silence, find the pitch over time (YIN; with the note known, up to its own
   frequency, and a clear peak there is enough when YIN loses a very high note) and the note;
2. short-time spectra sized for the pitch, of each channel; the inharmonicity B from the peaks
   of the steady part (f_k^2 / k^2 = f0^2 + f0^2 B k^2, a weighted line fit);
3. every partial's amplitude per frame: the spectral peak near k f(t) sqrt(1 + B k^2) of the
   channels' power together, refined by parabolic interpolation; in stereo, each channel's share
   of it (the balance) and the right's phase against the left over the steady part;
4. the residual: the spectrum without the partials' main lobes, as a noise PSD in log-spaced
   bands, per channel (their mean, their balance, and how alike they are: the coherence);
5. the head: the recording's first moments (until its transient has passed, three periods at
   least), kept as it is, with the partials' phases where the model takes over from it; without
   it, the residual's waveform at the start is kept as the attack and the band noise fades in as
   it ends;
6. sustain loop and release from the harmonic energy curve; readable parameters (ADSR,
   brightness, breath, slope) for the musical patch.

Amplitudes and PSDs follow the conventions of `synth.py`: a partial's value is its sine peak
amplitude (in stereo, the RMS of the two channels'); a band's value is the one-sided PSD
(amplitude^2/Hz; in stereo, the mean of the two channels').
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .audio_io import resample
from .model import Zone
from .pitch import hz_to_midi, midi_to_hz, yin

RATE = 48000
FRAME_SECONDS = 0.01
HEAD_FADE_SECONDS = 0.02


@dataclass
class AnalysisOptions:
    max_harmonics: int = 96
    noise_bands: int = 24
    keep_attack: bool = True          # the head (or, without it, the attack residual)
    head: bool = True                 # keep the recording's first moments as they are
    stereo: bool = True               # keep the two channels' image (a stereo recording)
    note_hint: float | None = None    # MIDI: picks the octave when the pitch is ambiguous
    velocity: float | None = None     # 0..1; by default from the level
    loop: bool | None = None          # force a sustain loop on (True) or off (False)


def _bounds(signal: np.ndarray, rate: int) -> tuple[int, int]:
    """Where the note is: from just before its first sound to just after it has died away."""
    peak = np.max(np.abs(signal)) if len(signal) else 0.0
    if peak <= 0:
        raise ValueError("the file is silent")
    loud = np.nonzero(np.abs(signal) > peak * 10 ** (-50 / 20))[0]
    start = max(0, loud[0] - int(0.002 * rate))
    quiet_end = np.nonzero(np.abs(signal) > peak * 10 ** (-70 / 20))[0][-1]
    end = min(len(signal), quiet_end + int(0.02 * rate))
    return start, end


def _trim(signal: np.ndarray, rate: int) -> np.ndarray:
    start, end = _bounds(signal, rate)
    return signal[start:end]


def _stft(signal: np.ndarray, size: int, hop: int, fft_size: int) -> tuple[np.ndarray, np.ndarray]:
    window = np.hanning(size + 1)[:-1]
    padded = np.concatenate([np.zeros(size // 2), signal, np.zeros(size)])
    count = max(1, int(np.ceil(len(signal) / hop)))
    frames = np.lib.stride_tricks.sliding_window_view(padded, size)[np.arange(count) * hop]
    return np.fft.rfft(frames * window, fft_size), window


def _istft(spectra: np.ndarray, size: int, hop: int, fft_size: int, length: int) -> np.ndarray:
    window = np.hanning(size + 1)[:-1]
    frames = np.fft.irfft(spectra, fft_size)[:, :size] * window
    output = np.zeros(len(frames) * hop + size)
    norm = np.zeros_like(output)
    for index, frame in enumerate(frames):
        output[index * hop:index * hop + size] += frame
        norm[index * hop:index * hop + size] += window * window
    output = output / np.maximum(norm, 1e-8)
    return output[size // 2:size // 2 + length]


def _blackman_harris(size: int) -> np.ndarray:
    n = np.arange(size)
    a = (0.35875, 0.48829, 0.14128, 0.01168)
    return (a[0] - a[1] * np.cos(2 * np.pi * n / size) + a[2] * np.cos(4 * np.pi * n / size)
            - a[3] * np.cos(6 * np.pi * n / size))


def _peaks(magnitude: np.ndarray, centres: np.ndarray, search: int) -> tuple[np.ndarray, np.ndarray]:
    """For each (frame, partial) expected bin: the interpolated peak (bin, log magnitude)."""
    frames, bins = magnitude.shape
    offsets = np.arange(-search, search + 1)
    columns = np.clip(np.round(centres).astype(int)[..., None] + offsets, 1, bins - 2)
    rows = np.arange(frames)[:, None, None]
    window = magnitude[rows, columns]
    best = np.argmax(window, axis=-1)
    peak = np.take_along_axis(columns, best[..., None], axis=-1)[..., 0]
    log = np.log(np.maximum(magnitude, 1e-20))
    a = log[rows[..., 0], peak - 1]
    b = log[rows[..., 0], peak]
    c = log[rows[..., 0], peak + 1]
    denominator = a - 2.0 * b + c
    shift = np.where(np.abs(denominator) > 1e-12, 0.5 * (a - c) / np.where(denominator == 0, 1, denominator), 0.0)
    shift = np.clip(shift, -0.5, 0.5)
    return peak + shift, b - 0.25 * (a - c) * shift


def _phases_at(signal: np.ndarray, rate: int, at: int, frequencies: np.ndarray, period: float) -> np.ndarray:
    """Each frequency's phase (sine convention) in `signal` at sample `at`: the frame around it
    (three periods, 1024 samples at least, within the signal) demodulated at that frequency."""
    half = int(min(at, len(signal) - at - 1, max(512, 1.5 * period * rate)))
    if half < 16 or not len(frequencies):
        return np.zeros(len(frequencies))
    segment = signal[at - half:at + half]
    window = np.hanning(len(segment))
    times = (np.arange(len(segment)) - half) / rate
    weighted = segment * window
    # One frequency at a time: a few dozen dot products of a few thousand samples.
    values = np.array([np.dot(weighted, np.exp(-2j * np.pi * f * times)) for f in frequencies])
    return np.angle(values) + 0.5 * np.pi


def _noise_bands(spectra_power: np.ndarray, free: np.ndarray, frequency: np.ndarray, edges: np.ndarray,
                 scale: float) -> np.ndarray:
    """One channel's noise PSD per frame and band: the median of its free bins (a noise bin's
    power is exponential: mean = median / ln 2)."""
    frames = spectra_power.shape[0]
    bands = len(edges) - 1
    noise = np.zeros((frames, bands))
    for band in range(bands):
        inside = (frequency >= edges[band]) & (frequency < edges[band + 1])
        if not np.any(inside):
            continue
        band_power = spectra_power[:, inside]
        band_free = free[:, inside]
        estimate = np.full(frames, np.nan)
        for frame in range(frames):
            values = band_power[frame][band_free[frame]]
            if len(values) >= 3:
                estimate[frame] = np.median(values) / np.log(2.0)
            elif len(values):
                estimate[frame] = np.mean(values)
        missing = np.isnan(estimate)
        if np.all(missing):
            continue
        if np.any(missing):
            estimate[missing] = np.interp(np.nonzero(missing)[0], np.nonzero(~missing)[0], estimate[~missing])
        noise[:, band] = estimate * scale
    return noise


def analyse(audio: np.ndarray, rate: int, options: AnalysisOptions | None = None) -> tuple[Zone, dict]:
    options = options or AnalysisOptions()
    audio = np.asarray(audio, dtype=np.float64)
    if audio.ndim == 1:
        audio = audio[:, None]
    # One or two channels (a third and more are left out).
    channels = [audio[:, index] for index in range(min(2, audio.shape[1]))]
    stereo = options.stereo and len(channels) == 2 and not np.allclose(channels[0], channels[1])
    if not stereo:
        channels = [np.mean(audio, axis=1)]
    if rate != RATE:
        channels = [resample(channel, rate, RATE) for channel in channels]
    rate = RATE
    mixed = np.mean(channels, axis=0)
    start, end = _bounds(mixed, rate)
    channels = [channel[start:end] for channel in channels]
    signal = np.mean(channels, axis=0)
    hop = int(round(FRAME_SECONDS * rate))

    # 1. Pitch: YIN up to the note asked for when it is very high (a piano's top octave).
    maximum_hz = 2500.0
    if options.note_hint is not None:
        maximum_hz = max(maximum_hz, 1.3 * midi_to_hz(options.note_hint))
    track = yin(signal, rate, hop, maximum_hz=min(maximum_hz, 0.4 * rate))
    f0 = track.median_f0()
    pitched = f0 > 0 and np.mean(track.f0 > 0) > 0.2
    if options.note_hint is not None and pitched:
        hint = midi_to_hz(options.note_hint)
        f0 *= 2.0 ** np.round(np.log2(hint / f0))
    frames_count = max(1, int(np.ceil(len(signal) / hop)))
    if not pitched and options.note_hint is not None:
        # YIN lost it: a clear peak within half a semitone of the note asked for is the note.
        hint = midi_to_hz(options.note_hint)
        size = int(np.clip(2 ** np.ceil(np.log2(8.0 * rate / hint)), 2048, 16384))
        part = signal[:min(len(signal), int(0.5 * rate))]
        if len(part) >= size // 2:
            power = np.abs(np.fft.rfft(part * np.hanning(len(part)), 4 * size)) ** 2
            bins = np.fft.rfftfreq(4 * size, 1.0 / rate)
            near = (bins > hint * 2 ** (-0.5 / 12)) & (bins < hint * 2 ** (0.5 / 12))
            around = (bins > hint * 0.7) & (bins < hint * 1.4) & ~near
            if np.any(near) and np.any(around) and power[near].max() > 30.0 * np.median(power[around]):
                pitched = True
                f0 = float(bins[near][np.argmax(power[near])])
                track.f0[:] = 0.0
    if not pitched:
        f0 = midi_to_hz(options.note_hint if options.note_hint is not None else 60)
    curve = np.where(track.f0[:frames_count] > 0, track.f0[:frames_count], f0)
    curve = np.where(np.abs(1200 * np.log2(curve / f0)) > 600, f0, curve)
    padded = np.pad(curve, 2, mode="edge")
    curve = np.median(np.lib.stride_tricks.sliding_window_view(padded, 5), axis=1)
    cents = np.clip(1200.0 * np.log2(curve / f0), -200.0, 200.0) if pitched else np.zeros(frames_count)

    # 2. Spectra sized for the pitch: four periods in the window at least. The channels' power
    #    together finds the partials (a partial in opposite phase in the two is still found).
    size = int(np.clip(2 ** np.ceil(np.log2(4.0 * rate / f0)), 1024, 8192))
    fft_size = size * 2
    channel_spectra = []
    for channel in channels:
        spectra, window = _stft(channel, size, hop, fft_size)
        channel_spectra.append(spectra[:frames_count])
    magnitude = np.sqrt(np.mean([np.abs(spectra) ** 2 for spectra in channel_spectra], axis=0))
    bin_hz = rate / fft_size
    window_sum = window.sum()
    window_power = np.sum(window * window)
    energy = np.sum(magnitude ** 2, axis=1)

    inharmonicity = 0.0
    if pitched:
        steady = energy >= np.quantile(energy, 0.7)
        average = np.sqrt(np.mean(magnitude[steady] ** 2, axis=0))[None, :]
        highest = min(30, int(0.45 * rate / f0))
        # A few partials first, then more as B tells where the upper ones sit.
        for reach in (6, 10, 16, 24, 30, 30):
            partials = np.arange(1, min(reach, highest) + 1, dtype=np.float64)
            expected = partials * f0 * np.sqrt(1.0 + inharmonicity * partials ** 2)
            keep = expected < 0.45 * rate
            found_bin, found_log = _peaks(average, (expected[keep] / bin_hz)[None, :], max(2, int(0.15 * f0 / bin_hz)))
            k = partials[keep]
            frequency = found_bin[0] * bin_hz
            weight = np.exp(found_log[0])
            weight = weight / weight.max()
            # Clear partials only (within 40 dB of the strongest), five at
            # least: a breathy note's noise peaks would bend the line.
            useful = weight > 1e-2
            if useful.sum() < 5:
                inharmonicity = 0.0
                break
            x = k[useful] ** 2
            y = (frequency[useful] / k[useful]) ** 2
            w = weight[useful]
            slope, intercept = np.polyfit(x, y, 1, w=w)
            if intercept <= 0:
                break
            inharmonicity = float(np.clip(slope / intercept, 0.0, 0.005))
            # The fundamental the partials agree on: an inharmonic string
            # pulls YIN's period towards its stretched upper partials.
            fitted = float(np.sqrt(intercept))
            if abs(1200.0 * np.log2(fitted / f0)) < 60.0:
                curve = curve * (fitted / f0)
                f0 = fitted

    # 3. Partials per frame; in stereo each channel's share and the right's phase.
    count = 0
    harmonics = np.zeros((frames_count, 0))
    peak_bins = np.zeros((frames_count, 0))
    balance = None
    phase = None
    if pitched:
        k = np.arange(1, options.max_harmonics + 1, dtype=np.float64)
        top = (k * f0 * np.sqrt(1.0 + inharmonicity * k * k)) < 0.45 * rate
        count = int(top.sum())
        k = k[:count]
        expected = (curve[:, None] * k[None, :] * np.sqrt(1.0 + inharmonicity * k * k)[None, :]) / bin_hz
        search = max(1, int(0.12 * f0 / bin_hz))
        peak_bins, peak_log = _peaks(magnitude, expected, search)
        harmonics = 2.0 * np.exp(peak_log) / window_sum
        if stereo and count:
            columns = np.clip(np.round(peak_bins).astype(int), 0, magnitude.shape[1] - 1)
            rows = np.arange(frames_count)[:, None]
            left = channel_spectra[0][rows, columns]
            right = channel_spectra[1][rows, columns]
            floor = 1e-9 * max(float(np.max(magnitude)), 1e-12)
            balance = 20.0 * np.log10((np.abs(right) + floor) / (np.abs(left) + floor))
            balance = np.clip(balance, -40.0, 40.0)
            # The right's phase against the left: their cross-spectrum over the loud frames.
            loud = energy >= np.quantile(energy, 0.5)
            cross = np.sum((right * np.conj(left))[loud], axis=0)
            phase = np.angle(cross)

    # 4. The residual. For the attack's waveform: the Hann spectra without the
    #    partials' main lobes (+-2 bins of the window, +-4 with the padding, and
    #    one more). For the band noise: spectra of its own with a
    #    Blackman-Harris window (sidelobes under -92 dB, main lobe +-4 bins)
    #    over eight periods, read only between the partials, by the median, per
    #    channel, so neither leakage nor vibrato pass for noise.
    lobe = 5
    mask = np.ones_like(magnitude, dtype=bool)
    if count:
        rows = np.repeat(np.arange(frames_count), count)
        for offset in range(-lobe, lobe + 1):
            columns = np.clip(np.round(peak_bins).astype(int).ravel() + offset, 0, magnitude.shape[1] - 1)
            mask[rows, columns] = False
    nyquist_band = 0.45 * rate
    edges = np.geomspace(40.0, nyquist_band, options.noise_bands + 1)
    noise_size = int(np.clip(2 ** np.ceil(np.log2(8.0 * rate / f0)), 2048, 16384)) if count else 2048
    noise_window = _blackman_harris(noise_size)
    noise_bin_hz = rate / noise_size
    noise_frequency = np.arange(noise_size // 2 + 1) * noise_bin_hz
    scale = 2.0 / (rate * np.sum(noise_window ** 2))
    guard = max(4.5 * noise_bin_hz, 0.2 * f0)
    partial_hz_all = peak_bins * bin_hz
    channel_noise = [np.zeros((frames_count, options.noise_bands)) for _ in channels]
    cross_sum = np.zeros(options.noise_bands, dtype=np.complex128)
    auto_sums = np.zeros((2, options.noise_bands))
    band_of_bin = np.clip(np.searchsorted(edges, noise_frequency, side="right") - 1, -1, options.noise_bands)
    in_band = (band_of_bin >= 0) & (band_of_bin < options.noise_bands)
    # A chunk of frames at a time: long notes would need gigabytes of spectra at once.
    chunk = 256
    for first in range(0, frames_count, chunk):
        last = min(frames_count, first + chunk)
        free = np.ones((last - first, len(noise_frequency)), dtype=bool)
        if count:
            for column in range(count):
                free &= np.abs(noise_frequency[None, :] - partial_hz_all[first:last, column:column + 1]) > guard
        spectra_by_channel = []
        for index, channel in enumerate(channels):
            padded = np.concatenate([np.zeros(noise_size // 2), channel, np.zeros(noise_size)])
            frames = np.lib.stride_tricks.sliding_window_view(padded, noise_size)[np.arange(first, last) * hop]
            spectra = np.fft.rfft(frames * noise_window, noise_size)
            spectra_by_channel.append(spectra)
            channel_noise[index][first:last] = _noise_bands(np.abs(spectra) ** 2, free, noise_frequency, edges, scale)
        if stereo:
            # How alike the two noises are, band by band: their coherence over the free bins.
            usable = free & in_band[None, :]
            left, right = spectra_by_channel
            products = (left * np.conj(right))[usable]
            bands_of = np.broadcast_to(band_of_bin[None, :], usable.shape)[usable]
            cross_sum += np.bincount(bands_of, weights=products.real, minlength=options.noise_bands)[:options.noise_bands] \
                + 1j * np.bincount(bands_of, weights=products.imag, minlength=options.noise_bands)[:options.noise_bands]
            auto_sums[0] += np.bincount(bands_of, weights=np.abs(left[usable]) ** 2,
                                        minlength=options.noise_bands)[:options.noise_bands]
            auto_sums[1] += np.bincount(bands_of, weights=np.abs(right[usable]) ** 2,
                                        minlength=options.noise_bands)[:options.noise_bands]
    # Bands no bin could read (below the first partial's spacing): the next one up.
    for noise_table in channel_noise:
        for band in range(options.noise_bands - 2, -1, -1):
            if not np.any(noise_table[:, band]):
                noise_table[:, band] = noise_table[:, band + 1]
    noise = np.mean(channel_noise, axis=0)
    noise_balance = None
    noise_coherence = None
    if stereo:
        noise_floor = 1e-30 + 1e-9 * float(np.max(noise)) if np.max(noise) > 0 else 1e-30
        noise_balance = np.clip(10.0 * np.log10((channel_noise[1] + noise_floor) / (channel_noise[0] + noise_floor)),
                                -40.0, 40.0)
        noise_coherence = np.clip(np.abs(cross_sum) / np.sqrt(np.maximum(auto_sums[0] * auto_sums[1], 1e-60)), 0.0, 1.0)
        # A band no bin could read: the next one's.
        for band in range(options.noise_bands - 2, -1, -1):
            if auto_sums[0, band] <= 0:
                noise_coherence[band] = noise_coherence[band + 1]

    # A partial no louder than the noise around it is that noise: its peak
    # measured what a sine would show, so what the band noise alone would show
    # comes off (in power), and a breathy note keeps no buzz of empty partials.
    if count:
        partial_hz = peak_bins * bin_hz
        band_index = np.clip(np.searchsorted(edges, partial_hz) - 1, 0, options.noise_bands - 1)
        noise_at = np.take_along_axis(noise, band_index, axis=1)
        # The PSD as the Hann spectra read it, then as a sine's amplitude.
        noise_bin_power = noise_at * rate * window_power / 2.0
        noise_amplitude = 2.0 * np.sqrt(noise_bin_power) / window_sum
        # The peak search keeps the loudest of a few noise bins (about 2.3
        # times their mean): three times off, and none left 6 dB from it.
        power = harmonics ** 2
        noise_power = noise_amplitude ** 2
        harmonics = np.where(power > 4.0 * noise_power, np.sqrt(np.maximum(power - 3.0 * noise_power, 0.0)), 0.0)

    harmonic_energy = np.sum(harmonics ** 2, axis=1) / 2.0 if count else np.zeros(frames_count)
    noise_energy = np.sum(noise * np.diff(edges)[None, :], axis=1)

    # 5. The transient: where the residual's waveform is loud at the start.
    residual_spectra = np.where(mask, np.mean(channel_spectra, axis=0), 0.0)
    residual_energy = np.sum(np.abs(residual_spectra) ** 2, axis=1)
    attack_peak = int(np.argmax(residual_energy[: max(1, int(0.2 / FRAME_SECONDS))]))
    threshold = residual_energy[attack_peak] * 0.25
    attack_end = attack_peak
    while attack_end + 1 < frames_count and residual_energy[attack_end + 1] > threshold:
        attack_end += 1
    attack_frames = int(np.clip(attack_end + 1, 2, 15))
    total = np.max(energy) if np.max(energy) > 0 else 1.0
    transient = residual_energy[attack_peak] > total * 10 ** (-35 / 10)

    attack = None
    head = None
    head_fade = 0
    head_phase = None
    head_ms = 0.0
    if options.keep_attack and options.head:
        # The head: until the transient has passed (and three periods at least, for the phases),
        # within 60 to 250 ms; the model takes over in its last 20 ms.
        fade = int(HEAD_FADE_SECONDS * rate)
        length = int(np.clip((attack_frames + 2) * hop, 0.06 * rate, 0.2 * rate))
        length = int(min(max(length, 3.0 * rate / f0 + fade), 0.25 * rate))
        length = min(length, len(signal) - 1)
        if length > 2 * fade:
            head = np.stack(channels, axis=1)[:length].copy()
            head_fade = fade
            head_ms = 1000.0 * length / rate
            at = length - fade
            frame = min(frames_count - 1, at // hop)
            if count:
                frequencies = k * curve[frame] * np.sqrt(1.0 + inharmonicity * k * k)
                head_phase = _phases_at(channels[0], rate, at, frequencies, 1.0 / f0)
            else:
                head_phase = np.zeros(0)
    elif options.keep_attack and transient:
        # The attack: the residual's own waveform while it is loud, then the band noise takes over.
        length = attack_frames * hop
        fade = int(0.01 * rate)
        waveform = _istft(residual_spectra[: attack_frames + size // hop + 2], size, hop, fft_size, length + fade)
        waveform[length:] *= np.cos(np.linspace(0.0, np.pi / 2, len(waveform) - length)) ** 2
        attack = waveform
        ramp = np.clip((np.arange(frames_count) - (attack_frames - 1)) / 2.0, 0.0, 1.0)
        noise *= ramp[:, None]

    # 6. Loop and release from the harmonic (or total) energy.
    level = harmonic_energy if count else noise_energy
    level_db = 10.0 * np.log10(np.maximum(level, 1e-20))
    peak_frame = int(np.argmax(level_db))
    loop = None
    release = None
    tail = level_db[peak_frame:]
    floor = level_db[peak_frame] - 20.0
    last_loud = peak_frame + int(np.nonzero(tail > floor)[0][-1]) if np.any(tail > floor) else peak_frame
    body_start = min(last_loud, peak_frame + 10)
    body = level_db[body_start:last_loud + 1]
    if len(body) >= 10:
        stable = np.median(body)
        held = np.nonzero(level_db[body_start:] >= stable - 3.0)[0]
        release_frame = body_start + int(held[-1]) if len(held) else body_start
        sustained_body = level_db[body_start:release_frame + 1]
        sustained = (release_frame - peak_frame) >= 30 and (np.max(sustained_body) - np.min(sustained_body)) < 6.0
        if options.loop is not None:
            sustained = options.loop and (release_frame - body_start) >= 10
        if sustained:
            length = release_frame - body_start
            loop = (body_start + length // 4, max(body_start + length // 4 + 2, release_frame - 3))
            release = release_frame

    velocity = options.velocity
    if velocity is None:
        peak_db = 20.0 * np.log10(max(np.max(np.abs(signal)), 1e-9))
        velocity = float(np.clip((peak_db + 36.0) / 36.0, 0.1, 1.0))

    root_note = int(np.round(hz_to_midi(f0)))
    zone = Zone(root_note=root_note, f0=float(f0), velocity=float(velocity), frame_seconds=hop / rate,
                harmonics=harmonics, noise_edges=edges, noise=noise, pitch_cents=cents,
                inharmonicity=inharmonicity, gain=1.0, attack=attack, attack_rate=rate, attack_gain=1.0,
                loop=loop, release=release)
    if stereo:
        zone.balance = balance if balance is not None else np.zeros((frames_count, count))
        zone.phase = phase if phase is not None else np.zeros(count)
        zone.noise_balance = noise_balance
        zone.noise_coherence = noise_coherence
    if head is not None:
        zone.head = head
        zone.head_rate = rate
        zone.head_fade = head_fade
        zone.head_phase = head_phase
    report = {
        "pitched": bool(pitched),
        "f0": float(f0),
        "note": root_note,
        "cents": float(1200.0 * np.log2(f0 / midi_to_hz(root_note))),
        "seconds": len(signal) / rate,
        "harmonics": count,
        "inharmonicity": inharmonicity,
        "attackMs": attack_frames * FRAME_SECONDS * 1000.0 if attack is not None else 0.0,
        "headMs": round(head_ms, 1),
        "stereo": bool(stereo),
        "sustained": loop is not None,
        "windowSize": size,
    }
    report["musical"] = musical_parameters(zone, level_db, peak_frame, last_loud)
    return zone, report


def musical_parameters(zone: Zone, level_db: np.ndarray, peak_frame: int, last_loud: int) -> dict:
    """Readable parameters: an ADSR, brightness, breath, spectral slope."""
    seconds = zone.frame_seconds
    peak = level_db[peak_frame]
    attack = peak_frame * seconds
    if zone.loop is not None:
        sustain_db = float(np.median(level_db[zone.loop[0]:zone.loop[1] + 1]) - peak)
        reach = np.nonzero(level_db[peak_frame:] <= peak + sustain_db + 1.0)[0]
        decay = float(reach[0] * seconds) if len(reach) else 0.1
        after = level_db[zone.release:]
        below = np.nonzero(after <= after[0] - 40.0)[0]
        release = float(below[0] * seconds * 1.5) if len(below) else 0.3
    else:
        sustain_db = -120.0
        below = np.nonzero(level_db[peak_frame:] <= peak - 60.0)[0]
        decay = float(below[0] * seconds) if len(below) else float((len(level_db) - peak_frame) * seconds) * 60.0 / max(
            1.0, peak - level_db[-1])
        release = min(decay, 0.3)
    parameters = {
        "attackSeconds": round(float(attack), 4),
        "decaySeconds": round(float(decay), 4),
        "sustainDb": round(float(sustain_db), 2),
        "releaseSeconds": round(float(release), 4),
        "inharmonicity": zone.inharmonicity,
        "f0": zone.f0,
    }
    if zone.harmonics.shape[1]:
        k = np.arange(1, zone.harmonics.shape[1] + 1)
        steady = zone.harmonics[max(0, peak_frame):max(peak_frame + 1, min(last_loud, peak_frame + 50))]
        mean_amplitude = np.sqrt(np.mean(steady ** 2, axis=0))
        weight = mean_amplitude ** 2
        parameters["brightness"] = round(float(np.sum(k * weight) / max(np.sum(weight), 1e-20)), 3)
        audible = mean_amplitude > mean_amplitude.max() * 1e-3
        if audible.sum() >= 3:
            slope = np.polyfit(np.log2(k[audible]), 20 * np.log10(mean_amplitude[audible]), 1)[0]
            parameters["slopeDbPerOctave"] = round(float(slope), 2)
            # Where the partials fall 24 dB under the strongest: a low-pass's cutoff.
            strongest = 20 * np.log10(mean_amplitude.max())
            under = np.nonzero(20 * np.log10(np.maximum(mean_amplitude, 1e-12)) > strongest - 24.0)[0]
            parameters["cutoffHz"] = round(float((under[-1] + 1) * zone.f0 * 1.2), 1)
        harmonic_power = float(np.sum(mean_amplitude ** 2) / 2.0)
        noise_power = float(np.mean(np.sum(zone.noise * np.diff(zone.noise_edges)[None, :], axis=1)))
        parameters["breathDb"] = round(10.0 * np.log10(max(noise_power, 1e-20) / max(harmonic_power, 1e-20)), 2)
    return parameters
