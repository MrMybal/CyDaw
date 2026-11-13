"""Explicit QA-only partial relink. Never rebuilds/mutates dependency checkouts.
Reuses immutable Release objects/libraries and recompiles only listed app TUs.
The normal CMake build never includes these scopes or the null-driver gate.
"""
import argparse,hashlib,json,os,re,shutil,subprocess,zipfile,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
assert out.is_relative_to(ROOT/'output')
if not (out/'initial-state.json').exists():subprocess.run([sys.executable,str(ROOT/'tests/cyaudio_cpu_verify.py'),'--output',str(out),'--initial'],check=True)
build=ROOT/'build/cyaudio-native-desktop';fixture=ROOT/'tests/fixtures/cpu-diagnostic'
# Archive committed revisions only. Concurrent uncommitted dependency work is never consumed.
for name,revision in [('CyAudioCore','bb26e93604d43de2069694b0791bc0bcf5a53ea6'),('CyDSP','fda6bd30c5b622795cc08e8bd2750188a6c5a0c0')]:
 destination=out/'frozen'/name
 if not destination.exists():
  archive=out/(name+'.zip');dependency=ROOT.parent/name
  subprocess.run(['git','-c','safe.directory='+dependency.as_posix(),'-C',str(dependency),'archive','--format=zip','--output',str(archive),revision],check=True)
  with zipfile.ZipFile(archive) as z:z.extractall(destination)

vc=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
env={k.upper():v for k,v in os.environ.items()}
for row in subprocess.check_output('cmd /d /s /c ""'+str(vc)+'" >nul && set"',text=True,encoding='utf-8',errors='replace').splitlines():
 if '=' in row:
  k,v=row.split('=',1);env[k.upper()]=v
compiler=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe');linker=compiler.with_name('link.exe')
manifest={'compiler':str(compiler),'configuration':'Release /O2 /Ob2 /MD /fp:precise; QA recompiled TUs add /Zi; original archives reused; no LTO','reusedInputs':{},'commands':[],'sites':[],'recompiled':[]}
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def run(command,cwd,log):
 manifest['commands'].append(dict(command=[str(x) for x in command],cwd=str(cwd),log=str(log)))
 with open(log,'w',encoding='utf-8') as f:r=subprocess.run(command,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:
  print(log.read_text(encoding='utf-8',errors='replace')[-6000:]);raise RuntimeError('Build failed: '+str(log))
def frozen_flags(s):
 for path,name in [(ROOT.parent/'CyAudioCore','CyAudioCore'),(ROOT.parent/'CyDSP','CyDSP')]:
  for form in [str(path),str(ROOT/'..'/name)]:s=re.sub(re.escape(form),lambda m:str(out/'frozen'/name),s,flags=re.I)
 return s
# No physical device: fixed buffer/vector kernel measurements against frozen headers and the exact original SDK archive.
micro=out/'micro';micro.mkdir(exist_ok=True)
dsp=build/'cyaudiocore/cydsp/Release/cydsp.lib';copy=micro/'cydsp.lib';shutil.copy2(dsp,copy);manifest['reusedInputs'][str(dsp)]=sha(dsp)
cmd=[str(compiler),'/nologo','/std:c++20','/O2','/Ob2','/DNDEBUG','/MD','/EHsc','/utf-8','/Zi','/I'+str(ROOT/'include'),'/I'+str(out/'frozen/CyDSP/include'),str(fixture/'BufferMeasurements.cpp'),'/Fo'+str(micro/'buffers.obj'),'/Fd'+str(micro/'buffers.pdb'),'/Fe'+str(micro/'buffers.exe'),str(copy),'/link','/DEBUG:FULL']
run(cmd,micro,micro/'build.log')
print('Microbenchmark built',flush=True)
# Cache original linker inputs. No CMake/MSBuild dependency evaluation can pick up changing sibling sources.
raw=(build/'CyDAW2.dir/Release/CyDAW2.tlog/link.command.1.tlog').read_text(encoding='utf-16').splitlines()[1:]
link=' '.join(raw)
tokens=re.findall(r'"[^"\n]*"|[^\s]+',link)
# Paths with spaces appear inside /OUT etc and are handled below; inputs in this build have no spaces.
cache=out/'link-inputs';cache.mkdir(exist_ok=True)
for token in sorted(set(tokens),key=len,reverse=True):
 candidate=token.strip('"');src=Path(candidate)
 if candidate.startswith('/') or not candidate.lower().endswith(('.obj','.lib','.res')):continue
 if not src.is_absolute():src=build/src
 if not src.is_file():continue # Windows system .libs are resolved by the original SDK LIB search path.
 dest=cache/(hashlib.sha256(str(src).lower().encode()).hexdigest()[:12]+'-'+src.name)
 shutil.copy2(src,dest);manifest['reusedInputs'][str(src)]=sha(src)
 link=link.replace(token,'"'+str(dest)+'"')
 # Remember each object token for selected app object replacement.
# Discover exactly the compile recipe for each source, irrespective of original target.
recipes={}
for log in build.glob('*.dir/Release/*.tlog/CL.command.1.tlog'):
 rows=log.read_text(encoding='utf-16').splitlines()
 for i,row in enumerate(rows[:-1]):
  if row.startswith('^') and row.count('|')==0:recipes[Path(row[1:]).name.lower()]=rows[i+1]
spec={
 'src/app/AudioTrackController.cpp':['GainPanAudioSource::getNextAudioBlock','GainPanAudioSource::processNativeDspGraph','GainPanAudioSource::publishAnalysisFrame','TrackMixer::getNextAudioBlock','MasterOutputAudioSource::getNextAudioBlock','MasterOutputAudioSource::publishAnalysisFrame','AudioDelayLine::process'],
 'src/app/DjEngine.cpp':['DjEngine::getNextAudioBlock'],
 'src/app/IsolatedPluginHost.cpp':['IsolatedPluginHost::processBlock','IsolatedPluginHost::pollWorkerHealth','IsolatedPluginHost::sendCommand','IsolatedPluginHost::getState','IsolatedPluginHost::refreshParameters'],
 'src/v2/Studio.cpp':['Studio::tick'],
 'src/v2/ImGuiHost.cpp':['ImGuiHost::frame'],
 'src/v2/AppUi.cpp':['AppUi::frame','AppUi::drawWindows','AppUi::drawTransportBar','AppUi::drawStatusBar'],
 'src/v2/DevicesView.cpp':['DevicesView::draw','DevicesView::drawCard','DevicesView::drawPlugins'],
 'src/v2/ArrangementView.cpp':['ArrangementView::draw'],
 'src/v2/DjView.cpp':['DjView::draw'],
 'src/base/EventLoop.cpp':['EventLoop::dispatchPending'],
 'src/integration/DeviceManager.cpp':[],
 'src/integration/AudioSources.cpp':['AudioTransportSource::getNextAudioBlock','AudioTransportSource::getCurrentPosition'],
 'src/integration/BufferedAudioSource.cpp':['BufferingAudioSource::getNextAudioBlock'],
 'src/native_plugin/NativeRuntime.cpp':['NativeDspRuntime::process','NativeDspRuntime::captureTaps'],
 'src/engine/AudioRenderPlan.cpp':['AudioRenderPlan::process'],
 'src/engine/AudioBuffer.cpp':['clearAudio','copyAudio','addAudio'],
 'src/engine/ProEffects.cpp':[],
 'src/engine/EffectProcessors.cpp':[],
}
# This is a QA copy of application source only. A missing method is reported, never silently asserted as measured.
def site(name,file,line):
 n=len(manifest['sites']);assert n<256;manifest['sites'].append(dict(id=n,name=name,file=file,line=line));return n
def instrument(text,file):
 changes=[]
 for method in spec[file]:
  pattern=r'(?m)^[^\n;{}]*?\b'+re.escape(method)+r'\s*\([^;{}]*?\)\s*(?:const\s*)?(?:noexcept\s*)?\{'
  matches=list(re.finditer(pattern,text))
  if not matches:print('No scope for',method,flush=True)
  for m in matches:
   n=site(method,file,text[:m.start()].count('\n')+1);changes.append((m.end(),' CPU_QA_SCOPE('+str(n)+'); '))
 if file.endswith(('ProEffects.cpp','EffectProcessors.cpp')):
  for cls in (['ReverbPro','CompressorPro'] if file.endswith('ProEffects.cpp') else ['ModulatedDelay']):
   begin=text.index('class '+cls+' ');match=re.search(r'void process\([^{};]*\) noexcept override\s*\{',text[begin:]);assert match
   at=begin+match.end();n=site(cls+'::process',file,text[:at].count('\n')+1);changes.append((at,' CPU_QA_SCOPE('+str(n)+'); '))
 if file.endswith('AudioRenderPlan.cpp'):
  marker='const ProcessContext& at) noexcept {';at=text.index(marker)+len(marker);n=site('AudioRenderPlan::nodeDispatch',file,text[:at].count('\n')+1);changes.append((at,' CPU_QA_SCOPE('+str(n)+'); '))
 if file.endswith('DeviceManager.cpp'):
  marker='const audio::DeviceCallbackContext &context) override {';at=text.index(marker)+len(marker)
  n=site('DeviceManager::callback',file,text[:at].count('\n')+1);changes.append((at,' CPU_QA_SCOPE('+str(n)+'); '))
 if file.endswith('BufferedAudioSource.cpp'):
  marker='bool fillOne() override {';at=text.index(marker)+len(marker);n=site('ReadAheadCache::fillOne',file,text[:at].count('\n')+1);changes.append((at,' CPU_QA_SCOPE('+str(n)+'); '))
 for at,s in sorted(changes,reverse=True):text=text[:at]+s+text[at:]
 if file.endswith('ImGuiHost.cpp'):
  for token,name in [('onFrame();','UI::onFrame'),('ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());','UI::D3Ddraw'),('n.swapChain->Present(0, 0);','UI::Present')]:
   at=text.index(token);n=site(name,file,text[:at].count('\n')+1)
   text=text[:at]+'{ CPU_QA_SCOPE('+str(n)+'); '+token+' }'+text[at+len(token):]
  text=text.replace('auto& n = *native_;\n    if (n.rendering)', 'auto& n = *native_;\n    if (std::getenv("CYDAW_QA_NO_FRAME")) return;\n    if (n.rendering)',1)
  marker='1.0 / (idle ? 12.0 : refreshHz_)'
  assert marker in text
  text=text.replace(marker,'1.0 / (idle ? 12.0 : (std::getenv("CYDAW_QA_FRAME_HZ") ? std::max(1.0, std::atof(std::getenv("CYDAW_QA_FRAME_HZ"))) : refreshHz_))',1)

 return text
qa_sources=out/'qa-sources';qa_sources.mkdir(exist_ok=True)
variants={}
for variant in ['null-uninstrumented','instrumented']:
 folder=out/variant;folder.mkdir(exist_ok=True);extra=[];newlink=link
 for file in spec:
  # Null-uninstrumented rebuilds only the factory gate; all real DSP stays as originally compiled.
  if variant=='null-uninstrumented' and not file.endswith('DeviceManager.cpp'):continue
  text=(ROOT/file).read_text(encoding='utf-8-sig')
  if file.endswith('DeviceManager.cpp'):
   marker='std::vector<std::unique_ptr<audio::DeviceType>> platformTypes() {'
   assert marker in text
   text=text.replace(marker,marker+'\n if(std::getenv("CYDAW_QA_NULL_AUDIO")) { std::vector<std::unique_ptr<audio::DeviceType>> qa; qa.push_back(createNativeCoreDeviceType("CyDAW QA Null", cyaudio::AudioBackend::nullDevice)); return qa; }\n',1)
   text='#include <cstdlib>\n'+text
  if variant=='instrumented':text='#include "'+str(fixture/'CpuScopes.h').replace('\\','/')+'"\n'+instrument(text,file)
  source=qa_sources/(variant+'-'+Path(file).name);source.write_text(text,encoding='utf-8')
  flags=frozen_flags(recipes[Path(file).name.lower()]);original=Path(file).resolve()
  flags=re.sub(r'/Yu"[^"]*"','/Y-',flags,flags=re.I);flags=re.sub(r'/Fp"[^"]*"','',flags,flags=re.I)
  flags=re.sub(r'/Fo"[^"]*"','',flags,flags=re.I);flags=re.sub(r'/Fd"[^"]*"','',flags,flags=re.I)
  flags=re.sub(re.escape(str(original)),lambda m:'"'+str(source)+'"',flags,flags=re.I)
  obj=folder/(Path(file).stem+'.obj')
  flags+=' /I"'+str((ROOT/file).parent)+'" /Fo"'+str(obj)+'" /Fd"'+str(folder/('compile-'+Path(file).stem+'.pdb'))+'" /Zi /DCYDAW_QA_CPU_SCOPES=1'
  rsp=folder/(Path(file).stem+'.rsp');rsp.write_text(flags,encoding='utf-8')
  run([str(compiler),'@'+str(rsp)],build,folder/(Path(file).stem+'.log'))
  print(variant,'compiled',file,flush=True)
  manifest['recompiled'].append(dict(variant=variant,source=file,originalSha256=sha(ROOT/file),generatedSha256=sha(source)))
  # App .obj was an explicit input; library TUs are replaced by an explicit object before archives.
  originalObject=build/'CyDAW2.dir/Release'/(Path(file).stem+'.obj')
  found=False
  for src,hashValue in manifest['reusedInputs'].items():
   if Path(src)==originalObject:
    cached=cache/(hashlib.sha256(str(Path(src)).lower().encode()).hexdigest()[:12]+'-'+Path(src).name)
    newlink=newlink.replace('"'+str(cached)+'"','"'+str(obj)+'"');found=True;break
  if not found:extra.append(obj)
 if variant=='instrumented':
  obj=folder/'CpuScopes.obj';run([str(compiler),'/c','/nologo','/O2','/MD','/std:c++20','/EHsc','/DCYDAW_QA_CPU_SCOPES=1',str(fixture/'CpuScopes.cpp'),'/Fo'+str(obj)],build,folder/'CpuScopes.log');extra.append(obj)
 exe=folder/'CyDAW 2.exe'
 for option in ['OUT','PDB','IMPLIB']:newlink=re.sub('/'+option+r':"[^"]*"','',newlink,flags=re.I)
 newlink+=' /OUT:"'+str(exe)+'" /MAP:"'+str(folder/'app.map')+'" /DEBUG:FULL /PDB:"'+str(folder/'app.pdb')+'" /OPT:REF /OPT:ICF'
 newlink=' '.join('"'+str(x)+'"' for x in extra)+' '+newlink
 rsp=folder/'link.rsp';rsp.write_text(newlink,encoding='utf-8');run([str(linker),'@'+str(rsp)],build,folder/'link.log')
 for f in (build/'CyDAW2_artefacts/Release').iterdir():
  if f.is_file() and (f.suffix.lower()=='.dll' or f.name in ['CyDAWPluginWorker.exe']):shutil.copy2(f,folder/f.name)
 variants[variant]=dict(exe=str(exe),sha256=sha(exe))
manifest['variants']=variants
(out/'build-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print('QA binaries ready',flush=True)
