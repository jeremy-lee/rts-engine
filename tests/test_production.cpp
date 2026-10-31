// RTS Engine — tests/test_production.cpp
// 验证 ProductionQueue / ProductionItem 是否对齐 OpenRA。
#include "test_framework.h"

#include "sim/miniyaml.h"
#include "sim/production.h"

#include <cstdio>

using namespace rts;

TEST(buildable_prerequisites) {
	Buildable b;
	b.prerequisites = {"anypower", "~barracks"};
	TechTree tree;
	CHECK(!b.PrerequisitesMet(tree.enabled)); // 缺 anypower
	tree.Enable("anypower");
	CHECK(b.PrerequisitesMet(tree.enabled));  // '~barracks' 只隐藏，不禁用

	Buildable inv;
	inv.prerequisites = {"!radar"};
	CHECK(inv.PrerequisitesMet(tree.enabled)); // 没有 radar → 允许
	tree.Enable("radar");
	CHECK(!inv.PrerequisitesMet(tree.enabled)); // 有 radar → 禁用
}

TEST(build_time_modifiers) {
	// GetBuildTime = ApplyPercentageModifiers(BuildDuration==-1 ? cost : duration, [b.mod, queue.mod])
	Buildable b;
	b.buildDuration = -1;
	b.buildDurationModifier = 100;
	CHECK_EQ(ProductionQueue::GetBuildTime(500, b, 100), 500);
	CHECK_EQ(ProductionQueue::GetBuildTime(500, b, 50), 250);   // 队列 50% → 一半时间
	CHECK_EQ(ProductionQueue::GetBuildTime(500, b, 200), 1000); // 200% → 双倍时间

	Buildable d;
	d.buildDuration = 300; // 显式时长优先于 cost
	CHECK_EQ(ProductionQueue::GetBuildTime(9999, d, 100), 300);
}

TEST(queue_enqueue_and_complete) {
	ProductionQueue q;
	q.type = "Infantry";
	q.buildDurationModifier = 100;
	Buildable b;
	b.buildDurationModifier = 100;
	int32_t cash = 1000;
	CHECK(q.Enqueue("E1", 100, b));
	CHECK_EQ(q.Size(), static_cast<size_t>(1));
	CHECK(q.Current() != nullptr);

	// 时间 = cost = 100 tick；每 tick 按比例扣钱，100 tick 后完成
	std::string finished;
	for (int i = 0; i < 200 && finished.empty(); ++i) {
		finished = q.Tick(cash, true);
	}
	CHECK_EQ(finished, std::string("E1"));
	CHECK(q.Empty());
	CHECK(cash >= 0);
	CHECK(cash <= 1000);
	std::printf("  [production] E1 done, cash left=%d\n", cash);
}

TEST(queue_low_power_slowdown) {
	ProductionQueue q;
	q.lowPowerModifier = 300; // 低电时 3 倍慢
	Buildable b;
	b.buildDuration = 10;
	b.buildDurationModifier = 100;
	int32_t cash = 10000;
	CHECK(q.Enqueue("E1", 100, b));

	int ticks = 0;
	std::string finished;
	while (finished.empty() && ticks < 500) {
		finished = q.Tick(cash, false); // 一直低电
		++ticks;
	}
	CHECK_EQ(finished, std::string("E1"));
	// 低电 300% → 大约 3 倍 tick 数（10 tick → ~30）
	std::printf("  [production] low power took %d ticks (normal would be 10)\n", ticks);
	CHECK(ticks > 10);
}

TEST(queue_cancel_and_limit) {
	ProductionQueue q;
	q.queueLimit = 2;
	Buildable b;
	b.buildDuration = 100;
	int32_t cash = 0;
	CHECK(q.Enqueue("E1", 10, b));
	CHECK(q.Enqueue("E2", 10, b));
	CHECK(!q.Enqueue("E3", 10, b)); // 超过 queueLimit
	CHECK(q.Cancel("E1"));
	CHECK_EQ(q.Size(), static_cast<size_t>(1));
	CHECK_EQ(q.Current()->item, std::string("E2"));
}

TEST(production_queue_parse) {
	const char* text =
		"ClassicProductionQueue@Building:\n"
		"\tType: Building\n"
		"\tLowPowerModifier: 300\n"
		"\tItemLimit: 999\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	const YamlNode* node = root.Child("ClassicProductionQueue");
	CHECK(node != nullptr);
	ProductionQueue q;
	// LoadFrom 要求 key == "ProductionQueue"，这里手工映射变体
	CHECK(!q.LoadFrom(*node));
	YamlNode renamed = *node;
	renamed.key = "ProductionQueue";
	CHECK(q.LoadFrom(renamed));
	CHECK_EQ(q.type, std::string("Building"));
	CHECK_EQ(q.lowPowerModifier, 300);
	CHECK_EQ(q.itemLimit, 999);
}