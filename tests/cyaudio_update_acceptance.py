"""Revalidate the committed CUE/export fixes in a real, silent ASIO callback.

Uses only synthetic audio and an isolated hidden session. No physical inputs.
The ASIO output configuration requires the hardware authorization of the caller.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import time
import traceback
import wave
import numpy as np
from cyaudio_cpu_diagnostic import CpuSession
from cyaudio_comparison import pcm, write_json


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--seconds', type=int, default=120)
    p.add_argument('--port', type=int, default=48315)
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = a.output.resolve()
    assert out.is_relative_to(root/'output') and a.seconds >= 12
    out.mkdir(parents=True, exist_ok=False)
    exe = a.exe.resolve()
    files = [exe, exe.parent/'CyDAWPluginWorker.exe', *exe.parent.glob('*.dll')]
    hashes = {str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in files}
    report = dict(protocol='Real ASIO 48000 Hz/1024 frames, zero input, silent, hidden isolated profile; no CPU or physical latency measurement',
                  files=hashes, cue=[], result='running')
    rate = 48000
    t = np.arange(a.seconds*rate)/rate
    x = np.column_stack((.14*np.sin(t*2*np.pi*220), .12*np.sin(t*2*np.pi*337)))
    x[:4800] = 0; x[-4800:] = 0
    quantized = np.round(x*32767).astype('<i2')
    source = out/'synthetic.wav'
    with wave.open(str(source), 'wb') as w:
        w.setparams((2,2,rate,0,'NONE','PCM')); w.writeframes(quantized.tobytes())
    del t, x
    s = None
    try:
        s = CpuSession(exe, out/'session', a.port, 'asio')
        report['audio'] = s.audio
        assert all(not d['enabled'] for d in s.q(cmd='midi_inputs')['devices'])
        s.q(cmd='app_mode', mode='dj')
        s.q(cmd='dj', action='mixer', quantize=False)
        s.q(cmd='dj', action='load', deck='A', file=str(source))
        def deck():
            return next(d for d in s.q(cmd='dj')['decks'] if d['deck']=='A')
        s.wait(lambda: deck().get('analysed'), 45)
        for keylock in (False, True):
            s.q(cmd='dj', action='keylock', deck='A', on=keylock)
            for trial in range(10):
                s.q(cmd='dj', action='play', deck='A', on=False)
                s.q(cmd='dj', action='seek', deck='A', seconds=8.21)
                time.sleep(.08)
                s.q(cmd='dj', action='cue', deck='A', down=True)
                time.sleep(.12)
                preview = deck()
                assert preview['position'] > preview['cuePoint']+.04, preview
                s.q(cmd='dj', action='cue', deck='A', down=False)
                time.sleep(.08)
                stopped = deck()
                drift = abs(stopped['position']-stopped['cuePoint'])
                assert not stopped['playing'] and drift<1e-8, stopped
                time.sleep(.06)
                later = deck()
                assert abs(later['position']-stopped['cuePoint'])<1e-8, later
                s.q(cmd='dj', action='play', deck='A', on=True)
                time.sleep(.08)
                assert deck()['playing']
                s.q(cmd='dj', action='cue', deck='A', down=True)
                time.sleep(.08)
                play_stop = deck()
                assert not play_stop['playing'] and abs(play_stop['position']-play_stop['cuePoint'])<1e-8, play_stop
                s.q(cmd='dj', action='cue', deck='A', down=False)
                report['cue'].append(dict(keylock=keylock,trial=trial,preview=preview,stopped=stopped,later=later,playingCue=play_stop,driftSeconds=drift))
            print('CUE keylock', keylock, '10/10, exact position', flush=True)
        s.reset()
        s.q(cmd='add_audio_track', file=str(source))
        s.q(cmd='set_master_gain', gainDb=0, muted=False)
        before = s.q(cmd='status')
        target = out/'master.wav'
        s.q(cmd='export_mix', path=str(target))
        start = time.perf_counter()
        s.wait(lambda: not s.q(cmd='status').get('exporting'), a.seconds+45)
        report['exportElapsedSeconds'] = time.perf_counter()-start
        report['after'] = s.q(cmd='status')
        sample_rate, rendered = pcm(target)
        expected_frames = round((before['length']+2)*rate)
        expected = np.zeros((expected_frames,2))
        expected[:len(quantized)] = quantized.astype(np.float64)/32768
        report['export'] = dict(expectedFrames=expected_frames,frames=len(rendered),sampleRate=sample_rate,
                                finite=bool(np.isfinite(rendered).all()),lengthSeconds=before['length'],
                                maxError=float(np.max(np.abs(rendered-expected))) if rendered.shape==expected.shape else None,
                                tailPeak=float(np.max(np.abs(rendered[len(quantized):]))) if len(rendered)>len(quantized) else None)
        assert sample_rate==rate and rendered.shape==expected.shape, report['export']
        assert report['export']['finite'] and report['export']['maxError']<3e-7, report['export']
        assert report['export']['tailPeak']==0, report['export']
        report['result']='passed'
        print('Master export:', json.dumps(report['export']), flush=True)
    except BaseException:
        report['result']='failed'; report['error']=traceback.format_exc()
        raise
    finally:
        if s: report['shutdown']=s.close()
        report['binariesStable']=all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==v for f,v in hashes.items())
        if not report['binariesStable'] or report.get('shutdown', {}).get('forced'):
            report['result']='failed'
            report.setdefault('error', 'Binary changed during acceptance or forced shutdown')
        write_json(out/'report.json', report)
    assert report['binariesStable'] and not report['shutdown']['forced'], report.get('shutdown')


if __name__=='__main__': main()
