"""Local production + UI-probed relink against immutable original audio objects.
Avoids consuming sibling SDK changes. Does not overwrite comparison executables.
The normal CMake target builds these same production sources without probes.
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'output');out.mkdir(parents=True,exist_ok=True)
build=ROOT/'build/cyaudio-native-desktop';frozen=ROOT/'output/cyaudio-cpu-diagnostic/2026-11-13/frozen'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
vc=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat');compiler=vc.parents[2]/'Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe';linker=compiler.with_name('link.exe')
env={k.upper():v for k,v in os.environ.items()}
for row in subprocess.check_output('cmd /d /s /c ""'+str(vc)+'" >nul && set"',text=True,encoding='utf-8',errors='replace').splitlines():
 if '=' in row:k,v=row.split('=',1);env[k.upper()]=v
m=dict(compiler=str(compiler),configuration='Original O2 Release audio/SDK objects; updated host, AppUi, BrowserView and LibraryWindow recompiled O2/MD/fp:precise; no LTO',inputs={},commands=[],sources={},variants={})
def run(cmd,folder,log):
 m['commands'].append(dict(command=[str(x) for x in cmd],cwd=str(folder),log=str(log)))
 with log.open('w',encoding='utf-8') as f:r=subprocess.run(cmd,cwd=folder,env=env,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:print(log.read_text(encoding='utf-8',errors='replace')[-5000:]);raise RuntimeError(str(log))
# Verify layout compatibility before reusing unchanged MainContent/other callers.
for label,header in [('old',out/'old-ImGuiHost.h'),('new',ROOT/'src/v2/ImGuiHost.h')]:
 if label=='old':header.write_bytes(subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'show','c136b4e:src/v2/ImGuiHost.h'],cwd=ROOT))
 source=out/(label+'-abi.cpp');source.write_text('#include "'+header.as_posix()+'"\n#include <cstdio>\nint main(){std::printf("%zu %zu",sizeof(cydaw::v2::ImGuiHost),alignof(cydaw::v2::ImGuiHost));}\n',encoding='utf-8')
 exe=out/(label+'-abi.exe');run([str(compiler),'/nologo','/std:c++20','/EHsc','/utf-8','/MD','/I'+str(ROOT/'include'),str(source),'/Fo'+str(out/(label+'-abi.obj')),'/Fe'+str(exe)],out,out/(label+'-abi.log'));m[label+'Abi']=subprocess.check_output([str(exe)],text=True)
assert m['oldAbi']==m['newAbi'],m
run([str(compiler),'/nologo','/std:c++20','/O2','/EHsc','/utf-8','/MD','/I'+str(ROOT/'src/v2'),str(ROOT/'tests/FramePacingTests.cpp'),'/Fo'+str(out/'pacing.obj'),'/Fe'+str(out/'pacing.exe')],out,out/'pacing-build.log')
m['pacingTest']=subprocess.check_output([str(out/'pacing.exe')],text=True);print(m['pacingTest'],flush=True)
link=' '.join((build/'CyDAW2.dir/Release/CyDAW2.tlog/link.command.1.tlog').read_text(encoding='utf-16').splitlines()[1:]);cache=out/'inputs';cache.mkdir(exist_ok=True);cached={}
for token in sorted(set(re.findall(r'"[^"\n]*"|[^\s]+',link)),key=len,reverse=True):
 value=token.strip('"');f=Path(value)
 if value.startswith('/') or not value.lower().endswith(('.obj','.lib','.res')):continue
 if not f.is_absolute():f=build/f
 if not f.is_file():continue
 target=cache/(hashlib.sha256(str(f).lower().encode()).hexdigest()[:12]+'-'+f.name);shutil.copy2(f,target);m['inputs'][str(f)]=sha(f);cached[f]=target;link=link.replace(token,'"'+str(target)+'"')
recipes={}
for log in build.glob('*.dir/Release/*.tlog/CL.command.1.tlog'):
 rows=log.read_text(encoding='utf-16').splitlines()
 for i,row in enumerate(rows[:-1]):
  if row.startswith('^') and '|' not in row:recipes[Path(row[1:]).name.lower()]=rows[i+1]
fixture=ROOT/'tests/fixtures/cpu-diagnostic';productionObjects={}
for variant in ['production','instrumented']:
 folder=out/variant;folder.mkdir(exist_ok=True);newlink=link;extra=[]
 for file in ['src/v2/ImGuiHost.cpp','src/v2/AppUi.cpp','src/v2/BrowserView.cpp','src/v2/LibraryWindow.cpp']:
  original=ROOT/file;name=original.name;obj=folder/(original.stem+'.obj')
  if variant=='instrumented' and name!='ImGuiHost.cpp':shutil.copy2(productionObjects[file],obj)
  else:
   source=original
   if variant=='instrumented':
    source=folder/name;text=original.read_text(encoding='utf-8-sig');marker='void ImGuiHost::frame()\n{';assert marker in text
    text=text.replace(marker,marker+' CPU_QA_SCOPE(0);',1);source.write_text('#include "'+(fixture/'CpuScopes.h').as_posix()+'"\n'+text,encoding='utf-8')
   flags=recipes[name.lower()]
   for dependency in ['CyAudioCore','CyDSP']:
    for form in [str(ROOT.parent/dependency),str(ROOT/'..'/dependency)]:flags=re.sub(re.escape(form),lambda v:str(frozen/dependency),flags,flags=re.I)
   flags=re.sub(r'/Yu"[^"]*"','/Y-',flags,flags=re.I);flags=re.sub(r'/Fp"[^"]*"|/Fo"[^"]*"|/Fd"[^"]*"','',flags,flags=re.I)
   flags=re.sub(re.escape(str(original)),lambda v:'"'+str(source)+'"',flags,flags=re.I)
   flags+=' /I"'+str(original.parent)+'" /Fo"'+str(obj)+'" /Fd"'+str(folder/(original.stem+'.pdb'))+'" /Zi'+(' /DCYDAW_QA_CPU_SCOPES=1' if variant=='instrumented' else '')
   rsp=folder/(original.stem+'.rsp');rsp.write_text(flags,encoding='utf-8');run([str(compiler),'@'+str(rsp)],build,folder/(original.stem+'.log'))
  if variant=='production':productionObjects[file]=obj
  old=build/'CyDAW2.dir/Release'/obj.name;assert old in cached;newlink=newlink.replace('"'+str(cached[old])+'"','"'+str(obj)+'"')
  m['sources'][file]=sha(original)
 if variant=='instrumented':
  obj=folder/'CpuScopes.obj';run([str(compiler),'/c','/nologo','/O2','/MD','/std:c++20','/EHsc','/DCYDAW_QA_CPU_SCOPES=1',str(fixture/'CpuScopes.cpp'),'/Fo'+str(obj)],build,folder/'CpuScopes.log');extra.append(obj)
 for option in ['OUT','PDB','IMPLIB']:newlink=re.sub('/'+option+r':"[^"]*"','',newlink,flags=re.I)
 exe=folder/'CyDAW 2.exe';newlink=' '.join('"'+str(x)+'"' for x in extra)+' '+newlink+' /OUT:"'+str(exe)+'" /DEBUG:FULL /PDB:"'+str(folder/'app.pdb')+'" /OPT:REF /OPT:ICF'
 rsp=folder/'link.rsp';rsp.write_text(newlink,encoding='utf-8');run([str(linker),'@'+str(rsp)],build,folder/'link.log')
 for f in (build/'CyDAW2_artefacts/Release').iterdir():
  if f.is_file() and (f.suffix.lower()=='.dll' or f.name=='CyDAWPluginWorker.exe'):shutil.copy2(f,folder/f.name)
 m['variants'][variant]=dict(exe=str(exe),sha256=sha(exe));print(variant,'ready',flush=True)
m['sources']['src/v2/ImGuiHost.h']=sha(ROOT/'src/v2/ImGuiHost.h');m['sources']['src/v2/FramePacing.h']=sha(ROOT/'src/v2/FramePacing.h')
(out/'build-manifest.json').write_text(json.dumps(m,indent=2),encoding='utf-8')
