"""Blueprint promotion and per-surface settings in an isolated CyDAW QA instance."""
import argparse, json, socket
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--port',type=int,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
def request(**cmd):
    with socket.create_connection(('127.0.0.1',args.port),timeout=10) as s:
        s.settimeout(120);s.sendall((json.dumps(cmd)+'\n').encode('utf8'));return json.loads(s.makefile('rb').readline())
def need(**cmd):
    r=request(**cmd);assert r.get('ok'),r;return r['result']
def workshop(action,**kw):return need(cmd='workshop',action=action,**kw)
workshop('new',name='Blueprint regression',instrument=False)
node=need(cmd='add_node',track='workshop',key='comp-pro')['id']
for cmd in [dict(cmd='disconnect',source=1,destination=2),dict(cmd='connect',source=1,destination=node),dict(cmd='connect',source=node,destination=2)]:need(track='workshop',**cmd)
need(cmd='set_node_parameter',track='workshop',nodeId=node,parameter='comppro.threshold',value=-23)
scene=workshop('presentation')
threshold=next(p for p in scene['parameters'] if p['key'].endswith('comppro.threshold'))
ratio=next(p for p in scene['parameters'] if p['key'].endswith('comppro.ratio'))
scene.update(designed=True,deviceDesigned=True,windowWidth=1280,windowHeight=800,deviceWidth=520,deviceHeight=240,
    windowDesign=dict(background='ff182432',accent='ffff7733',text='ffededed'),
    deviceDesign=dict(background='ff322418',accent='ff33ccee',text='ffffffff'),controls=[],deviceControls=[])
workshop('presentation',state=scene)
# An empty, authored surface keeps its size and appearance when the graph changes.
variable=workshop('promote_value',nodeId=node,binding=threshold['key'],mode='variable')
vid=variable['nodeId'];state=variable['state']
v=next(n for n in state['nodes'] if n['id']==vid)
assert v['type']=='value-constant' and abs(v['parameters']['valueOperand']+23)<1e-5
vp=v['interface']['parameters'][0]
assert (vp['minimum'],vp['maximum'],vp['unit'])==(threshold['minimum'],threshold['maximum'],threshold['unit'])
assert vp['value']==-23 and vp['defaultValue']==-23
wire=next(c for c in state['connections'] if c['source']==vid and c['destination']==node)
assert wire.get('mode',0)==0
# Repeating on an occupied socket must fail without adding nodes or cables.
assert not request(cmd='workshop',action='promote_value',nodeId=node,binding=threshold['key'],mode='input').get('ok')
assert len(workshop('runtime_state')['editor']['nodes'])==len(state['nodes'])
promoted=workshop('promote_value',nodeId=node,binding=ratio['key'],mode='input')
iid=promoted['nodeId'];state=promoted['state']
boundary=next(n for n in state['nodes'] if n['id']==iid)
assert boundary['type']=='graph-value-input'
assert abs(boundary['portDefaults'][0]['value']-ratio['value'])<1e-5
assert any(c['source']==iid and c['destination']==node for c in state['connections'])
restored=workshop('presentation')
for key in ['windowDesign','deviceDesign','windowWidth','windowHeight','deviceWidth','deviceHeight']:
    assert restored[key]==scene[key],(key,restored[key],scene[key])
bad=dict(restored,windowDesign=dict(background='invalid'))
assert not request(cmd='workshop',action='presentation',state=bad).get('ok')
# Save/reopen retains typed connections, variable ranges/defaults and both appearances.
state=workshop('runtime_state')['editor']
f=(args.output/'blueprint.cydawdevice').resolve();f.write_text(json.dumps(dict(format='CyDAW Device',schema=1,role='audio-effect',name='Blueprint regression',graph=state)),encoding='utf8')
workshop('open',file=str(f));workshop('save');workshop('open',file=str(f))
restored=workshop('presentation');assert restored['windowDesign']==scene['windowDesign'] and restored['deviceDesign']==scene['deviceDesign']
runtime=workshop('runtime_state')
(args.output/'runtime.json').write_text(json.dumps(runtime,indent=2),encoding='utf8')
check=workshop('vst3_check');assert check['ready'],check
export=workshop('vst3_export',folder=str((args.output/'NativeBlueprint').resolve()),productName='CyBlueprint',pluginCode='NBLP')
cpp=(Path(export['project'])/'generated/Definition.cpp').read_text(encoding='utf8')
assert 'd.width=1280' in cpp and 'd.height=800' in cpp
assert 'd.background=0xff182432u' in cpp and 'c.background=d.background' in cpp
assert 'valueConstant' in cpp or 'static_cast' in cpp
print('Blueprint promotion and window settings checks passed',flush=True)
