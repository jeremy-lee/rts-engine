// RTS Engine — sim/locomotor.cpp
#include "locomotor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace rts {

int32_t ApplyPercentageModifiers(int32_t baseValue, const std::vector<int32_t>& modifiers) {
	// OpenRA Util.ApplyPercentageModifiers：base * (100 + sum(mods)) / 100，整数除法
	int32_t sum = 100;
	for (const int32_t m : modifiers) sum += m;
	return static_cast<int32_t>(static_cast<int64_t>(baseValue) * sum / 100);
}

bool Locomotor::LoadFrom(const YamlNode& node) {
	if (node.key != "Locomotor") return false;
	key = node.variant; // Locomotor@FOOT → "FOOT"

	if (const YamlNode* n = node.Child("Name")) name = n->value;
	if (name.empty()) name = key; // 没有 Name 时退回变体名

	if (const YamlNode* n = node.Child("SharesCell")) sharesCell = MiniYaml::ToBool(n->value);

	const YamlNode* speeds = node.Child("TerrainSpeeds");
	if (speeds == nullptr) return true;
	for (const auto& t : speeds->children) {
		if (t.key.empty()) continue;
		const int32_t v = MiniYaml::ToInt(t.value, 0);
		terrainSpeeds.speed[t.key] = v;
		maxTerrainSpeed = std::max(maxTerrainSpeed, v);
	}
	return true;
}

const Locomotor* LocomotorSet::Find(const std::string& nameOrKey) const {
	// 先按 Name 精确查
	auto it = byName.find(nameOrKey);
	if (it != byName.end()) return &it->second;
	// 再按 yaml key 查
	const auto k = keyToName.find(nameOrKey);
	if (k != keyToName.end()) {
		it = byName.find(k->second);
		if (it != byName.end()) return &it->second;
	}
	// 最后做大小写不敏感匹配（Name 在 yaml 里通常是小写）
	std::string lower = nameOrKey;
	for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	for (const auto& kv : byName) {
		std::string n = kv.first;
		for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (n == lower) return &kv.second;
	}
	return nullptr;
}

void LocomotorSet::LoadFrom(const YamlNode& worldNode) {
	for (const auto& child : worldNode.children) {
		if (child.key != "Locomotor") continue;
		Locomotor loco;
		if (!loco.LoadFrom(child)) continue;
		keyToName[loco.key] = loco.name;
		byName[loco.name] = loco;
	}
}

} // namespace rts
