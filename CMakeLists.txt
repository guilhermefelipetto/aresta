cmake_minimum_required(VERSION 3.21)
project(aresta LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Deixa o compile_commands.json na raiz do build pro clangd achar.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

include(FetchContent)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b-docking
    GIT_SHALLOW TRUE
)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
)

FetchContent_MakeAvailable(imgui stb)

# Nenhum dos dois traz CMakeLists, então os alvos são montados aqui.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

add_executable(aresta
    src/main.cpp
    src/app.cpp
    src/canvas.cpp
    src/chain.cpp
    src/chain_panel.cpp
    src/components_window.cpp
    src/convolve.cpp
    src/curve_window.cpp
    src/expr.cpp
    src/export.cpp
    src/export_window.cpp
    src/fft.cpp
    src/histogram.cpp
    src/histogram_window.cpp
    src/image.cpp
    src/kernel.cpp
    src/kernel_window.cpp
    src/morphology.cpp
    src/ops.cpp
    src/regions.cpp
    src/restore.cpp
    src/texture.cpp
    src/ui.cpp
    src/value.cpp
)

target_compile_options(aresta PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(aresta PRIVATE imgui stb)
