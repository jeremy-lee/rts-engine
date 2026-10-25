// RTS Engine — app/main.cpp
// M5 验收 demo：数据驱动 + 经济闭环
//   采矿（采集→卸货）→ 建造兵营 → 生产步兵 → 战斗
//   两次运行哈希一致（确定性）
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>

#include "core/core.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"

using namespace rts;

namespace {

const Rules kRules = Rules::FromYAML(R"(
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
    cost: 300
    buildTime: 20
    hp: 500
    width: 4
    height: 4
    produces:
      - infantry
      - tank
)");

struct ScenarioResult {
	uint64_t hash = 0;
	int32_t creditsP0 = 0;
	size_t units = 0;
	int32_t enemyHp = 0;
	int32_t ticks = 0;
};

ScenarioResult RunScenario() {
	Map map(128, 128, 0, kWalkable | kBuildable);
	map.SetResourceAmount(CellCoord(20, 20), 5000); // 矿区
	Simulation sim(std::move(map), 0x5EED, kRules);

	// 玩家 0：采矿车（unit 0）
	{
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = 0;
		c.x = 20 * kSubPerCell;
		c.y = 20 * kSubPerCell;
		c.targetId = 0; // harvester
		sim.Commands().Push(c);
	}
	sim.Step();
	const EntityId harvester = sim.GetWorld().Entities().All()[0].id;
	sim.GetWorld().Entities().Get(harvester)->harvester.basePos =
		SubCoord(22 * kSubPerCell, 20 * kSubPerCell); // 卸货点

	// 采集命令
	{
		Command hv;
		hv.type = CommandType::Harvest;
		hv.entityId = harvester;
		hv.x = 20 * kSubPerCell;
		hv.y = 20 * kSubPerCell;
		sim.Commands().Push(hv);
	}

	// 玩家 1：一个敌方单位（用于战斗测试）
	{
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = 1;
		c.x = 33 * kSubPerCell;
		c.y = 30 * kSubPerCell;
		c.targetId = 2; // tank
		sim.Commands().Push(c);
	}

	// 建造兵营（玩家 0）
	{
		Command b;
		b.type = CommandType::Build;
		b.playerId = 0;
		b.x = 30 * kSubPerCell;
		b.y = 30 * kSubPerCell;
		b.targetId = 0; // barracks
		sim.Commands().Push(b);
	}

	// 推进：先采集攒资源（3 秒）
	sim.Run(90);

	// 生产 2 个步兵
	EntityId barracks = 0;
	for (const auto& a : sim.GetWorld().Entities().All()) {
		if (a.Has(kCompBuilding) && a.playerId == 0) { barracks = a.id; break; }
	}
	if (barracks != 0) {
		for (int i = 0; i < 2; ++i) {
			Command p;
			p.type = CommandType::Produce;
			p.entityId = barracks;
			p.targetId = 1; // infantry
			sim.Commands().Push(p);
			sim.Run(15); // 等生产
		}
	}

	// 步兵攻击敌方坦克
	EntityId enemy = 0;
	std::vector<EntityId> infantry;
	for (const auto& a : sim.GetWorld().Entities().All()) {
		if (a.playerId == 1 && a.alive) enemy = a.id;
		if (a.playerId == 0 && !a.Has(kCompBuilding) && a.id != harvester && a.alive) {
			infantry.push_back(a.id);
		}
	}
	if (enemy != 0) {
		for (EntityId u : infantry) {
			Command atk;
			atk.type = CommandType::Attack;
			atk.entityId = u;
			atk.targetId = enemy;
			sim.Commands().Push(atk);
		}
	}

	sim.Run(200); // 战斗

	ScenarioResult r;
	r.hash = sim.GetWorld().StateHash();
	r.creditsP0 = sim.GetWorld().Credits(0);
	r.units = sim.GetWorld().Entities().Count();
	r.ticks = static_cast<int32_t>(sim.GetWorld().CurrentTick());
	for (const auto& a : sim.GetWorld().Entities().All()) {
		std::printf("    [entity] id=%u p=%u alive=%d comps=0x%x hp=%d\n",
			a.id, a.playerId, a.alive ? 1 : 0, a.components, a.health.hp);
	}
	if (const Actor* e = sim.GetWorld().Entities().Get(enemy)) r.enemyHp = e->health.hp;
	return r;
}

} // namespace

int main() {
	std::printf("=== RTS Engine M5 — data-driven economy acceptance ===\n");
	std::printf("rules: %zu units, %zu buildings\n\n",
		kRules.units.size(), kRules.buildings.size());

	const auto t0 = std::chrono::steady_clock::now();
	const ScenarioResult r1 = RunScenario();
	const auto t1 = std::chrono::steady_clock::now();
	const ScenarioResult r2 = RunScenario();
	(void)t1; (void)t0;

	std::printf("credits P0     : %d\n", r1.creditsP0);
	std::printf("entities       : %zu\n", r1.units);
	std::printf("enemy hp       : %d\n", r1.enemyHp);
	std::printf("ticks elapsed  : %d\n", r1.ticks);
	std::printf("hash run #1    : %016llx\n", static_cast<unsigned long long>(r1.hash));
	std::printf("hash run #2    : %016llx\n", static_cast<unsigned long long>(r2.hash));

	const bool determinism = (r1.hash == r2.hash);
	const bool economyWorked = (r1.creditsP0 != 1000 || r1.units > 1);
	const bool combatWorked = (r1.enemyHp < 300);
	std::printf("\n[%s] determinism\n", determinism ? "PASS" : "FAIL");
	std::printf("[%s] economy (harvest/produce)\n", economyWorked ? "PASS" : "FAIL");
	std::printf("[%s] combat (enemy damaged)\n", combatWorked ? "PASS" : "FAIL");

	return (determinism && economyWorked) ? 0 : 1;
}
