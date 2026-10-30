// RTS Engine — render/tileset.h
// 地形集（OpenRA tileset 的等价物）：模板表 + 图集 UV + 地面属性。
//
// 数据来源：assets/tileset.txt（由 tools/build_tileset.cpp 从 OpenRA 的
// mods/ra/tilesets/temperat.yaml + temperat.mix 生成）。
//
// 关键概念（对齐 OpenRA）：
//   - 地图每个格子存的是 (templateId, index)：
//       templateId = 模板（对应一个 TMP 文件，如 255 → clear1.tem）
//       index      = 该模板下的变体号（TMP frame 序号）
//   - 每个 (templateId, index) 有自己的 TerrainType（Clear/Water/Rock/...），
//     TerrainType 决定可走性等属性。
//   - 模板有 Size（可占多格，如 3×3 的桥）。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rts {

class TileSet {
public:
	struct Template {
		int id = 0;
		std::string image;
		int sizeW = 1;
		int sizeH = 1;
		int frameBase = 0;  // 该模板第 0 帧在图集里的全局 tile 序号
		int frameCount = 0;
		std::unordered_map<int, std::string> terrain; // index → TerrainType 名
	};

	struct TerrainType {
		std::string name;
		bool walkable = true;
		uint32_t color = 0xFF000000u;
	};

	// 加载元数据（tileset.txt）。重复调用会覆盖。
	static bool Load(const std::string& metaPath);
	static bool Loaded() { return s_loaded; }

	static int TileCols() { return s_tileCols; }
	static int CellW() { return s_cellW; }
	static int CellH() { return s_cellH; }
	static int Rows() { return s_rows; }
	static int TotalTiles() { return s_totalTiles; }

	// 查找模板（不存在返回 nullptr）
	static const Template* Find(int templateId);
	static const TerrainType* FindTerrain(const std::string& name);

	// 编辑器面板用：所有模板（按 id 排序）
	static const std::vector<Template>& AllTemplates() { return s_templates; }
	static const std::vector<TerrainType>& AllTerrains() { return s_terrains; }

	// (templateId, index) 的图集 UV；失败返回 false
	static bool GetUV(int templateId, int index, float& u0, float& v0, float& u1, float& v1);

	// (templateId, index) 的 TerrainType 名（未定义时回退到模板的 0 号 index）
	static const std::string& TerrainOf(int templateId, int index);

	// 可走性（未知地形按可走处理）
	static bool Walkable(int templateId, int index);

private:
	static bool s_loaded;
	static int s_tileCols;
	static int s_cellW, s_cellH;
	static int s_rows;
	static int s_totalTiles;
	static std::vector<Template> s_templates;
	static std::unordered_map<int, int> s_templateIndex; // id → s_templates 下标
	static std::vector<TerrainType> s_terrains;
	static std::unordered_map<std::string, int> s_terrainIndex;
	static std::string s_unknownTerrain;
};

} // namespace rts
