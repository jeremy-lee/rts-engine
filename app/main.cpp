// RTS Engine — app/main.cpp
// M4 验收 demo：1000 单位穿越 256x256 地图（A* 寻路 + 群体分离）。
//   1) 性能：每 tick 平均耗时
//   2) 无穿墙：所有单位位于可通行格
//   3) 可复现：两次运行状态哈希一致
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <vector>

#include "core/core.h"
#include "sim/simulation.h"
#include "sim/replay.h"

using namespace rts;

namespace {

constexpr int32_t kMapW = 256, kMapH = 256;
constexpr int32_t kUnitsPerSide = 500; // 500 + 500 = 1000 单位
constexpr Tick kTicks = 600;
constexpr uint64_t kSeed = 0xA11CE;

void BuildCommands(Simulation& sim) {
	for (int i = 0; i < kUnitsPerSide; ++i) {
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = 0;
		c.x = (10 + (i % 10) * 2) * kSubPerCell;
		c.y = (20 + (i / 10) * 4) * kSubPerCell;
		sim.Commands().Push(c);

		Command c2 = c;
		c2.playerId = 1;
		c2.x = (240 - (i % 10) * 2) * kSubPerCell;
		c2.y = (20 + (i / 10) * 4) * kSubPerCell;
		sim.Commands().Push(c2);
	}
	sim.Step();

	for (int i = 0; i < kUnitsPerSide * 2; ++i) {
		Command m;
		m.type = CommandType::Move;
		m.entityId = static_cast<uint32_t>(i + 1);
		m.x = (120 + (i % 12)) * kSubPerCell;
		m.y = (120 + (i / 12) % 12) * kSubPerCell;
		sim.Commands().Push(m);
	}
}

struct RunResult {
	uint64_t hash = 0;
	double avgTickMs = 0.0;
	int outOfBounds = 0;
	int notWalkable = 0;
	int moving = 0;
	int idle = 0;
};

RunResult RunOnce(const Map& map, Tick ticks) {
	Simulation sim(map, kSeed);
	BuildCommands(sim);

	RunResult r;
	double totalMs = 0.0;
	for (Tick t = 0; t < ticks; ++t) {
		const auto t0 = std::chrono::steady_clock::now();
		sim.Step();
		const auto t1 = std::chrono::steady_clock::now();
		totalMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
	}

	for (const auto& a : sim.GetWorld().Entities().All()) {
		if (!a.alive) continue;
		if (a.activity.type == ActivityType::MoveTo) ++r.moving;
		else ++r.idle;
		const CellCoord c = a.pos.ToCell();
		if (!sim.GetWorld().GetMap().InBounds(c)) ++r.outOfBounds;
		else if (!sim.GetWorld().GetMap().Walkable(c)) ++r.notWalkable;
	}

	r.hash = sim.GetWorld().StateHash();
	r.avgTickMs = totalMs / static_cast<double>(ticks + 1);
	return r;
}

} // namespace

int main() {
	std::printf("=== RTS Engine M4 — pathfinding acceptance ===\n");

	const Map map = Map::GenerateTestMap(kMapW, kMapH, 2024);
	std::printf("map: %dx%d, units: %d, ticks: %lld\n\n",
		kMapW, kMapH, kUnitsPerSide * 2, static_cast<long long>(kTicks));

	const auto t0 = std::chrono::steady_clock::now();
	const RunResult r1 = RunOnce(map, kTicks);
	const auto t1 = std::chrono::steady_clock::now();
	const double totalSec = std::chrono::duration<double>(t1 - t0).count();

	const RunResult r2 = RunOnce(map, kTicks);

	std::printf("units: %d  (moving=%d idle=%d)\n", kUnitsPerSide * 2, r1.moving, r1.idle);
	std::printf("avg tick: %.3f ms  (total %.2f s for %lld ticks)\n",
		r1.avgTickMs, totalSec, static_cast<long long>(kTicks));
	std::printf("out-of-bounds: %d   not-walkable: %d\n", r1.outOfBounds, r1.notWalkable);
	std::printf("hash run #1: %016llx\n", static_cast<unsigned long long>(r1.hash));
	std::printf("hash run #2: %016llx\n", static_cast<unsigned long long>(r2.hash));

	const bool determinism = (r1.hash == r2.hash);
	const bool noClip = (r1.outOfBounds == 0 && r1.notWalkable == 0);
	const bool perfOk = (r1.avgTickMs < 33.3);
	std::printf("\n[%s] determinism\n", determinism ? "PASS" : "FAIL");
	std::printf("[%s] no wall clipping\n", noClip ? "PASS" : "FAIL");
	std::printf("[%s] performance (avg tick < 33.3ms)\n", perfOk ? "PASS" : "FAIL");

	return (determinism && noClip) ? 0 : 1;
}
