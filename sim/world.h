// RTS Engine — sim/world.h
// 世界：固定步长模拟的唯一状态容器。
// M4：接入 A* 寻路（路径数据归 World 管理，确定性遍历用 std::map）。
#pragma once

#include <cstdint>
#include <map>
#include <vector>
#include "core/core.h"
#include "command.h"
#include "command_queue.h"
#include "map.h"
#include "entity.h"
#include "entity_store.h"
#include "pathfinder.h"
#include "gamedefs.h"

namespace rts {

// 攻击特效（渲染层消费，不属于模拟状态）
struct AttackFx {
	SubCoord from;
	SubCoord to;
};

// 单位路径：格子序列 + 当前索引
struct UnitPath {
	std::vector<CellCoord> cells;
	size_t index = 0;
};

class World {
public:
	World(int32_t width, int32_t height, uint64_t seed);
	World(Map map, uint64_t seed);
	World(Map map, uint64_t seed, Rules rules);

	// ---- 查询 ----
	int32_t Width() const { return m_map.Width(); }
	int32_t Height() const { return m_map.Height(); }
	Tick CurrentTick() const { return m_tick; }
	const Map& GetMap() const { return m_map; }
	Map& GetMap() { return m_map; }
	EntityStore& Entities() { return m_entities; }
	const EntityStore& Entities() const { return m_entities; }
	Rng& Random() { return m_rng; }
	const Rules& GetRules() const { return m_rules; }

	// ---- 经济（M5）----
	int32_t Credits(int player) const { return (player < 2) ? m_credits[player] : 0; }
	bool SpendCredits(int player, int32_t amount);
	void AddCredits(int player, int32_t amount);

	// ---- 路径管理 ----
	bool ComputePath(EntityId id, CellCoord target);
	UnitPath* GetPath(EntityId id);
	const UnitPath* GetPath(EntityId id) const;
	void ClearPath(EntityId id);

	// ---- 命令处理（tick 边界调用）----
	void ApplyCommand(const Command& cmd);

	// ---- 模拟一步 ----
	void Advance(CommandQueue& queue);

	// ---- 确定性状态哈希 ----
	uint64_t StateHash() const;

private:
	void TickActors();
	void TickEconomy();
	void ApplySeparation();

public:
	// 攻击特效
	void AddAttackFx(const SubCoord& from, const SubCoord& to) { m_attackFx.push_back(AttackFx{from, to}); }
	const std::vector<AttackFx>& AttackFxList() const { return m_attackFx; }
	void ClearAttackFx() { m_attackFx.clear(); }

private:
	std::vector<AttackFx> m_attackFx;

	Map m_map;
	EntityStore m_entities;
	Rules m_rules;
	std::map<EntityId, UnitPath> m_paths;
	Pathfinder m_pathfinder;
	Tick m_tick = 0;
	Rng m_rng;
	int32_t m_credits[2] = {1000, 1000};
};

} // namespace rts
