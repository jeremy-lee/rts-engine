// RTS Engine — sim/gamedefs.cpp
#include "gamedefs.h"

#include <fstream>
#include <sstream>

namespace rts {

const UnitDef* Rules::FindUnit(const std::string& id) const {
	const auto it = units.find(id);
	return (it == units.end()) ? nullptr : &it->second;
}

const BuildingDef* Rules::FindBuilding(const std::string& id) const {
	const auto it = buildings.find(id);
	return (it == buildings.end()) ? nullptr : &it->second;
}

const UnitDef* Rules::FindUnitByNumeric(int32_t numericId) const {
	if (numericId < 0 || numericId >= static_cast<int32_t>(unitIds.size())) return nullptr;
	return FindUnit(unitIds[static_cast<size_t>(numericId)]);
}

Rules Rules::FromYAML(const std::string& text) {
	Rules rules;
	const DataNode root = DataNode::Parse(text);
	if (root.IsNull()) return rules;

	// units: { id: {name, cost, buildTime, hp, speed, weapon:{damage,range,cooldown}} }
	if (const DataNode* unitsNode = root.GetNode("units"); unitsNode && unitsNode->IsMap()) {
		for (const auto& kv : unitsNode->map) {
			UnitDef u;
			u.id = kv.first;
			u.name = kv.second.GetString("name", kv.first);
			u.cost = kv.second.GetInt("cost", 50);
			u.buildTime = kv.second.GetInt("buildTime", 30);
			u.hp = kv.second.GetInt("hp", 100);
			u.speed = kv.second.GetInt("speed", 32);
			if (const DataNode* w = kv.second.GetNode("weapon"); w && w->IsMap()) {
				u.weapon.id = w->GetString("id", kv.first + "_weapon");
				u.weapon.damage = w->GetInt("damage", 10);
				u.weapon.range = w->GetInt("range", 8 * kSubPerCell);
				u.weapon.cooldown = w->GetInt("cooldown", 15);
			}
			u.isHarvester = kv.second.GetBool("harvester", false);
			u.numericId = static_cast<int32_t>(rules.unitIds.size());
			rules.unitIds.push_back(u.id);
			rules.units[u.id] = std::move(u);
		}
	}

	// buildings: { id: {name, cost, buildTime, hp, width, height, produces:[...]} }
	if (const DataNode* bNode = root.GetNode("buildings"); bNode && bNode->IsMap()) {
		for (const auto& kv : bNode->map) {
			BuildingDef b;
			b.id = kv.first;
			b.name = kv.second.GetString("name", kv.first);
			b.cost = kv.second.GetInt("cost", 100);
			b.buildTime = kv.second.GetInt("buildTime", 60);
			b.hp = kv.second.GetInt("hp", 500);
			b.width = kv.second.GetInt("width", 2);
			b.height = kv.second.GetInt("height", 2);
			if (const DataNode* produces = kv.second.GetNode("produces"); produces && produces->IsList()) {
				for (const auto& item : produces->list) {
					if (item.IsScalar()) b.produces.push_back(item.scalar);
				}
			}
			b.numericId = static_cast<int32_t>(rules.buildingIds.size());
			rules.buildingIds.push_back(b.id);
			rules.buildings[b.id] = std::move(b);
		}
	}
	return rules;
}

bool Rules::LoadFile(const std::string& path, Rules& out) {
	std::ifstream in(path);
	if (!in) return false;
	std::stringstream ss;
	ss << in.rdbuf();
	out = FromYAML(ss.str());
	return !out.Empty();
}

} // namespace rts
