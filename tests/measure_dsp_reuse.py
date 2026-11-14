"""Run fresh offline DSP harness processes in alternating order; never opens a device.

Build tests/DspReuseTests.cpp for each preserved source/SDK revision first.
Example: python tests/measure_dsp_reuse.py --variant before=before.exe \
         --variant after=build/Release/cydaw_dsp_reuse_tests.exe --output output/dsp-pair.json
No compilation runs during measurement. The harness excludes its own warmup.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import platform
import statistics
import subprocess
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", action="append", required=True, metavar="NAME=EXECUTABLE")
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.rounds < 3:
        parser.error("Use at least three independent processes per variant")
    variants = {}
    for value in args.variant:
        label, sep, path = value.partition("=")
        if not label or not sep or label in variants:
            parser.error("Each variant needs a unique NAME=EXECUTABLE")
        variants[label] = Path(path).resolve(strict=True)
    hashes = {label: digest(path) for label, path in variants.items()}
    rows = []
    for iteration in range(args.rounds):
        labels = list(variants)
        if iteration % 2:
            labels.reverse()
        for label in labels:
            command = [str(variants[label]), "--bench"]
            result = subprocess.run(command, capture_output=True, text=True, check=True, timeout=120)
            measurements = []
            for line in result.stdout.splitlines():
                kernel, frames, microseconds = line.split(",")
                measurements.append(dict(kernel=kernel, frames=int(frames), microseconds=float(microseconds)))
            rows.append(dict(round=iteration + 1, variant=label, command=command,
                             stdout=result.stdout, stderr=result.stderr, measurements=measurements))
            print(iteration + 1, label, measurements, flush=True)
    assert all(digest(path) == hashes[label] for label, path in variants.items()), "A binary changed during measurement"
    summary = {}
    for label in variants:
        keys = {(m["kernel"], m["frames"]) for row in rows if row["variant"] == label for m in row["measurements"]}
        summary[label] = {}
        for kernel, frames in sorted(keys):
            values = [m["microseconds"] for row in rows if row["variant"] == label
                      for m in row["measurements"] if (m["kernel"], m["frames"]) == (kernel, frames)]
            summary[label][f"{kernel}/{frames}"] = dict(medianUs=statistics.median(values),
                                                       minUs=min(values), maxUs=max(values), rawUs=values)
    report = dict(protocol="offline, deterministic input, 48 kHz, fresh processes, alternating order, excluded warmup; elapsed processing time, not hardware CPU/xruns",
                  platform=platform.platform(), processor=platform.processor(), hashes=hashes, runs=rows, summary=summary)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
