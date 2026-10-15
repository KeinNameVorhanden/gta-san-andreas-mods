cmake_minimum_required(VERSION 3.25)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SACORE_VERSION LIMIT_COUNT 1)
# Re-run configure when VERSION changes, so the version string and resource never lag behind a bump.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(SACore VERSION ${SACORE_VERSION} LANGUAGES C CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "GTA San Andreas is a 32-bit game: configure with -A Win32 (see CMakePresets.json).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/utf-8)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

include(FetchContent)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(minhook tomlplusplus doctest imgui)

# Dear ImGui (MIT) with the DX9 and Win32 backends, used by the developer console mod.
add_library(imgui_dx9 STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui_dx9 PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_dx9 PUBLIC d3d9)

# Strict warnings for our own targets only (third-party code keeps its defaults).
function(sacore_target target)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /Zi)
    # Symbols and a map file for every configuration: crash reports give module+RVA, these turn it into a function.
    target_link_options(${target} PRIVATE /DEBUG /OPT:REF /OPT:ICF /MAP)
endfunction()

# ASI plugins are DLLs with the .asi suffix.
function(sacore_asi target)
    sacore_target(${target})
    set_target_properties(${target} PROPERTIES SUFFIX ".asi")
endfunction()

add_subdirectory(probes/hello)
add_subdirectory(src/api)
add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(mods)

enable_testing()
add_subdirectory(tests/unit)
