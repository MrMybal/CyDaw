# Application-side GPL extension; never modifies the standalone MIT repositories.
if(NOT WIN32)
    message(FATAL_ERROR "The CyDaw ASIO extension requires Windows")
endif()
if(NOT TARGET CyAudioCorePortAudio)
    message(FATAL_ERROR "ASIO requires CyAudioCore's PortAudio runtime")
endif()
set(cydaw_asio_sdk "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio-sdk")
set(cydaw_pa_asio "${CMAKE_CURRENT_SOURCE_DIR}/third_party/portaudio-asio")
target_sources(CyAudioCorePortAudio PRIVATE
    "${cydaw_pa_asio}/pa_asio.cpp"
    "${cydaw_pa_asio}/iasiothiscallresolver.cpp"
    "${cydaw_asio_sdk}/common/asio.cpp"
    "${cydaw_asio_sdk}/host/asiodrivers.cpp"
    "${cydaw_asio_sdk}/host/pc/asiolist.cpp")
target_include_directories(CyAudioCorePortAudio PRIVATE "${cydaw_pa_asio}"
    "${cydaw_asio_sdk}/common" "${cydaw_asio_sdk}/host" "${cydaw_asio_sdk}/host/pc")
target_compile_definitions(CyAudioCorePortAudio PRIVATE PA_USE_ASIO=1)
target_link_libraries(CyAudioCorePortAudio PRIVATE advapi32 user32)
add_library(cydaw_asio STATIC src/platform/AsioDriver.cpp)
target_compile_features(cydaw_asio PUBLIC cxx_std_20)
target_include_directories(cydaw_asio PUBLIC include PRIVATE src/platform "${cydaw_pa_asio}"
    "${CYDAW_CYAUDIOCORE_SOURCE_DIR}/third_party/portaudio/include")
target_link_libraries(cydaw_asio PUBLIC CyAudioCore::Devices PRIVATE CyAudioCorePortAudio)
if(MSVC)
    target_compile_options(cydaw_asio PRIVATE /W4 /permissive- /utf-8)
endif()
install(DIRECTORY "${cydaw_asio_sdk}/" DESTINATION Licenses/ASIO/SDK COMPONENT CyDAWRuntime)
install(FILES third_party/asio-provenance.json DESTINATION Licenses/ASIO COMPONENT CyDAWRuntime)

install(DIRECTORY "${cydaw_pa_asio}/" DESTINATION Licenses/ASIO/PortAudio COMPONENT CyDAWRuntime)
