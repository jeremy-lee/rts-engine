// RTS Engine — tools/autotest.cpp
// 自动对战测试：让两个 AI 完整打一局，统计结果并验证确定性。
// 支持种子对称性测试（交换 AI 种子，验证公平性/阵营优势）。
// 用法：
//   rts_autotest [--runs N] [--ticks MAX] [--seed S]
//   rts_autotest --symmetric [--runs N] [--ticks MAX] [--seed S]
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
	int winner = -1;      // -1=平局, 0/1=胜者
	Tick endTick = 0;
	int p0Units = 0, p1Units = 0;
	int p0Kills = 0, p1Kills = 0;
	int peakUnits = 0;
};

// 跑一局完整对战（可指定双方 AI 种子）
RunResult RunMatch(uint64_t mapSeed, uint64_t ai0Seed, uint64_t ai1Seed, Tick maxTicks) {
	Simulation sim(MakeMap(mapSeed), mapSeed, kRules);
	sim.AddAI(0, ai0Seed);
	sim.AddAI(1, ai1Seed);
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

		if (alive0 == 0 || alive1 == 0) {
			r.winner = (alive0 == 0) ? 1 : 0;
			r.endTick = tick;
			break;
		}
	}

	for (const auto& a : sim.GetWorld().Entities().All()) {
		if (a.alive) {
			if (a.playerId == 0) ++r.p0Units;
			else ++r.p1Units;
		} else {
			if (a.playerId == 0) ++r.p1Kills;
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

const char* WinnerText(int winner, char* buf, size_t n) {
	if (winner == 0) std::snprintf(buf, n, "P0 wins");
	else if (winner == 1) std::snprintf(buf, n, "P1 wins");
	else std::snprintf(buf, n, "draw");
	return buf;
}

} // namespace

int main(int argc, char** argv) {
	int runs = 3;
	Tick maxTicks = 12000;
	uint64_t baseSeed = 101;
	bool symmetric = false;

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--runs" && i + 1 < argc) runs = std::atoi(argv[++i]);
		else if (arg == "--ticks" && i + 1 < argc) maxTicks = std::atoi(argv[++i]);
		else if (arg == "--seed" && i + 1 < argc) baseSeed = static_cast<uint64_t>(std::atoi(argv[++i]));
		else if (arg == "--symmetric") symmetric = true;
	}

	std::printf("=== RTS Autotest: AI vs AI ===\n");
	std::printf("rules: %zu units, %zu buildings | max ticks: %lld | mode: %s\n\n",
		kRules.units.size(), kRules.buildings.size(), static_cast<long long>(maxTicks),
		symmetric ? "symmetric" : "runs");

	if (symmetric) {
		// ---- 种子对称性测试 ----
		// 每对：同一地图，交换 AI 种子（A 用 s1/B 用 s2，然后 A 用 s2/B 用 s1）。
		// 若 AI 强度决定胜负 → 交换后胜负应反转（mirror）。
		// 若总是同一侧胜利 → 阵营/位置优势（side advantage）。
		int mirrorOk = 0;
		int sideAdvantage = 0;
		bool allDet = true;

		for (int i = 0; i < runs; ++i) {
			const uint64_t mapSeed = baseSeed + static_cast<uint64_t>(i) * 17;
			const uint64_t s1 = baseSeed + 1;
			const uint64_t s2 = baseSeed + 2;

			const RunResult a = RunMatch(mapSeed, s1, s2, maxTicks);
			const RunResult b = RunMatch(mapSeed, s2, s1, maxTicks);
			const RunResult a2 = RunMatch(mapSeed, s1, s2, maxTicks);
			const bool det = (a.hash == a2.hash);
			if (!det) allDet = false;

			char wa[16], wb[16];
			WinnerText(a.winner, wa, sizeof(wa));
			WinnerText(b.winner, wb, sizeof(wb));

			const bool mirror = (a.winner >= 0 && b.winner >= 0 && a.winner != b.winner);
			if (mirror) ++mirrorOk;
			else ++sideAdvantage;

			std::printf("pair %d: seed=%llu\n", i + 1, static_cast<unsigned long long>(mapSeed));
			std::printf("  base : %s  kills %2d:%-2d  units %d:%d  tick=%lld  %s\n",
				wa, a.p0Kills, a.p1Kills, a.p0Units, a.p1Units,
				static_cast<long long>(a.endTick), det ? "hash-ok" : "HASH-MISMATCH");
			std::printf("  swap : %s  kills %2d:%-2d  units %d:%d  tick=%lld  %s\n",
				wb, b.p0Kills, b.p1Kills, b.p0Units, b.p1Units,
				static_cast<long long>(b.endTick), mirror ? "mirror" : "side-adv");
		}

		std::printf("\n--- summary ---\n");
		std::printf("mirror (AI strength decides): %d/%d\n", mirrorOk, runs);
		std::printf("side advantage (position decides): %d/%d\n", sideAdvantage, runs);
		std::printf("determinism: %s\n", allDet ? "PASS" : "FAIL");

		const bool pass = allDet;
		std::printf("\n[%s] symmetric autotest\n", pass ? "PASS" : "FAIL");
		return pass ? 0 : 1;
	}

	// ---- 普通 runs 模式 ----
	int completed = 0;
	int fights = 0;
	bool allDeterministic = true;

	for (int i = 0; i < runs; ++i) {
		const uint64_t seed = baseSeed + static_cast<uint64_t>(i) * 17;
		const RunResult r = RunMatch(seed, seed + 1, seed + 2, maxTicks);
		const RunResult r2 = RunMatch(seed, seed + 1, seed + 2, maxTicks);
		const bool deterministic = (r.hash == r2.hash);
		if (!deterministic) allDeterministic = false;

		char wt[16];
		WinnerText(r.winner, wt, sizeof(wt));
		std::printf("run %d: seed=%llu  %-8s  tick=%-6lld  P0=%d P1=%d  peak=%d  kills %d:%d  %s\n",
			i + 1, static_cast<unsigned long long>(seed), wt,
			static_cast<long long>(r.endTick), r.p0Units, r.p1Units, r.peakUnits,
			r.p0Kills, r.p1Kills, deterministic ? "hash-ok" : "HASH-MISMATCH");
		if (deterministic) ++completed;
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
