"""Local native UI FPS acceptance, isolated profiles, silent output-only ASIO.
Tests actual menu input and restart persistence; optional frame probes are QA-only.
"""
import argparse,csv,hashlib,json,os,subprocess,time,xml.etree.ElementTree as ET
from pathlib import Path
from cyaudio_comparison import ROOT,profile,write_json,assets
from cyaudio_cpu_diagnostic import CpuSession,scope_deltas
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--build',type=Path,required=True);a=p.parse_args();out=a.output.resolve();built=a.build.resolve();out.mkdir(parents=True,exist_ok=True)
m=json.loads((built/'build-manifest.json').read_text());report=dict(checks={},measurements=[]);asset=assets(out/'assets');port=48228

def set_preference(env,key,value):
 file=Path(env['APPDATA'])/'CyDAW/CyDAW.layout.settings';tree=ET.parse(file);root=tree.getroot();old=root.find("VALUE[@name='"+key+"']")
 if old is None:old=ET.SubElement(root,'VALUE',name=key)
 old.set('val',str(value));tree.write(file,encoding='utf-8',xml_declaration=True)

def preference(env,key):
 root=ET.parse(Path(env['APPDATA'])/'CyDAW/CyDAW.layout.settings').getroot();v=root.find("VALUE[@name='"+key+"']");return v.get('val') if v is not None else None

class RateSession(CpuSession):
 def __init__(self,exe,folder,env,trace=False):
  self.exe=Path(exe);self.folder=folder;self.port=port;folder.mkdir(parents=True,exist_ok=True);env=dict(env)
  if trace:env['CYDAW_QA_CPU_TRACE']=str(folder/'scopes.csv')
  self.log=(folder/'app.log').open('w',encoding='utf-8');self.commands=(folder/'commands.jsonl').open('w',encoding='utf-8')
  self.process=subprocess.Popen([str(self.exe),'--silent','--hidden',f'--control-port={port}','--window-size=1500x900','--language=en'],env=env,stdout=self.log,stderr=subprocess.STDOUT)
  try:
   self.wait(self.ready,60);assert self.q(cmd='status')['speakersSilenced'];self.q(cmd='device_channels',inputs=0,outputs=2)
   self.audio=self.q(cmd='set_audio_device',type='ASIO',output='ASIO Fireface USB',input='',sampleRate=48000,bufferSize=1024)
   assert self.audio['type']=='ASIO' and not self.audio['inputChannels'] and len(self.audio['outputChannels'])==2
  except BaseException:self.close();raise
 def snapshot(self,name):return self.q(cmd='snapshot',path=str(self.folder/(name+'.png')))
 def spots(self):return {s['key']:s for s in self.q(cmd='ui_spots',record=True)['spots']}
 def click(self,key):
  v=self.spots()[key];self.q(cmd='ui_input',events=[dict(type='move',x=v['x'],y=v['y']),dict(type='down',button=0),dict(type='up',button=0),dict(type='wait',frames=3)])
 def menu(self):
  self.q(cmd='ui_spots',record=True);self.snapshot('before-menu');self.click('menu:options');self.click('menu:frame-rate')
  assert all('display-fps:'+str(f) in self.spots() for f in [30,60,120])
 def select_fps(self,fps):self.menu();self.click('display-fps:'+str(fps));self.q(cmd='ui_input',events=[dict(type='release')])
 def arrangement(self):self.q(cmd='set_view',mode=1);time.sleep(.4);self.q(cmd='set_view',mode=3)

# First inspect the normal default menu without saving or enabling hardware MIDI.
env=profile(out/'default-profile',None);s=RateSession(m['variants']['production']['exe'],out/'default',env)
try:
 s.menu();s.snapshot('frame-rate-en');s.click('display-fps:120')
 midi=s.q(cmd='midi_inputs')['devices'];assert all(not d['enabled'] for d in midi);known=[d['identifier'] for d in midi]
 s.q(cmd='set_language',code='fr');s.menu();s.snapshot('frame-rate-fr');s.click('display-fps:60')
 report['checks']['menusEnglishFrench']=True
finally:report['checks']['defaultShutdown']=s.close()

# Real production settings persistence, with all existing MIDI IDs pre-marked
# as known so auto-detection cannot enable any physical MIDI input in this test.
env=profile(out/'saved-profile',None);env.pop('CYDAW_NO_RECOVERY',None);set_preference(env,'knownMidiInputs','\n'.join(known));s=RateSession(m['variants']['production']['exe'],out/'saved',env)
try:
 assert all(not d['enabled'] for d in s.q(cmd='midi_inputs')['devices'])
 for fps in [30,60,120]:
  s.select_fps(fps);assert preference(env,'uiFrameRate')==str(fps);report['checks']['saved'+str(fps)]=True
 s.menu();s.snapshot('saved-120');s.click('display-fps:120')
 assert all(not d['enabled'] for d in s.q(cmd='midi_inputs')['devices'])
finally:report['checks']['savedShutdown']=s.close()

# Settings actually restored by a fresh process; probes count natural frames,
# excluding snapshots and scripted input from the timed window.
cases=[('restart120',120,env),('default60',60,profile(out/'measure-default-profile',None)),('chosen30',30,profile(out/'measure30-profile',None)),('chosen120',120,profile(out/'measure120-profile',None)),('invalidDefaults60',60,profile(out/'measure-invalid-profile',None))]
set_preference(cases[2][2],'uiFrameRate',30);set_preference(cases[3][2],'uiFrameRate',120);set_preference(cases[4][2],'uiFrameRate',999)
for label,expected,environment in cases:
 s=RateSession(m['variants']['instrumented']['exe'],out/label,environment,True);row=dict(label=label,expected=expected,audio=s.audio);report['measurements'].append(row)
 try:
  assert all(not d['enabled'] for d in s.q(cmd='midi_inputs')['devices'])
  if label=='default60':
   assert not s.q(cmd='status')['audioTracks'] and not s.q(cmd='status')['midiTracks'];row['idle']=s.measure(4)
  s.reset();s.q(cmd='add_audio_track',file=str(asset/'long.wav'));s.arrangement();s.q(cmd='transport',action='play',position=.1)
  row['sample']=s.measure(4);assert row['sample']['positionDelta']>3;s.q(cmd='transport',action='stop')
  if label=='default60':
   for mode in ['dj','foundry','cinema','workshop','studio']:s.q(cmd='app_mode',mode=mode);s.snapshot('mode-'+mode)
 finally:
  row['shutdown']=s.close();write_json(out/'results.json',report)
 row['sample']['scopes']=scope_deltas(s.folder/'scopes.csv',row['sample'],[dict(name='ImGuiHost::frame')]);fps=row['sample']['scopes'][0]['callsPerSecond'];row['actualFps']=fps
 assert expected*.94<=fps<=expected*1.04,(label,fps,expected)
 if 'idle' in row:
  row['idle']['scopes']=scope_deltas(s.folder/'scopes.csv',row['idle'],[dict(name='ImGuiHost::frame')]);idle=row['idle']['scopes'][0]['callsPerSecond'];assert 10<=idle<=14,idle;row['idleFps']=idle
 assert not row['shutdown']['forced'];write_json(out/'results.json',report);print(label,'FPS',round(fps,2),'CPU',round(row['sample']['parentCpu'],2),flush=True)
write_json(out/'results.json',report)
print('Menu, persistence, defaults, natural pacing, idle and mode smoke checks passed',flush=True)
