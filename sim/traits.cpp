// RTS Engine — sim/traits.cpp
#include "traits.h"

#include <cstdio>

namespace rts {

// ---------------- Mobile ----------------
void MobileInfo::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("Locomotor")) locomotor = n->value;
	if (const YamlNode* n = node.Child("InitialFacing")) initialFacing = WAngle(MiniYaml::ToInt(n->value, 0));
	if (const YamlNode* n = node.Child("TurnSpeed")) turnSpeed = MiniYaml::ToInt(n->value, 512);
	if (const YamlNode* n = node.Child("Speed")) speed = MiniYaml::ToInt(n->value, 1);
	if (const YamlNode* n = node.Child("SharesCell")) sharesCell = MiniYaml::ToBool(n->value);
	if (const YamlNode* n = node.Child("AlwaysTurnInPlace")) alwaysTurnInPlace = MiniYaml::ToBool(n->value);
	if (const YamlNode* n = node.Child("TurnsWhileMoving")) turnsWhileMoving = MiniYaml::ToBool(n->value);
	instanceName = node.variant;
}

// ---------------- Health ----------------
void HealthInfo::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("HP")) hp = MiniYaml::ToInt(n->value, 1);
}

// ---------------- Armor ----------------
void ArmorInfo::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("Type")) type = n->value;
}

// ---------------- Armament ----------------
void ArmamentInfo::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("Weapon")) weapon = n->value;
	if (const YamlNode* n = node.Child("FireDelay")) fireDelay = MiniYaml::ToInt(n->value, 0);
	if (const YamlNode* n = node.Child("Turreted")) turreted = MiniYaml::ToBool(n->value);
	instanceName = node.variant;

	// LocalOffset: 1c0,1c0,1c256 三个分量
	if (const YamlNode* n = node.Child("LocalOffset")) {
		const auto parts = MiniYaml::SplitList(n->value);
		if (parts.size() >= 3) {
			localOffsetX = ParseWDist(parts[0], 0);
			localOffsetY = ParseWDist(parts[1], 0);
			localOffsetZ = ParseWDist(parts[2], 0);
		}
	}
}

// ---------------- Valued ----------------
void ValuedInfo::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("Cost")) cost = MiniYaml::ToInt(n->value, 0);
}

// ---------------- Harvester ----------------
void HarvesterInfo::LoadFrom(const YamlNode& node) {
	data.LoadFrom(node);
	if (const YamlNode* n = node.Child("Resources")) resources = MiniYaml::SplitList(n->value);
	// RA 的采集容量写在 IStoreResources 上；这里也接受 Capacity 直接给
	if (const YamlNode* n = node.Child("Capacity")) data.capacity = MiniYaml::ToInt(n->value, data.capacity);
}

// ---------------- Refinery ----------------
void RefineryInfo::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("Capacity")) capacity = MiniYaml::ToInt(n->value, 0);
	if (const YamlNode* n = node.Child("ResourceType")) resourceType = n->value;
}

// ---------------- Production ----------------
void ProductionInfo::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("Produces")) produces = MiniYaml::SplitList(n->value);
}

// ---------------- ProductionQueue ----------------
void ProductionQueueInfoTrait::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("Type")) type = n->value;
	if (const YamlNode* n = node.Child("BuildDurationModifier")) buildDurationModifier = MiniYaml::ToInt(n->value, 100);
	if (const YamlNode* n = node.Child("LowPowerModifier")) lowPowerModifier = MiniYaml::ToInt(n->value, 100);
	if (const YamlNode* n = node.Child("ItemLimit")) itemLimit = MiniYaml::ToInt(n->value, 999);
	if (const YamlNode* n = node.Child("QueueLimit")) queueLimit = MiniYaml::ToInt(n->value, 0);
	if (const YamlNode* n = node.Child("PayUpFront")) payUpFront = MiniYaml::ToBool(n->value);
	if (const YamlNode* n = node.Child("Sticky")) sticky = MiniYaml::ToBool(n->value);
}

// ---------------- Building ----------------
void BuildingInfo::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("Dimensions")) {
		int32_t w = 1, h = 1;
		if (MiniYaml::ToPair(n->value, w, h)) {
			width = (w > 0) ? w : 1;
			height = (h > 0) ? h : 1;
		}
	}
	if (const YamlNode* n = node.Child("LocalCenterOffset")) {
		const auto parts = MiniYaml::SplitList(n->value);
		if (parts.size() >= 3) {
			localCenterOffsetX = ParseWDist(parts[0], 0);
			localCenterOffsetY = ParseWDist(parts[1], 0);
			localCenterOffsetZ = ParseWDist(parts[2], 0);
		}
	}
	if (const YamlNode* n = node.Child("RequiresBaseProvider")) requiresBaseProvider = MiniYaml::ToBool(n->value);
}

// ---------------- 注册 ----------------
namespace {
const bool s_registered = []() {
	RegisterCoreTraits();
	return true;
}();
} // namespace

void RegisterCoreTraits() {
	static bool done = false;
	if (done) return;
	done = true;

	TraitRegistry::Register("Mobile", []() -> std::unique_ptr<TraitInfo> { return std::make_unique<MobileInfo>(); });
	TraitRegistry::Register("Health", []() -> std::unique_ptr<TraitInfo> { return std::make_unique<HealthInfo>(); });
	TraitRegistry::Register("Armor", []() -> std::unique_ptr<TraitInfo> { return std::make_unique<ArmorInfo>(); });
	TraitRegistry::Register("Armament", []() -> std::unique_ptr<TraitInfo> { return std::make_unique<ArmamentInfo>(); });
	TraitRegistry::Register("Buildable", []() -> std::unique_ptr<TraitInfo> { return std::make_unique<BuildableInfo>(); });
	TraitRegistry::Register("Valued", []() -> std::unique_ptr<TraitInfo> { return std::make_unique<ValuedInfo>(); });
	TraitRegistry::Register("Harvester", []() -> std::unique_ptr<TraitInfo> { return std::make_unique<HarvesterInfo>(); });
	TraitRegistry::Register("Refinery", []() -> std::unique_ptr<TraitInfo> { return std::make_unique<RefineryInfo>(); });
	TraitRegistry::Register("Production", []() -> std::unique_ptr<TraitInfo> { return std::make_unique<ProductionInfo>(); });
	TraitRegistry::Register("ProductionQueue", []() -> std::unique_ptr<TraitInfo> { return std::make_unique<ProductionQueueInfoTrait>(); });
	TraitRegistry::Register("Building", []() -> std::unique_ptr<TraitInfo> { return std::make_unique<BuildingInfo>(); });
}

} // namespace rts
