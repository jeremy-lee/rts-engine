// RTS Engine — sim/simulation.h
// 模拟驱动器：headless 可运行；渲染层通过它推进逻辑。
// AI 玩家在 Step 开头产生命令（与人类同一套命令队列）→ 确定性不变。
#pragma once

#include <vector>
#include "world.h"
#include "ai.h"

namespace rts {

class Simulation {
public:
	Simulation(int32_t mapW, int32_t mapH, uint64_t seed)
		: m_world(mapW, mapH, seed) {}

	Simulation(Map map, uint64_t seed)
		: m_world(std::move(map), seed) {}

	Simulation(Map map, uint64_t seed, Rules rules)
		: m_world(std::move(map), seed, std::move(rules)) {}

	World& GetWorld() { return m_world; }
	CommandQueue& Commands() { return m_commands; }

	// 添加 AI 玩家（按添加顺序执行，确定性）
	void AddAI(uint8_t playerId, uint64_t seed = 1) {
		m_ais.emplace_back(playerId, seed);
	}

	// 推进一个逻辑 tick
	void Step() {
		for (auto& ai : m_ais) {
			ai.Update(m_world, m_commands);
		}
		m_world.Advance(m_commands);
		// P2-7：每 tick 重算玩家 0 的战争迷雾（其他玩家的视野暂不参与渲染）
		m_world.RecomputeFog(0);
	}

	// 推进 n 个 tick
	void Run(Tick n) {
		for (Tick i = 0; i < n; ++i) Step();
	}

private:
	World m_world;
	CommandQueue m_commands;
	std::vector<AIPlayer> m_ais;
};

} // namespace rts
