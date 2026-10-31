// RTS Engine — sim/bot.h
// 对齐 OpenRA 的 AI BotModules 骨架：UnitBuilder / BaseBuilder / Harvester。
//
// 源码依据：
//   OpenRA.Mods.Common/Traits/BotModules/UnitBuilderBotModule.cs
//     UnitQueues 默认 { Vehicle, Infantry, Plane, Ship, Aircraft }
//     UnitsToBuild / UnitLimits / UnitDelays（按 actor 名的目标数量与间隔）
//     ProductionMinCashRequirement 默认 500 —— 现金低于它时完全不下单
//     IdleBaseUnitsMaximum 默认 -1（不限制）
//     BotTick 的决策顺序：
//       1) 有外部请求（RequestUnitProduction）先处理它
//       2) 否则按 UnitQueues 轮转（currentQueueIndex 循环），找到第一个有可用队列的分类，
//          在其中随机构建一个单位，然后 break（每次只推进一个队列）
//   OpenRA.Mods.Common/Traits/BotModules/HarvesterBotModule.cs
//     HarvesterTypes / RefineryTypes / ScanForIdleHarvestersInterval=50
//   OpenRA.Mods.Common/Traits/BotModules/BaseBuilderBotModule.cs
//     BuildingQueues / BuildingsToBuild / MinimumExcessPower / MaximumExcessPower
#pragma once

#include "miniyaml.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rts {

// ---- 单位建造 -
class UnitBuilderBot {
public:
	// 默认队列顺序，与 OpenRA 的 UnitQueues 一致
	std::vector<std::string> unitQueues{"Vehicle", "Infantry", "Plane", "Ship", "Aircraft"};
	// actor 名 → 目标数量 / 建造间隔（tick）
	std::map<std::string, int32_t> unitsToBuild;
	std::map<std::string, int32_t> unitLimits;
	std::map<std::string, int32_t> unitDelays;
	int32_t productionMinCashRequirement = 500;
	int32_t idleBaseUnitsMaximum = -1;
	int32_t feedbackTime = 1;

	// 外部请求（对应 IBotRequestUnitProduction）
	std::vector<std::string> queuedRequests;

	size_t currentQueueIndex = 0; // 轮转指针
	int64_t ticks = 0;

	// 每 tick 调用。返回要生产的 actor 名（空 = 不生产）。
	//   cash       —— 当前现金
	//   hasQueue   —— 某个分类是否有可用的 ProductionQueue（Key 是分类名）
	//   countOf    —— 当前该 actor 的数量（用于 UnitLimits）
	std::string Tick(int32_t cash,
		const std::set<std::string>& availableQueueCategories,
		const std::map<std::string, int32_t>& currentCounts);

	void LoadFrom(const YamlNode& node);
};

// ---- 采集者维持 -
class HarvesterBot {
public:
	std::set<std::string> harvesterTypes; // 可用的采集者 actor 名
	std::set<std::string> refineryTypes;  // 精炼厂 actor 名
	int32_t harvestersPerRefinery = 2;
	int32_t scanInterval = 50;
	int64_t ticks = 0;

	// 每 tick 调用；返回需要补造的 harvester 名（空 = 不需要）
	//   harvesterCount / refineryCount 是当前数量
	std::string Tick(int32_t harvesterCount, int32_t refineryCount);
};

// ---- 建筑建造 -
class BaseBuilderBot {
public:
	std::vector<std::string> buildingQueues{"Building"};
	std::map<std::string, int32_t> buildingsToBuild;
	std::map<std::string, int32_t> buildingLimits;
	int32_t minimumExcessPower = 0;
	int32_t maximumExcessPower = 0;
	int32_t feedbackTime = 1;
	int64_t ticks = 0;

	// 每 tick 调用；返回要建造的 actor 名（空 = 不建造）
	//   excessPower —— 当前富余电力（power provided - drained）
	std::string Tick(int32_t excessPower,
		const std::set<std::string>& availableQueueCategories,
		const std::map<std::string, int32_t>& currentCounts);

	void LoadFrom(const YamlNode& node);
};

} // namespace rts
