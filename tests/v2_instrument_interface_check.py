"""Native instrument section authoring, independent dimensions and portable export."""
import argparse,json,socket,uuid
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--port',type=int,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def request(**c):
 with socket.create_connection(('127.0.0.1',a.port),timeout=10) as s:
  s.settimeout(120);s.sendall((json.dumps(c)+'\n').encode());return json.loads(s.makefile('rb').readline())
def need(**c):
 r=request(**c);assert r.get('ok'),r;return r['result']
def w(action,**kw):return need(cmd='workshop',action=action,**kw)
models=['ambre','colosse','mosaique','nebuleuse','opale','onyx','tirette','alliage','quartz','argile','saphir','derive','bronze','silex','jade','prism','kick']
counts={}
for name in models:
 w('new',name=name+' designer',instrument=False)
 node=need(cmd='add_node',track='workshop',key=name+'-synth')['id']
 scene=w('instrument_interface',nodeId=node)
 panels=[c for c in scene['controls'] if c.get('design',{}).get('instrumentType')==name+'-synth']
 assert panels and len(panels)>=3,(name,len(panels))
 for c in panels:
  assert len(c['extraBindings'])==len(c['design']['instrumentKeys'])
  assert len(set(c['design']['instrumentKeys']))==len(c['design']['instrumentKeys'])
  assert c['x']+c['width']<=1.00001 and c['y']+c['height']<=1.00001
 assert scene['windowWidth']==1280 and scene['windowHeight']==900
 card=w('instrument_interface',nodeId=node,compact=True)
 assert card['deviceDesigned'] and card['deviceControls']
 assert len(card['controls'])==len(scene['controls'])
 assert card['windowWidth']==1280 and card['windowHeight']==900 and card['deviceWidth']==1200 and card['deviceHeight']==300
 counts[name]=dict(full=len(panels),card=len(card['deviceControls']))
 if name=='ambre':
  card.update(deviceWidth=640,deviceHeight=220,deviceDesigned=True);w('presentation',state=card)
  chosen=w('instrument_interface',nodeId=node,compact=True)
  assert chosen['deviceWidth']==640 and chosen['deviceHeight']==220

# A native section can also be authored on a custom graph with no synth node.
w('new',name='Custom filter face',instrument=False)
filter_id=need(cmd='add_node',track='workshop',key='lowpass')['id']
need(cmd='disconnect',track='workshop',source=1,destination=2)
need(cmd='connect',track='workshop',source=1,destination=filter_id)
need(cmd='connect',track='workshop',source=filter_id,destination=2)
scene=w('instrument_interface',type='opale-synth',panel='filter')
section=next(c for c in scene['controls'] if c.get('design',{}).get('instrumentType'))
assert not section.get('extraBindings',[]) and section['design']['instrumentKeys']==[]
cutoff=next(p['key'] for p in scene['parameters'] if p['key'].endswith('lowpass.cutoff'))
resonance=next(p['key'] for p in scene['parameters'] if p['key'].endswith('lowpass.resonance'))
section['design']['instrumentKeys']=['f.cutoff','f.resonance'];section['extraBindings']=[cutoff,resonance]
section['label']='MY FILTER';section['design']['background']='ff202d30'
scene.update(windowWidth=900,windowHeight=560);w('presentation',state=scene)
export=w('vst3_export',folder=str((a.output/'NativeCustomFilterUI').resolve()),productName='CyFilterUI',pluginCode='NFUI')
cpp=(Path(export['project'])/'generated/Definition.cpp').read_text(encoding='utf8')
assert 'c.instrumentType="opale-synth"' in cpp and 'MY FILTER' in cpp and 'd.width=900' in cpp

# A connected synthesis instrument can export the full interface as native C++.
w('new',name='Opale native sections',instrument=False)
w('remove_boundary',nodeId=1)
node=need(cmd='add_node',track='workshop',key='opale-synth')['id']
midi=need(cmd='add_node',track='workshop',key='graph-midi-input')['id']
allocator=need(cmd='add_node',track='workshop',key='voice-allocator')['id']
need(cmd='connect',track='workshop',source=midi,destination=allocator)
need(cmd='connect',track='workshop',source=allocator,sourcePort=2,destination=node,destinationPort=2)
envelope=need(cmd='add_node',track='workshop',key='envelope')['id']
gain=need(cmd='add_node',track='workshop',key='gain')['id']
filter_node=need(cmd='add_node',track='workshop',key='voice-filter')['id']
need(cmd='connect',track='workshop',source=allocator,sourcePort=3,destination=envelope,destinationPort=1)
need(cmd='connect',track='workshop',source=envelope,sourcePort=2,destination=gain,destinationPort=2)
need(cmd='connect',track='workshop',source=node,destination=gain)
need(cmd='connect',track='workshop',source=gain,destination=filter_node)
need(cmd='connect',track='workshop',source=filter_node,destination=2)
scene=w('instrument_interface',nodeId=node)
scene.update(windowWidth=1440,windowHeight=960)
w('presentation',state=scene)
# A section can bind to a different parameter through its stable key mapping.
section=next(c for c in scene['controls'] if c.get('label')=='AMP ENV')
bind_index=next((i for i,k in enumerate(section['design']['instrumentKeys']) if k=='amp.attack'),0)
replacement=next((p['key'] for p in scene['parameters'] if p['key'].endswith('.fenv.attack')),section['extraBindings'][bind_index])
section['extraBindings'][bind_index]=replacement
w('presentation',state=scene)
bad=dict(scene);bad['controls']=[dict(section,design=dict(section['design'],instrumentKeys=[]))]
assert not request(cmd='workshop',action='presentation',state=bad).get('ok')
# Reopen before exporting: authored panels, keys, bindings and dimensions persist.
state=w('port_exposure',nodeId=node,portId=1,exposed=True)
state['graphRole']='instrument'
f=(a.output/('opale-native-interface-'+uuid.uuid4().hex[:8]+'.cydawdevice')).resolve();f.write_text(json.dumps(dict(format='CyDAW Device',schema=1,role='instrument',name='Opale native sections',graph=state)),encoding='utf8')
opened=w('open',file=str(f));w('save');w('close_tab',id=opened['activeTab']);w('open',file=str(f))
restored=w('presentation');assert restored['windowWidth']==1440 and restored['windowHeight']==960
assert next(c for c in restored['controls'] if c.get('label')=='AMP ENV')['extraBindings'][bind_index]==replacement
check=w('vst3_check');assert check['ready'],check
runtime=w('runtime_state');assert len(runtime['allParameters'])>=len(section['extraBindings'])
assert all(any(p['key']==binding for p in runtime['allParameters']) for binding in section['extraBindings'])
export=w('vst3_export',folder=str((a.output/'NativeOpaleUI').resolve()),productName='CyOpaleUI',pluginCode='NOUI')
cpp=(Path(export['project'])/'generated/Definition.cpp').read_text(encoding='utf8')
assert 'c.instrumentType="opale-synth"' in cpp and 'c.instrumentKeys.push_back(' in cpp
assert 'd.width=1440' in cpp and 'd.height=960' in cpp
(a.output/'checks.json').write_text(json.dumps(dict(models=counts,savedBindings=True,independentSizes=True,nativeCode=True,libraryOnCustomGraph=True),indent=2),encoding='utf8')
print('Native instrument interfaces: all 17 synthesis instruments, persistence, bindings and export passed',flush=True)
