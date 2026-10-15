"""Small advanced-module evaluation pack, using the unchanged native DSP."""
import json
import math
from catalogue import ROOT, WORK, prepare
from make_runtime import prepare as runtime_prepare

SELECTION = [
    ('comp-pro', 'Compresseur Pro'), ('reverb-pro', 'Reverbe Pro'),
    ('halo', 'Halo'), ('givre', 'Givre'),
    ('prism-instrument', 'Prisme'), ('quartz-instrument', 'Quartz'),
]

def main():
    prepare()
    runtime_prepare()
    catalogue = json.loads((WORK/'catalogue.json').read_text(encoding='utf-8'))
    cmake = (WORK/'CMakeLists.txt').read_text(encoding='utf-8')
    cmake += '''
add_library(CatalogueRuntime STATIC "${CMAKE_CURRENT_SOURCE_DIR}/RuntimeSources/CatalogueRuntime.cpp")
target_include_directories(CatalogueRuntime PRIVATE
 "${CYDAW_SOURCE_ROOT}/src/app" "${CYDAW_SOURCE_ROOT}/src/graph_plugin"
 "${CYDAW_SOURCE_ROOT}/tools/vst3_pack" "$<TARGET_PROPERTY:CatalogueCommon,INCLUDE_DIRECTORIES>")
target_compile_definitions(CatalogueRuntime PRIVATE "$<TARGET_PROPERTY:CatalogueCommon,COMPILE_DEFINITIONS>")
target_compile_features(CatalogueRuntime PRIVATE cxx_std_20)
target_link_libraries(CatalogueRuntime PRIVATE CatalogueCommon juce::juce_recommended_config_flags)
'''
    manifest = []
    for i, (key, label) in enumerate(SELECTION, 1):
        d = next(d for d in catalogue if d['key']==key and d['status']=='pending')
        raw = json.loads((WORK/'Graphes'/f'{d["id"]}.json').read_text(encoding='utf-8'))
        assert not raw.get('error'), raw.get('error')
        state = {k: raw[k] for k in ['editor', 'native', 'polySynth', 'publishedParameters']}
        state['schema'] = 2
        params = [p for p in state['publishedParameters'] if p['exportedToHost']]
        state['publishedParameters'] = params
        values = [(p['default']-p['minimum'])/(p['maximum']-p['minimum']) if p['maximum']>p['minimum'] else 0 for p in params]
        state.update(hostValues=values, programs=[dict(name='Default', hostValues=values)], program=0)
        name = 'CyDAW '+label
        code = f'T{i:03d}'
        state['editor'].update(graphName=name, graphAuthor='CyDAW', graphDescription='Advanced V2 module; local evaluation build.')
        state['editor']['vst3Export'].update(productName=name, companyName='CyDAW', manufacturerCode='CyDw', pluginCode=code, version='0.2.0')
        ui = state['editor']['presentation']
        visible = [p for p in ui['parameters'] if p['showInDevice']][:30]
        rows = max(1, math.ceil(len(visible)/5))
        def control(kind, label, binding, x, y, w, h, id):
            return dict(id=id,kind=kind,label=label,binding=binding,secondaryBinding='',tertiaryBinding='',quaternaryBinding='',x=x,y=y,width=w,height=h,portKind=0,portChannels=1)
        controls = [control('text',name,'',.035,.02,.8,.07,1)]
        for j,p in enumerate(visible):
            controls.append(control('knob',p['label'],p['key'],.025+(j%5)*.19,.13+(j//5)*.79/rows,.18,.72/rows,j+2))
        ui.update(controls=controls,deviceControls=controls,nextControlId=len(controls)+1,windowWidth=1000,windowHeight=760)
        plugin = dict(name=name, inputs=0 if d['instrument'] else 2, outputs=2, acceptsMidi=d['instrument'], producesMidi=False, midiOnly=False, deviceKey=key, state=state)
        folder = WORK/'Selection'/key
        folder.mkdir(parents=True, exist_ok=True)
        (folder/'plugin.json').write_text(json.dumps(plugin,ensure_ascii=False,indent=2),encoding='utf-8')
        target = f'AdvancedTest{i:02d}'
        manifest.append(dict(key=key,name=name,target=target,code=code,category=d['category'],instrument=d['instrument'],parameters=len(params)))
        cmake += f'''
juce_add_plugin({target} COMPANY_NAME "CyDAW" PRODUCT_NAME "{name}" VERSION "0.2.0"
 BUNDLE_ID "audio.cydaw.test.{key}" PLUGIN_MANUFACTURER_CODE CyDw PLUGIN_CODE {code}
 FORMATS VST3 COPY_PLUGIN_AFTER_BUILD FALSE
 VST3_CATEGORIES {'Instrument Synth' if d['instrument'] else 'Fx'}
 IS_SYNTH {'TRUE' if d['instrument'] else 'FALSE'}
 NEEDS_MIDI_INPUT {'TRUE' if d['instrument'] else 'FALSE'} NEEDS_MIDI_OUTPUT FALSE)
target_sources({target} PRIVATE "${{CYDAW_SOURCE_ROOT}}/tools/vst3_pack/ThinWrapper.cpp")
target_include_directories({target} PRIVATE "${{CYDAW_SOURCE_ROOT}}/src/graph_plugin"
 "${{CYDAW_SOURCE_ROOT}}/tools/vst3_pack" "$<TARGET_PROPERTY:CatalogueCommon,INCLUDE_DIRECTORIES>")
target_compile_definitions({target} PRIVATE "$<TARGET_PROPERTY:CatalogueCommon,COMPILE_DEFINITIONS>")
target_compile_features({target} PRIVATE cxx_std_20)
target_link_libraries({target} PRIVATE CatalogueRuntime juce::juce_recommended_config_flags)
add_custom_command(TARGET {target}_VST3 POST_BUILD
 COMMAND ${{CMAKE_COMMAND}} -E make_directory "$<TARGET_FILE_DIR:{target}_VST3>/../Resources"
 COMMAND ${{CMAKE_COMMAND}} -E copy_if_different "${{CMAKE_CURRENT_SOURCE_DIR}}/Selection/{key}/plugin.json" "$<TARGET_FILE_DIR:{target}_VST3>/../Resources/plugin.json")
'''
    cmake += '\nadd_custom_target(AdvancedTests DEPENDS '+' '.join(d['target']+'_VST3' for d in manifest)+')\n'
    cmake += '''
juce_add_console_app(AdvancedBenchmark PRODUCT_NAME "CyDAW Advanced Test Benchmark")
target_sources(AdvancedBenchmark PRIVATE "${CYDAW_SOURCE_ROOT}/tools/vst3_pack/Benchmark.cpp")
target_compile_features(AdvancedBenchmark PRIVATE cxx_std_20)
target_compile_definitions(AdvancedBenchmark PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(AdvancedBenchmark PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
add_dependencies(AdvancedTests AdvancedBenchmark)
juce_add_console_app(AdvancedPreview PRODUCT_NAME "CyDAW Advanced Editor Preview")
target_sources(AdvancedPreview PRIVATE "${CYDAW_SOURCE_ROOT}/tools/vst3_pack/RuntimePreview.cpp")
target_include_directories(AdvancedPreview PRIVATE "${CYDAW_SOURCE_ROOT}/src/graph_plugin"
 "${CYDAW_SOURCE_ROOT}/tools/vst3_pack" "$<TARGET_PROPERTY:CatalogueCommon,INCLUDE_DIRECTORIES>")
target_compile_definitions(AdvancedPreview PRIVATE "$<TARGET_PROPERTY:CatalogueCommon,COMPILE_DEFINITIONS>")
target_compile_features(AdvancedPreview PRIVATE cxx_std_20)
target_link_libraries(AdvancedPreview PRIVATE CatalogueRuntime juce::juce_recommended_config_flags)
add_dependencies(AdvancedTests AdvancedPreview)
'''
    (WORK/'CMakeLists.txt').write_text(cmake,encoding='utf-8')
    (WORK/'selection.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Advanced selection:',[(d['name'],d['parameters']) for d in manifest])

if __name__=='__main__': main()
