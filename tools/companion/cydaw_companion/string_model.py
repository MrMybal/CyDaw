"""A plucked string made playable: extended Karplus-Strong, the instrument's body around it.

A zone of a plucked or struck string (a guitar, a harp, a harpsichord) can carry, besides its
partials, the string that made them ("string" in the zone):

    "string": {
      "gain": g, "pole": a, "zero": b, # the loop's loss per period:
      "order": 1 | 2,                  #   g ((1 - a) / (1 - b) (1 - b z^-1) / (1 - a z^-1))^order,
                                       #   unity at DC times g; b < a falls towards the highs, to
                                       #   a shelf (b = 0: a low-pass); two alike, a steeper fall
      "dispersion": c, "stages": M,    # M first-order allpasses (c): the string's stiffness
      "dips": [{"hz": f, "q": Q, "db": d}, ...],
                                       # the body's own losses: up to 3 peaking cuts in the loop
                                       # (d dB per period at f, fixed in Hz), where the body
                                       # takes the string's energy (a guitar's mids die early)
      "start": f,                      # the frame it takes over at (the head's fade, or 0)
      "excitation": [K],               # each harmonic's level there, dB x 10, the body taken out
      "bodyEdges": [B + 1], "body": [B]  # the body: its level per band (dB x 10), fixed in Hz
    }

At a note of frequency f the voice lays one period of the excitation (its harmonics, each times
the body at its new frequency) in a delay line about rate / f samples long and lets it go round:
the loss filter decays each harmonic at the rate the recording's did (high ones faster), the
allpasses stretch the upper ones as a stiff string does, a last allpass carries the period's
fraction. Played higher, the string decays faster and the body keeps its resonances where they
are; nothing of the recording is replayed but its head (its pluck).

The same code is the reference here and in C++ (PluckedString in src/engine/SoundModel.cpp).
"""
from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

MAXIMUM_STAGES = 4
LOWEST_HZ = 16.0


@dataclass
class StringModel:
    gain: float                 # loss per period at DC
    pole: float                 # the loss filter's pole (0: no frequency dependence)
    order: int                  # 1 or 2 such stages
    dispersion: float           # the stiffness allpasses' coefficient (<= 0)
    stages: int                 # how many (0..4)
    start: int                  # the frame it takes over at
    excitation_db: np.ndarray   # (K,) each harmonic's level there, dB, the body taken out
    body_edges: np.ndarray      # (B + 1,) Hz
    body_db: np.ndarray         # (B,) dB
    zero: float = 0.0           # the loss filter's zero (<= pole): a shelf rather than a low-pass
    dips: list = None           # [(hz, q, db)] the body's losses in the loop

    def to_json(self) -> dict:
        return {"gain": round(float(self.gain), 7), "pole": round(float(self.pole), 6), "zero": round(float(self.zero), 6),
                "order": int(self.order),
                "dispersion": round(float(self.dispersion), 5), "stages": int(self.stages), "start": int(self.start),
                "excitation": [int(round(v * 10)) for v in self.excitation_db],
                "dips": [{"hz": round(float(f), 2), "q": round(float(q), 3), "db": round(float(d), 4)} for f, q, d in (self.dips or [])],
                "bodyEdges": [round(float(e), 2) for e in self.body_edges],
                "body": [int(round(v * 10)) for v in self.body_db]}

    @staticmethod
    def from_json(value: dict) -> "StringModel":
        return StringModel(gain=float(value["gain"]), pole=float(value["pole"]), order=int(value.get("order", 1)),
                           zero=float(value.get("zero", 0.0)),
                           dispersion=float(value.get("dispersion", 0.0)),
                           stages=int(value.get("stages", 0)), start=int(value.get("start", 0)),
                           excitation_db=np.asarray(value["excitation"], dtype=np.float64) / 10.0,
                           body_edges=np.asarray(value["bodyEdges"], dtype=np.float64),
                           body_db=np.asarray(value["body"], dtype=np.float64) / 10.0,
                           dips=[(float(d["hz"]), float(d["q"]), float(d["db"])) for d in value.get("dips", [])])

    def body_at(self, frequency: np.ndarray) -> np.ndarray:
        """The body's level (dB) at these frequencies: its bands' values, linear across log
        frequency between band centres, flat beyond the outer ones."""
        centres = np.sqrt(self.body_edges[:-1] * self.body_edges[1:])
        return np.interp(np.log(np.maximum(frequency, 1.0)), np.log(centres), self.body_db)


def loss_response(pole: float, zero: float, omega):
    """One stage of the loss filter, (1 - a) / (1 - b) (1 - b z^-1) / (1 - a z^-1), at omega."""
    z = np.exp(-1j * np.asarray(omega, dtype=np.float64))
    return (1.0 - pole) / (1.0 - zero) * (1.0 - zero * z) / (1.0 - pole * z)


def lowpass_delay(pole: float, omega: float, zero: float = 0.0) -> float:
    """One stage of the loss filter: its phase delay (samples) at omega."""
    if omega <= 0:
        return pole / (1.0 - pole) - zero / (1.0 - zero)
    response = complex(loss_response(pole, zero, omega))
    return -math.atan2(response.imag, response.real) / omega


def peaking(frequency: float, q: float, gain_db: float, rate: float) -> tuple[np.ndarray, np.ndarray]:
    """A peaking filter (RBJ's cookbook): (b, a), a[0] = 1."""
    amplitude = 10.0 ** (gain_db / 40.0)
    omega = 2.0 * math.pi * min(frequency, 0.45 * rate) / rate
    alpha = math.sin(omega) / (2.0 * max(q, 0.1))
    a0 = 1.0 + alpha / amplitude
    b = np.array([1.0 + alpha * amplitude, -2.0 * math.cos(omega), 1.0 - alpha * amplitude]) / a0
    a = np.array([1.0, -2.0 * math.cos(omega) / a0, (1.0 - alpha / amplitude) / a0])
    return b, a


def biquad_response(b: np.ndarray, a: np.ndarray, omega):
    z = np.exp(-1j * np.asarray(omega, dtype=np.float64))
    return (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)


def biquad_delay(b: np.ndarray, a: np.ndarray, omega: float) -> float:
    """A biquad's phase delay (samples) at omega (its DC delay as omega goes to 0)."""
    omega = max(omega, 1e-4)
    response = complex(biquad_response(b, a, omega))
    return -math.atan2(response.imag, response.real) / omega


def allpass_delay(c: float, omega: float) -> float:
    """A first-order allpass (c + z^-1) / (1 + c z^-1): its phase delay (samples) at omega."""
    if omega <= 0:
        return (1.0 - c) / (1.0 + c)
    z = complex(math.cos(-omega), math.sin(-omega))
    response = (c + z) / (1.0 + c * z)
    return -math.atan2(response.imag, response.real) / omega


@dataclass
class Tuning:
    length: int      # the delay line, samples
    eta: float       # the fraction's allpass coefficient
    gain: float
    pole: float
    order: int
    zero: float
    dips: list       # [(b, a)] the body's peaking cuts
    dispersion: float
    stages: int


def tune(frequency: float, rate: float, gain: float, pole: float, dispersion: float, stages: int, speed: float = 1.0,
         stiffness: float = 1.0, order: int = 1, zero: float = 0.0, dips: list | None = None) -> Tuning:
    """The loop for a note: its line's length and its fraction, so the whole loop (line, loss,
    stiffness, fraction) lasts one period at the fundamental. `speed` makes it decay that much
    faster, `stiffness` scales the dispersion."""
    frequency = max(LOWEST_HZ, min(frequency, 0.4 * rate))
    period = rate / frequency
    omega = 2.0 * math.pi * frequency / rate
    c = max(-0.95, min(0.0, dispersion * stiffness))
    stages = max(0, min(MAXIMUM_STAGES, stages)) if c < 0 else 0
    order = 2 if order >= 2 else 1
    # The body's cuts, per period as measured, `speed` times deeper.
    cuts = [peaking(hz, q, db * max(0.05, speed), rate) for hz, q, db in (dips or [])[:3] if hz < 0.45 * rate]
    fixed = order * lowpass_delay(pole, omega, zero) + stages * allpass_delay(c, omega) \
        + sum(biquad_delay(b, a, omega) for b, a in cuts)
    rest = period - fixed
    length = max(2, int(math.floor(rest - 0.1)))
    wanted = rest - length
    # The fraction's allpass: its DC delay d made to give `wanted` at the fundamental.
    d = wanted
    for _ in range(3):
        eta = (1.0 - d) / (1.0 + d)
        d += wanted - allpass_delay(eta, omega)
        d = min(max(d, 0.05), 1.95)
    eta = (1.0 - d) / (1.0 + d)
    return Tuning(length=length, eta=eta, gain=min(0.999999, gain ** max(0.05, speed)), pole=pole, order=order, zero=zero,
                  dips=cuts, dispersion=c, stages=stages)


def excitation(string: StringModel, frequency: float, rate: float, brightness: float, count: int) -> np.ndarray:
    """Each harmonic's sine amplitude at the take-over: the excitation, the body at its new
    frequency, tilted by `brightness` dB per octave of harmonic number; nothing above 0.45 rate."""
    k = np.arange(1, count + 1, dtype=np.float64)
    own = min(count, len(string.excitation_db))
    levels = np.full(count, -200.0)
    levels[:own] = string.excitation_db[:own]
    levels = levels + string.body_at(k * frequency) + brightness * np.log2(k)
    amplitudes = 10.0 ** (levels / 20.0)
    amplitudes[k * frequency >= 0.45 * rate] = 0.0
    amplitudes[levels <= -190.0] = 0.0
    return amplitudes


class PluckedString:
    """The loop, sample by sample (what C++'s PluckedString does)."""

    def __init__(self, tuning: Tuning, frequency: float, rate: float, amplitudes: np.ndarray, phases: np.ndarray):
        self.tuning = tuning
        n = np.arange(tuning.length, dtype=np.float64)
        k = np.arange(1, len(amplitudes) + 1, dtype=np.float64)
        # One period laid in the line: what comes out first.
        self.line = (amplitudes[None, :] * np.sin(phases[None, :] + 2.0 * math.pi * frequency * n[:, None] * k[None, :] / rate)).sum(axis=1)
        self.position = 0
        self.lowpass = 0.0
        self.lowpass2 = 0.0
        self.input1 = 0.0
        self.input2 = 0.0
        self.cut_state = [[0.0, 0.0, 0.0, 0.0] for _ in tuning.dips]
        self.stage_in = [0.0] * MAXIMUM_STAGES
        self.stage_out = [0.0] * MAXIMUM_STAGES
        self.fraction_in = 0.0
        self.fraction_out = 0.0

    def render(self, count: int) -> np.ndarray:
        t = self.tuning
        line = self.line
        length = len(line)
        out = np.empty(count)
        g = t.gain
        a = t.pole
        zero = t.zero
        scale = (1.0 - a) / (1.0 - zero)
        second = t.order >= 2
        cuts = t.dips
        cut_state = self.cut_state
        lowpass2 = self.lowpass2
        input1 = self.input1
        input2 = self.input2
        c = t.dispersion
        eta = t.eta
        stages = t.stages
        lowpass = self.lowpass
        stage_in = self.stage_in
        stage_out = self.stage_out
        fraction_in = self.fraction_in
        fraction_out = self.fraction_out
        position = self.position
        for index in range(count):
            x = line[position]
            out[index] = x
            lowpass = scale * (x - zero * input1) + a * lowpass
            input1 = x
            if second:
                lowpass2 = scale * (lowpass - zero * input2) + a * lowpass2
                input2 = lowpass
                v = g * lowpass2
            else:
                v = g * lowpass
            for (b_cut, a_cut), state in zip(cuts, cut_state):
                y = b_cut[0] * v + b_cut[1] * state[0] + b_cut[2] * state[1] - a_cut[1] * state[2] - a_cut[2] * state[3]
                state[1] = state[0]
                state[0] = v
                state[3] = state[2]
                state[2] = y
                v = y
            for stage in range(stages):
                y = c * v + stage_in[stage] - c * stage_out[stage]
                stage_in[stage] = v
                stage_out[stage] = y
                v = y
            y = eta * v + fraction_in - eta * fraction_out
            fraction_in = v
            fraction_out = y
            line[position] = y
            position += 1
            if position >= length:
                position = 0
        self.lowpass = lowpass
        self.lowpass2 = lowpass2
        self.input1 = input1
        self.input2 = input2
        self.fraction_in = fraction_in
        self.fraction_out = fraction_out
        self.position = position
        return out


# ---- Analysis: the string of a recorded note --------------------------------------------------

BODY_BANDS = 24


def fit_string(harmonics: np.ndarray, f0: float, frame_seconds: float, inharmonicity: float, start: int,
               rate: float = 48000.0) -> StringModel | None:
    """The string that rings like these partials (frames x K sine amplitudes) from frame `start`:
    each partial's decay (dB per second, a line through its levels until 40 dB down) as a loss per
    period, the loss filter (g, a) that fits them (a grid of a, g by least squares, weighted by
    level), the stiffness from B, the excitation and the body from the levels at `start`."""
    frames, count = harmonics.shape
    if count < 4 or frames - start < 12 or f0 <= 0:
        return None
    levels = 20.0 * np.log10(np.maximum(harmonics[start:], 1e-12))
    initial = levels[0]
    strongest = float(initial.max())
    decay = np.full(count, np.nan)
    weight = np.zeros(count)
    for column in range(count):
        if initial[column] < strongest - 50.0:
            continue
        curve = levels[:, column]
        audible = np.nonzero(curve > initial[column] - 40.0)[0]
        last = int(audible[-1]) if len(audible) else 0
        if last < 8:
            continue
        times = np.arange(last + 1) * frame_seconds
        slope = np.polyfit(times, curve[:last + 1], 1)[0]
        if slope >= 0:
            continue
        decay[column] = slope
        # By their energy: the strong ones are what is heard ringing.
        weight[column] = 10.0 ** ((initial[column] - strongest) / 10.0)
    usable = np.isfinite(decay) & (weight > 0)
    if usable.sum() < 3:
        return None
    k = np.arange(1, count + 1, dtype=np.float64)
    omega = 2.0 * math.pi * k * f0 / rate
    per_period = decay / f0  # dB per period
    # The loss at DC from the first three partials that ring (they last longest, and set how long
    # the note is heard); the pole from them all.
    low = np.nonzero(usable)[0][:3]
    best = None
    for order in (1, 2):
        for pole in np.linspace(0.0, 0.98, 50):
            for zero in np.linspace(-0.9, pole, 40):
                response = order * 20.0 * np.log10(np.abs(loss_response(pole, zero, omega)))
                residual = per_period - response
                log_gain = float(np.sum(weight[low] * residual[low]) / np.sum(weight[low]))
                log_gain = min(log_gain, -1e-6)
                error = float(np.sum(weight[usable] * (residual[usable] - log_gain) ** 2))
                if best is None or error < best[0]:
                    best = (error, pole, log_gain, order, zero)
    _, pole, log_gain, order, zero = best
    gain = 10.0 ** (log_gain / 20.0)

    # The body's losses: what the smooth loss misses, up to three peaking cuts fixed in Hz, each
    # kept when it takes a tenth of the weighted error off.
    smooth = order * 20.0 * np.log10(np.abs(loss_response(pole, zero, omega))) + log_gain
    residual = np.where(usable, per_period - smooth, 0.0)
    frequencies_hz = k * f0
    dips = []
    error_now = float(np.sum(weight[usable] * residual[usable] ** 2))
    for _ in range(3):
        best_dip = None
        for centre in frequencies_hz[usable]:
            for q in (0.7, 1.4, 2.8, 5.0):
                for depth in np.linspace(-2.0, -0.02, 50):
                    b_cut, a_cut = peaking(centre, q, depth, rate)
                    response = 20.0 * np.log10(np.abs(biquad_response(b_cut, a_cut, omega)))
                    error = float(np.sum(weight[usable] * (residual[usable] - response[usable]) ** 2))
                    if best_dip is None or error < best_dip[0]:
                        best_dip = (error, centre, q, depth, response)
        if best_dip is None or best_dip[0] > 0.9 * error_now:
            break
        error_now, centre, q, depth, response = best_dip
        residual = np.where(usable, residual - response, 0.0)
        dips.append((float(centre), float(q), float(depth)))

    # Stiffness: the allpasses whose stretch of the first partials matches k sqrt(1 + B k^2).
    dispersion, stages = 0.0, 0
    if inharmonicity > 2e-5:
        stages = 2
        target = k[:12] * np.sqrt(1.0 + inharmonicity * k[:12] ** 2)
        best_c = None
        for c in np.linspace(-0.9, -0.01, 90):
            ratio = _resonances(f0, rate, pole, c, stages, 12, order, zero, dips) / f0
            error = float(np.sum((ratio - target) ** 2 / target ** 2))
            if best_c is None or error < best_c[0]:
                best_c = (error, c)
        dispersion = float(best_c[1])

    # The excitation, the body taken out: the body is the levels' smooth outline across frequency
    # (bands of log frequency), what is left (the pluck's own comb) moves with the note.
    frequencies = k * f0 * np.sqrt(1.0 + inharmonicity * k * k)
    edges = np.geomspace(40.0, 0.45 * rate, BODY_BANDS + 1)
    body = np.full(BODY_BANDS, np.nan)
    audible = initial > strongest - 60.0
    for band in range(BODY_BANDS):
        inside = audible & (frequencies >= edges[band]) & (frequencies < edges[band + 1])
        if np.any(inside):
            body[band] = float(np.max(initial[inside]))
    known = np.isfinite(body)
    if known.sum() < 2:
        return None
    centres = np.sqrt(edges[:-1] * edges[1:])
    body = np.interp(np.log(centres), np.log(centres[known]), body[known])
    # Smoothed (a third either side), relative to its loudest.
    body = np.convolve(np.pad(body, 1, mode="edge"), np.ones(3) / 3.0, mode="valid")
    body -= body.max()
    model = StringModel(gain=gain, pole=float(pole), order=int(order), zero=float(zero), dips=dips, dispersion=dispersion,
                        stages=stages, start=int(start),
                        excitation_db=np.zeros(count), body_edges=edges, body_db=body)
    model.excitation_db = np.where(initial > strongest - 90.0, initial - model.body_at(frequencies), -200.0)
    return model


def _resonances(f0: float, rate: float, pole: float, c: float, stages: int, count: int, order: int = 1,
                zero: float = 0.0, dips: list | None = None) -> np.ndarray:
    """The loop's first resonances (Hz), tuned at f0: where its total phase is a whole number of
    turns."""
    tuning = tune(f0, rate, 1.0, pole, c, stages, order=order, zero=zero, dips=dips)

    def loop_phase(frequency: float) -> float:
        omega = 2.0 * math.pi * frequency / rate
        delay = tuning.length + tuning.order * lowpass_delay(pole, omega, zero) \
            + sum(biquad_delay(b, a, omega) for b, a in tuning.dips) + tuning.stages * allpass_delay(tuning.dispersion, omega) \
            + allpass_delay(tuning.eta, omega)
        return omega * delay

    found = []
    for k in range(1, count + 1):
        frequency = k * f0
        for _ in range(8):
            phase = loop_phase(frequency)
            step = 1.0
            slope = (loop_phase(frequency + step) - phase) / step
            if slope <= 0:
                break
            frequency += (2.0 * math.pi * k - phase) / slope
        found.append(frequency)
    return np.asarray(found)
