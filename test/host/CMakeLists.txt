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

dsc_add_host_test(test_denon_codec_tables
    test_denon_codec_tables.c
    "${REPO_ROOT}/components/svc_denon/denon_codec_tables.c")
target_include_directories(test_denon_codec_tables PRIVATE
    "${REPO_ROOT}/components/svc_denon/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_denon_cmdq
    test_denon_cmdq.c
    "${REPO_ROOT}/components/svc_denon/denon_cmdq.c")
target_include_directories(test_denon_cmdq PRIVATE
    "${REPO_ROOT}/components/svc_denon/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_denon_link_fsm
    test_denon_link_fsm.c
    "${REPO_ROOT}/components/svc_denon/denon_link_fsm.c"
    "${REPO_ROOT}/components/core_fsm/core_fsm.c")
target_include_directories(test_denon_link_fsm PRIVATE
    "${REPO_ROOT}/components/svc_denon/include"
    "${REPO_ROOT}/components/core_fsm/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_denon_intent
    test_denon_intent.c
    "${REPO_ROOT}/components/svc_denon/denon_intent.c"
    "${REPO_ROOT}/components/svc_denon/denon_codec.c"
    "${REPO_ROOT}/components/svc_denon/denon_codec_tables.c")
target_include_directories(test_denon_intent PRIVATE
    "${REPO_ROOT}/components/svc_denon/include"
    "${REPO_ROOT}/components/core_types/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_denon_service_state
    test_denon_service_state.c
    "${REPO_ROOT}/components/svc_denon/denon_service_state.c"
    "${REPO_ROOT}/components/svc_denon/denon_link_fsm.c"
    "${REPO_ROOT}/components/svc_denon/denon_cmdq.c"
    "${REPO_ROOT}/components/core_fsm/core_fsm.c")
target_include_directories(test_denon_service_state PRIVATE
    "${REPO_ROOT}/components/svc_denon/include"
    "${REPO_ROOT}/components/core_types/include"
    "${REPO_ROOT}/components/core_fsm/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_ota_policy
    test_ota_policy.c
    "${REPO_ROOT}/components/svc_ota/ota_policy.c")
target_include_directories(test_ota_policy PRIVATE
    "${REPO_ROOT}/components/svc_ota/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_core_pool
    test_core_pool.c
    "${REPO_ROOT}/components/core_pool/core_pool_block.c"
    "${REPO_ROOT}/components/core_pool/core_pool_slot.c"
    "${REPO_ROOT}/components/core_pool/core_pool_ring.c")
target_include_directories(test_core_pool PRIVATE
    "${REPO_ROOT}/components/core_pool/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_core_fsm
    test_core_fsm.c
    "${REPO_ROOT}/components/core_fsm/core_fsm.c")
target_include_directories(test_core_fsm PRIVATE
    "${REPO_ROOT}/components/core_fsm/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_eventbus
    test_eventbus.c
    fake_lock.c
    "${REPO_ROOT}/components/core_eventbus/core_eventbus.c"
    "${REPO_ROOT}/components/core_pool/core_pool_ring.c")
target_include_directories(test_eventbus PRIVATE
    "${REPO_ROOT}/components/core_eventbus/include"
    "${REPO_ROOT}/components/core_pool/include"
    "${REPO_ROOT}/components/core_types/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_state_store
    test_state_store.c
    fake_lock.c
    fake_time_source.c
    "${REPO_ROOT}/components/core_state/core_state.c")
target_include_directories(test_state_store PRIVATE
    "${REPO_ROOT}/components/core_state/include"
    "${REPO_ROOT}/components/core_types/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_scheduler_jobs
    test_scheduler_jobs.c
    "${REPO_ROOT}/components/svc_scheduler/svc_scheduler_jobs.c")
target_include_directories(test_scheduler_jobs PRIVATE
    "${REPO_ROOT}/components/svc_scheduler/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_logging_redact
    test_logging_redact.c
    "${REPO_ROOT}/components/svc_logging/svc_logging_redact.c")
target_include_directories(test_logging_redact PRIVATE
    "${REPO_ROOT}/components/svc_logging/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_settings_migration
    test_settings_migration.c
    fake_kv_store.c
    "${REPO_ROOT}/components/svc_settings/svc_settings_migration.c")
target_include_directories(test_settings_migration PRIVATE
    "${REPO_ROOT}/components/svc_settings/include"
    "${REPO_ROOT}/components/core_types/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_diagnostics_faultlog
    test_diagnostics_faultlog.c
    fake_kv_store.c
    "${REPO_ROOT}/components/svc_diagnostics/svc_diagnostics_faultlog.c")
target_include_directories(test_diagnostics_faultlog PRIVATE
    "${REPO_ROOT}/components/svc_diagnostics/include"
    "${REPO_ROOT}/components/core_types/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_supervisor_graph
    test_supervisor_graph.c
    "${REPO_ROOT}/components/svc_supervisor/svc_supervisor_graph.c")
target_include_directories(test_supervisor_graph PRIVATE
    "${REPO_ROOT}/components/svc_supervisor/include"
    "${REPO_ROOT}/components/core_types/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_supervisor_restart
    test_supervisor_restart.c
    "${REPO_ROOT}/components/svc_supervisor/svc_supervisor_restart.c")
target_include_directories(test_supervisor_restart PRIVATE
    "${REPO_ROOT}/components/svc_supervisor/include"
    "${REPO_ROOT}/components/core_types/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_supervisor
    test_supervisor.c
    fake_lock.c
    fake_time_source.c
    "${REPO_ROOT}/components/svc_supervisor/svc_supervisor.c"
    "${REPO_ROOT}/components/svc_supervisor/svc_supervisor_graph.c"
    "${REPO_ROOT}/components/svc_supervisor/svc_supervisor_restart.c"
    "${REPO_ROOT}/components/svc_supervisor/svc_supervisor_health.c"
    "${REPO_ROOT}/components/core_eventbus/core_eventbus.c"
    "${REPO_ROOT}/components/core_pool/core_pool_ring.c")
target_include_directories(test_supervisor PRIVATE
    "${REPO_ROOT}/components/svc_supervisor/include"
    "${REPO_ROOT}/components/core_eventbus/include"
    "${REPO_ROOT}/components/core_pool/include"
    "${REPO_ROOT}/components/core_types/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_ota_manifest_vectors
    test_ota_manifest_vectors.c
    "${REPO_ROOT}/components/svc_ota/ota_manifest_parse.c")
target_include_directories(test_ota_manifest_vectors PRIVATE
    "${REPO_ROOT}/components/svc_ota/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_net_wifi_fsm
    test_net_wifi_fsm.c
    "${REPO_ROOT}/components/net_wifi/net_wifi_fsm.c"
    "${REPO_ROOT}/components/core_fsm/core_fsm.c")
target_include_directories(test_net_wifi_fsm PRIVATE
    "${REPO_ROOT}/components/net_wifi/include"
    "${REPO_ROOT}/components/core_fsm/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_net_wifi_backoff
    test_net_wifi_backoff.c
    "${REPO_ROOT}/components/net_wifi/net_wifi_backoff.c")
target_include_directories(test_net_wifi_backoff PRIVATE
    "${REPO_ROOT}/components/net_wifi/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_net_wifi_provisioning_fsm
    test_net_wifi_provisioning_fsm.c
    "${REPO_ROOT}/components/net_wifi/net_wifi_provisioning_fsm.c"
    "${REPO_ROOT}/components/core_fsm/core_fsm.c")
target_include_directories(test_net_wifi_provisioning_fsm PRIVATE
    "${REPO_ROOT}/components/net_wifi/include"
    "${REPO_ROOT}/components/core_fsm/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_net_wifi_credentials
    test_net_wifi_credentials.c
    fake_kv_store.c
    "${REPO_ROOT}/components/net_wifi/net_wifi_credentials.c")
target_include_directories(test_net_wifi_credentials PRIVATE
    "${REPO_ROOT}/components/net_wifi/include"
    "${REPO_ROOT}/components/core_types/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_net_wifi_ap_password
    test_net_wifi_ap_password.c
    "${REPO_ROOT}/components/net_wifi/net_wifi_ap_password.c")
target_include_directories(test_net_wifi_ap_password PRIVATE
    "${REPO_ROOT}/components/net_wifi/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_net_http_retry_policy
    test_net_http_retry_policy.c
    "${REPO_ROOT}/components/net_http/net_http_retry_policy.c")
target_include_directories(test_net_http_retry_policy PRIVATE
    "${REPO_ROOT}/components/net_http/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_supervisor_health
    test_supervisor_health.c
    "${REPO_ROOT}/components/svc_supervisor/svc_supervisor_health.c")
target_include_directories(test_supervisor_health PRIVATE
    "${REPO_ROOT}/components/svc_supervisor/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_ota_manifest_parse
    test_ota_manifest_parse.c
    "${REPO_ROOT}/components/svc_ota/ota_manifest_json.c"
    "${REPO_ROOT}/components/svc_ota/ota_manifest_codec.c")
target_include_directories(test_ota_manifest_parse PRIVATE
    "${REPO_ROOT}/components/svc_ota/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_ota_trust_anchor
    test_ota_trust_anchor.c
    "${REPO_ROOT}/components/svc_ota/ota_trust_anchors.c")
target_include_directories(test_ota_trust_anchor PRIVATE
    "${REPO_ROOT}/components/svc_ota/include"
    "${CMAKE_CURRENT_LIST_DIR}")

dsc_add_host_test(test_ota_fsm
    test_ota_fsm.c
    "${REPO_ROOT}/components/svc_ota/ota_fsm.c"
    "${REPO_ROOT}/components/core_fsm/core_fsm.c")
target_include_directories(test_ota_fsm PRIVATE
    "${REPO_ROOT}/components/svc_ota/include"
    "${REPO_ROOT}/components/core_fsm/include"
    "${CMAKE_CURRENT_LIST_DIR}")
