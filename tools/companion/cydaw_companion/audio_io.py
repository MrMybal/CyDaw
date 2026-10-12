"""WAV files in and out, with numpy alone.

Reads PCM (8, 16, 24, 32 bits) and IEEE float (32, 64 bits), plain or
WAVE_FORMAT_EXTENSIBLE; writes 16-bit PCM or 32-bit float.
"""
from __future__ import annotations

import struct
from pathlib import Path

import numpy as np

_PCM = 1
_FLOAT = 3
_EXTENSIBLE = 0xFFFE


def read_wav(path: str | Path) -> tuple[np.ndarray, int]:
    """The file's samples as float64 (frames, channels) in -1..1, and its rate."""
    data = Path(path).read_bytes()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV file")
    position = 12
    fmt = None
    samples = None
    while position + 8 <= len(data):
        chunk_id = data[position:position + 4]
        size = struct.unpack_from("<I", data, position + 4)[0]
        body = data[position + 8:position + 8 + size]
        if chunk_id == b"fmt ":
            tag, channels, rate, _, block_align, bits = struct.unpack_from("<HHIIHH", body, 0)
            if tag == _EXTENSIBLE and len(body) >= 26:
                tag = struct.unpack_from("<H", body, 24)[0]
            fmt = (tag, channels, rate, block_align, bits)
        elif chunk_id == b"data":
            samples = body
        position += 8 + size + (size & 1)
    if fmt is None or samples is None:
        raise ValueError(f"{path}: no format or no data")
    tag, channels, rate, block_align, bits = fmt
    frames = len(samples) // block_align
    samples = samples[:frames * block_align]
    if tag == _FLOAT and bits == 32:
        audio = np.frombuffer(samples, dtype="<f4").astype(np.float64)
    elif tag == _FLOAT and bits == 64:
        audio = np.frombuffer(samples, dtype="<f8").astype(np.float64)
    elif tag == _PCM and bits == 8:
        audio = (np.frombuffer(samples, dtype=np.uint8).astype(np.float64) - 128.0) / 128.0
    elif tag == _PCM and bits == 16:
        audio = np.frombuffer(samples, dtype="<i2").astype(np.float64) / 32768.0
    elif tag == _PCM and bits == 24:
        raw = np.frombuffer(samples, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        values = raw[:, 0] | (raw[:, 1] << 8) | (raw[:, 2] << 16)
        values = np.where(values >= 1 << 23, values - (1 << 24), values)
        audio = values.astype(np.float64) / float(1 << 23)
    elif tag == _PCM and bits == 32:
        audio = np.frombuffer(samples, dtype="<i4").astype(np.float64) / float(1 << 31)
    else:
        raise ValueError(f"{path}: unsupported WAV encoding (format {tag}, {bits} bits)")
    return audio.reshape(-1, channels), int(rate)


def write_wav(path: str | Path, audio: np.ndarray, rate: int, float32: bool = False) -> None:
    """Writes (frames,) or (frames, channels) samples in -1..1."""
    audio = np.asarray(audio, dtype=np.float64)
    if audio.ndim == 1:
        audio = audio[:, None]
    channels = audio.shape[1]
    if float32:
        payload = audio.astype("<f4").tobytes()
        tag, bits = _FLOAT, 32
    else:
        payload = (np.clip(audio, -1.0, 1.0) * 32767.0).round().astype("<i2").tobytes()
        tag, bits = _PCM, 16
    block_align = channels * bits // 8
    header = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(payload), b"WAVE", b"fmt ", 16, tag, channels,
                         rate, rate * block_align, block_align, bits, b"data", len(payload))
    Path(path).write_bytes(header + payload)


def mono(audio: np.ndarray) -> np.ndarray:
    return audio.mean(axis=1) if audio.ndim == 2 else audio


def resample(signal: np.ndarray, rate: int, target: int) -> np.ndarray:
    """Band-limited resampling by FFT (fine for a whole short note)."""
    if rate == target:
        return signal
    count = int(round(len(signal) * target / rate))
    spectrum = np.fft.rfft(signal)
    bins = count // 2 + 1
    if bins <= len(spectrum):
        spectrum = spectrum[:bins]
    else:
        spectrum = np.concatenate([spectrum, np.zeros(bins - len(spectrum), dtype=complex)])
    return np.fft.irfft(spectrum, count) * (count / len(signal))
