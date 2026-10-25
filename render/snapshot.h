// RTS Engine — render/snapshot.h
// 渲染快照：从 World 提取「只读的绘制数据」。
// 渲染层只消费快照，不直接访问模拟内部——保证逻辑/渲染解耦。
#pragma once

#include <cstdint>
#include <vector>
#include "sim/world.h"
#include "projection.h"
#include "assets.h"
#include "cnc_assets.h"

namespace rts {

struct RenderSprite {
	float worldX = 0.0f; // 世界像素（等距投影后）
	float worldY = 0.0f;
	float w = 0.0f;
	float h = 0.0f;
	uint32_t color = 0xFFFFFFFF;
	uint32_t entityId = 0; // 0 = 地形/装饰
	int spriteIndex = -1;  // AssetAtlas 图集索引；-1 = 纯色
	int cncSprite = -1;    // C&C 图集索引；-1 = 无
};

struct RenderSnapshot {
	std::vector<RenderSprite> sprites;
	int32_t mapW = 0;
	int32_t mapH = 0;

	// 从世界构建快照（地形格子 + 单位），应用战争迷雾
	static RenderSnapshot FromWorld(const World& world) {
		RenderSnapshot snap;
		snap.mapW = world.Width();
		snap.mapH = world.Height();

		const Map& map = world.GetMap();
		snap.sprites.reserve(static_cast<size_t>(map.Width()) * map.Height());
		for (int32_t y = 0; y < map.Height(); ++y) {
			for (int32_t x = 0; x < map.Width(); ++x) {
				const CellCoord c(x, y);
				const uint8_t fog = map.Fog(c); // 0=未探索 1=已探索 2=可见
				// 未探索：不绘制地形（纯黑）
				if (fog == 0) continue;

				const ScreenPos p = ProjectCell(c, map.TerrainHeight(c));
				RenderSprite s;
				s.worldX = static_cast<float>(p.x) - kTileW * 0.5f;
				s.worldY = static_cast<float>(p.y) - kTileH * 0.5f;
				s.w = static_cast<float>(kTileW);
				s.h = static_cast<float>(kTileH);
				// M16：地形映射到图集精灵
				SpriteId tid = SpriteId::Grass;
				if (map.Water(c)) tid = SpriteId::Water;
				else if (!map.Walkable(c)) tid = SpriteId::Rock;
				else if (map.TerrainHeight(c) > 0) tid = SpriteId::Highland;
				s.spriteIndex = static_cast<int>(tid);
				s.color = 0xFFFFFFFF; // 原色 tint
				// 已探索但不可见：变暗
				if (fog == 1) {
					const uint32_t a = s.color & 0xFF000000u;
					const uint32_t r = (s.color >> 16) & 0xFF;
					const uint32_t g = (s.color >> 8) & 0xFF;
					const uint32_t b = s.color & 0xFF;
					s.color = a | ((r * 2 / 5) << 16) | ((g * 2 / 5) << 8) | (b * 2 / 5);
				}
				snap.sprites.push_back(s);
			}
		}

		// 单位：己方始终可见；敌方仅在"可见"格显示
		for (const auto& a : world.Entities().All()) {
			if (!a.alive) continue;
			const CellCoord ac = a.pos.ToCell();
			const uint8_t fog = map.Fog(ac);
			if (a.playerId != 0 && fog != 2) continue; // 敌方在迷雾中隐藏
			const ScreenPos p = ProjectCell(ac, 0);
			RenderSprite s;
			s.color = (a.playerId == 0) ? 0xFF00C8FF : 0xFFFF4040;
			s.entityId = a.id;
			// M16：单位映射到 C&C 图集精灵
			CncSpriteId cid = CncSpriteId::Infantry;
			if (a.Has(kCompBuilding)) cid = CncSpriteId::Barracks;
			else if (a.Has(kCompHarvester)) cid = CncSpriteId::Harvester;
			else if (a.health.maxHp >= 200) cid = CncSpriteId::Tank;
			int sw = 32, sh = 32;
			CncAtlas::GetSize(cid, sw, sh);
			s.w = static_cast<float>(sw);
			s.h = static_cast<float>(sh);
			s.worldX = static_cast<float>(p.x) - s.w * 0.5f;
			s.worldY = static_cast<float>(p.y) - s.h * 0.5f;
			s.cncSprite = static_cast<int>(cid);
			snap.sprites.push_back(s);
		}
		return snap;
	}
};

} // namespace rts
