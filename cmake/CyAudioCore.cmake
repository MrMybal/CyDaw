# Explicit CI paths are supplied by the dependency checkout action.
set(cydaw_audio_default "${CMAKE_CURRENT_SOURCE_DIR}/../CyAudioCore")
set(cydaw_dsp_default "${CMAKE_CURRENT_SOURCE_DIR}/../CyDSP")
if(DEFINED ENV{CYDAW_CYAUDIOCORE_SOURCE_DIR})
    set(cydaw_audio_default "$ENV{CYDAW_CYAUDIOCORE_SOURCE_DIR}")
endif()
if(DEFINED ENV{CYDAW_CYDSP_SOURCE_DIR})
    set(cydaw_dsp_default "$ENV{CYDAW_CYDSP_SOURCE_DIR}")
endif()
# CyAudioCore owns the CyDSP dependency. The DAW only supplies source locations;
# it never copies SDKs or silently fetches a different revision.
set(CYDAW_CYAUDIOCORE_SOURCE_DIR "${cydaw_audio_default}" CACHE PATH
    "CyAudioCore checkout providing audio/MIDI, files, sources and plugin hosts")
set(CYDAW_CYDSP_SOURCE_DIR "${cydaw_dsp_default}" CACHE PATH
    "Numerical CyDSP checkout consumed internally by CyAudioCore")
if(NOT TARGET CyAudioCore::CyAudioCore)
    foreach(header IN ITEMS core/DspTypes.h io/AudioDeviceManager.h io/AudioFile.h
            io/AudioMetadata.h io/EditingPcmProxy.h plugins/Plugin.h plugins/PluginWorker.h)
        if(NOT EXISTS "${CYDAW_CYAUDIOCORE_SOURCE_DIR}/include/cyaudio/${header}")
            message(FATAL_ERROR "CyAudioCore is missing ${header}. Set CYDAW_CYAUDIOCORE_SOURCE_DIR; see docs/CyAudioCoreIntegration.md.")
        endif()
    endforeach()
    file(STRINGS "${CYDAW_CYAUDIOCORE_SOURCE_DIR}/include/cyaudio/plugins/Plugin.h"
        cydaw_isolation_api REGEX "struct PluginIsolationOptions")
    if(NOT cydaw_isolation_api)
        message(FATAL_ERROR "CyDaw requires the isolated plugin provider API (CyAudioCore 4e57109 or later); rebuild worker and host together.")
    endif()
    file(STRINGS "${CYDAW_CYAUDIOCORE_SOURCE_DIR}/include/cyaudio/plugins/Plugin.h"
        cydaw_sleep_api REGEX "struct PluginSleepInfo")
    if(NOT cydaw_sleep_api)
        message(FATAL_ERROR "CyDaw requires plugin sleep diagnostics (CyAudioCore f9b0c81 or later); rebuild worker and host together.")
    endif()
    set(CYAUDIOCORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CYAUDIOCORE_COMPILE_CHECKS OFF CACHE BOOL "" FORCE)
    foreach(component IN ITEMS FILES DEVICES SOURCES PLUGINS)
        set(CYAUDIOCORE_BUILD_${component} ON CACHE BOOL "" FORCE)
    endforeach()
    foreach(header IN ITEMS analysis/LevelMeter.h processors/MultiBusGraph.h processors/Dynamics.h)
        if(NOT EXISTS "${CYDAW_CYDSP_SOURCE_DIR}/include/cydsp/${header}")
            message(FATAL_ERROR "CyAudioCore's CyDSP dependency needs the expanded numerical API: missing ${header}")
        endif()
    endforeach()
    set(CYDSP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CYDSP_BUILD_MEASUREMENTS OFF CACHE BOOL "" FORCE)
    set(CYDSP_COMPILE_CHECKS OFF CACHE BOOL "" FORCE)
    set(CYDSP_BUILD_STRETCH ON CACHE BOOL "" FORCE)
    set(CYAUDIOCORE_CYDSP_SOURCE_DIR "${CYDAW_CYDSP_SOURCE_DIR}" CACHE PATH "" FORCE)
    add_subdirectory("${CYDAW_CYAUDIOCORE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/cyaudiocore" EXCLUDE_FROM_ALL)
endif()
foreach(component IN ITEMS Core Files Devices Sources Plugins)
    if(NOT TARGET CyAudioCore::${component})
        message(FATAL_ERROR "Required CyAudioCore target missing: CyAudioCore::${component}")
    endif()
endforeach()
if(NOT TARGET CyDSP::Stretch)
    message(FATAL_ERROR "CyAudioCore requires CyDSP::Stretch for CyDaw playback")
endif()

function(cydaw_copy_audio_notices target destination)
    set(cydaw_notice_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}/stb"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cydaw_notice_root}/third_party/ysfx/thirdparty/stb/LICENSE" "${destination}/stb/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}/miniz"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cydaw_notice_root}/third_party/miniz/LICENSE" "${destination}/miniz/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}/pugixml"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cydaw_notice_root}/third_party/pugixml/LICENSE.md" "${destination}/pugixml/LICENSE.md"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}/nlohmann-json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cydaw_notice_root}/third_party/nlohmann/LICENSE.MIT" "${destination}/nlohmann-json/LICENSE.MIT"
        VERBATIM)
    if(WIN32 AND CYDAW_ENABLE_ASIO)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}/ASIO"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${cydaw_notice_root}/third_party/asio-sdk" "${destination}/ASIO/SDK"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${cydaw_notice_root}/third_party/portaudio-asio" "${destination}/ASIO/PortAudio"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cydaw_notice_root}/third_party/asio-provenance.json" "${destination}/ASIO/asio-provenance.json"
            VERBATIM)
    endif()
    foreach(dependency IN ITEMS CyAudioCore CyDSP)
        if(dependency STREQUAL "CyAudioCore")
            set(source "${CYDAW_CYAUDIOCORE_SOURCE_DIR}")
        else()
            set(source "${CYDAW_CYDSP_SOURCE_DIR}")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}/${dependency}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${source}/LICENSE" "${destination}/${dependency}/LICENSE"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${source}/third_party/manifest.json" "${destination}/${dependency}/manifest.json"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${source}/third_party/licenses" "${destination}/${dependency}/third-party"
            VERBATIM)
    endforeach()
endfunction()

# cmake --install must carry the same notices as the build-tree executables.
foreach(dependency IN ITEMS CyAudioCore CyDSP)
    if(dependency STREQUAL "CyAudioCore")
        set(source "${CYDAW_CYAUDIOCORE_SOURCE_DIR}")
    else()
        set(source "${CYDAW_CYDSP_SOURCE_DIR}")
    endif()
    install(FILES "${source}/LICENSE" "${source}/third_party/manifest.json"
        DESTINATION "Licenses/${dependency}" COMPONENT CyDAWRuntime)
    install(DIRECTORY "${source}/third_party/licenses/"
        DESTINATION "Licenses/${dependency}/third-party" COMPONENT CyDAWRuntime)
endforeach()

install(FILES "${PROJECT_SOURCE_DIR}/third_party/nlohmann/LICENSE.MIT" DESTINATION Licenses/nlohmann-json COMPONENT CyDAWRuntime)

install(FILES "${PROJECT_SOURCE_DIR}/third_party/pugixml/LICENSE.md" DESTINATION Licenses/pugixml COMPONENT CyDAWRuntime)

install(FILES "${PROJECT_SOURCE_DIR}/third_party/miniz/LICENSE" DESTINATION Licenses/miniz COMPONENT CyDAWRuntime)

install(FILES "${PROJECT_SOURCE_DIR}/third_party/ysfx/thirdparty/stb/LICENSE" DESTINATION Licenses/stb COMPONENT CyDAWRuntime)
