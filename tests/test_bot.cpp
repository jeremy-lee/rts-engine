// RTS Engine — tests/test_bot.cpp
// 验证 AI BotModules 的决策逻辑是否对齐 OpenRA。
#include "test_framework.h"

#include "sim/bot.h"
#include "sim/miniyaml.h"

#include <cstdio>
#include <set>
#include <string>

using namespace rts;

TEST(unit_builder_respects_cash_floor) {
	UnitBuilderBot b;
	b.productionMinCashRequirement = 500;
	b.unitsToBuild["E1"] = 10;
	std::set<std::string> queues{"Infantry"};
	std::map<std::string, int32_t> counts;
	// 现金 499 < 500 → 不下单
	CHECK_EQ(b.Tick(499, queues, counts), std::string(""));
	// 500 → 可以下单
	CHECK_EQ(b.Tick(500, queues, counts), std::string("E1"));
}

TEST(unit_builder_respects_target_count) {
	UnitBuilderBot b;
	b.productionMinCashRequirement = 0;
	b.unitsToBuild["E1"] = 3;
	std::set<std::string> queues{"Infantry"};
	std::map<std::string, int32_t> counts{{"E1", 3}};
	// 已达目标 → 不下单
	CHECK_EQ(b.Tick(10000, queues, counts), std::string(""));
	counts["E1"] = 2;
	CHECK_EQ(b.Tick(10000, queues, counts), std::string("E1"));
}

TEST(unit_builder_respects_hard_limit) {
	UnitBuilderBot b;
	b.productionMinCashRequirement = 0;
	b.unitsToBuild["E1"] = 100;   // 目标 100
	b.unitLimits["E1"] = 2;       // 硬上限 2
	std::set<std::string> queues{"Infantry"};
	std::map<std::string, int32_t> counts{{"E1", 2}};
	CHECK_EQ(b.Tick(10000, queues, counts), std::string(""));
}

TEST(unit_builder_priority_request) {
	UnitBuilderBot b;
	b.productionMinCashRequirement = 0;
	b.unitsToBuild["E1"] = 100;
	b.queuedRequests.push_back("HTNK");
	std::set<std::string> queues{"Vehicle", "Infantry"};
	std::map<std::string, int32_t> counts;
	// 外部请求优先于常规轮转
	CHECK_EQ(b.Tick(10000, queues, counts), std::string("HTNK"));
	CHECK(b.queuedRequests.empty());
}

TEST(unit_builder_rotates_queues) {
	UnitBuilderBot b;
	b.productionMinCashRequirement = 0;
	b.unitsToBuild["E1"] = 100;
	std::set<std::string> queues{"Infantry"};
	std::set<std::string> none;
	std::map<std::string, int32_t> counts;
	// 第一次命中 Infantry（轮转从 Vehicle 开始，跳过不可用的）
	CHECK_EQ(b.Tick(10000, queues, counts), std::string("E1"));
	// 队列不可用时不产生订单
	CHECK_EQ(b.Tick(10000, none, counts), std::string(""));
}

TEST(harvester_bot_maintains_ratio) {
	HarvesterBot h;
	h.harvesterTypes = {"HARV"};
	h.refineryTypes = {"PROC"};
	h.harvestersPerRefinery = 2;
	h.scanInterval = 1; // 每 tick 都扫描（测试用）
	// 2 个精炼厂需要 4 台
	CHECK_EQ(h.Tick(0, 0), std::string(""));
	CHECK_EQ(h.Tick(1, 2), std::string("HARV"));
	CHECK_EQ(h.Tick(4, 2), std::string(""));  // 够了
	CHECK_EQ(h.Tick(3, 1), std::string(""));  // 1 个精炼厂需要 2，有 3 台 → 够
	CHECK_EQ(h.Tick(0, 1), std::string("HARV")); // 1 个精炼厂需要 2，一台都没有 → 补造
	CHECK_EQ(h.Tick(0, 0), std::string(""));     // 没有精炼厂 → 不造
}

TEST(base_builder_power_gate) {
	BaseBuilderBot b;
	b.buildingsToBuild["POWR"] = 10;
	b.maximumExcessPower = 100;
	b.feedbackTime = 1;
	std::set<std::string> queues{"Building"};
	std::map<std::string, int32_t> counts;
	// 富余电力超上限 → 不造
	CHECK_EQ(b.Tick(200, queues, counts), std::string(""));
	// 正常范围 → 造
	CHECK_EQ(b.Tick(50, queues, counts), std::string("POWR"));
	// 没有可用队列 → 不造
	std::set<std::string> none;
	CHECK_EQ(b.Tick(50, none, counts), std::string(""));
}

TEST(bot_module_parse) {
	const char* text =
		"UnitBuilderBotModule:\n"
		"\tProductionMinCashRequirement: 750\n"
		"\tUnitQueues: Vehicle, Infantry\n"
		"\tUnitsToBuild:\n"
		"\t\tE1: 12\n"
		"\t\tHTNK: 4\n"
		"\tUnitLimits:\n"
		"\t\tHTNK: 8\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	UnitBuilderBot b;
	b.LoadFrom(*root.Child("UnitBuilderBotModule"));
	CHECK_EQ(b.productionMinCashRequirement, 750);
	CHECK_EQ(b.unitQueues.size(), static_cast<size_t>(2));
	CHECK_EQ(b.unitQueues[0], std::string("Vehicle"));
	CHECK_EQ(b.unitsToBuild["E1"], 12);
	CHECK_EQ(b.unitsToBuild["HTNK"], 4);
	CHECK_EQ(b.unitLimits["HTNK"], 8);
	std::printf("  [bot] queues=%zu units=%zu limits=%zu\n",
		b.unitQueues.size(), b.unitsToBuild.size(), b.unitLimits.size());
}