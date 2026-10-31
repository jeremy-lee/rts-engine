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
	// Footprint 矩阵（width*height，按 y*width+x），字符含义同 OpenRA：
	//   '_' 空、'x' 占格、'X' 占格不可选中、'=' 占格可通行、'+' 占格仅可路过
	// 为空表示整块矩形全部占用（等价于全是 'x'）。
	std::vector<char> footprint;
	std::vector<std::string> produces;
	bool turret = false;      // 防御塔：自动攻击范围内敌人
	WeaponDef weapon;         // 武器（turret 用）
	int32_t numericId = -1;

	// 该格是否属于占地（'_' 不算）
	bool OccupiesCell(int32_t x, int32_t y) const {
		if (footprint.empty()) return true;
		const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
		if (idx >= footprint.size()) return true;
		return footprint[idx] != '_';
	}
};

// 科技升级定义（M19）
struct UpgradeDef {
	int32_t cost = 300;
	int32_t attack = 5;   // +伤害
	int32_t hp = 50;      // +最大生命
	int32_t speed = 2;    // +速度
};

struct Rules {
	std::map<std::string, UnitDef> units;
	std::map<std::string, BuildingDef> buildings;
	UpgradeDef upgrade; // M19：科技升级

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
