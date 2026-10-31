// RTS Engine — sim/economy.cpp
#include "economy.h"

#include <algorithm>

namespace rts {

void PlayerResources::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("Resources")) resources = MiniYaml::ToInt(n->value, 0);
	if (const YamlNode* v = node.Child("ResourceValues")) {
		for (const auto& t : v->children) {
			if (t.key.empty()) continue;
			resourceValues[t.key] = MiniYaml::ToInt(t.value, 0);
		}
	}
}

void ResourceLayer::LoadFrom(const YamlNode& node) {
	const YamlNode* typesNode = node.Child("ResourceTypes");
	if (typesNode == nullptr) return;
	for (const auto& t : typesNode->children) {
		if (t.key.empty()) continue;
		TypeInfo info;
		if (const YamlNode* n = t.Child("ResourceIndex")) info.resourceIndex = MiniYaml::ToInt(n->value, 0);
		if (const YamlNode* n = t.Child("MaxDensity")) info.maxDensity = MiniYaml::ToInt(n->value, 10);
		if (const YamlNode* n = t.Child("TerrainType")) info.terrainType = n->value;
		types[t.key] = info;
	}
	// 按字典序建立 索引 → 名称 表（与 Add 里分配 index 的顺序一致）
	indexToName.clear();
	for (const auto& kv : types) indexToName.push_back(kv.first);
}

const std::string& ResourceLayer::NameOfIndex(uint8_t idx) const {
	static const std::string s_empty;
	if (idx == 0 || idx > indexToName.size()) return s_empty;
	return indexToName[idx - 1];
}

int32_t ResourceLayer::ValueAt(const Map& map, CellCoord c, const PlayerResources& pr) const {
	if (!map.InBounds(c)) return 0;
	const int32_t amount = map.ResourceAmount(c); // density（bale 数）
	if (amount <= 0) return 0;
	const std::string& typeName = NameOfIndex(map.ResourceType(c));
	if (typeName.empty()) return 0;
	// 对齐 PlayerResources.ResourceValues：每个 bale 的价值（RA: Ore=25）
	return amount * pr.ValueOf(typeName);
}

int32_t ResourceLayer::Remove(Map& map, CellCoord c, int32_t amount) {
	if (!map.InBounds(c) || amount <= 0) return 0;
	const int32_t have = map.ResourceAmount(c);
	const int32_t take = (amount < have) ? amount : have;
	map.SetResourceAmount(c, have - take);
	if (have - take <= 0) map.SetResourceType(c, 0);
	return take;
}

bool ResourceLayer::Add(Map& map, CellCoord c, const std::string& type, int32_t density) {
	if (!map.InBounds(c)) return false;
	const int32_t maxD = MaxDensityOf(type);
	if (maxD <= 0) return false;
	if (density > maxD) density = maxD;
	if (density <= 0) return false;
	// 索引：按 types 的字典序稳定分配（0 表示无资源）
	int32_t index = 0;
	int32_t i = 1;
	for (const auto& kv : types) {
		if (kv.first == type) { index = i; break; }
		++i;
	}
	map.SetResourceType(c, static_cast<uint8_t>(index));
	map.SetResourceAmount(c, density);
	return true;
}

void Harvester::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("BaleLoadDelay")) baleLoadDelay = MiniYaml::ToInt(n->value, 4);
	if (const YamlNode* n = node.Child("BaleUnloadDelay")) baleUnloadDelay = MiniYaml::ToInt(n->value, 4);
	if (const YamlNode* n = node.Child("BaleUnloadAmount")) baleUnloadAmount = MiniYaml::ToInt(n->value, 1);
	if (const YamlNode* n = node.Child("HarvestFacings")) harvestFacings = MiniYaml::ToInt(n->value, 0);
	if (const YamlNode* n = node.Child("FullyLoadedSpeed")) fullyLoadedSpeed = MiniYaml::ToInt(n->value, 85);
	if (const YamlNode* n = node.Child("Capacity")) capacity = MiniYaml::ToInt(n->value, 0);
}

} // namespace rts
