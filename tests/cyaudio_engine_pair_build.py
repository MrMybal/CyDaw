"""Relink QA copies of BOTH frozen desktop versions with matched engine probes.
Reuses original O2 link inputs; never invokes dependency builds. Engine scopes, with UI rendering disabled in both variants during measurements.
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve()
assert out.is_relative_to(ROOT/'output');out.mkdir(parents=True,exist_ok=True)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
fixture=ROOT/'tests/fixtures/cpu-diagnostic';local=out/'probes';local.mkdir(exist_ok=True)
for name in ['CpuScopes.h','CpuScopes.cpp']:
 shutil.copy2(fixture/name,local/name)
vc=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat');compiler=vc.parents[2]/'Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe';linker=compiler.with_name('link.exe')
env={k.upper():v for k,v in os.environ.items()}
for row in subprocess.check_output('cmd /d /s /c ""'+str(vc)+'" >nul && set"',text=True,encoding='utf-8',errors='replace').splitlines():
 if '=' in row:k,v=row.split('=',1);env[k.upper()]=v
manifest=dict(compiler=str(compiler),configuration='Original Release O2, MD, fp:precise, no LTO; selected application TUs recompiled /Zi /Y-; matched app audio probes; frozen SDK archives retained; no-frame controls',variants={},sites=[],commands=[],inputs={})
def run(cmd,cwd,log):
 manifest['commands'].append(dict(command=[str(x) for x in cmd],cwd=str(cwd),log=str(log)))
 with log.open('w',encoding='utf-8') as f:r=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:print(log.read_text(encoding='utf-8',errors='replace')[-4500:]);raise RuntimeError(str(log))
def site(name,file,line):
 n=len(manifest['sites']);manifest['sites'].append(dict(id=n,name=name,file=file,line=line));return n
spec={
 'src/v2/ImGuiHost.cpp':['ImGuiHost::frame'],
 'src/v2/AppUi.cpp':[],
 'src/app/AudioTrackController.cpp':['MasterOutputAudioSource::getNextAudioBlock','TrackMixer::getNextAudioBlock','GainPanAudioSource::getNextAudioBlock','GainPanAudioSource::processNativeDspGraph','GainPanAudioSource::publishAnalysisFrame','MasterOutputAudioSource::publishAnalysisFrame','AudioDelayLine::process'],
 'src/app/DjEngine.cpp':['DjEngine::getNextAudioBlock'],
 'src/app/IsolatedPluginHost.cpp':['IsolatedPluginHost::processBlock','IsolatedPluginHost::pollWorkerHealth','IsolatedPluginHost::refreshParameters'],
 'src/native_plugin/NativeRuntime.cpp':['NativeDspRuntime::process','NativeDspRuntime::captureTaps'],
 'src/engine/AudioRenderPlan.cpp':['AudioRenderPlan::process'],
 'src/engine/AudioBuffer.cpp':['clearAudio','copyAudio','addAudio'],
 'src/engine/ProEffects.cpp':[],
 'src/engine/EffectProcessors.cpp':[],
}
commonSpec=dict(spec)
for label,source,build in [('JUCE',ROOT/'output/juce-core-comparison/juce-source',ROOT/'build/juce-comparison'),('native',ROOT,ROOT/'build/cyaudio-native-desktop')]:
 spec=dict(commonSpec)
 if label=='native':spec.update({'src/integration/AudioSources.cpp':['AudioTransportSource::getNextAudioBlock','AudioTransportSource::getCurrentPosition'],'src/integration/BufferedAudioSource.cpp':['BufferingAudioSource::getNextAudioBlock'],'src/integration/DeviceManager.cpp':[]})
 folder=out/label;folder.mkdir(exist_ok=True);cache=folder/'inputs';cache.mkdir(exist_ok=True);generated=folder/'sources';generated.mkdir(exist_ok=True)
 raw=(build/'CyDAW2.dir/Release/CyDAW2.tlog/link.command.1.tlog').read_text(encoding='utf-16').splitlines()[1:];link=' '.join(raw);cached={}
 for token in sorted(set(re.findall(r'"[^"\n]*"|[^\s]+',link)),key=len,reverse=True):
  candidate=token.strip('"');f=Path(candidate)
  if candidate.startswith('/') or not candidate.lower().endswith(('.obj','.lib','.res')):continue
  if not f.is_absolute():f=build/f
  if not f.is_file():continue
  dest=cache/(hashlib.sha256(str(f).lower().encode()).hexdigest()[:12]+'-'+f.name);shutil.copy2(f,dest);cached[f]=dest;manifest['inputs'][str(f)]=sha(f);link=link.replace(token,'"'+str(dest)+'"')
 recipes={}
 for log in build.glob('*.dir/Release/*.tlog/CL.command.1.tlog'):
  rows=log.read_text(encoding='utf-16').splitlines()
  for i,row in enumerate(rows[:-1]):
   if row.startswith('^') and '|' not in row:recipes[Path(row[1:]).name.lower()]=rows[i+1]
 extra=[];hashes={}
 for common,methods in spec.items():
  file=common if label=='native' else common.replace('GraphDocumentEditorAccess','NodeGraphComponentEditorAccess')
  original=source/file;text=original.read_text(encoding='utf-8-sig');hashes[file]=sha(original);edits=[]
  for method in methods:
   actual=method
   pattern=r'(?m)^[^\n;{}]*?\b'+re.escape(actual)+r'\s*\([^;{}]*?\)\s*(?:const\s*)?(?:noexcept\s*)?\{'
   matches=list(re.finditer(pattern,text));assert matches,(file,method)
   for m in matches:
    n=site(method,file,text[:m.start()].count('\n')+1);prefix=' if(std::getenv("CYDAW_QA_NO_FRAME")) return; ' if method=='ImGuiHost::frame' else ''
    edits.append((m.end(),prefix+' CPU_QA_SCOPE('+str(n)+'); '))
  if file.endswith(('ProEffects.cpp','EffectProcessors.cpp')):
   for cls in (['ReverbPro','CompressorPro'] if file.endswith('ProEffects.cpp') else ['ModulatedDelay']):
    begin=text.index('class '+cls+' ');match=re.search(r'void process\([^{};]*\) noexcept override\s*\{',text[begin:]);assert match
    at=begin+match.end();n=site(cls+'::process',file,text[:at].count('\n')+1);edits.append((at,' CPU_QA_SCOPE('+str(n)+'); '))
  for suffix,marker,name in [('AudioRenderPlan.cpp','const ProcessContext& at) noexcept {','AudioRenderPlan::nodeDispatch'),('DeviceManager.cpp','const audio::DeviceCallbackContext &context) override {','DeviceManager::callback'),('BufferedAudioSource.cpp','bool fillOne() override {','ReadAheadCache::fillOne')]:
   if file.endswith(suffix):
    at=text.index(marker)+len(marker);n=site(name,file,text[:at].count('\n')+1);edits.append((at,' CPU_QA_SCOPE('+str(n)+'); '))
  for at,s in sorted(edits,reverse=True):text=text[:at]+s+text[at:]
  if file.endswith('AppUi.cpp'):
   marker='AppUi::layoutFile() const\n{';assert marker in text
   at=text.index(marker)+len(marker)
   fileType='juce::File' if label=='JUCE' else 'cydaw::base::File'
   text=text[:at]+' if(auto* profile=std::getenv("CYDAW_BENCH_PROFILE")) return '+fileType+'(profile).getChildFile("CyDAW").getChildFile("CyDAW2.layout.ini"); '+text[at:]
  text='#include <cstdlib>\n'+text
  text='#include "'+str(local/'CpuScopes.h').replace('\\','/')+'"\n'+text
  target=generated/Path(file).name;target.write_text(text,encoding='utf-8')
  flags=recipes[Path(file).name.lower()]
  # Freeze native SDK headers to the exact already-tested SDK archive.
  if label=='native':
   for name in ['CyAudioCore','CyDSP']:
    for form in [str(ROOT.parent/name),str(ROOT/'..'/name)]:flags=re.sub(re.escape(form),lambda m:str(ROOT/'output/cyaudio-cpu-diagnostic/2026-11-13/frozen'/name),flags,flags=re.I)
  flags=re.sub(r'/Yu"[^"]*"','/Y-',flags,flags=re.I);flags=re.sub(r'/Fp"[^"]*"|/Fo"[^"]*"|/Fd"[^"]*"','',flags,flags=re.I)
  flags=re.sub(re.escape(str(original)),lambda m:'"'+str(target)+'"',flags,flags=re.I)
  obj=folder/(Path(file).stem+'.obj');flags+=' /I"'+str(original.parent)+'" /Fo"'+str(obj)+'" /Fd"'+str(folder/(Path(file).stem+'.pdb'))+'" /Zi /DCYDAW_QA_CPU_SCOPES=1'
  rsp=folder/(Path(file).stem+'.rsp');rsp.write_text(flags,encoding='utf-8')
  run([str(compiler),'@'+str(rsp)],build,folder/(Path(file).stem+'.log'))
  print(label,file,flush=True)
  old=build/'CyDAW2.dir/Release'/obj.name
  if old in cached:link=link.replace('"'+str(cached[old])+'"','"'+str(obj)+'"')
  else:extra.append(obj)
  if file.endswith('AppUi.cpp'):
   control=folder/'control';control.mkdir(exist_ok=True)
   observer=control/'CpuScopes.obj';run([str(compiler),'/c','/nologo','/O2','/MD','/std:c++20','/EHsc','/DCYDAW_QA_CPU_SCOPES=1',str(local/'CpuScopes.cpp'),'/Fo'+str(observer)],build,control/'CpuScopes.log')
   controlLink=link
   for option in ['OUT','PDB','IMPLIB']:controlLink=re.sub('/'+option+r':"[^"]*"','',controlLink,flags=re.I)
   controlExe=control/'CyDAW 2.exe';controlLink='"'+str(observer)+'" '+controlLink+' /OUT:"'+str(controlExe)+'" /DEBUG:FULL /PDB:"'+str(control/'app.pdb')+'" /OPT:REF /OPT:ICF'
   controlRsp=control/'link.rsp';controlRsp.write_text(controlLink,encoding='utf-8');run([str(linker),'@'+str(controlRsp)],build,control/'link.log')
   for runtime in (build/'CyDAW2_artefacts/Release').iterdir():
    if runtime.is_file() and (runtime.suffix.lower()=='.dll' or runtime.name=='CyDAWPluginWorker.exe'):shutil.copy2(runtime,control/runtime.name)
   manifest.setdefault('controls',{})[label]=dict(exe=str(controlExe),sha256=sha(controlExe))
 obj=folder/'CpuScopes.obj';run([str(compiler),'/c','/nologo','/O2','/MD','/std:c++20','/EHsc','/DCYDAW_QA_CPU_SCOPES=1',str(local/'CpuScopes.cpp'),'/Fo'+str(obj)],build,folder/'CpuScopes.log');extra.append(obj)
 for option in ['OUT','PDB','IMPLIB']:link=re.sub('/'+option+r':"[^"]*"','',link,flags=re.I)
 exe=folder/'CyDAW 2.exe';link=' '.join('"'+str(x)+'"' for x in extra)+' '+link+' /OUT:"'+str(exe)+'" /DEBUG:FULL /PDB:"'+str(folder/'app.pdb')+'" /OPT:REF /OPT:ICF'
 rsp=folder/'link.rsp';rsp.write_text(link,encoding='utf-8');run([str(linker),'@'+str(rsp)],build,folder/'link.log')
 for f in (build/'CyDAW2_artefacts/Release').iterdir():
  if f.is_file() and (f.suffix.lower()=='.dll' or f.name=='CyDAWPluginWorker.exe'):shutil.copy2(f,folder/f.name)
 manifest['variants'][label]=dict(exe=str(exe),sha256=sha(exe),sourceHashes=hashes,generatedHashes={f.name:sha(f) for f in generated.glob('*.cpp')})
 (out/'build-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
 print(label,'ready',flush=True)
