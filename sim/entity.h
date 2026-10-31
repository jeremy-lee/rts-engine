// RTS Engine — sim/entity.h
// 实体与组件（archetype-lite）：
//   Actor = 基础字段 + 组件位掩码 + 固定组件布局。
// 选型理由：RTS 单位类型少、组件差异大；固定布局 → 无动态分配、确定性遍历、
// 状态可直接哈希（联机校验/回放），比虚函数 Trait 更适合"确定性优先"。
#pragma once

#include <cstdint>
#include "core/core.h"
#include "core/wangle.h"

namespace rts {

using EntityId = uint32_t;
using EntityIndex = uint32_t;
inline constexpr EntityId kInvalidEntity = 0;

// 组件位掩码
enum ComponentFlags : uint8_t {
	kCompMovable = 1 << 0,
	kCompHealth = 1 << 1,
	kCompArmament = 1 << 2,
	kCompVision = 1 << 3,
	kCompHarvester = 1 << 4,   // 采矿单位
	kCompBuilding = 1 << 5,    // 建筑（可生产）
};

// 活动类型（行为状态机）
enum class ActivityType : uint8_t {
	Idle = 0,
	MoveTo = 1,
	Attack = 2,
	Guard = 3,
	Harvest = 4,
};

struct ActivityState {
	ActivityType type = ActivityType::Idle;
	Tick started = 0;
	EntityId targetEntity = kInvalidEntity;
	SubCoord targetPos;
};

struct Movable {
	SubCoord target;
	int32_t speed = 32;      // 子格/tick（对齐 OpenRA MobileInfo.Speed）
	int32_t turnSpeed = 512; // 每 tick 最大转角（对齐 OpenRA MobileInfo.TurnSpeed 默认值）
	bool moving = false;
	WAngle facing{0};     // 朝向（OpenRA WAngle：1024 单位 = 360°，8 方向 = 128 间隔）
	SubCoord prevPos;     // 上一 tick 起始位置（渲染插值用，不参与哈希）
	Tick lastMoveTick = 0; // 最近一次实际移动的 tick（动画滞后用，不参与哈希）
};

struct Health {
	int32_t hp = 100;
	int32_t maxHp = 100;
};

struct Armament {
	int32_t range = 8 * kSubPerCell; // 子格
	int32_t damage = 10;
	Tick cooldown = 15;              // tick
	Tick lastFire = 0;
};

struct Vision {
	int32_t range = 10 * kSubPerCell; // 子格
};

// 采矿者（M5）
enum class HarvestPhase : uint8_t { ToMine = 0, Mining = 1, ToBase = 2 };

struct Harvester {
	SubCoord basePos;      // 卸货点
	CellCoord mine;        // 采矿区
	int32_t cargo = 0;     // 当前载货
	int32_t capacity = 100;
	int32_t perTick = 10;  // 每 tick 采集量
	HarvestPhase phase = HarvestPhase::ToMine;
};

// 生产（建筑，M5）
struct Producer {
	int32_t unitNumericId = -1; // 生产中的单位
	Tick remaining = 0;
	Tick total = 0;
	int32_t queued = 0;         // 排队数量
};

struct Actor {
	EntityId id = kInvalidEntity;
	uint8_t playerId = 0;
	bool alive = true;
	uint8_t components = 0;
	uint8_t footprintW = 1; // 建筑占格宽
	uint8_t footprintH = 1; // 建筑占格高

	SubCoord pos;
	ActivityState activity;
	Movable movable;
	Health health;
	Armament armament;
	Vision vision;
	Harvester harvester;
	Producer producer;

	bool Has(uint8_t comp) const { return (components & comp) != 0; }
};

// ---- 组件哈希（确定性聚合）----
inline uint64_t HashMix(uint64_t h, uint64_t v) {
	h ^= v;
	h *= 1099511628211ull;
	return h;
}

inline uint64_t HashActor(const Actor& a) {
	uint64_t h = 1469598103934665603ull;
	h = HashMix(h, a.id);
	h = HashMix(h, a.playerId);
	h = HashMix(h, a.alive ? 1u : 0u);
	h = HashMix(h, a.components);
	h = HashMix(h, a.footprintW);
	h = HashMix(h, a.footprintH);
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.pos.x)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.pos.y)));
	h = HashMix(h, static_cast<uint64_t>(a.activity.type));
	h = HashMix(h, static_cast<uint64_t>(a.activity.started));
	h = HashMix(h, a.activity.targetEntity);
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.activity.targetPos.x)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.activity.targetPos.y)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.movable.target.x)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.movable.target.y)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.movable.speed)));
	h = HashMix(h, a.movable.moving ? 1u : 0u);
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.health.hp)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.health.maxHp)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.armament.range)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.armament.damage)));
	h = HashMix(h, static_cast<uint64_t>(a.armament.cooldown));
	h = HashMix(h, static_cast<uint64_t>(a.armament.lastFire));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.harvester.basePos.x)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.harvester.basePos.y)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.harvester.mine.x)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.harvester.mine.y)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.harvester.cargo)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.harvester.capacity)));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.harvester.perTick)));
	h = HashMix(h, static_cast<uint64_t>(a.harvester.phase));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.producer.unitNumericId)));
	h = HashMix(h, static_cast<uint64_t>(a.producer.remaining));
	h = HashMix(h, static_cast<uint64_t>(a.producer.total));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(a.producer.queued)));
	return h;
}

} // namespace rts
