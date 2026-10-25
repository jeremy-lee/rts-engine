// RTS Engine — app/main.cpp
// M0 headless 演示：
//   1) 运行两次相同命令序列，验证 StateHash 一致（确定性自检）
//   2) 验证渲染层与逻辑层解耦（NullRenderer 也能跑完整流程）
#include <cstdio>
#include <cstdint>
#include <vector>

#include "core/core.h"
#include "sim/simulation.h"
#include "render/null_renderer.h"

using namespace rts;

namespace {

// 构造固定命令序列（与真实输入无关，纯确定性）
std::vector<Command> MakeScriptedCommands() {
	std::vector<Command> cmds;
	// 生成 8 个单位
	for (int i = 0; i < 8; ++i) {
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = static_cast<uint8_t>(i % 2);
		c.x = 100 * kSubPerCell + i * 64;
		c.y = 100 * kSubPerCell;
		cmds.push_back(c);
	}
	// 向目标点移动
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

// 运行一次模拟，返回最终哈希
uint64_t RunOnce(int32_t mapW, int32_t mapH, uint64_t seed, Tick ticks) {
	Simulation sim(mapW, mapH, seed);
	auto cmds = MakeScriptedCommands();
	// 第 1 tick 前投递全部命令
	for (const auto& c : cmds) sim.Commands().Push(c);

	NullRenderer renderer;
	renderer.Initialize(640, 480, "headless");

	for (Tick t = 0; t < ticks; ++t) {
		sim.Step();
		// 渲染层每帧从世界取快照（这里简化为直接读位置）
		renderer.BeginFrame();
		for (const auto& u : sim.GetWorld().Units()) {
			SpriteDraw s;
			s.x = static_cast<float>(u.pos.x) / kSubPerCell;
			s.y = static_cast<float>(u.pos.y) / kSubPerCell;
			renderer.DrawSprite(s);
		}
		renderer.EndFrame();
	}
	return sim.GetWorld().StateHash();
}

} // namespace

int main() {
	std::printf("=== RTS Engine M0 — headless determinism check ===\n");

	constexpr int32_t kW = 256, kH = 256;
	constexpr uint64_t kSeed = 0xDEADBEEF12345678ull;
	constexpr Tick kTicks = 10000;

	const uint64_t h1 = RunOnce(kW, kH, kSeed, kTicks);
	const uint64_t h2 = RunOnce(kW, kH, kSeed, kTicks);

	std::printf("hash run #1 : %016llx\n", static_cast<unsigned long long>(h1));
	std::printf("hash run #2 : %016llx\n", static_cast<unsigned long long>(h2));

	if (h1 == h2) {
		std::printf("[PASS] determinism OK (%llu ticks, %d units)\n",
			static_cast<unsigned long long>(kTicks), 8);
		return 0;
	}
	std::printf("[FAIL] determinism broken\n");
	return 1;
}
