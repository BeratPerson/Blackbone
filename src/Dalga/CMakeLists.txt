cmake_minimum_required(VERSION 3.20)
project(Dalga VERSION 3.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "Windows-only project")
endif()

add_definitions(-DWIN32_LEAN_AND_MEAN -D_NOMINMAX -DUNICODE -D_UNICODE)

set(XIGN_SOURCES
    include/kernel/XignDriverInterface.cpp
    include/kernel/WbmfAuth.cpp
    include/crypto/PacketBuilder.cpp
    include/crypto/LcgCrypto.cpp
    main.cpp
)

add_executable(XignRecon ${XIGN_SOURCES})

target_include_directories(XignRecon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/crypto
    ${CMAKE_CURRENT_SOURCE_DIR}/include/kernel
)

target_link_libraries(XignRecon PRIVATE ntdll advapi32)

set_property(TARGET XignRecon PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    target_compile_options(XignRecon PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()