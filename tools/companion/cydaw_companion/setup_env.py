"""The companion's own Python environment, made from CyDAW (Options › Compagnon Python).

Standard library only: it runs with any Python 3.10+, before numpy exists.

    setup --env FOLDER [--torch none|cpu|cuda] [--clap] [--crepe]

1. a virtual environment in FOLDER (venv, with pip);
2. numpy in it (the analysis needs nothing else);
3. PyTorch for the refinement, when asked: the CPU build, or a CUDA build for
   an NVIDIA card (a few CUDA versions are tried, newest first);
4. with --clap, what the semantic sample search runs the model with:
   transformers (Apache-2.0), the version it was tried with;
5. with --crepe, CREPE for the pitch of long recordings (torchcrepe, MIT, its
   model inside): torchaudio first from the index PyTorch came from, at
   PyTorch's own version (else pip would bring a CPU build from PyPI, or
   another PyTorch altogether), then torchcrepe and what it needs from PyPI;
6. what it now has (the `info` command, run with its Python).

Every line pip prints becomes a progress message; nothing is downloaded but
from PyPI and download.pytorch.org. The model itself is not: CyDAW downloads
it apart, once its licence is shown. What each component brings and under
which licences: components.json, beside this file (CyDAW shows it before
installing).

Nothing is written outside FOLDER (CyDAW's folder for the companion, Options ›
Emplacements): pip keeps no cache, its temporary files go to FOLDER/tmp
(emptied at the end), and the environment, once made, keeps the caches of
PyTorch, Hugging Face, numba and librosa in FOLDER/cache (`contain`, called by
the companion whenever it runs from it).
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import venv
from pathlib import Path

CUDA_INDEXES = ("cu130", "cu128", "cu126", "cu124")
# What the semantic search was tried with (CLAP through transformers).
CLAP_PACKAGES = ("transformers==4.57.6",)
# Written in an environment CyDAW made: the companion, run from it, keeps its
# caches and temporary files in it.
MARKER = "cydaw-environment.json"
# Every pip call: no cache (a few gigabytes otherwise, on the system drive), no
# version check (a request and a file of its own).
PIP = ("-m", "pip", "--no-cache-dir", "--disable-pip-version-check")


def contain(folder: Path) -> None:
    """This process and what it starts keep their caches and temporary files in `folder`."""
    cache = folder / "cache"
    temporary = folder / "tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    places = {
        "TMP": temporary, "TEMP": temporary, "TMPDIR": temporary,
        "PIP_CACHE_DIR": cache / "pip", "XDG_CACHE_HOME": cache, "TORCH_HOME": cache / "torch",
        "HF_HOME": cache / "huggingface", "NUMBA_CACHE_DIR": cache / "numba", "LIBROSA_DATA_DIR": cache / "librosa",
        "MPLCONFIGDIR": cache / "matplotlib",
    }
    for name, value in places.items():
        os.environ[name] = str(value)
    tempfile.tempdir = str(temporary)


def emit(**value) -> None:
    print(json.dumps(value, ensure_ascii=False), flush=True)


def environment_python(folder: Path) -> Path:
    windows = folder / "Scripts" / "python.exe"
    return windows if windows.exists() or os.name == "nt" else folder / "bin" / "python"


def run(command: list[str], fraction: float, label: str) -> int:
    emit(progress=fraction, message=label)
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                               errors="replace", creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    assert process.stdout is not None
    for line in process.stdout:
        line = line.strip()
        if line:
            emit(progress=fraction, message=line[:200])
    return process.wait()


def installed_torch(python: Path) -> str | None:
    """The PyTorch in the environment (its version, "2.13.0+cu130"), or None."""
    result = subprocess.run([str(python), "-c", "import torch; print(torch.__version__)"], capture_output=True, text=True,
                            encoding="utf-8", errors="replace", creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    version = result.stdout.strip().splitlines()[-1] if result.returncode == 0 and result.stdout.strip() else ""
    return version or None


def setup(folder: Path, torch: str, clap: bool = False, crepe: bool = False) -> int:
    try:
        return _setup(folder, torch, clap, crepe)
    finally:
        shutil.rmtree(folder / "tmp", ignore_errors=True)


def _setup(folder: Path, torch: str, clap: bool, crepe: bool) -> int:
    if sys.version_info < (3, 10):
        emit(error=f"Python {sys.version.split()[0]} is too old: 3.10 or newer is needed")
        return 1
    folder.mkdir(parents=True, exist_ok=True)
    contain(folder)
    python = environment_python(folder)
    if not python.exists():
        emit(progress=0.05, message=f"create the environment in {folder}")
        venv.EnvBuilder(with_pip=True, clear=False, upgrade_deps=False).create(folder)
    if not python.exists():
        emit(error=f"no Python in the new environment ({python})")
        return 1
    (folder / MARKER).write_text(json.dumps({"format": "cydaw-companion-environment", "version": 1}), encoding="utf-8")
    if run([str(python), *PIP, "install", "--upgrade", "pip", "numpy"], 0.2, "numpy") != 0:
        emit(error="pip could not install numpy")
        return 1
    if torch == "cpu":
        if run([str(python), *PIP, "install", "torch", "--index-url", "https://download.pytorch.org/whl/cpu"],
               0.5, "PyTorch (CPU)") != 0:
            emit(error="pip could not install PyTorch (CPU)")
            return 1
    elif torch == "cuda":
        # A CPU build there first goes (pip would keep it: the same version).
        if (version := installed_torch(python)) is not None and "+cu" not in version:
            run([str(python), *PIP, "uninstall", "--yes", "torch", "torchaudio"], 0.3, f"PyTorch {version} (CPU) removed")
        for index in CUDA_INDEXES:
            if run([str(python), *PIP, "install", "--upgrade", "torch", "--index-url",
                    f"https://download.pytorch.org/whl/{index}"], 0.5, f"PyTorch (CUDA, {index})") == 0:
                break
        else:
            emit(error="pip could not install a CUDA build of PyTorch")
            return 1
    if clap:
        if run([str(python), *PIP, "install", *CLAP_PACKAGES], 0.85, "transformers (semantic search)") != 0:
            emit(error="pip could not install transformers")
            return 1
    if crepe:
        version = installed_torch(python)
        if version is None:
            emit(error="CREPE runs on PyTorch: install PyTorch (CPU or CUDA) first")
            return 1
        # The index of its build: "+cu130" -> cu130; "+cpu" or none -> cpu.
        local = version.split("+", 1)[1] if "+" in version else "cpu"
        index = local if local.startswith("cu") else "cpu"
        if run([str(python), *PIP, "install", "torchaudio", f"torch=={version}", "--index-url",
                f"https://download.pytorch.org/whl/{index}"], 0.88, f"torchaudio ({index})") != 0:
            emit(error="pip could not install torchaudio for this PyTorch")
            return 1
        if run([str(python), *PIP, "install", "torchcrepe", f"torch=={version}"], 0.92, "torchcrepe (CREPE)") != 0:
            emit(error="pip could not install torchcrepe")
            return 1
    emit(progress=0.95, message="check")
    package = Path(__file__).resolve().parents[1]
    result = subprocess.run([str(python), "-X", "utf8", "-c",
                             f"import sys; sys.path.insert(0, r'{package.as_posix()}'); "
                             "from cydaw_companion.__main__ import main; sys.exit(main(['info']))"],
                            capture_output=True, text=True, encoding="utf-8", errors="replace",
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    info = {}
    for line in result.stdout.splitlines():
        try:
            info = json.loads(line)
        except json.JSONDecodeError:
            continue
    emit(done=True, python=str(python), info=info)
    return 0
