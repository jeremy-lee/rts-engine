// RTS Engine — render/tileset.cpp
#include "tileset.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rts {

bool TileSet::s_loaded = false;
int TileSet::s_tileCols = 128;
int TileSet::s_cellW = 24;
int TileSet::s_cellH = 24;
int TileSet::s_rows = 0;
int TileSet::s_totalTiles = 0;
std::vector<TileSet::Template> TileSet::s_templates;
std::unordered_map<int, int> TileSet::s_templateIndex;
std::vector<TileSet::TerrainType> TileSet::s_terrains;
std::unordered_map<std::string, int> TileSet::s_terrainIndex;
std::string TileSet::s_unknownTerrain = "Clear";

bool TileSet::Load(const std::string& metaPath) {
	std::ifstream in(metaPath);
	if (!in) {
		std::fprintf(stderr, "[tileset] cannot open %s\n", metaPath.c_str());
		return false;
	}
	s_templates.clear();
	s_templateIndex.clear();
	s_terrains.clear();
	s_terrainIndex.clear();

	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream iss(line);
		std::string key;
		iss >> key;
		if (key == "tilecols") {
			iss >> s_tileCols;
		} else if (key == "cell") {
			iss >> s_cellW >> s_cellH;
		} else if (key == "rows") {
			iss >> s_rows;
		} else if (key == "tiles") {
			iss >> s_totalTiles;
		} else if (key == "template") {
			Template t;
			iss >> t.id >> t.image >> t.sizeW >> t.sizeH >> t.frameBase >> t.frameCount;
			if (!iss.fail()) {
				s_templateIndex[t.id] = static_cast<int>(s_templates.size());
				s_templates.push_back(std::move(t));
			}
		} else if (key == "tileterrain") {
			int id = 0, index = 0;
			std::string terr;
			iss >> id >> index >> terr;
			auto it = s_templateIndex.find(id);
			if (it != s_templateIndex.end()) {
				s_templates[static_cast<size_t>(it->second)].terrain[index] = terr;
			}
		} else if (key == "terrain") {
			TerrainType tt;
			int walkable = 1;
			std::string colorHex;
			iss >> tt.name >> walkable >> colorHex;
			if (!iss.fail()) {
				tt.walkable = walkable != 0;
				tt.color = static_cast<uint32_t>(std::strtoul(colorHex.c_str(), nullptr, 16)) | 0xFF000000u;
				s_terrainIndex[tt.name] = static_cast<int>(s_terrains.size());
				s_terrains.push_back(std::move(tt));
			}
		}
		// image 行只用于生成，运行时不需要
	}
	std::sort(s_templates.begin(), s_templates.end(),
		[](const Template& a, const Template& b) { return a.id < b.id; });
	s_templateIndex.clear();
	for (size_t i = 0; i < s_templates.size(); ++i) {
		s_templateIndex[s_templates[i].id] = static_cast<int>(i);
	}
	s_loaded = !s_templates.empty();
	std::printf("[tileset] loaded %zu templates, %zu terrain types, atlas %dx%d\n",
		s_templates.size(), s_terrains.size(), s_tileCols, s_rows);
	return s_loaded;
}

const TileSet::Template* TileSet::Find(int templateId) {
	auto it = s_templateIndex.find(templateId);
	return it == s_templateIndex.end() ? nullptr : &s_templates[static_cast<size_t>(it->second)];
}

const TileSet::TerrainType* TileSet::FindTerrain(const std::string& name) {
	auto it = s_terrainIndex.find(name);
	return it == s_terrainIndex.end() ? nullptr : &s_terrains[static_cast<size_t>(it->second)];
}

bool TileSet::GetUV(int templateId, int index, float& u0, float& v0, float& u1, float& v1) {
	const Template* t = Find(templateId);
	if (t == nullptr || t->frameCount <= 0) {
		u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
		return false;
	}
	int idx = index;
	if (idx < 0) idx = 0;
	if (idx >= t->frameCount) idx = t->frameCount - 1;
	const int global = t->frameBase + idx;
	const int col = global % s_tileCols;
	const int row = global / s_tileCols;
	const float texW = static_cast<float>(s_tileCols * s_cellW);
	const float texH = static_cast<float>(s_rows * s_cellH);
	const float inset = 1.0f / 128.0f;
	u0 = (static_cast<float>(col * s_cellW) + inset) / texW;
	v0 = (static_cast<float>(row * s_cellH) + inset) / texH;
	u1 = (static_cast<float>((col + 1) * s_cellW) - inset) / texW;
	v1 = (static_cast<float>((row + 1) * s_cellH) - inset) / texH;
	return true;
}

const std::string& TileSet::TerrainOf(int templateId, int index) {
	const Template* t = Find(templateId);
	if (t == nullptr) return s_unknownTerrain;
	auto it = t->terrain.find(index);
	if (it != t->terrain.end()) return it->second;
	// 未声明的 index：回退到该模板已声明的第一个
	if (!t->terrain.empty()) return t->terrain.begin()->second;
	return s_unknownTerrain;
}

bool TileSet::Walkable(int templateId, int index) {
	const TerrainType* tt = FindTerrain(TerrainOf(templateId, index));
	return tt == nullptr ? true : tt->walkable;
}

} // namespace rts
