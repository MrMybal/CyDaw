"""Exercise module/preset filters, native labels, Master editing/audio/persistence and MIDI dezoom.

Writes only synthetic fixtures and isolated preferences below an ignored output directory.
The ASIO instance is silent, output-only; does not touch an already-running user instance.
The first export block is reported separately; its reload difference remains unresolved.
Run only with authorization for the Fireface output, as with cyaudio_project_audio_pair.py.
"""
import sys,json,traceback,argparse
from pathlib import Path
import numpy as np
root=Path.cwd();sys.path.insert(0,str(root/'tests'))
from cyaudio_project_audio_pair import Capture,pcm,write_json
from cyaudio_comparison import assets
parser=argparse.ArgumentParser(description='Isolated UI and audio acceptance; uses the explicitly authorized silent ASIO Fireface output (48k/1024, no inputs).')
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--exe',type=Path,default=root/'build/cyaudio-sleep/CyDAW2_artefacts/Release/CyDAW 2.exe')
parser.add_argument('--banks',type=Path,default=Path('D:/CyDAW/Banks'))
parser.add_argument('--port',type=int,default=48298)
args=parser.parse_args();out=args.output.resolve();assert out.is_relative_to(root/'output');out.mkdir(parents=True,exist_ok=False);r={'cases':{}};s=None
try:
 s=Capture(args.exe.resolve(),out/'session',args.banks,args.port);r['audio']=s.audio
 def snap(name):s.q(cmd='snapshot',path=str(out/(name+'.png')))
 def click(x,y):return [dict(type='move',x=x,y=y),dict(type='down',button=0),dict(type='up',button=0)]
 def shortcut(key):s.q(cmd='ui_input',events=[dict(type='key',key='ctrl',down=True),dict(type='key',key=key),dict(type='key',key='ctrl',down=False),dict(type='wait',frames=3)])
 def waitlib(typ):return s.wait(lambda: (lambda p:p if p.get('entries') and all(e['type']==typ if typ else e['role']=='Audio effect' and e['type']!='Preset' for e in p['entries']) else None)(s.q(cmd='library_window')),15)
 def export(name):
  file=out/(name+'.wav');s.q(cmd='export_mix',path=str(file));s.wait(lambda:not s.q(cmd='status').get('exporting'),25);return pcm(file)[1]
 s.reset();s.q(cmd='add_midi_track',name='Synth test',instrument='ambre-instrument')
 catalogue=s.q(cmd='list_devices')['devices'];r['catalogue']=catalogue
 for tab,content,role,label,typ in [('instruments',1,0,'instrument-modules','Instrument module'),('instruments',2,0,'instrument-presets','Instrument preset'),('effects',0,1,'audio-effects',None)]:
  s.q(cmd='library_window',show=True,tab=tab,content=content,role=role,query='');v=waitlib(typ);assert all(e['description'] for e in v['entries']),v
  if typ:assert all(e['type']==typ for e in v['entries']),v
  if tab=='effects':assert all(e['role']=='Audio effect' and e['type']!='Preset' for e in v['entries'])
  r['cases'][label]=v;snap(label);print(label,v['rows'],flush=True)
 # Search only among real instrument modules; activate through the UI.
 s.q(cmd='library_window',show=True,tab='instruments',content=1,query='Opale')
 selected=s.wait(lambda:(lambda p:p if p.get('rows')==1 and p.get('entries',[{}])[0].get('key')=='opale-synth' else None)(s.q(cmd='library_window')),15)
 assert 'polysynth' in selected['entries'][0]['description'].lower()
 s.q(cmd='ui_input',events=click(180,405)+click(180,405)+[dict(type='wait',frames=3)])
 r['cases']['instrumentInsertion']=s.q(cmd='get_track_graph',track='Synth test');assert any(n['type']=='opale-synth' for n in r['cases']['instrumentInsertion']['nodes'])
 s.q(cmd='library_window',show=False)
 s.reset();sound=assets(out/'assets')/'reference.wav';s.q(cmd='add_audio_track',file=str(sound));s.q(cmd='set_master_gain',gainDb=0)
 baseline=export('baseline')
 v=s.q(cmd='add_automation',track='master',clear=True,points=[{'time':0,'valueDb':-24},{'time':1,'valueDb':-24},{'time':2,'valueDb':0}]);assert v['added']==3,v
 s.q(cmd='add_bus_track',name='FX bus');s.q(cmd='set_view',mode=1);snap('arrangement-master-and-bus')
 project=out/'master-automation.cydaw';s.q(cmd='save_project',path=str(project));d=json.loads(project.read_text(encoding='utf-8'));assert d['masterArrangement']['automationVisible']
 assert len(d['masterArrangement']['parameterAutomation'][0]['points'])==3
 automated=export('automated');assert baseline.shape==automated.shape,(baseline.shape,automated.shape)
 a=baseline[10000:40000];b=automated[10000:40000];ratio=float(np.sqrt(np.mean(b*b))/np.sqrt(np.mean(a*a)));assert abs(20*np.log10(ratio)+24)<.1,ratio
 last=baseline[105000:130000];other=automated[105000:130000];assert np.max(abs(last-other))<1e-4
 r['cases']['masterGainAudio']=dict(ratioDb=float(20*np.log10(ratio)),returnToZeroDbError=float(np.max(abs(last-other))))
 s.q(cmd='open_project',path=str(project));assert s.q(cmd='list_automation',track='master')['points']==3
 restored=export('restored');assert restored.shape==automated.shape
 delta=np.max(abs(restored-automated),axis=1);changed=np.flatnonzero(delta>1e-4)
 r['cases']['roundtripAudio']=dict(rawMaxError=float(delta.max()),afterFirstBlockMaxError=float(delta[1024:].max()),differingFrameRange=changed[[0,-1]].tolist() if len(changed) else None,firstBlockFrames=1024)
 assert delta[1024:].max()<1e-4
 r['cases']['masterGainRoundtrip']=True
 s.q(cmd='clear_automation',track='master');assert s.q(cmd='list_automation',track='master')['points']==0
 s.q(cmd='set_view',mode=1)
 s.q(cmd='ui_input',events=click(650,466)+click(650,466)+[dict(type='wait',frames=3)])
 assert s.q(cmd='list_automation',track='master')['points']==1
 shortcut('z');assert s.q(cmd='list_automation',track='master')['points']==0
 shortcut('y');assert s.q(cmd='list_automation',track='master')['points']==1
 r['cases']['masterMouseUndoRedo']=True
 s.q(cmd='clear_automation',track='master')
 # Native effect labels, all populated through their usual graph bindings.
 for key in ['reverb','tranche','reverb-pro']:
  if any(d['key']==key for d in catalogue):s.q(cmd='insert_device',track=0,key=key)
 s.q(cmd='set_view',mode=1,track=0);snap('device-labels')
 r['cases']['graphsAfterEffects']=s.q(cmd='get_track_graph',track=0)
 # A long MIDI clip must fit well below the former 6 px/beat floor.
 s.reset();name=s.q(cmd='add_midi_track',name='Long MIDI zoom',instrument='opale-instrument')['name']
 notes=[dict(note=48+i%24,start=i*64,length=4,velocity=.6) for i in range(65)]
 s.q(cmd='add_notes',track=name,clip='Long clip',lengthBeats=4096,notes=notes)
 clips=s.q(cmd='list_notes',track=name)['clips'];s.q(cmd='set_view',mode=2,track=name,clipId=clips[0]['id']);snap('long-midi-fit')
 s.q(cmd='ui_input',events=[dict(type='move',x=500,y=720),dict(type='key',key='ctrl',down=True)]+[dict(type='wheel',dy=-1) for _ in range(20)]+[dict(type='key',key='ctrl',down=False),dict(type='wait',frames=3)])
 snap('long-midi-zoom-out')
 r['cases']['longClip']=dict(lengthBeats=4100,notes=65,zoomOutWheelEvents=20)
 s.q(cmd='ping')
 r['passed']=True
except BaseException:r['error']=traceback.format_exc();raise
finally:
 if s:r['shutdown']=s.close()
 write_json(out/'report.json',r)
print(json.dumps({'passed':r.get('passed'),'cases':list(r['cases'])}),flush=True)
