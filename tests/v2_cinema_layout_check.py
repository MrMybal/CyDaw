"""Exercise Cinema's real timeline controls, filmstrip, docking and ImGui diagnostics.
Starts an isolated silent instance; requires Pillow and imageio_ffmpeg.
"""
import argparse,json,math,os,socket,struct,subprocess,time,uuid,wave
from pathlib import Path
from PIL import Image
import imageio_ffmpeg
p=argparse.ArgumentParser();p.add_argument('--exe',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--port',type=int,default=48077);a=p.parse_args()
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
def wait(fn,timeout=15):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        v=fn()
        if v:return v
        time.sleep(.05)
    raise AssertionError('condition timed out')
def events(*e):return q(cmd='ui_input',events=list(e)+[dict(type='wait',frames=4)])
def click(x,y):events(dict(type='move',x=x,y=y),dict(type='down'),dict(type='up'))
def windows():return {w['id']:w for w in tab()['windows']}
def screenshot(name):q(cmd='snapshot',path=str(folder/name))
report={}
try:
    for _ in range(150):
        if app.poll() is not None:raise RuntimeError(('app exit',app.returncode))
        try:
            if q(cmd='status'):break
        except OSError:time.sleep(.2)
    q(cmd='app_mode',mode='cinema');c(action='new',name='Cinema timeline and video');c(action='view',resetLayout=True,start=0,seconds=2,length=2)
    video=folder/'frames.mp4'
    subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-y','-loglevel','error','-f','lavfi','-i','testsrc2=size=320x180:rate=25:duration=2','-c:v','libx264','-pix_fmt','yuv420p',str(video)],check=True)
    c(action='video',file=str(video));wait(lambda:tab().get('decoder',{}).get('width')==320)
    wait(lambda:tab()['thumbnailCount']>=10)
    c(action='view',start=.96,seconds=.16)
    needed={24,25,26,27};wait(lambda:needed.issubset(tab()['thumbnailFrames']))
    screenshot('individual-frames.png')
    report['individualFramesAtCloseZoom']=sorted(needed)
    # Scrub the filmstrip, landing on the 25 fps frame grid.
    bounds=tab()['filmstripBounds'];at=1.04
    click(bounds['x']+(at-.96)/.16*bounds['width'],bounds['y']+25)
    assert abs(tab()['cursor']/48000-at)<.0001,tab()['cursor']
    wait(lambda:abs(tab()['decoder']['time']-at)<.021)
    report['filmstripScrubAndPreview']=True
    # The upper ruler band is the native two-axis timeline navigator.
    c(action='view',start=10,seconds=4);events(dict(type='wait',frames=3))
    nav=tab()['navigatorBounds'];assert nav['height']>=12
    x=nav['x']+nav['width']*.4;y=nav['y']+nav['height']*.5
    original_tracks=tab()['tracks'];original_cursor=tab()['cursor']
    def navigate(dx,dy):
        time.sleep(.6) # Distinct gestures, outside the configured double-click interval.
        events(dict(type='move',x=x,y=y),dict(type='down'),dict(type='move',x=x+dx,y=y+dy),dict(type='wait',frames=3),dict(type='up'))
    navigate(70,0);state=tab()
    assert abs(state['viewStart']-(10-70/nav['width']*4))<.001 and abs(state['viewSeconds']-4)<.001,state['viewStart']
    c(action='view',start=10,seconds=4);navigate(0,30);state=tab()
    assert abs(state['viewSeconds']-4/math.exp(.36))<.001,state['viewSeconds']
    assert abs(state['viewStart']+state['viewSeconds']*.4-11.6)<.001
    c(action='view',start=10,seconds=4);navigate(0,-30);assert tab()['viewSeconds']>4
    c(action='view',start=10,seconds=4);navigate(-60,24);state=tab()
    assert abs(state['viewStart']+(.4-60/nav['width'])*state['viewSeconds']-11.6)<.001
    assert state['cursor']==original_cursor and state['tracks']==original_tracks
    # Double-click fits the reference and containers without moving the playhead.
    time.sleep(.6);events(dict(type='move',x=x,y=y),dict(type='down'),dict(type='up'),dict(type='down'),dict(type='up'))
    state=tab();assert state['viewStart']==0 and 2<=state['viewSeconds']<3
    report['navigatorPanZoomAnchorAndDoubleClick']=True
    c(action='view',start=0,seconds=2,length=30);c(action='cursor',at=.1,seconds=True);c(action='play')
    navigate(-40,0);assert tab()['followPaused']
    c(action='view',start=20,seconds=2);events(dict(type='wait',frames=6));assert tab()['viewStart']==20
    c(action='stop');events(dict(type='wait',frames=3));assert not tab()['followPaused']
    report['manualNavigationPausesPlaybackFollowing']=True
    screenshot('double-band-navigator.png')
    # Mouse-wheel zoom must retain the time under the pointer.
    c(action='view',start=0,seconds=2);bounds=tab()['filmstripBounds']
    events(dict(type='move',x=bounds['x']+bounds['width']*.5,y=bounds['y']+25),dict(type='key',key='ctrl',down=True),dict(type='wheel',dy=3),dict(type='key',key='ctrl',down=False))
    state=tab();assert state['viewSeconds']<1.5,state['viewSeconds'];assert abs(state['viewStart']+state['viewSeconds']*.5-1)<.01
    report['anchoredWheelZoom']=True
    # The toolbar uses the same icon sizes/order as Arrangement. Click the real zoom buttons.
    w=windows()['###cinemaTimeline'];before=tab()['viewSeconds']
    click(w['x']+232,w['y']+36);assert abs(tab()['viewSeconds']-before/1.4)<.01
    click(w['x']+208,w['y']+36);assert abs(tab()['viewSeconds']-before)<.01
    report['nativeZoomButtons']=True
    source=folder/'tone.wav'
    with wave.open(str(source),'wb') as f:
        f.setparams((2,2,48000,0,'NONE','none'));f.writeframes(b''.join(struct.pack('<hh',int(6000*math.sin(i*.06)),int(6000*math.sin(i*.06))) for i in range(48000)))
    c(action='import',track=1,file=str(source),at=0);c(action='view',start=0,seconds=2);events(dict(type='wait',frames=3))
    pane=next(p for p in tab()['panes'] if p['track']==1)
    click(pane['timelineX']+pane['timelineWidth']*.25,pane['y']+35)
    c(action='cursor',at=.5,seconds=True,track=1)
    click(w['x']+317,w['y']+36)
    assert len(tab()['tracks'][1]['clips'])==2,tab()['tracks'][1]
    c(action='undo');assert len(tab()['tracks'][1]['clips'])==1
    c(action='redo');assert len(tab()['tracks'][1]['clips'])==2
    report['nativeSplitButtonUndoRedo']=True
    # Move the real video tab to another location in the workspace.
    preview=windows()['###cinemaPreview'];oldDock=preview['dock'];assert oldDock
    events(dict(type='move',x=preview['x']+25,y=preview['y']+10),dict(type='down'),dict(type='key',key='ctrl',down=True),dict(type='move',x=preview['x']+10,y=preview['y']+35),dict(type='move',x=700,y=430),dict(type='wait',frames=5),dict(type='up'),dict(type='key',key='ctrl',down=False))
    detached=windows()['###cinemaPreview'];assert detached['dock']!=oldDock and abs(detached['x']-preview['x'])>200,detached
    screenshot('movable-video.png');report['videoTabRepositionsAndRedocks']=True
    # Restore the workspace and verify the panel can be docked again by its native tab.
    c(action='view',resetLayout=True);events(dict(type='wait',frames=5));assert windows()['###cinemaPreview']['dock']
    events(dict(type='key',key='ctrl',down=True),dict(type='key',key='j'),dict(type='key',key='ctrl',down=False))
    wait(lambda:'###cinemaTools' not in windows());report['bottomPanelHide']=True
    events(dict(type='key',key='ctrl',down=True),dict(type='key',key='j'),dict(type='key',key='ctrl',down=False))
    wait(lambda:'###cinemaTools' in windows());report['bottomPanelRestore']=True
    # Exercise the Studio dock-header hide handles that produced the reported ImGui warning.
    q(cmd='app_mode',mode='studio');events(dict(type='wait',frames=12));screenshot('studio-dock-headers.png')
    q(cmd='app_mode',mode='cinema');events(dict(type='wait',frames=8));state=tab()
    assert state['imguiErrorCount']==0 and '[error]' not in state['imguiErrorLog'].lower(),state['imguiErrorLog']
    report['noImGuiLayoutErrors']=True
    c(action='view',start=0,seconds=2);wait(lambda:tab()['thumbnailCount']>=10);screenshot('cinema-workspace.png')
    (folder/'checks.json').write_text(json.dumps(report,indent=2),encoding='utf8');print('Cinema layout checks passed:',folder,flush=True)
finally:
    try:q(cmd='quit')
    except OSError:pass
    try:app.wait(timeout=10)
    except subprocess.TimeoutExpired:app.terminate();app.wait(timeout=10)
