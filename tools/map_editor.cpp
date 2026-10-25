// RTS Engine — tools/map_editor.cpp
// 地图编辑器（M8）：Win32 + OpenGL 可视化编辑地形/资源，导出 RTSMAP 格式。
// 操作：
//   左键拖动 = 绘制当前笔刷
//   1=平地  2=水  3=岩石  4=高地  5=资源  0=擦除资源
//   方向键/WASD = 平移相机，滚轮 = 缩放
//   Ctrl+S = 保存 map_export.map（RTSMAP 格式）
//   Ctrl+O = 加载 demo_map.map
#include <cstdio>
#include <cstring>
#include <string>

#include "core/core.h"
#include "sim/map.h"
#include "render/projection.h"
#include "render/sprite_batch.h"
#include "render/camera.h"
#include "render/gl_renderer.h"
#include "platform/win32_window.h"

using namespace rts;

namespace {

enum class Brush { Flat = 0, Water = 1, Rock = 2, Highland = 3, Resource = 4 };

void ApplyBrush(Map& map, CellCoord c, Brush brush) {
	if (!map.InBounds(c)) return;
	switch (brush) {
		case Brush::Flat:
			map.SetFlags(c, kWalkable | kBuildable);
			map.SetTerrainHeight(c, 0);
			break;
		case Brush::Water:
			map.SetFlags(c, kWater);
			map.SetTerrainHeight(c, 0);
			break;
		case Brush::Rock:
			map.SetFlags(c, kBuildable);
			map.SetTerrainHeight(c, 0);
			break;
		case Brush::Highland:
			map.SetFlags(c, kWalkable | kBuildable);
			map.SetTerrainHeight(c, 2);
			break;
		case Brush::Resource:
			map.SetResourceAmount(c, 1000);
			map.SetFlags(c, kWalkable | kBuildable);
			map.SetTerrainHeight(c, 0);
			break;
	}
}

} // namespace

int main() {
	Win32Window window;
	if (!window.Create("RTS Engine — Map Editor", 1280, 720)) return 1;

	GLRenderer renderer;
	if (!renderer.Initialize(window.DeviceContext())) return 1;

	Map map = Map::GenerateTestMap(256, 256, 2024);
	const char* exportPath = "map_export.map";
	const char* loadPath = "demo_map.map";

	Camera camera;
	camera.viewW = window.Width();
	camera.viewH = window.Height();
	{
		const ScreenPos c = ProjectCell(CellCoord(128, 128), 0);
		camera.centerX = static_cast<float>(c.x);
		camera.centerY = static_cast<float>(c.y);
		camera.zoom = 0.9f;
	}

	Brush brush = Brush::Flat;
	std::string status = "Ready";

	while (window.PumpMessages()) {
		// ---- 相机 ----
		const float pan = 6.0f / camera.zoom;
		if (window.IsKeyDown('W') || window.IsKeyDown(VK_UP)) camera.centerY -= pan;
		if (window.IsKeyDown('S') || window.IsKeyDown(VK_DOWN)) camera.centerY += pan;
		if (window.IsKeyDown('A') || window.IsKeyDown(VK_LEFT)) camera.centerX -= pan;
		if (window.IsKeyDown('D') || window.IsKeyDown(VK_RIGHT)) camera.centerX += pan;

		const int wheel = window.ConsumeWheel();
		if (wheel != 0) {
			camera.ZoomAt((wheel > 0) ? 1.1f : 1.0f / 1.1f,
				static_cast<float>(window.MouseX()), static_cast<float>(window.MouseY()));
		}

		// ---- 笔刷切换 ----
		if (window.IsKeyDown('1')) brush = Brush::Flat;
		if (window.IsKeyDown('2')) brush = Brush::Water;
		if (window.IsKeyDown('3')) brush = Brush::Rock;
		if (window.IsKeyDown('4')) brush = Brush::Highland;
		if (window.IsKeyDown('5')) brush = Brush::Resource;

		// ---- 绘制（左键）----
		CellCoord hoverCell(-1, -1);
		{
			float wx = 0, wy = 0;
			camera.ScreenToWorld(static_cast<float>(window.MouseX()), static_cast<float>(window.MouseY()), wx, wy);
			hoverCell = ScreenToCell(ScreenPos{static_cast<int32_t>(wx), static_cast<int32_t>(wy)});
		}
		if (window.IsMouseDown(0) && map.InBounds(hoverCell)) {
			ApplyBrush(map, hoverCell, brush);
		}

		// ---- 保存/加载 ----
		{
			static bool ctrlSPrev = false, ctrlOPrev = false;
			const bool ctrl = window.IsKeyDown(VK_CONTROL);
			const bool sPressed = window.IsKeyDown('S');
			const bool oPressed = window.IsKeyDown('O');
			if (ctrl && sPressed && !ctrlSPrev) {
				if (map.SaveToText(exportPath)) status = std::string("Saved: ") + exportPath;
				else status = "Save failed";
			}
			if (ctrl && oPressed && !ctrlOPrev) {
				if (map.LoadFromText(loadPath)) status = std::string("Loaded: ") + loadPath;
				else status = "Load failed (file missing?)";
			}
			ctrlSPrev = sPressed;
			ctrlOPrev = oPressed;
		}

		// ---- 渲染 ----
		SpriteBatch batch;
		batch.Begin();
		for (int32_t y = 0; y < map.Height(); ++y) {
			for (int32_t x = 0; x < map.Width(); ++x) {
				const CellCoord c(x, y);
				const ScreenPos p = ProjectCell(c, map.TerrainHeight(c));
				float w = static_cast<float>(kTileW);
				float h = static_cast<float>(kTileH);
				uint32_t color = 0xFF3A5A2A;
				if (map.Water(c)) color = 0xFF1A3A6B;
				else if (!map.Walkable(c)) color = 0xFF555555;
				else if (map.TerrainHeight(c) > 0) color = 0xFF4A7A3A;
				if (map.ResourceAmount(c) > 0) color = 0xFFD4A017; // 金色矿点
				batch.AddSolid(static_cast<float>(p.x) - w * 0.5f,
					static_cast<float>(p.y) - h * 0.5f, w, h, color);
			}
		}
		// 悬停高亮
		if (map.InBounds(hoverCell)) {
			const ScreenPos p = ProjectCell(hoverCell, map.TerrainHeight(hoverCell));
			batch.AddSolid(static_cast<float>(p.x) - kTileW * 0.5f,
				static_cast<float>(p.y) - kTileH * 0.5f, static_cast<float>(kTileW),
				static_cast<float>(kTileH), 0x80FFFFFF);
		}
		batch.End();

		renderer.BeginFrame(window.Width(), window.Height());
		renderer.DrawBatch(batch.Vertices(), camera);
		renderer.EndFrame();

		// ---- 标题 ----
		{
			static const char* brushNames[] = {"Flat", "Water", "Rock", "Highland", "Resource"};
			char title[256];
			const int brushIdx = static_cast<int>(brush);
			std::snprintf(title, sizeof(title),
				"Map Editor | brush=%s | cell=(%d,%d) res=%d | %s | Ctrl+S save",
				brushNames[brushIdx < 0 || brushIdx > 4 ? 0 : brushIdx],
				hoverCell.x, hoverCell.y,
				map.InBounds(hoverCell) ? map.ResourceAmount(hoverCell) : 0,
				status.c_str());
			SetWindowTextA(window.Handle(), title);
		}
	}

	// 退出时自动保存
	map.SaveToText(exportPath);
	renderer.Shutdown();
	window.Destroy();
	return 0;
}
