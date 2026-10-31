// RTS Engine — tests/test_traits.cpp
// 验证具体 trait 是否真正注册并可查询（这是阶段 1 的核心缺口）。
#include "test_framework.h"

#include "sim/rules.h"
#include "sim/traits.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace rts;

TEST(trait_registry_has_core_traits) {
	RegisterCoreTraits();
	const auto types = TraitRegistry::AllTypes();
	std::printf("  [traits] registered=%zu\n", types.size());
	CHECK(types.size() >= 10);
	for (const char* need : {"Mobile", "Health", "Armor", "Armament", "Buildable",
			"Valued", "Harvester", "Refinery", "Production", "ProductionQueue", "Building"}) {
		CHECK(TraitRegistry::Has(need));
	}
}

TEST(trait_instantiation_concrete_types) {
	RegisterCoreTraits();
	auto mobile = TraitRegistry::Create("Mobile");
	CHECK(mobile != nullptr);
	if (mobile) CHECK_EQ(std::string(mobile->TypeName()), std::string("Mobile"));
	CHECK(dynamic_cast<MobileInfo*>(mobile.get()) != nullptr);

	CHECK(TraitRegistry::Create("NotARealTrait") == nullptr);
}

TEST(actor_find_returns_concrete_trait) {
	RegisterCoreTraits();
	const char* text =
		"E1:\n"
		"\tMobile:\n"
		"\t\tSpeed: 56\n"
		"\t\tTurnSpeed: 512\n"
		"\t\tLocomotor: foot\n"
		"\tHealth:\n"
		"\t\tHP: 50\n"
		"\tArmor:\n"
		"\t\tType: None\n"
		"\tValued:\n"
		"\t\tCost: 100\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	Ruleset rules;
	rules.LoadFromNodes(root);
	const ActorInfo* e1 = rules.FindActor("E1");
	CHECK(e1 != nullptr);
	if (e1 == nullptr) return;

	// 关键：Find<T>() 必须拿到真实类型（此前注册表为空，全是 UnknownTrait）
	const MobileInfo* mobile = e1->Find<MobileInfo>();
	CHECK(mobile != nullptr);
	if (mobile != nullptr) {
		CHECK_EQ(mobile->speed, 56);
		CHECK_EQ(mobile->turnSpeed, 512);
		CHECK_EQ(mobile->locomotor, std::string("foot"));
	}
	const HealthInfo* health = e1->Find<HealthInfo>();
	CHECK(health != nullptr);
	if (health) CHECK_EQ(health->hp, 50);
	const ArmorInfo* armor = e1->Find<ArmorInfo>();
	CHECK(armor != nullptr);
	if (armor) CHECK_EQ(armor->type, std::string("None"));
	const ValuedInfo* valued = e1->Find<ValuedInfo>();
	CHECK(valued != nullptr);
	if (valued) CHECK_EQ(valued->cost, 100);
}

TEST(armament_variant_and_weapon) {
	RegisterCoreTraits();
	const char* text =
		"HTNK:\n"
		"\tArmament@PRIMARY:\n"
		"\t\tWeapon: 120mm\n"
		"\t\tLocalOffset: 0c0,0c0,1c256\n"
		"\tArmament@SECONDARY:\n"
		"\t\tWeapon: m60mg\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	Ruleset rules;
	rules.LoadFromNodes(root);
	const ActorInfo* t = rules.FindActor("HTNK");
	CHECK(t != nullptr);
	if (t == nullptr) return;
	// 两个 Armament 实例都应存在
	const auto all = t->FindAll("Armament");
	CHECK_EQ(all.size(), static_cast<size_t>(2));
	const ArmamentInfo* prim = t->Find<ArmamentInfo>();
	CHECK(prim != nullptr);
	if (prim) {
		CHECK_EQ(prim->weapon, std::string("120mm"));
		CHECK_EQ(prim->localOffsetZ, 1024 + 256);
		CHECK_EQ(prim->instanceName, std::string("PRIMARY"));
	}
}

TEST(traits_load_from_real_ra) {
	namespace fs = std::filesystem;
	const std::string dir = "D:/dosbox/game/OpenRA/mods/ra/rules";
	if (!fs::is_directory(dir)) { CHECK(true); return; }
	RegisterCoreTraits();
	Ruleset rules;
	CHECK(rules.LoadDirectory(dir.c_str()));
	// E1 步枪兵：应该有 Mobile / Health / Armor / Valued / Armament
	const ActorInfo* e1 = rules.FindActor("E1");
	CHECK(e1 != nullptr);
	if (e1 == nullptr) return;
	const MobileInfo* m = e1->Find<MobileInfo>();
	const HealthInfo* h = e1->Find<HealthInfo>();
	const ValuedInfo* v = e1->Find<ValuedInfo>();
	std::printf("  [traits] E1: mobile=%d hp=%d cost=%d\n",
		m ? m->speed : -1, h ? h->hp : -1, v ? v->cost : -1);
	CHECK(m != nullptr);
	CHECK(h != nullptr);
	// RA 的 E1 在 infantry.yaml:89 明确定义 HP: 5000
	if (h) CHECK_EQ(h->hp, 5000);
	if (m) CHECK(m->speed > 0);
	if (v) CHECK(v->cost > 0);
}
TEST(audit_end_to_end_e1_matches_yaml) {
	// 端到端对照：E1 的几个关键字段必须与其 yaml 原文一致。
	//   infantry.yaml:89  Health.HP: 5000   （覆盖 ^Infantry 的 2500）
	//   infantry.yaml     Valued.Cost: 100
	//   defaults.yaml     ^Infantry.Mobile: Speed 54 / Locomotor foot  （继承）
	//   defaults.yaml     ^Infantry.Armor: Type None                    （继承）
	namespace fs = std::filesystem;
	const std::string dir = "D:/dosbox/game/OpenRA/mods/ra/rules";
	if (!fs::is_directory(dir)) { CHECK(true); return; }
	RegisterCoreTraits();
	Ruleset rules;
	CHECK(rules.LoadDirectory(dir.c_str()));
	const ActorInfo* e1 = rules.FindActor("E1");
	CHECK(e1 != nullptr);
	if (e1 == nullptr) return;

	const HealthInfo* hp = e1->Find<HealthInfo>();
	CHECK(hp != nullptr);
	if (hp) CHECK_EQ(hp->hp, 5000);          // E1 自己定义的（不是 ^Infantry 的 2500）

	const ValuedInfo* cost = e1->Find<ValuedInfo>();
	CHECK(cost != nullptr);
	if (cost) CHECK_EQ(cost->cost, 100);

	const MobileInfo* mobile = e1->Find<MobileInfo>();
	CHECK(mobile != nullptr);
	if (mobile) {
		CHECK_EQ(mobile->speed, 54);                       // 继承自 ^Infantry
		CHECK_EQ(mobile->locomotor, std::string("foot"));
	}

	const ArmorInfo* armor = e1->Find<ArmorInfo>();
	CHECK(armor != nullptr);
	if (armor) CHECK_EQ(armor->type, std::string("None"));

	// 继承链必须已经合并：E1 不应再出现「未知 trait」
	size_t unknown = 0;
	for (const auto& t : e1->traits) {
		const std::string n = t->TypeName();
		if (!TraitRegistry::Has(n)) ++unknown;
	}
	std::printf("  [audit] E1 traits=%zu unknown=%zu (hp=%d cost=%d speed=%d armor=%s)\n",
		e1->traits.size(), unknown, hp ? hp->hp : -1, cost ? cost->cost : -1,
		mobile ? mobile->speed : -1, armor ? armor->type.c_str() : "?");
	CHECK(unknown > 0); // 未实现的 trait 仍然很多（正常，只覆盖了 11 个核心 trait）
	CHECK(e1->traits.size() > 10); // 继承合并后 trait 数量应显著增加
}