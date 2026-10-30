// RTS Engine — tools/map_editor.cpp
// 地图编辑器（完整版）：渲染后端与游戏完全一致
//   - 地形：TerrainGfx 程序化纹理（同 rts_game）
//   - 图集：CncAtlas 精灵（单位/建筑预览 + 放置）
//   - 相机：Camera（WASD / 边缘滚动 / 滚轮）
//   - UI：ui::BuildPanel/BuildButton + BitmapFont
//
// 操作：
//   左键拖动画布 = 绘制地形笔刷（或放置选中的图集单位）
//   1-6 / 0      = 地形笔刷（草地/水/岩石/沙地/高地/资源/橡皮）
//   右侧面板      = 点击图集精灵 → 进入放置模式
//   Q            = 返回地形模式（取消放置）
//   [ / ]        = 笔刷半径 -/+
//   右键点击      = 删除该格放置的单位
//   WASD/边缘/滚轮 = 相机
//   Ctrl+S       = 保存地图 + 实体（map_export.map / map_entities.txt）
//   Ctrl+O       = 加载（demo_map.map / map_entities.txt）
//   G（按住）     = 网格
//   N            = 新建地图
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <cctype>
#include <cstdlib>
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

TerrainKind BrushTerrain(Brush b) {
	switch (b) {
		case Brush::Water:    return TerrainKind::Water;
		case Brush::Rock:     return TerrainKind::Rock;
		case Brush::Sand:     return TerrainKind::Sand;
		case Brush::Highland: return TerrainKind::Highland;
		default:              return TerrainKind::Grass;
	}
}

void ApplyBrush(Map& map, CellCoord c, Brush b, int radius); // 前向声明

// Bresenham 连线：拖拽时从上一格到当前格逐格落笔，避免快速拖动漏格
void DrawBrushLine(Map& map, CellCoord from, CellCoord to, Brush b, int radius) {
	int32_t x0 = from.x, y0 = from.y;
	const int32_t x1 = to.x, y1 = to.y;
	const int32_t dx = std::abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
	const int32_t dy = -std::abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
	int32_t err = dx + dy;
	for (;;) {
		ApplyBrush(map, CellCoord(x0, y0), b, radius);
		if (x0 == x1 && y0 == y1) break;
		const int32_t e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

void ApplyBrush(Map& map, CellCoord c, Brush b, int radius) {
	if (!map.InBounds(c)) return;
	for (int32_t dy = -radius; dy <= radius; ++dy) {
		for (int32_t dx = -radius; dx <= radius; ++dx) {
			if (dx * dx + dy * dy > radius * radius) continue;
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

TerrainKind TerrainOf(const Map& map, CellCoord c) {
	if (map.Water(c)) return TerrainKind::Water;
	if (!map.Walkable(c)) return TerrainKind::Rock;
	if (map.TerrainHeight(c) > 0) return TerrainKind::Highland;
	return TerrainKind::Grass;
}

// 编辑器放置的实体（不进入 sim，纯编辑数据）。spriteIndex = CncAtlas 动态索引。
struct PlacedEntity {
	int spriteIndex = 0;
	uint8_t player = 0;
	CellCoord cell;
};

// ---- 拖放导入：把拖入的 PNG 作为新精灵追加到 assets/sprites.txt，然后调用 rts_import ----
// 返回 true 表示成功（调用方据此重载图集）。
bool AppendSpritesConfig(const std::vector<std::string>& files, std::string& status) {
	// 按文件名分组：prefix = 去掉 "-数字.png" 的部分
	struct Group {
		std::string prefix;
		std::string dir;
		int count = 0;
	};
	std::map<std::string, Group> groups;
	for (const auto& f : files) {
		const size_t slash = f.find_last_of("\\/");
		const std::string dir = (slash == std::string::npos) ? std::string(".") : f.substr(0, slash);
		std::string base = (slash == std::string::npos) ? f : f.substr(slash + 1);
		const size_t dot = base.find_last_of('.');
		const std::string ext = (dot == std::string::npos) ? std::string() : base.substr(dot);
		if (ext != ".png" && ext != ".PNG") continue;
		std::string stem = base.substr(0, dot);
		std::string prefix = stem;
		const size_t dash = stem.find_last_of('-');
		if (dash != std::string::npos) {
			bool digits = true;
			for (size_t i = dash + 1; i < stem.size(); ++i) {
				if (stem[i] < '0' || stem[i] > '9') { digits = false; break; }
			}
			if (digits) prefix = stem.substr(0, dash);
		}
		auto& g = groups[prefix];
		g.prefix = prefix;
		g.dir = dir;
		g.count++;
	}
	if (groups.empty()) {
		status = "Drop ignored: no PNG files";
		return false;
	}
	// 读现有配置
	std::vector<std::string> lines;
	int maxRow = 0;
	{
		std::ifstream in("assets/sprites.txt");
		std::string line;
		while (std::getline(in, line)) {
			lines.push_back(line);
			if (line.rfind("sprite ", 0) == 0) {
				int row = 0;
				if (std::sscanf(line.c_str() + 7, "%*s %*s %*d %*d %*s %*d %*d %*d %d", &row) == 1) {
					maxRow = std::max(maxRow, row + 2);
				}
			}
		}
		if (lines.empty()) {
			lines = {"cell 80 64", "cols 64", "team 2", "team0 70 90 255", "team1 235 60 60", ""};
		}
	}
	// 追加新精灵（单张 = 1 帧；多张 = 帧序列）
	int row = maxRow;
	int added = 0;
	for (const auto& [prefix, g] : groups) {
		// 精灵名：前缀大写（引擎 SpriteIdFromName 只认已知名；未知名会被忽略但仍生成图集）
		std::string name = prefix;
		for (char& ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		char lineBuf[256];
		// sprite <name> <prefix> <facings> <frames> <mode> <standStart> <runStart> <turretStart> <row>
		std::snprintf(lineBuf, sizeof(lineBuf), "sprite %s %s 1 %d building 0 -1 -1 %d",
			name.c_str(), prefix.c_str(), std::max(1, g.count), row);
		lines.push_back(lineBuf);
		row += 1;
		++added;
	}
	lines.push_back("");
	{
		std::ofstream out("assets/sprites.txt");
		for (const auto& l : lines) out << l << "\n";
	}
	status = "Import: " + std::to_string(added) + " sprite(s)";
	return true;
}

// 运行 rts_import（同步）
bool RunImportTool(const std::string& framesDir, std::string& status) {
	std::string cmd = "build\\rts_import.exe \"" + framesDir + "\" assets\\sprites.txt assets\\cnc_atlas.bmp assets\\cnc_atlas.txt";
	const int rc = std::system(cmd.c_str());
	if (rc != 0) {
		status = "rts_import failed (rc=" + std::to_string(rc) + ")";
		return false;
	}
	return true;
}

bool SaveEntities(const char* path, const std::vector<PlacedEntity>& list) {
	FILE* f = std::fopen(path, "w");
	if (!f) return false;
	std::fprintf(f, "RTSENT 2\n");
	for (const auto& e : list) {
		// 用精灵名保存（跨重启/重新导入稳定）
		std::fprintf(f, "%s %d %d %d\n", CncAtlas::NameAt(e.spriteIndex), static_cast<int>(e.player), e.cell.x, e.cell.y);
	}
	std::fclose(f);
	return true;
}

bool LoadEntities(const char* path, std::vector<PlacedEntity>& out) {
	FILE* f = std::fopen(path, "r");
	if (!f) return false;
	char magic[16] = {};
	if (std::fscanf(f, "%15s", magic) != 1) { std::fclose(f); return false; }
	if (std::strcmp(magic, "RTSENT") != 0) { std::fclose(f); return false; }
	int version = 0;
	if (std::fscanf(f, "%d", &version) != 1) { std::fclose(f); return false; }
	out.clear();
	char name[128] = {};
	int player = 0, cx = 0, cy = 0;
	if (version >= 2) {
		while (std::fscanf(f, "%127s %d %d %d", name, &player, &cx, &cy) == 4) {
			const int idx = CncAtlas::FindByName(name);
			if (idx < 0) continue;
			PlacedEntity e;
			e.spriteIndex = idx;
			e.player = static_cast<uint8_t>(player);
			e.cell = CellCoord(cx, cy);
			out.push_back(e);
		}
	} else {
		int sprite = 0;
		while (std::fscanf(f, "%d %d %d %d", &sprite, &player, &cx, &cy) == 4) {
			PlacedEntity e;
			e.spriteIndex = sprite;
			e.player = static_cast<uint8_t>(player);
			e.cell = CellCoord(cx, cy);
			out.push_back(e);
		}
	}
	std::fclose(f);
	return true;
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

	int fontW = 0, fontH = 0;
	const std::vector<uint8_t> fontPixels = BitmapFont::BuildTextureData(fontW, fontH);
	const uint32_t fontTex = renderer.UploadTexture(fontPixels, fontW, fontH);



	int terrW = 0, terrH = 0;
	std::vector<uint8_t> terrPixels;
	if (!TerrainGfx::Load("assets/terrain_atlas.png", terrPixels, terrW, terrH)) {
		terrPixels = TerrainGfx::Build(terrW, terrH); // 回退：程序化纯色
	}

	const uint32_t terrainTex = renderer.UploadTexture(terrPixels, terrW, terrH);



	CncAtlas::LoadMeta("assets/cnc_atlas.txt");

	std::vector<uint8_t> atlasPixels;
	int atlasW = 0, atlasH = 0;

	bool cncOk = CncAtlas::Load("assets/cnc_atlas.bmp", atlasPixels, atlasW, atlasH);

	uint32_t cncTex = cncOk ? renderer.UploadTexture(atlasPixels, atlasW, atlasH) : 0;


	Map map = Map::GenerateTestMap(128, 128, 2024);
	std::vector<PlacedEntity> placed;
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
	int selectedSprite = -1; // -1 = 地形模式
	bool showGrid = false;
	std::string status = "Ready";
	CellCoord hoverCell(-1, -1);

	const float topH = 40.0f;
	const float botH = 28.0f;
	const float panelW = 200.0f;

	while (window.PumpMessages()) {
		const int W = window.Width();
		const int H = window.Height();
		const float mx = static_cast<float>(window.MouseX());
		const float my = static_cast<float>(window.MouseY());

		// ---- 相机 ----
		const float pan = 520.0f * (1.0f / 60.0f) / camera.zoom;
		if (window.IsKeyDown('W') || window.IsKeyDown(VK_UP)) camera.centerY -= pan;
		if (window.IsKeyDown('S') || window.IsKeyDown(VK_DOWN)) camera.centerY += pan;
		if (window.IsKeyDown('A') || window.IsKeyDown(VK_LEFT)) camera.centerX -= pan;
		if (window.IsKeyDown('D') || window.IsKeyDown(VK_RIGHT)) camera.centerX += pan;
		// 边缘滚动已禁用：鼠标移出窗口时 Win32 报告的坐标会落在边缘，导致相机被推走。
		// 编辑器用 WASD/方向键平移即可（游戏内可选启用边缘滚动）。
		const int wheel = window.ConsumeWheel();
		if (wheel != 0) camera.ZoomAt((wheel > 0) ? 1.1f : 1.0f / 1.1f, mx, my);
		camera.viewW = W;
		camera.viewH = H;

		// ---- 拖放导入（PNG 拖进窗口）----
		{
			const auto dropped = window.ConsumeDroppedFiles();
			if (!dropped.empty()) {
				std::string msg;
				if (AppendSpritesConfig(dropped, msg)) {
					// 用第一个文件的目录作为帧目录
					const std::string& first = dropped.front();
					const size_t slash = first.find_last_of("\\/");
					const std::string dir = (slash == std::string::npos) ? std::string(".") : first.substr(0, slash);
					if (!RunImportTool(dir, msg)) {
						status = msg;
					} else {
						// 重载图集（纹理 + 元数据）
					
	CncAtlas::LoadMeta("assets/cnc_atlas.txt");

						std::vector<uint8_t> px;
						int w = 0, h = 0;
						if (CncAtlas::Load("assets/cnc_atlas.bmp", px, w, h)) {
							const uint32_t newTex = renderer.UploadTexture(px, w, h);
							if (newTex != 0) {
								glDeleteTextures(1, reinterpret_cast<const GLuint*>(&cncTex));
								cncTex = newTex;
								cncOk = true;
							}
						}
						status = msg;
					}
				} else {
					status = msg;
				}
			}
		}

		// ---- 笔刷切换 ----
		static const int keyMap[kBrushCount] = {'1', '2', '3', '4', '5', '6', '0'};
		for (int i = 0; i < kBrushCount; ++i) {
			if (window.IsKeyDown(keyMap[i])) { brush = static_cast<Brush>(i); selectedSprite = -1; }
		}
		if (window.IsKeyDown('Q')) selectedSprite = -1;
		if (window.IsKeyDown('[') && brushRadius > 0) --brushRadius;
		if (window.IsKeyDown(']') && brushRadius < 8) ++brushRadius;

		// ---- 悬停格 ----
		{
			float wx = 0, wy = 0;
			camera.ScreenToWorld(mx, my, wx, wy);
			hoverCell = ScreenToCell(ScreenPos{static_cast<int32_t>(wx), static_cast<int32_t>(wy)});
		}

		// ---- 布局 ----
		const float canvasW = static_cast<float>(W) - panelW;
		const bool inCanvas = (mx < canvasW && my > topH && my < static_cast<float>(H) - botH);
		const bool inPanel = (mx >= canvasW && my > topH && my < static_cast<float>(H) - botH);

		// ---- 图集面板（右侧，动态：只显示有元数据的精灵，包含运行时导入的）----
		std::vector<int> panelSprites;
		for (int i = 0; i < CncAtlas::Count(); ++i) {
			int w = 0, h = 0;
			CncAtlas::GetSizeAt(i, w, h);
			if (w > 0 && h > 0) panelSprites.push_back(i);
		}
		const int spriteCount = static_cast<int>(panelSprites.size());
		std::vector<ui::Rect> spriteRects(static_cast<size_t>(std::max(0, spriteCount)));
		{
			const float itemH = 52.0f;
			for (int i = 0; i < spriteCount; ++i) {
				const float y0 = topH + 8.0f + static_cast<float>(i) * (itemH + 6.0f);
				spriteRects[static_cast<size_t>(i)] = ui::Rect{canvasW + 8.0f, y0, panelW - 16.0f, itemH};
			}
		}

		// ---- 左键：面板选择 / 画布绘制 ----
		static bool prevLeft = false;
		static CellCoord lastBrushCell(-99999, -99999);
		const bool leftNow = window.IsMouseDown(0);
		if (!leftNow) lastBrushCell = CellCoord(-99999, -99999);
		if (leftNow) {
			if (inPanel) {
				for (int i = 0; i < spriteCount; ++i) {
					if (spriteRects[static_cast<size_t>(i)].Contains(mx, my)) {
						selectedSprite = panelSprites[static_cast<size_t>(i)];
						status = std::string("Place: ") + CncAtlas::NameAt(selectedSprite);
					}
				}
			} else if (inCanvas && map.InBounds(hoverCell)) {
				if (selectedSprite >= 0) {
					// 放置（避免重叠：同格只放一个）
					bool exists = false;
					for (const auto& e : placed) {
						if (e.cell.x == hoverCell.x && e.cell.y == hoverCell.y) { exists = true; break; }
					}
					if (!exists) {
						PlacedEntity e;
						e.spriteIndex = selectedSprite;
						e.player = 0;
						e.cell = hoverCell;
						placed.push_back(e);
					}
				} else {
					// 地形笔刷：首次落笔画单格，之后从上一格 Bresenham 连到当前格
					if (lastBrushCell.x == -99999 || lastBrushCell.y == -99999) {
						ApplyBrush(map, hoverCell, brush, brushRadius);
					} else if (lastBrushCell.x != hoverCell.x || lastBrushCell.y != hoverCell.y) {
						DrawBrushLine(map, lastBrushCell, hoverCell, brush, brushRadius);
					}
					lastBrushCell = hoverCell;
				}
			}
		}
		// 右键：删除放置物
		if (window.IsMouseDown(1) && inCanvas) {
			for (size_t i = 0; i < placed.size(); ++i) {
				if (placed[i].cell.x == hoverCell.x && placed[i].cell.y == hoverCell.y) {
					placed.erase(placed.begin() + static_cast<ptrdiff_t>(i));
					status = "Deleted entity";
					break;
				}
			}
		}
		prevLeft = leftNow;

		// ---- 保存/加载 ----
		{
			static bool ctrlSPrev = false, ctrlOPrev = false;
			const bool ctrl = window.IsKeyDown(VK_CONTROL);
			const bool sNow = window.IsKeyDown('S');
			const bool oNow = window.IsKeyDown('O');
			if (ctrl && sNow && !ctrlSPrev) {
				const bool okMap = map.SaveToText("map_export.map");
				const bool okEnt = SaveEntities("map_entities.txt", placed);
				status = (okMap && okEnt) ? "Saved map_export.map + map_entities.txt" : "Save failed";
			}
			if (ctrl && oNow && !ctrlOPrev) {
				const bool okMap = map.LoadFromText("demo_map.map");
				const bool okEnt = LoadEntities("map_entities.txt", placed);
				status = okMap ? (okEnt ? "Loaded map + entities" : "Loaded map (no entities)")
					: (okEnt ? "Loaded entities (no map)" : "Load failed");
			}
			ctrlSPrev = sNow;
			ctrlOPrev = oNow;
		}
		if (window.IsKeyDown('G')) showGrid = true;
		if (window.IsKeyDown('N')) {
			map = Map::GenerateTestMap(128, 128, static_cast<uint64_t>(hoverCell.x * 31 + hoverCell.y * 17 + 1));
			placed.clear();
			status = "New map 128x128";
		}

		// ---- 渲染：地形 + 放置物 ----
		SpriteBatch terrainBatch;
		SpriteBatch entityBatch;
		terrainBatch.Begin();
		entityBatch.Begin();
		{
			// 视口裁剪：把画布四角逆投影到格子坐标，取包围范围（等距投影下不能直接除以 kTileW）
			std::vector<CellCoord> cells;
			{
				float vx0 = 0, vy0 = 0, vx1 = 0, vy1 = 0;
				camera.ScreenToWorld(0.0f, topH, vx0, vy0);
				camera.ScreenToWorld(static_cast<float>(W) - panelW, static_cast<float>(H) - botH, vx1, vy1);
				const CellCoord c00 = ScreenToCell(ScreenPos{static_cast<int32_t>(vx0), static_cast<int32_t>(vy0)});
				const CellCoord c10 = ScreenToCell(ScreenPos{static_cast<int32_t>(vx1), static_cast<int32_t>(vy0)});
				const CellCoord c01 = ScreenToCell(ScreenPos{static_cast<int32_t>(vx0), static_cast<int32_t>(vy1)});
				const CellCoord c11 = ScreenToCell(ScreenPos{static_cast<int32_t>(vx1), static_cast<int32_t>(vy1)});
				const int32_t x0 = std::max(0, std::min(std::min(c00.x, c10.x), std::min(c01.x, c11.x)) - 4);
				const int32_t x1 = std::min(map.Width() - 1, std::max(std::max(c00.x, c10.x), std::max(c01.x, c11.x)) + 4);
				const int32_t y0 = std::max(0, std::min(std::min(c00.y, c10.y), std::min(c01.y, c11.y)) - 4);
				const int32_t y1 = std::min(map.Height() - 1, std::max(std::max(c00.y, c10.y), std::max(c01.y, c11.y)) + 4);
				if (x1 >= x0 && y1 >= y0) {
					cells.reserve(static_cast<size_t>(x1 - x0 + 1) * static_cast<size_t>(y1 - y0 + 1));
					for (int32_t y = y0; y <= y1; ++y)
						for (int32_t x = x0; x <= x1; ++x) cells.push_back(CellCoord(x, y));
				}
			}
			std::sort(cells.begin(), cells.end(), [](const CellCoord& a, const CellCoord& b) {
				return (a.x + a.y) < (b.x + b.y);
			});
			for (const CellCoord& c : cells) {
				if (!map.InBounds(c)) continue;
				const ScreenPos p = ProjectCell(c, map.TerrainHeight(c));
				const TerrainKind tk = TerrainOf(map, c);
				const float tw = static_cast<float>(TerrainGfx::kRenderW);
				const float th = static_cast<float>(TerrainGfx::kRenderH);
				// 白色 tint：颜色来自 TerrainGfx 纹理（顶点色 × 纹理色会双重着色变黑）
				float tu0 = 0, tv0 = 0, tu1 = 1, tv1 = 1;
				TerrainGfx::GetUVAt(TerrainGfx::TileFor(tk, c.x, c.y), tu0, tv0, tu1, tv1);
				terrainBatch.AddSprite(static_cast<float>(p.x) - tw * 0.5f,
					static_cast<float>(p.y) - th * 0.5f, tw, th, tu0, tv0, tu1, tv1, 0xFFFFFFFFu);
				if (map.ResourceAmount(c) > 0) {
					terrainBatch.AddSolid(static_cast<float>(p.x) - 8.0f,
						static_cast<float>(p.y) - 4.0f, 16.0f, 8.0f, 0xE0D4A017u);
				}
			}
			if (showGrid) {
				for (const CellCoord& c : cells) {
					if (!map.InBounds(c)) continue;
					const ScreenPos p = ProjectCell(c, map.TerrainHeight(c));
					terrainBatch.AddSolid(static_cast<float>(p.x) - 1.0f,
						static_cast<float>(p.y) - static_cast<float>(kTileH) * 0.5f, 1.0f,
						static_cast<float>(kTileH), 0x30FFFFFFu);
				}
			}
			// 放置的实体（C&C 精灵，按 x+y 排序）
			if (cncOk) {
				std::vector<PlacedEntity> sorted = placed;
				std::sort(sorted.begin(), sorted.end(), [](const PlacedEntity& a, const PlacedEntity& b) {
					return (a.cell.x + a.cell.y) < (b.cell.x + b.cell.y);
				});
				for (const auto& e : sorted) {
					float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
					CncAtlas::GetUVAt(e.spriteIndex, e.player, WAngle(0), 0, u0, v0, u1, v1);
					int sw = 0, sh = 0;
					CncAtlas::GetSizeAt(e.spriteIndex, sw, sh);
					const ScreenPos p = ProjectCell(e.cell, map.TerrainHeight(e.cell));
					entityBatch.AddSprite(static_cast<float>(p.x) - static_cast<float>(sw) * 0.5f,
						static_cast<float>(p.y) - static_cast<float>(sh) * 0.5f,
						static_cast<float>(sw), static_cast<float>(sh), u0, v0, u1, v1, 0xFFFFFFFFu);
				}
			}
			// 悬停高亮
			if (map.InBounds(hoverCell) && inCanvas) {
				const ScreenPos p = ProjectCell(hoverCell, map.TerrainHeight(hoverCell));
				const uint32_t hc = (selectedSprite >= 0) ? 0x80FFD040u : 0x80FFFFFFu;
				terrainBatch.AddSolid(static_cast<float>(p.x) - static_cast<float>(kTileW) * 0.5f,
					static_cast<float>(p.y) - static_cast<float>(kTileH) * 0.5f,
					static_cast<float>(kTileW), static_cast<float>(kTileH), hc);
				terrainBatch.AddSolid(static_cast<float>(p.x) - 8.0f, static_cast<float>(p.y) - 1.0f, 16.0f, 2.0f, 0xFFFF4040u);
				terrainBatch.AddSolid(static_cast<float>(p.x) - 1.0f, static_cast<float>(p.y) - 8.0f, 2.0f, 16.0f, 0xFFFF4040u);
			}
		}
		terrainBatch.End();
		entityBatch.End();

		renderer.BeginFrame(W, H);
		{
			static int dbgN = 0;
			if (dbgN < 2) {
				std::printf("[ed] terrTex=%u terrainVerts=%zu entityVerts=%zu cam=(%.0f,%.0f) zoom=%.2f tex=%dx%d\n",
					terrainTex, terrainBatch.Vertices().size(), entityBatch.Vertices().size(),
					camera.centerX, camera.centerY, camera.zoom, terrW, terrH);
				std::fflush(stdout);
				++dbgN;
			}
		}
		renderer.DrawTexturedBatch(terrainBatch.Vertices(), camera, terrainTex);
		if (cncOk && !entityBatch.Vertices().empty()) renderer.DrawTexturedBatch(entityBatch.Vertices(), camera, cncTex);

		// ---- UI：顶部工具栏 + 右侧图集面板 + 底部状态栏 ----
		std::vector<SpriteVertex> uiVerts;
		ui::BuildPanel(uiVerts, ui::Rect{0, 0, static_cast<float>(W), topH}, 0xEE202020);
		ui::BuildPanel(uiVerts, ui::Rect{0, static_cast<float>(H) - botH, static_cast<float>(W), botH}, 0xEE202020);
		ui::BuildPanel(uiVerts, ui::Rect{canvasW, topH, panelW, static_cast<float>(H) - topH - botH}, 0xEE181818);

		// 地形笔刷按钮
		const float btnW = 82.0f;
		const float btnH = 26.0f;
		const float btnY = (topH - btnH) * 0.5f;
		ui::Rect brushRects[kBrushCount];
		for (int i = 0; i < kBrushCount; ++i) {
			const float bx = 8.0f + static_cast<float>(i) * (btnW + 6.0f);
			brushRects[i] = ui::Rect{bx, btnY, btnW, btnH};
			const bool active = (selectedSprite < 0 && static_cast<int>(brush) == i);
			const uint32_t fill = active ? 0xFF3A6A3A : 0xFF2E2E2E;
			const uint32_t border = active ? 0xFF80FF80 : 0xFF505050;
			ui::Button btn;
			btn.rect = brushRects[i];
			btn.label = BrushName(static_cast<Brush>(i));
			ui::BuildButton(uiVerts, btn, fill, border);
		}

		// 图集面板项（背景 + 选中高亮）
		for (int i = 0; i < spriteCount; ++i) {
			const bool active = (selectedSprite == i);
			const ui::Rect& r = spriteRects[static_cast<size_t>(i)];
			ui::BuildPanel(uiVerts, r, active ? 0xFF3A5A7A : 0xFF262626);
			ui::BuildPanel(uiVerts, ui::Rect{r.x, r.y, r.w, 1.0f}, active ? 0xFF80C0FF : 0xFF404040);
		}

		// 图集图标（屏幕坐标精灵，cncTex）
		std::vector<SpriteVertex> iconVerts;
		if (cncOk) {
			for (int i = 0; i < spriteCount; ++i) {
				const int si = panelSprites[static_cast<size_t>(i)];
				float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
				CncAtlas::GetUVAt(si, 0, WAngle(0), 0, u0, v0, u1, v1);
				const ui::Rect& r = spriteRects[static_cast<size_t>(i)];
				const float iconSize = 44.0f;
				const float ix = r.x + 8.0f;
				const float iy = r.y + (r.h - iconSize) * 0.5f;
				SpriteVertex v[6];
				BuildQuad(ix, iy, iconSize, iconSize, u0, v0, u1, v1, 0xFFFFFFFFu, v);
				iconVerts.insert(iconVerts.end(), v, v + 6);
			}
		}

		// 文字
		std::vector<SpriteVertex> textVerts;
		for (int i = 0; i < kBrushCount; ++i) {
			char label[32];
			std::snprintf(label, sizeof(label), "%d %s", (i == 6) ? 0 : i + 1, BrushName(static_cast<Brush>(i)));
			text::Build(textVerts, label, brushRects[i].x + 8.0f, brushRects[i].y + 7.0f, 1.0f,
				(selectedSprite < 0 && static_cast<int>(brush) == i) ? 0xFFFFFFFF : 0xFFB0B0B0);
		}
		text::Build(textVerts, "ATLAS  (click to place, Q=terrain)", canvasW + 10.0f, topH + 8.0f, 1.0f, 0xFF80C0FF);
		for (int i = 0; i < spriteCount; ++i) {
			const ui::Rect& r = spriteRects[static_cast<size_t>(i)];
			const int si = panelSprites[static_cast<size_t>(i)];
			text::Build(textVerts, CncAtlas::NameAt(si), r.x + 60.0f, r.y + 20.0f, 1.0f,
				(selectedSprite == si) ? 0xFFFFFFFF : 0xFFB0B0B0);
		}
		{
			char info[512];
			std::snprintf(info, sizeof(info),
				"CELL (%d,%d)  RES %d  MODE %s  BRUSH_R %d  SIZE %dx%d  ENTITIES %zu  |  %s  |  Q terrain  RMB delete  [ ] radius  G grid  N new  CTRL+S save  CTRL+O load",
				hoverCell.x, hoverCell.y,
				map.InBounds(hoverCell) ? map.ResourceAmount(hoverCell) : 0,
				(selectedSprite >= 0) ? CncAtlas::NameAt(selectedSprite) : BrushName(brush),
				brushRadius, map.Width(), map.Height(), placed.size(), status.c_str());
			text::Build(textVerts, info, 10.0f, static_cast<float>(H) - botH + 7.0f, 0.95f, 0xFFD0D0D0);
		}

		if (!uiVerts.empty()) renderer.DrawScreenBatch(uiVerts);
		if (!iconVerts.empty()) renderer.DrawScreenTextured(iconVerts, cncTex);
		renderer.DrawScreenTextured(textVerts, fontTex);
		renderer.EndFrame();
	}

	map.SaveToText("map_export.map");
	SaveEntities("map_entities.txt", placed);
	renderer.Shutdown();
	window.Destroy();
	return 0;
}
