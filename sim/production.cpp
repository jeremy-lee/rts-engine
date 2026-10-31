// RTS Engine — sim/production.cpp
#include "production.h"

#include <algorithm>

#include "locomotor.h" // ApplyPercentageModifiers

namespace rts {

void Buildable::LoadFrom(const YamlNode& node) {
	if (const YamlNode* n = node.Child("Queue")) {
		queues = MiniYaml::SplitList(n->value);
	}
	if (const YamlNode* n = node.Child("Prerequisites")) {
		prerequisites = MiniYaml::SplitList(n->value);
	}
	if (const YamlNode* n = node.Child("BuildDuration")) buildDuration = MiniYaml::ToInt(n->value, -1);
	if (const YamlNode* n = node.Child("BuildDurationModifier")) buildDurationModifier = MiniYaml::ToInt(n->value, 100);
	if (const YamlNode* n = node.Child("BuildPaletteOrder")) buildPaletteOrder = MiniYaml::ToInt(n->value, 0);
	if (const YamlNode* n = node.Child("BuildLimit")) buildLimit = MiniYaml::ToInt(n->value, 0);
}

bool Buildable::PrerequisitesMet(const std::set<std::string>& enabled) const {
	for (const std::string& raw : prerequisites) {
		if (raw.empty()) continue;
		std::string p = raw;
		bool invert = false;
		bool hideOnly = false;
		// '!' 反转、'~' 仅隐藏（可同时出现）
		while (!p.empty() && (p[0] == '!' || p[0] == '~')) {
			if (p[0] == '!') invert = true;
			else hideOnly = true;
			p.erase(p.begin());
		}
		if (p.empty()) continue;
		if (hideOnly) continue; // 只影响可见性
		const bool has = enabled.find(p) != enabled.end();
		if (invert) {
			if (has) return false; // 有该前置时反而不允许
		} else {
			if (!has) return false;
		}
	}
	return true;
}

int32_t ProductionQueue::GetBuildTime(int32_t cost, const Buildable& b, int32_t queueModifier) {
	int32_t time = (b.buildDuration == -1) ? cost : b.buildDuration;
	if (time <= 0) time = 1;
	// OpenRA: ApplyPercentageModifiers(time, [.., bi.BuildDurationModifier, Info.BuildDurationModifier])
	return ApplyPercentageModifiers(time, {b.buildDurationModifier, queueModifier});
}

bool ProductionQueue::Enqueue(const std::string& actorName, int32_t cost, const Buildable& b) {
	if (!CanQueueMore()) return false;
	ProductionItem item(actorName, cost);
	item.buildPaletteOrder = b.buildPaletteOrder;
	// 对齐 OpenRA：构造时时长留 1，首次 Tick 才算真实值
	item.buildable = b;
	item.queueModifier = buildDurationModifier;
	item.started = false;
	items.push_back(std::move(item));
	return true;
}

bool ProductionQueue::Cancel(const std::string& actorName) {
	for (auto it = items.begin(); it != items.end(); ++it) {
		if (it->item == actorName) {
			items.erase(it);
			return true;
		}
	}
	return false;
}

bool ProductionItem::Tick(int32_t& cash, int32_t lowPowerModifier, bool powerNormal, bool payUpFront) {
	if (done) return true;

	// 首次 Tick：按当时的队列/物品修饰算真实时长（OpenRA ProductionItem.Tick 的 !Started 分支）
	if (!started) {
		const int32_t time = ProductionQueue::GetBuildTime(totalCost, buildable, queueModifier);
		if (time > 0) {
			remainingTime = totalTime = time;
		}
		started = true;
	}

	// 低电减速：对齐 OpenRA 的 Slowdown 计数
	if (!powerNormal) {
		slowdown -= 100;
		if (slowdown < 0) slowdown = lowPowerModifier + slowdown;
		else return false; // 这一 tick 不推进
	}

	if (!payUpFront) {
		const int32_t expectedRemainingCost =
			(remainingTime == 1) ? 0 : totalCost * remainingTime / std::max(1, totalTime);
		const int32_t costThisFrame = remainingCost - expectedRemainingCost;
		if (costThisFrame > 0) {
			const int32_t pay = std::min(cash, costThisFrame);
			cash -= pay;
			resourcesPaid += pay;
			remainingCost -= pay;
		}
	} else if (resourcesPaid == 0) {
		// PayUpFront：一开始全额扣
		const int32_t pay = std::min(cash, totalCost);
		cash -= pay;
		resourcesPaid += pay;
		remainingCost -= pay;
	}

	if (remainingTime > 0) --remainingTime;
	if (remainingTime <= 0) {
		remainingTime = 0;
		done = true;
		return true;
	}
	return false;
}

std::string ProductionQueue::Tick(int32_t& cash, bool powerNormal) {
	if (items.empty()) return {};
	ProductionItem& front = items.front();
	// PayUpFront 时资金不足则不推进（对齐 OpenRA：先检查钱够不够）
	if (front.Tick(cash, lowPowerModifier, powerNormal, payUpFront) || front.done) {
		const std::string name = front.item;
		items.erase(items.begin());
		return name;
	}
	return {};
}

bool ProductionQueue::LoadFrom(const YamlNode& node) {
	if (node.key != "ProductionQueue") return false;
	if (const YamlNode* n = node.Child("Type")) type = n->value;
	if (const YamlNode* n = node.Child("BuildDurationModifier")) buildDurationModifier = MiniYaml::ToInt(n->value, 100);
	if (const YamlNode* n = node.Child("LowPowerModifier")) lowPowerModifier = MiniYaml::ToInt(n->value, 100);
	if (const YamlNode* n = node.Child("ItemLimit")) itemLimit = MiniYaml::ToInt(n->value, 999);
	if (const YamlNode* n = node.Child("QueueLimit")) queueLimit = MiniYaml::ToInt(n->value, 0);
	if (const YamlNode* n = node.Child("PayUpFront")) payUpFront = MiniYaml::ToBool(n->value);
	if (const YamlNode* n = node.Child("Sticky")) sticky = MiniYaml::ToBool(n->value);
	return true;
}

} // namespace rts
