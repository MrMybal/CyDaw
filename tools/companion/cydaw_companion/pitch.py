"""The pitch of a note over time: YIN (de Cheveigné & Kawahara, 2002).

Vectorised over frames with numpy: the difference function comes from an FFT
autocorrelation and running energies, then the cumulative mean normalised
difference, an absolute threshold and parabolic interpolation.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass
class PitchTrack:
    times: np.ndarray        # seconds, frame centres
    f0: np.ndarray           # Hz, 0 where unvoiced
    aperiodicity: np.ndarray  # the CMNDF minimum: 0 periodic .. 1 noise
    energy: np.ndarray       # frame RMS

    def median_f0(self) -> float:
        """The note's pitch: the energy-weighted median of the voiced frames."""
        voiced = self.f0 > 0
        if not np.any(voiced):
            return 0.0
        f0 = self.f0[voiced]
        weight = self.energy[voiced] ** 2
        order = np.argsort(f0)
        cumulative = np.cumsum(weight[order])
        return float(f0[order][np.searchsorted(cumulative, cumulative[-1] * 0.5)])


def yin(signal: np.ndarray, rate: int, hop: int, minimum_hz: float = 30.0, maximum_hz: float = 2500.0,
        window: int | None = None, threshold: float = 0.15) -> PitchTrack:
    tau_max = int(np.ceil(rate / minimum_hz))
    tau_min = max(2, int(np.floor(rate / maximum_hz)))
    window = window or max(1024, tau_max)
    span = window + tau_max
    padded = np.concatenate([np.zeros(span // 2), signal, np.zeros(span)])
    count = max(1, int(np.ceil(len(signal) / hop)))
    starts = np.arange(count) * hop
    frames = np.lib.stride_tricks.sliding_window_view(padded, span)[starts]
    # r_t(tau) = sum_{j<W} x[j] x[j+tau], by FFT.
    size = 1 << int(np.ceil(np.log2(span + window)))
    head = np.fft.rfft(frames[:, :window], size)
    whole = np.fft.rfft(frames, size)
    correlation = np.fft.irfft(np.conj(head) * whole, size)[:, :tau_max + 1]
    squares = frames ** 2
    cumulative = np.concatenate([np.zeros((count, 1)), np.cumsum(squares, axis=1)], axis=1)
    energy_head = cumulative[:, window][:, None]
    taus = np.arange(tau_max + 1)
    energy_shift = cumulative[:, taus + window] - cumulative[:, taus]
    difference = energy_head + energy_shift - 2.0 * correlation
    difference[:, 0] = 0.0
    running = np.cumsum(difference[:, 1:], axis=1)
    cmndf = np.ones_like(difference)
    cmndf[:, 1:] = difference[:, 1:] * taus[1:] / np.maximum(running, 1e-12)

    f0 = np.zeros(count)
    aperiodicity = np.ones(count)
    for index in range(count):
        row = cmndf[index]
        below = np.nonzero(row[tau_min:tau_max] < threshold)[0]
        if len(below):
            tau = below[0] + tau_min
            while tau + 1 < tau_max and row[tau + 1] < row[tau]:
                tau += 1
        else:
            tau = int(np.argmin(row[tau_min:tau_max]) + tau_min)
        aperiodicity[index] = float(row[tau])
        if aperiodicity[index] > 0.35:
            continue
        # Parabolic interpolation around the dip.
        if 1 <= tau < tau_max:
            a, b, c = row[tau - 1], row[tau], row[tau + 1]
            denominator = a - 2 * b + c
            shift = 0.5 * (a - c) / denominator if abs(denominator) > 1e-12 else 0.0
        else:
            shift = 0.0
        f0[index] = rate / (tau + float(np.clip(shift, -1.0, 1.0)))
    energy = np.sqrt(np.mean(frames[:, :window] ** 2, axis=1))
    times = (starts + window / 2 - span // 2) / rate
    silent = energy < energy.max() * 1e-3 if energy.max() > 0 else np.ones(count, dtype=bool)
    f0[silent] = 0.0
    return PitchTrack(times, f0, aperiodicity, energy)


def hz_to_midi(frequency: float) -> float:
    return 69.0 + 12.0 * np.log2(frequency / 440.0)


def midi_to_hz(note: float) -> float:
    return 440.0 * 2.0 ** ((note - 69.0) / 12.0)
