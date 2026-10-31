// RTS Engine — sim/traits.h
// 具体 trait 定义，对齐 OpenRA 各个 XxxInfo 类。
//
// OpenRA 的 trait 全是 `XxxInfo : TraitInfo`，由 ObjectCreator 按名字反射创建；
// 这里每个类自己实现 TypeName() 并在 traits.cpp 底部静态注册。
//
// 已覆盖的核心 trait：
//   Mobile         → MobileInfo（Locomotor / Speed / TurnSpeed / InitialFacing / SharesCell）
//   Health         → HealthInfo（HP）
//   Armor          → ArmorInfo（Type，护甲类型名，供 Versus 查表）
//   Armament       → ArmamentInfo（Weapon / FireDelay / LocalOffset / Turreted）
//   Buildable      → BuildableInfo（Queue / Prerequisites / BuildDuration / ...）
//   Valued         → ValuedInfo（Cost，建造时间与生产成本的来源）
//   Harvester      → HarvesterInfo（BaleLoadDelay / BaleUnloadDelay / Capacity / ...）
//   Refinery       → RefineryInfo（容量与资源类型）
//   Production     → ProductionInfo（Produces，生产标签）
//   ProductionQueue→ ProductionQueueInfo（Type / BuildDurationModifier / LowPowerModifier）
//   Building       → BuildingInfo（Dimensions，占格）
#pragma once

#include "core/wangle.h"
#include "economy.h"
#include "production.h"
#include "rules.h"
#include "weapon.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rts {

// ---- Mobile（OpenRA.Mods.Common/Traits/Mobile.cs）----
struct MobileInfo : public TraitInfo {
	std::string locomotor;                 // Locomotor: foot
	WAngle initialFacing{0};
	int32_t turnSpeed = 512;               // 默认 512 = 180°/tick
	int32_t speed = 1;                     // subcell/tick
	bool sharesCell = false;
	bool alwaysTurnInPlace = false;
	bool turnsWhileMoving = false;
	// 'Mobile@variant' 的 variant 部分
	std::string instanceName;

	const char* TypeName() const override { return "Mobile"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<MobileInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- Health（Traits/Health.cs）----
struct HealthInfo : public TraitInfo {
	int32_t hp = 1;                        // HP:
	const char* TypeName() const override { return "Health"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<HealthInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- Armor（Traits/Armor.cs）：护甲类型名，供 Warhead.Versus 查表 ----
struct ArmorInfo : public TraitInfo {
	std::string type;                      // Type: Heavy
	const char* TypeName() const override { return "Armor"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<ArmorInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- Armament（Traits/Armament.cs）----
struct ArmamentInfo : public TraitInfo {
	std::string weapon;                    // Weapon: m60mg
	int32_t fireDelay = 0;                 // FireDelay
	int32_t localOffsetX = 0;              // LocalOffset 拆三轴（避免引入 WVec）
	int32_t localOffsetY = 0;
	int32_t localOffsetZ = 0;
	bool turreted = false;
	// 'Armament@PRIMARY' 的 PRIMARY 部分
	std::string instanceName;

	const char* TypeName() const override { return "Armament"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<ArmamentInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- Buildable（Traits/Buildable.cs）----
struct BuildableInfo : public TraitInfo {
	Buildable data;
	const char* TypeName() const override { return "Buildable"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<BuildableInfo>(*this); }
	void LoadFrom(const YamlNode& node) override { data.LoadFrom(node); }
};

// ---- Valued（Traits/Valued.cs）：Cost / 建造时间来源 ----
struct ValuedInfo : public TraitInfo {
	int32_t cost = 0;                      // Cost:
	const char* TypeName() const override { return "Valued"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<ValuedInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- Harvester（Traits/Harvester.cs）----
struct HarvesterInfo : public TraitInfo {
	Harvester data;
	std::vector<std::string> resources;    // Resources: Ore, Gems（可采集的资源种类）
	const char* TypeName() const override { return "Harvester"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<HarvesterInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- Refinery（Traits/Refinery.cs 的容量部分）----
struct RefineryInfo : public TraitInfo {
	int32_t capacity = 0;                  // 精炼厂内部储量
	std::string resourceType;              // 接受的资源类型
	const char* TypeName() const override { return "Refinery"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<RefineryInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- Production（Traits/Production.cs）：本建筑能生产什么标签 ----
struct ProductionInfo : public TraitInfo {
	std::vector<std::string> produces;     // Produces: Infantry, Vehicle
	const char* TypeName() const override { return "Production"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<ProductionInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- ProductionQueue（Traits/Player/ProductionQueue.cs 的配置部分）----
struct ProductionQueueInfoTrait : public TraitInfo {
	std::string type;                      // Type: Building
	int32_t buildDurationModifier = 100;
	int32_t lowPowerModifier = 100;
	int32_t itemLimit = 999;
	int32_t queueLimit = 0;
	bool payUpFront = false;
	bool sticky = true;

	const char* TypeName() const override { return "ProductionQueue"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<ProductionQueueInfoTrait>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- Building（Traits/Buildings/Building.cs）：占格形状 ----
// 对应 OpenRA.Mods.Common/Traits/Buildings/Building.cs 的 FootprintCellType。
// Footprint 在 yaml 里是多行字符串（无冒号），去掉所有空白后按 y*W+x 逐格映射，
// 长度必须等于 Dimensions.X * Dimensions.Y。
enum class FootprintCellType : uint8_t {
	Empty = '_',                       // 完全不占格
	Occupied = 'x',                    // 占格，阻挡
	OccupiedUntargetable = 'X',        // 占格，但不可被选为攻击目标
	OccupiedPassable = '=',            // 占格但可通行
	OccupiedPassableTransitOnly = '+', // 占格，仅可路过
};

struct BuildingInfo : public TraitInfo {
	int32_t width = 1;                     // Dimensions: 2,2
	int32_t height = 1;
	int32_t localCenterOffsetX = 0;        // LocalCenterOffset（拆三轴）
	int32_t localCenterOffsetY = 0;
	int32_t localCenterOffsetZ = 0;
	bool requiresBaseProvider = false;
	bool footprintValid = false;           // 是否成功从 yaml 解析出合法 Footprint
	// 长度 width*height，按 y*width+x 索引；为空时视为全 Occupied。
	std::vector<FootprintCellType> footprint;

	const char* TypeName() const override { return "Building"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<BuildingInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;

	// 某一格是否属于建筑占地（'_' 不算）。
	FootprintCellType CellType(int32_t x, int32_t y) const {
		if (footprintValid && static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)
			< footprint.size()) {
			return footprint[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
		}
		return FootprintCellType::Occupied;
	}

	// 以 origin 为左上角，返回真正占用的格子（跳过 '_'）。
	std::vector<std::pair<int32_t, int32_t>> OccupiedCells(int32_t originX, int32_t originY) const {
		std::vector<std::pair<int32_t, int32_t>> out;
		out.reserve(static_cast<size_t>(width) * height);
		for (int32_t y = 0; y < height; ++y) {
			for (int32_t x = 0; x < width; ++x) {
				if (CellType(x, y) == FootprintCellType::Empty) continue;
				out.push_back({originX + x, originY + y});
			}
		}
		return out;
	}
};

// ---- Tooltip（Traits/Tooltip.cs）：RA 里出现最多的 trait，纯展示用 ----
struct TooltipInfo : public TraitInfo {
	std::string text;
	const char* TypeName() const override { return "Tooltip"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<TooltipInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- Power（Traits/Power/Power.cs）：发电/耗电 ----
// amount > 0 发电，< 0 耗电（OpenRA 里耗电建筑用负数）
struct PowerInfo : public TraitInfo {
	int32_t amount = 0;
	bool dashed = false;            // 是否显示为虚线电力条
	const char* TypeName() const override { return "Power"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<PowerInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- RevealsShroud（Traits/RevealsShroud.cs）：提供视野 ----
struct RevealsShroudInfo : public TraitInfo {
	int32_t range = 0;              // WDist（子格）
	bool valid = false;
	const char* TypeName() const override { return "RevealsShroud"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<RevealsShroudInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- Targetable（Traits/Targetable.cs）：能否被选为攻击目标 ----
struct TargetableInfo : public TraitInfo {
	bool valid = true;
	const char* TypeName() const override { return "Targetable"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<TargetableInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};

// ---- HitShape（Traits/HitShape.cs）：命中判定形状 ----
// 简化实现：只解析 Type（Circle/Rectangle）和 Radius，够做近战/远程命中判定。
struct HitShapeInfo : public TraitInfo {
	std::string shape = "Circle";   // Type: Circle / Rectangle
	int32_t radius = 0;             // Radius（WDist）
	int32_t width = 0;              // Rectangle 用
	int32_t height = 0;
	const char* TypeName() const override { return "HitShape"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<HitShapeInfo>(*this); }
	void LoadFrom(const YamlNode& node) override;
};
// 注册所有内置 trait（第一次调用 LoadDirectory 前调用；可重复调用）
void RegisterCoreTraits();

// 便捷查询：从 ActorInfo 取某个具体 trait
template <typename T>
const T* TraitOf(const ActorInfo* ai) {
	if (ai == nullptr) return nullptr;
	return ai->Find<T>();
}

} // namespace rts
