cmake_minimum_required(VERSION 3.22)
project(DGPCorpRoot)

enable_testing()

# macOS-only: Universal Binary + deployment target (không áp dụng khi build Windows).
if (APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build Universal Binary" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS Target" FORCE)
endif()

add_subdirectory(ShowCue)
