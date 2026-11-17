"""Exercise oversized hidden rendering and the clocked Core null backend.
This is simulated audio acceptance; it measures no physical ASIO performance.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import wave

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--exe', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--port', type=int, default=48706)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
width = max(1800, ctypes.windll.user32.GetSystemMetrics(0) + 256)
height = max(1100, ctypes.windll.user32.GetSystemMetrics(1) + 128)
with tempfile.TemporaryDirectory(prefix='profile-', dir=a.output) as temporary:
    folder = Path(temporary).resolve()
    locations = folder / 'locations.json'
    locations.write_text(json.dumps(dict(format='CyDAW Locations', version=1,
        folders={k:str(folder/k) for k in ('cache','devices','presets','banks','mixes','foundry','pluginExports')})), 'utf-8')
    env = dict(os.environ, APPDATA=str(folder/'Roaming'), LOCALAPPDATA=str(folder/'Local'),
               CYDAW_NO_RECOVERY='1', CYDAW_CONTROL_TOKEN='', CYDAW_TEST_VIRTUAL_AUDIO='1',
               CYDAW_LOCATIONS_FILE=str(locations), CYDAW_DJ_COLLECTION_FILE=str(folder/'dj.json'))
    with (a.output/'application.log').open('w', encoding='utf-8') as log:
        app = subprocess.Popen([str(a.exe.resolve()), '--hidden', '--silent',
            f'--control-port={a.port}', f'--window-size={width}x{height}'],
            env=env, cwd=folder, stdout=log, stderr=subprocess.STDOUT)
        def q(**request):
            with socket.create_connection(('127.0.0.1', a.port), timeout=5) as sock:
                sock.settimeout(30)
                sock.sendall((json.dumps(request)+'\n').encode())
                result = json.loads(sock.makefile('rb').readline())
            assert result.get('ok'), result
            return result['result']
        def wait(fn, seconds=30):
            until = time.monotonic()+seconds
            while time.monotonic()<until:
                assert app.poll() is None, app.returncode
                try:
                    result=fn()
                    if result:return result
                except OSError:pass
                time.sleep(.05)
            raise AssertionError('headless acceptance timed out')
        try:
            assert wait(lambda:q(cmd='status'))['speakersSilenced']
            audio=q(cmd='audio_settings')
            assert audio['type']=='Virtual Audio (test only)' and len(audio['types'])==1, audio
            q(cmd='device_channels', inputs=0, outputs=2)
            snapshot=(a.output/'oversized.png').resolve()
            q(cmd='snapshot', path=str(snapshot))
            data=snapshot.read_bytes()
            assert data[:8]==b'\x89PNG\r\n\x1a\n'
            dimensions=struct.unpack('>II',data[16:24])
            assert dimensions==(width,height), (dimensions,(width,height))
            source=folder/'synthetic.wav'
            with wave.open(str(source),'wb') as f:
                f.setparams((2,2,48000,0,'NONE','not compressed'))
                f.writeframes(struct.pack('<hh',1000,-1000)*48000)
            q(cmd='add_audio_track', file=str(source))
            q(cmd='transport', action='play')
            state=wait(lambda:(s if (s:=q(cmd='status'))['position']>.05 else None))
            assert state['playing'], state
            q(cmd='transport', action='stop')
            report=dict(simulatedAudio=True,backend=audio['type'],zeroPhysicalDevices=True,
                        requested=[width,height],rendered=list(dimensions),callbackAdvancesTransport=True)
            (a.output/'checks.json').write_text(json.dumps(report,indent=2), 'utf-8')
            print(json.dumps(report))
        finally:
            try:q(cmd='quit');app.wait(timeout=15)
            except (OSError,AssertionError,subprocess.TimeoutExpired):app.terminate();app.wait(timeout=10)
