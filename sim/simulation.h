// RTS Engine — sim/simulation.h
// 模拟驱动器：headless 可运行；渲染层通过它推进逻辑。
#pragma once

#include "world.h"

namespace rts {

class Simulation {
public:
	Simulation(int32_t mapW, int32_t mapH, uint64_t seed)
		: m_world(mapW, mapH, seed) {}

	World& GetWorld() { return m_world; }
	CommandQueue& Commands() { return m_commands; }

	// 推进一个逻辑 tick
	void Step() { m_world.Advance(m_commands); }

	// 推进 n 个 tick
	void Run(Tick n) {
		for (Tick i = 0; i < n; ++i) Step();
	}

private:
	World m_world;
	CommandQueue m_commands;
};

} // namespace rts
