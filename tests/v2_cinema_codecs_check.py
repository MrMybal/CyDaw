"""Isolated FFmpeg codec and subtrack creation checks against a compiled CyDAW.
Uses imageio_ffmpeg only to produce fixtures, never for the DAW's decoding.
"""
import argparse,json,math,os,socket,struct,subprocess,time,uuid,wave
from pathlib import Path
import imageio_ffmpeg
p=argparse.ArgumentParser();p.add_argument('--exe',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--port',type=int,default=48078);a=p.parse_args()
folder=a.output.resolve()/uuid.uuid4().hex;folder.mkdir(parents=True)
locations=folder/'locations.json';locations.write_text(json.dumps(dict(format='CyDAW Locations',version=1,folders={k:str(folder/k) for k in ('cache','devices','pluginExports','presets','mixes','foundry','banks')})),encoding='utf8')
env=dict(os.environ,CYDAW_NO_RECOVERY='1',CYDAW_SINGLE_VIEWPORT='1',CYDAW_LOCATIONS_FILE=str(locations),CYDAW_DJ_COLLECTION_FILE=str(folder/'dj.json'),CYDAW_DJ_PROCESSING_FILE=str(folder/'dj-processing.json'))
info=subprocess.STARTUPINFO();info.dwFlags|=subprocess.STARTF_USESHOWWINDOW;info.wShowWindow=0
app=subprocess.Popen([str(a.exe.resolve()),'--silent',f'--control-port={a.port}','--window-size=1800x1100'],env=env,startupinfo=info,stdout=open(folder/'app.log','w'),stderr=subprocess.STDOUT)
def q(**data):
    with socket.create_connection(('127.0.0.1',a.port),timeout=5) as s:
        s.settimeout(120);s.sendall((json.dumps(data)+'\n').encode());v=json.loads(s.makefile('rb').readline())
    assert v.get('ok'),v
    return v['result']
def c(**data):return q(cmd='cinema',**data)
def tab():
    v=c(action='status');return v['tabs'][v['current']]
def wait(fn,timeout=20):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        v=fn()
        if v:return v
        time.sleep(.05)
    raise AssertionError('condition timed out: '+json.dumps(tab().get('decoder',{})))
def events(*e):return q(cmd='ui_input',events=list(e)+[dict(type='wait',frames=4)])
def click(x,y,button=0):events(dict(type='move',x=x,y=y),dict(type='down',button=button),dict(type='up',button=button))
def screenshot(name):q(cmd='snapshot',path=str(folder/name))
report={}
try:
    for _ in range(150):
        if app.poll() is not None:raise RuntimeError(('app exit',app.returncode))
        try:
            if q(cmd='status'):break
        except OSError:time.sleep(.2)
    assert q(cmd='status')['speakersSilenced']
    q(cmd='app_mode',mode='cinema')
    ffmpeg=imageio_ffmpeg.get_ffmpeg_exe()
    fixtures=[
        ('h264-aac.mp4',['-c:v','libx264','-pix_fmt','yuv420p'],['-c:a','aac'],'h264'),
        ('hevc-eac3-5.1.mkv',['-c:v','libx265','-x265-params','log-level=error:pools=1:frame-threads=1','-pix_fmt','yuv420p10le'],['-c:a','eac3','-ac','6'],'hevc'),
        ('vp9-opus.webm',['-c:v','libvpx-vp9','-deadline','realtime','-cpu-used','8','-pix_fmt','yuv420p'],['-c:a','libopus'],'vp9'),
        ('av1-flac.mkv',['-c:v','libaom-av1','-cpu-used','8','-row-mt','1','-pix_fmt','yuv420p'],['-c:a','flac'],'av1'),
        ('prores-pcm.mov',['-c:v','prores_ks','-profile:v','0','-pix_fmt','yuv422p10le'],['-c:a','pcm_s24le'],'prores'),
        ('dnxhr-pcm.mov',['-c:v','dnxhd','-profile:v','dnxhr_lb','-pix_fmt','yuv422p'],['-c:a','pcm_s16le'],'dnxhd'),
        ('dnxhr-pcm.mxf',['-c:v','dnxhd','-profile:v','dnxhr_lb','-pix_fmt','yuv422p'],['-c:a','pcm_s16le','-ar','48000'],'dnxhd'),
    ]
    for name,videoArgs,audioArgs,codec in fixtures:
        video=folder/name
        audio='aevalsrc='+ '|'.join(['0.05*sin(2*PI*440*t)']*6)+':s=48000:d=2' if '5.1' in name else 'sine=frequency=440:sample_rate=44100:duration=2'
        run=subprocess.run([ffmpeg,'-y','-loglevel','error','-f','lavfi','-i','testsrc2=size=320x180:rate=25:duration=2','-f','lavfi','-i',audio,*videoArgs,*audioArgs,'-shortest',str(video)],capture_output=True,text=True)
        assert run.returncode==0,run.stderr
        c(action='new',name=name);c(action='view',start=0,seconds=2,length=2);c(action='open' if name.endswith('.mxf') else 'video',file=str(video))
        wait(lambda:tab().get('decoder',{}).get('width')==320)
        state=tab()['decoder'];assert state['backend']=='FFmpeg' and state['codec']==codec and not state['error'],state
        c(action='cursor',at=1.2,seconds=True);wait(lambda:abs(tab()['decoder']['time']-1.2)<.022)
        c(action='cursor',at=.2,seconds=True);wait(lambda:abs(tab()['decoder']['time']-.2)<.022)
        wait(lambda:tab()['thumbnailCount']>=8)
        guide=wait(lambda:next((t for t in tab()['tracks'] if t['name']=='Guide vid\u00e9o' and t['clips']),None))
        clip=guide['clips'][0];assert 95000<=clip['length']<=101000,(name,clip['length'])
        output=folder/(name+'.wav');c(action='export',path=str(output),bits=16)
        with wave.open(str(output),'rb') as f:
            assert f.getnchannels()==2 and f.getframerate()==48000
            data=struct.unpack('<'+'h'*f.getnframes()*2,f.readframes(f.getnframes()))
        rms=math.sqrt(sum(float(x)*x for x in data)/len(data))/32768
        assert rms>.005,(name,rms)
        report[name]=dict(codec=codec,backend='FFmpeg',audioFrames=clip['length'],rms=rms,seekForwardBackward=True,filmstrip=True)
        print('Codec passed:',name,flush=True)
        c(action='close')
    broken=folder/'broken.mkv';broken.write_bytes(b'This is not a media stream')
    c(action='new',name='Invalid reference');c(action='video',file=str(broken));wait(lambda:tab().get('decoder',{}).get('error'))
    assert 'FFmpeg' in tab()['decoder']['error'];c(action='close');report['invalidReferenceRecoverable']=True
    # Real double-click on an empty MIDI subtrack, then its context menu.
    c(action='new',name='Subtrack creation');state=tab();bus=state['activeGroup']
    c(action='add_subtrack',groupId=bus,type='midi',name='Instrument',at=3,length=3,seconds=True)
    c(action='view',start=0,seconds=10,length=12);events(dict(type='wait',frames=5))
    state=tab();index=len(state['tracks'])-1
    pane=next(p for p in state['panes'] if p['track']==index);x=pane['timelineX']+pane['timelineWidth']*.32;y=pane['y']+35
    events(dict(type='move',x=x,y=y),dict(type='down'),dict(type='up'),dict(type='down'),dict(type='up'))
    state=tab();assert len(state['tracks'][index]['clips'])==1 and state['toolsPanel']==4,state
    assert abs(state['tracks'][index]['clips'][0]['start']/48000-3.2)<.042
    time.sleep(.6);pane=next(p for p in tab()['panes'] if p['track']==index);x=pane['timelineX']+pane['timelineWidth']*.56;y=pane['y']+40
    click(x,y,1);screenshot('midi-subtrack-menu.png');click(x+75,y+11)
    assert len(tab()['tracks'][index]['clips'])==2,tab()['tracks'][index]
    c(action='undo');assert len(tab()['tracks'][index]['clips'])==1
    c(action='redo');assert len(tab()['tracks'][index]['clips'])==2
    report['midiDoubleClickAndContextCreateUndoRedo']=True
    # The audio lane offers an empty PCM clip and importing into that exact lane.
    c(action='view',start=0,seconds=10);events(dict(type='wait',frames=4))
    pane=next(p for p in tab()['panes'] if p['track']==1);x=pane['timelineX']+pane['timelineWidth']*.1;y=pane['y']+35
    click(x,y,1);screenshot('audio-subtrack-menu.png');click(x+90,y+11)
    assert len(tab()['tracks'][1]['clips'])==1 and tab()['tracks'][1]['clips'][0]['length']>0,tab()['tracks'][1]
    c(action='undo');assert not tab()['tracks'][1]['clips'];c(action='redo');assert len(tab()['tracks'][1]['clips'])==1
    state=tab();assert not state['imguiErrorCount'] and '[error]' not in state['imguiErrorLog'].lower(),state['imguiErrorLog']
    report['audioContextCreateUndoRedoAndNoLayoutErrors']=True
    screenshot('subtrack-clips.png')
    (folder/'checks.json').write_text(json.dumps(report,indent=2),encoding='utf8');print('Cinema codec/subtrack checks passed:',folder,flush=True)
finally:
    try:q(cmd='quit')
    except OSError:pass
    try:app.wait(timeout=10)
    except subprocess.TimeoutExpired:app.terminate();app.wait(timeout=10)
