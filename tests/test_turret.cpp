// RTS Engine — tests/test_turret.cpp
// M19 验收：防御塔（建筑自动攻击）
#include "test_framework.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"

using namespace rts;

namespace {

Rules TurretRules() {
	return Rules::FromYAML(R"(
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
buildings:
  turret:
    name: "防御塔"
    cost: 200
    buildTime: 15
    hp: 400
    width: 2
    height: 2
    turret: true
    weapon:
      damage: 20
      range: 6144
      cooldown: 45
)");
}

} // namespace

TEST(turret_def_parsed) {
	const Rules rules = TurretRules();
	const BuildingDef* def = rules.FindBuilding("turret");
	CHECK(def != nullptr);
	CHECK(def->turret);
	CHECK_EQ(def->weapon.damage, 20);
	CHECK_EQ(def->weapon.range, 6144);
	CHECK_EQ(def->weapon.cooldown, static_cast<Tick>(45));
	CHECK_EQ(def->width, 2);
	CHECK_EQ(def->height, 2);
}

TEST(turret_builds_with_armament) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, TurretRules());
	world.AddCredits(0, 500);

	Command b;
	b.type = CommandType::Build;
	b.playerId = 0;
	b.x = 10 * kSubPerCell;
	b.y = 10 * kSubPerCell;
	b.targetId = 0; // turret (first building)
	world.ApplyCommand(b);

	CHECK_EQ(world.Entities().Count(), static_cast<size_t>(1));
	const Actor& t = world.Entities().All()[0];
	CHECK(t.Has(kCompBuilding));
	CHECK(t.Has(kCompArmament));
	CHECK_EQ(t.armament.damage, 20);
}

TEST(turret_attacks_enemy_in_range) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, TurretRules());
	world.AddCredits(0, 500);

	// 建造防御塔
	Command b;
	b.type = CommandType::Build;
	b.playerId = 0;
	b.x = 10 * kSubPerCell;
	b.y = 10 * kSubPerCell;
	b.targetId = 0;
	world.ApplyCommand(b);

	// 敌人在射程内（防御塔中心 (11,11)，敌人在 (13,11) 距离约 2 格）
	const EntityId enemy = world.Entities().Create(1,
		SubCoord(13 * kSubPerCell, 11 * kSubPerCell), kCompMovable | kCompHealth);
	world.Entities().Get(enemy)->health.hp = 100;

	const int32_t hpBefore = world.Entities().Get(enemy)->health.hp;
	// 跑几个 tick 让防御塔开火
	for (int i = 0; i < 60; ++i) {
		CommandQueue q;
		world.Advance(q);
	}
	const int32_t hpAfter = world.Entities().Get(enemy)->health.hp;
	CHECK(hpAfter < hpBefore);
}

TEST(turret_ignores_out_of_range) {
	Map map(64, 64, 0, kWalkable | kBuildable);
	World world(map, 1, TurretRules());
	world.AddCredits(0, 500);

	Command b;
	b.type = CommandType::Build;
	b.playerId = 0;
	b.x = 10 * kSubPerCell;
	b.y = 10 * kSubPerCell;
	b.targetId = 0;
	world.ApplyCommand(b);

	// 敌人很远（40,40）
	const EntityId enemy = world.Entities().Create(1,
		SubCoord(40 * kSubPerCell, 40 * kSubPerCell), kCompMovable | kCompHealth);
	world.Entities().Get(enemy)->health.hp = 100;

	for (int i = 0; i < 60; ++i) {
		CommandQueue q;
		world.Advance(q);
	}
	CHECK_EQ(world.Entities().Get(enemy)->health.hp, 100);
}

TEST(turret_attack_deterministic) {
	auto run = []() {
		Map map(32, 32, 0, kWalkable | kBuildable);
		World world(map, 1, TurretRules());
		world.AddCredits(0, 500);
		Command b;
		b.type = CommandType::Build;
		b.playerId = 0;
		b.x = 10 * kSubPerCell;
		b.y = 10 * kSubPerCell;
		b.targetId = 0;
		world.ApplyCommand(b);
		const EntityId enemy = world.Entities().Create(1,
			SubCoord(13 * kSubPerCell, 11 * kSubPerCell), kCompMovable | kCompHealth);
		world.Entities().Get(enemy)->health.hp = 500;
		for (int i = 0; i < 200; ++i) {
			CommandQueue q;
			world.Advance(q);
		}
		return world.StateHash();
	};
	CHECK_EQ(run(), run());
}
