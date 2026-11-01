cmake_minimum_required(VERSION 3.20)
project(AntiCheatSimulation VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_definitions(-DWIN32_LEAN_AND_MEAN -D_NOMINMAX -DUNICODE -D_UNICODE)
    # Suppress MSVC warnings about unchecked iterators / secure CRT
    add_definitions(-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS)
endif()

# ── Dalga crypto-only library ─────────────────────────────────────────────────
# Only the pure-math files: LcgCrypto + PacketBuilder.
# WbmfAuth and XignDriverInterface are deliberately excluded: they require
# ntdll.lib and must not be linked in a userspace simulation binary.
set(DALGA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../src/Dalga")

add_library(dalga_core STATIC
    "${DALGA_ROOT}/include/crypto/LcgCrypto.cpp"
    "${DALGA_ROOT}/include/crypto/PacketBuilder.cpp"
)

# PUBLIC so anticheat_sim inherits the same include directories
target_include_directories(dalga_core PUBLIC
    "${DALGA_ROOT}/include"
    "${DALGA_ROOT}/include/crypto"
    "${DALGA_ROOT}/include/kernel"
)

if(MSVC)
    target_compile_options(dalga_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(dalga_core PRIVATE -Wall -Wextra)
endif()

# ── Simulation core ────────────────────────────────────────────────────────────
add_library(sim_core STATIC
    src/common.cpp
    src/virtual_memory.cpp
    src/virtual_process.cpp
    src/heartbeat.cpp
    src/detection_engine.cpp
    src/scenario.cpp
    src/simulation.cpp
)

target_include_directories(sim_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(MSVC)
    target_compile_options(sim_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(sim_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Main test executable ───────────────────────────────────────────────────────
add_executable(anticheat_sim src/main.cpp)
target_link_libraries(anticheat_sim PRIVATE sim_core dalga_core)

if(MSVC)
    target_compile_options(anticheat_sim PRIVATE /W4 /permissive- /Zc:__cplusplus)
    set_property(TARGET anticheat_sim PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET dalga_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET sim_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(anticheat_sim PRIVATE -Wall -Wextra)
endif()
