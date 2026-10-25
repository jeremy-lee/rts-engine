// RTS Engine — sim/gamedefs.h
// 游戏规则数据：武器 / 单位 / 建筑定义，从 YAML 加载。
// 设计：数据与代码分离 —— 单位/建筑由 rules.yaml 定义，代码只提供机制。
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "core/core.h"
#include "datatable.h"

namespace rts {

struct WeaponDef {
	std::string id;
	int32_t damage = 10;
	int32_t range = 8 * kSubPerCell; // 子格
	Tick cooldown = 15;
};

struct UnitDef {
	std::string id;
	std::string name;
	int32_t cost = 50;
	Tick buildTime = 30;
	int32_t hp = 100;
	int32_t speed = 32;   // 子格/tick
	WeaponDef weapon;
	bool isHarvester = false; // 采矿单位
	int32_t numericId = -1;   // 运行时数字 id（用于 Command.targetId）
};

struct BuildingDef {
	std::string id;
	std::string name;
	int32_t cost = 100;
	Tick buildTime = 60;
	int32_t hp = 500;
	int32_t width = 2;  // 占用格子
	int32_t height = 2;
	std::vector<std::string> produces;
	int32_t numericId = -1;
};

struct Rules {
	std::map<std::string, UnitDef> units;
	std::map<std::string, BuildingDef> buildings;

	// 数字 id → 定义（按加载顺序分配）
	std::vector<std::string> unitIds;
	std::vector<std::string> buildingIds;

	// 加载：解析 YAML 文本
	static Rules FromYAML(const std::string& text);
	static bool LoadFile(const std::string& path, Rules& out);

	const UnitDef* FindUnit(const std::string& id) const;
	const BuildingDef* FindBuilding(const std::string& id) const;
	const UnitDef* FindUnitByNumeric(int32_t numericId) const;

	bool Empty() const { return units.empty() && buildings.empty(); }
};

} // namespace rts
