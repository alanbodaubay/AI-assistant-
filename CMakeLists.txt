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

# -------------------------------------------------------------
# Target: ScreenMate Shell (Milestone 1 Desktop App)
# -------------------------------------------------------------
add_executable(screenmate_shell
    src/main.cpp
    src/renderer/blob_renderer.cpp
    src/ui/layered_window.cpp
    src/ui/blob_window.cpp
    src/brain/llama_client.cpp
    src/ui/bubble_window.cpp
)
target_link_libraries(screenmate_shell PRIVATE ${PLATFORM_LIBS})

# -------------------------------------------------------------
# Target: Unit Tests
# -------------------------------------------------------------
add_executable(test_blob_renderer
    tests/test_blob_renderer.cpp
    src/renderer/blob_renderer.cpp
)
target_link_libraries(test_blob_renderer PRIVATE ${PLATFORM_LIBS})
add_test(NAME TestBlobRenderer COMMAND test_blob_renderer)

add_executable(test_window_create
    tests/test_window_create.cpp
    src/renderer/blob_renderer.cpp
    src/ui/layered_window.cpp
    src/ui/blob_window.cpp
)
target_link_libraries(test_window_create PRIVATE ${PLATFORM_LIBS})
add_test(NAME TestWindowCreate COMMAND test_window_create)

add_executable(test_bubble_window
    tests/test_bubble_window.cpp
    src/ui/bubble_window.cpp
)
target_link_libraries(test_bubble_window PRIVATE ${PLATFORM_LIBS})
add_test(NAME TestBubbleWindow COMMAND test_bubble_window)

add_executable(test_llama_client
    tests/test_llama_client.cpp
    src/brain/llama_client.cpp
)
target_link_libraries(test_llama_client PRIVATE ${PLATFORM_LIBS})
add_test(NAME TestLlamaClient COMMAND test_llama_client)

message(STATUS "ScreenMate AI C++20 configured for ${CMAKE_SYSTEM_NAME} (${CMAKE_CXX_COMPILER_ID})")
