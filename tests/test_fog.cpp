// RTS Engine — tests/test_fog.cpp
// M12 验收：战争迷雾（三态可见性 / 视野揭示 / 敌方隐藏 / 已探索保留）
#include "test_framework.h"
#include "sim/fog.h"
#include "sim/world.h"
#include "render/snapshot.h"

using namespace rts;

namespace {

World MakeWorld(int32_t w = 32) {
	Map map(w, w, 0, kWalkable | kBuildable);
	World world(map, 1);
	return world;
}

} // namespace

TEST(fog_initial_all_unexplored) {
	World world = MakeWorld();
	for (int32_t y = 0; y < world.Height(); ++y)
		for (int32_t x = 0; x < world.Width(); ++x)
			CHECK_EQ(static_cast<int>(world.GetMap().Fog(CellCoord(x, y))), 0);
}

TEST(fog_vision_reveals_cells) {
	World world = MakeWorld();
	const EntityId id = world.Entities().Create(0,
		SubCoord(16 * kSubPerCell, 16 * kSubPerCell),
		kCompMovable | kCompVision);
	Actor* a = world.Entities().Get(id);
	a->vision.range = 6 * kSubPerCell;

	fog::UpdateVisibility(world, 0);

	CHECK_EQ(static_cast<int>(world.GetMap().Fog(CellCoord(16, 16))), 2); // 中心可见
	CHECK_EQ(static_cast<int>(world.GetMap().Fog(CellCoord(18, 16))), 2); // 半径内
	CHECK_EQ(static_cast<int>(world.GetMap().Fog(CellCoord(31, 31))), 0); // 远处未探索
}

TEST(fog_explored_persists_after_leaving) {
	World world = MakeWorld();
	const EntityId id = world.Entities().Create(0,
		SubCoord(16 * kSubPerCell, 16 * kSubPerCell),
		kCompMovable | kCompVision);
	world.Entities().Get(id)->vision.range = 4 * kSubPerCell;

	fog::UpdateVisibility(world, 0);
	CHECK_EQ(static_cast<int>(world.GetMap().Fog(CellCoord(17, 17))), 2);

	// 单位离开
	world.Entities().Get(id)->pos = SubCoord(2 * kSubPerCell, 2 * kSubPerCell);
	fog::UpdateVisibility(world, 0);
	// 原区域从可见降级为已探索（保留）
	CHECK_EQ(static_cast<int>(world.GetMap().Fog(CellCoord(17, 17))), 1);
}

TEST(fog_hides_enemy_in_snapshot) {
	Map map(16, 16, 0, kWalkable | kBuildable);
	World world(map, 1);
	// 玩家 0 单位（视野 3 格）
	const EntityId own = world.Entities().Create(0,
		SubCoord(4 * kSubPerCell, 4 * kSubPerCell), kCompMovable | kCompVision);
	world.Entities().Get(own)->vision.range = 3 * kSubPerCell;
	// 敌方单位在远处（不可见）
	const EntityId enemyFar = world.Entities().Create(1,
		SubCoord(14 * kSubPerCell, 14 * kSubPerCell), kCompMovable);
	// 敌方单位在视野内
	const EntityId enemyNear = world.Entities().Create(1,
		SubCoord(5 * kSubPerCell, 5 * kSubPerCell), kCompMovable);

	fog::UpdateVisibility(world, 0);

	const RenderSnapshot snap = RenderSnapshot::FromWorld(world);
	bool sawFar = false, sawNear = false;
	for (const auto& s : snap.sprites) {
		if (s.entityId == enemyFar) sawFar = true;
		if (s.entityId == enemyNear) sawNear = true;
	}
	CHECK(!sawFar);  // 迷雾中的敌方不可见
	CHECK(sawNear);  // 视野内敌方可见
}

TEST(fog_building_vision) {
	World world = MakeWorld();
	// 建筑（视野 10 格）
	const EntityId b = world.Entities().Create(0,
		SubCoord(10 * kSubPerCell, 10 * kSubPerCell), kCompHealth | kCompBuilding);
	world.Entities().Get(b)->vision.range = 10 * kSubPerCell;

	fog::UpdateVisibility(world, 0);
	CHECK_EQ(static_cast<int>(world.GetMap().Fog(CellCoord(15, 10))), 2);
}

TEST(fog_state_in_hash) {
	World world = MakeWorld(8);
	const uint64_t h0 = world.StateHash();
	world.GetMap().SetFog(CellCoord(0, 0), 2);
	CHECK_NE(h0, world.StateHash());
}
