// RTS Engine — sim/activity.cpp
#include "activity.h"
#include "world.h"

namespace rts {
namespace activity {

namespace {
// 由位移增量计算 8 方向朝向（整数运算，确定性）
// 0=北, 1=东北, 2=东, 3=东南, 4=南, 5=西南, 6=西, 7=西北（顺时针）
int FacingFromDelta(int32_t dx, int32_t dy) {
	if (dx == 0 && dy == 0) return -1;
	const int32_t adx = (dx < 0) ? -dx : dx;
	const int32_t ady = (dy < 0) ? -dy : dy;
	if (dy < 0 && adx * 2 <= ady) return 0;   // 北
	if (dx > 0 && dy < 0) return 1;           // 东北
	if (dx > 0 && ady * 2 <= adx) return 2;   // 东
	if (dx > 0 && dy > 0) return 3;           // 东南
	if (dy > 0 && adx * 2 <= ady) return 4;   // 南
	if (dx < 0 && dy > 0) return 5;           // 西南
	if (dx < 0 && ady * 2 <= adx) return 6;   // 西
	if (dx < 0 && dy < 0) return 7;           // 西北
	return -1;
}
} // namespace

void SetMoveTo(Actor& a, SubCoord target, Tick now) {
	a.activity.type = ActivityType::MoveTo;
	a.activity.started = now;
	a.activity.targetPos = target;
	a.activity.targetEntity = kInvalidEntity;
	a.movable.target = target;
	a.movable.moving = true;
}

void SetAttack(Actor& a, EntityId target, Tick now) {
	a.activity.type = ActivityType::Attack;
	a.activity.started = now;
	a.activity.targetEntity = target;
	a.movable.moving = false;
}

void SetIdle(Actor& a, Tick now) {
	a.activity.type = ActivityType::Idle;
	a.activity.started = now;
	a.activity.targetEntity = kInvalidEntity;
	a.movable.moving = false;
}

bool MoveTowards(World& world, Actor& actor, SubCoord target) {
	const int32_t dx = target.x - actor.pos.x;
	const int32_t dy = target.y - actor.pos.y;
	const int64_t distSq = DistanceSq(actor.pos, target);
	if (distSq == 0) return true;

	const int32_t speed = actor.movable.speed;
	const int64_t stepSq = static_cast<int64_t>(speed) * speed;
	if (distSq <= stepSq) {
		// 到达前检查目标格可通行（M11：含建筑占格）
		const CellCoord tc = target.ToCell();
		if (!world.GetMap().InBounds(tc) || !world.GetMap().Walkable(tc) ||
			world.GetMap().Occupied(tc)) {
			return false;
		}
		actor.pos = target;
		return true;
	}

	const int64_t d = static_cast<int64_t>(Isqrt(static_cast<uint64_t>(distSq)));
	if (d == 0) return true;
	const int64_t sx = (static_cast<int64_t>(dx) * speed) / d;
	const int64_t sy = (static_cast<int64_t>(dy) * speed) / d;

	SubCoord next(
		actor.pos.x + static_cast<int32_t>(sx != 0 ? sx : (dx > 0 ? 1 : -1)),
		actor.pos.y + static_cast<int32_t>(sy != 0 ? sy : (dy > 0 ? 1 : -1)));

	const CellCoord nc = next.ToCell();
	if (world.GetMap().InBounds(nc) && world.GetMap().Walkable(nc) &&
		!world.GetMap().Occupied(nc)) {
		// 更新朝向（移动方向）
		const int f = FacingFromDelta(next.x - actor.pos.x, next.y - actor.pos.y);
		if (f >= 0) actor.movable.facing = static_cast<uint8_t>(f);
		actor.pos = next;
		return false;
	}
	// 受阻：停止（M4 寻路会绕过障碍）
	return false;
}

namespace {

bool InAttackRange(const Actor& a, const Actor& target) {
	const int64_t distSq = DistanceSq(a.pos, target.pos);
	const int64_t range = a.armament.range;
	return distSq <= range * range;
}

void TryFire(World& world, Actor& a, Actor& target) {
	world.AddAttackFx(a.pos, target.pos); // 攻击特效（渲染层）
	if (world.CurrentTick() - a.armament.lastFire < a.armament.cooldown) return;
	a.armament.lastFire = world.CurrentTick();
	target.health.hp -= a.armament.damage;
	if (target.health.hp <= 0) {
		target.health.hp = 0;
		target.alive = false;
		target.activity.type = ActivityType::Idle;
	}
}

} // namespace

void Update(World& world, Actor& actor) {
	if (!actor.alive) return;

	// M19：防御塔自动攻击（建筑 + 武器；TryFire 内部处理冷却）
	if (actor.Has(kCompBuilding) && actor.armament.damage > 0) {
		Actor* best = nullptr;
		int64_t bestD2 = INT64_MAX;
		const int64_t rangeSq = static_cast<int64_t>(actor.armament.range) * actor.armament.range;
		for (auto& e : world.Entities().All()) {
			if (!e.alive || e.playerId == actor.playerId) continue;
			const int64_t d2 = DistanceSq(actor.pos, e.pos);
			if (d2 <= rangeSq && d2 < bestD2) {
				bestD2 = d2;
				best = &e;
			}
		}
		if (best != nullptr) TryFire(world, actor, *best);
		return; // 建筑不执行其他活动
	}

	switch (actor.activity.type) {
		case ActivityType::Idle:
			break;

		case ActivityType::MoveTo: {
			// M4：优先沿 A* 路径移动
			UnitPath* path = world.GetPath(actor.id);
			if (path != nullptr && path->index < path->cells.size()) {
				const CellCoord cell = path->cells[path->index];
				const SubCoord center = SubCoord::FromCell(cell) + SubCoord(kSubPerCell / 2, kSubPerCell / 2);
				if (MoveTowards(world, actor, center)) {
					++path->index;
					if (path->index >= path->cells.size()) {
						world.ClearPath(actor.id);
						SetIdle(actor, world.CurrentTick());
					}
				}
				break;
			}
			// 无路径：直线移动（fallback）
			const bool arrived = MoveTowards(world, actor, actor.activity.targetPos);
			if (arrived) {
				SetIdle(actor, world.CurrentTick());
			} else {
				const CellCoord tc = actor.activity.targetPos.ToCell();
				if (!world.GetMap().InBounds(tc) || !world.GetMap().Walkable(tc)) {
					SetIdle(actor, world.CurrentTick());
				}
			}
			break;
		}

		case ActivityType::Attack: {
			Actor* target = world.Entities().Get(actor.activity.targetEntity);
			if (target == nullptr || !target->alive) {
				SetIdle(actor, world.CurrentTick());
				break;
			}
			if (InAttackRange(actor, *target)) {
				TryFire(world, actor, *target);
			} else {
				// 接近目标
				const bool arrived = MoveTowards(world, actor, target->pos);
				if (arrived) {
					// 到达后下一 tick 会进入射程判定
				}
			}
			break;
		}

		case ActivityType::Harvest: {
			// 阶段驱动由 TickEconomy 负责；这里处理移动
			if (actor.harvester.phase == HarvestPhase::ToMine) {
				const SubCoord target = SubCoord::FromCell(actor.harvester.mine) +
					SubCoord(kSubPerCell / 2, kSubPerCell / 2);
				MoveTowards(world, actor, target);
			} else if (actor.harvester.phase == HarvestPhase::ToBase) {
				MoveTowards(world, actor, actor.harvester.basePos);
			}
			break;
		}

		case ActivityType::Guard:
			// M2 占位：与 Idle 相同
			break;
	}
}

} // namespace activity
} // namespace rts
