// RTS Engine — app/main.cpp
// M1 headless 演示：
//   1) 地图生成/保存/加载（地形/可通行/迷雾）
//   2) 确定性自检（同命令序列两次运行状态哈希一致）
//   3) 投影/逆投影往返自检
#include <cstdio>
#include <cstdint>
#include <vector>

#include "core/core.h"
#include "sim/simulation.h"
#include "render/null_renderer.h"
#include "render/projection.h"

using namespace rts;

namespace {

std::vector<Command> MakeScriptedCommands() {
	std::vector<Command> cmds;
	for (int i = 0; i < 8; ++i) {
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = static_cast<uint8_t>(i % 2);
		c.x = 100 * kSubPerCell + i * 64;
		c.y = 100 * kSubPerCell;
		cmds.push_back(c);
	}
	for (int i = 0; i < 8; ++i) {
		Command m;
		m.type = CommandType::Move;
		m.entityId = static_cast<uint32_t>(i + 1);
		m.x = 128 * kSubPerCell;
		m.y = 128 * kSubPerCell;
		cmds.push_back(m);
	}
	return cmds;
}

uint64_t RunOnce(Map map, uint64_t seed, Tick ticks) {
	Simulation sim(std::move(map), seed);
	for (const auto& c : MakeScriptedCommands()) sim.Commands().Push(c);

	NullRenderer renderer;
	renderer.Initialize(640, 480, "headless");

	for (Tick t = 0; t < ticks; ++t) {
		sim.Step();
		renderer.BeginFrame();
		const auto& units = sim.GetWorld().Units();
		for (const auto& u : units) {
			// 投影到屏幕（渲染层用）
			const ScreenPos p = ProjectCell(u.pos.ToCell(), 0);
			SpriteDraw s;
			s.x = static_cast<float>(p.x);
			s.y = static_cast<float>(p.y);
			renderer.DrawSprite(s);
		}
		renderer.EndFrame();
	}
	return sim.GetWorld().StateHash();
}

void DemoMap() {
	std::printf("\n=== Map (M1) ===\n");
	constexpr int32_t kW = 256, kH = 256;
	const Map generated = Map::GenerateTestMap(kW, kH, 2024);

	// 统计
	int water = 0, rock = 0, highland = 0;
	for (int32_t y = 0; y < kH; ++y) {
		for (int32_t x = 0; x < kW; ++x) {
			const CellCoord c(x, y);
			if (generated.Water(c)) ++water;
			else if (!generated.Walkable(c)) ++rock;
			if (generated.TerrainHeight(c) > 0) ++highland;
		}
	}
	std::printf("size            : %d x %d (%zu cells)\n", kW, kH, generated.CellCount());
	std::printf("water cells     : %d\n", water);
	std::printf("blocked cells   : %d\n", rock);
	std::printf("highland cells  : %d\n", highland);

	// 保存/加载往返
	const char* path = "demo_map.map";
	if (generated.SaveToText(path)) {
		Map loaded;
		if (loaded.LoadFromText(path) && loaded.Hash() == generated.Hash()) {
			std::printf("[PASS] map text roundtrip hash equal\n");
		} else {
			std::printf("[FAIL] map roundtrip hash mismatch\n");
		}
		std::remove(path);
	}

	// 投影往返
	int mismatch = 0;
	for (int32_t y = 0; y < 64; ++y) {
		for (int32_t x = 0; x < 64; ++x) {
			const CellCoord c(x, y);
			const CellCoord back = ScreenToCell(ProjectCell(c, 0));
			if (back.x != c.x || back.y != c.y) ++mismatch;
		}
	}
	std::printf("[%s] projection roundtrip (64x64 sample), mismatches=%d\n",
		mismatch == 0 ? "PASS" : "FAIL", mismatch);
}

} // namespace

int main() {
	std::printf("=== RTS Engine M1 — headless demo ===\n");

	DemoMap();

	std::printf("\n=== Determinism (M0 regression) ===\n");
	constexpr uint64_t kSeed = 0xDEADBEEF12345678ull;
	constexpr Tick kTicks = 10000;
	const Map map = Map::GenerateTestMap(256, 256, 2024);

	const uint64_t h1 = RunOnce(map, kSeed, kTicks);
	const uint64_t h2 = RunOnce(map, kSeed, kTicks);
	std::printf("hash run #1 : %016llx\n", static_cast<unsigned long long>(h1));
	std::printf("hash run #2 : %016llx\n", static_cast<unsigned long long>(h2));
	if (h1 != h2) {
		std::printf("[FAIL] determinism broken\n");
		return 1;
	}
	std::printf("[PASS] determinism OK (%llu ticks, %d units)\n",
		static_cast<unsigned long long>(kTicks), 8);
	return 0;
}
