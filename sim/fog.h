// RTS Engine — sim/fog.h
// 战争迷雾：三态可见性（0=未探索, 1=已探索, 2=可见）。
// M12 简化：单视角（玩家 0）。视野来源为该玩家的单位/建筑。
#pragma once

#include <cstdint>
#include "entity.h"

namespace rts {
class World;
namespace fog {

// 每 tick 更新视野：
//   1) 所有"可见"格降级为"已探索"
//   2) 根据 playerId 的单位/建筑视野半径，重新标记"可见"
void UpdateVisibility(World& world, uint8_t playerId = 0);

// 视野格半径（子格 → 格）
inline int32_t VisionCells(const Actor& a) {
	const int32_t range = (a.vision.range > 0) ? a.vision.range : (8 * kSubPerCell);
	return range / kSubPerCell;
}

} // namespace fog
} // namespace rts
