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

// ---- Building（Traits/Buildings/Building.cs）：占格尺寸 ----
struct BuildingInfo : public TraitInfo {
	int32_t width = 1;                     // Dimensions: 2,2
	int32_t height = 1;
	const char* TypeName() const override { return "Building"; }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<BuildingInfo>(*this); }
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
