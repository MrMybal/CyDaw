#!/usr/bin/env python3
"""Read-only source inventory; optional JSON output, no build/network/device access.
Counts are lexical references, not reachable-code or completeness claims.
"""
import argparse
import collections
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def git(path, *args):
    return subprocess.check_output(['git', '-c', f'safe.directory={path.as_posix()}', '-C', str(path), *args], text=True).strip()

def inventory(source, audio_source):
    files = []
    counts = collections.Counter()
    for folder in ('src', 'include'):
        for path in sorted((ROOT / folder).rglob('*')):
            if path.suffix not in ('.cpp', '.h', '.mm', '.c', '.cmake'):
                continue
            text = path.read_text(encoding='utf-8-sig')
            refs = []
            for i, line in enumerate(text.splitlines(), 1):
                names = re.findall(r'\bjuce::([A-Za-z_]\w*)|#\s*include\s*[<"](juce_[^>"/]+)', line)
                symbols = [a or b for a, b in names]
                counts.update(symbols)
                if symbols:
                    refs.append({'line': i, 'symbols': symbols})
            if refs:
                files.append({'path': path.relative_to(ROOT).as_posix(), 'references': refs})
    def dependency(name, source, role):
        digest = hashlib.sha256()
        inputs = [source / 'CMakeLists.txt', source / 'LICENSE']
        for folder in ('include', 'src', 'cmake', 'third_party'):
            inputs.extend(p for p in (source / folder).rglob('*') if p.is_file())
        inputs = sorted(set(p for p in inputs if p.is_file()))
        for path in inputs:
            relative = path.relative_to(source).as_posix()
            digest.update(relative.encode() + b'\0' + hashlib.sha256(path.read_bytes()).digest())
        status = git(source, 'status', '--porcelain')
        return {'name': name, 'role': role, 'base_commit': git(source, 'rev-parse', 'HEAD'),
                'working_tree_dirty': bool(status), 'working_tree_entries': len(status.splitlines()),
                'source_tree_sha256': digest.hexdigest(), 'source_tree_file_count': len(inputs),
                'hash_scope': ['CMakeLists.txt', 'LICENSE', 'include/**', 'src/**', 'cmake/**', 'third_party/**']}
    return {'schema': 2, 'application_base_commit': git(ROOT, 'rev-parse', 'HEAD'),
            'branch': git(ROOT, 'branch', '--show-current'),
            'architecture': 'CyDaw -> CyAudioCore -> CyDSP (numerical processing)',
            'dependencies': [dependency('CyAudioCore', audio_source, 'application audio services'),
                             dependency('CyDSP', source, 'numerical dependency managed by CyAudioCore')],
            'remaining_juce': {'method': 'lexical scan; includes legacy targets and comments',
                'files': len(files), 'references': sum(counts.values()),
                'symbols': dict(counts.most_common()), 'locations': files}}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cydsp-source', type=Path, default=ROOT.parent / 'CyDSP')
    parser.add_argument('--cyaudiocore-source', type=Path, default=ROOT.parent / 'CyAudioCore')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--summary', action='store_true', help='Omit per-line locations from the JSON snapshot')
    args = parser.parse_args()
    report = inventory(args.cydsp_source.resolve(), args.cyaudiocore_source.resolve())
    if args.summary:
        report['remaining_juce'].pop('locations', None)
    result = json.dumps(report, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result, encoding='utf-8')
    print(json.dumps({'remaining_juce_files': report['remaining_juce']['files'],
        'remaining_juce_references': report['remaining_juce']['references'],
        'dependencies': report['dependencies']}, ensure_ascii=False, indent=2))
if __name__ == '__main__':
    main()
