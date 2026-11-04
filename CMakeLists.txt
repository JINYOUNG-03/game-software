cmake_minimum_required(VERSION 3.21)
project(HaedongGame VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "This game currently requires Windows, MSVC/clang-cl and the Windows SDK (GDI, WIC, Winsock).")
endif()

# Source-only, offline builds. Keep CRT and library configuration/architecture consistent.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)

add_library(haedong_glew STATIC third_party/glew/src/glew.c)
target_include_directories(haedong_glew PUBLIC third_party/glew/include)
target_compile_definitions(haedong_glew PUBLIC GLEW_STATIC GLEW_NO_GLU)
set_target_properties(haedong_glew PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)

set(game_dir "${CMAKE_CURRENT_SOURCE_DIR}/BaseProject_SE/SimpleGame")
add_executable(SimpleGame
    ${game_dir}/Actor.cpp ${game_dir}/SceneGraph.cpp
    ${game_dir}/LevelOne.cpp ${game_dir}/HarborWorld.cpp ${game_dir}/HarborTrade.cpp
    ${game_dir}/Renderer.cpp ${game_dir}/RendererBatch.cpp ${game_dir}/RendererModels.cpp
    ${game_dir}/Profiler.cpp ${game_dir}/WorldMonitor.cpp ${game_dir}/KoreanFont.cpp
    ${game_dir}/SimpleGame.cpp ${game_dir}/stdafx.cpp)
target_compile_features(SimpleGame PRIVATE cxx_std_17)
target_compile_definitions(SimpleGame PRIVATE NOMINMAX UNICODE _UNICODE GLFW_INCLUDE_NONE $<$<CONFIG:Debug>:_DEBUG>)
target_compile_options(SimpleGame PRIVATE /utf-8 /W3 /MP)
target_compile_options(haedong_glew PRIVATE /utf-8)
target_link_libraries(SimpleGame PRIVATE glfw haedong_glew opengl32 gdi32 user32 shell32
    windowscodecs ole32 oleaut32 uuid psapi ws2_32)
set_target_properties(SimpleGame PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:SimpleGame>")

set(runtime_dir "$<TARGET_FILE_DIR:SimpleGame>")
add_custom_command(TARGET SimpleGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${runtime_dir}/Resources" "${runtime_dir}/Fonts" "${runtime_dir}/Licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${game_dir}/Shaders" "${runtime_dir}/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${game_dir}/Monitor" "${runtime_dir}/Monitor"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/haedong_runtime/world_atlas.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/haedong_runtime/world_sprites.bin" "${runtime_dir}/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/Fonts/NanumGothic-Regular.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/Fonts/OFL-NanumGothic.txt" "${runtime_dir}/Fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/LICENSE.md" "${runtime_dir}/Licenses/GLFW.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glew/LICENSE.txt" "${runtime_dir}/Licenses/GLEW.txt"
    VERBATIM)
