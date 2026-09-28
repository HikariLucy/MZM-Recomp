cmake_minimum_required(VERSION 3.20)
project(MZMRecomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GBARECOMP_ROOT ""
    CACHE PATH "Path to the pinned gbarecomp checkout")

if(NOT GBARECOMP_ROOT)
    message(FATAL_ERROR
        "Set -DGBARECOMP_ROOT=<path-to-gbarecomp>. "
        "MZM-Recomp does not vendor the framework.")
endif()

if(NOT EXISTS "${GBARECOMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "GBARECOMP_ROOT is not a gbarecomp checkout:\n  ${GBARECOMP_ROOT}")
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

add_executable(MZMRecomp
    src/main.cpp
    src/mzm_ram_dispatch.cpp
    ${GAME_GENERATED_SOURCES}
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
)
