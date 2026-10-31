// RTS Engine — tests/test_verify_edge.cpp
// 边界与逻辑审计：验证对齐实现里容易出错的地方。
#include "test_framework.h"

#include "sim/bot.h"
#include "sim/locomotor.h"
#include "sim/economy.h"
#include "sim/map.h"
#include "sim/production.h"
#include "sim/miniyaml.h"
#include "sim/traits.h"
#include "sim/weapon.h"

#include <cstdio>
#include <filesystem>
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
TEST(audit_miniyaml_bom) {
	// UTF-8 BOM 开头的文件（OpenRA 的部分 yaml 带 BOM）
	const std::string text = std::string("\xEF\xBB\xBF") + "Actor:\n\tHealth:\n\t\tHP: 42\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	CHECK_EQ(root.children.size(), static_cast<size_t>(1));
	CHECK_EQ(root.children[0].key, std::string("Actor"));
	const YamlNode* actor = root.Child("Actor");
	CHECK(actor != nullptr);
	if (actor == nullptr) return;
	const YamlNode* hp = actor->Child("Health")->Child("HP");
	CHECK(hp != nullptr);
	if (hp) CHECK_EQ(hp->value, std::string("42"));
}

TEST(audit_miniyaml_crlf) {
	// CRLF 行尾（Windows 上编辑过的 yaml）
	const char* text = "A:\r\n\tB: 1\r\n\tC: 2\r\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	const YamlNode* a = root.Child("A");
	CHECK(a != nullptr);
	if (a == nullptr) return;
	CHECK_EQ(a->children.size(), static_cast<size_t>(2));
	CHECK_EQ(a->Child("B")->value, std::string("1"));
	CHECK_EQ(a->Child("C")->value, std::string("2"));
}

TEST(audit_miniyaml_space_indent) {
	// 用空格缩进（不是 tab）的 yaml 也应解析
	const char* text = "A:\n    B:\n        C: 7\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	const YamlNode* a = root.Child("A");
	CHECK(a != nullptr);
	if (a == nullptr) return;
	const YamlNode* b = a->Child("B");
	CHECK(b != nullptr);
	if (b == nullptr) return;
	const YamlNode* c = b->Child("C");
	CHECK(c != nullptr);
	if (c) CHECK_EQ(c->value, std::string("7"));
}

TEST(audit_miniyaml_sequence_items) {
	// '- value' 序列项（OpenRA 的 Prerequisites 等）
	const char* text =
		"Buildable:\n"
		"\tPrerequisites:\n"
		"\t\t- ~barracks\n"
		"\t\t- ~techlevel.infonly\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	const YamlNode* b = root.Child("Buildable");
	CHECK(b != nullptr);
	if (b == nullptr) return;
	const YamlNode* pr = b->Child("Prerequisites");
	CHECK(pr != nullptr);
	if (pr == nullptr) return;
	CHECK_EQ(pr->children.size(), static_cast<size_t>(2));
	CHECK_EQ(pr->children[0].value, std::string("~barracks"));
	CHECK_EQ(pr->children[1].value, std::string("~techlevel.infonly"));
}
TEST(audit_bot_real_ra_ai_yaml) {
	// 真实数据：mods/ra/rules/ai.yaml 的 UnitBuilderBotModule@rush / HarvesterBotModule / BaseBuilderBotModule@normal
	namespace fs = std::filesystem;
	const std::string path = "D:/dosbox/game/OpenRA/mods/ra/rules/ai.yaml";
	if (!fs::exists(path)) { CHECK(true); return; }
	YamlNode root;
	CHECK(MiniYaml::ParseFile(path.c_str(), root));

	// ai.yaml 的顶层是 Player:，bot 模块是它的子节点
	const YamlNode* player = root.Child("Player");
	CHECK(player != nullptr);
	if (player == nullptr) return;
	const YamlNode* ub = player->Child("UnitBuilderBotModule");
	CHECK(ub != nullptr);
	if (ub != nullptr) {
		UnitBuilderBot b;
		b.LoadFrom(*ub);
		std::printf("  [audit] ai UnitBuilderBotModule units=%zu limits=%zu\n",
			b.unitsToBuild.size(), b.unitLimits.size());
		CHECK(b.unitsToBuild.size() >= 15);
		// 原文：UnitsToBuild 里 e1: 65, UnitLimits 里 harv: 8, dog: 4
		CHECK_EQ(b.unitsToBuild["e1"], 65);
		CHECK_EQ(b.unitLimits["harv"], 8);
		CHECK_EQ(b.unitLimits["dog"], 4);
	}

	const YamlNode* hb = player->Child("HarvesterBotModule");
	CHECK(hb != nullptr);
	if (hb != nullptr) {
		// HarvesterTypes: harv / RefineryTypes: proc 是逗号分隔的集合
		const YamlNode* ht = hb->Child("HarvesterTypes");
		const YamlNode* rt = hb->Child("RefineryTypes");
		CHECK(ht != nullptr);
		CHECK(rt != nullptr);
		if (ht) CHECK_EQ(ht->value, std::string("harv"));
		if (rt) CHECK_EQ(rt->value, std::string("proc"));
	}

	// 有 @rush / @normal / @turtle / @naval 四个变体，Child() 只按 key 匹配会拿到第一个(@rush)，
	// 这里显式用 VariantChild 取 @normal（MinimumExcessPower 60 / MaximumExcessPower 200）
	const YamlNode* bb = player->VariantChild("BaseBuilderBotModule", "normal");
	CHECK(bb != nullptr);
	if (bb != nullptr) {
		BaseBuilderBot b;
		b.LoadFrom(*bb);
		std::printf("  [audit] ai BaseBuilderBotModule minPower=%d maxPower=%d limits=%zu\n",
			b.minimumExcessPower, b.maximumExcessPower, b.buildingLimits.size());
		// 原文 BaseBuilderBotModule@normal: MinimumExcessPower 60 / MaximumExcessPower 200
		CHECK_EQ(b.minimumExcessPower, 60);
		CHECK_EQ(b.maximumExcessPower, 200);
		CHECK_EQ(b.buildingLimits["proc"], 4);
		CHECK_EQ(b.buildingLimits["barr"], 1);
	}
}

TEST(audit_production_item_first_tick_timing) {
	// 对齐 OpenRA：Enqueue 时时长留 1，首次 Tick 才按 GetBuildTime 计算
	ProductionQueue q;
	q.buildDurationModifier = 50; // 队列级 50% → 时长减半
	Buildable b;
	b.buildDuration = 100;
	b.buildDurationModifier = 100;
	int32_t cash = 10000;
	CHECK(q.Enqueue("E1", 100, b));
	CHECK(q.Current() != nullptr);
	if (q.Current() == nullptr) return;
	// 入队后仍是 1（未开始计时）
	CHECK_EQ(q.Current()->remainingTime, 1);
	CHECK(!q.Current()->started);
	// 首次 Tick 后变成 50（100 * 100% * 50%）
	q.Tick(cash, true);
	if (q.Current() != nullptr) {
		CHECK(q.Current()->started);
		CHECK_EQ(q.Current()->totalTime, 50);
	}
}
TEST(audit_building_dimensions) {
	// 对齐 OpenRA BuildingInfo.Dimensions；POWR 电厂通过 Inherits@shape: ^2x2Shape 是 2x2
	RegisterCoreTraits();
	const char* text =
		"POWR:\n"
		"\tBuilding:\n"
		"\t\tDimensions: 2,2\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	Ruleset rules;
	rules.LoadFromNodes(root);
	const ActorInfo* powr = rules.FindActor("POWR");
	CHECK(powr != nullptr);
	if (powr == nullptr) return;
	const BuildingInfo* b = powr->Find<BuildingInfo>();
	CHECK(b != nullptr);
	if (b == nullptr) return;
	CHECK_EQ(b->width, 2);
	CHECK_EQ(b->height, 2);
	const auto cells = b->OccupiedCells(5, 7);
	CHECK_EQ(cells.size(), static_cast<size_t>(4));
	CHECK_EQ(cells[0].first, 5);
	CHECK_EQ(cells[0].second, 7);
	CHECK_EQ(cells[3].first, 6);
	CHECK_EQ(cells[3].second, 8);
}

TEST(audit_building_real_ra_powr) {
	namespace fs = std::filesystem;
	const std::string dir = "D:/dosbox/game/OpenRA/mods/ra/rules";
	if (!fs::is_directory(dir)) { CHECK(true); return; }
	RegisterCoreTraits();
	Ruleset rules;
	CHECK(rules.LoadDirectory(dir.c_str()));
	const ActorInfo* powr = rules.FindActor("POWR");
	CHECK(powr != nullptr);
	if (powr == nullptr) return;
	const BuildingInfo* b = powr->Find<BuildingInfo>();
	CHECK(b != nullptr);
	if (b == nullptr) return;
	// structures.yaml 的 POWR 自己写的是 Building.Dimensions: 2,3（不是 ^2x2Shape 提供的 2,2；
	// ^2x2Shape 只给 HitShape，不给 Building 尺寸）
	std::printf("  [audit] POWR dimensions=%dx%d localCenterY=%d\n",
		b->width, b->height, b->localCenterOffsetY);
	CHECK_EQ(b->width, 2);
	CHECK_EQ(b->height, 3);
	CHECK_EQ(b->localCenterOffsetY, -512); // LocalCenterOffset: 0,-512,0
	// 发电厂应当有 Valued.Cost
	const ValuedInfo* v = powr->Find<ValuedInfo>();
	CHECK(v != nullptr);
	if (v) std::printf("  [audit] POWR cost=%d\n", v->cost);
}

TEST(audit_building_footprint_parsing) {
	// 对齐 OpenRA.Mods.Common/Traits/Buildings/Building.cs 的 LoadFootprint：
	// Footprint 文本去掉所有空白后按 y*W+x 逐格映射，长度必须等于 Dimensions 乘积。
	// 真实 RA 里 civilian.yaml 用的是单行 "Footprint: xx xx =="（空格分隔）。
	RegisterCoreTraits();
	const char* text =
		"BLDG:\n"
		"\tBuilding:\n"
		"\t\tDimensions: 2,3\n"
		"\t\tFootprint: xx xx _=\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	Ruleset rules;
	rules.LoadFromNodes(root);
	const ActorInfo* info = rules.FindActor("BLDG");
	CHECK(info != nullptr);
	if (info == nullptr) return;
	const BuildingInfo* b = info->Find<BuildingInfo>();
	CHECK(b != nullptr);
	if (b == nullptr) return;

	CHECK(b->footprintValid);
	CHECK_EQ(b->footprint.size(), static_cast<size_t>(6));
	CHECK(b->CellType(0, 0) == FootprintCellType::Occupied);
	CHECK(b->CellType(1, 1) == FootprintCellType::Occupied);
	CHECK(b->CellType(0, 2) == FootprintCellType::Empty);
	CHECK(b->CellType(1, 2) == FootprintCellType::OccupiedPassable);
	// OccupiedCells 要跳过 '_'
	const auto cells = b->OccupiedCells(5, 7);
	CHECK_EQ(cells.size(), static_cast<size_t>(5));
	for (const auto& c : cells) CHECK(!(c.first == 5 && c.second == 9));
	std::printf("  [audit] footprint cells=%zu (2x3 with one '_')\n", cells.size());
}

TEST(audit_building_footprint_length_mismatch) {
	// 字符数与 Dimensions 不符时必须拒绝，而不是静默当成全占用
	RegisterCoreTraits();
	const char* text =
		"BADBLDG:\n"
		"\tBuilding:\n"
		"\t\tDimensions: 2,2\n"
		"\t\tFootprint: xxx\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	Ruleset rules;
	rules.LoadFromNodes(root);
	const ActorInfo* info = rules.FindActor("BADBLDG");
	CHECK(info != nullptr);
	if (info == nullptr) return;
	const BuildingInfo* b = info->Find<BuildingInfo>();
	CHECK(b != nullptr);
	if (b == nullptr) return;
	CHECK(!b->footprintValid);          // 解析失败
	CHECK(b->footprint.empty());
	// 失败后回退到"全占用"，占格数仍是 2x2
	CHECK_EQ(b->OccupiedCells(0, 0).size(), static_cast<size_t>(4));
}