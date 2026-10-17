"""Current full catalogue; the original V2 source tree is never rewritten."""
import argparse
import json
import os
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/'output/vst3-catalogue-2026-10-15'
# Where the pack goes: CYDAW_VST_PACK_DEST, else CyDaw_VST beside the project.
DEST=Path(os.environ.get('CYDAW_VST_PACK_DEST', str(ROOT.parent/'CyDaw_VST')))

STUDIO={'eq-pro','comp-pro','reverb-pro','limiter-pro','filter-pro'}
MIX={'pression','fournaise','echo-bande','multibande-pro','velours','patine','sonometre','tranche','finition','halo'}
GUITAR={'amp-sim','cab-sim','mic-sim'}
CREATIVE={'vocoder','harmonie','justesse','de-esser','pompe','empreinte','givre','grains','saccade','boucleur','decaleur','anneau','rotor','resonateurs','corps','peigne'}
UTIL={'utilitaire','tuner','stereoscope'}
BASIC={'chorus','flanger','phaser','tremolo-pan','bitcrusher','multimode-filter','auto-filter','eq-three','limiter','gate','transient-shaper','ping-pong','mono-to-stereo','stereo-to-mono','spectrometer'}
ADVANCED={'sound-model-synth','batterie-instrument','sampler-instrument','simple-sampler-instrument','nuee-instrument','prism-instrument','ambre-instrument','colosse-instrument','mosaique-instrument','nebuleuse-instrument','opale-instrument','onyx-instrument','tirette-instrument','alliage-instrument','quartz-instrument','argile-instrument','saphir-instrument','derive-instrument','bronze-instrument','silex-instrument','rack-instrument','layer-instrument','selector-instrument','jade-instrument','kick-instrument'}
EXCLUDED={'sound-model-synth','batterie-instrument','sampler-instrument','simple-sampler-instrument','nuee-instrument','rack-instrument','layer-instrument','selector-instrument','sound-model','sampler','batterie','drum-rack','instrument-layer','instrument-selector'}

def category(d):
    key=d['key']
    if d['input'] in ['control','transport'] or d['output']=='control':
        return 'Construction/Controle'
    if d['input']=='midi' and d['output']=='midi':
        return 'Effets/MIDI/Modules' if d['kind']=='module' else 'Effets/MIDI/Noeuds'
    if d['instrument'] or d['input']=='none':
        return 'Instruments/Avances' if key in ADVANCED else 'Instruments/Basiques/'+('Modules' if d['kind']=='module' else 'Graphes' if d['kind']=='preset' else 'Noeuds')
    if key in STUDIO: return 'Effets/Avances/Studio'
    if key in MIX: return 'Effets/Avances/Mixage'
    if key in GUITAR: return 'Effets/Avances/Guitare et voix'
    if key in CREATIVE: return 'Effets/Avances/Creatifs'
    if key in UTIL: return 'Effets/Avances/Utilitaires'
    if key in BASIC: return 'Effets/Basiques/Cartes'
    return 'Effets/Basiques/'+('Chaines' if d['kind']=='preset' else 'Modules' if d['kind']=='module' else 'Noeuds')

def prepare():
    devices=json.loads((WORK/'list_devices.json').read_text(encoding='utf-8'))['result']['devices']
    factory=json.loads((WORK/'factory_instruments.json').read_text(encoding='utf-8'))['result']['instruments']
    jobs=[]
    for i,d in enumerate(devices):
        j=dict(d,id=f'{i+1:03d}-{d["kind"]}-{d["key"]}',category=category(d),status='pending')
        if d['key']=='voice-allocator':
            j.update(status='construction-source',reason='Allocateur de voix interne aux instruments polyphoniques.')
        if d['kind']=='node' and d['key'].endswith('-synth') and any(x['key']==d['key'][:-6]+'-instrument' for x in devices):
            j.update(status='alias',alias=d['key'][:-6]+'-instrument',reason='Meme moteur que l\'instrument avance ; conserve comme source de construction.')
        if d['kind']=='node' and d['key'] in ['sampler','batterie','sound-model']:
            j.update(status='alias',alias={'sampler':'sampler-instrument','batterie':'batterie-instrument','sound-model':'sound-model-synth'}[d['key']])
        if d['key'] in ['drum-rack','rack-instrument','instrument-layer','layer-instrument','instrument-selector','selector-instrument']:
            j.update(status='unavailable',reason='Le moteur des pads appartient au DAW et ne joue pas dans le VST3 actuel.')
        if j['category'].startswith('Construction'):
            j.update(status='construction-source',reason='Bloc de controle pour construire un graphe ; pas un effet audio ou MIDI autonome.')
        if d['key'] in EXCLUDED:
            j.update(status='excluded',reason='Hors perimetre demande : samples, modeles de sons enregistres, ou pads.')
        if d['key'] in ['poly-oscillator','envelope','voice-filter']:
            j.update(status='construction-source',reason='Brique de la chaine polyphonique ; exportee dans le synthé polyphonique nodal.')
        jobs.append(j)
    # The user explicitly excludes factory samples and recorded sound models.
    (WORK/'catalogue.json').write_text(json.dumps(jobs,ensure_ascii=False,indent=2),encoding='utf-8')
    (WORK/'jobs.json').write_text(json.dumps([j for j in jobs if j['status']=='pending'],ensure_ascii=False),encoding='utf-8')
    original=(ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
    part=original.split('function(cydaw_add_graph_runtime_plugin target_name)',1)[1].split('endfunction()',1)[0]
    sources=re.findall(r'"\$\{cydaw_root\}/([^\"]+\.cpp)"',part)
    sources=[s for s in sources if not s.endswith('GraphRuntimePlugin.cpp')]
    sources.append('src/app/NodeGraphComponentEditorAccess.cpp')
    cmake='''cmake_minimum_required(VERSION 3.24)
project(CyDAWFullCatalogue VERSION 0.1.0 LANGUAGES C CXX)
set(CYDAW_SOURCE_ROOT "'''+ROOT.as_posix()+'''" CACHE PATH "CyDAW")
set(CYDAW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CYDAW_BUILD_GRAPH_EDITOR OFF CACHE BOOL "" FORCE)
set(CYDAW_BUILD_GRAPH_RUNTIME_PLUGIN OFF CACHE BOOL "" FORCE)
set(CYDAW_ENABLE_LTO OFF CACHE BOOL "" FORCE)
add_subdirectory("${CYDAW_SOURCE_ROOT}" cydaw-sdk EXCLUDE_FROM_ALL)
add_library(CatalogueCommon STATIC
'''+''.join(f' "${{CYDAW_SOURCE_ROOT}}/{s}"\n' for s in sources)+''')
target_include_directories(CatalogueCommon PRIVATE "${CYDAW_SOURCE_ROOT}/src/app")
target_compile_features(CatalogueCommon PRIVATE cxx_std_20)
target_compile_definitions(CatalogueCommon PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)
target_link_libraries(CatalogueCommon PRIVATE CyDAW::Core CyDAWTranslations juce::juce_audio_processors
 juce::juce_audio_utils juce::juce_data_structures juce::juce_gui_extra juce::juce_recommended_config_flags)
juce_add_console_app(CatalogueExport PRODUCT_NAME "CyDAW Catalogue Export")
target_sources(CatalogueExport PRIVATE "${CYDAW_SOURCE_ROOT}/tools/vst3_pack/ExportCatalogue.cpp")
target_include_directories(CatalogueExport PRIVATE "${CYDAW_SOURCE_ROOT}/src/app" "$<TARGET_PROPERTY:CatalogueCommon,INCLUDE_DIRECTORIES>")
target_compile_definitions(CatalogueExport PRIVATE "$<TARGET_PROPERTY:CatalogueCommon,COMPILE_DEFINITIONS>")
target_compile_features(CatalogueExport PRIVATE cxx_std_20)
target_link_libraries(CatalogueExport PRIVATE CatalogueCommon juce::juce_recommended_config_flags)
'''
    (WORK/'CMakeLists.txt').write_text(cmake,encoding='utf-8')
    print('Catalogue:',len(jobs),'entrees ;',sum(j['status']=='pending' for j in jobs),'graphes a compiler')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['prepare'])
    args=parser.parse_args();globals()[args.action]()
