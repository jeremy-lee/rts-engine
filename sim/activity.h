// RTS Engine — sim/activity.h
// 活动系统：Actor 的行为状态机（协程式链式调度，M2 最小版）。
// 每个 tick 对所有存活 Actor 执行，状态转移必须确定性。
#pragma once

#include "entity.h"

namespace rts {

class World;

namespace activity {

// 设置活动（会重置活动起始 tick）
void SetMoveTo(Actor& a, SubCoord target, Tick now);
void SetAttack(Actor& a, EntityId target, Tick now);
void SetIdle(Actor& a, Tick now);

// 每 tick 驱动一个 Actor 的活动
void Update(World& world, Actor& actor);

// 移动辅助：向目标移动一步（受地形约束）。返回 true 表示已到达。
bool MoveTowards(World& world, Actor& actor, SubCoord target);

} // namespace activity

} // namespace rts
