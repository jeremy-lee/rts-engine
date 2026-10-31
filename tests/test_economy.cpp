// RTS Engine — tests/test_economy.cpp
// 验证经济系统（PlayerResources / ResourceLayer / Harvester）是否对齐 OpenRA。
#include "test_framework.h"

#include "sim/economy.h"
#include "sim/map.h"
#include "sim/miniyaml.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace rts;

TEST(player_resources_cash) {
	PlayerResources pr;
	CHECK_EQ(pr.Resources(), 0);
	pr.GiveCash(1000);
	CHECK_EQ(pr.Resources(), 1000);
	CHECK(pr.TakeCash(400));
	CHECK_EQ(pr.Resources(), 600);
	// 钱不够：不扣、返回 false
	CHECK(!pr.TakeCash(10000));
	CHECK_EQ(pr.Resources(), 600);
}

TEST(player_resources_values) {
	const char* text =
		"PlayerResources:\n"
		"\tResourceValues:\n"
		"\t\tOre: 25\n"
		"\t\tGems: 50\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	PlayerResources pr;
	pr.LoadFrom(*root.Child("PlayerResources"));
	CHECK_EQ(pr.ValueOf("Ore"), 25);
	CHECK_EQ(pr.ValueOf("Gems"), 50);
	CHECK_EQ(pr.ValueOf("Unknown"), 0);
}

TEST(harvester_fullness_and_speed) {
	Harvester h;
	h.capacity = 20;
	h.fullyLoadedSpeed = 85;
	CHECK(h.Empty());
	CHECK_EQ(h.Fullness(), 0);
	CHECK_EQ(h.SpeedModifier(), 100); // 空车全速

	for (int i = 0; i < 10; ++i) h.LoadBale(1);
	CHECK_EQ(h.Fullness(), 50);
	// 100 - (100-85)*50/100 = 100 - 7 = 93
	CHECK_EQ(h.SpeedModifier(), 93);

	while (!h.IsFull()) h.LoadBale(1);
	CHECK_EQ(h.Fullness(), 100);
	// 满载：100 - 15 = 85
	CHECK_EQ(h.SpeedModifier(), 85);
}

TEST(harvester_unload) {
	Harvester h;
	h.capacity = 20;
	h.baleUnloadAmount = 3;
	for (int i = 0; i < 20; ++i) h.LoadBale(1);
	CHECK_EQ(h.contents, 20);
	CHECK_EQ(h.UnloadBale(), 3);
	CHECK_EQ(h.contents, 17);
	while (!h.Empty()) h.UnloadBale();
	CHECK(h.Empty());
	CHECK_EQ(h.UnloadBale(), 0);
}

TEST(resource_layer_add_remove) {
	const char* text =
		"ResourceLayer:\n"
		"\tResourceTypes:\n"
		"\t\tOre:\n"
		"\t\t\tResourceIndex: 1\n"
		"\t\t\tMaxDensity: 10\n"
		"\t\tGems:\n"
		"\t\t\tResourceIndex: 2\n"
		"\t\t\tMaxDensity: 5\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	ResourceLayer layer;
	layer.LoadFrom(*root.Child("ResourceLayer"));
	CHECK_EQ(layer.types.size(), static_cast<size_t>(2));
	CHECK_EQ(layer.MaxDensityOf("Ore"), 10);
	CHECK_EQ(layer.MaxDensityOf("Gems"), 5);

	Map map(16, 16);
	CHECK(layer.Add(map, CellCoord(2, 3), "Ore", 20)); // 超过 MaxDensity → 钳到 10
	CHECK_EQ(map.ResourceAmount(CellCoord(2, 3)), 10);
	CHECK(map.ResourceType(CellCoord(2, 3)) != 0);

	CHECK_EQ(layer.Remove(map, CellCoord(2, 3), 4), 4);
	CHECK_EQ(map.ResourceAmount(CellCoord(2, 3)), 6);
	CHECK_EQ(layer.Remove(map, CellCoord(2, 3), 100), 6); // 取完
	CHECK_EQ(map.ResourceAmount(CellCoord(2, 3)), 0);
	CHECK_EQ(map.ResourceType(CellCoord(2, 3)), 0);       // 空后重置类型
	// 不存在的类型
	CHECK(!layer.Add(map, CellCoord(1, 1), "Gold", 5));
}

TEST(resource_value_at_cell) {
	PlayerResources pr;
	pr.resourceValues["Ore"] = 25;
	ResourceLayer layer;
	layer.types["Ore"] = ResourceLayer::TypeInfo{1, 10, "Ore"};
	layer.indexToName.push_back("Ore");
	Map map(8, 8);
	layer.Add(map, CellCoord(1, 1), "Ore", 8);
	// 8 bale * 25 = 200
	CHECK_EQ(layer.ValueAt(map, CellCoord(1, 1), pr), 200);
	CHECK_EQ(layer.ValueAt(map, CellCoord(5, 5), pr), 0);
}

TEST(refinery_accept) {
	Refinery r;
	r.capacity = 10;
	r.resourceType = "Ore";
	CHECK_EQ(r.Accept(4, "Ore"), 4);
	CHECK_EQ(r.contents, 4);
	CHECK_EQ(r.Accept(20, "Ore"), 6);  // 只剩 6 空间
	CHECK_EQ(r.Accept(1, "Ore"), 0);   // 满
	CHECK_EQ(r.Accept(1, "Gems"), 0);  // 类型不符
	CHECK_EQ(r.WithdrawAll(), 10);
	CHECK_EQ(r.contents, 0);
}

TEST(economy_real_ra_values) {
	namespace fs = std::filesystem;
	const std::string path = "D:/dosbox/game/OpenRA/mods/ra/rules/player.yaml";
	if (!fs::exists(path)) { CHECK(true); return; }
	YamlNode root;
	CHECK(MiniYaml::ParseFile(path.c_str(), root));
	// PlayerResources 是 ^BasePlayer 的 trait 子节点，不是顶层
	const YamlNode* basePlayer = root.Child("^BasePlayer");
	CHECK(basePlayer != nullptr);
	if (basePlayer == nullptr) return;
	const YamlNode* pr = basePlayer->Child("PlayerResources");
	CHECK(pr != nullptr);
	if (pr == nullptr) return;
	PlayerResources res;
	res.LoadFrom(*pr);
	std::printf("  [economy] Ore=%d Gems=%d\n", res.ValueOf("Ore"), res.ValueOf("Gems"));
	CHECK_EQ(res.ValueOf("Ore"), 25);
	CHECK_EQ(res.ValueOf("Gems"), 50);
}