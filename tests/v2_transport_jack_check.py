"""JACK transport adapter contract against TransportJackFixture.cpp, no JACK/audio server needed."""
import argparse,ctypes,json,socket,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--port',type=int,required=True);p.add_argument('--library',type=Path,required=True);a=p.parse_args()
dll=ctypes.CDLL(str(a.library.resolve()))
dll.qa_set.argtypes=[ctypes.c_double,ctypes.c_int,ctypes.c_double,ctypes.c_int];dll.qa_number.argtypes=[ctypes.c_int];dll.qa_number.restype=ctypes.c_double
def raw(**kw):
 with socket.create_connection(('127.0.0.1',a.port),timeout=5) as s:
  s.settimeout(30);s.sendall((json.dumps(kw)+'\n').encode());return json.loads(s.makefile('rb').readline())
def q(**kw):
 v=raw(**kw);assert v['ok'],v;return v['result']
def sync(**kw):return q(cmd='transport_sync',**kw)
def wait(f):
 end=time.monotonic()+5
 while time.monotonic()<end:
  if f():return
  time.sleep(.04)
 raise AssertionError('timed out')
sync(action='disable')
q(cmd='transport',action='stop',position=0)
dll.qa_set(9,0,75,4);dll.qa_occupied(0)
s=sync(action='configure',enabled=True,protocol='jack',jackLibrary=str(a.library.resolve()),target='studio',role='follow',tempo=True,play=True,position=True)
wait(lambda:abs(sync()['transport']['position']-9)<.005)
wait(lambda:abs(sync()['transport']['bpm']-75)<.005)
assert dll.qa_number(6)==1 # no automatic server startup
dll.qa_set(12,1,180,8) # eighth-note BPM converted to quarter-note BPM
wait(lambda:sync()['transport']['playing'] and abs(sync()['transport']['bpm']-90)<.005)
assert 12<=sync()['transport']['position']<14
dll.qa_set(20,0,120,4)
wait(lambda:not sync()['transport']['playing'] and abs(sync()['transport']['position']-20)<.005 and abs(sync()['transport']['bpm']-120)<.005)
dll.qa_shutdown();wait(lambda:not sync()['connected'])
assert sync()['error']
sync(action='disable')
# Sender: locate/start/stop and timebase ownership.
q(cmd='set_tempo',bpm=133,numerator=4,denominator=4)
dll.qa_set(0,0,120,4)
sync(action='configure',enabled=True,protocol='jack',jackLibrary=str(a.library.resolve()),role='send')
q(cmd='transport',position=5)
wait(lambda:abs(dll.qa_number(0)-5)<.1)
q(cmd='transport',action='play')
wait(lambda:dll.qa_number(1)==1)
wait(lambda:abs(dll.qa_number(2)-133)<.005)
q(cmd='transport',action='stop')
wait(lambda:dll.qa_number(1)==0)
sync(action='disable')
dll.qa_occupied(1)
v=raw(cmd='transport_sync',action='configure',enabled=True,protocol='jack',role='send')
assert not v['ok'] and not sync()['enabled'],v
dll.qa_occupied(0)
print('JACK contract passed: receive, send, locate, start/stop, BBT tempo conversion, shutdown, existing master protection, no audio device change.')
