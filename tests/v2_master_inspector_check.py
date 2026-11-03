"""Master property preview/cancel checks in an isolated silent Windows instance.
Usage: python tests/v2_master_inspector_check.py --exe <CyDAW.exe> --output <scratch>
"""
import argparse,os,json,socket,subprocess,time,uuid
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('--exe',required=True,type=Path)
parser.add_argument('--output',required=True,type=Path)
parser.add_argument('--port',type=int,default=48046)
args=parser.parse_args()
out=args.output.resolve()/uuid.uuid4().hex;out.mkdir(parents=True,exist_ok=True)
keys=('cache','devices','pluginExports','presets','mixes','foundry','banks','downloads','companion','aiModels','soundModels','processings')
locations=out/'locations.json';locations.write_text(json.dumps(dict(format='CyDAW Locations',version=1,folders={k:str(out/k) for k in keys})),encoding='utf-8')
env=dict(os.environ,CYDAW_NO_RECOVERY='1',CYDAW_LOCATIONS_FILE=str(locations),CYDAW_DJ_COLLECTION_FILE=str(out/'dj.json'),CYDAW_DJ_PROCESSING_FILE=str(out/'dj-processing.json'),CYDAW_VIRTUAL_IO_FILE=str(out/'io.json'))
info=subprocess.STARTUPINFO();info.dwFlags|=subprocess.STARTF_USESHOWWINDOW;info.wShowWindow=0
app=subprocess.Popen([str(args.exe.resolve()),'--silent','--window-size=1800x1100','--language=en',f'--control-port={args.port}'],env=env,startupinfo=info,stdout=(out/'app.log').open('w'),stderr=subprocess.STDOUT)
def q(**data):
 with socket.create_connection(('127.0.0.1',args.port),timeout=10) as s:
  s.settimeout(120);s.sendall((json.dumps(data)+'\n').encode());r=json.loads(s.makefile('rb').readline())
 assert r.get('ok'),r
 return r['result']
def events(*actions):return q(cmd='ui_input',events=list(actions)+[dict(type='wait',frames=3)])
try:
 for _ in range(150):
  try:
   assert q(cmd='status')['speakersSilenced'];break
  except OSError:time.sleep(.1)
 else:raise AssertionError('isolated instance did not start')
 q(cmd='app_mode',mode='studio');q(cmd='get_track_graph',track='master')
 q(cmd='set_master_gain',gainDb=-12)
 q(cmd='snapshot',path=str(out/'inspector.png'))
 found=None
 for y in (105,110,115,120,125,130,140):
  q(cmd='set_master_gain',gainDb=-12)
  events(dict(type='move',x=210,y=y),dict(type='down'),dict(type='move',x=255,y=y))
  value=q(cmd='status')['masterGainDb']
  if abs(value+12)>.05:
   found=y
   events(dict(type='key',key='escape'))
   assert abs(q(cmd='status')['masterGainDb']+12)<.001,'Escape must cancel the preview'
   events(dict(type='up'))
   break
  events(dict(type='up'))
 assert found is not None,'Master field could not be located'
 print('Live master preview and Escape cancellation passed',flush=True)
 # Avoid turning the following independent drag into a double-click text edit.
 time.sleep(.6)
 events(dict(type='move',x=210,y=found),dict(type='down'),dict(type='move',x=255,y=found))
 assert abs(q(cmd='status')['masterGainDb']+12)>.05
 q(cmd='app_mode',mode='cinema')
 events(dict(type='wait',frames=3))
 assert abs(q(cmd='status')['masterGainDb']+12)<.001,'Hidden inspector must cancel the preview'
 events(dict(type='up'))
 q(cmd='app_mode',mode='studio');q(cmd='get_track_graph',track='master')
 time.sleep(.6)
 events(dict(type='move',x=210,y=found),dict(type='down'),dict(type='move',x=255,y=found))
 assert abs(q(cmd='status')['masterGainDb']+12)>.05
 q(cmd='set_master_gain',gainDb=-24)
 events(dict(type='wait',frames=3),dict(type='up'))
 assert abs(q(cmd='status')['masterGainDb']+24)<.001,'External replacement must invalidate stale drafts'
 print('Hidden inspector cancellation and external revision invalidation passed',flush=True)
finally:
 try:q(cmd='quit')
 except (OSError,AssertionError):pass
 try:app.wait(timeout=15)
 except subprocess.TimeoutExpired:app.terminate();app.wait(timeout=10)
