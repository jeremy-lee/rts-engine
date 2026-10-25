// RTS Engine — render/snapshot.h
// 渲染快照：从 World 提取「只读的绘制数据」。
// 渲染层只消费快照，不直接访问模拟内部——保证逻辑/渲染解耦。
#pragma once

#include <cstdint>
#include <vector>
#include "sim/world.h"
#include "projection.h"

namespace rts {

struct RenderSprite {
	float worldX = 0.0f; // 世界像素（等距投影后）
	float worldY = 0.0f;
	float w = 0.0f;
	float h = 0.0f;
	uint32_t color = 0xFFFFFFFF;
	uint32_t entityId = 0; // 0 = 地形/装饰
};

struct RenderSnapshot {
	std::vector<RenderSprite> sprites;
	int32_t mapW = 0;
	int32_t mapH = 0;

	// 从世界构建快照（地形格子 + 单位）
	static RenderSnapshot FromWorld(const World& world) {
		RenderSnapshot snap;
		snap.mapW = world.Width();
		snap.mapH = world.Height();

		const Map& map = world.GetMap();
		// 地形：每个格子一个菱形（用 64x32 矩形近似，M3 不追求美术）
		snap.sprites.reserve(static_cast<size_t>(map.Width()) * map.Height());
		for (int32_t y = 0; y < map.Height(); ++y) {
			for (int32_t x = 0; x < map.Width(); ++x) {
				const CellCoord c(x, y);
				if (!map.Walkable(c)) {
					// 障碍/水域不绘制地形层（或绘深色）
				}
				const ScreenPos p = ProjectCell(c, map.TerrainHeight(c));
				RenderSprite s;
				s.worldX = static_cast<float>(p.x) - kTileW * 0.5f;
				s.worldY = static_cast<float>(p.y) - kTileH * 0.5f;
				s.w = static_cast<float>(kTileW);
				s.h = static_cast<float>(kTileH);
				// 颜色：按地形
				if (map.Water(c)) {
					s.color = 0xFF1A3A6B; // 深蓝
				} else if (!map.Walkable(c)) {
					s.color = 0xFF555555; // 灰（障碍）
				} else if (map.TerrainHeight(c) > 0) {
					s.color = 0xFF4A7A3A; // 绿（高地）
				} else {
					s.color = 0xFF3A5A2A; // 草绿
				}
				snap.sprites.push_back(s);
			}
		}

		// 单位：小方块（16x16），按阵营着色
		for (const auto& a : world.Entities().All()) {
			if (!a.alive) continue;
			const ScreenPos p = ProjectCell(a.pos.ToCell(), 0);
			RenderSprite s;
			s.worldX = static_cast<float>(p.x) - 8.0f;
			s.worldY = static_cast<float>(p.y) - 8.0f;
			s.w = 16.0f;
			s.h = 16.0f;
			s.color = (a.playerId == 0) ? 0xFF00C8FF : 0xFFFF4040; // 蓝/红阵营
			s.entityId = a.id;
			snap.sprites.push_back(s);
		}
		return snap;
	}
};

} // namespace rts
