"""Measure raw Studio PCM after isolated Cinema and DJ lifecycle transitions.
Synthetic assets only; actual silent ASIO callback, isolated hidden profiles.
--assert-clean checks raw differences/tails; it never removes DC or aligns PCM.
"""
from __future__ import annotations
import argparse, hashlib, time, traceback
from pathlib import Path
import numpy as np
from cyaudio_cpu_diagnostic import CpuSession
from cyaudio_comparison import assets, pcm, write_json


def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--port',type=int,default=48321);p.add_argument('--assert-clean',action='store_true')
 a=p.parse_args();root=Path(__file__).resolve().parents[1];out=a.output.resolve();assert out.is_relative_to(root/'output');out.mkdir(parents=True,exist_ok=False)
 exe=a.exe.resolve();asset=assets(out/'assets');files=[exe,*exe.parent.glob('*.dll')]
 hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}
 report={'protocol':'Raw, unaligned/uncentered PCM. ASIO Fireface 48000/1024, silent, zero inputs. Fresh isolated process per workflow. Synthetic video and WAV only.','files':hashes,'workflows':[]}
 def one(workflow):
  folder=out/workflow;row={'workflow':workflow,'exports':[]};report['workflows'].append(row);s=None;reference=None
  try:
   s=CpuSession(exe,folder/'session',a.port,'asio');row['audio']=s.audio
   assert all(not d['enabled'] for d in s.q(cmd='midi_inputs')['devices'])
   def export(name):
    nonlocal reference
    s.reset();s.q(cmd='add_audio_track',file=str(asset/'reference.wav'));s.q(cmd='set_master_gain',gainDb=-6,muted=False)
    file=folder/(name+'.wav');s.q(cmd='export_mix',path=str(file));s.wait(lambda:not s.q(cmd='status').get('exporting'),30)
    rate,x=pcm(file);sample_rate,source=pcm(asset/'reference.wav');expected=np.zeros(x.shape);expected[:len(source)]=source*10**(-6/20)
    data={'name':name,'file':str(file),'frames':len(x),'rate':rate,'finite':bool(np.isfinite(x).all()),'mean':x.mean(axis=0).tolist(),'tailMean':x[-2000:].mean(axis=0).tolist(),'tailPeak':float(abs(x[-2000:]).max()),'maxErrorFromInput':float(abs(x-expected).max())}
    if reference is None:reference=x.copy()
    data['maxRawDifferenceFromFresh']=float(abs(x-reference).max()) if x.shape==reference.shape else None
    row['exports'].append(data);write_json(out/'report.json',report)
    print(workflow,name,data,flush=True)
    assert sample_rate==rate==48000 and len(x)==48000*5 and data['finite'],data
    if a.assert_clean:assert data['tailPeak']<3e-7 and data['maxErrorFromInput']<3e-7 and data['maxRawDifferenceFromFresh']<3e-7,data
   export('fresh-studio')
   if workflow=='dj-then-cinema':
    s.q(cmd='app_mode',mode='dj');s.q(cmd='dj',action='mixer',decks=4,autoGain=False)
    for d in 'ABCD':s.q(cmd='dj',action='load',deck=d,file=str(asset/'long.wav'))
    s.wait(lambda:all(d.get('analysed') for d in s.q(cmd='dj')['decks']),60)
    for d in 'ABCD':s.q(cmd='dj',action='channel',deck=d,fader=.7);s.q(cmd='dj',action='play',deck=d,on=True)
    time.sleep(1.5)
    row['djPlaying']=s.q(cmd='dj')
    for d in 'ABCD':s.q(cmd='dj',action='play',deck=d,on=False)
    time.sleep(.2);row['djStopped']=s.q(cmd='dj');export('after-dj-stop')
   s.q(cmd='app_mode',mode='cinema');s.q(cmd='cinema',action='new',name='Synthetic DC check')
   s.q(cmd='cinema',action='video',file=str(asset/'video.mp4'));s.q(cmd='cinema',action='import',track=1,file=str(asset/'long.wav'),at=0)
   s.q(cmd='cinema',action='cursor',at=.1,seconds=True);s.q(cmd='cinema',action='play');time.sleep(1.5)
   row['cinemaPlaying']=s.q(cmd='cinema');assert row['cinemaPlaying']['playing'] and row['cinemaPlaying']['outputPeakDb']>-60
   s.q(cmd='cinema',action='stop');time.sleep(.2);row['cinemaStopped']=s.q(cmd='cinema');assert not row['cinemaStopped']['playing'];export('after-cinema-stop')
  except BaseException:row['error']=traceback.format_exc();raise
  finally:
   if s:row['shutdown']=s.close()
   write_json(out/'report.json',report)
 for workflow in ['cinema-only','dj-then-cinema']:one(workflow)
 report['binariesStable']=all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==v for f,v in hashes.items())
 report['completed']=True;write_json(out/'report.json',report)
 assert report['binariesStable'] and all(not w['shutdown']['forced'] for w in report['workflows'])


if __name__=='__main__':main()
