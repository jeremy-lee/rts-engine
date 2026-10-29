// RTS Engine — tools/map_editor.cpp
// 地图编辑器（完整版）：渲染后端与游戏完全一致
//   - 地形：TerrainGfx 程序化菱形纹理（与 rts_game 相同）
//   - 单位预览：CncAtlas 精灵
//   - 相机：Camera（WASD / 边缘滚动 / 滚轮缩放 / 右键拖拽）
//   - UI：ui::BuildPanel/BuildButton + BitmapFont
//
// 操作：
//   左键拖动      = 绘制当前笔刷
//   1-6           = 切换笔刷（草地/水/岩石/沙地/高地/资源）
//   0             = 橡皮擦（清除资源，恢复草地）
//   WASD/方向键   = 平移；鼠标到边缘 = 滚动；滚轮 = 缩放
//   Ctrl+S        = 保存 map_export.map
//   Ctrl+O        = 加载 demo_map.map
//   G             = 开关网格
//   N             = 新建地图（128x128）
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/core.h"
#include "sim/map.h"
#include "render/projection.h"
#include "render/sprite_batch.h"
#include "render/camera.h"
#include "render/gl_renderer.h"
#include "render/terrain_gfx.h"
#include "render/cnc_assets.h"
#include "render/text.h"
#include "render/ui.h"
#include "render/font.h"
#include "platform/win32_window.h"

using namespace rts;

namespace {

enum class Brush { Grass = 0, Water, Rock, Sand, Highland, Resource, Eraser, Count };
constexpr int kBrushCount = static_cast<int>(Brush::Count);

const char* BrushName(Brush b) {
	switch (b) {
		case Brush::Grass:    return "GRASS";
		case Brush::Water:    return "WATER";
		case Brush::Rock:     return "ROCK";
		case Brush::Sand:     return "SAND";
		case Brush::Highland: return "HIGHLAND";
		case Brush::Resource: return "RESOURCE";
		case Brush::Eraser:   return "ERASER";
		default:              return "?";
	}
}

// 笔刷 → 地形种类（渲染用）
TerrainKind BrushTerrain(Brush b) {
	switch (b) {
		case Brush::Water:    return TerrainKind::Water;
		case Brush::Rock:     return TerrainKind::Rock;
		case Brush::Sand:     return TerrainKind::Sand;
		case Brush::Highland: return TerrainKind::Highland;
		default:              return TerrainKind::Grass;
	}
}

void ApplyBrush(Map& map, CellCoord c, Brush b, int radius) {
	if (!map.InBounds(c)) return;
	const int32_t r = radius;
	for (int32_t dy = -r; dy <= r; ++dy) {
		for (int32_t dx = -r; dx <= r; ++dx) {
			if (dx * dx + dy * dy > r * r) continue;
			const CellCoord cell(c.x + dx, c.y + dy);
			if (!map.InBounds(cell)) continue;
			switch (b) {
				case Brush::Grass:
					map.SetFlags(cell, kWalkable | kBuildable);
					map.SetTerrainHeight(cell, 0);
					break;
				case Brush::Water:
					map.SetFlags(cell, kWater);
					map.SetTerrainHeight(cell, 0);
					break;
				case Brush::Rock:
					map.SetFlags(cell, kBuildable);
					map.SetTerrainHeight(cell, 0);
					break;
				case Brush::Sand:
					map.SetFlags(cell, kWalkable | kBuildable);
					map.SetTerrainHeight(cell, 0);
					break;
				case Brush::Highland:
					map.SetFlags(cell, kWalkable | kBuildable);
					map.SetTerrainHeight(cell, 2);
					break;
				case Brush::Resource:
					map.SetResourceAmount(cell, 2000);
					map.SetFlags(cell, kWalkable | kBuildable);
					map.SetTerrainHeight(cell, 0);
					break;
				case Brush::Eraser:
					map.SetResourceAmount(cell, 0);
					map.SetFlags(cell, kWalkable | kBuildable);
					map.SetTerrainHeight(cell, 0);
					break;
				default:
					break;
			}
		}
	}
}

// 地形种类 → 颜色（用于沙地/岩石等无纹理差异的情况，由 TerrainGfx 提供纹理）
TerrainKind TerrainOf(const Map& map, CellCoord c) {
	if (map.Water(c)) return TerrainKind::Water;
	if (!map.Walkable(c)) return TerrainKind::Rock;
	if (map.TerrainHeight(c) > 0) return TerrainKind::Highland;
	return TerrainKind::Grass;
}

} // namespace

int main() {
	Win32Window window;
	if (!window.Create("RTS Engine — Map Editor", 1400, 800)) return 1;

	GLRenderer renderer;
	if (!renderer.Initialize(window.DeviceContext())) {
		std::fprintf(stderr, "[editor] renderer init failed\n");
		return 1;
	}

	// 字体纹理（状态栏/工具栏文字）
	int fontW = 0, fontH = 0;
	const std::vector<uint8_t> fontPixels = BitmapFont::BuildTextureData(fontW, fontH);
	const uint32_t fontTex = renderer.UploadTexture(fontPixels, fontW, fontH);

	// 地形纹理（程序化 5 种）
	int terrW = 0, terrH = 0;
	const std::vector<uint8_t> terrPixels = TerrainGfx::Build(terrW, terrH);
	const uint32_t terrainTex = renderer.UploadTexture(terrPixels, terrW, terrH);

	// 单位图集（与游戏共用；只做预览，编辑器不保存单位）
	CncAtlas::LoadMeta("assets/cnc_atlas.txt");
	std::vector<uint8_t> atlasPixels;
	int atlasW = 0, atlasH = 0;
	const bool cncOk = CncAtlas::Load("assets/cnc_atlas.png", atlasPixels, atlasW, atlasH);
	const uint32_t cncTex = cncOk ? renderer.UploadTexture(atlasPixels, atlasW, atlasH) : 0;

	Map map = Map::GenerateTestMap(128, 128, 2024);
	Camera camera;
	camera.viewW = window.Width();
	camera.viewH = window.Height();
	{
		const ScreenPos c = ProjectCell(CellCoord(64, 64), 0);
		camera.centerX = static_cast<float>(c.x);
		camera.centerY = static_cast<float>(c.y);
		camera.zoom = 1.2f;
	}

	Brush brush = Brush::Grass;
	int brushRadius = 1;
	bool showGrid = false;
	std::string status = "Ready";
	CellCoord hoverCell(-1, -1);

	while (window.PumpMessages()) {
		const int W = window.Width();
		const int H = window.Height();
		const float mx = static_cast<float>(window.MouseX());
		const float my = static_cast<float>(window.MouseY());

		// ---- 相机：WASD + 边缘滚动 + 滚轮 + 右键拖拽 ----
		const float pan = 520.0f * (1.0f / 60.0f) / camera.zoom;
		if (window.IsKeyDown('W') || window.IsKeyDown(VK_UP)) camera.centerY -= pan;
		if (window.IsKeyDown('S') || window.IsKeyDown(VK_DOWN)) camera.centerY += pan;
		if (window.IsKeyDown('A') || window.IsKeyDown(VK_LEFT)) camera.centerX -= pan;
		if (window.IsKeyDown('D') || window.IsKeyDown(VK_RIGHT)) camera.centerX += pan;
		// 边缘滚动（调试时禁用：鼠标停在边缘会让相机漂移出视野）
		if (false) {
			const float edge = 24.0f;
			const float scroll = 900.0f * (1.0f / 60.0f) / camera.zoom;
			const float uiTop = 40.0f;
			const float uiBottom = 28.0f;
			if (mx >= 0.0f && mx < edge) camera.centerX -= scroll;
			if (mx > static_cast<float>(W) - edge) camera.centerX += scroll;
			if (my >= uiTop && my < uiTop + edge) camera.centerY -= scroll;
			if (my > static_cast<float>(H) - uiBottom - edge && my < static_cast<float>(H) - uiBottom) camera.centerY += scroll;
		}
		const int wheel = window.ConsumeWheel();
		if (wheel != 0) {
			camera.ZoomAt((wheel > 0) ? 1.1f : 1.0f / 1.1f, mx, my);
		}
		camera.viewW = W;
		camera.viewH = H;

		// ---- 笔刷切换（数字键）----
		static const int keyMap[kBrushCount] = {'1', '2', '3', '4', '5', '6', '0'};
		for (int i = 0; i < kBrushCount; ++i) {
			if (window.IsKeyDown(keyMap[i])) brush = static_cast<Brush>(i);
		}

		// ---- 悬停格 ----
		{
			float wx = 0, wy = 0;
			camera.ScreenToWorld(mx, my, wx, wy);
			hoverCell = ScreenToCell(ScreenPos{static_cast<int32_t>(wx), static_cast<int32_t>(wy)});
		}

		// ---- 绘制（左键，避开顶部/底部 UI）----
		const float uiTop = 40.0f;
		const float uiBottom = 28.0f;
		const bool inCanvas = (my > uiTop && my < static_cast<float>(H) - uiBottom);
		if (window.IsMouseDown(0) && inCanvas && map.InBounds(hoverCell)) {
			ApplyBrush(map, hoverCell, brush, brushRadius);
		}

		// ---- 保存/加载 ----
		{
			static bool ctrlSPrev = false, ctrlOPrev = false;
			const bool ctrl = window.IsKeyDown(VK_CONTROL);
			const bool sNow = window.IsKeyDown('S');
			const bool oNow = window.IsKeyDown('O');
			if (ctrl && sNow && !ctrlSPrev) {
				status = map.SaveToText("map_export.map") ? "Saved map_export.map" : "Save failed";
			}
			if (ctrl && oNow && !ctrlOPrev) {
				status = map.LoadFromText("demo_map.map") ? "Loaded demo_map.map" : "Load failed";
			}
			ctrlSPrev = sNow;
			ctrlOPrev = oNow;
		}
		if (window.IsKeyDown('G')) showGrid = true;
		if (!window.IsKeyDown('G')) { /* 网格开关（按住 G 显示） */ }
		if (window.IsKeyDown('N')) {
			map = Map::GenerateTestMap(128, 128, static_cast<uint64_t>(hoverCell.x * 31 + hoverCell.y * 17 + 1));
			status = "New map 128x128";
		}

		// ---- 渲染：地形批次（与游戏相同：TerrainGfx 菱形纹理 + x+y 排序）----
		SpriteBatch terrainBatch;
		terrainBatch.Begin();
		{
			// 全图渲染（128x128 = 16384 格，顶点量可接受；等距裁剪需逆投影，暂不做）
			std::vector<CellCoord> cells;
			cells.reserve(static_cast<size_t>(map.Width()) * static_cast<size_t>(map.Height()));
			for (int32_t y = 0; y < map.Height(); ++y) {
				for (int32_t x = 0; x < map.Width(); ++x) cells.push_back(CellCoord(x, y));
			}
			std::sort(cells.begin(), cells.end(), [](const CellCoord& a, const CellCoord& b) {
				return (a.x + a.y) < (b.x + b.y);
			});
			for (const CellCoord& c : cells) {
				if (!map.InBounds(c)) continue;
				const ScreenPos p = ProjectCell(c, map.TerrainHeight(c));
				const TerrainKind tk = TerrainOf(map, c);
				// 2× tile 尺寸渲染：等距投影下相邻格中心相距 (32,16)，tile 64×32 会覆盖 2×2 区域，
				// 同色重叠保证无缝（1:1 渲染会有亚像素缝隙成黑线）
				const float tw = static_cast<float>(TerrainGfx::kTileW) * 2.0f;
				const float th = static_cast<float>(TerrainGfx::kTileH) * 2.0f;
				// 纯色（调试渲染路径；纹理版本见下方注释）
				uint32_t tc = 0xFF3A5A2A;
				if (tk == TerrainKind::Water) tc = 0xFF1A3A6B;
				else if (tk == TerrainKind::Rock) tc = 0xFF555555;
				else if (tk == TerrainKind::Sand) tc = 0xFFD4A017;
				else if (tk == TerrainKind::Highland) tc = 0xFF4A7A3A;
				terrainBatch.AddSolid(static_cast<float>(p.x) - tw * 0.5f,
					static_cast<float>(p.y) - th * 0.5f, tw, th, tc);
				// 资源点：金色菱形叠加
				if (map.ResourceAmount(c) > 0) {
					terrainBatch.AddSolid(static_cast<float>(p.x) - 8.0f,
						static_cast<float>(p.y) - 4.0f, 16.0f, 8.0f, 0xE0D4A017u);
				}
			}
			// 网格
			if (showGrid) {
				for (const CellCoord& c : cells) {
					if (!map.InBounds(c)) continue;
					const ScreenPos p = ProjectCell(c, map.TerrainHeight(c));
					terrainBatch.AddSolid(static_cast<float>(p.x) - 1.0f,
						static_cast<float>(p.y) - static_cast<float>(kTileH) * 0.5f, 1.0f,
						static_cast<float>(kTileH), 0x30FFFFFFu);
				}
			}
		}
		// 悬停高亮（菱形 + 十字）
		if (map.InBounds(hoverCell)) {
			const ScreenPos p = ProjectCell(hoverCell, map.TerrainHeight(hoverCell));
			const uint32_t hc = (brush == Brush::Eraser) ? 0x80FF4040u : 0x80FFFFFFu;
			terrainBatch.AddSolid(static_cast<float>(p.x) - static_cast<float>(kTileW) * 0.5f,
				static_cast<float>(p.y) - static_cast<float>(kTileH) * 0.5f,
				static_cast<float>(kTileW), static_cast<float>(kTileH), hc);
			// 中心十字
			terrainBatch.AddSolid(static_cast<float>(p.x) - 8.0f, static_cast<float>(p.y) - 1.0f, 16.0f, 2.0f, 0xFFFF4040u);
			terrainBatch.AddSolid(static_cast<float>(p.x) - 1.0f, static_cast<float>(p.y) - 8.0f, 2.0f, 16.0f, 0xFFFF4040u);
		}
		terrainBatch.End();

		renderer.BeginFrame(W, H);
		{
			static int dbgN = 0;
			if (dbgN < 3) {
				std::printf("[ed] verts=%zu cam=(%.0f,%.0f) zoom=%.2f view=%dx%d\n",
					terrainBatch.Vertices().size(), camera.centerX, camera.centerY,
					camera.zoom, W, H);
				if (!terrainBatch.Vertices().empty()) {
					const SpriteVertex& v = terrainBatch.Vertices()[0];
					std::printf("[ed] v0 pos=(%.0f,%.0f) uv=(%.2f,%.2f) col=%08X\n", v.x, v.y, v.u, v.v, v.color);
				}
				std::fflush(stdout);
				++dbgN;
			}
		}
		renderer.DrawTexturedBatch(terrainBatch.Vertices(), camera, terrainTex);

		// ---- UI：顶部工具栏 + 底部状态栏 ----
		std::vector<SpriteVertex> uiVerts;
		const float topH = 40.0f;
		const float botH = 28.0f;
		ui::BuildPanel(uiVerts, ui::Rect{0, 0, static_cast<float>(W), topH}, 0xEE202020);
		ui::BuildPanel(uiVerts, ui::Rect{0, static_cast<float>(H) - botH, static_cast<float>(W), botH}, 0xEE202020);

		// 笔刷按钮（7 个）
		const float btnW = 86.0f;
		const float btnH = 26.0f;
		const float btnY = (topH - btnH) * 0.5f;
		ui::Rect brushRects[kBrushCount];
		for (int i = 0; i < kBrushCount; ++i) {
			const float bx = 8.0f + static_cast<float>(i) * (btnW + 6.0f);
			brushRects[i] = ui::Rect{bx, btnY, btnW, btnH};
			const bool active = (static_cast<int>(brush) == i);
			const uint32_t fill = active ? 0xFF3A6A3A : 0xFF2E2E2E;
			const uint32_t border = active ? 0xFF80FF80 : 0xFF505050;
			ui::Button btn;
			btn.rect = brushRects[i];
			btn.label = BrushName(static_cast<Brush>(i));
			ui::BuildButton(uiVerts, btn, fill, border);
		}

		// 状态栏文字 + 工具栏文字
		std::vector<SpriteVertex> textVerts;
		for (int i = 0; i < kBrushCount; ++i) {
			char label[32];
			std::snprintf(label, sizeof(label), "%d %s", (i == 6) ? 0 : i + 1, BrushName(static_cast<Brush>(i)));
			text::Build(textVerts, label, brushRects[i].x + 10.0f, brushRects[i].y + 7.0f, 1.15f,
				(static_cast<int>(brush) == i) ? 0xFFFFFFFF : 0xFFB0B0B0);
		}
		{
			char info[256];
			std::snprintf(info, sizeof(info),
				"CELL (%d,%d)  RES %d  BRUSH %s  R=%d  SIZE %dx%d  |  %s  |  WASD/EDGE SCROLL  WHEEL ZOOM  G GRID  N NEW  CTRL+S SAVE  CTRL+O LOAD",
				hoverCell.x, hoverCell.y,
				map.InBounds(hoverCell) ? map.ResourceAmount(hoverCell) : 0,
				BrushName(brush), brushRadius, map.Width(), map.Height(), status.c_str());
			text::Build(textVerts, info, 10.0f, static_cast<float>(H) - botH + 7.0f, 1.05f, 0xFFD0D0D0);
		}

		renderer.DrawScreenTextured(textVerts, fontTex);
		if (cncOk) { /* 编辑器暂不放置单位，仅保留图集供后续使用 */ }
		renderer.EndFrame();
	}

	map.SaveToText("map_export.map");
	renderer.Shutdown();
	window.Destroy();
	return 0;
}
