"""Integration checks against an isolated CyDAW --silent --control-port instance.
Usage: python tests/v2_workshop_designer_check.py --port 48016 --output <scratch>
Creates its own workshop tabs and files; does not change the studio's tracks.
"""
import argparse,copy,json,socket,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--port',type=int,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
def request(cmd):
    with socket.create_connection(('127.0.0.1',args.port),timeout=10) as s:
        s.settimeout(120);s.sendall((json.dumps(cmd)+'\n').encode('utf8'));return json.loads(s.makefile('rb').readline())
def need(cmd):
    r=request(cmd);assert r.get('ok'),r;return r['result']
def workshop(action,**kw):return need(dict(cmd='workshop',action=action,**kw))
workshop('new',name='Designer regression',instrument=False)
node=need(dict(cmd='add_node',track='workshop',key='comp-pro'))['id']
for cmd in [dict(cmd='disconnect',source=1,destination=2),dict(cmd='connect',source=1,destination=node),dict(cmd='connect',source=node,destination=2)]:need(dict(track='workshop',**cmd))
ports=need(dict(cmd='describe_node',track='workshop',nodeId=node))['ports']
style=next(p for p in ports if p['key']=='style')
state=workshop('port_exposure',nodeId=node,portId=style['id'],exposed=True,name='Style',category='Dynamique/Mode')
scene=workshop('presentation');parameters=scene['parameters'];primary=parameters[0]['key']
source=workshop('component')['source']
scene.update(designed=True,deviceDesigned=True,windowWidth=1000,windowHeight=660,deviceWidth=520,deviceHeight=240,nextControlId=300)
scene['controls']=[dict(id=100,kind='text',label='DESIGNER QA',x=.02,y=.02,width=.9,height=.08,design=dict(accent='ffff8800',fontScale=1.5,page='Main')),
 dict(id=101,kind='custom',label='C++',binding=primary,x=.65,y=.2,width=.3,height=.6,design=dict(customSource=source,background='ff292330')),
 dict(id=102,kind='native-panel',label='Compressor',x=.02,y=.15,width=.6,height=.8,extraBindings=[v['key'] for v in parameters[:8]],design=dict(pane=24,indices=list(range(8)),large=True))]
scene['deviceControls']=[dict(id=200,kind='knob',binding=primary,label='Threshold',x=.05,y=.1,width=.25,height=.75,design=dict(accent='ffff8800',group='main'))]
workshop('presentation',state=scene)
for change in ['kind','binding','bounds','extra']:
    bad=copy.deepcopy(scene)
    if change=='kind':bad['controls'][0]['kind']='unknown-widget'
    elif change=='binding':bad['controls'][1]['binding']='node.999.missing'
    elif change=='bounds':bad['controls'][0]['x']=1.5
    else:bad['controls'][2]['extraBindings']=['missing']
    assert not request(dict(cmd='workshop',action='presentation',state=bad)).get('ok'),change
restored=workshop('presentation');assert restored['deviceDesigned'] and restored['deviceWidth']==520
assert next(c for c in restored['controls'] if c['id']==101)['design']['customSource']==source
# Save a real device and reopen it through the normal application path.
state=workshop('port_exposure',nodeId=node,portId=style['id'],exposed=True,name='Style',category='Dynamique/Mode')
file=(args.output/'designer.cydawdevice').resolve();file.write_text(json.dumps(dict(format='CyDAW Device',schema=1,role='audio-effect',name='Designer regression',graph=state),ensure_ascii=False),encoding='utf8')
workshop('open',file=str(file));workshop('save');workshop('open',file=str(file))
restored=workshop('presentation');assert restored['deviceDesigned'] and len(restored['deviceControls'])==1
assert next(c for c in restored['controls'] if c['id']==100)['design']['page']=='Main'
state=workshop('port_exposure',nodeId=node,portId=style['id'],exposed=True)
port=next(p for n in state['nodes'] if n['id']==node for p in n['portDefaults'] if p['id']==style['id'])
assert port['category']=='Dynamique/Mode' and port['exposure']==1
# A manually created, typed boundary can be removed independently.
boundary=need(dict(cmd='add_node',track='workshop',key='graph-value-input'))['id']
workshop('remove_boundary',nodeId=boundary)
workshop('component',source=source,build=True)
until=time.monotonic()+180
while True:
    compiled=workshop('component',source=source)
    if compiled['status']!=1:break
    assert time.monotonic()<until,'component build timeout';time.sleep(.2)
assert compiled['status']==2 and compiled['loadable'],compiled
need(dict(cmd='set_node_parameter',track='workshop',nodeId=node,parameter='comppro.threshold',value=-18))
workshop('programs',op='add',name='Default')
need(dict(cmd='set_node_parameter',track='workshop',nodeId=node,parameter='comppro.threshold',value=-30))
workshop('programs',op='add',name='Soft')
workshop('save')
export=workshop('vst3_export',folder=str((args.output/'NativeComp').resolve()),productName='CyNativeComp',pluginCode='NCMP')
project=Path(export['project']);cmake=(project/'CMakeLists.txt').read_text(encoding='utf8');cpp=(project/'generated/Definition.cpp').read_text(encoding='utf8')
assert 'NativePlugin.cmake' in cmake and 'JUCE' not in cmake
assert 'cydawCustom_101' in cpp and 'Soft' in cpp and 'p.values.push_back' in cpp
assert 'NodeGraphComponent' not in cpp
(args.output/'checks.json').write_text(json.dumps(dict(layouts=True,strictCodeValidation=True,portMetadata=True,manualBoundaries=True,cppComponent=True,nativeProject=True,programs=2),indent=2),encoding='utf8')
print('Workshop designer integration checks passed',flush=True)
