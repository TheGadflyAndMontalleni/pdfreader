# Can be included by a standalone plugin; does not link reader internals or PDFium.
set(PDFREADER_PLUGIN_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}")
function(add_pdfreader_plugin target)
    add_library(${target} MODULE ${ARGN})
    set_target_properties(${target} PROPERTIES
        AUTOMOC ON AUTORCC ON PREFIX ""
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/reader-plugins"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/reader-plugins")
    target_include_directories(${target} PRIVATE "${PDFREADER_PLUGIN_SDK_DIR}")
    target_link_libraries(${target} PRIVATE Qt6::Widgets)
endfunction()
