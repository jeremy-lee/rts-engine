// RTS Engine â€” tools/map_editor.cpp
// åœ°å›¾ç¼–è¾‘å™¨ï¼ˆå®Œæ•´ç‰ˆï¼‰ï¼šæ¸²æŸ“åŽç«¯ä¸Žæ¸¸æˆå®Œå…¨ä¸€è‡´
//   - åœ°å½¢ï¼šTerrainGfx ç¨‹åºåŒ–çº¹ç†ï¼ˆåŒ rts_gameï¼‰
//   - å›¾é›†ï¼šCncAtlas ç²¾çµï¼ˆå•ä½/å»ºç­‘é¢„è§ˆ + æ”¾ç½®ï¼‰
//   - ç›¸æœºï¼šCameraï¼ˆWASD / è¾¹ç¼˜æ»šåŠ¨ / æ»šè½®ï¼‰
//   - UIï¼šui::BuildPanel/BuildButton + BitmapFont
//
// æ“ä½œï¼š
//   å·¦é”®æ‹–åŠ¨ç”»å¸ƒ = ç»˜åˆ¶åœ°å½¢ç¬”åˆ·ï¼ˆæˆ–æ”¾ç½®é€‰ä¸­çš„å›¾é›†å•ä½ï¼‰
//   1-6 / 0      = åœ°å½¢ç¬”åˆ·ï¼ˆè‰åœ°/æ°´/å²©çŸ³/æ²™åœ°/é«˜åœ°/èµ„æº/æ©¡çš®ï¼‰
//   å³ä¾§é¢æ¿      = ç‚¹å‡»å›¾é›†ç²¾çµ â†’ è¿›å…¥æ”¾ç½®æ¨¡å¼
//   Q            = è¿”å›žåœ°å½¢æ¨¡å¼ï¼ˆå–æ¶ˆæ”¾ç½®ï¼‰
//   [ / ]        = ç¬”åˆ·åŠå¾„ -/+
//   å³é”®ç‚¹å‡»      = åˆ é™¤è¯¥æ ¼æ”¾ç½®çš„å•ä½
//   WASD/è¾¹ç¼˜/æ»šè½® = ç›¸æœº
//   Ctrl+S       = ä¿å­˜åœ°å›¾ + å®žä½“ï¼ˆmap_export.map / map_entities.txtï¼‰
//   Ctrl+O       = åŠ è½½ï¼ˆdemo_map.map / map_entities.txtï¼‰
//   Gï¼ˆæŒ‰ä½ï¼‰     = ç½‘æ ¼
//   N            = æ–°å»ºåœ°å›¾
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <cctype>
#include <cstdlib>
#include <vector>

#include "core/core.h"
#include "core/platform.h"
#include "sim/map.h"
#include "render/projection.h"
#include "render/sprite_batch.h"
#include "render/camera.h"
#include "render/gl_renderer.h"
#include "render/terrain_gfx.h"
#include "render/tileset.h"
#include "render/render_graph.h"
#include "render/cnc_assets.h"
#include "render/text.h"
#include "render/ui.h"
#include "render/screenshot.h"
#include "render/rhi.h"
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

// ç”¨æˆ·åœ¨å›¾é›†é¢æ¿é‡Œé€‰ä¸­çš„å…·ä½“æ¨¡æ¿ï¼ˆ-1 = ç”¨åœ°å½¢ç¬”åˆ·æ¨¡å¼ï¼‰
static int g_selectedTemplate = -1;
static int g_selectedIndex = 0;

// åˆ·ä¸€ä¸ªå…·ä½“æ¨¡æ¿ï¼štile æ•°æ® + ç”± tileset æ´¾ç”Ÿå‡ºçš„é€šè¡Œ/å¯å»ºé€ æ ‡å¿—
// æŠŠæ¨¡æ¿æ”¾åœ¨ originï¼šæ¨¡æ¿å¯èƒ½å  NÃ—M æ ¼ï¼ˆsh01.tem æ˜¯ 4Ã—5ï¼Œw2.tem æ˜¯ 2Ã—2ï¼‰ï¼Œ
// ä¸€æ¬¡è¦†ç›– NÃ—M å¹¶åœ¨æ¯æ ¼å†™å…¥å¯¹åº”çš„å­æ ¼ indexï¼ˆè¡Œä¼˜å…ˆï¼šy*sizeW + xï¼‰ï¼Œä¸Ž OpenRA çš„
// TileTemplate.Size / TerrainTile(type,index) è¯­ä¹‰ä¸€è‡´ã€‚
void PlaceTemplate(Map& map, CellCoord origin, int templateId, int variant) {
	const TileSet::Template* t = TileSet::Find(templateId);
	const int sw = (t != nullptr && t->sizeW > 0) ? t->sizeW : 1;
	const int sh = (t != nullptr && t->sizeH > 0) ? t->sizeH : 1;
	// variant åªå¯¹ 1Ã—1 æ¨¡æ¿æœ‰æ„ä¹‰ï¼ˆå¤šæ ¼æ¨¡æ¿çš„ index å°±æ˜¯å­æ ¼åºå·ï¼‰
	const bool multi = (sw > 1 || sh > 1) && t != nullptr && t->frameCount >= sw * sh;
	for (int dy = 0; dy < sh; ++dy) {
		for (int dx = 0; dx < sw; ++dx) {
			const CellCoord c(origin.x + dx, origin.y + dy);
			if (!map.InBounds(c)) continue;
			const int index = multi ? (dy * sw + dx) : variant;
			map.SetTile(c, static_cast<uint16_t>(templateId),
				static_cast<uint8_t>(index < 0 ? 0 : (index > 255 ? 255 : index)));
			const std::string& terr = TileSet::TerrainOf(templateId, index);
			uint8_t flags = kWalkable | kBuildable;
			if (terr == "Water" || terr == "River") flags = kWater;
			else if (!TileSet::Walkable(templateId, index)) flags = kBuildable;
			map.SetFlags(c, flags);
		}
	}
}

// å•æ ¼åˆ·ï¼ˆåŠå¾„å†…çš„æ¯ä¸€æ ¼éƒ½æ”¾ä¸€ä¸ªæ¨¡æ¿ï¼›å¤šæ ¼æ¨¡æ¿ä¼šæŒ‰è‡ªèº«å°ºå¯¸è¦†ç›–ï¼‰
void ApplyTemplateBrush(Map& map, CellCoord c, int templateId, int index) {
	PlaceTemplate(map, c, templateId, index);
}

void ApplyBrush(Map& map, CellCoord c, Brush b, int radius); // å‰å‘å£°æ˜Ž

// Bresenham è¿žçº¿ï¼šæ‹–æ‹½æ—¶ä»Žä¸Šä¸€æ ¼åˆ°å½“å‰æ ¼é€æ ¼è½ç¬”ï¼Œé¿å…å¿«é€Ÿæ‹–åŠ¨æ¼æ ¼
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
			if (g_selectedTemplate >= 0) {
				// ç›´æŽ¥åˆ·ç”¨æˆ·åœ¨å›¾é›†é¢æ¿é‡Œé€‰ä¸­çš„å…·ä½“æ¨¡æ¿ï¼ˆtemplateId + å˜ä½“ indexï¼‰
				ApplyTemplateBrush(map, cell, g_selectedTemplate, g_selectedIndex);
				continue;
			}
			switch (b) {
				case Brush::Grass:
					// clear1ï¼ˆæ¨¡æ¿ 255ï¼‰ï¼šéšæœºæŒ‘ä¸€ä¸ªå˜ä½“ï¼Œä¿å­˜åŽå˜ä½“ä¸ä¼šä¸¢ï¼ˆindex å­˜åœ¨åœ°å›¾é‡Œï¼‰
					map.SetTile(cell, 255, static_cast<uint8_t>((cell.x * 7 + cell.y * 13) & 0x0F));
					map.SetFlags(cell, kWalkable | kBuildable);
					map.SetTerrainHeight(cell, 0);
					break;
				case Brush::Water:
					map.SetTile(cell, 1, 0); // w1.tem
					map.SetFlags(cell, kWater);
					map.SetTerrainHeight(cell, 0);
					break;
				case Brush::Rock:
					map.SetTile(cell, 103, 0); // p01.temï¼ˆRockï¼‰
					map.SetFlags(cell, kBuildable);
					map.SetTerrainHeight(cell, 0);
					break;
				case Brush::Sand:
					map.SetTile(cell, 255, 3); // å¤ç”¨ clear1 çš„ä¸€ä¸ªå˜ä½“
					map.SetFlags(cell, kWalkable | kBuildable);
					map.SetTerrainHeight(cell, 0);
					break;
				case Brush::Highland:
					map.SetTile(cell, 255, static_cast<uint8_t>((cell.x + cell.y) & 0x0F));
					map.SetFlags(cell, kWalkable | kBuildable);
					map.SetTerrainHeight(cell, 2);
					break;
				case Brush::Resource:
					map.SetTile(cell, 255, 0);
					map.SetResourceAmount(cell, 2000);
					map.SetFlags(cell, kWalkable | kBuildable);
					map.SetTerrainHeight(cell, 0);
					break;
				case Brush::Eraser:
					map.SetTile(cell, 255, 0);
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

// ç¼–è¾‘å™¨æ”¾ç½®çš„å®žä½“ï¼ˆä¸è¿›å…¥ simï¼Œçº¯ç¼–è¾‘æ•°æ®ï¼‰ã€‚spriteIndex = CncAtlas åŠ¨æ€ç´¢å¼•ã€‚
struct PlacedEntity {
	int spriteIndex = 0;
	uint8_t player = 0;
	CellCoord cell;
};

// ---- æ‹–æ”¾å¯¼å…¥ï¼šæŠŠæ‹–å…¥çš„ PNG ä½œä¸ºæ–°ç²¾çµè¿½åŠ åˆ° assets/sprites.txtï¼Œç„¶åŽè°ƒç”¨ rts_import ----
// è¿”å›ž true è¡¨ç¤ºæˆåŠŸï¼ˆè°ƒç”¨æ–¹æ®æ­¤é‡è½½å›¾é›†ï¼‰ã€‚
bool AppendSpritesConfig(const std::vector<std::string>& files, std::string& status) {
	// æŒ‰æ–‡ä»¶ååˆ†ç»„ï¼šprefix = åŽ»æŽ‰ "-æ•°å­—.png" çš„éƒ¨åˆ†
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
	// è¯»çŽ°æœ‰é…ç½®
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
	// è¿½åŠ æ–°ç²¾çµï¼ˆå•å¼  = 1 å¸§ï¼›å¤šå¼  = å¸§åºåˆ—ï¼‰
	int row = maxRow;
	int added = 0;
	for (const auto& [prefix, g] : groups) {
		// ç²¾çµåï¼šå‰ç¼€å¤§å†™ï¼ˆå¼•æ“Ž SpriteIdFromName åªè®¤å·²çŸ¥åï¼›æœªçŸ¥åä¼šè¢«å¿½ç•¥ä½†ä»ç”Ÿæˆå›¾é›†ï¼‰
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

// è¿è¡Œ rts_importï¼ˆåŒæ­¥ï¼‰
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
		// ç”¨ç²¾çµåä¿å­˜ï¼ˆè·¨é‡å¯/é‡æ–°å¯¼å…¥ç¨³å®šï¼‰
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

int main(int argc, char** argv) {
	// 关键：不设工作目录的话，从别的目录启动会找不到 assets/，图集加载失败直接回退成
	// 程序化条纹纹理（这正是"地面全是竖条纹"的原因）。
	SetWorkingDirToExe();
	// --autoshot：跑若干帧后存一张 in-game 截图（避开外部截屏的 DPI 干扰）
	bool autoShot = false;
	std::string cliMapPath2;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--autoshot") autoShot = true;
		else if (!a.empty() && a[0] != '-') cliMapPath2 = a;
	}
	// å¯é€‰ï¼šå‘½ä»¤è¡Œç›´æŽ¥æŒ‡å®šè¦åŠ è½½çš„åœ°å›¾ï¼ˆçœå¾—æ¨¡æ‹ŸæŒ‰é”®ï¼‰
	std::string cliMapPath;
	if (argc > 1) cliMapPath = argv[1];

	Win32Window window;
	if (!window.Create("RTS Engine â€” Map Editor", 1400, 800)) return 1;

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
	// å¤šå¥— tilesetï¼šå…¨éƒ¨è½½å…¥ï¼ŒæŒ‰åœ°å›¾çš„ TilesetName åˆ‡æ¢ï¼ˆç¼ºçœ TEMPERATï¼‰
	TileSet::LoadSet("TEMPERAT", "assets/tileset_TEMPERAT.txt");
	TileSet::LoadSet("SNOW", "assets/tileset_SNOW.txt");
	TileSet::LoadSet("INTERIOR", "assets/tileset_INTERIOR.txt");
	TileSet::UseSet("TEMPERAT");
	if (!TerrainGfx::Load(TileSet::CurrentBitmapPath(), terrPixels, terrW, terrH)) {
		terrPixels = TerrainGfx::Build(terrW, terrH); // å›žé€€ï¼šç¨‹åºåŒ–çº¯è‰²
	}

	uint32_t terrainTex = renderer.UploadTexture(terrPixels, terrW, terrH);



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
	int selectedSprite = -1; // -1 = åœ°å½¢æ¨¡å¼
	int panelTab = 0;        // 0 = UNITSï¼ˆC&C å•ä½å›¾é›†ï¼‰, 1 = TERRAINï¼ˆtileset æ¨¡æ¿ï¼‰

	// åœ°å½¢é¢æ¿çš„å€™é€‰é¡¹ï¼šæ¯ç§ TerrainType å–ç¬¬ä¸€ä¸ª 1Ã—1 æ¨¡æ¿ä½œä¸ºä»£è¡¨
	struct TerrainChoice {
		int templateId = 0;
		std::string label;
	};
	std::vector<TerrainChoice> terrainChoices;
	if (TileSet::Loaded()) {
		for (const auto& tt : TileSet::AllTerrains()) {
			for (const auto& t : TileSet::AllTemplates()) {
				if (t.sizeW != 1 || t.sizeH != 1) continue;
				auto it = t.terrain.find(0);
				if (it != t.terrain.end() && it->second == tt.name) {
					terrainChoices.push_back(TerrainChoice{t.id, tt.name});
					break;
				}
			}
		}
	}
	bool showGrid = false;
	std::string status = "Ready";

	// å‘½ä»¤è¡ŒæŒ‡å®šåœ°å›¾ï¼šç›´æŽ¥åŠ è½½ï¼ˆçœå¾—æ¨¡æ‹Ÿ Ctrl+Oï¼‰
	if (!cliMapPath.empty()) {
		if (map.LoadBinary(cliMapPath)) {
			// meta / entities ä»Žåœ°å›¾åæŽ¨å¯¼ï¼ˆfoo.map â†’ foo_meta.txt / foo_actors.txtï¼‰
			const size_t dot = cliMapPath.find_last_of('.');
			const std::string base = (dot == std::string::npos) ? cliMapPath : cliMapPath.substr(0, dot);
			map.LoadMeta(base + "_meta.txt");
			LoadEntities((base + "_actors.txt").c_str(), placed);
			if (TileSet::UseSet(map.TilesetName())) {
				std::vector<uint8_t> px;
				int tw = 0, th = 0;
				if (TerrainGfx::Load(TileSet::CurrentBitmapPath(), px, tw, th)) {
					const uint32_t newTex = renderer.UploadTexture(px, tw, th);
					if (newTex != 0) {
						glDeleteTextures(1, reinterpret_cast<const GLuint*>(&terrainTex));
						terrainTex = newTex;
					}
				}
			}
			status = "Loaded " + cliMapPath;
			std::printf("[editor] loaded %s (%dx%d, tileset=%s)\n", cliMapPath.c_str(),
				map.Width(), map.Height(), map.TilesetName().c_str());
			std::fflush(stdout);
		} else {
			std::fprintf(stderr, "[editor] failed to load %s\n", cliMapPath.c_str());
		}
	}
	CellCoord hoverCell(-1, -1);

	const float topH = 40.0f;
	const float botH = 28.0f;
	const float panelW = 200.0f;

	while (window.PumpMessages()) {
		const int W = window.Width();
		const int H = window.Height();
		const float mx = static_cast<float>(window.MouseX());
		const float my = static_cast<float>(window.MouseY());

		// ---- ç›¸æœº ----
		const float pan = 520.0f * (1.0f / 60.0f) / camera.zoom;
		if (window.IsKeyDown('W') || window.IsKeyDown(VK_UP)) camera.centerY -= pan;
		if (window.IsKeyDown('S') || window.IsKeyDown(VK_DOWN)) camera.centerY += pan;
		if (window.IsKeyDown('A') || window.IsKeyDown(VK_LEFT)) camera.centerX -= pan;
		if (window.IsKeyDown('D') || window.IsKeyDown(VK_RIGHT)) camera.centerX += pan;
		// è¾¹ç¼˜æ»šåŠ¨å·²ç¦ç”¨ï¼šé¼ æ ‡ç§»å‡ºçª—å£æ—¶ Win32 æŠ¥å‘Šçš„åæ ‡ä¼šè½åœ¨è¾¹ç¼˜ï¼Œå¯¼è‡´ç›¸æœºè¢«æŽ¨èµ°ã€‚
		// ç¼–è¾‘å™¨ç”¨ WASD/æ–¹å‘é”®å¹³ç§»å³å¯ï¼ˆæ¸¸æˆå†…å¯é€‰å¯ç”¨è¾¹ç¼˜æ»šåŠ¨ï¼‰ã€‚
		const int wheel = window.ConsumeWheel();
		if (wheel != 0) camera.ZoomAt((wheel > 0) ? 1.1f : 1.0f / 1.1f, mx, my);
		camera.viewW = W;
		camera.viewH = H;

		// ---- æ‹–æ”¾å¯¼å…¥ï¼ˆPNG æ‹–è¿›çª—å£ï¼‰----
		{
			const auto dropped = window.ConsumeDroppedFiles();
			if (!dropped.empty()) {
				std::string msg;
				if (AppendSpritesConfig(dropped, msg)) {
					// ç”¨ç¬¬ä¸€ä¸ªæ–‡ä»¶çš„ç›®å½•ä½œä¸ºå¸§ç›®å½•
					const std::string& first = dropped.front();
					const size_t slash = first.find_last_of("\\/");
					const std::string dir = (slash == std::string::npos) ? std::string(".") : first.substr(0, slash);
					if (!RunImportTool(dir, msg)) {
						status = msg;
					} else {
						// é‡è½½å›¾é›†ï¼ˆçº¹ç† + å…ƒæ•°æ®ï¼‰
					
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

		// ---- ç¬”åˆ·åˆ‡æ¢ ----
		static const int keyMap[kBrushCount] = {'1', '2', '3', '4', '5', '6', '0'};
		for (int i = 0; i < kBrushCount; ++i) {
			if (window.IsKeyDown(keyMap[i])) { brush = static_cast<Brush>(i); selectedSprite = -1; }
		}
		if (window.IsKeyDown('Q')) selectedSprite = -1;
		if (window.IsKeyDown('[') && brushRadius > 0) --brushRadius;
		if (window.IsKeyDown(']') && brushRadius < 8) ++brushRadius;

		// ---- æ‚¬åœæ ¼ ----
		{
			float wx = 0, wy = 0;
			camera.ScreenToWorld(mx, my, wx, wy);
			hoverCell = ScreenToCell(ScreenPos{static_cast<int32_t>(wx), static_cast<int32_t>(wy)});
		}

		// ---- å¸ƒå±€ ----
		const float canvasW = static_cast<float>(W) - panelW;
		const bool inCanvas = (mx < canvasW && my > topH && my < static_cast<float>(H) - botH);
		const bool inPanel = (mx >= canvasW && my > topH && my < static_cast<float>(H) - botH);

		// ---- å›¾é›†é¢æ¿ï¼ˆå³ä¾§ï¼ŒåŠ¨æ€ï¼šåªæ˜¾ç¤ºæœ‰å…ƒæ•°æ®çš„ç²¾çµï¼ŒåŒ…å«è¿è¡Œæ—¶å¯¼å…¥çš„ï¼‰----
		std::vector<int> panelSprites;
		for (int i = 0; i < CncAtlas::Count(); ++i) {
			int w = 0, h = 0;
			CncAtlas::GetSizeAt(i, w, h);
			if (w > 0 && h > 0) panelSprites.push_back(i);
		}
		const int spriteCount = static_cast<int>(panelSprites.size());
		// é¢æ¿ tab æ ï¼ˆUNITS / TERRAINï¼‰
		const float tabH = 26.0f;
		const ui::Rect tabUnits{canvasW + 8.0f, topH + 6.0f, (panelW - 20.0f) * 0.5f, tabH};
		const ui::Rect tabTerrain{tabUnits.x + tabUnits.w + 4.0f, topH + 6.0f, (panelW - 20.0f) * 0.5f, tabH};
		const float contentTop = topH + 6.0f + tabH + 8.0f;

		std::vector<ui::Rect> spriteRects(static_cast<size_t>(std::max(0, spriteCount)));
		{
			const float itemH = 52.0f;
			for (int i = 0; i < spriteCount; ++i) {
				const float y0 = contentTop + static_cast<float>(i) * (itemH + 6.0f);
				spriteRects[static_cast<size_t>(i)] = ui::Rect{canvasW + 8.0f, y0, panelW - 16.0f, itemH};
			}
		}
		// TERRAIN é¢æ¿ï¼š3 åˆ—ç½‘æ ¼ï¼Œæ¯é¡¹ 62Ã—62ï¼ˆç¼©ç•¥å›¾ + åå­—ï¼‰
		const int terrainCount = static_cast<int>(terrainChoices.size());
		const float terrCellW = (panelW - 24.0f) / 3.0f;
		const float terrCellH = 62.0f;
		std::vector<ui::Rect> terrainRects(static_cast<size_t>(std::max(0, terrainCount)));
		for (int i = 0; i < terrainCount; ++i) {
			const int col = i % 3;
			const int row = i / 3;
			terrainRects[static_cast<size_t>(i)] = ui::Rect{
				canvasW + 8.0f + static_cast<float>(col) * terrCellW,
				contentTop + static_cast<float>(row) * (terrCellH + 4.0f),
				terrCellW - 4.0f, terrCellH};
		}

		// ---- å·¦é”®ï¼šé¢æ¿é€‰æ‹© / ç”»å¸ƒç»˜åˆ¶ ----
		static bool prevLeft = false;
		static CellCoord lastBrushCell(-99999, -99999);
		const bool leftNow = window.IsMouseDown(0);
		if (!leftNow) lastBrushCell = CellCoord(-99999, -99999);
		if (leftNow) {
			if (inPanel) {
				// tab åˆ‡æ¢
				if (tabUnits.Contains(mx, my)) {
					panelTab = 0;
					g_selectedTemplate = -1; // å›žåˆ°å•ä½æ¨¡å¼
				} else if (tabTerrain.Contains(mx, my)) {
					panelTab = 1;
					selectedSprite = -1; // å›žåˆ°åœ°å½¢æ¨¡å¼
				}
				if (panelTab == 0) {
					for (int i = 0; i < spriteCount; ++i) {
						if (spriteRects[static_cast<size_t>(i)].Contains(mx, my)) {
							selectedSprite = panelSprites[static_cast<size_t>(i)];
							g_selectedTemplate = -1;
							status = std::string("Place: ") + CncAtlas::NameAt(selectedSprite);
						}
					}
				} else {
					for (int i = 0; i < terrainCount; ++i) {
						if (terrainRects[static_cast<size_t>(i)].Contains(mx, my)) {
							g_selectedTemplate = terrainChoices[static_cast<size_t>(i)].templateId;
							g_selectedIndex = 0;
							selectedSprite = -1;
							status = "Terrain: " + terrainChoices[static_cast<size_t>(i)].label +
								" (template " + std::to_string(g_selectedTemplate) + ")";
						}
					}
				}
			} else if (inCanvas && map.InBounds(hoverCell)) {
				if (selectedSprite >= 0) {
					// æ”¾ç½®ï¼ˆé¿å…é‡å ï¼šåŒæ ¼åªæ”¾ä¸€ä¸ªï¼‰
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
					// åœ°å½¢ç¬”åˆ·ï¼šé¦–æ¬¡è½ç¬”ç”»å•æ ¼ï¼Œä¹‹åŽä»Žä¸Šä¸€æ ¼ Bresenham è¿žåˆ°å½“å‰æ ¼
					if (lastBrushCell.x == -99999 || lastBrushCell.y == -99999) {
						ApplyBrush(map, hoverCell, brush, brushRadius);
					} else if (lastBrushCell.x != hoverCell.x || lastBrushCell.y != hoverCell.y) {
						DrawBrushLine(map, lastBrushCell, hoverCell, brush, brushRadius);
					}
					lastBrushCell = hoverCell;
				}
			}
		}
		// å³é”®ï¼šåˆ é™¤æ”¾ç½®ç‰©
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

		// ---- ä¿å­˜/åŠ è½½ ----
		{
			static bool ctrlSPrev = false, ctrlOPrev = false;
			const bool ctrl = window.IsKeyDown(VK_CONTROL);
			const bool sNow = window.IsKeyDown('S');
			const bool oNow = window.IsKeyDown('O');
			if (ctrl && sNow && !ctrlSPrev) {
				// äºŒè¿›åˆ¶ï¼ˆä¸Ž OpenRA map.bin åŒæž„ï¼šå¤´ 17B + tiles/heights/resources ä¸‰æ®µï¼‰
				const bool okMap = map.SaveBinary("map_export.map");
				const bool okMeta = map.SaveMeta("map_meta.txt");
				const bool okEnt = SaveEntities("map_entities.txt", placed);
				status = (okMap && okMeta && okEnt)
					? "Saved map_export.map (binary) + map_meta.txt + map_entities.txt" : "Save failed";
			}
			if (ctrl && oNow && !ctrlOPrev) {
				bool okMap = map.LoadBinary("map_export.map");
				if (!okMap) okMap = map.LoadFromText("map_export.map"); // å…¼å®¹æ—§çš„æ–‡æœ¬åœ°å›¾
				map.LoadMeta("map_meta.txt"); // bounds / tileset
				// æŒ‰åœ°å›¾çš„ tileset åˆ‡æ¢å›¾é›†ï¼šéœ€è¦é‡æ–°è¯» bmp å¹¶é‡ä¼ çº¹ç†
				// æ³¨æ„ï¼šHasSet ä¸åšåˆ«åè§£æžï¼ˆWINTERâ†’SNOWï¼‰ï¼Œæ‰€ä»¥è¿™é‡Œåªç”¨ UseSet
				if (TileSet::UseSet(map.TilesetName())) {
					std::vector<uint8_t> px;
					int tw = 0, th = 0;
					if (TerrainGfx::Load(TileSet::CurrentBitmapPath(), px, tw, th)) {
						const uint32_t newTex = renderer.UploadTexture(px, tw, th);
						if (newTex != 0) {
							glDeleteTextures(1, reinterpret_cast<const GLuint*>(&terrainTex));
							terrainTex = newTex;
							terrW = tw;
							terrH = th;
						}
					}
				}
				const bool okEnt = LoadEntities("map_entities.txt", placed);
				status = okMap ? (okEnt ? "Loaded map + entities" : "Loaded map (no entities)")
					: (okEnt ? "Loaded entities (no map)" : "Load failed");
			}
			ctrlSPrev = sNow;
			ctrlOPrev = oNow;
		}
		if (window.IsKeyDown('G')) showGrid = true;
		if (autoShot) showGrid = true; // 截图时强制打开网格
		if (window.IsKeyDown('N')) {
			map = Map::GenerateTestMap(128, 128, static_cast<uint64_t>(hoverCell.x * 31 + hoverCell.y * 17 + 1));
			placed.clear();
			status = "New map 128x128";
		}

		// ---- æ¸²æŸ“ï¼šåœ°å½¢ + æ”¾ç½®ç‰© ----
		SpriteBatch terrainBatch;
		SpriteBatch entityBatch;
		terrainBatch.Begin();
		entityBatch.Begin();
		{
			// è§†å£è£å‰ªï¼šæŠŠç”»å¸ƒå››è§’é€†æŠ•å½±åˆ°æ ¼å­åæ ‡ï¼Œå–åŒ…å›´èŒƒå›´ï¼ˆç­‰è·æŠ•å½±ä¸‹ä¸èƒ½ç›´æŽ¥é™¤ä»¥ kTileWï¼‰
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
				// ç”»å®¶ç®—æ³•ï¼šç­‰è·æŠ•å½±ä¸‹å±å¹• Y = (x + y) * kStepYï¼Œå¿…é¡»æŒ‰ x+y æŽ’åºï¼Œ
			// è¿™æ ·åŽç”»çš„ï¼ˆæ›´é ä¸‹çš„ï¼‰tile æ‰èƒ½æ­£ç¡®è¦†ç›–å‰ä¸€ä¸ª tile çš„ä¸‹åŠéƒ¨åˆ†ã€‚
			// 正交网格：画家算法按屏幕 Y（= 格 y）从上到下
			if (a.y != b.y) return a.y < b.y;
			return a.x < b.x;
			});
			for (const CellCoord& c : cells) {
				if (!map.InBounds(c)) continue;
				const ScreenPos p = ProjectCell(c, map.TerrainHeight(c));
				const TerrainKind tk = TerrainOf(map, c);
				const float tw = static_cast<float>(TerrainGfx::kRenderW);
				const float th = static_cast<float>(TerrainGfx::kRenderH);
				// åœ°å›¾é‡Œå­˜çš„ (templateId, index) â†’ tileset å›¾é›† UV
				const TileRef tr = map.Tile(c);
				float tu0 = 0, tv0 = 0, tu1 = 1, tv1 = 1;
				TileSet::GetUV(tr.templateId, tr.index, tu0, tv0, tu1, tv1);
				terrainBatch.AddSprite(static_cast<float>(p.x) - tw * 0.5f,
					static_cast<float>(p.y) - th * 0.5f, tw, th, tu0, tv0, tu1, tv1,
					TerrainGfx::TintFor(tk));
				if (map.ResourceAmount(c) > 0)
					terrainBatch.AddSolid(static_cast<float>(p.x) - 8.0f,
						static_cast<float>(p.y) - 4.0f, 16.0f, 8.0f, 0xE0D4A017u);
			}
			{
				// TEMP 诊断：对比候选格数 / 有效格数 / 实际入批次的 sprite 数
				static int dbgCnt = 0;
				if (dbgCnt < 3) {
					size_t valid = 0;
					for (const CellCoord& c2 : cells) {
						if (map.InBounds(c2)) ++valid;
					}
					std::fprintf(stderr, "[ed] cells=%zu valid=%zu sprites=%zu verts=%zu\n",
						cells.size(), valid, terrainBatch.SpriteCount(),
						terrainBatch.Vertices().size());
					std::fflush(stderr);
					++dbgCnt;
				}
			}
			// debug 网格：完整格子边框（4 条边），洋红色。有框但内部露清屏色 = 该格没画上。
			if (showGrid) {
				const uint32_t gridCol = 0xFFFF00FFu;
				for (const CellCoord& c : cells) {
					if (!map.InBounds(c)) continue;
					const ScreenPos p = ProjectCell(c, map.TerrainHeight(c));
					const float gx = static_cast<float>(p.x) - static_cast<float>(kTileW) * 0.5f;
					const float gy = static_cast<float>(p.y) - static_cast<float>(kTileH) * 0.5f;
					const float gw = static_cast<float>(kTileW);
					const float gh = static_cast<float>(kTileH);
					terrainBatch.AddSolid(gx, gy, gw, 1.0f, gridCol);
					terrainBatch.AddSolid(gx, gy + gh - 1.0f, gw, 1.0f, gridCol);
					terrainBatch.AddSolid(gx, gy, 1.0f, gh, gridCol);
					terrainBatch.AddSolid(gx + gw - 1.0f, gy, 1.0f, gh, gridCol);
				}
			}
			// æ”¾ç½®çš„å®žä½“ï¼ˆC&C ç²¾çµï¼ŒæŒ‰ x+y æŽ’åºï¼‰
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
			// æ‚¬åœé«˜äº®
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
		// ---- å¸§å›¾ï¼ˆUE5 RDG é£Žæ ¼ï¼‰ï¼šWorld â†’ UI â†’ Icons â†’ Text ----
		RenderGraph graph;
		graph.AddPass("World", [&](PassContext& ctx) {
			renderer.DrawTexturedBatch(terrainBatch.Vertices(), camera, terrainTex); // 地形用混合：菱形 tile 的透明角要透出下层
			ctx.stats.drawCalls = 1;
			ctx.stats.vertices = static_cast<uint32_t>(terrainBatch.Vertices().size());
			if (cncOk && !entityBatch.Vertices().empty()) {
				renderer.DrawTexturedBatch(entityBatch.Vertices(), camera, cncTex);
				ctx.stats.drawCalls += 1;
				ctx.stats.vertices += static_cast<uint32_t>(entityBatch.Vertices().size());
			}
		});

		// ---- UIï¼šé¡¶éƒ¨å·¥å…·æ  + å³ä¾§å›¾é›†é¢æ¿ + åº•éƒ¨çŠ¶æ€æ  ----
		std::vector<SpriteVertex> uiVerts;
		ui::BuildPanel(uiVerts, ui::Rect{0, 0, static_cast<float>(W), topH}, 0xEE202020);
		ui::BuildPanel(uiVerts, ui::Rect{0, static_cast<float>(H) - botH, static_cast<float>(W), botH}, 0xEE202020);
		ui::BuildPanel(uiVerts, ui::Rect{canvasW, topH, panelW, static_cast<float>(H) - topH - botH}, 0xEE181818);

		// åœ°å½¢ç¬”åˆ·æŒ‰é’®
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

		// å›¾é›†é¢æ¿é¡¹ï¼ˆèƒŒæ™¯ + é€‰ä¸­é«˜äº®ï¼‰
		if (panelTab == 0) {
			for (int i = 0; i < spriteCount; ++i) {
				const bool active = (selectedSprite == panelSprites[static_cast<size_t>(i)]);
				const ui::Rect& r = spriteRects[static_cast<size_t>(i)];
				ui::BuildPanel(uiVerts, r, active ? 0xFF3A5A7A : 0xFF262626);
				ui::BuildPanel(uiVerts, ui::Rect{r.x, r.y, r.w, 1.0f}, active ? 0xFF80C0FF : 0xFF404040);
			}
		} else {
			for (int i = 0; i < terrainCount; ++i) {
				const bool active = (g_selectedTemplate == terrainChoices[static_cast<size_t>(i)].templateId);
				const ui::Rect& r = terrainRects[static_cast<size_t>(i)];
				ui::BuildPanel(uiVerts, r, active ? 0xFF3A5A7A : 0xFF262626);
				ui::BuildPanel(uiVerts, ui::Rect{r.x, r.y, r.w, 1.0f}, active ? 0xFF80C0FF : 0xFF404040);
			}
		}

		// å›¾é›†å›¾æ ‡ï¼ˆå±å¹•åæ ‡ç²¾çµï¼ŒcncTexï¼‰
		std::vector<SpriteVertex> iconVerts;
		std::vector<SpriteVertex> terrainIconVerts;
		if (cncOk && panelTab == 0) {
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
		if (panelTab == 1 && TileSet::Loaded()) {
			// åœ°å½¢ç¼©ç•¥å›¾èµ° tileset å›¾é›†ï¼ˆterrTexï¼‰ï¼Œä¸èƒ½æ··è¿›å•ä½å›¾é›†çš„ iconVerts
			for (int i = 0; i < terrainCount; ++i) {
				const auto& tc = terrainChoices[static_cast<size_t>(i)];
				float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
				TileSet::GetUV(tc.templateId, 0, u0, v0, u1, v1);
				const ui::Rect& r = terrainRects[static_cast<size_t>(i)];
				const float iconSize = 40.0f;
				SpriteVertex v[6];
				BuildQuad(r.x + (r.w - iconSize) * 0.5f, r.y + 4.0f, iconSize, iconSize,
					u0, v0, u1, v1, 0xFFFFFFFFu, v);
				terrainIconVerts.insert(terrainIconVerts.end(), v, v + 6);
			}
		}

		// æ–‡å­—
		std::vector<SpriteVertex> textVerts;
		for (int i = 0; i < kBrushCount; ++i) {
			char label[32];
			std::snprintf(label, sizeof(label), "%d %s", (i == 6) ? 0 : i + 1, BrushName(static_cast<Brush>(i)));
			text::Build(textVerts, label, brushRects[i].x + 8.0f, brushRects[i].y + 7.0f, 1.0f,
				(selectedSprite < 0 && static_cast<int>(brush) == i) ? 0xFFFFFFFF : 0xFFB0B0B0);
		}
		// tab æ 
		ui::BuildPanel(uiVerts, tabUnits, (panelTab == 0) ? 0xFF3A5A7A : 0xFF262626);
		ui::BuildPanel(uiVerts, tabTerrain, (panelTab == 1) ? 0xFF3A5A7A : 0xFF262626);
		text::Build(textVerts, "UNITS", tabUnits.x + 12.0f, tabUnits.y + 17.0f, 1.0f,
			(panelTab == 0) ? 0xFFFFFFFF : 0xFFA0A0A0);
		text::Build(textVerts, "TERRAIN", tabTerrain.x + 12.0f, tabTerrain.y + 17.0f, 1.0f,
			(panelTab == 1) ? 0xFFFFFFFF : 0xFFA0A0A0);

		if (panelTab == 0) {
			text::Build(textVerts, "C&C UNITS (click to place, Q=terrain)", canvasW + 10.0f, contentTop + 6.0f, 0.9f, 0xFF80C0FF);
			for (int i = 0; i < spriteCount; ++i) {
				const ui::Rect& r = spriteRects[static_cast<size_t>(i)];
				const int si = panelSprites[static_cast<size_t>(i)];
				text::Build(textVerts, CncAtlas::NameAt(si), r.x + 60.0f, r.y + 20.0f, 1.0f,
					(selectedSprite == si) ? 0xFFFFFFFF : 0xFFB0B0B0);
			}
		} else {
			text::Build(textVerts, "TERRAIN TILES (click to paint)", canvasW + 10.0f, contentTop + 6.0f, 0.9f, 0xFF80C0FF);
			for (int i = 0; i < terrainCount; ++i) {
				const ui::Rect& r = terrainRects[static_cast<size_t>(i)];
				const bool active = (g_selectedTemplate == terrainChoices[static_cast<size_t>(i)].templateId);
				text::Build(textVerts, terrainChoices[static_cast<size_t>(i)].label,
					r.x + 4.0f, r.y + r.h - 6.0f, 0.85f, active ? 0xFFFFFFFF : 0xFFB0B0B0);
			}
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

		graph.AddPass("UI", [&](PassContext& ctx) {
			if (!uiVerts.empty()) renderer.DrawScreenBatch(uiVerts);
			ctx.stats.drawCalls = uiVerts.empty() ? 0u : 1u;
			ctx.stats.vertices = static_cast<uint32_t>(uiVerts.size());
		});
		graph.AddPass("Icons", [&](PassContext& ctx) {
			ctx.stats.drawCalls = 0;
			ctx.stats.vertices = 0;
			if (!iconVerts.empty()) {
				renderer.DrawScreenTextured(iconVerts, cncTex);
				ctx.stats.drawCalls += 1;
				ctx.stats.vertices += static_cast<uint32_t>(iconVerts.size());
			}
			if (!terrainIconVerts.empty()) {
				renderer.DrawScreenTextured(terrainIconVerts, terrainTex);
				ctx.stats.drawCalls += 1;
				ctx.stats.vertices += static_cast<uint32_t>(terrainIconVerts.size());
			}
		});
		graph.AddPass("Text", [&](PassContext& ctx) {
			renderer.DrawScreenTextured(textVerts, fontTex);
			ctx.stats.drawCalls = 1;
			ctx.stats.vertices = static_cast<uint32_t>(textVerts.size());
		});
		graph.Execute(W, H, 0.0f);
		renderer.EndFrame();
	if (autoShot) {
		static int shotFrames = 0;
		if (++shotFrames == 30) {
			CaptureScreenshot(static_cast<int>(W), static_cast<int>(H), "editor_shot.bmp");
			// 只统计地形区（避开顶部 tab 栏 36px、底部状态栏 44px、右侧面板 210px）
			AnalyzeFrameClearColour(static_cast<int>(W), static_cast<int>(H));
			autoShot = false;
		}
	}
	}

	map.SaveBinary("map_export.map");
	map.SaveMeta("map_meta.txt");
	SaveEntities("map_entities.txt", placed);
	renderer.Shutdown();
	window.Destroy();
	return 0;
}
