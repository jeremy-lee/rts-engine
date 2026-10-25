cmake_minimum_required(VERSION 3.20)
project(rts_engine LANGUAGES CXX)

# ---- 全局设置 ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- 模拟层（core + sim）----
add_library(rts_sim STATIC
    sim/world.cpp
    sim/map.cpp
    sim/activity.cpp
    sim/pathfinder.cpp
)
target_include_directories(rts_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---- headless 可执行程序 ----
add_executable(rts_app app/main.cpp)
target_link_libraries(rts_app PRIVATE rts_sim)

# ---- 渲染层（Windows：Win32 + OpenGL 1.1）----
if(WIN32)
    add_library(rts_render STATIC
        platform/win32_window.cpp
        render/gl_renderer.cpp
    )
    target_include_directories(rts_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(rts_render PUBLIC opengl32 gdi32 user32 rts_sim)

    add_executable(rts_win_app app/win32_main.cpp)
    target_link_libraries(rts_win_app PRIVATE rts_render)
endif()

# ---- 测试（仅 headless，不依赖 GL）----
add_executable(rts_tests
    tests/test_main.cpp
    tests/test_fixed.cpp
    tests/test_random.cpp
    tests/test_cell_layer.cpp
    tests/test_determinism.cpp
    tests/test_map.cpp
    tests/test_projection.cpp
    tests/test_actor.cpp
    tests/test_activity.cpp
    tests/test_replay.cpp
    tests/test_camera.cpp
    tests/test_sprite_batch.cpp
    tests/test_snapshot.cpp
    tests/test_pathfinder.cpp
)
target_link_libraries(rts_tests PRIVATE rts_sim)

enable_testing()
add_test(NAME rts_tests COMMAND rts_tests)
