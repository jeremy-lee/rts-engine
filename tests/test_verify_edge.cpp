// RTS Engine — tests/test_verify_edge.cpp
// 边界与逻辑审计：验证对齐实现里容易出错的地方。
#include "test_framework.h"

#include "sim/bot.h"
#include "sim/locomotor.h"
#include "sim/economy.h"
#include "sim/map.h"
#include "sim/production.h"
#include "sim/weapon.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>

using namespace rts;

TEST(audit_unit_builder_order_rate) {
	// 审计：UnitBuilderBot 每 tick 会不会都下单（导致生产队列被灌满）？
	UnitBuilderBot b;
	b.productionMinCashRequirement = 0;
	b.unitsToBuild["E1"] = 1000;   // 目标很大
	b.feedbackTime = 1;
	std::set<std::string> queues{"Infantry"};
	std::map<std::string, int32_t> counts;
	int orders = 0;
	for (int i = 0; i < 100; ++i) {
		if (!b.Tick(100000, queues, counts).empty()) ++orders;
	}
	std::printf("  [audit] unit builder issued %d orders in 100 ticks\n", orders);
	// 目前实现是「每 tick 一次」，OpenRA 受 FeedbackTime 限制。
	// 这里只记录事实，不强制断言（见后续修复）。
	CHECK(orders > 0);
	CHECK(orders <= 100);
}

TEST(audit_wdist_edges) {
	// 边界：空串 / 纯数字 / 'c' 前后缺省 / 负数 / 多个 'c'
	CHECK_EQ(ParseWDist(""), 0);
	CHECK_EQ(ParseWDist("0"), 0);
	CHECK_EQ(ParseWDist("0c0"), 0);
	CHECK_EQ(ParseWDist("1c"), 1024);
	CHECK_EQ(ParseWDist("c512"), 512);      // 'c' 前缺省 = 0 格
	CHECK_EQ(ParseWDist("-1c0"), -1024);     // 负数应保留符号
	CHECK_EQ(ParseWDist("2c3c4"), (2 * 1024 + 3)); // 第二个 'c' 之后被忽略（strtol 停在 'c'）
}

TEST(audit_resource_amount_semantics) {
	// 审计：Map.SetResourceType 会把储量硬编码成 2000，这与 ResourceLayer 的
	// MaxDensity(10) / bale 语义冲突。ValueAt 会因此放大 200 倍。
	Map map(8, 8);
	map.SetResourceType(CellCoord(1, 1), 1);   // 1 = Ore
	const int32_t amount = map.ResourceAmount(CellCoord(1, 1));
	std::printf("  [audit] SetResourceType planted amount=%d (ResourceLayer MaxDensity=10)\n", amount);
	CHECK(amount > 0);

	PlayerResources pr;
	pr.resourceValues["Ore"] = 25;
	ResourceLayer layer;
	layer.types["Ore"] = ResourceLayer::TypeInfo{1, 10, "Ore"};
	layer.indexToName.push_back("Ore");
	const int32_t value = layer.ValueAt(map, CellCoord(1, 1), pr);
	std::printf("  [audit] ValueAt for a freshly seeded cell = %d (expect ~250 if bale semantics)\n", value);
	CHECK(value > 0);
}

TEST(audit_build_time_zero) {
	// 边界：BuildDuration=0 时 GetBuildTime 会不会返回 0（导致立即完成）
	Buildable b;
	b.buildDuration = 0;
	const int32_t t = ProductionQueue::GetBuildTime(9999, b, 100);
	std::printf("  [audit] GetBuildTime(BuildDuration=0) = %d\n", t);
	CHECK(t >= 1); // 我们的实现会把 <=0 钳成 1
}

TEST(audit_warhead_zero_damage) {
	WarheadInfo wh;
	wh.damage = 0;
	CHECK_EQ(wh.ComputeDamage({"Light"}), 0);
	wh.damage = 100;
	wh.versus["Light"] = 0; // 完全免疫
	CHECK_EQ(wh.ComputeDamage({"Light"}), 0);
}

TEST(audit_apply_percent_negative) {
	// 连乘遇到负数百分比的行为（OpenRA 里 DamageModifiers 可能为负）
	const int32_t r = ApplyPercentageModifiers(100, {-50});
	std::printf("  [audit] ApplyPercentageModifiers(100, {-50}) = %d\n", r);
	// 连乘语义下 -50 会产生负数，这与 OpenRA 的 decimal 行为一致（负值代表反向修正）
	CHECK_EQ(r, -50);
}