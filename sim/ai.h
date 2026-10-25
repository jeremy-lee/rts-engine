// RTS Engine — sim/ai.h
// 确定性 AI 玩家：只通过命令队列操作世界（与人类玩家同一套命令）。
// 决策间隔固定、随机数用独立种子 → 同输入同输出。
#pragma once

#include <cstdint>
#include "core/core.h"
#include "world.h"
#include "command_queue.h"

namespace rts {

class AIPlayer {
public:
	explicit AIPlayer(uint8_t playerId, uint64_t seed = 1);

	// 每个逻辑 tick 调用；内部按固定间隔决策
	void Update(World& world, CommandQueue& queue);

	uint8_t PlayerId() const { return m_playerId; }
	Tick LastDecisionTick() const { return m_lastDecision; }

private:
	// 找最近的资源格（返回 false 表示没有矿）
	bool FindNearestResource(const World& world, SubCoord from, CellCoord& out) const;
	// 找最近的敌方单位
	EntityId FindNearestEnemy(const World& world, SubCoord from) const;
	// 找自己的空闲兵营
	EntityId FindIdleBarracks(const World& world) const;
	// 找自己的建筑（基地）
	EntityId FindMainBuilding(const World& world) const;

	uint8_t m_playerId = 0;
	Rng m_rng;
	Tick m_lastDecision = -1;
	Tick m_lastAttack = -1;
};

// 固定决策间隔（tick）
inline constexpr Tick kAIDecisionInterval = 15;  // 0.5 秒
inline constexpr Tick kAIAttackInterval = 90;    // 3 秒

} // namespace rts
