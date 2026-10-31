// RTS Engine — render/tileset.h
// 地形集（OpenRA tileset 的等价物）：模板表 + 图集 UV + 地面属性。
//
// 支持多套 tileset（RA 有 TEMPERAT/SNOW/DESERT/INTERIOR）：
//   每套由 assets/tileset_<NAME>.txt + .bmp 描述（tools/build_tileset.cpp 生成）。
//   LoadSet 载入若干套，UseSet 切换当前使用的一套（地图的 TilesetName 决定）。
//
// 关键概念（对齐 OpenRA）：
//   - 地图每格存 (templateId, index)：
//       templateId = 模板（对应一个 TMP 文件，如 255 → clear1.tem）
//       index      = 该模板下的子格序号（多格模板时是 y*sizeW + x）
//   - 模板有 Size（可占多格，如 sh01.tem 是 4×5）
//   - 每个 (templateId, index) 有自己的 TerrainType，TerrainType 决定可走性
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
		int frameBase = 0;
		int frameCount = 0;
		std::unordered_map<int, std::string> terrain; // index → TerrainType 名
	};

	struct TerrainType {
		std::string name;
		bool walkable = true;
		uint32_t color = 0xFF000000u;
	};

	// 一套 tileset 的完整数据
	struct Set {
		std::string name;
		std::string bitmapPath; // tileset_<NAME>.bmp
		int tileCols = 128;
		int cellW = 24;
		int cellH = 24;
		int rows = 0;
		int totalTiles = 0;
		std::vector<Template> templates;
		std::unordered_map<int, int> templateIndex;
		std::vector<TerrainType> terrains;
		std::unordered_map<std::string, int> terrainIndex;
	};

	// ---- 载入 / 切换 ----
	static bool LoadSet(const std::string& name, const std::string& metaPath);
	static bool HasSet(const std::string& name);
	static bool UseSet(const std::string& name);
	static const std::string& CurrentName();
	static const std::string& CurrentBitmapPath();
	static const std::vector<std::string>& SetNames();

	static bool Loaded() { return Current() != nullptr; }

	// ---- 当前套的查询 ----
	static int TileCols();
	static int CellW();
	static int CellH();
	static int Rows();
	static int TotalTiles();
	static const Template* Find(int templateId);
	static const TerrainType* FindTerrain(const std::string& name);
	static const std::vector<Template>& AllTemplates();
	static const std::vector<TerrainType>& AllTerrains();
	static bool GetUV(int templateId, int index, float& u0, float& v0, float& u1, float& v1);
	static const std::string& TerrainOf(int templateId, int index);
	static bool Walkable(int templateId, int index);

private:
	static Set* Current();
	static const Set* CurrentConst();
	static std::unordered_map<std::string, Set> s_sets;
	static std::vector<std::string> s_setNames;
	static std::string s_current;
	static std::string s_emptyString;
};

} // namespace rts
