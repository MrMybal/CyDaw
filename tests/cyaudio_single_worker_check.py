"""Functional Windows acceptance for the parent -> SDK-worker architecture.

Runs synthetic audio only, with silent outputs, no audio/MIDI inputs and an
isolated profile. --mode asio requires the output-only authorization described
in the campaign report. This is not a performance benchmark.
"""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
import time

from cyaudio_comparison import ROOT, K, Entry, assets, write_json, process_sample
from cyaudio_cpu_diagnostic import CpuSession, descendants, image


def topology(parent: int) -> list[dict]:
    snapshot = K.CreateToolhelp32Snapshot(2, 0)
    parents = {}
    try:
        entry = Entry()
        entry.size = C.sizeof(entry)
        more = K.Process32FirstW(snapshot, C.byref(entry))
        while more:
            parents[entry.pid] = entry.parent
            more = K.Process32NextW(snapshot, C.byref(entry))
    finally:
        K.CloseHandle(snapshot)
    return [dict(pid=pid, parent=parents.get(pid), image=image(pid))
            for pid in sorted(descendants(parent)) if pid != parent]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--vst3', type=Path, required=True)
    parser.add_argument('--clap', type=Path, required=True)
    parser.add_argument('--lv2', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=['null', 'asio'], default='null')
    parser.add_argument('--port', type=int, default=48249)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / 'output')
    assert not (out / 'acceptance.json').exists(), 'Keep previous evidence intact'
    out.mkdir(parents=True, exist_ok=True)
    exe = args.exe.resolve()
    files = [exe, exe.parent / 'CyDAWPluginWorker.exe']
    for candidate in (args.vst3, args.clap, args.lv2):
        candidate = candidate.resolve()
        files.extend(sorted(candidate.rglob('*')) if candidate.is_dir() else [candidate])
    files = [p for p in files if p.is_file()]
    hashes = [dict(path=str(p), sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in files]
    revision = subprocess.check_output(['git', '-c', 'safe.directory=' + str(ROOT), 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    result = dict(baseRevision=revision, kind='functional, no accepted CPU timing', mode=args.mode,
                  files=hashes, cases={})
    sound = assets(out / 'assets') / 'long.wav'
    jsfx = out / 'sparse-gain.jsfx'
    jsfx.write_text('desc:Single SDK sparse gain\nslider7:0.5<0,1,.01>Gain\n'
                    '@sample\nspl0*=slider7;spl1*=slider7;\n', encoding='utf-8')
    session = CpuSession(exe, out / 'session', args.port, args.mode)
    result['audio'] = session.audio
    assert all(not p['enabled'] for p in session.q(cmd='midi_inputs')['devices'])

    def check_tree(count: int) -> list[dict]:
        try:
            session.wait(lambda: len(topology(session.process.pid)) == count)
        except BaseException:
            result['treeFailure'] = dict(expected=count, actual=topology(session.process.pid), status=session.q(cmd='status'))
            raise
        tree = topology(session.process.pid)
        assert all(p['parent'] == session.process.pid and p['image'] == 'CyDAWPluginWorker.exe' for p in tree), tree
        return tree

    def play_case(name: str, count: int) -> dict:
        tree = check_tree(count)
        first = session.q(cmd='status')
        plugins = [p for t in first['audioTracks'] for p in t['plugins']]
        assert len(plugins) == count and all(p['running'] and p['latencySamples'] == 1024 for p in plugins)
        session.q(cmd='transport', action='play', position=.1)
        samples = []
        for _ in range(12):
            time.sleep(.2)
            samples.append(session.q(cmd='status'))
        assert samples[-1]['position'] - samples[0]['position'] > 1.5
        assert max(t['peakDb'] for s in samples for t in s['meters']) > -80
        assert all(p['running'] for s in samples for t in s['audioTracks'] for p in t['plugins'])
        memory = process_sample(session.process.pid)
        value = dict(tree=tree, first=first, samples=samples,
                     privateMiB=sum(p['private'] for p in memory.values()) / 1048576,
                     missedBlocks=sum(p['missedBlocks'] for t in samples[-1]['audioTracks'] for p in t['plugins']))
        result['cases'][name] = value
        session.q(cmd='transport', action='stop')
        return value

    try:
        session.reset()
        for _ in range(8):
            index = session.q(cmd='add_audio_track', file=str(sound))['index']
            session.q(cmd='add_plugin', track=index, file=str(args.vst3.resolve()))
        result['initialEight'] = dict(tree=topology(session.process.pid), status=session.q(cmd='status'))
        print(json.dumps(result['initialEight']), flush=True)
        play_case('eightVst3', 8)
        session.q(cmd='set_plugin_parameter', track=0, plugin=0, parameter='Gain', value=.37)
        project = out / 'eight.cydaw'
        session.q(cmd='save_project', path=str(project))
        session.reset()
        check_tree(0)
        session.q(cmd='open_project', path=str(project))
        check_tree(8)
        params = session.q(cmd='list_plugin_parameters', track=0, plugin=0)['parameters']
        assert any(p['name'] == 'Gain' and abs(p['value'] - .37) < 1e-5 for p in params), params
        result['cases']['projectRestore'] = dict(tree=topology(session.process.pid), parameters=params)
        session.reset()
        check_tree(0)
        for plugin in (args.vst3.resolve(), args.clap.resolve(), args.lv2.resolve(), jsfx):
            index = session.q(cmd='add_audio_track', file=str(sound))['index']
            session.q(cmd='add_plugin', track=index, file=str(plugin))
            session.q(cmd='list_plugin_parameters', track=index, plugin=0)
            session.q(cmd='list_plugin_programs', track=index, plugin=0)
            session.q(cmd='show_plugin_editor', track=index, plugin=0)
        play_case('fourFormats', 4)
        # Kill one of our direct children only. SDK health must bypass it and
        # leave the three other instances alive.
        victim = topology(session.process.pid)[0]['pid']
        handle = K.OpenProcess(1, False, victim)
        assert handle
        try:
            K.TerminateProcess.argtypes = [C.c_void_p, C.c_uint]
            assert K.TerminateProcess(handle, 99)
        finally:
            K.CloseHandle(handle)
        def bypassed() -> dict | bool:
            state = session.q(cmd='status')
            plugins = [p for t in state['audioTracks'] for p in t['plugins']]
            return state if sum(p['running'] for p in plugins) == 3 else False
        result['cases']['crash'] = session.wait(bypassed, 15)
        session.reset()
        check_tree(0)
        result['allChildrenClosedOnReset'] = True
    finally:
        result['shutdown'] = session.close()
        result['filesStable'] = all(hashlib.sha256(Path(h['path']).read_bytes()).hexdigest() == h['sha256'] for h in hashes)
        write_json(out / 'acceptance.json', result)
    assert result['filesStable'] and not result['shutdown']['forced']
    print(json.dumps({name: dict(workers=len(value.get('tree', [])), missedBlocks=value.get('missedBlocks'))
                      for name, value in result['cases'].items() if 'tree' in value}, indent=2))
    print('Eight plugins -> eight direct SDK workers; four formats, project state, editors and crash cleanup passed')


if __name__ == '__main__':
    main()
