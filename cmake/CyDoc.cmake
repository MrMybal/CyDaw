# CyDoc is required only at build time. Ship Documentation, never .CyDocBuild.
function(cydaw_add_documentation target)
    find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)
    find_program(CYDAW_DOC_NODE NAMES node REQUIRED)
    find_program(CYDAW_DOC_NPM NAMES npm.cmd npm REQUIRED)
    file(GLOB_RECURSE cydaw_doc_inputs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/.CyDocBuild/manual/*.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/.CyDocBuild/api/*.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/.CyDocBuild/assets/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/.CyDocBuild/tools/*.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/.CyDocBuild/tools/*.mjs"
        "${CMAKE_CURRENT_SOURCE_DIR}/.CyDocBuild/vendor/*")
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/documentation.stamp"
            "${CMAKE_BINARY_DIR}/Documentation/Documentation.html"
            "${CMAKE_BINARY_DIR}/Documentation/DocData/integrity.json"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/.CyDocBuild/tools/build.py"
            --out "${CMAKE_BINARY_DIR}/Documentation" --node "${CYDAW_DOC_NODE}" --npm "${CYDAW_DOC_NPM}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/documentation.stamp"
        DEPENDS ${cydaw_doc_inputs}
            "${CMAKE_CURRENT_SOURCE_DIR}/.CyDocBuild/cydoc.config.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/cydaw/control/ControlPluginABI.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/v2/CustomInterfaceABI.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/cydaw/engine/AudioProcessor.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        VERBATIM)
    add_custom_target(cydaw_documentation DEPENDS "${CMAKE_BINARY_DIR}/documentation.stamp"
        "${CMAKE_BINARY_DIR}/Documentation/Documentation.html" "${CMAKE_BINARY_DIR}/Documentation/DocData/integrity.json")
    add_custom_target(cydaw_help_resources
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/.CyDocBuild/tools/install.py"
            "${CMAKE_BINARY_DIR}/Documentation" "$<TARGET_FILE_DIR:${target}>/Documentation"
        DEPENDS cydaw_documentation VERBATIM)
    add_dependencies(${target} cydaw_help_resources)
endfunction()
