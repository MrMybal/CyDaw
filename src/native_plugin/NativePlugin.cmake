# This file intentionally has no JUCE dependency.

set(CYDAW_BUILD_DESKTOP OFF CACHE BOOL "" FORCE)
set(CYDAW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CYDAW_ENABLE_LTO OFF CACHE BOOL "" FORCE)
set(CYDAW_ENABLE_JACK OFF CACHE BOOL "" FORCE)
add_subdirectory("${CYDAW_SOURCE_ROOT}" cydaw-core EXCLUDE_FROM_ALL)
set(CYDAW_VST3_SDK_DIR "" CACHE PATH "Steinberg VST3 SDK (MIT)")
if(NOT CYDAW_VST3_SDK_DIR)
    if(EXISTS "${CYDAW_SOURCE_ROOT}/build/native-vst3-sdk/CMakeLists.txt")
        set(CYDAW_VST3_SDK_DIR "${CYDAW_SOURCE_ROOT}/build/native-vst3-sdk")
    else()
        include(FetchContent)
        FetchContent_Declare(cydaw_vst3sdk GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git GIT_TAG 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96 GIT_SUBMODULES base cmake pluginterfaces public.sdk)
        FetchContent_GetProperties(cydaw_vst3sdk)
        if(NOT cydaw_vst3sdk_POPULATED)
            FetchContent_Populate(cydaw_vst3sdk)
        endif()
        set(CYDAW_VST3_SDK_DIR "${cydaw_vst3sdk_SOURCE_DIR}")
    endif()
endif()
if(CMAKE_CROSSCOMPILING)
    set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
endif()
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
option(CYDAW_NATIVE_VALIDATOR "Build the official VST3 validator" OFF)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ${CYDAW_NATIVE_VALIDATOR} CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
if(NOT TARGET sdk)
    add_subdirectory("${CYDAW_VST3_SDK_DIR}" vst3-sdk EXCLUDE_FROM_ALL)
endif()
# Zig provides filesystem in libc++; the SDK also adds GCC's legacy
# stdc++fs library to its static targets. Remove that GCC-only dependency.
if(CYDAW_TOOLCHAIN_ROOT)
    # The portable platform adapter strips with Zig at link time. Do not
    # invoke a Linux strip executable on the Windows build host.
    function(smtg_target_strip_symbols target)
    endfunction()
    foreach(sdkTarget sdk sdk_common sdk_hosting base pluginterfaces)
        if(TARGET ${sdkTarget})
            foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
                get_target_property(libraries ${sdkTarget} ${property})
                if(libraries)
                    list(REMOVE_ITEM libraries stdc++fs "$<LINK_ONLY:stdc++fs>")
                    set_property(TARGET ${sdkTarget} PROPERTY ${property} "${libraries}")
                endif()
            endforeach()
        endif()
    endforeach()
endif()
# The SDK's default helper probes the build host with uname. Cross exports
# use the target architecture instead (Windows need not provide Unix tools).
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    function(smtg_get_linux_architecture_name)
        set(LINUX_ARCHITECTURE_NAME "${CMAKE_SYSTEM_PROCESSOR}-linux" PARENT_SCOPE)
    endfunction()
endif()
include("${CYDAW_SOURCE_ROOT}/src/native_plugin/NativeVisuals.cmake")
include("${CYDAW_SOURCE_ROOT}/src/native_plugin/NativePlatform.cmake")
# The SDK is a subdirectory, so its local path variable is not inherited by
# this caller. Set it explicitly so smtg_add_vst3plugin adds the OS entry file
# (ModuleEntry/ModuleExit on Linux, InitDll/ExitDll on Windows).
set(public_sdk_SOURCE_DIR "${CYDAW_VST3_SDK_DIR}/public.sdk")
set(imgui "${CYDAW_SOURCE_ROOT}/third_party/imgui")
set(native "${CYDAW_SOURCE_ROOT}/src/native_plugin")
smtg_add_vst3plugin(CyDAWGeneratedGraphPlugin_VST3 PACKAGE_NAME "${CYDAW_PRODUCT_NAME}" SOURCES_LIST
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/Definition.cpp" ${CYDAW_CUSTOM_SOURCES}
    "${native}/NativePlugin.cpp" "${native}/NativeEditor.cpp" "${native}/NativeProcessor.cpp" "${native}/NativeResources.cpp" "${native}/NativeFonts.cpp" ${native_visual_sources}
    "${CYDAW_VST3_SDK_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp"
    "${imgui}/imgui.cpp" "${imgui}/imgui_draw.cpp" "${imgui}/imgui_tables.cpp" "${imgui}/imgui_widgets.cpp"
   )
target_compile_features(CyDAWGeneratedGraphPlugin_VST3 PRIVATE cxx_std_20)
target_include_directories(CyDAWGeneratedGraphPlugin_VST3 PRIVATE "${native_visual_dir}" "${native}" "${imgui}" "${CYDAW_SOURCE_ROOT}/src/v2")
target_compile_definitions(CyDAWGeneratedGraphPlugin_VST3 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    "CYDAW_PRODUCT=\"${CYDAW_PRODUCT_NAME}\"" "CYDAW_VENDOR=\"${CYDAW_COMPANY_NAME}\"" "CYDAW_VERSION=\"${PROJECT_VERSION}\"" "CYDAW_CATEGORY=\"${CYDAW_CATEGORY}\"" "CYDAW_VENDOR_ID=${CYDAW_VENDOR_ID}" "CYDAW_PRODUCT_ID=${CYDAW_PRODUCT_ID}")
cydaw_native_platform(CyDAWGeneratedGraphPlugin_VST3)
target_link_libraries(CyDAWGeneratedGraphPlugin_VST3 PRIVATE sdk CyDAW::Core)

if(CYDAW_NATIVE_VALIDATOR)
    add_executable(CyDAWNativeViewTest "${CYDAW_SOURCE_ROOT}/tests/NativeViewTest.cpp"
        "${CYDAW_VST3_SDK_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp"
        "${CYDAW_VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_win32.cpp")
    target_compile_features(CyDAWNativeViewTest PRIVATE cxx_std_17)
    target_compile_definitions(CyDAWNativeViewTest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(CyDAWNativeViewTest PRIVATE sdk_hosting ole32)
endif()
add_custom_command(TARGET CyDAWGeneratedGraphPlugin_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CyDAWGeneratedGraphPlugin_VST3>/../Resources/Licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CYDAW_VST3_SDK_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:CyDAWGeneratedGraphPlugin_VST3>/../Resources/Licenses/VST3-SDK.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${imgui}/LICENSE.txt" "$<TARGET_FILE_DIR:CyDAWGeneratedGraphPlugin_VST3>/../Resources/Licenses/Dear-ImGui.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CYDAW_SOURCE_ROOT}/LICENSE" "$<TARGET_FILE_DIR:CyDAWGeneratedGraphPlugin_VST3>/../Resources/Licenses/CyDAW-Engine.txt")
# The instrument's voice data (a sampler's samples, a sound model), in the
# bundle's Resources where the plug-in looks for it.
if(CYDAW_VOICE_ASSETS)
    add_custom_command(TARGET CyDAWGeneratedGraphPlugin_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CYDAW_VOICE_ASSETS}" "$<TARGET_FILE_DIR:CyDAWGeneratedGraphPlugin_VST3>/../Resources/Voice.cyvoice")
else()
    add_custom_command(TARGET CyDAWGeneratedGraphPlugin_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:CyDAWGeneratedGraphPlugin_VST3>/../Resources/Voice.cyvoice")
endif()
