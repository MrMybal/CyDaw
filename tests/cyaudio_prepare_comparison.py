"""Prepare the frozen JUCE reference and common panel without changing the active checkout.
Requires the application's existing JUCE/CyUI/FFmpeg SDKs and built native fixtures.
All generated files stay under this repository. No reset, checkout, deletion or push.
"""
import argparse,json,os,shutil,subprocess,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'output/juce-core-comparison')
p.add_argument('--build',type=Path,default=ROOT/'build/juce-comparison')
p.add_argument('--native-build',type=Path,default=ROOT/'build/cyaudio-native-desktop')
p.add_argument('--juce-source',type=Path,default=ROOT/'build/desktop/_deps/juce-src')
p.add_argument('--cyui-source',type=Path,default=ROOT.parent/'CyImgui_Interface')
p.add_argument('--revision',default='cf4228d5ed18897b786bb10f4844673593c21c5a')
fallback=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
p.add_argument('--cmake',default=shutil.which('cmake') or str(fallback));p.add_argument('--skip-build',action='store_true')
a=p.parse_args();a.output=a.output.resolve();a.build=a.build.resolve();source=a.output/'juce-source'
for directory in (a.output,a.build):
    if not directory.is_relative_to(ROOT):p.error('Generated paths must stay under the application checkout')
for directory in (a.juce_source,a.cyui_source,ROOT/'third_party/ffmpeg'):
    if not directory.is_dir():p.error('Existing SDK missing: '+str(directory))
a.output.mkdir(parents=True,exist_ok=True)
git=['git','-c','safe.directory='+ROOT.as_posix()]
def run(command,log=None):
    if log:
        with open(log,'w',encoding='utf-8') as out:subprocess.run(command,cwd=ROOT,stdout=out,stderr=subprocess.STDOUT,check=True)
    else:subprocess.run(command,cwd=ROOT,check=True)
revision=subprocess.check_output([*git,'rev-parse',a.revision],cwd=ROOT,text=True).strip()
marker=a.output/'reference-source.json'
if source.exists():
    if not marker.exists() or json.loads(marker.read_text())['revision']!=revision:
        p.error('Reference already exists without a matching preparation marker; choose a fresh --output')
else:
    archive=a.output/'juce-source.zip'
    run([*git,'archive','--format=zip','--output',str(archive),revision])
    with zipfile.ZipFile(archive) as z:z.extractall(source)
    subprocess.run([*git,'apply','--unidiff-zero','--directory='+source.relative_to(ROOT).as_posix()],cwd=ROOT,input=(ROOT/'tests/fixtures/juce-benchmark-profile.patch').read_bytes().replace(b'\r\n',b'\n'),check=True)
    marker.write_text(json.dumps(dict(revision=revision,profilePatch='tests/fixtures/juce-benchmark-profile.patch'),indent=2)+'\n')
configure=[a.cmake,'-S',str(source),'-B',str(a.build),'-G','Visual Studio 17 2022','-A','x64',
    '-DCYDAW_JUCE_SOURCE_DIR='+str(a.juce_source.resolve()),'-DFETCHCONTENT_SOURCE_DIR_CYUI='+str(a.cyui_source.resolve()),
    '-DCYDAW_FFMPEG_ROOT='+str(ROOT/'third_party/ffmpeg'),'-DCYDAW_FACTORY_CONTENT_DIR='+str(ROOT/'content/factory'),
    '-DCYDAW_BUILD_TESTS=OFF','-DCYDAW_BUILD_GRAPH_RUNTIME_PLUGIN=OFF','-DCYDAW_ENABLE_LTO=OFF',
    '-DCYDAW_ENABLE_ASIO=ON','-DCYDAW_ENABLE_JACK=OFF','-DCYDAW_FETCH_JUCE=OFF','-DCMAKE_CXX_FLAGS=/MP4',
    '-DPython3_EXECUTABLE='+sys.executable,
    '-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES='+str(ROOT/'tests/fixtures/juce-benchmark-optimization.cmake')]
build=[a.cmake,'--build',str(a.build),'--config','Release','--target','CyDAW2','--parallel','4']
(a.output/'build-commands.json').write_text(json.dumps(dict(configure=configure,build=build),indent=2)+'\n')
if not a.skip_build:run(configure,a.output/'juce-o2-configure.log');run(build,a.output/'juce-o2-build.log')
ctest=Path(a.cmake).with_name('ctest.exe' if os.name=='nt' else 'ctest')
panel=json.loads(subprocess.check_output([str(ctest),'--test-dir',str(a.native_build.resolve()),'-C','Release','--show-only=json-v1'],cwd=ROOT,text=True,encoding='utf-8'))
fixtures={fmt:ROOT/path for fmt,path in dict(clap='build/cyaudio-native-clap/CLAP/Release/CyGraphPresets.clap',lv2='build/cyaudio-native/LV2/Release/CyGraphPresets.lv2',vst3='build/cyaudio-native-vst3/VST3/Release/CyGraphPresets.vst3').items()}
if all(path.exists() for path in fixtures.values()):
    command=[sys.executable,str(ROOT/'tests/v2_plugin_formats_check.py'),'--exe',str(a.native_build.resolve()/'CyDAW2_artefacts/Release/CyDAW 2.exe'),'--output',str(a.output/'formats'),'--port','48194']
    for fmt,file in fixtures.items():command+=['--'+fmt,str(file)]
    panel['tests'].append(dict(name='cydaw2_plugin_formats_check',command=command))
(a.output/'ctest-panel.json').write_text(json.dumps(panel,indent=2)+'\n')
print('Shared executable scenarios:',sum('--exe' in t.get('command',[]) for t in panel['tests']))
