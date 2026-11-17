"""Relink QA copies of BOTH frozen desktop versions with identical UI probes.
Reuses original O2 link inputs; never invokes dependency builds. UI-only scopes.
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--reuse-from',type=Path);a=p.parse_args();out=a.output.resolve()
assert out.is_relative_to(ROOT/'output');out.mkdir(parents=True,exist_ok=True)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
fixture=ROOT/'tests/fixtures/cpu-diagnostic';local=out/'probes';local.mkdir(exist_ok=True)
h=(fixture/'CpuScopes.h').read_text(encoding='utf-8-sig').replace('start{}, children{}','start{}, children{}, cycleStart{}, childCycles{}')
h=h.replace('struct Scope {','bool frameDue() noexcept;\nstruct Scope {')
(local/'CpuScopes.h').write_text(h,encoding='utf-8')
c=(fixture/'CpuScopes.cpp').read_text(encoding='utf-8-sig')
c=c.replace('exclusive{};','exclusive{}, cycles{}, exclusiveCycles{};')
c=c.replace('Scope::Scope(unsigned id)', 'static std::uint64_t cycleNow() noexcept { ULONG64 v{}; QueryThreadCycleTime(GetCurrentThread(), &v); return v; }\nbool frameDue() noexcept { static const double hz=[] { auto* v=std::getenv("CYDAW_QA_FRAME_HZ"); return v?std::atof(v):0.; }(); if(hz<=0)return true; static thread_local double last{}; LARGE_INTEGER f;QueryPerformanceFrequency(&f); const auto t=double(now())/f.QuadPart; if(t-last<1./hz)return false;last=t;return true; }\nScope::Scope(unsigned id)')
c=c.replace('start=now();','start=now(); cycleStart=cycleNow();')
c=c.replace('const auto elapsed=now()-start;', 'const auto elapsed=now()-start; const auto cycles=cycleNow()-cycleStart;')
c=c.replace('if(parent) parent->children+=elapsed;', 'if(parent) { parent->children+=elapsed; parent->childCycles+=cycles; }')
c=c.replace('c.calls.fetch_add(1,std::memory_order_relaxed);','c.cycles.fetch_add(cycles,std::memory_order_relaxed); c.exclusiveCycles.fetch_add(cycles>=childCycles?cycles-childCycles:0,std::memory_order_relaxed);\n c.calls.fetch_add(1,std::memory_order_relaxed);')
c=c.replace('exclusiveSeconds\\n','exclusiveSeconds,inclusiveCycles,exclusiveCycles\\n')
c=c.replace('%.9f,%lu,%u,%llu,%.9f,%.9f\\n','%.9f,%lu,%u,%llu,%.9f,%.9f,%llu,%llu\\n')
c=c.replace('double(c.exclusive.load())/freq.QuadPart);','double(c.exclusive.load())/freq.QuadPart,(unsigned long long)c.cycles.load(),(unsigned long long)c.exclusiveCycles.load());')
(local/'CpuScopes.cpp').write_text(c,encoding='utf-8')
vc=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat');compiler=vc.parents[2]/'Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe';linker=compiler.with_name('link.exe')
env={k.upper():v for k,v in os.environ.items()}
for row in subprocess.check_output('cmd /d /s /c ""'+str(vc)+'" >nul && set"',text=True,encoding='utf-8',errors='replace').splitlines():
 if '=' in row:k,v=row.split('=',1);env[k.upper()]=v
manifest=dict(compiler=str(compiler),configuration='Original Release O2, MD, fp:precise, no LTO; selected UI TUs recompiled /Zi /Y-; same probes; original audio/SDK objects retained',variants={},sites=[],commands=[],inputs={})
def run(cmd,cwd,log):
 manifest['commands'].append(dict(command=[str(x) for x in cmd],cwd=str(cwd),log=str(log)))
 with log.open('w',encoding='utf-8') as f:r=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:print(log.read_text(encoding='utf-8',errors='replace')[-4500:]);raise RuntimeError(str(log))
def site(name,file,line):
 n=len(manifest['sites']);manifest['sites'].append(dict(id=n,name=name,file=file,line=line));return n
spec={'src/v2/ImGuiHost.cpp':['ImGuiHost::frame'], 'src/v2/AppUi.cpp':['AppUi::frame','AppUi::drawWindows'], 'src/v2/DevicesView.cpp':['DevicesView::draw','DevicesView::drawCard','DevicesView::drawPlugins','cardFace','fitWidth','flowControls'], 'src/v2/DeviceVisuals.cpp':['inferSurface','drawPane','formatValue','paneBody','compProPlot'], 'src/app/GraphDocumentEditorAccess.cpp':['GraphDocument::editorNodes']}
for label,source,build in [('JUCE',ROOT/'output/juce-core-comparison/juce-source',ROOT/'build/juce-comparison'),('native',ROOT,ROOT/'build/cyaudio-native-desktop')]:
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
   actual=method if label=='native' else method.replace('GraphDocument::','NodeGraphComponent::')
   pattern=r'(?m)^[^\n;{}]*?\b'+re.escape(actual)+r'\s*\([^;{}]*?\)\s*(?:const\s*)?(?:noexcept\s*)?\{'
   matches=list(re.finditer(pattern,text));assert matches,(file,method)
   for m in matches:
    n=site(method,file,text[:m.start()].count('\n')+1);prefix=' if(captureTarget_ == decltype(captureTarget_){} && !::cydaw_cpu_qa::frameDue()) return; ' if method=='ImGuiHost::frame' else ''
    edits.append((m.end(),prefix+' CPU_QA_SCOPE('+str(n)+'); '))
  if file.endswith('ImGuiHost.cpp'):
   marker='void ImGuiHost::vblank(const double timestampSeconds)\n{';assert marker in text
   at=text.index(marker)+len(marker);edits.append((at,' if(std::getenv("CYDAW_QA_FRAME_HZ")) { if(native_->backendsReady) frame(); return; } '))
   for token,name in [('onFrame();','UI::onFrame'),('ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());','UI::D3Ddraw'),('n.swapChain->Present(0, 0);','UI::Present')]:
    at=text.index(token);n=site(name,file,text[:at].count('\n')+1);edits.append((at+len(token),' }'));edits.append((at,'{ CPU_QA_SCOPE('+str(n)+'); '))
  for at,s in sorted(edits,reverse=True):text=text[:at]+s+text[at:]
  if label=='JUCE' and file.endswith('ImGuiHost.cpp'):
   # A 30-Hz fallback plus a strict 30-Hz guard loses ticks to jitter.
   # In the capped experiment only, feed the SAME frame guard at high cadence.
   text=text.replace('fallback_.startTimerHz(30);','fallback_.startTimerHz(std::getenv("CYDAW_QA_FRAME_HZ") ? 1000 : 30);')
   text=text.replace('if (now - host.lastVBlankMs_ > 200.0)','if (std::getenv("CYDAW_QA_FRAME_HZ") || now - host.lastVBlankMs_ > 200.0)')
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
  if a.reuse_from and not file.endswith(('ImGuiHost.cpp','AppUi.cpp')):
   previous=a.reuse_from.resolve();pm=json.loads((previous/'build-manifest.json').read_text(encoding='utf-8'))
   assert pm['variants'][label]['sourceHashes'][file]==sha(original)
   assert pm['sites'][:len(manifest['sites'])]==manifest['sites']
   assert (previous/'probes/CpuScopes.h').read_bytes()==(local/'CpuScopes.h').read_bytes()
   assert (previous/label/'sources'/Path(file).name).read_text(encoding='utf-8-sig').split('\n',1)[1]==text.split('\n',2)[2]
   prior=previous/label/obj.name;shutil.copy2(prior,obj);manifest.setdefault('reusedQaObjects',[]).append(dict(source=str(prior),sha256=sha(prior),target=str(obj)))
  else:run([str(compiler),'@'+str(rsp)],build,folder/(Path(file).stem+'.log'))
  print(label,file,flush=True)
  old=build/'CyDAW2.dir/Release'/obj.name
  if old in cached:link=link.replace('"'+str(cached[old])+'"','"'+str(obj)+'"')
  else:extra.append(obj)
 obj=folder/'CpuScopes.obj';run([str(compiler),'/c','/nologo','/O2','/MD','/std:c++20','/EHsc','/DCYDAW_QA_CPU_SCOPES=1',str(local/'CpuScopes.cpp'),'/Fo'+str(obj)],build,folder/'CpuScopes.log');extra.append(obj)
 for option in ['OUT','PDB','IMPLIB']:link=re.sub('/'+option+r':"[^"]*"','',link,flags=re.I)
 exe=folder/'CyDAW 2.exe';link=' '.join('"'+str(x)+'"' for x in extra)+' '+link+' /OUT:"'+str(exe)+'" /DEBUG:FULL /PDB:"'+str(folder/'app.pdb')+'" /OPT:REF /OPT:ICF'
 rsp=folder/'link.rsp';rsp.write_text(link,encoding='utf-8');run([str(linker),'@'+str(rsp)],build,folder/'link.log')
 for f in (build/'CyDAW2_artefacts/Release').iterdir():
  if f.is_file() and (f.suffix.lower()=='.dll' or f.name=='CyDAWPluginWorker.exe'):shutil.copy2(f,folder/f.name)
 manifest['variants'][label]=dict(exe=str(exe),sha256=sha(exe),sourceHashes=hashes,generatedHashes={f.name:sha(f) for f in generated.glob('*.cpp')})
 (out/'build-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
 print(label,'ready',flush=True)
