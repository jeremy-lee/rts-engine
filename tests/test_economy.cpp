// RTS Engine — tests/test_economy.cpp
// M5 验收：采集 → 建造 → 生产 → 战斗 闭环
#include "test_framework.h"
#include "sim/world.h"
#include "sim/simulation.h"

using namespace rts;

namespace {

Rules TestRules() {
	const std::string yaml = R"(
units:
  harvester:
    name: "采矿车"
    cost: 100
    hp: 150
    speed: 28
    harvester: true
    weapon:
      damage: 0
      range: 0
      cooldown: 60
  infantry:
    name: "步兵"
    cost: 50
    buildTime: 10
    hp: 100
    speed: 32
    weapon:
      damage: 10
      range: 8192
      cooldown: 15
buildings:
  barracks:
    name: "兵营"
    cost: 300
    buildTime: 20
    hp: 500
    width: 4
    height: 4
    produces:
      - infantry
)";
	return Rules::FromYAML(yaml);
}

} // namespace

TEST(economy_initial_credits) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, TestRules());
	CHECK_EQ(world.Credits(0), 1000);
	CHECK_EQ(world.Credits(1), 1000);
}

TEST(economy_spend_credits) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, TestRules());
	CHECK(world.SpendCredits(0, 300));
	CHECK_EQ(world.Credits(0), 700);
	CHECK(!world.SpendCredits(0, 1000)); // 余额不足
	CHECK_EQ(world.Credits(0), 700);
}

TEST(economy_harvest_gathers_credits) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	map.SetResourceAmount(CellCoord(5, 5), 500);
	World world(map, 1, TestRules());

	// 生成采矿车（unit id 0 = harvester）
	Command c;
	c.type = CommandType::Spawn;
	c.playerId = 0;
	c.x = 5 * kSubPerCell;
	c.y = 5 * kSubPerCell;
	c.targetId = 0; // harvester
	world.ApplyCommand(c);

	const Actor* h = world.Entities().All().empty() ? nullptr : &world.Entities().All()[0];
	CHECK(h != nullptr);
	CHECK(h->Has(kCompHarvester));

	// 发采集命令到矿点
	Command hv;
	hv.type = CommandType::Harvest;
	hv.entityId = h->id;
	hv.x = 5 * kSubPerCell;
	hv.y = 5 * kSubPerCell;
	world.ApplyCommand(hv);

	// 推进：采集 → 回基地卸货（基地就在矿点旁边，缩短周期）
	// 先把 basePos 设为矿点旁
	world.Entities().Get(h->id)->harvester.basePos = SubCoord(5 * kSubPerCell, 5 * kSubPerCell);

	CommandQueue q;
	for (int i = 0; i < 500; ++i) {
		world.Advance(q);
	}
	// 采集应产生资源（初始 1000，采集后 > 1000）
	CHECK(world.Credits(0) > 1000);
}

TEST(economy_build_building) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, TestRules());

	Command b;
	b.type = CommandType::Build;
	b.playerId = 0;
	b.x = 10 * kSubPerCell;
	b.y = 10 * kSubPerCell;
	b.targetId = 0; // barracks
	world.ApplyCommand(b);

	CHECK_EQ(world.Credits(0), 700); // 1000 - 300
	const auto& all = world.Entities().All();
	CHECK_EQ(all.size(), static_cast<size_t>(1));
	CHECK(all[0].Has(kCompBuilding));
	CHECK_EQ(all[0].health.hp, 500);
}

TEST(economy_produce_unit) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, TestRules());

	// 建造兵营
	Command b;
	b.type = CommandType::Build;
	b.playerId = 0;
	b.x = 10 * kSubPerCell;
	b.y = 10 * kSubPerCell;
	b.targetId = 0;
	world.ApplyCommand(b);

	const EntityId barracks = world.Entities().All()[0].id;
	const int32_t creditsBefore = world.Credits(0);

	// 生产步兵（unit numericId: 0=harvester, 1=infantry）
	Command p;
	p.type = CommandType::Produce;
	p.entityId = barracks;
	p.targetId = 1; // infantry
	world.ApplyCommand(p);

	CHECK_EQ(world.Credits(0), creditsBefore - 50);

	// 推进生产（buildTime=10）
	CommandQueue q;
	for (int i = 0; i < 30; ++i) {
		world.Advance(q);
	}
	// 应生成一个步兵（实体数 = 建筑 + 步兵）
	CHECK_EQ(world.Entities().All().size(), static_cast<size_t>(2));
	// 找到步兵（非建筑）
	bool foundInfantry = false;
	for (const auto& a : world.Entities().All()) {
		if (!a.Has(kCompBuilding) && a.alive) {
			foundInfantry = true;
			CHECK_EQ(a.health.hp, 100);
			CHECK_EQ(a.movable.speed, 32);
		}
	}
	CHECK(foundInfantry);
}

TEST(economy_full_loop_deterministic) {
	// 完整闭环（采集+建造+生产）两次运行哈希一致
	auto run = []() {
		Map map(64, 64, 0, kWalkable | kBuildable);
		map.SetResourceAmount(CellCoord(20, 20), 2000);
		World world(map, 42, TestRules());

		Command spawn;
		spawn.type = CommandType::Spawn;
		spawn.playerId = 0;
		spawn.x = 20 * kSubPerCell;
		spawn.y = 20 * kSubPerCell;
		spawn.targetId = 0;
		world.ApplyCommand(spawn);
		const EntityId h = world.Entities().All()[0].id;
		world.Entities().Get(h)->harvester.basePos = SubCoord(21 * kSubPerCell, 21 * kSubPerCell);

		Command hv;
		hv.type = CommandType::Harvest;
		hv.entityId = h;
		hv.x = 20 * kSubPerCell;
		hv.y = 20 * kSubPerCell;
		world.ApplyCommand(hv);

		Command b;
		b.type = CommandType::Build;
		b.playerId = 0;
		b.x = 30 * kSubPerCell;
		b.y = 30 * kSubPerCell;
		b.targetId = 0;
		world.ApplyCommand(b);

		CommandQueue q;
		for (int i = 0; i < 200; ++i) world.Advance(q);
		return world.StateHash();
	};
	CHECK_EQ(run(), run());
}
