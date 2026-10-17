# Shared FFmpeg is a DAW-only dependency, never part of exported plugins.
set(CYDAW_FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH "FFmpeg SDK (include, lib, bin)")
if(NOT EXISTS "${CYDAW_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "FFmpeg SDK missing. Run python tools/setup_ffmpeg.py, or set CYDAW_FFMPEG_ROOT to a shared LGPL SDK.")
endif()
target_include_directories(CyDAW2 SYSTEM PRIVATE "${CYDAW_FFMPEG_ROOT}/include")
foreach(component avformat avcodec avutil swscale swresample)
    find_library(CYDAW_FFMPEG_${component} NAMES ${component} PATHS "${CYDAW_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(CyDAW2 PRIVATE "${CYDAW_FFMPEG_${component}}")
    file(GLOB runtime "${CYDAW_FFMPEG_ROOT}/bin/${component}-*.dll")
    if(NOT runtime)
        message(FATAL_ERROR "FFmpeg ${component} runtime missing in ${CYDAW_FFMPEG_ROOT}/bin")
    endif()
    add_custom_command(TARGET CyDAW2 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${runtime} "$<TARGET_FILE_DIR:CyDAW2>" VERBATIM)
endforeach()
add_custom_command(TARGET CyDAW2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CYDAW_FFMPEG_ROOT}/sources" "$<TARGET_FILE_DIR:CyDAW2>/Licenses/FFmpeg/sources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CYDAW_FFMPEG_ROOT}/LICENSE-LGPL-3.0.txt" "${CYDAW_FFMPEG_ROOT}/COPYING.GPLv3" "${CYDAW_FFMPEG_ROOT}/BUILD-CONFIGURATION.txt" "${CYDAW_FFMPEG_ROOT}/MANIFEST.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/FFMPEG.md" "$<TARGET_FILE_DIR:CyDAW2>/Licenses/FFmpeg"
    VERBATIM)
