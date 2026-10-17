foreach(pair IN ITEMS "probe|0" "failing|1" "throwing|2" "incompatible|0")
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 name)
    list(GET parts 1 mode)
    add_pdfreader_plugin(plugin_fixture_${name} probe.cpp "${name}.json")
    target_compile_definitions(plugin_fixture_${name} PRIVATE
        FIXTURE_METADATA="${name}.json" FIXTURE_MODE=${mode})
    set_target_properties(plugin_fixture_${name} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin-fixtures"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin-fixtures")
endforeach()
add_pdfreader_plugin(plugin_fixture_wrong_interface wrong_interface.cpp wrong_interface.json)
set_target_properties(plugin_fixture_wrong_interface PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin-fixtures"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin-fixtures")
