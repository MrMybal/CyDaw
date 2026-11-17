cmake_minimum_required(VERSION 3.24)

project(CyDAW VERSION 0.79.0 LANGUAGES C CXX)


# zstd's decoder (third_party/zstd, BSD): Zrythm's projects are compressed with it.
add_library(cydaw_zstd STATIC
    third_party/zstd/lib/common/debug.c
    third_party/zstd/lib/common/entropy_common.c
    third_party/zstd/lib/common/error_private.c
    third_party/zstd/lib/common/fse_decompress.c
    third_party/zstd/lib/common/xxhash.c
    third_party/zstd/lib/common/zstd_common.c
    third_party/zstd/lib/decompress/huf_decompress.c
    third_party/zstd/lib/decompress/zstd_ddict.c
    third_party/zstd/lib/decompress/zstd_decompress.c
    third_party/zstd/lib/decompress/zstd_decompress_block.c)
target_include_directories(cydaw_zstd PUBLIC third_party/zstd/lib)
target_compile_definitions(cydaw_zstd PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_LEGACY_SUPPORT=0 DEBUGLEVEL=0)
set_target_properties(cydaw_zstd PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(CYDAW_BUILD_TESTS "Build the dependency-free core tests" ON)
option(CYDAW_BUILD_DESKTOP "Build CyDaw with native CyAudioCore audio and CyUI interface" ON)
set(cydaw_asio_default OFF)
if(WIN32 AND CYDAW_BUILD_DESKTOP)
    set(cydaw_asio_default ON)
endif()
option(CYDAW_ENABLE_ASIO "Build CyDaw's GPL ASIO backend using PortAudio" ${cydaw_asio_default})
option(CYDAW_ENABLE_JACK "Discover optional JACK development headers (transport uses the bundled dynamic API)" ON)
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
option(CYDAW_ENABLE_PCH "Precompile the standard/native surface shared by the desktop sources" ON)
# The GPL ASIO SDK is selected by CyDaw, outside the MIT source repositories.
if(WIN32 AND CYDAW_ENABLE_ASIO)
    set(CYAUDIOCORE_WITH_PORTAUDIO ON CACHE BOOL "" FORCE)
endif()
include(cmake/CyAudioCore.cmake)
if(WIN32 AND CYDAW_ENABLE_ASIO)
    include(cmake/Asio.cmake)
endif()
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
            "Optional JACK development integration is disabled. Dynamic JACK transport "
            "remains available when its runtime is installed; set JACK_ROOT/JACK_SDK_DIR for headers.")
    endif()
endif()

message(STATUS
    "CyDAW audio backends: ASIO=${CYDAW_ASIO_AVAILABLE}, JACK=${CYDAW_JACK_AVAILABLE}; "
    "plugin formats: VST3=ON, CLAP=ON, LV2=ON, JSFX=ON")

function(cydaw_configure_audio_backends target_name)
    cydaw_copy_audio_notices(${target_name} "$<TARGET_FILE_DIR:${target_name}>/Licenses")
    target_compile_definitions(${target_name} PRIVATE CYDAW_WITH_ASIO=$<BOOL:${CYDAW_ASIO_AVAILABLE}> CYDAW_CORE_AUDIO_DEVICES=1)
    target_link_libraries(${target_name} PRIVATE cydaw_native_device_ports)
    if(CYDAW_JACK_AVAILABLE)
        target_include_directories(${target_name} PRIVATE "${CYDAW_JACK_INCLUDE_DIR}")
    endif()
endfunction()
function(cydaw_configure_plugin_formats target_name)
    target_compile_definitions(${target_name} PRIVATE CYDAW_CORE_PLUGIN_HOST=1)
endfunction()

add_library(cydaw_miniz STATIC third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c)
target_include_directories(cydaw_miniz PRIVATE third_party/miniz)
set_target_properties(cydaw_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(cydaw_base STATIC src/base/Platform.cpp src/base/Url.cpp src/base/Zip.cpp src/base/Socket.cpp src/base/Process.cpp src/base/FileDialog.cpp src/base/EventLoop.cpp src/base/Image.cpp src/base/Value.cpp src/base/Compression.cpp src/base/Thread.cpp src/base/File.cpp src/base/Xml.cpp src/base/Properties.cpp third_party/pugixml/pugixml.cpp)
target_include_directories(cydaw_base PUBLIC include PRIVATE third_party)
target_compile_features(cydaw_base PUBLIC cxx_std_20)
target_link_libraries(cydaw_base PRIVATE cydaw_miniz ${CMAKE_DL_LIBS})
if(WIN32)
 target_link_libraries(cydaw_base PRIVATE winhttp shell32 user32)
endif()
include(cmake/Translations.cmake)
add_library(cydaw_localization STATIC src/app/Localization.cpp)
target_link_libraries(cydaw_localization PUBLIC cydaw_base CyDAWTranslations)
target_include_directories(cydaw_localization PUBLIC src/app)
target_compile_features(cydaw_localization PUBLIC cxx_std_20)
set_target_properties(cydaw_localization PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(cydaw_localization PRIVATE /utf-8)
endif()
add_library(cydaw_audio_files STATIC src/integration/CyAudioCoreAudioFormats.cpp)
target_link_libraries(cydaw_audio_files PUBLIC cydaw_base CyAudioCore::Files)
target_compile_features(cydaw_audio_files PUBLIC cxx_std_20)
set_target_properties(cydaw_audio_files PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(cydaw_audio_files PRIVATE /utf-8 /W4 /permissive-)
endif()

set_target_properties(cydaw_base PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    target_link_libraries(cydaw_base PRIVATE shell32 ole32 uuid ws2_32)
endif()
if(MSVC)
    target_compile_options(cydaw_base PRIVATE /utf-8 /W4 /permissive-)
endif()

add_library(cydaw_core STATIC
    src/control/Osc.cpp
    src/integration/StandardMidiFile.cpp
    src/integration/LiveMidiInput.cpp
    src/app/ProjectRecovery.cpp
    src/app/NativeDspRuntime.cpp
    src/app/ModuleInterfaceState.cpp
    src/app/NativeDspGraphState.cpp
    src/native_plugin/NativeRuntime.cpp
    src/graph/GraphDefinition.cpp
    src/graph/GraphPluginExport.cpp
    src/graph/PrimitiveRegistry.cpp
    src/engine/AudioGraph.cpp
    src/engine/AudioBuffer.cpp
    src/engine/AudioRenderPlan.cpp
    src/engine/BuiltinProcessors.cpp
    src/engine/EffectProcessors.cpp
    src/engine/ContainerProcessors.cpp
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
    src/engine/SynthMatch.cpp
    src/engine/AudioRepair.cpp
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
    src/engine/VoiceAssetsFile.cpp
    src/control/Control.cpp
    src/control/DirectMidiBridge.cpp
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
target_link_libraries(cydaw_core PUBLIC cydaw_base CyAudioCore::Core)
# The core is also linked into shared plug-ins; in particular its thread-local
# transport state needs PIC relocations on Linux.
set_target_properties(cydaw_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(cydaw_core PUBLIC include)
target_compile_features(cydaw_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(cydaw_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(cydaw_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(cydaw_plugin_host STATIC src/plugin_worker/NativeHostedPlugin.cpp src/plugin_worker/NativePluginEditor.cpp src/plugin_worker/NativeLv2Programs.cpp src/plugin_worker/NativeScriptDiagnostics.cpp)
target_link_libraries(cydaw_plugin_host PUBLIC CyDAW::Core CyAudioCore::Plugins)
target_include_directories(cydaw_plugin_host PUBLIC src/plugin_worker)
target_include_directories(cydaw_plugin_host SYSTEM PRIVATE "${CYDAW_CYAUDIOCORE_SOURCE_DIR}/third_party/ysfx/include" "${CYDAW_CYAUDIOCORE_SOURCE_DIR}/third_party/lilv/include" "${CYDAW_CYAUDIOCORE_SOURCE_DIR}/third_party/lv2/include")
target_compile_definitions(cydaw_plugin_host PRIVATE LILV_STATIC)
target_compile_features(cydaw_plugin_host PUBLIC cxx_std_20)
add_executable(CyDAWPluginWorker src/plugin_worker/NativePluginWorkerMain.cpp src/plugin_worker/WorkerWake.cpp)
target_link_libraries(CyDAWPluginWorker PRIVATE cydaw_plugin_host)
if(WIN32)
    # SDK CreateProcess launches this utility directly. A GUI subsystem avoids
    # one otherwise unnecessary conhost.exe per plugin; retain the argv main
    # entry and inherited stdout/stderr used by command-line probes.
    set_target_properties(CyDAWPluginWorker PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(CyDAWPluginWorker PRIVATE /ENTRY:mainCRTStartup)
    else()
        target_link_options(CyDAWPluginWorker PRIVATE -Wl,-e,mainCRTStartup)
    endif()
endif()
set_target_properties(CyDAWPluginWorker PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/CyDAWPluginWorker_artefacts/$<CONFIG>")
if(WIN32)
    target_link_libraries(cydaw_plugin_host PRIVATE winmm comctl32 ole32)
    target_compile_definitions(cydaw_plugin_host PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(CyDAWPluginWorker PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(cydaw_plugin_host PRIVATE /utf-8 /EHsc)
    target_compile_options(CyDAWPluginWorker PRIVATE /utf-8 /EHsc)
endif()
add_library(cydaw_plugin_client STATIC src/app/IsolatedPluginHost.cpp src/app/WindowsPluginBridge.cpp src/plugin_worker/WorkerWake.cpp)
target_link_libraries(cydaw_plugin_client PUBLIC CyDAW::Core PRIVATE cydaw_plugin_host)
target_include_directories(cydaw_plugin_client PUBLIC src/app)
target_compile_features(cydaw_plugin_client PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(cydaw_plugin_client PRIVATE /utf-8 /EHsc)
endif()
add_library(cydaw_native_device_ports STATIC src/integration/NativeCoreDevices.cpp src/integration/DeviceManager.cpp src/integration/MidiDevices.cpp)
target_link_libraries(cydaw_native_device_ports PUBLIC CyDAW::Core CyAudioCore::Devices)
target_compile_features(cydaw_native_device_ports PUBLIC cxx_std_20)
target_compile_definitions(cydaw_native_device_ports PUBLIC CYDAW_WITH_ASIO=$<BOOL:${CYDAW_ASIO_AVAILABLE}>)
if(WIN32 AND CYDAW_ENABLE_ASIO)
    target_sources(cydaw_native_device_ports PRIVATE src/integration/NativeAsioDevice.cpp)
    target_link_libraries(cydaw_native_device_ports PRIVATE cydaw_asio)
endif()
if(MSVC)
    target_compile_options(cydaw_native_device_ports PRIVATE /utf-8)
endif()

# Optional factory authoring tool, using the application's native codecs.
add_executable(cydaw_sample_tool EXCLUDE_FROM_ALL tools/factory_content/SampleTool.cpp)
target_link_libraries(cydaw_sample_tool PRIVATE cydaw_audio_files CyDAW::Core)
target_compile_features(cydaw_sample_tool PRIVATE cxx_std_20)
set_target_properties(cydaw_sample_tool PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cydaw_sample_tool_artefacts/$<CONFIG>")
if(MSVC)
    target_compile_options(cydaw_sample_tool PRIVATE /utf-8 /EHsc)
endif()

add_library(cydaw_audio_sources STATIC src/integration/AudioSources.cpp src/integration/BufferedAudioSource.cpp src/integration/MixerAudioSource.cpp)
target_link_libraries(cydaw_audio_sources PUBLIC CyDAW::Core CyAudioCore::Sources)
target_compile_features(cydaw_audio_sources PUBLIC cxx_std_20)
set_target_properties(cydaw_audio_sources PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(cydaw_assets STATIC src/app/PolySynthState.cpp src/app/SamplerMapFile.cpp src/app/SoundModelFile.cpp src/app/GraphPluginProjectGenerator.cpp)
target_link_libraries(cydaw_assets PUBLIC CyDAW::Core cydaw_audio_files)
target_include_directories(cydaw_assets PUBLIC src/app)
target_compile_features(cydaw_assets PUBLIC cxx_std_20)
set_target_properties(cydaw_assets PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(cydaw_assets PRIVATE /utf-8)
endif()

add_library(cydaw_instrument_renderer STATIC src/app/GraphPolySynthRenderer.cpp)
target_link_libraries(cydaw_instrument_renderer PUBLIC cydaw_assets)
target_compile_features(cydaw_instrument_renderer PUBLIC cxx_std_20)
set_target_properties(cydaw_instrument_renderer PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(cydaw_instrument_renderer PRIVATE /utf-8)
endif()

add_library(cydaw_graph_document STATIC src/app/GraphDocument.cpp src/app/GraphDocumentEditorAccess.cpp src/app/GraphDocumentFactoryData.cpp)
target_link_libraries(cydaw_graph_document PUBLIC cydaw_assets cydaw_localization)
target_compile_features(cydaw_graph_document PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(cydaw_graph_document PRIVATE /utf-8 /bigobj)
endif()

add_executable(CyDAWAudioProbe src/audio_probe/AudioBackendProbeMain.cpp)
target_link_libraries(CyDAWAudioProbe PRIVATE cydaw_native_device_ports)
target_compile_features(CyDAWAudioProbe PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(CyDAWAudioProbe PRIVATE /utf-8 /EHsc)
endif()
target_compile_definitions(CyDAWAudioProbe PRIVATE CYDAW_WITH_JACK=$<BOOL:${CYDAW_JACK_AVAILABLE}>)
if(CYDAW_BUILD_TESTS)
    enable_testing()
    add_executable(cydaw_frame_pacing_tests tests/FramePacingTests.cpp)
    target_include_directories(cydaw_frame_pacing_tests PRIVATE src/v2)
    target_compile_features(cydaw_frame_pacing_tests PRIVATE cxx_std_20)
    add_test(NAME cydaw_frame_pacing_tests COMMAND cydaw_frame_pacing_tests)
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

    add_executable(cydaw_dsp_adapter_tests tests/DspAdapterTests.cpp)
    target_link_libraries(cydaw_dsp_adapter_tests PRIVATE CyDAW::Core)
    target_include_directories(cydaw_dsp_adapter_tests PRIVATE src/engine)
    add_test(NAME cydaw_dsp_adapter_tests COMMAND cydaw_dsp_adapter_tests)
    cydaw_configure_test(cydaw_dsp_adapter_tests 30)
    add_executable(cydaw_full_dsp_reuse_tests tests/FullDspReuseTests.cpp)
    target_link_libraries(cydaw_full_dsp_reuse_tests PRIVATE CyDAW::Core)
    target_include_directories(cydaw_full_dsp_reuse_tests PRIVATE src/engine)
    add_test(NAME cydaw_full_dsp_reuse_tests COMMAND cydaw_full_dsp_reuse_tests)
    cydaw_configure_test(cydaw_full_dsp_reuse_tests 180)
    add_executable(cydaw_dsp_reuse_tests tests/DspReuseTests.cpp)
    target_link_libraries(cydaw_dsp_reuse_tests PRIVATE CyDAW::Core)
    add_test(NAME cydaw_dsp_reuse_tests COMMAND cydaw_dsp_reuse_tests)
    cydaw_configure_test(cydaw_dsp_reuse_tests 120)
    add_executable(cydaw_direct_midi_tests tests/DirectMidiBridgeTests.cpp)
    target_link_libraries(cydaw_direct_midi_tests PRIVATE CyDAW::Core)
    add_test(NAME cydaw_direct_midi_tests COMMAND cydaw_direct_midi_tests)
    cydaw_configure_test(cydaw_direct_midi_tests 30)
    add_executable(cydaw_engine_tests tests/EngineTests.cpp)
    target_link_libraries(cydaw_engine_tests PRIVATE CyDAW::Core)
    if(MSVC)
        target_compile_options(cydaw_engine_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(cydaw_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME cydaw_engine_tests COMMAND cydaw_engine_tests)
    cydaw_configure_test(cydaw_engine_tests 120)

    # Independent acceptance for the CyAudioCore services. No JUCE target,
    # headers, physical device or installed third-party plugin is required.
    add_executable(cydaw_cyaudio_services_tests tests/CyAudioCoreServicesTests.cpp)
    target_link_libraries(cydaw_cyaudio_services_tests PRIVATE CyAudioCore::Files CyAudioCore::Devices CyAudioCore::Plugins)
    target_compile_features(cydaw_cyaudio_services_tests PRIVATE cxx_std_20)
    add_test(NAME cydaw_cyaudio_services_tests COMMAND cydaw_cyaudio_services_tests)
    cydaw_configure_test(cydaw_cyaudio_services_tests 120)
    add_executable(cydaw_cyaudio_dsp_tests tests/CyAudioCoreDspTests.cpp)
    target_link_libraries(cydaw_cyaudio_dsp_tests PRIVATE CyDAW::Core)
    add_test(NAME cydaw_cyaudio_dsp_tests COMMAND cydaw_cyaudio_dsp_tests)
    cydaw_configure_test(cydaw_cyaudio_dsp_tests 120)
    if(TARGET cydaw_asio)
        add_executable(cydaw_asio_tests tests/AsioDriverTests.cpp)
        target_link_libraries(cydaw_asio_tests PRIVATE cydaw_asio)
        target_include_directories(cydaw_asio_tests PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/third_party/portaudio-asio"
            "${CYDAW_CYAUDIOCORE_SOURCE_DIR}/third_party/portaudio/include")
        add_test(NAME cydaw_asio_tests COMMAND cydaw_asio_tests)
        cydaw_configure_test(cydaw_asio_tests 60)
    endif()

endif()

if(CYDAW_BUILD_TESTS)
    add_executable(cydaw_native_osc_tests tests/NativeOscTests.cpp)
    target_link_libraries(cydaw_native_osc_tests PRIVATE CyDAW::Core)
    add_test(NAME cydaw_native_osc_tests COMMAND cydaw_native_osc_tests)
    cydaw_configure_test(cydaw_native_osc_tests 30)
    if(WIN32)
        add_executable(cydaw_native_plugin_window_probe tests/NativePluginWindowProbe.cpp)
        target_link_libraries(cydaw_native_plugin_window_probe PRIVATE cydaw_plugin_host ole32)
        if(MSVC)
            target_compile_options(cydaw_native_plugin_window_probe PRIVATE /utf-8 /EHsc)
        endif()
    endif()
    add_executable(cydaw_native_plugin_ipc_tests tests/PluginIpcTests.cpp src/v2/HiddenDesktop.cpp)
    target_link_libraries(cydaw_native_plugin_ipc_tests PRIVATE cydaw_plugin_client)
    add_test(NAME cydaw_native_plugin_ipc_tests COMMAND cydaw_native_plugin_ipc_tests
        $<TARGET_FILE:CyDAWPluginWorker> --jsfx-fixture --silent)
    cydaw_configure_test(cydaw_native_plugin_ipc_tests 40)
    add_library(cydaw_plugin_sleep_fixture MODULE tests/PluginSleepFixture.cpp)
    target_compile_features(cydaw_plugin_sleep_fixture PRIVATE cxx_std_20)
    target_include_directories(cydaw_plugin_sleep_fixture SYSTEM PRIVATE
        "${CYDAW_CYAUDIOCORE_SOURCE_DIR}/third_party/clap/include")
    set_target_properties(cydaw_plugin_sleep_fixture PROPERTIES PREFIX "" SUFFIX ".clap")
    add_executable(cydaw_native_plugin_sleep_tests tests/NativePluginSleepTests.cpp)
    target_link_libraries(cydaw_native_plugin_sleep_tests PRIVATE cydaw_plugin_host)
    add_dependencies(cydaw_native_plugin_sleep_tests cydaw_plugin_sleep_fixture CyDAWPluginWorker)
    add_test(NAME cydaw_native_plugin_sleep_tests COMMAND cydaw_native_plugin_sleep_tests
        $<TARGET_FILE:cydaw_plugin_sleep_fixture> $<TARGET_FILE:CyDAWPluginWorker>)
    cydaw_configure_test(cydaw_native_plugin_sleep_tests 30)
    add_executable(cydaw_native_hosted_plugin_tests tests/NativeHostedPluginTests.cpp)
    target_link_libraries(cydaw_native_hosted_plugin_tests PRIVATE cydaw_plugin_host)
    add_test(NAME cydaw_native_hosted_plugin_tests COMMAND cydaw_native_hosted_plugin_tests)
    cydaw_configure_test(cydaw_native_hosted_plugin_tests 30)
    add_executable(cydaw_native_device_manager_tests tests/NativeDeviceManagerTests.cpp)
    target_link_libraries(cydaw_native_device_manager_tests PRIVATE cydaw_native_device_ports)
    add_test(NAME cydaw_native_device_manager_tests COMMAND cydaw_native_device_manager_tests)
    cydaw_configure_test(cydaw_native_device_manager_tests 30)
    add_executable(cydaw_native_device_ports_tests tests/NativeDevicePortsTests.cpp)
    target_link_libraries(cydaw_native_device_ports_tests PRIVATE cydaw_native_device_ports)
    add_test(NAME cydaw_native_device_ports_tests COMMAND cydaw_native_device_ports_tests)
    cydaw_configure_test(cydaw_native_device_ports_tests 30)
    add_executable(cydaw_live_midi_tests tests/LiveMidiInputTests.cpp)
    target_link_libraries(cydaw_live_midi_tests PRIVATE CyDAW::Core)
    add_test(NAME cydaw_live_midi_tests COMMAND cydaw_live_midi_tests)
    cydaw_configure_test(cydaw_live_midi_tests 30)
    add_executable(cydaw_instrument_renderer_tests tests/InstrumentRendererTests.cpp)
    target_link_libraries(cydaw_instrument_renderer_tests PRIVATE cydaw_instrument_renderer)
    add_test(NAME cydaw_instrument_renderer_tests COMMAND cydaw_instrument_renderer_tests)
    cydaw_configure_test(cydaw_instrument_renderer_tests 120)
    add_executable(cydaw_audio_source_tests tests/AudioSourceTests.cpp)
    target_link_libraries(cydaw_audio_source_tests PRIVATE cydaw_audio_sources)
    add_test(NAME cydaw_audio_source_tests COMMAND cydaw_audio_source_tests)
    cydaw_configure_test(cydaw_audio_source_tests 30)
    add_executable(cydaw_sample_buffer_tests tests/SampleBufferTests.cpp)
    target_link_libraries(cydaw_sample_buffer_tests PRIVATE CyDAW::Core)
    add_test(NAME cydaw_sample_buffer_tests COMMAND cydaw_sample_buffer_tests)
    cydaw_configure_test(cydaw_sample_buffer_tests 30)
    add_executable(cydaw_remote_native_tests tests/RemoteControlNativeTests.cpp src/app/RemoteControlServer.cpp)
    target_link_libraries(cydaw_remote_native_tests PRIVATE cydaw_base)
    target_include_directories(cydaw_remote_native_tests PRIVATE src/app)
    add_test(NAME cydaw_remote_native_tests COMMAND cydaw_remote_native_tests)
    cydaw_configure_test(cydaw_remote_native_tests 30)
    add_executable(cydaw_process_tests tests/ProcessTests.cpp)
    target_link_libraries(cydaw_process_tests PRIVATE cydaw_base)
    add_test(NAME cydaw_process_tests COMMAND cydaw_process_tests)
    cydaw_configure_test(cydaw_process_tests 30)
    add_executable(cydaw_event_loop_tests tests/EventLoopTests.cpp)
    target_link_libraries(cydaw_event_loop_tests PRIVATE cydaw_base)
    add_test(NAME cydaw_event_loop_tests COMMAND cydaw_event_loop_tests)
    cydaw_configure_test(cydaw_event_loop_tests 30)
    add_executable(cydaw_midi_file_tests tests/StandardMidiFileTests.cpp)
    target_link_libraries(cydaw_midi_file_tests PRIVATE CyDAW::Core)
    add_test(NAME cydaw_midi_file_tests COMMAND cydaw_midi_file_tests)
    cydaw_configure_test(cydaw_midi_file_tests 60)
    add_executable(cydaw_graph_document_tests tests/GraphDocumentTests.cpp)
    target_link_libraries(cydaw_graph_document_tests PRIVATE cydaw_graph_document)
    add_test(NAME cydaw_graph_document_tests COMMAND cydaw_graph_document_tests)
    cydaw_configure_test(cydaw_graph_document_tests 120)
    add_executable(cydaw_asset_services_tests tests/AssetServicesTests.cpp)
    target_link_libraries(cydaw_asset_services_tests PRIVATE cydaw_assets)
    add_test(NAME cydaw_asset_services_tests COMMAND cydaw_asset_services_tests)
    cydaw_configure_test(cydaw_asset_services_tests 60)
    add_executable(cydaw_compression_tests tests/CompressionTests.cpp)
    target_link_libraries(cydaw_compression_tests PRIVATE cydaw_base)
    add_test(NAME cydaw_compression_tests COMMAND cydaw_compression_tests)
    cydaw_configure_test(cydaw_compression_tests 30)
    add_executable(cydaw_localization_tests tests/LocalizationTests.cpp)
    target_link_libraries(cydaw_localization_tests PRIVATE cydaw_localization)
    target_compile_definitions(cydaw_localization_tests PRIVATE CYDAW_TRANSLATIONS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n")
    add_test(NAME cydaw_localization_tests COMMAND cydaw_localization_tests)
    cydaw_configure_test(cydaw_localization_tests 60)
    add_executable(cydaw_thread_tests tests/ThreadTests.cpp)
    target_link_libraries(cydaw_thread_tests PRIVATE cydaw_base)
    add_test(NAME cydaw_thread_tests COMMAND cydaw_thread_tests)
    cydaw_configure_test(cydaw_thread_tests 30)
    add_executable(cydaw_audio_files_tests tests/AudioFilesTests.cpp)
    target_link_libraries(cydaw_audio_files_tests PRIVATE cydaw_audio_files)
    add_test(NAME cydaw_audio_files_tests COMMAND cydaw_audio_files_tests)
    cydaw_configure_test(cydaw_audio_files_tests 60)
    add_executable(cydaw_dj_cue_tests tests/DjCueTests.cpp src/app/DjEngine.cpp)
    target_include_directories(cydaw_dj_cue_tests PRIVATE src/app)
    target_link_libraries(cydaw_dj_cue_tests PRIVATE CyDAW::Core cydaw_audio_files)
    if(WIN32)
        target_link_libraries(cydaw_dj_cue_tests PRIVATE ole32)
    endif()
    if(MSVC)
        target_compile_options(cydaw_dj_cue_tests PRIVATE /utf-8 /EHsc)
    endif()
    add_test(NAME cydaw_dj_cue_tests COMMAND cydaw_dj_cue_tests)
    cydaw_configure_test(cydaw_dj_cue_tests 60)
    if(WIN32)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_executable(cydaw_native_platform_tests tests/NativePlatformTests.cpp)
            target_link_libraries(cydaw_native_platform_tests PRIVATE cydaw_base)
            if(MSVC)
                target_compile_options(cydaw_native_platform_tests PRIVATE /utf-8 /EHsc)
            endif()
            add_test(NAME cydaw_native_platform_tests COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_platform_check.py" "$<TARGET_FILE:cydaw_native_platform_tests>")
            cydaw_configure_test(cydaw_native_platform_tests 60)
        endif()
    endif()
    add_executable(cydaw_base_tests tests/BaseTests.cpp)
    target_link_libraries(cydaw_base_tests PRIVATE CyDAW::Core)
    add_test(NAME cydaw_base_tests COMMAND cydaw_base_tests)
    cydaw_configure_test(cydaw_base_tests 60)
endif()

if(CYDAW_BUILD_DESKTOP AND WIN32)
    if(NOT TARGET cydaw_imgui)
        add_library(cydaw_imgui STATIC
            third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
            third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
            third_party/imgui/imgui_demo.cpp third_party/imgui/backends/imgui_impl_win32.cpp
            third_party/imgui/backends/imgui_impl_dx11.cpp)
        target_include_directories(cydaw_imgui PUBLIC third_party/imgui)
        target_compile_features(cydaw_imgui PUBLIC cxx_std_17)
        target_link_libraries(cydaw_imgui PUBLIC d3d11 dxgi dwmapi d3dcompiler)
    endif()
    if(NOT TARGET CyUI::Core)
        include("${CMAKE_CURRENT_SOURCE_DIR}/src/v2/CyUI.cmake")
    endif()
        # CyDAW 2: a new interface in Dear ImGui (docking branch, third_party/imgui)
        # over the same engine, rendered with Direct3D 11 on the message thread.
        # src/v2 holds it.
        option(CYDAW_BUILD_V2 "Build CyDAW 2, the Dear ImGui interface" ON)
        if(WIN32 AND CYDAW_BUILD_V2)
            if(CYDAW_BUILD_TESTS)
                find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
            endif()
            add_executable(CyDAW2 WIN32)
            set(CYDAW_DESKTOP_RUNTIME_ROOT "${CMAKE_BINARY_DIR}/CyDAW2_artefacts" CACHE PATH
                "Runtime output root; override to build beside an already running portable copy")
            set_target_properties(CyDAW2 PROPERTIES OUTPUT_NAME "CyDAW 2"
                RUNTIME_OUTPUT_DIRECTORY "${CYDAW_DESKTOP_RUNTIME_ROOT}/$<CONFIG>")
            include(cmake/NativeDesktopResources.cmake)
            cydaw_native_desktop_resources(CyDAW2)
            # Header-only Link transport (no audio sharing) and JACK's dynamically loaded control API.
            include("${CMAKE_CURRENT_SOURCE_DIR}/third_party/ableton-link/AbletonLinkConfig.cmake")
            target_link_libraries(CyDAW2 PRIVATE Ableton::Link ws2_32 iphlpapi advapi32)
            include("${CMAKE_CURRENT_SOURCE_DIR}/src/v2/FFmpeg.cmake")
            target_compile_definitions(CyDAW2 PRIVATE ASIO_STANDALONE)
            target_include_directories(CyDAW2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jack-transport/include")
            # The logo inside the interface (menu bar, About, opening a project).
            target_link_libraries(CyDAW2 PRIVATE cydaw_zstd)
            # The Processing mode runs JSFX (ysfx) and note scripts (its EEL2) offline.
            target_link_libraries(CyDAW2 PRIVATE CyAudioCore::Plugins cydaw_plugin_host)
            target_include_directories(CyDAW2 PRIVATE "${CYDAW_CYAUDIOCORE_SOURCE_DIR}/third_party/ysfx/include" "${CYDAW_CYAUDIOCORE_SOURCE_DIR}/third_party/ysfx/thirdparty/WDL/source")
            target_sources(CyDAW2 PRIVATE
                src/v2/HelpDocumentation.cpp
                src/v2/HelpDocumentation.h
                src/v2/ProjectFileAssociation.cpp
                src/v2/ProjectFileAssociation.h
                src/v2/Main.cpp
                src/v2/HiddenDesktop.cpp
                src/v2/Processing.cpp
                src/v2/Processing.h
                src/v2/ProcessingCode.cpp
                src/v2/ProcessingCode.h
                src/v2/ProcessingFactory.cpp
                src/v2/ProcessingView.cpp
                src/v2/ProcessingView.h
                src/v2/CinemaVideo.cpp
                src/v2/CinemaVideo.h
                src/v2/CinemaEditing.inc
                src/v2/CinemaTimeline.inc
                src/v2/TransportSync.cpp
                src/v2/TransportSync.h
                src/v2/ControlTargets.cpp
                src/v2/ControlTargets.h
                src/v2/Controllers.cpp
                src/v2/NativeControllerInput.cpp
                src/v2/Controllers.h
                src/v2/ControllersImpl.h
                src/v2/ControllersView.cpp
                src/v2/AppUi.cpp
                src/v2/AppUi.h
                src/v2/ImGuiHost.cpp
                src/v2/ImGuiHost.h
                src/v2/FramePacing.h
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
                src/v2/ThemeRuntime.cpp
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
                src/v2/ModulatorsView.cpp
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
                src/v2/CurveEditor.cpp
                src/v2/MidiDevices.cpp
                src/v2/SystemLoad.cpp
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
                src/plugin_worker/WorkerWake.cpp
                src/app/WindowsPluginBridge.cpp
                src/app/SampleLibrary.cpp
                src/app/ProjectImport.cpp
                src/app/ProjectImportShared.cpp
                src/app/ProjectImportReaper.cpp
                src/app/ProjectImportFlStudio.cpp
                src/app/ProjectImportArdour.cpp
                src/app/ProjectImportLmms.cpp
                src/app/ProjectImportAaf.cpp
                src/app/ProjectImportZrythm.cpp
                src/app/ProjectImportMidi.cpp
                src/app/RemoteControlServer.cpp
                src/app/GraphPolySynthRenderer.cpp
            )
            target_include_directories(CyDAW2 PRIVATE src/app src/v2)
            target_compile_features(CyDAW2 PRIVATE cxx_std_20)
            target_compile_definitions(CyDAW2 PRIVATE
                CYDAW_APPLICATION_NAME="CyDaw"
                CYDAW_APPLICATION_VERSION="${PROJECT_VERSION}"
            )
            cydaw_configure_plugin_formats(CyDAW2)
            cydaw_configure_audio_backends(CyDAW2)
            if(CYDAW_BUILD_TESTS)
                add_executable(cydaw_help_tests tests/HelpDocumentationTests.cpp src/v2/HelpDocumentation.cpp)
                target_include_directories(cydaw_help_tests PRIVATE src/v2)
                target_compile_features(cydaw_help_tests PRIVATE cxx_std_20)
                target_link_libraries(cydaw_help_tests PRIVATE cydaw_base)
                if(MSVC)
                    target_compile_options(cydaw_help_tests PRIVATE /utf-8 /EHsc)
                endif()
                add_test(NAME cydaw_help_routes COMMAND cydaw_help_tests "${CMAKE_BINARY_DIR}/Documentation")
                cydaw_configure_test(cydaw_help_routes 60)
                add_dependencies(cydaw_help_tests cydaw_documentation)
                find_package(Python3 COMPONENTS Interpreter QUIET)
                if(Python3_Interpreter_FOUND)
                    add_test(NAME cydaw_help_preservation COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/cydoc_prepare_check.py")
                    add_test(NAME cydaw_help_export COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/.CyDocBuild/tools/validate.py" --root "${CMAKE_BINARY_DIR}/Documentation")
                    add_test(NAME cydaw_manual_reference COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/.CyDocBuild/capture/check_reference.py" --check)
                    if(WIN32)
                        add_test(NAME cydaw_help_entrypoints COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_help_check.py"
                            --exe "$<TARGET_FILE:CyDAW2>" --output "${CMAKE_BINARY_DIR}/help-check" --port 48064)
                        set_tests_properties(cydaw_help_entrypoints PROPERTIES TIMEOUT 180)
                    endif()
                endif()
                add_executable(cydaw_cyui_tests tests/CyUiTests.cpp
                    src/v2/Theme.cpp src/v2/ThemeRuntime.cpp src/v2/Widgets.cpp)
                target_include_directories(cydaw_cyui_tests PRIVATE src/v2)
                target_compile_features(cydaw_cyui_tests PRIVATE cxx_std_20)

                target_link_libraries(cydaw_cyui_tests PRIVATE CyUI::Core CyUI::Audio CyUI::AudioViews CyUI::Nodes CyUI::CodeEditor CyUI::Images CyUI::Inspector cydaw_base)
                if(MSVC)
                    target_compile_options(cydaw_cyui_tests PRIVATE /utf-8 /EHsc)
                endif()
                add_test(NAME cydaw_cyui_tests COMMAND cydaw_cyui_tests)
                cydaw_configure_test(cydaw_cyui_tests 60)
            endif()
            if(CYDAW_BUILD_TESTS AND Python3_Interpreter_FOUND AND MSVC)
                add_executable(cydaw_project_file_tests tests/ProjectFileAssociationTests.cpp src/v2/ProjectFileAssociation.cpp)
                target_include_directories(cydaw_project_file_tests PRIVATE src/v2)
                target_compile_features(cydaw_project_file_tests PRIVATE cxx_std_20)
                target_compile_definitions(cydaw_project_file_tests PRIVATE UNICODE _UNICODE)
                target_compile_options(cydaw_project_file_tests PRIVATE /utf-8)
                target_link_libraries(cydaw_project_file_tests PRIVATE advapi32 shell32)
                add_dependencies(cydaw_project_file_tests CyDAW2)
                add_test(NAME cydaw_project_file_association COMMAND cydaw_project_file_tests "$<TARGET_FILE:CyDAW2>")
                cydaw_configure_test(cydaw_project_file_association 30)
                add_test(NAME cydaw_no_juce_production_graph COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/no_juce_build_check.py" "${CMAKE_BINARY_DIR}")
                cydaw_configure_test(cydaw_no_juce_production_graph 30)
                add_test(NAME cydaw_native_headless COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_headless_check.py"
                    --exe "$<TARGET_FILE:CyDAW2>" --output "${CMAKE_BINARY_DIR}/headless-check")
                cydaw_configure_test(cydaw_native_headless 90)
            endif()
            if(CYDAW_BUILD_TESTS AND Python3_Interpreter_FOUND)
                add_library(cydaw_transport_jack_fixture SHARED tests/TransportJackFixture.cpp)
                set_target_properties(cydaw_transport_jack_fixture PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
                target_include_directories(cydaw_transport_jack_fixture PRIVATE third_party/jack-transport/include)
                target_compile_features(cydaw_transport_jack_fixture PRIVATE cxx_std_20)
                add_test(NAME cydaw2_transport_sync_check COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_silent_session.py"
                    --exe "$<TARGET_FILE:CyDAW2>" --port 48084 --peer-port 48085 "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_transport_sync_check.py"
                    --port 48084 --peer-port 48085 --output "${CMAKE_BINARY_DIR}/transport-check")
                add_test(NAME cydaw2_transport_jack_check COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_silent_session.py"
                    --exe "$<TARGET_FILE:CyDAW2>" --port 48086 "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_transport_jack_check.py"
                    --port 48086 --library "$<TARGET_FILE:cydaw_transport_jack_fixture>")
                set_tests_properties(cydaw2_transport_sync_check cydaw2_transport_jack_check PROPERTIES TIMEOUT 360)
                execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import PIL,imageio_ffmpeg;print(imageio_ffmpeg.get_ffmpeg_exe())"
                    RESULT_VARIABLE native_video_test_deps OUTPUT_VARIABLE native_video_test_ffmpeg OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
                if(native_video_test_deps EQUAL 0)
                    foreach(check layout codecs)
                        add_test(NAME cydaw2_cinema_${check}_check COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_cinema_${check}_check.py"
                            --exe "$<TARGET_FILE:CyDAW2>" --output "${CMAKE_BINARY_DIR}/cinema-${check}-check")
                        set_tests_properties(cydaw2_cinema_${check}_check PROPERTIES TIMEOUT 360)
                    endforeach()
                    add_test(NAME cydaw2_cinema_check COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_silent_session.py"
                        --exe "$<TARGET_FILE:CyDAW2>" --port 48087 "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_cinema_check.py"
                        --port 48087 --output "${CMAKE_BINARY_DIR}/cinema-check" --ffmpeg "${native_video_test_ffmpeg}")
                    set_tests_properties(cydaw2_cinema_check PROPERTIES TIMEOUT 360)
                endif()
            endif()
            include(cmake/CyDoc.cmake)
            cydaw_add_documentation(CyDAW2)
            cydaw_copy_cyui_notices(CyDAW2)
            if(CYDAW_ENABLE_PCH)
                target_precompile_headers(CyDAW2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/app/CyDawPrecompiled.h")
            endif()
            if(MSVC)
                target_compile_options(CyDAW2 PRIVATE /utf-8 /EHsc /bigobj /MP4)
            endif()
            if(CYDAW_ENABLE_LTO)
                set_property(TARGET CyDAW2 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
            endif()
            target_link_libraries(CyDAW2 PRIVATE
                cydaw_audio_sources
                CyDAW::Core
                cydaw_localization
                cydaw_assets
                cydaw_graph_document
                CyUI::Core CyUI::Audio CyUI::AudioViews CyUI::Nodes
                CyUI::CodeEditor CyUI::Images CyUI::Inspector
                cydaw_imgui
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
            # An application plug-in for the controllers tool (gamepads read
            # through XInput), beside CyDAW 2 in ControlPlugins: the
            # interface's example as much as a device.
            if(WIN32)
                add_library(CyDAWControlGamepad SHARED tools/control_plugins/gamepad/Gamepad.cpp)
                target_include_directories(CyDAWControlGamepad PRIVATE include)
                target_compile_features(CyDAWControlGamepad PRIVATE cxx_std_20)
                target_link_libraries(CyDAWControlGamepad PRIVATE xinput)
                add_dependencies(CyDAW2 CyDAWControlGamepad)
                add_custom_command(TARGET CyDAW2 POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CyDAW2>/ControlPlugins"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:CyDAWControlGamepad>"
                        "$<TARGET_FILE_DIR:CyDAW2>/ControlPlugins/Gamepads.cyctl"
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
                # Track modulation moves the engine's parameters and lets go of them
                # (on an example project: they stay local, not in the repository).
                if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/templates/06-tipper-glitch.cydaw")
                    add_test(NAME cydaw2_modulation
                             COMMAND CyDAW2 "--modulation-test=${CMAKE_CURRENT_SOURCE_DIR}/templates/06-tipper-glitch.cydaw")
                    set_tests_properties(cydaw2_modulation PROPERTIES TIMEOUT 120)
                endif()
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
                # It starts from an example project (local, not in the repository).
                if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/templates/02-basse-et-nappes.cydaw")
                    add_test(NAME cydaw2_mcp_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mcp/cydaw_mcp_check.py"
                                     --port 47822 --exe "$<TARGET_FILE:CyDAW2>" --quit)
                    set_tests_properties(cydaw2_mcp_check PROPERTIES TIMEOUT 300)
                    # A sound model through devices, the project, the workshop,
                    # My devices and the VST3 export (silent; nothing built).
                    set(cydaw_sound_model_host_arguments)
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
                    # The Traitements mode: audio processings (noise, hum, tails
                    # that ring past the end, endless noise cut) and MIDI ones on
                    # clips, undone; code checked; the apply window. Silent.
                    add_test(NAME cydaw2_processing_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_processing_check.py"
                                     --port 47871 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_processing_check PROPERTIES TIMEOUT 300)
                    # The controllers: an OSC endpoint in and back on this
                    # machine's sockets, virtual controls through modifiers,
                    # learning, a project's assignments. Silent.
                    add_test(NAME cydaw2_controllers_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_controllers_check.py"
                                     --port 47856 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_controllers_check PROPERTIES TIMEOUT 300)
                    # A card's list of values: every value, the last touched
                    # first, its search. Silent.
                    add_test(NAME cydaw2_value_list_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_value_list_check.py"
                                     --port 47857 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_value_list_check PROPERTIES TIMEOUT 300)
                    # Modulators on the cards: a macro and an LFO on a utility's
                    # knobs (the chain's sound offline), an envelope on an
                    # instrument's, armed, saved and opened again. Silent.
                    add_test(NAME cydaw2_modulators_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_modulators_check.py"
                                     --port 47858 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_modulators_check PROPERTIES TIMEOUT 300)
                    # Containers: layers summed, muted, a card moved by a drag, a
                    # container inside one, bands split, saved and opened again.
                    # Silent.
                    add_test(NAME cydaw2_containers_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_containers_check.py"
                                     --port 47859 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_containers_check PROPERTIES TIMEOUT 300)
                    # Instrument containers (Drum Machine, Instrument Layer,
                    # Instrument Selector), branch names, container presets,
                    # plug-in value lists. Silent (offline exports).
                    add_test(NAME cydaw2_instrument_containers_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_instrument_containers_check.py"
                                     --port 47863 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_instrument_containers_check PROPERTIES TIMEOUT 600)
                    # Projects of other applications imported (Reaper, FL
                    # Studio, Ardour, LMMS, Zrythm, a MIDI file and its stems;
                    # real AAF and other test projects when
                    # CYDAW_IMPORT_SAMPLES names them). Silent.
                    add_test(NAME cydaw2_project_import_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_project_import_check.py"
                                     --port 47867 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_project_import_check PROPERTIES TIMEOUT 600)
                    # Batterie: its kits (the banks downloaded on this computer;
                    # skipped without them), its pieces' mix, grooves, presets.
                    add_test(NAME cydaw2_batterie_check
                             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_batterie_check.py"
                                     --port 47846 --exe "$<TARGET_FILE:CyDAW2>")
                    set_tests_properties(cydaw2_batterie_check PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
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

        if(TARGET CyDAW2)
            install(DIRECTORY "${CMAKE_BINARY_DIR}/Documentation" DESTINATION . COMPONENT CyDAWRuntime)
            install(TARGETS CyDAW2
                    RUNTIME DESTINATION . COMPONENT CyDAWRuntime
                    RESOURCE DESTINATION . COMPONENT CyDAWRuntime)
        endif()
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
        install(FILES README.md docs/V2_TEST_BUILD.md
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

endif()
