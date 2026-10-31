// RTS Engine — tests/test_weapon.cpp
// 验证 WeaponSet / WarheadInfo 的伤害链是否对齐 OpenRA。
#include "test_framework.h"

#include "sim/miniyaml.h"
#include "sim/weapon.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace rts;

TEST(wdist_parse) {
	CHECK_EQ(ParseWDist("5c0"), 5 * 1024);
	CHECK_EQ(ParseWDist("0c512"), 512);
	CHECK_EQ(ParseWDist("1c"), 1024);
	CHECK_EQ(ParseWDist("128"), 128);
	CHECK_EQ(ParseWDist(""), 0);
	CHECK_EQ(ParseWDist("garbage", -1), -1);
}

TEST(warhead_damage_versus_empty) {
	WarheadInfo wh;
	wh.damage = 2500;
	// Versus 为空 → DamageVersus = 100 → 伤害就是 Damage
	CHECK_EQ(wh.DamageVersus({"infantry"}), 100);
	CHECK_EQ(wh.ComputeDamage({"infantry"}), 2500);
}

TEST(warhead_damage_versus_hit) {
	WarheadInfo wh;
	wh.damage = 1000;
	wh.versus["Light"] = 116;
	wh.versus["Heavy"] = 48;
	// 命中 Light → 116% → 1160
	CHECK_EQ(wh.DamageVersus({"Light"}), 116);
	CHECK_EQ(wh.ComputeDamage({"Light"}), 1160);
	// 命中 Heavy → 48%
	CHECK_EQ(wh.ComputeDamage({"Heavy"}), 480);
	// 未命中任何护甲类型 → 100%
	CHECK_EQ(wh.ComputeDamage({"Wood"}), 1000);
}

TEST(warhead_damage_modifiers_multiply) {
	WarheadInfo wh;
	wh.damage = 1000;
	wh.versus["Light"] = 50;
	// 连乘：1000 * 0.5(Versus) * 2.0(modifier) = 1000
	CHECK_EQ(wh.ComputeDamage({"Light"}, {200}), 1000);
	// 1000 * 0.5 * 0.5 = 250
	CHECK_EQ(wh.ComputeDamage({"Light"}, {50}), 250);
}

TEST(weapon_parse_from_yaml) {
	const char* text =
		"^Cannon:\n"
		"\tReloadDelay: 60\n"
		"\tRange: 5c0\n"
		"\tMinRange: 1c0\n"
		"\tBurst: 2\n"
		"\tWarhead@1Dam: SpreadDamage\n"
		"\t\tDamage: 2500\n"
		"\t\tSpread: 1c128\n"
		"\t\tVersus:\n"
		"\t\t\tNone: 30\n"
		"\t\t\tLight: 75\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	WeaponSet set;
	set.LoadFromNodes(root);
	CHECK_EQ(set.Size(), static_cast<size_t>(1));
	const WeaponInfo* w = set.Find("^Cannon");
	CHECK(w != nullptr);
	if (w == nullptr) return;
	CHECK_EQ(w->reloadDelay, 60);
	CHECK_EQ(w->range, 5 * 1024);
	CHECK_EQ(w->minRange, 1024);
	CHECK_EQ(w->burst, 2);
	CHECK_EQ(w->warheads.size(), static_cast<size_t>(1));
	CHECK_EQ(w->warheads[0].type, std::string("SpreadDamage"));
	CHECK_EQ(w->warheads[0].damage, 2500);
	CHECK_EQ(w->warheads[0].spread, 1024 + 128);
	CHECK_EQ(w->warheads[0].ComputeDamage({"None"}), 750);   // 2500 * 0.30
	CHECK_EQ(w->warheads[0].ComputeDamage({"Light"}), 1875); // 2500 * 0.75
}

TEST(weapon_load_real_ra) {
	namespace fs = std::filesystem;
	const std::string dir = "D:/dosbox/game/OpenRA/mods/ra/weapons";
	if (!fs::is_directory(dir)) { CHECK(true); return; }
	WeaponSet set;
	std::string err;
	CHECK(set.LoadDirectory(dir.c_str(), &err));
	std::printf("  [weapon] RA weapons=%zu\n", set.Size());
	CHECK(set.Size() > 50);
	// 步兵步枪：m60mg 在 smallcaliber.yaml
	const WeaponInfo* m60 = set.Find("m60mg");
	if (m60 != nullptr) {
		std::printf("  [weapon] m60mg range=%d reload=%d burst=%d warheads=%zu damage0=%d\n",
			m60->range, m60->reloadDelay, m60->burst, m60->warheads.size(),
			m60->warheads.empty() ? 0 : m60->warheads[0].damage);
		CHECK(m60->range > 0);
		CHECK(m60->reloadDelay > 0);
		CHECK(!m60->warheads.empty());
	}
}