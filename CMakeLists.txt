cmake_minimum_required(VERSION 3.20)
project(MZMRecomp VERSION 0.4.0 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
endif()
include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GBARECOMP_ROOT ""
    CACHE PATH "Path to the pinned gbarecomp checkout")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui"
    CACHE PATH "Path to the pinned recomp-ui checkout")
option(MZM_RECOMP_UI "Build the recomp-ui pre-boot launcher" ON)
# Human-readable release identity shown in the launcher About page and the log.
set(MZM_RELEASE_LABEL "Beta 4 (dev)" CACHE STRING "Release label shown to users")
set(MZM_RELEASE_CHANNEL "beta" CACHE STRING "Release channel: development | beta | rc | stable")
# Xbox / UWP target architecture definition
option(MZM_PLATFORM_UWP "Target Universal Windows Platform (Xbox Series X|S / UWP x64)" OFF)
if(MZM_PLATFORM_UWP)
    add_compile_definitions(MZM_PLATFORM_UWP=1)
endif()
# NES-PERF-1: opt-in timing/counter instrumentation for the NES harness only.
option(MZM_PERF_PROFILE "Instrument mzm_nes_behavior_test (profiling builds only)" OFF)
option(MZM_PERF_WRAP_TICK "With MZM_PERF_PROFILE: also count guest instructions (adds a call per instruction)" OFF)

if(MZM_PERF_PROFILE)
    add_compile_definitions(MZM_PERF_PROFILE=1)
    if(MZM_PERF_WRAP_TICK)
        add_compile_definitions(MZM_PERF_WRAP_TICK=1)
    endif()
endif()

if(NOT GBARECOMP_ROOT)
    message(FATAL_ERROR
        "Set -DGBARECOMP_ROOT=<path-to-gbarecomp>. "
        "MZM-Recomp does not vendor the framework.")
endif()

if(NOT EXISTS "${GBARECOMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "GBARECOMP_ROOT is not a gbarecomp checkout:\n  ${GBARECOMP_ROOT}")
endif()

# The source path is supplied by the builder; the revision is immutable.
set(MZM_GBARECOMP_PIN "266f82f556fe995ece1bfc558cfe59496b0ef31c"
    CACHE STRING "Exact GBARecomp revision for this build")
execute_process(COMMAND git -C "${GBARECOMP_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE MZM_GBARECOMP_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE MZM_GBARECOMP_GIT_RESULT)
if(NOT MZM_GBARECOMP_GIT_RESULT EQUAL 0 OR
   NOT MZM_GBARECOMP_HEAD STREQUAL MZM_GBARECOMP_PIN)
    message(FATAL_ERROR
        "GBARecomp must be at ${MZM_GBARECOMP_PIN}; found "
        "${MZM_GBARECOMP_HEAD} in ${GBARECOMP_ROOT}")
endif()

# The framework's in-game runtime menu (ESC) needs the recomp-ui shared model.
# Point it at the same pinned checkout the launcher uses; without it the
# framework falls back to its historical "ESC quits" behaviour and the host
# display enhancements are unavailable.
set(GBARECOMP_RUNTIME_UI_ROOT "${RECOMP_UI_ROOT}"
    CACHE PATH "recomp-ui checkout for the framework's in-game runtime menu")
# No silent fallback: a build that asks for the launcher (the default) must have
# recomp-ui, otherwise the result would lack the launcher and the Enhancements
# menu without the builder noticing. Pass -DMZM_RECOMP_UI=OFF for a CLI-only host.
if(MZM_RECOMP_UI AND NOT EXISTS "${GBARECOMP_RUNTIME_UI_ROOT}/src/recomp_runtime_ui.h")
    message(FATAL_ERROR
        "recomp-ui not found at ${GBARECOMP_RUNTIME_UI_ROOT}. The launcher and "
        "the in-game Enhancements menu need it. Run: "
        "git submodule update --init --recursive recomp-ui "
        "(or pass -DMZM_RECOMP_UI=OFF to build a CLI-only host).")
endif()

add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)

# GBARecomp bakes its absolute source directory into the runtime for the
# self-heal overlay compiler, which strict-static builds never use. Do not ship
# the builder's filesystem layout inside the executable.
get_target_property(_mzm_rt_defs gbarecomp_runtime COMPILE_DEFINITIONS)
if(_mzm_rt_defs)
    list(TRANSFORM _mzm_rt_defs REPLACE "^GBARECOMP_SRC_DIR=.*" "GBARECOMP_SRC_DIR=\"\"")
    set_property(TARGET gbarecomp_runtime PROPERTY COMPILE_DEFINITIONS "${_mzm_rt_defs}")
endif()

file(GLOB GAME_GENERATED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.cpp")

if(NOT GAME_GENERATED_SOURCES)
    message(FATAL_ERROR
        "No generated MZM translation units found under generated/. "
        "Run scripts/generate-m1.sh first.")
endif()

add_library(mzm_generated_objects OBJECT ${GAME_GENERATED_SOURCES})
target_include_directories(mzm_generated_objects PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    "${GBARECOMP_ROOT}/src/armv4t"
    "${GBARECOMP_ROOT}/src/gba"
    "${GBARECOMP_ROOT}/src/runtime")

add_executable(MZMRecomp
    src/main.cpp
    src/mzm_log.cpp
    src/mzm_diagnostics.cpp
    src/mzm_ram_dispatch.cpp
    src/mzm_milestone_probe.cpp
    $<TARGET_OBJECTS:mzm_generated_objects>
)

# Generated guest translation units are intentionally numerous and large.
# Keep their optimization/debug-info cost bounded during bring-up.
set(GAME_RECOMPILED_SOURCES ${GAME_GENERATED_SOURCES})
list(FILTER GAME_RECOMPILED_SOURCES INCLUDE REGEX
    "/recompiled_[0-9]+\\.cpp$")
if(GAME_RECOMPILED_SOURCES)
    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(_mzm_guest_options /O1)
    else()
        set(_mzm_guest_options -O1 -g0)
    endif()
    set_source_files_properties(${GAME_RECOMPILED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${_mzm_guest_options}")
endif()

target_include_directories(MZMRecomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    "${GBARECOMP_ROOT}/src/armv4t"
    "${GBARECOMP_ROOT}/src/gba"
    "${GBARECOMP_ROOT}/src/runtime"
    "${GBARECOMP_ROOT}/src/debug"
)

if(APPLE OR MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # Apple and MSVC-style linkers resolve archive cycles without GNU groups.
    target_link_libraries(MZMRecomp PRIVATE
        gbarecomp_runtime gbarecomp_debug gbarecomp_gba gbarecomp_armv4t
        gbarecomp_runtime gbarecomp_debug gbarecomp_gba gbarecomp_armv4t)
else()
    # GNU ld and GNU-style MinGW/Clang linkers need an archive group.
    target_link_libraries(MZMRecomp PRIVATE
        "-Wl,--start-group"
        gbarecomp_runtime gbarecomp_debug gbarecomp_gba gbarecomp_armv4t
        "-Wl,--end-group")
endif()

if(WIN32 AND NOT MZM_PLATFORM_UWP)
    set(MZM_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/mzm-recompiled.ico")
    configure_file(assets/windows/mzm-recompiled.rc
        "${CMAKE_CURRENT_BINARY_DIR}/mzm-recompiled.rc" @ONLY)
    target_sources(MZMRecomp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/mzm-recompiled.rc")
    gbarecomp_target_link_host_stack(MZMRecomp PRIVATE)
endif()

execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE MZM_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE MZM_GIT_RESULT)
if(NOT MZM_GIT_RESULT EQUAL 0)
    set(MZM_GIT_SHA "unknown")
else()
    execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" diff --quiet HEAD
        RESULT_VARIABLE MZM_GIT_DIRTY ERROR_QUIET)
    if(NOT MZM_GIT_DIRTY EQUAL 0)
        set(MZM_GIT_SHA "${MZM_GIT_SHA}-dirty")
    endif()
endif()
execute_process(COMMAND git -C "${RECOMP_UI_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE MZM_RECOMP_UI_SHA OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE MZM_RECOMP_UI_GIT_RESULT)
if(NOT MZM_RECOMP_UI_GIT_RESULT EQUAL 0)
    set(MZM_RECOMP_UI_SHA "unknown")
endif()

target_compile_definitions(MZMRecomp PRIVATE
    GBARECOMP_WINDOW_TITLE="MZM-Recomp"
    MZM_VERSION="${PROJECT_VERSION}"
    MZM_BUILD_SHA="${MZM_GIT_SHA}"
    MZM_GBARECOMP_SHA="${MZM_GBARECOMP_HEAD}"
    MZM_RELEASE_LABEL="${MZM_RELEASE_LABEL}"
    MZM_RECOMP_UI_SHA="${MZM_RECOMP_UI_SHA}"
    MZM_RELEASE_CHANNEL="${MZM_RELEASE_CHANNEL}"
    MZM_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    MZM_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)

# The state tests use no game data and do not require generated code to run.
if(BUILD_TESTING AND EXISTS "${RECOMP_UI_ROOT}/src/common/sha1.c")
    add_executable(mzm_launcher_state_test
        tests/launcher_state_test.cpp src/launcher_state.cpp
        "${RECOMP_UI_ROOT}/src/common/sha1.c")
    target_include_directories(mzm_launcher_state_test PRIVATE
        src "${RECOMP_UI_ROOT}/src/common")
    add_test(NAME mzm-launcher-state COMMAND mzm_launcher_state_test)
endif()

if(BUILD_TESTING)
    # Host display settings (presentation only). Header-only logic from the
    # pinned framework; needs no window, ROM or generated code.
    add_executable(mzm_display_settings_test tests/enhancements/display_settings_test.cpp)
    target_include_directories(mzm_display_settings_test PRIVATE
        "${GBARECOMP_ROOT}/src/runtime")
    add_test(NAME mzm-display-settings COMMAND mzm_display_settings_test)

    # Support/report-problem text generation (pure: no window, ROM or generated code).
    add_executable(mzm_diagnostics_test tests/diagnostics_test.cpp src/mzm_diagnostics.cpp)
    target_include_directories(mzm_diagnostics_test PRIVATE src "${GBARECOMP_ROOT}/src/runtime")
    add_test(NAME mzm-diagnostics COMMAND mzm_diagnostics_test)

    add_executable(mzm_haze_resolver_test tests/m4/haze_resolver_test.cpp)
    target_include_directories(mzm_haze_resolver_test PRIVATE src)
    add_test(NAME mzm-haze-resolver COMMAND mzm_haze_resolver_test)

    add_executable(mzm_chozodia_resolver_test tests/m4/chozodia_resolver_test.cpp)
    target_include_directories(mzm_chozodia_resolver_test PRIVATE src)
    add_test(NAME mzm-chozodia-resolver COMMAND mzm_chozodia_resolver_test)

    set(MZM_TEST_ROM "" CACHE FILEPATH "Optional local USA ROM for native RAM tests")
    if(UNIX AND MZM_TEST_ROM)
        set(MZM_TEST_BIOS "" CACHE FILEPATH "Optional local BIOS for NES loader frontier test")
        if(MZM_TEST_BIOS)
            add_executable(mzm_nes_loader_frontier_test
                tests/m4/nes_loader_frontier_test.cpp
                $<TARGET_OBJECTS:mzm_generated_objects>)
            target_include_directories(mzm_nes_loader_frontier_test PRIVATE
                src generated
                "${GBARECOMP_ROOT}/src/armv4t"
                "${GBARECOMP_ROOT}/src/gba"
                "${GBARECOMP_ROOT}/src/runtime")
            target_link_libraries(mzm_nes_loader_frontier_test PRIVATE
                "-Wl,--start-group" gbarecomp_runtime gbarecomp_debug
                gbarecomp_gba gbarecomp_armv4t "-Wl,--end-group")
            add_test(NAME mzm-nes-loader-frontier
                COMMAND mzm_nes_loader_frontier_test "${MZM_TEST_ROM}"
                    "${MZM_TEST_BIOS}" "${CMAKE_CURRENT_BINARY_DIR}/nes1a")

            add_executable(mzm_nes_payload_frontier_test
                tests/m4/nes_payload_frontier_test.cpp
                src/mzm_ram_dispatch.cpp
                src/mzm_milestone_probe.cpp
                $<TARGET_OBJECTS:mzm_generated_objects>)
            target_include_directories(mzm_nes_payload_frontier_test PRIVATE
                src generated
                "${GBARECOMP_ROOT}/src/armv4t"
                "${GBARECOMP_ROOT}/src/gba"
                "${GBARECOMP_ROOT}/src/runtime")
            target_link_libraries(mzm_nes_payload_frontier_test PRIVATE
                "-Wl,--start-group" gbarecomp_runtime gbarecomp_debug
                gbarecomp_gba gbarecomp_armv4t "-Wl,--end-group")
            add_test(NAME mzm-nes-payload-frontier
                COMMAND mzm_nes_payload_frontier_test "${MZM_TEST_ROM}"
                    "${MZM_TEST_BIOS}")

            add_executable(mzm_nes_emulator_frontier_test
                tests/m4/nes_emulator_frontier_test.cpp
                src/mzm_ram_dispatch.cpp
                src/mzm_milestone_probe.cpp
                $<TARGET_OBJECTS:mzm_generated_objects>)
            target_include_directories(mzm_nes_emulator_frontier_test PRIVATE
                src generated
                "${GBARECOMP_ROOT}/src/armv4t"
                "${GBARECOMP_ROOT}/src/gba"
                "${GBARECOMP_ROOT}/src/runtime")
            target_link_libraries(mzm_nes_emulator_frontier_test PRIVATE
                "-Wl,--start-group" gbarecomp_runtime gbarecomp_debug
                gbarecomp_gba gbarecomp_armv4t "-Wl,--end-group")
            target_compile_definitions(mzm_nes_emulator_frontier_test PRIVATE
                MZM_NES_EMULATOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.local/nes-emulator")
            add_test(NAME mzm-nes-emulator-frontier
                COMMAND mzm_nes_emulator_frontier_test "${MZM_TEST_ROM}"
                    "${MZM_TEST_BIOS}")

            add_executable(mzm_nes_scope_policy_test
                tests/m4/nes_scope_policy_test.cpp
                src/mzm_ram_dispatch.cpp
                src/mzm_milestone_probe.cpp
                $<TARGET_OBJECTS:mzm_generated_objects>)
            target_include_directories(mzm_nes_scope_policy_test PRIVATE
                src generated
                "${GBARECOMP_ROOT}/src/armv4t"
                "${GBARECOMP_ROOT}/src/gba"
                "${GBARECOMP_ROOT}/src/runtime")
            target_link_libraries(mzm_nes_scope_policy_test PRIVATE
                "-Wl,--start-group" gbarecomp_runtime gbarecomp_debug
                gbarecomp_gba gbarecomp_armv4t "-Wl,--end-group")
            target_compile_definitions(mzm_nes_scope_policy_test PRIVATE
                MZM_NES_EMULATOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.local/nes-emulator")
            add_test(NAME mzm-nes-scope-policy COMMAND mzm_nes_scope_policy_test)

            add_executable(mzm_nes_stack_helper_test
                tests/m4/nes_stack_helper_test.cpp
                src/mzm_ram_dispatch.cpp
                src/mzm_milestone_probe.cpp
                $<TARGET_OBJECTS:mzm_generated_objects>)
            target_include_directories(mzm_nes_stack_helper_test PRIVATE
                src generated
                "${GBARECOMP_ROOT}/src/armv4t"
                "${GBARECOMP_ROOT}/src/gba"
                "${GBARECOMP_ROOT}/src/runtime")
            target_link_libraries(mzm_nes_stack_helper_test PRIVATE
                "-Wl,--start-group" gbarecomp_runtime gbarecomp_debug
                gbarecomp_gba gbarecomp_armv4t "-Wl,--end-group")
            target_compile_definitions(mzm_nes_stack_helper_test PRIVATE
                MZM_NES_EMULATOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.local/nes-emulator")
            add_test(NAME mzm-nes-stack-helper
                COMMAND mzm_nes_stack_helper_test "${MZM_TEST_ROM}")

            add_executable(mzm_resume_entry_test
                tests/m4/resume_entry_test.cpp
                $<TARGET_OBJECTS:mzm_generated_objects>)
            target_include_directories(mzm_resume_entry_test PRIVATE
                src generated
                "${GBARECOMP_ROOT}/src/armv4t"
                "${GBARECOMP_ROOT}/src/gba"
                "${GBARECOMP_ROOT}/src/runtime")
            target_link_libraries(mzm_resume_entry_test PRIVATE
                "-Wl,--start-group" gbarecomp_runtime gbarecomp_debug
                gbarecomp_gba gbarecomp_armv4t "-Wl,--end-group")
            add_test(NAME mzm-resume-entry COMMAND mzm_resume_entry_test)

            add_executable(mzm_host_stack_dispatch_test
                tests/m4/host_stack_dispatch_test.cpp
                $<TARGET_OBJECTS:mzm_generated_objects>)
            target_include_directories(mzm_host_stack_dispatch_test PRIVATE
                src generated
                "${GBARECOMP_ROOT}/src/armv4t"
                "${GBARECOMP_ROOT}/src/gba"
                "${GBARECOMP_ROOT}/src/runtime")
            target_link_libraries(mzm_host_stack_dispatch_test PRIVATE
                "-Wl,--start-group" gbarecomp_runtime gbarecomp_debug
                gbarecomp_gba gbarecomp_armv4t "-Wl,--end-group")
            add_test(NAME mzm-host-stack-dispatch
                COMMAND mzm_host_stack_dispatch_test --bounded)

            add_executable(mzm_nes_behavior_test
                tests/m4/nes_behavior_test.cpp
                src/mzm_ram_dispatch.cpp
                src/mzm_milestone_probe.cpp
                $<TARGET_OBJECTS:mzm_generated_objects>)
            target_include_directories(mzm_nes_behavior_test PRIVATE
                src generated
                "${GBARECOMP_ROOT}/src/debug"
                "${GBARECOMP_ROOT}/src/armv4t"
                "${GBARECOMP_ROOT}/src/gba"
                "${GBARECOMP_ROOT}/src/runtime")
            target_link_libraries(mzm_nes_behavior_test PRIVATE
                "-Wl,--start-group" gbarecomp_runtime gbarecomp_debug
                gbarecomp_gba gbarecomp_armv4t "-Wl,--end-group")
            target_compile_definitions(mzm_nes_behavior_test PRIVATE
                MZM_NES_EMULATOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.local/nes-emulator")
            find_package(Python3 COMPONENTS Interpreter)
            if(Python3_FOUND)
                add_test(NAME mzm-nes-behavior
                    COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-nes-behavior.py"
                        "$<TARGET_FILE:mzm_nes_behavior_test>" "${MZM_TEST_ROM}"
                        "${MZM_TEST_BIOS}")
                set_tests_properties(mzm-nes-behavior PROPERTIES TIMEOUT 900)
                add_test(NAME mzm-nes-save-roundtrip
                    COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-nes-save-roundtrip.py"
                        "$<TARGET_FILE:mzm_nes_behavior_test>" "${MZM_TEST_ROM}"
                        "${MZM_TEST_BIOS}")
                set_tests_properties(mzm-nes-save-roundtrip PROPERTIES TIMEOUT 1500)
                add_test(NAME mzm-host-save-persistence
                    COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-host-save-persistence.py"
                        "$<TARGET_FILE:mzm_nes_behavior_test>" "$<TARGET_FILE:MZMRecomp>"
                        "${CMAKE_CURRENT_SOURCE_DIR}/configs/mzm-us.toml"
                        "${MZM_TEST_ROM}" "${MZM_TEST_BIOS}")
                set_tests_properties(mzm-host-save-persistence PROPERTIES TIMEOUT 1500)
                add_test(NAME mzm-host-savestate
                    COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-host-savestate.py"
                        "$<TARGET_FILE:MZMRecomp>"
                        "${CMAKE_CURRENT_SOURCE_DIR}/configs/mzm-us.toml"
                        "${MZM_TEST_ROM}" "${MZM_TEST_BIOS}")
                set_tests_properties(mzm-host-savestate PROPERTIES TIMEOUT 600)
                add_test(NAME mzm-host-display
                    COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-host-display.py"
                        "$<TARGET_FILE:MZMRecomp>"
                        "${CMAKE_CURRENT_SOURCE_DIR}/configs/mzm-us.toml"
                        "${MZM_TEST_ROM}" "${MZM_TEST_BIOS}")
                set_tests_properties(mzm-host-display PROPERTIES TIMEOUT 600
                    SKIP_RETURN_CODE 77)
                add_test(NAME mzm-support-module
                    COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-support-module.py"
                        "$<TARGET_FILE:MZMRecomp>" "${MZM_TEST_ROM}" "${MZM_TEST_BIOS}")
                set_tests_properties(mzm-support-module PROPERTIES TIMEOUT 300
                    SKIP_RETURN_CODE 77)
                add_test(NAME mzm-nes-pause
                    COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-nes-pause.py"
                        "$<TARGET_FILE:mzm_nes_behavior_test>" "$<TARGET_FILE:MZMRecomp>"
                        "${CMAKE_CURRENT_SOURCE_DIR}/configs/mzm-us.toml"
                        "${MZM_TEST_ROM}" "${MZM_TEST_BIOS}")
                set_tests_properties(mzm-nes-pause PROPERTIES TIMEOUT 900
                    SKIP_RETURN_CODE 77)
            endif()
        endif()
        add_executable(mzm_chozodia_native_test
            tests/m4/chozodia_native_test.cpp
            src/mzm_ram_dispatch.cpp
            src/mzm_milestone_probe.cpp
            $<TARGET_OBJECTS:mzm_generated_objects>)
        target_include_directories(mzm_chozodia_native_test PRIVATE
            src generated
            "${GBARECOMP_ROOT}/src/armv4t"
            "${GBARECOMP_ROOT}/src/gba"
            "${GBARECOMP_ROOT}/src/runtime")
        target_link_libraries(mzm_chozodia_native_test PRIVATE
            "-Wl,--start-group" gbarecomp_runtime gbarecomp_debug
            gbarecomp_gba gbarecomp_armv4t "-Wl,--end-group")
        add_test(NAME mzm-chozodia-native
            COMMAND mzm_chozodia_native_test
                "${MZM_TEST_ROM}")
    endif()
endif()


# NES-PERF-1: attach the profiling shims to every target that links the RAM hook.
if(MZM_PERF_PROFILE)
    set(_mzm_wraps
        runtime_dispatch runtime_dispatch_tail runtime_dispatch_with_exchange
        runtime_dispatch_with_exchange_tail runtime_tail_drain
        _ZN3gba6GbaPpu4tickEjt _ZN3gba6GbaPpu15render_scanlineEjtPKhS2_S2_S2_
        _ZN3gba8GbaAudio4tickEj _ZN3gba5GbaIo11tick_timersEj _ZN3gba5GbaIo13run_timed_dmaEi)
    if(MZM_PERF_WRAP_TICK)
        list(APPEND _mzm_wraps runtime_tick)
    endif()
    foreach(_t MZMRecomp mzm_nes_behavior_test mzm_nes_payload_frontier_test
               mzm_nes_emulator_frontier_test mzm_nes_scope_policy_test
               mzm_nes_stack_helper_test mzm_chozodia_native_test)
        if(TARGET ${_t})
            target_sources(${_t} PRIVATE src/mzm_perf_profile.cpp)
            target_include_directories(${_t} PRIVATE src)
            foreach(_w IN LISTS _mzm_wraps)
                target_link_options(${_t} PRIVATE "-Wl,--wrap=${_w}")
            endforeach()
        endif()
    endforeach()
endif()

# ─────────────────────────────────────────────────────────────────────
# Optional recomp-ui pre-boot launcher.
#
# The launcher is deliberately isolated from the generated guest corpus:
# only this tiny wrapper is compiled with RECOMP_LAUNCHER/recomp-ui flags.
# If the submodule is not initialized the native CLI build remains usable.
# ─────────────────────────────────────────────────────────────────────
if(MZM_RECOMP_UI)
    if(EXISTS "${RECOMP_UI_ROOT}/recomp_ui.cmake")
        include("${RECOMP_UI_ROOT}/recomp_ui.cmake")

        add_library(mzm_launcher_ui STATIC
            src/game_launcher_boot.cpp
            src/launcher_state.cpp
            src/mzm_support.cpp
        )
        target_include_directories(mzm_launcher_ui PRIVATE
            "${RECOMP_UI_ROOT}/src/third_party/imgui"
            "${RECOMP_UI_ROOT}/src/third_party/imgui/backends"
            "${RECOMP_UI_ROOT}/src/common"
            "${GBARECOMP_ROOT}/src/runtime"
            "${GBARECOMP_ROOT}/src/gba"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
        )

        recomp_target_launcher_ui(mzm_launcher_ui
            CONSOLE gba
        )
        target_compile_definitions(mzm_launcher_ui PRIVATE
            MZM_VERSION="${PROJECT_VERSION}"
            MZM_BUILD_SHA="${MZM_GIT_SHA}"
            MZM_GBARECOMP_SHA="${MZM_GBARECOMP_HEAD}"
            MZM_RELEASE_LABEL="${MZM_RELEASE_LABEL}")

        add_custom_command(TARGET MZMRecomp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:MZMRecomp>/assets/icons"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/mzm-recompiled.bmp"
                    "$<TARGET_FILE_DIR:MZMRecomp>/assets/icons/mzm-recompiled.bmp"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/mzm-brand-helm-core.png"
                    "$<TARGET_FILE_DIR:MZMRecomp>/assets/icons/mzm-brand-helm-core.png"
            VERBATIM)
        set_property(TARGET MZMRecomp APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/mzm-recompiled.bmp"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/mzm-brand-helm-core.png")

        find_package(SDL2 REQUIRED)
        target_link_libraries(mzm_launcher_ui PUBLIC SDL2::SDL2)
        if(WIN32 AND NOT MZM_PLATFORM_UWP)
            target_link_libraries(mzm_launcher_ui PUBLIC opengl32 shell32)
        endif()

        target_link_libraries(MZMRecomp PRIVATE mzm_launcher_ui)
        # SDL's Windows header otherwise renames our real main() to SDL_main.
        set_property(SOURCE src/main.cpp APPEND PROPERTY
            COMPILE_DEFINITIONS MZM_RECOMP_UI=1 SDL_MAIN_HANDLED)

        message(STATUS "MZM-Recomp: recomp-ui launcher enabled")
    else()
        message(FATAL_ERROR
            "MZM_RECOMP_UI=ON but recomp-ui is not initialized at "
            "${RECOMP_UI_ROOT}. Run: git submodule update --init --recursive "
            "recomp-ui, or pass -DMZM_RECOMP_UI=OFF for a CLI-only host.")
    endif()
endif()
