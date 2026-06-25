cmake_minimum_required(VERSION 3.22)

# ── Build target selection ────────────────────────────────────────────────────
# VCV Rack (local dev):   cmake -B build -DRACK_DIR=~/Documents/Rack-SDK
# MetaModule (CI/hw):     cmake -B build  (no RACK_DIR — uses ARM toolchain)
# ─────────────────────────────────────────────────────────────────────────────

if(DEFINED RACK_DIR)
    # ── VCV Rack build ────────────────────────────────────────────────────────
    cmake_path(ABSOLUTE_PATH RACK_DIR NORMALIZE OUTPUT_VARIABLE RACK_DIR)
    message(STATUS "Building for VCV Rack: ${RACK_DIR}")

    project(NZZL VERSION 0.1 LANGUAGES C CXX)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_SHARED_LIBRARY_PREFIX "")

    add_library(NZZL MODULE)
    target_sources(NZZL PRIVATE src/plugin.cc)
    target_include_directories(NZZL PRIVATE src ${RACK_DIR}/include ${RACK_DIR}/dep/include)

    if(APPLE)
        target_link_options(NZZL PRIVATE
            -undefined dynamic_lookup
            -Wl,-no_fixup_chains
        )
        set_target_properties(NZZL PROPERTIES SUFFIX ".dylib")
    endif()

    # Install into Rack 2 plugins folder so Rack picks it up automatically
    set(RACK_USER_DIR "$ENV{HOME}/Library/Application Support/Rack2")
    install(
        TARGETS NZZL
        DESTINATION "${RACK_USER_DIR}/plugins-mac-arm64/NZZL"
    )
    install(
        FILES plugin.json
        DESTINATION "${RACK_USER_DIR}/plugins-mac-arm64/NZZL"
    )

else()
    # ── MetaModule build ──────────────────────────────────────────────────────
    message(STATUS "Building for MetaModule (ARM cross-compile)")

    include(metamodule-plugin-sdk/plugin.cmake)

    project(NZZL
        VERSION 0.1
        DESCRIPTION "NZZL plugin for MetaModule"
        LANGUAGES C CXX ASM
    )

    add_library(NZZL STATIC)
    target_include_directories(NZZL PRIVATE src)
    target_sources(NZZL PRIVATE src/plugin.cc)

    if("${INSTALL_DIR}" STREQUAL "")
        set(INSTALL_DIR ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins)
    endif()

    create_plugin(
        SOURCE_LIB    NZZL
        PLUGIN_NAME   NZZL
        PLUGIN_JSON   ${CMAKE_CURRENT_LIST_DIR}/plugin.json
        SOURCE_ASSETS ${CMAKE_CURRENT_LIST_DIR}/assets
        DESTINATION   ${INSTALL_DIR}
    )

endif()
