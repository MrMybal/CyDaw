# CyDoc generation is a build-time dependency; the reader needs only file://.
function(cydaw_add_documentation target)
    find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)
    find_program(CYDAW_DOC_NODE NAMES node REQUIRED)
    find_program(CYDAW_DOC_NPM NAMES npm.cmd npm REQUIRED)
    file(GLOB_RECURSE cydaw_doc_inputs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/*.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/manual/*.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/manual/assets/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/docs/*.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/docs/*.mjs"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/docs/*.css"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/docs/*.html")
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/help.stamp" "${CMAKE_BINARY_DIR}/help/index.html" "${CMAKE_BINARY_DIR}/help/integrity.json"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/docs/build.py"
            --out "${CMAKE_BINARY_DIR}/help" --node "${CYDAW_DOC_NODE}" --npm "${CYDAW_DOC_NPM}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/docs/validate.py" --root "${CMAKE_BINARY_DIR}/help"
        COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/help.stamp"
        DEPENDS ${cydaw_doc_inputs}
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cydoc.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cydoc-0.1.0-774f2a7e.tgz"
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/cydoc.inventory.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${CMAKE_CURRENT_SOURCE_DIR}/content/factory/LICENCE.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/os/README.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/mcp/README.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/mcp/cydaw_mcp_server.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/cydaw/control/ControlPluginABI.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/v2/CustomInterfaceABI.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/cydaw/engine/AudioProcessor.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/cydaw/engine/AudioBuffer.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/cydaw/engine/Transport.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        VERBATIM)
    add_custom_target(cydaw_documentation DEPENDS "${CMAKE_BINARY_DIR}/help.stamp" "${CMAKE_BINARY_DIR}/help/index.html" "${CMAKE_BINARY_DIR}/help/integrity.json")
    add_custom_target(cydaw_help_resources
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/docs/install.py"
            "${CMAKE_BINARY_DIR}/help" "$<TARGET_FILE_DIR:${target}>/help"
        DEPENDS cydaw_documentation VERBATIM)
    add_dependencies(${target} cydaw_help_resources)
endfunction()
