// RTS Engine — sim/production.h
// 对齐 OpenRA 的 ProductionQueue / ProductionItem / Buildable。
//
// 源码依据：
//   OpenRA.Mods.Common/Traits/Player/ProductionQueue.cs
//     ProductionQueueInfo: Type / BuildDurationModifier(100) / LowPowerModifier(100) /
//       ItemLimit(999) / PayUpFront(false) / QueueLimit(0) / Sticky(true)
//     GetBuildTime: time = Buildable.BuildDuration；若为 -1 取 actor 的 Valued.Cost；
//       再 ApplyPercentageModifiers(time, [时间修饰..., bi.BuildDurationModifier, queue.BuildDurationModifier])
//     ProductionItem.Tick:
//       - 首次 Tick 计算 TotalTime
//       - 低电时用 Slowdown 计数实现减速（LowPowerModifier）
//       - 非 PayUpFront：expectedRemainingCost = RemainingTime==1 ? 0 : TotalCost*RemainingTime/TotalTime
//         costThisFrame = RemainingCost - expectedRemainingCost，按剩余现金支付
//       - RemainingTime-- / RemainingCost -= costThisFrame
//   OpenRA.Mods.Common/Traits/Buildable.cs
//     BuildableInfo: Queue(集合) / Prerequisites / BuildDuration(-1) / BuildDurationModifier(100)
//       Prerequisites 前缀：'!' 反转、'~' 仅隐藏而不是禁用
//     ProvidesPrerequisite 提供前置；TechTree 保存玩家已获得的前置集合
#pragma once

#include "miniyaml.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rts {

// ---- Buildable ----
struct Buildable {
	std::vector<std::string> queues;          // Queue（可属于多个生产队列）
	std::vector<std::string> prerequisites;   // Prerequisites（可带 '!' / '~' 前缀）
	int32_t buildDuration = -1;               // BuildDuration，-1 = 用 Cost
	int32_t buildDurationModifier = 100;      // BuildDurationModifier
	int32_t buildPaletteOrder = 0;            // BuildPaletteOrder
	int32_t buildLimit = 0;                   // BuildLimit（-1 = 无限）

	// 加载字段
	void LoadFrom(const YamlNode& node);

	// 前置条件是否满足（对齐 OpenRA 的 TechTree.HasPrerequisites）
	// 规则：普通项必须在 enabled 中；'!' 项必须在 enabled 中才「不满足」；
	//       '~' 项只影响可见性，不影响可建性。
	bool PrerequisitesMet(const std::set<std::string>& enabled) const;
};

// ---- 科技树（玩家已获得的前置）----
class TechTree {
public:
	std::set<std::string> enabled;

	void Enable(const std::string& p) { enabled.insert(p); }
	void Disable(const std::string& p) { enabled.erase(p); }
	bool Has(const std::string& p) const { return enabled.find(p) != enabled.end(); }
	bool HasPrerequisites(const Buildable& b) const { return b.PrerequisitesMet(enabled); }
};

// ---- 生产项 ----
class ProductionItem {
public:
	std::string item;        // actor 名
	int32_t totalCost = 0;
	int32_t remainingCost = 0;
	int32_t totalTime = 1;
	int32_t remainingTime = 1;
	int32_t resourcesPaid = 0;
	int32_t buildPaletteOrder = 0;

	// 首次 Tick 才计算真实时长所需的输入（对齐 OpenRA：构造时 RemainingTime=TotalTime=1）
	Buildable buildable;
	int32_t queueModifier = 100;

	bool started = false;
	bool paused = false;
	bool done = false;

	// OpenRA 的 Slowdown 计数（低电减速用）
	int32_t slowdown = 0;

	ProductionItem() = default;
	ProductionItem(std::string name, int32_t cost) : item(std::move(name)), totalCost(cost), remainingCost(cost) {
		// 对齐 OpenRA ProductionItem 构造：RemainingTime = TotalTime = 1，真实时长在首次 Tick 计算
		totalTime = 1;
		remainingTime = 1;
	}

	// 低电时的实际剩余时间（对齐 ProductionItem.RemainingTimeActual）
	int32_t RemainingTimeActual(int32_t lowPowerModifier, bool powerNormal) const {
		return powerNormal ? remainingTime : remainingTime * lowPowerModifier / 100;
	}

	// 推进一个 tick。返回 true 表示本 tick 完成。
	// cash 是玩家现金（会被扣减）；lowPowerModifier 来自队列；powerNormal 表示电力正常。
	bool Tick(int32_t& cash, int32_t lowPowerModifier, bool powerNormal, bool payUpFront);
};

// ---- 生产队列 ----
class ProductionQueue {
public:
	std::string type;                    // Building / Infantry / Vehicle / ...
	int32_t buildDurationModifier = 100;
	int32_t lowPowerModifier = 100;
	int32_t itemLimit = 999;
	int32_t queueLimit = 0;
	bool payUpFront = false;
	bool sticky = true;

	std::vector<ProductionItem> items;

	bool Empty() const { return items.empty(); }
	size_t Size() const { return items.size(); }
	bool CanQueueMore() const {
		if (queueLimit > 0 && static_cast<int32_t>(items.size()) >= queueLimit) return false;
		return static_cast<int32_t>(items.size()) < itemLimit;
	}

	// 对齐 GetBuildTime：time = buildDuration（-1 则取 cost），再连乘各修饰
	static int32_t GetBuildTime(int32_t cost, const Buildable& b, int32_t queueModifier);

	// 入队
	bool Enqueue(const std::string& actorName, int32_t cost, const Buildable& b);
	// 取消（按名字，取消队列里第一个匹配）
	bool Cancel(const std::string& actorName);
	// 当前正在建造的项（队首）
	ProductionItem* Current() { return items.empty() ? nullptr : &items.front(); }

	// 推进一步；返回本 tick 完成的 actor 名（没有则为空）
	// cash 会被扣减。
	std::string Tick(int32_t& cash, bool powerNormal);
	// 加载队列配置（ProductionQueue@XXX 节点）
	bool LoadFrom(const YamlNode& node);
};

} // namespace rts
