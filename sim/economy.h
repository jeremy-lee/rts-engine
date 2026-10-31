// RTS Engine — sim/economy.h
// 对齐 OpenRA 的经济系统：PlayerResources / ResourceLayer / Harvester / Refinery。
//
// 源码依据：
//   OpenRA.Mods.Common/Traits/Player/PlayerResources.cs
//     Resources（现金）, GetCashAndResources(), GiveCash(num), TakeCash(num)
//     ResourceValues: 资源名 → 单价（RA: Ore=25, Gems=50）
//   OpenRA.Mods.Common/Traits/World/ResourceLayer.cs
//     ResourceLayerContents { Type（资源名）, Density }
//     ResourceTypeInfo { ResourceIndex, TerrainType, AllowedTerrainTypes, MaxDensity=10 }
//   OpenRA.Mods.Common/Traits/Harvester.cs
//     BaleLoadDelay=4, BaleUnloadDelay=4, BaleUnloadAmount=1, HarvestFacings=0,
//     FullyLoadedSpeed=85, SearchFromProcRadius=24, SearchFromHarvesterRadius=12,
//     WaitDuration=25, ResourceRefineryDirectionPenalty=200
//     IsFull    = 所有 store 的 ContentsSum >= Capacity
//     Fullness  = Σ(ContentsSum*100/Capacity) / store 数
//     MovementSpeed 修正：100 - (100-FullyLoadedSpeed)*Fullness/100
#pragma once

#include "map.h"
#include "miniyaml.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rts {

// ---- 玩家资源 ----
class PlayerResources {
public:
	int32_t resources = 0;
	// 资源名 → 单价（RA: Ore 25 / Gems 50）
	std::map<std::string, int32_t> resourceValues;

	int32_t Resources() const { return resources; }

	void GiveCash(int32_t num) {
		if (num <= 0) return;
		resources += num;
	}

	// 对齐 TakeCash：钱不够时返回 false 且不扣
	bool TakeCash(int32_t num) {
		if (num <= 0) return true;
		if (resources < num) return false;
		resources -= num;
		return true;
	}

	// 某资源的单价（未定义返回 0）
	int32_t ValueOf(const std::string& resourceType) const {
		const auto it = resourceValues.find(resourceType);
		return it == resourceValues.end() ? 0 : it->second;
	}

	void LoadFrom(const YamlNode& node);
};

// ---- 资源层 ----
// 每个格子最多存放一种资源，Density 上限由 ResourceTypeInfo.MaxDensity 决定（默认 10）
class ResourceLayer {
public:
	struct Contents {
		std::string type;
		int32_t density = 0;
	};
	struct TypeInfo {
		int32_t resourceIndex = 0;
		int32_t maxDensity = 10;
		std::string terrainType;
	};
	std::map<std::string, TypeInfo> types;

	// 与 Map 的 m_resTypes/m_resAmount 同步：这里只做规则与换算，存储仍在 Map 里
	int32_t MaxDensityOf(const std::string& type) const {
		const auto it = types.find(type);
		return it == types.end() ? 0 : it->second.maxDensity;
	}
	bool IsValidType(const std::string& type) const { return types.find(type) != types.end(); }

	// 一格能提供的资源价值 = density * 单价
	// 索引（Map.ResourceType 的 1-based 值）→ 资源名
	std::vector<std::string> indexToName;
	const std::string& NameOfIndex(uint8_t idx) const;
	int32_t ValueAt(const Map& map, CellCoord c, const PlayerResources& pr) const;
	// 从一格取走 amount 单位，返回实际取走的量
	int32_t Remove(Map& map, CellCoord c, int32_t amount);
	// 在一格放置资源（钳制到 MaxDensity）
	bool Add(Map& map, CellCoord c, const std::string& type, int32_t density);

	void LoadFrom(const YamlNode& node);
};

// ---- 采集者 ----
class Harvester {
public:
	// 对应 Harvester 的 Info 默认值
	int32_t baleLoadDelay = 4;
	int32_t baleUnloadDelay = 4;
	int32_t baleUnloadAmount = 1;
	int32_t harvestFacings = 0;
	int32_t fullyLoadedSpeed = 85;
	int32_t capacity = 0;          // 来自 IStoreResources（RA 的 HARV 是 20 bales 左右）
	int32_t contents = 0;          // 当前装载量
	std::string resourceType;      // 当前装的资源名

	bool IsFull() const { return capacity > 0 && contents >= capacity; }
	int32_t Fullness() const { return capacity <= 0 ? 0 : contents * 100 / capacity; }

	// 装载速度修正：100 - (100 - FullyLoadedSpeed) * Fullness / 100
	int32_t SpeedModifier() const {
		return 100 - (100 - fullyLoadedSpeed) * Fullness() / 100;
	}

	// 采集一个 bale（返回 true 表示装满）
	bool LoadBale(int32_t balesPerHarvest = 1) {
		contents += balesPerHarvest;
		if (capacity > 0 && contents > capacity) contents = capacity;
		return IsFull();
	}
	// 卸载：返回本次卸载量
	int32_t UnloadBale() {
		const int32_t n = baleUnloadAmount;
		if (contents <= 0) return 0;
		const int32_t take = (n > contents) ? contents : n;
		contents -= take;
		return take;
	}
	bool Empty() const { return contents <= 0; }

	void LoadFrom(const YamlNode& node);
};

// ---- 精炼厂 ----
// 对应 IAcceptResources（Refinery）
class Refinery {
public:
	int32_t capacity = 0;
	int32_t contents = 0;
	std::string resourceType;

	// 返回实际接收量
	int32_t Accept(int32_t amount, const std::string& type) {
		if (type != resourceType && !resourceType.empty()) return 0;
		if (capacity > 0 && contents >= capacity) return 0;
		int32_t room = (capacity > 0) ? (capacity - contents) : amount;
		const int32_t take = (amount < room) ? amount : room;
		contents += take;
		return take;
	}
	int32_t WithdrawAll() {
		const int32_t n = contents;
		contents = 0;
		return n;
	}
};

} // namespace rts
