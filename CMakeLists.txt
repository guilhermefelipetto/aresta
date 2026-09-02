cmake_minimum_required(VERSION 3.21)
project(aresta LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Deixa o compile_commands.json na raiz do build pro clangd achar.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(aresta
    src/main.cpp
)

target_compile_options(aresta PRIVATE -Wall -Wextra -Wpedantic)
