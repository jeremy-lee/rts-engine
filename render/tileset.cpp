// RTS Engine — render/tileset.cpp
#include "tileset.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rts {

std::unordered_map<std::string, TileSet::Set> TileSet::s_sets;
std::vector<std::string> TileSet::s_setNames;
std::string TileSet::s_current;
std::string TileSet::s_emptyString;

TileSet::Set* TileSet::Current() {
	auto it = s_sets.find(s_current);
	return it == s_sets.end() ? nullptr : &it->second;
}

const TileSet::Set* TileSet::CurrentConst() {
	auto it = s_sets.find(s_current);
	return it == s_sets.end() ? nullptr : &it->second;
}

bool TileSet::LoadSet(const std::string& name, const std::string& metaPath) {
	std::ifstream in(metaPath);
	if (!in) {
		std::fprintf(stderr, "[tileset] cannot open %s\n", metaPath.c_str());
		return false;
	}
	Set set;
	set.name = name;
	// 图集路径约定：元数据同目录下的 tileset_<NAME>.bmp
	{
		const size_t slash = metaPath.find_last_of("\\/");
		const std::string dir = (slash == std::string::npos) ? std::string(".") : metaPath.substr(0, slash);
		set.bitmapPath = dir + "/tileset_" + name + ".bmp";
	}

	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream iss(line);
		std::string key;
		iss >> key;
		if (key == "tilecols") {
			iss >> set.tileCols;
		} else if (key == "cell") {
			iss >> set.cellW >> set.cellH;
		} else if (key == "rows") {
			iss >> set.rows;
		} else if (key == "tiles") {
			iss >> set.totalTiles;
		} else if (key == "template") {
			Template t;
			iss >> t.id >> t.image >> t.sizeW >> t.sizeH >> t.frameBase >> t.frameCount;
			if (!iss.fail()) {
				set.templateIndex[t.id] = static_cast<int>(set.templates.size());
				set.templates.push_back(std::move(t));
			}
		} else if (key == "tileterrain") {
			int id = 0, index = 0;
			std::string terr;
			iss >> id >> index >> terr;
			auto it = set.templateIndex.find(id);
			if (it != set.templateIndex.end()) {
				set.templates[static_cast<size_t>(it->second)].terrain[index] = terr;
			}
		} else if (key == "terrain") {
			TerrainType tt;
			int walkable = 1;
			std::string colorHex;
			iss >> tt.name >> walkable >> colorHex;
			if (!iss.fail()) {
				tt.walkable = walkable != 0;
				tt.color = static_cast<uint32_t>(std::strtoul(colorHex.c_str(), nullptr, 16)) | 0xFF000000u;
				set.terrainIndex[tt.name] = static_cast<int>(set.terrains.size());
				set.terrains.push_back(std::move(tt));
			}
		}
	}
	if (set.templates.empty()) {
		std::fprintf(stderr, "[tileset] %s has no templates\n", metaPath.c_str());
		return false;
	}
	std::sort(set.templates.begin(), set.templates.end(),
		[](const Template& a, const Template& b) { return a.id < b.id; });
	set.templateIndex.clear();
	for (size_t i = 0; i < set.templates.size(); ++i) {
		set.templateIndex[set.templates[i].id] = static_cast<int>(i);
	}

	std::printf("[tileset] set %s: %zu templates, %zu terrain types, atlas cols=%d rows=%d\n",
		name.c_str(), set.templates.size(), set.terrains.size(),
		set.tileCols, set.rows);
	std::fflush(stdout);

	if (s_sets.find(name) == s_sets.end()) s_setNames.push_back(name);
	s_sets[name] = std::move(set);
	if (s_current.empty()) s_current = name;
	return true;
}

bool TileSet::HasSet(const std::string& name) {
	return s_sets.find(name) != s_sets.end();
}

bool TileSet::UseSet(const std::string& name) {
	if (s_sets.find(name) != s_sets.end()) {
		s_current = name;
		return true;
	}
	// C&C 与 RA 的 tileset 命名不同（C&C: WINTER/TEMPERATE/DESERT，RA: SNOW/TEMPERAT/DESERT），
	// 素材是同一套，这里做别名映射。
	static const std::unordered_map<std::string, std::string> kAliases = {
		{"WINTER", "SNOW"},
		{"TEMPERATE", "TEMPERAT"},
		{"DESERT", "TEMPERAT"}, // 我们没导入 desert.mix，退到温带
		{"SNOW", "SNOW"},
		{"TEMPERAT", "TEMPERAT"},
		{"INTERIOR", "INTERIOR"},
	};
	auto it = kAliases.find(name);
	if (it != kAliases.end()) {
		if (s_sets.find(it->second) != s_sets.end()) {
			s_current = it->second;
			std::printf("[tileset] %s -> %s (alias)\n", name.c_str(), it->second.c_str());
			std::fflush(stdout);
			return true;
		}
	}
	return false;
}

const std::string& TileSet::CurrentName() {
	return s_current.empty() ? s_emptyString : s_current;
}

const std::string& TileSet::CurrentBitmapPath() {
	const Set* s = CurrentConst();
	return s == nullptr ? s_emptyString : s->bitmapPath;
}

const std::vector<std::string>& TileSet::SetNames() { return s_setNames; }

int TileSet::TileCols() {
	const Set* s = CurrentConst();
	return s == nullptr ? 128 : s->tileCols;
}
int TileSet::CellW() {
	const Set* s = CurrentConst();
	return s == nullptr ? 24 : s->cellW;
}
int TileSet::CellH() {
	const Set* s = CurrentConst();
	return s == nullptr ? 24 : s->cellH;
}
int TileSet::Rows() {
	const Set* s = CurrentConst();
	return s == nullptr ? 0 : s->rows;
}
int TileSet::TotalTiles() {
	const Set* s = CurrentConst();
	return s == nullptr ? 0 : s->totalTiles;
}

const TileSet::Template* TileSet::Find(int templateId) {
	const Set* s = CurrentConst();
	if (s == nullptr) return nullptr;
	auto it = s->templateIndex.find(templateId);
	return it == s->templateIndex.end() ? nullptr : &s->templates[static_cast<size_t>(it->second)];
}

const TileSet::TerrainType* TileSet::FindTerrain(const std::string& name) {
	const Set* s = CurrentConst();
	if (s == nullptr) return nullptr;
	auto it = s->terrainIndex.find(name);
	return it == s->terrainIndex.end() ? nullptr : &s->terrains[static_cast<size_t>(it->second)];
}

const std::vector<TileSet::Template>& TileSet::AllTemplates() {
	static const std::vector<Template> s_empty;
	const Set* s = CurrentConst();
	return s == nullptr ? s_empty : s->templates;
}

const std::vector<TileSet::TerrainType>& TileSet::AllTerrains() {
	static const std::vector<TerrainType> s_empty;
	const Set* s = CurrentConst();
	return s == nullptr ? s_empty : s->terrains;
}

bool TileSet::GetUV(int templateId, int index, float& u0, float& v0, float& u1, float& v1) {
	const Set* s = CurrentConst();
	if (s == nullptr) {
		u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
		return false;
	}
	const Template* t = Find(templateId);
	// 未知模板回退到 clear1（255）：导入别的 tileset 的地图时，模板 id 可能不存在
	if (t == nullptr) t = Find(255);
	if (t == nullptr || t->frameCount <= 0) {
		u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
		return false;
	}
	// OpenRA 语义：地图里的 Index 是"变体号"，不是帧号，必须对模板帧数取模。
	// 之前这里越界就 clamp 到最后一帧，导致同一个模板的所有大 index 变体
	// 都画成同一张图，整片地面看起来是乱拼的。
	int idx = index % t->frameCount;
	if (idx < 0) idx += t->frameCount;
	const int global = t->frameBase + idx;
	const int col = global % s->tileCols;
	const int row = global / s->tileCols;
	const float texW = static_cast<float>(s->tileCols * s->cellW);
	const float texH = static_cast<float>(s->rows * s->cellH);
	if (texW <= 0.0f || texH <= 0.0f) {
		u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
		return false;
	}
	const float inset = 1.0f / 128.0f;
	u0 = (static_cast<float>(col * s->cellW) + inset) / texW;
	v0 = (static_cast<float>(row * s->cellH) + inset) / texH;
	u1 = (static_cast<float>((col + 1) * s->cellW) - inset) / texW;
	v1 = (static_cast<float>((row + 1) * s->cellH) - inset) / texH;
	return true;
}

const std::string& TileSet::TerrainOf(int templateId, int index) {
	static const std::string s_unknown = "Clear";
	const Set* s = CurrentConst();
	if (s == nullptr) return s_unknown;
	const Template* t = Find(templateId);
	if (t == nullptr) t = Find(255);
	if (t == nullptr) return s_unknown;
	auto it = t->terrain.find(index);
	if (it != t->terrain.end()) return it->second;
	if (!t->terrain.empty()) return t->terrain.begin()->second;
	return s_unknown;
}

bool TileSet::Walkable(int templateId, int index) {
	const TerrainType* tt = FindTerrain(TerrainOf(templateId, index));
	return tt == nullptr ? true : tt->walkable;
}

} // namespace rts
