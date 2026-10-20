cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PDFReaderDeepSeekTranslate VERSION 1.0.0 LANGUAGES CXX)
    include("${CMAKE_CURRENT_LIST_DIR}/../../sdk/PdfReaderPlugin.cmake")
endif()
if(NOT WIN32)
    message(FATAL_ERROR "This plugin currently targets Windows x64, matching PDF Reader.")
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network)

# Existing reader releases have QtNetwork but no TLS backend. Embed the matching
# Windows backend so installing the single plugin DLL is sufficient.
get_target_property(_qt_network_location Qt6::Network IMPORTED_LOCATION_RELEASE)
get_filename_component(_qt_bin "${_qt_network_location}" DIRECTORY)
set(PDFREADER_SCHANNEL_DLL "${_qt_bin}/../plugins/tls/qschannelbackend.dll" CACHE FILEPATH "Matching Qt Schannel backend")
if(NOT EXISTS "${PDFREADER_SCHANNEL_DLL}")
    message(FATAL_ERROR "Set PDFREADER_SCHANNEL_DLL to Qt 6.8.3's qschannelbackend.dll")
endif()
file(TO_CMAKE_PATH "${PDFREADER_SCHANNEL_DLL}" _schannel_path)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/translation_tls.qrc"
    "<RCC><qresource prefix=\"/deepseek-translate\"><file alias=\"qschannelbackend.dll\">${_schannel_path}</file></qresource></RCC>")
set(_tls_qrc "${CMAKE_CURRENT_BINARY_DIR}/translation_tls.qrc")

add_library(deepseek_translation_core STATIC
    translation_client.cpp credentials.cpp tls_runtime.cpp translation_controller.cpp)
set_target_properties(deepseek_translation_core PROPERTIES AUTOMOC ON CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(deepseek_translation_core PUBLIC "${CMAKE_CURRENT_LIST_DIR}" "${PDFREADER_PLUGIN_SDK_DIR}")
target_link_libraries(deepseek_translation_core PUBLIC Qt6::Widgets Qt6::Network crypt32)

add_pdfreader_plugin(deepseek_translate deepseek_plugin.cpp deepseek_translate.json "${_tls_qrc}")
set_target_properties(deepseek_translate PROPERTIES OUTPUT_NAME "DeepSeekTranslate")
target_link_libraries(deepseek_translate PRIVATE deepseek_translation_core)

option(PDFREADER_TRANSLATION_TESTS "Build standalone translation plugin tests" ON)
if(PDFREADER_TRANSLATION_TESTS)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_executable(deepseek_translation_tests tests/translation_tests.cpp "${_tls_qrc}")
    set_target_properties(deepseek_translation_tests PROPERTIES AUTOMOC ON AUTORCC ON CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(deepseek_translation_tests PRIVATE deepseek_translation_core Qt6::Test)
endif()
