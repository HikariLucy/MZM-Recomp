cmake_minimum_required(VERSION 3.20)
project(MZMRecomp VERSION 0.1.0 LANGUAGES C CXX)
include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GBARECOMP_ROOT ""
    CACHE PATH "Path to the pinned gbarecomp checkout")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui"
    CACHE PATH "Path to the pinned recomp-ui checkout")
option(MZM_RECOMP_UI "Build the recomp-ui pre-boot launcher" ON)

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
set(MZM_GBARECOMP_PIN "2c40fe8539c566ce2aee7dce8a722917a6cf475c"
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

add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)

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
    set_source_files_properties(${GAME_RECOMPILED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-O1;-g0")
endif()

target_include_directories(MZMRecomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    "${GBARECOMP_ROOT}/src/armv4t"
    "${GBARECOMP_ROOT}/src/gba"
    "${GBARECOMP_ROOT}/src/runtime"
    "${GBARECOMP_ROOT}/src/debug"
)

target_link_libraries(MZMRecomp PRIVATE
    "-Wl,--start-group"
    gbarecomp_runtime
    gbarecomp_debug
    gbarecomp_gba
    gbarecomp_armv4t
    "-Wl,--end-group"
)

target_compile_definitions(MZMRecomp PRIVATE
    GBARECOMP_WINDOW_TITLE="MZM-Recomp"
    MZM_VERSION="${PROJECT_VERSION}"
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

            add_executable(mzm_nes_behavior_test
                tests/m4/nes_behavior_test.cpp
                src/mzm_ram_dispatch.cpp
                src/mzm_milestone_probe.cpp
                $<TARGET_OBJECTS:mzm_generated_objects>)
            target_include_directories(mzm_nes_behavior_test PRIVATE
                src generated
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
        target_compile_definitions(mzm_launcher_ui PRIVATE MZM_VERSION="${PROJECT_VERSION}")

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
        if(WIN32)
            target_link_libraries(mzm_launcher_ui PUBLIC opengl32)
        endif()

        target_link_libraries(MZMRecomp PRIVATE mzm_launcher_ui)
        set_property(SOURCE src/main.cpp APPEND PROPERTY
            COMPILE_DEFINITIONS MZM_RECOMP_UI=1)

        message(STATUS "MZM-Recomp: recomp-ui launcher enabled")
    else()
        message(WARNING
            "MZM_RECOMP_UI=ON but recomp-ui is not initialized at "
            "${RECOMP_UI_ROOT}; building CLI-only host. "
            "Run: git submodule update --init --recursive recomp-ui")
    endif()
endif()
