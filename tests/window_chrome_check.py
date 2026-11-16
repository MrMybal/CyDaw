"""Test only our own hidden desktop window. Silent output, no physical inputs.
Checks client-frame geometry, native hit testing, caption buttons and dirty-close.
Does not switch desktops or synthesize input into the user's desktop.
"""
import sys,os,time,json,subprocess,argparse,hashlib,traceback,ctypes as C,xml.etree.ElementTree as E
from pathlib import Path
sys.path.insert(0,'tests')
from cyaudio_comparison import Session,profile,write_json
root=Path.cwd();p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--exe',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(root/'output');out.mkdir(parents=True,exist_ok=False)
folder=out/'session';folder.mkdir();env=profile(folder,None)
personal=Path(os.environ['APPDATA'])/'CyDAW/CyDAW.layout.settings';before=hashlib.sha256(personal.read_bytes()).hexdigest()
vals={v.get('name'):v.get('val') for v in E.parse(personal).getroot().findall('VALUE')};dev=E.fromstring(vals['audioDeviceState'].replace(chr(92)+'n',chr(10)))
dev.attrib.update(audioInputDeviceName='',audioDeviceInChans='0',audioDeviceOutChans='11',useDefaultInputChannels='0',useDefaultOutputChannels='0')
for child in list(dev):dev.remove(child)
for suffix in ('layout','processing'):
 props=E.Element('PROPERTIES');E.SubElement(props,'VALUE',name='audioDeviceState',val=E.tostring(dev,encoding='unicode'))
 E.ElementTree(props).write(folder/'Roaming/CyDAW'/('CyDAW.'+suffix+'.settings'),encoding='utf-8')
s=Session.__new__(Session);s.port=48319;s.folder=folder;s.exe=a.exe.resolve();s.log=open(folder/'app.log','w',encoding='utf-8')
s.process=subprocess.Popen([str(s.exe),'--silent','--hidden',f'--control-port={s.port}','--window-size=1200x800','--language=en'],env=env,stdout=s.log,stderr=subprocess.STDOUT)
report={'profileSha256Before':before,'exeSha256':hashlib.sha256(s.exe.read_bytes()).hexdigest(),'checks':{}}
U=C.WinDLL('user32',use_last_error=True)
class Rect(C.Structure):_fields_=[('left',C.c_long),('top',C.c_long),('right',C.c_long),('bottom',C.c_long)]
class Point(C.Structure):_fields_=[('x',C.c_long),('y',C.c_long)]
class Monitor(C.Structure):_fields_=[('size',C.c_ulong),('monitor',Rect),('work',Rect),('flags',C.c_ulong)]
U.MonitorFromWindow.argtypes=[C.c_void_p,C.c_ulong];U.MonitorFromWindow.restype=C.c_void_p
U.GetMonitorInfoW.argtypes=[C.c_void_p,C.POINTER(Monitor)]
for name in ('GetWindowRect','GetClientRect'):
 getattr(U,name).argtypes=[C.c_void_p,C.POINTER(Rect)];getattr(U,name).restype=C.c_int
U.GetAncestor.argtypes=[C.c_void_p,C.c_uint];U.GetAncestor.restype=C.c_void_p
U.GetWindowThreadProcessId.argtypes=[C.c_void_p,C.POINTER(C.c_ulong)]
U.ClientToScreen.argtypes=[C.c_void_p,C.POINTER(Point)]
U.SendMessageW.argtypes=[C.c_void_p,C.c_uint,C.c_size_t,C.c_ssize_t];U.SendMessageW.restype=C.c_ssize_t
U.PostMessageW.argtypes=[C.c_void_p,C.c_uint,C.c_size_t,C.c_ssize_t]
U.SetWindowPos.argtypes=[C.c_void_p,C.c_void_p,C.c_int,C.c_int,C.c_int,C.c_int,C.c_uint]
U.IsZoomed.argtypes=[C.c_void_p];U.IsIconic.argtypes=[C.c_void_p];U.GetDpiForWindow.argtypes=[C.c_void_p]
WNDENUM=C.WINFUNCTYPE(C.c_int,C.c_void_p,C.c_ssize_t)
def locate(pid):
 found=[]
 @WNDENUM
 def child(h,_):
  n=C.c_ulong();U.GetWindowThreadProcessId(h,C.byref(n));cls=C.create_unicode_buffer(256);U.GetClassNameW(C.c_void_p(h),cls,256)
  if n.value==pid and cls.value.startswith('CyDAW2ImGuiSurface'):found.append(h)
  return 1
 @WNDENUM
 def top(h,_):
  child(h,0);U.EnumChildWindows(C.c_void_p(h),child,0);return 1
 U.OpenDesktopW.argtypes=[C.c_wchar_p,C.c_ulong,C.c_int,C.c_ulong];U.OpenDesktopW.restype=C.c_void_p
 U.SetThreadDesktop.argtypes=[C.c_void_p];U.EnumDesktopWindows.argtypes=[C.c_void_p,WNDENUM,C.c_ssize_t]
 desk=U.OpenDesktopW('CyDAW-automation',0,False,0x1|0x40|0x80);assert desk,C.get_last_error();assert U.SetThreadDesktop(desk)
 U.EnumDesktopWindows(desk,top,0);assert found;return found[0],U.GetAncestor(found[0],2)
def rect(h,client=False):
 r=Rect();assert (U.GetClientRect if client else U.GetWindowRect)(h,C.byref(r));return [r.left,r.top,r.right,r.bottom]
def check(name,condition,data=None):
 report['checks'][name]={'passed':bool(condition),'data':data};print(name,condition,data,flush=True);assert condition,name

def click(x,y):s.q(cmd='ui_input',events=[dict(type='move',x=x,y=y),dict(type='down',button=0),dict(type='up',button=0),dict(type='wait',frames=3)])
def escape():s.q(cmd='ui_input',events=[dict(type='key',key='escape'),dict(type='wait',frames=3)])
def dimensions():
 r=rect(parent);return r[2]-r[0],r[3]-r[1]
def hit(x,y,h=None):
 r=rect(parent);packed=((r[0]+x)&65535)|(((r[1]+y)&65535)<<16)
 return U.SendMessageW(h or parent,0x84,0,packed)
def snapshot(name):s.q(cmd='snapshot',path=str(out/(name+'.png')))
try:
 def ready():
  if s.process.poll() is not None:raise RuntimeError('Application exited')
  try:return s.q(cmd='ping') or True
  except OSError:return False
 s.wait(ready,60);assert s.q(cmd='status')['speakersSilenced'];assert not any(d['enabled'] for d in s.q(cmd='midi_inputs')['devices']);assert not s.q(cmd='audio_settings')['inputChannels']
 child,parent=locate(s.process.pid);time.sleep(.3);dpi=U.GetDpiForWindow(parent);scale=dpi/96;report['dpi']=dpi
 wr=rect(parent);cr=rect(parent,True);origin=Point();U.ClientToScreen(parent,C.byref(origin))
 check('entire_window_is_client',cr==[0,0,wr[2]-wr[0],wr[3]-wr[1]] and [origin.x,origin.y]==wr[:2],{'window':wr,'client':cr,'clientOrigin':[origin.x,origin.y]})
 w,h=dimensions();y=round(15*scale)
 for name,x,cy,expected in [('caption',w//2,y,2),('file_menu',round(110*scale),y,1),('close_button',w-round(22*scale),y,1),('transport',w//2,round(55*scale),1),('top_left',1,1,13),('top_right',w-1,1,14),('bottom_left',1,h-1,16),('bottom_right',w-1,h-1,17),('right_edge',w-1,h//2,11)]:check('hit_'+name,hit(x,cy)==expected,hit(x,cy))
 check('surface_passes_caption_to_parent',hit(w//2,y,child)==-1)
 check('surface_keeps_controls',hit(w-round(22*scale),y,child)==1)
 snapshot('integrated-normal')
 click(w-round(66*scale),y);s.wait(lambda:U.IsZoomed(parent),5);check('maximize_button',True,rect(parent));snapshot('integrated-maximized')
 monitor=Monitor();monitor.size=C.sizeof(monitor);assert U.GetMonitorInfoW(U.MonitorFromWindow(parent,2),C.byref(monitor));work=monitor.work
 check('maximized_uses_monitor_work_area',rect(parent)==[work.left,work.top,work.right,work.bottom],rect(parent))
 # A maximized border is a client region, not a resize handle.
 w,h=dimensions();check('no_resize_while_maximized',hit(1,h//2)==1)
 click(w-round(66*scale),y);s.wait(lambda:not U.IsZoomed(parent),5);check('restore_button',dimensions()==(1200,800),dimensions())
 w,h=dimensions();click(w-round(110*scale),y);s.wait(lambda:U.IsIconic(parent),5);check('minimize_button',True)
 U.PostMessageW(parent,0x112,0xf120,0);s.wait(lambda:not U.IsIconic(parent),5)
 # Native caption double-click still delegates to Windows.
 U.PostMessageW(parent,0xa3,2,0);s.wait(lambda:U.IsZoomed(parent),5);check('caption_double_click',True)
 U.PostMessageW(parent,0x112,0xf120,0);s.wait(lambda:not U.IsZoomed(parent),5)
 assert U.SetWindowPos(parent,None,100,100,1400,850,0x14)
 s.wait(lambda:rect(child,True)[2:]==[1400,850],5);check('move_and_resize',dimensions()==(1400,850),rect(parent));snapshot('integrated-resized')
 click(round(110*scale),y);check('open_menu_does_not_drag',hit(700,y)==1);escape();s.q(cmd='snapshot',path=str(out/'integrated-menu-closed.png'));check('closed_menu_restores_drag',hit(700,y)==2)
 s.q(cmd='add_midi_track',name='Unsaved window fixture',instrument='opale-instrument');click(1400-round(22*scale),y);time.sleep(.15)
 check('dirty_close_keeps_process',s.process.poll() is None);snapshot('integrated-unsaved-prompt');check('dirty_prompt_blocks_caption_drag',hit(700,y)==1)
 escape();s.q(cmd='save_project',path=str(out/'window-fixture.cydaw'));snapshot('integrated-ready')
 report['passed']=True
except BaseException:report['error']=traceback.format_exc();raise
finally:
 report['shutdown']=s.close();report['profileSha256After']=hashlib.sha256(personal.read_bytes()).hexdigest();assert report['profileSha256After']==before
 write_json(out/'report.json',report)
