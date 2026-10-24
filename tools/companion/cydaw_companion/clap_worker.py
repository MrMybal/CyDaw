"""The semantic sample search's model: CLAP, text and sounds into the same space.

    clap-check [--model DIR]                 what this Python has for it (torch,
                                             transformers, the GPU), the model
                                             loaded once when given
    clap-worker --model DIR [--device auto|cpu|cuda] [--idle SECONDS]
                                             the worker CyDAW talks to

The worker listens on 127.0.0.1 only, on a port of its own, and prints
{"port": N} once ready; CyDAW connects, and the worker serves that one
connection: one JSON request per line, one JSON answer per line. It ends when
the connection closes, when nothing comes for --idle seconds, or on
{"op": "shutdown"}. Nothing is ever downloaded: the model is the folder CyDAW
installed (HF_HUB_OFFLINE), and the audio comes decoded by CyDAW.

Requests:
    {"op": "info"}
        -> {"ok": true, "model": DIR, "device": "cuda", "dimension": 512, "rate": 48000}
    {"op": "embed_text", "texts": ["a kick drum", ...]}
        -> {"ok": true, "vectors": [[...], ...]}          (unit length)
    {"op": "embed_audio", "file": F, "items": [[offset, count], ...], "out": O}
        F: float32 mono samples at the model's rate, the items one after the
        other; O receives one unit-length float32 vector per item
        -> {"ok": true, "count": N, "dimension": 512}
    {"op": "shutdown"}
"""
from __future__ import annotations

import json
import os
import socket
import sys
import time
from pathlib import Path

os.environ.setdefault("HF_HUB_OFFLINE", "1")
os.environ.setdefault("TRANSFORMERS_OFFLINE", "1")
os.environ.setdefault("HF_HUB_DISABLE_TELEMETRY", "1")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

# A window this short is padded to it: the model wants a little sound.
MIN_SECONDS = 0.5
# The fast features (torch, on the model's device) are used only when they
# give the same vectors as transformers' own, to this likeness.
FAST_AGREEMENT = 0.995


def emit(**value) -> None:
    print(json.dumps(value, ensure_ascii=False), flush=True)


class Clap:
    """LAION-CLAP through transformers' ClapModel, from a local folder."""

    def __init__(self, folder: Path, device: str = "auto") -> None:
        import torch  # noqa: PLC0415
        from transformers import ClapModel, ClapProcessor  # noqa: PLC0415
        self.torch = torch
        if device == "auto":
            device = "cuda" if torch.cuda.is_available() else "cpu"
        self.device = device
        self.folder = folder
        self.processor = ClapProcessor.from_pretrained(str(folder), local_files_only=True)
        self.model = ClapModel.from_pretrained(str(folder), local_files_only=True).to(device).eval()
        extractor = getattr(self.processor, "feature_extractor", None)
        self.rate = int(getattr(extractor, "sampling_rate", 48000) or 48000)
        self.dimension = int(getattr(self.model.config, "projection_dim", 512) or 512)
        self.fast = False
        self.agreement = None
        try:
            self._prepare_fast(extractor)
        except Exception:  # noqa: BLE001 - transformers' own features then
            self.fast = False

    # ---- The log-mel features, computed by torch for a whole batch at once.
    # transformers' ClapFeatureExtractor does it with numpy, one sound after the
    # other (a few hundredths of a second each): the same steps here — repeat
    # then pad to the window ("repeatpad"), a centred STFT with a periodic Hann
    # window, power, its slaney mel filters, 10·log10 — on the model's device.

    def _prepare_fast(self, extractor) -> None:
        import numpy as np  # noqa: PLC0415
        torch = self.torch
        if extractor is None or getattr(extractor, "truncation", "") != "rand_trunc" \
                or getattr(extractor, "padding", "") != "repeatpad":
            return
        self.max_samples = int(extractor.nb_max_samples)
        self.n_fft = int(extractor.fft_window_size)
        self.hop = int(extractor.hop_length)
        self.mel = torch.tensor(np.asarray(extractor.mel_filters_slaney, dtype=np.float32), device=self.device)
        self.window = torch.hann_window(self.n_fft, periodic=True, dtype=torch.float32, device=self.device)
        # Checked against transformers' own on sounds of every kind of length.
        rng = np.random.default_rng(7)
        t = np.arange(int(self.rate * 2.0)) / self.rate
        probes = [
            (np.sin(2 * np.pi * 220 * t) * np.exp(-t * 3)).astype(np.float32),
            (rng.standard_normal(int(self.rate * 0.3)) * 0.2).astype(np.float32),
            (np.sin(2 * np.pi * 60 * t[: int(self.rate * 0.8)]) * 0.8).astype(np.float32),
            (rng.standard_normal(self.max_samples) * 0.05).astype(np.float32),
        ]
        reference = self._embed_features(self._features_slow(probes))
        fast = self._embed_features(self._features_fast(probes))
        self.agreement = float(np.min(np.sum(reference * fast, axis=1)))
        self.fast = self.agreement >= FAST_AGREEMENT

    def _features_slow(self, waves: list) -> dict:
        try:
            inputs = self.processor(audio=waves, sampling_rate=self.rate, return_tensors="pt")
        except TypeError:
            # Older transformers name it `audios`.
            inputs = self.processor(audios=waves, sampling_rate=self.rate, return_tensors="pt")
        return {k: v.to(self.device) for k, v in inputs.items()}

    def _features_fast(self, waves: list) -> dict:
        import numpy as np  # noqa: PLC0415
        torch = self.torch
        batch = []
        for wave in waves:
            wave = np.asarray(wave, dtype=np.float32)[: self.max_samples]
            if len(wave) < self.max_samples:
                wave = np.tile(wave, int(self.max_samples / len(wave)))
                wave = np.pad(wave, (0, self.max_samples - len(wave)))
            batch.append(wave)
        x = torch.from_numpy(np.stack(batch)).to(self.device)
        spectrum = torch.stft(x, self.n_fft, hop_length=self.hop, window=self.window, center=True, pad_mode="reflect",
                              return_complex=True)
        power = spectrum.real ** 2 + spectrum.imag ** 2
        mel = torch.clamp(torch.matmul(self.mel.T, power), min=1e-10)
        features = (10.0 * torch.log10(mel)).transpose(1, 2).unsqueeze(1)
        longer = torch.zeros((len(batch), 1), dtype=torch.bool, device=self.device)
        return {"input_features": features, "is_longer": longer}

    def _embed_features(self, inputs: dict):
        with self.torch.inference_mode():
            features = self._tensor(self.model.get_audio_features(**inputs))
        return self._unit(features)

    @staticmethod
    def _tensor(output):
        # transformers 4.x gives the projected tensor, 5.x an output holding it.
        return output if hasattr(output, "shape") else output.pooler_output

    def _unit(self, features):
        import numpy as np  # noqa: PLC0415
        vectors = features.float().cpu().numpy().astype(np.float32)
        norms = np.linalg.norm(vectors, axis=1, keepdims=True)
        return vectors / np.maximum(norms, 1e-12)

    def embed_text(self, texts: list[str]):
        inputs = self.processor(text=list(texts), return_tensors="pt", padding=True, truncation=True)
        with self.torch.inference_mode():
            features = self._tensor(self.model.get_text_features(**{k: v.to(self.device) for k, v in inputs.items()}))
        return self._unit(features)

    def embed_audio(self, waves: list) -> object:
        import numpy as np  # noqa: PLC0415
        shortest = int(MIN_SECONDS * self.rate)
        waves = [w if len(w) >= shortest else np.pad(w, (0, shortest - len(w))) for w in waves]
        batch = (64 if self.fast else 16) if self.device == "cuda" else (16 if self.fast else 4)
        out = []
        for start in range(0, len(waves), batch):
            part = waves[start:start + batch]
            inputs = self._features_fast(part) if self.fast else self._features_slow(part)
            out.append(self._embed_features(inputs))
        return np.concatenate(out, axis=0) if out else np.zeros((0, self.dimension), dtype=np.float32)


def handle(clap: Clap, request: dict) -> dict:
    import numpy as np  # noqa: PLC0415
    op = request.get("op")
    if op == "info":
        return {"ok": True, "model": str(clap.folder), "device": clap.device, "dimension": clap.dimension,
                "rate": clap.rate, "fastFeatures": clap.fast, "agreement": clap.agreement}
    if op == "embed_text":
        texts = [str(t) for t in request.get("texts", [])]
        if not texts:
            return {"ok": True, "vectors": []}
        vectors = clap.embed_text(texts)
        return {"ok": True, "vectors": [[round(float(x), 6) for x in row] for row in vectors]}
    if op == "embed_audio":
        samples = np.fromfile(request["file"], dtype="<f4")
        waves = []
        for offset, count in request.get("items", []):
            offset, count = int(offset), int(count)
            if offset < 0 or count <= 0 or offset + count > len(samples):
                return {"ok": False, "error": f"item {offset}+{count} is outside the file ({len(samples)} samples)"}
            waves.append(samples[offset:offset + count])
        vectors = clap.embed_audio(waves)
        vectors.astype("<f4").tofile(request["out"])
        return {"ok": True, "count": int(vectors.shape[0]), "dimension": int(vectors.shape[1]) if vectors.ndim == 2 else 0}
    return {"ok": False, "error": f"unknown op {op!r}"}


def serve(folder: Path, device: str, idle: float, token: str = "") -> int:
    """token: what CyDAW put in every request (given on the command line); a
    connection that brings another (some other local process, first on the
    port) is closed unanswered: it could make the worker read and write files."""
    started = time.time()
    clap = Clap(folder, device)
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # 127.0.0.1 only: nothing outside this machine can reach it.
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    server.settimeout(60.0)
    emit(port=server.getsockname()[1], device=clap.device, dimension=clap.dimension, rate=clap.rate,
         fastFeatures=clap.fast, agreement=clap.agreement, seconds=round(time.time() - started, 2))
    try:
        connection, _ = server.accept()
    except socket.timeout:
        return 0
    finally:
        server.close()
    connection.settimeout(idle if idle > 0 else None)
    pending = b""
    with connection:
        while True:
            try:
                chunk = connection.recv(1 << 16)
            except socket.timeout:
                return 0
            if not chunk:
                return 0
            pending += chunk
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                if not line.strip():
                    continue
                try:
                    request = json.loads(line.decode("utf-8"))
                except json.JSONDecodeError as error:
                    answer = {"ok": False, "error": f"bad request: {error}"}
                    request = {}
                else:
                    if token and request.get("token") != token:
                        return 1
                    if request.get("op") == "shutdown":
                        connection.sendall(b'{"ok": true}\n')
                        return 0
                    try:
                        answer = handle(clap, request)
                    except Exception as error:  # noqa: BLE001 - told to CyDAW, which shows it
                        answer = {"ok": False, "error": f"{type(error).__name__}: {error}"}
                if "id" in request:
                    answer["id"] = request["id"]
                connection.sendall((json.dumps(answer) + "\n").encode("utf-8"))


def check(folder: Path | None) -> int:
    value = {"done": True, "python": sys.version.split()[0], "executable": sys.executable, "numpy": None,
             "torch": None, "transformers": None, "cuda": False, "gpu": None}
    try:
        import numpy  # noqa: PLC0415
        value["numpy"] = numpy.__version__
    except Exception:  # noqa: BLE001 - reported as missing
        pass
    try:
        import torch  # noqa: PLC0415
        value["torch"] = torch.__version__
        value["cuda"] = bool(torch.cuda.is_available())
        if value["cuda"]:
            value["gpu"] = torch.cuda.get_device_name(0)
    except Exception:  # noqa: BLE001 - reported as missing
        pass
    try:
        import transformers  # noqa: PLC0415
        value["transformers"] = transformers.__version__
    except Exception:  # noqa: BLE001 - reported as missing
        pass
    if folder is not None and value["torch"] and value["transformers"]:
        started = time.time()
        clap = Clap(folder, "auto")
        vector = clap.embed_text(["a kick drum"])
        value["model"] = {"device": clap.device, "dimension": int(vector.shape[1]), "fastFeatures": clap.fast,
                          "agreement": clap.agreement, "seconds": round(time.time() - started, 2)}
    emit(**value)
    return 0
