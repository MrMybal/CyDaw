"""Capture native Prisme envelopes in a private profile with virtual audio only.
No existing window/process/profile is touched. DPI geometry is tested by CyUiTests.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
from PIL import Image

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--exe',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--port',type=int,default=48718)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='profile-',dir=out) as temporary:
    profile=Path(temporary);locations=profile/'locations.json'
    locations.write_text(json.dumps(dict(format='CyDAW Locations',version=1,
        folders={k:str(profile/k) for k in ('cache','presets','banks','devices','mixes','foundry','pluginExports')})),'utf-8')
    env=dict(os.environ,APPDATA=str(profile/'Roaming'),LOCALAPPDATA=str(profile/'Local'),
        CYDAW_NO_RECOVERY='1',CYDAW_TEST_VIRTUAL_AUDIO='1',CYDAW_CONTROL_TOKEN='',
        CYDAW_LOCATIONS_FILE=str(locations),CYDAW_SINGLE_VIEWPORT='1',
        CYDAW_DJ_COLLECTION_FILE=str(profile/'dj.json'))
    with (out/'application.log').open('w',encoding='utf-8') as log:
        app=subprocess.Popen([str(a.exe.resolve()),'--hidden','--silent','--window-size=1800x1100',
            '--language=en',f'--control-port={a.port}'],env=env,cwd=profile,stdout=log,stderr=subprocess.STDOUT)
        def q(**request):
            with socket.create_connection(('127.0.0.1',a.port),timeout=5) as sock:
                sock.settimeout(30);sock.sendall((json.dumps(request)+'\n').encode())
                result=json.loads(sock.makefile('rb').readline())
            assert result.get('ok'),result
            return result['result']
        def wait(fn,seconds=45):
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                assert app.poll() is None,app.returncode
                try:
                    value=fn()
                    if value:return value
                except OSError:pass
                time.sleep(.05)
            raise AssertionError('envelope view did not become ready')
        def snapshot(name):
            q(cmd='ui_input',events=[dict(type='wait',frames=4)])
            path=out/(name+'.png');q(cmd='snapshot',path=str(path))
            with Image.open(path) as image:
                assert image.size==(1800,1100)
                assert image.convert('RGB').getextrema()[0][0]!=image.convert('RGB').getextrema()[0][1]
            return str(path)
        report=dict(simulatedAudio=True,cases=[])
        try:
            assert wait(lambda:q(cmd='status'))['speakersSilenced']
            assert q(cmd='audio_settings')['type']=='Virtual Audio (test only)'
            track=q(cmd='add_midi_track',name='Envelope QA',instrument='prism-instrument')['name']
            q(cmd='show_dialog',name='prism')
            cases=[('zero-sustain',{'env1.attack':.02,'env1.hold':0,'env1.decay':.6,'env1.sustain':0,
                    'env1.release':.2,'env1.attackCurve':0,'env1.decayCurve':.8,'env1.releaseCurve':.6}),
                   ('nonlinear',{'env1.attack':.15,'env1.hold':0,'env1.decay':.5,'env1.sustain':.35,
                    'env1.release':.3,'env1.attackCurve':-.8,'env1.decayCurve':.9,'env1.releaseCurve':-.7}),
                   ('zero-durations',{'env1.attack':0,'env1.hold':0,'env1.decay':0,'env1.sustain':0,'env1.release':0})]
            for name,settings in cases:
                # The initial editor tab is ENV 2; cover both its curve and the amplitude envelope.
                settings={**settings,**{key.replace('env1.','env2.'):value for key,value in settings.items()}}
                updated=q(cmd='prism',track=track,init=True,set=settings)
                assert not updated.get('problems'),updated
                image=snapshot(name)
                checked=q(cmd='prism',track=track)['values']
                for key,value in settings.items():assert abs(checked[key]-value)<1e-6,(key,value,checked[key])
                report['cases'].append(dict(name=name,settings=settings,snapshot=image,parametersUnchanged=True))
            q(cmd='prism',track=track,init=True,set={'env1.attack':.01,'env1.decay':.2,'env1.sustain':.35,'env2.attack':.01,'env2.decay':.2,'env2.sustain':.35})
            clip=q(cmd='add_midi_clip',track=track,name='Held note',startBeats=0,lengthBeats=8)
            q(cmd='add_notes',track=track,clip='Held note',notes=[dict(note=60,start=0,length=7,velocity=.8)])
            q(cmd='transport',action='play')
            wait(lambda:q(cmd='status').get('position',0)>.1)
            report['liveSnapshot']=snapshot('live-envelope')
            q(cmd='transport',action='stop')
            wait(lambda:not q(cmd='status')['playing'])
            report['processId']=app.pid
            (out/'checks.json').write_text(json.dumps(report,indent=2),'utf-8')
            print(json.dumps(report))
        finally:
            try:q(cmd='quit');app.wait(timeout=15)
            except (OSError,AssertionError,subprocess.TimeoutExpired):
                app.terminate();app.wait(timeout=10)
