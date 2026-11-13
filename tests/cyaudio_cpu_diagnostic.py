"""Windows per-thread CPU and QA stage timings. No dependency edits or physical input.
--mode null opens ONLY the QA-only Core null provider. --mode asio uses the previously
accepted output-only silent Fireface setup; never infer hardware results from null.
"""
from __future__ import annotations
import argparse,hashlib,csv,ctypes as C,json,os,socket,statistics,subprocess,time
from ctypes import wintypes as W
from pathlib import Path
from cyaudio_comparison import Session,profile,assets,descendants,process_sample,write_json,K
ROOT=Path(__file__).resolve().parents[1]
class ThreadEntry(C.Structure):
 _fields_=[('size',W.DWORD),('usage',W.DWORD),('tid',W.DWORD),('pid',W.DWORD),('base',W.LONG),('delta',W.LONG),('flags',W.DWORD)]
K.Thread32First.argtypes=K.Thread32Next.argtypes=[W.HANDLE,C.POINTER(ThreadEntry)]
K.OpenThread.argtypes=[W.DWORD,W.BOOL,W.DWORD];K.OpenThread.restype=W.HANDLE
K.QueryThreadCycleTime.argtypes=[W.HANDLE,C.POINTER(C.c_uint64)]
K.QueryProcessCycleTime.argtypes=[W.HANDLE,C.POINTER(C.c_uint64)]
K.GetThreadTimes.argtypes=[W.HANDLE]+[C.POINTER(W.FILETIME)]*4
K.GetThreadDescription.argtypes=[W.HANDLE,C.POINTER(W.LPWSTR)]
K.LocalFree.argtypes=[W.HANDLE];K.LocalFree.restype=W.HANDLE
K.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)]
K.GetSystemTimes.argtypes=[C.POINTER(W.FILETIME)]*3
ft=lambda x:((x.dwHighDateTime<<32)+x.dwLowDateTime)*1e-7

def threads(pids):
 result={};h=K.CreateToolhelp32Snapshot(4,0)
 if h in (None,C.c_void_p(-1).value):raise C.WinError(C.get_last_error())
 try:
  e=ThreadEntry();e.size=C.sizeof(e);ok=K.Thread32First(h,C.byref(e))
  while ok:
   if e.pid in pids:
    t=K.OpenThread(0x800,False,e.tid)
    if t:
     try:
      v=[W.FILETIME() for _ in range(4)];name=W.LPWSTR()
      if K.GetThreadTimes(t,*(C.byref(x) for x in v)):
       if K.GetThreadDescription(t,C.byref(name))==0 and name:label=name.value;K.LocalFree(C.cast(name,W.HANDLE))
       else:label=''
       cycles=C.c_uint64();K.QueryThreadCycleTime(t,C.byref(cycles))
       result[e.tid]=dict(cycles=cycles.value,pid=e.pid,cpu=ft(v[2])+ft(v[3]),kernel=ft(v[2]),user=ft(v[3]),name=label,created=ft(v[0]))
     finally:K.CloseHandle(t)
   ok=K.Thread32Next(h,C.byref(e))
 finally:K.CloseHandle(h)
 return result

def ui_threads(pid):
 u=C.WinDLL('user32',use_last_error=True);found=set()
 callbackType=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
 u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
 @callbackType
 def visit(hwnd,_):
  owner=W.DWORD();tid=u.GetWindowThreadProcessId(hwnd,C.byref(owner))
  if owner.value==pid:found.add(tid)
  return True
 u.EnumWindows.argtypes=[callbackType,W.LPARAM];u.EnumWindows(visit,0)
 u.OpenDesktopW.argtypes=[W.LPCWSTR,W.DWORD,W.BOOL,W.DWORD];u.OpenDesktopW.restype=W.HANDLE
 u.EnumDesktopWindows.argtypes=[W.HANDLE,callbackType,W.LPARAM];u.CloseDesktop.argtypes=[W.HANDLE]
 desktop=u.OpenDesktopW('CyDAW-automation',0,False,0x41)
 if desktop:
  try:u.EnumDesktopWindows(desktop,visit,0)
  finally:u.CloseDesktop(desktop)
 return sorted(found)

def system():
 v=[W.FILETIME() for _ in range(3)];assert K.GetSystemTimes(*(C.byref(x) for x in v));return [ft(x) for x in v]
def process_cycles(pid):
 h=K.OpenProcess(0x1000,False,pid);value=C.c_uint64()
 try:
  if h:K.QueryProcessCycleTime(h,C.byref(value))
  return value.value
 finally:
  if h:K.CloseHandle(h)

def image(pid):
 h=K.OpenProcess(0x1000,False,pid);s=C.create_unicode_buffer(32768);n=W.DWORD(32768)
 try:return Path(s.value).name if h and K.QueryFullProcessImageNameW(h,0,s,C.byref(n)) else 'exited/unknown'
 finally:
  if h:K.CloseHandle(h)

class CpuSession(Session):
 def __init__(self,exe,folder,port,mode,instrumented=False,ui='normal'):
  self.exe=exe;self.folder=folder;self.port=port;folder.mkdir(parents=True,exist_ok=True)
  env=profile(folder,None)
  if mode=='null':
   env['CYDAW_QA_NULL_AUDIO']='1'
   # The QA binary exposes only this provider, so startup cannot open hardware.
   for f in (Path(env['APPDATA'])/'CyDAW').glob('CyDAW.*.settings'):
    s=f.read_text(encoding='utf-8');s=s.replace('ASIO Fireface USB','').replace('ASIO','CyDAW QA Null');f.write_text(s,encoding='utf-8')
  if instrumented:env['CYDAW_QA_CPU_TRACE']=str(folder/'scopes.csv')
  if ui=='off':env['CYDAW_QA_NO_FRAME']='1'
  if ui=='cap30':env['CYDAW_QA_FRAME_HZ']='30'
  self.log=open(folder/'app.log','w',encoding='utf-8');self.commands=open(folder/'commands.jsonl','w',encoding='utf-8')
  self.process=subprocess.Popen([str(exe),'--silent','--hidden',f'--control-port={port}','--window-size=1500x900','--language=en'],env=env,stdout=self.log,stderr=subprocess.STDOUT)
  try:
   self.wait(lambda:self.ready(),60);assert self.q(cmd='status')['speakersSilenced']
   settings=self.q(cmd='audio_settings');write_json(folder/'initial-audio.json',settings)
   if mode=='asio':
    self.q(cmd='device_channels',inputs=0,outputs=2)
    self.audio=self.q(cmd='set_audio_device',type='ASIO',output='ASIO Fireface USB',input='',sampleRate=48000,bufferSize=1024)
    assert self.audio['type']=='ASIO' and not self.audio['inputChannels']
   else:
    self.audio=self.q(cmd='set_audio_device',type='CyDAW QA Null',sampleRate=48000,bufferSize=1024,input='')
    assert self.audio['type']=='CyDAW QA Null',self.audio
   assert self.audio['sampleRate']==48000 and self.audio['bufferSize']==1024,self.audio
   write_json(folder/'audio.json',self.audio)
  except BaseException:self.close();raise
 def ready(self):
  if self.process.poll() is not None:raise RuntimeError('Application exited at startup')
  try:self.q(cmd='ping');return True
  except (OSError,ValueError):return False
 def q(self,**data):
  if hasattr(self,'commands'):self.commands.write(json.dumps(dict(at=time.perf_counter(),request=data))+'\n');self.commands.flush()
  return super().q(**data)
 def close(self):
  r=super().close()
  if hasattr(self,'commands'):self.commands.close()
  return r
 def measure(self,seconds=8,poll=.5):
  time.sleep(2)
  pids=descendants(self.process.pid);first=process_sample(self.process.pid);cycles0={pid:process_cycles(pid) for pid in pids};t0=threads(pids);sys0=system();begin=time.perf_counter();samples=[]
  initial=self.q(cmd='status')
  while time.perf_counter()-begin<seconds:
   time.sleep(poll);samples.append(self.q(cmd='status'))
  end=time.perf_counter();last=process_sample(self.process.pid);t1=threads(descendants(self.process.pid));sys1=system();elapsed=end-begin
  rows=[]
  for tid,v in t1.items():
   old=t0.get(tid)
   if old and old['created']==v['created']:
    rows.append(dict(cycles=max(0,v['cycles']-old['cycles']),tid=tid,pid=v['pid'],name=v['name'],cpuPercent=max(0,v['cpu']-old['cpu'])/elapsed*100,kernelPercent=max(0,v['kernel']-old['kernel'])/elapsed*100,userPercent=max(0,v['user']-old['user'])/elapsed*100))
  processes=[dict(pid=pid,image=image(pid),cycles=max(0,process_cycles(pid)-cycles0.get(pid,0)),cpuPercent=max(0,v['cpu']-first.get(pid,v)['cpu'])/elapsed*100) for pid,v in last.items()]
  cpu=sum(x['cpuPercent'] for x in processes);total=(sys1[1]-sys0[1])+(sys1[2]-sys0[2]);busy=total-(sys1[0]-sys0[0])
  return dict(uiThreadIds=ui_threads(self.process.pid),beginQpc=begin,endQpc=end,seconds=elapsed,parentCpu=sum(x['cpuPercent'] for x in processes if x['pid']==self.process.pid),workerCpu=sum(x['cpuPercent'] for x in processes if x['image']=='CyDAWPluginWorker.exe'),cpuPercent=cpu,systemBusyPercent=100*busy/total if total else None,systemOtherCorePercent=max(0,busy/elapsed*100-cpu),threads=sorted(rows,key=lambda x:x['cpuPercent'],reverse=True),processes=processes,positionDelta=samples[-1].get('position',0)-initial.get('position',0),lastStatus=samples[-1])

def scope_deltas(file,case,sites):
 if not file.is_file():return []
 snapshots={}
 with open(file,encoding='utf-8') as f:
  for row in csv.DictReader(f):
   at=float(row['qpcSeconds']);snapshots.setdefault(at,{})[(int(row['tid']),int(row['site']))]=[int(row['calls']),float(row['inclusiveSeconds']),float(row['exclusiveSeconds'])]
 times=sorted(t for t in snapshots if case['beginQpc']<=t<=case['endQpc'])
 if len(times)<2:return []
 start,end=times[0],times[-1];result=[]
 for (tid,site),v in snapshots[end].items():
  old=snapshots[start].get((tid,site),[0,0,0]);delta=[a-b for a,b in zip(v,old)]
  if delta[0]>0:
   result.append(dict(tid=tid,site=site,name=sites[site]['name'],calls=delta[0],callsPerSecond=delta[0]/(end-start),inclusivePercent=delta[1]/(end-start)*100,exclusivePercent=delta[2]/(end-start)*100,meanInclusiveUs=delta[1]/delta[0]*1e6,windowSeconds=end-start))
 return sorted(result,key=lambda x:x['exclusivePercent'],reverse=True)

def source_fingerprint(out):
 h=hashlib.sha256()
 for directory in [out/'frozen',out/'qa-sources',ROOT/'src',ROOT/'include']:
  for f in sorted(directory.rglob('*')):
   if f.is_file() and f.suffix.lower() in ['.cpp','.h','.hpp','.c','.inl']:
    h.update(str(f.relative_to(directory)).encode());h.update(b'\0');h.update(f.read_bytes())
 return h.hexdigest()

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--mode',choices=['null','asio'],default='null');p.add_argument('--rounds',type=int,default=3);p.add_argument('--seconds',type=float,default=8);p.add_argument('--variants',nargs='+');p.add_argument('--tag');p.add_argument('--ui',choices=['normal','cap30','off'],default='normal');p.add_argument('--cases',nargs='+',default=['idle','idle8','playing8','dj4','audio32','effects16']);a=p.parse_args();out=a.output.resolve()
 assert out.is_relative_to(ROOT/'output')
 variants={'null':{'native-null':out/'null-uninstrumented/CyDAW 2.exe','native-instrumented':out/'instrumented/CyDAW 2.exe'},'asio':{'JUCE':ROOT/'build/juce-comparison/CyDAW2_artefacts/Release/CyDAW 2.exe','native':ROOT/'build/cyaudio-native-desktop/CyDAW2_artefacts/Release/CyDAW 2.exe','native-instrumented':out/'instrumented/CyDAW 2.exe'}}[a.mode]
 if a.variants:variants={k:v for k,v in variants.items() if k in a.variants}
 asset=assets(out/'assets');manifest=json.loads((out/'build-manifest.json').read_text(encoding='utf-8'));sites=manifest['sites'];rows=[];tag=a.tag or (a.mode+('-'+a.ui if a.ui!='normal' else ''));assert all(c.isalnum() or c in '-_' for c in tag);results=out/(tag+'-results.json')
 for iteration in range(a.rounds):
  order=list(variants)
  if iteration%2:order.reverse()
  for label in order:
   before=source_fingerprint(out)
   folder=out/tag/f'{iteration+1}-{label}';s=CpuSession(variants[label],folder,48221,a.mode,'instrumented' in label,a.ui);r=dict(label=label,ui=a.ui,round=iteration+1,audio=s.audio,cases={});rows.append(r)
   try:
    s.reset()
    if 'idle' in a.cases:r['cases']['idle']=s.measure(a.seconds)
    if any(x in a.cases for x in ['idle8','playing8']):
     for _ in range(8):
      t=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['name'];s.q(cmd='add_plugin',track=t,file=str(ROOT/'build/cyaudio-desktop/CyDAWTestGain_artefacts/Release/VST3/CyDAW Test Gain.vst3'))
     if 'idle8' in a.cases:r['cases']['idle8']=s.measure(a.seconds)
     assert sum(x['image']=='CyDAWPluginWorker.exe' for x in r['cases'].get('idle8',{}).get('processes',[]))==8 if 'idle8' in a.cases else True
     if 'playing8' in a.cases:
      s.q(cmd='transport',action='play',position=.1);r['cases']['playing8']=s.measure(a.seconds);assert r['cases']['playing8']['positionDelta']>a.seconds*.6;s.q(cmd='transport',action='stop')
    s.reset();s.wait(lambda:len(descendants(s.process.pid))==1)
    for name,n in [('audio32',32),('effects16',16)]:
     if name not in a.cases:continue
     s.reset()
     for _ in range(n):
      t=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['name']
      if name=='effects16':
       for key in ['comp-pro','chorus','reverb-pro']:s.q(cmd='insert_device',track=t,key=key)
     s.q(cmd='transport',action='play',position=.1);r['cases'][name]=s.measure(a.seconds);assert r['cases'][name]['positionDelta']>a.seconds*.6;s.q(cmd='transport',action='stop')
    s.reset();s.wait(lambda:len(descendants(s.process.pid))==1)
    if 'dj4' in a.cases:
     s.q(cmd='app_mode',mode='dj');s.q(cmd='dj',action='mixer',decks=4,autoGain=False)
     for d in 'ABCD':s.q(cmd='dj',action='load',deck=d,file=str(asset/'long.wav'))
     s.wait(lambda:all(d.get('analysed') for d in s.q(cmd='dj')['decks']),60)
     for d in 'ABCD':s.q(cmd='dj',action='channel',deck=d,fader=.7);s.q(cmd='dj',action='play',deck=d,on=True)
     r['cases']['dj4']=s.measure(a.seconds);r['djState']=s.q(cmd='dj');assert all(d['playing'] for d in r['djState']['decks']);assert r['cases']['dj4']['workerCpu']==0 and len(r['cases']['dj4']['processes'])==1
     for d in 'ABCD':s.q(cmd='dj',action='play',deck=d,on=False)
   finally:
    r['shutdown']=s.close()
    after=source_fingerprint(out);r['sourceStability']=dict(before=before,after=after,stable=before==after);assert before==after,'Campaign sources changed'
    for case in r['cases'].values():case['scopes']=scope_deltas(folder/'scopes.csv',case,sites)
    write_json(results,rows)
   print(a.mode,iteration+1,label,{k:round(v['parentCpu'],2) for k,v in r['cases'].items()},flush=True)
 print('Results:',results,flush=True)
if __name__=='__main__':main()
