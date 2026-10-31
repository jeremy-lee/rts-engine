// RTS Engine — sim/locomotor.h
// 对齐 OpenRA.Mods.Common/Traits/World/Locomotor.cs 的 Locomotor 与 TerrainSpeeds。
//
// OpenRA 语义：
//   - 每个 World actor 定义若干 Locomotor（foot / wheeled / tracked / ship / ...）
//   - Locomotor.TerrainSpeeds 是「地形 → 速度百分比」，100 = 正常，0 = 不可通行
//   - MovementSpeedForCell(cell) = TerrainSpeeds[该格地形]，缺省被视为不可通行
//   - MovementCostForCell(cell) = 该格的寻路成本（与速度成反比，0 表示不可达）
//   - Mobile.MovementSpeedForCell(cell) = ApplyPercentageModifiers(Info.Speed, [地形速度, ...修饰])
//     即最终速度 = Speed * (100 + Σ修饰) / 100，所以 Info.Speed 的单位是 subcell/tick
#pragma once

#include "miniyaml.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rts {

// 地形名 → 速度百分比（100 = 正常，0/缺失 = 不可通行）
struct TerrainSpeeds {
	std::map<std::string, int32_t> speed; // 地形名 → 百分比

	// 对应 Locomotor.MovementSpeedForCell；未定义的地形返回 0（不可通行）
	int32_t For(const std::string& terrain) const {
		const auto it = speed.find(terrain);
		return it == speed.end() ? 0 : it->second;
	}
	bool Has(const std::string& terrain) const { return speed.find(terrain) != speed.end(); }
};

class Locomotor {
public:
	std::string name;             // Name: foot
	std::string key;              // yaml 节点名（Locomotor@FOOT 的 FOOT）
	bool sharesCell = false;      // SharesCell: true
	int32_t maxTerrainSpeed = 0;  // 预计算的最高速度（用于快速判断）
	TerrainSpeeds terrainSpeeds;

	// 对应 MovementSpeedForCell
	int32_t SpeedFor(const std::string& terrain) const { return terrainSpeeds.For(terrain); }
	bool CanEnter(const std::string& terrain) const { return terrainSpeeds.For(terrain) > 0; }

	// 从 yaml 节点（Locomotor@FOOT）解析
	bool LoadFrom(const YamlNode& node);
};

// 所有 Locomotor（对应 OpenRA 的 World 上的 WorldLocomotor / LocomotorInfo 集合）
class LocomotorSet {
public:
	std::map<std::string, Locomotor> byName; // Name（小写，如 "foot"）→ Locomotor
	std::map<std::string, std::string> keyToName; // yaml key（"FOOT"）→ Name（"foot"）

	bool Empty() const { return byName.empty(); }
	size_t Size() const { return byName.size(); }

	// 按 Name 查找（"foot"）；也接受 yaml key（"FOOT"）
	const Locomotor* Find(const std::string& nameOrKey) const;

	// 从 Actors/World 节点里的所有 Locomotor@ 子节点解析
	void LoadFrom(const YamlNode& worldNode);
};

// OpenRA 的 Util.ApplyPercentageModifiers：result = base * (100 + Σmods) / 100
int32_t ApplyPercentageModifiers(int32_t baseValue, const std::vector<int32_t>& modifiers);

} // namespace rts
