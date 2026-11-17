# QA-only: JUCE recommends /Ox; normalize to the native product's /O2.
# Applied through CMAKE_PROJECT_TOP_LEVEL_INCLUDES, without editing JUCE.
function(cydaw_normalize_benchmark_optimization)
    if(MSVC AND TARGET juce_recommended_config_flags)
        get_target_property(options juce_recommended_config_flags INTERFACE_COMPILE_OPTIONS)
        string(REPLACE "/Ox" "/O2" options "${options}")
        set_property(TARGET juce_recommended_config_flags PROPERTY INTERFACE_COMPILE_OPTIONS "${options}")
    endif()
endfunction()
cmake_language(DEFER CALL cydaw_normalize_benchmark_optimization)
