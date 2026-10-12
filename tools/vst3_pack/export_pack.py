"""Export the current CyDAW V2 graphs through its native control API."""
import argparse
import json
import math
import re
import socket
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACK = ROOT / 'output' / 'vst3-pack-2026-10-09'
DEVICES = [
    ('chorus', 'Chorus', False), ('flanger', 'Flanger', False),
    ('phaser', 'Phaser', False), ('tremolo-pan', 'Tremolo', False),
    ('bitcrusher', 'Bitcrusher', False), ('eq-three', 'EQ Three', False),
    ('comp-pro', 'Pro Compressor', False), ('limiter-pro', 'Pro Limiter', False),
    ('transient-shaper', 'Transient Shaper', False), ('ping-pong', 'Ping Pong', False),
    ('reverb-pro', 'Pro Reverb', False), ('multimode-filter', 'Multimode Filter', False),
    ('table-synth', 'Table Synth', True), ('string-keys', 'String Keys', True),
]

def request(cmd, **args):
    with socket.create_connection(('127.0.0.1', 48011), timeout=10) as sock:
        sock.settimeout(60)
        sock.sendall((json.dumps(dict(cmd=cmd, **args))+'\n').encode())
        reply = json.loads(sock.makefile('r', encoding='utf-8').readline())
    if not reply.get('ok'):
        raise RuntimeError((cmd, args, reply))
    return reply.get('result', {})

def export():
    PACK.mkdir(parents=True, exist_ok=True)
    result = []
    for i, (key, label, instrument) in enumerate(DEVICES, 1):
        name = 'CyDAW ' + label
        folder = PACK/'Sources'/key
        if (folder/'workshop.json').is_file():
            cached=json.loads((folder/'workshop.json').read_text())
            parameters=sum(p['vst3'] for p in cached['parameters'])
            result.append(dict(key=key,name=name,instrument=instrument,target=f'CyPack{i:02d}',
                code=f'C{i:03d}',check=dict(ready=True,parameters=parameters),project=str(folder/'projet')))
            print('EXPORTED (cached)',name,parameters,'parameters',flush=True)
            continue
        if instrument:
            request('add_midi_track', name=name, instrument=key)
            status = request('workshop', action='open_track', track=name)
        else:
            status = request('workshop', action='new', name=name)
            graph = status['graph']
            input_id = next(n['id'] for n in graph['nodes'] if n['type'].startswith('graph-audio-input'))
            output_id = next(n['id'] for n in graph['nodes'] if n['type'].startswith('graph-audio-output'))
            node = request('add_node', track='workshop', key=key, x=.5, y=.45)
            request('disconnect', track='workshop', source=input_id, destination=output_id)
            request('connect', track='workshop', source=input_id, destination=node['id'])
            request('connect', track='workshop', source=node['id'], destination=output_id)
        request('workshop', action='info', name=name, author='CyDAW',
                description='Native CyDAW V2 graph export; local runtime evaluation.')
        check = request('workshop', action='vst3_check', productName=name,
                        companyName='CyDAW', manufacturerCode='CyDw', pluginCode=f'C{i:03d}')
        assert check['ready'], check
        assert check['parameters'] > 0 and check['parameters'] <= 32, check
        request('workshop', action='programs', op='add', name='Default')
        folder = PACK/'Sources'/key
        generated = request('workshop', action='vst3_export', folder=str(folder), build=False)
        status = request('workshop', action='status')
        (folder/'workshop.json').write_text(json.dumps(status, indent=2), encoding='utf-8')
        result.append(dict(key=key, name=name, instrument=instrument, target=f'CyPack{i:02d}',
                           code=f'C{i:03d}', check=check, project=generated['project']))
        print('EXPORTED',name,check['parameters'],'parameters',flush=True)
        request('workshop', action='close')
    (PACK/'catalogue.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    request('quit')

def configure_sources():
    devices = json.loads((PACK/'catalogue.json').read_text())
    original = (ROOT/'CMakeLists.txt').read_text()
    function = original.split('function(cydaw_add_graph_runtime_plugin target_name)',1)[1].split('endfunction()',1)[0]
    sources = re.findall(r'"\$\{cydaw_root\}/([^\"]+\.cpp)"',function)
    sources = [p for p in sources if not p.endswith('GraphRuntimePlugin.cpp')]
    prefix = '''cmake_minimum_required(VERSION 3.24)
project(CyDAWEvaluationPack VERSION 0.1.0 LANGUAGES C CXX)
set(CYDAW_SOURCE_ROOT "" CACHE PATH "CyDAW source root")
set(CYDAW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CYDAW_BUILD_GRAPH_EDITOR OFF CACHE BOOL "" FORCE)
set(CYDAW_BUILD_GRAPH_RUNTIME_PLUGIN OFF CACHE BOOL "" FORCE)
set(CYDAW_ENABLE_LTO ON CACHE BOOL "" FORCE)
add_subdirectory("${CYDAW_SOURCE_ROOT}" cydaw-sdk EXCLUDE_FROM_ALL)
# Compile JUCE and the common graph code once; each VST3 still embeds its own
# identity, graph, host parameters and factory program in GraphRuntimePlugin.
add_library(CyPackCommon STATIC
'''
    cmake = prefix + ''.join(f'  "${{CYDAW_SOURCE_ROOT}}/{p}"\n' for p in sources) + ''')
target_include_directories(CyPackCommon PRIVATE "${CYDAW_SOURCE_ROOT}/src/app")
target_compile_features(CyPackCommon PRIVATE cxx_std_20)
target_compile_definitions(CyPackCommon PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
  JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)
target_link_libraries(CyPackCommon PRIVATE CyDAW::Core CyDAWTranslations
  juce::juce_audio_processors juce::juce_audio_utils juce::juce_data_structures
  juce::juce_gui_extra juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags)
'''
    targets=[]
    for d in devices:
        t=d['target']
        targets.append(t+'_VST3')
        cmake+=f'''\njuce_add_plugin({t}
  COMPANY_NAME "CyDAW" PRODUCT_NAME "{d['name']}" VERSION "0.1.0"
  BUNDLE_ID "audio.cydaw.{d['key']}" PLUGIN_MANUFACTURER_CODE CyDw
  PLUGIN_CODE {d['code']} FORMATS VST3 COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES {'Instrument Synth' if d['instrument'] else 'Fx'}
  IS_SYNTH {'TRUE' if d['instrument'] else 'FALSE'}
  NEEDS_MIDI_INPUT {'TRUE' if d['instrument'] else 'FALSE'} NEEDS_MIDI_OUTPUT FALSE)
target_sources({t} PRIVATE "${{CYDAW_SOURCE_ROOT}}/src/graph_plugin/GraphRuntimePlugin.cpp")
target_include_directories({t} BEFORE PRIVATE
  "${{CMAKE_CURRENT_SOURCE_DIR}}/Sources/{d['key']}/projet/generated"
  "${{CYDAW_SOURCE_ROOT}}/src/app"
  "$<TARGET_PROPERTY:CyPackCommon,INCLUDE_DIRECTORIES>")
target_compile_definitions({t} PRIVATE "$<TARGET_PROPERTY:CyPackCommon,COMPILE_DEFINITIONS>")
target_compile_features({t} PRIVATE cxx_std_20)
target_link_libraries({t} PRIVATE CyPackCommon juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags)
'''
    cmake+='\nadd_custom_target(CyPackAll DEPENDS '+' '.join(targets)+')\n'
    cmake+='''
juce_add_console_app(CyPackBenchmark PRODUCT_NAME "CyDAW VST3 Benchmark")
target_sources(CyPackBenchmark PRIVATE "${CYDAW_SOURCE_ROOT}/tools/vst3_pack/Benchmark.cpp")
target_compile_features(CyPackBenchmark PRIVATE cxx_std_20)
target_compile_definitions(CyPackBenchmark PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(CyPackBenchmark PRIVATE juce::juce_audio_processors
  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
add_dependencies(CyPackAll CyPackBenchmark)
juce_add_console_app(CyPackPreview PRODUCT_NAME "CyDAW Graph Presentation Preview")
target_sources(CyPackPreview PRIVATE "${CYDAW_SOURCE_ROOT}/tools/vst3_pack/Preview.cpp")
target_include_directories(CyPackPreview PRIVATE "${CYDAW_SOURCE_ROOT}/src/app"
  "$<TARGET_PROPERTY:CyPackCommon,INCLUDE_DIRECTORIES>")
target_compile_definitions(CyPackPreview PRIVATE "$<TARGET_PROPERTY:CyPackCommon,COMPILE_DEFINITIONS>")
target_compile_features(CyPackPreview PRIVATE cxx_std_20)
target_link_libraries(CyPackPreview PRIVATE CyPackCommon juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags)
'''
    (PACK/'CMakeLists.txt').write_text(cmake,encoding='utf-8')
    print('BATCH PROJECT READY',PACK,flush=True)

def prepare_ui():
    """Give standalone effect exports an actual knob panel, not the blank
    workshop canvas. Only presentation data changes; the DSP stays native."""
    for entry in json.loads((PACK/'catalogue.json').read_text()):
        project=Path(entry['project'])
        file=project/'generated/CyDawGeneratedGraphConfig.h'
        header=file.read_text(encoding='utf-8')
        start=header.index('inline constexpr std::array<const char*,')
        fragment=header[start:]
        strings=re.findall(r'"(?:\\.|[^"\\])*"',fragment)
        state=json.loads(''.join(json.loads(s) for s in strings))
        ui=state['editor']['presentation']
        parameters=[p for p in ui['parameters'] if p['showInDevice']]
        columns=5 if len(parameters)>12 else 4
        rows=math.ceil(len(parameters)/columns)
        def widget(kind,label,binding,x,y,w,h,i):
            return dict(id=i,kind=kind,label=label,binding=binding,secondaryBinding='',
                tertiaryBinding='',quaternaryBinding='',x=x,y=y,width=w,height=h,portKind=0,portChannels=1)
        controls=[widget('text',entry['name'],'',.04,.025,.72,.08,1),
                  widget('level-meter','Output','',.81,.035,.15,.075,2)]
        for i,p in enumerate(parameters):
            controls.append(widget('knob',p['label'],p['key'],.035+(i%columns)*.94/columns,
                .15+(i//columns)*.67/rows,.85/columns,.61/rows,i+3))
        controls.append(widget('waveform','Output','',.04,.86,.92,.11,len(controls)+1))
        ui.update(controls=controls,deviceControls=controls,nextControlId=len(controls)+1,
                  windowWidth=880,windowHeight=max(420,rows*100+160))
        state_json=json.dumps(state,ensure_ascii=False,separators=(',',':'))
        chunks=[state_json[i:i+2048] for i in range(0,len(state_json),2048)]
        header=header[:start]+f'inline constexpr std::array<const char*, {len(chunks)}> embeddedStateJsonChunks {{{{\n'
        header+=''.join('    '+json.dumps(c,ensure_ascii=False)+',\n' for c in chunks)
        header+='}};\n\n} // namespace cydaw::generated_graph\n'
        file.write_text(header,encoding='utf-8')
        manifest_file=project/'GraphManifest.cydawvst3.json'
        manifest=json.loads(manifest_file.read_text(encoding='utf-8'))
        manifest['graphEditorState']=state['editor']
        manifest['evaluationPackLayout']='knob-grid-v1; DSP contract unchanged'
        manifest_file.write_text(json.dumps(manifest,ensure_ascii=False),encoding='utf-8')
        (project/'RuntimeState.json').write_text(json.dumps(state,ensure_ascii=False),encoding='utf-8')
        print('UI',entry['name'],len(parameters),'knobs',flush=True)

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('action',choices=['export','configure_sources','prepare_ui'])
    args=parser.parse_args()
    globals()[args.action]()
