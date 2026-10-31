// RTS Engine — tests/test_rules.cpp
// 验证 MiniYaml 解析与 OpenRA Ruleset 加载。
#include "test_framework.h"

#include "sim/miniyaml.h"
#include "sim/rules.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace rts;

TEST(miniyaml_basic_nesting) {
	const char* text =
		"Actors:\n"
		"\tE1:\n"
		"\t\tMobile:\n"
		"\t\t\tSpeed: 56\n"
		"\t\tHealth:\n"
		"\t\t\tHP: 50\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	CHECK_EQ(root.children.size(), static_cast<size_t>(1));
	const YamlNode* actors = root.Child("Actors");
	CHECK(actors != nullptr);
	const YamlNode* e1 = actors->Child("E1");
	CHECK(e1 != nullptr);
	const YamlNode* mobile = e1->Child("Mobile");
	CHECK(mobile != nullptr);
	const YamlNode* speed = mobile->Child("Speed");
	CHECK(speed != nullptr);
	CHECK_EQ(speed->value, std::string("56"));
}

TEST(miniyaml_variant_and_list) {
	const char* text =
		"Rules:\n"
		"\tArmament@PRIMARY:\n"
		"\t\tWeapon: m60mg\n"
		"\tInherits: ^Infantry\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	const YamlNode* rules = root.Child("Rules");
	CHECK(rules != nullptr);
	const YamlNode* arm = rules->VariantChild("Armament", "PRIMARY");
	CHECK(arm != nullptr);
	const YamlNode* wpn = arm->Child("Weapon");
	CHECK(wpn != nullptr);
	CHECK_EQ(wpn->value, std::string("m60mg"));

	const auto list = MiniYaml::SplitList("^Infantry, ^Mobile");
	CHECK_EQ(list.size(), static_cast<size_t>(2));
	CHECK_EQ(list[0], std::string("^Infantry"));
	CHECK_EQ(list[1], std::string("^Mobile"));
}

TEST(miniyaml_inline_comment) {
	const char* text = "Speed: 56 # units per tick\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	CHECK_EQ(root.children.size(), static_cast<size_t>(1));
	CHECK_EQ(root.children[0].value, std::string("56"));
}

TEST(rules_load_ra_infantry) {
	// OpenRA 的 RA mod 里每个 actor 都是一组 trait
	const char* text =
		"E1:\n"
		"\tInherits: ^Infantry\n"
		"\tMobile:\n"
		"\t\tSpeed: 56\n"
		"\tHealth:\n"
		"\t\tHP: 50\n"
		"^Infantry:\n"
		"\tMobile:\n"
		"\t\tSpeed: 43\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	Ruleset rules;
	rules.LoadFromNodes(root);
	CHECK_EQ(rules.actors.size(), static_cast<size_t>(2));
	CHECK(rules.FindActor("E1") != nullptr);
	CHECK(rules.FindActor("^Infantry") != nullptr);
	CHECK(rules.FindActor("^Infantry")->IsAbstract());
	CHECK(!rules.FindActor("E1")->IsAbstract());
	CHECK_EQ(rules.ConcreteActorCount(), static_cast<size_t>(1));
	// trait 被保留（未注册的走 UnknownTrait）
	CHECK(rules.FindActor("E1")->Find("Mobile") != nullptr);
	CHECK(rules.FindActor("E1")->Find("Health") != nullptr);
	CHECK(rules.FindActor("E1")->Find("Nonexistent") == nullptr);
}
TEST(rules_load_real_ra_mod) {
	// 真实数据验证：加载 OpenRA RA mod 的 rules 目录（若本机存在）
	namespace fs = std::filesystem;
	const std::string dir = "D:/dosbox/game/OpenRA/mods/ra/rules";
	if (!fs::is_directory(dir)) {
		// 没有 mod 数据时跳过（不视为失败）
		CHECK(true);
		return;
	}
	Ruleset rules;
	std::string err;
	const bool ok = rules.LoadDirectory(dir.c_str(), &err);
	CHECK(ok);
	if (!ok) { std::printf("  [rules] load error: %s\n", err.c_str()); return; }
	std::printf("  [rules] RA actors=%zu (concrete=%zu)\n",
		rules.actors.size(), rules.ConcreteActorCount());
	CHECK(rules.actors.size() > 100);
	CHECK(rules.ConcreteActorCount() > 50);
	// 几个必定存在的 actor 与抽象基类
	CHECK(rules.FindActor("E1") != nullptr);      // 步枪兵
	CHECK(rules.FindActor("^Infantry") != nullptr);
	CHECK(rules.FindActor("E1")->IsAbstract() == false);
	CHECK(rules.FindActor("^Infantry")->IsAbstract() == true);
	// 每个 actor 至少有一条 trait（Inherits 不算 trait）
	const ActorInfo* e1 = rules.FindActor("E1");
	CHECK(e1->traits.size() > 0);
}