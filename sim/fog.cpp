// RTS Engine — sim/fog.cpp
#include "fog.h"
#include "world.h"

namespace rts {
namespace fog {

void UpdateVisibility(World& world, uint8_t playerId) {
	Map& map = world.GetMap();
	const int32_t w = map.Width();
	const int32_t h = map.Height();

	// 1) 可见 → 已探索
	for (int32_t y = 0; y < h; ++y) {
		for (int32_t x = 0; x < w; ++x) {
			const CellCoord c(x, y);
			if (map.Fog(c) == 2) map.SetFog(c, 1);
		}
	}

	// 2) 根据玩家单位/建筑视野标记可见
	for (const auto& a : world.Entities().All()) {
		if (!a.alive || a.playerId != playerId) continue;
		const int32_t r = VisionCells(a);
		const CellCoord center = a.pos.ToCell();
		const int32_t r2 = r * r;
		for (int32_t dy = -r; dy <= r; ++dy) {
			for (int32_t dx = -r; dx <= r; ++dx) {
				if (dx * dx + dy * dy > r2) continue;
				const CellCoord c(center.x + dx, center.y + dy);
				if (!map.InBounds(c)) continue;
				if (map.Fog(c) != 2) map.SetFog(c, 2);
			}
		}
	}
}

} // namespace fog
} // namespace rts
