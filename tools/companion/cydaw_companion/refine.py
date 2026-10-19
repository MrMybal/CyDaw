"""Refinement in the manner of DDSP: the model's curves tuned by gradient descent.

The reference player (synth.py), written again in PyTorch so it can be
differentiated, renders the zone along the recording's own timeline (no loop:
frame p at p frameSeconds); a multi-scale spectral loss against the recording
says how far it is, and Adam moves:

* every partial's amplitude in every frame (in log, so it stays positive),
* the inharmonicity B (harmonics only; free ratios stay where they were found),
* each noise band's level (one offset per band for the whole note), on a loss of
  its own: the level of the spectrum between the partials, smoothed over
  neighbouring bins, frames and the band, where two noises agree whatever their
  draw (a spectral loss between two raw noises is a matter of chance, and
  chasing it biased their level),
* the attack's level (a zone without a head).

A zone with a head is compared from where the model comes in (the head is the
recording itself): the partials are tuned for what is heard of them, and to meet
the head where they take over.

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
from .model import Zone, channel_gains, noise_gains
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
           progress: Callable[[float, str], None] | None = None, max_seconds: float | None = None) -> dict:
    """With `max_seconds`, only the note's first seconds are heard and learnt (a long note in a
    fraction of the time): the partials' change fades out over the window's last half second, the
    frames after it kept as they were."""
    import torch  # noqa: PLC0415

    report = {"steps": 0, "device": "none", "kept": False}
    source = _source(audio, rate)
    length = min(len(source), int(round(zone.frames * zone.frame_seconds * RATE)))
    whole = length
    if max_seconds is not None:
        length = min(length, int(round(max_seconds * RATE)))
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
    noise_offset = torch.zeros(max(1, bands), dtype=dtype, device=torch_device, requires_grad=True)
    log_attack = torch.zeros((), dtype=dtype, device=torch_device, requires_grad=True)
    log_b = torch.tensor(math.log(max(zone.inharmonicity, 1e-7)), dtype=dtype, device=torch_device, requires_grad=True)
    cents = torch.tensor(zone.pitch_cents, dtype=dtype, device=torch_device).unsqueeze(1)
    k = torch.arange(1, count + 1, device=torch_device, dtype=dtype)
    free = zone.ratios is not None
    ratios = torch.tensor(zone.ratios if free else np.zeros(count), dtype=dtype, device=torch_device)
    # In stereo the player's two channels, mixed as the recording is compared (their mean): each
    # partial's share of its RMS by its balance and the right's phase, each band's by its balance
    # and how alike the two noises are.
    partial_mix = torch.ones(frames, max(1, count), dtype=dtype, device=torch_device)
    noise_mix = torch.ones(frames, max(1, bands), dtype=dtype, device=torch_device)
    if zone.stereo and count:
        left, right = channel_gains(zone.balance)
        mixed = np.abs(left + right * np.exp(1j * zone.phase[None, :count])) / 2.0
        partial_mix = torch.tensor(mixed, dtype=dtype, device=torch_device)
    if zone.stereo and bands and zone.noise_balance is not None:
        left, right = noise_gains(zone.noise_balance)
        coherence = np.asarray(zone.noise_coherence)[None, :]
        noise_mix = torch.tensor((left + right + 2.0 * coherence * np.sqrt(left * right)) / 4.0, dtype=dtype, device=torch_device)
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
    # A head: the recording plays, the model comes in over its fade; compared from there.
    has_head = zone.head is not None and len(zone.head) > 2 * max(1, zone.head_fade)
    head = torch.zeros(samples, device=torch_device, dtype=dtype)
    model_in = torch.ones(samples, device=torch_device, dtype=dtype)
    compared_from = 0
    if has_head:
        recorded = np.asarray(zone.head, dtype=np.float64)
        recorded = recorded.mean(axis=1) if recorded.ndim == 2 else recorded
        n = min(samples, len(recorded))
        head[:n] = torch.tensor(recorded[:n], dtype=dtype, device=torch_device)
        fade_start = len(recorded) - zone.head_fade
        model_in = torch.clamp((torch.arange(samples, device=torch_device, dtype=dtype) - fade_start) / max(1, zone.head_fade), 0, 1)
        compared_from = max(0, min(samples - 4096, fade_start))
        onset = torch.ones(samples, device=torch_device, dtype=dtype)
    snap_at = None
    snap_phases = None
    if has_head and zone.head_phase is not None and len(zone.head_phase) and fade_start < samples:
        snap_at = int(fade_start)
        own = min(count, len(zone.head_phase))
        values = np.asarray(phases0.cpu().numpy(), dtype=np.float64)
        values[:own] = zone.head_phase[:own]
        snap_phases = torch.tensor(values, dtype=dtype, device=torch_device)

    def synthesise(noise_spectrum):
        amplitude = per_sample(torch.exp(log_harmonics) * partial_mix[:, :count])
        b = torch.exp(log_b)
        multiple = ratios if free else k * torch.sqrt(1 + b * k * k)
        partial = frequency * torch.pow(2.0, per_sample(cents) / 1200.0) * multiple
        amplitude = amplitude * torch.clamp((nyquist_guard - partial) / (0.05 * RATE), 0, 1)
        travelled = 2 * math.pi * torch.cumsum(partial, dim=0) / RATE
        if snap_at is not None:
            # As the player: the partials take the recording's phases where the head gives way.
            phase = snap_phases + travelled - travelled[snap_at:snap_at + 1]
        else:
            phase = phases0 + travelled - 2 * math.pi * partial[0:1] / RATE
        harmonic = torch.sum(amplitude * torch.sin(phase), dim=1) * onset
        psd = torch.exp(log_noise + noise_offset[:bands]) * noise_mix[:, :bands]
        psd_hop = psd[hop_lower] * (1 - hop_weight) + psd[hop_upper] * hop_weight  # (hops, bands)
        gains = torch.sqrt(psd_hop[:, band_of] * RATE / 2.0) * inside  # (hops, bins)
        # Overlap-add by hand (istft refuses a window that is zero at its ends).
        frames_out = torch.fft.irfft(noise_spectrum * gains.T, n=size, dim=0) * window.unsqueeze(1)  # (size, hops)
        total = (frames_out.shape[1] - 1) * hop + size
        shaped = torch.nn.functional.fold(frames_out.unsqueeze(0), output_size=(1, total), kernel_size=(1, size),
                                          stride=(1, hop)).reshape(-1)
        noise = shaped[hop:hop + samples]
        model = harmonic + noise + attack * torch.exp(log_attack)
        return model * model_in + head * (1 - model_in)

    def magnitudes(signal, fft):
        window_m = torch.hann_window(fft, device=torch_device, dtype=dtype)
        return torch.stft(signal, fft, fft // 4, window=window_m, return_complex=True).abs()

    compared = target[compared_from:]
    targets = {fft: magnitudes(compared, fft) for fft in SCALES}

    # The noise between the partials: power spectra smoothed over 5 bins and 4 frames, summed
    # per band, on the bins away from every partial (as the analysis read it).
    smooth_fft = 2048
    smooth_hop = 512
    smooth_bins = torch.arange(smooth_fft // 2 + 1, device=torch_device, dtype=dtype) * RATE / smooth_fft
    expected = (zone.ratios * zone.f0 if free
                else np.arange(1, count + 1) * zone.f0 * np.sqrt(1.0 + zone.inharmonicity * np.arange(1, count + 1) ** 2))
    guard = max(4.0 * RATE / smooth_fft, 0.2 * zone.f0)
    between = torch.ones(smooth_fft // 2 + 1, dtype=torch.bool, device=torch_device)
    for value in expected:
        between &= torch.abs(smooth_bins - float(value)) > guard
    smooth_band = torch.bucketize(smooth_bins, edges, right=True) - 1
    usable = between & (smooth_band >= 0) & (smooth_band < bands)
    smooth_band = smooth_band.clamp(0, max(0, bands - 1))
    membership = torch.zeros(smooth_fft // 2 + 1, max(1, bands), device=torch_device, dtype=dtype)
    if bands:
        membership[usable.nonzero().squeeze(1), smooth_band[usable]] = 1.0
    smoothing = torch.ones(1, 1, 4, 5, device=torch_device, dtype=dtype) / 20.0

    def band_levels(signal):
        window_s = torch.hann_window(smooth_fft, device=torch_device, dtype=dtype)
        power = torch.stft(signal, smooth_fft, smooth_hop, window=window_s, return_complex=True).abs() ** 2  # (bins, frames)
        power = torch.nn.functional.conv2d(power.T.unsqueeze(0).unsqueeze(0), smoothing, padding=(2, 2))[0, 0, :power.shape[1], :power.shape[0]]
        return power @ membership  # (frames, bands)

    noise_target = band_levels(compared)
    noise_audible = noise_target > noise_target.max() * 1e-6

    def loss_of(output):
        output = output[compared_from:]
        total = 0.0
        for fft in SCALES:
            a = targets[fft]
            b = magnitudes(output, fft)
            total = total + torch.mean(torch.abs(a - b)) / torch.mean(a) + torch.mean(torch.abs(torch.log(a + 1e-5) - torch.log(b + 1e-5)))
        total = total / len(SCALES)
        if bands and bool(noise_audible.any()):
            levels = band_levels(output)
            total = total + 0.5 * torch.mean(torch.abs(torch.log(levels[noise_audible] + 1e-12)
                                                       - torch.log(noise_target[noise_audible] + 1e-12)))
        return total

    def expected_loss(draws: int = 4) -> float:
        # The loss over a few noises: one draw is too much a matter of chance
        # to tell a small improvement from luck.
        with torch.no_grad():
            return float(sum(float(loss_of(synthesise(fresh_noise()))) for _ in range(draws)) / draws)

    initial_harmonics = log_harmonics.detach().clone()
    initial_b = log_b.detach().clone()
    expected_before = expected_loss()
    learnt = [log_harmonics, noise_offset]
    if not free:
        learnt.append(log_b)
    if zone.attack is not None and len(zone.attack) and not has_head:
        learnt.append(log_attack)
    optimiser = torch.optim.Adam(learnt, lr=0.01)
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
            # A band moves 12 dB at most; the attack 9 dB.
            noise_offset.clamp_(-math.log(10 ** 1.2), math.log(10 ** 1.2))
            log_attack.clamp_(-math.log(10 ** 0.45), math.log(10 ** 0.45))
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
    learnt_log = log_harmonics.detach().cpu().numpy().astype(np.float64)
    if length < whole:
        # Heard only so far: the change faded out at the window's end, none after it.
        start_log = initial_harmonics.cpu().numpy().astype(np.float64)
        end = length / RATE
        times = np.arange(zone.frames) * zone.frame_seconds
        weight = np.clip((end - times) / 0.5, 0.0, 1.0)[:, None]
        learnt_log = start_log + (learnt_log - start_log) * weight
        report["seconds"] = round(end, 2)
    refined.harmonics = np.exp(learnt_log)
    refined.harmonics[refined.harmonics <= floor * 1.01] = 0.0
    refined.inharmonicity = 0.0 if free else float(math.exp(float(log_b.detach())))
    if refined.inharmonicity < 2e-7:
        refined.inharmonicity = 0.0
    offsets = np.exp(noise_offset.detach().cpu().numpy().astype(np.float64))[:bands]
    refined.noise = zone.noise * offsets[None, :]
    refined.attack_gain = zone.attack_gain * float(math.exp(float(log_attack.detach())))
    report["noiseOffsetsDb"] = [round(float(10.0 * np.log10(value)), 2) for value in offsets]
    report["attackDb"] = round(float(20.0 * float(log_attack.detach()) / math.log(10.0)), 2)
    after = band_error_db(source, straight_render(refined))
    report["bandErrorBefore"] = round(before, 3)
    report["bandErrorAfter"] = round(after, 3)
    # Kept when it is closer by the measure it was tuned on (1 % at least), and no further by
    # what is heard (the third-octave bands).
    if expected_after < expected_before * 0.99 and after <= before + 0.05:
        zone.harmonics = refined.harmonics
        zone.inharmonicity = refined.inharmonicity
        zone.noise = refined.noise
        zone.attack_gain = refined.attack_gain
        report["kept"] = True
    del held, initial_harmonics, initial_b, first, last
    return report
