"""Measure OS key-message dispatch, UI polling, live MIDI collection and internal audio readiness.

Requires authorization for output-only silent use of the current audio device.
Does not measure physical switches, USB polling or DAC/speaker latency. Runtime
probe is inactive unless CYDAW_KEY_LATENCY_PROBE=1; no user settings are changed.
"""
import sys,os,time,json,socket,subprocess,ctypes as C,xml.etree.ElementTree as E,traceback,random,argparse,hashlib
from pathlib import Path
import numpy as np
sys.path.insert(0,'tests')
from cyaudio_comparison import Session,profile,write_json
root=Path.cwd()
parser=argparse.ArgumentParser(description='Opt-in, silent computer-keyboard latency measurement; clones current audio settings into an isolated profile. Only posts keys to its own test window on the hidden automation desktop.')
parser.add_argument('--output',type=Path,required=True);parser.add_argument('--fps',type=int,choices=[30,60,120],default=60)
parser.add_argument('--samples',type=int,default=24);parser.add_argument('--playing',action='store_true')
parser.add_argument('--mode',choices=['os','native','mapped'],default='os',help='os: legacy WM_KEYDOWN; native: synthetic event on the native service; mapped: same event through programmable MIDI bridge')
parser.add_argument('--stall-ms',type=int,default=0,help='opt-in app dispatcher stall during the last six trials (native/mapped only)')
parser.add_argument('--instrument',default='opale-instrument');parser.add_argument('--port',type=int,default=48311)
args=parser.parse_args();out=args.output.resolve();assert out.is_relative_to(root/'output');out.mkdir(parents=True,exist_ok=False)
random.seed(1387)

U=C.WinDLL('user32',use_last_error=True);U.PostMessageW.argtypes=[C.c_void_p,C.c_uint,C.c_size_t,C.c_ssize_t];U.PostMessageW.restype=C.c_int
U.GetWindowThreadProcessId.argtypes=[C.c_void_p,C.POINTER(C.c_ulong)]
U.GetClassNameW.argtypes=[C.c_void_p,C.c_wchar_p,C.c_int]
WNDENUM=C.WINFUNCTYPE(C.c_int,C.c_void_p,C.c_ssize_t)
U.EnumWindows.argtypes=[WNDENUM,C.c_ssize_t];U.EnumChildWindows.argtypes=[C.c_void_p,WNDENUM,C.c_ssize_t]
def hwnd_for(pid):
 found=[]
 @WNDENUM
 def child(h,_):
  n=C.c_ulong();U.GetWindowThreadProcessId(h,C.byref(n));cls=C.create_unicode_buffer(256);U.GetClassNameW(h,cls,256)
  if n.value==pid and cls.value.startswith('CyDAW2ImGuiSurface'):found.append(h)
  return 1
 @WNDENUM
 def f(h,_):
  child(h,0)
  n=C.c_ulong();U.GetWindowThreadProcessId(h,C.byref(n))
  if n.value==pid:U.EnumChildWindows(h,child,0)
  return 1
 U.EnumWindows(f,0)
 if not found:
  U.OpenDesktopW.argtypes=[C.c_wchar_p,C.c_ulong,C.c_int,C.c_ulong];U.OpenDesktopW.restype=C.c_void_p
  U.EnumDesktopWindows.argtypes=[C.c_void_p,WNDENUM,C.c_ssize_t];U.CloseDesktop.argtypes=[C.c_void_p]
  desk=U.OpenDesktopW('CyDAW-automation',0,False,0x1|0x40|0x80);assert desk,C.get_last_error()
  U.SetThreadDesktop.argtypes=[C.c_void_p];assert U.SetThreadDesktop(desk),C.get_last_error();U.EnumDesktopWindows(desk,f,0)
 return found[0] if found else None
personal=Path(os.environ['APPDATA'])/'CyDAW/CyDAW.layout.settings'
r=E.parse(personal).getroot();vals={v.get('name'):v.get('val') for v in r.findall('VALUE')};dev=E.fromstring(vals['audioDeviceState'].replace(chr(92)+'n',chr(10)))
folder=out/'session';folder.mkdir();env=profile(folder,Path('D:/CyDAW/Banks'));env['CYDAW_KEY_LATENCY_PROBE']='1'
dev.attrib.update(audioInputDeviceName='',audioDeviceInChans='0',audioDeviceOutChans='11',useDefaultInputChannels='0',useDefaultOutputChannels='0')
for ch in list(dev):dev.remove(ch)
for suffix in ['layout','processing']:
 props=E.Element('PROPERTIES');E.SubElement(props,'VALUE',name='audioDeviceState',val=E.tostring(dev,encoding='unicode'))
 for key,value in [('computerKeyboard','1'),('computerKeyboardOctave',vals.get('computerKeyboardOctave','5')),('computerKeyboardVelocity',vals.get('computerKeyboardVelocity','100')),('uiFrameRate',str(args.fps))]:E.SubElement(props,'VALUE',name=key,val=value)
 E.ElementTree(props).write(folder/'Roaming/CyDAW'/('CyDAW.'+suffix+'.settings'),encoding='utf-8')
s=Session.__new__(Session);s.port=args.port;s.folder=folder;s.exe=root/'build/cyaudio-sleep/CyDAW2_artefacts/Release/CyDAW 2.exe';s.log=open(folder/'app.log','w',encoding='utf-8');report={'profileSha256Before':hashlib.sha256(personal.read_bytes()).hexdigest(),'fps':args.fps,'playing':args.playing,'instrument':args.instrument,'mode':args.mode,'syntheticServiceEvents':args.mode!='os'};rows=[]
s.process=subprocess.Popen([str(s.exe),'--silent','--hidden',f'--control-port={args.port}','--window-size=1500x900','--language=en'],env=env,stdout=s.log,stderr=subprocess.STDOUT)
try:
 def ready():
  if s.process.poll() is not None:raise RuntimeError('Exited')
  try:return s.q(cmd='ping') or True
  except OSError:return False
 s.wait(ready,60);assert s.q(cmd='status')['speakersSilenced']
 report['audio']=s.q(cmd='audio_settings');report['midiInputs']=s.q(cmd='midi_inputs');assert all(not d['enabled'] for d in report['midiInputs']['devices'])
 print('Audio',json.dumps({k:v for k,v in report['audio'].items() if k!='types'},ensure_ascii=False),flush=True)
 s.q(cmd='new_project');name=s.q(cmd='add_midi_track',name='Latency probe',instrument=args.instrument)['name'];s.q(cmd='arm',track=name,armed=True)
 report['keyboard']=s.q(cmd='computer_keyboard');report['params']=s.q(cmd='list_parameters',track=name);report['status']=s.q(cmd='status')
 if args.playing:s.q(cmd='transport',action='play')
 time.sleep(2)
 h=hwnd_for(s.process.pid) if args.mode=='os' else None
 if args.mode=='os':assert h
 report['controllers']=s.q(cmd='controllers',action='status');assert report['controllers']['nativeInputActive'] if args.mode!='os' else True
 label=next(k['label'] for k in report['keyboard']['keys'] if k['semitone']==0);vk=ord(label.upper());scan=0x1e
 if args.mode=='mapped':
  box=s.q(cmd='controllers',action='add_virtual',name='Direct latency fixture',input='keyboard:computer')['device']
  control=s.q(cmd='controllers',action='add_control',device=box,sourceAddress='key/'+label.upper(),kind='button')['control']
  s.q(cmd='controllers',action='assign',device=box,control=control,target='midi/live/note/1/72')
 for i in range(args.samples+3):
  s.q(cmd='key_latency',arm=True,virtualKey=vk);time.sleep(random.uniform(.02,.12));sent=time.perf_counter_ns()
  stalled=args.stall_ms if args.mode!='os' and i>=args.samples-3 else 0
  if args.mode=='os':assert U.PostMessageW(h,0x100,vk,1|(scan<<16)),C.get_last_error()
  else:
   s.q(cmd='controllers',action='probe_native',delayMs=150 if stalled else 20,scan=scan,virtualKey=vk)
   if stalled:s.q(cmd='key_latency',stallMs=stalled)
  try:v=s.wait(lambda:(lambda p:p if p['outputNs'] else None)(s.q(cmd='key_latency')),3)
  finally:
   if h:U.PostMessageW(h,0x101,vk,1|(scan<<16)|(3<<30))
  assert v['preSignalPeak'] <= 1.0e-5,('Previous sound not silent',v)
  v.update(sentNs=sent,warmup=i<3,uiStallMs=stalled);rows.append(v)
  if i%6==0:print('trial',i,'window->queued',(v['queuedNs']-v['windowNs'])/1e6,'ms; ready',(v['outputNs']-(sent if args.mode=='os' else v['windowNs']))/1e6,'ms',flush=True)
  time.sleep(.8)
 report['rows']=rows;report['passed']=True
 report['summary']={}
 for a,b in [('sentNs','windowNs'),('windowNs','queuedNs'),('queuedNs','collectedNs'),('collectedNs','outputNs'),('sentNs','outputNs'),('windowNs','outputNs')]:
  x=np.array([(v[b]-v[a])/1e6 for v in rows if not v['warmup']]);report['summary'][a+'To'+b]=dict(n=len(x),minimumMs=float(x.min()),medianMs=float(np.median(x)),meanMs=float(x.mean()),p95Ms=float(np.percentile(x,95)),maximumMs=float(x.max()))
 if args.mode!='os':
  # Ignore deliberately scheduled delay between RPC and service entry.
  report['summary'].pop('sentNsTowindowNs',None);report['summary'].pop('sentNsTooutputNs',None)
  for v in rows:
   if v['uiStallMs']:assert v['outputNs']-v['windowNs']<100_000_000,('Audio waited for the stalled UI',v)
 print(json.dumps(report['summary']),flush=True)
except BaseException:report['error']=traceback.format_exc();raise
finally:
 report['shutdown']=s.close();report['profileSha256After']=hashlib.sha256(personal.read_bytes()).hexdigest();assert report['profileSha256After']==report['profileSha256Before'];report['rows']=rows;write_json(out/'report.json',report)
