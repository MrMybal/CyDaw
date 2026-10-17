# Native exports: only the format adapters vary; no JUCE in any binary.
if(NOT DEFINED CYDAW_EXPORT_FORMAT)
    set(CYDAW_EXPORT_FORMAT VST3)
endif()
if(CYDAW_EXPORT_FORMAT STREQUAL "VST3")
    include("${CYDAW_SOURCE_ROOT}/src/native_plugin/NativePlugin.cmake")
    return()
endif()

if(NOT CYDAW_EXPORT_FORMAT MATCHES "^(CLAP|LV2)$")
    message(FATAL_ERROR "Unknown export format: ${CYDAW_EXPORT_FORMAT}")
endif()
set(CYDAW_BUILD_DESKTOP OFF CACHE BOOL "" FORCE)
set(CYDAW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CYDAW_ENABLE_LTO OFF CACHE BOOL "" FORCE)
set(CYDAW_ENABLE_JACK OFF CACHE BOOL "" FORCE)
add_subdirectory("${CYDAW_SOURCE_ROOT}" cydaw-core EXCLUDE_FROM_ALL)
include("${CYDAW_SOURCE_ROOT}/src/native_plugin/NativeVisuals.cmake")
include("${CYDAW_SOURCE_ROOT}/src/native_plugin/NativePlatform.cmake")
set(native "${CYDAW_SOURCE_ROOT}/src/native_plugin")
set(imgui "${CYDAW_SOURCE_ROOT}/third_party/imgui")
set(target "CyDAWGeneratedGraphPlugin_${CYDAW_EXPORT_FORMAT}")
if(CYDAW_EXPORT_FORMAT STREQUAL "CLAP")
    set(adapter "${native}/ClapExport.cpp")
else()
    set(adapter "${native}/Lv2Export.cpp" "${native}/Lv2Metadata.cpp")
endif()
add_library(${target} MODULE "${CMAKE_CURRENT_SOURCE_DIR}/generated/Definition.cpp" ${CYDAW_CUSTOM_SOURCES}
    "${native}/NativeProcessor.cpp" "${native}/NativeResources.cpp" "${native}/NativeFonts.cpp" ${adapter} ${native_visual_sources}
    "${imgui}/imgui.cpp" "${imgui}/imgui_draw.cpp" "${imgui}/imgui_tables.cpp" "${imgui}/imgui_widgets.cpp"
   )
target_compile_features(${target} PRIVATE cxx_std_20)
target_include_directories(${target} PRIVATE "${native_visual_dir}" "${native}" "${imgui}" "${CYDAW_SOURCE_ROOT}/src/v2"
    "${CYDAW_SOURCE_ROOT}/third_party/clap/include" "${CYDAW_SOURCE_ROOT}/third_party/lv2/include")
set(uri "urn:cydaw:${CYDAW_VENDOR_ID}:${CYDAW_PRODUCT_ID}")
target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    "CYDAW_PRODUCT=\"${CYDAW_PRODUCT_NAME}\"" "CYDAW_VENDOR=\"${CYDAW_COMPANY_NAME}\"" "CYDAW_VERSION=\"${PROJECT_VERSION}\""
    "CYDAW_PLUGIN_URI=\"${uri}\"")
cydaw_native_platform(${target})
target_link_libraries(${target} PRIVATE CyDAW::Core)
if(CYDAW_EXPORT_FORMAT STREQUAL "CLAP")
    set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "${CYDAW_PRODUCT_NAME}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/CLAP/$<CONFIG>" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/CLAP/$<CONFIG>")
    set(licenseDir "$<TARGET_FILE_DIR:${target}>/${CYDAW_PRODUCT_NAME}.clap.Licenses")
    set(resourceDir "$<TARGET_FILE_DIR:${target}>/${CYDAW_PRODUCT_NAME}.clap.Resources")
else()
    set(bundle "${CMAKE_BINARY_DIR}/LV2/$<CONFIG>/${CYDAW_PRODUCT_NAME}.lv2")
    set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME "cydaw_native" LIBRARY_OUTPUT_DIRECTORY "${bundle}" RUNTIME_OUTPUT_DIRECTORY "${bundle}")
    # MSVC does not append another configuration suffix to directories with generator expressions.
    if(CMAKE_CROSSCOMPILING)
        if(NOT CYDAW_HOST_PYTHON)
            message(FATAL_ERROR "CYDAW_HOST_PYTHON is required to generate LV2 metadata during cross compilation")
        endif()
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/LV2/Release/${CYDAW_PRODUCT_NAME}.lv2")
        execute_process(COMMAND "${CYDAW_HOST_PYTHON}" "${native}/Lv2Metadata.py"
            --definition "${CMAKE_CURRENT_SOURCE_DIR}/generated/Metadata.json" --uri "${uri}"
            --output "${CMAKE_BINARY_DIR}/LV2/Release/${CYDAW_PRODUCT_NAME}.lv2" RESULT_VARIABLE metadata_result)
        if(NOT metadata_result EQUAL 0)
            message(FATAL_ERROR "LV2 metadata generation failed")
        endif()
    else()
        add_executable(CyDAWLv2Metadata "${native}/Lv2MetadataTool.cpp")
        add_dependencies(${target} CyDAWLv2Metadata)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "$<TARGET_FILE:CyDAWLv2Metadata>" "$<TARGET_FILE:${target}>" "${bundle}/plugin.ttl")
    endif()
    set(licenseDir "${bundle}/Licenses")
    set(resourceDir "${bundle}/Resources")
endif()
# The instrument's voice data (a sampler's samples, a sound model), where the
# plug-in looks for it: beside a CLAP file, inside an LV2 bundle. A build
# without any takes away one an earlier export of the same name left.
if(CYDAW_VOICE_ASSETS)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${resourceDir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CYDAW_VOICE_ASSETS}" "${resourceDir}/Voice.cyvoice")
else()
    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E rm -f "${resourceDir}/Voice.cyvoice")
endif()
# Each Replacer's instrument's voice data, by its own name.
foreach(extraVoice IN LISTS CYDAW_EXTRA_VOICE_ASSETS)
    get_filename_component(extraVoiceName "${extraVoice}" NAME)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${resourceDir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${extraVoice}" "${resourceDir}/${extraVoiceName}")
endforeach()
add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${licenseDir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${imgui}/LICENSE.txt" "${licenseDir}/Dear-ImGui.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CYDAW_SOURCE_ROOT}/LICENSE" "${licenseDir}/CyDAW-Engine.txt")
if(CYDAW_EXPORT_FORMAT STREQUAL "CLAP")
    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CYDAW_SOURCE_ROOT}/third_party/clap/LICENSE" "${licenseDir}/CLAP-SDK.txt")
else()
    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CYDAW_SOURCE_ROOT}/third_party/lv2/COPYING" "${licenseDir}/LV2-SDK.txt")
endif()

if(CYDAW_NATIVE_VALIDATOR)
    enable_testing()
    add_executable(CyDAWNativeFormatTest "${CYDAW_SOURCE_ROOT}/tests/NativeFormatTest.cpp")
    target_compile_features(CyDAWNativeFormatTest PRIVATE cxx_std_20)
    target_compile_definitions(CyDAWNativeFormatTest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(CyDAWNativeFormatTest PRIVATE "${CYDAW_SOURCE_ROOT}/third_party/clap/include" "${CYDAW_SOURCE_ROOT}/third_party/lv2/include")
    add_dependencies(CyDAWNativeFormatTest ${target})
    if(CYDAW_EXPORT_FORMAT STREQUAL "CLAP")
        add_test(NAME cydaw_native_clap COMMAND CyDAWNativeFormatTest CLAP "$<TARGET_FILE:${target}>")
    else()
        add_test(NAME cydaw_native_lv2 COMMAND CyDAWNativeFormatTest LV2 "${bundle}")
    endif()
endif()
