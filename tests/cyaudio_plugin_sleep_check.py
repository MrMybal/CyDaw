"""Functional sleep/wake acceptance in CyDaw; counters are not CPU percentages.
Uses deterministic generated audio, silent output, isolated profile and zero inputs.
"""
import argparse, hashlib, json, time
from pathlib import Path
from cyaudio_comparison import assets, write_json
from cyaudio_cpu_diagnostic import CpuSession
from cyaudio_single_worker_check import topology


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--plugin', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--mode', choices=['null', 'asio'], default='null')
    p.add_argument('--port', type=int, default=48295)
    a = p.parse_args()
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    paths = [a.exe.resolve(), a.exe.resolve().parent/'CyDAWPluginWorker.exe', a.plugin.resolve()]
    hashes = {str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in paths}
    sound = assets(out/'assets')/'long.wav'
    report = dict(kind='functional sleep counters, not a CPU benchmark', files=hashes, cases={})
    s = CpuSession(a.exe.resolve(), out/'session', a.port, a.mode)
    def plugins():
        return [p for t in s.q(cmd='status')['audioTracks'] for p in t['plugins']]
    def asleep():
        values = plugins()
        return values if len(values) == 8 and all(p['running'] and p['sleepAvailable'] and p['sleeping'] for p in values) else None
    try:
        report['audio'] = s.audio
        assert all(not d['enabled'] for d in s.q(cmd='midi_inputs')['devices'])
        s.reset()
        for _ in range(8):
            index = s.q(cmd='add_audio_track', file=str(sound))['index']
            s.q(cmd='add_plugin', track=index, file=str(a.plugin.resolve()))
        report['loaded'] = plugins(); write_json(out/'report.json', report)
        first = s.wait(asleep, 15)
        tree = topology(s.process.pid)
        assert len(tree) == 8 and all(p['parent'] == s.process.pid and p['image'] == 'CyDAWPluginWorker.exe' for p in tree), tree
        time.sleep(.75)
        second = plugins()
        assert all(b['processedBlocks'] == a['processedBlocks'] and b['skippedBlocks'] > a['skippedBlocks'] for a,b in zip(first, second)), (first, second)
        report['cases']['idle'] = dict(first=first, last=second, tree=tree)
        s.q(cmd='set_plugin_parameter', track=0, plugin=0, parameter='Gain', value=.75)
        s.wait(lambda: plugins()[0]['processedBlocks'] > second[0]['processedBlocks'], 10)
        report['cases']['automationWake'] = s.wait(asleep, 10)
        s.q(cmd='transport', action='play', position=.1)
        time.sleep(.75)
        playing = s.q(cmd='status')
        assert playing['position'] > .5
        assert max(t['peakDb'] for t in playing['meters']) > -80, playing['meters']
        assert all(p['running'] and not p['sleeping'] for t in playing['audioTracks'] for p in t['plugins'])
        report['cases']['playWake'] = playing
        s.q(cmd='transport', action='stop')
        report['cases']['stopSleep'] = s.wait(asleep, 15)
        project = out/'sleep.cydaw'; s.q(cmd='save_project', path=str(project))
        s.reset(); s.q(cmd='open_project', path=str(project))
        report['cases']['restoreSleep'] = s.wait(asleep, 15)
        params = s.q(cmd='list_plugin_parameters', track=0, plugin=0)['parameters']
        assert any(p['name'] == 'Gain' and abs(p['value']-.75)<1e-6 for p in params), params
        report['restoredParameter'] = params
        s.reset(); s.wait(lambda: not topology(s.process.pid), 15)
        report['allChildrenClosed'] = True
    finally:
        report['shutdown'] = s.close()
        report['filesStable'] = all(hashlib.sha256(Path(f).read_bytes()).hexdigest() == h for f,h in hashes.items())
        write_json(out/'report.json', report)
    assert report['filesStable'] and not report['shutdown']['forced']
    print(json.dumps(dict(passed=True, idleProcessed=[p['processedBlocks'] for p in second], skippedDelta=[b['skippedBlocks']-a['skippedBlocks'] for a,b in zip(first,second)], cases=list(report['cases']))))

if __name__ == '__main__': main()
