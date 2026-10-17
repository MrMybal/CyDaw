cmake_minimum_required(VERSION 3.24)

project(CyDAW VERSION 0.77.0 LANGUAGES C CXX)

option(CYDAW_BUILD_TESTS "Build the dependency-free core tests" ON)
option(CYDAW_BUILD_DESKTOP "Build the JUCE desktop shell" ON)
option(CYDAW_BUILD_GRAPH_EDITOR "Build the standalone CyDAW Graph Editor" ON)
option(CYDAW_BUILD_GRAPH_RUNTIME_PLUGIN
       "Build the editable CyDAW Graph Runtime VST3" ON)
option(CYDAW_FETCH_JUCE "Download the pinned JUCE release during configure" OFF)
option(CYDAW_ENABLE_ASIO "Enable the ASIO backend on Windows (licence-sensitive)" OFF)
option(CYDAW_ENABLE_JACK "Enable JACK where JUCE supports it" ON)
option(CYDAW_ENABLE_VST2 "Enable legacy VST2 hosting with a legally obtained SDK" OFF)
set(CYDAW_JUCE_SOURCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
# The instruments CyDAW ships (tools/factory_content builds them): a package
# carries them as "Factory" beside the executable; a development build finds
# them here.
set(CYDAW_FACTORY_CONTENT_DIR "${CMAKE_SOURCE_DIR}/content/factory" CACHE PATH
    "The factory content folder (sampler instruments, sound models)")
add_compile_definitions(CYDAW_FACTORY_CONTENT_DIR="${CYDAW_FACTORY_CONTENT_DIR}")
# Link-time optimisation is what actually sets the edit-build-run cycle here:
# measured on this tree, touching an 88-line source file cost 33.7 s, of which
# 33 s was the LTO link and only a fraction the compile. Keep it on for the
# builds that ship and let day-to-day work turn it off.
option(CYDAW_ENABLE_LTO "Link the desktop targets with link-time optimisation" ON)
option(CYDAW_ENABLE_PCH "Precompile the JUCE surface shared by the desktop sources" ON)
set(CYDAW_VST2_SDK_PATH "" CACHE PATH
    "Root containing pluginterfaces/vst2.x/aeffect.h from a licensed historical VST2 SDK")
set(CYDAW_WINDOWS_PLUGIN_WORKER "" CACHE FILEPATH
    "Optional prebuilt Windows CyDAWPluginWorker.exe bundled with Linux packages")

if(CYDAW_WINDOWS_PLUGIN_WORKER)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(NOT EXISTS "${CYDAW_WINDOWS_PLUGIN_WORKER}")
            message(FATAL_ERROR
                "CYDAW_WINDOWS_PLUGIN_WORKER does not exist: "
                "${CYDAW_WINDOWS_PLUGIN_WORKER}")
        endif()
    else()
        message(WARNING
            "CYDAW_WINDOWS_PLUGIN_WORKER is only bundled by Linux builds; "
            "the value is ignored on ${CMAKE_SYSTEM_NAME}.")
    endif()
endif()

set(CYDAW_ASIO_AVAILABLE OFF)
if(WIN32 AND CYDAW_ENABLE_ASIO)
    set(CYDAW_ASIO_AVAILABLE ON)
endif()

set(CYDAW_JACK_AVAILABLE OFF)
if(CYDAW_ENABLE_JACK AND (WIN32 OR APPLE OR UNIX))
    find_path(CYDAW_JACK_INCLUDE_DIR jack/jack.h
        HINTS
            "$ENV{JACK_ROOT}"
            "$ENV{JACK_SDK_DIR}"
            "C:/Program Files/JACK2"
            "C:/Program Files (x86)/JACK2"
        PATH_SUFFIXES include)
    if(CYDAW_JACK_INCLUDE_DIR)
        set(CYDAW_JACK_AVAILABLE ON)
    else()
        message(WARNING
            "CYDAW_ENABLE_JACK is ON, but jack/jack.h was not found. "
            "CyDAW will be built without JACK; install the JACK development headers "
            "or set JACK_ROOT/JACK_SDK_DIR to enable it.")
    endif()
endif()

set(CYDAW_VST2_AVAILABLE OFF)
if(CYDAW_BUILD_DESKTOP AND CYDAW_ENABLE_VST2)
    if(NOT CYDAW_VST2_SDK_PATH)
        message(FATAL_ERROR
            "CYDAW_ENABLE_VST2=ON requires CYDAW_VST2_SDK_PATH. "
            "Point it to a legally obtained historical SDK root containing "
            "pluginterfaces/vst2.x/aeffect.h and aeffectx.h.")
    endif()
    if(NOT EXISTS "${CYDAW_VST2_SDK_PATH}/pluginterfaces/vst2.x/aeffect.h"
       OR NOT EXISTS "${CYDAW_VST2_SDK_PATH}/pluginterfaces/vst2.x/aeffectx.h")
        message(FATAL_ERROR
            "The configured CYDAW_VST2_SDK_PATH does not contain the required "
            "pluginterfaces/vst2.x/aeffect.h and aeffectx.h files. "
            "CyDAW never downloads or redistributes this legacy SDK.")
    endif()
    set(CYDAW_VST2_AVAILABLE ON)
endif()

message(STATUS
    "CyDAW audio backends: ASIO=${CYDAW_ASIO_AVAILABLE}, JACK=${CYDAW_JACK_AVAILABLE}; "
    "plugin formats: VST3=ON, VST2=${CYDAW_VST2_AVAILABLE}")

function(cydaw_configure_audio_backends target_name)
    target_compile_definitions(${target_name} PRIVATE
        JUCE_ASIO=$<BOOL:${CYDAW_ASIO_AVAILABLE}>
        JUCE_JACK=$<BOOL:${CYDAW_JACK_AVAILABLE}>
    )
    if(CYDAW_JACK_AVAILABLE)
        target_include_directories(${target_name} PRIVATE "${CYDAW_JACK_INCLUDE_DIR}")
    endif()
endfunction()

function(cydaw_configure_plugin_formats target_name)
    target_compile_definitions(${target_name} PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=1
        JUCE_PLUGINHOST_VST=$<BOOL:${CYDAW_VST2_AVAILABLE}>
    )
endfunction()

add_library(cydaw_core STATIC
    src/native_plugin/NativeRuntime.cpp
    src/graph/GraphDefinition.cpp
    src/graph/GraphPluginExport.cpp
    src/graph/PrimitiveRegistry.cpp
    src/engine/AudioGraph.cpp
    src/engine/AudioBuffer.cpp
    src/engine/AudioRenderPlan.cpp
    src/engine/BuiltinProcessors.cpp
    src/engine/EffectProcessors.cpp
    src/engine/ProEffects.cpp
    src/engine/AmpDesign.cpp
    src/engine/AmpEffects.cpp
    src/engine/MixEffects.cpp
    src/engine/SuiteEffects.cpp
    src/engine/HaloReverb.cpp
    src/engine/AddedEffects.cpp
    src/engine/MidiEffects.cpp
    src/engine/VoiceEffects.cpp
    src/engine/PitchEffects.cpp
    src/engine/SpaceEffects.cpp
    src/engine/RhythmEffects.cpp
    src/engine/MotionEffects.cpp
    src/engine/ResonanceEffects.cpp
    src/engine/UtilityEffects.cpp
    src/engine/Convolver.cpp
    src/engine/TimeStretch.cpp
    src/engine/Loudness.cpp
    src/engine/DjMix.cpp
    src/engine/FoundrySound.cpp
    src/engine/FoundrySession.cpp
    src/engine/KickSynth.cpp
    src/engine/DrumSynth.cpp
    src/engine/WavetableShape.cpp
    src/engine/SoundModel.cpp
    src/engine/PrismSynth.cpp
    src/engine/SynthFamily.cpp
    src/engine/AmbreSynth.cpp
    src/engine/ColosseSynth.cpp
    src/engine/MosaiqueSynth.cpp
    src/engine/NebuleuseSynth.cpp
    src/engine/BronzeSynth.cpp
    src/engine/JadeSynth.cpp
    src/engine/SilexSynth.cpp
    src/engine/DeriveSynth.cpp
    src/engine/SaphirSynth.cpp
    src/engine/ArgileSynth.cpp
    src/engine/QuartzSynth.cpp
    src/engine/AlliageSynth.cpp
    src/engine/TiretteSynth.cpp
    src/engine/OnyxSynth.cpp
    src/engine/OpaleSynth.cpp
    src/engine/SynthTables.cpp
    src/engine/DrumKit.cpp
    src/engine/Sampler.cpp
    src/engine/AudioAnalysis.cpp
    src/engine/GraphDefinitionCompiler.cpp
    src/engine/GraphInstrumentCompiler.cpp
    src/engine/NativeDspCompiler.cpp
    src/engine/RealtimeEngine.cpp
    src/engine/Transport.cpp
    src/platform/AudioBackend.cpp
    src/plugins/PluginIsolation.cpp
    src/modules/ModuleLibrary.cpp
    src/ui/WorkspaceLayout.cpp
)
add_library(CyDAW::Core ALIAS cydaw_core)

target_include_directories(cydaw_core PUBLIC include)
target_compile_features(cydaw_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(cydaw_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(cydaw_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CYDAW_BUILD_TESTS)
    enable_testing()
    add_executable(cydaw_core_tests tests/CoreTests.cpp)
    target_link_libraries(cydaw_core_tests PRIVATE CyDAW::Core)
    if(MSVC)
        target_compile_options(cydaw_core_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(cydaw_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    # A test gets its own user profile and its own temp directory, so it
    # cannot read what another test — or the running application — left
    # behind, and cannot leave anything behind either. Self-test logs land in
    # <build>/test-sandbox/<test>/temp/cydaw-self-test.log.
    function(cydaw_configure_test test_name timeout_seconds)
        set(sandbox "${CMAKE_BINARY_DIR}/test-sandbox/${test_name}")
        file(MAKE_DIRECTORY "${sandbox}/appdata")
        file(MAKE_DIRECTORY "${sandbox}/localappdata")
        file(MAKE_DIRECTORY "${sandbox}/temp")
        set_tests_properties(${test_name} PROPERTIES
            TIMEOUT ${timeout_seconds}
            ENVIRONMENT
                "APPDATA=${sandbox}/appdata;LOCALAPPDATA=${sandbox}/localappdata;TEMP=${sandbox}/temp;TMP=${sandbox}/temp")
    endfunction()

    add_test(NAME cydaw_core_tests COMMAND cydaw_core_tests)
    add_executable(cydaw_transport_timecode_tests tests/TransportTimecodeTests.cpp)
    target_compile_features(cydaw_transport_timecode_tests PRIVATE cxx_std_20)
    add_test(NAME cydaw_transport_timecode_tests COMMAND cydaw_transport_timecode_tests)
    cydaw_configure_test(cydaw_core_tests 120)

    add_executable(cydaw_engine_tests tests/EngineTests.cpp)
    target_link_libraries(cydaw_engine_tests PRIVATE CyDAW::Core)
    if(MSVC)
        target_compile_options(cydaw_engine_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(cydaw_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME cydaw_engine_tests COMMAND cydaw_engine_tests)
    cydaw_configure_test(cydaw_engine_tests 120)
endif()

if(CYDAW_BUILD_DESKTOP)
    if(CYDAW_JUCE_SOURCE_DIR)
        add_subdirectory("${CYDAW_JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
    elseif(CYDAW_FETCH_JUCE)
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.15
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(JUCE)
    endif()

    # Identity for every executable's Windows version resource. Without these
    # JUCE stamps CompanyName as "yourcompany" and leaves the copyright empty,
    # which is what an untouched template looks like to a heuristic scanner.
    # JUCE_COMPANY_NAME and JUCE_COMPANY_COPYRIGHT are inherited properties,
    # so the directory carries them to every target defined below.
    set_property(DIRECTORY PROPERTY JUCE_COMPANY_NAME "CyDAW")
    set_property(DIRECTORY PROPERTY JUCE_COMPANY_COPYRIGHT
                 "Copyright (C) 2026 CyDAW")

    # JUCE compiles its module .cpp files as part of each consuming target, and
    # those files refuse to build once a precompiled header has already pulled
    # in the module headers. Collect them so the targets that use a precompiled
    # header can opt them out.
    if(TARGET juce::juce_core)
        get_target_property(cydaw_juce_modules_dir juce::juce_core
                            INTERFACE_JUCE_MODULE_PATH)
        if(NOT cydaw_juce_modules_dir)
            set(cydaw_juce_modules_dir "${JUCE_SOURCE_DIR}/modules")
        endif()
        file(GLOB_RECURSE CYDAW_JUCE_MODULE_SOURCES
             "${cydaw_juce_modules_dir}/*.cpp"
             "${cydaw_juce_modules_dir}/*.mm")
    endif()

    # Applies a precompiled header to CyDAW's own sources only. Without the
    # exclusion the JUCE module units fail with "Incorrect use of JUCE cpp file".
    function(cydaw_use_precompiled_header target_name)
        if(NOT CYDAW_ENABLE_PCH)
            return()
        endif()
        target_precompile_headers(${target_name} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/app/CyDawPrecompiled.h>")
        if(CYDAW_JUCE_MODULE_SOURCES)
            set_source_files_properties(${CYDAW_JUCE_MODULE_SOURCES}
                TARGET_DIRECTORY ${target_name}
                PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
        endif()
    endfunction()

    if(CYDAW_ENABLE_LTO)
        set(CYDAW_LTO_FLAGS juce::juce_recommended_lto_flags)
    else()
        set(CYDAW_LTO_FLAGS "")
        message(STATUS "CyDAW: link-time optimisation disabled for faster incremental builds")
    endif()

    if(TARGET juce::juce_gui_extra)
        if(CYDAW_VST2_AVAILABLE)
            juce_set_vst2_sdk_path("${CYDAW_VST2_SDK_PATH}")
        endif()
        juce_add_binary_data(CyDAWTranslations
            HEADER_NAME CyDAWTranslations.h
            NAMESPACE cydaw::translations
            SOURCES
                resources/i18n/en.json
                resources/i18n/fr.json
        )
        juce_add_gui_app(CyDAW
            PRODUCT_NAME "CyDAW"
            VERSION "${PROJECT_VERSION}"
            BUILD_VERSION "${PROJECT_VERSION}"
        )
        if(WIN32)
            get_target_property(CYDAW_JUCE_INFO_FILE CyDAW JUCE_INFO_FILE)
            get_target_property(CYDAW_JUCE_GENERATED_DIR
                                CyDAW JUCE_GENERATED_SOURCES_DIRECTORY)
            add_custom_command(
                OUTPUT "${CYDAW_JUCE_GENERATED_DIR}/CyDAW_resources.rc"
                APPEND
                DEPENDS "${CYDAW_JUCE_INFO_FILE}")
        endif()
        target_sources(CyDAW PRIVATE
            src/app/Main.cpp
            src/app/AutomationEditorComponent.cpp
            src/app/AutomationEditorComponent.h
            src/app/AudioEdit.h
            src/app/AudioEditorComponent.cpp
            src/app/AudioEditorComponent.h
            src/app/AudioTrackController.cpp
            src/app/DjEngine.cpp
            src/app/FoundryPlayer.cpp
            src/app/AudioTrackController.h
            src/app/ClipLauncherComponent.cpp
            src/app/ClipLauncherComponent.h
            src/app/MixerComponent.cpp
            src/app/MixerComponent.h
            src/app/MixerStripDrawing.h
            src/app/ClipTimelineEditorComponent.cpp
            src/app/ClipTimelineEditorComponent.h
            src/app/CyDawLookAndFeel.cpp
            src/app/CyDawLookAndFeel.h
            src/app/DeviceChainComponent.cpp
            src/app/DeviceChainComponent.h
            src/app/GraphManagerComponent.cpp
            src/app/GraphManagerComponent.h
            src/app/GraphPluginExportComponent.cpp
            src/app/GraphPluginExportComponent.h
            src/app/GraphPluginProjectGenerator.cpp
            src/app/GraphPluginProjectGenerator.h
            src/app/GraphPolySynthRenderer.cpp
            src/app/GraphPolySynthRenderer.h
            src/app/IsolatedPluginHost.cpp
            src/app/IsolatedPluginHost.h
            src/app/Localization.cpp
            src/app/Localization.h
            src/app/LibraryBrowserComponent.cpp
            src/app/LibraryBrowserComponent.h
            src/app/ModuleInterfaceEditor.cpp
            src/app/ModuleInterfaceEditor.h
            src/app/NativeDspGraphState.cpp
            src/app/NativeDspGraphState.h
            src/app/NativeDspRuntime.cpp
            src/app/NativeDspRuntime.h
            src/app/NodeGraphComponent.cpp
            src/app/SoundModelFile.cpp
            src/app/SamplerMapFile.cpp
            src/app/NodeGraphComponentFactoryData.cpp
            src/app/NodeGraphComponent.h
            src/app/MessageThreadSampler.cpp
            src/app/MessageThreadSampler.h
            src/app/PianoRollComponent.cpp
            src/app/PianoRollComponent.h
            src/app/ProjectRecovery.cpp
            src/app/ProjectRecovery.h
            src/app/TrackLaneComponent.cpp
            src/app/TrackLaneComponent.h
            src/app/TimelineRulerComponent.cpp
            src/app/TimelineRulerComponent.h
            src/app/VirtualAudioBus.cpp
            src/app/VirtualAudioBus.h
            src/app/VirtualBusManagerComponent.cpp
            src/app/VirtualBusManagerComponent.h
            src/app/WaveformComponent.cpp
            src/app/WaveformComponent.h
            src/app/WindowsPluginBridge.cpp
            src/app/WindowsPluginBridge.h
            src/app/RemoteControlServer.cpp
            src/app/RemoteControlServer.h
            src/app/SampleExplorerComponent.cpp
            src/app/SampleExplorerComponent.h
            src/app/SampleLibrary.cpp
            src/app/SampleLibrary.h
            src/app/WorkspaceComponent.cpp
            src/app/WorkspaceComponentInternal.h
            src/app/WorkspaceComponentRemote.cpp
            src/app/WorkspaceComponentImport.cpp
            src/app/ProjectImport.cpp
            src/app/ProjectImport.h
            src/app/WorkspaceComponentSelfTests.cpp
            src/app/WorkspaceComponent.h
        )
        target_compile_features(CyDAW PRIVATE cxx_std_20)
        target_compile_definitions(CyDAW PRIVATE
            JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CyDAW,JUCE_PRODUCT_NAME>"
            JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0
        )
        cydaw_configure_plugin_formats(CyDAW)
        cydaw_configure_audio_backends(CyDAW)
        # The JUCE surface dominates compilation of every src/app file, so it is
        # parsed once per target instead of once per translation unit.
        cydaw_use_precompiled_header(CyDAW)
        target_link_libraries(CyDAW PRIVATE
            CyDAW::Core
            CyDAWTranslations
            juce::juce_audio_devices
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_gui_extra
            juce::juce_recommended_config_flags
            ${CYDAW_LTO_FLAGS}
            juce::juce_recommended_warning_flags
        )

        if(CYDAW_BUILD_GRAPH_EDITOR)
            juce_add_gui_app(CyDAWGraphEditor
                PRODUCT_NAME "CyDAW Graph Editor"
                VERSION "${PROJECT_VERSION}"
                BUILD_VERSION "${PROJECT_VERSION}"
            )
            if(WIN32)
                get_target_property(CYDAW_GRAPH_EDITOR_INFO_FILE
                                    CyDAWGraphEditor JUCE_INFO_FILE)
                get_target_property(CYDAW_GRAPH_EDITOR_GENERATED_DIR
                                    CyDAWGraphEditor JUCE_GENERATED_SOURCES_DIRECTORY)
                add_custom_command(
                    OUTPUT "${CYDAW_GRAPH_EDITOR_GENERATED_DIR}/CyDAWGraphEditor_resources.rc"
                    APPEND
                    DEPENDS "${CYDAW_GRAPH_EDITOR_INFO_FILE}")
            endif()
            target_sources(CyDAWGraphEditor PRIVATE
                src/graph_editor/GraphEditorMain.cpp
                src/app/CyDawLookAndFeel.cpp
                src/app/CyDawLookAndFeel.h
                src/app/GraphManagerComponent.cpp
                src/app/GraphManagerComponent.h
                src/app/GraphPluginExportComponent.cpp
                src/app/GraphPluginExportComponent.h
                src/app/GraphPluginProjectGenerator.cpp
                src/app/GraphPluginProjectGenerator.h
                src/app/GraphPolySynthRenderer.cpp
                src/app/GraphPolySynthRenderer.h
                src/app/Localization.cpp
                src/app/Localization.h
                src/app/ModuleInterfaceEditor.cpp
                src/app/ModuleInterfaceEditor.h
                src/app/NativeDspGraphState.cpp
                src/app/NativeDspGraphState.h
                src/app/NativeDspRuntime.cpp
                src/app/NativeDspRuntime.h
                src/app/NodeGraphComponent.cpp
                src/app/SoundModelFile.cpp
                src/app/SamplerMapFile.cpp
                src/app/NodeGraphComponentFactoryData.cpp
                src/app/NodeGraphComponent.h
            )
            target_include_directories(CyDAWGraphEditor PRIVATE src/app)
            target_compile_features(CyDAWGraphEditor PRIVATE cxx_std_20)
            target_compile_definitions(CyDAWGraphEditor PRIVATE
                JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CyDAWGraphEditor,JUCE_PRODUCT_NAME>"
                JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
                JUCE_USE_CURL=0
                JUCE_WEB_BROWSER=0
            )
            cydaw_configure_audio_backends(CyDAWGraphEditor)
            target_link_libraries(CyDAWGraphEditor PRIVATE
                CyDAW::Core
                CyDAWTranslations
                juce::juce_audio_utils
                juce::juce_data_structures
                juce::juce_gui_extra
                juce::juce_recommended_config_flags
                ${CYDAW_LTO_FLAGS}
                juce::juce_recommended_warning_flags
            )
            if(CYDAW_BUILD_TESTS)
                add_test(NAME cydaw_graph_editor_smoke
                         COMMAND CyDAWGraphEditor --smoke-test)
                cydaw_configure_test(cydaw_graph_editor_smoke 30)
                # Every factory graph has to compile. A mistyped port in a
                # preset is otherwise only discovered when someone opens it.
                add_test(NAME cydaw_factory_graphs
                         COMMAND CyDAWGraphEditor --verify-presets)
                set_tests_properties(cydaw_factory_graphs
                                     PROPERTIES RUN_SERIAL TRUE)
                cydaw_configure_test(cydaw_factory_graphs 60)
            endif()
        endif()

        # Dear ImGui (docking branch, third_party/imgui), drawn with Direct3D 11:
        # CyDAW 2's interface, and the window of every plug-in exported from it.
        if(WIN32 AND NOT TARGET cydaw_imgui)
            add_library(cydaw_imgui STATIC
                third_party/imgui/imgui.cpp
                third_party/imgui/imgui_draw.cpp
                third_party/imgui/imgui_tables.cpp
                third_party/imgui/imgui_widgets.cpp
                third_party/imgui/imgui_demo.cpp
                third_party/imgui/backends/imgui_impl_win32.cpp
                third_party/imgui/backends/imgui_impl_dx11.cpp
            )
            target_include_directories(cydaw_imgui PUBLIC third_party/imgui)
            target_compile_definitions(cydaw_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
            target_compile_features(cydaw_imgui PUBLIC cxx_std_17)
            target_link_libraries(cydaw_imgui PUBLIC d3d11 dxgi dwmapi d3dcompiler)
        endif()

        function(cydaw_add_graph_runtime_plugin target_name)
            set(options IS_SYNTH ACCEPTS_MIDI PRODUCES_MIDI)
            set(one_value_args PRODUCT_NAME COMPANY_NAME BUNDLE_ID
                MANUFACTURER_CODE PLUGIN_CODE VERSION CONFIG_DIRECTORY)
            set(multi_value_args CATEGORIES)
            cmake_parse_arguments(GRAPH
                "${options}" "${one_value_args}" "${multi_value_args}"
                ${ARGN})
            set(cydaw_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
            juce_add_plugin(${target_name}
                COMPANY_NAME "${GRAPH_COMPANY_NAME}"
                BUNDLE_ID "${GRAPH_BUNDLE_ID}"
                PLUGIN_MANUFACTURER_CODE ${GRAPH_MANUFACTURER_CODE}
                PLUGIN_CODE ${GRAPH_PLUGIN_CODE}
                FORMATS VST3
                PRODUCT_NAME "${GRAPH_PRODUCT_NAME}"
                VERSION "${GRAPH_VERSION}"
                BUILD_VERSION "${GRAPH_VERSION}"
                VST3_CATEGORIES ${GRAPH_CATEGORIES}
                COPY_PLUGIN_AFTER_BUILD FALSE
                IS_SYNTH ${GRAPH_IS_SYNTH}
                NEEDS_MIDI_INPUT ${GRAPH_ACCEPTS_MIDI}
                NEEDS_MIDI_OUTPUT ${GRAPH_PRODUCES_MIDI}
                EDITOR_WANTS_KEYBOARD_FOCUS TRUE)
            target_sources(${target_name} PRIVATE
                "${cydaw_root}/src/graph_plugin/GraphRuntimePlugin.cpp"
                "${cydaw_root}/src/app/CyDawLookAndFeel.cpp"
                "${cydaw_root}/src/app/CyDawLookAndFeel.h"
                "${cydaw_root}/src/app/GraphManagerComponent.cpp"
                "${cydaw_root}/src/app/GraphManagerComponent.h"
                "${cydaw_root}/src/app/GraphPluginExportComponent.cpp"
                "${cydaw_root}/src/app/GraphPluginExportComponent.h"
                "${cydaw_root}/src/app/GraphPluginProjectGenerator.cpp"
                "${cydaw_root}/src/app/GraphPluginProjectGenerator.h"
                "${cydaw_root}/src/app/GraphPolySynthRenderer.cpp"
                "${cydaw_root}/src/app/GraphPolySynthRenderer.h"
                "${cydaw_root}/src/app/Localization.cpp"
                "${cydaw_root}/src/app/Localization.h"
                "${cydaw_root}/src/app/ModuleInterfaceEditor.cpp"
                "${cydaw_root}/src/app/ModuleInterfaceEditor.h"
                "${cydaw_root}/src/app/NativeDspGraphState.cpp"
                "${cydaw_root}/src/app/NativeDspGraphState.h"
                "${cydaw_root}/src/app/NativeDspRuntime.cpp"
                "${cydaw_root}/src/app/NativeDspRuntime.h"
                "${cydaw_root}/src/app/NodeGraphComponent.cpp"
                "${cydaw_root}/src/app/SoundModelFile.cpp"
                "${cydaw_root}/src/app/SamplerMapFile.cpp"
                "${cydaw_root}/src/app/NodeGraphComponentFactoryData.cpp"
                "${cydaw_root}/src/app/NodeGraphComponent.h")
            if(GRAPH_CONFIG_DIRECTORY)
                target_include_directories(${target_name} BEFORE PRIVATE
                    "${GRAPH_CONFIG_DIRECTORY}")
            endif()
            target_include_directories(${target_name} PRIVATE
                "${cydaw_root}/src/app"
                "${cydaw_root}/src/graph_plugin")
            if(WIN32)
                # Its window: CyDAW's own interface of its device (Dear ImGui),
                # the same views as in CyDAW 2.
                target_sources(${target_name} PRIVATE
                    "${cydaw_root}/src/plugin_ui/PluginInterface.cpp"
                    "${cydaw_root}/src/app/NodeGraphComponentEditorAccess.cpp"
                    "${cydaw_root}/src/plugin_ui/PluginInterface.h"
                    "${cydaw_root}/src/v2/ImGuiHost.cpp"
                    "${cydaw_root}/src/v2/Theme.cpp"
                    "${cydaw_root}/src/v2/Widgets.cpp"
                    "${cydaw_root}/src/v2/UiState.cpp"
                    "${cydaw_root}/src/v2/GraphSlot.cpp"
                    "${cydaw_root}/src/v2/DeviceOps.cpp"
                    "${cydaw_root}/src/v2/DeviceBody.cpp"
                    "${cydaw_root}/src/v2/DeviceSignals.cpp"
                    "${cydaw_root}/src/v2/SignalAnalysis.cpp"
                    "${cydaw_root}/src/v2/DeviceVisuals.cpp"
                    "${cydaw_root}/src/v2/StudioEffectsView.cpp"
                    "${cydaw_root}/src/v2/CardFiles.cpp"
                    "${cydaw_root}/src/v2/InterfaceEditor.cpp"
                    "${cydaw_root}/src/v2/InterfaceDesigner.cpp"
                    "${cydaw_root}/src/v2/InstrumentPanels.cpp"
                    "${cydaw_root}/src/v2/Presets.cpp"
                    "${cydaw_root}/src/v2/PresetMenu.cpp"
                    "${cydaw_root}/src/v2/Locations.cpp"
                    "${cydaw_root}/src/v2/InstrumentEffects.cpp"
                    "${cydaw_root}/src/v2/SynthLayouts.cpp"
                    "${cydaw_root}/src/v2/SynthView.cpp"
                    "${cydaw_root}/src/v2/PrismView.cpp"
                    "${cydaw_root}/src/v2/KickView.cpp")
                target_include_directories(${target_name} PRIVATE
                    "${cydaw_root}/src/v2"
                    "${cydaw_root}/src/plugin_ui")
                target_link_libraries(${target_name} PRIVATE cydaw_imgui juce::juce_dsp)
            endif()
            target_compile_features(${target_name} PRIVATE cxx_std_20)
            target_compile_definitions(${target_name} PRIVATE
                JUCE_USE_CURL=0
                JUCE_WEB_BROWSER=0
                JUCE_VST3_CAN_REPLACE_VST2=0
                JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0
            )
            target_link_libraries(${target_name} PRIVATE
                CyDAW::Core
                CyDAWTranslations
                juce::juce_audio_processors
                juce::juce_audio_utils
                juce::juce_data_structures
                juce::juce_gui_extra
                juce::juce_recommended_config_flags
                ${CYDAW_LTO_FLAGS}
                juce::juce_recommended_warning_flags
            )
        endfunction()

        if(CYDAW_BUILD_GRAPH_RUNTIME_PLUGIN)
            cydaw_add_graph_runtime_plugin(CyDAWGraphRuntime
                PRODUCT_NAME "CyDAW Graph Runtime"
                COMPANY_NAME "CyDAW"
                BUNDLE_ID "audio.cydaw.graph-runtime"
                MANUFACTURER_CODE CyDw
                PLUGIN_CODE GRun
                VERSION "${PROJECT_VERSION}"
                CONFIG_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/graph_plugin"
                CATEGORIES Fx Tools
                ACCEPTS_MIDI)
            if(CYDAW_BUILD_TESTS)
                # Hosts the runtime plug-in (what every exported device is
                # built from) offline: a state restored, a note played.
                juce_add_console_app(cydaw_graph_runtime_host
                    PRODUCT_NAME "CyDAW Graph Runtime Host"
                    VERSION "${PROJECT_VERSION}"
                    BUILD_VERSION "${PROJECT_VERSION}")
                target_sources(cydaw_graph_runtime_host PRIVATE tests/GraphRuntimeHostTest.cpp)
                target_compile_features(cydaw_graph_runtime_host PRIVATE cxx_std_20)
                target_compile_definitions(cydaw_graph_runtime_host PRIVATE
                    JUCE_USE_CURL=0
                    JUCE_WEB_BROWSER=0
                    JUCE_PLUGINHOST_VST3=1
                    JUCE_PLUGINHOST_LV2=1
                    JUCE_MODAL_LOOPS_PERMITTED=1)
                target_link_libraries(cydaw_graph_runtime_host PRIVATE
                    juce::juce_audio_processors
                    juce::juce_gui_extra
                    juce::juce_recommended_config_flags
                    juce::juce_recommended_warning_flags)
                add_dependencies(cydaw_graph_runtime_host CyDAWGraphRuntime_VST3)
                # The factory content's converter (tools/factory_content):
                # recorded notes cut to their sound and written as FLAC.
                juce_add_console_app(cydaw_sample_tool
                    PRODUCT_NAME "CyDAW Sample Tool"
                    VERSION "${PROJECT_VERSION}"
                    BUILD_VERSION "${PROJECT_VERSION}")
                target_sources(cydaw_sample_tool PRIVATE tools/factory_content/SampleTool.cpp)
                target_compile_features(cydaw_sample_tool PRIVATE cxx_std_20)
                target_compile_definitions(cydaw_sample_tool PRIVATE
                    JUCE_USE_CURL=0
                    JUCE_WEB_BROWSER=0
                    JUCE_USE_FLAC=1)
                target_link_libraries(cydaw_sample_tool PRIVATE
                    juce::juce_audio_formats
                    juce::juce_recommended_config_flags
                    juce::juce_recommended_warning_flags)
            endif()
        endif()

        if(CYDAW_BUILD_TESTS)
            function(cydaw_add_isolated_self_test test_name command_line timeout_seconds)
                add_test(NAME ${test_name} COMMAND CyDAW ${command_line})
                set_tests_properties(${test_name} PROPERTIES RUN_SERIAL TRUE)
                cydaw_configure_test(${test_name} ${timeout_seconds})
            endfunction()

            cydaw_add_isolated_self_test(cydaw_layout_window_smoke
                                         --layout-self-test 20)
            # Measured at 31-34 s now that every stage runs: the old 30 s budget
            # was sized for a test that gave up at stage five, so it started
            # timing out the moment the earlier stages were fixed.
            cydaw_add_isolated_self_test(cydaw_project_graph_smoke
                                         --project-graph-self-test 90)
            # This one existed as a switch for a long time without ever being
            # registered, so it had never run. It renders the master twice to
            # check that a chain placed behind an instrument is actually heard.
            cydaw_add_isolated_self_test(cydaw_master_export_smoke
                                         --master-export-self-test 90)
            cydaw_add_isolated_self_test(cydaw_project_recovery_smoke
                                         --project-recovery-self-test 30)
            # The control port and the MCP server, end to end: the check
            # launches the application on its own port, drives every tool,
            # and closes it. Needs Python 3 on the path.
            find_program(CYDAW_PYTHON NAMES python3 python)
            if(CYDAW_PYTHON)
                add_test(NAME cydaw_mcp_check
                         COMMAND "${CYDAW_PYTHON}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/tools/mcp/cydaw_mcp_check.py"
                                 --port 47821 --exe "$<TARGET_FILE:CyDAW>" --quit
                         WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
                set_tests_properties(cydaw_mcp_check PROPERTIES TIMEOUT 240)
            endif()
            cydaw_add_isolated_self_test(cydaw_audio_recording_smoke
                                         --audio-recording-self-test 30)
        endif()

        if(CYDAW_BUILD_TESTS)
            set(CYDAW_TEST_PLUGIN_FORMATS VST3)
            if(CYDAW_VST2_AVAILABLE)
                list(APPEND CYDAW_TEST_PLUGIN_FORMATS VST)
            endif()
            juce_add_plugin(CyDAWTestGain
                COMPANY_NAME "CyDAW"
                PLUGIN_MANUFACTURER_CODE CyDw
                PLUGIN_CODE TGa1
                FORMATS ${CYDAW_TEST_PLUGIN_FORMATS}
                PRODUCT_NAME "CyDAW Test Gain"
                VST3_CATEGORIES Fx
                COPY_PLUGIN_AFTER_BUILD FALSE
                IS_SYNTH FALSE
                NEEDS_MIDI_INPUT FALSE
                NEEDS_MIDI_OUTPUT FALSE
                EDITOR_WANTS_KEYBOARD_FOCUS FALSE
            )
            target_sources(CyDAWTestGain PRIVATE tests/TestGainPlugin.cpp)
            target_compile_features(CyDAWTestGain PRIVATE cxx_std_20)
            target_compile_definitions(CyDAWTestGain PRIVATE
                JUCE_USE_CURL=0
                JUCE_WEB_BROWSER=0
            )
            target_compile_definitions(CyDAWTestGain PUBLIC
                JUCE_VST3_CAN_REPLACE_VST2=0
            )
            target_link_libraries(CyDAWTestGain PRIVATE
                juce::juce_audio_utils
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags
            )
        endif()

        if(CYDAW_BUILD_TESTS)
            juce_add_console_app(cydaw_desktop_tests
                PRODUCT_NAME "CyDAW Desktop Tests"
                VERSION "${PROJECT_VERSION}"
                BUILD_VERSION "${PROJECT_VERSION}")
            target_sources(cydaw_desktop_tests PRIVATE
                tests/DesktopAudioTests.cpp
                src/app/AudioTrackController.cpp
                src/app/DjEngine.cpp
                src/app/FoundryPlayer.cpp
                src/app/VirtualAudioBus.cpp
                src/app/AudioTrackController.h
                src/app/AudioEditorComponent.cpp
                src/app/AudioEditorComponent.h
                src/app/ClipLauncherComponent.cpp
                src/app/ClipLauncherComponent.h
                src/app/MixerComponent.cpp
                src/app/MixerComponent.h
                src/app/MixerStripDrawing.h
                src/app/Localization.cpp
                src/app/Localization.h
                src/app/LibraryBrowserComponent.cpp
                src/app/LibraryBrowserComponent.h
                src/app/CyDawLookAndFeel.cpp
                src/app/CyDawLookAndFeel.h
                src/app/GraphManagerComponent.cpp
                src/app/GraphManagerComponent.h
                src/app/GraphPluginExportComponent.cpp
                src/app/GraphPluginExportComponent.h
                src/app/GraphPluginProjectGenerator.cpp
                src/app/GraphPluginProjectGenerator.h
                src/app/GraphPolySynthRenderer.cpp
                src/app/GraphPolySynthRenderer.h
                src/app/ModuleInterfaceEditor.cpp
                src/app/ModuleInterfaceEditor.h
                src/app/NativeDspGraphState.cpp
                src/app/NativeDspGraphState.h
                src/app/NativeDspRuntime.cpp
                src/app/NativeDspRuntime.h
                src/app/IsolatedPluginHost.cpp
                src/app/IsolatedPluginHost.h
                src/app/WindowsPluginBridge.cpp
                src/app/WindowsPluginBridge.h
                src/app/NodeGraphComponent.cpp
                src/app/NodeGraphComponentEditorAccess.cpp
                src/app/SoundModelFile.cpp
                src/app/SamplerMapFile.cpp
                src/app/NodeGraphComponentFactoryData.cpp
                src/app/NodeGraphComponent.h
                src/app/PianoRollComponent.cpp
                src/app/PianoRollComponent.h
                src/app/ProjectRecovery.cpp
                src/app/ProjectRecovery.h
                src/app/ProjectImport.cpp
                src/app/ProjectImport.h
            )
            target_include_directories(cydaw_desktop_tests PRIVATE src/app)
            target_compile_features(cydaw_desktop_tests PRIVATE cxx_std_20)
            target_compile_definitions(cydaw_desktop_tests PRIVATE
                JUCE_USE_CURL=0
                JUCE_WEB_BROWSER=0
            )
            cydaw_configure_plugin_formats(cydaw_desktop_tests)
            cydaw_configure_audio_backends(cydaw_desktop_tests)
            target_link_libraries(cydaw_desktop_tests PRIVATE
                CyDAW::Core
                CyDAWTranslations
                juce::juce_audio_utils
                juce::juce_data_structures
                juce::juce_gui_extra
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags
            )
            if(MSVC)
                # This integration test keeps several complete JUCE editors
                # alive in one scope. Use a larger test stack than the Windows
                # 1 MiB default; the application workspace itself lives on the heap.
                target_link_options(cydaw_desktop_tests PRIVATE /STACK:8388608)
            endif()
        endif()

        juce_add_console_app(CyDAWAudioProbe
            PRODUCT_NAME "CyDAW Audio Probe"
            VERSION "${PROJECT_VERSION}"
            BUILD_VERSION "${PROJECT_VERSION}")
        if(WIN32)
            get_target_property(CYDAW_AUDIO_PROBE_INFO_FILE CyDAWAudioProbe JUCE_INFO_FILE)
            get_target_property(CYDAW_AUDIO_PROBE_GENERATED_DIR
                                CyDAWAudioProbe JUCE_GENERATED_SOURCES_DIRECTORY)
            add_custom_command(
                OUTPUT "${CYDAW_AUDIO_PROBE_GENERATED_DIR}/CyDAWAudioProbe_resources.rc"
                APPEND
                DEPENDS "${CYDAW_AUDIO_PROBE_INFO_FILE}")
        endif()
        target_sources(CyDAWAudioProbe PRIVATE src/audio_probe/AudioBackendProbeMain.cpp)
        target_compile_features(CyDAWAudioProbe PRIVATE cxx_std_20)
        target_compile_definitions(CyDAWAudioProbe PRIVATE
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0
        )
        cydaw_configure_audio_backends(CyDAWAudioProbe)
        target_link_libraries(CyDAWAudioProbe PRIVATE
            juce::juce_audio_devices
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
        )
        if(CYDAW_BUILD_TESTS)
            add_test(NAME cydaw_audio_backend_probe COMMAND CyDAWAudioProbe --self-test)
            cydaw_configure_test(cydaw_audio_backend_probe 60)
        endif()

        juce_add_console_app(CyDAWPluginWorker
            PRODUCT_NAME "CyDAW Plugin Worker"
            VERSION "${PROJECT_VERSION}"
            BUILD_VERSION "${PROJECT_VERSION}")
        if(WIN32)
            get_target_property(CYDAW_PLUGIN_WORKER_INFO_FILE CyDAWPluginWorker JUCE_INFO_FILE)
            get_target_property(CYDAW_PLUGIN_WORKER_GENERATED_DIR
                                CyDAWPluginWorker JUCE_GENERATED_SOURCES_DIRECTORY)
            add_custom_command(
                OUTPUT "${CYDAW_PLUGIN_WORKER_GENERATED_DIR}/CyDAWPluginWorker_resources.rc"
                APPEND
                DEPENDS "${CYDAW_PLUGIN_WORKER_INFO_FILE}")
        endif()
        set(YSFX_PLUGIN OFF CACHE BOOL "" FORCE)
        set(YSFX_TESTS OFF CACHE BOOL "" FORCE)
        set(YSFX_SKIP_CHECKSUM ON CACHE BOOL "" FORCE)
        set(CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING "Build type for single-configuration generators")
        find_program(NASM_PROGRAM nasm PATHS "${CMAKE_CURRENT_SOURCE_DIR}/output/plugin-formats/tools/nasm-2.16.03")
        if(NASM_PROGRAM)
            set(YSFX_PORTABLE OFF CACHE BOOL "" FORCE)
        endif()
        if(MSVC AND NOT NASM_PROGRAM)
            set(YSFX_PORTABLE ON CACHE BOOL "EEL interpreter fallback without NASM" FORCE)
        endif()
        add_subdirectory(third_party/ysfx EXCLUDE_FROM_ALL)
        target_sources(CyDAWPluginWorker PRIVATE src/plugin_worker/PluginWorkerMain.cpp
            src/plugin_worker/ExtraPluginFormats.cpp src/plugin_worker/ClapInstance.cpp src/plugin_worker/JsfxInstance.cpp)
        target_include_directories(CyDAWPluginWorker PRIVATE third_party/clap/include)
        target_link_libraries(CyDAWPluginWorker PRIVATE ysfx::ysfx juce::juce_gui_extra)
        target_compile_features(CyDAWPluginWorker PRIVATE cxx_std_20)
        target_compile_definitions(CyDAWPluginWorker PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0
            JUCE_MODAL_LOOPS_PERMITTED=1
        )
        cydaw_configure_plugin_formats(CyDAWPluginWorker)
        target_link_libraries(CyDAWPluginWorker PRIVATE
            CyDAW::Core
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
        )
        if(CYDAW_BUILD_TESTS)
            add_test(NAME cydaw_plugin_worker_self_test
                     COMMAND CyDAWPluginWorker --self-test)
            cydaw_configure_test(cydaw_plugin_worker_self_test 60)
            if(CYDAW_BUILD_GRAPH_RUNTIME_PLUGIN)
                add_dependencies(CyDAWPluginWorker CyDAWGraphRuntime_VST3)
                add_test(NAME cydaw_graph_runtime_vst3_smoke
                         COMMAND CyDAWPluginWorker --validate-plugin
                                 "$<TARGET_FILE:CyDAWGraphRuntime_VST3>")
                cydaw_configure_test(cydaw_graph_runtime_vst3_smoke 60)
            endif()
        endif()
        add_custom_command(TARGET CyDAWPluginWorker POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:CyDAWPluginWorker>"
                "$<TARGET_FILE_DIR:CyDAW>"
            VERBATIM
        )

        if(CYDAW_BUILD_TESTS)
            juce_add_console_app(cydaw_plugin_ipc_tests
                PRODUCT_NAME "CyDAW Plugin IPC Tests"
                VERSION "${PROJECT_VERSION}"
                BUILD_VERSION "${PROJECT_VERSION}")
            target_sources(cydaw_plugin_ipc_tests PRIVATE
                tests/PluginIpcTests.cpp
                src/app/IsolatedPluginHost.cpp
                src/app/IsolatedPluginHost.h
                src/app/WindowsPluginBridge.cpp
                src/app/WindowsPluginBridge.h
            )
            target_include_directories(cydaw_plugin_ipc_tests PRIVATE src/app)
            target_compile_features(cydaw_plugin_ipc_tests PRIVATE cxx_std_20)
            target_compile_definitions(cydaw_plugin_ipc_tests PRIVATE
                JUCE_USE_CURL=0
                JUCE_WEB_BROWSER=0
            )
            cydaw_configure_plugin_formats(cydaw_plugin_ipc_tests)
            target_link_libraries(cydaw_plugin_ipc_tests PRIVATE
                CyDAW::Core
                juce::juce_audio_basics
                juce::juce_core
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags
            )
            add_dependencies(cydaw_plugin_ipc_tests CyDAWPluginWorker CyDAWTestGain_VST3)
            add_test(NAME cydaw_plugin_ipc_tests
                     COMMAND cydaw_plugin_ipc_tests
                             "$<TARGET_FILE:CyDAWPluginWorker>"
                             "$<TARGET_FILE:CyDAWTestGain_VST3>")
            cydaw_configure_test(cydaw_plugin_ipc_tests 120)
            if(CYDAW_VST2_AVAILABLE)
                add_dependencies(cydaw_plugin_ipc_tests CyDAWTestGain_VST)
                add_test(NAME cydaw_plugin_ipc_vst2_tests
                         COMMAND cydaw_plugin_ipc_tests
                                 "$<TARGET_FILE:CyDAWPluginWorker>"
                                 "$<TARGET_FILE:CyDAWTestGain_VST>")
                cydaw_configure_test(cydaw_plugin_ipc_vst2_tests 120)
            endif()

            add_dependencies(cydaw_desktop_tests CyDAWPluginWorker CyDAWTestGain_VST3)
            add_custom_command(TARGET cydaw_desktop_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:CyDAWPluginWorker>"
                    "$<TARGET_FILE_DIR:cydaw_desktop_tests>"
                VERBATIM)
            add_test(NAME cydaw_desktop_tests
                     COMMAND cydaw_desktop_tests "$<TARGET_FILE:CyDAWTestGain_VST3>")
            cydaw_configure_test(cydaw_desktop_tests 180)
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                find_program(CYDAW_BASH_EXECUTABLE bash REQUIRED)
                add_test(NAME cydaw_windows_vst_bridge_script_tests
                         COMMAND "${CYDAW_BASH_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/tests/BridgeScriptTests.sh"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cydaw-windows-vst-bridge.sh")
                set_tests_properties(cydaw_windows_vst_bridge_script_tests
                                     PROPERTIES TIMEOUT 10)
            endif()
        endif()

        add_custom_command(TARGET CyDAW POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CyDAW>/i18n"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/en.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/fr.json"
                "$<TARGET_FILE_DIR:CyDAW>/i18n"
            VERBATIM
        )
        # The sample classifier the explorer can call (tools/samples).
        add_custom_command(TARGET CyDAW POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CyDAW>/tools/samples"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/samples/classify_samples.py"
                "$<TARGET_FILE_DIR:CyDAW>/tools/samples"
            VERBATIM
        )
        # The ready-to-play projects the File menu lists; see --template=.
        if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates")
            add_custom_command(TARGET CyDAW POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/templates"
                    "$<TARGET_FILE_DIR:CyDAW>/templates"
                VERBATIM
            )
        endif()

        # CyDAW 2: a new interface in Dear ImGui (docking branch, third_party/imgui)
        # over the same engine, rendered with Direct3D 11 on the message thread.
        # Version 1 above stays buildable; src/v2 holds the new interface.
        option(CYDAW_BUILD_V2 "Build CyDAW 2, the Dear ImGui interface" ON)
        if(WIN32 AND CYDAW_BUILD_V2)
            juce_add_gui_app(CyDAW2
                PRODUCT_NAME "CyDAW 2"
                VERSION "${PROJECT_VERSION}"
                BUILD_VERSION "${PROJECT_VERSION}"
                # The logo: the executable's icon, its window's and the taskbar's.
                ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/cydaw-logo-512.png"
                ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/cydaw-icon-small.png"
            )
            # Header-only Link transport (no audio sharing) and JACK's dynamically loaded control API.
            include("${CMAKE_CURRENT_SOURCE_DIR}/third_party/ableton-link/AbletonLinkConfig.cmake")
            target_link_libraries(CyDAW2 PRIVATE Ableton::Link ws2_32 iphlpapi)
            include("${CMAKE_CURRENT_SOURCE_DIR}/src/v2/FFmpeg.cmake")
            target_compile_definitions(CyDAW2 PRIVATE ASIO_STANDALONE)
            target_include_directories(CyDAW2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jack-transport/include")
            # The logo inside the interface (menu bar, About, opening a project).
            juce_add_binary_data(CyDAW2Branding
                HEADER_NAME CyDAWBranding.h
                NAMESPACE cydaw::branding
                SOURCES
                    resources/branding/cydaw-logo-512.png
            )
            get_target_property(CYDAW2_JUCE_INFO_FILE CyDAW2 JUCE_INFO_FILE)
            get_target_property(CYDAW2_JUCE_GENERATED_DIR
                                CyDAW2 JUCE_GENERATED_SOURCES_DIRECTORY)
            add_custom_command(
                OUTPUT "${CYDAW2_JUCE_GENERATED_DIR}/CyDAW2_resources.rc"
                APPEND
                DEPENDS "${CYDAW2_JUCE_INFO_FILE}")
            target_sources(CyDAW2 PRIVATE
                src/v2/Main.cpp
                src/v2/CinemaVideo.cpp
                src/v2/CinemaVideo.h
                src/v2/CinemaEditing.inc
                src/v2/CinemaTimeline.inc
                src/v2/TransportSync.cpp
                src/v2/TransportSync.h
                src/v2/AppUi.cpp
                src/v2/AppUi.h
                src/v2/ImGuiHost.cpp
                src/v2/ImGuiHost.h
                src/v2/Icons.h
                src/v2/Studio.cpp
                src/v2/GraphSlot.cpp
                src/v2/DeviceOps.cpp
                src/v2/DeviceOps.h
                src/v2/StudioTypes.h
                src/v2/DeviceHost.h
                src/v2/ImportedInstruments.cpp
                src/v2/AddTrackMenu.cpp
                src/v2/RackView.cpp
                src/v2/RackView.h
                src/v2/AddTrackMenu.h
                src/v2/EditorWindows.h
                src/v2/ImportedInstruments.h
                src/v2/TrackModulation.cpp
                src/v2/SignalAnalysis.cpp
                src/v2/SignalAnalysis.h
                src/v2/DeviceVisuals.cpp
                src/v2/StudioEffectsView.cpp
                src/v2/DeviceSignals.cpp
                src/v2/DeviceSignals.h
                src/v2/DeviceVisuals.h
                src/v2/RemoteCommands.cpp
                src/v2/RemoteCommands.h
                src/v2/Studio.h
                src/v2/ProjectIo.cpp
                src/v2/Theme.cpp
                src/v2/Theme.h
                src/v2/Widgets.cpp
                src/v2/Widgets.h
                src/v2/UiState.cpp
                src/v2/UiState.h
                src/v2/CrashReport.cpp
                src/v2/CrashReport.h
                src/v2/ArrangementView.cpp
                src/v2/ArrangementView.h
                src/v2/MixerView.cpp
                src/v2/MixerView.h
                src/v2/RoutingView.cpp
                src/v2/RoutingView.h
                src/v2/SoundCardView.cpp
                src/v2/SoundCardView.h
                src/v2/GraphEditor.cpp
                src/v2/GraphEditor.h
                src/v2/GraphLayout.cpp
                src/v2/GraphLayout.h
                src/v2/DevicesView.cpp
                src/v2/DevicesView.h
                src/v2/SamplerView.cpp
                src/v2/SamplerControlsView.cpp
                src/v2/PrismView.cpp
                src/v2/PrismView.h
                src/v2/KickView.cpp
                src/v2/KickView.h
                src/v2/SynthView.cpp
                src/v2/SynthView.h
                src/v2/BatterieView.cpp
                src/v2/BatterieView.h
                src/v2/SynthLayouts.cpp
                src/v2/SynthLayouts.h
                src/v2/InstrumentEffects.cpp
                src/v2/InstrumentEffects.h
                src/v2/CardFiles.cpp
                src/v2/CardFiles.h
                src/v2/AudioEditorView.cpp
                src/v2/AudioEditorView.h
                src/v2/MasterClipEditor.cpp
                src/v2/MasterClipEditor.h
                src/v2/ComputerKeyboard.cpp
                src/v2/ComputerKeyboard.h
                src/v2/ThemesView.cpp
                src/v2/ThemesView.h
                src/v2/SamplerView.h
                src/v2/DeviceEditorView.cpp
                src/v2/DeviceBody.cpp
                src/v2/DeviceBody.h
                src/v2/DeviceEditorView.h
                src/v2/WorkshopView.cpp
                src/v2/JsfxView.cpp
            src/v2/PluginWindows.cpp
                src/v2/RoutingUi.cpp
                src/v2/Companion.cpp
                src/v2/WorkshopView.h
                src/v2/DjView.cpp
                src/v2/DjView.h
                src/v2/DjLibrary.cpp
                src/v2/DjLibrary.h
                src/v2/FoundryView.cpp
                src/v2/FoundryView.h
                src/v2/ModeProcessing.cpp
                src/v2/ModeProcessing.h
                src/v2/FoundryDocument.cpp
                src/v2/FoundryDocument.h
                src/v2/NodeTree.cpp
                src/v2/NodeTree.h
                src/v2/ContextMenus.cpp
                src/v2/ContextMenus.h
                src/v2/InterfaceEditor.cpp
                src/v2/InterfaceDesigner.cpp
                src/v2/InstrumentPanels.cpp
                src/v2/InterfaceEditor.h
                src/v2/Vst3Build.cpp
                src/v2/Vst3Build.h
                src/v2/ClipEditorView.cpp
                src/v2/ClipEditorView.h
                src/v2/BrowserView.cpp
                src/v2/BrowserView.h
                src/v2/InspectorView.cpp
                src/v2/InspectorView.h
                src/v2/LauncherView.cpp
                src/v2/LauncherView.h
                src/v2/Locations.cpp
                src/v2/Locations.h
                src/v2/SoundBanks.cpp
                src/v2/SoundBanks.h
                src/v2/AiModels.cpp
                src/v2/AiModels.h
                src/v2/SemanticSearch.cpp
                src/v2/SemanticSearch.h
                src/v2/SemanticSearchView.cpp
                src/v2/SemanticSearchView.h
                src/v2/SampleCatalog.cpp
                src/v2/SampleCatalog.h
                src/v2/LibraryWindow.cpp
                src/v2/LibraryWindow.h
                src/v2/LocationsView.cpp
                src/v2/LocationsView.h
                src/v2/BanksView.cpp
                src/v2/BanksView.h
                src/v2/Presets.cpp
                src/v2/Presets.h
                src/v2/PresetMenu.cpp
                src/v2/PresetMenu.h
                # The engine and the services the interface drives.
                src/app/AudioTrackController.cpp
                src/app/DjEngine.cpp
                src/app/FoundryPlayer.cpp
                src/app/AudioTrackController.h
                src/app/VirtualAudioBus.cpp
                src/app/IsolatedPluginHost.cpp
                src/app/WindowsPluginBridge.cpp
                src/app/Localization.cpp
                src/app/NativeDspRuntime.cpp
                src/app/NativeDspGraphState.cpp
                src/app/SampleLibrary.cpp
                src/app/ProjectRecovery.cpp
                src/app/ProjectImport.cpp
                src/app/RemoteControlServer.cpp
                # The graph model and compiler, used without being shown.
                src/app/NodeGraphComponent.cpp
                src/app/SoundModelFile.cpp
                src/app/SamplerMapFile.cpp
                src/app/NodeGraphComponentFactoryData.cpp
                src/app/NodeGraphComponentEditorAccess.cpp
                src/app/ModuleInterfaceEditor.cpp
                src/app/GraphPolySynthRenderer.cpp
                src/app/GraphManagerComponent.cpp
                src/app/GraphPluginExportComponent.cpp
                src/app/GraphPluginProjectGenerator.cpp
                src/app/CyDawLookAndFeel.cpp
            )
            target_include_directories(CyDAW2 PRIVATE src/app src/v2)
            target_compile_features(CyDAW2 PRIVATE cxx_std_20)
            target_compile_definitions(CyDAW2 PRIVATE
                JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CyDAW2,JUCE_PRODUCT_NAME>"
                JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
                JUCE_USE_CURL=0
                JUCE_WEB_BROWSER=0
            )
            cydaw_configure_plugin_formats(CyDAW2)
            cydaw_configure_audio_backends(CyDAW2)
            cydaw_use_precompiled_header(CyDAW2)
            target_link_libraries(CyDAW2 PRIVATE
                CyDAW::Core
                CyDAWTranslations
                CyDAW2Branding
                cydaw_imgui
                juce::juce_audio_devices
                juce::juce_audio_formats
                juce::juce_audio_processors
                juce::juce_audio_utils
                juce::juce_dsp
                juce::juce_gui_extra
                juce::juce_recommended_config_flags
                ${CYDAW_LTO_FLAGS}
                juce::juce_recommended_warning_flags
            )
            if(TARGET CyDAWPluginWorker)
                add_dependencies(CyDAW2 CyDAWPluginWorker)
                add_custom_command(TARGET CyDAW2 POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:CyDAWPluginWorker>"
                        "$<TARGET_FILE_DIR:CyDAW2>"
                    VERBATIM
                )
            endif()
            add_custom_command(TARGET CyDAW2 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Examples/JSFX" "$<TARGET_FILE_DIR:CyDAW2>/JSFX"
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CyDAW2>/Licenses/PluginFormats"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ysfx/LICENSE" "$<TARGET_FILE_DIR:CyDAW2>/Licenses/PluginFormats/ysfx-Apache-2.txt"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ysfx/thirdparty/WDL/LICENSE.txt" "$<TARGET_FILE_DIR:CyDAW2>/Licenses/PluginFormats/WDL.txt"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ysfx/thirdparty/stb/LICENSE" "$<TARGET_FILE_DIR:CyDAW2>/Licenses/PluginFormats/stb.txt"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ysfx/thirdparty/dr_libs/LICENSE" "$<TARGET_FILE_DIR:CyDAW2>/Licenses/PluginFormats/dr_libs.txt"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clap/LICENSE" "$<TARGET_FILE_DIR:CyDAW2>/Licenses/PluginFormats/CLAP.txt"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lv2/COPYING" "$<TARGET_FILE_DIR:CyDAW2>/Licenses/PluginFormats/LV2.txt"
                VERBATIM)
            add_custom_command(TARGET CyDAW2 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CyDAW2>/i18n"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/en.json"
                    "${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/fr.json"
                    "$<TARGET_FILE_DIR:CyDAW2>/i18n"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/v2"
                    "$<TARGET_FILE_DIR:CyDAW2>/i18n/v2"
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CyDAW2>/fonts"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/misc/fonts/Roboto-Medium.ttf"
                    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/misc/fonts/Cousine-Regular.ttf"
                    "$<TARGET_FILE_DIR:CyDAW2>/fonts"
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CyDAW2>/tools/samples"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/samples/classify_samples.py"
                    "$<TARGET_FILE_DIR:CyDAW2>/tools/samples"
                # The Python companion (sound matching), run when Python is there.
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/companion/cydaw_companion"
                    "$<TARGET_FILE_DIR:CyDAW2>/tools/companion/cydaw_companion"
                VERBATIM
            )
            # Every template survives a save and a reload in CyDAW 2.
            if(CYDAW_BUILD_TESTS)
                file(GLOB CYDAW2_TEMPLATE_PROJECTS "${CMAKE_CURRENT_SOURCE_DIR}/templates/*.cydaw")
                # Every factory instrument with its samples (several GB) and
                # every model: too much for a two-minute round trip.
                set(CYDAW2_LARGE_TEMPLATES "12-tous-les-instruments")
                foreach(template_project IN LISTS CYDAW2_TEMPLATE_PROJECTS)
                    get_filename_component(template_name "${template_project}" NAME_WE)
                    if(template_name IN_LIST CYDAW2_LARGE_TEMPLATES)
                        continue()
                    endif()
                    add_test(NAME cydaw2_roundtrip_${template_name}
                             COMMAND CyDAW2 "--roundtrip-test=${template_project}")
                    set_tests_properties(cydaw2_roundtrip_${template_name} PROPERTIES TIMEOUT 120)
                endforeach()
                # Track modulation moves the engine's parameters and lets go of them.
                add_test(NAME cydaw2_modulation
                         COMMAND CyDAW2 "--modulation-test=${CMAKE_CURRENT_SOURCE_DIR}/templates/06-tipper-glitch.cydaw")
                set_tests_properties(cydaw2_modulation PROPERTIES TIMEOUT 120)
                # Every effect of the catalogue compiles, is editable and sounds right.
                add_test(NAME cydaw2_effects COMMAND CyDAW2 "--effects-test")
                set_tests_properties(cydaw2_effects PROPERTIES TIMEOUT 120)
                # Every template's chains compile, and the device displays' taps
                # fill with what each device hears and makes.
                foreach(template_project IN LISTS CYDAW2_TEMPLATE_PROJECTS)
                    get_filename_component(template_name "${template_project}" NAME_WE)
                    if(template_name IN_LIST CYDAW2_LARGE_TEMPLATES)
                        continue()
                    endif()
                    add_test(NAME cydaw2_device_taps_${template_name}
                             COMMAND CyDAW2 "--display-test=${template_project}")
                    set_tests_properties(cydaw2_device_taps_${template_name} PROPERTIES TIMEOUT 120)
                endforeach()
                # The MCP server drives CyDAW 2 through its control port.
                find_package(Python3 COMPONENTS Interpreter QUIET)
                if(Python3_Interpreter_FOUND)
                    add_test(NAME cydaw2_mcp_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mcp/cydaw_mcp_check.py"
                                     --port 47822 --exe "$<TARGET_FILE:CyDAW2>" --quit)
                    set_tests_properties(cydaw2_mcp_check PROPERTIES TIMEOUT 300)
                    # A sound model through devices, the project, the workshop,
                    # My devices and the VST3 export (silent; nothing built).
                    set(cydaw_sound_model_host_arguments)
                    if(TARGET cydaw_graph_runtime_host)
                        set(cydaw_sound_model_host_arguments
                            --host "$<TARGET_FILE:cydaw_graph_runtime_host>"
                            --plugin "${CMAKE_BINARY_DIR}/CyDAWGraphRuntime_artefacts/$<CONFIG>/VST3/CyDAW Graph Runtime.vst3")
                    endif()
                    add_test(NAME cydaw2_sound_model_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_sound_model_check.py"
                                     --port 47824 --exe "$<TARGET_FILE:CyDAW2>" ${cydaw_sound_model_host_arguments})
                    set_tests_properties(cydaw2_sound_model_check PROPERTIES TIMEOUT 300)
                    # The sampler: files mapped by name, played through every
                    # path (devices, project, workshop, My devices, SFZ, VST3)
                    # and the factory instruments when they are built. Silent.
                    add_test(NAME cydaw2_sampler_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_sampler_check.py"
                                     --port 47826 --exe "$<TARGET_FILE:CyDAW2>" ${cydaw_sound_model_host_arguments})
                    set_tests_properties(cydaw2_sampler_check PROPERTIES TIMEOUT 600)
                    # The launcher: arming by hand, held notes kept when another
                    # track is picked, empty slots' stops through a scene. Silent.
                    add_test(NAME cydaw2_launcher_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_launcher_check.py"
                                     --port 47827 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_launcher_check PROPERTIES TIMEOUT 180)
                    # Track formats (mono to 7.1) and tracks sent straight to an
                    # output of the sound card, saved and read back. Silent.
                    add_test(NAME cydaw2_routing_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_routing_check.py"
                                     --port 47828 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_routing_check PROPERTIES TIMEOUT 180)
                    # Sound banks downloaded from a local server into folders of
                    # the test's own, and the locations moved. Silent, offline.
                    add_test(NAME cydaw2_banks_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_banks_check.py"
                                     --port 47829 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_banks_check PROPERTIES TIMEOUT 180)
                    # Kick and the CyDAW Electro bank: generated in a banks folder of
                    # the test's own, its kits, Kick's presets and project. Silent.
                    add_test(NAME cydaw2_electro_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_electro_check.py"
                                     --port 47839 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_electro_check PROPERTIES TIMEOUT 300)
                    # The amp, cabinet and microphone simulators: their presets, their
                    # chain rendered offline as an exported plug-in gets it. Silent.
                    # The sample library with the banks, its categories, the AI
                    # models' licences and downloads (a local server), and the
                    # semantic search with a real CLAP when one is given
                    # (CYDAW_TEST_CLAP_MODEL: laion/clap-htsat-unfused's folder,
                    # CYDAW_TEST_CLAP_PYTHON: a Python with torch and
                    # transformers). Silent, offline.
                    set(CYDAW_TEST_CLAP_MODEL "" CACHE PATH "A laion/clap-htsat-unfused folder for the semantic search test (optional)")
                    set(CYDAW_TEST_CLAP_PYTHON "" CACHE FILEPATH "A Python with torch and transformers for the semantic search test (optional)")
                    set(cydaw_clap_test_arguments)
                    if(CYDAW_TEST_CLAP_MODEL AND CYDAW_TEST_CLAP_PYTHON)
                        set(cydaw_clap_test_arguments --clap-model "${CYDAW_TEST_CLAP_MODEL}" --clap-python "${CYDAW_TEST_CLAP_PYTHON}")
                    endif()
                    add_test(NAME cydaw2_sample_search_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_sample_search_check.py"
                                     --port 47841 --exe "$<TARGET_FILE:CyDAW2>" ${cydaw_clap_test_arguments})
                    set_tests_properties(cydaw2_sample_search_check PROPERTIES TIMEOUT 600)
                    add_test(NAME cydaw2_amps_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_amps_check.py"
                                     --port 47840 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_amps_check PROPERTIES TIMEOUT 300)
                    # Audio editing, takes and comping, the modulation graph,
                    # a hosted plug-in's knobs and modulation, Prisme's tables.
                    set(cydaw_editing_plugin_arguments)
                    if(TARGET CyDAWTestGain_VST3)
                        set(cydaw_editing_plugin_arguments
                            --plugin "${CMAKE_BINARY_DIR}/CyDAWTestGain_artefacts/$<CONFIG>/VST3/CyDAW Test Gain.vst3")
                    endif()
                    add_test(NAME cydaw2_editing_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_editing_check.py"
                                     --port 47842 --exe "$<TARGET_FILE:CyDAW2>" ${cydaw_editing_plugin_arguments})
                    set_tests_properties(cydaw2_editing_check PROPERTIES TIMEOUT 420)
                    # Latency compensation, sidechains, automation of an
                    # instrument's settings and of a knob inside a module.
                    add_test(NAME cydaw2_mixing_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_mixing_check.py"
                                     --port 47843 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_mixing_check PROPERTIES TIMEOUT 300)
                    # Pression, Fournaise, Écho Bande: presets, sound, a project.
                    add_test(NAME cydaw2_mix_effects_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_mix_effects_check.py"
                                     --port 47844 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_mix_effects_check PROPERTIES TIMEOUT 300)
                    # The synth family (Ambre…): settings, sound, presets, the track
                    # and an exported plug-in playing it, a project keeping it.
                    add_test(NAME cydaw2_synths_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_synths_check.py"
                                     --port 47845 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_synths_check PROPERTIES TIMEOUT 300)
                    # The creative effects, the MIDI effects, the pad rack, Silex
                    # and Nuée: presets, sound, order, a project keeping the pads.
                    add_test(NAME cydaw2_creative_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_creative_check.py"
                                     --port 47852 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_creative_check PROPERTIES TIMEOUT 400)
                    # An exported plug-in's window: CyDAW's own interface of its
                    # device (Prisme, Jade, Kick, the effects' large views), off
                    # the screen, and a host that closes it and exits.
                    if(TARGET cydaw_graph_runtime_host)
                        add_test(NAME cydaw2_plugin_window_check
                                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_plugin_window_check.py"
                                         --port 47853 --exe "$<TARGET_FILE:CyDAW2>"
                                         --host "$<TARGET_FILE:cydaw_graph_runtime_host>"
                                         --plugin "${CMAKE_BINARY_DIR}/CyDAWGraphRuntime_artefacts/$<CONFIG>/VST3/CyDAW Graph Runtime.vst3")
                        set_tests_properties(cydaw2_plugin_window_check PROPERTIES TIMEOUT 600)
                    endif()
                    # The Mix DJ mode: the collection, the decks (analysis, sync, loops,
                    # cues), the mixer, the mix recorded, the collection kept.
                    add_test(NAME cydaw2_dj_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_dj_check.py"
                                     --port 47854 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_dj_check PROPERTIES TIMEOUT 300)
                    # The Foundry: a sound file opened, edited, processed, saved with
                    # its markers, a track's effects rendered into it.
                    add_test(NAME cydaw2_foundry_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_foundry_check.py"
                                     --port 47855 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_foundry_check PROPERTIES TIMEOUT 300)
                    # Batterie: its kits (the banks downloaded on this computer;
                    # skipped without them), its pieces' mix, grooves, presets.
                    add_test(NAME cydaw2_batterie_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_batterie_check.py"
                                     --port 47846 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_batterie_check PROPERTIES TIMEOUT 300)
                    # An SFZ played as it moves: its controllers, keyswitches and
                    # release on the card, the samples decoded as they are reached.
                    add_test(NAME cydaw2_sfz_live_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_sfz_live_check.py"
                                     --port 47847 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_sfz_live_check PROPERTIES TIMEOUT 300)
                    # The library's loops heard at the project's tempo, in step
                    # with the transport.
                    add_test(NAME cydaw2_tempo_audition_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_tempo_audition_check.py"
                                     --port 47848 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_tempo_audition_check PROPERTIES TIMEOUT 300)
                    # Cards switched off: an effect lets its input through,
                    # an instrument falls silent, saved with the project.
                    add_test(NAME cydaw2_bypass_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_bypass_check.py"
                                     --port 47849 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_bypass_check PROPERTIES TIMEOUT 300)
                    # A knob jumping while a note plays is heard as a slope.
                    add_test(NAME cydaw2_knob_glide_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_knob_glide_check.py"
                                     --port 47850 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_knob_glide_check PROPERTIES TIMEOUT 300)
                    # What a synth's voice plays, shown as it plays.
                    add_test(NAME cydaw2_voice_live_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_voice_live_check.py"
                                     --port 47851 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_voice_live_check PROPERTIES TIMEOUT 300)
                    # Presets of a card, a track's chain and a hosted plug-in (the
                    # graph runtime VST3), in a presets folder of the test's own.
                    set(cydaw_presets_plugin_arguments)
                    if(TARGET CyDAWGraphRuntime)
                        set(cydaw_presets_plugin_arguments
                            --plugin "${CMAKE_BINARY_DIR}/CyDAWGraphRuntime_artefacts/$<CONFIG>/VST3/CyDAW Graph Runtime.vst3")
                    endif()
                    add_test(NAME cydaw2_presets_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_presets_check.py"
                                     --port 47830 --exe "$<TARGET_FILE:CyDAW2>" ${cydaw_presets_plugin_arguments})
                    set_tests_properties(cydaw2_presets_check PROPERTIES TIMEOUT 300)
                    # The sound matching companion, with a Python that has numpy
                    # (CYDAW_COMPANION_PYTHON when the one found has not).
                    set(CYDAW_COMPANION_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH
                        "The Python (with numpy) that runs the sound matching companion's test")
                    execute_process(COMMAND "${CYDAW_COMPANION_PYTHON}" -c "import numpy"
                                    RESULT_VARIABLE cydaw_companion_numpy OUTPUT_QUIET ERROR_QUIET)
                    if(cydaw_companion_numpy EQUAL 0)
                        add_test(NAME cydaw2_companion_analysis
                                 COMMAND "${CYDAW_COMPANION_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/companion/tests/test_companion.py")
                        set_tests_properties(cydaw2_companion_analysis PROPERTIES TIMEOUT 300)
                    endif()
                endif()
            endif()
            if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates")
                add_custom_command(TARGET CyDAW2 POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${CMAKE_CURRENT_SOURCE_DIR}/templates"
                        "$<TARGET_FILE_DIR:CyDAW2>/templates"
                    VERBATIM
                )
            endif()
        endif()

        install(TARGETS CyDAW
                RUNTIME DESTINATION . COMPONENT CyDAWRuntime
                BUNDLE DESTINATION . COMPONENT CyDAWRuntime)
        install(TARGETS CyDAWPluginWorker CyDAWAudioProbe
                RUNTIME DESTINATION . COMPONENT CyDAWRuntime)
        install(DIRECTORY Examples/JSFX/ DESTINATION JSFX COMPONENT CyDAWRuntime)
        install(FILES third_party/clap/LICENSE DESTINATION Licenses/PluginFormats RENAME CLAP.txt COMPONENT CyDAWRuntime)
        install(FILES third_party/lv2/COPYING DESTINATION Licenses/PluginFormats RENAME LV2.txt COMPONENT CyDAWRuntime)
        install(FILES third_party/ysfx/LICENSE DESTINATION Licenses/PluginFormats RENAME ysfx-Apache-2.txt COMPONENT CyDAWRuntime)
        install(FILES third_party/ysfx/thirdparty/WDL/LICENSE.txt DESTINATION Licenses/PluginFormats RENAME WDL.txt COMPONENT CyDAWRuntime)
        install(FILES third_party/ysfx/thirdparty/stb/LICENSE DESTINATION Licenses/PluginFormats RENAME stb.txt COMPONENT CyDAWRuntime)
        install(FILES third_party/ysfx/thirdparty/dr_libs/LICENSE DESTINATION Licenses/PluginFormats RENAME dr_libs.txt COMPONENT CyDAWRuntime)
        install(FILES docs/PLUGIN_FORMATS.md DESTINATION docs COMPONENT CyDAWRuntime)
        install(DIRECTORY resources/i18n
                DESTINATION . COMPONENT CyDAWRuntime)
        install(FILES README.md docs/TEST_BUILD_README.md
                DESTINATION . COMPONENT CyDAWRuntime)
        install(FILES docs/ARCHITECTURE.md docs/BUILD_MATRIX.md
                      docs/GRAPH_SYSTEM.md docs/PRODUCT_SPEC.md
                DESTINATION docs COMPONENT CyDAWRuntime)
        install(FILES docs/LINUX_WINDOWS_VST.md docs/VST2_BUILD.md
                DESTINATION docs COMPONENT CyDAWRuntime)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            install(PROGRAMS scripts/cydaw-windows-vst-bridge.sh
                    DESTINATION bin COMPONENT CyDAWRuntime)
        endif()
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
           AND CYDAW_WINDOWS_PLUGIN_WORKER)
            install(FILES "${CYDAW_WINDOWS_PLUGIN_WORKER}"
                    DESTINATION windows-bridge
                    RENAME CyDAWPluginWorker.exe
                    COMPONENT CyDAWRuntime)
        endif()
        if(EXISTS "${JUCE_SOURCE_DIR}/LICENSE.md")
            install(FILES "${JUCE_SOURCE_DIR}/LICENSE.md"
                    DESTINATION . RENAME JUCE_LICENSE.md COMPONENT CyDAWRuntime)
        endif()
        if(CYDAW_ASIO_AVAILABLE
           AND EXISTS "${JUCE_SOURCE_DIR}/modules/juce_audio_devices/native/asio/LICENSE.txt")
            install(FILES
                    "${JUCE_SOURCE_DIR}/modules/juce_audio_devices/native/asio/LICENSE.txt"
                    DESTINATION . RENAME ASIO_SDK_LICENSE.txt COMPONENT CyDAWRuntime)
        endif()
    else()
        message(STATUS
            "JUCE was not provided: building the CyDAW core only. "
            "Set CYDAW_JUCE_SOURCE_DIR or CYDAW_FETCH_JUCE=ON for the desktop shell.")
    endif()
endif()
