// RTS Engine — tests/test_upgrade.cpp
// M19 验收：科技升级（攻/防/速）
#include "test_framework.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"

using namespace rts;

namespace {

Rules UpgradeRules() {
	return Rules::FromYAML(R"(
upgrade:
  cost: 300
  attack: 5
  hp: 50
  speed: 2
units:
  infantry:
    name: "步兵"
    cost: 50
    buildTime: 10
    hp: 100
    speed: 32
    weapon:
      damage: 10
      range: 4096
      cooldown: 15
)");
}

} // namespace

TEST(upgrade_def_parsed) {
	const Rules rules = UpgradeRules();
	CHECK_EQ(rules.upgrade.cost, 300);
	CHECK_EQ(rules.upgrade.attack, 5);
	CHECK_EQ(rules.upgrade.hp, 50);
	CHECK_EQ(rules.upgrade.speed, 2);
}

TEST(upgrade_applies_to_units) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, UpgradeRules());
	const EntityId id = world.Entities().Create(0,
		SubCoord(10 * kSubPerCell, 10 * kSubPerCell),
		kCompMovable | kCompHealth | kCompArmament);
	Actor* a = world.Entities().Get(id);
	a->health.hp = 100;
	a->health.maxHp = 100;
	a->armament.damage = 10;
	a->movable.speed = 32;

	world.AddCredits(0, 1000);
	const int32_t creditsBefore = world.Credits(0);

	Command up;
	up.type = CommandType::Upgrade;
	up.playerId = 0;
	world.ApplyCommand(up);

	CHECK_EQ(world.Credits(0), creditsBefore - 300);
	CHECK_EQ(world.Entities().Get(id)->armament.damage, 15);
	CHECK_EQ(world.Entities().Get(id)->health.maxHp, 150);
	CHECK_EQ(world.Entities().Get(id)->health.hp, 150);
	CHECK_EQ(world.Entities().Get(id)->movable.speed, 34);
}

TEST(upgrade_requires_credits) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, UpgradeRules());
	const EntityId id = world.Entities().Create(0,
		SubCoord(10 * kSubPerCell, 10 * kSubPerCell), kCompHealth | kCompArmament);
	world.Entities().Get(id)->armament.damage = 10;
	CHECK(world.SpendCredits(0, 900)); // 初始 1000 → 剩 100（不足 300）

	Command up;
	up.type = CommandType::Upgrade;
	up.playerId = 0;
	world.ApplyCommand(up);

	CHECK_EQ(world.Entities().Get(id)->armament.damage, 10); // 未升级
	CHECK_EQ(world.Credits(0), 100);
}

TEST(upgrade_stacks) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, UpgradeRules());
	const EntityId id = world.Entities().Create(0,
		SubCoord(10 * kSubPerCell, 10 * kSubPerCell), kCompHealth | kCompArmament);
	world.Entities().Get(id)->armament.damage = 10;
	world.AddCredits(0, 2000);

	for (int i = 0; i < 2; ++i) {
		Command up;
		up.type = CommandType::Upgrade;
		up.playerId = 0;
		world.ApplyCommand(up);
	}
	CHECK_EQ(world.Entities().Get(id)->armament.damage, 20); // 10 + 5*2
	CHECK_EQ(world.Upgrades(0).attackBonus, 10);
}

TEST(upgrade_deterministic) {
	auto run = []() {
		Map map(32, 32, 0, kWalkable | kBuildable);
		World world(map, 1, UpgradeRules());
		world.Entities().Create(0, SubCoord(10 * kSubPerCell, 10 * kSubPerCell),
			kCompMovable | kCompHealth | kCompArmament);
		world.AddCredits(0, 1000);
		Command up;
		up.type = CommandType::Upgrade;
		up.playerId = 0;
		world.ApplyCommand(up);
		return world.StateHash();
	};
	CHECK_EQ(run(), run());
}
