"""Refinement in the manner of DDSP: the model's curves tuned by gradient descent.

The reference player (synth.py), written again in PyTorch so it can be
differentiated, renders the zone along the recording's own timeline (no loop:
frame p at p frameSeconds); a multi-scale spectral loss against the recording
says how far it is, and Adam moves:

* every partial's amplitude in every frame (in log, so it stays positive),
* the inharmonicity B.

The noise bands keep the analysis's PSD: a spectral loss between two noises
is a matter of chance, and chasing it biased their level.

starting from the classic analysis. No trained network: the parameters of this
one sound are what is learnt. On a GPU (CUDA) a few hundred steps take
seconds; on the processor, about a minute.

The result is kept only when it is closer to the recording (third-octave band
error) than the analysis was.
"""
from __future__ import annotations

import math
from typing import Callable

import numpy as np

from .audio_io import mono, resample
from .loss import band_error_db
from .model import Zone
from .synth import GOLDEN, PlayParameters, render

RATE = 48000
SCALES = (2048, 1024, 512, 256, 128, 64)


def _device(choice: str):
    import torch  # noqa: PLC0415
    if choice == "cuda" or (choice == "auto" and torch.cuda.is_available()):
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA was asked for, and PyTorch sees no CUDA device")
        return torch.device("cuda")
    return torch.device("cpu")


def _source(audio: np.ndarray, rate: int) -> np.ndarray:
    signal = mono(np.asarray(audio, dtype=np.float64))
    if rate != RATE:
        signal = resample(signal, rate, RATE)
    peak = np.max(np.abs(signal))
    loud = np.nonzero(np.abs(signal) > peak * 10 ** (-50 / 20))[0]
    return signal[max(0, loud[0] - int(0.002 * RATE)):]


def refine(zone: Zone, audio: np.ndarray, rate: int, device: str = "auto", steps: int = 300,
           progress: Callable[[float, str], None] | None = None) -> dict:
    import torch  # noqa: PLC0415

    report = {"steps": 0, "device": "none", "kept": False}
    source = _source(audio, rate)
    length = min(len(source), int(round(zone.frames * zone.frame_seconds * RATE)))
    if length < 2048 or zone.frames < 4:
        report["skipped"] = "too short"
        return report
    source = source[:length]
    held = zone.release * zone.frame_seconds if zone.release is not None else None
    straight = PlayParameters()

    # How far the analysis alone is: the same straight timeline as below.
    def straight_render(z: Zone) -> np.ndarray:
        copy = Zone(**{**z.__dict__, "loop": None, "release": None})
        return render(copy, 69 + 12 * math.log2(z.f0 / 440.0), z.velocity, RATE, length / RATE, None, straight)

    before = band_error_db(source, straight_render(zone))
    torch_device = _device(device)
    report["device"] = str(torch_device)
    dtype = torch.float32
    target = torch.tensor(source, dtype=dtype, device=torch_device)

    frames = zone.frames
    count = zone.harmonics.shape[1]
    bands = zone.noise.shape[1]
    samples = length
    # Where each sample sits among the frames (straight), for interpolation.
    position = torch.arange(samples, device=torch_device, dtype=dtype) / (zone.frame_seconds * RATE)
    lower = torch.clamp(position.floor().long(), 0, frames - 1)
    upper = torch.clamp(lower + 1, max=frames - 1)
    weight = (position - lower.to(dtype)).clamp(0, 1).unsqueeze(1)

    def per_sample(curves):  # (frames, n) -> (samples, n)
        return curves[lower] * (1 - weight) + curves[upper] * weight

    floor = 1e-7
    log_harmonics = torch.tensor(np.log(np.maximum(zone.harmonics, floor)), dtype=dtype, device=torch_device,
                                 requires_grad=True)
    log_noise = torch.tensor(np.log(np.maximum(zone.noise, 1e-20)), dtype=dtype, device=torch_device)
    log_b = torch.tensor(math.log(max(zone.inharmonicity, 1e-7)), dtype=dtype, device=torch_device, requires_grad=True)
    cents = torch.tensor(zone.pitch_cents, dtype=dtype, device=torch_device).unsqueeze(1)
    k = torch.arange(1, count + 1, device=torch_device, dtype=dtype)
    frequency = zone.f0
    phases0 = 2 * math.pi * torch.tensor((np.arange(1, count + 1) * GOLDEN) % 1.0, dtype=dtype, device=torch_device)
    nyquist_guard = 0.45 * RATE

    # The noise: white noise shaped by the band PSD (the player's sqrt-Hann
    # STFT at the same size), bins mapped to bands once. A new noise every
    # step: with one fixed stream the partials learnt to answer that noise.
    size = int(2 ** round(math.log2(0.0213 * RATE)))
    hop = size // 2
    window = torch.sqrt(torch.hann_window(size, periodic=True, device=torch_device, dtype=dtype))
    generator = torch.Generator(device="cpu").manual_seed(1)

    def fresh_noise():
        stream = ((torch.rand(samples + size, generator=generator) * 2 - 1) * math.sqrt(3.0)).to(torch_device, dtype)
        return torch.stft(stream, size, hop, window=window, center=False, return_complex=True)
    bin_hz = torch.arange(size // 2 + 1, device=torch_device, dtype=dtype) * RATE / size
    edges = torch.tensor(zone.noise_edges, dtype=dtype, device=torch_device)
    band_of = torch.bucketize(bin_hz, edges, right=True) - 1
    inside = (band_of >= 0) & (band_of < bands)
    band_of = band_of.clamp(0, max(0, bands - 1))
    hop_frames = 1 + samples // hop
    # Each STFT frame t covers stream[t hop, t hop + size): output time t hop - hop, centre t hop.
    hop_position = (torch.arange(hop_frames, device=torch_device, dtype=dtype) * hop / (zone.frame_seconds * RATE))
    hop_lower = torch.clamp(hop_position.floor().long(), 0, frames - 1)
    hop_upper = torch.clamp(hop_lower + 1, max=frames - 1)
    hop_weight = (hop_position - hop_lower.to(dtype)).clamp(0, 1).unsqueeze(1)

    attack = torch.zeros(samples, device=torch_device, dtype=dtype)
    if zone.attack is not None and len(zone.attack):
        n = min(samples, len(zone.attack))
        attack[:n] = torch.tensor(zone.attack[:n] * zone.attack_gain, dtype=dtype, device=torch_device)
    onset = torch.clamp(torch.arange(samples, device=torch_device, dtype=dtype) / (0.002 * RATE), 0, 1)

    def synthesise(noise_spectrum):
        amplitude = per_sample(torch.exp(log_harmonics))
        b = torch.exp(log_b)
        partial = frequency * torch.pow(2.0, per_sample(cents) / 1200.0) * (k * torch.sqrt(1 + b * k * k))
        amplitude = amplitude * torch.clamp((nyquist_guard - partial) / (0.05 * RATE), 0, 1)
        phase = phases0 + 2 * math.pi * torch.cumsum(partial, dim=0) / RATE - 2 * math.pi * partial[0:1] / RATE
        harmonic = torch.sum(amplitude * torch.sin(phase), dim=1) * onset
        psd = torch.exp(log_noise)
        psd_hop = psd[hop_lower] * (1 - hop_weight) + psd[hop_upper] * hop_weight  # (hops, bands)
        gains = torch.sqrt(psd_hop[:, band_of] * RATE / 2.0) * inside  # (hops, bins)
        # Overlap-add by hand (istft refuses a window that is zero at its ends).
        frames_out = torch.fft.irfft(noise_spectrum * gains.T, n=size, dim=0) * window.unsqueeze(1)  # (size, hops)
        total = (frames_out.shape[1] - 1) * hop + size
        shaped = torch.nn.functional.fold(frames_out.unsqueeze(0), output_size=(1, total), kernel_size=(1, size),
                                          stride=(1, hop)).reshape(-1)
        noise = shaped[hop:hop + samples]
        return harmonic + noise + attack

    def magnitudes(signal, fft):
        window_m = torch.hann_window(fft, device=torch_device, dtype=dtype)
        return torch.stft(signal, fft, fft // 4, window=window_m, return_complex=True).abs()

    targets = {fft: magnitudes(target, fft) for fft in SCALES}

    def loss_of(output):
        total = 0.0
        for fft in SCALES:
            a = targets[fft]
            b = magnitudes(output, fft)
            total = total + torch.mean(torch.abs(a - b)) / torch.mean(a) + torch.mean(torch.abs(torch.log(a + 1e-5) - torch.log(b + 1e-5)))
        return total / len(SCALES)

    def expected_loss(draws: int = 4) -> float:
        # The loss over a few noises: one draw is too much a matter of chance
        # to tell a small improvement from luck.
        with torch.no_grad():
            return float(sum(float(loss_of(synthesise(fresh_noise()))) for _ in range(draws)) / draws)

    initial_harmonics = log_harmonics.detach().clone()
    initial_b = log_b.detach().clone()
    expected_before = expected_loss()
    optimiser = torch.optim.Adam([log_harmonics, log_b], lr=0.01)
    first = None
    last = None
    for step in range(max(1, steps)):
        optimiser.zero_grad()
        loss = loss_of(synthesise(fresh_noise()))
        loss.backward()
        optimiser.step()
        with torch.no_grad():
            log_harmonics.clamp_(math.log(floor), 0.0)
            log_b.clamp_(math.log(1e-7), math.log(0.005))
        value = float(loss.detach())
        first = value if first is None else first
        last = value
        if progress is not None and step % 10 == 0:
            progress(step / steps, f"refine {step}/{steps}: loss {value:.3f}")
    report["steps"] = steps
    report["lossBefore"] = round(expected_before, 4)
    expected_after = expected_loss()
    report["lossAfter"] = round(expected_after, 4)

    refined = Zone(**zone.__dict__)
    refined.harmonics = np.exp(log_harmonics.detach().cpu().numpy().astype(np.float64))
    refined.harmonics[refined.harmonics <= floor * 1.01] = 0.0
    refined.inharmonicity = float(math.exp(float(log_b.detach())))
    if refined.inharmonicity < 2e-7:
        refined.inharmonicity = 0.0
    after = band_error_db(source, straight_render(refined))
    report["bandErrorBefore"] = round(before, 3)
    report["bandErrorAfter"] = round(after, 3)
    # Kept when it is closer by the measure it was tuned on (1 % at least).
    if expected_after < expected_before * 0.99:
        zone.harmonics = refined.harmonics
        zone.inharmonicity = refined.inharmonicity
        report["kept"] = True
    del held, initial_harmonics, initial_b, first, last
    return report
