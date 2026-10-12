"""Synthetic notes whose making is known, to check the analysis against.

    python tools/companion/tests/make_test_notes.py OUT_FOLDER

pluck-a3.wav    Karplus-Strong string, 220 Hz, decaying (no loop)
brass-c4.wav    saw through an opening low-pass, ADSR, vibrato, breath; held 1.4 s then released
piano-c4.wav    inharmonic partials (B = 0.0004), each decaying, a hammer knock
flute-g4.wav    few harmonics, strong breath noise, vibrato, sustained
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cydaw_companion.audio_io import write_wav  # noqa: E402

RATE = 48000
rng = np.random.default_rng(7)


def one_pole_lowpass(signal: np.ndarray, cutoff: np.ndarray | float) -> np.ndarray:
    cutoff = np.broadcast_to(np.asarray(cutoff, dtype=np.float64), signal.shape)
    coefficient = 1.0 - np.exp(-2.0 * np.pi * cutoff / RATE)
    output = np.empty_like(signal)
    state = 0.0
    for index, value in enumerate(signal):
        state += coefficient[index] * (value - state)
        output[index] = state
    return output


def pluck(frequency: float, seconds: float) -> np.ndarray:
    period = RATE / frequency
    length = int(period)
    fraction = period - length
    line = rng.uniform(-1, 1, length + 2)
    line = np.convolve(line, [0.5, 0.5], mode="same")
    output = np.empty(int(seconds * RATE))
    buffer = list(line)
    for index in range(len(output)):
        a = buffer[0]
        b = buffer[1]
        value = 0.996 * (0.5 * (a + b))
        sample = buffer.pop(0)
        buffer.append(value * (1 - fraction) + buffer[-1] * fraction if fraction else value)
        output[index] = sample
    return 0.5 * output / np.max(np.abs(output))


def brass(frequency: float, seconds: float, held: float) -> np.ndarray:
    t = np.arange(int(seconds * RATE)) / RATE
    vibrato = 1.0 + 0.004 * np.sin(2 * np.pi * 5.5 * t) * np.clip((t - 0.5) / 0.3, 0, 1)
    phase = np.cumsum(frequency * vibrato / RATE)
    saw = 2.0 * (phase % 1.0) - 1.0
    envelope = np.where(t < 0.05, t / 0.05, np.where(t < held, 1.0 - 0.3 * np.clip((t - 0.05) / 0.2, 0, 1),
                                                    0.7 * np.exp(-(t - held) / 0.08)))
    cutoff = 400.0 + 3000.0 * np.clip(envelope, 0, 1)
    tone = one_pole_lowpass(one_pole_lowpass(saw, cutoff), cutoff)
    breath = one_pole_lowpass(rng.normal(0, 1, len(t)), 3000.0) * 0.03
    return 0.4 * envelope * (tone + breath)


def piano(frequency: float, seconds: float) -> np.ndarray:
    t = np.arange(int(seconds * RATE)) / RATE
    output = np.zeros_like(t)
    b = 0.0004
    for k in range(1, 40):
        f = k * frequency * np.sqrt(1 + b * k * k)
        if f > 0.45 * RATE:
            break
        amplitude = 1.0 / k ** 1.1
        decay = 0.9 / (1 + 0.25 * k)
        output += amplitude * np.exp(-t / decay) * np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi))
    knock = rng.normal(0, 1, len(t)) * np.exp(-t / 0.006) * 0.6
    output += one_pole_lowpass(knock, 2500.0)
    attack = np.clip(t / 0.002, 0, 1)
    return 0.3 * attack * output / np.max(np.abs(output))


def flute(frequency: float, seconds: float, held: float) -> np.ndarray:
    t = np.arange(int(seconds * RATE)) / RATE
    vibrato = 1.0 + 0.006 * np.sin(2 * np.pi * 4.8 * t)
    phase = 2 * np.pi * np.cumsum(frequency * vibrato / RATE)
    tone = np.sin(phase) + 0.25 * np.sin(2 * phase) + 0.08 * np.sin(3 * phase)
    envelope = np.where(t < 0.12, t / 0.12, np.where(t < held, 1.0, np.exp(-(t - held) / 0.06)))
    breath = one_pole_lowpass(rng.normal(0, 1, len(t)), 6000.0) - one_pole_lowpass(rng.normal(0, 1, len(t)), 800.0)
    return 0.35 * envelope * (tone + 0.12 * breath)


def main() -> None:
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    folder.mkdir(parents=True, exist_ok=True)
    write_wav(folder / "pluck-a3.wav", pluck(220.0, 2.0), RATE)
    write_wav(folder / "brass-c4.wav", brass(261.63, 1.8, 1.4), RATE)
    write_wav(folder / "piano-c4.wav", piano(261.63, 3.0), RATE)
    write_wav(folder / "flute-g4.wav", flute(392.0, 2.0, 1.6), RATE)
    print("ok", folder)


if __name__ == "__main__":
    main()
