// RTS Engine — sim/world.h
// 世界：固定步长模拟的唯一状态容器。M0 版本聚焦「确定性验证」：
// 单位直线移动 + 状态哈希 + 命令驱动，足以证明地基正确。
#pragma once

#include <cstdint>
#include <vector>
#include "core/core.h"
#include "command.h"
#include "command_queue.h"

namespace rts {

struct Unit {
	uint32_t id = 0;
	uint8_t playerId = 0;
	bool alive = true;
	bool moving = false;
	SubCoord pos;     // 子格坐标
	SubCoord target;  // 目标子格坐标
	int32_t speed = 0; // 子格/tick（整数，保证确定性）
};

class World {
public:
	World(int32_t width, int32_t height, uint64_t seed)
		: m_width(width), m_height(height), m_rng(seed) {}

	// ---- 查询 ----
	int32_t Width() const { return m_width; }
	int32_t Height() const { return m_height; }
	Tick CurrentTick() const { return m_tick; }
	const std::vector<Unit>& Units() const { return m_units; }
	Rng& Random() { return m_rng; }

	Unit* FindUnit(uint32_t id) {
		for (auto& u : m_units) {
			if (u.id == id && u.alive) return &u;
		}
		return nullptr;
	}

	// ---- 命令处理（tick 边界调用）----
	void ApplyCommand(const Command& cmd);

	// ---- 模拟一步 ----
	void Advance(CommandQueue& queue);

	// ---- 确定性状态哈希（联机校验 / 回放校验）----
	uint64_t StateHash() const;

private:
	void MoveUnits();

	int32_t m_width = 0;
	int32_t m_height = 0;
	Tick m_tick = 0;
	Rng m_rng;
	std::vector<Unit> m_units;
	uint32_t m_nextUnitId = 1;
};

} // namespace rts
