cmake_minimum_required(VERSION 3.22)
include(metamodule-plugin-sdk/plugin.cmake)

project(NZZL
    VERSION 0.1
    DESCRIPTION "NZZL plugin for MetaModule"
    LANGUAGES C CXX ASM
)

add_library(NZZL STATIC)
target_include_directories(NZZL PRIVATE src)

target_sources(NZZL PRIVATE
    src/plugin.cc
)

create_plugin(
    SOURCE_LIB    NZZL
    PLUGIN_NAME   NZZL
    PLUGIN_JSON   ${CMAKE_CURRENT_LIST_DIR}/plugin.json
    SOURCE_ASSETS ${CMAKE_CURRENT_LIST_DIR}/assets
    DESTINATION   ${INSTALL_DIR}
)
