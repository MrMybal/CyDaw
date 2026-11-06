"""Verify actual CyDAW help entry points in a private hidden silent process."""
import argparse,json,os,socket,subprocess,tempfile,time
from pathlib import Path
def main():
 p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--port',type=int,default=48064);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='cydaw-help-') as tmp:
  scratch=Path(tmp);locations=scratch/'locations.json'
  locations.write_text(json.dumps(dict(format='CyDAW Locations',version=1,folders={k:str(scratch/k) for k in ('cache','devices','pluginExports','presets','mixes','foundry','banks','downloads','companion','aiModels','soundModels','processings')})),encoding='utf-8')
  env=dict(os.environ,CYDAW_NO_RECOVERY='1',CYDAW_SINGLE_VIEWPORT='1',CYDAW_LOCATIONS_FILE=str(locations),CYDAW_DJ_COLLECTION_FILE=str(scratch/'dj.json'),CYDAW_DJ_PROCESSING_FILE=str(scratch/'processing.json'),CYDAW_VIRTUAL_IO_FILE=str(scratch/'io.json'))
  startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
  with (a.output/'app.log').open('w',encoding='utf-8') as log:
   app=subprocess.Popen([str(a.exe.resolve()),'--silent','--language=en','--window-size=1800x1100',f'--control-port={a.port}'],cwd=scratch,env=env,startupinfo=startup,stdout=log,stderr=subprocess.STDOUT)
   def raw(**data):
    with socket.create_connection(('127.0.0.1',a.port),timeout=5) as s:
     s.settimeout(120);s.sendall((json.dumps(data)+'\n').encode());return json.loads(s.makefile('rb').readline())
   def q(**data):
    result=raw(**data);assert result.get('ok'),(data,result);return result['result']
   def help(**kw):return q(cmd='manual',**kw)
   def events(*items):return q(cmd='ui_input',events=list(items)+[dict(type='wait',frames=4)])
   def key(name,**kw):return dict(type='key',key=name,**kw)
   def shortcut(context=False):
    values=[key('ctrl',down=True)]
    if context:values.append(key('shift',down=True))
    values.append(key('f1'))
    if context:values.append(key('shift',down=False))
    values.append(key('ctrl',down=False));events(*values)
   try:
    for _ in range(200):
     if app.poll() is not None:raise RuntimeError('help instance exited')
     try:
      initial=q(cmd='status');assert initial['speakersSilenced'];break
     except OSError:time.sleep(.1)
    else:raise RuntimeError('help instance did not start')
    events(dict(type='move',x=270,y=12),dict(type='down'),dict(type='up'))
    q(cmd='snapshot',path=str((a.output/'help-menu.png').resolve()))
    beforeMenu=help()['requests']
    events(dict(type='move',x=315,y=33),dict(type='down'),dict(type='up'))
    assert help()['requests']==beforeMenu+1,'real Help > User manual'
    events(dict(type='move',x=270,y=12),dict(type='down'),dict(type='up'))
    events(dict(type='move',x=315,y=75),dict(type='down'),dict(type='up'))
    assert help()['url'].endswith('#/en/shortcuts'),'real Help > Keyboard shortcuts'
    assert help()['engine']=='CyDoc';before=help()['requests'];shortcut()
    current=help();assert current['requests']==before+1 and current['url'].startswith('file:///') and current['launchSuppressed']
    assert current['url'].endswith('/Documentation/Documentation.html'),'generic help opens the official local reader'
    events(dict(type='text',text='?'));assert help()['url'].endswith('#/en/shortcuts')
    contexts={}
    for mode,topic in [('foundry','foundry'),('cinema','cinema'),('dj','dj'),('workshop','workshop'),('processing','processing')]:
     q(cmd='app_mode',mode=mode);shortcut(True);state=help();assert state['url'].endswith('#/en/'+topic),(mode,state)
     contexts[mode]=state['url']
    # Existing manual: and Markdown links are converted to genuine CyDoc routes.
    state=help(action='follow',target='manual:mixing.md#sidechain',language='fr');assert state['url'].endswith('#/fr/mixing/sidechain')
    state=help(action='follow',target='#master');assert state['url'].endswith('#/fr/mixing/master')
    assert not raw(cmd='help',action='follow',target='https://example.com')['ok']
    assert q(cmd='status')['playing']==initial['playing']
    q(cmd='app_mode',mode='studio');q(cmd='snapshot',path=str((a.output/'native-help.png').resolve()))
    report=dict(engine='CyDoc',ctrlF1=True,shortcutReference=True,contexts=contexts,legacyAliases=True,externalLinksRejected=True,silentLaunchSuppressed=True)
    (a.output/'checks.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print('Native CyDoc help checks passed',flush=True)
   finally:
    try:q(cmd='quit')
    except (OSError,AssertionError):pass
    try:app.wait(timeout=15)
    except subprocess.TimeoutExpired:app.terminate();app.wait(timeout=10)
if __name__=='__main__':main()
