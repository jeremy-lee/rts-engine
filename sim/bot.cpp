// RTS Engine — sim/bot.cpp
#include "bot.h"

#include <algorithm>

namespace rts {

void UnitBuilderBot::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("UnitQueues")) {
		const auto list = MiniYaml::SplitList(n->value);
		if (!list.empty()) unitQueues = list;
	}
	if (const YamlNode* n = node.Child("ProductionMinCashRequirement")) {
		productionMinCashRequirement = MiniYaml::ToInt(n->value, 500);
	}
	if (const YamlNode* n = node.Child("IdleBaseUnitsMaximum")) {
		idleBaseUnitsMaximum = MiniYaml::ToInt(n->value, -1);
	}
	const auto loadMap = [&](const char* key, std::map<std::string, int32_t>& out) {
		if (const YamlNode* n = node.Child(key)) {
			for (const auto& t : n->children) {
				if (t.key.empty()) continue;
				out[t.key] = MiniYaml::ToInt(t.value, 0);
			}
		}
	};
	loadMap("UnitsToBuild", unitsToBuild);
	loadMap("UnitLimits", unitLimits);
	loadMap("UnitDelays", unitDelays);
}

std::string UnitBuilderBot::Tick(int32_t cash,
	const std::set<std::string>& availableQueueCategories,
	const std::map<std::string, int32_t>& currentCounts) {
	// 对齐 OpenRA：现金不足时完全不下单
	if (cash < productionMinCashRequirement) return {};

	++ticks;
	if (feedbackTime > 0 && (ticks % feedbackTime) != 0) return {};

	// 1) 外部请求优先
	if (!queuedRequests.empty()) {
		const std::string req = queuedRequests.front();
		queuedRequests.erase(queuedRequests.begin());
		return req;
	}

	// 2) 按 UnitQueues 轮转，找到第一个有可用队列的分类
	if (unitQueues.empty()) return {};
	for (size_t tries = 0; tries < unitQueues.size(); ++tries) {
		++currentQueueIndex;
		if (currentQueueIndex >= unitQueues.size()) currentQueueIndex = 0;
		const std::string& category = unitQueues[currentQueueIndex];
		if (availableQueueCategories.find(category) == availableQueueCategories.end()) continue;

		// 在该分类下挑一个「还没达到目标数量」的 actor
		// 对齐 OpenRA 的 BuildRandomUnit：这里用确定性顺序（按名字排序）而不是随机，
		// 因为我们的 sim 要求确定性。
		std::vector<std::string> candidates;
		candidates.reserve(unitsToBuild.size());
		for (const auto& kv : unitsToBuild) candidates.push_back(kv.first);
		std::sort(candidates.begin(), candidates.end());
		for (const std::string& actor : candidates) {
			const int32_t want = unitsToBuild.at(actor);
			if (want <= 0) continue;
			// UnitLimits：硬上限
			if (const auto it = unitLimits.find(actor); it != unitLimits.end()) {
				if (it->second >= 0) {
					const auto cnt = currentCounts.find(actor);
					const int32_t have = (cnt == currentCounts.end()) ? 0 : cnt->second;
					if (have >= it->second) continue;
				}
			}
			const auto cnt = currentCounts.find(actor);
			const int32_t have = (cnt == currentCounts.end()) ? 0 : cnt->second;
			if (have >= want) continue;
			return actor;
		}
	}
	return {};
}

std::string HarvesterBot::Tick(int32_t harvesterCount, int32_t refineryCount) {
	++ticks;
	if (scanInterval > 0 && (ticks % scanInterval) != 0) return {};
	if (refineryCount <= 0) return {};

	const int32_t wanted = refineryCount * harvestersPerRefinery;
	if (harvesterCount >= wanted) return {};
	// 按名字排序保证确定性
	if (harvesterTypes.empty()) return {};
	return *harvesterTypes.begin();
}

void BaseBuilderBot::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("BuildingQueues")) {
		const auto list = MiniYaml::SplitList(n->value);
		if (!list.empty()) buildingQueues = list;
	}
	if (const YamlNode* n = node.Child("MinimumExcessPower")) minimumExcessPower = MiniYaml::ToInt(n->value, 0);
	if (const YamlNode* n = node.Child("MaximumExcessPower")) maximumExcessPower = MiniYaml::ToInt(n->value, 0);
	if (const YamlNode* n = node.Child("BuildingsToBuild")) {
		for (const auto& t : n->children) {
			if (t.key.empty()) continue;
			buildingsToBuild[t.key] = MiniYaml::ToInt(t.value, 0);
		}
	}
	if (const YamlNode* n = node.Child("BuildingLimits")) {
		for (const auto& t : n->children) {
			if (t.key.empty()) continue;
			buildingLimits[t.key] = MiniYaml::ToInt(t.value, 0);
		}
	}
}

std::string BaseBuilderBot::Tick(int32_t excessPower,
	const std::set<std::string>& availableQueueCategories,
	const std::map<std::string, int32_t>& currentCounts) {
	++ticks;
	if (feedbackTime > 0 && (ticks % feedbackTime) != 0) return {};

	// 富余电力超过上限时不再造耗电建筑（对齐 MaximumExcessPower 的语义）
	if (maximumExcessPower > 0 && excessPower > maximumExcessPower) return {};

	bool hasQueue = false;
	for (const std::string& q : buildingQueues) {
		if (availableQueueCategories.find(q) != availableQueueCategories.end()) {
			hasQueue = true;
			break;
		}
	}
	if (!hasQueue) return {};

	std::vector<std::string> candidates;
	candidates.reserve(buildingsToBuild.size());
	for (const auto& kv : buildingsToBuild) candidates.push_back(kv.first);
	std::sort(candidates.begin(), candidates.end());
	for (const std::string& actor : candidates) {
		const int32_t want = buildingsToBuild.at(actor);
		if (want <= 0) continue;
		if (const auto it = buildingLimits.find(actor); it != buildingLimits.end()) {
			if (it->second >= 0) {
				const auto cnt = currentCounts.find(actor);
				const int32_t have = (cnt == currentCounts.end()) ? 0 : cnt->second;
				if (have >= it->second) continue;
			}
		}
		const auto cnt = currentCounts.find(actor);
		const int32_t have = (cnt == currentCounts.end()) ? 0 : cnt->second;
		if (have >= want) continue;
		return actor;
	}
	return {};
}

} // namespace rts
