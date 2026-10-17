"""Portable native-device round trip through two independent CyDAW instances."""
import argparse, copy, json, os, shutil, socket, subprocess, time, uuid
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--exe', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--port', type=int, default=47977)
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
        folders={k:str(here/k) for k in ('cache','devices','pluginExports','presets','mixes','foundry','banks')})), encoding='utf8')
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
    start('sender')
    w('new', name='Transfer synth effect', instrument=False)
    node = q(cmd='add_node', track='workshop', key='clean-channel')['id']
    q(cmd='disconnect', track='workshop', source=1, destination=2)
    q(cmd='connect', track='workshop', source=1, destination=node)
    q(cmd='connect', track='workshop', source=node, destination=2)
    node = q(cmd='create_device', track='workshop', nodeIds=[node], name='Nested wrapper')['nodeId']
    # Preserve a user-owned nested graph, including its own preset bank.
    w('enter', nodeId=node)
    w('programs', op='add', name='Nested default', defaults=True)
    w('leave')
    w('programs', op='add', name='Init', defaults=True)
    scene = w('presentation')
    binding = scene['parameters'][0]['key']
    source = w('component')['source']
    scene.update(designed=True, deviceDesigned=True, windowWidth=940, windowHeight=610,
        deviceWidth=450, deviceHeight=210, nextControlId=120)
    scene['controls'] = [dict(id=100,kind='custom',label='Portable C++',binding=binding,
        x=.05,y=.1,width=.4,height=.8,design=dict(customSource=source))]
    scene['deviceControls'] = [dict(id=110,kind='knob',label='Compact',binding=binding,
        x=.1,y=.1,width=.4,height=.8,design=dict(accent='ffff8800'))]
    w('presentation', state=scene)
    before = w('status')
    exported = w('device_export', file=str(folder/'sender'/'shared.cydawdevice'))
    assert not exported['externalFiles'], exported
    after = w('status')
    assert before['unsaved'] == after['unsaved'] and before.get('file') == after.get('file')
    doc = json.loads(Path(exported['written']).read_text(encoding='utf-8-sig'))
    graph = doc['graph']
    assert doc['portableGraph'] and len(graph['modules']) >= 2
    assert all(m['inline'] for m in graph['modules'])
    assert len(graph['modules']) < 30, 'exported the entire unused factory library'
    # Move the native file alone, then remove its source installation data.
    receiver_file = folder/'Transferred.cydawdevice'
    shutil.copyfile(exported['written'], receiver_file)
    stop()
    shutil.rmtree(folder/'sender')
    start('receiver')
    assert not q(cmd='list_my_devices')['devices']
    opened = w('open', file=str(receiver_file))
    assert opened['name'] == 'Transfer synth effect' and not opened['instrument']
    restored = w('presentation')
    assert restored['windowWidth'] == 940 and restored['windowHeight'] == 610
    assert restored['deviceWidth'] == 450 and restored['deviceHeight'] == 210
    assert restored['controls'][0]['design']['customSource'] == source
    assert len(restored['deviceControls']) == 1
    assert [v['name'] for v in w('programs')['programs']] == ['Init']
    w('enter', nodeId=node)
    assert [v['name'] for v in w('programs')['programs']] == ['Nested default']
    w('leave')
    w('simulate', source='sine', play=True)
    time.sleep(.8)
    assert w('status')['simulator']['levelDb'] > -65
    w('simulate', play=False)
    # Instruments use the same transfer path and retain their MIDI/audio role.
    effect_tab = opened['activeTab']
    w('new', name='Portable synth', instrument=True)
    synth = w('device_export', file=str(folder/'portable-synth.cydawdevice'))
    synth_tab = w('status')['activeTab']
    w('close_tab', id=synth_tab)
    synth_opened = w('open', file=synth['written'])
    assert synth_opened['instrument'] and synth_opened['inputChannels'] == 0
    w('simulate', pattern='held', play=True)
    time.sleep(.8)
    assert w('status')['simulator']['levelDb'] > -65
    w('simulate', play=False)
    w('close_tab', id=synth_opened['activeTab'])
    w('tab', id=effect_tab)
    # Normal library save retains the imported inline definitions.
    saved = w('save', asNew=True)
    saved_doc = json.loads(Path(saved['written']).read_text(encoding='utf-8-sig'))
    assert all(any(s['key']==m['key'] for s in saved_doc['graph']['modules']) for m in graph['modules'])
    q(cmd='snapshot', path=str(folder/'workshop.png'))
    # Unsupported graph/document versions are rejected without replacing the tab.
    for change in ('document', 'graph', 'missing'):
        bad = copy.deepcopy(doc)
        if change == 'document': bad['schema'] = 999
        elif change == 'graph': bad['graph']['schema'] = 999
        else: bad.pop('graph')
        file = folder / f'bad-{change}.cydawdevice'
        file.write_text(json.dumps(bad), encoding='utf8')
        assert not request(cmd='workshop',action='open',file=str(file))['ok']
        assert w('status')['activeTab'] == opened['activeTab']
    report = dict(freshInstance=True, libraryIndependent=True, fullAndCompactInterfaces=True,
        cppSource=True, rootAndNestedPresets=True, graphAudio=True, synthAudio=True, rejectsFutureVersions=True,
        exportLeavesWorkingTabUnchanged=True, modules=len(graph['modules']), file=str(receiver_file))
    (folder/'checks.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(folder,flush=True)
finally:
    stop()
