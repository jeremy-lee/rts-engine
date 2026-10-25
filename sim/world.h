// RTS Engine — sim/world.h
// 世界：固定步长模拟的唯一状态容器。
// M2：EntityStore（组件式实体）+ 活动状态机 + 命令分发。
#pragma once

#include <cstdint>
#include "core/core.h"
#include "command.h"
#include "command_queue.h"
#include "map.h"
#include "entity.h"
#include "entity_store.h"

namespace rts {

class World {
public:
	World(int32_t width, int32_t height, uint64_t seed);
	World(Map map, uint64_t seed);

	// ---- 查询 ----
	int32_t Width() const { return m_map.Width(); }
	int32_t Height() const { return m_map.Height(); }
	Tick CurrentTick() const { return m_tick; }
	const Map& GetMap() const { return m_map; }
	Map& GetMap() { return m_map; }
	EntityStore& Entities() { return m_entities; }
	const EntityStore& Entities() const { return m_entities; }
	Rng& Random() { return m_rng; }

	// ---- 命令处理（tick 边界调用）----
	void ApplyCommand(const Command& cmd);

	// ---- 模拟一步 ----
	void Advance(CommandQueue& queue);

	// ---- 确定性状态哈希 ----
	uint64_t StateHash() const;

private:
	void TickActors();

	Map m_map;
	EntityStore m_entities;
	Tick m_tick = 0;
	Rng m_rng;
};

} // namespace rts
