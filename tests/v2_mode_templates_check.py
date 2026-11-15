"""Native installed mode templates: isolated silent load, audio, buses, video, DJ and save copies."""
import argparse,hashlib,json
from pathlib import Path
import numpy as np
from cyaudio_project_audio_pair import Capture,pcm,stats,write_json

def sha(f):return hashlib.sha256(f.read_bytes()).hexdigest()

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--banks',type=Path,default=Path('D:/CyDAW/Banks'));p.add_argument('--port',type=int,default=48287);a=p.parse_args()
 exe=a.exe.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 originals={str(f):sha(f) for mode,ext in [('foundry','cyfoundry'),('cinema','cycinema'),('dj','cydjtemplate')] for f in (exe.parent/'templates'/mode).glob('*.'+ext)};assert len(originals)==6
 report=dict(executable=str(exe),executableSha256=sha(exe),cases=[]);s=Capture(exe,out/'session',a.banks,a.port)
 def tab(mode):
  state=s.q(cmd=mode,action='status');return state['tabs'][state['current']]
 try:
  for mode in ('foundry','cinema','dj'):
   s.q(cmd='app_mode',mode=mode);templates=s.q(cmd='list_templates')['templates'];assert len(templates)==2,(mode,templates)
   for template in templates:
    name=template['name'];row=dict(mode=mode,name=name);print('Check',mode,name,flush=True);s.q(cmd='load_template',name=name)
    if mode!='dj':
     state=tab(mode);assert not state['sessionFile'],state['sessionFile'];row['tracks']=[(t['name'],len(t['clips'])) for t in state['tracks']];assert sum(len(t['clips']) for t in state['tracks'])>=6
     wav=out/(mode+'-'+name+'.wav');s.q(cmd=mode,action='export',path=str(wav),bits=24);rate,x=pcm(wav);row['audio']=stats(x,rate);assert row['audio']['finite'] and row['audio']['activeFrames']>0 and not row['audio']['clippedSampleValues']
     if mode=='cinema':
      buses=[i for i,t in enumerate(state['tracks']) if t['bus']];assert len(buses)==2
      bus_ids={t['id'] for t in state['tracks'] if t['bus']};assert all(t['parentId'] in bus_ids for t in state['tracks'] if not t['bus'])
      for i in buses:s.q(cmd=mode,action='track',track=i,mute=True)
      quiet=out/(name+'-buses-muted.wav');s.q(cmd=mode,action='export',path=str(quiet),bits=24);_,y=pcm(quiet);assert np.max(abs(y))<1e-5
      for i in buses:s.q(cmd=mode,action='track',track=i,mute=False)
      row['busRoutingVerified']=True
      if 'orbital' in name:s.wait(lambda:tab(mode).get('decoder',{}).get('width')==960,30);row['videoDecoded']=True
     s.q(cmd=mode,action='save_session',path=str(out/(name+('.cycinema' if mode=='cinema' else '.cyfoundry'))));row['editableCopySaved']=True
    else:
     expected=4 if 'quatre' in name else 2
     def ready():
      state=s.q(cmd='dj',action='status');return state if all(d['state']=='ready' and d['analysed'] for d in state['decks'][:expected]) else None
     state=s.wait(ready,40);assert state['deckCount']==expected and all(not d['playing'] for d in state['decks']);assert all(d['hotCues'][0]==0 and d['hotCues'][3]>0 for d in state['decks'][:expected])
     s.q(cmd='dj',action='play',deck='A',on=True);s.wait(lambda:s.q(cmd='dj',action='status')['masterPeakDb']>-45,10);row.update(decks=expected,signalVerified=True);s.q(cmd='dj',action='play',deck='A',on=False)
    s.q(cmd='snapshot',path=str(out/(mode+'-'+name+'.png')));row['passed']=True;report['cases'].append(row);write_json(out/'report.json',report)
  report['missingMediaRejected']={}
  for mode,ext in [('foundry','cyfoundry'),('cinema','cycinema'),('dj','cydjtemplate')]:
   s.q(cmd='app_mode',mode=mode)
   first=next((exe.parent/'templates'/mode).glob('*.'+ext));bad=first.with_name('zz-missing-media.'+ext);assert not bad.exists()
   data=json.loads(first.read_text(encoding='utf-8'))
   if mode=='foundry':data['tracks'][0]['clips'][0]['file']='missing-for-template-check.wav';before=len(s.q(cmd=mode,action='status')['tabs'])
   elif mode=='cinema':data['picture']['file']='missing-for-template-check.mp4';before=len(s.q(cmd=mode,action='status')['tabs'])
   else:data['decks'][0]['file']='missing-for-template-check.wav';before=[d['file'] for d in s.q(cmd='dj',action='status')['decks']]
   try:
    write_json(bad,data)
    try:s.q(cmd='load_template',name='zz-missing-media')
    except RuntimeError:pass
    else:raise AssertionError('Missing media was silently accepted')
    after=len(s.q(cmd=mode,action='status')['tabs']) if mode!='dj' else [d['file'] for d in s.q(cmd='dj',action='status')['decks']]
    assert before==after
    report['missingMediaRejected'][mode]=True
   finally:bad.unlink()
  report['originalTemplatesUnchanged']=all(sha(Path(f))==h for f,h in originals.items());assert report['originalTemplatesUnchanged'];s.q(cmd='app_mode',mode='studio');assert len(s.q(cmd='list_templates')['templates'])>=24;report['studioTemplatesRetained']=True
 finally:report['shutdown']=s.close();write_json(out/'report.json',report)
 print('Six mode templates passed; originals unchanged',flush=True)

if __name__=='__main__':main()
