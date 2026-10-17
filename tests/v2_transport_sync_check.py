"""Transport protocols against isolated silent CyDAW instances, never a user's session.
Usage: python tests/v2_transport_sync_check.py --port 48064 --peer-port 48065 --output output/transport-sync
"""
import argparse,json,math,socket,struct,time,wave
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--port',type=int,required=True);p.add_argument('--peer-port',type=int,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
def q(port=None,**data):
    with socket.create_connection(('127.0.0.1',port or a.port),timeout=10) as s:
        s.settimeout(60);s.sendall((json.dumps(data)+'\n').encode());v=json.loads(s.makefile('rb').readline())
    assert v.get('ok'),v
    return v['result']
def sync(**kw):return q(cmd='transport_sync',**kw)
def cfg(**kw):return sync(action='configure',enabled=False,role='follow',**kw)
def inject(bytes_list,**kw):
    return sync(action='inject',packets=[{'bytes':b,'time':10+i*.02} for i,b in enumerate(bytes_list)],**kw)
def wait(predicate,timeout=8):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=predicate()
        if value:return value
        time.sleep(.08)
    raise AssertionError('condition timed out')
report={}
assert q(cmd='status')['speakersSilenced']
assert q(a.peer_port,cmd='status')['speakersSilenced']
tone=a.output/'transport-tone.wav'
with wave.open(str(tone),'wb') as w:
    w.setparams((2,2,48000,0,'NONE','not compressed'))
    cycle=b''.join(struct.pack('<hh',int(1500*math.sin(i*math.tau*220/48000)),int(1500*math.sin(i*math.tau*220/48000))) for i in range(48000))
    w.writeframes(cycle*90)
q(cmd='add_audio_track',file=str(tone.resolve()))
q(a.peer_port,cmd='add_audio_track',file=str(tone.resolve()))
cfg(protocol='mtc-mmc',target='studio',deviceId=3)
full=[0xf0,0x7f,3,1,1,0x21,2,3,12,0xf7]
s=inject([full])['transport'];assert abs(s['position']-3723.48)<.002,s
assert not s['playing']
# Wrong device is ignored; MMC Locate and Play/Stop use the same receiver.
s=inject([[0xf0,0x7f,4,1,1,0x20,0,9,0,0xf7]])['transport'];assert abs(s['position']-3723.48)<.002,s
s=inject([[0xf0,0x7f,3,6,0x44,6,1,0x20,0,5,0,0,0xf7]])['transport'];assert abs(s['position']-5)<.002,s
s=inject([[0xf0,0x7f,3,6,2,0xf7]])['transport'];assert s['playing'],s
s=inject([[0xf0,0x7f,3,6,1,0xf7]])['transport'];assert not s['playing'],s
# Complete forward quarter-frame sequence; compensate its 2-frame transmission delay.
cfg(protocol='mtc-mmc',target='studio',deviceId=127)
n=[0,0,8,0,0,0,0,2]
s=inject([[0xf1,(i<<4)|x] for i,x in enumerate(n)])['transport']
assert s['playing'] and abs(s['position']-8.08)<.04,s
inject([[0xf0,0x7f,127,6,1,0xf7]])
# Invalid/partial/reversed timecodes must not seek.
q(cmd='transport',position=3)
s=inject([[0xf1,0x70],[0xf1,0x61],[0xf1,0x52],[0xf0,0x7f,127,1,1,0x20,99,0,0,0xf7]])['transport']
assert abs(s['position']-3)<.002,s
report['mtc_mmc']='full frame, QF delay, locate, device filtering, start/stop, malformed frames'
cfg(protocol='midi-clock',target='studio',offsetMs=0)
# 24 clocks per quarter note, 100 BPM.
packets=[{'bytes':[0xfa],'time':10}]+[{'bytes':[0xf8],'time':10+i*.025} for i in range(1,49)]
s=sync(action='inject',packets=packets)['transport']
assert abs(s['bpm']-100)<.01 and s['playing'] and abs(s['position']-1.2)<.05,s
s=inject([[0xfc]])['transport'];assert not s['playing'],s
s=inject([[0xf2,80,0],[0xfb],[0xf8]])['transport'];assert s['playing'] and abs(s['position']-12)<.04,s
inject([[0xfc]])
# Switch off individual received dimensions.
cfg(protocol='midi-clock',target='studio',tempo=False,play=False,position=False)
before=sync()['transport'];s=inject([[0xfa],[0xf2,1,0],[0xfb]])['transport'];assert s==before,(s,before)
report['midi_clock']='tempo 100 BPM, Start/Stop/Continue/SPP, independent switches'
# Fonderie uses its own session and cursor.
q(cmd='foundry',action='open',file=str(tone.resolve()))
cfg(protocol='mtc-mmc',target='foundry',tempo=True,play=True,position=True)
s=inject([[0xf0,0x7f,127,1,1,0x20,0,7,0,0xf7],[0xf0,0x7f,127,6,2,0xf7]])['transport']
assert s['playing'] and abs(s['position']-7)<.1,s
s=inject([[0xf0,0x7f,127,6,1,0xf7]])['transport'];assert not s['playing']
# Deck A uses its beat grid; changing BPM preserves keylock configuration.
q(cmd='dj',action='load',deck='A',file=str(tone.resolve()))
wait(lambda:q(cmd='dj',action='status')['decks'][0]['state']=='ready',20)
q(cmd='dj',action='grid',deck='A',bpm=120,firstBeat=0)
cfg(protocol='midi-clock',target='deck-a',tempo=True,play=True,position=True)
sync(action='inject',packets=packets);s=wait(lambda: (v if (v:=sync()['transport'])['playing'] else None));assert abs(s['bpm']-100)<.2,s
inject([[0xfc]])
report['targets']='Studio, Foundry, Deck A'
# Real driver scheduling on the local Microsoft synth: system messages only, no notes.
ports=sync(action='refresh')
gs=next((d['id'] for d in ports['outputs'] if d['name']=='Microsoft GS Wavetable Synth'),None)
if gs:
    q(cmd='transport',action='stop',position=0);q(cmd='set_tempo',bpm=120)
    sync(action='configure',enabled=True,protocol='midi-clock',target='studio',role='send',output=gs,tempo=True,play=True,position=True)
    sync(action='capture');q(cmd='transport',action='play')
    time.sleep(.55);sent=sync(action='capture')['messages']
    assert any(b in ([0xfa],[0xfb]) for b in sent),sent
    assert 18<=sum(b==[0xf8] for b in sent)<=34,len(sent)
    q(cmd='transport',action='stop');time.sleep(.08);assert [0xfc] in sync(action='capture')['messages']
    sync(action='disable')
    sync(action='configure',enabled=True,protocol='mtc-mmc',target='studio',role='send',output=gs,position=True,play=True,frameRate=1)
    messages=sync(action='capture')['messages'];assert any(len(b)==13 and b[4]==0x44 for b in messages),messages
    q(cmd='transport',action='play');time.sleep(.3);messages=sync(action='capture')['messages']
    assert any(b[:1]==[0xf1] for b in messages),messages
    sync(action='disable');q(cmd='transport',action='stop')
    report['midi_output']='real Windows driver: 24 PPQN, transport, MTC quarter/full frames, MMC locate'
# Link: real peer discovery, BPM bidirectionality, shared start/stop.
for port in [a.port,a.peer_port]:
    q(port,cmd='transport',action='stop',position=0)
    q(port,cmd='transport_sync',action='configure',enabled=True,protocol='link',target='studio',play=True,tempo=True,position=True,quantum=1)
wait(lambda:sync()['peers']>=1 and q(a.peer_port,cmd='transport_sync')['peers']>=1,15)
q(cmd='set_tempo',bpm=137)
wait(lambda:abs(q(a.peer_port,cmd='transport_sync')['transport']['bpm']-137)<.02)
q(a.peer_port,cmd='set_tempo',bpm=113)
wait(lambda:abs(sync()['transport']['bpm']-113)<.02)
q(cmd='transport',action='play')
wait(lambda:sync()['transport']['playing'] and q(a.peer_port,cmd='transport_sync')['transport']['playing'])
time.sleep(.5)
x=sync()['transport'];y=q(a.peer_port,cmd='transport_sync')['transport']
phase=(x['position']-y['position'])*113/60
assert abs(phase-round(phase))<.15,(x,y)
q(a.peer_port,cmd='transport',action='stop')
wait(lambda:not sync()['transport']['playing'])
for port in [a.port,a.peer_port]:q(port,cmd='transport_sync',action='disable')
report['link']='real UDP peers, tempo 137/113 bidirectional, quantum launch, phase, stop'
# Missing dependencies and invalid config must fail explicitly.
with socket.create_connection(('127.0.0.1',a.port),timeout=10) as sock:
    sock.sendall(b'{"cmd":"transport_sync","action":"configure","protocol":"jack","enabled":true,"jackLibrary":"Z:/missing-jack.dll"}\n')
    assert not json.loads(sock.makefile('rb').readline())['ok']
assert not sync()['enabled']
q(cmd='transport_sync',show=True)
q(cmd='snapshot',path=str((a.output/'panel-tested.png').resolve()))
(a.output/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
