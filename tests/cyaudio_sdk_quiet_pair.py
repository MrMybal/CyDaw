"""Run the unchanged ASIO scenes with bounded quiet-machine checks and noise retries.
All rejected windows remain attached to the accepted/provisional result. No process
is interrupted, no system/antivirus setting is modified. Only the owned test
transport is repositioned before each measured attempt.
"""
import runpy,sys,time,json
from pathlib import Path
from cyaudio_cpu_diagnostic import CpuSession,system
from cyaudio_comparison import process_sample
original=CpuSession.measure

def other_cpu(session,seconds=1):
 start=system();first=process_sample(session.process.pid);begin=time.perf_counter()
 time.sleep(seconds)
 end=system();last=process_sample(session.process.pid);elapsed=time.perf_counter()-begin
 busy=(end[1]-start[1])+(end[2]-start[2])-(end[0]-start[0])
 own=sum(max(0,v['cpu']-first.get(pid,v)['cpu']) for pid,v in last.items())
 return max(0,(busy-own)/elapsed*100)

def quiet_measure(self,seconds=8,poll=.5):
 rejected=[];quiet=[]
 for attempt in range(3):
  deadline=time.perf_counter()+30;consecutive=0
  while consecutive<2 and time.perf_counter()<deadline:
   load=other_cpu(self);quiet.append(load);consecutive=consecutive+1 if load<=150 else 0
  # The deterministic 40-beat patterns must not expire during a noise retry.
  self.q(cmd='transport',action='play',position=.1)
  value=original(self,seconds,poll)
  value['noiseAccepted']=value['systemOtherCorePercent']<=200
  with (self.folder/'noise-windows.jsonl').open('a',encoding='utf-8') as evidence:
   evidence.write(json.dumps(dict(attempt=attempt+1,window=value))+'\n')
  print('ASIO window', self.exe.parent.name, 'attempt', attempt+1, 'other cores', round(value['systemOtherCorePercent']/100,2), 'accepted',value['noiseAccepted'],flush=True)
  if value['noiseAccepted']:break
  rejected.append(value)
 if not value['noiseAccepted']:
  raise RuntimeError('External CPU stayed above the acceptance limit; see noise-windows.jsonl. No final comparison is accepted.')
 value['quietCheckCorePercent']=quiet
 value['noiseRejectedWindows']=rejected if value['noiseAccepted'] else rejected[:-1]
 return value

CpuSession.measure=quiet_measure
runpy.run_path(str(Path(__file__).with_name('cyaudio_complete_pair.py')),run_name='__main__')
