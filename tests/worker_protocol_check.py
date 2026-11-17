"""Validate a worker handshake through redirected pipes, including GUI executables.

PowerShell can return before a Windows-subsystem executable finishes. A native
subprocess with owned stdout/stderr pipes waits and captures the actual reply.
No audio device, plugin or personal settings are opened by --self-test.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', type=Path, required=True)
    args = parser.parse_args()
    header = Path(__file__).resolve().parents[1] / 'include/cydaw/plugins/PluginIsolation.h'
    match = re.search(r'workerProtocolVersion\s*=\s*(\d+)', header.read_text('utf-8'))
    if not match:
        raise RuntimeError('Worker protocol declaration is missing')
    expected = int(match.group(1))
    result = subprocess.run([str(args.worker.resolve()), '--self-test'],
                            capture_output=True, text=True, encoding='utf-8', timeout=30, check=True)
    report = json.loads(result.stdout)
    if (report.get('protocol') != expected or report.get('status') != 'ready'
            or report.get('isolation') != 'per-instance'):
        raise RuntimeError(f'Incompatible worker handshake: expected {expected}, received {report}')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
