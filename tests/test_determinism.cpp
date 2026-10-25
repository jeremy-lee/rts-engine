// RTS Engine — tests/test_determinism.cpp
// 核心回归测试：同种子 + 同命令序列 → 同状态哈希。
#include "test_framework.h"
#include "sim/simulation.h"
#include <vector>

using namespace rts;

namespace {

std::vector<Command> ScriptedCommands() {
	std::vector<Command> cmds;
	for (int i = 0; i < 6; ++i) {
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = static_cast<uint8_t>(i % 2);
		c.x = 50 * kSubPerCell + i * 100;
		c.y = 50 * kSubPerCell;
		cmds.push_back(c);
	}
	for (int i = 0; i < 6; ++i) {
		Command m;
		m.type = CommandType::Move;
		m.entityId = static_cast<uint32_t>(i + 1);
		m.x = 80 * kSubPerCell;
		m.y = 80 * kSubPerCell;
		cmds.push_back(m);
	}
	return cmds;
}

uint64_t RunSim(uint64_t seed, Tick ticks) {
	Simulation sim(128, 128, seed);
	for (const auto& c : ScriptedCommands()) sim.Commands().Push(c);
	sim.Run(ticks);
	return sim.GetWorld().StateHash();
}

} // namespace

TEST(determinism_same_seed_same_hash) {
	// 两次独立运行必须完全一致（跨进程/跨平台的核心保证）
	const uint64_t h1 = RunSim(123, 2000);
	const uint64_t h2 = RunSim(123, 2000);
	CHECK_EQ(h1, h2);
}

TEST(determinism_different_seed_may_differ) {
	// 不同种子不保证哈希不同（M0 世界尚未用随机数），但必须能运行
	const uint64_t h1 = RunSim(1, 100);
	const uint64_t h2 = RunSim(2, 100);
	(void)h1; (void)h2;
	CHECK(true);
}

TEST(determinism_tick_count_matters) {
	const uint64_t h100 = RunSim(7, 100);
	const uint64_t h200 = RunSim(7, 200);
	CHECK_NE(h100, h200);
}

TEST(determinism_command_order_matters) {
	// 命令顺序影响结果（锁步要求：所有客户端必须同序）
	uint64_t a, b;
	{
		Simulation sim(64, 64, 42);
		Command c1; c1.type = CommandType::Spawn; c1.x = 10 * kSubPerCell; c1.y = 10 * kSubPerCell;
		Command c2; c2.type = CommandType::Spawn; c2.x = 20 * kSubPerCell; c2.y = 20 * kSubPerCell;
		sim.Commands().Push(c1);
		sim.Commands().Push(c2);
		sim.Run(10);
		a = sim.GetWorld().StateHash();
	}
	{
		Simulation sim(64, 64, 42);
		Command c1; c1.type = CommandType::Spawn; c1.x = 10 * kSubPerCell; c1.y = 10 * kSubPerCell;
		Command c2; c2.type = CommandType::Spawn; c2.x = 20 * kSubPerCell; c2.y = 20 * kSubPerCell;
		sim.Commands().Push(c2); // 顺序反转
		sim.Commands().Push(c1);
		sim.Run(10);
		b = sim.GetWorld().StateHash();
	}
	// 反转后实体 id 分配不同 -> 哈希不同
	CHECK_NE(a, b);
}
