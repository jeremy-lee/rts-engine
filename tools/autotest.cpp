// RTS Engine — tools/autotest.cpp
// 自动对战测试：让两个 AI 完整打一局，统计结果并验证确定性。
// 用法：rts_autotest [--runs N] [--ticks MAX] [--seed S]
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "core/core.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"
#include "sim/map.h"

using namespace rts;

namespace {

const Rules kRules = Rules::FromYAML(R"(
upgrade:
  cost: 300
  attack: 5
  hp: 50
  speed: 2
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

Map MakeMap(uint64_t seed) {
	Map map = Map::GenerateTestMap(192, 192, seed);
	map.SetResourceAmount(CellCoord(30, 30), 12000);
	map.SetResourceAmount(CellCoord(160, 160), 12000);
	return map;
}

struct RunResult {
	uint64_t hash = 0;
	int winner = -1;      // -1=超时/平局, 0/1=胜者
	Tick endTick = 0;
	int p0Units = 0, p1Units = 0;
	int p0Kills = 0, p1Kills = 0;
	int peakUnits = 0;
};

// 跑一局完整对战
RunResult RunMatch(uint64_t seed, Tick maxTicks) {
	Simulation sim(MakeMap(seed), seed, kRules);
	sim.AddAI(0, seed + 1);
	sim.AddAI(1, seed + 2);
	sim.GetWorld().AddCredits(0, 2000);
	sim.GetWorld().AddCredits(1, 2000);

	// 开局单位（避免第 1 tick 因"无单位"误判全灭）
	auto addStartingUnit = [&](uint8_t player, CellCoord pos) {
		const EntityId id = sim.GetWorld().Entities().Create(player,
			SubCoord(pos.x * kSubPerCell, pos.y * kSubPerCell),
			kCompMovable | kCompHealth | kCompArmament | kCompVision);
		Actor* a = sim.GetWorld().Entities().Get(id);
		a->health.hp = 100;
		a->health.maxHp = 100;
		a->movable.speed = 32;
		a->armament.damage = 10;
		a->armament.range = 4096;
		a->armament.cooldown = 15;
		a->vision.range = 8 * kSubPerCell;
	};
	addStartingUnit(0, CellCoord(40, 96));
	addStartingUnit(1, CellCoord(152, 96));

	RunResult r;
	int prevDead0 = 0, prevDead1 = 0;
	(void)prevDead0;
	(void)prevDead1;

	for (Tick t = 0; t < maxTicks; ++t) {
		sim.Step();
		const Tick tick = sim.GetWorld().CurrentTick();

		int alive0 = 0, alive1 = 0;
		for (const auto& a : sim.GetWorld().Entities().All()) {
			if (!a.alive) continue;
			if (a.playerId == 0) ++alive0;
			else ++alive1;
		}
		r.peakUnits = std::max(r.peakUnits, alive0 + alive1);

		// 一方全灭 → 结束
		if (alive0 == 0 || alive1 == 0) {
			r.winner = (alive0 == 0) ? 1 : 0;
			r.endTick = tick;
			break;
		}
		if (tick % 600 == 0 && tick > 0) {
			// 无操作
		}
	}

	// 统计
	for (const auto& a : sim.GetWorld().Entities().All()) {
		if (a.alive) {
			if (a.playerId == 0) ++r.p0Units;
			else ++r.p1Units;
		} else {
			// 死亡实体归属方
			if (a.playerId == 0) ++r.p1Kills; // 玩家0 死亡 = 玩家1 的击杀
			else ++r.p0Kills;
		}
	}
	if (r.endTick == 0) r.endTick = sim.GetWorld().CurrentTick();
	// 超时按击杀数判胜负
	if (r.winner < 0) {
		if (r.p0Kills > r.p1Kills) r.winner = 0;
		else if (r.p1Kills > r.p0Kills) r.winner = 1;
	}
	r.hash = sim.GetWorld().StateHash();
	return r;
}

} // namespace

int main(int argc, char** argv) {
	int runs = 3;
	Tick maxTicks = 12000; // 400 秒模拟
	uint64_t baseSeed = 101;

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--runs" && i + 1 < argc) runs = std::atoi(argv[++i]);
		else if (arg == "--ticks" && i + 1 < argc) maxTicks = std::atoi(argv[++i]);
		else if (arg == "--seed" && i + 1 < argc) baseSeed = static_cast<uint64_t>(std::atoi(argv[++i]));
	}

	std::printf("=== RTS Autotest: AI vs AI ===\n");
	std::printf("rules: %zu units, %zu buildings | runs: %d | max ticks: %lld\n\n",
		kRules.units.size(), kRules.buildings.size(), runs,
		static_cast<long long>(maxTicks));

	int completed = 0;
	int decided = 0;
	int fights = 0;
	bool allDeterministic = true;

	for (int i = 0; i < runs; ++i) {
		const uint64_t seed = baseSeed + static_cast<uint64_t>(i) * 17;
		const RunResult r = RunMatch(seed, maxTicks);

		// 确定性：同种子跑第二遍
		const RunResult r2 = RunMatch(seed, maxTicks);
		const bool deterministic = (r.hash == r2.hash);
		if (!deterministic) allDeterministic = false;

		char winnerText[16];
		if (r.winner == 0) std::snprintf(winnerText, sizeof(winnerText), "P0 wins");
		else if (r.winner == 1) std::snprintf(winnerText, sizeof(winnerText), "P1 wins");
		else std::snprintf(winnerText, sizeof(winnerText), "timeout");

		std::printf("run %d: seed=%llu  %-8s  tick=%-6lld  P0=%d P1=%d  peak=%d  kills %d:%d  %s\n",
			i + 1, static_cast<unsigned long long>(seed), winnerText,
			static_cast<long long>(r.endTick), r.p0Units, r.p1Units, r.peakUnits,
			r.p0Kills, r.p1Kills, deterministic ? "hash-ok" : "HASH-MISMATCH");
		if (deterministic) ++completed;
		if (r.winner >= 0) ++decided;
		if (r.p0Kills + r.p1Kills > 0) ++fights;
	}

	std::printf("\n--- summary ---\n");
	std::printf("runs completed (deterministic): %d/%d\n", completed, runs);
	std::printf("matches with combat: %d/%d\n", fights, runs);
	std::printf("determinism: %s\n", allDeterministic ? "PASS" : "FAIL");

	const bool pass = (completed == runs) && allDeterministic && (fights == runs);
	std::printf("\n[%s] autotest\n", pass ? "PASS" : "FAIL");
	return pass ? 0 : 1;
}
