cmake_minimum_required(VERSION 3.20)
project(ScreenMate VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings and options
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Platform libraries for Windows
if(WIN32)
    set(PLATFORM_LIBS
        user32
        gdi32
        shell32
        dwmapi
        ole32
        ws2_32
        winmm
    )
endif()

# Enable Testing
enable_testing()

# Status output
message(STATUS "ScreenMate AI C++20 configured for ${CMAKE_SYSTEM_NAME} (${CMAKE_CXX_COMPILER_ID})")
