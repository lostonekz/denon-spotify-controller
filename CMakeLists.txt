# Denon/Spotify Controller — ESP-IDF project root
#
# Scope of this skeleton: make the architecture in docs/ falsifiable.
# It is NOT product feature work. See docs/ROADMAP.md Phase 0-2.

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(denon-spotify-controller)

# ---------------------------------------------------------------------------
# Architecture gates, run at build time so a violation fails the build rather
# than waiting for CI. Same scripts CI runs; see tools/run_all_checks.py.
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
    add_custom_target(arch_checks ALL
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_LIST_DIR}/tools/run_all_checks.py"
                --root "${CMAKE_CURRENT_LIST_DIR}"
                --skip lvgl_boundary   # needs built objects; run separately post-link
        COMMENT "Running architecture gates (includes, limits, partitions, sdkconfig, keys)"
        VERBATIM)
else()
    message(WARNING "Python3 not found - architecture gates will NOT run at build time")
endif()
