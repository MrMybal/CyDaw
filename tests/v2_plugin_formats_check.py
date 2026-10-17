"""Isolated --silent DAW checks: JSFX editing/diagnostics/reload/tempo, hosted formats and export projects.
Use --clap / --lv2 / --vst3 to additionally check previously built native fixtures.
"""
import argparse, json, math, os, socket, struct, subprocess, time, uuid, wave
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--exe',required=True,type=Path)
parser.add_argument('--output',required=True,type=Path)
parser.add_argument('--port',type=int,default=47932)
for fmt in ('clap','lv2','vst3'):parser.add_argument('--'+fmt,type=Path)
a=parser.parse_args()
folder=a.output.resolve()/uuid.uuid4().hex
folder.mkdir(parents=True)
locations=folder/'locations.json'
locations.write_text(json.dumps(dict(format='CyDAW Locations',version=1,folders={k:str(folder/k) for k in ('cache','devices','pluginExports','presets','mixes','foundry','banks')})),encoding='utf8')
env=dict(os.environ,CYDAW_NO_RECOVERY='1',CYDAW_LOCATIONS_FILE=str(locations),CYDAW_DJ_COLLECTION_FILE=str(folder/'dj.json'),CYDAW_DJ_PROCESSING_FILE=str(folder/'dj-processing.json'),CYDAW_VIRTUAL_IO_FILE=str(folder/'virtual-io.json'))
info=subprocess.STARTUPINFO() if os.name=='nt' else None
if info:info.dwFlags|=subprocess.STARTF_USESHOWWINDOW;info.wShowWindow=0
process=subprocess.Popen([str(a.exe.resolve()),'--silent',f'--control-port={a.port}','--window-size=1500x900'],env=env,startupinfo=info,stdout=open(folder/'app.log','w'),stderr=subprocess.STDOUT)

def q(**data):
    with socket.create_connection(('127.0.0.1',a.port),timeout=10) as connection:
        connection.settimeout(120);connection.sendall((json.dumps(data)+'\n').encode())
        result=json.loads(connection.makefile('rb').readline())
    assert result.get('ok'),result
    return result['result']

def wait(fn,timeout=15):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=fn()
        if value:return value
        time.sleep(.05)
    raise AssertionError('condition timed out')

def compiled():
    state=q(cmd='jsfx')
    return state if not state['compiling'] else None

def level():
    q(cmd='transport',action='play',position=.2)
    time.sleep(.5)
    # The hidden test window may redraw only when queried. Let its held
    # peak meter decay through enough UI ticks after a six-dB level change.
    # This measures the settled signal rather than the previous held peak.
    for _ in range(12):
        value=q(cmd='status')['meters'][0]['peakDb']
        time.sleep(.04)
    return value

report={}
try:
    for attempt in range(150):
        try:
            state=q(cmd='status');break
        except (OSError,AssertionError):time.sleep(.1)
    else:raise AssertionError('DAW did not start')
    assert state['speakersSilenced']
    source=folder/'tone.wav'
    with wave.open(str(source),'wb') as wav:
        wav.setparams((2,2,48000,0,'NONE','not compressed'))
        wav.writeframes(b''.join(struct.pack('<hh',int(math.sin(i*2*math.pi*440/48000)*8000),int(math.sin(i*2*math.pi*440/48000)*8000)) for i in range(48000*3)))
    q(cmd='add_audio_track',file=str(source))
    dry=level()
    script=folder/'Editable Gain.jsfx'
    initial='desc:JSFX editor test\nslider1:-6<-60,12,0.1>Gain\n@slider\ngain=10^(slider1/20);\n@sample\nspl0*=gain;spl1*=gain;\n'
    script.write_text(initial,encoding='utf8')
    q(cmd='jsfx',action='open',file=str(script))
    q(cmd='jsfx',action='compile');assert not wait(compiled)['error']
    q(cmd='jsfx',action='apply',insert=True);assert not wait(compiled)['error']
    wet=level();assert abs(wet-dry+6)<.6,(dry,wet)
    report['jsfxGainDb']=wet-dry
    # Recompile a working replacement and exercise the shared transport snapshot.
    tempo_script='desc:JSFX editor test\nslider1:-6<-60,12,0.1>Gain\n@block\ngain=tempo/120;\n@sample\nspl0*=gain;spl1*=gain;\n'
    q(cmd='jsfx',action='source',source=tempo_script)
    q(cmd='jsfx',action='apply');state=wait(compiled);assert not state['error'],state
    q(cmd='set_tempo',bpm=60);slow=level()
    q(cmd='set_tempo',bpm=120);normal=level()
    assert abs(normal-dry)<.6 and abs(slow-normal+6.0206)<.6,(dry,slow,normal)
    report['jsfxTempoDb']=slow-normal
    # Invalid text must not replace the file or stop the running version.
    q(cmd='jsfx',action='source',source='desc:Bad syntax\n@sample\nspl0=(1+;')
    q(cmd='jsfx',action='apply');state=wait(compiled)
    assert state['error'] and 'syntax' in state['diagnostics'],state
    assert script.read_text(encoding='utf8')==tempo_script
    assert abs(level()-dry)<.6
    report['failedCompilationPreservesActiveScript']=True
    report['diagnostics']=state['diagnostics']
    q(cmd='snapshot',path=str(folder/'jsfx-editor.png'))
    q(cmd='jsfx',action='open',file=str(script))
    # Load actual exported plugins through the production worker.
    for fmt in ('clap','lv2','vst3'):
        path=getattr(a,fmt)
        if path:
            index=q(cmd='add_plugin',track=0,file=str(path.resolve()))['pluginIndex']
            parameters=q(cmd='list_plugin_parameters',track=0,plugin=index)
            programs=q(cmd='list_plugin_programs',track=0,plugin=index)
            if fmt=='lv2':
                assert len(programs['programs'])>=2,programs
                q(cmd='select_plugin_program',track=0,plugin=index,program=1)
            report[fmt]=dict(plugin=index,programs=programs,parameters=parameters)
    assert level()>-70
    project=folder/'formats.cydaw'
    q(cmd='transport',action='stop')
    q(cmd='save_project',path=str(project))
    q(cmd='open_project',path=str(project))
    wait(lambda:not q(cmd='status').get('opening',False))
    # Build one graph and generate each format through the actual UI-side API.
    q(cmd='workshop',action='new',name='Format Test',instrument=False)
    node=q(cmd='add_node',track='workshop',key='gain')['id']
    q(cmd='disconnect',track='workshop',source=1,destination=2)
    q(cmd='connect',track='workshop',source=1,destination=node)
    q(cmd='connect',track='workshop',source=node,destination=2)
    q(cmd='set_node_parameter',track='workshop',nodeId=node,parameter='gain.db',value=-6)
    q(cmd='workshop',action='programs',op='add',name='Minus 6')
    q(cmd='set_node_parameter',track='workshop',nodeId=node,parameter='gain.db',value=-18)
    q(cmd='workshop',action='programs',op='add',name='Minus 18')
    q(cmd='set_node_parameter',track='workshop',nodeId=node,parameter='gain.db',value=-6)
    exports={}
    for fmt in ('VST3','CLAP','LV2'):
        result=q(cmd='workshop',action='plugin_export',format=fmt,folder=str(folder/fmt),productName='CyFormatTest',pluginCode='FMTS')
        project=Path(result['project']);cmake=(project/'CMakeLists.txt').read_text(encoding='utf8')
        assert f'set(CYDAW_EXPORT_FORMAT {fmt}' in cmake
        assert 'NativeFormats.cmake' in cmake
        definition=(project/'generated/Definition.cpp').read_text(encoding='utf8')
        assert 'Minus 18' in definition and 'Minus 6' in definition
        exports[fmt]=str(project)
    report['projects']=exports
    (folder/'checks.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(str(folder),flush=True)
finally:
    try:q(cmd='quit')
    except OSError:pass
    try:process.wait(timeout=10)
    except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=10)
