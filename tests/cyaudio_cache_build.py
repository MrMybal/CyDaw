"""Build cache correction against immutable audio SDK objects and run source regressions."""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--source',type=Path,default=ROOT/'src/integration/BufferedAudioSource.cpp');p.add_argument('--tests',type=Path,default=ROOT/'tests/AudioSourceTests.cpp');a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'output');out.mkdir(parents=True,exist_ok=True)
build=ROOT/'build/cyaudio-native-desktop';vc=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat');compiler=vc.parents[2]/'Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe';linker=compiler.with_name('link.exe');env={k.upper():v for k,v in os.environ.items()};sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
for row in subprocess.check_output('cmd /d /s /c ""'+str(vc)+'" >nul && set"',text=True,encoding='utf-8',errors='replace').splitlines():
 if '=' in row:k,v=row.split('=',1);env[k.upper()]=v
m=dict(commands=[],source=dict(path=str(a.source.resolve()),sha256=sha(a.source)),tests=dict(path=str(a.tests.resolve()),sha256=sha(a.tests)),variants={})
def run(cmd,log):
 m['commands'].append(dict(command=[str(v) for v in cmd],cwd=str(build),log=str(log)))
 with log.open('w',encoding='utf-8') as f:r=subprocess.run(cmd,cwd=build,env=env,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:print(log.read_text(encoding='utf-8',errors='replace')[-5000:]);raise RuntimeError(str(log))
recipes={}
for log in build.glob('*.dir/Release/*.tlog/CL.command.1.tlog'):
 rows=log.read_text(encoding='utf-16').splitlines()
 for i,row in enumerate(rows[:-1]):
  if row.startswith('^') and '|' not in row:recipes[Path(row[1:]).name.lower()]=(row[1:],rows[i+1])
def compile(originalName,source,obj):
 original,flags=recipes[originalName.lower()]
 for name in ['CyAudioCore','CyDSP']:
  for form in [str(ROOT.parent/name),str(ROOT/'..'/name)]:flags=re.sub(re.escape(form),lambda v:str(ROOT/'output/cyaudio-cpu-diagnostic/2026-11-13/frozen'/name),flags,flags=re.I)
 flags=re.sub(r'/Yu"[^"]*"','/Y-',flags,flags=re.I);flags=re.sub(r'/Fp"[^"]*"|/Fo"[^"]*"|/Fd"[^"]*"','',flags,flags=re.I);flags=re.sub(re.escape(original),lambda v:'"'+str(source.resolve())+'"',flags,flags=re.I)
 flags+=' /I"'+str(Path(original).parent)+'" /Fo"'+str(obj)+'" /Fd"'+str(obj.with_suffix('.pdb'))+'"';rsp=obj.with_suffix('.rsp');rsp.write_text(flags,encoding='utf-8');run([str(compiler),'@'+str(rsp)],obj.with_suffix('.log'))
obj=out/'BufferedAudioSource.obj';compile('BufferedAudioSource.cpp',a.source,obj)
testObj=out/'AudioSourceTests.obj';compile('AudioSourceTests.cpp',a.tests,testObj)
log=next(build.glob('cydaw_audio_source_tests.dir/Release/*.tlog/link.command.1.tlog'));link=' '.join(log.read_text(encoding='utf-16').splitlines()[1:]);link=re.sub(r'CYDAW_AUDIO_SOURCE_TESTS.DIR[\\/]RELEASE[\\/]AUDIOSOURCETESTS.OBJ',lambda v:'"'+str(testObj)+'"',link,flags=re.I)
def relink(link,folder,extra):
 folder.mkdir(exist_ok=True)
 for option in ['OUT','PDB','IMPLIB']:link=re.sub('/'+option+r':"[^"]*"','',link,flags=re.I)
 exe=folder/'CyDAW 2.exe';link='"'+str(extra)+'" '+link+' /OUT:"'+str(exe)+'" /PDB:"'+str(folder/'app.pdb')+'"';rsp=folder/'link.rsp';rsp.write_text(link,encoding='utf-8');run([str(linker),'@'+str(rsp)],folder/'link.log');return exe
exe=relink(link,out/'source-tests',obj)
for iteration in range(5):
 r=subprocess.run([str(exe)],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
m['regressions']=dict(repetitions=5,output=r.stdout);print(r.stdout,flush=True)
for label,base in [('control',ROOT/'output/cyaudio-engine-pair/2026-11-14/native/control'),('production',ROOT/'output/cyaudio-frame-rate/2026-11-14-fixed-idle/production')]:
 exe=relink((base/'link.rsp').read_text(encoding='utf-8'),out/label,obj)
 for f in base.iterdir():
  if f.is_file() and (f.suffix.lower()=='.dll' or f.name=='CyDAWPluginWorker.exe'):shutil.copy2(f,exe.parent/f.name)
 m['variants'][label]=dict(exe=str(exe),sha256=sha(exe));print(label,'ready',flush=True)
(out/'build-manifest.json').write_text(json.dumps(m,indent=2),encoding='utf-8')
