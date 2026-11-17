"""Build the new numerical corpus against immutable existing Core objects."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();build=a.build.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
vc=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat');cl=vc.parents[2]/'Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe';linker=cl.with_name('link.exe');env={k.upper():v for k,v in os.environ.items()}
for row in subprocess.check_output('cmd /d /s /c ""'+str(vc)+'" >nul && set"',text=True,encoding='utf-8',errors='replace').splitlines():
 if '=' in row:k,v=row.split('=',1);env[k.upper()]=v
log=next(build.glob('cydaw_dsp_reuse_tests.dir/Release/*.tlog/CL.command.1.tlog'));rows=log.read_text(encoding='utf-16').splitlines();i=next(i for i,v in enumerate(rows) if v.startswith('^') and v.lower().endswith('dspreusetests.cpp'))
original=rows[i][1:];flags=rows[i+1];source=ROOT/'tests/FullDspReuseTests.cpp';obj=out/'corpus.obj'
flags=re.sub(r'/Yu"[^"]*"','/Y-',flags,flags=re.I);flags=re.sub(r'/Fp"[^"]*"|/Fo"[^"]*"|/Fd"[^"]*"','',flags,flags=re.I);flags=re.sub(re.escape(original),lambda _:str(source),flags,flags=re.I)
flags+=' /I"'+str(ROOT/'src/engine')+'" /Fo"'+str(obj)+'" /Fd"'+str(out/'corpus.pdb')+'" /utf-8';rsp=out/'compile.rsp';rsp.write_text(flags,encoding='utf-8')
def run(cmd,name):
 with (out/name).open('w',encoding='utf-8') as f:r=subprocess.run(cmd,cwd=build,env=env,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:raise RuntimeError((out/name).read_text(encoding='utf-8',errors='replace')[-5000:])
run([str(cl),'@'+str(rsp)],'compile.log')
log=next(build.glob('cydaw_dsp_reuse_tests.dir/Release/*.tlog/link.command.1.tlog'));link=' '.join(log.read_text(encoding='utf-16').splitlines()[1:]);tokens=re.findall(r'"[^"\n]*"|[^\s]+',link);inputs={}
for token in tokens:
 f=Path(token.strip('"'));name=f.name.lower()
 if name=='dspreusetests.obj':link=link.replace(token,'"'+str(obj)+'"')
 elif f.suffix.lower() in ('.obj','.lib','.res'):
  if not f.is_absolute():f=build/f
  if f.is_file():inputs[str(f)]=hashlib.sha256(f.read_bytes()).hexdigest()
for option in ['OUT','PDB','IMPLIB']:link=re.sub('/'+option+r':"[^"]*"','',link,flags=re.I)
exe=out/'corpus.exe';link+=' /OUT:"'+str(exe)+'" /PDB:"'+str(out/'link.pdb')+'"';rsp=out/'link.rsp';rsp.write_text(link,encoding='utf-8');run([str(linker),'@'+str(rsp)],'link.log')
(out/'manifest.json').write_text(json.dumps(dict(exe=str(exe),sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),inputs=inputs,sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest()),indent=2),encoding='utf-8')
print(exe)
