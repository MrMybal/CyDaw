"""How close a rendering is to its source: the multi-scale spectral loss.

For FFT sizes 2048 .. 64: mean |S - S'| / mean |S| (spectral convergence-like,
on linear magnitudes) plus the mean |log S - log S'|; averaged over the
scales. 0 is identical; a well matched note sits under ~1.
"""
from __future__ import annotations

import numpy as np

SCALES = (2048, 1024, 512, 256, 128, 64)


def _magnitudes(signal: np.ndarray, size: int) -> np.ndarray:
    hop = size // 4
    window = np.hanning(size + 1)[:-1]
    padded = np.concatenate([signal, np.zeros(size)])
    count = max(1, (len(signal)) // hop)
    frames = np.lib.stride_tricks.sliding_window_view(padded, size)[np.arange(count) * hop]
    return np.abs(np.fft.rfft(frames * window, axis=1))


def spectral_loss(reference: np.ndarray, candidate: np.ndarray) -> float:
    length = min(len(reference), len(candidate))
    reference = reference[:length]
    candidate = candidate[:length]
    total = 0.0
    for size in SCALES:
        a = _magnitudes(reference, size)
        b = _magnitudes(candidate, size)
        linear = np.mean(np.abs(a - b)) / max(np.mean(a), 1e-12)
        logarithmic = np.mean(np.abs(np.log(a + 1e-5) - np.log(b + 1e-5)))
        total += linear + logarithmic
    return float(total / len(SCALES))


def level_difference_db(reference: np.ndarray, candidate: np.ndarray) -> float:
    rms = lambda x: np.sqrt(np.mean(x ** 2)) if len(x) else 0.0
    return float(20.0 * np.log10(max(rms(candidate), 1e-12) / max(rms(reference), 1e-12)))


def band_error_db(reference: np.ndarray, candidate: np.ndarray, rate: int = 48000) -> float:
    """The mean distance in dB between third-octave band levels, every 20 ms,
    where the reference is within 60 dB of its loudest: how far off it sounds."""
    length = min(len(reference), len(candidate))
    size = 2048
    hop = int(0.02 * rate)
    window = np.hanning(size + 1)[:-1]
    edges = 25.0 * 2.0 ** (np.arange(0, 31) / 3.0)
    edges = edges[edges < 0.45 * rate]
    frequencies = np.fft.rfftfreq(size, 1.0 / rate)
    band_of = np.searchsorted(edges, frequencies) - 1

    def levels(signal: np.ndarray) -> np.ndarray:
        padded = np.concatenate([signal[:length], np.zeros(size)])
        count = max(1, length // hop)
        frames = np.lib.stride_tricks.sliding_window_view(padded, size)[np.arange(count) * hop]
        power = np.abs(np.fft.rfft(frames * window, axis=1)) ** 2
        bands = np.zeros((count, len(edges) - 1))
        for band in range(len(edges) - 1):
            inside = band_of == band
            if np.any(inside):
                bands[:, band] = power[:, inside].sum(axis=1)
        return 10.0 * np.log10(bands + 1e-12)

    a = levels(reference)
    b = levels(candidate)
    audible = a > a.max() - 60.0
    return float(np.mean(np.abs(a - b)[audible])) if np.any(audible) else 0.0
