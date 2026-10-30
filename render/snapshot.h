// RTS Engine — render/snapshot.h
// 渲染快照：从 World 提取「只读的绘制数据」。
// 渲染层只消费快照，不直接访问模拟内部——保证逻辑/渲染解耦。
#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include "sim/world.h"
#include "projection.h"
#include "assets.h"
#include "cnc_assets.h"
#include "terrain_gfx.h"

namespace rts {

struct RenderSprite {
	float worldX = 0.0f; // 世界像素（等距投影后）
	float worldY = 0.0f;
	float prevX = 0.0f;  // 上一 tick 位置（渲染插值）
	float prevY = 0.0f;
	float w = 0.0f;
	float h = 0.0f;
	uint32_t color = 0xFFFFFFFF;
	uint32_t entityId = 0; // 0 = 地形/装饰
	int spriteIndex = -1;  // AssetAtlas 图集索引；-1 = 纯色
	int cncSprite = -1;    // C&C 图集索引；-1 = 无
	uint8_t player = 255;  // 所属玩家（255 = 无，用于软件后端着色）
	WAngle facing{};       // 朝向（OpenRA WAngle：1024 = 360°）
	uint8_t animFrame = 0; // 动画帧：0=stand，1-4=run
};

struct RenderEffect {
	float fromX = 0, fromY = 0; // 世界坐标
	float toX = 0, toY = 0;
	uint8_t kind = 0; // 0=tracer（曳光）, 1=impact（命中）, 2=death（爆炸）
	uint8_t age = 0;  // 特效老化（tick，用于淡出/扩散）
};

struct RenderSnapshot {
	std::vector<RenderSprite> sprites;
	std::vector<RenderEffect> effects; // 攻击线
	int32_t mapW = 0;
	int32_t mapH = 0;

	// 从世界构建快照（地形格子 + 单位），应用战争迷雾
	// ignoreFog = true 时全揭示（调试/截图用）
	static RenderSnapshot FromWorld(const World& world, bool ignoreFog = false) {
		RenderSnapshot snap;
		snap.mapW = world.Width();
		snap.mapH = world.Height();

		// OpenRA 地形排序：按屏幕 Y（等距深度 (x+y)）从远到近绘制（painter's algorithm）
		const Map& map = world.GetMap();
		snap.sprites.reserve(static_cast<size_t>(map.Width()) * map.Height());
		std::vector<CellCoord> tileOrder;
		tileOrder.reserve(static_cast<size_t>(map.Width()) * map.Height());
		for (int32_t y = 0; y < map.Height(); ++y)
			for (int32_t x = 0; x < map.Width(); ++x) tileOrder.push_back(CellCoord(x, y));
		std::sort(tileOrder.begin(), tileOrder.end(), [](const CellCoord& a, const CellCoord& b) {
			return (a.x + a.y) < (b.x + b.y);
		});
		for (const CellCoord& cell : tileOrder) {
			const int32_t x = cell.x;
			const int32_t y = cell.y;
				const CellCoord c(x, y);
				const uint8_t fog = map.Fog(c); // 0=未探索 1=已探索 2=可见
				// 未探索：不绘制地形（纯黑）
				if (fog == 0 && !ignoreFog) continue;

				const ScreenPos p = ProjectCell(c, map.TerrainHeight(c));
				RenderSprite s;
				TerrainKind tk = TerrainKind::Grass;
				if (map.Water(c)) tk = TerrainKind::Water;
				else if (!map.Walkable(c)) tk = TerrainKind::Rock;
				else if (map.TerrainHeight(c) > 0) tk = TerrainKind::Highland;
				// TMP 图集 tile 索引（按位置散列选变体）；渲染时用 GetUVAt(spriteIndex)
				s.spriteIndex = TerrainGfx::TileFor(tk, x, y);
				s.cncSprite = -1; // 地形不用 C&C 单位图集
				// 渲染尺寸 = 32×32（24×24 画布放大 4/3，菱形 32×16 = 格间距，边对边无缝）
				s.w = static_cast<float>(TerrainGfx::kRenderW);
				s.h = static_cast<float>(TerrainGfx::kRenderH);
				s.worldX = static_cast<float>(p.x) - s.w * 0.5f;
				s.worldY = static_cast<float>(p.y) - s.h * 0.5f;
				s.color = TerrainGfx::TintFor(tk);
				s.prevX = s.worldX; // 地形不插值
				s.prevY = s.worldY;
				// 已探索但不可见：变暗
				if (fog == 1 && !ignoreFog) {
					const uint32_t a = s.color & 0xFF000000u;
					const uint32_t r = (s.color >> 16) & 0xFF;
					const uint32_t g = (s.color >> 8) & 0xFF;
					const uint32_t b = s.color & 0xFF;
					s.color = a | ((r * 2 / 5) << 16) | ((g * 2 / 5) << 8) | (b * 2 / 5);
				}
				snap.sprites.push_back(s);
		}

		// 攻击特效（世界坐标，子格精度）：曳光 + 命中火花
		for (const auto& fx : world.AttackFxList()) {
			const ScreenPos f = ProjectSub(fx.from, 0);
			const ScreenPos t = ProjectSub(fx.to, 0);
			RenderEffect e;
			e.fromX = static_cast<float>(f.x);
			e.fromY = static_cast<float>(f.y);
			e.toX = static_cast<float>(t.x);
			e.toY = static_cast<float>(t.y);
			e.kind = 0;
			snap.effects.push_back(e);
			RenderEffect imp;
			imp.fromX = imp.toX = static_cast<float>(t.x);
			imp.fromY = imp.toY = static_cast<float>(t.y);
			imp.kind = 1;
			snap.effects.push_back(imp);
		}
		// 死亡爆炸（15 tick 内扩散淡出）
		for (const auto& d : world.DeathFxList()) {
			const Tick age = world.CurrentTick() - d.tick;
			if (age < 0 || age > 15) continue;
			const ScreenPos p = ProjectSub(d.pos, 0);
			RenderEffect e;
			e.fromX = e.toX = static_cast<float>(p.x);
			e.fromY = e.toY = static_cast<float>(p.y);
			e.kind = 2;
			e.age = static_cast<uint8_t>(age);
			snap.effects.push_back(e);
		}

		// 单位：己方始终可见；敌方仅在"可见"格显示（按世界 y 排序，远的先画）
		std::vector<RenderSprite> units;
		for (const auto& a : world.Entities().All()) {
			if (!a.alive) continue;
			const CellCoord ac = a.pos.ToCell();
			const uint8_t fog = map.Fog(ac);
			if (a.playerId != 0 && fog != 2 && !ignoreFog) continue; // 敌方在迷雾中隐藏
			const ScreenPos p = ProjectSub(a.pos, 0); // 子格精度（不平滑会跳格）
			RenderSprite s;
			// GL 模式：白色 tint（保留 C&C 原色）；建造中的建筑半透明
			s.color = (a.Has(kCompBuilding) && a.producer.remaining > 0) ? 0x90FFFFFFu : 0xFFFFFFFFu;
			s.player = a.playerId;
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
			// 插值起点（上一 tick 位置；未初始化时用当前）
			const SubCoord prev = (a.movable.prevPos.x == 0 && a.movable.prevPos.y == 0 &&
				(a.pos.x != 0 || a.pos.y != 0)) ? a.pos : a.movable.prevPos;
			const ScreenPos pp = ProjectSub(prev, 0); // 子格精度
			s.prevX = static_cast<float>(pp.x) - s.w * 0.5f;
			s.prevY = static_cast<float>(pp.y) - s.h * 0.5f;
			s.cncSprite = static_cast<int>(cid);
			// 朝向：单位取 Movable.facing，建筑固定 0
			s.facing = a.Has(kCompBuilding) ? WAngle(0) : a.movable.facing;
			// 动画帧：移动时循环 run 帧
			// 动画：战斗（Attack）或移动中保持 run 帧；静止 10 tick 后回 stand
			// （Attack 期间单位会反复停/走，若切 stand 会造成 stand/run 帧跳变 = 闪烁）
			const bool inCombat = (a.activity.type == ActivityType::Attack);
			const Tick sinceMove = world.CurrentTick() - a.movable.lastMoveTick;
			if (a.Has(kCompBuilding) || (!inCombat && !a.movable.moving && sinceMove > 10)) {
				s.animFrame = 0; // stand
			} else {
				s.animFrame = static_cast<uint8_t>(1 + (world.CurrentTick() / 3) % 6); // 10 FPS，6 帧循环
			}
			units.push_back(s);
		}
		std::sort(units.begin(), units.end(), [](const RenderSprite& a, const RenderSprite& b) {
			return a.worldY < b.worldY;
		});
		snap.sprites.insert(snap.sprites.end(), units.begin(), units.end());

		// 墓体/血渍不画（之前黑影/黑点太显眼，直接让死亡单位消失）
		return snap;
	}
};

} // namespace rts
