"""Capture genuine bilingual CyDAW screens and registry metadata in a private silent process.
No user session, songs, banks or hardware settings are used. Requires a Windows V2 executable.
"""
import argparse,json,math,os,socket,struct,subprocess,tempfile,time,wave
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--images',type=Path,required=True);p.add_argument('--catalogue',type=Path);p.add_argument('--ffmpeg',type=Path);p.add_argument('--port',type=int,default=48063);a=p.parse_args()
a.images.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='cydaw-manual-') as temporary:
 scratch=Path(temporary)
 folders=('cache','devices','pluginExports','presets','mixes','foundry','banks','downloads','companion','aiModels','soundModels','processings')
 locations=scratch/'locations.json';locations.write_text(json.dumps(dict(format='CyDAW Locations',version=1,folders={k:str(scratch/k) for k in folders})),encoding='utf-8')
 env=dict(os.environ,CYDAW_NO_RECOVERY='1',CYDAW_SINGLE_VIEWPORT='1',CYDAW_LOCATIONS_FILE=str(locations),CYDAW_DJ_COLLECTION_FILE=str(scratch/'dj.json'),CYDAW_DJ_PROCESSING_FILE=str(scratch/'dj-processing.json'),CYDAW_VIRTUAL_IO_FILE=str(scratch/'io.json'))
 startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
 log=(scratch/'app.log').open('w',encoding='utf-8')
 app=subprocess.Popen([str(a.exe.resolve()),'--silent','--language=en','--window-size=1800x1100',f'--control-port={a.port}'],env=env,startupinfo=startup,stdout=log,stderr=subprocess.STDOUT)
 def raw(**data):
  with socket.create_connection(('127.0.0.1',a.port),timeout=10) as s:
   s.settimeout(120);s.sendall((json.dumps(data)+'\n').encode());return json.loads(s.makefile('rb').readline())
 def q(**data):
  result=raw(**data)
  if not result.get('ok'):raise RuntimeError((data,result))
  return result['result']
 def shot(name):
  for lang in ('en','fr'):
   q(cmd='set_language',code=lang);q(cmd='ui_input',events=[dict(type='wait',frames=4)])
   q(cmd='snapshot',path=str((a.images/(name+'-'+lang+'.png')).resolve()))
  q(cmd='set_language',code='en')
  print('Captured',name,flush=True)
 try:
  for _ in range(200):
   if app.poll() is not None:raise RuntimeError('test app exited')
   try:
    assert q(cmd='status')['speakersSilenced'];break
   except OSError:time.sleep(.1)
  else:raise RuntimeError('test app did not start')
  if a.catalogue:
   result=dict(schema=1,version='0.78.0',languages={})
   for lang in ('en','fr'):
    q(cmd='set_language',code=lang);types=[]
    for entry in q(cmd='list_devices')['devices']:
     if entry['kind']!='node':continue
     value=raw(cmd='describe_node_type',key=entry['key'])
     if value.get('ok'):types.append(value['result'])
    result['languages'][lang]=types
   a.catalogue.parent.mkdir(parents=True,exist_ok=True);a.catalogue.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
   print('Registry snapshot:',{k:len(v) for k,v in result['languages'].items()},flush=True)
  # Eight seconds of an original test phrase: not a recording or third-party track.
  sound=scratch/'Manual sound.wav'
  with wave.open(str(sound),'wb') as f:
   f.setnchannels(2);f.setsampwidth(2);f.setframerate(48000)
   for start in range(0,8*48000,48000):
    frames=bytearray()
    for i in range(start,start+48000):
     t=i/48000;phase=t%.5;envv=math.exp(-phase*9);note=(220,277.18,329.63,415.3)[int(t*2)%4]
     value=int(8500*envv*(math.sin(2*math.pi*note*t)+.15*math.sin(2*math.pi*note*2*t)))
     frames+=struct.pack('<hh',value,value)
    f.writeframes(frames)
  q(cmd='new_project');mid=q(cmd='add_midi_track',name='Manual synth',instrument='opale-instrument')
  track='Manual synth'
  q(cmd='add_notes',track=track,clip='First phrase',lengthBeats=16,notes=[dict(note=n,start=i*.5,length=.4,velocity=.7) for i,n in enumerate((60,64,67,71,69,67,64,62,60,64,67,72,71,67,64,60))])
  q(cmd='add_audio_track',file=str(sound));q(cmd='set_view',mode=1)
  shot('studio')
  clips=q(cmd='list_notes',track=track)['clips'];clip=clips[0]['id']
  q(cmd='set_view',mode=2,track=track,clipId=clip);shot('midi')
  q(cmd='get_track_graph',track=track);q(cmd='set_view',mode=7);shot('graph')
  q(cmd='set_view',mode=9);shot('mixer')
  q(cmd='add_midi_clip',track=track,scene=0,name='Launch phrase',lengthBeats=8);q(cmd='set_view',mode=4);shot('launcher')
  q(cmd='get_track_graph',track=track);q(cmd='set_view',mode=7)
  q(cmd='show_dialog',name='opale');shot('synth')
  # Close the instrument window via its real title-bar control before changing modes.
  q(cmd='ui_input',events=[dict(type='move',x=1226,y=70),dict(type='down'),dict(type='up'),dict(type='wait',frames=4)])
  # Separate sources and effects, with meaningful settings.
  q(cmd='workshop',action='new',name='Manual compressor',instrument=False)
  node=q(cmd='add_node',track='workshop',key='comp-pro')['id']
  q(cmd='disconnect',track='workshop',source=1,destination=2);q(cmd='connect',track='workshop',source=1,destination=node);q(cmd='connect',track='workshop',source=node,destination=2)
  q(cmd='set_node_parameter',track='workshop',nodeId=node,parameter='comppro.threshold',value=-18)
  q(cmd='workshop',action='programs',op='add',name='Gentle control');shot('workshop')
  q(cmd='app_mode',mode='foundry');q(cmd='foundry',action='new',name='Manual sound editing')
  q(cmd='foundry',action='import',file=str(sound),at=0);q(cmd='foundry',action='import',file=str(sound),at=2,track=0);shot('foundry')
  q(cmd='app_mode',mode='cinema');q(cmd='cinema',action='new',name='Manual picture session');q(cmd='cinema',action='view',resetLayout=True,start=0,seconds=8,length=8)
  if a.ffmpeg:
   video=scratch/'Reference.mp4';subprocess.run([str(a.ffmpeg.resolve()),'-y','-loglevel','error','-f','lavfi','-i','testsrc2=size=640x360:rate=25:duration=8','-c:v','mpeg4','-pix_fmt','yuv420p',str(video)],check=True)
   q(cmd='cinema',action='video',file=str(video));time.sleep(1)
  q(cmd='cinema',action='import',file=str(sound),track=0,at=0);shot('cinema')
  q(cmd='app_mode',mode='dj');q(cmd='dj',action='load',deck=0,file=str(sound));q(cmd='dj',action='load',deck=1,file=str(sound));time.sleep(1);shot('dj')
  q(cmd='app_mode',mode='studio')
  q(cmd='jsfx',action='new');shot('jsfx')
 finally:
  try:q(cmd='quit')
  except (OSError,RuntimeError):pass
  try:app.wait(timeout=15)
  except subprocess.TimeoutExpired:app.terminate();app.wait(timeout=10)
  log.close()
