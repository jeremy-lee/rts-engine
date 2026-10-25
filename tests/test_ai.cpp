// RTS Engine — tests/test_ai.cpp
// M6 验收：AI 能完整打一局（建造→采矿→生产→进攻），且确定性
#include "test_framework.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"
#include <cstdio>

using namespace rts;

namespace {

Rules AIRules() {
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
      range: 4096
      cooldown: 15
  tank:
    name: "坦克"
    cost: 150
    buildTime: 30
    hp: 300
    speed: 20
    weapon:
      damage: 25
      range: 5120
      cooldown: 30
buildings:
  barracks:
    name: "兵营"
    cost: 100
    buildTime: 20
    hp: 500
    width: 4
    height: 4
    produces:
      - infantry
      - tank
)";
	return Rules::FromYAML(yaml);
}

Map AIMap() {
	Map map(128, 128, 0, kWalkable | kBuildable);
	// 两个矿区
	map.SetResourceAmount(CellCoord(20, 20), 5000);
	map.SetResourceAmount(CellCoord(100, 100), 5000);
	return map;
}

} // namespace

TEST(ai_builds_and_economy) {
	Simulation sim(AIMap(), 0x1234, AIRules());
	sim.AddAI(0, 7);
	sim.GetWorld().AddCredits(0, 1000); // 初始资源

	sim.Run(600); // 20 秒


	// 应有建筑（兵营）
	int buildings = 0;
	int harvesters = 0;
	for (const auto& a : sim.GetWorld().Entities().All()) {
		if (!a.alive || a.playerId != 0) continue;
		if (a.Has(kCompBuilding)) ++buildings;
		if (a.Has(kCompHarvester)) ++harvesters;
	}
	CHECK(buildings >= 1);
	CHECK(harvesters >= 1);
}

TEST(ai_produces_units) {
	Simulation sim(AIMap(), 0x5678, AIRules());
	sim.AddAI(0, 11);
	sim.GetWorld().AddCredits(0, 2000);

	sim.Run(900); // 30 秒

	int combat = 0;
	for (const auto& a : sim.GetWorld().Entities().All()) {
		if (!a.alive || a.playerId != 0) continue;
		if (!a.Has(kCompBuilding) && !a.Has(kCompHarvester) &&
			a.Has(kCompArmament) && a.armament.damage > 0) {
			++combat;
		}
	}
	CHECK(combat >= 1);
}

TEST(ai_attacks_enemy) {
	Map map = AIMap();
	Simulation sim(std::move(map), 0x9ABC, AIRules());
	sim.AddAI(0, 13);
	sim.GetWorld().AddCredits(0, 3000);
	sim.GetWorld().AddCredits(1, 0);

	// 预置一个敌方单位（玩家 1）
	{
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = 1;
		c.x = 30 * kSubPerCell;
		c.y = 30 * kSubPerCell;
		c.targetId = 2; // tank
		sim.Commands().Push(c);
	}
	sim.Step();
	EntityId enemy = 0;
	for (const auto& a : sim.GetWorld().Entities().All()) {
		if (a.playerId == 1) { enemy = a.id; break; }
	}
	CHECK(enemy != 0);
	const int32_t hpBefore = sim.GetWorld().Entities().Get(enemy)->health.hp;

	sim.Run(1500);
	const int32_t hpAfter = sim.GetWorld().Entities().Get(enemy)->health.hp;
	CHECK(hpAfter < hpBefore);
}

TEST(ai_deterministic) {
	auto run = []() {
		Simulation sim(AIMap(), 0xDEAD, AIRules());
		sim.AddAI(0, 21);
		sim.AddAI(1, 22);
		sim.GetWorld().AddCredits(0, 1500);
		sim.GetWorld().AddCredits(1, 1500);
		sim.Run(800);
		return sim.GetWorld().StateHash();
	};
	CHECK_EQ(run(), run());
}

TEST(ai_vs_ai_game) {
	// AI vs AI 完整对局：双方都应有发展
	Simulation sim(AIMap(), 0xFFFF, AIRules());
	sim.AddAI(0, 31);
	sim.AddAI(1, 32);
	sim.GetWorld().AddCredits(0, 2000);
	sim.GetWorld().AddCredits(1, 2000);

	sim.Run(2000); // 66 秒

	int p0units = 0, p1units = 0;
	for (const auto& a : sim.GetWorld().Entities().All()) {
		if (!a.alive) continue;
		if (a.playerId == 0 && !a.Has(kCompBuilding)) ++p0units;
		if (a.playerId == 1 && !a.Has(kCompBuilding)) ++p1units;
	}
	std::printf("    [ai-vs-ai] p0 units=%d, p1 units=%d, entities=%zu\n",
		p0units, p1units, sim.GetWorld().Entities().Count());
	CHECK(p0units >= 1);
	CHECK(p1units >= 1);
}
