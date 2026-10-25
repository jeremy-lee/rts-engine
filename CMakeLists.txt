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
    sim/datatable.cpp
    sim/gamedefs.cpp
    sim/ai.cpp
    sim/fog.cpp
    sim/campaign.cpp
    render/software_renderer.cpp
)
target_include_directories(rts_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 网络层（仅 Windows）
if(WIN32)
    target_sources(rts_sim PRIVATE
        net/socket.cpp
        net/lockstep.cpp
    )
endif()

# ---- headless 可执行程序 ----
add_executable(rts_app app/main.cpp)
target_link_libraries(rts_app PRIVATE rts_sim)

# ---- 自动对战测试（跨平台）----
add_executable(rts_autotest tools/autotest.cpp)
target_link_libraries(rts_autotest PRIVATE rts_sim)

# ---- 测试（仅 headless，不依赖 GL）----
set(RTS_TEST_SOURCES
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
    tests/test_gamedefs.cpp
    tests/test_economy.cpp
    tests/test_ai.cpp
    tests/test_building.cpp
    tests/test_fog.cpp
    tests/test_audio.cpp
    tests/test_campaign.cpp
    tests/test_backend.cpp
    tests/test_turret.cpp
    tests/test_upgrade.cpp
    tests/test_save.cpp
)
if(WIN32)
    list(APPEND RTS_TEST_SOURCES tests/test_net.cpp)
endif()

add_executable(rts_tests ${RTS_TEST_SOURCES})
target_link_libraries(rts_tests PRIVATE rts_sim)
if(WIN32)
    target_link_libraries(rts_tests PRIVATE rts_audio)
endif()

enable_testing()
add_test(NAME rts_tests COMMAND rts_tests)

# ---- Windows 专属：渲染 / 联机 demo / 工具链 ----
if(WIN32)
    add_library(rts_audio STATIC
        audio/audio.cpp
    )
    target_include_directories(rts_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    if(MSVC)
        target_compile_options(rts_audio PRIVATE /wd4244)
    endif()

    add_library(rts_render STATIC
        platform/win32_window.cpp
        render/gl_renderer.cpp
        render/font.cpp
        render/assets.cpp
        render/cnc_assets.cpp
        render/terrain_gfx.cpp
        render/screenshot.cpp
    )
    target_include_directories(rts_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    target_link_libraries(rts_render PUBLIC opengl32 gdi32 user32 rts_sim)

    add_executable(rts_win_app app/win32_main.cpp)
    target_link_libraries(rts_win_app PRIVATE rts_render)

    add_executable(rts_net app/net_main.cpp)
    target_link_libraries(rts_net PRIVATE rts_sim)

    add_executable(rts_game app/game_main.cpp)
    target_link_libraries(rts_game PRIVATE rts_render rts_audio)

    # ---- 工具链（M8）----
    add_executable(rts_lint tools/rules_lint.cpp)
    target_link_libraries(rts_lint PRIVATE rts_sim)



    add_executable(rts_editor tools/map_editor.cpp)
    target_link_libraries(rts_editor PRIVATE rts_render)

    add_executable(rts_autotest_visual tools/autotest_visual.cpp)
    target_link_libraries(rts_autotest_visual PRIVATE rts_render)
endif()
