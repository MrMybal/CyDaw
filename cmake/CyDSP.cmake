# Independently maintained dependency; no silent fallback or copied working tree.
set(CYDAW_CYDSP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../CyDSP" CACHE PATH
    "CyDSP checkout with the DAW integration modules")
if(NOT TARGET CyDSP::CyDSP)
    if(NOT EXISTS "${CYDAW_CYDSP_SOURCE_DIR}/include/cydsp/io/AudioDeviceManager.h"
       OR NOT EXISTS "${CYDAW_CYDSP_SOURCE_DIR}/include/cydsp/io/AudioFile.h")
        message(FATAL_ERROR "CyDSP-CORE needs expanded CyDSP sources. Set CYDAW_CYDSP_SOURCE_DIR; see docs/CyDSPIntegration.md.")
    endif()
    foreach(header IN ITEMS io/AudioMetadata.h io/EditingPcmProxy.h plugins/Plugin.h)
        if(NOT EXISTS "${CYDAW_CYDSP_SOURCE_DIR}/include/cydsp/${header}")
            message(FATAL_ERROR "CyDSP is missing ${header}. Update the dependency; tested revision ea2361a. See docs/CyDSPIntegration.md.")
        endif()
    endforeach()
    set(CYDSP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CYDSP_BUILD_MEASUREMENTS OFF CACHE BOOL "" FORCE)
    set(CYDSP_COMPILE_CHECKS OFF CACHE BOOL "" FORCE)
    foreach(component IN ITEMS FILES DEVICES SOURCES STRETCH PLUGINS)
        set(CYDSP_BUILD_${component} ON CACHE BOOL "" FORCE)
    endforeach()
    add_subdirectory("${CYDAW_CYDSP_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/cydsp" EXCLUDE_FROM_ALL)
endif()
foreach(component IN ITEMS CyDSP Files Devices Sources Stretch Plugins)
    if(NOT TARGET CyDSP::${component})
        message(FATAL_ERROR "Required CyDSP target missing: CyDSP::${component}")
    endif()
endforeach()

function(cydaw_copy_cydsp_notices target destination)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CYDAW_CYDSP_SOURCE_DIR}/LICENSE" "${destination}/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CYDAW_CYDSP_SOURCE_DIR}/third_party/manifest.json" "${destination}/manifest.json"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CYDAW_CYDSP_SOURCE_DIR}/third_party/licenses" "${destination}/third-party"
        VERBATIM)
endfunction()
