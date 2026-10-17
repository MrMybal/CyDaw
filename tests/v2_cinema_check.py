"""Cinema integration checks on an isolated --silent instance. Requires Pillow and ffmpeg.
Verifies audio, buses, MIDI, automation, persistence, UI editing, video and MTC/MMC.
"""
import argparse,json,math,socket,struct,subprocess,time,uuid,wave
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--port',type=int,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--ffmpeg',required=True);a=p.parse_args()
folder=a.output.resolve()/uuid.uuid4().hex;folder.mkdir(parents=True)
def q(**data):
    with socket.create_connection(('127.0.0.1',a.port),timeout=10) as s:
        s.settimeout(120);s.sendall((json.dumps(data)+'\n').encode());v=json.loads(s.makefile('rb').readline())
    assert v.get('ok'),v
    return v['result']
def c(**data):return q(cmd='cinema',**data)
def tab(v=None):
    v=v or c(action='status');return v['tabs'][v['current']]
def wait(fn,timeout=12):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        v=fn()
        if v:return v
        time.sleep(.06)
    raise AssertionError('condition timed out')
def events(*items):return q(cmd='ui_input',events=list(items)+[dict(type='wait',frames=3)])
def move(x,y):events(dict(type='move',x=x,y=y))
def drag(x,y,tox,toy):
    move(x,y);events(dict(type='down'),dict(type='move',x=x+10,y=y),dict(type='move',x=tox,y=toy),dict(type='wait',frames=4),dict(type='up'))
def export(name,**kw):
    path=folder/(name+'.wav');c(action='export',path=str(path),bits=16,**kw)
    with wave.open(str(path),'rb') as w:
        n=w.getnframes();channels=w.getnchannels();size=w.getsampwidth();raw=w.readframes(n)
    return [int.from_bytes(raw[i:i+size],'little',signed=True)/2**(size*8-1) for i in range(0,len(raw),size*channels)]
def rms(v):return math.sqrt(sum(x*x for x in v)/max(1,len(v)))
def db(v,ref):return 20*math.log10(rms(v)/rms(ref))
def binding(state,inputId):
    nodes=next(t for t in state['tracks'] if t['inputId']==inputId)['graph']['nodes'] if inputId else state['master']['nodes']
    node=next(n for n in nodes if n['type']=='gain');return f"node.{node['id']}.gain.db"
report={}
assert q(cmd='status')['speakersSilenced'],'Use an isolated silent instance'
before=q(cmd='status')['audioTracks']
q(cmd='app_mode',mode='cinema');t=tab(c(action='new',name='Cinema integration'))
group=next(t['id'] for t in t['tracks'] if t['virtualGroup'])
assert t['cinema'] and t['tracks'][1]['parentId']==group
c(action='group',op='rename',id=group,name='Audio bus');assert tab()['tracks'][0]['name']=='Audio bus'
c(action='container',op='rename',id=t['tracks'][1]['id'],name='First container');assert tab()['tracks'][1]['name']=='First container'
c(action='add_subtrack',groupId=group,type='audio',name='Audio 2');c(action='view',start=0,seconds=3,length=1,snap=True)
events(dict(type='wait',frames=5));panes=tab()['panes']
assert len(panes)==2 and abs(panes[0]['timelineX']-panes[1]['timelineX'])<1 and panes[1]['y']>panes[0]['y']+panes[0]['height'],panes
report['arrangementBusRowsAndCommonTimeline']=True
def tx(p,seconds):return p['timelineX']+(seconds-tab()['viewStart'])/tab()['viewSeconds']*p['timelineWidth']
source=folder/'tone.wav'
with wave.open(str(source),'wb') as w:
    w.setparams((2,2,48000,0,'NONE','none'));w.writeframes(b''.join(struct.pack('<hh',int(4000*math.sin(i*.131)),int(4000*math.sin(i*.131))) for i in range(48000)))
c(action='import',track=1,file=str(source),at=0);c(action='import',track=2,file=str(source),at=0)
dry=export('two-inputs');assert rms(dry)>.1
bus=next(b for b in tab()['buses'] if b['id']==group)
move(bus['x']-182,bus['y']+42);events(dict(type='down'),dict(type='up'));assert tab()['tracks'][0]['mute']
assert max(map(abs,export('ui-muted-bus')))<.0001
events(dict(type='down'),dict(type='up'));assert not tab()['tracks'][0]['mute']
c(action='processing',op='mix',inputId=group,gain=-6);quiet=export('group-gain');delta=db(quiet,dry);assert abs(delta+6)<.15,delta
state=c(action='processing',op='add_effect',inputId=group,key='gain');c(action='processing',op='parameter',inputId=group,binding=binding(state,group),value=-6)
wet=export('group-effect');effect=db(wet,quiet);assert abs(effect+6)<.15,effect
c(action='automation',track=-1,lanes=[dict(binding='',points=[dict(frame=0,value=-6)])]);master=export('master-automation');assert abs(db(master,wet)+6)<.15
c(action='track',track=0,mute=True);assert max(map(abs,export('muted-bus')))<.0001
c(action='track',track=0,mute=False)
c(action='automation',track=0,lanes=[dict(binding='',points=[dict(frame=0,value=-6)])]);assert abs(db(export('bus-automation'),master)+6)<.15
c(action='automation',track=0,lanes=[])
c(action='cursor',at=0);c(action='play');time.sleep(.15);live=c(action='status');assert live['playing'] and abs(live['outputPeakDb']-20*math.log10(max(map(abs,master))))<.5,live;c(action='stop')
report.update(groupGainDb=delta,groupNativeEffectDb=effect,masterAutomation=True,realtimePlaying=True)
c(action='add_subtrack',groupId=group,type='midi',name='Synth');t=tab(c(action='midi_clip',track=3,at=0));cid=t['tracks'][3]['clips'][0]['id']
c(action='group',op='select',id=group);events(dict(type='wait',frames=4));assert tab()['toolsPanel']==5
c(action='clip',id=cid,length=48000);c(action='notes',id=cid,notes=[dict(start=0,length=18000,key=60,velocity=.7),dict(start=24000,length=18000,key=67,velocity=.6)])
c(action='track',track=1,mute=True);c(action='track',track=2,mute=True)
midi=export('native-midi');assert max(map(abs,midi))>.001
c(action='automation',track=3,lanes=[dict(binding='',points=[dict(frame=0,value=-12)])]);assert abs(db(export('midi-automation'),midi)+12)<.2
report['nativeMidiAutomation']=True
c(action='remove_track',track=3);c(action='track',track=1,mute=False);c(action='track',track=2,mute=False);c(action='clip',id=tab()['tracks'][2]['clips'][0]['id'],delete=True)
events(dict(type='wait',frames=5));t=tab();p1,p2=t['panes'];cid=t['tracks'][1]['clips'][0]['id']
x1=tx(p1,.36);y1=p1['y']+p1['height']*.5;x2=tx(p2,.36);y2=p2['y']+p2['height']*.5
q(cmd="snapshot",path=str(folder/"before-drag.png"))
drag(x1,y1,x2,y2);q(cmd="snapshot",path=str(folder/"after-drag.png"));t=tab();assert not t['tracks'][1]['clips'] and t['tracks'][2]['clips'][0]['id']==cid,('UI drag',t['tracks'])
clip=t['tracks'][2]['clips'][0];p2=t['panes'][1];right=tx(p2,(clip['start']+clip['length'])/48000)
drag(right-2,y2,right-p2['timelineWidth']*.14,y2);trimmed=tab()['tracks'][2]['clips'][0]['length'];assert 20000<trimmed<39000,trimmed
c(action='undo');assert tab()['tracks'][2]['clips'][0]['length']==48000
c(action='redo');assert tab()['tracks'][2]['clips'][0]['length']==trimmed
events(dict(type='file_drop',files=[str(source)],x=x1,y=y1));assert len(tab()['tracks'][1]['clips'])==1
report['uiDragTrimUndoFileDrop']=True;q(cmd='snapshot',path=str(folder/'horizontal-tracks.png'))
video=folder/'reference.mp4'
subprocess.run([a.ffmpeg,'-y','-loglevel','error','-f','lavfi','-i','color=c=red:s=640x360:r=25:d=1,drawbox=x=0:y=180:w=640:h=180:color=green:t=fill','-f','lavfi','-i','color=c=blue:s=640x360:r=25:d=1,drawbox=x=0:y=180:w=640:h=180:color=yellow:t=fill','-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=2','-filter_complex','[0:v][1:v]concat=n=2:v=1:a=0[v]','-map','[v]','-map','2:a','-c:v','libx264','-pix_fmt','yuv420p','-c:a','aac',str(video)],check=True)
c(action='video',file=str(video));wait(lambda:tab().get('decoder',{}).get('width')==640)
for second,upper,lower in [(0,'red','green'),(.96,'red','green'),(1,'blue','yellow'),(.2,'red','green')]:
    c(action='cursor',at=second,seconds=True);wait(lambda:abs(tab()['decoder']['time']-second)<.022)
    name=folder/f'video-{second}.png';q(cmd='snapshot',path=str(name));im=Image.open(name).convert('RGB')
    def iscolor(rgb,col):
        r,g,b=rgb
        return {'red':r>170 and g<60 and b<60,'green':g>75 and r<60 and b<60,'blue':b>170 and r<60 and g<60,'yellow':r>170 and g>170 and b<60}[col]
    points={col:[(x,y) for y in range(160,600,4) for x in range(im.width-400,im.width-25,4) if iscolor(im.getpixel((x,y)),col)] for col in [upper,lower]}
    assert all(len(v)>300 for v in points.values()),(second,{k:len(v) for k,v in points.items()})
    assert sum(y for x,y in points[upper])/len(points[upper])<sum(y for x,y in points[lower])/len(points[lower]),'Video orientation'
report['videoFrameBoundaryAndOrientation']=True
wait(lambda:any(t['name']=='Guide vid\u00e9o' and t['clips'] for t in tab()['tracks']))
guide=next(t for t in tab()['tracks'] if t['name']=='Guide vid\u00e9o');video_bus=next(t for t in tab()['tracks'] if t['id']==guide['parentId'])
assert video_bus['bus'] and video_bus['virtualGroup'] and 96000<=guide['clips'][0]['length']<100000
state=c(action='processing');native_bus=next(t for t in state['tracks'] if t['inputId']==video_bus['id']);native_guide=next(t for t in state['tracks'] if t['inputId']==guide['id'])
assert native_guide['outputs'][0]['destination']==native_bus['key'] and native_bus['outputs'][0]['destination']==0
report['automaticWholeSoundtrackBus']=True
# The decoded soundtrack is a normal editable audio clip: split it, drag the
# second piece into another child and confirm the rendered mix stays identical.
original_soundtrack=export('whole-soundtrack')
c(action='add_subtrack',groupId=video_bus['id'],type='audio',name='Soundtrack edits')
t=tab();guide_index=next(i for i,tr in enumerate(t['tracks']) if tr['id']==guide['id']);destination=len(t['tracks'])-1
c(action='cursor',at=.6,seconds=True,track=guide_index);c(action='edit',op='split');t=tab()
pieces=t['tracks'][guide_index]['clips'];assert len(pieces)==2 and sum(p['length'] for p in pieces)==guide['clips'][0]['length']
second=next(p for p in pieces if p['start']>0);events(dict(type='wait',frames=4));t=tab();panes={p['track']:p for p in t['panes']};src,dst=panes[guide_index],panes[destination]
drag(tx(src,.84),src['y']+40,tx(dst,.84),dst['y']+40)
t=tab();assert len(t['tracks'][guide_index]['clips'])==1 and t['tracks'][destination]['clips'][0]['id']==second['id']
reassembled=export('split-and-moved-soundtrack');assert len(reassembled)==len(original_soundtrack) and max(abs(x-y) for x,y in zip(reassembled,original_soundtrack))<.0001
report['soundtrackSplitAndUiMove']=True

session=folder/'cinema.cycinema';c(action='save_session',path=str(session));expected=export('saved');saved=tab();assert not saved['dirty']
c(action='close');opened=tab(c(action='open',file=str(session)));assert opened['cinema'] and not opened['dirty']
assert [(t['id'],t['parentId'],t['virtualGroup'],t['containerStart'],t['containerLength']) for t in opened['tracks']]==[(t['id'],t['parentId'],t['virtualGroup'],t['containerStart'],t['containerLength']) for t in saved['tracks']]
assert opened['picture']['file']==str(video)
reopened=export('reopened');assert len(expected)==len(reopened) and max(abs(x-y) for x,y in zip(expected,reopened))<.0001
q(cmd='transport_sync',action='configure',enabled=False,protocol='mtc-mmc',target='cinema',role='follow',deviceId=127,position=True,play=True,tempo=True)
def inject(b):return q(cmd='transport_sync',action='inject',packets=[dict(bytes=b,time=10)])['transport']
s=inject([0xf0,0x7f,127,1,1,0x20,0,1,0,0xf7]);assert abs(s['position']-1)<.002,s
s=inject([0xf0,0x7f,127,6,2,0xf7]);assert s['playing'],s
s=inject([0xf0,0x7f,127,6,1,0xf7]);assert not s['playing'],s
q(cmd='transport_sync',action='disable');report['cinemaMtcMmc']=True
state=c(action='processing',op='bus',name='Mixer bus');assert any(t['name']=='Mixer bus' and t['virtualGroup'] for t in tab()['tracks'])
c(action='video',file=str(video),start=.24,sourceIn=.12,duration=1.5);c(action='save_session',path=str(session));c(action='close');video.rename(folder/'moved-reference.mp4')
opened=tab(c(action='open',file=str(session)));wait(lambda:tab().get('decoder',{}).get('error'));assert opened['tracks'] and rms(export('missing-video'))>0
# A silent reference does not create an empty soundtrack bus.
silent=folder/'silent.mp4';subprocess.run([a.ffmpeg,'-y','-loglevel','error','-i',str(folder/'moved-reference.mp4'),'-an','-c:v','copy',str(silent)],check=True)
c(action='new',name='Silent reference');c(action='video',file=str(silent));wait(lambda:tab().get('decoder',{}).get('width')==640);time.sleep(.4)
assert len(tab()['tracks'])==2,tab()['tracks']
report['silentVideoNoExtraBus']=True
c(action='new',name='Timed subtrack containers');t=tab();group=t['activeGroup']
c(action='view',start=0,seconds=10,length=0)
t=tab(c(action='add_subtrack',groupId=group,type='audio',name='Scene later',at=4,length=2,seconds=True));index=len(t['tracks'])-1;tid=t['tracks'][index]['id']
c(action='import',track=index,file=str(source),at=4,seconds=True);events(dict(type='wait',frames=4));t=tab();panes={p['track']:p for p in t['panes']}
assert abs(panes[1]['y']-panes[index]['y'])<1 and panes[index]['x']>panes[1]['x']+panes[1]['width']
pane=panes[index];original_clip=t['tracks'][index]['clips'][0];before_move=export('container-before-move')
# Drag the enclosing frame by its title bar, moving all contents together.
drag(tx(pane,4.5),pane['y']-12,tx(pane,6.5),pane['y']-12)
t=tab();assert abs(t['tracks'][index]['containerStart']/48000-6)<.042,t['tracks'][index]
assert t['tracks'][index]['clips'][0]['start']-original_clip['start']==t['tracks'][index]['containerStart']-192000
after_move=export('container-after-move');assert max(abs(x-y) for x,y in zip(before_move[192000:240000],after_move[288000:336000]))<.0001
c(action='undo');assert tab()['tracks'][index]['containerStart']==192000
c(action='redo');assert abs(tab()['tracks'][index]['containerStart']/48000-6)<.042
# Click the actual bus header: its native tool strip gains the Subtracks tab.
events(dict(type='wait',frames=4));bus=next(b for b in tab()['buses'] if b['id']==group)
move(bus['x']-100,bus['y']+14);events(dict(type='down'),dict(type='up'));assert tab()['toolsPanel']==5
# A real Focus button must recover a container far outside the displayed range.
c(action='container',op='move',id=tid,at=90,seconds=True);events(dict(type='wait',frames=5))
button=next(b for b in tab()['subtrackFocusButtons'] if b['id']==tid)
move(button['x']+button['width']/2,button['y']+button['height']/2);events(dict(type='down'),dict(type='up'));events(dict(type='wait',frames=5))
t=tab();assert t['viewStart']<90<t['viewStart']+t['viewSeconds'] and index in t['pickedTracks'],t
assert any(p['track']==index for p in t['panes'])
q(cmd='snapshot',path=str(folder/'arrangement-containers-and-list.png'))
report['timedContainerMoveUndoAndListFocus']=True
# Create a container with the same double-click gesture as creating a clip.
c(action='view',start=0,seconds=10);events(dict(type='wait',frames=4));bus=next(b for b in tab()['buses'] if b['id']==group);before_count=len(tab()['tracks'])
x=bus['x']+bus['width']*.45;y=bus['y']+bus['height']-12
move(x,y);events(dict(type='down'),dict(type='up'),dict(type='down'),dict(type='up'))
assert len(tab()['tracks'])==before_count+1
report['doubleClickCreatesContainer']=True
# Changing bus membership and undo must change the actual main output as well.
t=tab();original_bus=group;other=tab(c(action='group',op='add',name='Other bus'))['activeGroup']
c(action='assign',trackId=tid,groupId=other)
def destination(input_id):return next(tr for tr in c(action='processing')['tracks'] if tr['inputId']==input_id)['outputs'][0]['destination']
def key(input_id):return next(tr for tr in c(action='processing')['tracks'] if tr['inputId']==input_id)['key']
assert destination(tid)==key(other)
c(action='undo');assert destination(tid)==key(original_bus)
c(action='redo');assert destination(tid)==key(other)
report['containerBusAssignmentUndo']=True
saved_containers=folder/'timed-containers.cycinema';c(action='save_session',path=str(saved_containers));c(action='close');t=tab(c(action='open',file=str(saved_containers)))
assert next(tr for tr in t['tracks'] if tr['id']==tid)['containerStart']==4320000
# Sessions from the first Cinema version still load, retaining their clips.
legacy=json.loads(saved_containers.read_text(encoding='utf-8-sig'));legacy['version']=4
for tr in legacy['tracks']:tr.pop('containerStart',None);tr.pop('containerLength',None)
legacy_file=folder/'legacy-cinema.cycinema';legacy_file.write_text(json.dumps(legacy),encoding='utf8')
c(action='close');t=tab(c(action='open',file=str(legacy_file)))
assert next(tr for tr in t['tracks'] if tr['id']==tid)['clips'][0]['start']==4320000
report['legacyCinemaSessionRetained']=True


report.update(persistence=True,missingVideoRecoverable=True,studioUntouched=q(cmd='status')['audioTracks']==before);assert report['studioUntouched']
(folder/'checks.json').write_text(json.dumps(report,indent=2),encoding='utf8');print('Cinema checks passed:',folder,flush=True)
