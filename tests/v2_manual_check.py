"""Offline manual reader integration on its own hidden, silent Windows instance.
Exercises genuine UI input without modifying the user's session or audio hardware.
"""
import argparse,json,os,socket,subprocess,tempfile,time
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--port',type=int,default=48064);a=p.parse_args()
 a.output.mkdir(parents=True,exist_ok=True);out=a.output.resolve()
 with tempfile.TemporaryDirectory(prefix='cydaw-reader-') as tmp:
  scratch=Path(tmp)
  locations=scratch/'locations.json';locations.write_text(json.dumps(dict(format='CyDAW Locations',version=1,folders={k:str(scratch/k) for k in ('cache','devices','pluginExports','presets','mixes','foundry','banks','downloads','companion','aiModels','soundModels','processings')})),encoding='utf-8')
  env=dict(os.environ,CYDAW_NO_RECOVERY='1',CYDAW_SINGLE_VIEWPORT='1',CYDAW_LOCATIONS_FILE=str(locations),CYDAW_DJ_COLLECTION_FILE=str(scratch/'dj.json'),CYDAW_DJ_PROCESSING_FILE=str(scratch/'dj-processing.json'),CYDAW_VIRTUAL_IO_FILE=str(scratch/'io.json'))
  startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
  with (out/'app.log').open('w',encoding='utf-8') as log:
   app=subprocess.Popen([str(a.exe.resolve()),'--silent','--language=fr','--window-size=1800x1100',f'--control-port={a.port}'],env=env,cwd=str(scratch),startupinfo=startup,stdout=log,stderr=subprocess.STDOUT)
   def raw(**data):
    with socket.create_connection(('127.0.0.1',a.port),timeout=5) as s:
     s.settimeout(120);s.sendall((json.dumps(data)+'\n').encode());return json.loads(s.makefile('rb').readline())
   def q(**data):
    result=raw(**data)
    assert result.get('ok'),(data,result)
    return result['result']
   def m(**kw):return q(cmd='manual',**kw)
   def events(*items):return q(cmd='ui_input',events=list(items)+[dict(type='wait',frames=3)])
   def key(k,**kw):return dict(type='key',key=k,**kw)
   def click(r):events(dict(type='move',x=r['x']+r['width']/2,y=r['y']+r['height']/2),dict(type='down'),dict(type='up'))
   def drag(x,y,dx,dy):events(dict(type='move',x=x,y=y),dict(type='down'),dict(type='move',x=x+10,y=y),dict(type='move',x=x+dx,y=y+dy),dict(type='up'))
   def shot(name):q(cmd='snapshot',path=str(out/(name+'.png')))
   report={}
   try:
    for _ in range(200):
     if app.poll() is not None:raise RuntimeError('reader instance exited')
     try:
      assert q(cmd='status')['speakersSilenced'];break
     except OSError:time.sleep(.1)
    else:raise RuntimeError('reader instance did not start')
    initial=q(cmd='status')
    assert not m()['visible'] and m()['language']=='en','English default, independent of French application'
    events(key('ctrl',down=True),key('f1'),key('ctrl',down=False))
    assert m()['visible'],'real Ctrl+F1 opens the reader'
    shot('manual-en')
    chapters=m()['topics'];assert len(chapters)==40 and not m()['error']
    # Every section is parsed and rendered, including long native-schema tables.
    rendered=0
    for lang in ('en','fr'):
     for chapter in chapters:
      for section in chapter['sections']:
       state=m(action='navigate',language=lang,topic=chapter['id'],section=section['id'])
       events(dict(type='wait',frames=1))
       assert state['textLength']>0 and not state['error'],(lang,chapter['id'],section['id'])
       rendered+=1
       assert m()['textureCount']<=10
    report['renderedSections']=rendered
    m(action='navigate',topic='mixing',section='sidechain',language='en')
    events(dict(type='wait',frames=4));click(m()['languageBounds']);events(key('down'),key('enter'))
    assert m()['language']=='fr','real language selector'
    assert m()['topic']=='mixing' and m()['section']=='sidechain'
    shot('manual-fr')
    state=m(action='search',query='reverberation');assert state['results']
    assert not m(action='search',query='zxqunfindable')['results']
    m(action='search',query='')
    # Ctrl+F, text entry and spaces must stay inside the reader.
    events(key('ctrl',down=True),key('f'),key('ctrl',down=False),dict(type='text',text='bus master'))
    assert m()['query']=='bus master',m()['query']
    assert q(cmd='status')['playing']==initial['playing'],'search typing must not start playback'
    events(key('escape'));assert m()['query']=='' and m()['visible']
    # History keeps section identity and real document scroll position.
    m(action='navigate',topic='reference-instruments',section='colosse-synth')
    events(dict(type='wait',frames=4));r=m()['bounds']
    events(dict(type='move',x=r['x']+r['width']*.65,y=r['y']+r['height']*.72),dict(type='wheel',dy=-8))
    previous=m()['scroll'];assert previous>0,('table scroll',m())
    m(action='navigate',topic='welcome');m(action='back');events(dict(type='wait',frames=4))
    assert m()['topic']=='reference-instruments' and abs(m()['scroll']-previous)<2
    m(action='forward');assert m()['topic']=='welcome'
    # All themes render readable prose and figures without rebuilding document data.
    for theme in ('mid-grey','light-grey','dark','violet','navy'):
     q(cmd='theme',key=theme);m(action='navigate',topic='cinema',language='fr');events(dict(type='wait',frames=4));shot('manual-'+theme)
    q(cmd='theme',key='mid-grey');m(action='navigate',topic='welcome',language='en');events(dict(type='wait',frames=4))
    state=m();assert state['figureBounds'],'visible illustration'
    click(state['figureBounds'][0]);state=m();assert state['figureEnlarged'] and state['focused']
    zoom=state['imageZoom'];r=state['imageBounds'];click(r)
    events(dict(type='wheel',dy=2));assert m()['imageZoom']>zoom,'figure wheel zoom'
    events(key('space'));assert q(cmd='status')['playing']==initial['playing'],'enlarged figure isolates transport shortcuts'
    state=m();origin=state['imageOriginX'];r=state['imageBounds']
    x=r['x']+r['width']*.5;y=r['y']+r['height']*.5
    events(dict(type='move',x=x,y=y),dict(type='down',button=2),dict(type='move',x=x+60,y=y+20),dict(type='up',button=2))
    assert abs(m()['imageOriginX']-origin)>1,'middle-button image pan'
    events(dict(type='move',x=x,y=y),dict(type='down'),dict(type='up'),dict(type='down'),dict(type='up'))
    assert abs(m()['imageZoom']-zoom)<.01,'double-click fit'
    shot('manual-enlarged')
    events(key('escape'));assert not m()['figureEnlarged'] and m()['visible']
    m(action='open');events(dict(type='wait',frames=4));r=m()['zoomBounds']
    events(dict(type='move',x=r['x']+r['width']*.9,y=r['y']+r['height']/2),dict(type='down'),dict(type='up'))
    assert m()['textScale']>1.3,'text-size slider'
    shot('manual-large-text')
    # Resize the actual reader into its narrow layout, then return to wide.
    m(action='open');events(dict(type='wait',frames=4));r=m()['bounds']
    drag(r['x']+r['width']-3,r['y']+r['height']-3,-500,-120)
    state=m();assert state['bounds']['width']<1000,state['bounds']
    m(action='navigate',topic='plugin-formats',language='fr');events(dict(type='wait',frames=4));shot('manual-narrow')
    # Local resolver refuses arbitrary URLs/files over the testing/control API.
    assert not raw(cmd='manual',action='follow',target='file:///C:/Windows/win.ini')['ok']
    diagnostic=m()
    assert diagnostic['imguiErrorCount']==0 and 'Error' not in diagnostic['imguiErrorLog'],diagnostic['imguiErrorLog']
    m(action='close')
    assert q(cmd='status')['audioTracks']==initial['audioTracks']
    report.update(englishDefault=True,search=True,history=True,figureZoom=True,narrowLayout=True,themes=5,imguiErrors=0,transportUnchanged=True)
    (out/'checks.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('Manual reader checks passed:',json.dumps(report),flush=True)
   finally:
    try:q(cmd='quit')
    except (OSError,AssertionError):pass
    try:app.wait(timeout=15)
    except subprocess.TimeoutExpired:app.terminate();app.wait(timeout=10)

if __name__=='__main__':main()
