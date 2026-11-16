"""Controller editor, existing OSC/DAW routing and settings round-trip.
Silent output-only current Windows device in a private profile. Known MIDI ports
are seeded disabled; no external MIDI or audio input is intentionally enabled.
Native peripheral hardware itself is not exercised by this software fixture.
"""
import sys,os,time,json,subprocess,argparse,hashlib,traceback,xml.etree.ElementTree as E
from pathlib import Path
sys.path.insert(0,'tests')
from cyaudio_comparison import Session,profile,write_json
import v2_controllers_check as legacy
root=Path.cwd();p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(root/'output');out.mkdir(parents=True,exist_ok=False)
folder=out/'session';folder.mkdir();env=profile(folder,None)
# Isolated persistence is enabled. Seed every known port OFF to suppress auto-enable.
personal=Path(os.environ['APPDATA'])/'CyDAW/CyDAW.layout.settings';before=hashlib.sha256(personal.read_bytes()).hexdigest()
known=''
vals={v.get('name'):v.get('val') for v in E.parse(personal).getroot().findall('VALUE')}
dev=E.fromstring(vals['audioDeviceState'].replace(chr(92)+'n',chr(10)));dev.attrib.update(audioInputDeviceName='',audioDeviceInChans='0',audioDeviceOutChans='11',useDefaultInputChannels='0',useDefaultOutputChannels='0')
for child in list(dev):dev.remove(child)
for suffix in ('layout','processing'):
 props=E.Element('PROPERTIES')
 for k,v in [('audioDeviceState',E.tostring(dev,encoding='unicode')),('knownMidiInputs',known),('computerKeyboard','1')]:E.SubElement(props,'VALUE',name=k,val=v)
 E.ElementTree(props).write(folder/'Roaming/CyDAW'/('CyDAW.'+suffix+'.settings'),encoding='utf-8')
s=Session.__new__(Session);s.port=48318;s.folder=folder;s.exe=root/'build/cyaudio-sleep/CyDAW2_artefacts/Release/CyDAW 2.exe'
report={'personalProfileSha256Before':before};running=False

def start():
 global running
 s.log=open(folder/'app.log','a',encoding='utf-8');s.process=subprocess.Popen([str(s.exe),'--silent','--hidden',f'--control-port={s.port}','--window-size=1500x1000','--language=en'],env=env,stdout=s.log,stderr=subprocess.STDOUT);running=True
 def ready():
  if s.process.poll() is not None:raise RuntimeError('Application exited')
  try:return s.q(cmd='ping') or True
  except OSError:return False
 s.wait(ready,60);assert s.q(cmd='status')['speakersSilenced'];assert not any(d['enabled'] for d in s.q(cmd='midi_inputs')['devices']);assert not s.q(cmd='audio_settings')['inputChannels']
def ctl(**kw):return s.q(cmd='controllers',**kw)
try:
 # Discover identifiers with persistence/auto-enable OFF, then seed them disabled.
 start();known='\n'.join(d['identifier'] for d in s.q(cmd='midi_inputs')['devices']);report['discoveryShutdown']=s.close();running=False
 layout=folder/'Roaming/CyDAW/CyDAW.layout.settings';tree=E.parse(layout);next(v for v in tree.getroot().findall('VALUE') if v.get('name')=='knownMidiInputs').set('val',known);tree.write(layout,encoding='utf-8')
 env.pop('CYDAW_NO_RECOVERY',None);start()
 legacy.port=s.port;legacy.exe_path=s.exe;legacy.scenario(out);assert not legacy.failures,legacy.failures
 raw=ctl(action='status');assert raw['nativeInputActive'];assert any(d['id']=='mouse:computer' and d['kind']=='mouse' for d in raw['raw'])
 box=ctl(action='add_virtual',name='Mouse macro',input='mouse:computer')['device'];points=[(i/16)**2 for i in range(17)]
 control=ctl(action='add_control',device=box,name='Expression curve',kind='axis',sourceAddress='axis/x',chain=[dict(kind='drawnCurve',points=points)])['control']
 ctl(action='assign',device=box,control=control,target='midi/live/cc/1/74',minimum=.2,maximum=.8)
 ctl(action='assign',device=box,control=control,target='master/volume',minimum=.2,maximum=.8)
 before_count=ctl(action='status')['directMidiMessages'];ctl(action='inject',device='mouse:computer',address='axis/x',value=.5)
 moved=ctl(action='value',target='master/volume')['value'];assert abs(moved-.35)<.001,moved
 assert ctl(action='status')['directMidiMessages']==before_count+1
 ctl(action='set_raw',device='mouse:computer',enabled=False);ctl(action='inject',device='mouse:computer',address='axis/x',value=.8)
 assert ctl(action='status')['directMidiMessages']==before_count+1
 ctl(action='set_raw',device='mouse:computer',enabled=True)
 copy=ctl(action='duplicate_virtual',device=box)['device'];copied=next(d for d in ctl(action='status')['virtual'] if d['id']==copy);assert copied['controls'][0]['chain'][0]['points']==points
 ctl(action='remove_virtual',device=copy)
 ctl(action='show',device=box,control=control,center=True)
 time.sleep(.3)
 # Native ImGui curve gesture, then inspect the serialized settings after close.
 def edit_curve(x,y):
  s.q(cmd='ui_input',events=[dict(type='move',x=x,y=y),dict(type='down',button=0),dict(type='up',button=0),dict(type='wait',frames=3)])
 edit_curve(1166,490)
 edited=next(c for d in ctl(action='status')['virtual'] if d['id']==box for c in d['controls'] if c['id']==control)['chain'][0]['points']
 assert abs(edited[8]-points[8])>.1,('Curve gesture did not update knot',edited)
 points=edited;s.q(cmd='snapshot',path=str(out/'controller-editor.png'));report['curveGesturePassed']=True
 report['box']=box;report['control']=control;report['macroValue']=moved;report['state']=ctl(action='status');report['legacyPassed']=True
 report['firstShutdown']=s.close();running=False
 stored=E.parse(folder/'Roaming/CyDAW/CyDAW.layout.settings').getroot();definitions=json.loads(next(v.get('val') for v in stored.findall('VALUE') if v.get('name')=='controllers'))
 saved=next(d for d in definitions['virtual'] if d['id']==box);assert saved['controls'][0]['chain'][0]['points']==points
 assert len([v for v in definitions['assignments'] if v['device']==box])==2
 # Restart the real application with the same isolated preferences.
 env['CYDAW_NO_RECOVERY']='1';start();state=ctl(action='status');restored=next(d for d in state['virtual'] if d['id']==box);assert restored['controls'][0]['chain'][0]['points']==points
 assert next(d for d in state['raw'] if d['id']=='mouse:computer')['present']
 before_count=state['directMidiMessages'];ctl(action='inject',device='mouse:computer',address='axis/x',value=.5);assert ctl(action='status')['directMidiMessages']==before_count+1
 report['settingsRoundtripPassed']=True;report['passed']=True
except BaseException:
 report['error']=traceback.format_exc();raise
finally:
 if running:report['shutdown']=s.close()
 report['personalProfileSha256After']=hashlib.sha256(personal.read_bytes()).hexdigest();assert report['personalProfileSha256After']==before
 write_json(out/'report.json',report)
