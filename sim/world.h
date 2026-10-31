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

// 死亡特效（位置 + 触发 tick，供渲染层做爆炸/淡出）
struct DeathFx {
	SubCoord pos;
	Tick tick = 0;
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

	// P1-5：OpenRA 的 ActorInfo 数据源。设置后 Spawn 会优先从 TraitInfo
	// （MobileInfo 速度、HealthInfo 血量、ArmamentInfo 武器）取值，
	// 取不到时回退到硬编码的 UnitDef。
	// P1-5：从 OpenRA ActorInfo 提取出来的运行时数值。
	// 刻意做成 POD 而不是让 sim 直接认识 TraitInfo——traits.h 依赖 economy.h/weapon.h，
	// 而这两个头又和 entity.h 重复定义 Harvester/Armament，sim 层 include 它会循环。
	struct ActorStats {
		int32_t hp = 0;
		int32_t speed = 0;      // subcell/tick
		int32_t fireDelay = 0;  // tick
	};

	// 按 unitNumericId 索引（与 Rules.unitIds 对齐）；由上层从 Ruleset 填好后传进来
	void SetActorStats(std::map<int32_t, ActorStats> stats) { m_actorStats = std::move(stats); }
	const ActorStats* FindActorStats(int32_t numericId) const {
		const auto it = m_actorStats.find(numericId);
		return it == m_actorStats.end() ? nullptr : &it->second;
	}

	// P2-7：按玩家视野重算战争迷雾。
	//   fog=2 可见    —— 落在任一己方 actor 的 Vision 半径内
	//   fog=1 已探索  —— 曾经可见过（保留）
	//   fog=0 未探索  —— 从未见过
	void RecomputeFog(uint8_t playerId);
	// sim 层不直接持有 Ruleset（会引入 traits.h 的循环依赖），只吃 ActorStats

	// ---- 经济（M5）----
	int32_t Credits(int player) const { return (player < 2) ? m_credits[player] : 0; }

	// ---- 科技升级（M19）----
	struct UpgradeState {
		int32_t attackBonus = 0;
		int32_t hpBonus = 0;
		int32_t speedBonus = 0;
	};
	const UpgradeState& Upgrades(int player) const { return m_upgrades[player < 2 ? player : 0]; }
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

	// ---- 存档（M21）----
	std::string SaveToText() const;
	bool LoadFromText(const std::string& text);

private:
	void TickActors();
	void TickEconomy();
	void ApplySeparation();

public:
	// 攻击特效
	void AddAttackFx(const SubCoord& from, const SubCoord& to) { m_attackFx.push_back(AttackFx{from, to}); }
	const std::vector<AttackFx>& AttackFxList() const { return m_attackFx; }
	void ClearAttackFx() { m_attackFx.clear(); }
	void AddDeathFx(const SubCoord& pos, Tick tick) { m_deathFx.push_back(DeathFx{pos, tick}); }
	const std::vector<DeathFx>& DeathFxList() const { return m_deathFx; }
	// 清理过期死亡特效（保留 60 tick，渲染只需 15）
	void PruneDeathFx(Tick now) {
		std::erase_if(m_deathFx, [now](const DeathFx& d) { return now - d.tick > 60; });
	}

private:
	std::vector<AttackFx> m_attackFx;
	std::vector<DeathFx> m_deathFx;
	UpgradeState m_upgrades[2];

	Map m_map;
	EntityStore m_entities;
	Rules m_rules;

	std::map<int32_t, ActorStats> m_actorStats;
	std::map<EntityId, UnitPath> m_paths;
	Pathfinder m_pathfinder;
	Tick m_tick = 0;
	Rng m_rng;
	int32_t m_credits[2] = {1000, 1000};
};

} // namespace rts
