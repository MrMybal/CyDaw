"""Local six-target export through the real application, without a runner."""
import argparse, copy, json, os, shutil, socket, subprocess, time, uuid
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--exe', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--port', type=int, default=47978)
p.add_argument('--cache', type=Path)
a = p.parse_args()
folder = a.output.resolve() / uuid.uuid4().hex
folder.mkdir(parents=True)
process = None
log = None

def request(**data):
    with socket.create_connection(('127.0.0.1', a.port), timeout=10) as sock:
        sock.settimeout(120)
        sock.sendall((json.dumps(data) + '\n').encode())
        return json.loads(sock.makefile('rb').readline())

def q(**data):
    value = request(**data)
    assert value.get('ok'), value
    return value['result']

def w(action, **data):
    return q(cmd='workshop', action=action, **data)

def stop():
    global process, log
    if process:
        try: q(cmd='quit')
        except OSError: pass
        try: process.wait(timeout=15)
        except subprocess.TimeoutExpired: process.terminate(); process.wait(timeout=10)
        process = None
    if log: log.close(); log = None

def start(name):
    global process, log
    here = folder / name
    here.mkdir()
    locations = here / 'locations.json'
    locations.write_text(json.dumps(dict(format='CyDAW Locations', version=1,
        folders={k:str(a.cache.resolve() if k=='cache' and a.cache else here/k) for k in ('cache','devices','pluginExports','presets','mixes','foundry','banks')})), encoding='utf8')
    env = dict(os.environ, CYDAW_NO_RECOVERY='1', CYDAW_SINGLE_VIEWPORT='1',
        CYDAW_LOCATIONS_FILE=str(locations), CYDAW_DJ_COLLECTION_FILE=str(here/'dj.json'),
        CYDAW_DJ_PROCESSING_FILE=str(here/'processing.json'), CYDAW_VIRTUAL_IO_FILE=str(here/'io.json'))
    info = subprocess.STARTUPINFO() if os.name == 'nt' else None
    if info: info.dwFlags |= subprocess.STARTF_USESHOWWINDOW; info.wShowWindow = 0
    log = open(here/'app.log', 'w')
    process = subprocess.Popen([str(a.exe.resolve()), '--silent', f'--control-port={a.port}',
        '--window-size=1800x1100'], env=env, startupinfo=info, stdout=log, stderr=subprocess.STDOUT)
    for _ in range(250):
        try:
            if q(cmd='status')['speakersSilenced']: return
        except (OSError, AssertionError): pass
        time.sleep(.1)
    raise AssertionError('DAW did not start')

try:
    start('batch')
    w('new',name='Cross platform gain',instrument=False)
    node=q(cmd='add_node',track='workshop',key='gain')['id']
    q(cmd='disconnect',track='workshop',source=1,destination=2)
    q(cmd='connect',track='workshop',source=1,destination=node)
    q(cmd='connect',track='workshop',source=node,destination=2)
    q(cmd='set_node_parameter',track='workshop',nodeId=node,parameter='gain.db',value=-6)
    w('programs',op='add',name='Minus 6')
    q(cmd='set_node_parameter',track='workshop',nodeId=node,parameter='gain.db',value=-18)
    w('programs',op='add',name='Minus 18')
    # Compile all six targets through the same API used by the workshop UI.
    targets=w('plugin_export_batch',formats=['VST3','CLAP','LV2'],platforms=['Windows','Linux'],
        folder=str(folder/'exports'),productName='CyCrossGain',pluginCode='CXGN',sourceRoot=str(Path.cwd()),build=True)
    assert len(targets['targets'])==6
    end=time.monotonic()+1800
    last=None
    while time.monotonic()<end:
        state=w('vst3_status',logLines=50)
        (folder/'progress.json').write_text(json.dumps(state,indent=2),encoding='utf8')
        stamp=(state['phase'],len(state['targets']))
        if stamp!=last: print(stamp,flush=True);last=stamp
        if state['phase'] in ('done','failed','cancelled'): break
        time.sleep(1)
    else: raise AssertionError('batch build timeout')
    assert state['phase']=='done',state
    assert len(state['targets'])==6
    artifacts={}
    for target in state['targets']:
        assert not target['error'],target
        file=Path(target['file']);assert file.exists(),target
        binary=file if target['format']=='CLAP' else next(p for p in file.rglob('*') if p.is_file() and p.open('rb').read(4)[:2] in (b'MZ',b'\x7fE'))
        data=binary.read_bytes()
        assert (data[:2]==b'MZ' if target['platform']=='Windows' else data[:4]==b'\x7fELF')
        if target['platform']=='Linux':
            if target['format']=='VST3': assert binary.parent.name=='x86_64-linux',binary
            import struct
            assert data[4:6]==bytes([2,1]) and struct.unpack_from('<H',data,18)[0]==62
            assert b'juce_' not in data and b'JUCE v' not in data
            expected = {'VST3': (b'GetPluginFactory\0',b'ModuleEntry\0',b'ModuleExit\0'),
                'CLAP':(b'clap_entry\0',),'LV2':(b'lv2_descriptor\0',b'lv2ui_descriptor\0')}[target['format']]
            assert all(name in data for name in expected),(binary,expected)
            phoff=struct.unpack_from('<Q',data,32)[0];phsize,phnum=struct.unpack_from('<HH',data,54)
            headers=[struct.unpack_from('<IIQQQQQQ',data,phoff+i*phsize) for i in range(phnum)]
            dynamic=next(p for p in headers if p[0]==2)
            tags=[struct.unpack_from('<qQ',data,i) for i in range(dynamic[2],dynamic[2]+dynamic[5],16)]
            assert not [item for item in tags if item[0] in (15,29)],'Build-host path in RPATH'
        if target['format']=='LV2':
            manifest=(file/'manifest.ttl').read_text(encoding='utf8')
            metadata=(file/'plugin.ttl').read_text(encoding='utf8')
            assert ('cydaw_native.dll' if target['platform']=='Windows' else 'cydaw_native.so') in manifest
            assert 'Minus 18' in metadata and 'Minus 6' in metadata
            assert ('WindowsUI' if target['platform']=='Windows' else 'X11UI') in metadata
        artifacts[target['platform']+'/'+target['format']]=str(file)
    q(cmd='snapshot',path=str(folder/'batch.png'))
    (folder/'checks.json').write_text(json.dumps(dict(batch=True,artifacts=artifacts,platforms=['Windows x64','Linux x64 glibc 2.36'],
        linuxRuntimeValidated=False),indent=2),encoding='utf8')
    print(folder,flush=True)
finally:
    stop()
