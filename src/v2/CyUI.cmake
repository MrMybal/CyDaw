# UI-only dependency: the DSP core and plugin workers never link CyUI.
# Native exports select only Core/Audio/AudioViews, without authoring/code tools.
include(FetchContent)
set(CYUI_IMGUI_TARGET cydaw_imgui)
set(CYUI_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(CYUI_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
set(CYUI_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(CYUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CYUI_BUILD_WEB_CEF OFF CACHE BOOL "" FORCE)
if(NOT DEFINED CYDAW_UI_MODULES)
    set(CYDAW_UI_MODULES "Audio;AudioViews;Nodes;CodeEditor;Images;Inspector")
endif()
if(CYDAW_ENABLE_ASSISTANT AND CYDAW_BUILD_DESKTOP)
    list(APPEND CYDAW_UI_MODULES DocumentViews)
    list(REMOVE_DUPLICATES CYDAW_UI_MODULES)
endif()
set(CYUI_MODULES "${CYDAW_UI_MODULES}")
FetchContent_Declare(CyUI
    GIT_REPOSITORY https://github.com/MrMybal/CyUI.git
    GIT_TAG 6ef01cfcdb197f3a89d64618ce5ef8818c704728
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(CyUI)
# Check developer-provided source overrides as well as the published pin.
file(READ "${cyui_SOURCE_DIR}/include/cyui/modules/Plots.h" cydaw_cyui_plots_api)
file(READ "${cyui_SOURCE_DIR}/include/cyui/modules/AudioViews.h" cydaw_cyui_audio_views_api)
if(NOT cydaw_cyui_plots_api MATCHES "drawCurveFill" OR
   NOT cydaw_cyui_audio_views_api MATCHES "struct EnvelopeEditState")
    message(FATAL_ERROR "CyDaw requires CyUI curve fill/envelope APIs. Select the updated checkout with -DFETCHCONTENT_SOURCE_DIR_CYUI=<path-to-CyUI>; see docs/CYUI_MIGRATION.md.")
endif()

# Runtime notices are copied by each UI target's packaging step.
function(cydaw_copy_cyui_notices target)
    set(notices LICENSE THIRD_PARTY_NOTICES.md)
    if(TARGET cyui_codeeditor)
        list(APPEND notices third_party/texteditor/LICENSE third_party/lexilla/License.txt
                    third_party/scintilla/License.txt third_party/SciTE-Properties-License.txt)
    endif()
    if(TARGET CyUI::DocumentViews)
        list(APPEND notices third_party/md4c/LICENSE.md)
    endif()
    foreach(notice IN LISTS notices)
        get_filename_component(folder "${notice}" DIRECTORY)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Licenses/CyUI/${folder}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cyui_SOURCE_DIR}/${notice}"
                    "$<TARGET_FILE_DIR:${target}>/Licenses/CyUI/${notice}" VERBATIM)
    endforeach()
endfunction()
