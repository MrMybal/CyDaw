include(FetchContent)

set(CYDAW_CYMCPSDK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../CyMCPSDK" CACHE PATH
    "Local CyMCPSDK source checkout used in preference to the published pin")
set(cydaw_mcp_revision e562c731a146d599e3404718ea8414bb5b96b66d)
foreach(component IN ITEMS HTTP PLATFORM MCP AI CHAT_UI)
    set(CYMCP_BUILD_${component} ON)
endforeach()
foreach(component IN ITEMS LOCAL EXAMPLES TESTS BENCHMARKS CHAT_APP)
    set(CYMCP_BUILD_${component} OFF)
endforeach()
if(EXISTS "${CYDAW_CYMCPSDK_SOURCE_DIR}/CMakeLists.txt")
    file(REAL_PATH "${CYDAW_CYMCPSDK_SOURCE_DIR}" cydaw_mcp_source)
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${cydaw_mcp_source}"
            -C "${cydaw_mcp_source}" rev-parse HEAD
            OUTPUT_VARIABLE cydaw_mcp_revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    else()
        set(cydaw_mcp_revision "")
    endif()
    if(NOT cydaw_mcp_revision)
        set(cydaw_mcp_revision "local source snapshot")
    endif()
    add_subdirectory("${cydaw_mcp_source}" "${CMAKE_CURRENT_BINARY_DIR}/cymcp" EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(CyMCPSDK GIT_REPOSITORY https://github.com/MrMybal/CyMCPSDK.git
        GIT_TAG "${cydaw_mcp_revision}" GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(CyMCPSDK)
    set(cydaw_mcp_source "${cymcpsdk_SOURCE_DIR}")
endif()
message(STATUS "CyMCPSDK source: ${cydaw_mcp_source} (${cydaw_mcp_revision})")

function(cydaw_copy_cymcp_notices target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Licenses/CyMCPSDK"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${cydaw_mcp_source}/LICENSE" "${cydaw_mcp_source}/THIRD_PARTY_NOTICES.md"
            "$<TARGET_FILE_DIR:${target}>/Licenses/CyMCPSDK" VERBATIM)
endfunction()
