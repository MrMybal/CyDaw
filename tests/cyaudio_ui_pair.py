"""Paired UI CPU/cadence comparison; output-only ASIO, silent, hidden, isolated profiles.
Requires cyaudio_ui_pair_build.py. No production or SDK changes.
"""
import argparse,csv,hashlib,json,statistics,time
from pathlib import Path
from cyaudio_cpu_diagnostic import CpuSession,descendants
from cyaudio_comparison import ROOT,assets,write_json

def scopes(file,case,sites):
 snapshots={}
 with file.open(encoding='utf-8') as f:
  for r in csv.DictReader(f):snapshots.setdefault(float(r['qpcSeconds']),{})[(int(r['tid']),int(r['site']))]=[int(r['calls']),float(r['inclusiveSeconds']),float(r['exclusiveSeconds']),int(r['inclusiveCycles']),int(r['exclusiveCycles'])]
 ts=sorted(t for t in snapshots if case['beginQpc']<=t<=case['endQpc']);assert len(ts)>2
 lo,hi=ts[0],ts[-1];result=[]
 for (tid,site),v in snapshots[hi].items():
  old=snapshots[lo].get((tid,site),[0]*5);d=[x-y for x,y in zip(v,old)]
  if d[0]>0:result.append(dict(tid=tid,name=sites[site]['name'],calls=d[0],callsPerSecond=d[0]/(hi-lo),meanUs=d[1]/d[0]*1e6,exclusiveUs=d[2]/d[0]*1e6,cyclesPerCall=d[3]/d[0],exclusiveCyclesPerCall=d[4]/d[0],inclusiveCyclesPerSecond=d[3]/(hi-lo),exclusiveCyclesPerSecond=d[4]/(hi-lo),windowSeconds=hi-lo))
 return result

def fingerprint(out,m):
 files=[out/'build-manifest.json',*sorted((out/'probes').glob('*'))]
 for label,v in m['variants'].items():
  files+=[Path(v['exe']),*sorted((out/label/'sources').glob('*.cpp'))]
  source=ROOT if label=='native' else ROOT/'output/juce-core-comparison/juce-source'
  files += [source/f for f in v['sourceHashes']]
 h=hashlib.sha256()
 for f in files:h.update(str(f).encode());h.update(f.read_bytes())
 return h.hexdigest()

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--rounds',type=int,default=3);p.add_argument('--seconds',type=float,default=6);p.add_argument('--tag',default='pair');p.add_argument('--cases',nargs='+',default=['idle8','audio32','effectsSerial48','effects16','dj4']);p.add_argument('--cadences',nargs='+',default=['normal','cap30']);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'output')
 m=json.loads((out/'build-manifest.json').read_text(encoding='utf-8'));start=fingerprint(out,m);asset=assets(out/'assets');rows=[];result=out/(a.tag+'-results.json');assert not result.exists(),'Choose a new tag'
 order=[('JUCE',u) for u in a.cadences]+[('native',u) for u in reversed(a.cadences)]
 for iteration in range(a.rounds):
  for label,ui in (order if iteration%2==0 else list(reversed(order))):
   folder=out/a.tag/f'{iteration+1}-{label}-{ui}';s=CpuSession(Path(m['variants'][label]['exe']),folder,48221,'asio',True,ui);r=dict(label=label,ui=ui,round=iteration+1,audio=s.audio,cases={});rows.append(r)
   try:
    s.reset()
    if 'idle8' in a.cases:
     for _ in range(8):
      t=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['name'];s.q(cmd='add_plugin',track=t,file=str(ROOT/'build/cyaudio-desktop/CyDAWTestGain_artefacts/Release/VST3/CyDAW Test Gain.vst3'))
     s.q(cmd='set_view',mode=1);time.sleep(.4);s.q(cmd='set_view',mode=3);s.q(cmd='snapshot',path=str(folder/'idle8.png'));r['cases']['idle8']=s.measure(a.seconds);assert sum(p['image']=='CyDAWPluginWorker.exe' for p in r['cases']['idle8']['processes'])==8
    s.reset();s.wait(lambda:len(descendants(s.process.pid))==1)
    for name,n in [('audio32',32),('effectsSerial48',16),('effects16',16)]:
     if name not in a.cases:continue
     s.reset()
     for _ in range(n):
      created=s.q(cmd='add_audio_track',file=str(asset/'long.wav'));t=created['index'] if name=='effects16' else created['name']
      if name in ['effects16','effectsSerial48']:
       for key in ['comp-pro','chorus','reverb-pro']:s.q(cmd='insert_device',track=t,key=key)
     topology=s.q(cmd='status')['audioTracks']
     if name=='effects16':assert len(topology)==16 and all(set(t['devices'])=={'comp-pro','chorus','reverb-pro'} for t in topology),topology
     if name=='effectsSerial48':assert set(topology[0]['devices'])=={'comp-pro','chorus','reverb-pro'} and all(not t['devices'] for t in topology[1:])
     r.setdefault('topologies',{})[name]=topology
     s.q(cmd='set_view',mode=1);time.sleep(.4);s.q(cmd='set_view',mode=3);s.q(cmd='snapshot',path=str(folder/(name+'.png')));s.q(cmd='transport',action='play',position=.1);r['cases'][name]=s.measure(a.seconds);assert r['cases'][name]['positionDelta']>a.seconds*.6;s.q(cmd='transport',action='stop')
    s.reset();s.wait(lambda:len(descendants(s.process.pid))==1)
    if 'dj4' in a.cases:
     s.q(cmd='app_mode',mode='dj');s.q(cmd='dj',action='mixer',decks=4,autoGain=False)
     for d in 'ABCD':s.q(cmd='dj',action='load',deck=d,file=str(asset/'long.wav'))
     s.wait(lambda:all(d.get('analysed') for d in s.q(cmd='dj')['decks']),60)
     for d in 'ABCD':s.q(cmd='dj',action='channel',deck=d,fader=.7);s.q(cmd='dj',action='play',deck=d,on=True)
     s.q(cmd='set_view',mode=1);time.sleep(.4);s.q(cmd='set_view',mode=3);s.q(cmd='snapshot',path=str(folder/'dj4.png'));r['cases']['dj4']=s.measure(a.seconds);r['djState']=s.q(cmd='dj');assert all(d['playing'] for d in r['djState']['decks']);assert len(r['cases']['dj4']['processes'])==1
     for d in 'ABCD':s.q(cmd='dj',action='play',deck=d,on=False)
   finally:
    r['shutdown']=s.close();assert not r['shutdown']['forced'];r['sourceStable']=fingerprint(out,m)==start;assert r['sourceStable']
    for name,case in r['cases'].items():
     case['scopes']=scopes(folder/'scopes.csv',case,m['sites'])
     assert any(z['name']=='DevicesView::draw' for z in case['scopes']),('Devices panel not drawn',label,name)
    write_json(result,rows)
   print(iteration+1,label,ui,{k:(round(v['parentCpu'],2),round(next(z['callsPerSecond'] for z in v['scopes'] if z['name']=='ImGuiHost::frame'),2)) for k,v in r['cases'].items()},flush=True)
 write_json(out/(a.tag+'-verification.json'),dict(fingerprint=start,stable=fingerprint(out,m)==start,executables={k:hashlib.sha256(Path(v['exe']).read_bytes()).hexdigest() for k,v in m['variants'].items()},allOriginalInputsStable=all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==h for f,h in m['inputs'].items())))
if __name__=='__main__':main()
