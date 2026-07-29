# Host test harness. Runs on the dev machine, no ESP32 involved.
#
#   cmake -S test/host -B build_host
#   cmake --build build_host
#   ctest --test-dir build_host --output-on-failure
#
# Not "build/host": ESP-IDF owns build/ outright and refuses to reuse a
# directory that isn't purely its own CMake build dir.
#
# Deliberately a plain CMake project, NOT an ESP-IDF one: these tests must be
# runnable by anyone with a C compiler, with no toolchain install. That is what
# keeps them actually being run.

cmake_minimum_required(VERSION 3.16)
project(dsc_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

# --- Unity ----------------------------------------------------------------
# Vendored via FetchContent so a fresh clone needs no manual step. Pinned to an
# exact tag: an unpinned test framework is a test suite that changes underneath
# you (docs/RISKS.md R9, same reasoning as LVGL).
include(FetchContent)
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.6.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(unity)

# --- Warnings are errors on host builds -----------------------------------
# The target build cannot always afford this (third-party components), but the
# host build has no excuse.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror -Wconversion -Wshadow
                        -Wpointer-arith -Wstrict-prototypes)
elseif(MSVC)
    add_compile_options(/W4 /WX)
endif()

# --- Tests ----------------------------------------------------------------
# Each test links ONLY the pure module under test. If a module cannot be
# host-tested without dragging in ESP-IDF, that is a design smell, not a
# reason to skip the test.

function(dsc_add_host_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE unity)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

dsc_add_host_test(test_denon_codec
    test_denon_codec.c
    "${REPO_ROOT}/components/svc_denon/denon_codec.c")
target_include_directories(test_denon_codec PRIVATE
    "${REPO_ROOT}/components/svc_denon/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_ota_policy
    test_ota_policy.c
    "${REPO_ROOT}/components/svc_ota/ota_policy.c")
target_include_directories(test_ota_policy PRIVATE
    "${REPO_ROOT}/components/svc_ota/include"
    "${CMAKE_CURRENT_LIST_DIR}")
