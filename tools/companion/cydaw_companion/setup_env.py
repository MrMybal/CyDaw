"""The companion's own Python environment, made from CyDAW (Options › Compagnon Python).

Standard library only: it runs with any Python 3.10+, before numpy exists.

    setup --env FOLDER [--torch none|cpu|cuda] [--clap]

1. a virtual environment in FOLDER (venv, with pip);
2. numpy in it (the analysis needs nothing else);
3. PyTorch for the refinement, when asked: the CPU build, or a CUDA build for
   an NVIDIA card (a few CUDA versions are tried, newest first);
4. with --clap, what the semantic sample search runs the model with:
   transformers (Apache-2.0), the version it was tried with;
5. what it now has (the `info` command, run with its Python).

Every line pip prints becomes a progress message; nothing is downloaded but
from PyPI and download.pytorch.org. The model itself is not: CyDAW downloads
it apart, once its licence is shown.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import venv
from pathlib import Path

CUDA_INDEXES = ("cu130", "cu128", "cu126", "cu124")
# What the semantic search was tried with (CLAP through transformers).
CLAP_PACKAGES = ("transformers==4.57.6",)


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


def setup(folder: Path, torch: str, clap: bool = False) -> int:
    if sys.version_info < (3, 10):
        emit(error=f"Python {sys.version.split()[0]} is too old: 3.10 or newer is needed")
        return 1
    python = environment_python(folder)
    if not python.exists():
        emit(progress=0.05, message=f"create the environment in {folder}")
        venv.EnvBuilder(with_pip=True, clear=False, upgrade_deps=False).create(folder)
    if not python.exists():
        emit(error=f"no Python in the new environment ({python})")
        return 1
    if run([str(python), "-m", "pip", "install", "--upgrade", "pip", "numpy"], 0.2, "numpy") != 0:
        emit(error="pip could not install numpy")
        return 1
    if torch == "cpu":
        if run([str(python), "-m", "pip", "install", "torch", "--index-url", "https://download.pytorch.org/whl/cpu"],
               0.5, "PyTorch (CPU)") != 0:
            emit(error="pip could not install PyTorch (CPU)")
            return 1
    elif torch == "cuda":
        for index in CUDA_INDEXES:
            if run([str(python), "-m", "pip", "install", "--upgrade", "torch", "--index-url",
                    f"https://download.pytorch.org/whl/{index}"], 0.5, f"PyTorch (CUDA, {index})") == 0:
                break
        else:
            emit(error="pip could not install a CUDA build of PyTorch")
            return 1
    if clap:
        if run([str(python), "-m", "pip", "install", *CLAP_PACKAGES], 0.85, "transformers (semantic search)") != 0:
            emit(error="pip could not install transformers")
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
